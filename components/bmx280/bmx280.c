// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "bmx280.h"
#include "board_config.h"

#include <math.h>
#include <string.h>
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "bmx280";

/* ── chip IDs ────────────────────────────────────────────────────────── */
#define CHIP_ID_BME280  0x60
#define CHIP_ID_BMP280A 0x56
#define CHIP_ID_BMP280B 0x57
#define CHIP_ID_BMP280C 0x58
#define CHIP_ID_BME680  0x61

/* ── BME280/BMP280 register addresses ───────────────────────────────── */
#define REG_CHIP_ID    0xD0
#define REG_CTRL_HUM   0xF2
#define REG_CTRL_MEAS  0xF4
#define REG_CONFIG     0xF5
#define REG_PRESS_MSB  0xF7
#define REG_HUM_MSB    0xFD
#define REG_CALIB_TP   0x88
#define REG_CALIB_H1   0xA1
#define REG_CALIB_H2   0xE1

/* ── BME680 register addresses ──────────────────────────────────────── */
#define BME680_REG_CTRL_GAS_0  0x70
#define BME680_REG_CTRL_GAS_1  0x71
#define BME680_REG_CTRL_HUM    0x72
#define BME680_REG_CTRL_MEAS   0x74
#define BME680_REG_CONFIG      0x75
#define BME680_REG_FIELD0      0x1D  /* 17-byte burst data */
#define BME680_REG_RES_HEAT_0  0x5A
#define BME680_REG_GAS_WAIT_0  0x64
#define BME680_REG_CALIB1      0x8A  /* 23 bytes: T2,T3,P1–P10 */
#define BME680_REG_CALIB2      0xE1  /* 14 bytes: H1–H7,T1,GH1–GH3 */
#define BME680_REG_CALIB3      0x00  /* 5 bytes: res_heat_val, range, err */

/* ── BME280/BMP280 calibration ───────────────────────────────────────── */
typedef struct {
    uint16_t T1;
    int16_t  T2, T3;
    uint16_t P1;
    int16_t  P2, P3, P4, P5, P6, P7, P8, P9;
    uint8_t  H1;
    int16_t  H2;
    uint8_t  H3;
    int16_t  H4, H5;
    int8_t   H6;
} calib_t;

/* ── BME680 calibration ──────────────────────────────────────────────── */
typedef struct {
    /* Temperature */
    uint16_t par_t1;
    int16_t  par_t2;
    int8_t   par_t3;
    /* Pressure */
    uint16_t par_p1;
    int16_t  par_p2;
    int8_t   par_p3;
    int16_t  par_p4;
    int16_t  par_p5;
    int8_t   par_p6;
    int8_t   par_p7;
    int16_t  par_p8;
    int16_t  par_p9;
    uint8_t  par_p10;
    /* Humidity */
    uint16_t par_h1;
    uint16_t par_h2;
    int8_t   par_h3;
    int8_t   par_h4;
    int8_t   par_h5;
    uint8_t  par_h6;
    int8_t   par_h7;
    /* Gas heater */
    int8_t   par_gh1;
    int16_t  par_gh2;
    int8_t   par_gh3;
    uint8_t  res_heat_range;
    int8_t   res_heat_val;
    int8_t   range_sw_err;
} bme680_calib_t;

/* ── module state ────────────────────────────────────────────────────── */
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static bmx280_type_t           s_type        = BMX280_TYPE_NONE;
static uint16_t                s_addr        = 0;
static TaskHandle_t            s_task        = NULL;
static calib_t                 s_calib;
static bme680_calib_t          s_calib680;
static float                   s_temp_offset = 0.0f;
static SemaphoreHandle_t       s_mutex;
static bmx280_data_t           s_latest;

/* CO2eq rolling average — one sample per gas heater cycle (~5 s), 6 slots = 30 s */
#define BME680_CO2_AVG_SIZE 6
static float   s_co2eq_buf[BME680_CO2_AVG_SIZE];
static uint8_t s_co2eq_idx   = 0;
static uint8_t s_co2eq_count = 0;

