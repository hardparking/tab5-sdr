/*
 * sdr.h — RTL-SDR device management, IQ streaming, and a PSRAM ring buffer.
 *
 * Ownership of the rtlsdr device handle and the USB-host streaming task lives
 * here so main.c can stay focused on board/UI concerns.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Default radio configuration (FM broadcast band). 1.152 Msps decimates cleanly
 * by 4 then 6 to 48 kHz audio (1.152M/4 = 288k, /6 = 48k). */
#define SDR_DEFAULT_SAMPLE_RATE 1152000u
#define SDR_DEFAULT_CENTER_FREQ 100000000u /* 100.0 MHz */

typedef struct {
    uint64_t produced_bytes; /* total IQ bytes pushed by the USB callback   */
    uint64_t dropped_bytes;  /* bytes discarded because the ring was full   */
    uint32_t sample_rate;    /* configured RTL2832 sample rate (Hz)         */
    uint32_t center_freq;    /* configured tuner centre frequency (Hz)      */
    const char *tuner;       /* tuner name string                           */
} sdr_stats_t;

/* Open the first RTL-SDR on the USB bus. Blocks/retries until one appears.
 * Returns true once opened. */
bool sdr_open(void);

/* Configure rate/freq/gain and start the async IQ stream on its own task.
 * Must be called after sdr_open(). Returns true on success. */
bool sdr_stream_start(uint32_t center_freq, uint32_t sample_rate);

/* Retune the open device on the fly (safe to call while streaming). */
int sdr_set_center_freq(uint32_t freq);

/* Snapshot the current streaming statistics. */
void sdr_get_stats(sdr_stats_t *out);

/* Consume up to `max` IQ bytes from the ring buffer into `dst`.
 * Returns the number of bytes copied (0 if empty). Single consumer only. */
size_t sdr_read(uint8_t *dst, size_t max);

#ifdef __cplusplus
}
#endif
