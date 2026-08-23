// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#pragma once
#include "esp_err.h"
#include <stdbool.h>

/* Call once from app_main after wifi_manager_init().
 * Reads broker URI and device token from NVS.  If either is empty the
 * component stays disabled and returns ESP_OK — safe to call unconditionally.
 * Connection is deferred until the first IP_EVENT_STA_GOT_IP event. */
esp_err_t tb_mqtt_init(void);

/* Re-read NVS credentials and (re)start the MQTT client immediately.
 * Safe to call at runtime after the user saves new settings via the UI.
 * Tears down any existing client first; starts the new one without waiting
 * for an IP event (assumes WiFi is already up).
 * A no-op if telemetry is disabled or credentials are missing. */
esp_err_t tb_mqtt_start(void);

/* Disconnect and stop publishing. Safe to call when not running. */
void tb_mqtt_stop(void);

/* True while the MQTT session with ThingsBoard is established. */
bool tb_mqtt_is_connected(void);

/* True if the last published message received a PUBACK from the broker.
 * Resets to false on each new publish attempt, then back to true on PUBACK.
 * Always false when not connected or nothing has been published yet. */
bool tb_mqtt_last_confirmed(void);