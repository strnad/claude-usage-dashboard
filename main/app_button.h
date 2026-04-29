/*
 * Hardware BOOT button (GPIO9) — long-press triggers factory reset.
 *
 * The display touch is reserved for everyday actions (tap = next account,
 * long press = toggle auto-cycle). Hardware button is the destructive path
 * to reduce accidental wipes from a clumsy palm on the screen.
 */
#pragma once
#include "esp_err.h"

typedef void (*app_button_long_cb_t)(void *user);

esp_err_t app_button_start(app_button_long_cb_t cb, void *user);
