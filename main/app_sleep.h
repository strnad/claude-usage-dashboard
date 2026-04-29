/*
 * Sleep schedule — turns backlight off during configured night hours.
 *
 * Updated by the main app on each tick. Touch input wakes display.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Returns true if "now" falls within the sleep window. */
bool app_sleep_is_night_now(void);

/* Force display awake for the given minutes (touch override). */
void app_sleep_force_wake(uint16_t minutes);

/* Apply current state to backlight. Call periodically. */
void app_sleep_apply(void);
