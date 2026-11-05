#ifndef NT_RECORDER_INTERNAL_H
#define NT_RECORDER_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#define NT_RECORDER_AUDIO_RATE 48000U

/* ready is false while codec selection is pending. open logs and returns false when it cannot record.
   Failed video/audio writes return false; the core still calls close. Web emits no file on failure;
   Windows keeps completed fragments if any frame was written. */
bool nt_recorder_platform_ready(void);
bool nt_recorder_platform_open(const char *name, uint32_t width, uint32_t height, uint32_t fps, uint32_t bitrate_bps);
/* Records the current framebuffer as output frames first .. first + count - 1 (pts index / fps).
   Windows encodes the oldest read before queuing a fourth pending read; close drains the rest unless a write failed. */
bool nt_recorder_platform_video(uint32_t first, uint32_t count);
/* Interleaved stereo samples at track sample at (pts at / 48000), continuing the track; it may run
   ahead of the video. */
bool nt_recorder_platform_audio(uint64_t at, const float *stereo, uint32_t frames);
/* Finishes the file; Windows first encodes queued reads unless a write failed. False when a write of the
   recording failed; web returns true and reports a failed finish in its event. */
bool nt_recorder_platform_close(void);

#endif // NT_RECORDER_INTERNAL_H
