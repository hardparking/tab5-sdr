/*
 * sdr.c — RTL-SDR streaming over the ESP-IDF USB host stack.
 *
 * The librtlsdr async API delivers 8-bit interleaved IQ in a callback that runs
 * in the context of the task driving rtlsdr_read_async() (which blocks pumping
 * USB events). We run that task pinned to core 1 and have the callback push raw
 * IQ into a single-producer/single-consumer ring buffer in PSRAM; the DSP/UI
 * side (core 0) drains it via sdr_read().
 */
#include "sdr.h"

#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "rtl-sdr.h"
#include "libusb.h" /* usbhost_begin() */

static const char *TAG = "sdr";

/* USB transfer geometry handed to rtlsdr_read_async(). buf_len must be a
 * multiple of 512 (USB packet size); 16 KiB ≈ 8 ms at 1.024 Msps. */
#define SDR_XFER_NUM 8
#define SDR_XFER_LEN 16384

/* ~0.5 s of IQ at 1.024 Msps. Power of two so we can mask instead of modulo. */
#define RING_SIZE (1u << 20) /* 1 MiB */
#define RING_MASK (RING_SIZE - 1)

/* ---- single-producer / single-consumer byte ring in PSRAM ---------------- */
static uint8_t *s_ring;
static volatile uint32_t s_head; /* producer write offset (free-running) */
static volatile uint32_t s_tail; /* consumer read offset  (free-running) */

static rtlsdr_dev_t *s_dev;
static volatile uint64_t s_produced;
static volatile uint64_t s_dropped;
static uint32_t s_sample_rate;
static uint32_t s_center_freq;
static const char *s_tuner = "unknown";

static const char *tuner_name(enum rtlsdr_tuner t)
{
    switch (t) {
    case RTLSDR_TUNER_E4000:  return "Elonics E4000";
    case RTLSDR_TUNER_FC0012: return "Fitipower FC0012";
    case RTLSDR_TUNER_FC0013: return "Fitipower FC0013";
    case RTLSDR_TUNER_FC2580: return "FCI FC2580";
    case RTLSDR_TUNER_R820T:  return "Rafael Micro R820T";
    case RTLSDR_TUNER_R828D:  return "Rafael Micro R828D";
    default:                  return "unknown";
    }
}

bool sdr_open(void)
{
    usbhost_begin();
    vTaskDelay(pdMS_TO_TICKS(1000)); /* let the host enumerate */

    for (int attempt = 1;; attempt++) {
        uint32_t count = rtlsdr_get_device_count();
        if (count > 0 && rtlsdr_open(&s_dev, 0) == 0 && s_dev) {
            s_tuner = tuner_name(rtlsdr_get_tuner_type(s_dev));
            ESP_LOGI(TAG, "opened RTL-SDR — tuner: %s", s_tuner);
            return true;
        }
        ESP_LOGI(TAG, "attempt %d: no RTL-SDR yet (count=%" PRIu32 ")", attempt, count);
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}

/* librtlsdr async callback — runs on the streaming task. Push IQ into the ring,
 * dropping whole buffers if the consumer has fallen behind. */
static void on_iq(unsigned char *buf, uint32_t len, void *ctx)
{
    (void)ctx;
    uint32_t head = s_head;
    uint32_t space = RING_SIZE - (head - s_tail);
    if (len > space) {
        s_dropped += len;
        return;
    }
    uint32_t first = RING_SIZE - (head & RING_MASK);
    if (first >= len) {
        memcpy(s_ring + (head & RING_MASK), buf, len);
    } else {
        memcpy(s_ring + (head & RING_MASK), buf, first);
        memcpy(s_ring, buf + first, len - first);
    }
    s_head = head + len;      /* publish after the data is in place */
    s_produced += len;
}

static void stream_task(void *arg)
{
    (void)arg;
    /* rtlsdr_read_async blocks here pumping USB transfers until cancelled. */
    int r = rtlsdr_read_async(s_dev, on_iq, NULL, SDR_XFER_NUM, SDR_XFER_LEN);
    ESP_LOGW(TAG, "rtlsdr_read_async returned %d (stream ended)", r);
    vTaskDelete(NULL);
}

bool sdr_stream_start(uint32_t center_freq, uint32_t sample_rate)
{
    if (!s_dev) {
        ESP_LOGE(TAG, "sdr_stream_start before sdr_open");
        return false;
    }

    s_ring = heap_caps_malloc(RING_SIZE, MALLOC_CAP_SPIRAM);
    if (!s_ring) {
        ESP_LOGE(TAG, "failed to allocate %u-byte ring in PSRAM", RING_SIZE);
        return false;
    }
    s_head = s_tail = 0;
    s_produced = s_dropped = 0;

    if (rtlsdr_set_sample_rate(s_dev, sample_rate) < 0)
        ESP_LOGW(TAG, "set_sample_rate failed");
    if (rtlsdr_set_center_freq(s_dev, center_freq) < 0)
        ESP_LOGW(TAG, "set_center_freq failed");
    rtlsdr_set_tuner_gain_mode(s_dev, 0); /* automatic gain */
    if (rtlsdr_reset_buffer(s_dev) < 0)   /* mandatory before reading */
        ESP_LOGW(TAG, "reset_buffer failed");

    s_sample_rate = sample_rate;
    s_center_freq = center_freq;
    ESP_LOGI(TAG, "streaming: %.3f MHz @ %" PRIu32 " Hz", center_freq / 1e6, sample_rate);

    /* Pin to core 1 so the USB/IQ path doesn't contend with DSP+LVGL on core 0. */
    BaseType_t ok = xTaskCreatePinnedToCore(stream_task, "sdr_stream", 8192, NULL, 12, NULL, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to create stream task");
        return false;
    }
    return true;
}

int sdr_set_center_freq(uint32_t freq)
{
    if (!s_dev) return -1;
    int r = rtlsdr_set_center_freq(s_dev, freq);
    if (r >= 0) s_center_freq = freq;
    return r;
}

void sdr_get_stats(sdr_stats_t *out)
{
    out->produced_bytes = s_produced;
    out->dropped_bytes = s_dropped;
    out->sample_rate = s_sample_rate;
    out->center_freq = s_center_freq;
    out->tuner = s_tuner;
}

size_t sdr_read(uint8_t *dst, size_t max)
{
    uint32_t tail = s_tail;
    uint32_t avail = s_head - tail;
    if (avail == 0) return 0;
    if (max > avail) max = avail;

    uint32_t first = RING_SIZE - (tail & RING_MASK);
    if (first >= max) {
        memcpy(dst, s_ring + (tail & RING_MASK), max);
    } else {
        memcpy(dst, s_ring + (tail & RING_MASK), first);
        memcpy(dst + first, s_ring, max - first);
    }
    s_tail = tail + max;
    return max;
}
