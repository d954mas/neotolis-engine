#ifndef NT_RECORDER_H
#define NT_RECORDER_H

#include <stdbool.h>
#include <stdint.h>

/* Dev-only video recorder: an MP4 (H.264 + AAC 48 kHz stereo) of the presented frames at a constant
   frame rate. Output frame i shows the presented frame nearest to t0 + (i + 1) / fps, where t0 is the
   start time, so MANUAL lockstep at step_dt = 1 / fps records exactly one frame per step. File time 0
   is t0 + 1 / fps for video and audio alike.
   The size is the window framebuffer at start, rounded down to even; a framebuffer resize or an
   encoder error ends the recording with a WARN. An audio track is always written: pushed samples are
   padded with silence or trimmed to the recorded time.
   Web: the bytes stay in tab memory; once the encoders flush after stop, window event 'nt-recorder'
   (cancelable, detail {name, blob, frames, error}) is dispatched and, unless a listener cancels it,
   the browser downloads blob as name. */

/* name: output name (see above), copied; fps 1..120. t: the caller's clock, the same one passed to
   nt_recorder_frame. Returns false (logged) when the platform has no encoder or the output cannot be
   opened. Asserts while a recording is active. */
bool nt_recorder_start(const char *name, uint32_t fps, double t);

/* Call after nt_gfx_end_frame and before nt_window_swap_buffers, once per presented frame. dt is the
   advance of this frame. No-op while idle. */
void nt_recorder_frame(double t, double dt);

/* Interleaved stereo float samples at 48 kHz for the time since the previous frame call. Main thread
   only. No-op while idle. */
void nt_recorder_push_audio(const float *stereo48k, uint32_t frames);

/* Ends the recording and returns the number of frames written; 0 while idle. */
uint32_t nt_recorder_stop(void);

bool nt_recorder_active(void);

#endif // NT_RECORDER_H