/* ── I2C helpers ─────────────────────────────────────────────────────── */

static esp_err_t reg_read(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, 50);
}

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = { reg, val };
    return i2c_master_transmit(s_dev, b, 2, 50);
}

/* ── BME280/BMP280 calibration load ──────────────────────────────────── */

static esp_err_t load_calib_tp(void)
{
    uint8_t raw[24];
    ESP_RETURN_ON_ERROR(reg_read(REG_CALIB_TP, raw, 24), TAG, "calib T/P read");

    s_calib.T1 = (uint16_t)(raw[1]  << 8 | raw[0]);
    s_calib.T2 = (int16_t) (raw[3]  << 8 | raw[2]);
    s_calib.T3 = (int16_t) (raw[5]  << 8 | raw[4]);
    s_calib.P1 = (uint16_t)(raw[7]  << 8 | raw[6]);
    s_calib.P2 = (int16_t) (raw[9]  << 8 | raw[8]);
    s_calib.P3 = (int16_t) (raw[11] << 8 | raw[10]);
    s_calib.P4 = (int16_t) (raw[13] << 8 | raw[12]);
    s_calib.P5 = (int16_t) (raw[15] << 8 | raw[14]);
    s_calib.P6 = (int16_t) (raw[17] << 8 | raw[16]);
    s_calib.P7 = (int16_t) (raw[19] << 8 | raw[18]);
    s_calib.P8 = (int16_t) (raw[21] << 8 | raw[20]);
    s_calib.P9 = (int16_t) (raw[23] << 8 | raw[22]);
    return ESP_OK;
}

static esp_err_t load_calib_h(void)
{
    uint8_t h1;
    ESP_RETURN_ON_ERROR(reg_read(REG_CALIB_H1, &h1, 1), TAG, "calib H1");
    s_calib.H1 = h1;

    uint8_t raw[7];
    ESP_RETURN_ON_ERROR(reg_read(REG_CALIB_H2, raw, 7), TAG, "calib H2-6");

    s_calib.H2 = (int16_t)(raw[1] << 8 | raw[0]);
    s_calib.H3 = raw[2];
    /* H4/H5 share a nybble in raw[4]; (int8_t) cast is required for sign extension. */
    s_calib.H4 = (int16_t)((int8_t)raw[3] << 4 | (raw[4] & 0x0F));
    s_calib.H5 = (int16_t)((int8_t)raw[5] << 4 | (raw[4] >> 4));
    s_calib.H6 = (int8_t)raw[6];
    return ESP_OK;
}

/* ── BME280/BMP280 Bosch compensation formulas ───────────────────────── */

static int32_t compensate_T(int32_t adc_T, int32_t *t_fine)
{
    int32_t var1, var2, T;
    var1 = ((((adc_T >> 3) - ((int32_t)s_calib.T1 << 1)))
            * ((int32_t)s_calib.T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)s_calib.T1))
            * ((adc_T >> 4) - ((int32_t)s_calib.T1))) >> 12)
            * ((int32_t)s_calib.T3)) >> 14;
    *t_fine = var1 + var2;
    T = (*t_fine * 5 + 128) >> 8;
    return T;
}

static uint32_t compensate_P(int32_t adc_P, int32_t t_fine)
{
    int64_t var1, var2, p;
    var1 = ((int64_t)t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)s_calib.P6;
    var2 = var2 + ((var1 * (int64_t)s_calib.P5) << 17);
    var2 = var2 + (((int64_t)s_calib.P4) << 35);
    var1 = ((var1 * var1 * (int64_t)s_calib.P3) >> 8)
         + ((var1 * (int64_t)s_calib.P2) << 12);
    var1 = ((((int64_t)1) << 47) + var1) * ((int64_t)s_calib.P1) >> 33;
    if (var1 == 0) return 0;
    p    = 1048576 - adc_P;
    p    = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)s_calib.P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)s_calib.P8) * p) >> 19;
    p    = ((p + var1 + var2) >> 8) + (((int64_t)s_calib.P7) << 4);
    return (uint32_t)p;
}

