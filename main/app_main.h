/*
 * Read-only accessors into the main poll loop's cached state.
 * Lets other components (admin server) report what's currently displayed
 * without poking the wire themselves.
 */

#pragma once

#include <stdint.h>
#include "app_claude_api.h"

/* Pointer to the per-account usage cache slot (NULL if idx out of range).
   Pointer is stable; contents may change between polls. */
const claude_usage_t *app_main_get_cached_usage(uint8_t idx);

/* Unix-ms timestamp the slot was last refreshed (0 if never). */
int64_t app_main_get_cache_age_ms(uint8_t idx);
