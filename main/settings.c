/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "history.h"

static const char *TAG = "settings";

#define CFG_NS "cfg"

#define KEY_NAME    "name"
#define KEY_WSSID   "wssid"
#define KEY_WPASS   "wpass"
#define KEY_NTP     "ntp"
#define KEY_POLL    "poll"
#define KEY_MQ_EN   "mq_en"
#define KEY_MQ_HOST "mq_host"
#define KEY_MQ_PORT "mq_port"
#define KEY_MQ_USER "mq_user"
#define KEY_MQ_PASS "mq_pass"
#define KEY_MQ_TLS  "mq_tls"
#define KEY_MQ_CA   "mq_ca"
#define KEY_MQ_SKIP "mq_skip"
#define KEY_MQ_PFX  "mq_pfx"
#define KEY_MQ_DISC "mq_disc"

/* Per-slot keys: s<i>_name, s<i>_low, s<i>_high, s<i>_rom. */
static const char *slot_key(int i, const char *field, char *buf, size_t sz)
{
    snprintf(buf, sz, "s%d_%s", i, field);
    return buf;
}

static void erase_partition_by_name(const char *name)
{
    const esp_partition_t *p = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, name);
    if (p == NULL) {
        ESP_LOGE(TAG, "partition \"%s\" not found", name);
        return;
    }
    ESP_LOGW(TAG, "erasing partition \"%s\"", name);
    esp_err_t err = esp_partition_erase_range(p, 0, p->size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "erase \"%s\": %s", name, esp_err_to_name(err));
    }
}

void settings_nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "nvs: %s, erasing once", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* History is not worth a boot loop: on any other failure it stays
     * disabled (history_start() cannot open the partition and says so). */
    err = nvs_flash_init_partition(HISTORY_PART_NAME);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        erase_partition_by_name(HISTORY_PART_NAME);
        err = nvs_flash_init_partition(HISTORY_PART_NAME);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "history partition: %s", esp_err_to_name(err));
    }
}

/* nvs_get_str() fills the buffer before it checks the data CRC, so a
 * corrupt entry read straight into *out would wipe the default. */
static void get_str(nvs_handle_t h, const char *key, char *out, size_t out_sz)
{
    char *tmp = malloc(out_sz);
    if (tmp == NULL) {
        ESP_LOGW(TAG, "key %s: no memory, using default", key);
        return;
    }
    size_t sz = out_sz;
    esp_err_t err = nvs_get_str(h, key, tmp, &sz);
    if (err == ESP_OK && sz > 0) {
        memcpy(out, tmp, sz);
        out[sz - 1] = '\0';
    } else if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "key %s: %s, using default", key, esp_err_to_name(err));
    }
    free(tmp);
}

static void get_bool(nvs_handle_t h, const char *key, bool *out)
{
    uint8_t u8;
    if (nvs_get_u8(h, key, &u8) == ESP_OK) {
        *out = u8 != 0;
    }
}

static void get_u16(nvs_handle_t h, const char *key, uint16_t *out)
{
    uint16_t u16;
    if (nvs_get_u16(h, key, &u16) == ESP_OK) {
        *out = u16;
    }
}

static void get_i16(nvs_handle_t h, const char *key, int16_t *out)
{
    int16_t i16;
    if (nvs_get_i16(h, key, &i16) == ESP_OK) {
        *out = i16;
    }
}

static void get_u64(nvs_handle_t h, const char *key, uint64_t *out)
{
    uint64_t u64;
    if (nvs_get_u64(h, key, &u64) == ESP_OK) {
        *out = u64;
    }
}

