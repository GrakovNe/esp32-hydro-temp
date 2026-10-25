/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "appconf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t rom;
    bool     present;
    bool     valid;
    int16_t  temp_c100;
    uint32_t age_ms;       /* saturates */
    int64_t  read_epoch;   /* -1 when the clock was not synced */
    uint32_t errors;
} sensors_slot_t;

typedef struct {
    sensors_slot_t slot[APPCONF_SENSOR_COUNT];
    uint32_t seq;
    uint32_t max_age_ms;
    uint8_t  found;
    bool     bus_ok;
} sensors_reading_t;

/* Not const: the poll task stores bound ROMs in cfg. */
void sensors_start(appconf_t *cfg);

void sensors_get(sensors_reading_t *out);

#ifdef __cplusplus
}
#endif
