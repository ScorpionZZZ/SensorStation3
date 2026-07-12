// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "ui.h"
#include "nvs_settings.h"
#include "ota_manager.h"
#include "wifi_manager.h"
#include "lvgl.h"
#include "esp_log.h"
#include "build_info.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

static const char *TAG = "ui_ota";

/* ── Widget state ──────────────────────────────────────────────────────── */

static lv_obj_t *s_overlay;
static lv_obj_t *s_info_lbl;      /* check-state / version info line */
static lv_obj_t *s_url_section;   /* container: url label + textarea + checkbox */
static lv_obj_t *s_url_ta;
static lv_obj_t *s_kbd;
static lv_obj_t *s_start_btn;
static lv_obj_t *s_status_lbl;
static lv_obj_t *s_bar;
static lv_obj_t *s_back_btn;

/* ── Info line refresh ─────────────────────────────────────────────────── */

static void refresh_info_line(void)
{
    if (!s_info_lbl) return;

    char buf[48];
    ota_check_state_t state = ota_manager_check_state();

    switch (state) {
    case OTA_CHECK_CHECKING:
        lv_label_set_text(s_info_lbl, "Checking for updates...");
        lv_obj_set_style_text_color(s_info_lbl, lv_palette_main(LV_PALETTE_GREY), 0);
        break;
    case OTA_CHECK_AVAILABLE: {
        char sv[16] = {0};
        ota_manager_get_server_version(sv, sizeof(sv));
        if (sv[0])
            snprintf(buf, sizeof(buf), "New version: v%s available", sv);
        else
            snprintf(buf, sizeof(buf), "Update available!");
        lv_label_set_text(s_info_lbl, buf);
        lv_obj_set_style_text_color(s_info_lbl, lv_palette_main(LV_PALETTE_YELLOW), 0);
        break;
    }
    case OTA_CHECK_UP_TO_DATE:
        lv_label_set_text(s_info_lbl, "Firmware is up to date");
        lv_obj_set_style_text_color(s_info_lbl, lv_palette_main(LV_PALETTE_GREEN), 0);
        break;
    case OTA_CHECK_FAILED:
        lv_label_set_text(s_info_lbl, "Update check failed");
        lv_obj_set_style_text_color(s_info_lbl, lv_palette_main(LV_PALETTE_RED), 0);
        break;
    default:
        snprintf(buf, sizeof(buf), "Current: v" APP_VERSION_STR);
        lv_label_set_text(s_info_lbl, buf);
        lv_obj_set_style_text_color(s_info_lbl, lv_palette_main(LV_PALETTE_GREY), 0);
        break;
    }
}

static void info_timer_cb(lv_timer_t *t)
{
    (void)t;
    refresh_info_line();
}

/* ── Mode switching: config ↔ progress ────────────────────────────────── */

static void enter_progress_mode(void)
{
    if (s_url_section) lv_obj_add_flag(s_url_section, LV_OBJ_FLAG_HIDDEN);
    if (s_bar)         lv_obj_remove_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
    if (s_start_btn)   lv_obj_add_state(s_start_btn, LV_STATE_DISABLED);
    if (s_back_btn)    lv_obj_add_state(s_back_btn,  LV_STATE_DISABLED);
}

static void enter_config_mode(void)
{
    if (s_url_section) lv_obj_remove_flag(s_url_section, LV_OBJ_FLAG_HIDDEN);
    if (s_bar)         lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
    if (s_start_btn)   lv_obj_remove_state(s_start_btn, LV_STATE_DISABLED);
    if (s_back_btn)    lv_obj_remove_state(s_back_btn,  LV_STATE_DISABLED);
}

/* ── OTA progress callback (called from OTA task, NOT LVGL task) ───────── */

typedef struct {
    int         percent;
    ota_state_t state;
    char        msg[64];
} ota_ui_update_t;

static void ota_ui_apply(void *param)
{
    ota_ui_update_t *u = (ota_ui_update_t *)param;

    if (!s_status_lbl) goto done;

    lv_label_set_text(s_status_lbl, u->msg);

    switch (u->state) {
    case OTA_STATE_DOWNLOADING:
    case OTA_STATE_VERIFYING:
        enter_progress_mode();
        lv_bar_set_value(s_bar, u->percent, LV_ANIM_ON);
        lv_obj_set_style_text_color(s_status_lbl, lv_palette_main(LV_PALETTE_CYAN), 0);
        break;
    case OTA_STATE_REBOOTING:
        lv_bar_set_value(s_bar, 100, LV_ANIM_OFF);
        lv_obj_set_style_text_color(s_status_lbl, lv_palette_main(LV_PALETTE_GREEN), 0);
        break;
    case OTA_STATE_FAILED:
        enter_config_mode();
        lv_obj_set_style_text_color(s_status_lbl, lv_palette_main(LV_PALETTE_RED), 0);
        break;
    default:
        break;
    }

done:
    free(u);
}

