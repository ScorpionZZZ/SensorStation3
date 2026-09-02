// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "history_fetch.h"
#include "sensor_history.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "ntp_clock.h"
#include "tb_mqtt.h"
#include "domoticz_mqtt.h"
#include "ha_mqtt.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "hist_fetch";

/* Same namespace/keys as nvs_settings.c — avoids a dependency on main/,
 * same convention as tb_mqtt/domoticz_mqtt/ha_mqtt. */
#define NVS_NS            "settings"
#define KEY_HIST_SOURCE    "hist_source"
#define KEY_DZ_HTTP_URL    "dz_http_url"
#define KEY_DZ_THP_IDX     "dz_thp_idx"
#define KEY_DZ_HTTP_USER   "dz_http_user"
#define KEY_DZ_HTTP_PASS   "dz_http_pass"
#define KEY_TB_REST_URL    "tb_rest_url"
#define KEY_TB_REST_USER   "tb_rest_user"
#define KEY_TB_REST_PASS   "tb_rest_pass"
#define KEY_TB_DEVICE_ID   "tb_device_id"
#define KEY_HA_HTTP_URL    "ha_http_url"
#define KEY_HA_HTTP_TOKEN  "ha_http_token"
#define KEY_HA_TEMP_ENTITY "ha_temp_entity"
#define KEY_HA_HUM_ENTITY  "ha_hum_entity"

#define HIST_SOURCE_NONE     0
#define HIST_SOURCE_TB       1
#define HIST_SOURCE_DOMOTICZ 2
#define HIST_SOURCE_HA       3

static bool s_triggered;

/* ── streaming Domoticz response parser ───────────────────────────────────
 * Domoticz's /json.htm?type=command&param=graph&sensor=temp&range=day&
 * idx=N returns {"status":"OK","title":"...","result":[
 * {"d":"2026-08-22 08:20:00","te":"21.400","hu":"46", ...}, ... ]}. A day's
 * worth of samples from a real instance turned out to be tens of KB — too
 * large to buffer whole and DOM-parse on this device: buffering the full
 * body hit "OOM growing response buffer" around 8-16 KB even after a 30 s
 * post-boot delay (i.e. it's a real memory ceiling, not early-boot
 * WiFi/MQTT contention that clears with time). A first rewrite kept a
 * compact (timestamp, temp, humidity) array per record instead of the raw
 * text — better, but a real instance logging near our own ~30 s publish
 * interval needed thousands of points/day, and even a single one-time
 * ~38 KB allocation for that array still failed outright ("OOM allocating
 * 3200-point buffer").
 *
 * This version keeps no per-point storage at all: each {...} record is
 * still parsed individually (into a small fixed scratch buffer), but
 * immediately folded into a small ring of 10-minute bucket sums. A second
 * design anchored buckets on elapsed-time-since-the-first-record instead —
 * simpler, but assumed "range=day" meant the response covers roughly a
 * day; a real instance turned out to span a full 7 days (604800 s) under
 * "range=day", so a fixed-size array sized for ~33h silently dropped every
 * record beyond that (and since records stream oldest-first, that's the
 * *recent* end — exactly the data actually needed). A true ring buffer,
 * indexed by absolute-bucket-number modulo the ring size, self-evicts
 * whatever's oldest as new records arrive, so it stays correct for
 * whatever the true span turns out to be without having to guess it up
 * front. Only the most recent ~27h are ever retained — plenty for the 24h
 * sensor_history actually wants. */

#define DZ_OBJ_BUF_LEN   320   /* generous for one record; Domoticz's rows
                                * are ~60-80 bytes in practice */
#define DZ_RING_SLOTS    160   /* ~26.7h of 10-min buckets — comfortably
                                * more than the 144 (24h) actually needed */

typedef enum {
    DZ_SEEK_RESULT,      /* scanning for the literal "result" key */
    DZ_SEEK_ARRAY_START, /* found "result", scanning for the '[' */
    DZ_IN_ARRAY,         /* between records, or at ']' */
    DZ_IN_OBJECT,        /* inside a {...} record */
    DZ_DONE,             /* array closed — ignore the rest of the body */
} dz_scan_state_t;

typedef struct {
    dz_scan_state_t state;
    int             result_match;   /* chars of "\"result\"" matched so far */
    bool            in_string;
    bool            escape_next;
    int             depth;

    char obj_buf[DZ_OBJ_BUF_LEN];
    int  obj_len;
    bool obj_overflow;

    bool   have_first;
    time_t t_first;   /* anchor for computing each record's absolute bucket */
    time_t t_last;    /* running "latest seen" — order assumed oldest-first,
                        * same as sensor_history's own ring buffer */

    /* Ring buffer: slot_abs[i] records which absolute 10-minute bucket
     * (relative to t_first) ring slot i currently holds, so a slot being
     * reused for a much-later bucket (the "wrap") is detected and cleared
     * rather than silently mixed with stale data. -1 = never touched. */
    long   slot_abs[DZ_RING_SLOTS];
    double sum_t[DZ_RING_SLOTS];
    double sum_h[DZ_RING_SLOTS];
    int    cnt[DZ_RING_SLOTS];
} dz_stream_ctx_t;

/* Only one fetch ever runs at a time (boot-time singleton), so this lives
 * in .bss rather than being allocated — sidesteps entirely the "will this
 * allocation succeed under boot-time heap pressure" question that sank the
 * two earlier designs. */
static dz_stream_ctx_t s_ctx;

static double json_num(const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!item) return NAN;
    if (cJSON_IsNumber(item)) return item->valuedouble;
    if (cJSON_IsString(item) && item->valuestring && item->valuestring[0]) return atof(item->valuestring);
    return NAN;
}

