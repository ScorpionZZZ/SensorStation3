// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "nvs_settings.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "nvs_settings";

#define NVS_NS        "settings"
#define KEY_PIN       "pin"
#define KEY_WIFI_SSID "wifi_ssid"
#define KEY_WIFI_PASS "wifi_pass"
#define KEY_TZ        "tz"
#define KEY_TB_URI    "tb_uri"
#define KEY_TB_TOKEN  "tb_token"
#define KEY_TB_INTERV "tb_interval"
#define KEY_TB_ENABLED "tb_enabled"
#define KEY_OTA_URL      "ota_url"
#define KEY_OTA_AUTO     "ota_auto"
#define KEY_BME680_T_OFF "bme680_toff"
#define KEY_DZ_URI       "dz_uri"
#define KEY_DZ_ENABLED   "dz_enabled"
#define KEY_DZ_THP_IDX   "dz_thp_idx"
#define KEY_DZ_CO2_IDX   "dz_co2_idx"
#define KEY_DZ_USER      "dz_user"
#define KEY_DZ_PASS      "dz_pass"
#define DEFAULT_PIN      "1234"
/* -0.5°C expressed as tenths — compensates residual self-heating at 3% duty */
#define BME680_T_OFF_DEFAULT_TENTHS ((int16_t)(-5))

esp_err_t nvs_settings_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition corrupt, erasing");
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase");
        ret = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "init");

    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "open");

    char buf[NVS_SETTINGS_PIN_LEN + 1];
    size_t len = sizeof(buf);
    if (nvs_get_str(h, KEY_PIN, buf, &len) == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "PIN absent, writing default");
        nvs_set_str(h, KEY_PIN, DEFAULT_PIN);
        nvs_commit(h);
    }

    char tb_uri[NVS_SETTINGS_TB_URI_LEN + 1];
    size_t tb_uri_len = sizeof(tb_uri);
    if (nvs_get_str(h, KEY_TB_URI, tb_uri, &tb_uri_len) == ESP_ERR_NVS_NOT_FOUND) {
        nvs_set_str(h, KEY_TB_URI, NVS_SETTINGS_TB_URI_DEFAULT);
        nvs_commit(h);
    }

    uint16_t tb_interval;
    if (nvs_get_u16(h, KEY_TB_INTERV, &tb_interval) == ESP_ERR_NVS_NOT_FOUND) {
        nvs_set_u16(h, KEY_TB_INTERV, (uint16_t)NVS_SETTINGS_TB_INTERVAL_DEFAULT);
        nvs_commit(h);
    }

    nvs_close(h);
    return ESP_OK;
}

esp_err_t nvs_settings_get_pin(char *out, size_t len)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READONLY, &h), TAG, "open");
    esp_err_t ret = nvs_get_str(h, KEY_PIN, out, &len);
    nvs_close(h);
    return ret;
}

esp_err_t nvs_settings_set_pin(const char *pin)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "open");
    esp_err_t ret = nvs_set_str(h, KEY_PIN, pin);
    if (ret == ESP_OK) ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}

esp_err_t nvs_settings_get_wifi_ssid(char *out, size_t len)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READONLY, &h), TAG, "open");
    esp_err_t ret = nvs_get_str(h, KEY_WIFI_SSID, out, &len);
    nvs_close(h);
    return ret;
}

esp_err_t nvs_settings_set_wifi_ssid(const char *ssid)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "open");
    esp_err_t ret = nvs_set_str(h, KEY_WIFI_SSID, ssid);
    if (ret == ESP_OK) ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}

esp_err_t nvs_settings_get_wifi_pass(char *out, size_t len)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READONLY, &h), TAG, "open");
    esp_err_t ret = nvs_get_str(h, KEY_WIFI_PASS, out, &len);
    nvs_close(h);
    return ret;
}

esp_err_t nvs_settings_set_wifi_pass(const char *pass)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "open");
    esp_err_t ret = nvs_set_str(h, KEY_WIFI_PASS, pass);
    if (ret == ESP_OK) ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}

