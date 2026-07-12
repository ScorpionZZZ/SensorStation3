// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "ui.h"
#include "nvs_settings.h"
#include "wifi_manager.h"
#include "lvgl.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ui_wifi";

#define MAX_APS  16

/* ── state ───────────────────────────────────────────────────────────── */

static lv_obj_t   *s_overlay;
static lv_obj_t   *s_scan_cont;
static lv_obj_t   *s_pass_cont;
static lv_obj_t   *s_list;
static lv_obj_t   *s_scan_status;
static lv_obj_t   *s_conn_status;   /* current connection status line */
static lv_obj_t   *s_pass_title;    /* "Connect to: SSID" header label */
static lv_obj_t   *s_ta_pass;
static lv_obj_t   *s_kbd;
static lv_timer_t *s_scan_poll;

static char s_selected_ssid[NVS_SETTINGS_SSID_LEN + 1];

/* ── forward declarations ────────────────────────────────────────────── */

static void show_pass_view(void);
static void show_scan_view(void);

/* ── close ────────────────────────────────────────────────────────────── */

static void close_modal(void)
{
    if (s_scan_poll) {
        lv_timer_delete(s_scan_poll);
        s_scan_poll = NULL;
    }
    if (s_overlay) {
        lv_obj_delete(s_overlay);
        s_overlay     = NULL;
        s_scan_cont   = NULL;
        s_pass_cont   = NULL;
        s_list        = NULL;
        s_scan_status = NULL;
        s_conn_status = NULL;
        s_pass_title  = NULL;
        s_ta_pass     = NULL;
        s_kbd         = NULL;
    }
}

/* ── connection status line (scan view) ──────────────────────────────── */

static void refresh_conn_status(void)
{
    if (!s_conn_status) return;
    char buf[64];
    switch (wifi_manager_get_state()) {
    case WIFI_STATE_CONNECTED: {
        char ip[20], ssid[NVS_SETTINGS_SSID_LEN + 1] = {0};
        wifi_manager_get_ip(ip, sizeof(ip));
        nvs_settings_get_wifi_ssid(ssid, sizeof(ssid));
        snprintf(buf, sizeof(buf), LV_SYMBOL_OK " %s  %s", ssid, ip);
        lv_label_set_text(s_conn_status, buf);
        lv_obj_set_style_text_color(s_conn_status, lv_palette_main(LV_PALETTE_GREEN), 0);
        break;
    }
    case WIFI_STATE_CONNECTING:
        lv_label_set_text(s_conn_status, "Connecting...");
        lv_obj_set_style_text_color(s_conn_status, lv_palette_main(LV_PALETTE_YELLOW), 0);
        break;
    case WIFI_STATE_FAILED:
        lv_label_set_text(s_conn_status, LV_SYMBOL_CLOSE " Connection failed");
        lv_obj_set_style_text_color(s_conn_status, lv_palette_main(LV_PALETTE_RED), 0);
        break;
    default:
        lv_label_set_text(s_conn_status, "Not connected");
        lv_obj_set_style_text_color(s_conn_status, lv_palette_main(LV_PALETTE_GREY), 0);
        break;
    }
}

static void conn_status_timer_cb(lv_timer_t *t)
{
    (void)t;
    refresh_conn_status();
}

/* ── RSSI → bar string ───────────────────────────────────────────────── */

static const char *rssi_bars(int8_t rssi)
{
    if (rssi >= -55) return "####";
    if (rssi >= -67) return "### ";
    if (rssi >= -78) return "##  ";
    return "#   ";
}

/* ── scan list ────────────────────────────────────────────────────────── */

static void ap_btn_cb(lv_event_t *e)
{
    const char *ssid = (const char *)lv_event_get_user_data(e);
    strncpy(s_selected_ssid, ssid, sizeof(s_selected_ssid) - 1);
    s_selected_ssid[sizeof(s_selected_ssid) - 1] = '\0';
    show_pass_view();
}

static void populate_list(void)
{
    wifi_ap_record_t aps[MAX_APS];
    uint16_t count = MAX_APS;

    if (wifi_manager_scan_get(aps, &count) != ESP_OK || count == 0) {
        lv_label_set_text(s_scan_status, "No networks found");
        return;
    }

    lv_label_set_text(s_scan_status, "Tap a network to connect");
    lv_obj_clean(s_list);

    static char ssid_pool[MAX_APS][NVS_SETTINGS_SSID_LEN + 1];
    for (uint16_t i = 0; i < count && i < MAX_APS; i++) {
        strncpy(ssid_pool[i], (char *)aps[i].ssid, NVS_SETTINGS_SSID_LEN);
        ssid_pool[i][NVS_SETTINGS_SSID_LEN] = '\0';

        char label[48];
        snprintf(label, sizeof(label), "%-22s %s", ssid_pool[i], rssi_bars(aps[i].rssi));
        lv_obj_t *btn = lv_list_add_button(s_list, NULL, label);
        lv_obj_add_event_cb(btn, ap_btn_cb, LV_EVENT_CLICKED, ssid_pool[i]);
    }
}

