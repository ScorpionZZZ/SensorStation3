// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "sensor_history.h"
#include "sensor_hub.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <math.h>

static const char *TAG = "sensor_history";

#define BUF_30S_SLOTS    60      /* 60 × 500 ms = 30 s  (temp/hum/pressure) */
#define CO2_SLOTS        15      /* 15 × 2 s    = 30 s  (CO2) */
#define CO2_DECIM        4       /* push CO2 once every 4 ticks → 2 s cadence */
#define ACCUM_SAMPLES    1200    /* 1200 × 500 ms = 10 min */
#define HIST_24H_SLOTS   SENSOR_HISTORY_24H_SLOTS

/* ── 30-second rolling buffers (temp/hum/pressure) ───────────────────── */
static float s_buf_temp[BUF_30S_SLOTS];
static float s_buf_hum[BUF_30S_SLOTS];
static float s_buf_pres[BUF_30S_SLOTS];
static int   s_buf_head;
static int   s_buf_count;

/* ── CO2 rolling buffer (15 × 2 s) ───────────────────────────────────── */
static float s_buf_co2[CO2_SLOTS];
static int   s_co2_head;
static int   s_co2_count;
static int   s_co2_decim;       /* tick counter for the ÷4 decimation */

/* ── which fields the connected sensor set actually provides ─────────── */
static bool  s_have_hum;
static bool  s_have_pres;
static bool  s_have_co2;
static bool  s_co2_is_equiv;

/* ── 10-minute accumulator (temp/hum only — the charted fields) ──────── */
static double s_accum_temp;
static double s_accum_hum;
static int    s_accum_count;

/* ── 24-hour ring buffer ─────────────────────────────────────────────── */
static history_slot_t s_hist[HIST_24H_SLOTS];
static int            s_hist_head;
static int            s_hist_count;

/* ── rolling average output ──────────────────────────────────────────── */
static float s_cur_temp;
static float s_cur_hum;
static float s_cur_pres;
static float s_cur_co2;
static bool  s_cur_valid;

/* ── trend state ─────────────────────────────────────────────────────── */
static bool           s_trend_valid;
static sensor_trend_t s_trend_temp;
static sensor_trend_t s_trend_hum;
static float          s_delta_temp;
static float          s_delta_hum;

static SemaphoreHandle_t s_mutex;

/* ── helpers ─────────────────────────────────────────────────────────── */

static float buf_mean(const float *buf, int count)
{
    if (count == 0) return 0.0f;
    double sum = 0.0;
    for (int i = 0; i < count; i++) sum += buf[i];
    return (float)(sum / count);
}

static sensor_trend_t classify_delta(float rounded)
{
    if (rounded > 0.0f) return TREND_UP;
    if (rounded < 0.0f) return TREND_DOWN;
    return TREND_STABLE;
}

/* ── task ────────────────────────────────────────────────────────────── */

static void history_task(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(500));

        sensor_reading_t s;
        if (!sensor_hub_get_current(&s)) continue;
        if (!(s.valid & SENSOR_CAP_TEMPERATURE)) continue;  /* temperature is required */

        float temperature = s.temperature;
        bool  has_hum  = (s.valid & SENSOR_CAP_HUMIDITY) != 0;
        bool  has_pres = (s.valid & SENSOR_CAP_PRESSURE) != 0;
        bool  has_co2  = (s.valid & SENSOR_CAP_CO2) != 0;
        float hum  = has_hum  ? s.humidity : 0.0f;
        float pres = has_pres ? s.pressure : 0.0f;

        xSemaphoreTake(s_mutex, portMAX_DELAY);

        if (has_hum)  s_have_hum  = true;
        if (has_pres) s_have_pres = true;
        if (has_co2) {
            s_have_co2     = true;
            s_co2_is_equiv = s.co2_is_equiv;
        }

        /* push into 30-second circular buffer (temp/hum/pressure) */
        s_buf_temp[s_buf_head] = temperature;
        s_buf_hum[s_buf_head]  = hum;
        s_buf_pres[s_buf_head] = pres;
        s_buf_head = (s_buf_head + 1) % BUF_30S_SLOTS;
        if (s_buf_count < BUF_30S_SLOTS) s_buf_count++;

        /* CO2 loop buffer, decimated to one push per CO2_DECIM ticks (2 s) */
        if (has_co2 && ++s_co2_decim >= CO2_DECIM) {
            s_co2_decim = 0;
            s_buf_co2[s_co2_head] = s.co2;
            s_co2_head = (s_co2_head + 1) % CO2_SLOTS;
            if (s_co2_count < CO2_SLOTS) s_co2_count++;
        }

        /* accumulate toward next 10-min history slot (temp/hum only) */
        s_accum_temp  += temperature;
        s_accum_hum   += hum;
        s_accum_count++;
        if (s_accum_count >= ACCUM_SAMPLES) {
            s_hist[s_hist_head].temp     = (float)(s_accum_temp / s_accum_count);
            s_hist[s_hist_head].humidity = (float)(s_accum_hum  / s_accum_count);
            s_hist[s_hist_head].valid    = true;
            s_hist_head = (s_hist_head + 1) % HIST_24H_SLOTS;
            if (s_hist_count < HIST_24H_SLOTS) s_hist_count++;
            s_accum_temp  = 0.0;
            s_accum_hum   = 0.0;
            s_accum_count = 0;
            ESP_LOGD(TAG, "10-min slot committed (%d/144)", s_hist_count);
        }

        /* rolling means */
        float mean_temp = buf_mean(s_buf_temp, s_buf_count);
        float mean_hum  = buf_mean(s_buf_hum,  s_buf_count);
        float mean_pres = buf_mean(s_buf_pres, s_buf_count);
        s_cur_temp  = mean_temp;
        s_cur_hum   = mean_hum;
        s_cur_pres  = mean_pres;
        s_cur_co2   = buf_mean(s_buf_co2, s_co2_count);
        s_cur_valid = true;

        /* trend: momentary reading vs. its own rolling 30 s mean, every tick */
        float rt = roundf((temperature   - mean_temp) * 10.0f) / 10.0f;
        float rh = roundf((hum           - mean_hum)  * 10.0f) / 10.0f;
        s_delta_temp  = rt;
        s_delta_hum   = rh;
        s_trend_temp  = classify_delta(rt);
        s_trend_hum   = classify_delta(rh);
        s_trend_valid = (s_buf_count >= BUF_30S_SLOTS);

        xSemaphoreGive(s_mutex);
    }
}