void settings_load(appconf_t *c, const char device_id[5])
{
    appconf_defaults(c, device_id);

    nvs_handle_t h;
    esp_err_t err = nvs_open(CFG_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "no saved settings, using factory defaults");
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s, using factory defaults", esp_err_to_name(err));
        return;
    }

    char k[16];
    get_str(h, KEY_NAME, c->device_name, sizeof c->device_name);
    get_str(h, KEY_WSSID, c->wifi_ssid, sizeof c->wifi_ssid);
    get_str(h, KEY_WPASS, c->wifi_password, sizeof c->wifi_password);
    get_str(h, KEY_NTP, c->ntp_server, sizeof c->ntp_server);
    get_u16(h, KEY_POLL, &c->poll_interval_s);
    for (int i = 0; i < APPCONF_SENSOR_COUNT; i++) {
        appconf_sensor_t *s = &c->sensor[i];
        get_str(h, slot_key(i, "name", k, sizeof k), s->name, sizeof s->name);
        get_i16(h, slot_key(i, "low", k, sizeof k), &s->low_c100);
        get_i16(h, slot_key(i, "high", k, sizeof k), &s->high_c100);
        get_u64(h, slot_key(i, "rom", k, sizeof k), &s->rom);
    }
    get_bool(h, KEY_MQ_EN, &c->mqtt_enabled);
    get_str(h, KEY_MQ_HOST, c->mqtt_host, sizeof c->mqtt_host);
    get_u16(h, KEY_MQ_PORT, &c->mqtt_port);
    get_str(h, KEY_MQ_USER, c->mqtt_username, sizeof c->mqtt_username);
    get_str(h, KEY_MQ_PASS, c->mqtt_password, sizeof c->mqtt_password);
    get_bool(h, KEY_MQ_TLS, &c->mqtt_tls);
    get_str(h, KEY_MQ_CA, c->mqtt_ca_cert, sizeof c->mqtt_ca_cert);
    get_bool(h, KEY_MQ_SKIP, &c->mqtt_skip_verify);
    get_str(h, KEY_MQ_PFX, c->mqtt_prefix, sizeof c->mqtt_prefix);
    get_bool(h, KEY_MQ_DISC, &c->mqtt_discovery);
    nvs_close(h);

    appconf_derive(c, device_id);
    int reset = appconf_sanitize(c, device_id);
    if (reset > 0) {
        ESP_LOGW(TAG, "stored settings invalid, %d group(s) reset to defaults",
                 reset);
    }
}

esp_err_t settings_save(const appconf_t *c)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CFG_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(err));
        return err;
    }

#define PUT(call)                  \
    do {                           \
        if (err == ESP_OK) {       \
            err = (call);          \
        }                          \
    } while (0)

    char k[16];
    PUT(nvs_set_str(h, KEY_NAME, c->device_name));
    PUT(nvs_set_str(h, KEY_WSSID, c->wifi_ssid));
    PUT(nvs_set_str(h, KEY_WPASS, c->wifi_password));
    PUT(nvs_set_str(h, KEY_NTP, c->ntp_server));
    PUT(nvs_set_u16(h, KEY_POLL, c->poll_interval_s));
    for (int i = 0; i < APPCONF_SENSOR_COUNT; i++) {
        const appconf_sensor_t *s = &c->sensor[i];
        PUT(nvs_set_str(h, slot_key(i, "name", k, sizeof k), s->name));
        PUT(nvs_set_i16(h, slot_key(i, "low", k, sizeof k), s->low_c100));
        PUT(nvs_set_i16(h, slot_key(i, "high", k, sizeof k), s->high_c100));
    }
    PUT(nvs_set_u8(h, KEY_MQ_EN, c->mqtt_enabled));
    PUT(nvs_set_str(h, KEY_MQ_HOST, c->mqtt_host));
    PUT(nvs_set_u16(h, KEY_MQ_PORT, c->mqtt_port));
    PUT(nvs_set_str(h, KEY_MQ_USER, c->mqtt_username));
    PUT(nvs_set_str(h, KEY_MQ_PASS, c->mqtt_password));
    PUT(nvs_set_u8(h, KEY_MQ_TLS, c->mqtt_tls));
    PUT(nvs_set_str(h, KEY_MQ_CA, c->mqtt_ca_cert));
    PUT(nvs_set_u8(h, KEY_MQ_SKIP, c->mqtt_skip_verify));
    PUT(nvs_set_str(h, KEY_MQ_PFX, c->mqtt_prefix));
    PUT(nvs_set_u8(h, KEY_MQ_DISC, c->mqtt_discovery));
    PUT(nvs_commit(h));
#undef PUT

    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t settings_save_rom(int slot, uint64_t rom)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CFG_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(err));
        return err;
    }
    char k[16];
    err = nvs_set_u64(h, slot_key(slot, "rom", k, sizeof k), rom);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save rom %d failed: %s", slot, esp_err_to_name(err));
    }
    return err;
}