static void scan_poll_cb(lv_timer_t *t)
{
    (void)t;
    if (!wifi_manager_scan_ready()) return;
    lv_timer_delete(s_scan_poll);
    s_scan_poll = NULL;
    populate_list();
}

/* ── scan view callbacks ─────────────────────────────────────────────── */

static void scan_btn_cb(lv_event_t *e)
{
    (void)e;
    lv_label_set_text(s_scan_status, "Scanning...");
    lv_obj_clean(s_list);
    wifi_manager_scan_start();
    if (s_scan_poll) lv_timer_delete(s_scan_poll);
    s_scan_poll = lv_timer_create(scan_poll_cb, 300, NULL);
}

static void close_btn_cb(lv_event_t *e)
{
    (void)e;
    close_modal();
}

static void show_scan_view(void)
{
    lv_obj_remove_flag(s_scan_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_pass_cont,    LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_kbd,          LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(s_kbd, NULL);
}

/* ── password view callbacks ─────────────────────────────────────────── */

static void connect_btn_cb(lv_event_t *e)
{
    (void)e;
    const char *pass = lv_textarea_get_text(s_ta_pass);
    nvs_settings_set_wifi_ssid(s_selected_ssid);
    nvs_settings_set_wifi_pass(pass);
    wifi_manager_connect(s_selected_ssid, pass);
    ESP_LOGI(TAG, "connecting to \"%s\"", s_selected_ssid);
    close_modal();
}

static void back_btn_cb(lv_event_t *e)
{
    (void)e;
    show_scan_view();
}

static void show_pass_view(void)
{
    lv_obj_add_flag(s_scan_cont,    LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_pass_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_kbd,       LV_OBJ_FLAG_HIDDEN);

    char buf[48];
    snprintf(buf, sizeof(buf), "Connect to: %.28s", s_selected_ssid);
    lv_label_set_text(s_pass_title, buf);

    lv_textarea_set_text(s_ta_pass, "");
    lv_keyboard_set_textarea(s_kbd, s_ta_pass);
}

/* ── public ──────────────────────────────────────────────────────────── */

void ui_wifi_show(void)
{
    if (s_overlay) return;

    /* ── overlay ────────────────────────────────────────────────────── */
    s_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_overlay, 240, 320);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_overlay, 0, 0);

    /* ══════════════════════════════════════════════════════════════════
     * SCAN VIEW
     * ══════════════════════════════════════════════════════════════════ */
    s_scan_cont = lv_obj_create(s_overlay);
    lv_obj_remove_flag(s_scan_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_scan_cont, 240, 320);
    lv_obj_set_pos(s_scan_cont, 0, 0);
    lv_obj_set_style_bg_color(s_scan_cont, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_scan_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_scan_cont, 0, 0);
    lv_obj_set_style_pad_all(s_scan_cont, 0, 0);

    /* Header */
    lv_obj_t *scan_hdr = lv_obj_create(s_scan_cont);
    lv_obj_remove_flag(scan_hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(scan_hdr, 240, 38);
    lv_obj_set_pos(scan_hdr, 0, 0);
    lv_obj_set_style_bg_color(scan_hdr, lv_color_make(20, 20, 60), 0);
    lv_obj_set_style_bg_opa(scan_hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scan_hdr, 0, 0);
    lv_obj_set_style_pad_all(scan_hdr, 0, 0);
    lv_obj_t *scan_title = lv_label_create(scan_hdr);
    lv_label_set_text(scan_title, LV_SYMBOL_WIFI " WiFi Configuration");
    lv_obj_set_style_text_color(scan_title, lv_color_white(), 0);
    lv_obj_align(scan_title, LV_ALIGN_LEFT_MID, 8, 0);

    /* Current connection status */
    s_conn_status = lv_label_create(s_scan_cont);
    lv_obj_set_width(s_conn_status, 224);
    lv_label_set_long_mode(s_conn_status, LV_LABEL_LONG_DOT);
    lv_obj_align(s_conn_status, LV_ALIGN_TOP_LEFT, 8, 44);
    refresh_conn_status();
    lv_timer_create(conn_status_timer_cb, 2000, NULL);

    /* Scan button */
    lv_obj_t *scan_btn = lv_button_create(s_scan_cont);
    lv_obj_set_size(scan_btn, 224, 34);
    lv_obj_align(scan_btn, LV_ALIGN_TOP_MID, 0, 66);
    lv_obj_add_event_cb(scan_btn, scan_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *scan_lbl = lv_label_create(scan_btn);
    lv_label_set_text(scan_lbl, LV_SYMBOL_REFRESH " Scan networks");
    lv_obj_center(scan_lbl);

    /* Scan status */
    s_scan_status = lv_label_create(s_scan_cont);
    lv_label_set_text(s_scan_status, "Tap Scan to find networks");
    lv_obj_set_style_text_color(s_scan_status, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_align(s_scan_status, LV_ALIGN_TOP_LEFT, 8, 106);

    /* Network list — fixed height that leaves room for buttons */
    s_list = lv_list_create(s_scan_cont);
    lv_obj_set_size(s_list, 224, 164);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 122);
    lv_obj_set_style_bg_color(s_list, lv_color_make(15, 15, 15), 0);
    lv_obj_set_style_border_width(s_list, 1, 0);

    /* Close button */
    lv_obj_t *close_btn = lv_button_create(s_scan_cont);
    lv_obj_set_size(close_btn, 80, 34);
    lv_obj_align(close_btn, LV_ALIGN_TOP_LEFT, 8, 292);
    lv_obj_set_style_bg_color(close_btn, lv_color_make(60, 30, 30), 0);
    lv_obj_add_event_cb(close_btn, close_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(close_lbl);

    /* ══════════════════════════════════════════════════════════════════
     * PASSWORD VIEW
     * ══════════════════════════════════════════════════════════════════ */
    s_pass_cont = lv_obj_create(s_overlay);
    lv_obj_add_flag(s_pass_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_pass_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_pass_cont, 240, 320);
    lv_obj_set_pos(s_pass_cont, 0, 0);
    lv_obj_set_style_bg_color(s_pass_cont, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_pass_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_pass_cont, 0, 0);
    lv_obj_set_style_pad_all(s_pass_cont, 0, 0);

    /* Header — updated in show_pass_view() */
    lv_obj_t *pass_hdr = lv_obj_create(s_pass_cont);
    lv_obj_remove_flag(pass_hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(pass_hdr, 240, 38);
    lv_obj_set_pos(pass_hdr, 0, 0);
    lv_obj_set_style_bg_color(pass_hdr, lv_color_make(20, 20, 60), 0);
    lv_obj_set_style_bg_opa(pass_hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pass_hdr, 0, 0);
    lv_obj_set_style_pad_all(pass_hdr, 0, 0);
    s_pass_title = lv_label_create(pass_hdr);
    lv_label_set_text(s_pass_title, "");
    lv_obj_set_style_text_color(s_pass_title, lv_color_white(), 0);
    lv_obj_set_width(s_pass_title, 224);
    lv_label_set_long_mode(s_pass_title, LV_LABEL_LONG_DOT);
    lv_obj_align(s_pass_title, LV_ALIGN_LEFT_MID, 8, 0);

    /* Password label + textarea */
    lv_obj_t *pass_lbl = lv_label_create(s_pass_cont);
    lv_label_set_text(pass_lbl, "Password:");
    lv_obj_set_style_text_color(pass_lbl, lv_color_white(), 0);
    lv_obj_align(pass_lbl, LV_ALIGN_TOP_LEFT, 8, 44);

    s_ta_pass = lv_textarea_create(s_pass_cont);
    lv_obj_set_size(s_ta_pass, 224, 36);
    lv_obj_align(s_ta_pass, LV_ALIGN_TOP_MID, 0, 62);
    lv_textarea_set_one_line(s_ta_pass, true);
    lv_textarea_set_max_length(s_ta_pass, NVS_SETTINGS_PASS_LEN);
    lv_textarea_set_placeholder_text(s_ta_pass, "enter password");

    /* [← Back] [Connect] row */
    lv_obj_t *back_btn = lv_button_create(s_pass_cont);
    lv_obj_set_size(back_btn, 96, 36);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 8, 106);
    lv_obj_set_style_bg_color(back_btn, lv_color_make(60, 30, 30), 0);
    lv_obj_add_event_cb(back_btn, back_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_lbl);

    lv_obj_t *conn_btn = lv_button_create(s_pass_cont);
    lv_obj_set_size(conn_btn, 120, 36);
    lv_obj_align(conn_btn, LV_ALIGN_TOP_RIGHT, -8, 106);
    lv_obj_set_style_bg_color(conn_btn, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_add_event_cb(conn_btn, connect_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *conn_lbl = lv_label_create(conn_btn);
    lv_label_set_text(conn_lbl, LV_SYMBOL_WIFI " Connect");
    lv_obj_center(conn_lbl);

    /* ── keyboard — always at bottom, shown only in password view ───── */
    s_kbd = lv_keyboard_create(s_overlay);
    lv_obj_set_size(s_kbd, 240, 140);
    lv_obj_align(s_kbd, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(s_kbd, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_flag(s_kbd, LV_OBJ_FLAG_HIDDEN);
}
