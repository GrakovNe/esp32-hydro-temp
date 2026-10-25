/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "history.h"

#include <inttypes.h>
#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

static const char *TAG = "history";

#define HIST_NS       "hist"
#define HIST_KEY      "ring"
#define HIST_WINDOW_S 300u

static hist_store_t s_store;
static SemaphoreHandle_t s_lock;
static nvs_handle_t s_nvs;

static uint32_t s_window;
static int32_t s_sum[HIST_CHANNELS];
static uint16_t s_n[HIST_CHANNELS];

static void store_save_locked(void)
{
    esp_err_t err = nvs_set_blob(s_nvs, HIST_KEY, &s_store, sizeof s_store);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save: %s", esp_err_to_name(err));
        return;
    }
    err = nvs_commit(s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "commit: %s", esp_err_to_name(err));
    }
}

/* RAM keeps the running average of the open window; flash is written
 * once per window and before a planned restart. */
static void update_window_locked(void)
{
    hist_rec_t r = { .t = s_window };
    for (int c = 0; c < HIST_CHANNELS; c++) {
        r.temp_c100[c] = s_n[c] > 0
            ? (int16_t)lroundf((float)s_sum[c] / (float)s_n[c]) : HIST_NO_TEMP;
    }
    hist_store_append(&s_store, r);
}

void history_add(const int16_t temp_c100[HIST_CHANNELS],
                 const bool valid[HIST_CHANNELS], int64_t now_epoch)
{
    if (s_lock == NULL || now_epoch < 0) {
        return;
    }
    uint32_t window = (uint32_t)(now_epoch - now_epoch % HIST_WINDOW_S);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (window != s_window) {
        if (s_window != 0) {
            store_save_locked();
            ESP_LOGI(TAG, "window %" PRIu32 " saved", s_window);
        }
        s_window = window;
        memset(s_sum, 0, sizeof s_sum);
        memset(s_n, 0, sizeof s_n);
        /* After a restart inside a window the flushed average counts as
         * one sample, so the pre-restart readings are not lost. */
        hist_rec_t last;
        if (hist_store_last(&s_store, &last) && last.t == window) {
            for (int c = 0; c < HIST_CHANNELS; c++) {
                if (last.temp_c100[c] != HIST_NO_TEMP) {
                    s_sum[c] = last.temp_c100[c];
                    s_n[c] = 1;
                }
            }
        }
    }
    bool have = false;
    for (int c = 0; c < HIST_CHANNELS; c++) {
        if (valid[c]) {
            s_sum[c] += temp_c100[c];
            s_n[c]++;
        }
        have = have || s_n[c] > 0;
    }
    if (have) {
        update_window_locked();
    }
    xSemaphoreGive(s_lock);
}

void history_flush(void)
{
    if (s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    store_save_locked();
    xSemaphoreGive(s_lock);
}

void history_start(void)
{
    esp_err_t err = nvs_open_from_partition(HISTORY_PART_NAME, HIST_NS,
                                            NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s, history disabled", esp_err_to_name(err));
        return;
    }

    size_t sz = sizeof s_store;
    err = nvs_get_blob(s_nvs, HIST_KEY, &s_store, &sz);
    if (err == ESP_OK && sz == sizeof s_store &&
        s_store.magic == HIST_MAGIC && s_store.version == HIST_VERSION) {
        if (s_store.count > HIST_RING_CAP) {
            s_store.count = HIST_RING_CAP;
        }
        if (s_store.next >= HIST_RING_CAP) {
            s_store.next = 0;
        }
        ESP_LOGI(TAG, "ring loaded: %u records", s_store.count);
    } else {
        hist_store_init(&s_store);
        store_save_locked();
        ESP_LOGI(TAG, "ring created");
    }

    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock != NULL);
}

uint16_t history_snapshot(hist_rec_t *out)
{
    if (s_lock == NULL) {
        return 0;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint16_t n = hist_store_sorted(&s_store, out);
    xSemaphoreGive(s_lock);
    return n;
}

uint32_t history_step_s(void)
{
    return HIST_WINDOW_S;
}
