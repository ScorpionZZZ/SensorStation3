// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "sensor_hub.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "sensor_hub";

#define MAX_DRIVERS 4

static sensor_driver_t *s_drivers[MAX_DRIVERS];
static int              s_count;
static sensor_caps_t    s_caps;

esp_err_t sensor_hub_register(sensor_driver_t *drv)
{
    if (!drv || !drv->read) return ESP_ERR_INVALID_ARG;
    if (s_count >= MAX_DRIVERS) {
        ESP_LOGE(TAG, "driver table full (%d), cannot register %s",
                 MAX_DRIVERS, drv->name ? drv->name : "?");
        return ESP_ERR_NO_MEM;
    }
    s_drivers[s_count++] = drv;
    s_caps |= drv->caps;
    ESP_LOGI(TAG, "registered %s (caps=0x%02X)",
             drv->name ? drv->name : "?", drv->caps);
    return ESP_OK;
}

esp_err_t sensor_hub_init(void)
{
    ESP_LOGI(TAG, "%d driver(s) registered, aggregate caps=0x%02X",
             s_count, s_caps);
    return ESP_OK;
}

sensor_caps_t sensor_hub_caps(void)
{
    return s_caps;
}

/* Copy a scalar field from r into m if r has it and m doesn't yet
 * (first-registered driver wins). */
static inline void take(sensor_reading_t *m, const sensor_reading_t *r,
                        sensor_cap_t cap, float *dst, float src)
{
    if ((r->valid & cap) && !(m->valid & cap)) {
        *dst = src;
        m->valid |= cap;
    }
}

bool sensor_hub_get_current(sensor_reading_t *m)
{
    if (!m) return false;

    memset(m, 0, sizeof(*m));
    m->temperature = m->humidity = m->pressure = NAN;
    m->gas_resistance = m->co2 = m->iaq = m->static_iaq = m->voc_eq = NAN;

    bool any = false;
    for (int i = 0; i < s_count; i++) {
        sensor_driver_t *d = s_drivers[i];
        sensor_reading_t r;
        memset(&r, 0, sizeof(r));
        if (d->read(d, &r) != ESP_OK) continue;
        any = true;

        take(m, &r, SENSOR_CAP_TEMPERATURE,    &m->temperature,    r.temperature);
        take(m, &r, SENSOR_CAP_HUMIDITY,       &m->humidity,       r.humidity);
        take(m, &r, SENSOR_CAP_PRESSURE,       &m->pressure,       r.pressure);
        take(m, &r, SENSOR_CAP_GAS_RESISTANCE, &m->gas_resistance, r.gas_resistance);
        take(m, &r, SENSOR_CAP_VOC_EQ,         &m->voc_eq,         r.voc_eq);

        /* CO2: real always beats equivalent, otherwise first-registered wins. */
        if (r.valid & SENSOR_CAP_CO2) {
            bool takeit = !(m->valid & SENSOR_CAP_CO2) ||
                          (m->co2_is_equiv && !r.co2_is_equiv);
            if (takeit) {
                m->co2          = r.co2;
                m->co2_is_equiv = r.co2_is_equiv;
                m->valid       |= SENSOR_CAP_CO2;
            }
        }

        /* IAQ travels as a group with static_iaq + accuracy. */
        if ((r.valid & SENSOR_CAP_IAQ) && !(m->valid & SENSOR_CAP_IAQ)) {
            m->iaq          = r.iaq;
            m->static_iaq   = r.static_iaq;
            m->iaq_accuracy = r.iaq_accuracy;
            m->valid       |= SENSOR_CAP_IAQ;
        }
    }
    return any;
}