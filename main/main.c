/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"

#include "appconf.h"
#include "boot_btn.h"
#include "display.h"
#include "history.h"
#include "mdns.h"
#include "mqtt_mgr.h"
#include "reset.h"
#include "sensors.h"
#include "settings.h"
#include "time_mgr.h"
#include "web_server.h"
#include "wifi_mgr.h"

static const char *TAG = "main";

#ifndef APP_VER
#define APP_VER "dev"
#endif

static appconf_t s_cfg;

static void mdns_start(const appconf_t *cfg)
{
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mdns_init: %s", esp_err_to_name(err));
        return;
    }
    err = mdns_hostname_set(cfg->hostname);
    if (err == ESP_OK) {
        err = mdns_instance_name_set(cfg->device_name);
    }
    if (err == ESP_OK) {
        err = mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mdns registration: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "mdns: http://%s.local/", cfg->hostname);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "HydroTemp firmware %s (idf %s, built %s %s)",
             APP_VER, esp_get_idf_version(), __DATE__, __TIME__);

    reset_early_init();
    settings_nvs_init();

    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    char device_id[5];
    appconf_device_id(mac, device_id);

    appconf_t *cfg = &s_cfg;
    settings_load(cfg, device_id);
    ESP_LOGI(TAG, "device %s: name \"%s\", hostname %s.local",
             device_id, cfg->device_name, cfg->hostname);
    ESP_LOGI(TAG, "wifi: %s, mqtt: %s, ntp: %s, poll %u s",
             cfg->wifi_ssid[0] != '\0' ? cfg->wifi_ssid : "not configured",
             cfg->mqtt_enabled ? cfg->mqtt_host : "disabled",
             cfg->ntp_server, (unsigned)cfg->poll_interval_s);
    for (int i = 0; i < APPCONF_SENSOR_COUNT; i++) {
        char rom[17], low[12], high[12];
        appconf_rom_str(cfg->sensor[i].rom, rom);
        appconf_format_c100(cfg->sensor[i].low_c100, 2, low, sizeof low);
        appconf_format_c100(cfg->sensor[i].high_c100, 2, high, sizeof high);
        ESP_LOGI(TAG, "slot %d: \"%s\" %s..%s C, rom %s", i,
                 cfg->sensor[i].name, low, high, rom);
    }

    boot_btn_start();
    display_start(cfg, device_id);
    wifi_mgr_start(cfg, device_id);
    web_server_start(cfg, device_id);
    time_mgr_start(cfg);
    mdns_start(cfg);

    history_start();
    sensors_start(cfg);
    mqtt_mgr_start(cfg, device_id);
}
