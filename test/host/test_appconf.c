/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "appconf.h"

#include <string.h>

#include "tinytest.h"

static const char ID[] = "1a2b";

static void test_device_id(void)
{
    const uint8_t mac1[6] = { 0x7c, 0x4f, 0xad, 0xbb, 0x1a, 0x2b };
    char id[5];
    appconf_device_id(mac1, id);
    CHECK_STR(id, "1a2b");

    const uint8_t mac2[6] = { 0, 0, 0, 0, 0xff, 0x0a };
    appconf_device_id(mac2, id);
    CHECK_STR(id, "ff0a");
}

static void test_hostname(void)
{
    char h[APPCONF_HOST_MAX + 1];

    appconf_hostname("HydroTemp-1A2B", ID, h, sizeof h);
    CHECK_STR(h, "hydrotemp-1a2b");

    appconf_hostname("  --foo--bar--  ", ID, h, sizeof h);
    CHECK_STR(h, "foo-bar");

    appconf_hostname("a_b!", ID, h, sizeof h);
    CHECK_STR(h, "a-b");

    appconf_hostname("МойДатчик", ID, h, sizeof h);
    CHECK_STR(h, "hydrotemp-1a2b");

    appconf_hostname("", ID, h, sizeof h);
    CHECK_STR(h, "hydrotemp-1a2b");
}

static void test_defaults(void)
{
    appconf_t c;
    appconf_defaults(&c, ID);

    CHECK_STR(c.device_name, "HydroTemp-1A2B");
    CHECK_STR(c.hostname, "hydrotemp-1a2b");
    CHECK_STR(c.ntp_server, "pool.ntp.org");
    CHECK(c.wifi_ssid[0] == '\0');
    CHECK(c.mqtt_enabled == false);
    CHECK(c.mqtt_discovery == true);
    CHECK(c.mqtt_port == APPCONF_MQTT_PORT_PLAIN);
    CHECK_STR(c.mqtt_prefix, "esp32-hydro-temp/1a2b");
    CHECK(c.poll_interval_s == 300);
    CHECK_STR(c.sensor[0].name, "left");
    CHECK_STR(c.sensor[1].name, "right");
    CHECK(c.sensor[0].low_c100 == 1800 && c.sensor[0].high_c100 == 2300);
    CHECK(c.sensor[1].low_c100 == 1800 && c.sensor[1].high_c100 == 2300);
    CHECK(c.sensor[0].rom == 0 && c.sensor[1].rom == 0);

    char err[128];
    CHECK(appconf_validate(&c, err, sizeof err));
}

static void test_sensor_helpers(void)
{
    appconf_t c;
    appconf_defaults(&c, ID);

    char topic[APPCONF_TOPIC_MAX + 1];
    appconf_sensor_topic(&c, 0, topic, sizeof topic);
    CHECK_STR(topic, "esp32-hydro-temp/1a2b/left");
    appconf_avail_topic(&c, topic, sizeof topic);
    CHECK_STR(topic, "esp32-hydro-temp/1a2b/availability");
    CHECK(appconf_slot_letter(0) == 'a' && appconf_slot_letter(1) == 'b');

    CHECK(appconf_reading_ok(2137));
    CHECK(appconf_reading_ok(-5500) && appconf_reading_ok(12500));
    CHECK(!appconf_reading_ok(-5501) && !appconf_reading_ok(12501));
    CHECK(!appconf_reading_ok(0) && !appconf_reading_ok(8500));
    CHECK(appconf_reading_ok(6) && appconf_reading_ok(8506));

    strcpy(c.sensor[1].name, "Sweet Potato");
    appconf_sensor_topic(&c, 1, topic, sizeof topic);
    CHECK_STR(topic, "esp32-hydro-temp/1a2b/sweet_potato");

    CHECK(appconf_sensor_letter("mint") == 'M');
    CHECK(appconf_sensor_letter("Potato") == 'P');
    CHECK(appconf_sensor_letter("_2nd") == '2');
    CHECK(appconf_sensor_letter("") == '?');
    CHECK(appconf_sensor_letter("__") == '?');

    char buf[16];
    appconf_format_c100(2250, 1, buf, sizeof buf);
    CHECK_STR(buf, "22.5");
    appconf_format_c100(2249, 1, buf, sizeof buf);
    CHECK_STR(buf, "22.5");
    appconf_format_c100(2244, 1, buf, sizeof buf);
    CHECK_STR(buf, "22.4");
    appconf_format_c100(-5, 1, buf, sizeof buf);
    CHECK_STR(buf, "-0.1");
    appconf_format_c100(-4, 1, buf, sizeof buf);
    CHECK_STR(buf, "0.0");
    appconf_format_c100(-1250, 1, buf, sizeof buf);
    CHECK_STR(buf, "-12.5");
    appconf_format_c100(2137, 2, buf, sizeof buf);
    CHECK_STR(buf, "21.37");
    appconf_format_c100(-7, 2, buf, sizeof buf);
    CHECK_STR(buf, "-0.07");
    CHECK(appconf_deg(1850) == 18.5);

    char rom[17];
    appconf_rom_str(0x9BC74A0531652128ull, rom);
    CHECK_STR(rom, "28216531054AC79B");
    appconf_rom_str(0, rom);
    CHECK_STR(rom, "0000000000000000");

    appconf_sensor_t s = { .low_c100 = 1800, .high_c100 = 2300 };
    CHECK(appconf_in_range(&s, 1800));
    CHECK(appconf_in_range(&s, 2300));
    CHECK(appconf_in_range(&s, 2050));
    CHECK(!appconf_in_range(&s, 1794));
    CHECK(appconf_in_range(&s, 1795));   /* shows as 18.0 */
    CHECK(appconf_in_range(&s, 2304));   /* shows as 23.0 */
    CHECK(!appconf_in_range(&s, 2305));
    CHECK(appconf_round_c10(2304) == 2300 && appconf_round_c10(2305) == 2310);
    CHECK(appconf_round_c10(-1795) == -1800 && appconf_round_c10(-1794) == -1790);

    appconf_sensor_t slots[APPCONF_SENSOR_COUNT] = { { .rom = 0 }, { .rom = 0 } };
    bool seen[APPCONF_SENSOR_COUNT] = { false, false };
    CHECK(appconf_free_slot(slots, seen) == 0);
    seen[0] = true;
    CHECK(appconf_free_slot(slots, seen) == 1);
    slots[0].rom = 1; slots[1].rom = 0; seen[0] = false; seen[1] = false;
    CHECK(appconf_free_slot(slots, seen) == 1);      /* empty before missing */
    slots[1].rom = 2;
    CHECK(appconf_free_slot(slots, seen) == 0);      /* both missing: first */
    seen[0] = true; seen[1] = true;
    CHECK(appconf_free_slot(slots, seen) == -1);
}

