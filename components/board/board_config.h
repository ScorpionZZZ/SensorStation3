// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#pragma once

/**
 * @file board_config.h
 * @brief All board-specific GPIO and peripheral constants for CYD ESP32-2432S028.
 *
 * ╔══════════════════════════════════════════════════════════════════════════╗
 * ║  IMPORTANT — CYD BOARD REVISION CAVEAT                                   ║
 * ║                                                                          ║
 * ║  The ESP32-2432S028 ("Cheap Yellow Display") has been produced in        ║
 * ║  multiple hardware revisions. Pin assignments below reflect the most     ║
 * ║  common (rev 1 / "R1") variant. Before powering on with external         ║
 * ║  wiring, verify every pin against YOUR specific PCB silkscreen,          ║
 * ║  schematic, or oscilloscope probe.                                       ║
 * ║                                                                          ║
 * ║  Known variation points:                                                 ║
 * ║   • Touch SPI: some revisions share MOSI/MISO/SCK with LCD bus.          ║
 * ║   • Backlight: most use GPIO21, some use GPIO27.                         ║
 * ║   • LCD RST: often tied to EN, so -1 works; some boards route it.        ║
 * ║   • RGB LED: some units omit it entirely.                                ║
 * ╚══════════════════════════════════════════════════════════════════════════╝
 *
 * To adapt for a different revision: change the #defines below; do not edit
 * driver code.
 */

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"

/* ══════════════════════════════════════════════════════════════════════════
 * LCD / ILI9341 — SPI (HSPI / SPI2)
 * ════════════════════════════════════════════════════════════════════════ */
#define CYD_LCD_SPI_HOST    SPI2_HOST   /**< HSPI on ESP32 */
#define CYD_LCD_SPI_FREQ_HZ (40 * 1000 * 1000) /**< 40 MHz — ILI9341 max */
#define CYD_LCD_PIN_MOSI    GPIO_NUM_13
#define CYD_LCD_PIN_MISO    GPIO_NUM_12 /**< Not needed for write-only display, but set for bus init */
#define CYD_LCD_PIN_SCK     GPIO_NUM_14
#define CYD_LCD_PIN_CS      GPIO_NUM_15
#define CYD_LCD_PIN_DC      GPIO_NUM_2  /**< Data/Command select */
#define CYD_LCD_PIN_RST     GPIO_NUM_NC /**< NC = not connected; tied to system EN on most CYD units */

/* LCD panel resolution */
#define CYD_LCD_H_RES       240
#define CYD_LCD_V_RES       320

/* Number of parallel SPI DMA transactions queued for the display.
 * Higher = more throughput; each slot uses ~40 bytes of DRAM. */
#define CYD_LCD_SPI_QUEUE_DEPTH 10

/* ══════════════════════════════════════════════════════════════════════════
 * LCD Backlight — LEDC PWM
 *
 * GPIO21 is used for backlight on most CYD units. IMPORTANT: This pin is
 * also the default ESP32 I2C SDA. The I2C bus for BME280 is therefore
 * routed to GPIO22 (SDA) and GPIO27 (SCL) instead of the default GPIO21/22.
 * ════════════════════════════════════════════════════════════════════════ */
#define CYD_LCD_BL_PIN      GPIO_NUM_21 /**< HIGH = backlight on */
#define CYD_LCD_BL_LEDC_CH  0           /**< LEDC channel 0 */
#define CYD_LCD_BL_LEDC_TMR 0           /**< LEDC timer 0 */
#define CYD_LCD_BL_FREQ_HZ  5000        /**< 5 kHz — above audible range */
#define CYD_LCD_BL_DUTY_RES 8           /**< 8-bit → 0..255 duty levels */

/* ══════════════════════════════════════════════════════════════════════════
 * Touch / XPT2046 — SPI (VSPI / SPI3)
 *
 * Most CYD R1 units use a SEPARATE SPI bus for touch so that LCD DMA
 * transfers are not interrupted by touch polls.
 *
 * Revision note: Some early CYD units share MOSI=13/MISO=12/SCK=14 with
 * the LCD. If yours does, change CYD_TOUCH_SPI_HOST to SPI2_HOST and the
 * pin macros to match LCD pins. The CS pins must remain different.
 * ════════════════════════════════════════════════════════════════════════ */
