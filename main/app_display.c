/*
 * Display init — adapted from factory-base/main/main.c (rotation 90).
 */

#include "app_display.h"

#include "esp_log.h"
#include "esp_check.h"
#include "esp_lvgl_port.h"

#include "bsp_display.h"
#include "bsp_touch.h"
#include "bsp_i2c.h"
#include "bsp_spi.h"

static const char *TAG = "display";

#define DRAW_BUFF_HEIGHT 50
#define DRAW_BUFF_DOUBLE 1

static esp_lcd_panel_io_handle_t s_io_handle = NULL;
static esp_lcd_panel_handle_t    s_panel_handle = NULL;
static esp_lcd_touch_handle_t    s_touch_handle = NULL;
static lv_disp_t                *s_lvgl_disp = NULL;
static lv_indev_t               *s_lvgl_touch = NULL;

esp_err_t app_display_init(void)
{
    /* I2C bus + SPI bus (shared with battery monitor and touch) */
    i2c_master_bus_handle_t i2c_bus = bsp_i2c_init();
    bsp_spi_init();

    /* LCD panel */
    bsp_display_init(&s_io_handle, &s_panel_handle, APP_LCD_H_RES * DRAW_BUFF_HEIGHT);
    bsp_touch_init(&s_touch_handle, i2c_bus, APP_LCD_H_RES, APP_LCD_V_RES, 90);

    /* LVGL port */
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority    = 4,
        .task_stack       = 1024 * 10,
        .task_affinity    = -1,
        .task_max_sleep_ms = 500,
        .timer_period_ms  = 5,
    };
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "lvgl_port_init");

    /* Configure display rotation 90 — copied from factory-base */
    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle    = s_io_handle,
        .panel_handle = s_panel_handle,
        .buffer_size  = APP_LCD_H_RES * DRAW_BUFF_HEIGHT,
        .double_buffer = DRAW_BUFF_DOUBLE,
        .hres = APP_LCD_H_RES,
        .vres = APP_LCD_V_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy = true,
            .mirror_x = true,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
#if LVGL_VERSION_MAJOR >= 9
            .swap_bytes = true,
#endif
        },
    };
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel_handle, 0, 34));

    s_lvgl_disp = lvgl_port_add_disp(&disp_cfg);
    if (!s_lvgl_disp) return ESP_FAIL;

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = s_lvgl_disp,
        .handle = s_touch_handle,
    };
    s_lvgl_touch = lvgl_port_add_touch(&touch_cfg);

    /* Backlight */
    bsp_display_brightness_init();
    bsp_display_set_brightness(100);

    ESP_LOGI(TAG, "Display ready: %dx%d", APP_LCD_H_RES, APP_LCD_V_RES);
    return ESP_OK;
}

lv_disp_t *app_display_get_disp(void) { return s_lvgl_disp; }
esp_lcd_touch_handle_t app_display_get_touch(void) { return s_touch_handle; }
lv_indev_t *app_display_get_touch_indev(void) { return s_lvgl_touch; }

void app_display_set_brightness(uint8_t pct) { bsp_display_set_brightness(pct); }
uint8_t app_display_get_brightness(void) { return bsp_display_get_brightness(); }

bool app_display_lock(uint32_t timeout_ms) { return lvgl_port_lock(timeout_ms); }
void app_display_unlock(void) { lvgl_port_unlock(); }
