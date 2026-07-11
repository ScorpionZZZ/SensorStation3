// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "ui.h"
#include "nvs_settings.h"
#include "domoticz_mqtt.h"
#include "bmx280.h"
#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "ui_domoticz";

/* ── state ─────────────────────────────────────────────────────────── */

static lv_obj_t *s_overlay;
static lv_obj_t *s_status_lbl;
static lv_obj_t *s_ta_uri;
static lv_obj_t *s_ta_thp;
static lv_obj_t *s_ta_co2;   /* NULL when sensor has no gas channel */
static lv_obj_t *s_ta_user;
static lv_obj_t *s_ta_pass;
static lv_obj_t *s_kbd;

/* ── status line ────────────────────────────────────────────────────── */

static void refresh_status(void)
{
    if (!s_status_lbl) return;

    char uri[NVS_SETTINGS_DZ_URI_LEN + 1] = {0};
    bool enabled = false;
    nvs_settings_get_dz_uri(uri, sizeof(uri));
    nvs_settings_get_dz_enabled(&enabled);

    if (!enabled) {
        lv_label_set_text(s_status_lbl, "Disabled");
        lv_obj_set_style_text_color(s_status_lbl, lv_palette_main(LV_PALETTE_GREY), 0);
    } else if (uri[0] == '\0') {
        lv_label_set_text(s_status_lbl, "Not configured — enter URI below");
        lv_obj_set_style_text_color(s_status_lbl, lv_palette_main(LV_PALETTE_GREY), 0);
    } else if (domoticz_mqtt_is_connected()) {
        lv_label_set_text(s_status_lbl, LV_SYMBOL_OK " Connected");
        lv_obj_set_style_text_color(s_status_lbl, lv_palette_main(LV_PALETTE_GREEN), 0);
    } else {
        lv_label_set_text(s_status_lbl, LV_SYMBOL_CLOSE " Disconnected");
        lv_obj_set_style_text_color(s_status_lbl, lv_palette_main(LV_PALETTE_YELLOW), 0);
    }
}

static void status_timer_cb(lv_timer_t *t)
{
    (void)t;
    refresh_status();
}

/* ── keyboard ───────────────────────────────────────────────────────── */

static void ta_focused_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    if (ta == s_ta_thp || ta == s_ta_co2)
        lv_keyboard_set_mode(s_kbd, LV_KEYBOARD_MODE_NUMBER);
    else
        lv_keyboard_set_mode(s_kbd, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(s_kbd, ta);
    lv_obj_remove_flag(s_kbd, LV_OBJ_FLAG_HIDDEN);
}

static void kbd_done_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_kbd, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(s_kbd, NULL);
}

/* ── save ───────────────────────────────────────────────────────────── */

static void save_cb(lv_event_t *e)
{
    (void)e;
    const char *uri     = lv_textarea_get_text(s_ta_uri);
    uint16_t    thp_idx = (uint16_t)atoi(lv_textarea_get_text(s_ta_thp));
    uint16_t    co2_idx = s_ta_co2 ? (uint16_t)atoi(lv_textarea_get_text(s_ta_co2)) : 0;
    const char *user    = lv_textarea_get_text(s_ta_user);
    const char *pass    = lv_textarea_get_text(s_ta_pass);

    nvs_settings_set_dz_uri(uri);
    nvs_settings_set_dz_thp_idx(thp_idx);
    nvs_settings_set_dz_co2_idx(co2_idx);
    nvs_settings_set_dz_user(user);
    nvs_settings_set_dz_pass(pass);
    ESP_LOGI(TAG, "saved uri=%s thp_idx=%u co2_idx=%u user=%s",
             uri, thp_idx, co2_idx, user);

    domoticz_mqtt_start();

    if (s_status_lbl) {
        lv_label_set_text(s_status_lbl, "Saved — connecting...");
        lv_obj_set_style_text_color(s_status_lbl, lv_palette_main(LV_PALETTE_CYAN), 0);
    }
}

/* ── back ───────────────────────────────────────────────────────────── */

static void back_cb(lv_event_t *e)
{
    (void)e;
    if (!s_overlay) return;
    lv_obj_delete(s_overlay);
    s_overlay    = NULL;
    s_status_lbl = NULL;
    s_ta_uri     = NULL;
    s_ta_thp     = NULL;
    s_ta_co2     = NULL;
    s_ta_user    = NULL;
    s_ta_pass    = NULL;
    s_kbd        = NULL;
}

/* ── public ─────────────────────────────────────────────────────────── */

