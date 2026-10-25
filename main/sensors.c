/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "sensors.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "ds18b20.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "onewire_bus.h"
#include "sdkconfig.h"

#include "fresh.h"
#include "history.h"
#include "settings.h"
#include "time_mgr.h"

static const char *TAG = "sensors";

#define SETTLE_S       5
#define MIN_GAP_MS     1000
#define MAX_ON_BUS     8
#define DS18B20_FAMILY 0x28
#define READ_ATTEMPTS  3

typedef struct {
    ds18b20_device_handle_t dev;
    bool present;
    bool valid;
    int16_t temp_c100;
    int64_t read_mono_us;
    int64_t read_epoch;
    uint32_t errors;
} slot_t;

static struct {
    appconf_t *cfg;
    onewire_bus_handle_t bus;
    SemaphoreHandle_t lock;
    slot_t slot[APPCONF_SENSOR_COUNT];
    uint32_t seq;
    uint8_t found;
    bool bus_ok;
} ctx;

static void power_pins(void)
{
    int pins[2] = { CONFIG_APP_SENSOR_GND_GPIO, CONFIG_APP_SENSOR_VCC_GPIO };
    int levels[2] = { 0, 1 };
    for (int i = 0; i < 2; i++) {
        if (pins[i] < 0) {
            continue;
        }
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << pins[i],
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&io));
        ESP_ERROR_CHECK(gpio_set_level(pins[i], levels[i]));
    }
    ESP_LOGI(TAG, "sensor power: gnd gpio %d, vcc gpio %d",
             CONFIG_APP_SENSOR_GND_GPIO, CONFIG_APP_SENSOR_VCC_GPIO);
}

static int slot_of(uint64_t rom)
{
    for (int i = 0; i < APPCONF_SENSOR_COUNT; i++) {
        if (ctx.cfg->sensor[i].rom == rom) {
            return i;
        }
    }
    return -1;
}

static void attach(int i, onewire_device_t *dev)
{
    if (ctx.slot[i].dev != NULL) {
        ds18b20_del_device(ctx.slot[i].dev);
        ctx.slot[i].dev = NULL;
    }
    ds18b20_config_t dc;
    memset(&dc, 0, sizeof dc);
    esp_err_t err = ds18b20_new_device(dev, &dc, &ctx.slot[i].dev);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "slot %d: ds18b20_new_device: %s", i, esp_err_to_name(err));
        ctx.slot[i].errors++;
        return;
    }
    ds18b20_set_resolution(ctx.slot[i].dev, DS18B20_RESOLUTION_12B);
}

/* A search error ends the list early; bound probes are still read
 * directly. */
static int scan(onewire_device_t *found)
{
    onewire_device_iter_handle_t it;
    if (onewire_new_device_iter(ctx.bus, &it) != ESP_OK) {
        ESP_LOGW(TAG, "bus search failed");
        ctx.bus_ok = false;
        return 0;
    }
    int n = 0;
    onewire_device_t dev;
    esp_err_t err;
    while (n < MAX_ON_BUS && (err = onewire_device_iter_get_next(it, &dev)) == ESP_OK) {
        if ((dev.address & 0xFF) != DS18B20_FAMILY) {
            char rs[17];
            appconf_rom_str(dev.address, rs);
            ESP_LOGW(TAG, "ignoring non-DS18B20 device %s", rs);
            continue;
        }
        found[n++] = dev;
    }
    onewire_del_device_iter(it);
    ctx.bus_ok = err == ESP_ERR_NOT_FOUND || n == MAX_ON_BUS;
    if (!ctx.bus_ok) {
        ESP_LOGW(TAG, "bus search stopped after %d devices: %s", n,
                 esp_err_to_name(err));
    }

    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (found[j].address < found[i].address) {
                onewire_device_t t = found[i];
                found[i] = found[j];
                found[j] = t;
            }
        }
    }
    return n;
}

