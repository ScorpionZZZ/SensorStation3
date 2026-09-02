// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "ui.h"
#include "nvs_settings.h"
#include "ha_mqtt.h"
#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui_ha";

/* Overlay is 240x320 with a 38 px header and a 140 px keyboard — same
 * convention as ui_domoticz.c/ui_mqtt.c's History sub-overlay. */
#define HA_HDR_H              38
#define HA_KBD_H              140
#define HA_CONTENT_FULL_H     (320 - HA_HDR_H)
#define HA_CONTENT_VISIBLE_H  (320 - HA_HDR_H - HA_KBD_H)

/* ── state ─────────────────────────────────────────────────────────── */

static lv_obj_t *s_overlay;
static lv_obj_t *s_status_lbl;
static lv_obj_t *s_ta_uri;
static lv_obj_t *s_ta_user;
static lv_obj_t *s_ta_pass;
static lv_obj_t *s_kbd;
static lv_timer_t *s_status_timer;

/* ── History source sub-overlay: own screen, not more fields crammed into
 * the overlay above — see ui_domoticz.c's identical History sub-overlay for
 * why (LVGL's fixed-size memory pool). */
static lv_obj_t *s_h_overlay;
static lv_obj_t *s_h_content;
static lv_obj_t *s_h_ta_url;
static lv_obj_t *s_h_ta_token;
static lv_obj_t *s_h_ta_temp_entity;
static lv_obj_t *s_h_ta_hum_entity;
static lv_obj_t *s_h_kbd;

static void ui_ha_history_show_cb(lv_event_t *e);

/* ── status line ────────────────────────────────────────────────────── */

