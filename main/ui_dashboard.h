/*
 * Dashboard UI — 320x172 landscape, dark theme.
 *
 * Layout:
 *   [Header 24px]   account label · email                     wifi-icon
 *   [5h block 60px] "5h limit"   42%   reset za 2h 14m
 *                   ▓▓▓▓▓▓░░░░░░░░░░░░░░░░░░░░░░  (color-zoned)
 *   [7d block 60px] "7d limit"   18%   reset za 5d 4h
 *                   ▓▓▓▓░░░░░░░░░░░░░░░░░░░░░░░░
 *   [Footer 28px]   • • ●               12:34
 *
 * All functions must be called with app_display_lock() held.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "app_claude_api.h"

/* Build the dashboard UI. */
void ui_dashboard_init(void);

/* Update dashboard with fresh data + account info. */
void ui_dashboard_update(uint8_t account_idx, uint8_t total_accounts,
                        const char *label, const char *email, const char *tier,
                        const claude_usage_t *data, bool wifi_ok);

/* Show overlay: "Connecting WiFi..." */
void ui_dashboard_show_connecting(const char *ssid);

/* Show overlay: connection failed / error */
void ui_dashboard_show_error(const char *msg);

/* Hide all overlays — return to normal dashboard */
void ui_dashboard_hide_overlay(void);

/* Long-press progress overlay — secs_remaining counts down. */
void ui_dashboard_show_long_press(uint8_t secs_remaining);
void ui_dashboard_hide_long_press(void);

/* Set IP address shown in header (e.g. "192.168.1.123"). NULL or empty hides it. */
void ui_dashboard_set_ip(const char *ip);
