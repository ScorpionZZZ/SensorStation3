// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#pragma once
#include "esp_err.h"
#include <stdbool.h>

#define NVS_SETTINGS_PIN_LEN 4

/* Call once from app_main before nvs_flash or NVS APIs are used.
 * Initialises the NVS partition and writes the default PIN ("1234")
 * if no PIN is stored yet. */
esp_err_t nvs_settings_init(void);

/* Read the stored PIN into out (must be at least NVS_SETTINGS_PIN_LEN+1 bytes). */
esp_err_t nvs_settings_get_pin(char *out, size_t len);

/* Write a new PIN to NVS. pin must be exactly NVS_SETTINGS_PIN_LEN digits. */
esp_err_t nvs_settings_set_pin(const char *pin);

#define NVS_SETTINGS_SSID_LEN  32
#define NVS_SETTINGS_PASS_LEN  64

/* WiFi credentials — return ESP_ERR_NVS_NOT_FOUND if not set yet. */
esp_err_t nvs_settings_get_wifi_ssid(char *out, size_t len);
esp_err_t nvs_settings_set_wifi_ssid(const char *ssid);
esp_err_t nvs_settings_get_wifi_pass(char *out, size_t len);
esp_err_t nvs_settings_set_wifi_pass(const char *pass);

#define NVS_SETTINGS_TZ_LEN  64
#define NVS_SETTINGS_TZ_DEFAULT "CET-1CEST,M3.5.0,M10.5.0/3"

/* Timezone as POSIX TZ string. Returns default if not set. */
esp_err_t nvs_settings_get_tz(char *out, size_t len);
esp_err_t nvs_settings_set_tz(const char *tz);

/* ThingsBoard MQTT.  If tb_uri or tb_token are empty, MQTT is disabled. */
#define NVS_SETTINGS_TB_URI_LEN          127
#define NVS_SETTINGS_TB_URI_DEFAULT      "mqtt://demo.thingsboard.io"
#define NVS_SETTINGS_TB_TOKEN_LEN        63
#define NVS_SETTINGS_TB_INTERVAL_DEFAULT 30u   /* seconds */

esp_err_t nvs_settings_get_tb_uri(char *out, size_t len);
esp_err_t nvs_settings_set_tb_uri(const char *uri);
esp_err_t nvs_settings_get_tb_token(char *out, size_t len);
esp_err_t nvs_settings_set_tb_token(const char *token);
esp_err_t nvs_settings_get_tb_interval(uint16_t *out);
esp_err_t nvs_settings_set_tb_interval(uint16_t seconds);

/* Master on/off switch for telemetry publishing. Default: enabled. */
esp_err_t nvs_settings_get_tb_enabled(bool *out);
esp_err_t nvs_settings_set_tb_enabled(bool enabled);

/* OTA firmware URL. Returns the default (this project's update server) if
 * never explicitly set. */
#define NVS_SETTINGS_OTA_URL_LEN 255
#define NVS_SETTINGS_OTA_URL_DEFAULT \
    "http://iot.scorpionzzz.com/sensorstation3/firmware/sensorstation3.latest.bin"
esp_err_t nvs_settings_get_ota_url(char *out, size_t len);
esp_err_t nvs_settings_set_ota_url(const char *url);

/* BME680 temperature offset in °C (compensates residual self-heating).
 * Stored as int16 tenths-of-degree; default -0.5°C. */
esp_err_t nvs_settings_get_bme680_t_off(float *out);
esp_err_t nvs_settings_set_bme680_t_off(float offset);

/* OTA auto-update: install update automatically as soon as it is detected.
 * Default: false (off). */
esp_err_t nvs_settings_get_ota_auto(bool *out);
esp_err_t nvs_settings_set_ota_auto(bool enabled);

/* Domoticz MQTT.  Empty URI or disabled means Domoticz publishing is off.
 * dz_thp_idx: virtual device IDX for Temp+Hum+Baro (0 = not set).
 * dz_co2_idx: virtual device IDX for Air Quality / CO2eq (0 = skip).
 * dz_user / dz_pass: MQTT credentials; empty strings = anonymous. */
