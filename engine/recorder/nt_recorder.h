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
   frame's advance. A resize or an encoder failure ends the recording (WARN). No-op while idle. */
void nt_recorder_frame(double t, double dt);

/* Interleaved stereo 48 kHz samples that start at t_start on the clock passed to nt_recorder_frame;
   a block within 1024 samples of the queued audio's end joins it. Main thread. No-op while idle. */
void nt_recorder_push_audio(const float *stereo48k, uint32_t frames, double t_start);

/* Returns the frames written; 0 while idle or when the final audio write failed. */
uint32_t nt_recorder_stop(void);

bool nt_recorder_active(void);

#endif // NT_RECORDER_H
