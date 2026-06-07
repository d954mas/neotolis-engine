#include "audio/nt_audio.h"
#include "log/nt_log.h"

#include "miniaudio.h"

#include <stdlib.h>
#include <string.h>

/* Spec deviation (§22.1): the spec splits audio into a web Web-Audio JS bridge
   plus a native miniaudio backend. We use miniaudio on BOTH platforms — its
   built-in Web Audio backend (ScriptProcessorNode) covers the web — so there is
   a single implementation file. The public API (nt_audio.h) is unchanged. */

/* Each play creates its own ma_decoder over the clip's encoded bytes, giving
   every voice an independent read cursor (simultaneous plays of one clip work).
   ma_decoder_init_memory does NOT copy, so a clip's bytes must outlive its
   voices — clips are expected to live for the session and are freed at shutdown. */

// #region state
typedef struct {
    uint8_t *data; /* owned copy of encoded bytes */
    size_t size;
    nt_audio_clip_state_t state;
    bool used;
} nt_audio_clip_slot_t;

typedef struct {
    ma_sound sound;
    ma_decoder decoder;
    uint32_t order; /* play sequence number; oldest = lowest, for eviction */
    uint16_t generation;
    bool active;
    bool looping;
    bool sound_inited;
    bool decoder_inited;
} nt_audio_voice_slot_t;

static ma_engine s_engine;
static bool s_engine_inited;
static nt_audio_state_t s_state = NT_AUDIO_FAILED;
static float s_master_volume = 1.0F;
static uint32_t s_play_order; /* monotonic counter for eviction ordering */

static nt_audio_clip_slot_t s_clips[NT_MAX_AUDIO_CLIPS];
static nt_audio_voice_slot_t s_voices[NT_MAX_AUDIO_VOICES];
// #endregion

// #region voices
static void voice_free(int i) {
    nt_audio_voice_slot_t *v = &s_voices[i];
    if (v->sound_inited) {
        ma_sound_uninit(&v->sound);
        v->sound_inited = false;
    }
    if (v->decoder_inited) {
        ma_decoder_uninit(&v->decoder);
        v->decoder_inited = false;
    }
    v->active = false;
    v->looping = false;
    v->generation++; /* invalidate any handle still pointing at this slot */
}

/* Free slot if any, else evict the oldest non-looping voice (spec §22.5).
   Returns -1 when every voice is looping. */
static int voice_acquire(void) {
    for (int i = 0; i < NT_MAX_AUDIO_VOICES; ++i) {
        if (!s_voices[i].active) {
            return i;
        }
    }
    int oldest = -1;
    uint32_t oldest_order = 0xFFFFFFFFU;
    for (int i = 0; i < NT_MAX_AUDIO_VOICES; ++i) {
        if (!s_voices[i].looping && s_voices[i].order < oldest_order) {
            oldest_order = s_voices[i].order;
            oldest = i;
        }
    }
    if (oldest >= 0) {
        voice_free(oldest);
    }
    return oldest;
}

static nt_audio_voice_slot_t *voice_resolve(nt_audio_voice_t h) {
    if (h.index >= NT_MAX_AUDIO_VOICES) {
        return NULL;
    }
    nt_audio_voice_slot_t *v = &s_voices[h.index];
    if (!v->active || v->generation != h.generation) {
        return NULL;
    }
    return v;
}
// #endregion

// #region lifecycle
void nt_audio_init(void) {
    ma_engine_config cfg = ma_engine_config_init();
    ma_result r = ma_engine_init(&cfg, &s_engine);
    if (r != MA_SUCCESS) {
        s_state = NT_AUDIO_FAILED;
        NT_LOG_ERROR("init failed: ma_engine_init returned %d", (int)r);
        return;
    }
    s_engine_inited = true;
    ma_engine_set_volume(&s_engine, s_master_volume);
#ifdef __EMSCRIPTEN__
    /* Browsers block audio until a user gesture; miniaudio auto-resumes the
       AudioContext on the first click/touch, we flip RUNNING in try_resume. */
    s_state = NT_AUDIO_SUSPENDED;
#else
    s_state = NT_AUDIO_RUNNING;
#endif
    NT_LOG_INFO("initialized");
}

void nt_audio_shutdown(void) {
    if (!s_engine_inited) {
        return;
    }
    nt_audio_stop_all();
    for (int i = 0; i < NT_MAX_AUDIO_CLIPS; ++i) {
        free(s_clips[i].data);
        s_clips[i] = (nt_audio_clip_slot_t){0};
    }
    ma_engine_uninit(&s_engine);
    s_engine_inited = false;
    s_state = NT_AUDIO_FAILED;
}

void nt_audio_update(void) {
    if (!s_engine_inited) {
        return;
    }
    for (int i = 0; i < NT_MAX_AUDIO_VOICES; ++i) {
        nt_audio_voice_slot_t *v = &s_voices[i];
        if (v->active && !v->looping && v->sound_inited && ma_sound_at_end(&v->sound)) {
            voice_free(i);
        }
    }
}

nt_audio_state_t nt_audio_get_state(void) { return s_state; }

void nt_audio_try_resume(void) {
    if (!s_engine_inited || s_state != NT_AUDIO_SUSPENDED) {
        return;
    }
    /* The real AudioContext resume is done by miniaudio's own click/touch
       handler (it fires inside the gesture); here we just start the device and
       mark RUNNING so plays stop being dropped. */
    ma_engine_start(&s_engine);
    s_state = NT_AUDIO_RUNNING;
}
// #endregion

