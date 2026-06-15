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
} ili9881c_lcd_config_t;

typedef struct ili9881c_lcd_state *ili9881c_lcd_t;

BSP_NONNULL(1, 2) esp_err_t ili9881c_lcd_init(const ili9881c_lcd_config_t *config, ili9881c_lcd_t *lcd);
BSP_NONNULL(1) esp_err_t ili9881c_lcd_deinit(ili9881c_lcd_t lcd);
BSP_NONNULL(1) esp_err_t ili9881c_lcd_set_brightness(ili9881c_lcd_t lcd, int brightness);
BSP_NONNULL(1, 3) esp_err_t ili9881c_lcd_draw_bitmap(ili9881c_lcd_t lcd, bsp_rect_t rect, const void *data);
BSP_NONNULL(1) esp_err_t ili9881c_lcd_flush(ili9881c_lcd_t lcd, int fb_index);
BSP_NONNULL(1) void **ili9881c_lcd_get_frame_buffers(ili9881c_lcd_t lcd);
