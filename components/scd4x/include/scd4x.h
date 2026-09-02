// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#pragma once

#include <math.h>
#include <stdbool.h>
#include "esp_err.h"
#include "sensor_types.h"

typedef struct {
    uint16_t co2;         /* ppm; 0 until first valid reading */
    float    temperature; /* degrees Celsius; NAN until first valid reading */
    float    humidity;    /* % relative humidity; NAN until first valid reading */
} scd4x_data_t;

/* Initialize sensor: adds own I2C device on the bmx280 bus (fixed address
 * 0x62), reads the serial number to confirm presence, starts periodic
 * measurement, and starts the sampling task. Returns ESP_ERR_NOT_FOUND if no
 * SCD4x answers on the bus. Works with both SCD40 and SCD41 — they share the
 * same I2C address and the command subset this driver uses (periodic
 * measurement, data-ready status, read measurement, serial number, wake-up);
 * this driver never issues SCD41-only commands (single-shot mode, etc.), and
 * doesn't distinguish the two variants. Call after bmx280_init() has
 * succeeded, since it owns the shared I2C bus handle. */
esp_err_t scd4x_init(void);

/* Thread-safe copy of the latest reading. Never blocks on I2C. */
esp_err_t scd4x_read(scd4x_data_t *out);

/* True once scd4x_init() has completed successfully. */
bool scd4x_is_active(void);

/* sensor_hub uniform-driver handle. Register with sensor_hub after a
 * successful scd4x_init(). Provides temperature, humidity and real CO2. */
sensor_driver_t *scd4x_driver(void);