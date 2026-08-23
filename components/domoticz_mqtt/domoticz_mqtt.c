// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "domoticz_mqtt.h"
#include "mqtt_base.h"
#include "mqtt_client.h"
#include "sensor_hub.h"
#include "sensor_history.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_check.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "dz_mqtt";

/* Same namespace/keys as nvs_settings.c — avoids a dependency on main/. */
#define NVS_NS         "settings"
#define KEY_DZ_URI     "dz_uri"
#define KEY_DZ_ENABLED "dz_enabled"
#define KEY_DZ_THP_IDX "dz_thp_idx"
#define KEY_DZ_CO2_IDX "dz_co2_idx"
#define KEY_DZ_USER    "dz_user"
#define KEY_DZ_PASS    "dz_pass"

#define DZ_TOPIC        "domoticz/in"
#define DZ_INTERVAL_US  (30 * 1000000UL)   /* 30 s */

static char s_uri[128];
static char s_user[64];
static char s_pass[64];
static uint16_t s_thp_idx = 0;
static uint16_t s_co2_idx = 0;

static mqtt_base_t *s_base = NULL;

/* ── helpers ──────────────────────────────────────────────────────────── */

/* Domoticz humidity status codes: 0=Normal 1=Comfortable 2=Dry 3=Wet */
static uint8_t hum_status(float rh)
{
    if (rh < 30.0f)  return 2;
    if (rh < 45.0f)  return 0;
    if (rh <= 70.0f) return 1;
    return 3;
}

static int rssi_domoticz(void)
{
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return 0;
    int v = (ap.rssi + 100) / 8;
    if (v < 0)  v = 0;
    if (v > 12) v = 12;
    return v;
}

/* ── publish (mqtt_base on_publish_tick) ──────────────────────────────────
 * Runs in the esp_timer task, once on connect then every DZ_INTERVAL_US. */
static void dz_tick(esp_mqtt_client_handle_t client, void *ctx)
{
    (void)ctx;

    sensor_reading_t s;
    if (!sensor_history_get_snapshot(&s)) return;

    int  rssi = rssi_domoticz();
    char buf[160];
    int  len = 0;

    /* Temperature virtual device — the Domoticz device *type* is chosen by
     * which fields the sensor set provides, and must match the type of the
     * virtual device created for s_thp_idx:
     *   temp+pressure → "Temp+Hum+Baro"  svalue "temp;hum;hum_status;baro;forecast"
     *   temp+humidity → "Temp+Hum"       svalue "temp;hum;hum_status"   (e.g. SCD4x)
     *   temp only     → "Temperature"    svalue "temp"
     * A barometer sensor keeps the original THB payload byte-for-byte. */
    if (s_thp_idx > 0 && (s.valid & SENSOR_CAP_TEMPERATURE)) {
        bool    has_hum  = (s.valid & SENSOR_CAP_HUMIDITY) != 0;
        bool    has_pres = (s.valid & SENSOR_CAP_PRESSURE) != 0;
        float   hum = has_hum ? s.humidity : 0.0f;
        uint8_t hs  = has_hum ? hum_status(s.humidity) : 0;

        if (has_pres) {
            len = snprintf(buf, sizeof(buf),
                           "{\"idx\":%u,\"nvalue\":0,"
                           "\"svalue\":\"%.1f;%.0f;%u;%.1f;0\",\"RSSI\":%d}",
                           (unsigned)s_thp_idx,
                           s.temperature, hum, (unsigned)hs,
                           s.pressure / 100.0f, rssi);
        } else if (has_hum) {
            len = snprintf(buf, sizeof(buf),
                           "{\"idx\":%u,\"nvalue\":0,"
                           "\"svalue\":\"%.1f;%.0f;%u\",\"RSSI\":%d}",
                           (unsigned)s_thp_idx,
                           s.temperature, hum, (unsigned)hs, rssi);
        } else {
            len = snprintf(buf, sizeof(buf),
                           "{\"idx\":%u,\"nvalue\":0,"
                           "\"svalue\":\"%.1f\",\"RSSI\":%d}",
                           (unsigned)s_thp_idx, s.temperature, rssi);
        }
        if (len > 0 && len < (int)sizeof(buf))
            esp_mqtt_client_publish(client, DZ_TOPIC, buf, len,
                                    /*qos*/0, /*retain*/0);
    }

    /* Air Quality virtual device — real CO2 (SCD4x) or CO2-equivalent (BME680),
     * whichever the hub merged into the snapshot. */
    if (s_co2_idx > 0 && (s.valid & SENSOR_CAP_CO2)) {
        int co2_int = (int)s.co2;
        len = snprintf(buf, sizeof(buf),
                       "{\"idx\":%u,\"nvalue\":%d,\"svalue\":\"%d\",\"RSSI\":%d}",
                       (unsigned)s_co2_idx, co2_int, co2_int, rssi);
        if (len > 0 && len < (int)sizeof(buf))
            esp_mqtt_client_publish(client, DZ_TOPIC, buf, len,
                                    /*qos*/0, /*retain*/0);
    }
}

