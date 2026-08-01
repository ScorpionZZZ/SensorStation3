// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#pragma once

#include "sensor_types.h"
#include <stddef.h>

/* Magnus-formula dew point (°C) from temperature (°C) and relative humidity
 * (%). Shared so every telemetry consumer computes it identically. */
float sensor_dew_point(float temp_c, float rh_pct);

/* Emit the sensor-value fields of `s` as a JSON fragment WITHOUT surrounding
 * braces and without a leading or trailing comma, e.g.
 *   "temperature":21.4,"humidity":48.0,"pressure":1013.2,"dew_point":10.1
 * Only fields flagged in s->valid are emitted. Field order is fixed and
 * key-for-key compatible with the previous per-sensor payloads:
 *   temperature, humidity, pressure (hPa), dew_point (when T+H present),
 *   gas_resistance, iaq, static_iaq, iaq_accuracy, co2 / co2_eq, voc_eq.
 * co2 is emitted as integer "co2" when real, or "co2_eq":%.1f when equivalent.
 * Returns bytes written (excluding NUL), or -1 on truncation. */
int sensor_json_fields(char *buf, size_t n, const sensor_reading_t *s);

/* Emit the shared diagnostic fields common to the TB and HA telemetry payloads
 * as a JSON fragment WITHOUT surrounding braces and without a leading or
 * trailing comma:
 *   "uptime":<s>,"rssi":<dBm>,"backlight":<pct>,"ldr_mv":<mV>
 * Values are gathered live at call time: uptime from esp_timer, rssi from the
 * STA AP record (0 when unavailable), backlight from display_get_backlight()
 * scaled to %, ldr_mv from the photoresistor. Field order and formatting match
 * the previous inline per-module payloads. Returns bytes written (excluding
 * NUL), or -1 on truncation. */
int mqtt_diag_fields(char *buf, size_t n);