/*
 * Tab5-SDR — standalone RTL-SDR receiver on the M5Stack Tab5 (ESP32-P4).
 *
 * Stage 1: BSP bring-up + RGB565 test pattern (panel proof).
 * Stage 2: USB host power + librtlsdr enumeration (see sdr.c).
 * Stage 3: stream IQ into a PSRAM ring buffer (see sdr.c).
 * Stage 4: FFT spectrum + waterfall (see dsp.c).
 *
 * app_main handles boot/enumeration with solid-colour status, then hands the
 * screen to the DSP renderer and just emits a periodic serial heartbeat.
 */
#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "bsp_tab5.h"
#include "sdr.h"
#include "dsp.h"
#include "audio.h"
#include "ui.h"

static const char *TAG = "tab5-sdr";

/* Tab5 panel: 720x1280, RGB565 (see bsp_tab5.c). */
#define PANEL_W 720
#define PANEL_H 1280

#define RGB565(r, g, b) \
    ((uint16_t)(((((r) >> 3) & 0x1F) << 11) | ((((g) >> 2) & 0x3F) << 5) | (((b) >> 3) & 0x1F)))

static uint16_t *fb(void) { return (uint16_t *)bsp_tab5_display_get_frame_buffer(0); }

static void draw_test_pattern(void)
{
    uint16_t *f = fb();
    if (!f) {
        ESP_LOGE(TAG, "no framebuffer");
        return;
    }
    static const uint16_t bars[8] = {
        RGB565(0, 0, 0),     RGB565(0, 0, 255),   RGB565(0, 255, 0),   RGB565(0, 255, 255),
        RGB565(255, 0, 0),   RGB565(255, 0, 255), RGB565(255, 255, 0), RGB565(255, 255, 255),
    };
    for (int y = 0; y < PANEL_H; y++) {
        uint16_t *row = f + (size_t)y * PANEL_W;
        for (int x = 0; x < PANEL_W; x++) row[x] = bars[(x * 8) / PANEL_W];
    }
    bsp_tab5_display_flush(0);
}

static void fill_screen(uint16_t color)
{
    uint16_t *f = fb();
    if (!f) return;
    for (size_t i = 0; i < (size_t)PANEL_W * PANEL_H; i++) f[i] = color;
    bsp_tab5_display_flush(0);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Tab5-SDR boot — free heap %" PRIu32 ", PSRAM %u",
             esp_get_free_heap_size(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    ESP_ERROR_CHECK(bsp_tab5_init(&(bsp_tab5_config_t){
        .display.fb_num = 2, /* double buffer: PPA writes one while the other scans out */
        .touch.interrupt = true,
        .wifi.enable = false,
        .bluetooth.enable = false,
    }));
    bsp_tab5_display_set_brightness(80);
    draw_test_pattern();

    /* USB-A 5V VBUS is off by default — power the dongle, then enumerate. */
    bsp_tab5_set_usb_host_power(true);
    vTaskDelay(pdMS_TO_TICKS(300));

    fill_screen(RGB565(0, 0, 200)); /* blue: searching */
    sdr_open();                     /* blocks until a dongle is opened */
    fill_screen(RGB565(0, 120, 0)); /* green: opened */

    if (!sdr_stream_start(SDR_DEFAULT_CENTER_FREQ, SDR_DEFAULT_SAMPLE_RATE)) {
        fill_screen(RGB565(220, 0, 0)); /* red: stream failed */
        return;
    }

    /* Speaker up, then demod + landscape UI take over the screen. */
    audio_init();
    dsp_start();
    ui_start();

    /* Heartbeat: confirm the stream stays drop-free under the DSP load. */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        sdr_stats_t st;
        sdr_get_stats(&st);
        ESP_LOGI(TAG, "%.3f MHz, dropped=%llu bytes total",
                 st.center_freq / 1e6, (unsigned long long)st.dropped_bytes);
    }
}
