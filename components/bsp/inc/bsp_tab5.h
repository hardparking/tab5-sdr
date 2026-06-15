/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#pragma once
#include "bsp_common.h"
#include "esp_lcd_touch.h"
#include "esp_codec_dev.h"

typedef struct {
    struct {
        uint8_t fb_num;
    } display;
    struct {
        bool interrupt;
    } touch;
    struct {
        bool enable;
    } wifi;
    struct {
        bool enable;
    } bluetooth;
} bsp_tab5_config_t;

esp_err_t bsp_tab5_init(const bsp_tab5_config_t *config);
// Enable/disable the 5V VBUS supplied to the USB-A host port (off by default).
// Required for host-mode peripherals such as an RTL-SDR dongle.
void bsp_tab5_set_usb_host_power(bool on);
// Initialise the ES8388 speaker DAC and return an esp_codec_dev output handle.
esp_codec_dev_handle_t bsp_tab5_audio_speaker_init(void);
void bsp_tab5_display_set_brightness(int brightness);
void *bsp_tab5_display_get_frame_buffer(int fb_index);
void bsp_tab5_display_flush(int fb_index);
int bsp_tab5_touch_read(esp_lcd_touch_point_data_t *points, uint8_t max_points);
void bsp_tab5_touch_wait_interrupt(void);
