// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#pragma once
#include "esp_err.h"

/* Registers a one-shot WiFi IP-event hook: on the first IP_EVENT_STA_GOT_IP
 * after boot, fetches the last 24h of temperature/humidity history from the
 * backend selected by the "hist_source" NVS setting (nvs_settings.h) and
 * seeds sensor_history's ring buffer via sensor_history_seed_24h(), so the
 * main-screen charts aren't empty right after boot. Must be called before
 * wifi_manager_connect(), same ordering requirement as tb_mqtt_init() and
 * friends — mirrors mqtt_base's own registration pattern.
 *
 * No-ops silently (never fails the boot) if hist_source is "none" or the
 * selected backend's settings are incomplete, and only ever fires once per
 * boot even across WiFi reconnects. Only NVS_SETTINGS_HIST_SOURCE_DOMOTICZ
 * is implemented so far. */
esp_err_t history_fetch_init(void);