static uint32_t compensate_H(int32_t adc_H, int32_t t_fine)
{
    int32_t v;
    v = t_fine - ((int32_t)76800);
    v = (((((adc_H << 14) - (((int32_t)s_calib.H4) << 20)
          - (((int32_t)s_calib.H5) * v)) + ((int32_t)16384)) >> 15)
        * (((((((v * ((int32_t)s_calib.H6)) >> 10)
          * (((v * ((int32_t)s_calib.H3)) >> 11) + ((int32_t)32768))) >> 10)
          + ((int32_t)2097152)) * ((int32_t)s_calib.H2) + 8192) >> 14));
    v = v - (((((v >> 15) * (v >> 15)) >> 7) * ((int32_t)s_calib.H1)) >> 4);
    v = (v < 0) ? 0 : v;
    v = (v > 419430400) ? 419430400 : v;
    return (uint32_t)(v >> 12);
}

/* ── BME680 calibration load ─────────────────────────────────────────── */

static esp_err_t bme680_load_calib(void)
{
    /* Block 1: 0x8A, 23 bytes — T2,T3 and P1–P10 */
    uint8_t c1[23];
    ESP_RETURN_ON_ERROR(reg_read(BME680_REG_CALIB1, c1, 23), TAG, "680 calib1");

    s_calib680.par_t2  = (int16_t) ((uint16_t)c1[1]  << 8 | c1[0]);
    s_calib680.par_t3  = (int8_t)  c1[2];
    s_calib680.par_p1  = (uint16_t)((uint16_t)c1[5]  << 8 | c1[4]);
    s_calib680.par_p2  = (int16_t) ((uint16_t)c1[7]  << 8 | c1[6]);
    s_calib680.par_p3  = (int8_t)  c1[8];
    s_calib680.par_p4  = (int16_t) ((uint16_t)c1[11] << 8 | c1[10]);
    s_calib680.par_p5  = (int16_t) ((uint16_t)c1[13] << 8 | c1[12]);
    s_calib680.par_p7  = (int8_t)  c1[14];
    s_calib680.par_p6  = (int8_t)  c1[15];
    s_calib680.par_p8  = (int16_t) ((uint16_t)c1[19] << 8 | c1[18]);
    s_calib680.par_p9  = (int16_t) ((uint16_t)c1[21] << 8 | c1[20]);
    s_calib680.par_p10 = c1[22];

    /* Block 2: 0xE1, 14 bytes — H1–H7, T1, GH1–GH3 */
    uint8_t c2[14];
    ESP_RETURN_ON_ERROR(reg_read(BME680_REG_CALIB2, c2, 14), TAG, "680 calib2");

    /* H1 and H2 share a byte at offset 1 (0xE2):
     *   H2 = (c2[0] << 4) | (c2[1] >> 4)
     *   H1 = (c2[2] << 4) | (c2[1] & 0x0F)  */
    s_calib680.par_h2  = (uint16_t)(((uint16_t)c2[0] << 4) | (c2[1] >> 4));
    s_calib680.par_h1  = (uint16_t)(((uint16_t)c2[2] << 4) | (c2[1] & 0x0F));
    s_calib680.par_h3  = (int8_t)  c2[3];
    s_calib680.par_h4  = (int8_t)  c2[4];
    s_calib680.par_h5  = (int8_t)  c2[5];
    s_calib680.par_h6  = c2[6];
    s_calib680.par_h7  = (int8_t)  c2[7];
    s_calib680.par_t1  = (uint16_t)((uint16_t)c2[9]  << 8 | c2[8]);
    s_calib680.par_gh2 = (int16_t) ((uint16_t)c2[11] << 8 | c2[10]);
    s_calib680.par_gh1 = (int8_t)  c2[12];
    s_calib680.par_gh3 = (int8_t)  c2[13];

    /* Block 3: 0x00, 5 bytes — res_heat_val, res_heat_range, range_sw_err */
    uint8_t c3[5];
    ESP_RETURN_ON_ERROR(reg_read(BME680_REG_CALIB3, c3, 5), TAG, "680 calib3");

    s_calib680.res_heat_val   = (int8_t) c3[0];
    s_calib680.res_heat_range = (c3[2] & 0x30) >> 4;
    s_calib680.range_sw_err   = (int8_t)c3[4] >> 4;

    return ESP_OK;
}

