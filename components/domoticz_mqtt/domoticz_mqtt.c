// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "domoticz_mqtt.h"
#include "mqtt_client.h"
#include "bmx280.h"
#if CONFIG_SS3_USE_BSEC
#include "bsec_sensor.h"
#endif
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_timer.h"
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
static char s_client_id[24];   /* "ss3-dz-aabbccddeeff" — must differ from
                                 * tb_mqtt's/ha_mqtt's client IDs or the
                                 * broker drops whichever connected first. */

static esp_mqtt_client_handle_t s_client    = NULL;
static esp_timer_handle_t       s_timer     = NULL;
static volatile bool            s_connected = false;
static volatile bool            s_started   = false;
static uint16_t                 s_thp_idx   = 0;
static uint16_t                 s_co2_idx   = 0;

/* ── helpers ──────────────────────────────────────────────────────────── */

/* MAC never changes at runtime, so this only needs to run once. */
static void ensure_client_id(void)
{
    if (s_client_id[0]) return;
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_client_id, sizeof(s_client_id), "ss3-dz-%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

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

static void publish_readings(void *arg)
{
    (void)arg;
    if (!s_connected) return;

    bmx280_data_t bme;
    if (bmx280_read(&bme) != ESP_OK) return;

    int  rssi = rssi_domoticz();
    char buf[160];
    int  len;

    /* Temp+Hum+Baro virtual device — svalue: "temp;hum;hum_status;baro_hpa;forecast" */
    if (s_thp_idx > 0) {
        float   hum = isnan(bme.humidity) ? 0.0f : bme.humidity;
        uint8_t hs  = isnan(bme.humidity) ? 0    : hum_status(bme.humidity);
        len = snprintf(buf, sizeof(buf),
                       "{\"idx\":%u,\"nvalue\":0,"
                       "\"svalue\":\"%.1f;%.0f;%u;%.1f;0\",\"RSSI\":%d}",
                       (unsigned)s_thp_idx,
                       bme.temperature, hum, (unsigned)hs,
                       bme.pressure / 100.0f, rssi);
        if (len > 0 && len < (int)sizeof(buf))
            esp_mqtt_client_publish(s_client, DZ_TOPIC, buf, len,
                                    /*qos*/0, /*retain*/0);
    }

    /* Air Quality virtual device — CO2eq ppm (BME680 only). BSEC's
     * compensated estimate when active, else bmx280's own gas-resistance-
     * based approximation. */
    if (s_co2_idx > 0 && bmx280_get_type() == BMX280_TYPE_BME680) {
        float co2 = NAN;
#if CONFIG_SS3_USE_BSEC
        bsec_data_t bsec;
        if (bsec_sensor_is_active() && bsec_sensor_read(&bsec) == ESP_OK)
            co2 = bsec.co2_eq;
#else
        bmx280_co2eq_avg(&co2);
#endif
        if (!isnan(co2)) {
            int co2_int = (int)co2;
            len = snprintf(buf, sizeof(buf),
                           "{\"idx\":%u,\"nvalue\":%d,\"svalue\":\"%d\",\"RSSI\":%d}",
                           (unsigned)s_co2_idx, co2_int, co2_int, rssi);
            if (len > 0 && len < (int)sizeof(buf))
                esp_mqtt_client_publish(s_client, DZ_TOPIC, buf, len,
                                        /*qos*/0, /*retain*/0);
        }
    }
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
        esp_timer_start_periodic(s_timer, DZ_INTERVAL_US);
        publish_readings(NULL);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected");
        s_connected = false;
        esp_timer_stop(s_timer);
        break;

    case MQTT_EVENT_ERROR:
        if (ev->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
            ESP_LOGE(TAG, "transport error: esp_err=0x%x",
                     ev->error_handle->esp_tls_last_esp_err);
        break;

    default:
        break;
    }
}

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
    s_connected = false;
    s_started   = false;
}

/* ── internal: read NVS + create client + timer ───────────────────────── */

static esp_err_t apply_config(void)
{
    teardown();
    ensure_client_id();

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
        return ESP_OK;
    }
    if (s_uri[0] == '\0') {
        ESP_LOGI(TAG, "broker URI not set — disabled");
        return ESP_OK;
    }
    if (thp_idx == 0 && co2_idx == 0) {
        ESP_LOGI(TAG, "no device IDX configured — disabled");
        return ESP_OK;
    }

    s_thp_idx = thp_idx;
    s_co2_idx = co2_idx;

    esp_timer_create_args_t ta = {
        .callback = publish_readings,
        .name     = "dz_pub",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&ta, &s_timer), TAG, "timer_create");

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri                    = s_uri,
        .credentials.client_id                 = s_client_id,
        .credentials.username                  = s_user[0] ? s_user : NULL,
        .credentials.authentication.password   = s_pass[0] ? s_pass : NULL,
    };
    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) return ESP_ERR_NO_MEM;

    ESP_RETURN_ON_ERROR(
        esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                       mqtt_event_handler, NULL),
        TAG, "reg_mqtt");

    ESP_LOGI(TAG, "configured — broker=%s thp_idx=%u co2_idx=%u auth=%s",
             s_uri, (unsigned)thp_idx, (unsigned)co2_idx,
             s_user[0] ? "yes" : "no");
    return ESP_OK;
}

/* ── public API ───────────────────────────────────────────────────────── */

bool domoticz_mqtt_is_connected(void) { return s_connected; }

esp_err_t domoticz_mqtt_init(void)
{
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            on_got_ip, NULL, NULL),
        TAG, "reg_ip");
    return apply_config();
}

esp_err_t domoticz_mqtt_start(void)
{
    esp_err_t ret = apply_config();
    if (ret != ESP_OK || !s_client) return ret;
    esp_mqtt_client_start(s_client);
    s_started = true;
    ESP_LOGI(TAG, "client started");
    return ESP_OK;
}

void domoticz_mqtt_stop(void)
{
    teardown();
    ESP_LOGI(TAG, "stopped");
}
