// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "tb_mqtt.h"
#include "mqtt_client.h"
#include "bmx280.h"
#if CONFIG_SS3_USE_BSEC
#include "bsec_sensor.h"
#endif
#include "photores.h"
#include "display.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <math.h>
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

static esp_mqtt_client_handle_t s_client;
static esp_timer_handle_t       s_timer;
static volatile bool            s_connected;
static volatile bool            s_started;
static uint32_t                 s_interval_us;
static volatile int             s_pending_msg_id  = -1;
static volatile bool            s_last_confirmed  = false;

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

static float dew_point(float t, float rh)
{
    const float a = 17.625f, b = 243.04f;
    float alpha = logf(rh / 100.0f) + a * t / (b + t);
    return b * alpha / (a - alpha);
}

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

/* Publish the head entry if idle and connected. Must be called with s_q_mutex held. */
static void try_publish_head(void)
{
    if (!s_connected || s_pending_msg_id != -1 || s_q_count == 0) return;
    q_entry_t *e = &s_queue[s_q_head];
    int msg_id = esp_mqtt_client_publish(s_client, TB_TELEMETRY_TOPIC,
                                         e->data, e->len, /*qos*/1, /*retain*/0);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "publish failed (queue depth %d)", s_q_count);
    } else {
        s_pending_msg_id = msg_id;
        s_last_confirmed = false;
        ESP_LOGD(TAG, "sent id=%d queue=%d", msg_id, s_q_count);
    }
}

static void publish_telemetry(void *arg)
{
    (void)arg;
    if (!s_connected) return;

    photores_data_t ldr = {0};
    photores_read(&ldr);

    int8_t rssi = 0;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) rssi = ap.rssi;

    int64_t uptime_s = esp_timer_get_time() / 1000000LL;
    time_t  ts       = time(NULL);
    uint8_t bl_pct   = (uint8_t)((uint32_t)display_get_backlight() * 100u / 255u);

    char buf[512];
    int  len;

    /* BME680 with BSEC active: emit full air-quality payload */
#if CONFIG_SS3_USE_BSEC
    if (bmx280_get_type() == BMX280_TYPE_BME680 && bsec_sensor_is_active()) {
        bsec_data_t bsec;
        if (bsec_sensor_read(&bsec) != ESP_OK) return;
        float dp = dew_point(bsec.temperature, bsec.humidity);
        len = snprintf(buf, sizeof(buf),
                       "{\"temperature\":%.1f,\"humidity\":%.1f,"
                       "\"pressure\":%.1f,\"dew_point\":%.1f,"
                       "\"iaq\":%.1f,\"static_iaq\":%.1f,\"iaq_accuracy\":%u,"
                       "\"co2_eq\":%.1f,\"voc_eq\":%.3f,"
                       "\"timestamp\":%lld,\"uptime\":%lld,"
                       "\"rssi\":%d,\"backlight\":%u,\"ldr_mv\":%d}",
                       bsec.temperature, bsec.humidity,
                       bsec.pressure / 100.0f, dp,
                       bsec.iaq, bsec.static_iaq, (unsigned)bsec.iaq_accuracy,
                       bsec.co2_eq, bsec.voc_eq,
                       (long long)ts, (long long)uptime_s,
                       (int)rssi, (unsigned)bl_pct, ldr.voltage_mv);
    } else
#endif
    {
        /* BME280 / BMP280 path — unchanged */
        bmx280_data_t bme;
        if (bmx280_read(&bme) != ESP_OK) return;

        bool has_hum = !isnan(bme.humidity);
        if (has_hum) {
            float dp = dew_point(bme.temperature, bme.humidity);
            len = snprintf(buf, sizeof(buf),
                           "{\"temperature\":%.1f,\"humidity\":%.1f,"
                           "\"pressure\":%.1f,\"dew_point\":%.1f,"
                           "\"timestamp\":%lld,\"uptime\":%lld,"
                           "\"rssi\":%d,\"backlight\":%u,\"ldr_mv\":%d}",
                           bme.temperature, bme.humidity,
                           bme.pressure / 100.0f, dp,
                           (long long)ts, (long long)uptime_s,
                           (int)rssi, (unsigned)bl_pct, ldr.voltage_mv);
        } else {
            len = snprintf(buf, sizeof(buf),
                           "{\"temperature\":%.1f,\"pressure\":%.1f,"
                           "\"timestamp\":%lld,\"uptime\":%lld,"
                           "\"rssi\":%d,\"backlight\":%u,\"ldr_mv\":%d}",
                           bme.temperature, bme.pressure / 100.0f,
                           (long long)ts, (long long)uptime_s,
                           (int)rssi, (unsigned)bl_pct, ldr.voltage_mv);
        }
    }

    if (len <= 0 || len >= (int)sizeof(buf)) return;

    xSemaphoreTake(s_q_mutex, portMAX_DELAY);
    queue_push(buf, len);
    try_publish_head();
    xSemaphoreGive(s_q_mutex);
}