/* ── BME680 Bosch compensation formulas (datasheet 4.2) ──────────────── */

static float bme680_compensate_T(uint32_t adc_T, int32_t *t_fine)
{
    float var1 = ((float)adc_T / 16384.0f) - ((float)s_calib680.par_t1 / 1024.0f);
    float var2 = var1 * (float)s_calib680.par_t2;
    float var3 = var1 * var1 * (float)s_calib680.par_t3 * 16.0f;
    *t_fine = (int32_t)(var2 + var3);
    return (var2 + var3) / 5120.0f;
}

static float bme680_compensate_P(uint32_t adc_P, int32_t t_fine)
{
    float var1 = ((float)t_fine / 2.0f) - 64000.0f;
    float var2 = var1 * var1 * ((float)s_calib680.par_p6 / 131072.0f);
    var2 = var2 + (var1 * (float)s_calib680.par_p5 * 2.0f);
    var2 = (var2 / 4.0f) + ((float)s_calib680.par_p4 * 65536.0f);
    var1 = (((float)s_calib680.par_p3 * var1 * var1 / 16384.0f)
            + ((float)s_calib680.par_p2 * var1)) / 524288.0f;
    var1 = (1.0f + var1 / 32768.0f) * (float)s_calib680.par_p1;
    if (var1 == 0.0f) return 0.0f;

    float pres = 1048576.0f - (float)adc_P;
    pres = ((pres - (var2 / 4096.0f)) * 6250.0f) / var1;
    var1 = (float)s_calib680.par_p9 * pres * pres / 2147483648.0f;
    var2 = pres * ((float)s_calib680.par_p8 / 32768.0f);
    float var3 = (pres / 256.0f) * (pres / 256.0f) * (pres / 256.0f)
                 * ((float)s_calib680.par_p10 / 131072.0f);
    return pres + (var1 + var2 + var3 + (float)s_calib680.par_p7 * 128.0f) / 16.0f;
}

static float bme680_compensate_H(uint16_t adc_H, int32_t t_fine)
{
    float temp_sc = (float)t_fine / 5120.0f;
    float var1 = (float)adc_H
                 - ((float)s_calib680.par_h1 * 16.0f)
                 - (temp_sc * (float)s_calib680.par_h3 / 200.0f);
    float var2 = (float)s_calib680.par_h2 / 262144.0f
                 * (1.0f + ((float)s_calib680.par_h4 / 16384.0f) * temp_sc
                         + ((float)s_calib680.par_h5 / 1048576.0f) * temp_sc * temp_sc);
    float var3 = var1 * var2;
    float var4 = (float)s_calib680.par_h6 / 16384.0f;
    float var5 = (float)s_calib680.par_h7 / 2097152.0f;
    float hum = var3 + (var4 + var5 * temp_sc) * var3 * var3;
    if (hum > 100.0f) hum = 100.0f;
    if (hum < 0.0f)   hum = 0.0f;
    return hum;
}

/* Gas resistance lookup tables — Bosch BME680 datasheet appendix */
static const uint32_t s_gas_lut1[16] = {
    2147483647UL, 2147483647UL, 2147483647UL, 2147483647UL,
    2147483647UL, 2126008810UL, 2147483647UL, 2130303777UL,
    2147483647UL, 2147483647UL, 2143188679UL, 2136746228UL,
    2147483647UL, 2126008810UL, 2147483647UL, 2147483647UL
};
static const uint32_t s_gas_lut2[16] = {
    4096000000UL, 2048000000UL, 1024000000UL,  512000000UL,
     255744255UL,  127110228UL,   64000000UL,   32258064UL,
      16016016UL,    8000000UL,    4000000UL,    2000000UL,
       1000000UL,     500000UL,     250000UL,     125000UL
};

