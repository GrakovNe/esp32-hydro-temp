/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "appconf.h"

#define HIST_RING_CAP 288
#define HIST_CHANNELS APPCONF_SENSOR_COUNT
#define HIST_MAGIC    0x48547432u
#define HIST_VERSION  1

#define HIST_NO_TEMP ((int16_t)-32768)

typedef struct {
    uint32_t t;                        /* window start, unix s; 0 = empty slot */
    int16_t  temp_c100[HIST_CHANNELS];
} hist_rec_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t next;
    uint16_t count;
    uint16_t pad;
    hist_rec_t rec[HIST_RING_CAP];
} hist_store_t;

void hist_store_init(hist_store_t *s);

void hist_store_append(hist_store_t *s, hist_rec_t r);

uint16_t hist_store_sorted(const hist_store_t *s, hist_rec_t *out);

/* The most recently appended record; false when the ring is empty. */
bool hist_store_last(const hist_store_t *s, hist_rec_t *out);