/* ── event handlers ───────────────────────────────────────────────────── */

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    esp_mqtt_event_handle_t ev = data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected to %s", s_uri);
        s_connected = true;
        esp_timer_start_periodic(s_timer, s_interval_us);
        /* Drain any messages queued while offline, then enqueue a fresh reading. */
        xSemaphoreTake(s_q_mutex, portMAX_DELAY);
        try_publish_head();
        xSemaphoreGive(s_q_mutex);
        publish_telemetry(NULL);
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "delivery confirmed id=%d (queue depth %d)", ev->msg_id, s_q_count);
        if (ev->msg_id == s_pending_msg_id) {
            xSemaphoreTake(s_q_mutex, portMAX_DELAY);
            s_pending_msg_id = -1;
            s_last_confirmed = true;
            queue_pop();
            try_publish_head();   /* immediately send next queued entry */
            xSemaphoreGive(s_q_mutex);
        }
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected (queue depth %d)", s_q_count);
        s_connected      = false;
        s_pending_msg_id = -1;   /* in-flight msg stays at queue head for retry */
        s_last_confirmed = false;
        esp_timer_stop(s_timer);
        break;

    case MQTT_EVENT_ERROR:
        if (ev->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGE(TAG, "transport error: esp_err=0x%x sock_errno=%d",
                     ev->error_handle->esp_tls_last_esp_err,
                     ev->error_handle->esp_transport_sock_errno);
        }
        break;

    default:
        break;
    }
}

/* IP event: start client on first connection after boot.
 * A no-op when s_client is NULL (MQTT disabled at init). */
static void on_got_ip(void *arg, esp_event_base_t base,
                      int32_t id, void *data)
{
    if (!s_client) return;
    if (!s_started) {
        ESP_LOGI(TAG, "got IP, starting MQTT client");
        esp_mqtt_client_start(s_client);
        s_started = true;
    }
}

/* ── internal: destroy existing client + timer ────────────────────────── */

static void teardown(void)
{
    if (s_timer) {
        esp_timer_stop(s_timer);
        esp_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (s_client) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    s_connected      = false;
    s_started        = false;
    s_pending_msg_id = -1;
    s_last_confirmed = false;
    /* Clear queue — teardown means new credentials, old payloads are irrelevant. */
    if (s_q_mutex) {
        xSemaphoreTake(s_q_mutex, portMAX_DELAY);
        s_q_head = s_q_tail = s_q_count = 0;
        xSemaphoreGive(s_q_mutex);
    }
}

/* ── internal: read NVS + create client + timer ───────────────────────── */

static esp_err_t apply_config(void)
{
    teardown();

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
        return ESP_OK;
    }
    if (s_uri[0] == '\0' || s_token[0] == '\0') {
        ESP_LOGI(TAG, "credentials not set — MQTT disabled");
        return ESP_OK;
    }

    if (interval_s == 0) interval_s = 30;
    s_interval_us = (uint32_t)interval_s * 1000000UL;

    esp_timer_create_args_t ta = {
        .callback = publish_telemetry,
        .name     = "tb_pub",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&ta, &s_timer), TAG, "timer_create");

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri   = s_uri,
        .credentials.username = s_token,
    };
    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) return ESP_ERR_NO_MEM;

    ESP_RETURN_ON_ERROR(
        esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                       mqtt_event_handler, NULL),
        TAG, "reg_mqtt");

    ESP_LOGI(TAG, "configured — broker=%s interval=%" PRIu16 "s", s_uri, interval_s);
    return ESP_OK;
}

/* ── public API ───────────────────────────────────────────────────────── */

bool tb_mqtt_is_connected(void)   { return s_connected; }
bool tb_mqtt_last_confirmed(void) { return s_last_confirmed; }

esp_err_t tb_mqtt_init(void)
{
    if (!s_q_mutex) {
        s_q_mutex = xSemaphoreCreateMutex();
        if (!s_q_mutex) return ESP_ERR_NO_MEM;
    }

    /* Always register the IP handler — even when MQTT is currently disabled,
     * so that tb_mqtt_start() called later benefits from it on WiFi reconnects. */
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            on_got_ip, NULL, NULL),
        TAG, "reg_ip");

    return apply_config();
    /* Client will be started by on_got_ip when WiFi gets an IP. */
}

esp_err_t tb_mqtt_start(void)
{
    /* Re-read NVS, recreate client. */
    esp_err_t ret = apply_config();
    if (ret != ESP_OK || !s_client) return ret;

    /* Start immediately — don't wait for the IP event.
     * The MQTT client retries internally if the network is briefly unavailable. */
    esp_mqtt_client_start(s_client);
    s_started = true;
    ESP_LOGI(TAG, "client started");
    return ESP_OK;
}

void tb_mqtt_stop(void)
{
    teardown();
    ESP_LOGI(TAG, "stopped");
}