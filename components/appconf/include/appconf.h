/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APPCONF_NAME_MAX        32
#define APPCONF_SSID_MAX        32
#define APPCONF_PASS_MAX        64
#define APPCONF_HOST_MAX        64
#define APPCONF_USER_MAX        32
#define APPCONF_PREFIX_MAX      64
#define APPCONF_CERT_MAX        3072
#define APPCONF_SENSOR_NAME_MAX 24
#define APPCONF_TOPIC_MAX       (APPCONF_PREFIX_MAX + 1 + APPCONF_SENSOR_NAME_MAX)

#define APPCONF_SENSOR_COUNT 2

#define APPCONF_NTP_DEFAULT      "pool.ntp.org"
#define APPCONF_MQTT_PORT_PLAIN  1883
#define APPCONF_POLL_DEFAULT_S   300
#define APPCONF_POLL_MIN_S       10
#define APPCONF_POLL_MAX_S       3600

/* Temperatures are kept in hundredths of a degree. */
#define APPCONF_TEMP_MIN_C100     (-5500)
#define APPCONF_TEMP_MAX_C100     12500
#define APPCONF_LOW_DEFAULT_C100  1800
#define APPCONF_HIGH_DEFAULT_C100 2300

typedef struct {
    char     name[APPCONF_SENSOR_NAME_MAX + 1];
    int16_t  low_c100;
    int16_t  high_c100;
    /* 1-Wire ROM code bound to this slot, 0 when none yet. Assigned by the
     * sensor module on first sight, never edited over the API. */
    uint64_t rom;
} appconf_sensor_t;

typedef struct {
    char     device_name[APPCONF_NAME_MAX + 1];
    char     wifi_ssid[APPCONF_SSID_MAX + 1];
    char     wifi_password[APPCONF_PASS_MAX + 1];
    char     ntp_server[APPCONF_HOST_MAX + 1];

    appconf_sensor_t sensor[APPCONF_SENSOR_COUNT];
    uint16_t poll_interval_s;

    bool     mqtt_enabled;
    char     mqtt_host[APPCONF_HOST_MAX + 1];
    uint16_t mqtt_port;
    char     mqtt_username[APPCONF_USER_MAX + 1];
    char     mqtt_password[APPCONF_PASS_MAX + 1];
    bool     mqtt_tls;
    char     mqtt_ca_cert[APPCONF_CERT_MAX + 1];
    bool     mqtt_skip_verify;
    char     mqtt_prefix[APPCONF_PREFIX_MAX + 1];
    bool     mqtt_discovery;

    char     hostname[APPCONF_HOST_MAX + 1];
} appconf_t;

void appconf_device_id(const uint8_t mac[6], char out[5]);

void appconf_default_name(const char device_id[5], char *out, size_t out_sz);

void appconf_defaults(appconf_t *c, const char device_id[5]);

void appconf_hostname(const char *device_name, const char device_id[5],
                      char *out, size_t out_sz);

void appconf_derive(appconf_t *c, const char device_id[5]);

bool appconf_validate(const appconf_t *c, char *err, size_t err_sz);

/* Keeps every field group of *c that validates on top of the defaults and
 * resets the rest, so a stored config that no longer passes (older
 * firmware, corrupt entry) cannot outlive an update. Sensor ROMs are kept
 * as is. Returns how many groups were reset. */
int appconf_sanitize(appconf_t *c, const char device_id[5]);

/* Topic segment for a sensor name: lower case, spaces become "_". */
void appconf_sensor_slug(const char *name, char *out, size_t out_sz);

void appconf_sensor_topic(const appconf_t *c, int slot, char *out,
                          size_t out_sz);

/* "<prefix>/availability"; the segment is reserved, no sensor may slug
 * to it. */
void appconf_avail_topic(const appconf_t *c, char *out, size_t out_sz);

/* 'a' for slot 0, 'b' for slot 1: the API's "slot" and the HA ids. */
char appconf_slot_letter(int slot);

/* A DS18B20 reading worth keeping. Exactly 0.0 and 85.0 are rejected: a
 * data line shorted low reads as all-zero scratchpad bytes with a valid
 * CRC, and 85.0 is the power-on default before any conversion. */
bool appconf_reading_ok(int16_t c100);

/* The one letter shown on the display for a sensor: its first letter or
 * digit in upper case, "?" when the name has none. */
char appconf_sensor_letter(const char *name);

/* Judged on the value as displayed: rounded to a tenth first. */
bool appconf_in_range(const appconf_sensor_t *s, int16_t temp_c100);

int16_t appconf_round_c10(int16_t c100);

/* Slot for a probe that is bound nowhere: an empty slot first, then a
 * slot whose probe is not seen; -1 when every slot's probe is present. */
int appconf_free_slot(const appconf_sensor_t s[APPCONF_SENSOR_COUNT],
                      const bool seen[APPCONF_SENSOR_COUNT]);

static inline double appconf_deg(int16_t c100)
{
    return c100 / 100.0;
}

/* "22.50" (two decimals) or "22.5" (one, rounded half away from zero). */
void appconf_format_c100(int16_t c100, int decimals, char *out, size_t out_sz);

/* "28216531054AC79B": family byte first, as printed on the sensor. */
void appconf_rom_str(uint64_t rom, char out[17]);

#ifdef __cplusplus
}
#endif
