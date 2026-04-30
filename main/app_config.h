/*
 * Runtime Configuration Store (NVS)
 *
 * Stores WiFi credentials, list of Claude accounts (up to MAX_ACCOUNTS),
 * auto-cycle and sleep settings.
 *
 * Account data:
 *   - label   : human-readable (e.g. "Personal", "Work")
 *   - email   : optional email (for header display)
 *   - type    : ACCT_TYPE_API_KEY (sk-ant-...) or ACCT_TYPE_OAUTH
 *   - token   : API key OR OAuth access token
 *   - refresh : OAuth refresh token (empty for API key)
 *   - expires : OAuth access token expiry (unix ms, 0 for API key)
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define APP_NVS_NAMESPACE     "cdash"
#define APP_MAX_ACCOUNTS      8

#define APP_LABEL_MAX_LEN     32
#define APP_TIER_MAX_LEN      16
#define APP_EMAIL_MAX_LEN     64
#define APP_TOKEN_MAX_LEN     2048   /* OAuth access tokens can be long */
#define APP_REFRESH_MAX_LEN   512
#define APP_SSID_MAX_LEN      33
#define APP_PASS_MAX_LEN      65

typedef enum {
    ACCT_TYPE_API_KEY = 0,
    ACCT_TYPE_OAUTH   = 1,
} acct_type_t;

typedef struct {
    char        label[APP_LABEL_MAX_LEN];
    char        email[APP_EMAIL_MAX_LEN];
    char        tier[APP_TIER_MAX_LEN];  /* Pretty tier (e.g. "Max 20x") — auto-fetched */
    acct_type_t type;
    char        token[APP_TOKEN_MAX_LEN];
    char        refresh[APP_REFRESH_MAX_LEN];
    int64_t     expires_ms; /* unix ms, 0 = no expiry (API key) */
} app_account_t;

/* WiFi credentials */
const char *app_config_get_wifi_ssid(void);
const char *app_config_get_wifi_pass(void);
bool        app_config_has_wifi(void);
esp_err_t   app_config_set_wifi(const char *ssid, const char *pass);

/* Account management */
uint8_t     app_config_get_account_count(void);
uint8_t     app_config_get_active_index(void);
esp_err_t   app_config_set_active_index(uint8_t idx);

/* Returns true if loaded; false if idx out of range or missing */
bool        app_config_get_account(uint8_t idx, app_account_t *out);

/* Add account at next free slot. Returns its new index in *out_idx. */
esp_err_t   app_config_add_account(const app_account_t *acct, uint8_t *out_idx);

/* Replace account at idx */
esp_err_t   app_config_set_account(uint8_t idx, const app_account_t *acct);

/* Remove account, shift higher slots down */
esp_err_t   app_config_delete_account(uint8_t idx);

/* Update OAuth tokens after a refresh (keeps label/email/type) */
esp_err_t   app_config_update_oauth_tokens(uint8_t idx,
                                           const char *access_token,
                                           const char *refresh_token,
                                           int64_t expires_ms);

/* Auto-cycle config */
bool        app_config_get_cycle_enabled(void);
uint16_t    app_config_get_cycle_interval(void);
esp_err_t   app_config_set_cycle(bool enabled, uint16_t seconds);

/* Poll interval — seconds between Claude API fetches.
   Anthropic's public OAuth usage endpoint rate-limits aggressively
   (~4 req per 5 min, then 5+ min cooldown). Default 300s, min 60, max 3600. */
uint16_t    app_config_get_poll_interval(void);
esp_err_t   app_config_set_poll_interval(uint16_t seconds);

/* Sleep schedule */
bool        app_config_get_sleep_enabled(void);
uint8_t     app_config_get_sleep_start_h(void);
uint8_t     app_config_get_sleep_end_h(void);
esp_err_t   app_config_set_sleep(bool enabled, uint8_t start_h, uint8_t end_h);

/* Initialize NVS namespace and load cached values. */
esp_err_t   app_config_init(void);

/* Erase all config and reboot — does not return. */
void        app_config_factory_reset(void);
