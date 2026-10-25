/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "web_server.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "captive.h"
#include "config_merge.h"
#include "fresh.h"
#include "history.h"
#include "mqtt_mgr.h"
#include "reset.h"
#include "sensors.h"
#include "settings.h"
#include "time_mgr.h"
#include "wifi_mgr.h"

#include "setup_html.h"
#include "main_html.h"
#include "info_html.h"

static const char *TAG = "web";

#define POST_BODY_MAX 8192

static httpd_handle_t s_httpd;
static char s_device_id[5];
static char s_mac_str[18];
static const appconf_t *s_cfg;

/* The config POST works on a full appconf_t; too big for the httpd stack
 * and the server is single-threaded, so it lives here. */
static appconf_t s_merged;

static void add_str_or_null(cJSON *obj, const char *key, const char *val)
{
    if (val != NULL && val[0] != '\0') {
        cJSON_AddStringToObject(obj, key, val);
    } else {
        cJSON_AddNullToObject(obj, key);
    }
}

static esp_err_t send_json(httpd_req_t *req, const char *status, cJSON *body)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    char *s = cJSON_PrintUnformatted(body);
    esp_err_t err = ESP_OK;
    if (s != NULL) {
        err = httpd_resp_send(req, s, HTTPD_RESP_USE_STRLEN);
        cJSON_free(s);
    } else {
        err = httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    }
    cJSON_Delete(body);
    return err;
}

static esp_err_t send_json_err(httpd_req_t *req, const char *msg)
{
    cJSON *o = cJSON_CreateObject();
    if (o == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    }
    cJSON_AddStringToObject(o, "error", msg);
    return send_json(req, "400 Bad Request", o);
}

static esp_err_t serve_gz(httpd_req_t *req, const unsigned char *gz,
                          unsigned int len)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)gz, len);
}

static int read_body(httpd_req_t *req, char *buf, size_t cap)
{
    if ((size_t)req->content_len >= cap) {
        return -1;
    }
    size_t total = 0;
    while (total < (size_t)req->content_len) {
        /* httpd_req_recv() can return a partial read. */
        int n = httpd_req_recv(req, buf + total, req->content_len - total);
        if (n <= 0) {
            return -1;
        }
        total += (size_t)n;
    }
    buf[total] = '\0';
    return (int)total;
}

static esp_err_t handler_main(httpd_req_t *req)
{
    return serve_gz(req, main_html_gz, main_html_gz_len);
}

static esp_err_t handler_settings_page(httpd_req_t *req)
{
    return serve_gz(req, setup_html_gz, setup_html_gz_len);
}

static esp_err_t handler_info(httpd_req_t *req)
{
    return serve_gz(req, info_html_gz, info_html_gz_len);
}

static esp_err_t handler_root(httpd_req_t *req)
{
    if (wifi_mgr_ap_up()) {
        return handler_settings_page(req);
    }
    return handler_main(req);
}

static esp_err_t handler_wifi_scan(httpd_req_t *req)
{
    char query[64] = "";
    if (httpd_req_get_url_query_str(req, query, sizeof query) == ESP_OK) {
        char val[8];
        if (httpd_query_key_value(query, "refresh", val, sizeof val) == ESP_OK) {
            wifi_mgr_scan_start();
        }
    }

    wifi_mgr_scan_entry_t nets[WIFI_MGR_SCAN_MAX];
    size_t n = wifi_mgr_scan_result(nets, WIFI_MGR_SCAN_MAX);

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "networks");
    if (root == NULL || arr == NULL) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    }
    for (size_t i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        if (o == NULL) {
            break;
        }
        cJSON_AddStringToObject(o, "ssid", nets[i].ssid);
        cJSON_AddNumberToObject(o, "rssi", nets[i].rssi);
        cJSON_AddBoolToObject(o, "secure", nets[i].secure);
        cJSON_AddItemToArray(arr, o);
    }
    cJSON_AddBoolToObject(root, "scanning", wifi_mgr_scan_busy());
    return send_json(req, "200 OK", root);
}

