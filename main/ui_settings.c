// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "ui.h"
#include "nvs_settings.h"
#include "wifi_manager.h"
#include "ntp_clock.h"
#include "tb_mqtt.h"
#include "domoticz_mqtt.h"
#include "ha_mqtt.h"
#include "bmx280.h"
#include "scd4x.h"
#if CONFIG_SS3_USE_BSEC
#include "bsec_sensor.h"
#endif
#include "lvgl.h"
#include "esp_log.h"
#include "build_info.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ui_settings";

#define KBD_BTN_W    62
#define KBD_BTN_H    42
#define KBD_BTN_GAP   4

/* ── Timezone table ──────────────────────────────────────────────────── */

typedef struct { const char *name; const char *posix; } tz_entry_t;

static const tz_entry_t k_tz_list[] = {
    { "UTC",                       "UTC0"                                     },
    { "London (GMT/BST)",          "GMT0BST,M3.5.0/1,M10.5.0"                },
    { "Lisbon (WET/WEST)",         "WET0WEST,M3.5.0/1,M10.5.0"               },
    { "Paris / Berlin (CET)",      "CET-1CEST,M3.5.0,M10.5.0/3"             },
    { "Helsinki / Kyiv (EET)",     "EET-2EEST,M3.5.0/3,M10.5.0/4"           },
    { "Moscow (MSK)",              "MSK-3"                                    },
    { "Dubai (GST +4)",            "<+04>-4"                                  },
    { "Karachi (PKT +5)",          "PKT-5"                                    },
    { "Kolkata (IST +5:30)",       "IST-5:30"                                 },
    { "Dhaka (+6)",                "<+06>-6"                                  },
    { "Bangkok (ICT +7)",          "<+07>-7"                                  },
    { "Singapore / HK (+8)",       "<+08>-8"                                  },
    { "Tokyo (JST +9)",            "JST-9"                                    },
    { "Seoul (KST +9)",            "KST-9"                                    },
    { "Sydney (AEST/AEDT)",        "AEST-10AEDT,M10.1.0,M4.1.0/3"           },
    { "Auckland (NZST/NZDT)",      "NZST-12NZDT,M9.5.0,M4.1.0/3"           },
    { "Azores (-1)",               "<-01>1<+00>,M3.5.0/0,M10.5.0/1"          },
    { "Sao Paulo (BRT)",           "<-03>3<-02>,M10.3.0,M2.3.0"              },
    { "New York (ET)",             "EST5EDT,M3.2.0,M11.1.0"                   },
    { "Chicago (CT)",              "CST6CDT,M3.2.0,M11.1.0"                   },
    { "Denver (MT)",               "MST7MDT,M3.2.0,M11.1.0"                   },
    { "Los Angeles (PT)",          "PST8PDT,M3.2.0,M11.1.0"                   },
    { "Anchorage (AKT)",           "AKST9AKDT,M3.2.0,M11.1.0"                },
    { "Hawaii (HST)",              "HST10"                                    },
};
#define TZ_COUNT ((int)(sizeof(k_tz_list) / sizeof(k_tz_list[0])))

static const char *tz_posix_to_name(const char *posix)
{
    for (int i = 0; i < TZ_COUNT; i++)
        if (strcmp(k_tz_list[i].posix, posix) == 0)
            return k_tz_list[i].name;
    return posix;
}

/* ── Widget state ────────────────────────────────────────────────────── */

static lv_obj_t *s_screen;
static lv_obj_t *s_pin_label;
static lv_obj_t *s_wifi_status;
static lv_obj_t *s_tz_label;
static lv_obj_t *s_mqtt_status;
static lv_obj_t *s_dz_status;
static lv_obj_t *s_ha_status;
static lv_obj_t *s_kbd_overlay;
static lv_obj_t *s_kbd_ind;
static lv_obj_t *s_tz_overlay;
static lv_obj_t *s_toff_val_lbl;
static int16_t   s_toff_tenths;
static lv_obj_t *s_uptime_lbl;

static char s_new_pin[NVS_SETTINGS_PIN_LEN + 1];
static char s_stored_pin[NVS_SETTINGS_PIN_LEN + 1];
static char s_kbd_saved[NVS_SETTINGS_PIN_LEN + 1];
static char s_kbd_buf[NVS_SETTINGS_PIN_LEN + 1];
static int  s_kbd_count;

/* ── PIN field helpers ───────────────────────────────────────────────── */

static lv_color_t pin_color(const char *pin)
{
    if ((int)strlen(pin) < NVS_SETTINGS_PIN_LEN)
        return lv_palette_main(LV_PALETTE_RED);
    return strcmp(pin, s_stored_pin) == 0
           ? lv_palette_main(LV_PALETTE_GREEN)
           : lv_palette_main(LV_PALETTE_YELLOW);
}

static void fill_display(const char *src, char out[NVS_SETTINGS_PIN_LEN + 1])
{
    int len = (int)strlen(src);
    for (int i = 0; i < NVS_SETTINGS_PIN_LEN; i++)
        out[i] = (i < len) ? src[i] : '_';
    out[NVS_SETTINGS_PIN_LEN] = '\0';
}