static float bme680_compensate_gas(uint16_t gas_adc, uint8_t gas_range)
{
    int64_t var1 = (int64_t)((1340 + (5 * (int64_t)s_calib680.range_sw_err))
                   * (int64_t)s_gas_lut1[gas_range]) >> 16;
    uint64_t var2 = ((uint64_t)((uint64_t)gas_adc << 15) - (uint64_t)16777216) + (uint64_t)var1;
    int64_t var3 = ((int64_t)s_gas_lut2[gas_range] * (int64_t)var1) >> 9;
    return (float)((var3 + ((int64_t)var2 >> 1)) / (int64_t)var2);
}

/* ── BME680 heater helpers ───────────────────────────────────────────── */

/* Calculate res_heat register value for a target temperature (°C).
 * Uses s_latest.temperature as ambient; falls back to 25°C before first reading. */
static uint8_t bme680_calc_res_heat(int16_t target_temp)
{
    float amb = isnan(s_latest.temperature) ? 25.0f : s_latest.temperature;
    float var1 = (float)s_calib680.par_gh1 / 16.0f + 49.0f;
    float var2 = (float)s_calib680.par_gh2 / 32768.0f * 0.0005f + 0.00235f;
    float var3 = (float)s_calib680.par_gh3 / 1024.0f;
    float var4 = var1 * (1.0f + var2 * (float)target_temp);
    float var5 = var4 + var3 * amb;
    float heat_r = 3.4f * ((var5 * (4.0f / (4.0f + (float)s_calib680.res_heat_range))
                   * (1.0f / (1.0f + (float)s_calib680.res_heat_val * 0.002f))) - 25.0f);
    if (heat_r < 0.0f) heat_r = 0.0f;
    return (uint8_t)(heat_r + 0.5f);
}

/* ── BME280/BMP280 read one sample ───────────────────────────────────── */

static esp_err_t bme280_read_sample(bmx280_data_t *out)
{
    uint8_t raw[6];
    ESP_RETURN_ON_ERROR(reg_read(REG_PRESS_MSB, raw, 6), TAG, "P/T read");

    int32_t adc_P = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) | (raw[2] >> 4);
    int32_t adc_T = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) | (raw[5] >> 4);

    int32_t t_fine;
    int32_t  T = compensate_T(adc_T, &t_fine);
    uint32_t P = compensate_P(adc_P, t_fine);

    out->temperature  = (float)T / 100.0f;
    out->pressure     = (float)P / 256.0f;
    out->gas_resistance = NAN;

    if (s_type == BMX280_TYPE_BME280) {
        uint8_t hraw[2];
        ESP_RETURN_ON_ERROR(reg_read(REG_HUM_MSB, hraw, 2), TAG, "H read");
        int32_t  adc_H = ((int32_t)hraw[0] << 8) | hraw[1];
        uint32_t H     = compensate_H(adc_H, t_fine);
        out->humidity  = (float)H / 1024.0f;
    } else {
        out->humidity = NAN;
    }
    return ESP_OK;
}

/* ── BME680 read one sample ──────────────────────────────────────────── */