// #region clips
nt_audio_clip_t nt_audio_clip_create(const uint8_t *data, uint32_t size) {
    if (!s_engine_inited || data == NULL || size == 0) {
        return NT_AUDIO_CLIP_INVALID;
    }
    int slot = -1;
    for (int i = 0; i < NT_MAX_AUDIO_CLIPS; ++i) {
        if (!s_clips[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        NT_LOG_WARN("clip pool full (%d)", NT_MAX_AUDIO_CLIPS);
        return NT_AUDIO_CLIP_INVALID;
    }

    uint8_t *copy = malloc(size);
    if (copy == NULL) {
        return NT_AUDIO_CLIP_INVALID;
    }
    memcpy(copy, data, size);

    /* Validate the format up front so a bad blob fails here, not at play time. */
    ma_decoder probe;
    if (ma_decoder_init_memory(copy, size, NULL, &probe) != MA_SUCCESS) {
        free(copy);
        NT_LOG_ERROR("clip decode failed (unsupported format?)");
        return NT_AUDIO_CLIP_INVALID;
    }
    ma_decoder_uninit(&probe);

    s_clips[slot].data = copy;
    s_clips[slot].size = size;
    s_clips[slot].state = NT_AUDIO_CLIP_READY;
    s_clips[slot].used = true;
    return (nt_audio_clip_t){(uint16_t)slot};
}

void nt_audio_clip_destroy(nt_audio_clip_t clip) {
    if (clip.index >= NT_MAX_AUDIO_CLIPS || !s_clips[clip.index].used) {
        return;
    }
    /* Caller must ensure no live voice is still reading this clip's bytes. */
    free(s_clips[clip.index].data);
    s_clips[clip.index] = (nt_audio_clip_slot_t){0};
}

nt_audio_clip_state_t nt_audio_clip_get_state(nt_audio_clip_t clip) {
    if (clip.index >= NT_MAX_AUDIO_CLIPS || !s_clips[clip.index].used) {
        return NT_AUDIO_CLIP_NONE;
    }
    return s_clips[clip.index].state;
}
// #endregion

// #region playback
nt_audio_voice_t nt_audio_play(nt_audio_clip_t clip, float volume, float pitch, bool loop) {
    if (s_state != NT_AUDIO_RUNNING) {
        return NT_AUDIO_VOICE_INVALID;
    }
    if (clip.index >= NT_MAX_AUDIO_CLIPS || !s_clips[clip.index].used) {
        return NT_AUDIO_VOICE_INVALID;
    }
    nt_audio_clip_slot_t *cs = &s_clips[clip.index];

    int i = voice_acquire();
    if (i < 0) {
        return NT_AUDIO_VOICE_INVALID; /* all voices looping */
    }
    nt_audio_voice_slot_t *v = &s_voices[i];

    if (ma_decoder_init_memory(cs->data, cs->size, NULL, &v->decoder) != MA_SUCCESS) {
        return NT_AUDIO_VOICE_INVALID;
    }
    v->decoder_inited = true;

    ma_uint32 flags = MA_SOUND_FLAG_NO_SPATIALIZATION;
    if (ma_sound_init_from_data_source(&s_engine, &v->decoder, flags, NULL, &v->sound) != MA_SUCCESS) {
        ma_decoder_uninit(&v->decoder);
        v->decoder_inited = false;
        return NT_AUDIO_VOICE_INVALID;
    }
    v->sound_inited = true;

    ma_sound_set_volume(&v->sound, volume);
    ma_sound_set_pitch(&v->sound, pitch);
    ma_sound_set_looping(&v->sound, loop ? MA_TRUE : MA_FALSE);

    if (ma_sound_start(&v->sound) != MA_SUCCESS) {
        voice_free(i);
        return NT_AUDIO_VOICE_INVALID;
    }

    v->looping = loop;
    v->order = ++s_play_order;
    v->active = true;
    return (nt_audio_voice_t){(uint16_t)i, v->generation};
}

void nt_audio_stop(nt_audio_voice_t voice) {
    nt_audio_voice_slot_t *v = voice_resolve(voice);
    if (v != NULL) {
        voice_free((int)(v - s_voices));
    }
}

void nt_audio_stop_all(void) {
    for (int i = 0; i < NT_MAX_AUDIO_VOICES; ++i) {
        if (s_voices[i].active) {
            voice_free(i);
        }
    }
}

void nt_audio_set_volume(nt_audio_voice_t voice, float volume) {
    nt_audio_voice_slot_t *v = voice_resolve(voice);
    if (v != NULL) {
        ma_sound_set_volume(&v->sound, volume);
    }
}

void nt_audio_set_pitch(nt_audio_voice_t voice, float pitch) {
    nt_audio_voice_slot_t *v = voice_resolve(voice);
    if (v != NULL) {
        ma_sound_set_pitch(&v->sound, pitch);
    }
}

bool nt_audio_is_playing(nt_audio_voice_t voice) {
    nt_audio_voice_slot_t *v = voice_resolve(voice);
    return v != NULL && ma_sound_is_playing(&v->sound) == MA_TRUE;
}
// #endregion

// #region global
void nt_audio_set_master_volume(float volume) {
    s_master_volume = volume;
    if (s_engine_inited) {
        ma_engine_set_volume(&s_engine, volume);
    }
}

float nt_audio_get_master_volume(void) { return s_master_volume; }
// #endregion
