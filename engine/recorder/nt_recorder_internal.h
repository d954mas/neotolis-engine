#ifndef NT_RECORDER_INTERNAL_H
#define NT_RECORDER_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#define NT_RECORDER_AUDIO_RATE 48000U

/* Platform backend. All return false on an encoder failure; the core then ends the recording. */
bool nt_recorder_platform_open(const char *name, uint32_t width, uint32_t height, uint32_t fps);
/* Encodes the current framebuffer as output frame index (pts index / fps). */
bool nt_recorder_platform_video(uint32_t index);
/* Interleaved stereo samples continuing the audio track. */
bool nt_recorder_platform_audio(const float *stereo, uint32_t frames);
/* Finishes the file. frames is the video frame count written. */
void nt_recorder_platform_close(uint32_t frames);

#endif // NT_RECORDER_INTERNAL_H
