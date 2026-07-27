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

/* OTA firmware URL — empty string if not set. */
#define NVS_SETTINGS_OTA_URL_LEN 255
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