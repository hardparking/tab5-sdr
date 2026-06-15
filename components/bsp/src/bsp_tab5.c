/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "bsp_private.h"
#include "bsp_tab5.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es8388_codec.h"
#include "misc/bsp_display.h"
#include "pi4io/pi4io.h"
#include "ili9881c/ili9881c.h"
#include "gt911/gt911.h"
#include "st7123/st7123_lcd.h"
#include "st7123/st7123_touch.h"
#include "nvs_flash.h"
#include "esp_hosted.h"
#ifdef CONFIG_BT_BLUEDROID_ENABLED
#include "esp_hosted_bt.h"
#endif
#ifdef CONFIG_BT_NIMBLE_ENABLED
#include "nimble/nimble_port.h"
#endif

static const char *TAG = "BSP_TAB5";

#define I2C0_PORT_NUM (0)
static i2c_master_bus_handle_t i2c0;
static pi4io_t pi4ioe1, pi4ioe2;

static void **frame_buffers;
static ili9881c_lcd_t ili9881c;
static gt911_touch_t gt911;
static st7123_lcd_t st7123_lcd;
static st7123_touch_t st7123_touch;

esp_err_t bsp_tab5_init(const bsp_tab5_config_t *config) {
    esp_err_t err;

    // Check config values
    bsp_tab5_config_t tmp_config = *config;
    if (!tmp_config.display.fb_num) tmp_config.display.fb_num = 1;
    config = &tmp_config;

    // Initialize I2C0 bus
    err = i2c_new_master_bus(&(i2c_master_bus_config_t){
        .i2c_port = I2C0_PORT_NUM,
        .sda_io_num = GPIO_NUM_31,
        .scl_io_num = GPIO_NUM_32,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .flags.enable_internal_pullup = true,
    }, &i2c0);
    BSP_RETURN_ERR(err);

    // Initialize PI4IOE1 (address 0x43)
    err = pi4io_init(i2c0, 0x43, (pi4io_pin_config_t[8]){
        [0] = { PI4IO_PIN_MODE_OUTPUT, .initial_value = false },  // RF_INT_EXT_SWITCH
        [1] = { PI4IO_PIN_MODE_OUTPUT, .initial_value = true },   // SPK_EN
        [2] = { PI4IO_PIN_MODE_OUTPUT, .initial_value = true },   // EXT5V_EN
        [4] = { PI4IO_PIN_MODE_OUTPUT, .initial_value = true },   // LCD_RST
        [5] = { PI4IO_PIN_MODE_OUTPUT, .initial_value = true },   // TP_RST
        [6] = { PI4IO_PIN_MODE_OUTPUT, .initial_value = true },   // CAM_RST
        [7] = { PI4IO_PIN_MODE_INPUT },                           // HP_DET
    }, &pi4ioe1);
    BSP_RETURN_ERR(err);

    // Initialize PI4IOE2 (address 0x44)
    err = pi4io_init(i2c0, 0x44, (pi4io_pin_config_t[8]){
        [0] = { PI4IO_PIN_MODE_OUTPUT, .initial_value = true },   // WLAN_PWR_EN
        [3] = { PI4IO_PIN_MODE_OUTPUT, .initial_value = false },  // USB5V_EN
        [4] = { PI4IO_PIN_MODE_OUTPUT, .initial_value = false },  // PWROFF_PLUSE
        [5] = { PI4IO_PIN_MODE_OUTPUT, .initial_value = false },  // nCHG_QC_EN
        [6] = { PI4IO_PIN_MODE_INPUT },                           // CHG_STAT
        [7] = { PI4IO_PIN_MODE_OUTPUT, .initial_value = false },  // CHG_EN
    }, &pi4ioe2);
    BSP_RETURN_ERR(err);

    // Reset Touch Panel and LCD
    gpio_reset_pin(GPIO_NUM_23);
    pi4io_set_output(pi4ioe1, 4, false);  // LCD_RST = Low
    pi4io_set_output(pi4ioe1, 5, false);  // TP_RST = Low
    vTaskDelay(pdMS_TO_TICKS(100));
    pi4io_set_output(pi4ioe1, 4, true);   // LCD_RST = High
    pi4io_set_output(pi4ioe1, 5, true);   // TP_RST = High
    vTaskDelay(pdMS_TO_TICKS(100));

    if (i2c_master_probe(i2c0, 0x55, 10) == ESP_OK) {
        // Initialize ST7123 LCD
        err = st7123_lcd_init(&(st7123_lcd_config_t){
            .backlight_gpio = GPIO_NUM_22,
            .size = (bsp_size_t){ 720, 1280 },
            .pixel_format = BSP_PIXEL_FORMAT_RGB565,
            .fb_num = config->display.fb_num,
        }, &st7123_lcd);
        BSP_RETURN_ERR(err);
        frame_buffers = st7123_lcd_get_frame_buffers(st7123_lcd);

        // Initialize ST7123 Touch Panel
        err = st7123_touch_init(&(st7123_touch_config_t){
            .i2c_bus = i2c0,
            .size = (bsp_size_t){ 720, 1280 },
            .int_gpio = GPIO_NUM_23,
            .rst_gpio = GPIO_NUM_NC,
            .scl_speed_hz = 100000,
            .interrupt = config->touch.interrupt,
        }, &st7123_touch);
        BSP_RETURN_ERR(err);
    } else if (i2c_master_probe(i2c0, 0x14, 10) == ESP_OK) {
        // Initialize ILI9881C LCD
        err = ili9881c_lcd_init(&(ili9881c_lcd_config_t){
            .backlight_gpio = GPIO_NUM_22,
            .size = (bsp_size_t){ 720, 1280 },
            .pixel_format = BSP_PIXEL_FORMAT_RGB565,
            .fb_num = config->display.fb_num,
        }, &ili9881c);
        BSP_RETURN_ERR(err);
        frame_buffers = ili9881c_lcd_get_frame_buffers(ili9881c);

        // Initialize GT911 Touch Panel
        err = gt911_touch_init(&(gt911_touch_config_t){
            .i2c_bus = i2c0,
            .size = (bsp_size_t){ 720, 1280 },
            .int_gpio = GPIO_NUM_23,
            .rst_gpio = GPIO_NUM_NC,
            .scl_speed_hz = 100000,
            .interrupt = config->touch.interrupt,
        }, &gt911);
        BSP_RETURN_ERR(err);
    } else {
        return ESP_ERR_NOT_FOUND;
    }

    if (config->wifi.enable || config->bluetooth.enable) {
        // NVS (for WiFi & Bluetooth)
        err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            if ((err = nvs_flash_erase()) == ESP_OK) {
                err = nvs_flash_init();
            }
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize NVS flash");
            return err;
        }
    }

    // WiFi
    if (config->wifi.enable) {
        ESP_LOGE(TAG, "WiFi initialization not implemented yet!");
        assert(0);
        // ESP_ERROR_CHECK(esp_netif_init());
        // ESP_ERROR_CHECK(esp_event_loop_create_default());
        // esp_netif_create_default_wifi_ap();
        // wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        // ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    }

    // Bluetooth
    if (config->bluetooth.enable) {
#if defined(CONFIG_BT_BLUEDROID_ENABLED)
        /* initialize TRANSPORT first */
        hosted_hci_bluedroid_open();

        /* get HCI driver operations */
        esp_bluedroid_hci_driver_operations_t operations = {
            .send = hosted_hci_bluedroid_send,
            .check_send_available = hosted_hci_bluedroid_check_send_available,
            .register_host_callback = hosted_hci_bluedroid_register_host_callback,
        };
        esp_bluedroid_attach_hci_driver(&operations);
#elif defined(CONFIG_BT_NIMBLE_ENABLED)
        err = nimble_port_init();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize NimBLE");
            return err;
        }
#else
        ESP_LOGE(TAG, "Bluetooth Stack is not Enabled.");
#endif
    }

    return ESP_OK;
}

