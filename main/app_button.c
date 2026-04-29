/*
 * BOOT button (GPIO9, active-low). 4 s long-press fires factory reset
 * with progress overlay so user sees the countdown.
 */
#include "app_button.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "iot_button.h"
#include "button_gpio.h"
#include "driver/gpio.h"

#include "app_display.h"
#include "ui_dashboard.h"

static const char *TAG = "button";
#define BOOT_BTN_GPIO  GPIO_NUM_9
#define LONG_PRESS_MS  4000

static app_button_long_cb_t s_cb = NULL;
static void                *s_user = NULL;
static int64_t              s_press_start_us = 0;

static void on_press_down(void *handle, void *arg)
{
    (void)handle; (void)arg;
    s_press_start_us = esp_timer_get_time();
    if (app_display_lock(50)) {
        ui_dashboard_show_long_press_msg("Hold for factory reset", LONG_PRESS_MS / 1000);
        app_display_unlock();
    }
}

static void on_press_up(void *handle, void *arg)
{
    (void)handle; (void)arg;
    if (app_display_lock(50)) {
        ui_dashboard_hide_long_press();
        app_display_unlock();
    }
    s_press_start_us = 0;
}

static void on_long_hold(void *handle, void *arg)
{
    (void)handle; (void)arg;
    if (s_press_start_us == 0) return;
    int64_t held_ms = (esp_timer_get_time() - s_press_start_us) / 1000;
    int remaining = (LONG_PRESS_MS - (int)held_ms + 999) / 1000;
    if (remaining < 1) remaining = 1;
    if (app_display_lock(20)) {
        ui_dashboard_show_long_press_msg("Hold for factory reset", (uint8_t)remaining);
        app_display_unlock();
    }
}

static void on_long_press(void *handle, void *arg)
{
    (void)handle; (void)arg;
    static volatile bool fired = false;
    if (fired) return;
    fired = true;
    ESP_LOGW(TAG, "BOOT long-press fired — factory reset");
    if (s_cb) s_cb(s_user);
}

esp_err_t app_button_start(app_button_long_cb_t cb, void *user)
{
    s_cb = cb;
    s_user = user;

    button_config_t btn_cfg = {
        .long_press_time = LONG_PRESS_MS,
        .short_press_time = 180,
    };
    button_gpio_config_t btn_gpio_cfg = {
        .gpio_num = BOOT_BTN_GPIO,
        .active_level = 0,
        .enable_power_save = false,
        .disable_pull = false,
    };
    button_handle_t btn = NULL;
    esp_err_t err = iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &btn);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "button create failed: %s", esp_err_to_name(err));
        return err;
    }
    iot_button_register_cb(btn, BUTTON_PRESS_DOWN,       NULL, on_press_down, NULL);
    iot_button_register_cb(btn, BUTTON_PRESS_UP,         NULL, on_press_up,   NULL);
    iot_button_register_cb(btn, BUTTON_LONG_PRESS_HOLD,  NULL, on_long_hold,  NULL);
    iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, NULL, on_long_press, NULL);
    ESP_LOGI(TAG, "BOOT button armed (GPIO%d, long_press=%dms)", BOOT_BTN_GPIO, LONG_PRESS_MS);
    return ESP_OK;
}
