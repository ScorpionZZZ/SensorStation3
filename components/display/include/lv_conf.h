// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

/**
 * @file lv_conf.h
 * LVGL v9 configuration for CYD ESP32-2432S028 (ILI9341, 240x320, RGB565).
 *
 * Only settings that differ from LVGL defaults are listed here.
 * lv_conf_internal.h provides #ifndef-guarded defaults for everything else.
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
 * COLOR
 *====================*/
#define LV_COLOR_DEPTH 16   /* RGB565 — matches ILI9341 native format */

/*====================
 * MEMORY
 *====================*/
/* Use the system (FreeRTOS) heap instead of a private LVGL pool. */
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC   malloc
#define LV_MEM_CUSTOM_FREE    free
#define LV_MEM_CUSTOM_REALLOC realloc

/*====================
 * OPERATING SYSTEM
 *====================*/
/* Enable FreeRTOS mutex so lv_lock() / lv_unlock() work across tasks. */
#define LV_USE_OS LV_OS_FREERTOS

/*====================
 * DRAW BUFFERS
 *====================*/
#define LV_DRAW_BUF_STRIDE_ALIGN 1
#define LV_DRAW_BUF_ALIGN        4  /* 32-bit alignment for ESP32 DMA */

/*====================
 * LOGGING
 *====================*/
#define LV_USE_LOG    1
#define LV_LOG_LEVEL  LV_LOG_LEVEL_WARN
/* Route through lv_log_register_print_cb() → ESP_LOG instead of printf. */
#define LV_LOG_PRINTF 0

/*====================
 * FONTS
 *====================*/
/* Enable Montserrat 14 as default; add others as needed. */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*====================
 * EXAMPLES / DEMOS
 *====================*/
#define LV_BUILD_EXAMPLES     0
#define LV_USE_DEMO_WIDGETS   0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_STRESS    0
#define LV_USE_DEMO_MUSIC     0

#endif /* LV_CONF_H */