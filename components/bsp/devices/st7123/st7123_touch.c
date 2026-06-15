/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "st7123_touch.h"
#include "esp_lcd_touch_st7123.h"

static const char *TAG = "ST7123_TP";

struct st7123_touch_state {
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_touch_handle_t handle;
    SemaphoreHandle_t interrupt_semaphore;
};

static void st7123_touch_interrupt_callback(esp_lcd_touch_handle_t tp) {
    struct st7123_touch_state *state = tp->config.user_data;
    BaseType_t pxHigherPriorityTaskWoken;
    xSemaphoreGiveFromISR(state->interrupt_semaphore, &pxHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);
}

esp_err_t st7123_touch_init(const st7123_touch_config_t *config, st7123_touch_t *touch) {
    esp_err_t ret;

    struct st7123_touch_state *state = calloc(1, sizeof(struct st7123_touch_state));
    if (state == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for touch state");
        return ESP_ERR_NO_MEM;
    }

    // Setup IO
    esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_ST7123_CONFIG();
    io_config.scl_speed_hz = config->scl_speed_hz;

    ret = esp_lcd_new_panel_io_i2c(config->i2c_bus, &io_config, &state->io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create panel IO: %s", esp_err_to_name(ret));
        free(state);
        return ret;
    }

    // Init ST7123 Touch
    esp_lcd_touch_config_t touch_config = {
        .x_max = config->size.width,
        .y_max = config->size.height,
        .rst_gpio_num = config->rst_gpio,
        .int_gpio_num = config->int_gpio,
    };

    ret = esp_lcd_touch_new_i2c_st7123(state->io_handle, &touch_config, &state->handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ST7123 touch: %s", esp_err_to_name(ret));
        esp_lcd_panel_io_del(state->io_handle);
        free(state);
        return ret;
    }

    if (config->interrupt) {
        state->interrupt_semaphore = xSemaphoreCreateBinary();
        if (!state->interrupt_semaphore) {
            esp_lcd_touch_del(state->handle);
            esp_lcd_panel_io_del(state->io_handle);
            free(state);
            return ESP_ERR_NO_MEM;
        }

        ret = gpio_config(&(gpio_config_t){
            .mode = GPIO_MODE_INPUT,
            .pin_bit_mask = 1 << config->int_gpio,
            .intr_type = GPIO_INTR_NEGEDGE,
        });
        if (ret != ESP_OK) {
            esp_lcd_touch_del(state->handle);
            esp_lcd_panel_io_del(state->io_handle);
            free(state);
            return ret;
        }

        ret = esp_lcd_touch_register_interrupt_callback_with_data(state->handle, st7123_touch_interrupt_callback, state);
        if (ret != ESP_OK) {
            esp_lcd_touch_del(state->handle);
            esp_lcd_panel_io_del(state->io_handle);
            free(state);
            return ret;
        }
    }

    *touch = state;
    return ESP_OK;
}

esp_err_t st7123_touch_deinit(st7123_touch_t touch) {
    esp_lcd_touch_del(touch->handle);
    esp_lcd_panel_io_del(touch->io_handle);
    free(touch);
    return ESP_OK;
}

int st7123_touch_read(st7123_touch_t touch, esp_lcd_touch_point_data_t *points, uint8_t max_points) {
    if (max_points == 0) return 0;
    if (max_points > 5) max_points = 5;

    esp_err_t ret = esp_lcd_touch_read_data(touch->handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read touch data: %s", esp_err_to_name(ret));
        return 0;
    }

    uint8_t count = 0;
    esp_lcd_touch_get_data(touch->handle, points, &count, max_points);
    return count;
}

void st7123_touch_wait_interrupt(st7123_touch_t touch) {
    xSemaphoreTake(touch->interrupt_semaphore, portMAX_DELAY);
}
