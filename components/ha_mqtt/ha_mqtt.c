// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "ha_mqtt.h"
#include "mqtt_client.h"
#include "bmx280.h"
#if CONFIG_SS3_USE_BSEC
#include "bsec_sensor.h"
#endif
#include "photores.h"
#include "display.h"
#include "build_info.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "ha_mqtt";

/* Same namespace/keys as nvs_settings.c — avoids a dependency on main/. */
#define NVS_NS         "settings"
#define KEY_HA_URI     "ha_uri"
#define KEY_HA_ENABLED "ha_enabled"
#define KEY_HA_USER    "ha_user"
#define KEY_HA_PASS    "ha_pass"

#define HA_INTERVAL_US      (30 * 1000000UL)   /* 30 s */
#define HA_DISCOVERY_PREFIX "homeassistant"

static char s_uri[128];
static char s_user[64];
static char s_pass[64];

static char s_dev_id[20];        /* "ss3-aabbccddeeff" */
static char s_state_topic[40];   /* "ss3/aabbccddeeff/state" */
static char s_avail_topic[40];   /* "ss3/aabbccddeeff/status" */
static char s_client_id[24];     /* "ss3-ha-aabbccddeeff" — must differ from
                                   * tb_mqtt's/domoticz_mqtt's client IDs or
                                   * the broker drops whichever connected first. */

static esp_mqtt_client_handle_t s_client    = NULL;
static esp_timer_handle_t       s_timer     = NULL;
static volatile bool            s_connected = false;
static volatile bool            s_started   = false;

/* ── device identity ─────────────────────────────────────────────────── */

