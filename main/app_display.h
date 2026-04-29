/*
 * Display setup — JD9853 LCD + LVGL via esp_lvgl_port.
 */

#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"

/* 320x172 landscape (rotation 90) */
#define APP_LCD_H_RES 320
#define APP_LCD_V_RES 172

esp_err_t app_display_init(void);

/* LVGL handles for UI code */
lv_disp_t *app_display_get_disp(void);
esp_lcd_touch_handle_t app_display_get_touch(void);
lv_indev_t *app_display_get_touch_indev(void);

/* Backlight control (0-100). */
void app_display_set_brightness(uint8_t pct);
uint8_t app_display_get_brightness(void);

/* Lock/unlock LVGL for thread-safe UI updates. */
bool app_display_lock(uint32_t timeout_ms);
void app_display_unlock(void);
