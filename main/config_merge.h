/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"

#include "appconf.h"

/* Applies a /api/v1/config POST body to c: omitted fields keep their
 * value, null is rejected, temperatures arrive as decimals. Validates the
 * result. On false, err holds the reason. */
bool config_merge(const cJSON *root, appconf_t *c, char *err, size_t err_sz);