static void refresh_status(void)
{
    if (!s_status_lbl) return;

    char uri[NVS_SETTINGS_HA_URI_LEN + 1] = {0};
    bool enabled = false;
    nvs_settings_get_ha_uri(uri, sizeof(uri));
    nvs_settings_get_ha_enabled(&enabled);

    if (!enabled) {
        lv_label_set_text(s_status_lbl, "Disabled");
        lv_obj_set_style_text_color(s_status_lbl, lv_palette_main(LV_PALETTE_GREY), 0);
    } else if (uri[0] == '\0') {
        lv_label_set_text(s_status_lbl, "Not configured — enter URI below");
        lv_obj_set_style_text_color(s_status_lbl, lv_palette_main(LV_PALETTE_GREY), 0);
    } else if (ha_mqtt_is_connected()) {
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
    const char *uri  = lv_textarea_get_text(s_ta_uri);
    const char *user = lv_textarea_get_text(s_ta_user);
    const char *pass = lv_textarea_get_text(s_ta_pass);

    nvs_settings_set_ha_uri(uri);
    nvs_settings_set_ha_user(user);
    nvs_settings_set_ha_pass(pass);
    ESP_LOGI(TAG, "saved uri=%s user=%s", uri, user);

    ha_mqtt_start();

    if (s_status_lbl) {
        lv_label_set_text(s_status_lbl, "Saved — connecting...");
        lv_obj_set_style_text_color(s_status_lbl, lv_palette_main(LV_PALETTE_CYAN), 0);
    }
}

/* ── back ───────────────────────────────────────────────────────────── */

/* Tears down every widget/timer this overlay owns. Called both by the Back
 * button and when navigating into the History sub-overlay (siblings under
 * lv_layer_top() — see ui_domoticz.c's identical rationale: never leave two
 * overlays resident at once, it exhausts LVGL's fixed memory pool). */
static void ha_overlay_close(void)
{
    if (!s_overlay) return;
    if (s_status_timer) {
        lv_timer_delete(s_status_timer);
        s_status_timer = NULL;
    }
    lv_obj_delete(s_overlay);
    s_overlay    = NULL;
    s_status_lbl = NULL;
    s_ta_uri     = NULL;
    s_ta_user    = NULL;
    s_ta_pass    = NULL;
    s_kbd        = NULL;
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    ha_overlay_close();
}

/* ── public ─────────────────────────────────────────────────────────── */

void ui_home_assistant_show(void)
{
    if (s_overlay) return;

    char uri[NVS_SETTINGS_HA_URI_LEN + 1]   = {0};
    char user[NVS_SETTINGS_HA_USER_LEN + 1] = {0};
    char pass[NVS_SETTINGS_HA_PASS_LEN + 1] = {0};
    nvs_settings_get_ha_uri(uri, sizeof(uri));
    nvs_settings_get_ha_user(user, sizeof(user));
    nvs_settings_get_ha_pass(pass, sizeof(pass));

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
    lv_label_set_text(title, LV_SYMBOL_SETTINGS " Home Assistant");
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
    s_status_timer = lv_timer_create(status_timer_cb, 2000, NULL);

    /* ── Broker URI ─────────────────────────────────────────────────── */
    s_ta_uri = lv_textarea_create(s_overlay);
    lv_obj_set_size(s_ta_uri, 224, 36);
    lv_obj_align(s_ta_uri, LV_ALIGN_TOP_MID, 0, 54);
    lv_textarea_set_one_line(s_ta_uri, true);
    lv_textarea_set_max_length(s_ta_uri, NVS_SETTINGS_HA_URI_LEN);
    lv_textarea_set_placeholder_text(s_ta_uri, "mqtt://192.168.1.100:1883");
    lv_textarea_set_text(s_ta_uri, uri);
    lv_obj_add_event_cb(s_ta_uri, ta_focused_cb, LV_EVENT_FOCUSED, NULL);

    /* ── Username ───────────────────────────────────────────────────── */
    s_ta_user = lv_textarea_create(s_overlay);
    lv_obj_set_size(s_ta_user, 224, 30);
    lv_obj_align(s_ta_user, LV_ALIGN_TOP_MID, 0, 94);
    lv_textarea_set_one_line(s_ta_user, true);
    lv_textarea_set_max_length(s_ta_user, NVS_SETTINGS_HA_USER_LEN);
    lv_textarea_set_placeholder_text(s_ta_user, "Username (optional)");
    lv_textarea_set_text(s_ta_user, user);
    lv_obj_add_event_cb(s_ta_user, ta_focused_cb, LV_EVENT_FOCUSED, NULL);

    /* ── Password ───────────────────────────────────────────────────── */
    s_ta_pass = lv_textarea_create(s_overlay);
    lv_obj_set_size(s_ta_pass, 224, 30);
    lv_obj_align(s_ta_pass, LV_ALIGN_TOP_MID, 0, 128);
    lv_textarea_set_one_line(s_ta_pass, true);
    lv_textarea_set_max_length(s_ta_pass, NVS_SETTINGS_HA_PASS_LEN);
    lv_textarea_set_placeholder_text(s_ta_pass, "Password (optional)");
    lv_textarea_set_password_mode(s_ta_pass, true);
    lv_textarea_set_text(s_ta_pass, pass);
    lv_obj_add_event_cb(s_ta_pass, ta_focused_cb, LV_EVENT_FOCUSED, NULL);

    /* ── Save button ────────────────────────────────────────────────── */
    lv_obj_t *save_btn = lv_button_create(s_overlay);
    lv_obj_set_size(save_btn, 224, 36);
    lv_obj_align(save_btn, LV_ALIGN_TOP_MID, 0, 166);
    lv_obj_set_style_bg_color(save_btn, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_add_event_cb(save_btn, save_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *save_lbl = lv_label_create(save_btn);
    lv_label_set_text(save_lbl, LV_SYMBOL_OK " Save");
    lv_obj_center(save_lbl);

    /* ── History chart source button (own overlay — see s_h_* above) ─── */
    lv_obj_t *hist_btn = lv_button_create(s_overlay);
    lv_obj_set_size(hist_btn, 224, 36);
    lv_obj_align(hist_btn, LV_ALIGN_TOP_MID, 0, 204);
    lv_obj_add_event_cb(hist_btn, ui_ha_history_show_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *hist_btn_lbl = lv_label_create(hist_btn);
    lv_label_set_text(hist_btn_lbl, LV_SYMBOL_SETTINGS " History Chart Source");
    lv_obj_center(hist_btn_lbl);

    /* ── keyboard (hidden until a field is focused) ─────────────────── */
    s_kbd = lv_keyboard_create(s_overlay);
    lv_obj_set_size(s_kbd, 240, 140);
    lv_obj_align(s_kbd, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(s_kbd, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_flag(s_kbd, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_kbd, kbd_done_cb, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(s_kbd, kbd_done_cb, LV_EVENT_CANCEL, NULL);
}

/* ── History source sub-overlay ───────────────────────────────────────── */

static void hist_ta_focused_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    lv_keyboard_set_textarea(s_h_kbd, ta);
    lv_obj_remove_flag(s_h_kbd, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(s_h_content, HA_CONTENT_VISIBLE_H);
    lv_obj_scroll_to_view_recursive(ta, LV_ANIM_ON);
}

static void hist_kbd_done_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_h_kbd, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(s_h_kbd, NULL);
    lv_obj_set_height(s_h_content, HA_CONTENT_FULL_H);
    lv_obj_scroll_to_y(s_h_content, 0, LV_ANIM_ON);
}

static void hist_save_cb(lv_event_t *e)
{
    (void)e;
    const char *url         = lv_textarea_get_text(s_h_ta_url);
    const char *token       = lv_textarea_get_text(s_h_ta_token);
    const char *temp_entity = lv_textarea_get_text(s_h_ta_temp_entity);
    const char *hum_entity  = lv_textarea_get_text(s_h_ta_hum_entity);

    nvs_settings_set_ha_http_url(url);
    nvs_settings_set_ha_http_token(token);
    nvs_settings_set_ha_temp_entity(temp_entity);
    nvs_settings_set_ha_hum_entity(hum_entity);
    ESP_LOGI(TAG, "history saved url=%s temp_entity=%s hum_entity=%s",
             url, temp_entity, hum_entity);
}

static void hist_back_cb(lv_event_t *e)
{
    (void)e;
    if (!s_h_overlay) return;
    lv_obj_delete(s_h_overlay);
    s_h_overlay        = NULL;
    s_h_content        = NULL;
    s_h_ta_url         = NULL;
    s_h_ta_token       = NULL;
    s_h_ta_temp_entity = NULL;
    s_h_ta_hum_entity  = NULL;
    s_h_kbd            = NULL;
    ui_home_assistant_show();   /* return to the Home Assistant screen, now torn down */
}

static void ui_ha_history_show(void)
{
    if (s_h_overlay) return;

    char url[NVS_SETTINGS_HA_HTTP_URL_LEN + 1]       = {0};
    char token[NVS_SETTINGS_HA_HTTP_TOKEN_LEN + 1]   = {0};
    char temp_entity[NVS_SETTINGS_HA_TEMP_ENTITY_LEN + 1] = {0};
    char hum_entity[NVS_SETTINGS_HA_HUM_ENTITY_LEN + 1]   = {0};
    nvs_settings_get_ha_http_url(url, sizeof(url));
    nvs_settings_get_ha_http_token(token, sizeof(token));
    nvs_settings_get_ha_temp_entity(temp_entity, sizeof(temp_entity));
    nvs_settings_get_ha_hum_entity(hum_entity, sizeof(hum_entity));

    s_h_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_flag(s_h_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_h_overlay, 240, 320);
    lv_obj_set_pos(s_h_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_h_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_h_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_h_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_h_overlay, 0, 0);

    lv_obj_t *hdr = lv_obj_create(s_h_overlay);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(hdr, 240, 38);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_make(0, 40, 50), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS " History Chart Source");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

    lv_obj_t *back_btn = lv_button_create(hdr);
    lv_obj_set_size(back_btn, 60, 28);
    lv_obj_align(back_btn, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_make(60, 30, 30), 0);
    lv_obj_add_event_cb(back_btn, hist_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_lbl);

    s_h_content = lv_obj_create(s_h_overlay);
    lv_obj_set_size(s_h_content, 240, HA_CONTENT_FULL_H);
    lv_obj_set_pos(s_h_content, 0, HA_HDR_H);
    lv_obj_set_scroll_dir(s_h_content, LV_DIR_VER);
    lv_obj_set_style_bg_opa(s_h_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_h_content, 0, 0);
    lv_obj_set_style_pad_all(s_h_content, 0, 0);

    lv_obj_t *hint = lv_label_create(s_h_content);
    lv_label_set_text(hint, "Feeds the main screen's 24h chart\non boot. Needs a Long-Lived Access\nToken and the entity IDs from HA's\nEntities list — not the MQTT login.");
    lv_obj_set_style_text_color(hint, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_width(hint, 224);
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 8, 4);

    /* ── REST API base URL ─────────────────────────────────────────── */
    s_h_ta_url = lv_textarea_create(s_h_content);
    lv_obj_set_size(s_h_ta_url, 224, 30);
    lv_obj_align(s_h_ta_url, LV_ALIGN_TOP_MID, 0, 72);
    lv_textarea_set_one_line(s_h_ta_url, true);
    lv_textarea_set_max_length(s_h_ta_url, NVS_SETTINGS_HA_HTTP_URL_LEN);
    lv_textarea_set_placeholder_text(s_h_ta_url, "Base URL: http://host:8123");
    lv_textarea_set_text(s_h_ta_url, url);
    lv_obj_add_event_cb(s_h_ta_url, hist_ta_focused_cb, LV_EVENT_FOCUSED, NULL);

    /* ── Long-Lived Access Token (Profile → Security in HA) ───────────── */
    s_h_ta_token = lv_textarea_create(s_h_content);
    lv_obj_set_size(s_h_ta_token, 224, 30);
    lv_obj_align(s_h_ta_token, LV_ALIGN_TOP_MID, 0, 106);
    lv_textarea_set_one_line(s_h_ta_token, true);
    lv_textarea_set_max_length(s_h_ta_token, NVS_SETTINGS_HA_HTTP_TOKEN_LEN);
    lv_textarea_set_placeholder_text(s_h_ta_token, "Long-Lived Access Token");
    lv_textarea_set_text(s_h_ta_token, token);
    lv_obj_add_event_cb(s_h_ta_token, hist_ta_focused_cb, LV_EVENT_FOCUSED, NULL);

    /* ── Temperature entity_id (required) ─────────────────────────────── */
    s_h_ta_temp_entity = lv_textarea_create(s_h_content);
    lv_obj_set_size(s_h_ta_temp_entity, 224, 30);
    lv_obj_align(s_h_ta_temp_entity, LV_ALIGN_TOP_MID, 0, 140);
    lv_textarea_set_one_line(s_h_ta_temp_entity, true);
    lv_textarea_set_max_length(s_h_ta_temp_entity, NVS_SETTINGS_HA_TEMP_ENTITY_LEN);
    lv_textarea_set_placeholder_text(s_h_ta_temp_entity, "sensor.xxx_temperature");
    lv_textarea_set_text(s_h_ta_temp_entity, temp_entity);
    lv_obj_add_event_cb(s_h_ta_temp_entity, hist_ta_focused_cb, LV_EVENT_FOCUSED, NULL);

    /* ── Humidity entity_id (optional) ─────────────────────────────────── */
    s_h_ta_hum_entity = lv_textarea_create(s_h_content);
    lv_obj_set_size(s_h_ta_hum_entity, 224, 30);
    lv_obj_align(s_h_ta_hum_entity, LV_ALIGN_TOP_MID, 0, 174);
    lv_textarea_set_one_line(s_h_ta_hum_entity, true);
    lv_textarea_set_max_length(s_h_ta_hum_entity, NVS_SETTINGS_HA_HUM_ENTITY_LEN);
    lv_textarea_set_placeholder_text(s_h_ta_hum_entity, "sensor.xxx_humidity (optional)");
    lv_textarea_set_text(s_h_ta_hum_entity, hum_entity);
    lv_obj_add_event_cb(s_h_ta_hum_entity, hist_ta_focused_cb, LV_EVENT_FOCUSED, NULL);

    lv_obj_t *save_btn = lv_button_create(s_h_content);
    lv_obj_set_size(save_btn, 224, 36);
    lv_obj_align(save_btn, LV_ALIGN_TOP_MID, 0, 212);
    lv_obj_set_style_bg_color(save_btn, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_add_event_cb(save_btn, hist_save_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *save_lbl2 = lv_label_create(save_btn);
    lv_label_set_text(save_lbl2, LV_SYMBOL_OK " Save");
    lv_obj_center(save_lbl2);

    s_h_kbd = lv_keyboard_create(s_h_overlay);
    lv_obj_set_size(s_h_kbd, 240, 140);
    lv_obj_align(s_h_kbd, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(s_h_kbd, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_flag(s_h_kbd, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_h_kbd, hist_kbd_done_cb, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(s_h_kbd, hist_kbd_done_cb, LV_EVENT_CANCEL, NULL);
}

static void ui_ha_history_show_cb(lv_event_t *e)
{
    (void)e;
    ha_overlay_close();   /* siblings under lv_layer_top() — only one at a time */
    ui_ha_history_show();
}