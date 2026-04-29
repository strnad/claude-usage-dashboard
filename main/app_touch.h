/*
 * Touch event handling — drives next-account / long-press-reset behavior.
 *
 * Polls touch handle in a FreeRTOS task. Detects:
 *   - tap        → callback APP_TOUCH_TAP
 *   - long press → callback fires APP_TOUCH_LONG_PRESS_PROGRESS every 100ms
 *                  (with hold_ms), then APP_TOUCH_LONG_PRESS_FIRED at 3000ms.
 *
 * UI uses LVGL's lvgl_port_add_touch indirectly via app_display.c, so we
 * deliberately do NOT consume coordinates here — only edge transitions.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    APP_TOUCH_TAP,
    APP_TOUCH_LONG_PRESS_PROGRESS,  /* held but not yet 3s */
    APP_TOUCH_LONG_PRESS_FIRED,     /* >= 3s — issue factory reset */
    APP_TOUCH_RELEASED_BEFORE_FIRE, /* released before reaching 3s */
} app_touch_event_t;

typedef void (*app_touch_cb_t)(app_touch_event_t evt, uint32_t hold_ms, void *user);

esp_err_t app_touch_start(app_touch_cb_t cb, void *user);
