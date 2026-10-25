/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include "appconf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the OLED and a task that redraws it when the readings or the
 * Wi-Fi mode change. Without a display no task is started. */
void display_start(const appconf_t *cfg, const char device_id[5]);

#ifdef __cplusplus
}
#endif
