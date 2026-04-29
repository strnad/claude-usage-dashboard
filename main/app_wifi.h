/*
 * WiFi STA + AP modes.
 *
 * - app_wifi_common_init() — netif + event loop (call once)
 * - app_wifi_sta_connect() — connect to SSID, blocks up to 30s
 * - app_wifi_ap_start()    — open AP "Claude-Dashboard-XXXX" on 192.168.4.1
 * - app_wifi_is_connected()
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t app_wifi_common_init(void);
esp_err_t app_wifi_sta_connect(const char *ssid, const char *password);
esp_err_t app_wifi_ap_start(char *out_ssid, size_t out_ssid_len);
bool      app_wifi_is_connected(void);

/* Returns SSID for AP mode (after app_wifi_ap_start). */
const char *app_wifi_get_ap_ssid(void);

/* Get RSSI of currently-connected AP. Returns 0 if not connected. */
int       app_wifi_get_rssi(void);
