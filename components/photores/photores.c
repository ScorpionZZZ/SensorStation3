// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "photores.h"
#include "board_config.h"

#include "esp_log.h"
#include "esp_check.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"  /* line-fitting scheme */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

// TODO: move this values to settings
#define CLAMP_VALUE              600

static const char *TAG = "photores";

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t         s_cali;
static bool                      s_cali_ok;
static SemaphoreHandle_t         s_mutex;
static photores_data_t           s_latest;
static QueueHandle_t             s_brightness_q;

static void photores_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "start");
    while (1) {
        int raw;
        if (adc_oneshot_read(s_adc, CYD_PHOTORES_ADC_CH, &raw) == ESP_OK) {
            photores_data_t sample;
            sample.adc_raw = raw;

            if (s_cali_ok) {
                adc_cali_raw_to_voltage(s_cali, raw, &sample.voltage_mv);
            } else {
                sample.voltage_mv = (int)((int32_t)raw * CYD_PHOTORES_VCC_MV / 4095);
            }

            xSemaphoreTake(s_mutex, portMAX_DELAY);
            s_latest = sample;
            xSemaphoreGive(s_mutex);

            /* raw=0 → 255 (100%), raw≥900 → 13 (5%), linear between */
            int clamped = raw > CLAMP_VALUE ? CLAMP_VALUE : raw;
            uint8_t brightness = (uint8_t)(255 - 242 * clamped / CLAMP_VALUE);
            xQueueOverwrite(s_brightness_q, &brightness);

            ESP_LOGD(TAG, "raw=%d  V=%d mV  bl=%d%%",
                     sample.adc_raw, sample.voltage_mv,
                     (int)brightness * 100 / 255);
        } else {
            ESP_LOGW(TAG, "read failed");
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t photores_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = CYD_PHOTORES_ADC_UNIT,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_cfg, &s_adc), TAG, "adc unit");

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = CYD_PHOTORES_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(
        adc_oneshot_config_channel(s_adc, CYD_PHOTORES_ADC_CH, &chan_cfg),
        TAG, "adc chan");

    /* ESP32 supports line-fitting calibration only (curve-fitting is S2/S3+). */
    adc_cali_line_fitting_config_t lf_cfg = {
        .unit_id  = CYD_PHOTORES_ADC_UNIT,
        .atten    = CYD_PHOTORES_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    s_cali_ok = (adc_cali_create_scheme_line_fitting(&lf_cfg, &s_cali) == ESP_OK);
    ESP_LOGI(TAG, "ADC calibration: %s", s_cali_ok ? "ok" : "none (linear fallback)");

    /* Scan only GPIO34 (CH6) and GPIO35 (CH7) — the two safe LDR candidates.
     * GPIO32/33 (CH4/CH5) are the touch SPI MOSI/CS pins and must NOT be
     * reconfigured as ADC inputs. */
    adc_oneshot_chan_cfg_t scan_cfg = {
        .atten    = CYD_PHOTORES_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    const adc_channel_t scan_chs[]  = { ADC_CHANNEL_6, ADC_CHANNEL_7 };
    const int           scan_gpios[] = { 34, 35 };
    for (int i = 0; i < 2; i++) {
        int v = -1;
        if (adc_oneshot_config_channel(s_adc, scan_chs[i], &scan_cfg) == ESP_OK) {
            adc_oneshot_read(s_adc, scan_chs[i], &v);
        }
        ESP_LOGI(TAG, "scan  GPIO%d (CH%d): raw=%d", scan_gpios[i], scan_chs[i], v);
    }

    s_brightness_q = xQueueCreate(1, sizeof(uint8_t));
    if (!s_brightness_q) return ESP_ERR_NO_MEM;

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    s_latest = (photores_data_t){ 0 };

    BaseType_t r = xTaskCreate(photores_task, "photores", 2048, NULL, 4, NULL);
    if (r != pdPASS) return ESP_ERR_NO_MEM;

    ESP_LOGI(TAG, "ready");
    return ESP_OK;
}

QueueHandle_t photores_brightness_queue(void)
{
    return s_brightness_q;
}

esp_err_t photores_read(photores_data_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_latest;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}