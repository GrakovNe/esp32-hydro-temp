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
    uint64_t rom;          /* bound ROM code, 0 when the slot is free */
    bool     present;      /* answered at the last poll */
    bool     valid;        /* temp_c100 holds a reading */
    int16_t  temp_c100;
    uint32_t age_ms;       /* since that reading, saturates */
    int64_t  read_epoch;   /* unix time of that reading, -1 when unsynced */
    uint32_t errors;       /* failed conversions and CRC errors */
} sensors_slot_t;

typedef struct {
    sensors_slot_t slot[APPCONF_SENSOR_COUNT];
    uint32_t seq;          /* increments after every completed poll */
    uint32_t max_age_ms;   /* freshness limit for this poll period, see fresh.h */
    uint8_t  found;        /* devices on the bus at the last scan */
    bool     bus_ok;
} sensors_reading_t;

/* Binds ROM codes to free slots in cfg and saves them through settings. */
void sensors_start(appconf_t *cfg);

void sensors_get(sensors_reading_t *out);

#ifdef __cplusplus
}
#endif
