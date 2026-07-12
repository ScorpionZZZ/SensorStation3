// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "ui.h"
#include "wifi_manager.h"
#include "ntp_clock.h"
#include "sensor_history.h"
#include "bmx280.h"
#if CONFIG_SS3_USE_BSEC
#include "bsec_sensor.h"
#endif
#include "ota_manager.h"
#include "lvgl.h"
#include "fonts.h"
#include <math.h>
#include <limits.h>
#include <stdio.h>

/* ── color palette (from designUI/mainScreenUI.md) ───────────────────── */
#define C(r,g,b)        lv_color_make((r),(g),(b))
#define C_BG            C( 11, 12, 24)
#define C_CARD          C( 14, 15, 34)
#define C_BORDER        C( 26, 27, 53)
#define C_BORDER_DEEP   C( 37, 37, 80)
#define C_TEMP          C(249,115, 22)
#define C_HUM           C( 56,189,248)
#define C_WIFI_OFF      C( 30, 32, 66)
#define C_TIME          C(208,208,240)
#define C_LABEL         C(0x4D, 0x4D, 0x9B)
#define C_DEW           C(128,128,176)
#define C_PRESSURE      C(104,104,168)
#define C_HUM_BADGE     C( 96,144,184)
#define C_TREND_UP      C(239, 68, 68)
#define C_TREND_BG      C( 30,  5,  0)
#define C_HUM_BADGE_BG  C(  0, 24, 40)
#define C_SETTINGS      C( 74, 74,128)
#define C_DATE          C(0xDD, 0xDD, 0xDD)
#define C_YEAR          C(0xDD, 0xDD, 0xDD)
#define C_GAS           C( 80, 200, 100)

