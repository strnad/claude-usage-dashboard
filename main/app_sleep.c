/*
 * Sleep schedule — implementation.
 */

#include "app_sleep.h"
#include "app_config.h"
#include "app_display.h"

#include <time.h>
#include <sys/time.h>
#include "esp_log.h"

static const char *TAG = "sleep";

static int64_t s_force_wake_until_ms = 0;
static bool    s_currently_dim = false;

static int64_t now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
}

bool app_sleep_is_night_now(void)
{
    if (!app_config_get_sleep_enabled()) return false;
    time_t t = time(NULL);
    if (t < 1735689600) return false; /* before 2025 → no SNTP yet, skip */
    struct tm tm; localtime_r(&t, &tm);
    uint8_t h = (uint8_t)tm.tm_hour;
    uint8_t s = app_config_get_sleep_start_h();
    uint8_t e = app_config_get_sleep_end_h();
    if (s == e) return false;
    if (s < e) return (h >= s && h < e);
    /* Wraps midnight (e.g. 23 → 7) */
    return (h >= s || h < e);
}

void app_sleep_force_wake(uint16_t minutes)
{
    s_force_wake_until_ms = now_ms() + (int64_t)minutes * 60LL * 1000LL;
}

void app_sleep_apply(void)
{
    bool should_dim = app_sleep_is_night_now() && (now_ms() > s_force_wake_until_ms);
    if (should_dim != s_currently_dim) {
        s_currently_dim = should_dim;
        if (should_dim) {
            ESP_LOGI(TAG, "Night mode → backlight off");
            app_display_set_brightness(0);
        } else {
            ESP_LOGI(TAG, "Wake → backlight on");
            app_display_set_brightness(100);
        }
    }
}
