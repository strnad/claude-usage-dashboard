/*
 * Claude Usage Dashboard — main entry point.
 *
 * Boot sequence:
 *   1. NVS init + load config
 *   2. Display init (LVGL)
 *   3. Touch task start
 *   4. WiFi common init
 *   5. If WiFi unconfigured → AP mode + captive portal + setup screen
 *      Otherwise → STA connect → mDNS + admin server + Claude poll
 *   6. SNTP sync
 *   7. Poll loop: every 30s fetch active account usage, update display
 */

#include "app_config.h"
#include "app_wifi.h"
#include "app_portal.h"
#include "app_admin.h"
#include "app_claude_api.h"
#include "app_display.h"
#include "app_touch.h"
#include "app_sleep.h"
#include "app_button.h"
#include "ui_dashboard.h"
#include "ui_setup.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_check.h"
#include "esp_system.h"
#include "esp_sntp.h"
#include "esp_netif.h"
#include "mdns.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

#define POLL_INTERVAL_MS         30000
#define INITIAL_FAIL_RETRY_MS    5000

/* Currently displayed account index (separate from "active" — auto-cycle/tap can
 * temporarily change view without rewriting NVS each time). */
static volatile uint8_t s_view_idx = 0;
static int64_t s_last_tier_fetch[8] = {0};  /* unix ms per account */

/* Timestamp of last user touch (ms). Auto-cycle pauses for 60s after a tap. */
static volatile int64_t s_last_tap_ms = 0;

/* If non-zero, force redraw on next poll iteration. */
static volatile bool s_force_refresh = false;

static int64_t now_ms(void)
{
    struct timeval tv; gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
}

/* ------------------------------------------------------------------ */
/* Touch event handler                                                 */
/* ------------------------------------------------------------------ */

static void on_touch(app_touch_event_t evt, uint32_t hold_ms, void *user)
{
    (void)user;
    switch (evt) {
    case APP_TOUCH_TAP: {
        ESP_LOGI(TAG, "tap");
        /* Wake from sleep if dimmed */
        app_sleep_force_wake(15);
        s_last_tap_ms = now_ms();
        uint8_t cnt = app_config_get_account_count();
        if (cnt == 0) return;
        s_view_idx = (uint8_t)((s_view_idx + 1) % cnt);
        s_force_refresh = true;
        break;
    }
    case APP_TOUCH_LONG_PRESS_PROGRESS: {
        uint8_t remaining = (uint8_t)((3000 - hold_ms + 999) / 1000);
        if (remaining < 1) remaining = 1;
        if (app_display_lock(100)) {
            ui_dashboard_show_long_press(remaining);
            app_display_unlock();
        }
        break;
    }
    case APP_TOUCH_RELEASED_BEFORE_FIRE:
        if (app_display_lock(100)) {
            ui_dashboard_hide_long_press();
            app_display_unlock();
        }
        break;
    case APP_TOUCH_LONG_PRESS_FIRED: {
        bool was = app_config_get_cycle_enabled();
        app_config_set_cycle(!was, app_config_get_cycle_interval());
        ESP_LOGW(TAG, "Long press fired — cycle %s", was ? "OFF" : "ON");
        if (app_display_lock(100)) {
            ui_dashboard_hide_long_press();
            ui_dashboard_show_error(was ? "Auto-cycle OFF" : "Auto-cycle ON");
            app_display_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(800));
        if (app_display_lock(100)) {
            ui_dashboard_hide_overlay();
            app_display_unlock();
        }
        break;
    }
    }
}


/* ------------------------------------------------------------------ */
/* BOOT button → factory reset                                         */
/* ------------------------------------------------------------------ */

static void on_button_long_press(void *user)
{
    (void)user;
    static volatile bool fired = false;
    if (fired) return;
    fired = true;
    ESP_LOGW(TAG, "BOOT long-press — factory reset");
    if (app_display_lock(100)) {
        ui_dashboard_show_error("Factory reset");
        app_display_unlock();
    }
    vTaskDelay(pdMS_TO_TICKS(700));
    app_config_factory_reset();
}

/* ------------------------------------------------------------------ */
/* SNTP                                                                */
/* ------------------------------------------------------------------ */

