/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OLED_W 128
#define OLED_H 64

typedef struct {
    int  sda_gpio;
    int  scl_gpio;
    uint8_t i2c_addr;
    uint32_t scl_hz;
    bool flip;
} oled_config_t;

esp_err_t oled_init(const oled_config_t *cfg);

void oled_clear(void);

void oled_pixel(int x, int y, bool on);

/* 1 bpp, row-major, MSB first, rows padded to a byte */
void oled_blit(const uint8_t *bits, int w, int h, int x, int y);

esp_err_t oled_flush(void);

#ifdef __cplusplus
}
#endif