static void add_timestamp(cJSON *obj, const char *key)
{
    int64_t ts = time_mgr_unix();
    if (ts < 0) {
        cJSON_AddNullToObject(obj, key);
    } else {
        cJSON_AddNumberToObject(obj, key, (double)ts);
    }
}

/* One sensor as the dashboard sees it: name, range and the reading with
 * its verdict, or null for a reading that is missing or stale. */
static cJSON *sensor_json(int i, const sensors_reading_t *r)
{
    const appconf_sensor_t *sc = &s_cfg->sensor[i];
    const sensors_slot_t *ss = &r->slot[i];
    cJSON *o = cJSON_CreateObject();
    if (o == NULL) {
        return NULL;
    }
    char slot[2] = { appconf_slot_letter(i), '\0' };
    cJSON_AddStringToObject(o, "slot", slot);
    cJSON_AddStringToObject(o, "name", sc->name);
    cJSON_AddNumberToObject(o, "low", appconf_deg(sc->low_c100));
    cJSON_AddNumberToObject(o, "high", appconf_deg(sc->high_c100));
    cJSON_AddBoolToObject(o, "present", ss->present);
    if (fresh_slot(r, i)) {
        cJSON_AddNumberToObject(o, "temperature", appconf_deg(ss->temp_c100));
        cJSON_AddNumberToObject(o, "age_s", ss->age_ms / 1000u);
        cJSON_AddBoolToObject(o, "out_of_range",
                              !appconf_in_range(sc, ss->temp_c100));
    } else {
        cJSON_AddNullToObject(o, "temperature");
        cJSON_AddNullToObject(o, "age_s");
        cJSON_AddNullToObject(o, "out_of_range");
    }
    return o;
}

static esp_err_t handler_current(httpd_req_t *req)
{
    sensors_reading_t r;
    sensors_get(&r);

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "sensors");
    if (root == NULL || arr == NULL) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    }
    add_timestamp(root, "timestamp");
    cJSON_AddBoolToObject(root, "time_synced", time_mgr_synced());
    cJSON_AddNumberToObject(root, "uptime_s", esp_timer_get_time() / 1000000);
    cJSON_AddNumberToObject(root, "poll_interval_s", s_cfg->poll_interval_s);
    for (int i = 0; i < APPCONF_SENSOR_COUNT; i++) {
        cJSON *o = sensor_json(i, &r);
        if (o == NULL) {
            break;
        }
        cJSON_AddItemToArray(arr, o);
    }
    return send_json(req, "200 OK", root);
}

/* 288 records as cJSON would be ~80 KB of heap; the format is simple
 * enough to write by hand in chunks. */
static esp_err_t handler_history(httpd_req_t *req)
{
    hist_rec_t *recs = malloc(sizeof(hist_rec_t) * HIST_RING_CAP);
    if (recs == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    }
    uint16_t n = history_snapshot(recs);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    char buf[128];
    int len = snprintf(buf, sizeof buf, "{\"step_s\":%" PRIu32 ",\"sensors\":[",
                       history_step_s());
    esp_err_t err = httpd_resp_send_chunk(req, buf, len);
    /* Names are validated to [A-Za-z0-9 _-], nothing to escape. */
    for (int i = 0; i < APPCONF_SENSOR_COUNT && err == ESP_OK; i++) {
        len = snprintf(buf, sizeof buf, "%s\"%s\"", i ? "," : "", s_cfg->sensor[i].name);
        err = httpd_resp_send_chunk(req, buf, len);
    }
    if (err == ESP_OK) {
        err = httpd_resp_send_chunk(req, "],\"records\":[", HTTPD_RESP_USE_STRLEN);
    }
    for (uint16_t i = 0; i < n && err == ESP_OK; i++) {
        len = snprintf(buf, sizeof buf, "%s{\"t\":%" PRIu32 ",\"v\":[", i ? "," : "",
                       recs[i].t);
        for (int c = 0; c < HIST_CHANNELS; c++) {
            char v[12] = "null";
            if (recs[i].temp_c100[c] != HIST_NO_TEMP) {
                appconf_format_c100(recs[i].temp_c100[c], 2, v, sizeof v);
            }
            len += snprintf(buf + len, sizeof buf - len, "%s%s", c ? "," : "", v);
        }
        len += snprintf(buf + len, sizeof buf - len, "]}");
        err = httpd_resp_send_chunk(req, buf, len);
    }
    free(recs);
    if (err == ESP_OK) {
        err = httpd_resp_send_chunk(req, "]}", HTTPD_RESP_USE_STRLEN);
    }
    esp_err_t end = httpd_resp_send_chunk(req, NULL, 0);
    return err != ESP_OK ? err : end;
}