static void sntp_start(void)
{
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.cloudflare.com");
    esp_sntp_init();
    /* TZ: CET/CEST */
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
}

/* ------------------------------------------------------------------ */
/* mDNS                                                                */
/* ------------------------------------------------------------------ */

static void start_mdns(void)
{
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mdns_init failed: %s", esp_err_to_name(err));
        return;
    }
    mdns_hostname_set("claude-dashboard");
    mdns_instance_name_set("Claude Usage Dashboard");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mDNS: claude-dashboard.local");
}

/* ------------------------------------------------------------------ */
/* AP / setup mode                                                     */
/* ------------------------------------------------------------------ */

static void run_setup_mode(void)
{
    char ap_ssid[33] = {0};
    ESP_ERROR_CHECK(app_wifi_ap_start(ap_ssid, sizeof(ap_ssid)));
    ESP_ERROR_CHECK(app_portal_start());

    if (app_display_lock(0)) {
        ui_setup_show(ap_ssid);
        app_display_unlock();
    }

    /* Block forever — portal handles save + reboot */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ------------------------------------------------------------------ */
/* Poll loop                                                           */
/* ------------------------------------------------------------------ */

static void poll_loop(void)
{
    int64_t last_poll_ms = 0;
    int64_t last_cycle_ms = now_ms();
    uint8_t last_view = 0xFF;

    /* Cache last fetched data per account so we can swap accounts without re-fetching */
    static claude_usage_t s_cache[APP_MAX_ACCOUNTS];
    static int64_t s_cache_age_ms[APP_MAX_ACCOUNTS] = {0};

    while (1) {
        int64_t t = now_ms();

        uint8_t cnt = app_config_get_account_count();
        if (cnt == 0) {
            if (app_display_lock(0)) {
                char ip[20] = {0};
                char buf[128];
                if (app_wifi_get_ip_str(ip, sizeof(ip)) && ip[0])
                    snprintf(buf, sizeof(buf), "Open in browser:\nclaude-dashboard.local\n%s", ip);
                else
                    snprintf(buf, sizeof(buf), "Open in browser:\nclaude-dashboard.local");
                ui_dashboard_show_info("No accounts yet", buf);
                app_display_unlock();
            }
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        /* Auto-cycle: if no recent tap, advance every cycle_interval seconds */
        if (app_config_get_cycle_enabled() && cnt > 1) {
            int64_t since_tap = t - s_last_tap_ms;
            int64_t since_cycle = t - last_cycle_ms;
            int64_t period = (int64_t)app_config_get_cycle_interval() * 1000LL;
            if (since_tap > 60000 && since_cycle >= period) {
                s_view_idx = (uint8_t)((s_view_idx + 1) % cnt);
                last_cycle_ms = t;
                s_force_refresh = true;
            }
        }

        bool view_changed = (s_view_idx != last_view) || s_force_refresh;
        if (s_view_idx >= cnt) s_view_idx = 0;

        /* Fetch fresh data for the active view if we don't have cached data
         * younger than POLL_INTERVAL_MS, or if view changed. */
        bool need_fetch = (t - last_poll_ms) >= POLL_INTERVAL_MS;
        if (view_changed) {
            int64_t age = t - s_cache_age_ms[s_view_idx];
            if (age > POLL_INTERVAL_MS) need_fetch = true;
        }

        if (need_fetch) {
            uint8_t idx = s_view_idx;
            ESP_LOGI(TAG, "Fetching usage for account[%u]", idx);
            esp_err_t err = app_claude_api_fetch(idx, &s_cache[idx]);

            /* Auto-fetch tier: when empty, or every 3 hours to catch plan changes */
            if (err == ESP_OK && idx < 8) {
                app_account_t check;
                if (app_config_get_account(idx, &check)) {
                    struct timeval tv; gettimeofday(&tv, NULL);
                    int64_t now_ms = (int64_t)tv.tv_sec * 1000LL;
                    bool need = (check.tier[0] == 0) ||
                                (s_last_tier_fetch[idx] == 0) ||
                                (now_ms - s_last_tier_fetch[idx] > 3LL * 3600LL * 1000LL);
                    if (need) {
                        char fe[APP_EMAIL_MAX_LEN] = {0};
                        char ft[APP_TIER_MAX_LEN]  = {0};
                        if (app_claude_api_fetch_profile(idx, fe, sizeof(fe), ft, sizeof(ft)) == ESP_OK) {
                            bool changed = false;
                            if (check.email[0] == 0 && fe[0]) {
                                strncpy(check.email, fe, sizeof(check.email)-1);
                                changed = true;
                            }
                            if (ft[0] && strcmp(check.tier, ft) != 0) {
                                strncpy(check.tier, ft, sizeof(check.tier)-1);
                                changed = true;
                            }
                            if (changed) app_config_set_account(idx, &check);
                            s_last_tier_fetch[idx] = now_ms;
                        }
                    }
                }
            }
            s_cache_age_ms[idx] = t;
            last_poll_ms = t;
            if (err != ESP_OK && !s_cache[idx].error_msg[0]) {
                snprintf(s_cache[idx].error_msg, sizeof(s_cache[idx].error_msg),
                         "%s", esp_err_to_name(err));
            }
        }

        /* Render */
        app_account_t a;
        bool have = app_config_get_account(s_view_idx, &a);

        if (app_display_lock(100)) {
            ui_dashboard_hide_overlay();
            ui_dashboard_update(s_view_idx, cnt,
                                have ? a.label : "(?)", have ? a.email : "", have ? a.tier : "",
                                &s_cache[s_view_idx],
                                app_wifi_is_connected());
            app_display_unlock();
        }

        last_view = s_view_idx;
        s_force_refresh = false;

        /* Apply sleep schedule */
        app_sleep_apply();

        /* Wait — use shorter wake interval if force_refresh likely incoming */
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ------------------------------------------------------------------ */
/* app_main                                                            */
/* ------------------------------------------------------------------ */

void app_main(void)
{
    ESP_LOGI(TAG, "=== Claude Usage Dashboard ===");

    /* 1. Config / NVS */
    ESP_ERROR_CHECK(app_config_init());

    /* 2. Display */
    ESP_ERROR_CHECK(app_display_init());

    /* Build dashboard UI early; show splash */
    if (app_display_lock(0)) {
        ui_dashboard_init();
        ui_dashboard_show_connecting("starting...");
        app_display_unlock();
    }

    /* 3. Touch */
    ESP_ERROR_CHECK(app_touch_start(on_touch, NULL));
    ESP_ERROR_CHECK(app_button_start(on_button_long_press, NULL));

    /* 4. WiFi common */
    ESP_ERROR_CHECK(app_wifi_common_init());

    /* 5. WiFi configured? */
    if (!app_config_has_wifi()) {
        ESP_LOGI(TAG, "WiFi unconfigured → AP mode");
        run_setup_mode(); /* never returns */
    }

    /* 6. STA connect */
    if (app_display_lock(0)) {
        ui_dashboard_show_connecting(app_config_get_wifi_ssid());
        app_display_unlock();
    }

    esp_err_t err = app_wifi_sta_connect(app_config_get_wifi_ssid(),
                                          app_config_get_wifi_pass());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi failed: %s", esp_err_to_name(err));
        if (app_display_lock(0)) {
            ui_dashboard_show_error("WiFi failed.\nLong-press to reset.");
            app_display_unlock();
        }
        /* Stay alive so user can long-press to reset */
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    /* Show IP on dashboard header */
    {
        char ip_buf[20] = {0};
        if (app_wifi_get_ip_str(ip_buf, sizeof(ip_buf)) && app_display_lock(0)) {
            ui_dashboard_set_ip(ip_buf);
            app_display_unlock();
        }
    }

    /* 7. mDNS + admin server */
    start_mdns();
    ESP_ERROR_CHECK(app_admin_start());

    /* 8. SNTP */
    sntp_start();

    /* 9. Set initial view to active account */
    s_view_idx = app_config_get_active_index();

    /* 10. Poll loop */
    if (app_display_lock(0)) {
        ui_dashboard_hide_overlay();
        app_display_unlock();
    }

    poll_loop(); /* never returns */
}
