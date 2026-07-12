// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

typedef struct {
    float   temperature;    /* heat-compensated °C; NAN until first valid reading */
    float   humidity;       /* heat-compensated % RH */
    float   pressure;       /* Pascal */
    float   iaq;            /* Indoor Air Quality 0-500 */
    float   static_iaq;     /* Unscaled IAQ (not normalised to environment) */
    float   co2_eq;         /* CO2-equivalent ppm */
    float   voc_eq;         /* Breath-VOC ppm */
    uint8_t iaq_accuracy;   /* 0=unreliable 1=low 2=medium 3=high */
    float   gas_resistance; /* Ohm; raw compensated gas resistance fed into BSEC. NAN until first valid reading */
} bsec_data_t;

/* Initialize BSEC: adds own I2C device on the bmx280 bus, reads BME680
 * calibration, loads BSEC config (33V/LP/4d), restores saved state from NVS,
 * subscribes to virtual outputs, suspends the bmx280 BME680 task, and starts
 * the BSEC measurement task.
 * Call after bmx280_init() once BMX280_TYPE_BME680 is confirmed. */
esp_err_t bsec_sensor_init(void);

/* Thread-safe copy of the latest BSEC output.
 * All float fields are NAN until the first bsec_do_steps() succeeds. */
esp_err_t bsec_sensor_read(bsec_data_t *out);

/* True once bsec_sensor_init() has completed successfully. Callers that key
 * off bmx280_get_type() == BMX280_TYPE_BME680 must also check this before
 * calling bsec_sensor_read() — init can fail (bad calibration, bsec_init()
 * error, etc.) leaving BSEC's internal state uninitialized. */
bool      bsec_sensor_is_active(void);

/* Current IAQ accuracy (0-3). Shorthand for bsec_sensor_read().iaq_accuracy. */
uint8_t   bsec_sensor_get_accuracy(void);

/* Apply a correction offset to the reported temperature (e.g. to compensate
 * for board self-heating).  Negative value lowers the reading.
 * Thread-safe; takes effect on the next measurement cycle. */
void bsec_sensor_set_temp_offset(float offset_celsius);
