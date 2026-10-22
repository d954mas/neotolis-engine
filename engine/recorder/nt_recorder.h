#ifndef NT_RECORDER_H
#define NT_RECORDER_H

#include <stdbool.h>
#include <stdint.h>

/* Dev-only MP4 recorder (H.264 + AAC or Opus, 48 kHz stereo) of the presented frames on the caller's
   clock; frame selection, audio sync and the web 'nt-recorder' event: docs/spec/debug/recorder.md. */

/* name: web — download name, copied; fps 1..120. Returns false (logged) when the framebuffer is under
   2x2, there is no encoder, or the output cannot be opened. Asserts while recording. */
bool nt_recorder_start(const char *name, uint32_t fps, double t);

/* After nt_gfx_end_frame and before nt_window_swap_buffers, once per presented frame; dt is this
   frame's advance. A resize, over two seconds of due frames or an encoder failure ends the recording
   (WARN). No-op while idle. */
void nt_recorder_frame(double t, double dt);

/* The next interleaved stereo 48 kHz samples of the game's mix: sample n of the stream plays at t0 +
   n / 48000. Push every step's samples from the first step after start, silence included, or none at
   all; a stream that drifts from the clock by over 100 ms is reported once (WARN). Main thread. No-op
   while idle. */
void nt_recorder_push_audio(const float *stereo48k, uint32_t frames);

/* Returns the frames written; 0 while idle or when the final audio write failed. */
uint32_t nt_recorder_stop(void);

bool nt_recorder_active(void);

#endif // NT_RECORDER_H