static bool parse_domoticz_datetime(const char *s, time_t *out)
{
    int y, mo, d, h, mi;
    if (sscanf(s, "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi) != 5) return false;
    struct tm tmv = {0};
    tmv.tm_year  = y - 1900;
    tmv.tm_mon   = mo - 1;
    tmv.tm_mday  = d;
    tmv.tm_hour  = h;
    tmv.tm_min   = mi;
    tmv.tm_isdst = -1;
    time_t t = mktime(&tmv);
    if (t == (time_t)-1) return false;
    *out = t;
    return true;
}

/* One complete {...} record has been assembled in ctx->obj_buf — parse just
 * that (a ~70-byte cJSON tree, freed immediately) and fold it straight into
 * the ring's bucket sums, evicting whatever stale bucket currently occupies
 * that ring slot first if this record has wrapped around into it. */
static void dz_stream_commit_object(dz_stream_ctx_t *ctx)
{
    if (ctx->obj_overflow) return;

    ctx->obj_buf[ctx->obj_len] = '\0';
    cJSON *obj = cJSON_Parse(ctx->obj_buf);
    if (!obj) return;

    const cJSON *d = cJSON_GetObjectItemCaseSensitive(obj, "d");
    if (cJSON_IsString(d) && d->valuestring) {
        time_t t;
        if (parse_domoticz_datetime(d->valuestring, &t)) {
            double temp = json_num(obj, "te");
            if (!isnan(temp)) {
                double hum = json_num(obj, "hu");

                if (!ctx->have_first) {
                    ctx->t_first    = t;
                    ctx->have_first = true;
                }
                ctx->t_last = t;   /* order assumed chronological, so this
                                     * simply tracks the last one seen */

                long elapsed_min = (long)((t - ctx->t_first) / 60);
                if (elapsed_min < 0) elapsed_min = 0;   /* guard a rare
                                                          * out-of-order
                                                          * record */
                long abs_bucket = elapsed_min / 10;
                int  ring_idx   = (int)(abs_bucket % DZ_RING_SLOTS);

                if (ctx->slot_abs[ring_idx] != abs_bucket) {
                    ctx->slot_abs[ring_idx] = abs_bucket;
                    ctx->sum_t[ring_idx]    = 0.0;
                    ctx->sum_h[ring_idx]    = 0.0;
                    ctx->cnt[ring_idx]      = 0;
                }
                ctx->sum_t[ring_idx] += temp;
                ctx->sum_h[ring_idx] += isnan(hum) ? 0.0 : hum;
                ctx->cnt[ring_idx]++;
            }
        }
    }
    cJSON_Delete(obj);
}

static void dz_stream_feed(dz_stream_ctx_t *ctx, const char *data, int len)
{
    static const char needle[] = "\"result\"";

    for (int i = 0; i < len; i++) {
        char c = data[i];
        switch (ctx->state) {
        case DZ_SEEK_RESULT:
            if (c == needle[ctx->result_match]) {
                ctx->result_match++;
                if (needle[ctx->result_match] == '\0') ctx->state = DZ_SEEK_ARRAY_START;
            } else {
                ctx->result_match = (c == needle[0]) ? 1 : 0;
            }
            break;

        case DZ_SEEK_ARRAY_START:
            if (c == '[') ctx->state = DZ_IN_ARRAY;
            break;

        case DZ_IN_ARRAY:
            if (c == '{') {
                ctx->state        = DZ_IN_OBJECT;
                ctx->obj_len       = 0;
                ctx->obj_overflow  = false;
                ctx->obj_buf[ctx->obj_len++] = c;
                ctx->depth         = 1;
                ctx->in_string     = false;
                ctx->escape_next   = false;
            } else if (c == ']') {
                ctx->state = DZ_DONE;
            }
            break;

        case DZ_IN_OBJECT:
            if (ctx->obj_len < (int)sizeof(ctx->obj_buf) - 1) {
                ctx->obj_buf[ctx->obj_len++] = c;
            } else {
                ctx->obj_overflow = true;
            }
            if (ctx->escape_next) {
                ctx->escape_next = false;
            } else if (ctx->in_string) {
                if (c == '\\')      ctx->escape_next = true;
                else if (c == '"')  ctx->in_string = false;
            } else if (c == '"') {
                ctx->in_string = true;
            } else if (c == '{') {
                ctx->depth++;
            } else if (c == '}') {
                ctx->depth--;
                if (ctx->depth == 0) {
                    dz_stream_commit_object(ctx);
                    ctx->state = DZ_IN_ARRAY;
                }
            }
            break;

        case DZ_DONE:
        default:
            return;   /* nothing left worth scanning */
        }
    }
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA || evt->data_len <= 0) return ESP_OK;
    dz_stream_ctx_t *ctx = (dz_stream_ctx_t *)evt->user_data;
    if (ctx->state != DZ_DONE) dz_stream_feed(ctx, (const char *)evt->data, evt->data_len);
    return ESP_OK;
}

/* out[] must hold SENSOR_HISTORY_24H_SLOTS entries. Returns the number of
 * slots written (always SENSOR_HISTORY_24H_SLOTS on success, 0 if no usable
 * records were seen). Walks backward from ctx->t_last (the most recent
 * record actually seen) through the ring, matching what
 * sensor_history_get_24h() itself would produce (oldest-first, last slot =
 * most recent) — a ring slot is only trusted if it still holds the exact
 * absolute bucket being asked for, since a slot last written by an older
 * bucket (never revisited, i.e. a real gap) must not be misread as data. */
static int build_slots_from_accum(const dz_stream_ctx_t *ctx, history_slot_t *out)
{
    if (!ctx->have_first) return 0;

    long last_bucket = (long)((ctx->t_last - ctx->t_first) / 600);

    for (int i = 0; i < SENSOR_HISTORY_24H_SLOTS; i++) {
        int  offset_from_end = SENSOR_HISTORY_24H_SLOTS - 1 - i;   /* 0 = now */
        long abs_bucket       = last_bucket - offset_from_end;
        int  ring_idx         = (abs_bucket >= 0) ? (int)(abs_bucket % DZ_RING_SLOTS) : -1;

        if (ring_idx >= 0 && ctx->slot_abs[ring_idx] == abs_bucket && ctx->cnt[ring_idx] > 0) {
            out[i].temp     = (float)(ctx->sum_t[ring_idx] / ctx->cnt[ring_idx]);
            out[i].humidity = (float)(ctx->sum_h[ring_idx] / ctx->cnt[ring_idx]);
            out[i].valid    = true;
        } else {
            out[i].temp     = 0.0f;
            out[i].humidity = 0.0f;
            out[i].valid    = false;
        }
    }
    return SENSOR_HISTORY_24H_SLOTS;
}

/* ── orchestration ───────────────────────────────────────────────────────── */

static void fetch_domoticz(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;

    char     base_url[128] = {0};
    char     user[64]      = {0};
    char     pass[64]      = {0};
    size_t   url_len  = sizeof(base_url);
    size_t   user_len = sizeof(user);
    size_t   pass_len = sizeof(pass);
    uint16_t thp_idx  = 0;

    nvs_get_str(h, KEY_DZ_HTTP_URL,  base_url, &url_len);
    nvs_get_u16(h, KEY_DZ_THP_IDX,   &thp_idx);
    nvs_get_str(h, KEY_DZ_HTTP_USER, user, &user_len);
    nvs_get_str(h, KEY_DZ_HTTP_PASS, pass, &pass_len);
    nvs_close(h);

    if (base_url[0] == '\0' || thp_idx == 0) {
        ESP_LOGI(TAG, "domoticz history not configured — skipping");
        return;
    }

    /* The field is meant to hold just the origin (scheme://host:port), but
     * the user may have pasted a full endpoint (path/query included)
     * instead — never trust anything past the origin; always build our own
     * canonical request URL rather than passing a stale/wrong path through. */
    char *scheme_end = strstr(base_url, "://");
    char *path_start = scheme_end ? strchr(scheme_end + 3, '/') : NULL;
    if (path_start) *path_start = '\0';

    /* type=graph is the old/deprecated shortcut — current Domoticz versions
     * route graph data through the unified type=command&param=graph command
     * instead and 404 on the old form. sensor=temp is still required to
     * select which log table to read; Temp+Hum(+Baro) combo devices are
     * queried under "temp" too, since that table holds all three readings
     * together. Confirmed against a live Domoticz 2026.2 instance. */
    char url[192];
    int  ulen = snprintf(url, sizeof(url),
                          "%s/json.htm?type=command&param=graph&sensor=temp&range=day&idx=%u",
                          base_url, (unsigned)thp_idx);
    if (ulen <= 0 || ulen >= (int)sizeof(url)) {
        ESP_LOGW(TAG, "domoticz history URL too long");
        return;
    }

    ESP_LOGI(TAG, "fetching 24h history: %s", url);

    memset(&s_ctx, 0, sizeof(s_ctx));

    esp_http_client_config_t cfg = {
        .url           = url,
        .timeout_ms    = 15000,
        .event_handler = http_event_handler,
        .user_data     = &s_ctx,
        .username      = user[0] ? user : NULL,
        .password      = pass[0] ? pass : NULL,
        .auth_type     = user[0] ? HTTP_AUTH_TYPE_BASIC : HTTP_AUTH_TYPE_NONE,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return;

    esp_err_t ret = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (ret != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "GET %s failed: ret=%s status=%d", url, esp_err_to_name(ret), status);
        return;
    }

    history_slot_t slots[SENSOR_HISTORY_24H_SLOTS];
    int count = build_slots_from_accum(&s_ctx, slots);

    if (count > 0) {
        sensor_history_seed_24h(slots, count);
    } else {
        ESP_LOGW(TAG, "no usable history data in domoticz response");
    }
}

/* ── ThingsBoard REST fetch ────────────────────────────────────────────────
 * tb_token (the MQTT device access token) can publish telemetry but can't
 * read history over REST — that needs a full user login (POST
 * /api/auth/login → JWT), not the device token. Unlike Domoticz's raw
 * per-sample dump, the timeseries endpoint aggregates server-side
 * (interval=600000ms, agg=AVG), so the response is bounded to roughly
 * SENSOR_HISTORY_24H_SLOTS points per key regardless of how densely the
 * device actually reports — small enough that a plain buffer-then-parse is
 * fine here; no streaming ring buffer needed like the Domoticz path. */

#define TB_BODY_MAX_BYTES (32 * 1024)   /* generous for the small login
                                          * response — telemetry is streamed,
                                          * never buffered whole (see below) */

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
    bool   overflow;
} tb_body_t;

