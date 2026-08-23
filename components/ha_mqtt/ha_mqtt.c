// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "ha_mqtt.h"
#include "mqtt_base.h"
#include "mqtt_client.h"
#include "sensor_hub.h"
#include "sensor_history.h"
#include "sensor_json.h"
#include "build_info.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_check.h"
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

/* The MQTT client_id ("ss3-ha-<mac>") is owned by mqtt_base via the id_prefix
 * below; it must differ from tb_mqtt's/domoticz_mqtt's or the broker drops
 * whichever connected first. */
static mqtt_base_t *s_base = NULL;

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
}

/* ── helpers ──────────────────────────────────────────────────────────── */

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

/* BMx280 family only — SCD4x has no barometer. */
static const ha_entity_t k_ent_pressure[] = {
    { "pressure", "Pressure", "pressure",
      "\"unit_of_measurement\":\"hPa\",\"device_class\":\"atmospheric_pressure\","
      "\"state_class\":\"measurement\"," },
};

/* BME280 and BME680 both report humidity; SCD4x does too, when it's the
 * active temp/humidity source (i.e. no BMx280 present). */
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

/* BME680 with BSEC active only (IAQ group — CO2-equivalent is a separate
 * entity below since it is keyed independently in the state payload). */
static const ha_entity_t k_ent_bsec[] = {
    { "iaq", "IAQ", "iaq", "\"state_class\":\"measurement\"," },
    { "static_iaq", "Static IAQ", "static_iaq", "\"state_class\":\"measurement\"," },
    { "iaq_accuracy", "IAQ Accuracy", "iaq_accuracy",
      "\"state_class\":\"measurement\",\"entity_category\":\"diagnostic\"," },
    { "voc_eq", "VOC Equivalent", "voc_eq",
      "\"unit_of_measurement\":\"ppm\","
      "\"device_class\":\"volatile_organic_compounds_parts\","
      "\"state_class\":\"measurement\"," },
};

/* Real NDIR CO2 (SCD4x) — state key "co2". */
static const ha_entity_t k_ent_co2[] = {
    { "co2", "CO2", "co2",
      "\"unit_of_measurement\":\"ppm\",\"device_class\":\"carbon_dioxide\","
      "\"state_class\":\"measurement\"," },
};

/* Estimated CO2-equivalent (BME680, via BSEC or gas resistance) — state key
 * "co2_eq". Mutually exclusive with k_ent_co2: the hub merges to a single CO2
 * value and sensor_json emits exactly one of the two keys. */
static const ha_entity_t k_ent_co2eq[] = {
    { "co2_eq", "CO2 Equivalent", "co2_eq",
      "\"unit_of_measurement\":\"ppm\",\"device_class\":\"carbon_dioxide\","
      "\"state_class\":\"measurement\"," },
};

static void publish_entity_config(esp_mqtt_client_handle_t client, const ha_entity_t *e)
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
    esp_mqtt_client_publish(client, topic, payload, len, /*qos*/1, /*retain*/1);
}

/* Republished (idempotent, retained) on every connect — cheap, and keeps
 * Home Assistant in sync after firmware updates or a sensor swap. */
static void publish_discovery(esp_mqtt_client_handle_t client)
{
    sensor_caps_t caps = sensor_hub_caps();
    bool has_pres = (caps & SENSOR_CAP_PRESSURE) != 0;
    bool has_hum  = (caps & SENSOR_CAP_HUMIDITY) != 0;
    bool has_gas  = (caps & SENSOR_CAP_GAS_RESISTANCE) != 0;
    bool has_bsec = (caps & SENSOR_CAP_IAQ) != 0;
    bool has_co2  = (caps & SENSOR_CAP_CO2) != 0;

    /* One CO2 entity, keyed "co2" (real) or "co2_eq" (equivalent) to match the
     * key sensor_json emits. A BME680 co2 source always carries a gas cap and
     * SCD4x's real CO2 does not, so has_gas is the static tell; the live
     * snapshot is authoritative once CO2 data has arrived. */
    bool co2_equiv = has_gas;
    if (has_co2) {
        sensor_reading_t s;
        if (sensor_history_get_snapshot(&s) && (s.valid & SENSOR_CAP_CO2))
            co2_equiv = s.co2_is_equiv;
    }

    for (size_t i = 0; i < sizeof(k_ent_common) / sizeof(k_ent_common[0]); i++)
        publish_entity_config(client, &k_ent_common[i]);
    if (has_pres)
        for (size_t i = 0; i < sizeof(k_ent_pressure) / sizeof(k_ent_pressure[0]); i++)
            publish_entity_config(client, &k_ent_pressure[i]);
    if (has_hum)
        for (size_t i = 0; i < sizeof(k_ent_hum) / sizeof(k_ent_hum[0]); i++)
            publish_entity_config(client, &k_ent_hum[i]);
    if (has_gas)
        for (size_t i = 0; i < sizeof(k_ent_gas) / sizeof(k_ent_gas[0]); i++)
            publish_entity_config(client, &k_ent_gas[i]);
    if (has_bsec)
        for (size_t i = 0; i < sizeof(k_ent_bsec) / sizeof(k_ent_bsec[0]); i++)
            publish_entity_config(client, &k_ent_bsec[i]);
    if (has_co2) {
        if (co2_equiv)
            for (size_t i = 0; i < sizeof(k_ent_co2eq) / sizeof(k_ent_co2eq[0]); i++)
                publish_entity_config(client, &k_ent_co2eq[i]);
        else
            for (size_t i = 0; i < sizeof(k_ent_co2) / sizeof(k_ent_co2[0]); i++)
                publish_entity_config(client, &k_ent_co2[i]);
    }

    ESP_LOGI(TAG, "discovery published (hum=%d pres=%d gas=%d bsec=%d co2=%d equiv=%d)",
             has_hum, has_pres, has_gas, has_bsec, has_co2, co2_equiv);
}

