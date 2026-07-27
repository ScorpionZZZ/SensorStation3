// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "sensor_history.h"
#include "bmx280.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <math.h>

static const char *TAG = "sensor_history";

#define BUF_30S_SLOTS    60      /* 60 × 500 ms = 30 s */
#define ACCUM_SAMPLES    1200    /* 1200 × 500 ms = 10 min */
#define HIST_24H_SLOTS   SENSOR_HISTORY_24H_SLOTS

/* ── 30-second rolling buffer ────────────────────────────────────────── */
static float s_buf_temp[BUF_30S_SLOTS];
static float s_buf_hum[BUF_30S_SLOTS];
static float s_buf_pres[BUF_30S_SLOTS];
static int   s_buf_head;
static int   s_buf_count;

/* ── 10-minute accumulator ───────────────────────────────────────────── */
static double s_accum_temp;
static double s_accum_hum;
static double s_accum_pres;
static int    s_accum_count;

/* ── 24-hour ring buffer ─────────────────────────────────────────────── */
static history_slot_t s_hist[HIST_24H_SLOTS];
static int            s_hist_head;
static int            s_hist_count;

/* ── rolling average output ──────────────────────────────────────────── */
static float s_cur_temp;
static float s_cur_hum;
static float s_cur_pres;
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

        bmx280_data_t d;
        if (bmx280_read(&d) != ESP_OK) continue;
        if (isnan(d.temperature)) continue;

        float hum  = isnan(d.humidity) ? 0.0f : d.humidity;
        float pres = d.pressure;

        xSemaphoreTake(s_mutex, portMAX_DELAY);

        /* push into 30-second circular buffer */
        s_buf_temp[s_buf_head] = d.temperature;
        s_buf_hum[s_buf_head]  = hum;
        s_buf_pres[s_buf_head] = pres;
        s_buf_head = (s_buf_head + 1) % BUF_30S_SLOTS;
        if (s_buf_count < BUF_30S_SLOTS) s_buf_count++;

        /* accumulate toward next 10-min history slot */
        s_accum_temp  += d.temperature;
        s_accum_hum   += hum;
        s_accum_pres  += pres;
        s_accum_count++;
        if (s_accum_count >= ACCUM_SAMPLES) {
            s_hist[s_hist_head].temp     = (float)(s_accum_temp / s_accum_count);
            s_hist[s_hist_head].humidity = (float)(s_accum_hum  / s_accum_count);
            s_hist[s_hist_head].pressure = (float)(s_accum_pres / s_accum_count);
            s_hist[s_hist_head].valid    = true;
            s_hist_head = (s_hist_head + 1) % HIST_24H_SLOTS;
            if (s_hist_count < HIST_24H_SLOTS) s_hist_count++;
            s_accum_temp  = 0.0;
            s_accum_hum   = 0.0;
            s_accum_pres  = 0.0;
            s_accum_count = 0;
            ESP_LOGD(TAG, "10-min slot committed (%d/144)", s_hist_count);
        }

        /* rolling 30-second mean */
        float mean_temp = buf_mean(s_buf_temp, s_buf_count);
        float mean_hum  = buf_mean(s_buf_hum,  s_buf_count);
        float mean_pres = buf_mean(s_buf_pres, s_buf_count);
        s_cur_temp  = mean_temp;
        s_cur_hum   = mean_hum;
        s_cur_pres  = mean_pres;
        s_cur_valid = true;

        /* trend: momentary reading vs. its own rolling 30 s mean, every tick */
        float rt = roundf((d.temperature - mean_temp) * 10.0f) / 10.0f;
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
    xTaskCreate(history_task, "sens_hist", 2048, NULL, 4, NULL);
    ESP_LOGI(TAG, "started (30 s buffer: %d × 500 ms, 24 h ring: %d × 10 min)",
             BUF_30S_SLOTS, HIST_24H_SLOTS);
    return ESP_OK;
}

bool sensor_history_get_current(float *temp, float *humidity, float *pressure)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool valid = s_cur_valid;
    if (temp)     *temp     = s_cur_temp;
    if (humidity) *humidity = s_cur_hum;
    if (pressure) *pressure = s_cur_pres;
    xSemaphoreGive(s_mutex);
    return valid;
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