/* A truncated search binds nothing: probes it did not reach are not
 * missing. */
static void bind(onewire_device_t *found, int n, bool seen[APPCONF_SENSOR_COUNT])
{
    bool bound[MAX_ON_BUS] = { 0 };
    for (int i = 0; i < n; i++) {
        int k = slot_of(found[i].address);
        if (k >= 0) {
            seen[k] = true;
            bound[i] = true;
            if (ctx.slot[k].dev == NULL) {
                attach(k, &found[i]);
            }
        }
    }

    for (int i = 0; i < n && ctx.bus_ok; i++) {
        if (bound[i]) {
            continue;
        }
        char rs[17];
        appconf_rom_str(found[i].address, rs);
        int k = appconf_free_slot(ctx.cfg->sensor, seen);
        if (k < 0) {
            ESP_LOGW(TAG, "no free slot for %s", rs);
            continue;
        }
        ESP_LOGI(TAG, "slot %d (%s) <- %s%s", k, ctx.cfg->sensor[k].name, rs,
                 ctx.cfg->sensor[k].rom != 0 ? " (replaces missing sensor)" : "");
        xSemaphoreTake(ctx.lock, portMAX_DELAY);
        ctx.cfg->sensor[k].rom = found[i].address;
        xSemaphoreGive(ctx.lock);
        seen[k] = true;
        attach(k, &found[i]);
        settings_save_rom(k, found[i].address);
    }
}

static bool read_slot(int i, int attempts, int16_t *out)
{
    slot_t *sl = &ctx.slot[i];
    for (int attempt = 1; attempt <= attempts; attempt++) {
        float t;
        esp_err_t err = ds18b20_trigger_temperature_conversion(sl->dev);
        if (err == ESP_OK) {
            err = ds18b20_get_temperature(sl->dev, &t);
        }
        long c100 = err == ESP_OK ? lroundf(t * 100.0f) : 0;
        if (err == ESP_OK && c100 >= INT16_MIN && c100 <= INT16_MAX &&
            appconf_reading_ok((int16_t)c100)) {
            *out = (int16_t)c100;
            return true;
        }
        sl->errors++;
        ESP_LOGW(TAG, "slot %d: read failed (%s), attempt %d", i,
                 err == ESP_OK ? "bad value" : esp_err_to_name(err), attempt);
    }
    return false;
}

static void poll_once(void)
{
    onewire_device_t found[MAX_ON_BUS];
    bool seen[APPCONF_SENSOR_COUNT] = { 0 };
    int n = scan(found);
    bind(found, n, seen);
    ctx.found = (uint8_t)n;

    int16_t temps[APPCONF_SENSOR_COUNT];
    bool ok[APPCONF_SENSOR_COUNT];
    for (int i = 0; i < APPCONF_SENSOR_COUNT; i++) {
        int attempts = seen[i] || ctx.slot[i].present ? READ_ATTEMPTS : 1;
        ok[i] = ctx.slot[i].dev != NULL && read_slot(i, attempts, &temps[i]);
    }

    int64_t now_mono = esp_timer_get_time();
    int64_t now_epoch = time_mgr_unix();

    xSemaphoreTake(ctx.lock, portMAX_DELAY);
    for (int i = 0; i < APPCONF_SENSOR_COUNT; i++) {
        slot_t *sl = &ctx.slot[i];
        if (ok[i]) {
            sl->valid = true;
            sl->temp_c100 = temps[i];
            sl->read_mono_us = now_mono;
            sl->read_epoch = now_epoch;
        } else if (!seen[i]) {
            /* Gone from the bus: drop the value now. A probe that is seen but
             * fails to convert keeps it until stale. */
            sl->valid = false;
        }
        if (sl->present && !ok[i]) {
            char rs[17];
            appconf_rom_str(ctx.cfg->sensor[i].rom, rs);
            ESP_LOGW(TAG, "slot %d (%s): sensor %s did not answer", i,
                     ctx.cfg->sensor[i].name, rs);
        }
        sl->present = ok[i];
    }
    ctx.seq++;
    xSemaphoreGive(ctx.lock);

    char line[96];
    size_t o = 0;
    for (int i = 0; i < APPCONF_SENSOR_COUNT; i++) {
        char t[12] = "n/a";
        if (ok[i]) {
            appconf_format_c100(temps[i], 2, t, sizeof t);
        }
        o += snprintf(line + o, sizeof line - o, "%s%s=%s", i ? ", " : "",
                      ctx.cfg->sensor[i].name, t);
    }
    ESP_LOGI(TAG, "poll %" PRIu32 ": %s (%d on bus)", ctx.seq, line, n);

    history_add(temps, ok, now_epoch);
}

