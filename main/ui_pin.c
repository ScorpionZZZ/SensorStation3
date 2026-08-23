// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "ui.h"
#include "nvs_settings.h"
#include "lvgl.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ui_pin";

/* Keypad geometry */
#define BTN_W  62
#define BTN_H  42
#define BTN_GAP 4

/* Modal state — reset on every open */
static struct {
    lv_obj_t *overlay;
    lv_obj_t *dots[NVS_SETTINGS_PIN_LEN];
    lv_obj_t *error_label;
    char       entered[NVS_SETTINGS_PIN_LEN + 1];
    int        count;
    char       stored[NVS_SETTINGS_PIN_LEN + 1];
} s;

/* ── helpers ─────────────────────────────────────────────────────────── */

static void update_dots(void)
{
    if (!s.overlay) return;  /* dots not created yet or modal already closed */
    for (int i = 0; i < NVS_SETTINGS_PIN_LEN; i++) {
        lv_obj_set_style_bg_color(s.dots[i],
            i < s.count ? lv_palette_main(LV_PALETTE_BLUE) : lv_color_white(), 0);
    }
}

static void clear_input(void)
{
    s.count = 0;
    memset(s.entered, 0, sizeof(s.entered));
    update_dots();
}

static void close_modal(void)
{
    if (s.overlay) {
        lv_obj_delete(s.overlay);   /* recursively deletes dots too */
        s.overlay = NULL;
        memset(s.dots, 0, sizeof(s.dots));  /* clear dangling child pointers */
    }
}

/* ── timer callback: hide error and reset after wrong PIN ────────────── */

static void error_timer_cb(lv_timer_t *t)
{
    (void)t;
    lv_label_set_text(s.error_label, " ");
    clear_input();
}

/* ── keypad event callback ───────────────────────────────────────────── */

/*
 * Button index layout:
 *   0  1  2   →  1 2 3
 *   3  4  5   →  4 5 6
 *   6  7  8   →  7 8 9
 *   9  10 11  →  ⌫ 0 ✕
 */
static void key_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    if (idx == 11) {            /* Cancel */
        close_modal();
        return;
    }

    if (idx == 9) {             /* Backspace */
        if (s.count > 0) {
            s.entered[--s.count] = '\0';
            update_dots();
        }
        return;
    }

    if (s.count >= NVS_SETTINGS_PIN_LEN) return;

    s.entered[s.count++] = (idx == 10) ? '0' : (char)('1' + idx);
    s.entered[s.count]   = '\0';
    update_dots();

    if (s.count < NVS_SETTINGS_PIN_LEN) return;

    /* All 4 digits entered — verify */
    if (strcmp(s.entered, s.stored) == 0) {
        ESP_LOGI(TAG, "PIN correct");
        close_modal();
        ui_show_settings();
    } else {
        ESP_LOGW(TAG, "PIN incorrect");
        lv_label_set_text(s.error_label, "Wrong PIN!");
        lv_timer_t *t = lv_timer_create(error_timer_cb, 1500, NULL);
        lv_timer_set_repeat_count(t, 1);
    }
}

/* ── public ──────────────────────────────────────────────────────────── */

void ui_pin_show(void)
{
    if (s.overlay) return;  /* already open */

    clear_input();
    if (nvs_settings_get_pin(s.stored, sizeof(s.stored)) != ESP_OK) {
        strncpy(s.stored, "1234", sizeof(s.stored));
    }

    /* ── Full-screen semi-transparent overlay ─────────────────────── */
    s.overlay = lv_obj_create(lv_screen_active());
    lv_obj_remove_flag(s.overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s.overlay, 240, 320);
    lv_obj_set_pos(s.overlay, 0, 0);
    lv_obj_set_style_bg_color(s.overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s.overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s.overlay, 0, 0);
    lv_obj_set_style_pad_all(s.overlay, 0, 0);

    /* ── Card ─────────────────────────────────────────────────────── */
    lv_obj_t *card = lv_obj_create(s.overlay);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(card, 218, LV_SIZE_CONTENT);
    lv_obj_center(card);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* ── Title ────────────────────────────────────────────────────── */
    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "Enter PIN");
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    /* ── Dot indicators ───────────────────────────────────────────── */
    lv_obj_t *dots_row = lv_obj_create(card);
    lv_obj_remove_flag(dots_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(dots_row, LV_PCT(100), 42);
    lv_obj_set_style_bg_opa(dots_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dots_row, 0, 0);
    lv_obj_set_style_pad_all(dots_row, 0, 0);
    lv_obj_set_style_pad_column(dots_row, 16, 0);
    lv_obj_set_layout(dots_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(dots_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots_row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (int i = 0; i < NVS_SETTINGS_PIN_LEN; i++) {
        s.dots[i] = lv_obj_create(dots_row);
        lv_obj_set_size(s.dots[i], 28, 28);
        lv_obj_set_style_radius(s.dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s.dots[i], lv_color_white(), 0);
        lv_obj_set_style_border_color(s.dots[i], lv_palette_main(LV_PALETTE_BLUE), 0);
        lv_obj_set_style_border_width(s.dots[i], 2, 0);
        lv_obj_set_style_pad_all(s.dots[i], 0, 0);
    }

    /* ── Error label (single space reserves height when no error) ─── */
    s.error_label = lv_label_create(card);
    lv_label_set_text(s.error_label, " ");
    lv_obj_set_width(s.error_label, LV_PCT(100));
    lv_obj_set_style_text_align(s.error_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s.error_label, lv_palette_main(LV_PALETTE_RED), 0);

    /* ── Keypad ───────────────────────────────────────────────────── */
    lv_obj_t *keypad = lv_obj_create(card);
    lv_obj_remove_flag(keypad, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(keypad, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(keypad, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(keypad, 0, 0);
    lv_obj_set_style_pad_all(keypad, 0, 0);
    lv_obj_set_style_pad_row(keypad, BTN_GAP, 0);
    lv_obj_set_style_pad_column(keypad, BTN_GAP, 0);
    lv_obj_set_layout(keypad, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(keypad, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(keypad, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    static const char *const labels[12] = {
        "1", "2", "3",
        "4", "5", "6",
        "7", "8", "9",
        LV_SYMBOL_BACKSPACE, "0", LV_SYMBOL_CLOSE
    };

    for (int i = 0; i < 12; i++) {
        lv_obj_t *btn = lv_button_create(keypad);
        lv_obj_set_size(btn, BTN_W, BTN_H);
        lv_obj_add_event_cb(btn, key_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, labels[i]);
        lv_obj_center(lbl);
    }
}