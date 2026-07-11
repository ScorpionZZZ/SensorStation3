// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#pragma once
#include "esp_err.h"
#include <stdbool.h>

#define SENSOR_HISTORY_24H_SLOTS 144

typedef enum {
    TREND_UNKNOWN,   /* not enough data yet */
    TREND_UP,
    TREND_STABLE,
    TREND_DOWN,
} sensor_trend_t;

typedef struct {
    float temp;
    float humidity;
    float pressure;
    bool  valid;     /* false until the first 10-min slot completes */
} history_slot_t;

/* Start the background sampling task (500 ms period). */
esp_err_t sensor_history_init(void);

/* Rolling 30-second average (60 × 500 ms samples).
 * Returns false if fewer than one sample has arrived yet. */
bool sensor_history_get_current(float *temp, float *humidity, float *pressure);

/* Trend vs the 30-second window from 30 s ago.
 * Returns TREND_UNKNOWN until the first 30 s reference is established. */
sensor_trend_t sensor_history_get_temp_trend(void);
sensor_trend_t sensor_history_get_humidity_trend(void);

/* Delta (current 30 s mean minus the previous reference mean).
 * Returns 0.0 if TREND_UNKNOWN. */
float sensor_history_get_temp_delta(void);
float sensor_history_get_hum_delta(void);

/* Copy the 24-hour history into out[] in chronological order (oldest first).
 * Returns the number of valid slots written (0..144). */
int sensor_history_get_24h(history_slot_t *out, int max_slots);