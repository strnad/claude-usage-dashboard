/*
 * Claude API client — HTTPS via esp_http_client + cert bundle.
 */

#include "app_claude_api.h"
#include "app_config.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_check.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_tls.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "claude_api";

#define USAGE_URL    "https://api.anthropic.com/api/oauth/usage"
#define REFRESH_URL  "https://console.anthropic.com/v1/oauth/token"
#define USER_AGENT   "claude-code/2.1.81"
#define BETA_HEADER  "oauth-2025-04-20"
#define OAUTH_CLIENT_ID "9d1c250a-e61b-44d9-88ed-5944d1962f5e"

#define HTTP_TIMEOUT_MS    15000
#define RESPONSE_BUF_SIZE  4096

static char s_resp_buf[RESPONSE_BUF_SIZE];
static int  s_resp_len = 0;

/* ------------------------------------------------------------------ */
/* HTTP event handler — collect body                                   */
/* ------------------------------------------------------------------ */

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (!esp_http_client_is_chunked_response(evt->client) && evt->data_len > 0) {
            int copy = evt->data_len;
            if (s_resp_len + copy >= RESPONSE_BUF_SIZE - 1) {
                copy = RESPONSE_BUF_SIZE - 1 - s_resp_len;
            }
            if (copy > 0) {
                memcpy(s_resp_buf + s_resp_len, evt->data, copy);
                s_resp_len += copy;
                s_resp_buf[s_resp_len] = '\0';
            }
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* ISO-8601 → unix ms (very loose: "2026-04-29T20:00:00.000Z")         */
/* ------------------------------------------------------------------ */

static int64_t parse_iso8601_ms(const char *s)
{
    if (!s) return 0;
    struct tm tm = {0};
    int year, mon, day, hour, min, sec;
    if (sscanf(s, "%d-%d-%dT%d:%d:%d", &year, &mon, &day, &hour, &min, &sec) != 6) {
        return 0;
    }
    tm.tm_year = year - 1900;
    tm.tm_mon  = mon - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min  = min;
    tm.tm_sec  = sec;
    /* newlib doesn't expose timegm — interpret as UTC via TZ override */
    char *old_tz = getenv("TZ");
    char saved[64] = {0};
    if (old_tz) { strncpy(saved, old_tz, sizeof(saved) - 1); }
    setenv("TZ", "UTC0", 1);
    tzset();
    time_t t = mktime(&tm);
    if (saved[0]) setenv("TZ", saved, 1); else unsetenv("TZ");
    tzset();
    if (t < 0) return 0;
    return (int64_t)t * 1000LL;
}

/* ------------------------------------------------------------------ */
/* OAuth token refresh                                                 */
/* ------------------------------------------------------------------ */

static esp_err_t refresh_oauth_tokens(uint8_t idx, app_account_t *acct)
{
    ESP_LOGI(TAG, "Refreshing OAuth tokens for account[%u]", idx);

    s_resp_len = 0;
    s_resp_buf[0] = '\0';

    esp_http_client_config_t cfg = {
        .url = REFRESH_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;

    /* Body: JSON */
    char body[1024];
    int blen = snprintf(body, sizeof(body),
        "{\"grant_type\":\"refresh_token\",\"refresh_token\":\"%s\",\"client_id\":\"%s\"}",
        acct->refresh, OAUTH_CLIENT_ID);
    if (blen <= 0 || blen >= (int)sizeof(body)) {
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "User-Agent", USER_AGENT);
    esp_http_client_set_header(client, "anthropic-beta", BETA_HEADER);
    esp_http_client_set_post_field(client, body, blen);

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Refresh transport failed: %s", esp_err_to_name(err));
        return err;
    }
    if (status != 200) {
        ESP_LOGE(TAG, "Refresh HTTP %d (body: %.200s)", status, s_resp_buf);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(s_resp_buf);
    if (!root) {
        ESP_LOGE(TAG, "Refresh JSON parse failed");
        return ESP_ERR_INVALID_RESPONSE;
    }
    cJSON *at = cJSON_GetObjectItemCaseSensitive(root, "access_token");
    cJSON *rt = cJSON_GetObjectItemCaseSensitive(root, "refresh_token");
    cJSON *ex = cJSON_GetObjectItemCaseSensitive(root, "expires_at");
    cJSON *ei = cJSON_GetObjectItemCaseSensitive(root, "expires_in");

    if (!cJSON_IsString(at) || !at->valuestring) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    int64_t expires_ms = 0;
    if (cJSON_IsNumber(ex)) {
        /* Anthropic returns expires_at as unix seconds (sometimes) or ms */
        double v = ex->valuedouble;
        expires_ms = (v > 1e12) ? (int64_t)v : (int64_t)(v * 1000.0);
    } else if (cJSON_IsNumber(ei)) {
        struct timeval tv; gettimeofday(&tv, NULL);
        int64_t now_ms = (int64_t)tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
        expires_ms = now_ms + (int64_t)(ei->valuedouble * 1000.0);
    }

    /* Update cache */
    strncpy(acct->token, at->valuestring, sizeof(acct->token) - 1);
    acct->token[sizeof(acct->token) - 1] = '\0';
    if (cJSON_IsString(rt) && rt->valuestring) {
        strncpy(acct->refresh, rt->valuestring, sizeof(acct->refresh) - 1);
        acct->refresh[sizeof(acct->refresh) - 1] = '\0';
    }
    acct->expires_ms = expires_ms;

    /* Persist to NVS */
    app_config_update_oauth_tokens(idx, acct->token, acct->refresh, expires_ms);

    cJSON_Delete(root);
    ESP_LOGI(TAG, "OAuth tokens refreshed (expires_at=%lld)", (long long)expires_ms);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Usage parser                                                        */
/* ------------------------------------------------------------------ */

static void parse_block(const cJSON *root, const char *key, float *util_out, int64_t *resets_out)
{
    if (util_out)  *util_out  = USAGE_VAL_MISSING;
    if (resets_out) *resets_out = 0;
    if (!root || !key) return;
    cJSON *blk = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsObject(blk)) return;
    cJSON *u = cJSON_GetObjectItemCaseSensitive(blk, "utilization");
    if (cJSON_IsNumber(u) && util_out) *util_out = (float)u->valuedouble;
    cJSON *r = cJSON_GetObjectItemCaseSensitive(blk, "resets_at");
    if (cJSON_IsString(r) && r->valuestring && resets_out) {
        *resets_out = parse_iso8601_ms(r->valuestring);
    }
}

static esp_err_t parse_usage_response(claude_usage_t *out)
{
    cJSON *root = cJSON_Parse(s_resp_buf);
    if (!root) return ESP_ERR_INVALID_RESPONSE;

    parse_block(root, "five_hour",   &out->five_h_util,   &out->five_h_resets_at_ms);
    parse_block(root, "seven_day",   &out->seven_d_util,  &out->seven_d_resets_at_ms);
    parse_block(root, "seven_day_opus",   &out->seven_d_opus_util,   NULL);
    parse_block(root, "seven_day_sonnet", &out->seven_d_sonnet_util, NULL);

    cJSON *extra = cJSON_GetObjectItemCaseSensitive(root, "extra_usage");
    if (cJSON_IsObject(extra)) {
        cJSON *en = cJSON_GetObjectItemCaseSensitive(extra, "is_enabled");
        out->extra_enabled = cJSON_IsTrue(en);
        cJSON *u  = cJSON_GetObjectItemCaseSensitive(extra, "utilization");
        if (cJSON_IsNumber(u))  out->extra_util          = (float)u->valuedouble;
        cJSON *uc = cJSON_GetObjectItemCaseSensitive(extra, "used_credits");
        if (cJSON_IsNumber(uc)) out->extra_used_credits  = (float)uc->valuedouble;
        cJSON *ml = cJSON_GetObjectItemCaseSensitive(extra, "monthly_limit");
        if (cJSON_IsNumber(ml)) out->extra_monthly_limit = (float)ml->valuedouble;
    }

    cJSON_Delete(root);
    out->valid = true;
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Public                                                              */
/* ------------------------------------------------------------------ */

esp_err_t app_claude_api_fetch(uint8_t account_idx, claude_usage_t *out)
{
    memset(out, 0, sizeof(*out));
    out->five_h_util          = USAGE_VAL_MISSING;
    out->seven_d_util         = USAGE_VAL_MISSING;
    out->seven_d_opus_util    = USAGE_VAL_MISSING;
    out->seven_d_sonnet_util  = USAGE_VAL_MISSING;

    app_account_t acct;
    if (!app_config_get_account(account_idx, &acct)) {
        snprintf(out->error_msg, sizeof(out->error_msg), "no account");
        return ESP_ERR_INVALID_ARG;
    }
    if (acct.token[0] == '\0') {
        snprintf(out->error_msg, sizeof(out->error_msg), "no token");
        return ESP_ERR_INVALID_STATE;
    }

    /* Refresh if OAuth and expiring within 60s */
    if (acct.type == ACCT_TYPE_OAUTH && acct.refresh[0] != '\0') {
        struct timeval tv; gettimeofday(&tv, NULL);
        int64_t now_ms = (int64_t)tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
        if (acct.expires_ms != 0 && acct.expires_ms < now_ms + 60000) {
            esp_err_t rerr = refresh_oauth_tokens(account_idx, &acct);
            if (rerr != ESP_OK) {
                snprintf(out->error_msg, sizeof(out->error_msg), "refresh failed");
                /* Continue with old token — server might still accept it */
            }
        }
    }

    /* Build Authorization header */
    char auth_hdr[2200];
    snprintf(auth_hdr, sizeof(auth_hdr), "Bearer %s", acct.token);

    s_resp_len = 0;
    s_resp_buf[0] = '\0';

    esp_http_client_config_t cfg = {
        .url = USAGE_URL,
        .method = HTTP_METHOD_GET,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        snprintf(out->error_msg, sizeof(out->error_msg), "http init");
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Authorization", auth_hdr);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "User-Agent", USER_AGENT);
    esp_http_client_set_header(client, "anthropic-beta", BETA_HEADER);

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Usage transport failed: %s", esp_err_to_name(err));
        snprintf(out->error_msg, sizeof(out->error_msg), "%s", esp_err_to_name(err));
        return err;
    }
    if (status == 401 && acct.type == ACCT_TYPE_OAUTH && acct.refresh[0] != '\0') {
        /* Try one forced refresh + retry */
        ESP_LOGW(TAG, "401 — forcing OAuth refresh and retrying once");
        esp_err_t rerr = refresh_oauth_tokens(account_idx, &acct);
        if (rerr == ESP_OK) {
            snprintf(auth_hdr, sizeof(auth_hdr), "Bearer %s", acct.token);
            s_resp_len = 0; s_resp_buf[0] = '\0';
            client = esp_http_client_init(&cfg);
            esp_http_client_set_header(client, "Authorization", auth_hdr);
            esp_http_client_set_header(client, "Content-Type", "application/json");
            esp_http_client_set_header(client, "User-Agent", USER_AGENT);
            esp_http_client_set_header(client, "anthropic-beta", BETA_HEADER);
            err = esp_http_client_perform(client);
            status = esp_http_client_get_status_code(client);
            esp_http_client_cleanup(client);
        }
    }

    if (status != 200) {
        ESP_LOGE(TAG, "Usage HTTP %d (body: %.200s)", status, s_resp_buf);
        snprintf(out->error_msg, sizeof(out->error_msg), "HTTP %d", status);
        return ESP_FAIL;
    }

    return parse_usage_response(out);
}