void ui_domoticz_show(void)
{
    if (s_overlay) return;

    bool has_co2   = (bmx280_get_type() == BMX280_TYPE_BME680);
    int  co2_extra = has_co2 ? 34 : 0;

    char     uri[NVS_SETTINGS_DZ_URI_LEN + 1]   = {0};
    char     user[NVS_SETTINGS_DZ_USER_LEN + 1]  = {0};
    char     pass[NVS_SETTINGS_DZ_PASS_LEN + 1]  = {0};
    uint16_t thp_idx = 0, co2_idx = 0;
    nvs_settings_get_dz_uri(uri, sizeof(uri));
    nvs_settings_get_dz_thp_idx(&thp_idx);
    nvs_settings_get_dz_co2_idx(&co2_idx);
    nvs_settings_get_dz_user(user, sizeof(user));
    nvs_settings_get_dz_pass(pass, sizeof(pass));

    /* ── overlay ────────────────────────────────────────────────────── */
    s_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_overlay, 240, 320);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_overlay, 0, 0);

    /* ── header (with Back button) ──────────────────────────────────── */
    lv_obj_t *hdr = lv_obj_create(s_overlay);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(hdr, 240, 38);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_make(0, 40, 50), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS " Domoticz MQTT");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

    lv_obj_t *back_btn = lv_button_create(hdr);
    lv_obj_set_size(back_btn, 60, 28);
    lv_obj_align(back_btn, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_make(60, 30, 30), 0);
    lv_obj_add_event_cb(back_btn, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_lbl);

    /* ── status line ────────────────────────────────────────────────── */
    s_status_lbl = lv_label_create(s_overlay);
    lv_obj_set_width(s_status_lbl, 224);
    lv_label_set_long_mode(s_status_lbl, LV_LABEL_LONG_DOT);
    lv_obj_align(s_status_lbl, LV_ALIGN_TOP_LEFT, 8, 40);
    refresh_status();
    lv_timer_create(status_timer_cb, 2000, NULL);

    /* ── Broker URI ─────────────────────────────────────────────────── */
    s_ta_uri = lv_textarea_create(s_overlay);
    lv_obj_set_size(s_ta_uri, 224, 36);
    lv_obj_align(s_ta_uri, LV_ALIGN_TOP_MID, 0, 54);
    lv_textarea_set_one_line(s_ta_uri, true);
    lv_textarea_set_max_length(s_ta_uri, NVS_SETTINGS_DZ_URI_LEN);
    lv_textarea_set_placeholder_text(s_ta_uri, "mqtt://192.168.1.100:1883");
    lv_textarea_set_text(s_ta_uri, uri);
    lv_obj_add_event_cb(s_ta_uri, ta_focused_cb, LV_EVENT_FOCUSED, NULL);

    /* ── Temp+Hum+Baro IDX ──────────────────────────────────────────── */
    s_ta_thp = lv_textarea_create(s_overlay);
    lv_obj_set_size(s_ta_thp, 224, 30);
    lv_obj_align(s_ta_thp, LV_ALIGN_TOP_MID, 0, 94);
    lv_textarea_set_one_line(s_ta_thp, true);
    lv_textarea_set_max_length(s_ta_thp, 5);
    lv_textarea_set_placeholder_text(s_ta_thp, "Temp+Hum+Baro IDX");
    char idx_buf[8];
    snprintf(idx_buf, sizeof(idx_buf), "%u", (unsigned)thp_idx);
    lv_textarea_set_text(s_ta_thp, idx_buf);
    lv_obj_add_event_cb(s_ta_thp, ta_focused_cb, LV_EVENT_FOCUSED, NULL);

    /* ── CO2eq IDX (BME680 only) ────────────────────────────────────── */
    if (has_co2) {
        s_ta_co2 = lv_textarea_create(s_overlay);
        lv_obj_set_size(s_ta_co2, 224, 30);
        lv_obj_align(s_ta_co2, LV_ALIGN_TOP_MID, 0, 128);
        lv_textarea_set_one_line(s_ta_co2, true);
        lv_textarea_set_max_length(s_ta_co2, 5);
        lv_textarea_set_placeholder_text(s_ta_co2, "CO2eq IDX");
        snprintf(idx_buf, sizeof(idx_buf), "%u", (unsigned)co2_idx);
        lv_textarea_set_text(s_ta_co2, idx_buf);
        lv_obj_add_event_cb(s_ta_co2, ta_focused_cb, LV_EVENT_FOCUSED, NULL);
    }

    /* ── Username ───────────────────────────────────────────────────── */
    s_ta_user = lv_textarea_create(s_overlay);
    lv_obj_set_size(s_ta_user, 224, 30);
    lv_obj_align(s_ta_user, LV_ALIGN_TOP_MID, 0, 128 + co2_extra);
    lv_textarea_set_one_line(s_ta_user, true);
    lv_textarea_set_max_length(s_ta_user, NVS_SETTINGS_DZ_USER_LEN);
    lv_textarea_set_placeholder_text(s_ta_user, "Username (optional)");
    lv_textarea_set_text(s_ta_user, user);
    lv_obj_add_event_cb(s_ta_user, ta_focused_cb, LV_EVENT_FOCUSED, NULL);

    /* ── Password ───────────────────────────────────────────────────── */
    s_ta_pass = lv_textarea_create(s_overlay);
    lv_obj_set_size(s_ta_pass, 224, 30);
    lv_obj_align(s_ta_pass, LV_ALIGN_TOP_MID, 0, 162 + co2_extra);
    lv_textarea_set_one_line(s_ta_pass, true);
    lv_textarea_set_max_length(s_ta_pass, NVS_SETTINGS_DZ_PASS_LEN);
    lv_textarea_set_placeholder_text(s_ta_pass, "Password (optional)");
    lv_textarea_set_password_mode(s_ta_pass, true);
    lv_textarea_set_text(s_ta_pass, pass);
    lv_obj_add_event_cb(s_ta_pass, ta_focused_cb, LV_EVENT_FOCUSED, NULL);

    /* ── Save button ────────────────────────────────────────────────── */
    lv_obj_t *save_btn = lv_button_create(s_overlay);
    lv_obj_set_size(save_btn, 224, 36);
    lv_obj_align(save_btn, LV_ALIGN_TOP_MID, 0, 200 + co2_extra);
    lv_obj_set_style_bg_color(save_btn, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_add_event_cb(save_btn, save_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *save_lbl = lv_label_create(save_btn);
    lv_label_set_text(save_lbl, LV_SYMBOL_OK " Save");
    lv_obj_center(save_lbl);

    /* ── keyboard (hidden until a field is focused) ─────────────────── */
    s_kbd = lv_keyboard_create(s_overlay);
    lv_obj_set_size(s_kbd, 240, 140);
    lv_obj_align(s_kbd, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(s_kbd, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_flag(s_kbd, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_kbd, kbd_done_cb, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(s_kbd, kbd_done_cb, LV_EVENT_CANCEL, NULL);
}