/* Minimal JSON string escaping (quotes/backslashes) for building the login
 * request body — this is our own request, not untrusted input, so this only
 * needs to keep the JSON well-formed for a typical credential, not defend
 * against adversarial content. */
static void json_escape(const char *src, char *dst, size_t dst_size)
{
    size_t pos = 0;
    for (; *src && pos + 2 < dst_size; src++) {
        if (*src == '"' || *src == '\\') dst[pos++] = '\\';
        if (pos + 1 < dst_size) dst[pos++] = *src;
    }
    dst[pos] = '\0';
}

/* ── streaming ThingsBoard telemetry parser ──────────────────────────────
 * {"temperature":[{"ts":..,"value":".."},...],"humidity":[...]}. Assumed
 * this would stay small thanks to interval=600000&agg=AVG server-side
 * aggregation (~144 points/key) — a real instance returned enough that
 * buffering it whole OOM'd past 8 KB regardless, so this scans the same way
 * the Domoticz path does: byte-by-byte, one small object parsed at a time,
 * never the raw response text or a whole-array JSON tree. Simpler than
 * Domoticz's version, though — no ring buffer needed, since start_ts_ms is
 * already known from our own request rather than having to be discovered
 * from the data, so each record can be indexed directly into the final
 * fixed-size slot array as it arrives. */

#define TB_OBJ_BUF_LEN 160

typedef enum {
    TB_SEEK_KEY,         /* scanning for "temperature": or "humidity": */
    TB_SEEK_ARRAY_START, /* found one, scanning for the '[' */
    TB_IN_ARRAY,         /* between records, or at ']' */
    TB_IN_OBJECT,        /* inside a {...} record */
    TB_DONE,             /* both arrays closed — ignore the rest */
} tb_scan_state_t;

typedef struct {
    tb_scan_state_t state;
    int  temp_match;      /* chars of "\"temperature\":" matched so far */
    int  hum_match;        /* chars of "\"humidity\":" matched so far */
    int  current_field;    /* 0=none (seeking), 1=temperature, 2=humidity */
    int  arrays_done;

    bool in_string;
    bool escape_next;
    int  depth;
    char obj_buf[TB_OBJ_BUF_LEN];
    int  obj_len;
    bool obj_overflow;

    int64_t          start_ts_ms;
    history_slot_t  *slots;   /* SENSOR_HISTORY_24H_SLOTS entries, caller-owned */
} tb_stream_ctx_t;