static void ota_progress_cb(int percent, ota_state_t state,
                            const char *msg, void *user_data)
{
    (void)user_data;

    static int s_last_pct = -1;
    if (state == OTA_STATE_DOWNLOADING) {
        if (percent == s_last_pct) return;
        s_last_pct = percent;
    } else {
        s_last_pct = -1;
    }

    ota_ui_update_t *u = malloc(sizeof(*u));
    if (!u) return;
    u->percent = percent;
    u->state   = state;
    strncpy(u->msg, msg, sizeof(u->msg) - 1);
    u->msg[sizeof(u->msg) - 1] = '\0';

    lv_lock();
    lv_async_call(ota_ui_apply, u);
    lv_unlock();
}

/* ── Keyboard ──────────────────────────────────────────────────────────── */

static void kbd_done_cb(lv_event_t *e)
{
    (void)e;
    if (s_kbd) lv_obj_add_flag(s_kbd, LV_OBJ_FLAG_HIDDEN);
}

static void ta_focused_cb(lv_event_t *e)
{
    (void)e;
    if (s_kbd) lv_obj_remove_flag(s_kbd, LV_OBJ_FLAG_HIDDEN);
}

/* ── Auto-update checkbox ──────────────────────────────────────────────── */

static void auto_cb(lv_event_t *e)
{
    lv_obj_t *cb = lv_event_get_target(e);
    bool enabled = lv_obj_has_state(cb, LV_STATE_CHECKED);
    nvs_settings_set_ota_auto(enabled);
    ota_manager_set_auto(enabled);
    ESP_LOGI(TAG, "auto-update %s", enabled ? "on" : "off");
}

/* ── Start button ──────────────────────────────────────────────────────── */

static void start_btn_cb(lv_event_t *e)
{
    (void)e;

    if (wifi_manager_get_state() != WIFI_STATE_CONNECTED) {
        lv_label_set_text(s_status_lbl, "No WiFi connection");
        lv_obj_set_style_text_color(s_status_lbl, lv_palette_main(LV_PALETTE_RED), 0);
        return;
    }

    const char *url = lv_textarea_get_text(s_url_ta);
    if (!url || url[0] == '\0') {
        lv_label_set_text(s_status_lbl, "Enter a firmware URL");
        lv_obj_set_style_text_color(s_status_lbl, lv_palette_main(LV_PALETTE_RED), 0);
        return;
    }

    nvs_settings_set_ota_url(url);
    ESP_LOGI(TAG, "starting OTA from %s", url);

    lv_label_set_text(s_status_lbl, "Starting...");
    lv_obj_set_style_text_color(s_status_lbl, lv_palette_main(LV_PALETTE_GREY), 0);

    esp_err_t ret = ota_manager_start(url, ota_progress_cb, NULL);
    if (ret != ESP_OK) {
        lv_label_set_text(s_status_lbl, esp_err_to_name(ret));
        lv_obj_set_style_text_color(s_status_lbl, lv_palette_main(LV_PALETTE_RED), 0);
    }
}

/* ── Back button ───────────────────────────────────────────────────────── */

static void back_cb(lv_event_t *e)
{
    (void)e;
    if (!s_overlay) return;
    lv_obj_delete(s_overlay);
    s_overlay     = NULL;
    s_info_lbl    = NULL;
    s_url_section = NULL;
    s_url_ta      = NULL;
    s_kbd         = NULL;
    s_start_btn   = NULL;
    s_status_lbl  = NULL;
    s_bar         = NULL;
    s_back_btn    = NULL;
}

/* ── Public ────────────────────────────────────────────────────────────── */

