// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "bsec_sensor.h"
#include "bmx280.h"
#include "bsec_interface.h"
#include "bsec_datatypes.h"
#include "bsec_iaq.h"

#include <math.h>
#include <string.h>
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs.h"

static const char *TAG = "bsec";

/* ── BME680 register map ────────────────────────────────────────────── */
#define REG_CTRL_GAS_1  0x71
#define REG_CTRL_HUM    0x72
#define REG_CTRL_MEAS   0x74
#define REG_CONFIG      0x75
#define REG_FIELD0      0x1D
#define REG_RES_HEAT_0  0x5A
#define REG_GAS_WAIT_0  0x64
#define REG_CALIB1      0x8A   /* 23 bytes */
#define REG_CALIB2      0xE1   /* 14 bytes */
#define REG_CALIB3      0x00   /* 5 bytes  */

/* ── BME680 calibration (re-read at init; independent of bmx280 internals) */
typedef struct {
    uint16_t par_t1; int16_t  par_t2; int8_t   par_t3;
    uint16_t par_p1; int16_t  par_p2; int8_t   par_p3;
    int16_t  par_p4; int16_t  par_p5; int8_t   par_p6;
    int8_t   par_p7; int16_t  par_p8; int16_t  par_p9; uint8_t par_p10;
    uint16_t par_h1; uint16_t par_h2;
    int8_t   par_h3; int8_t   par_h4; int8_t   par_h5;
    uint8_t  par_h6; int8_t   par_h7;
    int8_t   par_gh1; int16_t par_gh2; int8_t  par_gh3;
    uint8_t  res_heat_range; int8_t res_heat_val; int8_t range_sw_err;
} bme680_calib_t;

/* ── Gas resistance lookup tables (Bosch BME680 datasheet appendix) ── */
static const uint32_t GAS_LUT1[16] = {
    2147483647UL, 2147483647UL, 2147483647UL, 2147483647UL,
    2147483647UL, 2126008810UL, 2147483647UL, 2130303777UL,
    2147483647UL, 2147483647UL, 2143188679UL, 2136746228UL,
    2147483647UL, 2126008810UL, 2147483647UL, 2147483647UL
};
static const uint32_t GAS_LUT2[16] = {
    4096000000UL, 2048000000UL, 1024000000UL,  512000000UL,
     255744255UL,  127110228UL,   64000000UL,   32258064UL,
      16016016UL,    8000000UL,    4000000UL,    2000000UL,
       1000000UL,     500000UL,     250000UL,     125000UL
};

/* ── NVS persistence ────────────────────────────────────────────────── */
#define NVS_NS    "bsec"
#define NVS_KEY   "state"
#define STATE_SAVE_INTERVAL_US  (24ULL * 3600ULL * 1000000ULL)  /* 24 hours */

/* Static work buffers — allocated in BSS to keep stack usage bounded.
 * Only one BSEC task ever runs, so no concurrent access. */
static uint8_t s_work_buf[BSEC_MAX_WORKBUFFER_SIZE];  /* 4096 bytes */
static uint8_t s_state_buf[BSEC_MAX_STATE_BLOB_SIZE]; /* 180 bytes  */

/* ── Module state ───────────────────────────────────────────────────── */
static i2c_master_dev_handle_t  s_dev;
static bme680_calib_t           s_calib;
static SemaphoreHandle_t        s_mutex;
static bsec_data_t              s_latest;
static int64_t                  s_last_save_us;
static volatile float           s_temp_offset = 0.0f;
static volatile bool            s_active = false;

/* ── I2C helpers ────────────────────────────────────────────────────── */

static esp_err_t reg_read(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, 50);
}

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = { reg, val };
    return i2c_master_transmit(s_dev, b, 2, 50);
}

/* ── BME680 calibration load ──────────────────────────────────────── */