static void tb_stream_commit_object(tb_stream_ctx_t *ctx)
{
    if (ctx->obj_overflow || ctx->current_field == 0) return;

    ctx->obj_buf[ctx->obj_len] = '\0';
    cJSON *obj = cJSON_Parse(ctx->obj_buf);
    if (!obj) return;

    const cJSON *ts_item  = cJSON_GetObjectItemCaseSensitive(obj, "ts");
    const cJSON *val_item = cJSON_GetObjectItemCaseSensitive(obj, "value");
    if (cJSON_IsNumber(ts_item) && val_item) {
        double val = cJSON_IsString(val_item) ? atof(val_item->valuestring) : val_item->valuedouble;
        long idx = (long)llround((ts_item->valuedouble - (double)ctx->start_ts_ms) / 600000.0);
        if (idx >= 0 && idx < SENSOR_HISTORY_24H_SLOTS) {
            if (ctx->current_field == 1) {
                ctx->slots[idx].temp  = (float)val;
                ctx->slots[idx].valid = true;
            } else if (ctx->current_field == 2 && ctx->slots[idx].valid) {
                ctx->slots[idx].humidity = (float)val;
            }
        }
    }
    cJSON_Delete(obj);
}

static void tb_stream_feed(tb_stream_ctx_t *ctx, const char *data, int len)
{
    static const char TEMP_NEEDLE[] = "\"temperature\":";
    static const char HUM_NEEDLE[]  = "\"humidity\":";

    for (int i = 0; i < len; i++) {
        char c = data[i];
        switch (ctx->state) {
        case TB_SEEK_KEY:
            if (c == TEMP_NEEDLE[ctx->temp_match]) {
                ctx->temp_match++;
                if (TEMP_NEEDLE[ctx->temp_match] == '\0') {
                    ctx->current_field = 1;
                    ctx->state = TB_SEEK_ARRAY_START;
                    break;
                }
            } else {
                ctx->temp_match = (c == TEMP_NEEDLE[0]) ? 1 : 0;
            }
            if (c == HUM_NEEDLE[ctx->hum_match]) {
                ctx->hum_match++;
                if (HUM_NEEDLE[ctx->hum_match] == '\0') {
                    ctx->current_field = 2;
                    ctx->state = TB_SEEK_ARRAY_START;
                }
            } else {
                ctx->hum_match = (c == HUM_NEEDLE[0]) ? 1 : 0;
            }
            break;

        case TB_SEEK_ARRAY_START:
            if (c == '[') ctx->state = TB_IN_ARRAY;
            break;

        case TB_IN_ARRAY:
            if (c == '{') {
                ctx->state        = TB_IN_OBJECT;
                ctx->obj_len       = 0;
                ctx->obj_overflow  = false;
                ctx->obj_buf[ctx->obj_len++] = c;
                ctx->depth         = 1;
                ctx->in_string     = false;
                ctx->escape_next   = false;
            } else if (c == ']') {
                ctx->arrays_done++;
                if (ctx->arrays_done >= 2) {
                    ctx->state = TB_DONE;
                } else {
                    ctx->temp_match    = 0;
                    ctx->hum_match     = 0;
                    ctx->current_field = 0;
                    ctx->state         = TB_SEEK_KEY;
                }
            }
            break;

        case TB_IN_OBJECT:
            if (ctx->obj_len < (int)sizeof(ctx->obj_buf) - 1) {
                ctx->obj_buf[ctx->obj_len++] = c;
            } else {
                ctx->obj_overflow = true;
            }
            if (ctx->escape_next) {
                ctx->escape_next = false;
            } else if (ctx->in_string) {
                if (c == '\\')      ctx->escape_next = true;
                else if (c == '"')  ctx->in_string = false;
            } else if (c == '"') {
                ctx->in_string = true;
            } else if (c == '{') {
                ctx->depth++;
            } else if (c == '}') {
                ctx->depth--;
                if (ctx->depth == 0) {
                    tb_stream_commit_object(ctx);
                    ctx->state = TB_IN_ARRAY;
                }
            }
            break;

        case TB_DONE:
        default:
            return;
        }
    }
}

/* ── one connection, several requests ────────────────────────────────────
 * Login and telemetry (now split into two half-window telemetry requests,
 * see fetch_thingsboard()) share one esp_http_client handle rather than each
 * opening its own. keep_alive_enable=true was hoped to also skip repeat TLS
 * handshakes, but that turned out not to be reliable (see CLAUDE.md) —
 * reusing the handle is kept anyway since it's harmless and saves repeated
 * esp_http_client_init() calls. The event handler dispatches on `mode` since
 * login and telemetry need different buffering strategies: login's response
 * is small and bounded (buffer-then-parse), telemetry's isn't (streamed the
 * same way the Domoticz path is). */

typedef enum { TB_REQ_LOGIN, TB_REQ_TELEMETRY } tb_req_mode_t;

typedef struct {
    tb_req_mode_t   mode;
    tb_body_t       login;
    tb_stream_ctx_t telemetry;
} tb_conn_ctx_t;

static esp_err_t tb_conn_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA || evt->data_len <= 0) return ESP_OK;
    tb_conn_ctx_t *ctx = (tb_conn_ctx_t *)evt->user_data;

    if (ctx->mode == TB_REQ_TELEMETRY) {
        if (ctx->telemetry.state != TB_DONE)
            tb_stream_feed(&ctx->telemetry, (const char *)evt->data, evt->data_len);
        return ESP_OK;
    }

    tb_body_t *b = &ctx->login;
    if (b->overflow) return ESP_OK;
    size_t need = b->len + (size_t)evt->data_len + 1;
    if (need > TB_BODY_MAX_BYTES) {
        ESP_LOGW(TAG, "tb login response exceeds %d bytes, truncating", TB_BODY_MAX_BYTES);
        b->overflow = true;
        return ESP_OK;
    }
    if (need > b->cap) {
        size_t new_cap = b->cap ? b->cap * 2 : 2048;
        while (new_cap < need) new_cap *= 2;
        if (new_cap > TB_BODY_MAX_BYTES) new_cap = TB_BODY_MAX_BYTES;
        char *grown = realloc(b->buf, new_cap);
        if (!grown) {
            ESP_LOGW(TAG, "OOM growing tb login response buffer at %u bytes", (unsigned)b->cap);
            b->overflow = true;
            return ESP_OK;
        }
        b->buf = grown;
        b->cap = new_cap;
    }
    memcpy(b->buf + b->len, evt->data, evt->data_len);
    b->len += evt->data_len;
    b->buf[b->len] = '\0';
    return ESP_OK;
}

