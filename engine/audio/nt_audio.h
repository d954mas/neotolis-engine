#ifndef NT_AUDIO_H
#define NT_AUDIO_H

#include "audio/nt_audio_types.h"

/* Cross-platform audio: music + sound effects on native and web.
   The public API carries zero platform types (spec §22.2) — only handles,
   floats, and bools — so game code is identical across builds. */

/* ---- Lifecycle ---- */

void nt_audio_init(void);
void nt_audio_shutdown(void);
void nt_audio_update(void); /* once per frame: reaps finished one-shot voices */
nt_audio_state_t nt_audio_get_state(void);

/* Call on a confirmed user gesture (web autoplay policy). No-op on native. */
void nt_audio_try_resume(void);

/* ---- Clips ----
   Decodes an encoded WAV or MP3 blob. The clip keeps its own copy of the bytes,
   so the caller may free `data` immediately. Returns NT_AUDIO_CLIP_INVALID if
   the data is not decodable or the pool is full. */
nt_audio_clip_t nt_audio_clip_create(const uint8_t *data, uint32_t size);
void nt_audio_clip_destroy(nt_audio_clip_t clip);
nt_audio_clip_state_t nt_audio_clip_get_state(nt_audio_clip_t clip);

/* ---- Playback ----
   Returns NT_AUDIO_VOICE_INVALID if audio is suspended/failed, the clip is
   invalid, or every voice is busy with a looping sound (spec §22.5). */
nt_audio_voice_t nt_audio_play(nt_audio_clip_t clip, float volume, float pitch, bool loop);
void nt_audio_stop(nt_audio_voice_t voice);
void nt_audio_stop_all(void);

/* ---- Voice control ---- */

void nt_audio_set_volume(nt_audio_voice_t voice, float volume);
void nt_audio_set_pitch(nt_audio_voice_t voice, float pitch);
bool nt_audio_is_playing(nt_audio_voice_t voice);

/* ---- Global ---- */

void nt_audio_set_master_volume(float volume);
float nt_audio_get_master_volume(void);

#endif /* NT_AUDIO_H */