static const char *k_day[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
static const char *k_mon[] = {"JAN","FEB","MAR","APR","MAY","JUN",
                               "JUL","AUG","SEP","OCT","NOV","DEC"};

/* ── widget handles ──────────────────────────────────────────────────── */
static lv_obj_t *s_screen;

/* header */
static lv_obj_t *s_lbl_date_top;
static lv_obj_t *s_lbl_date_year;
static lv_obj_t *s_lbl_hh;
static lv_obj_t *s_lbl_sep;
static lv_obj_t *s_lbl_mm;
static lv_obj_t *s_wifi_bars[5];
static lv_obj_t *s_lbl_update;  /* yellow "!" — visible when update is available */

/* temperature card */
static lv_obj_t         *s_lbl_temp;
static lv_obj_t         *s_temp_badge;
static lv_obj_t         *s_temp_badge_sym;
static lv_obj_t         *s_temp_badge_val;
static lv_obj_t          *s_temp_chart;
static lv_chart_series_t *s_temp_ser;
static lv_chart_series_t *s_temp_ser_old;  /* darker "yesterday" segment */
static lv_chart_cursor_t *s_temp_cursor;

/* humidity card */
static lv_obj_t          *s_lbl_hum;
static lv_obj_t          *s_hum_badge;
static lv_obj_t          *s_hum_badge_sym;
static lv_obj_t          *s_hum_badge_val;
static lv_obj_t          *s_hum_chart;
static lv_chart_series_t *s_hum_ser;
static lv_chart_series_t *s_hum_ser_old;  /* darker "yesterday" segment */
static lv_chart_cursor_t *s_hum_cursor;

/* other cards */
static lv_obj_t *s_lbl_dew;
static lv_obj_t *s_lbl_pressure;
static lv_obj_t *s_lbl_gas;  /* BME680 gas resistance — NULL for other sensors */

/* chart data buffer (static to avoid stack overflow in timer callback) */
static history_slot_t s_chart_buf[SENSOR_HISTORY_24H_SLOTS];

/* ── helpers ─────────────────────────────────────────────────────────── */

static int rssi_to_bars(int dbm)
{
    if (dbm <= -100) return 0;
    if (dbm >= -50)  return 5;
    if (dbm >= -60)  return 4;
    if (dbm >= -70)  return 3;
    if (dbm >= -80)  return 2;
    return 1;
}

static float dew_point_calc(float t, float rh)
{
    const float a = 17.625f, b = 243.04f;
    float alpha = logf(rh / 100.0f) + a * t / (b + t);
    return b * alpha / (a - alpha);
}

static void wifi_bars_update(int active)
{
    for (int i = 0; i < 5; i++) {
        lv_obj_set_style_bg_color(s_wifi_bars[i],
            (i < active) ? C_HUM : C_WIFI_OFF, 0);
    }
}

static void badge_update(lv_obj_t *badge, lv_obj_t *sym, lv_obj_t *val,
                         sensor_trend_t trend, float delta, const char *unit)
{
    if (trend == TREND_UNKNOWN) {
        lv_obj_add_flag(badge, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_HIDDEN);

    char delta_str[12];
    snprintf(delta_str, sizeof(delta_str), "%+.1f%s", delta, unit);

    lv_color_t sym_col, val_col, bg_col;
    const char *arrow;

    switch (trend) {
    case TREND_UP:
        bg_col  = C_TREND_BG;
        sym_col = C_TREND_UP;
        val_col = C_TREND_UP;
        arrow   = LV_SYMBOL_UP;
        break;
    case TREND_DOWN:
        bg_col  = C_HUM_BADGE_BG;
        sym_col = C_HUM;
        val_col = C_HUM;
        arrow   = LV_SYMBOL_DOWN;
        break;
    default: /* TREND_STABLE */
        bg_col  = C_HUM_BADGE_BG;
        sym_col = C_HUM_BADGE;
        val_col = C_HUM_BADGE;
        arrow   = "=";
        break;
    }

    lv_obj_set_style_bg_color(badge, bg_col, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_label_set_text(sym, arrow);
    lv_obj_set_style_text_color(sym, sym_col, 0);
    lv_label_set_text(val, delta_str);
    lv_obj_set_style_text_color(val, val_col, 0);
}

static void charts_update(void)
{
    int count = sensor_history_get_24h(s_chart_buf, SENSOR_HISTORY_24H_SLOTS);

    /* Map data to fixed time-of-day positions: slot 0 = 0:00, slot 143 = 23:50.
     * Requires a synced clock; falls back to right-aligned when unsynced. */
    int end_slot;   /* which 10-min slot of the day the newest datum belongs to */
    struct tm tm_now;
    if (ntp_clock_get_local(&tm_now)) {
        end_slot = (tm_now.tm_hour * 60 + tm_now.tm_min) / 10;
    } else {
        end_slot = SENSOR_HISTORY_24H_SLOTS - 1;  /* right-aligned fallback */
    }

    lv_value_precise_t *yt     = lv_chart_get_y_array(s_temp_chart, s_temp_ser);
    lv_value_precise_t *yt_old = lv_chart_get_y_array(s_temp_chart, s_temp_ser_old);
    int32_t t_min = INT32_MAX, t_max = INT32_MIN;
    for (int i = 0; i < SENSOR_HISTORY_24H_SLOTS; i++) {
        /* Wrap modulo 24h so day-slot i always maps to its most recent
         * occurrence in the ring buffer, looping past midnight instead of
         * going out of range once the buffer holds a full day. */
        int offset   = (end_slot - i + SENSOR_HISTORY_24H_SLOTS) % SENSOR_HISTORY_24H_SLOTS;
        int data_idx = count - 1 - offset;
        if (data_idx >= 0 && data_idx < count && s_chart_buf[data_idx].valid) {
            int32_t v = (int32_t)(s_chart_buf[data_idx].temp * 10.0f);
            /* Slots not yet reached today (i > end_slot) still hold
             * yesterday's reading — draw those on the darker series.
             * The boundary point is shared so the two segments join. */
            yt[i]     = (i <= end_slot) ? v : LV_CHART_POINT_NONE;
            yt_old[i] = (i >= end_slot) ? v : LV_CHART_POINT_NONE;
            if (v < t_min) t_min = v;
            if (v > t_max) t_max = v;
        } else {
            yt[i]     = LV_CHART_POINT_NONE;
            yt_old[i] = LV_CHART_POINT_NONE;
        }
    }
    if (t_min < t_max)
        lv_chart_set_range(s_temp_chart, LV_CHART_AXIS_PRIMARY_Y,
                           t_min - 5, t_max + 5);
    if (count > 0)
        lv_chart_set_cursor_point(s_temp_chart, s_temp_cursor,
                                  s_temp_ser, (uint32_t)end_slot);
    lv_chart_refresh(s_temp_chart);

    if (bmx280_get_type() == BMX280_TYPE_BME280 ||
        bmx280_get_type() == BMX280_TYPE_BME680) {
        lv_value_precise_t *yh     = lv_chart_get_y_array(s_hum_chart, s_hum_ser);
        lv_value_precise_t *yh_old = lv_chart_get_y_array(s_hum_chart, s_hum_ser_old);
        int32_t h_min = INT32_MAX, h_max = INT32_MIN;
        for (int i = 0; i < SENSOR_HISTORY_24H_SLOTS; i++) {
            int offset   = (end_slot - i + SENSOR_HISTORY_24H_SLOTS) % SENSOR_HISTORY_24H_SLOTS;
            int data_idx = count - 1 - offset;
            if (data_idx >= 0 && data_idx < count && s_chart_buf[data_idx].valid) {
                int32_t v = (int32_t)(s_chart_buf[data_idx].humidity);
                yh[i]     = (i <= end_slot) ? v : LV_CHART_POINT_NONE;
                yh_old[i] = (i >= end_slot) ? v : LV_CHART_POINT_NONE;
                if (v < h_min) h_min = v;
                if (v > h_max) h_max = v;
            } else {
                yh[i]     = LV_CHART_POINT_NONE;
                yh_old[i] = LV_CHART_POINT_NONE;
            }
        }
        if (h_min < h_max)
            lv_chart_set_range(s_hum_chart, LV_CHART_AXIS_PRIMARY_Y,
                               h_min - 2, h_max + 2);
        if (count > 0)
            lv_chart_set_cursor_point(s_hum_chart, s_hum_cursor,
                                      s_hum_ser, (uint32_t)end_slot);
        lv_chart_refresh(s_hum_chart);
    }
}

/* ── 500ms timer ─────────────────────────────────────────────────────── */

static void tick_timer_cb(lv_timer_t *t)
{
    (void)t;
    static bool s_colon_on   = false;
    static int  s_chart_tick = 0;
    s_colon_on = !s_colon_on;

    /* time + date */
    struct tm tm_local;
    bool synced = ntp_clock_get_local(&tm_local);
    if (synced) {
        char hh[3], mm[3];
        snprintf(hh, sizeof(hh), "%02d", tm_local.tm_hour);
        snprintf(mm, sizeof(mm), "%02d", tm_local.tm_min);
        lv_label_set_text(s_lbl_hh, hh);
        lv_label_set_text(s_lbl_mm, mm);
        lv_obj_set_style_text_opa(s_lbl_sep,
            s_colon_on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);

        char date_buf[16];
        snprintf(date_buf, sizeof(date_buf), "%s, %d %s",
                 k_day[tm_local.tm_wday], tm_local.tm_mday,
                 k_mon[tm_local.tm_mon]);
        lv_label_set_text(s_lbl_date_top, date_buf);

        char year_buf[16];
        snprintf(year_buf, sizeof(year_buf), "%d", tm_local.tm_year + 1900);
        lv_label_set_text(s_lbl_date_year, year_buf);
    } else {
        lv_label_set_text(s_lbl_hh,  "--");
        lv_label_set_text(s_lbl_mm,  "--");
        lv_obj_set_style_text_opa(s_lbl_sep, LV_OPA_COVER, 0);
        lv_label_set_text(s_lbl_date_top,  "");
        lv_label_set_text(s_lbl_date_year, "");
    }

    /* WiFi bars */
    wifi_bars_update(rssi_to_bars(wifi_manager_get_rssi_dbm()));

    /* update-available icon */
    if (s_lbl_update) {
        if (ota_manager_check_state() == OTA_CHECK_AVAILABLE)
            lv_obj_remove_flag(s_lbl_update, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(s_lbl_update, LV_OBJ_FLAG_HIDDEN);
    }

    /* sensor data */
    float temp, hum, pres;
    bool valid = sensor_history_get_current(&temp, &hum, &pres);
    if (valid) {
        char buf[16];

        snprintf(buf, sizeof(buf), "%.1f", temp);
        lv_label_set_text(s_lbl_temp, buf);
        badge_update(s_temp_badge, s_temp_badge_sym, s_temp_badge_val,
                     sensor_history_get_temp_trend(),
                     sensor_history_get_temp_delta(), "\xc2\xb0");

        if (bmx280_get_type() == BMX280_TYPE_BME280 ||
            bmx280_get_type() == BMX280_TYPE_BME680) {
            snprintf(buf, sizeof(buf), "%.1f", hum);
            lv_label_set_text(s_lbl_hum, buf);
            badge_update(s_hum_badge, s_hum_badge_sym, s_hum_badge_val,
                         sensor_history_get_humidity_trend(),
                         sensor_history_get_hum_delta(), "%");

            float dp = dew_point_calc(temp, hum);
            snprintf(buf, sizeof(buf), "%.1f", dp);
            lv_label_set_text(s_lbl_dew, buf);
        } else {
            lv_label_set_text(s_lbl_hum, "--");
            lv_obj_add_flag(s_hum_badge, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(s_lbl_dew, "--");
        }

        /* CO2eq ppm (BME680 only) — color-coded by air quality level.
         * BSEC's compensated estimate when active, else bmx280's own
         * gas-resistance-based approximation. */
        if (s_lbl_gas) {
            float co2 = NAN;
            if (bmx280_get_type() == BMX280_TYPE_BME680) {
#if CONFIG_SS3_USE_BSEC
                bsec_data_t bsec;
                if (bsec_sensor_is_active() && bsec_sensor_read(&bsec) == ESP_OK)
                    co2 = bsec.co2_eq;
#else
                bmx280_co2eq_avg(&co2);
#endif
            }
            if (!isnan(co2)) {
                snprintf(buf, sizeof(buf), "%.0f", co2);
                lv_label_set_text(s_lbl_gas, buf);
                lv_color_t co2_color;
                if      (co2 <= 700)  co2_color = C( 80, 200, 100); /* green   — perfect  */
                else if (co2 <= 1000) co2_color = C(220, 200,  50); /* yellow  — good     */
                else if (co2 <= 1500) co2_color = C(230, 130,  30); /* orange  — fair     */
                else if (co2 <= 2500) co2_color = C(210,  50,  50); /* red     — poor     */
                else                  co2_color = C(160,  80, 220); /* violet  — danger   */
                lv_obj_set_style_text_color(s_lbl_gas, co2_color, 0);
            } else {
                lv_label_set_text(s_lbl_gas, "--");
                lv_obj_set_style_text_color(s_lbl_gas, C_GAS, 0);
            }
        }

        snprintf(buf, sizeof(buf), "%.0f", pres / 100.0);
        lv_label_set_text(s_lbl_pressure, buf);
    } else {
        lv_label_set_text(s_lbl_temp, "---");
        lv_obj_add_flag(s_temp_badge, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_lbl_hum, "--");
        lv_obj_add_flag(s_hum_badge, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_lbl_dew, "--");
        lv_label_set_text(s_lbl_pressure, "----");
    }

    /* update sparklines every 30 s */
    if (++s_chart_tick >= 60) {
        s_chart_tick = 0;
        charts_update();
    }
}

/* ── settings ────────────────────────────────────────────────────────── */

static void settings_cb(lv_event_t *e)
{
    (void)e;
    ui_pin_show();
}

/* ── widget builders ─────────────────────────────────────────────────── */

/* Create a styled card as a direct child of s_screen. */
static lv_obj_t *make_card(int x, int y, int w, int h)
{
    lv_obj_t *card = lv_obj_create(s_screen);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, C_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, C_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 5, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    return card;
}

/* Create a 3 px left accent bar inside a card. */
static void add_accent(lv_obj_t *card, int card_h, lv_color_t color)
{
    lv_obj_t *acc = lv_obj_create(card);
    lv_obj_remove_flag(acc, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(acc, 0, 5);
    lv_obj_set_size(acc, 3, card_h - 10);
    lv_obj_set_style_bg_color(acc, color, 0);
    lv_obj_set_style_bg_opa(acc, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(acc, 0, 0);
    lv_obj_set_style_radius(acc, 0, 0);
    lv_obj_set_style_pad_all(acc, 0, 0);
}

/*
 * Build a trend badge right-aligned inside a card at the given y offset
 * from the card's top edge.  Badge is hidden until badge_update() is called.
 */
static lv_obj_t *make_badge(lv_obj_t *card, int y_ofs,
                             lv_obj_t **out_sym, lv_obj_t **out_val)
{
    lv_obj_t *badge = lv_obj_create(card);
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(badge, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(badge, 18, 0);
    lv_obj_set_style_radius(badge, 5, 0);
    lv_obj_set_style_pad_hor(badge, 4, 0);
    lv_obj_set_style_pad_ver(badge, 2, 0);
    lv_obj_set_style_pad_column(badge, 2, 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_add_flag(badge, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -10, y_ofs);
    lv_obj_set_layout(badge, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(badge, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(badge, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    *out_sym = lv_label_create(badge);
    lv_label_set_text(*out_sym, "");
    lv_obj_set_style_text_font(*out_sym, &lv_font_montserrat_14, 0);

    *out_val = lv_label_create(badge);
    lv_label_set_text(*out_val, "");
    lv_obj_set_style_text_font(*out_val, &lv_font_montserrat_14, 0);

    return badge;
}

/*
 * Create a line sparkline chart inside a card.
 * Position (rx, ry) is relative to the card's top-left corner.
 * Returns the chart series handle; sets *out_chart.
 */
static lv_chart_series_t *make_sparkline(lv_obj_t *card,
                                         int rx, int ry, int w, int h,
                                         lv_color_t color,
                                         lv_obj_t **out_chart,
                                         lv_chart_cursor_t **out_cursor)
{
    lv_obj_t *chart = lv_chart_create(card);
    lv_obj_remove_flag(chart, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(chart, rx, ry);
    lv_obj_set_size(chart, w, h);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_pad_all(chart, 0, 0);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(chart, 0, 0);
    lv_chart_set_point_count(chart, SENSOR_HISTORY_24H_SLOTS);
    lv_obj_set_style_width(chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(chart, 1, LV_PART_ITEMS);

    lv_chart_series_t *ser = lv_chart_add_series(chart, color,
                                                  LV_CHART_AXIS_PRIMARY_Y);
    lv_value_precise_t *ya = lv_chart_get_y_array(chart, ser);
    for (int i = 0; i < SENSOR_HISTORY_24H_SLOTS; i++)
        ya[i] = LV_CHART_POINT_NONE;

    /* Vertical "now" cursor — dashed line at the newest data point */
    lv_chart_cursor_t *cur = lv_chart_add_cursor(chart, C(200, 200, 220),
                                                  LV_DIR_VER);
    lv_obj_set_style_line_width(chart, 1, LV_PART_CURSOR);
    lv_obj_set_style_line_dash_width(chart, 3, LV_PART_CURSOR);
    lv_obj_set_style_line_dash_gap(chart, 3, LV_PART_CURSOR);

    *out_chart  = chart;
    *out_cursor = cur;
    return ser;
}

/* Add a second, darker series to a sparkline chart for the "yesterday"
 * portion of the loop (the part not yet overwritten by today's data). */
static lv_chart_series_t *add_dark_series(lv_obj_t *chart, lv_color_t color)
{
    lv_chart_series_t *ser = lv_chart_add_series(chart, lv_color_darken(color, 120),
                                                  LV_CHART_AXIS_PRIMARY_Y);
    lv_value_precise_t *ya = lv_chart_get_y_array(chart, ser);
    for (int i = 0; i < SENSOR_HISTORY_24H_SLOTS; i++)
        ya[i] = LV_CHART_POINT_NONE;
    return ser;
}

/* ── public ──────────────────────────────────────────────────────────── */

lv_obj_t *ui_main_screen(void) { return s_screen; }

void ui_main_create(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_screen, C_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);

    /* ── Header bar (y=0, h=32) ─────────────────────────────────────── */
    lv_obj_t *header = lv_obj_create(s_screen);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, 240, 32);
    lv_obj_set_style_bg_color(header, C_CARD, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(header, C_BORDER, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);

    /* date (left side) */
    s_lbl_date_top = lv_label_create(header);
    lv_obj_set_pos(s_lbl_date_top, 12, 3);
    lv_obj_set_style_text_font(s_lbl_date_top, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_lbl_date_top, C_DATE, 0);
    lv_label_set_text(s_lbl_date_top, "");

    s_lbl_date_year = lv_label_create(header);
    lv_obj_set_pos(s_lbl_date_year, 12, 17);
    lv_obj_set_style_text_font(s_lbl_date_year, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s_lbl_date_year, C_YEAR, 0);
    lv_label_set_text(s_lbl_date_year, "");

    /* update-available indicator — hidden until check confirms a newer version */
    s_lbl_update = lv_label_create(header);
    lv_label_set_text(s_lbl_update, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(s_lbl_update, lv_palette_main(LV_PALETTE_YELLOW), 0);
    lv_obj_set_pos(s_lbl_update, 118, 9);
    lv_obj_add_flag(s_lbl_update, LV_OBJ_FLAG_HIDDEN);

    /* time HH:MM (right of center) */
    lv_obj_t *time_row = lv_obj_create(header);
    lv_obj_remove_flag(time_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(time_row, 142, 8);
    lv_obj_set_size(time_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(time_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(time_row, 0, 0);
    lv_obj_set_style_pad_all(time_row, 0, 0);
    lv_obj_set_style_pad_column(time_row, 0, 0);
    lv_obj_set_layout(time_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(time_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_lbl_hh = lv_label_create(time_row);
    lv_label_set_text(s_lbl_hh, "--");
    lv_obj_set_style_text_font(s_lbl_hh, &GoogleSans22_digits_2bpp, 0);
    lv_obj_set_style_text_color(s_lbl_hh, C_TIME, 0);

    s_lbl_sep = lv_label_create(time_row);
    lv_label_set_text(s_lbl_sep, ":");
    lv_obj_set_style_text_font(s_lbl_sep, &GoogleSans22_digits_2bpp, 0);
    lv_obj_set_style_text_color(s_lbl_sep, C_TIME, 0);

    s_lbl_mm = lv_label_create(time_row);
    lv_label_set_text(s_lbl_mm, "--");
    lv_obj_set_style_text_font(s_lbl_mm, &GoogleSans22_digits_2bpp, 0);
    lv_obj_set_style_text_color(s_lbl_mm, C_TIME, 0);

    /* WiFi bars (5 bars, bottom-aligned at y=27 in header) */
    static const int k_bar_x[5] = {207, 212, 217, 222, 227};
    static const int k_bar_h[5] = {5, 7, 10, 12, 14};
    for (int i = 0; i < 5; i++) {
        s_wifi_bars[i] = lv_obj_create(header);
        lv_obj_remove_flag(s_wifi_bars[i],
                           LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(s_wifi_bars[i], k_bar_x[i], 26 - k_bar_h[i]);
        lv_obj_set_size(s_wifi_bars[i], 3, k_bar_h[i]);
        lv_obj_set_style_bg_color(s_wifi_bars[i], C_WIFI_OFF, 0);
        lv_obj_set_style_bg_opa(s_wifi_bars[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_wifi_bars[i], 0, 0);
        lv_obj_set_style_radius(s_wifi_bars[i], 1, 0);
        lv_obj_set_style_pad_all(s_wifi_bars[i], 0, 0);
    }

    /* ── Temperature card (x=8, y=39, w=224, h=98) ──────────────────── */
    lv_obj_t *temp_card = make_card(8, 39, 224, 98);
    add_accent(temp_card, 98, C_TEMP);

    lv_obj_t *lbl_temp_hdr = lv_label_create(temp_card);
    lv_obj_set_pos(lbl_temp_hdr, 13, 5);
    lv_obj_set_style_text_font(lbl_temp_hdr, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_temp_hdr, C_LABEL, 0);
    lv_label_set_text(lbl_temp_hdr, "TEMPERATURE");

    /* value row: big number + small unit, bottom-aligned */
    lv_obj_t *temp_val = lv_obj_create(temp_card);
    lv_obj_remove_flag(temp_val, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(temp_val, 13, 20);
    lv_obj_set_size(temp_val, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(temp_val, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(temp_val, 0, 0);
    lv_obj_set_style_pad_all(temp_val, 0, 0);
    lv_obj_set_style_pad_column(temp_val, 2, 0);
    lv_obj_set_layout(temp_val, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(temp_val, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(temp_val, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);

    s_lbl_temp = lv_label_create(temp_val);
    lv_label_set_text(s_lbl_temp, "---");
    lv_obj_set_style_text_font(s_lbl_temp, &GoogleSans44_digits_2bpp, 0);
    lv_obj_set_style_text_color(s_lbl_temp, C_TEMP, 0);

    lv_obj_t *temp_unit = lv_label_create(temp_val);
    lv_label_set_text(temp_unit, "\xc2\xb0\x43");  /* °C */
    lv_obj_set_style_text_font(temp_unit, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(temp_unit, C_TEMP, 0);
    lv_obj_set_style_text_opa(temp_unit, LV_OPA_70, 0);
    lv_obj_set_style_margin_bottom(temp_unit, 24, 0);

    s_temp_badge = make_badge(temp_card, 34,
                              &s_temp_badge_sym, &s_temp_badge_val);

    /* sparkline: card-relative (13, 73) → screen (21, 114), size 201×16 */
    s_temp_ser = make_sparkline(temp_card, 13, 73, 201, 16,
                                C_TEMP, &s_temp_chart, &s_temp_cursor);
    s_temp_ser_old = add_dark_series(s_temp_chart, C_TEMP);

    /* ── Humidity card (x=8, y=142, w=224, h=76) ───────────────────── */
    lv_obj_t *hum_card = make_card(8, 142, 224, 76);
    add_accent(hum_card, 76, C_HUM);

    lv_obj_t *lbl_hum_hdr = lv_label_create(hum_card);
    lv_obj_set_pos(lbl_hum_hdr, 13, 5);
    lv_obj_set_style_text_font(lbl_hum_hdr, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_hum_hdr, C_LABEL, 0);
    lv_label_set_text(lbl_hum_hdr, "HUMIDITY");

    lv_obj_t *hum_val = lv_obj_create(hum_card);
    lv_obj_remove_flag(hum_val, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(hum_val, 13, 18);
    lv_obj_set_size(hum_val, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(hum_val, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hum_val, 0, 0);
    lv_obj_set_style_pad_all(hum_val, 0, 0);
    lv_obj_set_style_pad_column(hum_val, 2, 0);
    lv_obj_set_layout(hum_val, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(hum_val, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hum_val, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);

    s_lbl_hum = lv_label_create(hum_val);
    lv_label_set_text(s_lbl_hum, "--");
    lv_obj_set_style_text_font(s_lbl_hum, &GoogleSans32_digits_2bpp, 0);
    lv_obj_set_style_text_color(s_lbl_hum, C_HUM, 0);

    lv_obj_t *hum_unit = lv_label_create(hum_val);
    lv_label_set_text(hum_unit, "%");
    lv_obj_set_style_text_font(hum_unit, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hum_unit, C_HUM, 0);
    lv_obj_set_style_text_opa(hum_unit, LV_OPA_70, 0);
    lv_obj_set_style_margin_bottom(hum_unit, 14, 0);

    s_hum_badge = make_badge(hum_card, 26,
                             &s_hum_badge_sym, &s_hum_badge_val);

    /* sparkline: card-relative (13, 52) → screen (21, 196), size 201×13 */
    s_hum_ser = make_sparkline(hum_card, 13, 52, 201, 13,
                               C_HUM, &s_hum_chart, &s_hum_cursor);
    s_hum_ser_old = add_dark_series(s_hum_chart, C_HUM);

    /* ── Dew point card (x=8, y=223, w=109, h=40) ──────────────────── */
    lv_obj_t *dew_card = make_card(8, 223, 109, 40);

    lv_obj_t *lbl_dew_hdr = lv_label_create(dew_card);
    lv_obj_set_pos(lbl_dew_hdr, 10, 3);
    lv_obj_set_style_text_font(lbl_dew_hdr, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_dew_hdr, C_LABEL, 0);
    lv_label_set_text(lbl_dew_hdr, "DEW POINT");

    lv_obj_t *dew_val = lv_obj_create(dew_card);
    lv_obj_remove_flag(dew_val, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(dew_val, 10, 16);
    lv_obj_set_size(dew_val, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(dew_val, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dew_val, 0, 0);
    lv_obj_set_style_pad_all(dew_val, 0, 0);
    lv_obj_set_style_pad_column(dew_val, 2, 0);
    lv_obj_set_layout(dew_val, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(dew_val, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dew_val, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);

    s_lbl_dew = lv_label_create(dew_val);
    lv_obj_set_style_text_font(s_lbl_dew, &GoogleSans22_digits_2bpp, 0);
    lv_obj_set_style_text_color(s_lbl_dew, C_DEW, 0);
    lv_label_set_text(s_lbl_dew, "--");

    lv_obj_t *dew_temp_unit = lv_label_create(dew_val);
    lv_label_set_text(dew_temp_unit, "\xc2\xb0\x43");  /* °C */
    lv_obj_set_style_text_font(dew_temp_unit, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(dew_temp_unit, C_DEW, 0);
    lv_obj_set_style_text_opa(dew_temp_unit, LV_OPA_70, 0);
    lv_obj_set_style_margin_bottom(dew_temp_unit, 4, 0);



    /* ── Pressure card (x=122, y=223, w=110, h=40) ───────────────────── */
    lv_obj_t *pres_card = make_card(122, 223, 110, 40);

    lv_obj_t *lbl_pres_hdr = lv_label_create(pres_card);
    lv_obj_set_pos(lbl_pres_hdr, 9, 3);
    lv_obj_set_style_text_font(lbl_pres_hdr, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lbl_pres_hdr, C_LABEL, 0);
    lv_label_set_text(lbl_pres_hdr, "PRESSURE");

    lv_obj_t *pres_val = lv_obj_create(pres_card);
    lv_obj_remove_flag(pres_val, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(pres_val, 9, 16);
    lv_obj_set_size(pres_val, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(pres_val, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pres_val, 0, 0);
    lv_obj_set_style_pad_all(pres_val, 0, 0);
    lv_obj_set_style_pad_column(pres_val, 3, 0);
    lv_obj_set_layout(pres_val, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(pres_val, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pres_val, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);

    s_lbl_pressure = lv_label_create(pres_val);
    lv_label_set_text(s_lbl_pressure, "----");
    lv_obj_set_style_text_font(s_lbl_pressure, &GoogleSans22_digits_2bpp, 0);
    lv_obj_set_style_text_color(s_lbl_pressure, C_PRESSURE, 0);

    lv_obj_t *pres_unit = lv_label_create(pres_val);
    lv_label_set_text(pres_unit, "hPa");
    lv_obj_set_style_text_font(pres_unit, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pres_unit, C_YEAR, 0);

    /* ── Settings card (x=122, y=270, w=110, h=43) ─────────────────── */
    lv_obj_t *settings_card = make_card(122, 270, 110, 43);
    lv_obj_set_style_border_color(settings_card, C_BORDER_DEEP, 0);

    lv_obj_t *settings_row = lv_obj_create(settings_card);
    lv_obj_remove_flag(settings_row,
                       LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(settings_row, 110, 43);
    lv_obj_set_pos(settings_row, 0, 0);
    lv_obj_set_style_bg_opa(settings_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(settings_row, 0, 0);
    lv_obj_set_style_pad_all(settings_row, 0, 0);
    lv_obj_set_style_pad_column(settings_row, 4, 0);
    lv_obj_set_layout(settings_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(settings_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(settings_row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *settings_icon = lv_label_create(settings_row);
    lv_label_set_text(settings_icon, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(settings_icon, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(settings_icon, C_SETTINGS, 0);

    lv_obj_t *settings_lbl = lv_label_create(settings_row);
    lv_label_set_text(settings_lbl, "SETTINGS");
    lv_obj_set_style_text_font(settings_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(settings_lbl, C_SETTINGS, 0);

    lv_obj_add_event_cb(settings_card, settings_cb, LV_EVENT_CLICKED, NULL);

    /* ── Gas resistance card (x=8, y=270, w=109, h=43) — BME680 only ── */
    if (bmx280_get_type() == BMX280_TYPE_BME680) {
        lv_obj_t *gas_card = make_card(8, 270, 109, 43);

        lv_obj_t *lbl_gas_hdr = lv_label_create(gas_card);
        lv_obj_set_pos(lbl_gas_hdr, 10, 3);
        lv_obj_set_style_text_font(lbl_gas_hdr, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(lbl_gas_hdr, C_LABEL, 0);
        lv_label_set_text(lbl_gas_hdr, "CO2 EQ.");

        lv_obj_t *gas_val = lv_obj_create(gas_card);
        lv_obj_remove_flag(gas_val, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(gas_val, 10, 16);
        lv_obj_set_size(gas_val, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(gas_val, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(gas_val, 0, 0);
        lv_obj_set_style_pad_all(gas_val, 0, 0);
        lv_obj_set_style_pad_column(gas_val, 2, 0);
        lv_obj_set_layout(gas_val, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(gas_val, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(gas_val, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);

        s_lbl_gas = lv_label_create(gas_val);
        lv_label_set_text(s_lbl_gas, "--");
        lv_obj_set_style_text_font(s_lbl_gas, &GoogleSans22_digits_2bpp, 0);
        lv_obj_set_style_text_color(s_lbl_gas, C_GAS, 0);

        lv_obj_t *gas_unit = lv_label_create(gas_val);
        lv_label_set_text(gas_unit, "ppm");
        lv_obj_set_style_text_font(gas_unit, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(gas_unit, C_GAS, 0);
        lv_obj_set_style_text_opa(gas_unit, LV_OPA_70, 0);
        lv_obj_set_style_margin_bottom(gas_unit, 4, 0);
    }

    /* ── start 500 ms refresh timer ─────────────────────────────────── */
    lv_timer_create(tick_timer_cb, 500, NULL);
    tick_timer_cb(NULL);
}