static esp_err_t bme680_read_sample(bmx280_data_t *out)
{
    /* 17-byte field burst: [0]=status, [1]=meas_index,
     * [2-4]=P MSB/LSB/XLSB, [5-7]=T MSB/LSB/XLSB, [8-9]=H MSB/LSB,
     * [10-12]=idac/res_heat/gas_wait readback, [13-14]=gas MSB/LSB+status */
    uint8_t raw[17];
    ESP_RETURN_ON_ERROR(reg_read(BME680_REG_FIELD0, raw, 17), TAG, "680 data");

    /* Check new_data_0 bit */
    if (!(raw[0] & 0x80)) {
        ESP_LOGW(TAG, "BME680: no new data");
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t adc_P = ((uint32_t)raw[2] << 12) | ((uint32_t)raw[3] << 4) | (raw[4] >> 4);
    uint32_t adc_T = ((uint32_t)raw[5] << 12) | ((uint32_t)raw[6] << 4) | (raw[7] >> 4);
    uint16_t adc_H = ((uint16_t)raw[8] << 8) | raw[9];

    int32_t t_fine;
    out->temperature = bme680_compensate_T(adc_T, &t_fine) + s_temp_offset;
    out->pressure    = bme680_compensate_P(adc_P, t_fine);
    out->humidity    = bme680_compensate_H(adc_H, t_fine);

    uint8_t gas_valid  = (raw[14] >> 5) & 0x01;
    uint8_t heat_stab  = (raw[14] >> 4) & 0x01;
    uint16_t adc_gas   = ((uint16_t)raw[13] << 2) | (raw[14] >> 6);
    uint8_t  gas_range = raw[14] & 0x0F;

    if (gas_valid && heat_stab) {
        out->gas_resistance = bme680_compensate_gas(adc_gas, gas_range);
    } else {
        out->gas_resistance = NAN;
        ESP_LOGD(TAG, "BME680: gas not ready (valid=%d stab=%d)", gas_valid, heat_stab);
    }
    return ESP_OK;
}

/* ── sensor task ─────────────────────────────────────────────────────── */

static void bmx280_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "start");

    /* Gas heater runs 1 in every BME680_GAS_EVERY cycles to reduce self-heating.
     * At 500ms/cycle this means gas every 5 s, cutting heater duty from ~30% to ~3%.
     * T/P/H is still refreshed every 500ms. */