static esp_err_t load_calib(void)
{
    uint8_t c1[23];
    if (reg_read(REG_CALIB1, c1, 23) != ESP_OK) return ESP_FAIL;

    s_calib.par_t2  = (int16_t) ((uint16_t)c1[1]  << 8 | c1[0]);
    s_calib.par_t3  = (int8_t)  c1[2];
    s_calib.par_p1  = (uint16_t)((uint16_t)c1[5]  << 8 | c1[4]);
    s_calib.par_p2  = (int16_t) ((uint16_t)c1[7]  << 8 | c1[6]);
    s_calib.par_p3  = (int8_t)  c1[8];
    s_calib.par_p4  = (int16_t) ((uint16_t)c1[11] << 8 | c1[10]);
    s_calib.par_p5  = (int16_t) ((uint16_t)c1[13] << 8 | c1[12]);
    s_calib.par_p7  = (int8_t)  c1[14];
    s_calib.par_p6  = (int8_t)  c1[15];
    s_calib.par_p8  = (int16_t) ((uint16_t)c1[19] << 8 | c1[18]);
    s_calib.par_p9  = (int16_t) ((uint16_t)c1[21] << 8 | c1[20]);
    s_calib.par_p10 = c1[22];

    uint8_t c2[14];
    if (reg_read(REG_CALIB2, c2, 14) != ESP_OK) return ESP_FAIL;

    s_calib.par_h2  = (uint16_t)(((uint16_t)c2[0] << 4) | (c2[1] >> 4));
    s_calib.par_h1  = (uint16_t)(((uint16_t)c2[2] << 4) | (c2[1] & 0x0F));
    s_calib.par_h3  = (int8_t)  c2[3];
    s_calib.par_h4  = (int8_t)  c2[4];
    s_calib.par_h5  = (int8_t)  c2[5];
    s_calib.par_h6  = c2[6];
    s_calib.par_h7  = (int8_t)  c2[7];
    s_calib.par_t1  = (uint16_t)((uint16_t)c2[9]  << 8 | c2[8]);
    s_calib.par_gh2 = (int16_t) ((uint16_t)c2[11] << 8 | c2[10]);
    s_calib.par_gh1 = (int8_t)  c2[12];
    s_calib.par_gh3 = (int8_t)  c2[13];

    uint8_t c3[5];
    if (reg_read(REG_CALIB3, c3, 5) != ESP_OK) return ESP_FAIL;

    s_calib.res_heat_val   = (int8_t) c3[0];
    s_calib.res_heat_range = (c3[2] & 0x30) >> 4;
    s_calib.range_sw_err   = (int8_t) c3[4] >> 4;

    return ESP_OK;
}

/* ── BME680 Bosch compensation formulas (datasheet §4.2) ─────────── */

static float compensate_T(uint32_t adc_T, int32_t *t_fine)
{
    float var1 = (float)adc_T / 16384.0f - (float)s_calib.par_t1 / 1024.0f;
    float var2 = var1 * (float)s_calib.par_t2;
    float var3 = var1 * var1 * (float)s_calib.par_t3 * 16.0f;
    *t_fine = (int32_t)(var2 + var3);
    return (var2 + var3) / 5120.0f;
}

static float compensate_P(uint32_t adc_P, int32_t t_fine)
{
    float var1 = (float)t_fine / 2.0f - 64000.0f;
    float var2 = var1 * var1 * ((float)s_calib.par_p6 / 131072.0f);
    var2 += var1 * (float)s_calib.par_p5 * 2.0f;
    var2  = var2 / 4.0f + (float)s_calib.par_p4 * 65536.0f;
    var1  = (((float)s_calib.par_p3 * var1 * var1 / 16384.0f)
             + ((float)s_calib.par_p2 * var1)) / 524288.0f;
    var1  = (1.0f + var1 / 32768.0f) * (float)s_calib.par_p1;
    if (var1 == 0.0f) return 0.0f;
    float p = 1048576.0f - (float)adc_P;
    p = ((p - var2 / 4096.0f) * 6250.0f) / var1;
    var1 = (float)s_calib.par_p9 * p * p / 2147483648.0f;
    var2 = p * ((float)s_calib.par_p8 / 32768.0f);
    float var3 = (p / 256.0f) * (p / 256.0f) * (p / 256.0f)
                 * ((float)s_calib.par_p10 / 131072.0f);
    return p + (var1 + var2 + var3 + (float)s_calib.par_p7 * 128.0f) / 16.0f;
}

static float compensate_H(uint16_t adc_H, int32_t t_fine)
{
    float tc  = (float)t_fine / 5120.0f;
    float v1  = (float)adc_H
                - ((float)s_calib.par_h1 * 16.0f)
                - (tc * (float)s_calib.par_h3 / 200.0f);
    float v2  = (float)s_calib.par_h2 / 262144.0f
                * (1.0f + ((float)s_calib.par_h4 / 16384.0f) * tc
                        + ((float)s_calib.par_h5 / 1048576.0f) * tc * tc);
    float v3  = v1 * v2;
    float hum = v3 + ((float)s_calib.par_h6 / 16384.0f
                    + (float)s_calib.par_h7 / 2097152.0f * tc) * v3 * v3;
    if (hum > 100.0f) hum = 100.0f;
    if (hum <   0.0f) hum =   0.0f;
    return hum;
}

