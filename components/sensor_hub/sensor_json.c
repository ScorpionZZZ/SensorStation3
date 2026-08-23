// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "sensor_json.h"
#include "photores.h"
#include "display.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include <math.h>
#include <stdio.h>
#include <stdint.h>

float sensor_dew_point(float temp_c, float rh_pct)
{
    const float a = 17.625f, b = 243.04f;
    float alpha = logf(rh_pct / 100.0f) + a * temp_c / (b + temp_c);
    return b * alpha / (a - alpha);
}

/* Append formatted text at *off, keeping the running offset and flagging any
 * truncation so the caller can bail out with -1. */
#define APPEND(...)                                                      \
    do {                                                                 \
        int _w = snprintf(buf + off, (off < n) ? (n - off) : 0, __VA_ARGS__); \
        if (_w < 0 || off + _w >= n) return -1;                          \
        off += _w;                                                       \
    } while (0)

int sensor_json_fields(char *buf, size_t n, const sensor_reading_t *s)
{
    if (!buf || !s || n == 0) return -1;
    size_t off = 0;
    const char *sep = "";  /* becomes "," after the first field */

    if (s->valid & SENSOR_CAP_TEMPERATURE) {
        APPEND("%s\"temperature\":%.1f", sep, s->temperature);
        sep = ",";
    }
    if (s->valid & SENSOR_CAP_HUMIDITY) {
        APPEND("%s\"humidity\":%.1f", sep, s->humidity);
        sep = ",";
    }
    if (s->valid & SENSOR_CAP_PRESSURE) {
        APPEND("%s\"pressure\":%.1f", sep, s->pressure / 100.0f);
        sep = ",";
    }
    if ((s->valid & SENSOR_CAP_TEMPERATURE) && (s->valid & SENSOR_CAP_HUMIDITY)) {
        APPEND("%s\"dew_point\":%.1f", sep, sensor_dew_point(s->temperature, s->humidity));
        sep = ",";
    }
    if (s->valid & SENSOR_CAP_GAS_RESISTANCE) {
        APPEND("%s\"gas_resistance\":%.0f", sep, s->gas_resistance);
        sep = ",";
    }
    if (s->valid & SENSOR_CAP_IAQ) {
        APPEND("%s\"iaq\":%.1f,\"static_iaq\":%.1f,\"iaq_accuracy\":%u",
               sep, s->iaq, s->static_iaq, (unsigned)s->iaq_accuracy);
        sep = ",";
    }
    if (s->valid & SENSOR_CAP_CO2) {
        if (s->co2_is_equiv)
            APPEND("%s\"co2_eq\":%.1f", sep, s->co2);
        else
            APPEND("%s\"co2\":%u", sep, (unsigned)(s->co2 + 0.5f));
        sep = ",";
    }
    if (s->valid & SENSOR_CAP_VOC_EQ) {
        APPEND("%s\"voc_eq\":%.3f", sep, s->voc_eq);
        sep = ",";
    }

    return (int)off;
}

int mqtt_diag_fields(char *buf, size_t n)
{
    if (!buf || n == 0) return -1;

    photores_data_t ldr = {0};
    photores_read(&ldr);

    int8_t rssi = 0;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) rssi = ap.rssi;

    int64_t uptime_s = esp_timer_get_time() / 1000000LL;
    uint8_t bl_pct   = (uint8_t)((uint32_t)display_get_backlight() * 100u / 255u);

    int len = snprintf(buf, n,
                       "\"uptime\":%lld,\"rssi\":%d,\"backlight\":%u,\"ldr_mv\":%d",
                       (long long)uptime_s, (int)rssi, (unsigned)bl_pct, ldr.voltage_mv);
    if (len < 0 || len >= (int)n) return -1;
    return len;
}