static void refresh_pin_field(void)
{
    char display[NVS_SETTINGS_PIN_LEN + 1];
    fill_display(s_new_pin, display);
    lv_label_set_text(s_pin_label, display);
    lv_obj_set_style_text_color(s_pin_label, pin_color(s_new_pin), 0);
}

static void refresh_kbd_ind(void)
{
    if (!s_kbd_ind) return;
    char display[NVS_SETTINGS_PIN_LEN + 1];
    fill_display(s_kbd_buf, display);
    lv_label_set_text(s_kbd_ind, display);
    lv_obj_set_style_text_color(s_kbd_ind, pin_color(s_kbd_buf), 0);
}

/* ── PIN keypad popup ────────────────────────────────────────────────── */

static void close_kbd(void)
{
    if (s_kbd_overlay) {
        lv_obj_delete(s_kbd_overlay);
        s_kbd_overlay = NULL;
        s_kbd_ind = NULL;
    }
}

static void kbd_key_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    if (idx == 11) {
        memcpy(s_new_pin, s_kbd_saved, sizeof(s_new_pin));
        refresh_pin_field();
        close_kbd();
        return;
    }
    if (idx == 9) {
        if (s_kbd_count > 0) {
            s_kbd_buf[--s_kbd_count] = '\0';
            memcpy(s_new_pin, s_kbd_buf, sizeof(s_new_pin));
            refresh_pin_field();
            refresh_kbd_ind();
        }
        return;
    }
    if (s_kbd_count >= NVS_SETTINGS_PIN_LEN) return;

    s_kbd_buf[s_kbd_count++] = (idx == 10) ? '0' : (char)('1' + idx);
    s_kbd_buf[s_kbd_count]   = '\0';
    memcpy(s_new_pin, s_kbd_buf, sizeof(s_new_pin));
    refresh_pin_field();
    refresh_kbd_ind();

    if (s_kbd_count == NVS_SETTINGS_PIN_LEN)
        close_kbd();
}