static float compensate_gas(uint16_t gas_adc, uint8_t gas_range)
{
    int64_t  var1 = (int64_t)((1340 + (5 * (int64_t)s_calib.range_sw_err))
                    * (int64_t)GAS_LUT1[gas_range]) >> 16;
    uint64_t var2 = (((uint64_t)gas_adc << 15) - (uint64_t)16777216) + (uint64_t)var1;
    int64_t  var3 = ((int64_t)GAS_LUT2[gas_range] * (int64_t)var1) >> 9;
    return (float)((var3 + ((int64_t)var2 >> 1)) / (int64_t)var2);
}

/* ── Heater register helpers ──────────────────────────────────────── */

static uint8_t calc_res_heat(int16_t target_c, float amb_c)
{
    float v1 = (float)s_calib.par_gh1 / 16.0f + 49.0f;
    float v2 = (float)s_calib.par_gh2 / 32768.0f * 0.0005f + 0.00235f;
    float v3 = (float)s_calib.par_gh3 / 1024.0f;
    float v4 = v1 * (1.0f + v2 * (float)target_c);
    float v5 = v4 + v3 * amb_c;
    float r  = 3.4f * ((v5 * (4.0f / (4.0f + (float)s_calib.res_heat_range))
               * (1.0f / (1.0f + (float)s_calib.res_heat_val * 0.002f))) - 25.0f);
    if (r < 0.0f) r = 0.0f;
    return (uint8_t)(r + 0.5f);
}

/* Encode heater duration in ms into the BME680 gas_wait register format. */
static uint8_t encode_gas_wait(uint16_t dur_ms)
{
    uint8_t factor = 0;
    while (dur_ms > 63 && factor < 3) { dur_ms >>= 2; factor++; }
    if (dur_ms > 63) dur_ms = 63;
    return (uint8_t)((factor << 6) | dur_ms);
}

/* ── NVS state persistence ────────────────────────────────────────── */

static void state_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;

    size_t len = sizeof(s_state_buf);
    if (nvs_get_blob(h, NVS_KEY, s_state_buf, &len) == ESP_OK) {
        bsec_library_return_t ret = bsec_set_state(s_state_buf, (uint32_t)len,
                                                   s_work_buf, sizeof(s_work_buf));
        if (ret == BSEC_OK)
            ESP_LOGI(TAG, "state restored (%u bytes)", (unsigned)len);
        else
            ESP_LOGW(TAG, "bsec_set_state: %d (starting fresh)", ret);
    }
    nvs_close(h);
}

static void state_save(void)
{
    uint32_t len = 0;
    bsec_library_return_t ret = bsec_get_state(0, s_state_buf, sizeof(s_state_buf),
                                               s_work_buf, sizeof(s_work_buf), &len);
    if (ret != BSEC_OK || len == 0) {
        ESP_LOGW(TAG, "bsec_get_state: %d", ret);
        return;
    }

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;

    if (nvs_set_blob(h, NVS_KEY, s_state_buf, len) == ESP_OK)
        nvs_commit(h);

    nvs_close(h);
    s_last_save_us = esp_timer_get_time();
    ESP_LOGI(TAG, "state saved (%u bytes)", (unsigned)len);
}

/* ── BSEC measurement task ────────────────────────────────────────── */