static const char *reset_reason_str(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "power_on";
    case ESP_RST_EXT:      return "ext";
    case ESP_RST_SW:       return "software";
    case ESP_RST_PANIC:    return "panic";
    case ESP_RST_INT_WDT:  return "int_watchdog";
    case ESP_RST_TASK_WDT: return "task_watchdog";
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO:     return "sdio";
    default:               return "unknown";
    }
}

static esp_err_t handler_status(httpd_req_t *req)
{
    sensors_reading_t r;
    sensors_get(&r);
    wifi_mgr_status_t w;
    wifi_mgr_get_status(&w);

    cJSON *root = cJSON_CreateObject();
    cJSON *tm = cJSON_AddObjectToObject(root, "time");
    cJSON *dev = cJSON_AddObjectToObject(root, "device");
    cJSON *bus = cJSON_AddObjectToObject(root, "bus");
    cJSON *sensors = cJSON_AddArrayToObject(root, "sensors");
    cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
    cJSON *mqtt = cJSON_AddObjectToObject(root, "mqtt");
    if (root == NULL || tm == NULL || dev == NULL || bus == NULL ||
        sensors == NULL || wifi == NULL || mqtt == NULL) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    }

    add_timestamp(tm, "timestamp");
    cJSON_AddBoolToObject(tm, "synced", time_mgr_synced());
    cJSON_AddNumberToObject(tm, "uptime_s", esp_timer_get_time() / 1000000);

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    char rev[12];
    snprintf(rev, sizeof rev, "v%u.%u",
             (unsigned)chip.revision / 100, (unsigned)chip.revision % 100);
    uint32_t flash_sz = 0;
    if (esp_flash_get_size(NULL, &flash_sz) != ESP_OK) {
        flash_sz = 0;
    }
    cJSON_AddStringToObject(dev, "name", s_cfg->device_name);
    cJSON_AddStringToObject(dev, "id", s_device_id);
    cJSON_AddStringToObject(dev, "hostname", s_cfg->hostname);
    cJSON_AddStringToObject(dev, "chip", "ESP32");
    cJSON_AddStringToObject(dev, "revision", rev);
    cJSON_AddNumberToObject(dev, "cores", chip.cores);
    cJSON_AddNumberToObject(dev, "cpu_mhz", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    cJSON_AddStringToObject(dev, "firmware", APP_VER);
    cJSON_AddStringToObject(dev, "idf", esp_get_idf_version());
    cJSON_AddStringToObject(dev, "build_date", __DATE__);
    cJSON_AddStringToObject(dev, "mac", s_mac_str);
    if (flash_sz != 0) {
        cJSON_AddNumberToObject(dev, "flash_bytes", flash_sz);
    } else {
        cJSON_AddNullToObject(dev, "flash_bytes");
    }
    cJSON_AddNumberToObject(dev, "heap_free", esp_get_free_heap_size());
    cJSON_AddNumberToObject(dev, "heap_min_free",
                            esp_get_minimum_free_heap_size());
    cJSON_AddStringToObject(dev, "reset_reason", reset_reason_str());

    cJSON_AddStringToObject(root, "mode", wifi_mgr_mode_name(w.mode));

    cJSON_AddBoolToObject(bus, "ok", r.bus_ok);
    cJSON_AddNumberToObject(bus, "found", r.found);
    cJSON_AddNumberToObject(bus, "polls", r.seq);
    cJSON_AddNumberToObject(bus, "poll_interval_s", s_cfg->poll_interval_s);

    for (int i = 0; i < APPCONF_SENSOR_COUNT; i++) {
        cJSON *o = sensor_json(i, &r);
        if (o == NULL) {
            break;
        }
        char rom[17];
        if (r.slot[i].rom != 0) {
            appconf_rom_str(r.slot[i].rom, rom);
            cJSON_AddStringToObject(o, "rom", rom);
        } else {
            cJSON_AddNullToObject(o, "rom");
        }
        cJSON_AddNumberToObject(o, "errors", r.slot[i].errors);
        if (r.slot[i].read_epoch > 0) {
            cJSON_AddNumberToObject(o, "last_reading", (double)r.slot[i].read_epoch);
        } else {
            cJSON_AddNullToObject(o, "last_reading");
        }
        cJSON_AddItemToArray(sensors, o);
    }

    cJSON_AddBoolToObject(wifi, "connected", w.sta_connected);
    add_str_or_null(wifi, "ssid", w.ssid);
    add_str_or_null(wifi, "bssid", w.bssid);
    if (w.sta_connected) {
        cJSON_AddNumberToObject(wifi, "rssi", w.rssi);
        cJSON_AddNumberToObject(wifi, "channel", w.channel);
        add_str_or_null(wifi, "ip", w.ip);
        add_str_or_null(wifi, "gateway", w.gateway);
    } else {
        cJSON_AddNullToObject(wifi, "rssi");
        cJSON_AddNullToObject(wifi, "channel");
        cJSON_AddNullToObject(wifi, "ip");
        cJSON_AddNullToObject(wifi, "gateway");
    }
    add_str_or_null(wifi, "last_disconnect", w.last_disconnect);

    mqtt_mgr_status_t mq;
    mqtt_mgr_get_status(&mq);
    cJSON_AddBoolToObject(mqtt, "enabled", mq.enabled);
    cJSON_AddBoolToObject(mqtt, "connected", mq.connected);
    if (mq.last_publish > 0) {
        cJSON_AddNumberToObject(mqtt, "last_publish", (double)mq.last_publish);
    } else {
        cJSON_AddNullToObject(mqtt, "last_publish");
    }
    cJSON_AddNumberToObject(mqtt, "errors", mq.errors);

    return send_json(req, "200 OK", root);
}

