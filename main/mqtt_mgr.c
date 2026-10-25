/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "mqtt_client.h"

#include "appconf.h"
#include "fresh.h"
#include "mqtt_mgr.h"
#include "sensors.h"
#include "time_mgr.h"

static const char *TAG = "mqtt";

#define HA_STATUS_TOPIC "homeassistant/status"

#define AVAIL_ONLINE  "online"
#define AVAIL_OFFLINE "offline"

typedef struct {
    char topic[APPCONF_TOPIC_MAX + 1];
    char id_temp[40];
    char id_range[40];
} slot_topics_t;

static struct {
    esp_mqtt_client_handle_t client;
    const appconf_t *cfg;
    bool enabled;

    slot_topics_t slot[APPCONF_SENSOR_COUNT];
    char t_avail[APPCONF_PREFIX_MAX + 16];
    char dev_id[24];

    bool connected;
    bool subscribed;
    bool discovery_dirty;
    bool cleanup_done;
    bool avail_published;
    uint32_t published_seq;
    int64_t last_pub_epoch;
    uint32_t errors;
} ctx;

/* An entity is unavailable while the device is offline or its slot
 * publishes a null temperature. */
static cJSON *discovery_base(int i, const char *suffix, const char *object_id)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    char name[APPCONF_SENSOR_NAME_MAX + 16];
    snprintf(name, sizeof name, "%s %s", ctx.cfg->sensor[i].name, suffix);
    const char *topic = ctx.slot[i].topic;
    cJSON_AddStringToObject(root, "name", name);
    cJSON_AddStringToObject(root, "unique_id", object_id);
    cJSON_AddStringToObject(root, "state_topic", topic);
    cJSON_AddStringToObject(root, "payload_available", AVAIL_ONLINE);
    cJSON_AddStringToObject(root, "payload_not_available", AVAIL_OFFLINE);
    cJSON_AddStringToObject(root, "availability_mode", "all");
    cJSON *avail = cJSON_AddArrayToObject(root, "availability");
    if (avail != NULL) {
        cJSON *dev = cJSON_CreateObject();
        cJSON_AddStringToObject(dev, "topic", ctx.t_avail);
        cJSON_AddItemToArray(avail, dev);
        cJSON *slot = cJSON_CreateObject();
        cJSON_AddStringToObject(slot, "topic", topic);
        cJSON_AddStringToObject(slot, "value_template",
                                "{{ 'online' if value_json.temperature is not none "
                                "else 'offline' }}");
        cJSON_AddItemToArray(avail, slot);
    }
    cJSON_AddStringToObject(root, "json_attributes_topic", topic);
    cJSON_AddStringToObject(root, "json_attributes_template",
                            "{{ {'low': value_json.low, 'high': value_json.high, "
                            "'timestamp': value_json.timestamp} | tojson }}");

    cJSON *dev = cJSON_AddObjectToObject(root, "device");
    if (dev != NULL) {
        cJSON_AddStringToObject(dev, "identifiers", ctx.dev_id);
        cJSON_AddStringToObject(dev, "name", ctx.cfg->device_name);
        cJSON_AddStringToObject(dev, "manufacturer", "GrakovNe");
        cJSON_AddStringToObject(dev, "model", "esp32-hydro-temp");
        cJSON_AddStringToObject(dev, "sw_version", APP_VER);
    }
    return root;
}

