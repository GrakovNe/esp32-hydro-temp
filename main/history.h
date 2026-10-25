/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "history_ring.h"

#define HISTORY_PART_NAME "history"

void history_start(void);

/* Called after every poll. Readings land in the five-minute window of
 * now_epoch; with no synced clock (now_epoch < 0) nothing is recorded,
 * so the history starts once NTP has answered. */
void history_add(const int16_t temp_c100[HIST_CHANNELS],
                 const bool valid[HIST_CHANNELS], int64_t now_epoch);

/* Writes the ring now; called before a planned restart. */
void history_flush(void);

uint16_t history_snapshot(hist_rec_t *out);

uint32_t history_step_s(void);
