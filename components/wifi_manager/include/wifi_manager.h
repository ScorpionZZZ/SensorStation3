// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#pragma once
#include "esp_err.h"
#include "esp_wifi_types.h"
#include <stdint.h>

typedef enum {
    WIFI_STATE_IDLE,        /* not started or no credentials stored */
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_FAILED,      /* last attempt failed, retrying */
} wifi_state_t;

/* Call once from app_main after nvs_settings_init().
 * Reads stored credentials and auto-connects if present. */
esp_err_t wifi_manager_init(void);

/* Connect to the given network and persist credentials to NVS.
 * Non-blocking — check wifi_manager_get_state() for result. */
esp_err_t wifi_manager_connect(const char *ssid, const char *pass);

/* Start an async AP scan (~2 s).  Call wifi_manager_scan_get() after
 * WIFI_STATE_CONNECTED or from a timer/poll to retrieve results. */
esp_err_t wifi_manager_scan_start(void);

/* Returns true once WIFI_EVENT_SCAN_DONE has fired since the last scan_start. */
bool wifi_manager_scan_ready(void);

/* Retrieve scan results.  *count in: capacity; *count out: actual entries.
 * Returns ESP_ERR_INVALID_STATE if scan not finished yet.
 * NOTE: esp_wifi clears the result buffer on the first call — call only once. */
esp_err_t wifi_manager_scan_get(wifi_ap_record_t *out, uint16_t *count);

wifi_state_t wifi_manager_get_state(void);

/* Fills out with dotted-decimal IP or "" if not connected. */
void wifi_manager_get_ip(char *out, size_t len);

/* -1 when not connected, 0..100 otherwise (maps rssi to percent). */
int  wifi_manager_get_rssi_pct(void);

/* Raw dBm value, or -100 when not connected. */
int  wifi_manager_get_rssi_dbm(void);