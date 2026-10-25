/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "display.h"

#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "captive.h"
#include "fresh.h"
#include "glyphs.h"
#include "oled.h"
#include "sensors.h"
#include "wifi_mgr.h"

static const char *TAG = "display";

_Static_assert(LAYOUT_SCREEN_W == OLED_W && LAYOUT_SCREEN_H == OLED_H,
               "generator and panel disagree on the screen size");
_Static_assert(APPCONF_SENSOR_COUNT * LAYOUT_ROW_H <= LAYOUT_SCREEN_H,
               "not every sensor gets a row");

#define CHECK_MS      1000
#define OLED_I2C_ADDR 0x3C
#define OLED_I2C_HZ   100000   /* 400 kHz failed on this panel */

static const appconf_t *s_cfg;
static char s_ap_suffix[8];

static const glyph_t *glyph(char c)
{
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    for (size_t i = 0; i < sizeof GLYPHS / sizeof GLYPHS[0]; i++) {
        if (GLYPHS[i].ch == c) {
            return &GLYPHS[i];
        }
    }
    return glyph('?');
}

static void blit_glyph(const glyph_t *g, int x, int y, int scale)
{
    int stride = (g->w + 7) / 8;
    for (int yy = 0; yy < GLYPH_H; yy += scale) {
        const uint8_t *row = g->bits + yy * stride;
        for (int xx = 0; xx < g->w; xx += scale) {
            if (row[xx / 8] & (0x80u >> (xx % 8))) {
                oled_pixel(x + xx / scale, y + yy / scale, true);
            }
        }
    }
}

static int text_width(const char *s, int scale)
{
    int w = 0;
    for (; *s != '\0'; s++) {
        w += (glyph(*s)->w + LAYOUT_GAP) / scale;
    }
    return w > 0 ? w - LAYOUT_GAP / scale : 0;
}

static void draw_text(const char *s, int x, int y, int scale)
{
    for (; *s != '\0'; s++) {
        const glyph_t *g = glyph(*s);
        blit_glyph(g, x, y, scale);
        x += (g->w + LAYOUT_GAP) / scale;
    }
}

static void draw_centered(const char *s, int cx, int y, int scale)
{
    draw_text(s, cx - text_width(s, scale) / 2, y, scale);
}

static void draw_row(int i, const sensors_reading_t *r)
{
    const appconf_sensor_t *sc = &s_cfg->sensor[i];
    int y = i * LAYOUT_ROW_H + (LAYOUT_ROW_H - GLYPH_H) / 2;
    const glyph_t *letter = glyph(appconf_sensor_letter(sc->name));
    blit_glyph(letter, (LAYOUT_BLOCK_L - letter->w) / 2, y, 1);

    const uint8_t *face = face_none;
    char value[8] = "--.-";
    if (fresh_slot(r, i)) {
        int16_t t = r->slot[i].temp_c100;
        appconf_format_c100(t, 1, value, sizeof value);
        if (text_width(value, 1) > LAYOUT_VALUE_MAX_W) {
            snprintf(value, sizeof value, "%d", t / 100);
        }
        face = appconf_in_range(sc, t) ? face_happy : face_sad;
    }
    draw_centered(value, OLED_W / 2, y, 1);
    oled_blit(face, FACE_W, FACE_H,
              LAYOUT_BLOCK_R + (OLED_W - LAYOUT_BLOCK_R - FACE_W) / 2,
              i * LAYOUT_ROW_H + (LAYOUT_ROW_H - FACE_H) / 2);
}

static void draw_readings(const sensors_reading_t *r)
{
    oled_clear();
    for (int i = 0; i < APPCONF_SENSOR_COUNT; i++) {
        draw_row(i, r);
    }
    oled_flush();
}

static void draw_setup(void)
{
    oled_clear();
    draw_centered(s_ap_suffix, OLED_W / 2, (LAYOUT_ROW_H - GLYPH_H) / 2, 1);
    draw_centered(CAPTIVE_AP_IP, OLED_W / 2,
                  LAYOUT_ROW_H + (LAYOUT_ROW_H - GLYPH_H / 2) / 2, 2);
    oled_flush();
}

static void display_task(void *arg)
{
    (void)arg;
    uint32_t drawn_seq = UINT32_MAX;
    uint32_t drawn_fresh_bits = 0;
    bool drawn_setup = false;

    for (;;) {
        if (wifi_mgr_mode() == WIFI_MGR_PROVISIONING) {
            if (!drawn_setup) {
                draw_setup();
                drawn_setup = true;
            }
            vTaskDelay(pdMS_TO_TICKS(CHECK_MS));
            continue;
        }

        sensors_reading_t r;
        sensors_get(&r);
        /* A reading going stale changes the face without a new seq. */
        uint32_t fresh_bits = 0;
        for (int i = 0; i < APPCONF_SENSOR_COUNT; i++) {
            fresh_bits = fresh_bits * 2 + (fresh_slot(&r, i) ? 1 : 0);
        }
        if (drawn_setup || r.seq != drawn_seq || fresh_bits != drawn_fresh_bits) {
            draw_readings(&r);
            drawn_seq = r.seq;
            drawn_fresh_bits = fresh_bits;
            drawn_setup = false;
        }
        vTaskDelay(pdMS_TO_TICKS(CHECK_MS));
    }
}

void display_start(const appconf_t *cfg, const char device_id[5])
{
    s_cfg = cfg;
    snprintf(s_ap_suffix, sizeof s_ap_suffix, "%s", device_id);

    const oled_config_t oc = {
        .sda_gpio = CONFIG_APP_I2C_SDA_GPIO,
        .scl_gpio = CONFIG_APP_I2C_SCL_GPIO,
        .i2c_addr = OLED_I2C_ADDR,
        .scl_hz = OLED_I2C_HZ,
        .flip = CONFIG_APP_OLED_FLIP,
    };
    if (oled_init(&oc) != ESP_OK) {
        ESP_LOGW(TAG, "no display, running headless");
        return;
    }
    BaseType_t ok = xTaskCreate(display_task, "display", 4096, NULL, 3, NULL);
    configASSERT(ok == pdPASS);
}