#define BME680_GAS_EVERY 10
    int gas_countdown = 1;  /* fire on first cycle so we get an initial reading quickly */

    while (1) {
        if (s_type == BMX280_TYPE_BME680) {
            bool do_gas = (--gas_countdown <= 0);
            if (do_gas) gas_countdown = BME680_GAS_EVERY;

            if (do_gas) {
                uint8_t res_heat = bme680_calc_res_heat(320);
                reg_write(BME680_REG_RES_HEAT_0, res_heat);
                reg_write(BME680_REG_CTRL_GAS_1, 0x10);  /* run_gas=1, nb_conv=0 */
                reg_write(BME680_REG_CTRL_MEAS,  0xB5);  /* x16 T/P, forced */
                /* T/P/H oversampling (~80ms) + 150ms gas heater = ~230ms; use 350ms. */
                vTaskDelay(pdMS_TO_TICKS(350));
            } else {
                /* Heater off — T/P/H only measurement, ~80ms for x16 oversampling. */
                reg_write(BME680_REG_CTRL_GAS_1, 0x00);  /* heater off */
                reg_write(BME680_REG_CTRL_MEAS,  0xB5);  /* x16 T/P, forced */
                vTaskDelay(pdMS_TO_TICKS(150));
            }

            bmx280_data_t sample;
            if (bme680_read_sample(&sample) == ESP_OK) {
                xSemaphoreTake(s_mutex, portMAX_DELAY);
                if (!do_gas) {
                    /* Preserve last valid gas reading — it does not change between
                     * heater cycles and bme680_read_sample() returns NAN without heat. */
                    sample.gas_resistance = s_latest.gas_resistance;
                }
                s_latest = sample;
                if (do_gas && !isnan(sample.gas_resistance)) {
                    float co2eq = bmx280_gas_to_co2eq(sample.gas_resistance, sample.humidity);
                    s_co2eq_buf[s_co2eq_idx] = co2eq;
                    s_co2eq_idx = (s_co2eq_idx + 1) % BME680_CO2_AVG_SIZE;
                    if (s_co2eq_count < BME680_CO2_AVG_SIZE) s_co2eq_count++;
                }
                xSemaphoreGive(s_mutex);
                ESP_LOGD(TAG, "T=%.2f°C P=%.1f hPa H=%.1f%% Gas=%.0f Ohm (gas=%d)",
                         sample.temperature, sample.pressure / 100.0f,
                         sample.humidity, sample.gas_resistance, do_gas);
            } else {
                ESP_LOGW(TAG, "BME680 read failed");
            }
            /* Pad remaining time to keep ~500ms total cycle. */
            vTaskDelay(do_gas ? pdMS_TO_TICKS(150) : pdMS_TO_TICKS(350));
        } else {
            bmx280_data_t sample;
            if (bme280_read_sample(&sample) == ESP_OK) {
                xSemaphoreTake(s_mutex, portMAX_DELAY);
                s_latest = sample;
                xSemaphoreGive(s_mutex);
                ESP_LOGD(TAG, "T=%.2f°C P=%.1f hPa H=%.1f%%",
                         sample.temperature, sample.pressure / 100.0f, sample.humidity);
            } else {
                ESP_LOGW(TAG, "read failed");
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

/* ── public API ──────────────────────────────────────────────────────── */

esp_err_t bmx280_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port          = CYD_BME280_I2C_PORT,
        .sda_io_num        = CYD_BME280_I2C_SDA,
        .scl_io_num        = CYD_BME280_I2C_SCL,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority     = 0,
        .trans_queue_depth = 0,
        .flags = { .enable_internal_pullup = true },
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG, "bus init");

    const uint16_t candidates[] = { 0x76, 0x77 };
    uint8_t chip_id = 0;
    bool found = false;
    for (int i = 0; i < 2; i++) {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address  = candidates[i],
            .scl_speed_hz    = CYD_BME280_I2C_FREQ_HZ,
            .scl_wait_us     = 0,
            .flags           = { .disable_ack_check = false },
        };
        if (i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev) != ESP_OK) continue;
        if (reg_read(REG_CHIP_ID, &chip_id, 1) == ESP_OK) {
            ESP_LOGI(TAG, "found at 0x%02X, chip_id=0x%02X", candidates[i], chip_id);
            s_addr = candidates[i];
            found = true;
            break;
        }
        i2c_master_bus_rm_device(s_dev);
    }
    if (!found) {
        ESP_LOGE(TAG, "no BMx280/BME680 at 0x76 or 0x77");
        i2c_del_master_bus(s_bus);
        return ESP_ERR_NOT_FOUND;
    }

    if (chip_id == CHIP_ID_BME680) {
        s_type = BMX280_TYPE_BME680;
    } else if (chip_id == CHIP_ID_BME280) {
        s_type = BMX280_TYPE_BME280;
    } else if (chip_id == CHIP_ID_BMP280A ||
               chip_id == CHIP_ID_BMP280B ||
               chip_id == CHIP_ID_BMP280C) {
        s_type = BMX280_TYPE_BMP280;
    } else {
        ESP_LOGE(TAG, "unknown chip_id 0x%02X", chip_id);
        return ESP_ERR_NOT_SUPPORTED;
    }

    const char *names[] = { "none", "BMP280", "BME280", "BME680" };
    ESP_LOGI(TAG, "detected %s", names[s_type]);

    if (s_type == BMX280_TYPE_BME680) {
        ESP_RETURN_ON_ERROR(bme680_load_calib(), TAG, "BME680 calib");

        /* ctrl_hum must be written before ctrl_meas */
        ESP_RETURN_ON_ERROR(reg_write(BME680_REG_CTRL_HUM,   0x01), TAG, "680 ctrl_hum");
        ESP_RETURN_ON_ERROR(reg_write(BME680_REG_CONFIG,     0x00), TAG, "680 config");
        ESP_RETURN_ON_ERROR(reg_write(BME680_REG_CTRL_GAS_0, 0x00), TAG, "680 gas0"); /* heater on */
        /* run_gas=1 (bit4), nb_conv=0 (profile 0) */
        ESP_RETURN_ON_ERROR(reg_write(BME680_REG_CTRL_GAS_1, 0x10), TAG, "680 gas1");
        /* gas_wait_0: 150ms → dur=37, factor=1 → 37 + 64 = 101 = 0x65 */
        ESP_RETURN_ON_ERROR(reg_write(BME680_REG_GAS_WAIT_0, 0x65), TAG, "680 gas_wait");
    } else {
        ESP_RETURN_ON_ERROR(load_calib_tp(), TAG, "calib T/P");
        if (s_type == BMX280_TYPE_BME280) {
            ESP_RETURN_ON_ERROR(load_calib_h(), TAG, "calib H");
            /* ctrl_hum must be written before ctrl_meas (BME280 datasheet §5.4.3). */
            ESP_RETURN_ON_ERROR(reg_write(REG_CTRL_HUM,  0x05), TAG, "ctrl_hum");
        }
        ESP_RETURN_ON_ERROR(reg_write(REG_CONFIG,    0x00), TAG, "config");
        ESP_RETURN_ON_ERROR(reg_write(REG_CTRL_MEAS, 0xB7), TAG, "ctrl_meas");
    }

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    s_latest.temperature  = NAN;
    s_latest.pressure     = 0.0f;
    s_latest.humidity     = NAN;
    s_latest.gas_resistance = NAN;

    BaseType_t r = xTaskCreate(bmx280_task, "bmx280", 4096, NULL, 4, &s_task);
    if (r != pdPASS) return ESP_ERR_NO_MEM;

    ESP_LOGI(TAG, "ready");
    return ESP_OK;
}

