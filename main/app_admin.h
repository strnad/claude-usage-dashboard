/*
 * Admin web server — runs in STA mode (after WiFi connection).
 *
 * Endpoints:
 *   GET  /                  — single-page admin UI (HTML)
 *   GET  /api/state         — JSON: accounts, settings, active idx
 *   POST /api/account/add   — body: label, type, token, refresh, expires_ms, email
 *   POST /api/account/del   — body: idx
 *   POST /api/account/active — body: idx
 *   POST /api/cycle         — body: enabled, interval
 *   POST /api/sleep         — body: enabled, start_h, end_h
 *   POST /api/factory_reset — wipes NVS and reboots
 *   POST /api/reboot        — reboots
 */

#pragma once

#include "esp_err.h"

esp_err_t app_admin_start(void);
void      app_admin_stop(void);