#define CYD_TOUCH_SPI_HOST  SPI3_HOST   /**< VSPI on ESP32 */
#define CYD_TOUCH_SPI_FREQ_HZ (2 * 1000 * 1000) /**< 2 MHz — XPT2046 max */
#define CYD_TOUCH_PIN_MOSI  GPIO_NUM_32
#define CYD_TOUCH_PIN_MISO  GPIO_NUM_39 /**< Input-only GPIO; no pull required */
#define CYD_TOUCH_PIN_SCK   GPIO_NUM_25
#define CYD_TOUCH_PIN_CS    GPIO_NUM_33
#define CYD_TOUCH_PIN_IRQ   GPIO_NUM_36 /**< Input-only GPIO; no pull required */

/* Touch calibration (raw ADC units → pixel coordinates).
 * These are approximate defaults. Calibrate against real hardware for accuracy. */
#define CYD_TOUCH_CAL_X_MIN   200
#define CYD_TOUCH_CAL_X_MAX   3900
#define CYD_TOUCH_CAL_Y_MIN   200
#define CYD_TOUCH_CAL_Y_MAX   3900

/* Touch panel orientation — must mirror the LCD panel transformations.
 * Current LCD: mirror_y=true → set MIRROR_Y=1 here to match.
 * Adjust SWAP_XY and MIRROR_X if touch axes feel transposed or flipped. */
#define CYD_TOUCH_SWAP_XY  0
#define CYD_TOUCH_MIRROR_X 0
#define CYD_TOUCH_MIRROR_Y 1

/* ══════════════════════════════════════════════════════════════════════════
 * BME280 — I2C
 *
 * GPIO21 is taken by the LCD backlight, so we use GPIO22 (SDA) + GPIO27 (SCL).
 * These are available on the CYD GPIO header (P3 connector, typically).
 *
 * I2C ADDRESS:
 *   0x76 when BME280 SDO pin → GND (most breakout boards default)
 *   0x77 when BME280 SDO pin → VCC
 * Check your module to confirm.
 * ════════════════════════════════════════════════════════════════════════ */
#define CYD_BME280_I2C_PORT     I2C_NUM_0
#define CYD_BME280_I2C_SDA      GPIO_NUM_27
#define CYD_BME280_I2C_SCL      GPIO_NUM_22
#define CYD_BME280_I2C_FREQ_HZ  400000      /**< Fast mode (400 kHz) */
//#define CYD_BME280_I2C_ADDR     0x76        /**< Change to 0x77 if SDO → VCC */

/* ══════════════════════════════════════════════════════════════════════════
 * Photoresistor — ADC
 *
 * A voltage-divider photoresistor is connected to GPIO34 (ADC1_CH6).
 * Circuit: 3.3V ─[10kΩ]─ GPIO34 ─[LDR]─ GND
 * Brighter ambient light → lower LDR resistance → lower ADC value.
 * GPIO34 is input-only; internal pullup is not available.
 * ════════════════════════════════════════════════════════════════════════ */
#define CYD_PHOTORES_ADC_UNIT   ADC_UNIT_1
#define CYD_PHOTORES_ADC_CH     ADC_CHANNEL_6  /**< GPIO34 = ADC1_CH6 */
#define CYD_PHOTORES_ADC_ATTEN  ADC_ATTEN_DB_12 /**< Full 0–3.3 V range */
#define CYD_PHOTORES_VCC_MV     3300           /**< Divider supply voltage (mV) */
#define CYD_PHOTORES_REF_OHM    10000          /**< Fixed pull-down resistor in divider (Ω) */

/* ══════════════════════════════════════════════════════════════════════════
 * RGB LED — Active Low (common cathode per GPIO, anode to 3.3 V)
 *
 * Driving a GPIO LOW turns the corresponding LED channel ON.
 * Omit or #undef these if your board lacks the RGB LED.
 * ════════════════════════════════════════════════════════════════════════ */
#define CYD_RGB_RED_PIN     GPIO_NUM_4
#define CYD_RGB_GREEN_PIN   GPIO_NUM_16
#define CYD_RGB_BLUE_PIN    GPIO_NUM_17