static void sensors_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(SETTLE_S * 1000));
    for (;;) {
        int64_t start = esp_timer_get_time();
        poll_once();
        int64_t spent_ms = (esp_timer_get_time() - start) / 1000;
        int64_t wait_ms = (int64_t)ctx.cfg->poll_interval_s * 1000 - spent_ms;
        vTaskDelay(pdMS_TO_TICKS(wait_ms > MIN_GAP_MS ? wait_ms : MIN_GAP_MS));
    }
}

void sensors_start(appconf_t *cfg)
{
    ctx.cfg = cfg;
    ctx.lock = xSemaphoreCreateMutex();
    configASSERT(ctx.lock != NULL);

    power_pins();

    onewire_bus_config_t bc = {
        .bus_gpio_num = CONFIG_APP_ONEWIRE_GPIO,
        .flags.en_pull_up = true,
    };
    onewire_bus_rmt_config_t rc = { .max_rx_bytes = 10 };
    esp_err_t err = onewire_new_bus_rmt(&bc, &rc, &ctx.bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "1-wire bus on gpio %d: %s", CONFIG_APP_ONEWIRE_GPIO,
                 esp_err_to_name(err));
        return;
    }
    for (int i = 0; i < APPCONF_SENSOR_COUNT; i++) {
        ctx.slot[i].read_epoch = -1;
    }
    ESP_LOGI(TAG, "1-wire on gpio %d, first poll in %d s, then every %u s",
             CONFIG_APP_ONEWIRE_GPIO, SETTLE_S, (unsigned)cfg->poll_interval_s);

    BaseType_t ok = xTaskCreate(sensors_task, "sensors", 4096, NULL, 5, NULL);
    configASSERT(ok == pdPASS);
}

void sensors_get(sensors_reading_t *out)
{
    memset(out, 0, sizeof *out);
    uint32_t poll_ms = (ctx.cfg != NULL ? ctx.cfg->poll_interval_s
                                        : APPCONF_POLL_DEFAULT_S) * 1000u;
    out->max_age_ms = fresh_max_age_ms(poll_ms);
    if (ctx.lock == NULL) {
        return;
    }
    int64_t now = esp_timer_get_time();
    xSemaphoreTake(ctx.lock, portMAX_DELAY);
    for (int i = 0; i < APPCONF_SENSOR_COUNT; i++) {
        const slot_t *sl = &ctx.slot[i];
        sensors_slot_t *o = &out->slot[i];
        o->rom = ctx.cfg->sensor[i].rom;
        o->present = sl->present;
        o->valid = sl->valid;
        o->temp_c100 = sl->temp_c100;
        if (sl->valid) {
            int64_t age = (now - sl->read_mono_us) / 1000;
            o->age_ms = age > UINT32_MAX ? UINT32_MAX : (uint32_t)age;
        }
        o->read_epoch = sl->read_epoch;
        o->errors = sl->errors;
    }
    out->seq = ctx.seq;
    out->found = ctx.found;
    out->bus_ok = ctx.bus_ok;
    xSemaphoreGive(ctx.lock);
}
