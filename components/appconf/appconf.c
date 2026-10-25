/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "appconf.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool str_fits(const char *s, size_t cap)
{
    return strnlen(s, cap + 1) <= cap;
}

static bool has_ctrl(const char *s)
{
    for (const char *p = s; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c == 0x7f) {
            return true;
        }
    }
    return false;
}

static bool is_printable_ascii(const char *s)
{
    for (const char *p = s; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c > 0x7e) {
            return false;
        }
    }
    return true;
}

static bool host_chars_ok(const char *s)
{
    for (const char *p = s; *p != '\0'; p++) {
        char c = *p;
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '.' || c == '-' || c == ':';
        if (!ok) {
            return false;
        }
    }
    return true;
}

static bool topic_ok(const char *s)
{
    if (s[0] == '\0') {
        return false;
    }
    for (const char *p = s; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x21 || c > 0x7e || c == '+' || c == '#') {
            return false;
        }
    }
    return true;
}

static bool sensor_name_ok(const char *s)
{
    if (s[0] == '\0' || s[0] == ' ' || s[strlen(s) - 1] == ' ') {
        return false;
    }
    for (const char *p = s; *p != '\0'; p++) {
        char c = *p;
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

void appconf_device_id(const uint8_t mac[6], char out[5])
{
    snprintf(out, 5, "%02x%02x", mac[4], mac[5]);
}

void appconf_hostname(const char *device_name, const char device_id[5],
                      char *out, size_t out_sz)
{
    if (out_sz == 0) {
        return;
    }
    size_t o = 0;
    bool pending_dash = false;
    for (const char *p = device_name; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        char keep = 0;
        if (c >= 'A' && c <= 'Z') {
            keep = (char)(c - 'A' + 'a');
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            keep = (char)c;
        }
        if (keep != 0) {
            if (pending_dash && o > 0 && o + 2 <= out_sz) {
                out[o++] = '-';
            }
            if (o + 2 <= out_sz) {
                out[o++] = keep;
            }
            pending_dash = false;
        } else {
            pending_dash = true;
        }
    }
    while (o > 0 && out[o - 1] == '-') {
        o--;
    }
    out[o] = '\0';
    if (o == 0) {
        snprintf(out, out_sz, "hydrotemp-%s", device_id);
    }
}

void appconf_default_name(const char device_id[5], char *out, size_t out_sz)
{
    char id_upper[5] = { 0 };
    for (int i = 0; i < 4; i++) {
        id_upper[i] = (char)toupper((unsigned char)device_id[i]);
    }
    snprintf(out, out_sz, "HydroTemp-%s", id_upper);
}

void appconf_defaults(appconf_t *c, const char device_id[5])
{
    static const char *const names[APPCONF_SENSOR_COUNT] = { "left", "right" };

    memset(c, 0, sizeof *c);
    appconf_default_name(device_id, c->device_name, sizeof c->device_name);
    snprintf(c->ntp_server, sizeof c->ntp_server, "%s", APPCONF_NTP_DEFAULT);
    for (int i = 0; i < APPCONF_SENSOR_COUNT; i++) {
        snprintf(c->sensor[i].name, sizeof c->sensor[i].name, "%s", names[i]);
        c->sensor[i].low_c100 = APPCONF_LOW_DEFAULT_C100;
        c->sensor[i].high_c100 = APPCONF_HIGH_DEFAULT_C100;
    }
    c->poll_interval_s = APPCONF_POLL_DEFAULT_S;
    c->mqtt_port = APPCONF_MQTT_PORT_PLAIN;
    snprintf(c->mqtt_prefix, sizeof c->mqtt_prefix, "esp32-hydro-temp/%s", device_id);
    c->mqtt_discovery = true;
    appconf_derive(c, device_id);
}

void appconf_derive(appconf_t *c, const char device_id[5])
{
    appconf_hostname(c->device_name, device_id, c->hostname, sizeof c->hostname);
}

void appconf_sensor_slug(const char *name, char *out, size_t out_sz)
{
    if (out_sz == 0) {
        return;
    }
    size_t o = 0;
    for (const char *p = name; *p != '\0' && o + 1 < out_sz; p++) {
        char c = *p;
        if (c == ' ') {
            c = '_';
        } else if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        out[o++] = c;
    }
    out[o] = '\0';
}

void appconf_sensor_topic(const appconf_t *c, int slot, char *out,
                          size_t out_sz)
{
    char slug[APPCONF_SENSOR_NAME_MAX + 1];
    appconf_sensor_slug(c->sensor[slot].name, slug, sizeof slug);
    snprintf(out, out_sz, "%s/%s", c->mqtt_prefix, slug);
}

#define AVAIL_SEGMENT "availability"

void appconf_avail_topic(const appconf_t *c, char *out, size_t out_sz)
{
    snprintf(out, out_sz, "%s/%s", c->mqtt_prefix, AVAIL_SEGMENT);
}

char appconf_slot_letter(int slot)
{
    return (char)('a' + slot);
}

bool appconf_reading_ok(int16_t c100)
{
    return c100 >= APPCONF_TEMP_MIN_C100 && c100 <= APPCONF_TEMP_MAX_C100 &&
           c100 != 0 && c100 != 8500;
}

char appconf_sensor_letter(const char *name)
{
    for (const char *p = name; *p != '\0'; p++) {
        if (isalnum((unsigned char)*p)) {
            return (char)toupper((unsigned char)*p);
        }
    }
    return '?';
}

int16_t appconf_round_c10(int16_t c100)
{
    /* half away from zero */
    int v = c100 >= 0 ? (c100 + 5) / 10 : -((-c100 + 5) / 10);
    return (int16_t)(v * 10);
}

bool appconf_in_range(const appconf_sensor_t *s, int16_t temp_c100)
{
    int16_t t = appconf_round_c10(temp_c100);
    return t >= s->low_c100 && t <= s->high_c100;
}

int appconf_free_slot(const appconf_sensor_t s[APPCONF_SENSOR_COUNT],
                      const bool seen[APPCONF_SENSOR_COUNT])
{
    int k = -1;
    for (int j = 0; j < APPCONF_SENSOR_COUNT; j++) {
        if (seen[j]) {
            continue;
        }
        if (k < 0 || (s[j].rom == 0 && s[k].rom != 0)) {
            k = j;
        }
    }
    return k;
}

void appconf_format_c100(int16_t c100, int decimals, char *out, size_t out_sz)
{
    int v = c100;
    if (decimals == 1) {
        v = appconf_round_c10(v) / 10;
        snprintf(out, out_sz, "%s%d.%d", v < 0 ? "-" : "", abs(v) / 10, abs(v) % 10);
    } else {
        snprintf(out, out_sz, "%s%d.%02d", v < 0 ? "-" : "", abs(v) / 100, abs(v) % 100);
    }
}

void appconf_rom_str(uint64_t rom, char out[17])
{
    for (int i = 0; i < 8; i++) {
        snprintf(out + 2 * i, 3, "%02X", (unsigned)((rom >> (8 * i)) & 0xFF));
    }
}

bool appconf_validate(const appconf_t *c, char *err, size_t err_sz)
{
#define BAD(...)                       \
    do {                               \
        snprintf(err, err_sz, __VA_ARGS__); \
        return false;                  \
    } while (0)

    if (!str_fits(c->device_name, APPCONF_NAME_MAX)) {
        BAD("Device name is too long (max %d).", APPCONF_NAME_MAX);
    }
    if (c->device_name[0] == '\0') {
        BAD("Device name is empty.");
    }
    if (has_ctrl(c->device_name)) {
        BAD("Device name has control characters.");
    }

    if (!str_fits(c->wifi_ssid, APPCONF_SSID_MAX)) {
        BAD("Wi-Fi SSID is too long (max %d).", APPCONF_SSID_MAX);
    }
    if (has_ctrl(c->wifi_ssid)) {
        BAD("Wi-Fi SSID has control characters.");
    }
    if (!str_fits(c->wifi_password, APPCONF_PASS_MAX)) {
        BAD("Wi-Fi password is too long (max %d).", APPCONF_PASS_MAX);
    }
    if (!is_printable_ascii(c->wifi_password)) {
        BAD("Wi-Fi password must be printable ASCII.");
    }
    if (c->wifi_ssid[0] == '\0' && c->wifi_password[0] != '\0') {
        BAD("Wi-Fi password without an SSID.");
    }

    size_t pass_len = strlen(c->wifi_password);
    if (c->wifi_ssid[0] != '\0' && pass_len > 0 && pass_len < 8) {
        BAD("Wi-Fi password must be at least 8 characters.");
    }

    if (!str_fits(c->ntp_server, APPCONF_HOST_MAX)) {
        BAD("NTP server is too long (max %d).", APPCONF_HOST_MAX);
    }
    if (c->ntp_server[0] == '\0') {
        BAD("NTP server is empty.");
    }
    if (!host_chars_ok(c->ntp_server)) {
        BAD("NTP server has invalid characters.");
    }

    char slugs[APPCONF_SENSOR_COUNT][APPCONF_SENSOR_NAME_MAX + 1];
    for (int i = 0; i < APPCONF_SENSOR_COUNT; i++) {
        const appconf_sensor_t *s = &c->sensor[i];
        if (!str_fits(s->name, APPCONF_SENSOR_NAME_MAX)) {
            BAD("Sensor %d name is too long (max %d).", i + 1,
                APPCONF_SENSOR_NAME_MAX);
        }
        if (!sensor_name_ok(s->name)) {
            BAD("Sensor %d name: letters, digits, space, - and _ only, "
                "no leading or trailing space.", i + 1);
        }
        if (s->low_c100 < APPCONF_TEMP_MIN_C100 ||
            s->high_c100 > APPCONF_TEMP_MAX_C100) {
            BAD("Sensor %d range must stay within %d..%d C.", i + 1,
                APPCONF_TEMP_MIN_C100 / 100, APPCONF_TEMP_MAX_C100 / 100);
        }
        if (s->low_c100 >= s->high_c100) {
            BAD("Sensor %d: low must be below high.", i + 1);
        }
        appconf_sensor_slug(s->name, slugs[i], sizeof slugs[i]);
        if (strcmp(slugs[i], AVAIL_SEGMENT) == 0) {
            BAD("Sensor %d name is reserved.", i + 1);
        }
        for (int j = 0; j < i; j++) {
            if (strcmp(slugs[i], slugs[j]) == 0) {
                BAD("Sensor names must differ.");
            }
        }
    }

    if (c->poll_interval_s < APPCONF_POLL_MIN_S ||
        c->poll_interval_s > APPCONF_POLL_MAX_S) {
        BAD("Poll interval must be %d-%d s.", APPCONF_POLL_MIN_S,
            APPCONF_POLL_MAX_S);
    }

    if (c->mqtt_enabled) {
        if (!str_fits(c->mqtt_host, APPCONF_HOST_MAX)) {
            BAD("MQTT host is too long (max %d).", APPCONF_HOST_MAX);
        }
        if (c->mqtt_host[0] == '\0') {
            BAD("MQTT host is empty.");
        }
        if (!host_chars_ok(c->mqtt_host)) {
            BAD("MQTT host has invalid characters.");
        }
        if (c->mqtt_port == 0) {
            BAD("MQTT port must be 1-65535.");
        }
        if (!str_fits(c->mqtt_username, APPCONF_USER_MAX)) {
            BAD("MQTT user is too long (max %d).", APPCONF_USER_MAX);
        }
        if (!is_printable_ascii(c->mqtt_username)) {
            BAD("MQTT user must be printable ASCII.");
        }
        if (!str_fits(c->mqtt_password, APPCONF_PASS_MAX)) {
            BAD("MQTT password is too long (max %d).", APPCONF_PASS_MAX);
        }
        if (!is_printable_ascii(c->mqtt_password)) {
            BAD("MQTT password must be printable ASCII.");
        }
    }

    if (!str_fits(c->mqtt_prefix, APPCONF_PREFIX_MAX) ||
        !topic_ok(c->mqtt_prefix) ||
        c->mqtt_prefix[strlen(c->mqtt_prefix) - 1] == '/') {
        BAD("MQTT topic prefix is empty or invalid (max %d, no spaces, "
            "+ # or trailing /).", APPCONF_PREFIX_MAX);
    }

    if (!str_fits(c->mqtt_ca_cert, APPCONF_CERT_MAX)) {
        BAD("CA certificate is too long (max %d).", APPCONF_CERT_MAX);
    }
    if (c->mqtt_ca_cert[0] != '\0' &&
        strstr(c->mqtt_ca_cert, "-----BEGIN") == NULL) {
        BAD("CA certificate does not look like PEM.");
    }

    return true;
#undef BAD
}

static void copy_device_name(appconf_t *dst, const appconf_t *src)
{
    memcpy(dst->device_name, src->device_name, sizeof dst->device_name);
}

static void copy_wifi(appconf_t *dst, const appconf_t *src)
{
    memcpy(dst->wifi_ssid, src->wifi_ssid, sizeof dst->wifi_ssid);
    memcpy(dst->wifi_password, src->wifi_password, sizeof dst->wifi_password);
}

static void copy_ntp(appconf_t *dst, const appconf_t *src)
{
    memcpy(dst->ntp_server, src->ntp_server, sizeof dst->ntp_server);
}

static void copy_poll(appconf_t *dst, const appconf_t *src)
{
    dst->poll_interval_s = src->poll_interval_s;
}

static void copy_sensor_0(appconf_t *dst, const appconf_t *src)
{
    dst->sensor[0] = src->sensor[0];
}

static void copy_sensor_1(appconf_t *dst, const appconf_t *src)
{
    dst->sensor[1] = src->sensor[1];
}

static void copy_mqtt_link(appconf_t *dst, const appconf_t *src)
{
    dst->mqtt_enabled = src->mqtt_enabled;
    memcpy(dst->mqtt_host, src->mqtt_host, sizeof dst->mqtt_host);
    dst->mqtt_port = src->mqtt_port;
    memcpy(dst->mqtt_username, src->mqtt_username, sizeof dst->mqtt_username);
    memcpy(dst->mqtt_password, src->mqtt_password, sizeof dst->mqtt_password);
    dst->mqtt_tls = src->mqtt_tls;
    dst->mqtt_skip_verify = src->mqtt_skip_verify;
    dst->mqtt_discovery = src->mqtt_discovery;
}

static void copy_mqtt_prefix(appconf_t *dst, const appconf_t *src)
{
    memcpy(dst->mqtt_prefix, src->mqtt_prefix, sizeof dst->mqtt_prefix);
}

static void copy_mqtt_ca(appconf_t *dst, const appconf_t *src)
{
    memcpy(dst->mqtt_ca_cert, src->mqtt_ca_cert, sizeof dst->mqtt_ca_cert);
}

_Static_assert(APPCONF_SENSOR_COUNT == 2, "one copy_sensor_N per slot");

int appconf_sanitize(appconf_t *c, const char device_id[5])
{
    static void (*const groups[])(appconf_t *, const appconf_t *) = {
        copy_device_name, copy_wifi, copy_ntp, copy_poll, copy_sensor_0,
        copy_sensor_1, copy_mqtt_link, copy_mqtt_prefix, copy_mqtt_ca,
    };
    char err[128];
    if (appconf_validate(c, err, sizeof err)) {
        return 0;
    }

    /* The defaults validate, and a group is only taken when the result
     * still does, so the outcome is valid whatever was stored. */
    static appconf_t good, next;   /* too big for the stack; boot only */
    appconf_defaults(&good, device_id);
    for (int i = 0; i < APPCONF_SENSOR_COUNT; i++) {
        good.sensor[i].rom = c->sensor[i].rom;
    }
    int reset = 0;
    for (size_t g = 0; g < sizeof groups / sizeof groups[0]; g++) {
        next = good;
        groups[g](&next, c);
        if (appconf_validate(&next, err, sizeof err)) {
            good = next;
        } else {
            reset++;
        }
    }
    *c = good;
    appconf_derive(c, device_id);
    return reset;
}
