// Copyright 2026 Viacheslav Korniienko
// Licensed under the Apache License, Version 2.0 — see LICENSE file

// Shared MQTT lifecycle plumbing for tb_mqtt / domoticz_mqtt / ha_mqtt.
//
// Owns: client-id generation (id_prefix + STA MAC), IP-event-gated first
// start, the publish timer, the CONNECTED/DISCONNECTED/PUBLISHED/ERROR event
// skeleton, and teardown.
//
// Owns NOTHING that reaches the wire: topics, payloads, QoS, retain, LWT,
// auth, cadence and connect-time ordering all come from the caller via the
// callbacks and mqtt_base_conn_t below. This keeps each module in full
// control of its broker's observable contract.
#pragma once
#include "mqtt_client.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct mqtt_base mqtt_base_t;   // opaque, one instance per module

/* ── Callbacks ────────────────────────────────────────────────────────────
 * Threading: on_connected/on_disconnected/on_published run in the esp-mqtt
 * event task; on_publish_tick runs in the esp_timer task (and once inline on
 * connect). A callback must not block. `ctx` is the pointer from cfg.ctx. */
typedef struct {
    const char *tag;            /* log tag, e.g. "dz_mqtt"                    */
    const char *id_prefix;      /* client-id prefix → "<prefix><mac>"        */
    void       *ctx;            /* opaque, passed back to every callback      */

    /* Fires once per connect, BEFORE the first publish. HA uses it to publish
     * "online" + discovery in the required order. May be NULL. */
    void (*on_connected)(esp_mqtt_client_handle_t client, void *ctx);

    /* The actual publish. Called once immediately on connect, then every
     * interval. The module owns topic/payload/QoS/retain here. Required. */
    void (*on_publish_tick)(esp_mqtt_client_handle_t client, void *ctx);

    /* Fires on disconnect. TB uses it to reset its in-flight msg id. May be NULL. */
    void (*on_disconnected)(void *ctx);

    /* Fires on MQTT_EVENT_PUBLISHED (PUBACK). TB uses it to advance its
     * offline queue (and publish the next entry) and set last_confirmed.
     * May be NULL. */
    void (*on_published)(esp_mqtt_client_handle_t client, int msg_id, void *ctx);
} mqtt_base_cfg_t;

/* ── Per-connection parameters (resolved from NVS by the module) ──────────
 * The module reads its own tb_ / dz_ / ha_ keys and fills this in. NULL/empty
 * user or pass ⇒ anonymous. lwt_topic == NULL ⇒ no last-will. String fields
 * must stay valid until the next configure()/stop() — modules keep them in
 * their own static buffers (esp-mqtt also copies them at client init). */
typedef struct {
    const char *uri;            /* broker URI (mqtt:// or mqtts://)          */
    const char *username;       /* NULL/"" ⇒ none  (TB passes token here)    */
    const char *password;       /* NULL/"" ⇒ none                            */
    uint32_t    interval_us;    /* publish cadence; 0 ⇒ 30 s default         */

    const char *lwt_topic;      /* NULL ⇒ no last-will (HA only)             */
    const char *lwt_msg;
    int         lwt_qos;
    bool        lwt_retain;
} mqtt_base_conn_t;

/* ── Lifecycle ────────────────────────────────────────────────────────────
 * new():        one-time. Generates the client-id and registers the
 *               IP_EVENT_STA_GOT_IP handler (deferred first start). No client
 *               yet. Returns NULL on allocation/registration failure.
 * configure():  teardown any existing client+timer, then build & register a
 *               new client from `conn`. Pass conn == NULL to stay disabled
 *               (returns ESP_OK, no client) — mirrors the "credentials
 *               missing ⇒ silent no-op" behaviour of the standalone modules.
 * start():      start the client now (after the user saves settings). A
 *               no-op returning ESP_OK when disabled (no client).
 * stop():       teardown (stop+destroy client, stop+delete timer).           */
mqtt_base_t *mqtt_base_new(const mqtt_base_cfg_t *cfg);
esp_err_t    mqtt_base_configure(mqtt_base_t *b, const mqtt_base_conn_t *conn);
esp_err_t    mqtt_base_start(mqtt_base_t *b);
void         mqtt_base_stop(mqtt_base_t *b);
bool         mqtt_base_is_connected(const mqtt_base_t *b);