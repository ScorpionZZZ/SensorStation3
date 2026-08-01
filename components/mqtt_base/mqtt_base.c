// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

#include "mqtt_base.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_check.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define DEFAULT_INTERVAL_US  (30 * 1000000UL)

struct mqtt_base {
    mqtt_base_cfg_t          cfg;
    char                     client_id[24];   /* "<prefix><12 hex>" */
    esp_mqtt_client_handle_t client;
    esp_timer_handle_t       timer;
    uint32_t                 interval_us;
    volatile bool            connected;
    volatile bool            started;
};

/* ── internal: teardown existing client + timer ───────────────────────────── */

static void base_teardown(mqtt_base_t *b)
{
    if (b->timer) {
        esp_timer_stop(b->timer);
        esp_timer_delete(b->timer);
        b->timer = NULL;
    }
    if (b->client) {
        esp_mqtt_client_stop(b->client);
        esp_mqtt_client_destroy(b->client);
        b->client = NULL;
    }
    b->connected = false;
    b->started   = false;
}

/* ── timer trampoline: drive the module's publish callback ────────────────── */

static void pub_timer_cb(void *arg)
{
    mqtt_base_t *b = arg;
    if (b->connected) b->cfg.on_publish_tick(b->client, b->cfg.ctx);
}

/* ── IP event: start the client on first connection after boot ────────────── */

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    mqtt_base_t *b = arg;
    if (!b->client || b->started) return;
    ESP_LOGI(b->cfg.tag, "got IP, starting MQTT client");
    esp_mqtt_client_start(b->client);
    b->started = true;
}

/* ── MQTT event skeleton ──────────────────────────────────────────────────── */

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    mqtt_base_t *b = arg;
    esp_mqtt_event_handle_t ev = data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(b->cfg.tag, "connected");
        b->connected = true;
        if (b->cfg.on_connected) b->cfg.on_connected(b->client, b->cfg.ctx);
        b->cfg.on_publish_tick(b->client, b->cfg.ctx);   /* immediate first sample */
        esp_timer_stop(b->timer);                        /* idempotent */
        esp_timer_start_periodic(b->timer, b->interval_us);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(b->cfg.tag, "disconnected");
        b->connected = false;
        esp_timer_stop(b->timer);
        if (b->cfg.on_disconnected) b->cfg.on_disconnected(b->cfg.ctx);
        break;

    case MQTT_EVENT_PUBLISHED:
        if (b->cfg.on_published) b->cfg.on_published(b->client, ev->msg_id, b->cfg.ctx);
        break;

    case MQTT_EVENT_ERROR:
        if (ev->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGE(b->cfg.tag, "transport error: esp_err=0x%x sock_errno=%d",
                     ev->error_handle->esp_tls_last_esp_err,
                     ev->error_handle->esp_transport_sock_errno);
        } else if (ev->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            /* Surfaces bad-credential / not-authorized failures that were
             * previously invisible behind a silent reconnect loop. */
            ESP_LOGE(b->cfg.tag, "connection refused: return_code=%d",
                     ev->error_handle->connect_return_code);
        }
        break;

    default:
        break;
    }
}

/* ── public API ───────────────────────────────────────────────────────────── */

mqtt_base_t *mqtt_base_new(const mqtt_base_cfg_t *cfg)
{
    if (!cfg || !cfg->tag || !cfg->id_prefix || !cfg->on_publish_tick) return NULL;

    mqtt_base_t *b = calloc(1, sizeof(*b));
    if (!b) return NULL;
    b->cfg = *cfg;

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(b->client_id, sizeof(b->client_id), "%s%02x%02x%02x%02x%02x%02x",
             cfg->id_prefix, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    /* Registered even while disabled, so a later start() (user saves settings)
     * still benefits from it on WiFi reconnects. */
    if (esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            on_got_ip, b, NULL) != ESP_OK) {
        free(b);
        return NULL;
    }
    return b;
}

esp_err_t mqtt_base_configure(mqtt_base_t *b, const mqtt_base_conn_t *conn)
{
    if (!b) return ESP_ERR_INVALID_ARG;
    base_teardown(b);
    if (!conn) return ESP_OK;   /* disabled — no client */

    b->interval_us = conn->interval_us ? conn->interval_us : DEFAULT_INTERVAL_US;

    esp_timer_create_args_t ta = {
        .callback = pub_timer_cb,
        .arg      = b,
        .name     = "mqtt_pub",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&ta, &b->timer), b->cfg.tag, "timer_create");

    esp_mqtt_client_config_t mcfg = {
        .broker.address.uri                  = conn->uri,
        .credentials.client_id               = b->client_id,
        .credentials.username                = (conn->username && conn->username[0])
                                                   ? conn->username : NULL,
        .credentials.authentication.password = (conn->password && conn->password[0])
                                                   ? conn->password : NULL,
    };
    if (conn->lwt_topic) {
        mcfg.session.last_will.topic  = conn->lwt_topic;
        mcfg.session.last_will.msg    = conn->lwt_msg;
        mcfg.session.last_will.qos    = conn->lwt_qos;
        mcfg.session.last_will.retain = conn->lwt_retain;
    }

    b->client = esp_mqtt_client_init(&mcfg);
    if (!b->client) {
        base_teardown(b);
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(
        esp_mqtt_client_register_event(b->client, ESP_EVENT_ANY_ID,
                                       mqtt_event_handler, b),
        b->cfg.tag, "reg_mqtt");
    return ESP_OK;
}

esp_err_t mqtt_base_start(mqtt_base_t *b)
{
    if (!b) return ESP_ERR_INVALID_ARG;
    if (!b->client) return ESP_OK;   /* disabled — no-op success */
    esp_err_t ret = esp_mqtt_client_start(b->client);
    b->started = true;
    return ret;
}

void mqtt_base_stop(mqtt_base_t *b)
{
    if (b) base_teardown(b);
}

bool mqtt_base_is_connected(const mqtt_base_t *b)
{
    return b && b->connected;
}