// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display.h"
#include "bmx280.h"
#include "photores.h"
#include "nvs_settings.h"
#include "wifi_manager.h"
#include "ntp_clock.h"
#include "sensor_history.h"
#include "ui.h"
#include "tb_mqtt.h"
#include "domoticz_mqtt.h"
#if CONFIG_SS3_USE_BSEC
#include "bsec_sensor.h"
#endif
#include "ota_manager.h"
#include "build_info.h"
#include <string.h>

static const char *TAG = "SS3";

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_settings_init());
    ESP_ERROR_CHECK(display_init());

    esp_err_t bmx_ret = bmx280_init();
    if (bmx_ret != ESP_OK) {
        ESP_LOGW(TAG, "BMx280 not available (%s) — running without sensor",
                 esp_err_to_name(bmx_ret));
    } else {
        ESP_ERROR_CHECK(sensor_history_init());
        if (bmx280_get_type() == BMX280_TYPE_BME680) {
            float t_off = 0.0f;
            nvs_settings_get_bme680_t_off(&t_off);
            bmx280_set_temp_offset(t_off);
#if CONFIG_SS3_USE_BSEC
            esp_err_t bsec_ret = bsec_sensor_init();
            if (bsec_ret != ESP_OK) {
                ESP_LOGW(TAG, "BSEC init failed (%s) — BME680 running via bmx280 task",
                         esp_err_to_name(bsec_ret));
            } else {
                bsec_sensor_set_temp_offset(t_off);
            }
#endif
            ESP_LOGI(TAG, "BME680 temp offset: %.1f C", t_off);
        }
    }

    ESP_ERROR_CHECK(photores_init());
    display_attach_brightness_queue(photores_brightness_queue());

    ESP_ERROR_CHECK(wifi_manager_init());
    /* Must run before wifi_manager_connect() so the IP event handler is
     * registered before the first IP_EVENT_STA_GOT_IP fires. */
    ESP_ERROR_CHECK(tb_mqtt_init());
    ESP_ERROR_CHECK(domoticz_mqtt_init());
    /* Auto-connect if credentials were previously saved */
    char wifi_ssid[NVS_SETTINGS_SSID_LEN + 1] = {0};
    char wifi_pass[NVS_SETTINGS_PASS_LEN + 1]  = {0};
    if (nvs_settings_get_wifi_ssid(wifi_ssid, sizeof(wifi_ssid)) == ESP_OK &&
        wifi_ssid[0] != '\0') {
        nvs_settings_get_wifi_pass(wifi_pass, sizeof(wifi_pass));
        wifi_manager_connect(wifi_ssid, wifi_pass);
    }

    /* NTP clock — reads TZ from NVS (default CET-1CEST,M3.5.0,M10.5.0/3) */
    char tz[NVS_SETTINGS_TZ_LEN + 1] = {0};
    nvs_settings_get_tz(tz, sizeof(tz));
    ESP_ERROR_CHECK(ntp_clock_init(tz));

    ESP_ERROR_CHECK(ota_manager_init());
    ota_manager_mark_valid();

    /* Derive check URL from ota_url by replacing .bin -> .json */
    char ota_url[NVS_SETTINGS_OTA_URL_LEN + 1] = {0};
    nvs_settings_get_ota_url(ota_url, sizeof(ota_url));
    if (ota_url[0] != '\0') {
        char check_url[NVS_SETTINGS_OTA_URL_LEN + 1];
        strncpy(check_url, ota_url, sizeof(check_url) - 1);
        check_url[sizeof(check_url) - 1] = '\0';
        char *dot_bin = strstr(check_url, ".bin");
        if (dot_bin && dot_bin[4] == '\0')
            strcpy(dot_bin, ".json");
        ota_manager_check_start(check_url, APP_VERSION_STR);
    }
    bool ota_auto = false;
    nvs_settings_get_ota_auto(&ota_auto);
    ota_manager_set_auto(ota_auto);

    lv_lock();
    ui_init();
    lv_unlock();

    ESP_LOGI(TAG, "ready");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}