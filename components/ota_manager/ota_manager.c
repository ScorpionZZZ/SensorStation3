// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "ota_manager.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ota_manager";

#define OTA_TASK_STACK  8192
#define OTA_TASK_PRIO   5
#define OTA_URL_MAX     256

typedef struct {
    char              url[OTA_URL_MAX];
    ota_progress_cb_t cb;
    void             *user_data;
} ota_task_args_t;

static volatile ota_state_t       s_state       = OTA_STATE_IDLE;
static ota_task_args_t             s_args;
static volatile ota_check_state_t s_check_state    = OTA_CHECK_IDLE;
static char                        s_check_url[OTA_URL_MAX];
static char                        s_running_ver[32];
static char                        s_server_ver[16];
static volatile bool               s_auto_update   = false;

void ota_manager_mark_valid(void)
{
    esp_err_t ret = esp_ota_mark_app_valid_cancel_rollback();
    if (ret == ESP_OK)
        ESP_LOGI(TAG, "firmware committed");
    else if (ret != ESP_ERR_NOT_SUPPORTED)
        ESP_LOGW(TAG, "mark_valid: %s", esp_err_to_name(ret));
}

esp_err_t ota_manager_init(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(running, &desc) == ESP_OK)
        ESP_LOGI(TAG, "running: %s %s", desc.project_name, desc.version);
    return ESP_OK;
}

ota_state_t ota_manager_get_state(void)
{
    return s_state;
}

static void notify(ota_progress_cb_t cb, void *user_data,
                   int pct, ota_state_t state, const char *msg)
{
    s_state = state;
    if (cb) cb(pct, state, msg, user_data);
}

