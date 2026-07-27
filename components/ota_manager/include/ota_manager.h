// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#pragma once
#include "esp_err.h"

typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_VERIFYING,
    OTA_STATE_REBOOTING,
    OTA_STATE_FAILED,
} ota_state_t;

typedef enum {
    OTA_CHECK_IDLE,
    OTA_CHECK_CHECKING,
    OTA_CHECK_UP_TO_DATE,
    OTA_CHECK_AVAILABLE,
    OTA_CHECK_FAILED,
} ota_check_state_t;

/* Called from the OTA task — do NOT call LVGL APIs directly; use lv_async_call(). */
typedef void (*ota_progress_cb_t)(int percent, ota_state_t state,
                                  const char *msg, void *user_data);

/* Call once from app_main after all inits succeed to commit the running firmware. */
void      ota_manager_mark_valid(void);

esp_err_t ota_manager_init(void);

/* Begin OTA from url. Returns ESP_ERR_INVALID_STATE if already in progress. */
esp_err_t ota_manager_start(const char *url, ota_progress_cb_t cb, void *user_data);

ota_state_t ota_manager_get_state(void);

/* Start periodic update check (first fire at +60 s, then every 6 h).
 * check_url: URL of a JSON manifest file: {"version":"x.y.z"}
 * Derived from ota_url by replacing .bin -> .json. */
esp_err_t         ota_manager_check_start(const char *check_url, const char *running_version);
ota_check_state_t ota_manager_check_state(void);

/* Enable/disable automatic flashing when a newer version is detected.
 * When enabled, ota_manager_start() is called automatically with no UI callback. */
void ota_manager_set_auto(bool enabled);

/* Returns the server version string from the last successful check.
 * Empty string if no successful check has been completed yet. */
void ota_manager_get_server_version(char *out, size_t len);