esp_err_t nvs_settings_get_tz(char *out, size_t len)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READONLY, &h), TAG, "open");
    esp_err_t ret = nvs_get_str(h, KEY_TZ, out, &len);
    nvs_close(h);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        strncpy(out, NVS_SETTINGS_TZ_DEFAULT, len - 1);
        out[len - 1] = '\0';
        return ESP_OK;
    }
    return ret;
}

esp_err_t nvs_settings_set_tz(const char *tz)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "open");
    esp_err_t ret = nvs_set_str(h, KEY_TZ, tz);
    if (ret == ESP_OK) ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}

esp_err_t nvs_settings_get_tb_uri(char *out, size_t len)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READONLY, &h), TAG, "open");
    esp_err_t ret = nvs_get_str(h, KEY_TB_URI, out, &len);
    nvs_close(h);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        strncpy(out, NVS_SETTINGS_TB_URI_DEFAULT, len - 1);
        out[len - 1] = '\0';
        return ESP_OK;
    }
    return ret;
}

esp_err_t nvs_settings_set_tb_uri(const char *uri)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "open");
    esp_err_t ret = nvs_set_str(h, KEY_TB_URI, uri);
    if (ret == ESP_OK) ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}

esp_err_t nvs_settings_get_tb_token(char *out, size_t len)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READONLY, &h), TAG, "open");
    esp_err_t ret = nvs_get_str(h, KEY_TB_TOKEN, out, &len);
    nvs_close(h);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        out[0] = '\0';
        return ESP_OK;
    }
    return ret;
}

esp_err_t nvs_settings_set_tb_token(const char *token)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "open");
    esp_err_t ret = nvs_set_str(h, KEY_TB_TOKEN, token);
    if (ret == ESP_OK) ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}

esp_err_t nvs_settings_get_tb_interval(uint16_t *out)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READONLY, &h), TAG, "open");
    esp_err_t ret = nvs_get_u16(h, KEY_TB_INTERV, out);
    nvs_close(h);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        *out = (uint16_t)NVS_SETTINGS_TB_INTERVAL_DEFAULT;
        return ESP_OK;
    }
    return ret;
}

esp_err_t nvs_settings_set_tb_interval(uint16_t seconds)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "open");
    esp_err_t ret = nvs_set_u16(h, KEY_TB_INTERV, seconds);
    if (ret == ESP_OK) ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}

esp_err_t nvs_settings_get_tb_enabled(bool *out)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READONLY, &h), TAG, "open");
    uint8_t v = 1;
    esp_err_t ret = nvs_get_u8(h, KEY_TB_ENABLED, &v);
    nvs_close(h);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        *out = true;
        return ESP_OK;
    }
    *out = (v != 0);
    return ret;
}

esp_err_t nvs_settings_set_tb_enabled(bool enabled)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "open");
    esp_err_t ret = nvs_set_u8(h, KEY_TB_ENABLED, enabled ? 1 : 0);
    if (ret == ESP_OK) ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}

esp_err_t nvs_settings_get_ota_url(char *out, size_t len)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READONLY, &h), TAG, "open");
    esp_err_t ret = nvs_get_str(h, KEY_OTA_URL, out, &len);
    nvs_close(h);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        out[0] = '\0';
        return ESP_OK;
    }
    return ret;
}

esp_err_t nvs_settings_set_ota_url(const char *url)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "open");
    esp_err_t ret = nvs_set_str(h, KEY_OTA_URL, url);
    if (ret == ESP_OK) ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}

esp_err_t nvs_settings_get_ota_auto(bool *out)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READONLY, &h), TAG, "open");
    uint8_t v = 0;
    esp_err_t ret = nvs_get_u8(h, KEY_OTA_AUTO, &v);
    nvs_close(h);
    if (ret == ESP_ERR_NVS_NOT_FOUND) { *out = false; return ESP_OK; }
    *out = (v != 0);
    return ret;
}

esp_err_t nvs_settings_set_ota_auto(bool enabled)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "open");
    esp_err_t ret = nvs_set_u8(h, KEY_OTA_AUTO, enabled ? 1 : 0);
    if (ret == ESP_OK) ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}

