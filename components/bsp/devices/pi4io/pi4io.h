/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "bsp_private.h"

typedef enum {
    PI4IO_PIN_MODE_NONE,
    PI4IO_PIN_MODE_INPUT,
    PI4IO_PIN_MODE_OUTPUT,
} pi4io_pin_mode_t;

typedef enum {
    PI4IO_PULL_NONE,
    PI4IO_PULL_UP,
    PI4IO_PULL_DOWN,
} pi4io_pull_t;

typedef struct {
    pi4io_pin_mode_t mode : 2;
    bool initial_value : 1;
    pi4io_pull_t pull : 2;
    bool interrupt : 1;
} pi4io_pin_config_t;

typedef struct pi4io_state *pi4io_t;
typedef struct i2c_master_bus_t *i2c_master_bus_handle_t;

esp_err_t pi4io_init(i2c_master_bus_handle_t i2c_bus, uint8_t address, pi4io_pin_config_t config[8], pi4io_t *pi4io);
esp_err_t pi4io_deinit(pi4io_t pi4io);
esp_err_t pi4io_set_output(pi4io_t pi4io, uint8_t pin, bool value);
esp_err_t pi4io_get_input(pi4io_t pi4io, uint8_t pin, bool *value);
esp_err_t pi4io_get_all_inputs(pi4io_t pi4io, uint8_t *value);
esp_err_t pi4io_get_irq_status(pi4io_t pi4io, uint8_t *status);