/* ── internal: read NVS + (re)configure the shared client ──────────────── */

static esp_err_t apply_config(void)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READONLY, &h), TAG, "nvs_open");

    memset(s_uri,  0, sizeof(s_uri));
    memset(s_user, 0, sizeof(s_user));
    memset(s_pass, 0, sizeof(s_pass));
    size_t   uri_len  = sizeof(s_uri);
    size_t   user_len = sizeof(s_user);
    size_t   pass_len = sizeof(s_pass);
    uint8_t  enabled  = 0;
    uint16_t thp_idx  = 0;
    uint16_t co2_idx  = 0;

    nvs_get_str(h, KEY_DZ_URI,     s_uri,    &uri_len);
    nvs_get_u8(h,  KEY_DZ_ENABLED, &enabled);
    nvs_get_u16(h, KEY_DZ_THP_IDX, &thp_idx);
    nvs_get_u16(h, KEY_DZ_CO2_IDX, &co2_idx);
    nvs_get_str(h, KEY_DZ_USER,    s_user,   &user_len);
    nvs_get_str(h, KEY_DZ_PASS,    s_pass,   &pass_len);
    nvs_close(h);

    if (!enabled) {
        ESP_LOGI(TAG, "disabled by user setting");
        return mqtt_base_configure(s_base, NULL);
    }
    if (s_uri[0] == '\0') {
        ESP_LOGI(TAG, "broker URI not set — disabled");
        return mqtt_base_configure(s_base, NULL);
    }
    if (thp_idx == 0 && co2_idx == 0) {
        ESP_LOGI(TAG, "no device IDX configured — disabled");
        return mqtt_base_configure(s_base, NULL);
    }

    s_thp_idx = thp_idx;
    s_co2_idx = co2_idx;

    mqtt_base_conn_t conn = {
        .uri         = s_uri,
        .username    = s_user[0] ? s_user : NULL,
        .password    = s_pass[0] ? s_pass : NULL,
        .interval_us = DZ_INTERVAL_US,
    };
    esp_err_t ret = mqtt_base_configure(s_base, &conn);
    if (ret == ESP_OK)
        ESP_LOGI(TAG, "configured — broker=%s thp_idx=%u co2_idx=%u auth=%s",
                 s_uri, (unsigned)thp_idx, (unsigned)co2_idx,
                 s_user[0] ? "yes" : "no");
    return ret;
}

/* ── public API ───────────────────────────────────────────────────────── */

bool domoticz_mqtt_is_connected(void) { return mqtt_base_is_connected(s_base); }

esp_err_t domoticz_mqtt_init(void)
{
    if (!s_base) {
        mqtt_base_cfg_t cfg = {
            .tag             = TAG,
            .id_prefix       = "ss3-dz-",
            .on_publish_tick = dz_tick,
        };
        s_base = mqtt_base_new(&cfg);
        if (!s_base) return ESP_ERR_NO_MEM;
    }
    return apply_config();
}

esp_err_t domoticz_mqtt_start(void)
{
    esp_err_t ret = apply_config();
    if (ret != ESP_OK) return ret;
    ret = mqtt_base_start(s_base);
    if (ret == ESP_OK) ESP_LOGI(TAG, "client started");
    return ret;
}

void domoticz_mqtt_stop(void)
{
    mqtt_base_stop(s_base);
    ESP_LOGI(TAG, "stopped");
}