static void fetch_thingsboard(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;

    char   base_url[128]  = {0};
    char   user[64]       = {0};
    char   pass[64]       = {0};
    char   device_id[64]  = {0};
    size_t url_len = sizeof(base_url), user_len = sizeof(user);
    size_t pass_len = sizeof(pass),    dev_len  = sizeof(device_id);

    nvs_get_str(h, KEY_TB_REST_URL,  base_url,  &url_len);
    nvs_get_str(h, KEY_TB_REST_USER, user,      &user_len);
    nvs_get_str(h, KEY_TB_REST_PASS, pass,      &pass_len);
    nvs_get_str(h, KEY_TB_DEVICE_ID, device_id, &dev_len);
    nvs_close(h);

    if (base_url[0] == '\0' || user[0] == '\0' || device_id[0] == '\0') {
        ESP_LOGI(TAG, "thingsboard history not configured — skipping");
        return;
    }

    /* startTs/endTs need a real epoch "now" — this task runs only 5 s after
     * boot, before NTP has necessarily synced, so wait (bounded) for it.
     * Observed taking over 30 s on a real network on occasion, hence the
     * generous cap here rather than the more typical ~6 s. */
    for (int i = 0; i < 60 && !ntp_clock_is_synced(); i++)
        vTaskDelay(pdMS_TO_TICKS(1000));
    if (!ntp_clock_is_synced()) {
        ESP_LOGW(TAG, "thingsboard history: NTP never synced, skipping");
        return;
    }

    int64_t end_ts_ms   = (int64_t)time(NULL) * 1000;
    int64_t start_ts_ms = end_ts_ms - (int64_t)24 * 3600 * 1000;

    /* Same defensive "trust only the origin" handling as the Domoticz URL —
     * the field is meant to hold just scheme://host:port. */
    char *scheme_end = strstr(base_url, "://");
    char *path_start = scheme_end ? strchr(scheme_end + 3, '/') : NULL;
    if (path_start) *path_start = '\0';

    char user_esc[80], pass_esc[80];
    json_escape(user, user_esc, sizeof(user_esc));
    json_escape(pass, pass_esc, sizeof(pass_esc));
    char login_body[192];
    int  lblen = snprintf(login_body, sizeof(login_body),
                           "{\"username\":\"%s\",\"password\":\"%s\"}", user_esc, pass_esc);
    if (lblen <= 0 || lblen >= (int)sizeof(login_body)) {
        ESP_LOGW(TAG, "thingsboard login body too long");
        return;
    }

    char login_url[160];
    snprintf(login_url, sizeof(login_url), "%s/api/auth/login", base_url);

    /* TLS handshakes are memory-hungry throughout their whole lifetime, not
     * just at one moment, and this device runs 3 concurrent MQTT clients
     * alongside WiFi/sensors/display — confirmed on real hardware that the
     * handshake fails with insufficient memory (PSA_ERROR_INSUFFICIENT_
     * MEMORY) even with dynamic TLS buffers and the smaller cert bundle.
     * Briefly stopping the MQTT clients frees their connection buffers for
     * the ~1-2 s the fetch needs; every exit path below restarts them
     * (re-reads NVS, reconnects) via the cleanup label, whether the fetch
     * succeeded or not. */
    tb_mqtt_stop();
    domoticz_mqtt_stop();
    ha_mqtt_stop();

    cJSON *login_json = NULL;
    esp_http_client_handle_t client = NULL;

    tb_conn_ctx_t conn = {0};
    conn.mode = TB_REQ_LOGIN;

    esp_http_client_config_t cfg = {
        .url               = login_url,
        .method            = HTTP_METHOD_POST,
        .timeout_ms        = 15000,
        .event_handler     = tb_conn_event_handler,
        .user_data         = &conn,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,   /* harmless even though the server doesn't
                                       * honor it across requests — see comment
                                       * at the second esp_http_client_perform() */
        /* Default TX buffer is 512 bytes — holds the *entire* outgoing
         * request (request line + all headers combined), and ThingsBoard's
         * JWT alone runs ~570 chars, well past that once wrapped in the
         * X-Authorization header used for the second request below. */
        .buffer_size_tx    = 2048,
        .buffer_size       = 2048,
    };
    client = esp_http_client_init(&cfg);
    if (!client) goto cleanup;

    ESP_LOGI(TAG, "thingsboard: logging in at %s", login_url);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, login_body, (int)strlen(login_body));

    {
        esp_err_t ret = esp_http_client_perform(client);
        int status = esp_http_client_get_status_code(client);
        if (ret != ESP_OK || status < 200 || status >= 300 || conn.login.overflow || !conn.login.buf) {
            ESP_LOGW(TAG, "POST %s failed: ret=%s status=%d", login_url, esp_err_to_name(ret), status);
            goto cleanup;
        }
    }

    login_json = cJSON_Parse(conn.login.buf);
    free(conn.login.buf);
    conn.login.buf = NULL;
    if (!login_json) {
        ESP_LOGW(TAG, "thingsboard login response parse failed");
        goto cleanup;
    }
    char jwt[1200] = {0};
    {
        const cJSON *token_item = cJSON_GetObjectItemCaseSensitive(login_json, "token");
        if (cJSON_IsString(token_item) && token_item->valuestring)
            strncpy(jwt, token_item->valuestring, sizeof(jwt) - 1);
    }
    cJSON_Delete(login_json);
    login_json = NULL;

    if (jwt[0] == '\0') {
        ESP_LOGW(TAG, "thingsboard login did not return a token");
        goto cleanup;
    }

    {
        history_slot_t slots[SENSOR_HISTORY_24H_SLOTS];
        for (int i = 0; i < SENSOR_HISTORY_24H_SLOTS; i++) {
            slots[i].temp     = 0.0f;
            slots[i].humidity = 0.0f;
            slots[i].valid    = false;
        }

        /* Reuse the same esp_http_client handle for every request (switch
         * mode/URL/method/headers rather than opening a new one each time).
         * keep_alive_enable=true does NOT reliably avoid a fresh TLS
         * handshake per request — confirmed on real hardware, inconsistent
         * across boots whether "Certificate validated" logs once or twice —
         * but reusing the handle is still correct regardless (no downside,
         * avoids repeated esp_http_client_init()). */
        conn.mode = TB_REQ_TELEMETRY;
        conn.telemetry.start_ts_ms = start_ts_ms;
        conn.telemetry.slots       = slots;

        /* Split the 24h/144-slot request into two 12h/72-slot requests.
         * Reading the single 144-slot response failed on real hardware with
         * `Dynamic Impl: alloc(~14 KB) failed` at the raw TLS record layer,
         * intermittently, regardless of MQTT-pause/connection-reuse/buffer
         * tuning (see CLAUDE.md). Untested whether response size actually
         * drives that allocation size — an earlier test comparing limit=200
         * vs 144 showed an identically-sized failure, suggesting the
         * server's own chunking may not scale down with a smaller `limit` —
         * but each half is requested and parsed independently, so even if
         * this doesn't shrink the failure, one half succeeding while the
         * other fails still seeds half the chart instead of none of it. */
        const int     half_slots = SENSOR_HISTORY_24H_SLOTS / 2;
        const int64_t half_ms    = (int64_t)half_slots * 600000;
        bool ok_any = false;

        for (int seg = 0; seg < 2; seg++) {
            int64_t seg_start = start_ts_ms + (int64_t)seg * half_ms;
            int64_t seg_end   = (seg == 0) ? seg_start + half_ms : end_ts_ms;

            char tele_url[320];
            int  tulen = snprintf(tele_url, sizeof(tele_url),
                                   "%s/api/plugins/telemetry/DEVICE/%s/values/timeseries"
                                   "?keys=temperature,humidity&startTs=%lld&endTs=%lld"
                                   "&interval=600000&agg=AVG&limit=%d",
                                   base_url, device_id, (long long)seg_start, (long long)seg_end,
                                   half_slots);
            if (tulen <= 0 || tulen >= (int)sizeof(tele_url)) {
                ESP_LOGW(TAG, "thingsboard telemetry URL too long (segment %d/2)", seg + 1);
                continue;
            }

            /* Fresh parser state per segment; start_ts_ms/slots stay pinned
             * to the full 24h window so each record lands in the right half
             * of the shared array no matter which segment fetched it. */
            tb_stream_ctx_t *tctx = &conn.telemetry;
            tctx->state         = TB_SEEK_KEY;
            tctx->temp_match    = 0;
            tctx->hum_match     = 0;
            tctx->current_field = 0;
            tctx->arrays_done   = 0;
            tctx->in_string     = false;
            tctx->escape_next   = false;
            tctx->depth         = 0;
            tctx->obj_len       = 0;
            tctx->obj_overflow  = false;

            esp_http_client_set_url(client, tele_url);
            esp_http_client_set_method(client, HTTP_METHOD_GET);
            esp_http_client_set_post_field(client, NULL, 0);
            esp_http_client_delete_header(client, "Content-Type");
            char hdr[1280];
            if (snprintf(hdr, sizeof(hdr), "Bearer %s", jwt) < (int)sizeof(hdr))
                esp_http_client_set_header(client, "X-Authorization", hdr);

            ESP_LOGI(TAG, "fetching 24h history (segment %d/2): %s", seg + 1, tele_url);
            esp_err_t ret = esp_http_client_perform(client);
            int status = esp_http_client_get_status_code(client);
            if (ret != ESP_OK || status < 200 || status >= 300) {
                ESP_LOGW(TAG, "GET %s failed: ret=%s status=%d (segment %d/2)",
                         tele_url, esp_err_to_name(ret), status, seg + 1);
                continue;
            }
            ok_any = true;
        }

        int valid_count = 0;
        for (int i = 0; i < SENSOR_HISTORY_24H_SLOTS; i++)
            if (slots[i].valid) valid_count++;

        if (valid_count > 0) {
            ESP_LOGI(TAG, "thingsboard: %d/%d slots have data", valid_count, SENSOR_HISTORY_24H_SLOTS);
            sensor_history_seed_24h(slots, SENSOR_HISTORY_24H_SLOTS);
        } else if (ok_any) {
            ESP_LOGW(TAG, "no usable history data in thingsboard response");
        }
    }

