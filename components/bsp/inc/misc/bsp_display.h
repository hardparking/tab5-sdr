/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include <stdint.h>

typedef struct { int x, y; } bsp_point_t;
typedef struct { int width, height; } bsp_size_t;

typedef struct { bsp_point_t origin; bsp_size_t size; } bsp_rect_t;
static inline int bsp_rect_width(bsp_rect_t rect) { return rect.size.width; }
static inline int bsp_rect_height(bsp_rect_t rect) { return rect.size.height; }
static inline int bsp_rect_min_x(bsp_rect_t rect) { return rect.origin.x; }
static inline int bsp_rect_min_y(bsp_rect_t rect) { return rect.origin.y; }
static inline int bsp_rect_max_x(bsp_rect_t rect) { return rect.origin.x + rect.size.width; }
static inline int bsp_rect_max_y(bsp_rect_t rect) { return rect.origin.y + rect.size.height; }

typedef enum {
    BSP_PIXEL_FORMAT_RGB565,
    BSP_PIXEL_FORMAT_RGB888,
} bsp_pixel_format_t;
