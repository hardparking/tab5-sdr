/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "bsp_private.h"
#include "misc/bsp_display.h"
#include "driver/i2c_master.h"
#include "esp_lcd_touch.h"

typedef struct {
    i2c_master_bus_handle_t i2c_bus;
    bsp_size_t size;
    gpio_num_t int_gpio;
    gpio_num_t rst_gpio;
    uint32_t scl_speed_hz;
    bool interrupt;
} st7123_touch_config_t;

typedef struct st7123_touch_state *st7123_touch_t;

BSP_NONNULL(1, 2) esp_err_t st7123_touch_init(const st7123_touch_config_t *config, st7123_touch_t *touch);
BSP_NONNULL(1) esp_err_t st7123_touch_deinit(st7123_touch_t touch);
BSP_NONNULL(1, 2) int st7123_touch_read(st7123_touch_t touch, esp_lcd_touch_point_data_t *points, uint8_t max_points);
BSP_NONNULL(1) void st7123_touch_wait_interrupt(st7123_touch_t touch);
