// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "esp_err.h"

/* ── Capability bitmask ──────────────────────────────────────────────────
 * A driver's static capability set describes what fields it can *ever*
 * produce (fixed at probe time). A reading's `valid` mask (below) describes
 * which fields carry a real value in *this* sample. The two are distinct:
 * a BMP280 has the TEMPERATURE|PRESSURE capability but never HUMIDITY, while
 * a BME680 whose gas heater has not stabilised yet reports GAS_RESISTANCE as
 * a capability but leaves it out of `valid` until the reading is good. */
typedef enum {
    SENSOR_CAP_TEMPERATURE    = 1 << 0,
    SENSOR_CAP_HUMIDITY       = 1 << 1,
    SENSOR_CAP_PRESSURE       = 1 << 2,
    SENSOR_CAP_GAS_RESISTANCE = 1 << 3,
    SENSOR_CAP_CO2            = 1 << 4,  /* real ppm OR equivalent — see co2_is_equiv */
    SENSOR_CAP_IAQ            = 1 << 5,  /* implies static_iaq + iaq_accuracy present too */
    SENSOR_CAP_VOC_EQ         = 1 << 6,
} sensor_cap_t;

typedef uint16_t sensor_caps_t;  /* OR of sensor_cap_t values */

/* ── One reading covering every possible field ───────────────────────────
 * `valid` says which fields are populated this sample. Unset fields are NAN
 * (or 0 for iaq_accuracy) and must not be read. */
typedef struct {
    sensor_caps_t valid;
    float   temperature;    /* °C */
    float   humidity;       /* % relative humidity */
    float   pressure;       /* Pascal */
    float   gas_resistance; /* Ohm */
    float   co2;            /* ppm (real or equivalent) */
    bool    co2_is_equiv;   /* true = CO2-equivalent estimate, false = real CO2 */
    float   iaq;            /* Indoor Air Quality 0-500 */
    float   static_iaq;     /* Unscaled IAQ */
    float   voc_eq;         /* Breath-VOC equivalent ppm */
    uint8_t iaq_accuracy;   /* 0=unreliable 1=low 2=medium 3=high */
} sensor_reading_t;

/* ── Uniform driver interface ────────────────────────────────────────────
 * Every sensor driver exposes a singleton sensor_driver_t. Drivers keep their
 * own internal I2C sampling task and mutex-protected latch; read() is a
 * non-blocking copy of the latest latched sample mapped onto sensor_reading_t.
 * read() must set out->valid only for fields that carry a real value in this
 * sample (e.g. a BMP280 leaves HUMIDITY out of valid). */
typedef struct sensor_driver sensor_driver_t;
struct sensor_driver {
    const char   *name;
    sensor_caps_t caps;  /* static capability set, valid once the driver is created */
    esp_err_t   (*read)(sensor_driver_t *self, sensor_reading_t *out);
    void         *ctx;   /* driver-private state (optional) */
};