/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "oled.h"

#include <string.h>

#include "driver/i2c_master.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_log.h"

static const char *TAG = "oled";

/* SSD1306 page layout: byte = 8 vertical pixels, bit 0 on top. */
static uint8_t s_fb[OLED_W * OLED_H / 8];
static esp_lcd_panel_handle_t s_panel;

esp_err_t oled_init(const oled_config_t *cfg)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = cfg->sda_gpio,
        .scl_io_num = cfg->scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c bus: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr = cfg->i2c_addr,
        .control_phase_bytes = 1,
        .dc_bit_offset = 6,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .scl_speed_hz = cfg->scl_hz,
    };
    esp_lcd_panel_io_handle_t io;
    err = esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel io: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_panel_ssd1306_config_t ssd_cfg = { .height = OLED_H };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .bits_per_pixel = 1,
        .reset_gpio_num = -1,
        .vendor_config = &ssd_cfg,
    };
    err = esp_lcd_new_panel_ssd1306(io, &panel_cfg, &s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_lcd_panel_reset(s_panel);
    if (err == ESP_OK) {
        err = esp_lcd_panel_init(s_panel);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_mirror(s_panel, cfg->flip, cfg->flip);
    }
    if (err == ESP_OK) {
        oled_clear();
        err = oled_flush();
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_disp_on_off(s_panel, true);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "init on 0x%02x: %s", cfg->i2c_addr, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "ssd1306 %dx%d on sda %d scl %d", OLED_W, OLED_H,
             cfg->sda_gpio, cfg->scl_gpio);
    return ESP_OK;
}

void oled_clear(void)
{
    memset(s_fb, 0, sizeof s_fb);
}

void oled_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= OLED_W || y < 0 || y >= OLED_H) {
        return;
    }
    uint8_t *b = &s_fb[(y / 8) * OLED_W + x];
    uint8_t m = (uint8_t)(1u << (y % 8));
    if (on) {
        *b |= m;
    } else {
        *b &= (uint8_t)~m;
    }
}

void oled_blit(const uint8_t *bits, int w, int h, int x, int y)
{
    int stride = (w + 7) / 8;
    for (int yy = 0; yy < h; yy++) {
        const uint8_t *row = bits + yy * stride;
        for (int xx = 0; xx < w; xx++) {
            if (row[xx / 8] & (0x80u >> (xx % 8))) {
                oled_pixel(x + xx, y + yy, true);
            }
        }
    }
}

esp_err_t oled_flush(void)
{
    return esp_lcd_panel_draw_bitmap(s_panel, 0, 0, OLED_W, OLED_H, s_fb);
}
