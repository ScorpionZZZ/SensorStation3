// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "scd4x.h"
#include "bmx280.h"

#include <math.h>
#include <string.h>
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "scd4x";

/* SCD4x (SCD40/SCD41) has a single fixed I2C address — no alternate/SDO-
 * selectable address like the BMx280 family. */
#define SCD4X_I2C_ADDR 0x62

/* ── Sensirion command words (16-bit, sent MSB first) ───────────────────── */
#define CMD_START_PERIODIC_MEASUREMENT 0x21B1
#define CMD_READ_MEASUREMENT           0xEC05
#define CMD_STOP_PERIODIC_MEASUREMENT  0x3F86
#define CMD_GET_DATA_READY_STATUS      0xE4B8
#define CMD_GET_SERIAL_NUMBER          0x3682
#define CMD_WAKE_UP                    0x36F6

/* ── module state ────────────────────────────────────────────────────── */
static i2c_master_dev_handle_t s_dev;
static TaskHandle_t             s_task = NULL;
static SemaphoreHandle_t        s_mutex;
static scd4x_data_t             s_latest;
static bool                     s_active = false;

/* ── Sensirion CRC-8 (poly 0x31, init 0xFF, no xorout) ──────────────────── */

static uint8_t crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

/* ── I2C helpers ─────────────────────────────────────────────────────── */

static esp_err_t send_cmd(uint16_t cmd)
{
    uint8_t buf[2] = { (uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF) };
    return i2c_master_transmit(s_dev, buf, 2, 50);
}

/* Send `cmd`, wait `exec_time_ms` for the sensor to prepare its answer, then
 * read `nwords` big-endian 16-bit words, each followed by a CRC-8 byte,
 * verifying every checksum. `nwords` must not exceed 3 (largest response
 * used by this driver is get_serial_number / read_measurement, 9 bytes). */
static esp_err_t read_words(uint16_t cmd, uint16_t *words, size_t nwords, uint32_t exec_time_ms)
{
    uint8_t cmd_buf[2] = { (uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF) };
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_dev, cmd_buf, 2, 50), TAG, "cmd");
    if (exec_time_ms) vTaskDelay(pdMS_TO_TICKS(exec_time_ms));

    uint8_t raw[9];
    size_t  len = nwords * 3;
    ESP_RETURN_ON_ERROR(i2c_master_receive(s_dev, raw, len, 50), TAG, "recv");

    for (size_t i = 0; i < nwords; i++) {
        const uint8_t *w = &raw[i * 3];
        if (crc8(w, 2) != w[2]) {
            ESP_LOGW(TAG, "CRC mismatch on word %u", (unsigned)i);
            return ESP_ERR_INVALID_CRC;
        }
        words[i] = (uint16_t)((w[0] << 8) | w[1]);
    }
    return ESP_OK;
}

/* ── sensor task ─────────────────────────────────────────────────────── */

static void scd4x_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "start");

    while (1) {
        uint16_t ready_word;
        esp_err_t err = read_words(CMD_GET_DATA_READY_STATUS, &ready_word, 1, 1);
        if (err == ESP_OK && (ready_word & 0x07FF) != 0) {
            uint16_t words[3];
            if (read_words(CMD_READ_MEASUREMENT, words, 3, 1) == ESP_OK) {
                scd4x_data_t sample = {
                    .co2         = words[0],
                    .temperature = -45.0f + 175.0f * (float)words[1] / 65536.0f,
                    .humidity    = 100.0f * (float)words[2] / 65536.0f,
                };

                xSemaphoreTake(s_mutex, portMAX_DELAY);
                s_latest = sample;
                xSemaphoreGive(s_mutex);
                ESP_LOGD(TAG, "CO2=%u ppm T=%.2f°C RH=%.1f%%",
                         sample.co2, sample.temperature, sample.humidity);
            } else {
                ESP_LOGW(TAG, "read_measurement failed");
            }
        } else if (err != ESP_OK) {
            ESP_LOGW(TAG, "data-ready status read failed");
        }
        /* New data is produced every 5 s in periodic measurement mode;
         * polling once a second catches it promptly without hammering the bus. */
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ── public API ──────────────────────────────────────────────────────── */

esp_err_t scd4x_init(void)
{
    i2c_master_bus_handle_t bus = bmx280_get_i2c_bus_handle();
    if (!bus) {
        ESP_LOGE(TAG, "no I2C bus — call bmx280_init() first");
        return ESP_ERR_INVALID_STATE;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = SCD4X_I2C_ADDR,
        .scl_speed_hz    = 100000, /* SCD4x supports standard mode only, 100 kHz max */
        .scl_wait_us     = 0,
        .flags           = { .disable_ack_check = false },
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_dev), TAG, "add device");

    /* Wake up in case the sensor is asleep; NAK'd if it was already awake,
     * which is expected and not fatal. */
    send_cmd(CMD_WAKE_UP);
    vTaskDelay(pdMS_TO_TICKS(30));

    /* Stop any measurement left running from a previous session — several
     * commands, including get_serial_number, only respond while idle. */
    send_cmd(CMD_STOP_PERIODIC_MEASUREMENT);
    vTaskDelay(pdMS_TO_TICKS(500));

    uint16_t serial[3];
    if (read_words(CMD_GET_SERIAL_NUMBER, serial, 3, 1) != ESP_OK) {
        ESP_LOGW(TAG, "not found at 0x%02X", SCD4X_I2C_ADDR);
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "found, serial=%04X%04X%04X", serial[0], serial[1], serial[2]);

    ESP_RETURN_ON_ERROR(send_cmd(CMD_START_PERIODIC_MEASUREMENT), TAG, "start measurement");

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    s_latest.co2         = 0;
    s_latest.temperature = NAN;
    s_latest.humidity    = NAN;

    BaseType_t r = xTaskCreate(scd4x_task, "scd4x", 3072, NULL, 4, &s_task);
    if (r != pdPASS) return ESP_ERR_NO_MEM;

    s_active = true;
    ESP_LOGI(TAG, "ready");
    return ESP_OK;
}

esp_err_t scd4x_read(scd4x_data_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_latest;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

bool scd4x_is_active(void)
{
    return s_active;
}

/* ── sensor_hub uniform driver ───────────────────────────────────────── */

static esp_err_t scd4x_drv_read(sensor_driver_t *self, sensor_reading_t *out)
{
    (void)self;
    scd4x_data_t d;
    if (scd4x_read(&d) != ESP_OK) return ESP_FAIL;

    if (!isnan(d.temperature)) {
        out->temperature = d.temperature;
        out->valid |= SENSOR_CAP_TEMPERATURE;
    }
    if (!isnan(d.humidity)) {
        out->humidity = d.humidity;
        out->valid |= SENSOR_CAP_HUMIDITY;
    }
    if (d.co2 > 0) {
        out->co2          = (float)d.co2;
        out->co2_is_equiv = false;  /* real NDIR CO2 */
        out->valid       |= SENSOR_CAP_CO2;
    }
    return ESP_OK;
}

static sensor_driver_t s_hub_driver = {
    .name = "scd4x",
    .caps = SENSOR_CAP_TEMPERATURE | SENSOR_CAP_HUMIDITY | SENSOR_CAP_CO2,
    .read = scd4x_drv_read,
};

sensor_driver_t *scd4x_driver(void)
{
    return &s_hub_driver;
}