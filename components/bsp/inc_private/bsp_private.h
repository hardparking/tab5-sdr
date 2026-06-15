/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include "esp_err.h"
#include "esp_log.h"

#define BSP_ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#define BSP_NONNULL(...) __attribute__((nonnull(__VA_ARGS__)))
#define BSP_RETURN_ERR(e) do { if (e != ESP_OK) return e; } while (0)
