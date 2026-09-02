// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#pragma once

#include "sensor_types.h"

/* Aggregation layer over the uniform sensor drivers. Merges every registered
 * driver into a single live snapshot and exposes the union of their static
 * capabilities. Merging is lazy — there is no hub task; sensor_hub_get_current()
 * reads each driver's non-blocking latch on the caller's thread. */

/* Register a driver before sensor_hub_init(). Registration order is the
 * source priority for fields that more than one sensor can supply (the first
 * driver to report a field wins) — with one exception: a real CO2 reading
 * always overrides a CO2-equivalent estimate regardless of order. Returns
 * ESP_ERR_NO_MEM if the internal table is full. */
esp_err_t sensor_hub_register(sensor_driver_t *drv);

/* Finalise registration. Cheap: just logs the driver set and aggregate caps.
 * Call after all sensors are probed and registered, before sensor_history. */
esp_err_t sensor_hub_init(void);

/* Union of every registered driver's static capabilities. The one place
 * consumers ask "is CO2/humidity/IAQ available?" — fixed for the session. */
sensor_caps_t sensor_hub_caps(void);
static inline bool sensor_hub_has(sensor_cap_t c) { return (sensor_hub_caps() & c) != 0; }

/* Merged live snapshot across all drivers. `out->valid` says which fields are
 * live this sample. Returns false (and leaves out->valid == 0) if no driver
 * produced any data. */
bool sensor_hub_get_current(sensor_reading_t *out);