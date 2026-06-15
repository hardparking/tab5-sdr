/*
 * ui.h — landscape touch UI (spectrum/waterfall + tune/volume/squelch).
 *
 * Renders into a 1280x720 landscape buffer and uses the P4 PPA to rotate it 90°
 * onto the portrait panel. Owns the render+touch task.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Start the UI task. Call after sdr_stream_start() + dsp_start(). */
void ui_start(void);

#ifdef __cplusplus
}
#endif
