/*
 * Claude API HTTPS client.
 *
 * Two endpoints:
 *   GET  https://api.anthropic.com/api/oauth/usage
 *   POST https://console.anthropic.com/v1/oauth/token  (refresh)
 *
 * Auth modes:
 *   ACCT_TYPE_API_KEY — Authorization: Bearer sk-ant-api03-...
 *   ACCT_TYPE_OAUTH   — Authorization: Bearer <access_token>
 *                       refresh via /v1/oauth/token when expired
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "app_config.h"

#define USAGE_VAL_MISSING (-1.0f)

typedef struct {
    bool    valid;

    /* 5h limit */
    float   five_h_util;          /* 0-100; -1 if missing */
    int64_t five_h_resets_at_ms;  /* unix ms; 0 if missing */

    /* 7d limit (combined) */
    float   seven_d_util;
    int64_t seven_d_resets_at_ms;

    /* 7d Opus / Sonnet (optional) */
    float   seven_d_opus_util;
    float   seven_d_sonnet_util;

    /* Extra usage credits */
    bool    extra_enabled;
    float   extra_util;
    float   extra_used_credits;
    float   extra_monthly_limit;

    /* Last error message (for display when valid=false) */
    char    error_msg[64];
} claude_usage_t;

/* Fetch usage for the active account. Auto-refreshes OAuth tokens if expired.
 * On success, returns ESP_OK and fills *out. On failure, *out->valid=false. */
esp_err_t app_claude_api_fetch(uint8_t account_idx, claude_usage_t *out);
/* Fetch /api/oauth/profile — populates email + pretty tier (e.g. "Max 20x").
   Both buffers must be at least 64+16 chars. */
esp_err_t app_claude_api_fetch_profile(uint8_t account_idx, char *email_out, size_t email_len, char *tier_out, size_t tier_len);