cleanup:
    free(conn.login.buf);
    if (login_json) cJSON_Delete(login_json);
    if (client) esp_http_client_cleanup(client);
    tb_mqtt_start();
    domoticz_mqtt_start();
    ha_mqtt_start();
}

/* ── Home Assistant REST fetch ────────────────────────────────────────────
 * Unlike ThingsBoard/Domoticz, HA's /api/history/period endpoint does no
 * server-side downsampling — it returns every raw state change for the
 * requested entities, so (at this device's ~30s publish interval) up to
 * ~2880 records per entity across 24h. minimal_response=true+no_attributes=
 * true trims each record to just {"state":..,"last_changed":..}, but the
 * response can still run large — so this is scanned the same
 * byte-at-a-time way as the Domoticz/ThingsBoard paths, one small object at
 * a time, never buffered whole. Unlike Domoticz's response (whose actual
 * span/order wasn't trustworthy — see its ring-buffer comment above), HA's
 * start/end_time are honored exactly as requested, so — like ThingsBoard —
 * records can be indexed directly into a fixed-size slot array, no ring
 * buffer needed. Unlike ThingsBoard's server-aggregated single-point-per-
 * bucket response though, multiple raw records can land in the same
 * 10-minute bucket here, so (like Domoticz) sums/counts are accumulated per
 * bucket and averaged at the end, not overwritten. */

#define HA_OBJ_BUF_LEN 96   /* one {"state":"21.4","last_changed":"...Z"}
                              * record, minimal_response+no_attributes keeps
                              * these short */

typedef enum {
    HA_SEEK_OUTER,      /* looking for the opening '[' of the outer array */
    HA_SEEK_ENTITY,     /* looking for the next entity's '[', or the outer ']' */
    HA_IN_ENTITY,       /* inside one entity's array of records */
    HA_IN_OBJECT,        /* inside a {...} record */
    HA_DONE,
} ha_scan_state_t;

typedef struct {
    ha_scan_state_t state;
    int  entity_index;    /* 0 = temperature, 1 = humidity, matching the
                             * order entities were listed in filter_entity_id */

    bool in_string;
    bool escape_next;
    int  depth;
    char obj_buf[HA_OBJ_BUF_LEN];
    int  obj_len;
    bool obj_overflow;

    int64_t start_ts_ms;
    double  sum_t[SENSOR_HISTORY_24H_SLOTS];
    double  sum_h[SENSOR_HISTORY_24H_SLOTS];
    int     cnt_t[SENSOR_HISTORY_24H_SLOTS];
    int     cnt_h[SENSOR_HISTORY_24H_SLOTS];
} ha_stream_ctx_t;

static ha_stream_ctx_t s_ha_ctx;

