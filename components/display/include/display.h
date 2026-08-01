// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#pragma once

#include "esp_err.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* Initialise the ILI9341 panel and LVGL, then start the LVGL timer task.
 * Call once from app_main before touching any lv_* API. */
esp_err_t display_init(void);

/* Set LCD backlight brightness (0 = off, 255 = full on). */
esp_err_t display_set_backlight(uint8_t brightness);

/* Return the last brightness value actually applied to the PWM (0–255). */
uint8_t display_get_backlight(void);

/* Attach a queue (length 1, uint8_t items) whose values are applied as
 * backlight brightness by the LVGL task on every iteration. */
void display_attach_brightness_queue(QueueHandle_t q);
