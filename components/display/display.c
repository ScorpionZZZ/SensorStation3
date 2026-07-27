// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "display.h"
#include "board_config.h"

#include <inttypes.h>
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "lvgl.h"

static const char *TAG = "display";

/* ── draw buffers: 2 × (width × 30 lines × 2 bytes) ≈ 19 KB in DRAM ─── */
#define DISP_BUF_LINES 30
static lv_color_t s_buf1[CYD_LCD_H_RES * DISP_BUF_LINES] __attribute__((aligned(4)));
static lv_color_t s_buf2[CYD_LCD_H_RES * DISP_BUF_LINES] __attribute__((aligned(4)));

static esp_lcd_panel_handle_t  s_panel;
static spi_device_handle_t     s_touch_spi;
static QueueHandle_t           s_brightness_q;
static volatile uint8_t        s_bl_applied = 255;

/* ── LVGL tick source ────────────────────────────────────────────────── */

/*
 * esp_timer gives microsecond resolution; LVGL wants milliseconds.
 * lv_tick_set_cb() is the programmatic equivalent of LV_TICK_CUSTOM in
 * lv_conf.h. We use it here so the tick works regardless of whether
 * CONFIG_LV_TICK_CUSTOM is set in Kconfig.
 */
static uint32_t lvgl_tick_get_cb(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

/* ── LVGL callbacks ──────────────────────────────────────────────────── */

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_draw_bitmap(s_panel,
                              area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1,
                              px_map);
    lv_display_flush_ready(disp);
}

static void log_cb(lv_log_level_t level, const char *buf)
{
    switch (level) {
    case LV_LOG_LEVEL_ERROR: ESP_LOGE(TAG, "%s", buf); break;
    case LV_LOG_LEVEL_WARN:  ESP_LOGW(TAG, "%s", buf); break;
    case LV_LOG_LEVEL_INFO:  ESP_LOGI(TAG, "%s", buf); break;
    default:                 ESP_LOGD(TAG, "%s", buf); break;
    }
}

/* ── LVGL timer task ─────────────────────────────────────────────────── */

static void lvgl_task(void *arg)
{
    (void)arg;
    /* Initialised to 255 — matches display_init which starts at full brightness. */
    static float   s_current = 255.0f;
    static uint8_t s_target  = 255;

    while (1) {
        lv_lock();
        uint32_t delay_ms = lv_timer_handler();
        lv_unlock();
        if (delay_ms < 10)  delay_ms = 10;
        if (delay_ms > 100) delay_ms = 100;

        uint8_t incoming;
        if (s_brightness_q && xQueueReceive(s_brightness_q, &incoming, 0) == pdTRUE) {
            s_target = incoming;
        }

        /* Exponential low-pass: each tick closes 8% of the remaining gap.
         * At ~50 ms/tick this gives ~1.5 s to reach 90% of a new target.
         * Snap to exact value when within 0.5 to avoid infinite crawl.    */
        s_current += ((float)s_target - s_current) * 0.08f;
        float remaining = (float)s_target - s_current;
        if (remaining > -0.5f && remaining < 0.5f) {
            s_current = (float)s_target;
        }

        uint8_t bl = (uint8_t)s_current;
        if (bl != s_bl_applied) {
            display_set_backlight(bl);
            s_bl_applied = bl;
        }

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

/* ── backlight (LEDC PWM) ────────────────────────────────────────────── */

static esp_err_t backlight_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = CYD_LCD_BL_DUTY_RES,
        .timer_num       = CYD_LCD_BL_LEDC_TMR,
        .freq_hz         = CYD_LCD_BL_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_cfg), TAG, "LEDC timer");

    ledc_channel_config_t ch_cfg = {
        .gpio_num   = CYD_LCD_BL_PIN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = CYD_LCD_BL_LEDC_CH,
        .timer_sel  = CYD_LCD_BL_LEDC_TMR,
        .intr_type  = LEDC_INTR_DISABLE,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&ch_cfg), TAG, "LEDC channel");
    return ESP_OK;
}

uint8_t display_get_backlight(void) { return s_bl_applied; }

esp_err_t display_set_backlight(uint8_t brightness)
{
    uint32_t max_duty = (1u << CYD_LCD_BL_DUTY_RES) - 1;
    uint32_t duty     = ((uint32_t)brightness * max_duty) / 255u;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, CYD_LCD_BL_LEDC_CH, duty),
                        TAG, "set duty");
    ESP_RETURN_ON_ERROR(ledc_update_duty(LEDC_LOW_SPEED_MODE, CYD_LCD_BL_LEDC_CH),
                        TAG, "update duty");
    return ESP_OK;
}

/* ── touch (XPT2046 via raw SPI master) ──────────────────────────────── */

/*
 * XPT2046 command byte: Start | A2:A0 | MODE | SER/DFR | PD1 | PD0
 *   0xD0 = X position  (A=101, 12-bit differential, power-down between conversions)
 *   0x90 = Y position  (A=001, 12-bit differential, power-down between conversions)
 * Send 1 command byte, clock out 16-bit response; result is bits[14:3] (12-bit MSB-first).
 */