/* Manual UTC (Y-M-D h:m:s) → epoch conversion — deliberately not mktime()
 * (which interprets its input as local time per the device's TZ setting)
 * nor timegm(); HA's REST API returns last_changed/last_updated already in
 * UTC (a trailing "Z" or "+00:00" offset), and time(NULL) on this device is
 * always a UTC epoch counter regardless of the display TZ, so this must
 * convert as UTC to line up with start_ts_ms/end_ts_ms below. */
static time_t ha_ymdhms_to_unix_utc(int y, int mo, int d, int h, int mi, int s)
{
    static const int cum_days[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int64_t days = 0;
    for (int yy = 1970; yy < y; yy++)
        days += ((yy % 4 == 0 && (yy % 100 != 0 || yy % 400 == 0)) ? 366 : 365);
    days += cum_days[mo - 1];
    if (mo > 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))) days += 1;
    days += (d - 1);
    return (time_t)(days * 86400 + h * 3600 + mi * 60 + s);
}

/* Only the "YYYY-MM-DDTHH:MM:SS" prefix is parsed — fractional seconds and
 * the trailing offset (always "+00:00" in practice from HA's REST API) are
 * ignored by sscanf's %d matching, not validated. */
static bool parse_ha_iso_utc(const char *s, time_t *out)
{
    int y, mo, d, h, mi, se;
    if (sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) return false;
    *out = ha_ymdhms_to_unix_utc(y, mo, d, h, mi, se);
    return true;
}

static void ha_stream_commit_object(ha_stream_ctx_t *ctx)
{
    if (ctx->obj_overflow || ctx->entity_index > 1) return;

    ctx->obj_buf[ctx->obj_len] = '\0';
    cJSON *obj = cJSON_Parse(ctx->obj_buf);
    if (!obj) return;

    const cJSON *state_item = cJSON_GetObjectItemCaseSensitive(obj, "state");
    const cJSON *lc_item    = cJSON_GetObjectItemCaseSensitive(obj, "last_changed");
    if (cJSON_IsString(state_item) && state_item->valuestring &&
        cJSON_IsString(lc_item) && lc_item->valuestring) {
        char *end = NULL;
        double val = strtod(state_item->valuestring, &end);
        time_t t;
        if (end != state_item->valuestring && parse_ha_iso_utc(lc_item->valuestring, &t)) {
            long idx = (long)(((int64_t)t * 1000 - ctx->start_ts_ms) / 600000);
            if (idx >= 0 && idx < SENSOR_HISTORY_24H_SLOTS) {
                if (ctx->entity_index == 0) {
                    ctx->sum_t[idx] += val;
                    ctx->cnt_t[idx]++;
                } else {
                    ctx->sum_h[idx] += val;
                    ctx->cnt_h[idx]++;
                }
            }
        }
    }
    cJSON_Delete(obj);
}

static void ha_stream_feed(ha_stream_ctx_t *ctx, const char *data, int len)
{
    for (int i = 0; i < len; i++) {
        char c = data[i];
        switch (ctx->state) {
        case HA_SEEK_OUTER:
            if (c == '[') ctx->state = HA_SEEK_ENTITY;
            break;

        case HA_SEEK_ENTITY:
            if (c == '[') ctx->state = HA_IN_ENTITY;
            else if (c == ']') ctx->state = HA_DONE;   /* outer array closed */
            break;

        case HA_IN_ENTITY:
            if (c == '{') {
                ctx->state        = HA_IN_OBJECT;
                ctx->obj_len       = 0;
                ctx->obj_overflow  = false;
                ctx->obj_buf[ctx->obj_len++] = c;
                ctx->depth         = 1;
                ctx->in_string     = false;
                ctx->escape_next   = false;
            } else if (c == ']') {
                ctx->entity_index++;
                ctx->state = HA_SEEK_ENTITY;
            }
            break;

        case HA_IN_OBJECT:
            if (ctx->obj_len < (int)sizeof(ctx->obj_buf) - 1) {
                ctx->obj_buf[ctx->obj_len++] = c;
            } else {
                ctx->obj_overflow = true;
            }
            if (ctx->escape_next) {
                ctx->escape_next = false;
            } else if (ctx->in_string) {
                if (c == '\\')      ctx->escape_next = true;
                else if (c == '"')  ctx->in_string = false;
            } else if (c == '"') {
                ctx->in_string = true;
            } else if (c == '{') {
                ctx->depth++;
            } else if (c == '}') {
                ctx->depth--;
                if (ctx->depth == 0) {
                    ha_stream_commit_object(ctx);
                    ctx->state = HA_IN_ENTITY;
                }
            }
            break;

        case HA_DONE:
        default:
            return;
        }
    }
}

static esp_err_t ha_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA || evt->data_len <= 0) return ESP_OK;
    ha_stream_ctx_t *ctx = (ha_stream_ctx_t *)evt->user_data;
    if (ctx->state != HA_DONE) ha_stream_feed(ctx, (const char *)evt->data, evt->data_len);
    return ESP_OK;
}