#define NVS_SETTINGS_DZ_URI_LEN  127
#define NVS_SETTINGS_DZ_USER_LEN  63
#define NVS_SETTINGS_DZ_PASS_LEN  63

esp_err_t nvs_settings_get_dz_uri(char *out, size_t len);
esp_err_t nvs_settings_set_dz_uri(const char *uri);
esp_err_t nvs_settings_get_dz_enabled(bool *out);
esp_err_t nvs_settings_set_dz_enabled(bool enabled);
esp_err_t nvs_settings_get_dz_thp_idx(uint16_t *out);
esp_err_t nvs_settings_set_dz_thp_idx(uint16_t idx);
esp_err_t nvs_settings_get_dz_co2_idx(uint16_t *out);
esp_err_t nvs_settings_set_dz_co2_idx(uint16_t idx);
esp_err_t nvs_settings_get_dz_user(char *out, size_t len);
esp_err_t nvs_settings_set_dz_user(const char *user);
esp_err_t nvs_settings_get_dz_pass(char *out, size_t len);
esp_err_t nvs_settings_set_dz_pass(const char *pass);

/* Home Assistant MQTT Discovery. Empty URI or disabled means publishing
 * is off. ha_user / ha_pass: MQTT credentials; empty strings = anonymous. */
#define NVS_SETTINGS_HA_URI_LEN  127
#define NVS_SETTINGS_HA_USER_LEN  63
#define NVS_SETTINGS_HA_PASS_LEN  63

esp_err_t nvs_settings_get_ha_uri(char *out, size_t len);
esp_err_t nvs_settings_set_ha_uri(const char *uri);
esp_err_t nvs_settings_get_ha_enabled(bool *out);
esp_err_t nvs_settings_set_ha_enabled(bool enabled);
esp_err_t nvs_settings_get_ha_user(char *out, size_t len);
esp_err_t nvs_settings_set_ha_user(const char *user);
esp_err_t nvs_settings_get_ha_pass(char *out, size_t len);
esp_err_t nvs_settings_set_ha_pass(const char *pass);

/* Display unit for temperature readings on the main screen. Default: false
 * (Celsius). Does not affect MQTT telemetry, which always publishes °C. */
esp_err_t nvs_settings_get_use_fahrenheit(bool *out);
esp_err_t nvs_settings_set_use_fahrenheit(bool enabled);

/* 24h history backfill: selects which backend components/history_fetch/
 * pulls 24h temp/humidity history from, once, right after boot, to fill the
 * main-screen charts instead of starting empty. Default: none (off).
 * ThingsBoard, Domoticz, and Home Assistant are all implemented. */
#define NVS_SETTINGS_HIST_SOURCE_NONE     0
#define NVS_SETTINGS_HIST_SOURCE_TB       1
#define NVS_SETTINGS_HIST_SOURCE_DOMOTICZ 2
#define NVS_SETTINGS_HIST_SOURCE_HA       3

esp_err_t nvs_settings_get_hist_source(uint8_t *out);
esp_err_t nvs_settings_set_hist_source(uint8_t source);

/* Domoticz JSON/web API base URL for the 24h history fetch, e.g.
 * "http://192.168.1.50:8080" — distinct from dz_uri, which is the MQTT
 * broker URI on a different port. Empty ⇒ history fetch skipped. Uses
 * dz_thp_idx to select the device and dz_http_user/dz_http_pass (below,
 * NOT dz_user/dz_pass — Domoticz's web/JSON login is a separate credential
 * from its MQTT gateway login) for HTTP Basic auth. */
#define NVS_SETTINGS_DZ_HTTP_URL_LEN 127
esp_err_t nvs_settings_get_dz_http_url(char *out, size_t len);
esp_err_t nvs_settings_set_dz_http_url(const char *url);

/* Domoticz web/JSON API credentials for HTTP Basic auth (Settings → Security
 * on the Domoticz dashboard) — separate from dz_user/dz_pass, which
 * authenticate against the MQTT broker instead. Empty ⇒ anonymous. */