void ui_ota_show(void)
{
    if (s_overlay) return;

    lv_obj_t *parent = lv_layer_top();

    s_overlay = lv_obj_create(parent);
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_overlay, 240, 320);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_overlay, 0, 0);

    /* ── Header ─────────────────────────────────────────────────────── */
    lv_obj_t *hdr = lv_obj_create(s_overlay);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(hdr, 240, 38);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_make(20, 20, 60), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, LV_SYMBOL_DOWNLOAD " Firmware Update");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

    /* ── Info line (check state / version) ──────────────────────────── */
    s_info_lbl = lv_label_create(s_overlay);
    lv_obj_set_width(s_info_lbl, 224);
    lv_label_set_long_mode(s_info_lbl, LV_LABEL_LONG_DOT);
    lv_obj_align(s_info_lbl, LV_ALIGN_TOP_LEFT, 8, 44);
    refresh_info_line();

    lv_timer_create(info_timer_cb, 2000, NULL);

    /* ── URL section (hidden during active OTA) ──────────────────────── */
    s_url_section = lv_obj_create(s_overlay);
    lv_obj_remove_flag(s_url_section, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_url_section, 240, 106);
    lv_obj_set_pos(s_url_section, 0, 64);
    lv_obj_set_style_bg_opa(s_url_section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_url_section, 0, 0);
    lv_obj_set_style_pad_all(s_url_section, 0, 0);

    lv_obj_t *url_lbl = lv_label_create(s_url_section);
    lv_label_set_text(url_lbl, "Firmware URL:");
    lv_obj_set_style_text_color(url_lbl, lv_color_white(), 0);
    lv_obj_set_pos(url_lbl, 8, 2);

    s_url_ta = lv_textarea_create(s_url_section);
    lv_obj_set_size(s_url_ta, 224, 48);
    lv_obj_align(s_url_ta, LV_ALIGN_TOP_MID, 0, 18);
    lv_textarea_set_one_line(s_url_ta, true);
    lv_textarea_set_placeholder_text(s_url_ta, "http://192.168.1.x/fw.bin");
    lv_obj_add_event_cb(s_url_ta, ta_focused_cb, LV_EVENT_FOCUSED, NULL);

    char saved_url[NVS_SETTINGS_OTA_URL_LEN + 1] = {0};
    nvs_settings_get_ota_url(saved_url, sizeof(saved_url));
    if (saved_url[0] != '\0')
        lv_textarea_set_text(s_url_ta, saved_url);

    lv_obj_t *auto_cb_widget = lv_checkbox_create(s_url_section);
    lv_checkbox_set_text(auto_cb_widget, "Auto-update when available");
    lv_obj_set_style_text_color(auto_cb_widget, lv_color_white(), 0);
    lv_obj_set_pos(auto_cb_widget, 8, 72);
    bool auto_on = false;
    nvs_settings_get_ota_auto(&auto_on);
    if (auto_on)
        lv_obj_add_state(auto_cb_widget, LV_STATE_CHECKED);
    lv_obj_add_event_cb(auto_cb_widget, auto_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* ── Progress bar (hidden in config mode) ────────────────────────── */
    s_bar = lv_bar_create(s_overlay);
    lv_obj_set_size(s_bar, 224, 14);
    lv_obj_align(s_bar, LV_ALIGN_TOP_MID, 0, 178);
    lv_bar_set_range(s_bar, 0, 100);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
    lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);

    /* ── Status label ────────────────────────────────────────────────── */
    s_status_lbl = lv_label_create(s_overlay);
    lv_obj_set_width(s_status_lbl, 224);
    lv_obj_align(s_status_lbl, LV_ALIGN_TOP_MID, 0, 196);
    lv_label_set_long_mode(s_status_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_status_lbl, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_label_set_text(s_status_lbl, "Enter URL and tap Start Update");

    /* ── Start button ────────────────────────────────────────────────── */
    s_start_btn = lv_button_create(s_overlay);
    lv_obj_set_size(s_start_btn, 224, 36);
    lv_obj_align(s_start_btn, LV_ALIGN_TOP_MID, 0, 230);
    lv_obj_add_event_cb(s_start_btn, start_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *start_lbl = lv_label_create(s_start_btn);
    lv_label_set_text(start_lbl, LV_SYMBOL_DOWNLOAD " Start Update");
    lv_obj_center(start_lbl);

    /* ── Back button ─────────────────────────────────────────────────── */
    s_back_btn = lv_button_create(s_overlay);
    lv_obj_set_size(s_back_btn, 80, 34);
    lv_obj_align(s_back_btn, LV_ALIGN_TOP_LEFT, 8, 276);
    lv_obj_set_style_bg_color(s_back_btn, lv_color_make(60, 30, 30), 0);
    lv_obj_add_event_cb(s_back_btn, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(s_back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back_lbl);

    /* ── Keyboard (hidden initially, slides up on textarea focus) ────── */
    s_kbd = lv_keyboard_create(s_overlay);
    lv_obj_set_size(s_kbd, 240, 140);
    lv_obj_align(s_kbd, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(s_kbd, s_url_ta);
    lv_obj_add_flag(s_kbd, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_kbd, kbd_done_cb, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(s_kbd, kbd_done_cb, LV_EVENT_CANCEL, NULL);
}