static esp_err_t handler_config_get(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *dev = cJSON_AddObjectToObject(root, "device");
    cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
    cJSON *time = cJSON_AddObjectToObject(root, "time");
    cJSON *sensors = cJSON_AddArrayToObject(root, "sensors");
    cJSON *polling = cJSON_AddObjectToObject(root, "polling");
    cJSON *mqtt = cJSON_AddObjectToObject(root, "mqtt");
    if (root == NULL || dev == NULL || wifi == NULL || time == NULL ||
        sensors == NULL || polling == NULL || mqtt == NULL) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    }
    cJSON_AddStringToObject(dev, "name", s_cfg->device_name);
    cJSON_AddStringToObject(wifi, "ssid", s_cfg->wifi_ssid);
    cJSON_AddBoolToObject(wifi, "password_set", s_cfg->wifi_password[0] != '\0');
    cJSON_AddStringToObject(time, "ntp_server", s_cfg->ntp_server);
    for (int i = 0; i < APPCONF_SENSOR_COUNT; i++) {
        cJSON *o = cJSON_CreateObject();
        if (o == NULL) {
            break;
        }
        cJSON_AddStringToObject(o, "name", s_cfg->sensor[i].name);
        cJSON_AddNumberToObject(o, "low", appconf_deg(s_cfg->sensor[i].low_c100));
        cJSON_AddNumberToObject(o, "high", appconf_deg(s_cfg->sensor[i].high_c100));
        cJSON_AddItemToArray(sensors, o);
    }
    cJSON_AddNumberToObject(polling, "interval_s", s_cfg->poll_interval_s);
    cJSON_AddBoolToObject(mqtt, "enabled", s_cfg->mqtt_enabled);
    cJSON_AddStringToObject(mqtt, "host", s_cfg->mqtt_host);
    cJSON_AddNumberToObject(mqtt, "port", s_cfg->mqtt_port);
    cJSON_AddStringToObject(mqtt, "username", s_cfg->mqtt_username);
    cJSON_AddBoolToObject(mqtt, "password_set", s_cfg->mqtt_password[0] != '\0');
    cJSON_AddBoolToObject(mqtt, "tls", s_cfg->mqtt_tls);
    cJSON_AddBoolToObject(mqtt, "ca_cert_set", s_cfg->mqtt_ca_cert[0] != '\0');
    cJSON_AddBoolToObject(mqtt, "skip_verify", s_cfg->mqtt_skip_verify);
    cJSON_AddStringToObject(mqtt, "topic_prefix", s_cfg->mqtt_prefix);
    cJSON_AddBoolToObject(mqtt, "discovery", s_cfg->mqtt_discovery);
    return send_json(req, "200 OK", root);
}