static bool xpt2046_read(uint8_t cmd, uint16_t *out)
{
    uint8_t tx[3] = { cmd, 0x00, 0x00 };
    uint8_t rx[3] = { 0x00, 0x00, 0x00 };
    spi_transaction_t t = {
        .length    = 24,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    if (spi_device_polling_transmit(s_touch_spi, &t) != ESP_OK) return false;
    /* Bit 15 is the null/busy bit; mask it out to keep the 12-bit result. */
    *out = (((uint16_t)rx[1] << 8 | rx[2]) >> 3) & 0x0FFF;
    return true;
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    /* Z1 (0xB0) is the XPT2046 pressure measurement: near 0 when not touched,
     * non-zero when pressed.  We use this instead of the PENIRQ pin because
     * GPIO36 has no internal pull-up and the IRQ line is often unrouted or
     * floating on CYD board revisions, causing gpio_get_level to read high
     * permanently and gate all touch events. */
    uint16_t z1;
    if (!xpt2046_read(0xB0, &z1) || z1 < 50) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    /* Average 4 samples to reduce resistive-panel jitter. */
    uint32_t x_sum = 0, y_sum = 0;
    for (int i = 0; i < 4; i++) {
        uint16_t rx, ry;
        if (!xpt2046_read(0xD0, &rx) || !xpt2046_read(0x90, &ry)) {
            data->state = LV_INDEV_STATE_RELEASED;
            return;
        }
        x_sum += rx;
        y_sum += ry;
    }

    /* Map raw ADC range → pixel coordinates. */
    int32_t x = ((int32_t)(x_sum / 4) - CYD_TOUCH_CAL_X_MIN) * CYD_LCD_H_RES
                / (CYD_TOUCH_CAL_X_MAX - CYD_TOUCH_CAL_X_MIN);
    int32_t y = ((int32_t)(y_sum / 4) - CYD_TOUCH_CAL_Y_MIN) * CYD_LCD_V_RES
                / (CYD_TOUCH_CAL_Y_MAX - CYD_TOUCH_CAL_Y_MIN);

    if (x < 0) x = 0;
    if (x >= CYD_LCD_H_RES) x = CYD_LCD_H_RES - 1;
    if (y < 0) y = 0;
    if (y >= CYD_LCD_V_RES) y = CYD_LCD_V_RES - 1;

    /* Apply orientation transforms from board_config.h. */
    if (CYD_TOUCH_MIRROR_X) x = CYD_LCD_H_RES - 1 - x;
    if (CYD_TOUCH_MIRROR_Y) y = CYD_LCD_V_RES - 1 - y;
    if (CYD_TOUCH_SWAP_XY)  { int32_t t = x; x = y; y = t; }

    ESP_LOGD(TAG, "touch x=%"PRId32" y=%"PRId32" (z1=%u)", x, y, z1);
    data->point.x = x;
    data->point.y = y;
    data->state   = LV_INDEV_STATE_PRESSED;
}

static esp_err_t touch_init(void)
{
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = CYD_TOUCH_PIN_MOSI,
        .miso_io_num     = CYD_TOUCH_PIN_MISO,
        .sclk_io_num     = CYD_TOUCH_PIN_SCK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 0,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(CYD_TOUCH_SPI_HOST, &bus_cfg, SPI_DMA_DISABLED),
                        TAG, "touch SPI bus");

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = CYD_TOUCH_SPI_FREQ_HZ,
        .mode           = 0,
        .spics_io_num   = CYD_TOUCH_PIN_CS,
        .queue_size     = 1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(CYD_TOUCH_SPI_HOST, &dev_cfg, &s_touch_spi),
                        TAG, "touch SPI device");

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(indev, lv_display_get_default());
    lv_indev_set_read_cb(indev, touch_read_cb);

    ESP_LOGI(TAG, "touch ready (SPI3, IRQ=GPIO%d)", CYD_TOUCH_PIN_IRQ);
    return ESP_OK;
}

void display_attach_brightness_queue(QueueHandle_t q)
{
    s_brightness_q = q;
}

/* ── panel auto-detection ────────────────────────────────────────────── */

typedef enum {
    PANEL_TYPE_ILI9341,
    PANEL_TYPE_ST7789,
} panel_type_t;

/*
 * Reads the 3-byte RDDID (0x04) response over the panel IO handle to tell
 * ILI9341 and ST7789 apart — both use identical SPI wiring on this board.
 * ILI9341 reports a stable 00 93 41. ST7789 clones are inconsistent (real
 * Sitronix silicon reports 85 85 52, many clones report 00 00 00 because
 * SDO isn't actually bonded), so a read that doesn't match ILI9341 and
 * isn't all-zero/all-0xFF is treated as ST7789. An inconclusive read
 * (all-zero/all-0xFF) falls back to ILI9341 to preserve behavior on
 * existing deployed boards.
 */
