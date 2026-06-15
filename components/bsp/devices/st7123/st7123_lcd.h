/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "bsp_private.h"
#include "misc/bsp_display.h"

typedef struct {
    int backlight_gpio;
    bsp_size_t size;
    bsp_pixel_format_t pixel_format;
    uint8_t fb_num;
} st7123_lcd_config_t;

typedef struct st7123_lcd_state *st7123_lcd_t;

BSP_NONNULL(1, 2) esp_err_t st7123_lcd_init(const st7123_lcd_config_t *config, st7123_lcd_t *lcd);
BSP_NONNULL(1) esp_err_t st7123_lcd_deinit(st7123_lcd_t lcd);
BSP_NONNULL(1) esp_err_t st7123_lcd_set_brightness(st7123_lcd_t lcd, int brightness);
BSP_NONNULL(1, 3) esp_err_t st7123_lcd_draw_bitmap(st7123_lcd_t lcd, bsp_rect_t rect, const void *data);
BSP_NONNULL(1) esp_err_t st7123_lcd_flush(st7123_lcd_t lcd, int fb_index);
BSP_NONNULL(1) void **st7123_lcd_get_frame_buffers(st7123_lcd_t lcd);
