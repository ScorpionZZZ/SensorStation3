// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef struct {
    int   adc_raw;        /* raw ADC count (0..4095) */
    int   voltage_mv;     /* calibrated voltage in millivolts */
} photores_data_t;

/* Initialize ADC and start 100 ms sampling task. */
esp_err_t    photores_init(void);
/* Thread-safe copy of the latest reading. */
esp_err_t    photores_read(photores_data_t *out);
/* Queue of length 1 carrying uint8_t backlight values (0–255).
 * Producer overwrites; consumer receives with zero timeout. */
QueueHandle_t photores_brightness_queue(void);