static panel_type_t detect_panel_type(esp_lcd_panel_io_handle_t io)
{
#if CONFIG_SS3_LCD_PANEL_ILI9341
    return PANEL_TYPE_ILI9341;
#elif CONFIG_SS3_LCD_PANEL_ST7789
    return PANEL_TYPE_ST7789;
#else
    uint8_t id[3] = { 0 };
    esp_err_t ret = esp_lcd_panel_io_rx_param(io, LCD_CMD_RDDID, id, sizeof(id));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "panel ID read failed (%s), defaulting to ILI9341", esp_err_to_name(ret));
        return PANEL_TYPE_ILI9341;
    }

    ESP_LOGI(TAG, "panel ID: %02X %02X %02X", id[0], id[1], id[2]);

    if (id[0] == 0x00 && id[1] == 0x93 && id[2] == 0x41) {
        return PANEL_TYPE_ILI9341;
    }

    bool all_zero = (id[0] == 0x00 && id[1] == 0x00 && id[2] == 0x00);
    bool all_ff    = (id[0] == 0xFF && id[1] == 0xFF && id[2] == 0xFF);
    if (all_zero || all_ff) {
        ESP_LOGW(TAG, "panel ID inconclusive, defaulting to ILI9341");
        return PANEL_TYPE_ILI9341;
    }

    return PANEL_TYPE_ST7789;
#endif
}

/* ── public init ─────────────────────────────────────────────────────── */

esp_err_t display_init(void)
{
    /* SPI bus for the LCD (SPI2 / HSPI). */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = CYD_LCD_PIN_MOSI,
        .miso_io_num     = CYD_LCD_PIN_MISO,
        .sclk_io_num     = CYD_LCD_PIN_SCK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = CYD_LCD_H_RES * DISP_BUF_LINES * sizeof(lv_color_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(CYD_LCD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO),
                        TAG, "SPI bus init");

    /* Attach the LCD as an SPI device. */
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num       = CYD_LCD_PIN_DC,
        .cs_gpio_num       = CYD_LCD_PIN_CS,
        .pclk_hz           = CYD_LCD_SPI_FREQ_HZ,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
        .spi_mode          = 0,
        .trans_queue_depth = CYD_LCD_SPI_QUEUE_DEPTH,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(CYD_LCD_SPI_HOST, &io_cfg, &io),
                        TAG, "LCD IO init");

    /* Create the panel driver — ILI9341 or ST7789, decided by detect_panel_type(). */
    panel_type_t panel_type = detect_panel_type(io);

    if (panel_type == PANEL_TYPE_ST7789) {
        /* Verified on real hardware: no color inversion needed, and this
         * board's ST7789 module needs mirror_x (unlike the ILI9341, which
         * only needs mirror_y for correct orientation). */
        esp_lcd_panel_dev_config_t panel_cfg = {
            .reset_gpio_num = CYD_LCD_PIN_RST,
            .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
            .bits_per_pixel = 16,
        };
        ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io, &panel_cfg, &s_panel),
                            TAG, "ST7789 init");

        ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel),        TAG, "panel reset");
        ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel),         TAG, "panel init");
        ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, true, true), TAG, "panel mirror");
        ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "display on");
        ESP_LOGI(TAG, "panel: ST7789");
    } else {
        esp_lcd_panel_dev_config_t panel_cfg = {
            .reset_gpio_num = CYD_LCD_PIN_RST,
            .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_BGR,
            .bits_per_pixel = 16,
        };
        ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(io, &panel_cfg, &s_panel),
                            TAG, "ILI9341 init");

        ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel),        TAG, "panel reset");
        ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel),         TAG, "panel init");
        ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, false, true), TAG, "panel mirror");
        ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "display on");
        ESP_LOGI(TAG, "panel: ILI9341");
    }

    /* Backlight on at full brightness. */
    ESP_RETURN_ON_ERROR(backlight_init(),             TAG, "backlight init");
    ESP_RETURN_ON_ERROR(display_set_backlight(255),   TAG, "backlight on");

    /* ── LVGL ─────────────────────────────────────────────────────────── */
    lv_init();
    lv_tick_set_cb(lvgl_tick_get_cb);  /* must be set before first lv_timer_handler() */
    lv_log_register_print_cb(log_cb);

    lv_display_t *disp = lv_display_create(CYD_LCD_H_RES, CYD_LCD_V_RES);
    /* ILI9341 expects RGB565 big-endian (MSB first) over SPI, but ESP32 is
     * little-endian. RGB565_SWAPPED makes LVGL store each pixel byte-swapped
     * in the draw buffer so the DMA transfer delivers the correct byte order. */
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, s_buf1, s_buf2,
                           sizeof(s_buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);

    ESP_RETURN_ON_ERROR(touch_init(), TAG, "touch init");

    /* LVGL timer task — single task owns lv_timer_handler(). */
    BaseType_t r = xTaskCreate(lvgl_task, "lvgl", 6144, NULL, 5, NULL);
    if (r != pdPASS) {
        ESP_LOGE(TAG, "LVGL task create failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "ready (%dx%d)", CYD_LCD_H_RES, CYD_LCD_V_RES);
    return ESP_OK;
}