/*
 * Runtime Configuration Store (NVS) — implementation
 */

#include "app_config.h"

#include <stdio.h>
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_system.h"

static const char *TAG = "config";

/* Cached scalar values to avoid round-tripping NVS for every read */
static char     s_wifi_ssid[APP_SSID_MAX_LEN] = {0};
static char     s_wifi_pass[APP_PASS_MAX_LEN] = {0};
static uint8_t  s_acct_count   = 0;
static uint8_t  s_acct_active  = 0;
static bool     s_cycle_enabled  = true;
static uint16_t s_cycle_interval = 30;
static bool     s_sleep_enabled = true;
static uint8_t  s_sleep_start_h = 23;
static uint8_t  s_sleep_end_h   = 7;

/* ------------------------------------------------------------------ */
/* NVS helpers                                                         */
/* ------------------------------------------------------------------ */

static esp_err_t nvs_get_str_dyn(nvs_handle_t handle, const char *key, char *dst, size_t dst_size)
{
    size_t len = dst_size;
    esp_err_t err = nvs_get_str(handle, key, dst, &len);
    if (err != ESP_OK) {
        dst[0] = '\0';
    }
    return err;
}

static void account_key(char *out, size_t n, uint8_t idx, const char *suffix)
{
    snprintf(out, n, "a%u_%s", (unsigned)idx, suffix);
}

/* ------------------------------------------------------------------ */
/* Init / load                                                         */
/* ------------------------------------------------------------------ */

esp_err_t app_config_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase, doing it now");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return err;

    nvs_handle_t handle;
    err = nvs_open(APP_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved config (first boot)");
        return ESP_OK;
    }
    if (err != ESP_OK) return err;

    nvs_get_str_dyn(handle, "wifi_ssid", s_wifi_ssid, sizeof(s_wifi_ssid));
    nvs_get_str_dyn(handle, "wifi_pass", s_wifi_pass, sizeof(s_wifi_pass));

    nvs_get_u8(handle, "acct_count", &s_acct_count);
    nvs_get_u8(handle, "acct_active", &s_acct_active);

    uint8_t v8;
    if (nvs_get_u8(handle, "cycle_en", &v8) == ESP_OK) s_cycle_enabled = v8 != 0;
    nvs_get_u16(handle, "cycle_int", &s_cycle_interval);

    if (nvs_get_u8(handle, "sleep_en", &v8) == ESP_OK) s_sleep_enabled = v8 != 0;
    nvs_get_u8(handle, "sleep_sh", &s_sleep_start_h);
    nvs_get_u8(handle, "sleep_eh", &s_sleep_end_h);

    nvs_close(handle);

    if (s_acct_count > APP_MAX_ACCOUNTS) s_acct_count = APP_MAX_ACCOUNTS;
    if (s_acct_active >= s_acct_count && s_acct_count > 0) s_acct_active = 0;
    if (s_cycle_interval == 0) s_cycle_interval = 30;
    if (s_sleep_start_h > 23) s_sleep_start_h = 23;
    if (s_sleep_end_h > 23) s_sleep_end_h = 7;

    ESP_LOGI(TAG, "Loaded config: SSID='%s', accounts=%u, active=%u",
             s_wifi_ssid, s_acct_count, s_acct_active);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* WiFi                                                                */
/* ------------------------------------------------------------------ */

const char *app_config_get_wifi_ssid(void) { return s_wifi_ssid; }
const char *app_config_get_wifi_pass(void) { return s_wifi_pass; }
bool app_config_has_wifi(void) { return s_wifi_ssid[0] != '\0'; }

