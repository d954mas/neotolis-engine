#include "audio/nt_audio.h"

/* No-op audio backend for headless builds and tests: links the full API but
   never opens a device. All plays return invalid handles; nothing crashes. */

void nt_audio_init(void) {}
void nt_audio_shutdown(void) {}
void nt_audio_update(void) {}
nt_audio_state_t nt_audio_get_state(void) { return NT_AUDIO_RUNNING; }
void nt_audio_try_resume(void) {}

nt_audio_clip_t nt_audio_clip_create(const uint8_t *data, uint32_t size) {
    (void)data;
    (void)size;
    return NT_AUDIO_CLIP_INVALID;
}
void nt_audio_clip_destroy(nt_audio_clip_t clip) { (void)clip; }
nt_audio_clip_state_t nt_audio_clip_get_state(nt_audio_clip_t clip) {
    (void)clip;
    return NT_AUDIO_CLIP_NONE;
}

nt_audio_voice_t nt_audio_play(nt_audio_clip_t clip, float volume, float pitch, bool loop) {
    (void)clip;
    (void)volume;
    (void)pitch;
    (void)loop;
    return NT_AUDIO_VOICE_INVALID;
}
void nt_audio_stop(nt_audio_voice_t voice) { (void)voice; }
void nt_audio_stop_all(void) {}

void nt_audio_set_volume(nt_audio_voice_t voice, float volume) {
    (void)voice;
    (void)volume;
}
void nt_audio_set_pitch(nt_audio_voice_t voice, float pitch) {
    (void)voice;
    (void)pitch;
}
bool nt_audio_is_playing(nt_audio_voice_t voice) {
    (void)voice;
    return false;
}

void nt_audio_set_master_volume(float volume) { (void)volume; }
float nt_audio_get_master_volume(void) { return 1.0F; }