// MARK: Audio (ES8388 speaker DAC over I2S)
// Pins per the M5Stack Tab5 schematic; ES8388 sits on the shared I2C0 bus, and
// its power amp is gated by SPK_EN (PI4IOE1 P1), already enabled in bsp_tab5_init.
#define BSP_I2S_MCLK GPIO_NUM_30
#define BSP_I2S_BCLK GPIO_NUM_27
#define BSP_I2S_WS   GPIO_NUM_29
#define BSP_I2S_DOUT GPIO_NUM_26
#define BSP_I2S_DIN  GPIO_NUM_28

static i2s_chan_handle_t i2s_tx;

esp_codec_dev_handle_t bsp_tab5_audio_speaker_init(void) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    if (i2s_new_channel(&chan_cfg, &i2s_tx, NULL) != ESP_OK) return NULL;

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(48000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK, .bclk = BSP_I2S_BCLK, .ws = BSP_I2S_WS,
            .dout = BSP_I2S_DOUT, .din = BSP_I2S_DIN,
        },
    };
    if (i2s_channel_init_std_mode(i2s_tx, &std_cfg) != ESP_OK) return NULL;

    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&(audio_codec_i2s_cfg_t){
        .port = I2S_NUM_0,
        .tx_handle = i2s_tx,
    });
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&(audio_codec_i2c_cfg_t){
        .port = I2C0_PORT_NUM,
        .addr = ES8388_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c0,
    });
    const audio_codec_if_t *es8388 = es8388_codec_new(&(es8388_codec_cfg_t){
        .codec_mode  = ESP_CODEC_DEV_WORK_MODE_DAC,
        .master_mode = false,   /* P4 I2S is master, codec is slave */
        .ctrl_if     = ctrl_if,
        .pa_pin      = -1,      /* PA handled by SPK_EN, not the codec */
    });
    if (!data_if || !ctrl_if || !es8388) return NULL;

    return esp_codec_dev_new(&(esp_codec_dev_cfg_t){
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = es8388,
        .data_if  = data_if,
    });
}

// MARK: USB host power
void bsp_tab5_set_usb_host_power(bool on) {
    // PI4IOE2 (0x44) pin 3 = USB5V_EN — gates 5V VBUS on the USB-A host port.
    pi4io_set_output(pi4ioe2, 3, on);
}

// MARK: Display
void bsp_tab5_display_set_brightness(int brightness) {
    if (ili9881c) ili9881c_lcd_set_brightness(ili9881c, brightness);
    if (st7123_lcd) st7123_lcd_set_brightness(st7123_lcd, brightness);
}
void *bsp_tab5_display_get_frame_buffer(int fb_index) {
    return frame_buffers[fb_index];
}
void bsp_tab5_display_flush(int fb_index) {
    if (ili9881c) ili9881c_lcd_flush(ili9881c, fb_index);
    if (st7123_lcd) st7123_lcd_flush(st7123_lcd, fb_index);
}

// MARK: Touch Panel
int bsp_tab5_touch_read(esp_lcd_touch_point_data_t *points, uint8_t max_points) {
    if (gt911) return gt911_touch_read(gt911, points, max_points);
    if (st7123_touch) return st7123_touch_read(st7123_touch, points, max_points);
    return 0;
}
void bsp_tab5_touch_wait_interrupt(void) {
    if (gt911) gt911_touch_wait_interrupt(gt911);
    if (st7123_touch) st7123_touch_wait_interrupt(st7123_touch);
}