esp_err_t app_config_set_wifi(const char *ssid, const char *pass)
{
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(APP_NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "nvs_open");
    ESP_RETURN_ON_ERROR(nvs_set_str(handle, "wifi_ssid", ssid ? ssid : ""), TAG, "set ssid");
    ESP_RETURN_ON_ERROR(nvs_set_str(handle, "wifi_pass", pass ? pass : ""), TAG, "set pass");
    ESP_RETURN_ON_ERROR(nvs_commit(handle), TAG, "commit");
    nvs_close(handle);

    strncpy(s_wifi_ssid, ssid ? ssid : "", sizeof(s_wifi_ssid) - 1);
    s_wifi_ssid[sizeof(s_wifi_ssid) - 1] = '\0';
    strncpy(s_wifi_pass, pass ? pass : "", sizeof(s_wifi_pass) - 1);
    s_wifi_pass[sizeof(s_wifi_pass) - 1] = '\0';

    ESP_LOGI(TAG, "WiFi saved: SSID='%s'", s_wifi_ssid);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Account access                                                      */
/* ------------------------------------------------------------------ */

uint8_t app_config_get_account_count(void) { return s_acct_count; }

uint8_t app_config_get_active_index(void)
{
    if (s_acct_count == 0) return 0;
    if (s_acct_active >= s_acct_count) return 0;
    return s_acct_active;
}

esp_err_t app_config_set_active_index(uint8_t idx)
{
    if (idx >= s_acct_count) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(APP_NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "nvs_open");
    ESP_RETURN_ON_ERROR(nvs_set_u8(handle, "acct_active", idx), TAG, "set active");
    ESP_RETURN_ON_ERROR(nvs_commit(handle), TAG, "commit");
    nvs_close(handle);
    s_acct_active = idx;
    return ESP_OK;
}

bool app_config_get_account(uint8_t idx, app_account_t *out)
{
    if (idx >= s_acct_count || out == NULL) return false;
    memset(out, 0, sizeof(*out));

    nvs_handle_t handle;
    if (nvs_open(APP_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;

    char key[16];
    account_key(key, sizeof(key), idx, "lbl");  nvs_get_str_dyn(handle, key, out->label,   sizeof(out->label));
    account_key(key, sizeof(key), idx, "eml");  nvs_get_str_dyn(handle, key, out->email,   sizeof(out->email));
    account_key(key, sizeof(key), idx, "tok");  nvs_get_str_dyn(handle, key, out->token,   sizeof(out->token));
    account_key(key, sizeof(key), idx, "ref");  nvs_get_str_dyn(handle, key, out->refresh, sizeof(out->refresh));

    uint8_t typ = 0;
    account_key(key, sizeof(key), idx, "typ");  nvs_get_u8(handle, key, &typ);
    out->type = (typ == 1) ? ACCT_TYPE_OAUTH : ACCT_TYPE_API_KEY;

    int64_t exp = 0;
    account_key(key, sizeof(key), idx, "exp");  nvs_get_i64(handle, key, &exp);
    out->expires_ms = exp;

    nvs_close(handle);
    return true;
}

static esp_err_t write_account(uint8_t idx, const app_account_t *acct)
{
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(APP_NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "nvs_open");
    char key[16];

    account_key(key, sizeof(key), idx, "lbl");
    ESP_RETURN_ON_ERROR(nvs_set_str(handle, key, acct->label), TAG, "set label");
    account_key(key, sizeof(key), idx, "eml");
    ESP_RETURN_ON_ERROR(nvs_set_str(handle, key, acct->email), TAG, "set email");
    account_key(key, sizeof(key), idx, "tok");
    ESP_RETURN_ON_ERROR(nvs_set_str(handle, key, acct->token), TAG, "set token");
    account_key(key, sizeof(key), idx, "ref");
    ESP_RETURN_ON_ERROR(nvs_set_str(handle, key, acct->refresh), TAG, "set refresh");
    account_key(key, sizeof(key), idx, "typ");
    ESP_RETURN_ON_ERROR(nvs_set_u8(handle, key, (uint8_t)acct->type), TAG, "set type");
    account_key(key, sizeof(key), idx, "exp");
    ESP_RETURN_ON_ERROR(nvs_set_i64(handle, key, acct->expires_ms), TAG, "set exp");

    ESP_RETURN_ON_ERROR(nvs_commit(handle), TAG, "commit");
    nvs_close(handle);
    return ESP_OK;
}

esp_err_t app_config_add_account(const app_account_t *acct, uint8_t *out_idx)
{
    if (s_acct_count >= APP_MAX_ACCOUNTS) return ESP_ERR_NO_MEM;
    uint8_t idx = s_acct_count;

    ESP_RETURN_ON_ERROR(write_account(idx, acct), TAG, "write");

    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(APP_NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "nvs_open");
    s_acct_count++;
    ESP_RETURN_ON_ERROR(nvs_set_u8(handle, "acct_count", s_acct_count), TAG, "count");
    if (s_acct_count == 1) {
        s_acct_active = 0;
        nvs_set_u8(handle, "acct_active", 0);
    }
    ESP_RETURN_ON_ERROR(nvs_commit(handle), TAG, "commit");
    nvs_close(handle);

    if (out_idx) *out_idx = idx;
    ESP_LOGI(TAG, "Added account[%u] '%s' (type=%d)", idx, acct->label, (int)acct->type);
    return ESP_OK;
}

esp_err_t app_config_set_account(uint8_t idx, const app_account_t *acct)
{
    if (idx >= s_acct_count) return ESP_ERR_INVALID_ARG;
    return write_account(idx, acct);
}

esp_err_t app_config_delete_account(uint8_t idx)
{
    if (idx >= s_acct_count) return ESP_ERR_INVALID_ARG;

    /* Shift later slots down */
    for (uint8_t i = idx; i + 1 < s_acct_count; i++) {
        app_account_t a;
        if (app_config_get_account((uint8_t)(i + 1), &a)) {
            ESP_RETURN_ON_ERROR(write_account(i, &a), TAG, "shift");
        }
    }

    /* Clear the now-empty top slot */
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(APP_NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "nvs_open");
    char key[16];
    const char *suffixes[] = {"lbl", "eml", "tok", "ref", "typ", "exp"};
    uint8_t last = (uint8_t)(s_acct_count - 1);
    for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
        account_key(key, sizeof(key), last, suffixes[i]);
        nvs_erase_key(handle, key); /* ignore errors */
    }

    s_acct_count = (uint8_t)(s_acct_count - 1);
    nvs_set_u8(handle, "acct_count", s_acct_count);

    if (s_acct_active >= s_acct_count) {
        s_acct_active = (s_acct_count > 0) ? (uint8_t)(s_acct_count - 1) : 0;
        nvs_set_u8(handle, "acct_active", s_acct_active);
    }

    ESP_RETURN_ON_ERROR(nvs_commit(handle), TAG, "commit");
    nvs_close(handle);

    ESP_LOGI(TAG, "Deleted account[%u]; remaining=%u", idx, s_acct_count);
    return ESP_OK;
}

esp_err_t app_config_update_oauth_tokens(uint8_t idx,
                                         const char *access_token,
                                         const char *refresh_token,
                                         int64_t expires_ms)
{
    if (idx >= s_acct_count) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(APP_NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "nvs_open");
    char key[16];

    if (access_token) {
        account_key(key, sizeof(key), idx, "tok");
        ESP_RETURN_ON_ERROR(nvs_set_str(handle, key, access_token), TAG, "tok");
    }
    if (refresh_token) {
        account_key(key, sizeof(key), idx, "ref");
        ESP_RETURN_ON_ERROR(nvs_set_str(handle, key, refresh_token), TAG, "ref");
    }
    account_key(key, sizeof(key), idx, "exp");
    ESP_RETURN_ON_ERROR(nvs_set_i64(handle, key, expires_ms), TAG, "exp");
    ESP_RETURN_ON_ERROR(nvs_commit(handle), TAG, "commit");
    nvs_close(handle);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Auto-cycle / sleep                                                  */
/* ------------------------------------------------------------------ */

bool     app_config_get_cycle_enabled(void)   { return s_cycle_enabled; }
uint16_t app_config_get_cycle_interval(void)  { return s_cycle_interval; }

esp_err_t app_config_set_cycle(bool enabled, uint16_t seconds)
{
    if (seconds < 5)   seconds = 5;
    if (seconds > 600) seconds = 600;

    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(APP_NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "nvs_open");
    nvs_set_u8(handle, "cycle_en", enabled ? 1 : 0);
    nvs_set_u16(handle, "cycle_int", seconds);
    ESP_RETURN_ON_ERROR(nvs_commit(handle), TAG, "commit");
    nvs_close(handle);
    s_cycle_enabled  = enabled;
    s_cycle_interval = seconds;
    return ESP_OK;
}

bool    app_config_get_sleep_enabled(void) { return s_sleep_enabled; }
uint8_t app_config_get_sleep_start_h(void) { return s_sleep_start_h; }
uint8_t app_config_get_sleep_end_h(void)   { return s_sleep_end_h;   }

esp_err_t app_config_set_sleep(bool enabled, uint8_t start_h, uint8_t end_h)
{
    if (start_h > 23) start_h = 23;
    if (end_h > 23)   end_h = 23;

    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(APP_NVS_NAMESPACE, NVS_READWRITE, &handle), TAG, "nvs_open");
    nvs_set_u8(handle, "sleep_en", enabled ? 1 : 0);
    nvs_set_u8(handle, "sleep_sh", start_h);
    nvs_set_u8(handle, "sleep_eh", end_h);
    ESP_RETURN_ON_ERROR(nvs_commit(handle), TAG, "commit");
    nvs_close(handle);
    s_sleep_enabled = enabled;
    s_sleep_start_h = start_h;
    s_sleep_end_h   = end_h;
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Factory reset                                                       */
/* ------------------------------------------------------------------ */

void app_config_factory_reset(void)
{
    ESP_LOGW(TAG, "Factory reset — wiping NVS namespace and rebooting");
    nvs_handle_t handle;
    if (nvs_open(APP_NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
    }
    esp_restart();
}
