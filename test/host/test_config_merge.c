/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "config_merge.h"

#include <string.h>

#include "tinytest.h"

static const char ID[] = "1a2b";

static bool merge(appconf_t *c, const char *json, char *err, size_t err_sz)
{
    cJSON *root = cJSON_Parse(json);
    CHECK(root != NULL);
    err[0] = '\0';
    bool ok = config_merge(root, c, err, err_sz);
    cJSON_Delete(root);
    return ok;
}

static void test_omitted_fields_keep_values(void)
{
    appconf_t c;
    char err[160];
    appconf_defaults(&c, ID);
    strcpy(c.wifi_ssid, "Home");
    strcpy(c.wifi_password, "secret123");

    CHECK(merge(&c, "{\"device\":{\"name\":\"Tank\"}}", err, sizeof err));
    CHECK_STR(c.device_name, "Tank");
    CHECK_STR(c.wifi_ssid, "Home");
    CHECK_STR(c.wifi_password, "secret123");
    CHECK(c.poll_interval_s == 300);

    CHECK(merge(&c, "{}", err, sizeof err));
    CHECK_STR(c.device_name, "Tank");
}

static void test_sensors(void)
{
    appconf_t c;
    char err[160];
    appconf_defaults(&c, ID);

    CHECK(merge(&c, "{\"sensors\":[{\"name\":\"Mint\",\"low\":17.5,\"high\":22.05},"
                    "{\"name\":\"Potato\"}]}", err, sizeof err));
    CHECK_STR(c.sensor[0].name, "Mint");
    CHECK(c.sensor[0].low_c100 == 1750 && c.sensor[0].high_c100 == 2205);
    CHECK_STR(c.sensor[1].name, "Potato");
    CHECK(c.sensor[1].low_c100 == 1800 && c.sensor[1].high_c100 == 2300);

    CHECK(!merge(&c, "{\"sensors\":[{\"name\":\"x\"}]}", err, sizeof err));
    CHECK(strstr(err, "array of 2") != NULL);

    CHECK(!merge(&c, "{\"sensors\":[{\"low\":130},{}]}", err, sizeof err));
    CHECK(strstr(err, "between -55 and 125") != NULL);

    CHECK(!merge(&c, "{\"sensors\":[{\"low\":23},{}]}", err, sizeof err));
    CHECK(strstr(err, "low must be below high") != NULL);

    CHECK(!merge(&c, "{\"sensors\":[{\"name\":null},{}]}", err, sizeof err));
    CHECK(strstr(err, "must be a string") != NULL);

    appconf_defaults(&c, ID);
    CHECK(!merge(&c, "{\"sensors\":[{\"name\":\"Mint\"},{\"name\":\"mint\"}]}",
                 err, sizeof err));
    CHECK(strstr(err, "must differ") != NULL);
}

static void test_sections(void)
{
    appconf_t c;
    char err[160];
    appconf_defaults(&c, ID);

    CHECK(!merge(&c, "{\"wifi\":\"Home\"}", err, sizeof err));
    CHECK_STR(err, "\"wifi\" must be an object");

    CHECK(!merge(&c, "{\"polling\":{\"interval_s\":5}}", err, sizeof err));
    CHECK(strstr(err, "between 10 and 3600") != NULL);
    CHECK(c.poll_interval_s == 300);

    CHECK(!merge(&c, "{\"polling\":{\"interval_s\":60.5}}", err, sizeof err));

    CHECK(merge(&c, "{\"polling\":{\"interval_s\":60}}", err, sizeof err));
    CHECK(c.poll_interval_s == 60);

    CHECK(!merge(&c, "{\"mqtt\":{\"enabled\":\"yes\"}}", err, sizeof err));
    CHECK(strstr(err, "true or false") != NULL);

    CHECK(!merge(&c, "{\"mqtt\":{\"enabled\":true}}", err, sizeof err));
    CHECK(strstr(err, "MQTT host is empty") != NULL);

    CHECK(merge(&c, "{\"mqtt\":{\"enabled\":true,\"host\":\"broker\",\"port\":8883,"
                    "\"tls\":true,\"topic_prefix\":\"tank\"}}", err, sizeof err));
    CHECK(c.mqtt_enabled && c.mqtt_tls && c.mqtt_port == 8883);
    CHECK_STR(c.mqtt_prefix, "tank");

    CHECK(!merge(&c, "{\"mqtt\":{\"topic_prefix\":\"tank/\"}}", err, sizeof err));
}

int main(void)
{
    test_omitted_fields_keep_values();
    test_sensors();
    test_sections();
    REPORT();
}
