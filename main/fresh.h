/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>

#include "sensors.h"

/* two missed polls plus a margin */
#define FRESH_MARGIN_MS 30000u

static inline uint32_t fresh_max_age_ms(uint32_t poll_ms)
{
    return 2u * poll_ms + FRESH_MARGIN_MS;
}

static inline bool fresh_slot(const sensors_reading_t *r, int i)
{
    return r->slot[i].valid && r->slot[i].age_ms <= r->max_age_ms;
}
