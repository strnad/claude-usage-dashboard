/*
 * Touch poll task — converts AXS5106 reads into tap / long-press events.
 *
 * NOTE: esp_lvgl_port already polls the touch handle for LVGL input. We poll
 * separately to derive *gesture* state, which doesn't conflict because both
 * reads are stateless.
 */

#include "app_touch.h"
#include "app_display.h"

#include "esp_log.h"
#include "esp_lcd_touch.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "touch";

#define POLL_PERIOD_MS         50
#define LONG_PRESS_MS          3000
#define DEBOUNCE_MS            30  /* min press duration to count as tap */

static app_touch_cb_t s_cb = NULL;
static void          *s_user = NULL;

static void touch_task(void *arg)
{
    esp_lcd_touch_handle_t tp = app_display_get_touch();
    if (!tp) {
        ESP_LOGE(TAG, "no touch handle");
        vTaskDelete(NULL);
        return;
    }

    bool was_pressed = false;
    bool fired = false;
    uint32_t held_ms = 0;

    while (1) {
        esp_lcd_touch_read_data(tp);
        uint16_t x[1], y[1]; uint8_t cnt = 0;
        bool now_pressed = esp_lcd_touch_get_coordinates(tp, x, y, NULL, &cnt, 1) && cnt > 0;

        if (now_pressed && !was_pressed) {
            /* Press start */
            held_ms = 0;
            fired = false;
        } else if (now_pressed && was_pressed) {
            held_ms += POLL_PERIOD_MS;
            if (!fired && held_ms >= LONG_PRESS_MS) {
                fired = true;
                if (s_cb) s_cb(APP_TOUCH_LONG_PRESS_FIRED, held_ms, s_user);
            } else if (!fired && held_ms >= 500) {
                /* progress callback — UI shows hold-to-reset hint */
                if (s_cb) s_cb(APP_TOUCH_LONG_PRESS_PROGRESS, held_ms, s_user);
            }
        } else if (!now_pressed && was_pressed) {
            /* Release */
            if (!fired) {
                if (held_ms >= 500 && held_ms < LONG_PRESS_MS) {
                    /* Overlay was shown (PROGRESS fired at 500ms) — must hide it */
                    if (s_cb) s_cb(APP_TOUCH_RELEASED_BEFORE_FIRE, held_ms, s_user);
                } else if (held_ms >= DEBOUNCE_MS && held_ms < 500) {
                    if (s_cb) s_cb(APP_TOUCH_TAP, held_ms, s_user);
                }
            }
            held_ms = 0;
            fired = false;
        }

        was_pressed = now_pressed;
        vTaskDelay(pdMS_TO_TICKS(POLL_PERIOD_MS));
    }
}

esp_err_t app_touch_start(app_touch_cb_t cb, void *user)
{
    s_cb = cb;
    s_user = user;
    if (xTaskCreate(touch_task, "touch_evt", 4096, NULL, 5, NULL) != pdPASS) {
        return ESP_FAIL;
    }
    return ESP_OK;
}