static void test_validate_sensors(void)
{
    appconf_t c;
    char err[128];

    appconf_defaults(&c, ID);
    c.sensor[0].name[0] = '\0';
    CHECK(!appconf_validate(&c, err, sizeof err));

    appconf_defaults(&c, ID);
    strcpy(c.sensor[0].name, "mint/tea");
    CHECK(!appconf_validate(&c, err, sizeof err));

    appconf_defaults(&c, ID);
    strcpy(c.sensor[0].name, " mint");
    CHECK(!appconf_validate(&c, err, sizeof err));

    appconf_defaults(&c, ID);
    strcpy(c.sensor[0].name, "Mint");
    strcpy(c.sensor[1].name, "Sweet Potato");
    CHECK(appconf_validate(&c, err, sizeof err));

    appconf_defaults(&c, ID);
    strcpy(c.sensor[0].name, "Mint");
    strcpy(c.sensor[1].name, "mint");
    CHECK(!appconf_validate(&c, err, sizeof err));

    appconf_defaults(&c, ID);
    strcpy(c.sensor[1].name, "Availability");
    CHECK(!appconf_validate(&c, err, sizeof err));

    appconf_defaults(&c, ID);
    memset(c.sensor[1].name, 'x', APPCONF_SENSOR_NAME_MAX + 1);
    CHECK(!appconf_validate(&c, err, sizeof err));

    appconf_defaults(&c, ID);
    c.sensor[0].low_c100 = 2300;
    CHECK(!appconf_validate(&c, err, sizeof err));
    c.sensor[0].low_c100 = 2299;
    CHECK(appconf_validate(&c, err, sizeof err));

    appconf_defaults(&c, ID);
    c.sensor[1].high_c100 = APPCONF_TEMP_MAX_C100 + 1;
    CHECK(!appconf_validate(&c, err, sizeof err));
    c.sensor[1].high_c100 = APPCONF_TEMP_MAX_C100;
    CHECK(appconf_validate(&c, err, sizeof err));

    appconf_defaults(&c, ID);
    c.sensor[1].low_c100 = APPCONF_TEMP_MIN_C100 - 1;
    CHECK(!appconf_validate(&c, err, sizeof err));
}

static void test_validate_poll(void)
{
    appconf_t c;
    char err[128];

    appconf_defaults(&c, ID);
    c.poll_interval_s = APPCONF_POLL_MIN_S - 1;
    CHECK(!appconf_validate(&c, err, sizeof err));
    c.poll_interval_s = APPCONF_POLL_MIN_S;
    CHECK(appconf_validate(&c, err, sizeof err));
    c.poll_interval_s = APPCONF_POLL_MAX_S + 1;
    CHECK(!appconf_validate(&c, err, sizeof err));
}

