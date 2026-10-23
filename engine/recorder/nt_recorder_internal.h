#ifndef NT_RECORDER_INTERNAL_H
#define NT_RECORDER_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#define NT_RECORDER_AUDIO_RATE 48000U

/* Platform backend. open returns false, after logging why, when it cannot record. video and audio
   return false when a write to the encoder fails; the core then ends the recording and still calls
   close: web writes no file, native keeps the fragments already on disk (none without a frame). */
bool nt_recorder_platform_open(const char *name, uint32_t width, uint32_t height, uint32_t fps);
/* Records the current framebuffer as output frames first .. first + count - 1 (pts index / fps).
   Windows queues a read of it and encodes that read when its ring of three is full, or in close. */
bool nt_recorder_platform_video(uint32_t first, uint32_t count);
/* Interleaved stereo samples at track sample at (pts at / 48000), continuing the track; it may run
   ahead of the video. */
bool nt_recorder_platform_audio(uint64_t at, const float *stereo, uint32_t frames);
/* Finishes the file; Windows first encodes the reads still queued. False when a write of the
   recording failed; web returns true and reports a failed finish in its event. */
bool nt_recorder_platform_close(void);

#endif // NT_RECORDER_INTERNAL_H