esp_err_t nvs_settings_get_bme680_t_off(float *out)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READONLY, &h), TAG, "open");
    int16_t v = BME680_T_OFF_DEFAULT_TENTHS;
    esp_err_t ret = nvs_get_i16(h, KEY_BME680_T_OFF, &v);
    nvs_close(h);
    if (ret == ESP_ERR_NVS_NOT_FOUND) ret = ESP_OK;
    *out = (float)v / 10.0f;
    return ret;
}

esp_err_t nvs_settings_set_bme680_t_off(float offset)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "open");
    int16_t v = (int16_t)(offset * 10.0f);
    esp_err_t ret = nvs_set_i16(h, KEY_BME680_T_OFF, v);
    if (ret == ESP_OK) ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}

esp_err_t nvs_settings_get_dz_uri(char *out, size_t len)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READONLY, &h), TAG, "open");
    esp_err_t ret = nvs_get_str(h, KEY_DZ_URI, out, &len);
    nvs_close(h);
    if (ret == ESP_ERR_NVS_NOT_FOUND) { out[0] = '\0'; return ESP_OK; }
    return ret;
}

esp_err_t nvs_settings_set_dz_uri(const char *uri)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "open");
    esp_err_t ret = nvs_set_str(h, KEY_DZ_URI, uri);
    if (ret == ESP_OK) ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}

esp_err_t nvs_settings_get_dz_enabled(bool *out)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READONLY, &h), TAG, "open");
    uint8_t v = 0;
    esp_err_t ret = nvs_get_u8(h, KEY_DZ_ENABLED, &v);
    nvs_close(h);
    if (ret == ESP_ERR_NVS_NOT_FOUND) { *out = false; return ESP_OK; }
    *out = (v != 0);
    return ret;
}

esp_err_t nvs_settings_set_dz_enabled(bool enabled)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "open");
    esp_err_t ret = nvs_set_u8(h, KEY_DZ_ENABLED, enabled ? 1 : 0);
    if (ret == ESP_OK) ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}

esp_err_t nvs_settings_get_dz_thp_idx(uint16_t *out)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READONLY, &h), TAG, "open");
    esp_err_t ret = nvs_get_u16(h, KEY_DZ_THP_IDX, out);
    nvs_close(h);
    if (ret == ESP_ERR_NVS_NOT_FOUND) { *out = 0; return ESP_OK; }
    return ret;
}

esp_err_t nvs_settings_set_dz_thp_idx(uint16_t idx)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "open");
    esp_err_t ret = nvs_set_u16(h, KEY_DZ_THP_IDX, idx);
    if (ret == ESP_OK) ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}

esp_err_t nvs_settings_get_dz_co2_idx(uint16_t *out)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READONLY, &h), TAG, "open");
    esp_err_t ret = nvs_get_u16(h, KEY_DZ_CO2_IDX, out);
    nvs_close(h);
    if (ret == ESP_ERR_NVS_NOT_FOUND) { *out = 0; return ESP_OK; }
    return ret;
}

esp_err_t nvs_settings_set_dz_co2_idx(uint16_t idx)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "open");
    esp_err_t ret = nvs_set_u16(h, KEY_DZ_CO2_IDX, idx);
    if (ret == ESP_OK) ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}

esp_err_t nvs_settings_get_dz_user(char *out, size_t len)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READONLY, &h), TAG, "open");
    esp_err_t ret = nvs_get_str(h, KEY_DZ_USER, out, &len);
    nvs_close(h);
    if (ret == ESP_ERR_NVS_NOT_FOUND) { out[0] = '\0'; return ESP_OK; }
    return ret;
}

esp_err_t nvs_settings_set_dz_user(const char *user)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "open");
    esp_err_t ret = nvs_set_str(h, KEY_DZ_USER, user);
    if (ret == ESP_OK) ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}

esp_err_t nvs_settings_get_dz_pass(char *out, size_t len)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READONLY, &h), TAG, "open");
    esp_err_t ret = nvs_get_str(h, KEY_DZ_PASS, out, &len);
    nvs_close(h);
    if (ret == ESP_ERR_NVS_NOT_FOUND) { out[0] = '\0'; return ESP_OK; }
    return ret;
}

esp_err_t nvs_settings_set_dz_pass(const char *pass)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "open");
    esp_err_t ret = nvs_set_str(h, KEY_DZ_PASS, pass);
    if (ret == ESP_OK) ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}