void bmx280_set_temp_offset(float offset_celsius)
{
    s_temp_offset = offset_celsius;
}

void bmx280_update_latest(const bmx280_data_t *d)
{
    if (!d) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_latest = *d;
    xSemaphoreGive(s_mutex);
}

i2c_master_bus_handle_t bmx280_get_i2c_bus_handle(void)
{
    return s_bus;
}

uint16_t bmx280_get_addr(void)
{
    return s_addr;
}

void bmx280_stop_bme680_task(void)
{
    if (s_type != BMX280_TYPE_BME680 || !s_task) return;
    vTaskSuspend(s_task);
    ESP_LOGI(TAG, "BME680 sampling task suspended — BSEC taking over");
}

bmx280_type_t bmx280_get_type(void)
{
    return s_type;
}

esp_err_t bmx280_co2eq_avg(float *out)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint8_t count = s_co2eq_count;
    float sum = 0.0f;
    for (int i = 0; i < count; i++) sum += s_co2eq_buf[i];
    xSemaphoreGive(s_mutex);
    if (count == 0) return ESP_ERR_INVALID_STATE;
    *out = sum / (float)count;
    return ESP_OK;
}

float bmx280_gas_to_co2eq(float gas_ohm, float humidity_pct)
{
    if (gas_ohm <= 0.0f) return 400.0f;

    /* Humidity compensation: high humidity lowers resistance independently of
     * air quality.  Correct by ~0.6% per % RH deviation from 40% reference. */
    float gas_comp = gas_ohm * (1.0f + (humidity_pct - 40.0f) * 0.006f);
    if (gas_comp <= 0.0f) gas_comp = 1.0f;

    /* Inverse power-law mapping.  Calibration anchor:
     *   80 kOhm @ 40% RH  →  400 ppm CO2eq (clean outdoor air)
     * Baseline tuned for 3% heater duty cycle (1-in-10 firing).
     * Exponent 1.3 is empirical — matches typical indoor sensor curves. */
    const float baseline = 80000.0f;
    float co2_eq = 400.0f * powf(baseline / gas_comp, 1.3f);

    if (co2_eq < 400.0f)   co2_eq = 400.0f;
    if (co2_eq > 60000.0f) co2_eq = 60000.0f;
    return co2_eq;
}

esp_err_t bmx280_read(bmx280_data_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_latest;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}