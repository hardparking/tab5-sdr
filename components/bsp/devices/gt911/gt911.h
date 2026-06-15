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
} gt911_touch_config_t;

typedef struct gt911_touch_state *gt911_touch_t;

BSP_NONNULL(1, 2) esp_err_t gt911_touch_init(const gt911_touch_config_t *config, gt911_touch_t *touch);
BSP_NONNULL(1) esp_err_t gt911_touch_deinit(gt911_touch_t touch);
BSP_NONNULL(1, 2) int gt911_touch_read(gt911_touch_t touch, esp_lcd_touch_point_data_t *points, uint8_t max_points);
BSP_NONNULL(1) void gt911_touch_wait_interrupt(gt911_touch_t touch);
