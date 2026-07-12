// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#pragma once
#include "lvgl.h"

/* Create all screens and load the main screen. Call inside lv_lock(). */
void      ui_init(void);

/* Screen transitions — safe to call from any LVGL event callback. */
void      ui_show_main(void);
void      ui_show_settings(void);

/* Show the PIN entry modal over the currently active screen. */
void      ui_pin_show(void);

/* Screen object accessors (used internally between ui_*.c files). */
lv_obj_t *ui_main_screen(void);
lv_obj_t *ui_settings_screen(void);

/* Creation functions called by ui_init() (defined in ui_main.c / ui_settings.c). */
void      ui_main_create(void);
void      ui_settings_create(void);

/* WiFi configuration modal — call from inside LVGL lock. */
void      ui_wifi_show(void);

/* ThingsBoard MQTT configuration modal — call from inside LVGL lock. */
void      ui_mqtt_show(void);

/* OTA firmware update overlay — call from inside LVGL lock. */
void      ui_ota_show(void);

/* Domoticz MQTT configuration overlay — call from inside LVGL lock. */
void      ui_domoticz_show(void);

/* Home Assistant MQTT Discovery configuration overlay — call from inside LVGL lock. */
void      ui_home_assistant_show(void);