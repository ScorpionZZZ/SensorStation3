// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "tb_mqtt.h"
#include "mqtt_base.h"
#include "mqtt_client.h"
#include "sensor_hub.h"
#include "sensor_history.h"
#include "sensor_json.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>

static const char *TAG = "tb_mqtt";

/* Same namespace/keys as nvs_settings.c — avoids a dependency on main/. */
#define NVS_NS        "settings"
#define KEY_TB_URI     "tb_uri"
#define KEY_TB_TOKEN   "tb_token"
#define KEY_TB_INTERV  "tb_interval"
#define KEY_TB_ENABLED "tb_enabled"

#define TB_TELEMETRY_TOPIC "v1/devices/me/telemetry"

static char s_uri[128];
static char s_token[64];

static mqtt_base_t     *s_base = NULL;
static volatile int     s_pending_msg_id  = -1;
static volatile bool    s_last_confirmed  = false;

/* ── circular publish queue ───────────────────────────────────────────── */

#define QUEUE_SIZE   20
#define PAYLOAD_MAX  400

typedef struct { char data[PAYLOAD_MAX]; int len; } q_entry_t;

static q_entry_t         s_queue[QUEUE_SIZE];
static int               s_q_head  = 0;
static int               s_q_tail  = 0;
static int               s_q_count = 0;
static SemaphoreHandle_t s_q_mutex = NULL;

/* ── helpers ──────────────────────────────────────────────────────────── */

/* Must be called with s_q_mutex held. */
static void queue_push(const char *buf, int len)
{
    if (len <= 0 || len >= PAYLOAD_MAX) return;
    if (s_q_count == QUEUE_SIZE) {
        /* Full — drop oldest entry to make room. */
        s_q_head = (s_q_head + 1) % QUEUE_SIZE;
        s_q_count--;
        ESP_LOGW(TAG, "queue full — oldest entry dropped");
    }
    memcpy(s_queue[s_q_tail].data, buf, len);
    s_queue[s_q_tail].data[len] = '\0';
    s_queue[s_q_tail].len = len;
    s_q_tail  = (s_q_tail + 1) % QUEUE_SIZE;
    s_q_count++;
    ESP_LOGD(TAG, "queued (%d/%d)", s_q_count, QUEUE_SIZE);
}

/* Must be called with s_q_mutex held. */
static void queue_pop(void)
{
    if (s_q_count == 0) return;
    s_q_head  = (s_q_head + 1) % QUEUE_SIZE;
    s_q_count--;
}

/* Reset queue + in-flight state. Called on (re)configure — new credentials
 * make old payloads irrelevant. */
static void queue_reset(void)
{
    if (s_q_mutex) {
        xSemaphoreTake(s_q_mutex, portMAX_DELAY);
        s_q_head = s_q_tail = s_q_count = 0;
        xSemaphoreGive(s_q_mutex);
    }
    s_pending_msg_id = -1;
    s_last_confirmed = false;
}

/* Publish the head entry if idle and connected. Must be called with s_q_mutex held. */
static void try_publish_head(esp_mqtt_client_handle_t client)
{
    if (!mqtt_base_is_connected(s_base) || s_pending_msg_id != -1 || s_q_count == 0)
        return;
    q_entry_t *e = &s_queue[s_q_head];
    int msg_id = esp_mqtt_client_publish(client, TB_TELEMETRY_TOPIC,
                                         e->data, e->len, /*qos*/1, /*retain*/0);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "publish failed (queue depth %d)", s_q_count);
    } else {
        s_pending_msg_id = msg_id;
        s_last_confirmed = false;
        ESP_LOGD(TAG, "sent id=%d queue=%d", msg_id, s_q_count);
    }
}

/* ── mqtt_base hooks ──────────────────────────────────────────────────── */

/* on_publish_tick: build a fresh reading, enqueue it, then publish the queue
 * head (which is any older backlog first, else this fresh entry). Runs in the
 * esp_timer task, once on connect then every interval. */
