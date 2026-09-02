// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#pragma once
#include "esp_err.h"
#include <stdbool.h>

/* Call once from app_main after wifi_manager_init().
 * Reads broker URI and enabled flag from NVS. A no-op returning ESP_OK if
 * disabled or the URI is empty — safe to call unconditionally.
 * Connection is deferred until the first IP_EVENT_STA_GOT_IP event. */
esp_err_t ha_mqtt_init(void);

/* Re-read NVS and (re)start the MQTT client immediately.
 * Call after the user saves Home Assistant settings via the UI. */
esp_err_t ha_mqtt_start(void);

/* Disconnect and stop publishing. Safe to call when not running. */
void ha_mqtt_stop(void);

/* True while the MQTT session with the Home Assistant broker is established. */
bool ha_mqtt_is_connected(void);