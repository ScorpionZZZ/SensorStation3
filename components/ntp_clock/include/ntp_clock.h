// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#pragma once
#include "esp_err.h"
#include <time.h>
#include <stdbool.h>

/* Call once from app_main after wifi_manager_init().
 * tz_posix: POSIX timezone string, e.g. "UTC0" or "CET-1CEST,M3.5.0,M10.5.0/3".
 * SNTP polling starts immediately and syncs automatically when IP is available. */
esp_err_t ntp_clock_init(const char *tz_posix);

/* True after the first successful NTP sync. */
bool ntp_clock_is_synced(void);

/* Fill *out with current local time. Returns false if not yet synced. */
bool ntp_clock_get_local(struct tm *out);

/* Apply a new POSIX TZ string at runtime (also call tzset internally). */
void ntp_clock_set_tz(const char *tz_posix);