/* ── state (mqtt_base on_publish_tick) ────────────────────────────────────
 * Runs in the esp_timer task, once on connect then every HA_INTERVAL_US. */
static void ha_tick(esp_mqtt_client_handle_t client, void *ctx)
{
    (void)ctx;

    /* Sensor values: rolling-average temperature/humidity/pressure/CO2 from the
     * loop buffers plus live gas_resistance/iaq/voc from the hub. The emitted
     * keys match the discovery value_templates registered in publish_discovery(). */
    sensor_reading_t s;
    char fields[256];
    char diag[128];
    if (!sensor_history_get_snapshot(&s) ||
        sensor_json_fields(fields, sizeof(fields), &s) <= 0 ||
        mqtt_diag_fields(diag, sizeof(diag)) <= 0)
        return;

    char buf[512];
    int  len = snprintf(buf, sizeof(buf), "{%s,%s}", fields, diag);
    if (len <= 0 || len >= (int)sizeof(buf)) return;
    esp_mqtt_client_publish(client, s_state_topic, buf, len, /*qos*/1, /*retain*/0);
}

/* ── connect (mqtt_base on_connected) ─────────────────────────────────────
 * Announce availability, then (re)publish retained discovery — in that order,
 * before the first state sample the base fires immediately after this. */
static void ha_on_connected(esp_mqtt_client_handle_t client, void *ctx)
{
    (void)ctx;
    esp_mqtt_client_publish(client, s_avail_topic, "online", 0,
                            /*qos*/1, /*retain*/1);
    publish_discovery(client);
}

/* ── internal: read NVS + (re)configure the shared client ──────────────── */

static esp_err_t apply_config(void)
{
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
        return mqtt_base_configure(s_base, NULL);
    }
    if (s_uri[0] == '\0') {
        ESP_LOGI(TAG, "broker URI not set — disabled");
        return mqtt_base_configure(s_base, NULL);
    }

    mqtt_base_conn_t conn = {
        .uri         = s_uri,
        .username    = s_user[0] ? s_user : NULL,
        .password    = s_pass[0] ? s_pass : NULL,
        .interval_us = HA_INTERVAL_US,
        .lwt_topic   = s_avail_topic,
        .lwt_msg     = "offline",
        .lwt_qos     = 1,
        .lwt_retain  = true,
    };
    esp_err_t ret = mqtt_base_configure(s_base, &conn);
    if (ret == ESP_OK)
        ESP_LOGI(TAG, "configured — broker=%s dev_id=%s auth=%s",
                 s_uri, s_dev_id, s_user[0] ? "yes" : "no");
    return ret;
}

/* ── public API ───────────────────────────────────────────────────────── */

bool ha_mqtt_is_connected(void) { return mqtt_base_is_connected(s_base); }

esp_err_t ha_mqtt_init(void)
{
    if (!s_base) {
        ensure_device_id();
        mqtt_base_cfg_t cfg = {
            .tag             = TAG,
            .id_prefix       = "ss3-ha-",
            .on_connected    = ha_on_connected,
            .on_publish_tick = ha_tick,
        };
        s_base = mqtt_base_new(&cfg);
        if (!s_base) return ESP_ERR_NO_MEM;
    }
    return apply_config();
}

esp_err_t ha_mqtt_start(void)
{
    esp_err_t ret = apply_config();
    if (ret != ESP_OK) return ret;
    ret = mqtt_base_start(s_base);
    if (ret == ESP_OK) ESP_LOGI(TAG, "client started");
    return ret;
}

void ha_mqtt_stop(void)
{
    mqtt_base_stop(s_base);
    ESP_LOGI(TAG, "stopped");
}