#define NVS_SETTINGS_DZ_HTTP_USER_LEN 63
#define NVS_SETTINGS_DZ_HTTP_PASS_LEN 63
esp_err_t nvs_settings_get_dz_http_user(char *out, size_t len);
esp_err_t nvs_settings_set_dz_http_user(const char *user);
esp_err_t nvs_settings_get_dz_http_pass(char *out, size_t len);
esp_err_t nvs_settings_set_dz_http_pass(const char *pass);

/* ThingsBoard REST API base URL for the 24h history fetch, e.g.
 * "https://tb0.scorpionzzz.com" — distinct from tb_uri, which is the MQTT
 * broker URI on a different port/scheme. Empty ⇒ history fetch skipped.
 * ThingsBoard's device access token (tb_token) authenticates MQTT publishes
 * only — it cannot read historical telemetry over REST, which requires a
 * full user login (tb_rest_user/tb_rest_pass), NOT the device token. */
#define NVS_SETTINGS_TB_REST_URL_LEN 127
esp_err_t nvs_settings_get_tb_rest_url(char *out, size_t len);
esp_err_t nvs_settings_set_tb_rest_url(const char *url);

/* ThingsBoard user login for the REST API (POST /api/auth/login) — a
 * dashboard/tenant user account, not the device access token above. */
#define NVS_SETTINGS_TB_REST_USER_LEN 63
#define NVS_SETTINGS_TB_REST_PASS_LEN 63
esp_err_t nvs_settings_get_tb_rest_user(char *out, size_t len);
esp_err_t nvs_settings_set_tb_rest_user(const char *user);
esp_err_t nvs_settings_get_tb_rest_pass(char *out, size_t len);
esp_err_t nvs_settings_set_tb_rest_pass(const char *pass);

/* ThingsBoard device UUID (Device Details in the TB dashboard — not the
 * access token) — the telemetry history endpoint is keyed by this, not by
 * access token. */
#define NVS_SETTINGS_TB_DEVICE_ID_LEN 63
esp_err_t nvs_settings_get_tb_device_id(char *out, size_t len);
esp_err_t nvs_settings_set_tb_device_id(const char *id);

/* Home Assistant REST API base URL for the 24h history fetch, e.g.
 * "http://homeassistant.local:8123" — distinct from ha_uri, which is the
 * MQTT broker URI on a different port. Empty ⇒ history fetch skipped. */
#define NVS_SETTINGS_HA_HTTP_URL_LEN 127
esp_err_t nvs_settings_get_ha_http_url(char *out, size_t len);
esp_err_t nvs_settings_set_ha_http_url(const char *url);

/* Home Assistant Long-Lived Access Token (Profile → Security → Long-Lived
 * Access Tokens in Home Assistant) — a separate credential from ha_user/
 * ha_pass, which authenticate the MQTT broker connection instead. Sent as
 * an "Authorization: Bearer <token>" header on the history REST call. */
#define NVS_SETTINGS_HA_HTTP_TOKEN_LEN 255
esp_err_t nvs_settings_get_ha_http_token(char *out, size_t len);
esp_err_t nvs_settings_set_ha_http_token(const char *token);

/* entity_id of the temperature/humidity sensors to pull history for
 * (Settings → Devices & Services → Entities in Home Assistant — copy the
 * entity ID shown there, e.g. "sensor.ss3_aabbccddeeff_temperature"; the
 * exact id depends on how the device/entities were named in HA, so this
 * isn't derived automatically). Temperature is required for the fetch to
 * run; humidity may be left empty for sensors with no humidity channel. */
#define NVS_SETTINGS_HA_TEMP_ENTITY_LEN 63
#define NVS_SETTINGS_HA_HUM_ENTITY_LEN  63
esp_err_t nvs_settings_get_ha_temp_entity(char *out, size_t len);
esp_err_t nvs_settings_set_ha_temp_entity(const char *entity_id);
esp_err_t nvs_settings_get_ha_hum_entity(char *out, size_t len);
esp_err_t nvs_settings_set_ha_hum_entity(const char *entity_id);