static char *discovery_temp_json(int i)
{
    cJSON *root = discovery_base(i, "temperature", ctx.slot[i].id_temp);
    if (root == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "device_class", "temperature");
    cJSON_AddStringToObject(root, "unit_of_measurement", "°C");
    cJSON_AddStringToObject(root, "state_class", "measurement");
    cJSON_AddStringToObject(root, "value_template", "{{ value_json.temperature }}");
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static char *discovery_range_json(int i)
{
    cJSON *root = discovery_base(i, "out of range", ctx.slot[i].id_range);
    if (root == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "device_class", "problem");
    cJSON_AddStringToObject(root, "value_template",
                            "{{ 'ON' if value_json.out_of_range else 'OFF' }}");
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static int publish_config(const char *component, const char *object_id,
                          const char *payload)
{
    char topic[96];
    snprintf(topic, sizeof topic, "homeassistant/%s/%s/config", component,
             object_id);
    return esp_mqtt_client_publish(ctx.client, topic, payload, 0, 1, true);
}

static void publish_discovery(void)
{
    bool ok = true;
    for (int i = 0; i < APPCONF_SENSOR_COUNT; i++) {
        char *t = discovery_temp_json(i);
        char *r = discovery_range_json(i);
        if (t == NULL || r == NULL ||
            publish_config("sensor", ctx.slot[i].id_temp, t) < 0 ||
            publish_config("binary_sensor", ctx.slot[i].id_range, r) < 0) {
            ok = false;
        }
        free(t);
        free(r);
    }
    if (!ok) {
        ctx.errors++;
        ESP_LOGW(TAG, "discovery publish failed");
    } else {
        ESP_LOGI(TAG, "discovery configs published");
    }
}

static void publish_discovery_cleanup(void)
{
    for (int i = 0; i < APPCONF_SENSOR_COUNT; i++) {
        publish_config("sensor", ctx.slot[i].id_temp, "");
        publish_config("binary_sensor", ctx.slot[i].id_range, "");
    }
    ctx.cleanup_done = true;
}

static void publish_availability(void)
{
    if (esp_mqtt_client_publish(ctx.client, ctx.t_avail, AVAIL_ONLINE, 0, 1,
                                true) < 0) {
        ctx.errors++;
        return;
    }
    ctx.avail_published = true;
    ESP_LOGI(TAG, "availability: %s", AVAIL_ONLINE);
}

static char *reading_json(const sensors_reading_t *r, int i)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    const appconf_sensor_t *sc = &ctx.cfg->sensor[i];
    const sensors_slot_t *ss = &r->slot[i];
    bool fresh = fresh_slot(r, i);
    if (fresh) {
        cJSON_AddNumberToObject(root, "temperature", appconf_deg(ss->temp_c100));
        cJSON_AddBoolToObject(root, "out_of_range",
                              !appconf_in_range(sc, ss->temp_c100));
    } else {
        cJSON_AddNullToObject(root, "temperature");
        cJSON_AddNullToObject(root, "out_of_range");
    }
    cJSON_AddNumberToObject(root, "low", appconf_deg(sc->low_c100));
    cJSON_AddNumberToObject(root, "high", appconf_deg(sc->high_c100));
    if (fresh && ss->read_epoch > 0) {
        cJSON_AddNumberToObject(root, "timestamp", (double)ss->read_epoch);
    } else {
        cJSON_AddNullToObject(root, "timestamp");
    }
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static int publish_readings(const sensors_reading_t *r)
{
    int published = 0;
    for (int i = 0; i < APPCONF_SENSOR_COUNT; i++) {
        char *s = reading_json(r, i);
        if (s == NULL) {
            ctx.errors++;
            continue;
        }
        if (esp_mqtt_client_publish(ctx.client, ctx.slot[i].topic, s, 0, 1,
                                    false) >= 0) {
            published++;
        } else {
            ctx.errors++;
        }
        free(s);
    }
    if (published > 0) {
        ctx.last_pub_epoch = time_mgr_unix();
    }
    ESP_LOGI(TAG, "poll %" PRIu32 ": published %d of %d sensors", r->seq,
             published, APPCONF_SENSOR_COUNT);
    return published;
}

static void subscribe_ha_status(void)
{
    if (esp_mqtt_client_subscribe(ctx.client, HA_STATUS_TOPIC, 0) < 0) {
        ctx.errors++;
        ESP_LOGW(TAG, "subscribe %s failed", HA_STATUS_TOPIC);
        return;
    }
    ctx.subscribed = true;
    ESP_LOGI(TAG, "subscribed to %s", HA_STATUS_TOPIC);
}

/* Every publish and subscribe happens here. esp-mqtt callbacks must not
 * block, and esp_mqtt_client_* calls from its own event task deadlock it. */
static void publish_task(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (ctx.client == NULL || !ctx.connected) {
            continue;
        }
        if (!ctx.avail_published) {
            publish_availability();
        }
        if (ctx.cfg->mqtt_discovery && !ctx.subscribed) {
            subscribe_ha_status();
        }
        if (ctx.discovery_dirty) {
            /* Cleared first: a request that arrives while publishing must
             * survive to the next round. */
            ctx.discovery_dirty = false;
            if (ctx.cfg->mqtt_discovery) {
                publish_discovery();
            } else if (!ctx.cleanup_done) {
                publish_discovery_cleanup();
            }
        }

        /* Retried until both messages are accepted. mqtt_event resets
         * published_seq with discovery_dirty, so state follows configs. */
        sensors_reading_t r;
        sensors_get(&r);
        if (r.seq == 0 || r.seq == ctx.published_seq) {
            continue;
        }
        if (publish_readings(&r) == APPCONF_SENSOR_COUNT) {
            ctx.published_seq = r.seq;
        }
    }
}

static void mqtt_event(void *arg, esp_event_base_t base, int32_t id,
                       void *data)
{
    (void)arg;
    (void)base;
    esp_mqtt_event_t *ev = data;

    switch (id) {
    case MQTT_EVENT_CONNECTED:
        ctx.avail_published = false;
        ctx.discovery_dirty = true;
        ctx.published_seq = 0;
        ctx.subscribed = false;
        ctx.connected = true;
        ESP_LOGI(TAG, "connected to broker");
        break;

    case MQTT_EVENT_DISCONNECTED:
        ctx.connected = false;
        ESP_LOGW(TAG, "disconnected from broker");
        break;

    case MQTT_EVENT_DATA:
        /* Home Assistant publishes "online" here when it restarts and has
         * forgotten the discovered entities. */
        if (ev->topic_len == (int)strlen(HA_STATUS_TOPIC) &&
            strncmp(ev->topic, HA_STATUS_TOPIC, ev->topic_len) == 0 &&
            ev->data_len == (int)strlen(AVAIL_ONLINE) &&
            strncmp(ev->data, AVAIL_ONLINE, ev->data_len) == 0) {
            ESP_LOGI(TAG, "home assistant online, republishing discovery");
            ctx.discovery_dirty = true;
            ctx.published_seq = 0;
        }
        break;

    case MQTT_EVENT_ERROR:
        ctx.errors++;
        ESP_LOGW(TAG, "mqtt error (total %" PRIu32 ")", ctx.errors);
        break;

    default:
        break;
    }
}

void mqtt_mgr_start(const appconf_t *cfg, const char device_id[5])
{
    ctx.cfg = cfg;
    if (!cfg->mqtt_enabled) {
        ESP_LOGI(TAG, "mqtt disabled");
        return;
    }
    ctx.enabled = true;

    appconf_avail_topic(cfg, ctx.t_avail, sizeof ctx.t_avail);
    char dev_id[sizeof ctx.dev_id];
    snprintf(dev_id, sizeof dev_id, "hydrotemp_%s", device_id);
    strcpy(ctx.dev_id, dev_id);
    for (int i = 0; i < APPCONF_SENSOR_COUNT; i++) {
        slot_topics_t *st = &ctx.slot[i];
        appconf_sensor_topic(cfg, i, st->topic, sizeof st->topic);
        snprintf(st->id_temp, sizeof st->id_temp, "%s_%c_temperature", dev_id,
                 appconf_slot_letter(i));
        snprintf(st->id_range, sizeof st->id_range, "%s_%c_out_of_range", dev_id,
                 appconf_slot_letter(i));
    }

    esp_mqtt_client_config_t mc = { 0 };
    char uri[APPCONF_HOST_MAX + 32];
    bool ipv6 = strchr(cfg->mqtt_host, ':') != NULL;
    snprintf(uri, sizeof uri, "%s://%s%s%s:%u", cfg->mqtt_tls ? "mqtts" : "mqtt",
             ipv6 ? "[" : "", cfg->mqtt_host, ipv6 ? "]" : "",
             (unsigned)cfg->mqtt_port);
    mc.broker.address.uri = uri;
    if (cfg->mqtt_username[0] != '\0') {
        mc.credentials.username = cfg->mqtt_username;
    }
    if (cfg->mqtt_password[0] != '\0') {
        mc.credentials.authentication.password = cfg->mqtt_password;
    }

    if (cfg->mqtt_tls) {
        if (cfg->mqtt_skip_verify) {
            ESP_LOGW(TAG, "TLS certificate verification is disabled");
        } else if (cfg->mqtt_ca_cert[0] != '\0') {
            mc.broker.verification.certificate = cfg->mqtt_ca_cert;
        } else {
            mc.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
        }
    }

    mc.session.last_will.topic = ctx.t_avail;
    mc.session.last_will.msg = AVAIL_OFFLINE;
    mc.session.last_will.qos = 1;
    mc.session.last_will.retain = true;
    mc.session.keepalive = 30;
    mc.network.reconnect_timeout_ms = 5000;
    mc.network.timeout_ms = 10000;

    ctx.client = esp_mqtt_client_init(&mc);
    if (ctx.client == NULL) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        ctx.enabled = false;
        return;
    }
    esp_mqtt_client_register_event(ctx.client, ESP_EVENT_ANY_ID, mqtt_event,
                                   NULL);
    esp_err_t err = esp_mqtt_client_start(ctx.client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_start: %s", esp_err_to_name(err));
        ctx.enabled = false;
        return;
    }

    BaseType_t ok = xTaskCreate(publish_task, "mqtt_pub", 6144, NULL, 4, NULL);
    configASSERT(ok == pdPASS);
    ESP_LOGI(TAG, "mqtt started: %s, prefix %s, discovery %s", uri,
             cfg->mqtt_prefix, cfg->mqtt_discovery ? "on" : "off");
}

void mqtt_mgr_get_status(mqtt_mgr_status_t *out)
{
    out->enabled = ctx.enabled;
    out->connected = ctx.connected;
    out->last_publish = ctx.last_pub_epoch;
    out->errors = ctx.errors;
}