/* ── public ──────────────────────────────────────────────────────────── */

esp_err_t sensor_history_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_mutex, ESP_ERR_NO_MEM, TAG, "mutex");
    memset(s_hist, 0, sizeof(s_hist));
    xTaskCreate(history_task, "sens_hist", 2560, NULL, 4, NULL);
    ESP_LOGI(TAG, "started (T/H/P: %d × 500 ms, CO2: %d × 2 s, 24 h ring: %d × 10 min)",
             BUF_30S_SLOTS, CO2_SLOTS, HIST_24H_SLOTS);
    return ESP_OK;
}

bool sensor_history_get_snapshot(sensor_reading_t *out)
{
    if (!out) return false;

    /* Start from the live hub snapshot — this fills gas_resistance / iaq /
     * static_iaq / voc_eq (fields with no loop buffer) and co2_is_equiv. */
    bool have_live = sensor_hub_get_current(out);

    /* Overlay the rolling-average values for the buffered fields. */
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool have_hist = s_cur_valid;
    if (s_cur_valid) {
        out->temperature = s_cur_temp;
        out->valid |= SENSOR_CAP_TEMPERATURE;
        if (s_have_hum) {
            out->humidity = s_cur_hum;
            out->valid |= SENSOR_CAP_HUMIDITY;
        }
        if (s_have_pres) {
            out->pressure = s_cur_pres;
            out->valid |= SENSOR_CAP_PRESSURE;
        }
        if (s_have_co2 && s_co2_count > 0) {
            out->co2          = s_cur_co2;
            out->co2_is_equiv = s_co2_is_equiv;
            out->valid |= SENSOR_CAP_CO2;
        }
    }
    xSemaphoreGive(s_mutex);

    return have_live || have_hist;
}

sensor_trend_t sensor_history_get_temp_trend(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    sensor_trend_t t = s_trend_valid ? s_trend_temp : TREND_UNKNOWN;
    xSemaphoreGive(s_mutex);
    return t;
}

sensor_trend_t sensor_history_get_humidity_trend(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    sensor_trend_t t = s_trend_valid ? s_trend_hum : TREND_UNKNOWN;
    xSemaphoreGive(s_mutex);
    return t;
}

float sensor_history_get_temp_delta(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    float d = s_trend_valid ? s_delta_temp : 0.0f;
    xSemaphoreGive(s_mutex);
    return d;
}

float sensor_history_get_hum_delta(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    float d = s_trend_valid ? s_delta_hum : 0.0f;
    xSemaphoreGive(s_mutex);
    return d;
}

int sensor_history_get_24h(history_slot_t *out, int max_slots)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int count  = s_hist_count < max_slots ? s_hist_count : max_slots;
    int oldest = (s_hist_head - s_hist_count + HIST_24H_SLOTS) % HIST_24H_SLOTS;
    for (int i = 0; i < count; i++)
        out[i] = s_hist[(oldest + i) % HIST_24H_SLOTS];
    xSemaphoreGive(s_mutex);
    return count;
}