static void test_validate_wifi(void)
{
    appconf_t c;
    char err[128];

    appconf_defaults(&c, ID);
    strcpy(c.wifi_ssid, "CafeOpen");
    CHECK(appconf_validate(&c, err, sizeof err));

    appconf_defaults(&c, ID);
    strcpy(c.wifi_ssid, "Home");
    strcpy(c.wifi_password, "1234567");
    CHECK(!appconf_validate(&c, err, sizeof err));
    strcpy(c.wifi_password, "12345678");
    CHECK(appconf_validate(&c, err, sizeof err));

    appconf_defaults(&c, ID);
    strcpy(c.wifi_password, "12345678");
    CHECK(!appconf_validate(&c, err, sizeof err));
}

static void test_validate_mqtt(void)
{
    appconf_t c;
    char err[128];

    appconf_defaults(&c, ID);
    c.mqtt_enabled = true;
    CHECK(!appconf_validate(&c, err, sizeof err));
    strcpy(c.mqtt_host, "192.168.1.10");
    CHECK(appconf_validate(&c, err, sizeof err));

    c.mqtt_port = 0;
    CHECK(!appconf_validate(&c, err, sizeof err));
    c.mqtt_port = 8883;

    strcpy(c.mqtt_prefix, "has space");
    CHECK(!appconf_validate(&c, err, sizeof err));
    strcpy(c.mqtt_prefix, "a/#");
    CHECK(!appconf_validate(&c, err, sizeof err));
    strcpy(c.mqtt_prefix, "home/tank/");
    CHECK(!appconf_validate(&c, err, sizeof err));
    strcpy(c.mqtt_prefix, "home/tank");
    CHECK(appconf_validate(&c, err, sizeof err));

    appconf_defaults(&c, ID);
    strcpy(c.mqtt_host, "bad host!!");
    CHECK(appconf_validate(&c, err, sizeof err));

    appconf_defaults(&c, ID);
    strcpy(c.mqtt_prefix, "tank/#");
    CHECK(!appconf_validate(&c, err, sizeof err));
}

static void test_validate_cert(void)
{
    appconf_t c;
    char err[128];

    appconf_defaults(&c, ID);
    strcpy(c.mqtt_ca_cert, "not a certificate");
    CHECK(!appconf_validate(&c, err, sizeof err));

    strcpy(c.mqtt_ca_cert, "-----BEGIN CERTIFICATE-----\nMIIB\n-----END CERTIFICATE-----\n");
    CHECK(appconf_validate(&c, err, sizeof err));
}

static void test_sanitize(void)
{
    appconf_t c;
    char err[128];

    appconf_defaults(&c, ID);
    CHECK(appconf_sanitize(&c, ID) == 0);

    /* What an old firmware let through: a reserved name next to good data. */
    strcpy(c.sensor[0].name, "Availability");
    c.sensor[0].rom = 0x28216531054AC79Bull;
    strcpy(c.sensor[1].name, "Potato");
    strcpy(c.wifi_ssid, "home");
    strcpy(c.wifi_password, "secret123");
    c.poll_interval_s = 0;
    CHECK(appconf_sanitize(&c, ID) == 2);
    CHECK(appconf_validate(&c, err, sizeof err));
    CHECK(strcmp(c.sensor[0].name, "left") == 0);
    CHECK(c.sensor[0].rom == 0x28216531054AC79Bull);
    CHECK(strcmp(c.sensor[1].name, "Potato") == 0);
    CHECK(strcmp(c.wifi_ssid, "home") == 0);
    CHECK(c.poll_interval_s == APPCONF_POLL_DEFAULT_S);

    /* Slot 1 taking slot 0's default name must not leave a duplicate. */
    appconf_defaults(&c, ID);
    strcpy(c.sensor[0].name, "right");
    strcpy(c.sensor[1].name, "right");
    CHECK(appconf_sanitize(&c, ID) == 1);
    CHECK(appconf_validate(&c, err, sizeof err));

    appconf_defaults(&c, ID);
    c.mqtt_enabled = true;
    c.mqtt_host[0] = '\0';
    strcpy(c.mqtt_prefix, "tank/#");
    CHECK(appconf_sanitize(&c, ID) == 2);
    CHECK(!c.mqtt_enabled);
    CHECK(appconf_validate(&c, err, sizeof err));
}

int main(void)
{
    test_device_id();
    test_hostname();
    test_defaults();
    test_sensor_helpers();
    test_validate_sensors();
    test_validate_poll();
    test_validate_wifi();
    test_validate_mqtt();
    test_validate_cert();
    test_sanitize();
    REPORT();
}
