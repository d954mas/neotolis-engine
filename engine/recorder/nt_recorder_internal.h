#ifndef NT_RECORDER_INTERNAL_H
#define NT_RECORDER_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#define NT_RECORDER_AUDIO_RATE 48000U

/* Platform backend. open returns false, after logging why, when it cannot record. video and audio
   return false when the frame or samples cannot be encoded; the core then ends the recording and still
   calls close: web writes no file, native keeps the fragments already on disk (none without a frame). */
bool nt_recorder_platform_open(const char *name, uint32_t width, uint32_t height, uint32_t fps);
/* Encodes the current framebuffer as output frames first .. first + count - 1 (pts index / fps).
   Windows encodes it a frame or two later, from a read the GPU finishes meanwhile. */
bool nt_recorder_platform_video(uint32_t first, uint32_t count);
/* Interleaved stereo samples at track sample at (pts at / 48000), continuing the track; it may run
   ahead of the video. */
bool nt_recorder_platform_audio(uint64_t at, const float *stereo, uint32_t frames);
/* Finishes the file with the frames the backend took, which can be fewer than those passed to
   platform_video when a write failed (Windows writes its last reads here); false when one did. */
bool nt_recorder_platform_close(void);

#endif // NT_RECORDER_INTERNAL_H