static void tb_tick(esp_mqtt_client_handle_t client, void *ctx)
{
    (void)ctx;

    time_t ts = time(NULL);

    /* Sensor values from the unified snapshot; diagnostics from the shared
     * helper. Field order and formatting match the previous inline payload. */
    sensor_reading_t s;
    char fields[256];
    char diag[128];
    if (!sensor_history_get_snapshot(&s) ||
        sensor_json_fields(fields, sizeof(fields), &s) <= 0 ||
        mqtt_diag_fields(diag, sizeof(diag)) <= 0)
        return;

    char buf[512];
    int  len = snprintf(buf, sizeof(buf), "{%s,\"timestamp\":%lld,%s}",
                        fields, (long long)ts, diag);
    if (len <= 0 || len >= (int)sizeof(buf)) return;

    xSemaphoreTake(s_q_mutex, portMAX_DELAY);
    queue_push(buf, len);
    try_publish_head(client);
    xSemaphoreGive(s_q_mutex);
}

/* on_published: PUBACK for the in-flight entry — drop it and send the next. */
static void tb_on_published(esp_mqtt_client_handle_t client, int msg_id, void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "delivery confirmed id=%d (queue depth %d)", msg_id, s_q_count);
    if (msg_id != s_pending_msg_id) return;
    xSemaphoreTake(s_q_mutex, portMAX_DELAY);
    s_pending_msg_id = -1;
    s_last_confirmed = true;
    queue_pop();
    try_publish_head(client);   /* immediately send next queued entry */
    xSemaphoreGive(s_q_mutex);
}

/* on_disconnected: the in-flight message stays at the queue head for retry. */
static void tb_on_disconnected(void *ctx)
{
    (void)ctx;
    ESP_LOGW(TAG, "disconnected (queue depth %d)", s_q_count);
    s_pending_msg_id = -1;
    s_last_confirmed = false;
}

/* ── internal: read NVS + (re)configure the shared client ──────────────── */

static esp_err_t apply_config(void)
{
    queue_reset();

    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READONLY, &h), TAG, "nvs_open");

    memset(s_uri,   0, sizeof(s_uri));
    memset(s_token, 0, sizeof(s_token));
    size_t uri_len   = sizeof(s_uri);
    size_t token_len = sizeof(s_token);
    uint16_t interval_s = 30;
    uint8_t  enabled    = 1;   /* default: on, key may be absent */

    nvs_get_str(h, KEY_TB_URI,     s_uri,   &uri_len);
    nvs_get_str(h, KEY_TB_TOKEN,   s_token, &token_len);
    nvs_get_u16(h, KEY_TB_INTERV,  &interval_s);
    nvs_get_u8(h,  KEY_TB_ENABLED, &enabled);
    nvs_close(h);

    if (!enabled) {
        ESP_LOGI(TAG, "telemetry disabled by user setting");
        return mqtt_base_configure(s_base, NULL);
    }
    if (s_uri[0] == '\0' || s_token[0] == '\0') {
        ESP_LOGI(TAG, "credentials not set — MQTT disabled");
        return mqtt_base_configure(s_base, NULL);
    }

    if (interval_s == 0) interval_s = 30;

    mqtt_base_conn_t conn = {
        .uri         = s_uri,
        .username    = s_token,   /* device token = MQTT username */
        .interval_us = (uint32_t)interval_s * 1000000UL,
    };
    esp_err_t ret = mqtt_base_configure(s_base, &conn);
    if (ret == ESP_OK)
        ESP_LOGI(TAG, "configured — broker=%s interval=%" PRIu16 "s", s_uri, interval_s);
    return ret;
}

/* ── public API ───────────────────────────────────────────────────────── */

bool tb_mqtt_is_connected(void)   { return mqtt_base_is_connected(s_base); }
bool tb_mqtt_last_confirmed(void) { return s_last_confirmed; }

esp_err_t tb_mqtt_init(void)
{
    if (!s_q_mutex) {
        s_q_mutex = xSemaphoreCreateMutex();
        if (!s_q_mutex) return ESP_ERR_NO_MEM;
    }
    if (!s_base) {
        mqtt_base_cfg_t cfg = {
            .tag             = TAG,
            .id_prefix       = "ss3-tb-",
            .on_publish_tick = tb_tick,
            .on_published    = tb_on_published,
            .on_disconnected = tb_on_disconnected,
        };
        s_base = mqtt_base_new(&cfg);
        if (!s_base) return ESP_ERR_NO_MEM;
    }
    return apply_config();
}

esp_err_t tb_mqtt_start(void)
{
    esp_err_t ret = apply_config();
    if (ret != ESP_OK) return ret;
    ret = mqtt_base_start(s_base);
    if (ret == ESP_OK) ESP_LOGI(TAG, "client started");
    return ret;
}

void tb_mqtt_stop(void)
{
    mqtt_base_stop(s_base);
    ESP_LOGI(TAG, "stopped");
}