/* MAC never changes at runtime, so this only needs to run once. */
static void ensure_device_id(void)
{
    if (s_dev_id[0]) return;

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_dev_id, sizeof(s_dev_id), "ss3-%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(s_state_topic, sizeof(s_state_topic), "ss3/%02x%02x%02x%02x%02x%02x/state",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(s_avail_topic, sizeof(s_avail_topic), "ss3/%02x%02x%02x%02x%02x%02x/status",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(s_client_id, sizeof(s_client_id), "ss3-ha-%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* ── helpers ──────────────────────────────────────────────────────────── */

static float dew_point(float t, float rh)
{
    const float a = 17.625f, b = 243.04f;
    float alpha = logf(rh / 100.0f) + a * t / (b + t);
    return b * alpha / (a - alpha);
}

/* ── Home Assistant MQTT Discovery ───────────────────────────────────────
 * https://www.home-assistant.io/integrations/mqtt/#discovery-messages
 * One retained config message per entity, referencing the single shared
 * state topic via value_template — same approach ESPHome uses. */

typedef struct {
    const char *object_id;
    const char *name;
    const char *value_key;
    /* Fully-formed "key":value pairs incl. trailing comma, or "". */
    const char *extra_json;
} ha_entity_t;

static const ha_entity_t k_ent_common[] = {
    { "temperature", "Temperature", "temperature",
      "\"unit_of_measurement\":\"\xc2\xb0" "C\",\"device_class\":\"temperature\","
      "\"state_class\":\"measurement\"," },
    { "pressure", "Pressure", "pressure",
      "\"unit_of_measurement\":\"hPa\",\"device_class\":\"atmospheric_pressure\","
      "\"state_class\":\"measurement\"," },
    { "rssi", "WiFi Signal", "rssi",
      "\"unit_of_measurement\":\"dBm\",\"device_class\":\"signal_strength\","
      "\"state_class\":\"measurement\",\"entity_category\":\"diagnostic\"," },
    { "uptime", "Uptime", "uptime",
      "\"unit_of_measurement\":\"s\",\"device_class\":\"duration\","
      "\"state_class\":\"measurement\",\"entity_category\":\"diagnostic\"," },
    { "backlight", "Backlight", "backlight",
      "\"unit_of_measurement\":\"%\",\"state_class\":\"measurement\","
      "\"entity_category\":\"diagnostic\"," },
    { "ldr_mv", "Light Sensor", "ldr_mv",
      "\"unit_of_measurement\":\"mV\",\"state_class\":\"measurement\","
      "\"entity_category\":\"diagnostic\"," },
};

/* BME280 and BME680 both report humidity. */
static const ha_entity_t k_ent_hum[] = {
    { "humidity", "Humidity", "humidity",
      "\"unit_of_measurement\":\"%\",\"device_class\":\"humidity\","
      "\"state_class\":\"measurement\"," },
    { "dew_point", "Dew Point", "dew_point",
      "\"unit_of_measurement\":\"\xc2\xb0" "C\",\"device_class\":\"temperature\","
      "\"state_class\":\"measurement\"," },
};

/* BME680 only (raw compensated gas resistance, ohms). */
static const ha_entity_t k_ent_gas[] = {
    { "gas_resistance", "Gas Resistance", "gas_resistance",
      "\"unit_of_measurement\":\"\xce\xa9\",\"state_class\":\"measurement\"," },
};

/* BME680 with BSEC active only. */
static const ha_entity_t k_ent_bsec[] = {
    { "iaq", "IAQ", "iaq", "\"state_class\":\"measurement\"," },
    { "static_iaq", "Static IAQ", "static_iaq", "\"state_class\":\"measurement\"," },
    { "iaq_accuracy", "IAQ Accuracy", "iaq_accuracy",
      "\"state_class\":\"measurement\",\"entity_category\":\"diagnostic\"," },
    { "co2_eq", "CO2 Equivalent", "co2_eq",
      "\"unit_of_measurement\":\"ppm\",\"device_class\":\"carbon_dioxide\","
      "\"state_class\":\"measurement\"," },
    { "voc_eq", "VOC Equivalent", "voc_eq",
      "\"unit_of_measurement\":\"ppm\","
      "\"device_class\":\"volatile_organic_compounds_parts\","
      "\"state_class\":\"measurement\"," },
};

static void publish_entity_config(const ha_entity_t *e)
{
    char topic[96];
    snprintf(topic, sizeof(topic), HA_DISCOVERY_PREFIX "/sensor/%s/%s/config",
             s_dev_id, e->object_id);

    char payload[640];
    int len = snprintf(payload, sizeof(payload),
        "{\"name\":\"%s\",\"unique_id\":\"%s_%s\","
        "\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.%s }}\","
        "%s"
        "\"qos\":1,"
        "\"availability_topic\":\"%s\","
        "\"payload_available\":\"online\",\"payload_not_available\":\"offline\","
        "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"SensorStation3\","
        "\"manufacturer\":\"ScorpionZZZ\",\"model\":\"CYD ESP32-2432S028\","
        "\"sw_version\":\"%s\"}}",
        e->name, s_dev_id, e->object_id,
        s_state_topic, e->value_key,
        e->extra_json,
        s_avail_topic,
        s_dev_id, APP_VERSION_STR);

    if (len <= 0 || len >= (int)sizeof(payload)) return;
    esp_mqtt_client_publish(s_client, topic, payload, len, /*qos*/1, /*retain*/1);
}

/* Republished (idempotent, retained) on every connect — cheap, and keeps
 * Home Assistant in sync after firmware updates or a sensor swap. */
static void publish_discovery(void)
{
    bool has_hum = false, has_gas = false, has_bsec = false;
    bmx280_type_t type = bmx280_get_type();

#if CONFIG_SS3_USE_BSEC
    has_bsec = (type == BMX280_TYPE_BME680) && bsec_sensor_is_active();
#endif
    if (has_bsec) {
        has_hum = true;
        has_gas = true;
    } else {
        bmx280_data_t bme;
        if (bmx280_read(&bme) == ESP_OK) {
            has_hum = !isnan(bme.humidity);
            has_gas = (type == BMX280_TYPE_BME680) && !isnan(bme.gas_resistance);
        }
    }

    for (size_t i = 0; i < sizeof(k_ent_common) / sizeof(k_ent_common[0]); i++)
        publish_entity_config(&k_ent_common[i]);
    if (has_hum)
        for (size_t i = 0; i < sizeof(k_ent_hum) / sizeof(k_ent_hum[0]); i++)
            publish_entity_config(&k_ent_hum[i]);
    if (has_gas)
        for (size_t i = 0; i < sizeof(k_ent_gas) / sizeof(k_ent_gas[0]); i++)
            publish_entity_config(&k_ent_gas[i]);
    if (has_bsec)
        for (size_t i = 0; i < sizeof(k_ent_bsec) / sizeof(k_ent_bsec[0]); i++)
            publish_entity_config(&k_ent_bsec[i]);

    ESP_LOGI(TAG, "discovery published (hum=%d gas=%d bsec=%d)",
             has_hum, has_gas, has_bsec);
}

/* ── state ────────────────────────────────────────────────────────────── */

static void publish_state(void *arg)
{
    (void)arg;
    if (!s_connected) return;

    photores_data_t ldr = {0};
    photores_read(&ldr);

    int8_t rssi = 0;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) rssi = ap.rssi;

    int64_t uptime_s = esp_timer_get_time() / 1000000LL;
    uint8_t bl_pct   = (uint8_t)((uint32_t)display_get_backlight() * 100u / 255u);

    char buf[512];
    int  len;

    /* Field set mirrors tb_mqtt.c's publish_telemetry() branching so the
     * same value_template keys work regardless of sensor/BSEC availability. */
#if CONFIG_SS3_USE_BSEC
    if (bmx280_get_type() == BMX280_TYPE_BME680 && bsec_sensor_is_active()) {
        bsec_data_t bsec;
        if (bsec_sensor_read(&bsec) != ESP_OK) return;
        float dp = dew_point(bsec.temperature, bsec.humidity);
        len = snprintf(buf, sizeof(buf),
                       "{\"temperature\":%.1f,\"humidity\":%.1f,"
                       "\"pressure\":%.1f,\"dew_point\":%.1f,"
                       "\"iaq\":%.1f,\"static_iaq\":%.1f,\"iaq_accuracy\":%u,"
                       "\"co2_eq\":%.1f,\"voc_eq\":%.3f,\"gas_resistance\":%.0f,"
                       "\"uptime\":%lld,\"rssi\":%d,\"backlight\":%u,\"ldr_mv\":%d}",
                       bsec.temperature, bsec.humidity,
                       bsec.pressure / 100.0f, dp,
                       bsec.iaq, bsec.static_iaq, (unsigned)bsec.iaq_accuracy,
                       bsec.co2_eq, bsec.voc_eq, bsec.gas_resistance,
                       (long long)uptime_s,
                       (int)rssi, (unsigned)bl_pct, ldr.voltage_mv);
    } else
#endif
    {
        bmx280_data_t bme;
        if (bmx280_read(&bme) != ESP_OK) return;

        bool has_hum = !isnan(bme.humidity);
        if (has_hum) {
            float dp = dew_point(bme.temperature, bme.humidity);
            bool has_gas = bmx280_get_type() == BMX280_TYPE_BME680 &&
                           !isnan(bme.gas_resistance);
            if (has_gas) {
                len = snprintf(buf, sizeof(buf),
                               "{\"temperature\":%.1f,\"humidity\":%.1f,"
                               "\"pressure\":%.1f,\"dew_point\":%.1f,"
                               "\"gas_resistance\":%.0f,"
                               "\"uptime\":%lld,\"rssi\":%d,\"backlight\":%u,\"ldr_mv\":%d}",
                               bme.temperature, bme.humidity,
                               bme.pressure / 100.0f, dp, bme.gas_resistance,
                               (long long)uptime_s,
                               (int)rssi, (unsigned)bl_pct, ldr.voltage_mv);
            } else {
                len = snprintf(buf, sizeof(buf),
                               "{\"temperature\":%.1f,\"humidity\":%.1f,"
                               "\"pressure\":%.1f,\"dew_point\":%.1f,"
                               "\"uptime\":%lld,\"rssi\":%d,\"backlight\":%u,\"ldr_mv\":%d}",
                               bme.temperature, bme.humidity,
                               bme.pressure / 100.0f, dp,
                               (long long)uptime_s,
                               (int)rssi, (unsigned)bl_pct, ldr.voltage_mv);
            }
        } else {
            len = snprintf(buf, sizeof(buf),
                           "{\"temperature\":%.1f,\"pressure\":%.1f,"
                           "\"uptime\":%lld,\"rssi\":%d,\"backlight\":%u,\"ldr_mv\":%d}",
                           bme.temperature, bme.pressure / 100.0f,
                           (long long)uptime_s,
                           (int)rssi, (unsigned)bl_pct, ldr.voltage_mv);
        }
    }

    if (len <= 0 || len >= (int)sizeof(buf)) return;
    esp_mqtt_client_publish(s_client, s_state_topic, buf, len, /*qos*/1, /*retain*/0);
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
        esp_mqtt_client_publish(s_client, s_avail_topic, "online", 0,
                                /*qos*/1, /*retain*/1);
        publish_discovery();
        esp_timer_start_periodic(s_timer, HA_INTERVAL_US);
        publish_state(NULL);
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
    ensure_device_id();

    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READONLY, &h), TAG, "nvs_open");

    memset(s_uri,  0, sizeof(s_uri));
    memset(s_user, 0, sizeof(s_user));
    memset(s_pass, 0, sizeof(s_pass));
    size_t  uri_len  = sizeof(s_uri);
    size_t  user_len = sizeof(s_user);
    size_t  pass_len = sizeof(s_pass);
    uint8_t enabled  = 0;

    nvs_get_str(h, KEY_HA_URI,     s_uri,  &uri_len);
    nvs_get_u8(h,  KEY_HA_ENABLED, &enabled);
    nvs_get_str(h, KEY_HA_USER,    s_user, &user_len);
    nvs_get_str(h, KEY_HA_PASS,    s_pass, &pass_len);
    nvs_close(h);

    if (!enabled) {
        ESP_LOGI(TAG, "disabled by user setting");
        return ESP_OK;
    }
    if (s_uri[0] == '\0') {
        ESP_LOGI(TAG, "broker URI not set — disabled");
        return ESP_OK;
    }

    esp_timer_create_args_t ta = {
        .callback = publish_state,
        .name     = "ha_pub",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&ta, &s_timer), TAG, "timer_create");

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri                  = s_uri,
        .credentials.client_id                = s_client_id,
        .credentials.username                 = s_user[0] ? s_user : NULL,
        .credentials.authentication.password  = s_pass[0] ? s_pass : NULL,
        .session.last_will = {
            .topic  = s_avail_topic,
            .msg    = "offline",
            .qos    = 1,
            .retain = 1,
        },
    };
    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) return ESP_ERR_NO_MEM;

    ESP_RETURN_ON_ERROR(
        esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                       mqtt_event_handler, NULL),
        TAG, "reg_mqtt");

    ESP_LOGI(TAG, "configured — broker=%s dev_id=%s auth=%s",
             s_uri, s_dev_id, s_user[0] ? "yes" : "no");
    return ESP_OK;
}

/* ── public API ───────────────────────────────────────────────────────── */

bool ha_mqtt_is_connected(void) { return s_connected; }

esp_err_t ha_mqtt_init(void)
{
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            on_got_ip, NULL, NULL),
        TAG, "reg_ip");
    return apply_config();
}

esp_err_t ha_mqtt_start(void)
{
    esp_err_t ret = apply_config();
    if (ret != ESP_OK || !s_client) return ret;
    esp_mqtt_client_start(s_client);
    s_started = true;
    ESP_LOGI(TAG, "client started");
    return ESP_OK;
}

void ha_mqtt_stop(void)
{
    teardown();
    ESP_LOGI(TAG, "stopped");
}