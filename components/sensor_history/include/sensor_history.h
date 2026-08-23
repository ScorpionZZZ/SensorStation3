// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#pragma once
#include "esp_err.h"
#include "sensor_types.h"
#include <stdbool.h>

#define SENSOR_HISTORY_24H_SLOTS 144

typedef enum {
    TREND_UNKNOWN,   /* not enough data yet */
    TREND_UP,
    TREND_STABLE,
    TREND_DOWN,
} sensor_trend_t;

/* One 10-minute 24 h history slot. Only temperature and humidity are charted,
 * so pressure/CO2 are intentionally not retained here (they get the live
 * rolling average via sensor_history_get_snapshot() instead). */
typedef struct {
    float temp;
    float humidity;
    bool  valid;     /* false until the first 10-min slot completes */
} history_slot_t;

/* Start the background sampling task (500 ms period). Reads its data from
 * sensor_hub, so at least one driver must be registered first. */
esp_err_t sensor_history_init(void);

/* Combined "numbers" snapshot for display and MQTT:
 *   - temperature / humidity / pressure / co2 are the rolling-average values
 *     from the loop buffers (60 × 500 ms for T/H/P, 15 × 2 s for CO2);
 *   - gas_resistance / iaq / static_iaq / voc_eq and co2_is_equiv are taken
 *     live from sensor_hub (fields without a loop buffer).
 * out->valid marks which fields are present. Returns false only when neither
 * the history nor the hub has produced any data yet. */
bool sensor_history_get_snapshot(sensor_reading_t *out);

/* Trend of the momentary reading vs. its own rolling 30-second mean,
 * recomputed every 500 ms sampling tick. Returns TREND_UNKNOWN until the
 * 30 s buffer has filled (first 30 s after boot). */
sensor_trend_t sensor_history_get_temp_trend(void);
sensor_trend_t sensor_history_get_humidity_trend(void);

/* Delta (momentary sample minus its rolling 30 s mean), rounded to one
 * decimal place — this rounded value is exactly what decides the trend
 * (> 0 up, < 0 down, == 0 stable). Returns 0.0 if TREND_UNKNOWN. */
float sensor_history_get_temp_delta(void);
float sensor_history_get_hum_delta(void);

/* Copy the 24-hour history into out[] in chronological order (oldest first).
 * Returns the number of valid slots written (0..144). */
int sensor_history_get_24h(history_slot_t *out, int max_slots);