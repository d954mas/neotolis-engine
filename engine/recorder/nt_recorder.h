#ifndef NT_RECORDER_H
#define NT_RECORDER_H

#include <stdbool.h>
#include <stdint.h>

/* Dev-only MP4 recorder (H.264 + AAC or Opus, 48 kHz stereo) of the presented frames on the caller's
   clock; frame selection, the audio stream and the web 'nt-recorder' event: docs/spec/debug/recorder.md. */

/* Whether a start can pick its codecs now: web probes the browser's encoders once at load and is not
   ready until that probe settles; Windows is always ready. Ready does not promise an encoder exists. */
bool nt_recorder_ready(void);

/* Non-empty name: web download name (copied), Windows UTF-8 path (overwritten). Requires idle, fps 1..120 and bitrate_bps > 0; violations assert.
   bitrate_bps is target video bits/s; VBR output varies. Returns false (logged) for a framebuffer side under 2 px or backend rejection
   (not ready, no encoder, unsupported size/bitrate or output failure; web: past H.264 level 6.2). */
bool nt_recorder_start(const char *name, uint32_t fps, uint32_t bitrate_bps);

/* Once per presented frame, after nt_gfx_end_frame and before nt_window_swap_buffers; dt is the advance that produced it.
   The first call sets t0 to t - dt, or the start of audio already pushed. Resize, over two seconds of due frames or encoder failure ends it (WARN).
   Windows can wait for the GPU or encoder. No-op while idle. */
void nt_recorder_frame(double t, double dt);

/* Push each step's interleaved stereo 48 kHz float samples before its frame call, on the main thread; include silence once pushing starts.
   Until the first push, frame calls fill silence to t. Later samples continue the stream; drift over 100 ms warns once. No-op while idle. */
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