static void open_kbd(lv_event_t *e)
{
    (void)e;
    if (s_kbd_overlay) return;

    memcpy(s_kbd_saved, s_new_pin, sizeof(s_kbd_saved));
    s_kbd_count = 0;
    memset(s_kbd_buf, 0, sizeof(s_kbd_buf));

    s_kbd_overlay = lv_obj_create(s_screen);
    lv_obj_remove_flag(s_kbd_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_kbd_overlay, 240, 320);
    lv_obj_set_pos(s_kbd_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_kbd_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_kbd_overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_kbd_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_kbd_overlay, 0, 0);

    lv_obj_t *card = lv_obj_create(s_kbd_overlay);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(card, 218, LV_SIZE_CONTENT);
    lv_obj_center(card);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_style_pad_row(card, 8, 0);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *popup_title = lv_label_create(card);
    lv_label_set_text(popup_title, "New PIN");
    lv_obj_set_width(popup_title, LV_PCT(100));
    lv_obj_set_style_text_align(popup_title, LV_TEXT_ALIGN_CENTER, 0);

    s_kbd_ind = lv_label_create(card);
    lv_obj_set_width(s_kbd_ind, LV_PCT(100));
    lv_obj_set_style_text_align(s_kbd_ind, LV_TEXT_ALIGN_CENTER, 0);
    refresh_kbd_ind();

    lv_obj_t *keypad = lv_obj_create(card);
    lv_obj_remove_flag(keypad, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(keypad, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(keypad, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(keypad, 0, 0);
    lv_obj_set_style_pad_all(keypad, 0, 0);
    lv_obj_set_style_pad_row(keypad, KBD_BTN_GAP, 0);
    lv_obj_set_style_pad_column(keypad, KBD_BTN_GAP, 0);
    lv_obj_set_layout(keypad, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(keypad, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(keypad, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    static const char *const labels[12] = {
        "1","2","3","4","5","6","7","8","9",
        LV_SYMBOL_BACKSPACE, "0", LV_SYMBOL_CLOSE
    };
    for (int i = 0; i < 12; i++) {
        lv_obj_t *btn = lv_button_create(keypad);
        lv_obj_set_size(btn, KBD_BTN_W, KBD_BTN_H);
        lv_obj_add_event_cb(btn, kbd_key_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, labels[i]);
        lv_obj_center(lbl);
    }
}

/* ── Update PIN button ───────────────────────────────────────────────── */

static void update_pin_cb(lv_event_t *e)
{
    (void)e;
    if ((int)strlen(s_new_pin) < NVS_SETTINGS_PIN_LEN) return;
    if (nvs_settings_set_pin(s_new_pin) == ESP_OK) {
        memcpy(s_stored_pin, s_new_pin, sizeof(s_stored_pin));
        ESP_LOGI(TAG, "PIN updated");
        refresh_pin_field();
    } else {
        ESP_LOGE(TAG, "PIN save failed");
    }
}

/* ── WiFi section ────────────────────────────────────────────────────── */

static void refresh_mqtt_status(void)
{
    if (!s_mqtt_status) return;
    char uri[NVS_SETTINGS_TB_URI_LEN + 1] = {0};
    char token[NVS_SETTINGS_TB_TOKEN_LEN + 1] = {0};
    bool enabled = true;
    nvs_settings_get_tb_uri(uri, sizeof(uri));
    nvs_settings_get_tb_token(token, sizeof(token));
    nvs_settings_get_tb_enabled(&enabled);
    if (!enabled) {
        lv_label_set_text(s_mqtt_status, "Disabled");
        lv_obj_set_style_text_color(s_mqtt_status, lv_palette_main(LV_PALETTE_GREY), 0);
    } else if (token[0] == '\0') {
        lv_label_set_text(s_mqtt_status, "Not configured");
        lv_obj_set_style_text_color(s_mqtt_status, lv_palette_main(LV_PALETTE_GREY), 0);
    } else if (tb_mqtt_is_connected()) {
        lv_label_set_text(s_mqtt_status, "Connected");
        lv_obj_set_style_text_color(s_mqtt_status, lv_palette_main(LV_PALETTE_GREEN), 0);
    } else {
        lv_label_set_text(s_mqtt_status, "Disconnected");
        lv_obj_set_style_text_color(s_mqtt_status, lv_palette_main(LV_PALETTE_YELLOW), 0);
    }
}

static void wifi_status_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_wifi_status) return;

    char buf[64];
    switch (wifi_manager_get_state()) {
    case WIFI_STATE_IDLE:
        snprintf(buf, sizeof(buf), "Not configured");
        lv_obj_set_style_text_color(s_wifi_status, lv_palette_main(LV_PALETTE_GREY), 0);
        break;
    case WIFI_STATE_CONNECTING:
        snprintf(buf, sizeof(buf), "Connecting...");
        lv_obj_set_style_text_color(s_wifi_status, lv_palette_main(LV_PALETTE_YELLOW), 0);
        break;
    case WIFI_STATE_CONNECTED: {
        char ip[20];
        char ssid[NVS_SETTINGS_SSID_LEN + 1] = {0};
        wifi_manager_get_ip(ip, sizeof(ip));
        if (nvs_settings_get_wifi_ssid(ssid, sizeof(ssid)) != ESP_OK)
            ssid[0] = '\0';
        snprintf(buf, sizeof(buf), "%s  IP: %s", ssid, ip);
        lv_obj_set_style_text_color(s_wifi_status, lv_palette_main(LV_PALETTE_GREEN), 0);
        break;
    }
    case WIFI_STATE_FAILED:
        snprintf(buf, sizeof(buf), "Connection failed, retrying...");
        lv_obj_set_style_text_color(s_wifi_status, lv_palette_main(LV_PALETTE_RED), 0);
        break;
    }
    lv_label_set_text(s_wifi_status, buf);
}

static void refresh_dz_status(void)
{
    if (!s_dz_status) return;
    char uri[NVS_SETTINGS_DZ_URI_LEN + 1] = {0};
    bool enabled = false;
    nvs_settings_get_dz_uri(uri, sizeof(uri));
    nvs_settings_get_dz_enabled(&enabled);
    if (!enabled) {
        lv_label_set_text(s_dz_status, "Disabled");
        lv_obj_set_style_text_color(s_dz_status, lv_palette_main(LV_PALETTE_GREY), 0);
    } else if (uri[0] == '\0') {
        lv_label_set_text(s_dz_status, "Not configured");
        lv_obj_set_style_text_color(s_dz_status, lv_palette_main(LV_PALETTE_GREY), 0);
    } else if (domoticz_mqtt_is_connected()) {
        lv_label_set_text(s_dz_status, "Connected");
        lv_obj_set_style_text_color(s_dz_status, lv_palette_main(LV_PALETTE_GREEN), 0);
    } else {
        lv_label_set_text(s_dz_status, "Disconnected");
        lv_obj_set_style_text_color(s_dz_status, lv_palette_main(LV_PALETTE_YELLOW), 0);
    }
}

static void refresh_ha_status(void)
{
    if (!s_ha_status) return;
    char uri[NVS_SETTINGS_HA_URI_LEN + 1] = {0};
    bool enabled = false;
    nvs_settings_get_ha_uri(uri, sizeof(uri));
    nvs_settings_get_ha_enabled(&enabled);
    if (!enabled) {
        lv_label_set_text(s_ha_status, "Disabled");
        lv_obj_set_style_text_color(s_ha_status, lv_palette_main(LV_PALETTE_GREY), 0);
    } else if (uri[0] == '\0') {
        lv_label_set_text(s_ha_status, "Not configured");
        lv_obj_set_style_text_color(s_ha_status, lv_palette_main(LV_PALETTE_GREY), 0);
    } else if (ha_mqtt_is_connected()) {
        lv_label_set_text(s_ha_status, "Connected");
        lv_obj_set_style_text_color(s_ha_status, lv_palette_main(LV_PALETTE_GREEN), 0);
    } else {
        lv_label_set_text(s_ha_status, "Disconnected");
        lv_obj_set_style_text_color(s_ha_status, lv_palette_main(LV_PALETTE_YELLOW), 0);
    }
}

static void wifi_cfg_btn_cb(lv_event_t *e)
{
    (void)e;
    ui_wifi_show();
}

static void mqtt_cfg_btn_cb(lv_event_t *e)
{
    (void)e;
    ui_mqtt_show();
}

static void dz_cfg_btn_cb(lv_event_t *e)
{
    (void)e;
    ui_domoticz_show();
}

static void dz_enable_cb(lv_event_t *e)
{
    lv_obj_t *cb = lv_event_get_target(e);
    bool enabled = lv_obj_has_state(cb, LV_STATE_CHECKED);
    nvs_settings_set_dz_enabled(enabled);
    if (enabled)
        domoticz_mqtt_start();
    else
        domoticz_mqtt_stop();
    ESP_LOGI(TAG, "Domoticz %s", enabled ? "enabled" : "disabled");
    refresh_dz_status();
}

static void ha_cfg_btn_cb(lv_event_t *e)
{
    (void)e;
    ui_home_assistant_show();
}

static void ha_enable_cb(lv_event_t *e)
{
    lv_obj_t *cb = lv_event_get_target(e);
    bool enabled = lv_obj_has_state(cb, LV_STATE_CHECKED);
    nvs_settings_set_ha_enabled(enabled);
    if (enabled)
        ha_mqtt_start();
    else
        ha_mqtt_stop();
    ESP_LOGI(TAG, "Home Assistant %s", enabled ? "enabled" : "disabled");
    refresh_ha_status();
}

static void ota_btn_cb(lv_event_t *e)
{
    (void)e;
    ui_ota_show();
}

static void mqtt_enable_cb(lv_event_t *e)
{
    lv_obj_t *cb = lv_event_get_target(e);
    bool enabled = lv_obj_has_state(cb, LV_STATE_CHECKED);
    nvs_settings_set_tb_enabled(enabled);
    if (enabled)
        tb_mqtt_start();
    else
        tb_mqtt_stop();
    ESP_LOGI(TAG, "MQTT telemetry %s", enabled ? "enabled" : "disabled");
    refresh_mqtt_status();
}

/* ── Timezone list overlay ───────────────────────────────────────────── */

static void tz_cancel_cb(lv_event_t *e)
{
    (void)e;
    if (s_tz_overlay) {
        lv_obj_delete(s_tz_overlay);
        s_tz_overlay = NULL;
    }
}

static void tz_select_cb(lv_event_t *e)
{
    const tz_entry_t *entry = (const tz_entry_t *)lv_event_get_user_data(e);
    nvs_settings_set_tz(entry->posix);
    ntp_clock_set_tz(entry->posix);
    if (s_tz_label) lv_label_set_text(s_tz_label, entry->name);
    ESP_LOGI(TAG, "TZ: %s (%s)", entry->name, entry->posix);
    if (s_tz_overlay) {
        lv_obj_delete(s_tz_overlay);
        s_tz_overlay = NULL;
    }
}

static void open_tz_list(lv_event_t *e)
{
    (void)e;
    if (s_tz_overlay) return;

    char cur_tz[NVS_SETTINGS_TZ_LEN + 1] = {0};
    nvs_settings_get_tz(cur_tz, sizeof(cur_tz));

    /* Full-screen overlay */
    s_tz_overlay = lv_obj_create(s_screen);
    lv_obj_remove_flag(s_tz_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_tz_overlay, 240, 320);
    lv_obj_set_pos(s_tz_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_tz_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_tz_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_tz_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_tz_overlay, 0, 0);

    /* Header bar */
    lv_obj_t *hdr = lv_obj_create(s_tz_overlay);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(hdr, 240, 38);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_make(30, 30, 30), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);

    lv_obj_t *hdr_title = lv_label_create(hdr);
    lv_label_set_text(hdr_title, "Select Timezone");
    lv_obj_set_style_text_color(hdr_title, lv_color_white(), 0);
    lv_obj_align(hdr_title, LV_ALIGN_LEFT_MID, 8, 0);

    lv_obj_t *cancel_btn = lv_button_create(hdr);
    lv_obj_set_size(cancel_btn, 56, 28);
    lv_obj_align(cancel_btn, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_make(80, 30, 30), 0);
    lv_obj_add_event_cb(cancel_btn, tz_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, LV_SYMBOL_CLOSE " Back");
    lv_obj_center(cancel_lbl);

    /* Scrollable list */
    lv_obj_t *list = lv_list_create(s_tz_overlay);
    lv_obj_set_size(list, 240, 282);
    lv_obj_set_pos(list, 0, 38);
    lv_obj_set_style_bg_color(list, lv_color_black(), 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 2, 0);
    lv_obj_set_style_pad_row(list, 2, 0);

    for (int i = 0; i < TZ_COUNT; i++) {
        lv_obj_t *btn = lv_list_add_button(list, NULL, k_tz_list[i].name);
        lv_obj_add_event_cb(btn, tz_select_cb, LV_EVENT_CLICKED,
                            (void *)&k_tz_list[i]);
        /* Highlight the currently active entry */
        if (strcmp(k_tz_list[i].posix, cur_tz) == 0) {
            lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_BLUE), 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        }
    }
}

/* ── BME680 temperature offset +/- ──────────────────────────────────── */

static void refresh_toff_label(void)
{
    if (!s_toff_val_lbl) return;
    char buf[12];
    snprintf(buf, sizeof(buf), "%.1f C", (float)s_toff_tenths / 10.0f);
    lv_label_set_text(s_toff_val_lbl, buf);
}

static void toff_minus_cb(lv_event_t *e)
{
    (void)e;
    if (s_toff_tenths <= -50) return;
    s_toff_tenths--;
    float off = (float)s_toff_tenths / 10.0f;
    nvs_settings_set_bme680_t_off(off);
    bmx280_set_temp_offset(off);
#if CONFIG_SS3_USE_BSEC
    bsec_sensor_set_temp_offset(off);
#endif
    refresh_toff_label();
}

static void toff_plus_cb(lv_event_t *e)
{
    (void)e;
    if (s_toff_tenths >= 50) return;
    s_toff_tenths++;
    float off = (float)s_toff_tenths / 10.0f;
    nvs_settings_set_bme680_t_off(off);
    bmx280_set_temp_offset(off);
#if CONFIG_SS3_USE_BSEC
    bsec_sensor_set_temp_offset(off);
#endif
    refresh_toff_label();
}

/* ── Uptime ───────────────────────────────────────────────────────────── */

static void uptime_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_uptime_lbl) return;
    int64_t secs = esp_timer_get_time() / 1000000LL;
    int d = (int)(secs / 86400);
    int h = (int)((secs % 86400) / 3600);
    int m = (int)((secs % 3600) / 60);
    int s = (int)(secs % 60);
    char buf[32];
    if (d > 0)
        snprintf(buf, sizeof(buf), "%dd %dh %dm %ds", d, h, m, s);
    else
        snprintf(buf, sizeof(buf), "%dh %dm %ds", h, m, s);
    lv_label_set_text(s_uptime_lbl, buf);
}