static void bsec_task(void *arg)
{
    (void)arg;
    uint8_t prev_accuracy = 0;

    while (1) {
        int64_t now_ns = esp_timer_get_time() * 1000LL;
        bsec_bme_settings_t settings = {0};
        bsec_library_return_t bret = bsec_sensor_control(now_ns, &settings);
        if (bret != BSEC_OK) {
            ESP_LOGW(TAG, "sensor_control: %d", bret);
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        if (settings.trigger_measurement) {
            /* Ambient temperature for heater resistance calculation */
            float amb_temp;
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            amb_temp = s_latest.temperature;
            xSemaphoreGive(s_mutex);
            if (isnan(amb_temp)) amb_temp = 25.0f;

            /* ctrl_hum must be written before ctrl_meas (BME680 datasheet) */
            reg_write(REG_CTRL_HUM, settings.humidity_oversampling & 0x07);
            reg_write(REG_CONFIG, 0x00);  /* IIR filter off */

            if (settings.run_gas) {
                reg_write(REG_RES_HEAT_0, calc_res_heat(settings.heater_temperature, amb_temp));
                reg_write(REG_GAS_WAIT_0, encode_gas_wait(settings.heater_duration));
                reg_write(REG_CTRL_GAS_1, 0x10);  /* run_gas=1, nb_conv=0 (profile 0) */
            } else {
                reg_write(REG_CTRL_GAS_1, 0x00);
            }

            uint8_t ctrl_meas = (uint8_t)((settings.temperature_oversampling << 5)
                                         | (settings.pressure_oversampling    << 2)
                                         | 0x01);  /* forced mode */
            reg_write(REG_CTRL_MEAS, ctrl_meas);

            /* Wait for measurement to complete.
             * T/P/H at x16 takes ~80ms; add gas heater duration plus margin. */
            uint32_t wait_ms = 100;
            if (settings.run_gas) wait_ms += settings.heater_duration + 50;
            vTaskDelay(pdMS_TO_TICKS(wait_ms));

            /* Read 17-byte field-0 burst */
            uint8_t raw[17] = {0};
            if (reg_read(REG_FIELD0, raw, 17) != ESP_OK || !(raw[0] & 0x80)) {
                ESP_LOGW(TAG, "no new data");
                goto sleep;
            }

            uint32_t adc_P = ((uint32_t)raw[2] << 12) | ((uint32_t)raw[3] << 4) | (raw[4] >> 4);
            uint32_t adc_T = ((uint32_t)raw[5] << 12) | ((uint32_t)raw[6] << 4) | (raw[7] >> 4);
            uint16_t adc_H = ((uint16_t)raw[8] << 8)  | raw[9];

            int32_t t_fine;
            float temp = compensate_T(adc_T, &t_fine);
            float pres = compensate_P(adc_P, t_fine);
            float hum  = compensate_H(adc_H, t_fine);

            uint8_t  gas_valid = (raw[14] >> 5) & 0x01;
            uint8_t  heat_stab = (raw[14] >> 4) & 0x01;
            uint16_t adc_gas   = ((uint16_t)raw[13] << 2) | (raw[14] >> 6);
            uint8_t  gas_rng   = raw[14] & 0x0F;
            float gas = (gas_valid && heat_stab) ? compensate_gas(adc_gas, gas_rng) : NAN;

            /* Build inputs for bsec_do_steps — use time after measurement read */
            int64_t ts_ns = esp_timer_get_time() * 1000LL;
            bsec_input_t inputs[5];
            uint8_t n_inputs = 0;

            if (settings.process_data & BSEC_PROCESS_TEMPERATURE)
                inputs[n_inputs++] = (bsec_input_t){ .sensor_id = BSEC_INPUT_TEMPERATURE,
                    .signal = temp, .signal_dimensions = 1, .time_stamp = ts_ns };
            if (settings.process_data & BSEC_PROCESS_PRESSURE)
                inputs[n_inputs++] = (bsec_input_t){ .sensor_id = BSEC_INPUT_PRESSURE,
                    .signal = pres, .signal_dimensions = 1, .time_stamp = ts_ns };
            if (settings.process_data & BSEC_PROCESS_HUMIDITY)
                inputs[n_inputs++] = (bsec_input_t){ .sensor_id = BSEC_INPUT_HUMIDITY,
                    .signal = hum,  .signal_dimensions = 1, .time_stamp = ts_ns };
            if ((settings.process_data & BSEC_PROCESS_GAS) && !isnan(gas))
                inputs[n_inputs++] = (bsec_input_t){ .sensor_id = BSEC_INPUT_GASRESISTOR,
                    .signal = gas,  .signal_dimensions = 1, .time_stamp = ts_ns };

            bsec_output_t outputs[BSEC_NUMBER_OUTPUTS];
            uint8_t n_outputs = BSEC_NUMBER_OUTPUTS;
            bret = bsec_do_steps(inputs, n_inputs, outputs, &n_outputs);
            if (bret != BSEC_OK && bret != BSEC_I_DOSTEPS_NOOUTPUTSRETURNABLE) {
                ESP_LOGW(TAG, "bsec_do_steps: %d", bret);
                goto sleep;
            }

            bsec_data_t next = s_latest;  /* carry unchanged fields */
            for (int i = 0; i < n_outputs; i++) {
                float sig = outputs[i].signal;
                switch (outputs[i].sensor_id) {
                case BSEC_OUTPUT_IAQ:
                    next.iaq = sig; next.iaq_accuracy = outputs[i].accuracy; break;
                case BSEC_OUTPUT_STATIC_IAQ:
                    next.static_iaq = sig; break;
                case BSEC_OUTPUT_CO2_EQUIVALENT:
                    next.co2_eq = sig; break;
                case BSEC_OUTPUT_BREATH_VOC_EQUIVALENT:
                    next.voc_eq = sig; break;
                case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE:
                    next.temperature = sig; break;
                case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY:
                    next.humidity = sig; break;
                case BSEC_OUTPUT_RAW_PRESSURE:
                    next.pressure = sig; break;
                default: break;
                }
            }
            if (!isnan(gas)) next.gas_resistance = gas;

            /* User correction offset (board self-heating, etc.) — applied to the
             * reported value only; BSEC's internal algorithms remain unaffected. */
            next.temperature += s_temp_offset;

            xSemaphoreTake(s_mutex, portMAX_DELAY);
            s_latest = next;
            xSemaphoreGive(s_mutex);

            /* Keep bmx280_read() returning live data so sensor_history and any
             * other bmx280 consumer stays fed while the bmx280 task is suspended. */
            bmx280_data_t bmx_view = {
                .temperature  = next.temperature,
                .humidity     = next.humidity,
                .pressure     = next.pressure,
                .gas_resistance = NAN,
            };
            bmx280_update_latest(&bmx_view);

            ESP_LOGD(TAG, "T=%.2f H=%.1f P=%.0f IAQ=%.0f acc=%d voc=%.2f co2=%.0f",
                     next.temperature, next.humidity, next.pressure / 100.0f,
                     next.iaq, next.iaq_accuracy, next.voc_eq, next.co2_eq);

            /* Save state on accuracy improvement to high, or every 6 hours */
            if ((next.iaq_accuracy == 3 && prev_accuracy < 3) ||
                (esp_timer_get_time() - s_last_save_us >= (int64_t)STATE_SAVE_INTERVAL_US)) {
                state_save();
            }
            prev_accuracy = next.iaq_accuracy;
        }

sleep:;
        /* Sleep until next scheduled call */
        int64_t next_us  = settings.next_call / 1000LL;
        int64_t now2_us  = esp_timer_get_time();
        int64_t sleep_ms = (next_us - now2_us) / 1000LL;
        if (sleep_ms > 0)
            vTaskDelay(pdMS_TO_TICKS(sleep_ms));
    }
}

/* ── Public API ───────────────────────────────────────────────────── */

esp_err_t bsec_sensor_init(void)
{
    /* Add our own device handle on the I2C bus bmx280 already opened */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = bmx280_get_addr(),
        .scl_speed_hz    = 100000,
        .scl_wait_us     = 0,
        .flags           = { .disable_ack_check = false },
    };
    ESP_RETURN_ON_ERROR(
        i2c_master_bus_add_device(bmx280_get_i2c_bus_handle(), &dev_cfg, &s_dev),
        TAG, "add I2C device");

    ESP_RETURN_ON_ERROR(load_calib(), TAG, "BME680 calib");

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    s_latest = (bsec_data_t){
        .temperature = NAN, .humidity = NAN, .pressure = NAN,
        .iaq = NAN, .static_iaq = NAN, .co2_eq = NAN, .voc_eq = NAN,
        .iaq_accuracy = 0, .gas_resistance = NAN,
    };

    /* BSEC library initialisation */
    bsec_library_return_t bret = bsec_init();
    if (bret != BSEC_OK) {
        ESP_LOGE(TAG, "bsec_init: %d", bret);
        return ESP_FAIL;
    }

    /* Load selected configuration: 3.3V supply, LP (3s), 4-day training */
    bret = bsec_set_configuration(bsec_config_iaq, sizeof(bsec_config_iaq),
                                  s_work_buf, sizeof(s_work_buf));
    if (bret != BSEC_OK) {
        ESP_LOGE(TAG, "bsec_set_configuration: %d", bret);
        return ESP_FAIL;
    }

    /* Restore previously saved calibration state (best-effort) */
    state_load();

    /* Subscribe to virtual sensors — all at LP rate (one reading per ~3 s) */
    bsec_sensor_configuration_t requested[] = {
        { .sensor_id = BSEC_OUTPUT_IAQ,                                 .sample_rate = BSEC_SAMPLE_RATE_LP },
        { .sensor_id = BSEC_OUTPUT_STATIC_IAQ,                          .sample_rate = BSEC_SAMPLE_RATE_LP },
        { .sensor_id = BSEC_OUTPUT_CO2_EQUIVALENT,                      .sample_rate = BSEC_SAMPLE_RATE_LP },
        { .sensor_id = BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,               .sample_rate = BSEC_SAMPLE_RATE_LP },
        { .sensor_id = BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE, .sample_rate = BSEC_SAMPLE_RATE_LP },
        { .sensor_id = BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,    .sample_rate = BSEC_SAMPLE_RATE_LP },
        { .sensor_id = BSEC_OUTPUT_RAW_PRESSURE,                        .sample_rate = BSEC_SAMPLE_RATE_LP },
    };
    bsec_sensor_configuration_t required[BSEC_MAX_PHYSICAL_SENSOR];
    uint8_t n_required = BSEC_MAX_PHYSICAL_SENSOR;
    bret = bsec_update_subscription(requested,
                                    sizeof(requested) / sizeof(requested[0]),
                                    required, &n_required);
    if (bret != BSEC_OK) {
        ESP_LOGE(TAG, "bsec_update_subscription: %d", bret);
        return ESP_FAIL;
    }

    /* Hand off BME680 hardware control from bmx280 to this component */
    bmx280_stop_bme680_task();

    s_last_save_us = esp_timer_get_time();

    BaseType_t r = xTaskCreate(bsec_task, "bsec", 8192, NULL, 5, NULL);
    if (r != pdPASS) return ESP_ERR_NO_MEM;

    s_active = true;

    bsec_version_t ver;
    bsec_get_version(&ver);
    ESP_LOGI(TAG, "ready (BSEC %d.%d.%d.%d, %u required sensor settings)",
             ver.major, ver.minor, ver.major_bugfix, ver.minor_bugfix, n_required);
    return ESP_OK;
}