static void ota_task(void *arg)
{
    ota_task_args_t *a = (ota_task_args_t *)arg;

    notify(a->cb, a->user_data, 0, OTA_STATE_DOWNLOADING, "Connecting.");

    esp_http_client_config_t http_cfg = {
        .url            = a->url,
        .timeout_ms     = 10000,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_https_ota_handle_t handle = NULL;
    esp_err_t ret = esp_https_ota_begin(&ota_cfg, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ota_begin: %s", esp_err_to_name(ret));
        notify(a->cb, a->user_data, 0, OTA_STATE_FAILED, esp_err_to_name(ret));
        goto done;
    }

    esp_app_desc_t new_desc;
    if (esp_https_ota_get_img_desc(handle, &new_desc) == ESP_OK)
        ESP_LOGI(TAG, "new firmware: %s %s", new_desc.project_name, new_desc.version);

    int image_size = esp_https_ota_get_image_size(handle);

    while (1) {
        ret = esp_https_ota_perform(handle);
        if (ret != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;

        int written = esp_https_ota_get_image_len_read(handle);
        int pct = (image_size > 0) ? (written * 100 / image_size) : 0;
        char msg[32];
        snprintf(msg, sizeof(msg), "Downloading: %d%%", pct);
        notify(a->cb, a->user_data, pct, OTA_STATE_DOWNLOADING, msg);
    }

    if (!esp_https_ota_is_complete_data_received(handle)) {
        ESP_LOGE(TAG, "incomplete data");
        notify(a->cb, a->user_data, 0, OTA_STATE_FAILED, "Incomplete download");
        esp_https_ota_abort(handle);
        goto done;
    }

    notify(a->cb, a->user_data, 100, OTA_STATE_VERIFYING, "Verifying.");
    ret = esp_https_ota_finish(handle);
    handle = NULL;

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ota_finish: %s", esp_err_to_name(ret));
        notify(a->cb, a->user_data, 0, OTA_STATE_FAILED, esp_err_to_name(ret));
        goto done;
    }

    ESP_LOGI(TAG, "update complete, rebooting");
    notify(a->cb, a->user_data, 100, OTA_STATE_REBOOTING, "Done! Rebooting.");
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();

done:
    if (handle) esp_https_ota_abort(handle);
    vTaskDelete(NULL);
}

esp_err_t ota_manager_start(const char *url, ota_progress_cb_t cb, void *user_data)
{
    if (s_state == OTA_STATE_DOWNLOADING || s_state == OTA_STATE_VERIFYING)
        return ESP_ERR_INVALID_STATE;

    if (!url || url[0] == '\0')
        return ESP_ERR_INVALID_ARG;

    strncpy(s_args.url, url, OTA_URL_MAX - 1);
    s_args.url[OTA_URL_MAX - 1] = '\0';
    /* http_parser_parse_url rejects any trailing whitespace */
    int url_len = strlen(s_args.url);
    while (url_len > 0 && (s_args.url[url_len - 1] == ' '  ||
                            s_args.url[url_len - 1] == '\t' ||
                            s_args.url[url_len - 1] == '\r' ||
                            s_args.url[url_len - 1] == '\n'))
        s_args.url[--url_len] = '\0';

    s_args.cb        = cb;
    s_args.user_data = user_data;
    s_state          = OTA_STATE_IDLE;

    BaseType_t ok = xTaskCreate(ota_task, "ota", OTA_TASK_STACK,
                                &s_args, OTA_TASK_PRIO, NULL);
    return (ok == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

/* ── Update check ────────────────────────────────────────────────────── */

ota_check_state_t ota_manager_check_state(void)
{
    return s_check_state;
}

/* Compare two "major.minor.build" strings. Returns true if a > b. */
static bool version_gt(const char *a, const char *b)
{
    int am[3] = {0}, bm[3] = {0};
    sscanf(a, "%d.%d.%d", &am[0], &am[1], &am[2]);
    sscanf(b, "%d.%d.%d", &bm[0], &bm[1], &bm[2]);
    for (int i = 0; i < 3; i++) {
        if (am[i] > bm[i]) return true;
        if (am[i] < bm[i]) return false;
    }
    return false;
}

static void check_task(void *arg)
{
    (void)arg;
    s_check_state = OTA_CHECK_CHECKING;

    esp_http_client_config_t cfg = {
        .url        = s_check_url,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        s_check_state = OTA_CHECK_FAILED;
        vTaskDelete(NULL);
        return;
    }

    esp_err_t ret = esp_http_client_open(client, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "check open: %s", esp_err_to_name(ret));
        s_check_state = OTA_CHECK_FAILED;
        goto done;
    }

    esp_http_client_fetch_headers(client);

    char body[36] = {0};  /* 35 bytes payload + null terminator */
    int read_len = esp_http_client_read(client, body, sizeof(body) - 1);
    if (read_len <= 0) {
        ESP_LOGW(TAG, "check read empty");
        s_check_state = OTA_CHECK_FAILED;
        goto done;
    }
    body[read_len] = '\0';

    /* Parse {"version":"x.y.z"} — no JSON library needed for this shape. */
    char *p = strstr(body, "\"version\"");
    if (!p) { s_check_state = OTA_CHECK_FAILED; goto done; }
    p = strchr(p, ':');
    if (!p) { s_check_state = OTA_CHECK_FAILED; goto done; }
    p = strchr(p, '"');
    if (!p) { s_check_state = OTA_CHECK_FAILED; goto done; }
    p++;
    char *end = strchr(p, '"');
    if (!end) { s_check_state = OTA_CHECK_FAILED; goto done; }
    *end = '\0';

    strncpy(s_server_ver, p, sizeof(s_server_ver) - 1);
    s_server_ver[sizeof(s_server_ver) - 1] = '\0';

    const char *running = s_running_ver[0] ? s_running_ver : esp_app_get_description()->version;
    ESP_LOGI(TAG, "check: server=%s running=%s", p, running);

    if (version_gt(p, running)) {
        s_check_state = OTA_CHECK_AVAILABLE;
        if (s_auto_update) {
            /* Derive binary URL: replace trailing .json with .bin */
            char bin_url[OTA_URL_MAX];
            strncpy(bin_url, s_check_url, sizeof(bin_url) - 1);
            bin_url[sizeof(bin_url) - 1] = '\0';
            char *dot_json = strstr(bin_url, ".json");
            if (dot_json && dot_json[5] == '\0')
                strcpy(dot_json, ".bin");
            ESP_LOGI(TAG, "auto-update: starting OTA from %s", bin_url);
            esp_err_t r = ota_manager_start(bin_url, NULL, NULL);
            if (r != ESP_OK)
                ESP_LOGW(TAG, "auto-update start failed: %s", esp_err_to_name(r));
        }
    } else {
        s_check_state = OTA_CHECK_UP_TO_DATE;
    }

done:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    vTaskDelete(NULL);
}

static void check_timer_cb(void *arg)
{
    (void)arg;
    if (s_check_url[0] == '\0') return;
    xTaskCreate(check_task, "ota_chk", 4096, NULL, 3, NULL);
}

void ota_manager_get_server_version(char *out, size_t len)
{
    strncpy(out, s_server_ver, len - 1);
    out[len - 1] = '\0';
}

void ota_manager_set_auto(bool enabled)
{
    s_auto_update = enabled;
    ESP_LOGI(TAG, "auto-update %s", enabled ? "enabled" : "disabled");
}

esp_err_t ota_manager_check_start(const char *check_url, const char *running_version)
{
    if (!check_url || check_url[0] == '\0') return ESP_ERR_INVALID_ARG;
    strncpy(s_check_url, check_url, sizeof(s_check_url) - 1);
    s_check_url[sizeof(s_check_url) - 1] = '\0';
    if (running_version && running_version[0] != '\0') {
        strncpy(s_running_ver, running_version, sizeof(s_running_ver) - 1);
        s_running_ver[sizeof(s_running_ver) - 1] = '\0';
    }

    /* One-shot timer fires first check after 60 s (WiFi + NTP settle time). */
    esp_timer_handle_t t_first;
    esp_timer_create_args_t a_first = { .callback = check_timer_cb, .name = "ota_chk1" };
    ESP_RETURN_ON_ERROR(esp_timer_create(&a_first, &t_first), TAG, "chk timer1");
    ESP_RETURN_ON_ERROR(esp_timer_start_once(t_first, 60ULL * 1000000ULL), TAG, "chk start1");

    /* Periodic timer: every 6 hours. First fire is at +6 h. */
    esp_timer_handle_t t_periodic;
    esp_timer_create_args_t a_periodic = { .callback = check_timer_cb, .name = "ota_chk" };
    ESP_RETURN_ON_ERROR(esp_timer_create(&a_periodic, &t_periodic), TAG, "chk timer2");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(t_periodic,
                        6ULL * 3600ULL * 1000000ULL), TAG, "chk start2");

    ESP_LOGI(TAG, "update check scheduled: %s", s_check_url);
    return ESP_OK;
}