/* ── Back button ─────────────────────────────────────────────────────── */

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_show_main();
}

/* ── public ──────────────────────────────────────────────────────────── */

lv_obj_t *ui_settings_screen(void)
{
    return s_screen;
}

void ui_settings_create(void)
{
    if (nvs_settings_get_pin(s_stored_pin, sizeof(s_stored_pin)) != ESP_OK)
        strncpy(s_stored_pin, "1234", sizeof(s_stored_pin));
    memcpy(s_new_pin, s_stored_pin, sizeof(s_new_pin));

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    lv_obj_t *ver_lbl = lv_label_create(s_screen);
    lv_label_set_text(ver_lbl, "v" APP_VERSION_STR);
    lv_obj_set_style_text_color(ver_lbl, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_align(ver_lbl, LV_ALIGN_TOP_RIGHT, -8, 8);

    /* ── PIN section ───────────────────────────────────────────────── */
    lv_obj_t *pin_hdr = lv_label_create(s_screen);
    lv_label_set_text(pin_hdr, "PIN Code:");
    lv_obj_set_style_text_color(pin_hdr, lv_color_white(), 0);
    lv_obj_align(pin_hdr, LV_ALIGN_TOP_LEFT, 10, 34);

    lv_obj_t *pin_row = lv_obj_create(s_screen);
    lv_obj_remove_flag(pin_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(pin_row, 220, 48);
    lv_obj_align(pin_row, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_style_bg_opa(pin_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pin_row, 0, 0);
    lv_obj_set_style_pad_all(pin_row, 0, 0);
    lv_obj_set_style_pad_column(pin_row, 8, 0);
    lv_obj_set_layout(pin_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(pin_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pin_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *field = lv_obj_create(pin_row);
    lv_obj_set_size(field, 100, 40);
    lv_obj_set_flex_grow(field, 1);
    lv_obj_set_style_pad_all(field, 4, 0);
    lv_obj_remove_flag(field, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(field, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(field, open_kbd, LV_EVENT_CLICKED, NULL);

    s_pin_label = lv_label_create(field);
    lv_obj_center(s_pin_label);
    refresh_pin_field();

    lv_obj_t *update_btn = lv_button_create(pin_row);
    lv_obj_set_size(update_btn, 82, 40);
    lv_obj_add_event_cb(update_btn, update_pin_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *update_lbl = lv_label_create(update_btn);
    lv_label_set_text(update_lbl, "Update");
    lv_obj_center(update_lbl);

    /* ── WiFi section ──────────────────────────────────────────────── */
    lv_obj_t *wifi_hdr = lv_label_create(s_screen);
    lv_label_set_text(wifi_hdr, "WiFi:");
    lv_obj_set_style_text_color(wifi_hdr, lv_color_white(), 0);
    lv_obj_align(wifi_hdr, LV_ALIGN_TOP_LEFT, 10, 112);

    s_wifi_status = lv_label_create(s_screen);
    lv_label_set_text(s_wifi_status, "");
    lv_obj_set_style_text_color(s_wifi_status, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_width(s_wifi_status, 220);
    lv_obj_align(s_wifi_status, LV_ALIGN_TOP_LEFT, 10, 130);
    lv_label_set_long_mode(s_wifi_status, LV_LABEL_LONG_SCROLL_CIRCULAR);
    wifi_status_timer_cb(NULL);

    lv_obj_t *wifi_btn = lv_button_create(s_screen);
    lv_obj_set_size(wifi_btn, 220, 36);
    lv_obj_align(wifi_btn, LV_ALIGN_TOP_MID, 0, 152);
    lv_obj_add_event_cb(wifi_btn, wifi_cfg_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *wifi_btn_lbl = lv_label_create(wifi_btn);
    lv_label_set_text(wifi_btn_lbl, LV_SYMBOL_WIFI " Configure WiFi");
    lv_obj_center(wifi_btn_lbl);

    lv_timer_create(wifi_status_timer_cb, 2000, NULL);
    lv_timer_create((lv_timer_cb_t)refresh_mqtt_status, 2000, NULL);
    lv_timer_create((lv_timer_cb_t)refresh_dz_status,   2000, NULL);
    lv_timer_create((lv_timer_cb_t)refresh_ha_status,   2000, NULL);

    /* ── Timezone section ──────────────────────────────────────────── */
    lv_obj_t *tz_hdr = lv_label_create(s_screen);
    lv_label_set_text(tz_hdr, "Timezone:");
    lv_obj_set_style_text_color(tz_hdr, lv_color_white(), 0);
    lv_obj_align(tz_hdr, LV_ALIGN_TOP_LEFT, 10, 202);

    lv_obj_t *tz_row = lv_obj_create(s_screen);
    lv_obj_remove_flag(tz_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(tz_row, 220, 40);
    lv_obj_align(tz_row, LV_ALIGN_TOP_MID, 0, 220);
    lv_obj_set_style_bg_opa(tz_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tz_row, 0, 0);
    lv_obj_set_style_pad_all(tz_row, 0, 0);
    lv_obj_set_style_pad_column(tz_row, 8, 0);
    lv_obj_set_layout(tz_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tz_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tz_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    char cur_tz[NVS_SETTINGS_TZ_LEN + 1] = {0};
    nvs_settings_get_tz(cur_tz, sizeof(cur_tz));

    s_tz_label = lv_label_create(tz_row);
    lv_label_set_text(s_tz_label, tz_posix_to_name(cur_tz));
    lv_obj_set_style_text_color(s_tz_label, lv_palette_main(LV_PALETTE_CYAN), 0);
    lv_obj_set_flex_grow(s_tz_label, 1);
    lv_label_set_long_mode(s_tz_label, LV_LABEL_LONG_DOT);

    lv_obj_t *tz_edit_btn = lv_button_create(tz_row);
    lv_obj_set_size(tz_edit_btn, 60, 36);
    lv_obj_add_event_cb(tz_edit_btn, open_tz_list, LV_EVENT_CLICKED, NULL);
    lv_obj_t *tz_edit_lbl = lv_label_create(tz_edit_btn);
    lv_label_set_text(tz_edit_lbl, "Change");
    lv_obj_center(tz_edit_lbl);

    /* ── ThingsBoard MQTT section ──────────────────────────────────── */
    lv_obj_t *mqtt_hdr = lv_label_create(s_screen);
    lv_label_set_text(mqtt_hdr, "ThingsBoard MQTT:");
    lv_obj_set_style_text_color(mqtt_hdr, lv_color_white(), 0);
    lv_obj_align(mqtt_hdr, LV_ALIGN_TOP_LEFT, 10, 272);

    s_mqtt_status = lv_label_create(s_screen);
    lv_obj_set_width(s_mqtt_status, 220);
    lv_obj_align(s_mqtt_status, LV_ALIGN_TOP_LEFT, 10, 290);
    refresh_mqtt_status();

    bool mqtt_enabled = true;
    nvs_settings_get_tb_enabled(&mqtt_enabled);

    lv_obj_t *mqtt_cb = lv_checkbox_create(s_screen);
    lv_checkbox_set_text(mqtt_cb, "Send telemetry");
    lv_obj_set_style_text_color(mqtt_cb, lv_color_white(), 0);
    lv_obj_align(mqtt_cb, LV_ALIGN_TOP_LEFT, 10, 310);
    if (mqtt_enabled)
        lv_obj_add_state(mqtt_cb, LV_STATE_CHECKED);
    lv_obj_add_event_cb(mqtt_cb, mqtt_enable_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *mqtt_btn = lv_button_create(s_screen);
    lv_obj_set_size(mqtt_btn, 220, 36);
    lv_obj_align(mqtt_btn, LV_ALIGN_TOP_MID, 0, 340);
    lv_obj_add_event_cb(mqtt_btn, mqtt_cfg_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *mqtt_btn_lbl = lv_label_create(mqtt_btn);
    lv_label_set_text(mqtt_btn_lbl, LV_SYMBOL_SETTINGS " Configure MQTT");
    lv_obj_center(mqtt_btn_lbl);

    /* ── Domoticz MQTT section ────────────────────────────────────────── */
    lv_obj_t *dz_hdr = lv_label_create(s_screen);
    lv_label_set_text(dz_hdr, "Domoticz MQTT:");
    lv_obj_set_style_text_color(dz_hdr, lv_color_white(), 0);
    lv_obj_align(dz_hdr, LV_ALIGN_TOP_LEFT, 10, 388);

    s_dz_status = lv_label_create(s_screen);
    lv_obj_set_width(s_dz_status, 220);
    lv_obj_align(s_dz_status, LV_ALIGN_TOP_LEFT, 10, 406);
    refresh_dz_status();

    bool dz_enabled = false;
    nvs_settings_get_dz_enabled(&dz_enabled);

    lv_obj_t *dz_cb = lv_checkbox_create(s_screen);
    lv_checkbox_set_text(dz_cb, "Send to Domoticz");
    lv_obj_set_style_text_color(dz_cb, lv_color_white(), 0);
    lv_obj_align(dz_cb, LV_ALIGN_TOP_LEFT, 10, 424);
    if (dz_enabled)
        lv_obj_add_state(dz_cb, LV_STATE_CHECKED);
    lv_obj_add_event_cb(dz_cb, dz_enable_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *dz_btn = lv_button_create(s_screen);
    lv_obj_set_size(dz_btn, 220, 36);
    lv_obj_align(dz_btn, LV_ALIGN_TOP_MID, 0, 448);
    lv_obj_add_event_cb(dz_btn, dz_cfg_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *dz_btn_lbl = lv_label_create(dz_btn);
    lv_label_set_text(dz_btn_lbl, LV_SYMBOL_SETTINGS " Configure Domoticz");
    lv_obj_center(dz_btn_lbl);

    /* ── Home Assistant MQTT section ──────────────────────────────────── */
    const int ha_extra_y = 108;

    lv_obj_t *ha_hdr = lv_label_create(s_screen);
    lv_label_set_text(ha_hdr, "Home Assistant:");
    lv_obj_set_style_text_color(ha_hdr, lv_color_white(), 0);
    lv_obj_align(ha_hdr, LV_ALIGN_TOP_LEFT, 10, 496);

    s_ha_status = lv_label_create(s_screen);
    lv_obj_set_width(s_ha_status, 220);
    lv_obj_align(s_ha_status, LV_ALIGN_TOP_LEFT, 10, 514);
    refresh_ha_status();

    bool ha_enabled = false;
    nvs_settings_get_ha_enabled(&ha_enabled);

    lv_obj_t *ha_cb = lv_checkbox_create(s_screen);
    lv_checkbox_set_text(ha_cb, "Send to Home Assistant");
    lv_obj_set_style_text_color(ha_cb, lv_color_white(), 0);
    lv_obj_align(ha_cb, LV_ALIGN_TOP_LEFT, 10, 532);
    if (ha_enabled)
        lv_obj_add_state(ha_cb, LV_STATE_CHECKED);
    lv_obj_add_event_cb(ha_cb, ha_enable_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *ha_btn = lv_button_create(s_screen);
    lv_obj_set_size(ha_btn, 220, 36);
    lv_obj_align(ha_btn, LV_ALIGN_TOP_MID, 0, 556);
    lv_obj_add_event_cb(ha_btn, ha_cfg_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ha_btn_lbl = lv_label_create(ha_btn);
    lv_label_set_text(ha_btn_lbl, LV_SYMBOL_SETTINGS " Configure Home Assistant");
    lv_obj_center(ha_btn_lbl);

    /* ── BME680 temperature offset (only when BME680 detected) ───────── */
    int bme680_extra_y = 0;
    if (bmx280_get_type() == BMX280_TYPE_BME680) {
        bme680_extra_y = 72;

        float toff = 0.0f;
        nvs_settings_get_bme680_t_off(&toff);
        s_toff_tenths = (int16_t)(toff * 10.0f);

        lv_obj_t *toff_hdr = lv_label_create(s_screen);
        lv_label_set_text(toff_hdr, "BME680 Temp Offset:");
        lv_obj_set_style_text_color(toff_hdr, lv_color_white(), 0);
        lv_obj_align(toff_hdr, LV_ALIGN_TOP_LEFT, 10, 496 + ha_extra_y);

        lv_obj_t *toff_row = lv_obj_create(s_screen);
        lv_obj_remove_flag(toff_row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(toff_row, 220, 38);
        lv_obj_align(toff_row, LV_ALIGN_TOP_MID, 0, 514 + ha_extra_y);
        lv_obj_set_style_bg_opa(toff_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(toff_row, 0, 0);
        lv_obj_set_style_pad_all(toff_row, 0, 0);
        lv_obj_set_style_pad_column(toff_row, 8, 0);
        lv_obj_set_layout(toff_row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(toff_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(toff_row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *minus_btn = lv_button_create(toff_row);
        lv_obj_set_size(minus_btn, 52, 34);
        lv_obj_add_event_cb(minus_btn, toff_minus_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *minus_lbl = lv_label_create(minus_btn);
        lv_label_set_text(minus_lbl, LV_SYMBOL_MINUS);
        lv_obj_center(minus_lbl);

        s_toff_val_lbl = lv_label_create(toff_row);
        lv_obj_set_flex_grow(s_toff_val_lbl, 1);
        lv_obj_set_style_text_color(s_toff_val_lbl, lv_palette_main(LV_PALETTE_CYAN), 0);
        lv_obj_set_style_text_align(s_toff_val_lbl, LV_TEXT_ALIGN_CENTER, 0);
        refresh_toff_label();

        lv_obj_t *plus_btn = lv_button_create(toff_row);
        lv_obj_set_size(plus_btn, 52, 34);
        lv_obj_add_event_cb(plus_btn, toff_plus_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *plus_lbl = lv_label_create(plus_btn);
        lv_label_set_text(plus_lbl, LV_SYMBOL_PLUS);
        lv_obj_center(plus_lbl);
    }

    /* ── OTA section ───────────────────────────────────────────────── */
    lv_obj_t *ota_btn = lv_button_create(s_screen);
    lv_obj_set_size(ota_btn, 220, 36);
    lv_obj_align(ota_btn, LV_ALIGN_TOP_MID, 0, 496 + ha_extra_y + bme680_extra_y);
    lv_obj_set_style_bg_color(ota_btn, lv_color_make(20, 20, 80), 0);
    lv_obj_add_event_cb(ota_btn, ota_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ota_btn_lbl = lv_label_create(ota_btn);
    lv_label_set_text(ota_btn_lbl, LV_SYMBOL_DOWNLOAD " Firmware Update");
    lv_obj_center(ota_btn_lbl);

    /* ── Device Info section ─────────────────────────────────────────── */
    int info_y = 542 + ha_extra_y + bme680_extra_y;

    lv_obj_t *info_hdr = lv_label_create(s_screen);
    lv_label_set_text(info_hdr, "Device Info:");
    lv_obj_set_style_text_color(info_hdr, lv_color_white(), 0);
    lv_obj_align(info_hdr, LV_ALIGN_TOP_LEFT, 10, info_y);

    /* Firmware version row */
    lv_obj_t *fw_key = lv_label_create(s_screen);
    lv_label_set_text(fw_key, "Firmware:");
    lv_obj_set_style_text_color(fw_key, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_align(fw_key, LV_ALIGN_TOP_LEFT, 10, info_y + 20);

    lv_obj_t *fw_val = lv_label_create(s_screen);
    lv_label_set_text(fw_val, "v" APP_VERSION_STR);
    lv_obj_set_style_text_color(fw_val, lv_color_white(), 0);
    lv_obj_align(fw_val, LV_ALIGN_TOP_RIGHT, -10, info_y + 20);

    /* Sensor type row */
    lv_obj_t *sensor_key = lv_label_create(s_screen);
    lv_label_set_text(sensor_key, "Sensor:");
    lv_obj_set_style_text_color(sensor_key, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_align(sensor_key, LV_ALIGN_TOP_LEFT, 10, info_y + 38);

    lv_obj_t *sensor_val = lv_label_create(s_screen);
    /* Collect every connected sensor, joined as "name1 + name2 + …" (max 3). */
    const char *names[3];
    int         n = 0;
    switch (bmx280_get_type()) {
    case BMX280_TYPE_BME680: names[n++] = "BME680"; break;
    case BMX280_TYPE_BME280: names[n++] = "BME280"; break;
    case BMX280_TYPE_BMP280: names[n++] = "BMP280"; break;
    default:                 break;
    }
    if (scd4x_is_active() && n < 3) names[n++] = "SCD4x";

    char sensor_name[48];
    if (n == 0) {
        snprintf(sensor_name, sizeof(sensor_name), "Not connected");
    } else {
        int off = 0;
        for (int i = 0; i < n; i++)
            off += snprintf(sensor_name + off, sizeof(sensor_name) - off,
                            "%s%s", i ? " + " : "", names[i]);
    }
    lv_label_set_text(sensor_val, sensor_name);
    lv_obj_set_style_text_color(sensor_val,
        (n == 0)
            ? lv_palette_main(LV_PALETTE_RED)
            : lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_align(sensor_val, LV_ALIGN_TOP_RIGHT, -10, info_y + 38);

    /* Uptime row */
    lv_obj_t *uptime_key = lv_label_create(s_screen);
    lv_label_set_text(uptime_key, "Uptime:");
    lv_obj_set_style_text_color(uptime_key, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_align(uptime_key, LV_ALIGN_TOP_LEFT, 10, info_y + 56);

    s_uptime_lbl = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_uptime_lbl, lv_color_white(), 0);
    lv_obj_align(s_uptime_lbl, LV_ALIGN_TOP_RIGHT, -10, info_y + 56);
    uptime_timer_cb(NULL);
    lv_timer_create(uptime_timer_cb, 1000, NULL);

    /* ── Back button ────────────────────────────────────────────────── */
    lv_obj_t *back_btn = lv_button_create(s_screen);
    lv_obj_set_size(back_btn, 80, 36);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 8, info_y + 80);
    lv_obj_add_event_cb(back_btn, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_lbl);
}