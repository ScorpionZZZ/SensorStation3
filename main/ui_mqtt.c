// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "ui.h"
#include "nvs_settings.h"
#include "tb_mqtt.h"
#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "ui_mqtt";

/* ── state ─────────────────────────────────────────────────────────── */

static lv_obj_t *s_overlay;
static lv_obj_t *s_status_lbl;
static lv_obj_t *s_ta_uri;
static lv_obj_t *s_ta_token;
static lv_obj_t *s_ta_interval;
static lv_obj_t *s_kbd;

/* ── status line ────────────────────────────────────────────────────── */

static void refresh_status(void)
{
    if (!s_status_lbl) return;

    char token[NVS_SETTINGS_TB_TOKEN_LEN + 1] = {0};
    bool enabled = true;
    nvs_settings_get_tb_token(token, sizeof(token));
    nvs_settings_get_tb_enabled(&enabled);

    if (!enabled) {
        lv_label_set_text(s_status_lbl, "Telemetry disabled");
        lv_obj_set_style_text_color(s_status_lbl, lv_palette_main(LV_PALETTE_GREY), 0);
    } else if (token[0] == '\0') {
        lv_label_set_text(s_status_lbl, "Not configured — enter token below");
        lv_obj_set_style_text_color(s_status_lbl, lv_palette_main(LV_PALETTE_GREY), 0);
    } else if (tb_mqtt_is_connected()) {
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
    if (ta == s_ta_interval)
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
    const char *uri   = lv_textarea_get_text(s_ta_uri);
    const char *token = lv_textarea_get_text(s_ta_token);
    const char *ivstr = lv_textarea_get_text(s_ta_interval);

    uint16_t interval = (uint16_t)atoi(ivstr);
    if (interval == 0) interval = (uint16_t)NVS_SETTINGS_TB_INTERVAL_DEFAULT;

    nvs_settings_set_tb_uri(uri);
    nvs_settings_set_tb_token(token);
    nvs_settings_set_tb_interval(interval);
    ESP_LOGI(TAG, "saved uri=%s interval=%us", uri, interval);

    tb_mqtt_start();

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
    s_overlay     = NULL;
    s_status_lbl  = NULL;
    s_ta_uri      = NULL;
    s_ta_token    = NULL;
    s_ta_interval = NULL;
    s_kbd         = NULL;
}

/* ── public ─────────────────────────────────────────────────────────── */

void ui_mqtt_show(void)
{
    if (s_overlay) return;

    char uri[NVS_SETTINGS_TB_URI_LEN + 1]     = {0};
    char token[NVS_SETTINGS_TB_TOKEN_LEN + 1] = {0};
    uint16_t interval = (uint16_t)NVS_SETTINGS_TB_INTERVAL_DEFAULT;
    nvs_settings_get_tb_uri(uri, sizeof(uri));
    nvs_settings_get_tb_token(token, sizeof(token));
    nvs_settings_get_tb_interval(&interval);

    /* ── overlay ────────────────────────────────────────────────────── */
    s_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_overlay, 240, 320);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_overlay, 0, 0);

    /* ── header ─────────────────────────────────────────────────────── */
    lv_obj_t *hdr = lv_obj_create(s_overlay);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(hdr, 240, 38);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_make(20, 40, 20), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS " ThingsBoard MQTT");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

    /* ── status line ────────────────────────────────────────────────── */
    s_status_lbl = lv_label_create(s_overlay);
    lv_obj_set_width(s_status_lbl, 224);
    lv_label_set_long_mode(s_status_lbl, LV_LABEL_LONG_DOT);
    lv_obj_align(s_status_lbl, LV_ALIGN_TOP_LEFT, 8, 44);
    refresh_status();

    lv_timer_create(status_timer_cb, 2000, NULL);

    /* ── Broker URI ─────────────────────────────────────────────────── */
    lv_obj_t *uri_lbl = lv_label_create(s_overlay);
    lv_label_set_text(uri_lbl, "Broker URI:");
    lv_obj_set_style_text_color(uri_lbl, lv_color_white(), 0);
    lv_obj_align(uri_lbl, LV_ALIGN_TOP_LEFT, 8, 66);

    s_ta_uri = lv_textarea_create(s_overlay);
    lv_obj_set_size(s_ta_uri, 224, 36);
    lv_obj_align(s_ta_uri, LV_ALIGN_TOP_MID, 0, 82);
    lv_textarea_set_one_line(s_ta_uri, true);
    lv_textarea_set_max_length(s_ta_uri, NVS_SETTINGS_TB_URI_LEN);
    lv_textarea_set_placeholder_text(s_ta_uri, "mqtt://demo.thingsboard.io");
    lv_textarea_set_text(s_ta_uri, uri);
    lv_obj_add_event_cb(s_ta_uri, ta_focused_cb, LV_EVENT_FOCUSED, NULL);

    /* ── Device Token ───────────────────────────────────────────────── */
    lv_obj_t *token_lbl = lv_label_create(s_overlay);
    lv_label_set_text(token_lbl, "Device Token:");
    lv_obj_set_style_text_color(token_lbl, lv_color_white(), 0);
    lv_obj_align(token_lbl, LV_ALIGN_TOP_LEFT, 8, 124);

    s_ta_token = lv_textarea_create(s_overlay);
    lv_obj_set_size(s_ta_token, 224, 36);
    lv_obj_align(s_ta_token, LV_ALIGN_TOP_MID, 0, 140);
    lv_textarea_set_one_line(s_ta_token, true);
    lv_textarea_set_max_length(s_ta_token, NVS_SETTINGS_TB_TOKEN_LEN);
    lv_textarea_set_placeholder_text(s_ta_token, "Device access token");
    lv_textarea_set_text(s_ta_token, token);
    lv_obj_add_event_cb(s_ta_token, ta_focused_cb, LV_EVENT_FOCUSED, NULL);

    /* ── Interval row ───────────────────────────────────────────────── */
    lv_obj_t *iv_row = lv_obj_create(s_overlay);
    lv_obj_remove_flag(iv_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(iv_row, 224, 32);
    lv_obj_align(iv_row, LV_ALIGN_TOP_MID, 0, 184);
    lv_obj_set_style_bg_opa(iv_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(iv_row, 0, 0);
    lv_obj_set_style_pad_all(iv_row, 0, 0);
    lv_obj_set_style_pad_column(iv_row, 6, 0);
    lv_obj_set_layout(iv_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(iv_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(iv_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *iv_lbl = lv_label_create(iv_row);
    lv_label_set_text(iv_lbl, "Publish interval:");
    lv_obj_set_style_text_color(iv_lbl, lv_color_white(), 0);

    s_ta_interval = lv_textarea_create(iv_row);
    lv_obj_set_size(s_ta_interval, 52, 32);
    lv_textarea_set_one_line(s_ta_interval, true);
    lv_textarea_set_max_length(s_ta_interval, 4);
    char iv_buf[8];
    snprintf(iv_buf, sizeof(iv_buf), "%u", interval);
    lv_textarea_set_text(s_ta_interval, iv_buf);
    lv_obj_add_event_cb(s_ta_interval, ta_focused_cb, LV_EVENT_FOCUSED, NULL);

    lv_obj_t *iv_unit = lv_label_create(iv_row);
    lv_label_set_text(iv_unit, "s");
    lv_obj_set_style_text_color(iv_unit, lv_palette_main(LV_PALETTE_GREY), 0);

    /* ── Save button ────────────────────────────────────────────────── */
    lv_obj_t *save_btn = lv_button_create(s_overlay);
    lv_obj_set_size(save_btn, 224, 36);
    lv_obj_align(save_btn, LV_ALIGN_TOP_MID, 0, 224);
    lv_obj_set_style_bg_color(save_btn, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_add_event_cb(save_btn, save_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *save_lbl = lv_label_create(save_btn);
    lv_label_set_text(save_lbl, LV_SYMBOL_OK " Save");
    lv_obj_center(save_lbl);

    /* ── Back button ────────────────────────────────────────────────── */
    lv_obj_t *back_btn = lv_button_create(s_overlay);
    lv_obj_set_size(back_btn, 80, 34);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 8, 270);
    lv_obj_set_style_bg_color(back_btn, lv_color_make(60, 30, 30), 0);
    lv_obj_add_event_cb(back_btn, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_lbl);

    /* ── keyboard (hidden until a field is focused) ─────────────────── */
    s_kbd = lv_keyboard_create(s_overlay);
    lv_obj_set_size(s_kbd, 240, 140);
    lv_obj_align(s_kbd, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(s_kbd, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_flag(s_kbd, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_kbd, kbd_done_cb, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(s_kbd, kbd_done_cb, LV_EVENT_CANCEL, NULL);
}
