/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "st7123_lcd.h"
#include "misc/bsp_display.h"
#include "st7123_init_data.h"
#include "driver/ledc.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_st7123.h"

struct st7123_lcd_state {
    ledc_timer_config_t ledc_timer;
    ledc_channel_config_t ledc_channel;
    esp_ldo_channel_handle_t phy_power_channel;
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus;
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
    bsp_size_t size;
    bsp_pixel_format_t pixel_format;
    uint8_t fb_num;
    void *frame_buffers[3];
};

esp_err_t st7123_lcd_init(const st7123_lcd_config_t *config, st7123_lcd_t *lcd) {
    esp_err_t ret;

    struct st7123_lcd_state *state = calloc(1, sizeof(struct st7123_lcd_state));
    if (state == NULL) {
        return ESP_ERR_NO_MEM;
    }

    state->size = config->size;
    state->pixel_format = config->pixel_format;
    state->fb_num = config->fb_num > 0 ? config->fb_num : 1;
    if (state->fb_num > 3) state->fb_num = 3;

    bool rgb888 = (config->pixel_format == BSP_PIXEL_FORMAT_RGB888);

    // Setup Backlight using LEDC
    state->ledc_timer = (ledc_timer_config_t){
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_12_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ret = ledc_timer_config(&state->ledc_timer);
    if (ret != ESP_OK) goto err_free;

    state->ledc_channel = (ledc_channel_config_t){
        .gpio_num = config->backlight_gpio,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ret = ledc_channel_config(&state->ledc_channel);
    if (ret != ESP_OK) goto err_free;

    // Enable DSI PHY power
    esp_ldo_channel_config_t ldo_config = {
        .chan_id = 3,
        .voltage_mv = 2500,
    };
    ret = esp_ldo_acquire_channel(&ldo_config, &state->phy_power_channel);
    if (ret != ESP_OK) goto err_free;

    // Create MIPI DSI Bus
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = 2,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = 870,
    };
    ret = esp_lcd_new_dsi_bus(&bus_config, &state->mipi_dsi_bus);
    if (ret != ESP_OK) goto err_ldo;

    // Install MIPI DSI LCD control panel (DBI)
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ret = esp_lcd_new_panel_io_dbi(state->mipi_dsi_bus, &dbi_config, &state->io);
    if (ret != ESP_OK) goto err_dsi_bus;

    // Install LCD Driver of ST7123
    esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 75,
        .pixel_format = rgb888 ? LCD_COLOR_PIXEL_FORMAT_RGB888 : LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = state->fb_num,
        .video_timing = {
            .h_size = config->size.width,
            .v_size = config->size.height,
            .hsync_pulse_width = 40,
            .hsync_back_porch = 140,
            .hsync_front_porch = 40,
            .vsync_pulse_width = 2,
            .vsync_back_porch = 8,
            .vsync_front_porch = 220,
        },
        .flags = {
            .use_dma2d = 1,
        },
    };

    st7123_vendor_config_t vendor_config = {
        .init_cmds = bsp_lcd_st7123_specific_init_code_default_ptr,
        .init_cmds_size = bsp_lcd_st7123_specific_init_code_default_num,
        .mipi_config = {
            .dsi_bus = state->mipi_dsi_bus,
            .dpi_config = &dpi_config,
        },
    };

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 24,
        .vendor_config = &vendor_config,
    };

    ret = esp_lcd_new_panel_st7123(state->io, &panel_config, &state->panel);
    if (ret != ESP_OK) goto err_io;

    ret = esp_lcd_panel_reset(state->panel);
    if (ret != ESP_OK) goto err_panel;

    ret = esp_lcd_panel_init(state->panel);
    if (ret != ESP_OK) goto err_panel;

    ret = esp_lcd_panel_disp_on_off(state->panel, true);
    if (ret != ESP_OK) goto err_panel;

    // Get frame buffers
    void *fb0 = NULL, *fb1 = NULL, *fb2 = NULL;
    esp_lcd_dpi_panel_get_frame_buffer(state->panel, state->fb_num, &fb0, &fb1, &fb2);
    state->frame_buffers[0] = fb0;
    state->frame_buffers[1] = fb1;
    state->frame_buffers[2] = fb2;

    *lcd = state;
    return ESP_OK;

err_panel:
    esp_lcd_panel_del(state->panel);
err_io:
    esp_lcd_panel_io_del(state->io);
err_dsi_bus:
    esp_lcd_del_dsi_bus(state->mipi_dsi_bus);
err_ldo:
    esp_ldo_release_channel(state->phy_power_channel);
err_free:
    free(state);
    return ret;
}

esp_err_t st7123_lcd_deinit(st7123_lcd_t lcd) {
    esp_lcd_panel_del(lcd->panel);
    esp_lcd_panel_io_del(lcd->io);
    esp_lcd_del_dsi_bus(lcd->mipi_dsi_bus);
    esp_ldo_release_channel(lcd->phy_power_channel);
    free(lcd);
    return ESP_OK;
}

esp_err_t st7123_lcd_set_brightness(st7123_lcd_t lcd, int brightness) {
    if (brightness < 0) brightness = 0;
    if (brightness > 100) brightness = 100;

    uint32_t duty = (uint32_t)(((float)brightness / 100.0f) * ((1 << 12) - 1));
    esp_err_t ret = ledc_set_duty(lcd->ledc_channel.speed_mode, lcd->ledc_channel.channel, duty);
    if (ret != ESP_OK) return ret;
    return ledc_update_duty(lcd->ledc_channel.speed_mode, lcd->ledc_channel.channel);
}

esp_err_t st7123_lcd_draw_bitmap(st7123_lcd_t lcd, bsp_rect_t rect, const void *data) {
    return esp_lcd_panel_draw_bitmap(lcd->panel,
        bsp_rect_min_x(rect), bsp_rect_min_y(rect),
        bsp_rect_max_x(rect), bsp_rect_max_y(rect), data);
}

esp_err_t st7123_lcd_flush(st7123_lcd_t lcd, int fb_index) {
    if (fb_index < 0 || fb_index >= lcd->fb_num) return ESP_ERR_INVALID_ARG;
    if (lcd->frame_buffers[fb_index] == NULL) return ESP_ERR_INVALID_STATE;

    return esp_lcd_panel_draw_bitmap(lcd->panel, 0, 0, lcd->size.width, lcd->size.height, lcd->frame_buffers[fb_index]);
}

void **st7123_lcd_get_frame_buffers(st7123_lcd_t lcd) {
    return lcd->frame_buffers;
}
