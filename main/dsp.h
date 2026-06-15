/*
 * dsp.h — IQ processing: WBFM demod (→ audio) + FFT spectrum (→ UI).
 *
 * Owns the sole ring consumer task. Rendering lives in ui.c, which reads the
 * spectrum snapshot exposed here.
 */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start the IQ-processing task. Call after sdr_stream_start(). */
void dsp_start(void);

/* Demod mode: 0 = wideband FM (broadcast), 1 = narrowband FM (NOAA / public
 * safety voice). Changes the decimation chain + audio shaping. */
void dsp_set_mode(int nfm);
int dsp_get_mode(void);

/* Number of spectrum bins exposed (fftshifted: index 0 = lowest freq). */
int dsp_spectrum_bins(void);

/* Pointer to the latest spectrum frame (dB per bin), or NULL if none yet.
 * Valid until the next frame; read-only. */
const float *dsp_spectrum(void);

/* Squelch: mute audio when the signal level falls below `db`. Use a very low
 * value (e.g. -200) to disable. */
void dsp_set_squelch_db(float db);
float dsp_get_squelch_db(void);

/* Current smoothed signal level (dB, same scale as the squelch threshold). */
float dsp_signal_db(void);

#ifdef __cplusplus
}
#endif
