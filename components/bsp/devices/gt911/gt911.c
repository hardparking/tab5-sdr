/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "gt911.h"
#include "esp_lcd_touch_gt911.h"

static const char *TAG = "GT911";

struct gt911_touch_state {
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_touch_handle_t handle;
    SemaphoreHandle_t interrupt_semaphore;
};

static void gt911_touch_interrupt_callback(esp_lcd_touch_handle_t tp) {
    struct gt911_touch_state *state = tp->config.user_data;
    BaseType_t pxHigherPriorityTaskWoken;
    xSemaphoreGiveFromISR(state->interrupt_semaphore, &pxHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);
}

esp_err_t gt911_touch_init(const gt911_touch_config_t *config, gt911_touch_t *touch) {
    struct gt911_touch_state *state = calloc(1, sizeof(struct gt911_touch_state));
    if (!state) {
        ESP_LOGE(TAG, "Failed to allocate memory for touch state");
        return ESP_ERR_NO_MEM;
    }

    esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_config.scl_speed_hz = config->scl_speed_hz;
    io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;

    esp_err_t ret = esp_lcd_new_panel_io_i2c(config->i2c_bus, &io_config, &state->io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create panel IO: %s", esp_err_to_name(ret));
        free(state);
        return ret;
    }

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = config->size.width,
        .y_max = config->size.height,
        .rst_gpio_num = config->rst_gpio,
        .int_gpio_num = config->int_gpio,
    };

    ret = esp_lcd_touch_new_i2c_gt911(state->io_handle, &tp_cfg, &state->handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize GT911: %s", esp_err_to_name(ret));
        esp_lcd_panel_io_del(state->io_handle);
        free(state);
        return ret;
    }

    ret = esp_lcd_touch_exit_sleep(state->handle);
    if (ret != ESP_OK) {
        esp_lcd_touch_del(state->handle);
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

        ret = esp_lcd_touch_register_interrupt_callback_with_data(state->handle, gt911_touch_interrupt_callback, state);
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

esp_err_t gt911_touch_deinit(gt911_touch_t touch) {
    esp_lcd_touch_del(touch->handle);
    esp_lcd_panel_io_del(touch->io_handle);
    free(touch);
    return ESP_OK;
}

int gt911_touch_read(gt911_touch_t touch, esp_lcd_touch_point_data_t *points, uint8_t max_points) {
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

void gt911_touch_wait_interrupt(gt911_touch_t touch) {
    xSemaphoreTake(touch->interrupt_semaphore, portMAX_DELAY);
}
