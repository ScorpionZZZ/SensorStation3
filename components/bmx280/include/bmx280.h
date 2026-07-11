// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#pragma once

#include <math.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

typedef enum {
    BMX280_TYPE_NONE   = 0,
    BMX280_TYPE_BMP280 = 1,
    BMX280_TYPE_BME280 = 2,
    BMX280_TYPE_BME680 = 3,
} bmx280_type_t;

typedef struct {
    float temperature;    /* degrees Celsius */
    float pressure;       /* Pascal */
    float humidity;       /* % relative humidity; NAN for BMP280 */
    float gas_resistance; /* Ohm; NAN unless BME680 with valid gas measurement */
} bmx280_data_t;

/* Initialize sensor: auto-detect I2C address and chip type, start sampling task. */
esp_err_t     bmx280_init(void);
bmx280_type_t bmx280_get_type(void);
/* Thread-safe copy of the latest reading. Never blocks on I2C. */
esp_err_t     bmx280_read(bmx280_data_t *out);

/* Set temperature correction offset applied after BME680 compensation.
 * Negative value lowers the reading (typical for self-heating correction).
 * Call after bmx280_init() when BMX280_TYPE_BME680 is detected. */
void bmx280_set_temp_offset(float offset_celsius);

/* 30-second rolling average of CO2eq (one sample per ~5 s gas heater cycle).
 * Returns ESP_ERR_INVALID_STATE while the buffer is still filling at boot. */
esp_err_t bmx280_co2eq_avg(float *out);

/* Overwrite the latest reading under mutex.  Used by the bsec component to push
 * heat-compensated T/H/P so that bmx280_read() and sensor_history stay live
 * while the bmx280 sampling task is suspended. */
void bmx280_update_latest(const bmx280_data_t *d);

/* Return the I2C bus handle — used by the bsec component to add its own device.
 * Valid after bmx280_init() returns ESP_OK. */
i2c_master_bus_handle_t bmx280_get_i2c_bus_handle(void);

/* Return the I2C address (0x76 or 0x77) at which the sensor was detected. */
uint16_t bmx280_get_addr(void);

/* Suspend the BME680 sampling task so the bsec component can take over
 * sensor control.  No-op if sensor is not BME680 or task not running. */
void bmx280_stop_bme680_task(void);

/* Empirical CO2-equivalent estimate from BME680 gas resistance.
 * The BME680 is a VOC sensor, not a CO2 sensor — this is an approximation.
 * Reference: ~75 kOhm at 40% RH ≈ 400 ppm CO2eq (clean outdoor air, 3% duty).
 * Returns ppm in the range 400–60000. */
float bmx280_gas_to_co2eq(float gas_ohm, float humidity_pct);