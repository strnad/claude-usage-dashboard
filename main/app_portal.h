/*
 * Captive Portal — runs in AP mode.
 * Hosts a single-page WiFi setup form on http://192.168.4.1/.
 * On submit, saves to NVS and reboots into STA mode.
 */

#pragma once

#include "esp_err.h"

esp_err_t app_portal_start(void);
void      app_portal_stop(void);
