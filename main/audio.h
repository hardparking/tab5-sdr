/*
 * audio.h — Tab5 speaker output for demodulated audio.
 *
 * Decouples the (real-time, must-not-block) demod path from the (blocking) I2S
 * codec writes via a StreamBuffer drained by a dedicated task.
 */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_SAMPLE_RATE 48000

/* Bring up the ES8388 codec and start the output task. */
void audio_init(void);

/* Queue n mono samples for playback (duplicated to L/R). Non-blocking: if the
 * buffer is full the excess is dropped rather than stalling the caller. */
void audio_play(const int16_t *mono, int n);

/* Output volume, 0..100. */
void audio_set_volume(int vol);

#ifdef __cplusplus
}
#endif
