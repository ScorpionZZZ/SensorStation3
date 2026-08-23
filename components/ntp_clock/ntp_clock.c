// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "ntp_clock.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_sntp.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

static const char *TAG = "ntp_clock";

static volatile bool s_synced = false;

/* ── SNTP callback ───────────────────────────────────────────────────── */

static void sync_cb(struct timeval *tv)
{
    (void)tv;
    s_synced = true;
    struct tm tm;
    time_t now = time(NULL);
    localtime_r(&now, &tm);
    ESP_LOGI(TAG, "synced: %04d-%02d-%02d %02d:%02d:%02d local",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
}

/* ── public API ──────────────────────────────────────────────────────── */

esp_err_t ntp_clock_init(const char *tz_posix)
{
    ntp_clock_set_tz(tz_posix);

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(sync_cb);
    esp_sntp_init();

    /* Re-sync every hour (default is CONFIG_LWIP_SNTP_UPDATE_DELAY, usually 1 h) */
    esp_sntp_set_sync_interval(3600000);

    ESP_LOGI(TAG, "SNTP started, tz=%s", tz_posix ? tz_posix : "UTC0");
    return ESP_OK;
}

bool ntp_clock_is_synced(void) { return s_synced; }

bool ntp_clock_get_local(struct tm *out)
{
    if (!out) return false;
    time_t now = time(NULL);
    localtime_r(&now, out);
    return s_synced;
}

void ntp_clock_set_tz(const char *tz_posix)
{
    setenv("TZ", tz_posix && tz_posix[0] ? tz_posix : "UTC0", 1);
    tzset();
}