static void fetch_home_assistant(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;

    char base_url[128]     = {0};
    char token[256]        = {0};
    char temp_entity[64]   = {0};
    char hum_entity[64]    = {0};
    size_t url_len = sizeof(base_url), tok_len = sizeof(token);
    size_t te_len  = sizeof(temp_entity), he_len = sizeof(hum_entity);

    nvs_get_str(h, KEY_HA_HTTP_URL,    base_url,    &url_len);
    nvs_get_str(h, KEY_HA_HTTP_TOKEN,  token,       &tok_len);
    nvs_get_str(h, KEY_HA_TEMP_ENTITY, temp_entity, &te_len);
    nvs_get_str(h, KEY_HA_HUM_ENTITY,  hum_entity,  &he_len);
    nvs_close(h);

    if (base_url[0] == '\0' || token[0] == '\0' || temp_entity[0] == '\0') {
        ESP_LOGI(TAG, "home assistant history not configured — skipping");
        return;
    }

    for (int i = 0; i < 60 && !ntp_clock_is_synced(); i++)
        vTaskDelay(pdMS_TO_TICKS(1000));
    if (!ntp_clock_is_synced()) {
        ESP_LOGW(TAG, "home assistant history: NTP never synced, skipping");
        return;
    }

    time_t end_t   = time(NULL);
    time_t start_t = end_t - 24 * 3600;
    int64_t start_ts_ms = (int64_t)start_t * 1000;

    struct tm tm_start, tm_end;
    gmtime_r(&start_t, &tm_start);
    gmtime_r(&end_t,   &tm_end);
    char start_iso[32], end_iso[32];
    strftime(start_iso, sizeof(start_iso), "%Y-%m-%dT%H:%M:%SZ", &tm_start);
    strftime(end_iso,   sizeof(end_iso),   "%Y-%m-%dT%H:%M:%SZ", &tm_end);

    /* Same "trust only the origin" handling as the Domoticz/ThingsBoard
     * URLs above. */
    char *scheme_end = strstr(base_url, "://");
    char *path_start = scheme_end ? strchr(scheme_end + 3, '/') : NULL;
    if (path_start) *path_start = '\0';

    char filter[132];
    int flen = hum_entity[0]
                   ? snprintf(filter, sizeof(filter), "%s,%s", temp_entity, hum_entity)
                   : snprintf(filter, sizeof(filter), "%s", temp_entity);
    if (flen <= 0 || flen >= (int)sizeof(filter)) {
        ESP_LOGW(TAG, "home assistant entity_id(s) too long");
        return;
    }

    char url[512];
    int ulen = snprintf(url, sizeof(url),
                         "%s/api/history/period/%s?filter_entity_id=%s&end_time=%s"
                         "&minimal_response=true&no_attributes=true",
                         base_url, start_iso, filter, end_iso);
    if (ulen <= 0 || ulen >= (int)sizeof(url)) {
        ESP_LOGW(TAG, "home assistant history URL too long");
        return;
    }

    char auth_hdr[300];
    if (snprintf(auth_hdr, sizeof(auth_hdr), "Bearer %s", token) >= (int)sizeof(auth_hdr)) {
        ESP_LOGW(TAG, "home assistant token too long");
        return;
    }

    /* Same MQTT-pause rationale as the ThingsBoard fetch — harmless when
     * the HA instance is plain http (typical for a local install, no TLS
     * memory pressure at all), and consistent/cheap to always do. */
    tb_mqtt_stop();
    domoticz_mqtt_stop();
    ha_mqtt_stop();

    memset(&s_ha_ctx, 0, sizeof(s_ha_ctx));
    s_ha_ctx.start_ts_ms = start_ts_ms;

    esp_http_client_config_t cfg = {
        .url               = url,
        .timeout_ms        = 20000,
        .event_handler     = ha_event_handler,
        .user_data         = &s_ha_ctx,
        .crt_bundle_attach = esp_crt_bundle_attach,   /* no-op for http:// */
        .buffer_size_tx    = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) goto ha_cleanup;

    esp_http_client_set_header(client, "Authorization", auth_hdr);

    ESP_LOGI(TAG, "fetching 24h history: %s", url);
    {
        esp_err_t ret = esp_http_client_perform(client);
        int status = esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);

        if (ret != ESP_OK || status < 200 || status >= 300) {
            ESP_LOGW(TAG, "GET %s failed: ret=%s status=%d", url, esp_err_to_name(ret), status);
            goto ha_cleanup;
        }
    }

    {
        history_slot_t slots[SENSOR_HISTORY_24H_SLOTS];
        int valid_count = 0;
        for (int i = 0; i < SENSOR_HISTORY_24H_SLOTS; i++) {
            if (s_ha_ctx.cnt_t[i] > 0) {
                slots[i].temp     = (float)(s_ha_ctx.sum_t[i] / s_ha_ctx.cnt_t[i]);
                slots[i].humidity = (s_ha_ctx.cnt_h[i] > 0)
                                         ? (float)(s_ha_ctx.sum_h[i] / s_ha_ctx.cnt_h[i]) : 0.0f;
                slots[i].valid    = true;
                valid_count++;
            } else {
                slots[i].temp     = 0.0f;
                slots[i].humidity = 0.0f;
                slots[i].valid    = false;
            }
        }

        if (valid_count > 0) {
            ESP_LOGI(TAG, "home assistant: %d/%d slots have data", valid_count, SENSOR_HISTORY_24H_SLOTS);
            sensor_history_seed_24h(slots, SENSOR_HISTORY_24H_SLOTS);
        } else {
            ESP_LOGW(TAG, "no usable history data in home assistant response");
        }
    }

ha_cleanup:
    tb_mqtt_start();
    domoticz_mqtt_start();
    ha_mqtt_start();
}

static void fetch_task(void *arg)
{
    (void)arg;

    uint8_t source = HIST_SOURCE_NONE;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, KEY_HIST_SOURCE, &source);
        nvs_close(h);
    }

    switch (source) {
    case HIST_SOURCE_DOMOTICZ:
        fetch_domoticz();
        break;
    case HIST_SOURCE_TB:
        fetch_thingsboard();
        break;
    case HIST_SOURCE_HA:
        fetch_home_assistant();
        break;
    default:
        break;   /* HIST_SOURCE_NONE: silent no-op */
    }

    vTaskDelete(NULL);
}

/* tb_mqtt/dz_mqtt/ha_mqtt all start connecting the instant IP_EVENT_STA_GOT_IP
 * fires too, and their TLS/TCP setup competes for heap right at that
 * moment — a short delay avoids piling straight onto that initial burst.
 * For Domoticz, a longer delay didn't help past a point (that response is
 * just too large to buffer whole on this device regardless of timing,
 * which is what the streaming parser above actually fixes); ThingsBoard's
 * HTTPS calls hit a similarly persistent wall (mbedtls TLS handshake
 * allocation failures, confirmed identical at both 5 s and 65 s — a real
 * memory ceiling, not transient boot contention that clears with time), so
 * there's no benefit to a longer wait here either. */
static void fetch_delay_cb(void *arg)
{
    (void)arg;
    xTaskCreate(fetch_task, "hist_fetch", 16384, NULL, 4, NULL);
}

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)id; (void)data;
    if (s_triggered) return;
    s_triggered = true;

    esp_timer_handle_t t;
    esp_timer_create_args_t a = { .callback = fetch_delay_cb, .name = "hist_fetch_dly" };
    if (esp_timer_create(&a, &t) == ESP_OK) {
        esp_timer_start_once(t, 5ULL * 1000000ULL);
    } else {
        xTaskCreate(fetch_task, "hist_fetch", 16384, NULL, 4, NULL);
    }
}

esp_err_t history_fetch_init(void)
{
    s_triggered = false;
    return esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                on_got_ip, NULL, NULL);
}