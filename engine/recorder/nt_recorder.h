#ifndef NT_RECORDER_H
#define NT_RECORDER_H

#include <stdbool.h>
#include <stdint.h>

/* Dev-only MP4 recorder (H.264 + AAC or Opus, 48 kHz stereo) of the presented frames on the caller's
   clock; frame selection, the audio stream and the web 'nt-recorder' event: docs/spec/debug/recorder.md. */

/* Whether a start can pick its codecs now: web probes the browser's encoders once at load and is not
   ready until that probe settles; Windows is always ready. Ready does not promise an encoder exists. */
bool nt_recorder_ready(void);

/* name: web — download name, copied; Windows — UTF-8 file path, overwritten. fps 1..120. Returns false
   (logged) when a framebuffer side is under 2 px or the backend refuses (not ready, no encoder, size
   rejected, output not opened; web: past H.264 level 6.2). Asserts while recording. */
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

typedef struct {
    uint32_t frames;  /* frames sent to the encoder */
    double max_drift; /* seconds; the signed audio-minus-clock gap of largest magnitude after the first push */
} nt_recorder_stats_t;

/* Returns the recording's stats; all zero while idle, after a self-end or when a final write failed
   (audio, or on Windows the last frames; web: the 'nt-recorder' event says whether a file exists). */
nt_recorder_stats_t nt_recorder_stop(void);

bool nt_recorder_active(void);

#endif // NT_RECORDER_H