#define RESTART_DELAY_US (1000 * 1000)

static void restart_timer_cb(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "restarting");
    esp_restart();
}

/* Restart after the response has left: a restart inside the handler
 * would cut the connection before the client sees the answer. */
static void schedule_restart(void)
{
    history_flush();
    const esp_timer_create_args_t args = {
        .callback = restart_timer_cb,
        .name = "restart",
    };
    esp_timer_handle_t t;
    if (esp_timer_create(&args, &t) == ESP_OK) {
        esp_timer_start_once(t, RESTART_DELAY_US);
    } else {
        esp_restart();
    }
}

static esp_err_t handler_config_post(httpd_req_t *req)
{
    char *body = malloc(POST_BODY_MAX);
    if (body == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    }
    int len = read_body(req, body, POST_BODY_MAX);
    if (len < 0) {
        free(body);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (root == NULL) {
        return send_json_err(req, "body is not valid JSON");
    }

    /* NVS is the base, not s_cfg: the sensor module updates ROM keys in
     * NVS and this module only reads the boot-time struct. */
    appconf_t *m = &s_merged;
    settings_load(m, s_device_id);
    char err[160];
    bool ok = config_merge(root, m, err, sizeof err);
    cJSON_Delete(root);
    if (!ok) {
        return send_json_err(req, err);
    }
    if (settings_save(m) != ESP_OK) {
        return send_json_err(req, "could not save settings");
    }

    appconf_derive(m, s_device_id);
    cJSON *out = cJSON_CreateObject();
    if (out == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    }
    char url[APPCONF_HOST_MAX + 32];
    snprintf(url, sizeof url, "http://%s.local/", m->hostname);
    cJSON_AddBoolToObject(out, "ok", true);
    cJSON_AddStringToObject(out, "url", url);
    ESP_LOGI(TAG, "settings saved, restarting soon");
    esp_err_t r = send_json(req, "200 OK", out);
    schedule_restart();
    return r;
}

static void drain_body(httpd_req_t *req)
{
    char scratch[64];
    int remaining = req->content_len;
    while (remaining > 0) {
        int n = httpd_req_recv(req, scratch,
                               remaining < (int)sizeof scratch
                                   ? remaining : (int)sizeof scratch);
        if (n <= 0) {
            return;
        }
        remaining -= n;
    }
}

static esp_err_t send_ok(httpd_req_t *req)
{
    cJSON *o = cJSON_CreateObject();
    if (o == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    }
    cJSON_AddBoolToObject(o, "ok", true);
    return send_json(req, "200 OK", o);
}

static esp_err_t handler_options(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods",
                       "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t handler_factory_reset(httpd_req_t *req)
{
    drain_body(req);
    ESP_LOGW(TAG, "factory reset requested over HTTP");
    esp_err_t r = send_ok(req);
    reset_mark_factory();
    schedule_restart();
    return r;
}

static esp_err_t handler_reboot(httpd_req_t *req)
{
    drain_body(req);
    ESP_LOGW(TAG, "reboot requested over HTTP");
    esp_err_t r = send_ok(req);
    schedule_restart();
    return r;
}

static esp_err_t handler_404(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    if (wifi_mgr_ap_up()) {
        static const char body[] =
            "<html><head><meta http-equiv=\"refresh\" content=\"0;url=" CAPTIVE_URL "\"></head>"
            "<body>Setting up your device: <a href=\"" CAPTIVE_URL "\">HydroTemp setup</a></body></html>";
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", CAPTIVE_URL);
        httpd_resp_set_type(req, "text/html; charset=utf-8");

        return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    }
    httpd_resp_set_status(req, "404 Not Found");
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, "not found", HTTPD_RESP_USE_STRLEN);
}

void web_server_start(const appconf_t *cfg, const char device_id[5])
{
    s_cfg = cfg;
    snprintf(s_device_id, sizeof s_device_id, "%s", device_id);
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    snprintf(s_mac_str, sizeof s_mac_str, MACSTR, MAC2STR(mac));

    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.max_uri_handlers = 16;
    hc.stack_size = 8192;
    hc.lru_purge_enable = true;
    hc.server_port = 80;

    ESP_ERROR_CHECK(httpd_start(&s_httpd, &hc));

    const httpd_uri_t uris[] = {
        { .uri = "/", .method = HTTP_GET, .handler = handler_root },
        { .uri = "/main", .method = HTTP_GET, .handler = handler_main },
        { .uri = "/config", .method = HTTP_GET, .handler = handler_settings_page },
        { .uri = "/info", .method = HTTP_GET, .handler = handler_info },
        { .uri = "/api/v1/wifi/scan", .method = HTTP_GET, .handler = handler_wifi_scan },
        { .uri = "/api/v1/current", .method = HTTP_GET, .handler = handler_current },
        { .uri = "/api/v1/history", .method = HTTP_GET, .handler = handler_history },
        { .uri = "/api/v1/status", .method = HTTP_GET, .handler = handler_status },
        { .uri = "/api/v1/config", .method = HTTP_GET, .handler = handler_config_get },
        { .uri = "/api/v1/config", .method = HTTP_POST, .handler = handler_config_post },
        { .uri = "/api/v1/config", .method = HTTP_OPTIONS, .handler = handler_options },
        { .uri = "/api/v1/factory-reset", .method = HTTP_POST, .handler = handler_factory_reset },
        { .uri = "/api/v1/factory-reset", .method = HTTP_OPTIONS, .handler = handler_options },
        { .uri = "/api/v1/reboot", .method = HTTP_POST, .handler = handler_reboot },
        { .uri = "/api/v1/reboot", .method = HTTP_OPTIONS, .handler = handler_options },
    };
    for (size_t i = 0; i < sizeof uris / sizeof uris[0]; i++) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(s_httpd, &uris[i]));
    }
    ESP_ERROR_CHECK(httpd_register_err_handler(s_httpd, HTTPD_404_NOT_FOUND,
                                               handler_404));

    ESP_LOGI(TAG, "http server up on :80");
}
