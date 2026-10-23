#ifndef NT_RECORDER_H
#define NT_RECORDER_H

#include <stdbool.h>
#include <stdint.h>

/* Dev-only MP4 recorder (H.264 + AAC or Opus, 48 kHz stereo) of the presented frames on the caller's
   clock; frame selection, the audio stream and the web 'nt-recorder' event: docs/spec/debug/recorder.md. */

/* name: web — download name, copied; Windows — UTF-8 file path, overwritten. fps 1..120. Returns false
   (logged) when a framebuffer side is under 2 px or the backend refuses (no encoder, size rejected,
   output not opened; web: codec probe running, past H.264 level 6.2). Asserts while recording. */
bool nt_recorder_start(const char *name, uint32_t fps);

/* After nt_gfx_end_frame and before nt_window_swap_buffers, once per presented frame; dt is the advance
   that produced this frame. The first call sets the recording's origin: t - dt, or where audio pushed
   before it begins. A resize, over two seconds of due frames or an encoder failure ends the recording
   (WARN). Windows blocks here while the encoder is behind. No-op while idle. */
void nt_recorder_frame(double t, double dt);

/* The next interleaved stereo 48 kHz samples of the game's mix, pushed before nt_recorder_frame for
   the same step. Until the first push the track is silence up to each frame's t; from then on the stream
   continues sample by sample, so push every step's samples, silence included. A stream that drifts
   from the clock by over 100 ms is reported once (WARN). Main thread. No-op while idle. */
void nt_recorder_push_audio(const float *stereo48k, uint32_t frames);

/* Returns the frames sent to the encoder; 0 while idle, after a self-end or when the final audio write
   failed (web: the 'nt-recorder' event says whether a file exists). Windows destroys gfx buffers here:
   not while a frame is being drawn (the gfx frame rule). */
uint32_t nt_recorder_stop(void);

bool nt_recorder_active(void);

#endif // NT_RECORDER_H