esp_err_t bsec_sensor_read(bsec_data_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_latest;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

uint8_t bsec_sensor_get_accuracy(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint8_t acc = s_latest.iaq_accuracy;
    xSemaphoreGive(s_mutex);
    return acc;
}

void bsec_sensor_set_temp_offset(float offset_celsius)
{
    s_temp_offset = offset_celsius;
}

bool bsec_sensor_is_active(void)
{
    return s_active;
}

/* ── sensor_hub uniform driver (decorator over BME680 raw data) ───────── */

static esp_err_t bsec_drv_read(sensor_driver_t *self, sensor_reading_t *out)
{
    (void)self;
    bsec_data_t d;
    if (bsec_sensor_read(&d) != ESP_OK) return ESP_FAIL;

    if (!isnan(d.temperature)) {
        out->temperature = d.temperature;
        out->valid |= SENSOR_CAP_TEMPERATURE;
    }
    if (!isnan(d.humidity)) {
        out->humidity = d.humidity;
        out->valid |= SENSOR_CAP_HUMIDITY;
    }
    if (!isnan(d.pressure) && d.pressure > 0.0f) {
        out->pressure = d.pressure;
        out->valid |= SENSOR_CAP_PRESSURE;
    }
    if (!isnan(d.gas_resistance)) {
        out->gas_resistance = d.gas_resistance;
        out->valid |= SENSOR_CAP_GAS_RESISTANCE;
    }
    if (!isnan(d.co2_eq)) {
        out->co2          = d.co2_eq;
        out->co2_is_equiv = true;
        out->valid       |= SENSOR_CAP_CO2;
    }
    if (!isnan(d.iaq)) {
        out->iaq          = d.iaq;
        out->static_iaq   = d.static_iaq;
        out->iaq_accuracy = d.iaq_accuracy;
        out->valid       |= SENSOR_CAP_IAQ;
    }
    if (!isnan(d.voc_eq)) {
        out->voc_eq = d.voc_eq;
        out->valid |= SENSOR_CAP_VOC_EQ;
    }
    return ESP_OK;
}

static sensor_driver_t s_hub_driver = {
    .name = "bsec",
    .caps = SENSOR_CAP_TEMPERATURE | SENSOR_CAP_HUMIDITY | SENSOR_CAP_PRESSURE |
            SENSOR_CAP_GAS_RESISTANCE | SENSOR_CAP_CO2 | SENSOR_CAP_IAQ |
            SENSOR_CAP_VOC_EQ,
    .read = bsec_drv_read,
};

sensor_driver_t *bsec_driver(void)
{
    return &s_hub_driver;
}
