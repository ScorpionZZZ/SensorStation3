// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "wifi_mgr";

#define BIT_CONNECTED  BIT0
#define BIT_SCAN_DONE  BIT1

static EventGroupHandle_t    s_eg;
static esp_netif_t          *s_netif;
static volatile wifi_state_t s_state = WIFI_STATE_IDLE;
static int                   s_retry;
static bool                  s_scan_ready;
static bool                  s_has_config;  /* true after wifi_manager_connect() */

/* ── event handler ────────────────────────────────────────────────────── */

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            if (s_has_config) {
                esp_wifi_connect();
                s_state = WIFI_STATE_CONNECTING;
            }
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *ev = data;
            ESP_LOGW(TAG, "disconnected reason=%d retry=%d", ev->reason, s_retry);
            s_state = WIFI_STATE_FAILED;
            if (!s_has_config) break;
            s_retry++;
            /* exponential back-off, capped at 30 s */
            uint32_t delay_ms = 1000u * (s_retry < 5 ? (1u << s_retry) : 30u);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
            esp_wifi_connect();
            s_state = WIFI_STATE_CONNECTING;
            break;
        }

        case WIFI_EVENT_SCAN_DONE:
            s_scan_ready = true;
            xEventGroupSetBits(s_eg, BIT_SCAN_DONE);
            break;

        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = data;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        s_retry = 0;
        s_state = WIFI_STATE_CONNECTED;
        xEventGroupSetBits(s_eg, BIT_CONNECTED);
    }
}

/* ── public API ───────────────────────────────────────────────────────── */

esp_err_t wifi_manager_init(void)
{
    s_eg = xEventGroupCreate();
    if (!s_eg) return ESP_ERR_NO_MEM;

    ESP_RETURN_ON_ERROR(esp_netif_init(),               TAG, "netif_init");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event_loop");

    s_netif = esp_netif_create_default_wifi_sta();
    if (!s_netif) return ESP_ERR_NO_MEM;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi_init");

    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            wifi_event_handler, NULL, NULL),
        TAG, "reg_wifi");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            wifi_event_handler, NULL, NULL),
        TAG, "reg_ip");

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set_mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(),                  TAG, "start");

    ESP_LOGI(TAG, "ready (idle — call wifi_manager_connect() to associate)");
    return ESP_OK;
}

/* Caller is responsible for persisting credentials to NVS before calling.
 * Non-blocking — poll wifi_manager_get_state() for result. */
esp_err_t wifi_manager_connect(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0]) return ESP_ERR_INVALID_ARG;

    esp_wifi_disconnect();

    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid,     ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, pass ? pass : "", sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = (pass && pass[0]) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable    = true;

    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wc), TAG, "set_cfg");
    s_retry     = 0;
    s_has_config = true;
    s_state     = WIFI_STATE_CONNECTING;
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "connect");
    ESP_LOGI(TAG, "connecting to \"%s\"", ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_scan_start(void)
{
    s_scan_ready = false;
    xEventGroupClearBits(s_eg, BIT_SCAN_DONE);
    wifi_scan_config_t sc = {
        .ssid        = NULL,
        .bssid       = NULL,
        .channel     = 0,
        .show_hidden = false,
        .scan_type   = WIFI_SCAN_TYPE_ACTIVE,
    };
    return esp_wifi_scan_start(&sc, false);   /* async */
}

esp_err_t wifi_manager_scan_get(wifi_ap_record_t *out, uint16_t *count)
{
    if (!s_scan_ready) return ESP_ERR_INVALID_STATE;
    return esp_wifi_scan_get_ap_records(count, out);
}

wifi_state_t wifi_manager_get_state(void)   { return s_state; }
bool         wifi_manager_scan_ready(void)   { return s_scan_ready; }

void wifi_manager_get_ip(char *out, size_t len)
{
    if (s_state != WIFI_STATE_CONNECTED || !s_netif) {
        out[0] = '\0';
        return;
    }
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(s_netif, &info) == ESP_OK)
        snprintf(out, len, IPSTR, IP2STR(&info.ip));
    else
        out[0] = '\0';
}

int wifi_manager_get_rssi_pct(void)
{
    if (s_state != WIFI_STATE_CONNECTED) return -1;
    wifi_ap_record_t info;
    if (esp_wifi_sta_get_ap_info(&info) != ESP_OK) return -1;
    int r = info.rssi;
    if (r <= -100) return 0;
    if (r >= -50)  return 100;
    return (r + 100) * 2;
}

int wifi_manager_get_rssi_dbm(void)
{
    if (s_state != WIFI_STATE_CONNECTED) return -100;
    wifi_ap_record_t info;
    if (esp_wifi_sta_get_ap_info(&info) != ESP_OK) return -100;
    return info.rssi;
}