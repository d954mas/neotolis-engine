#ifndef NT_AUDIO_TYPES_H
#define NT_AUDIO_TYPES_H

#include "core/nt_types.h"

/* Compile-time caps (spec §5.3 / §22). Pools are preallocated, never grown. */
#define NT_MAX_AUDIO_CLIPS 256
#define NT_MAX_AUDIO_VOICES 32

/* Handles are indices wrapped in structs so the compiler rejects accidental
   int<->handle mixing. Voices also carry a generation: the voice pool evicts
   under pressure (spec §22.5), so a raw index could outlive its sound. */
typedef struct nt_audio_clip_t {
    uint16_t index;
} nt_audio_clip_t;

typedef struct nt_audio_voice_t {
    uint16_t index;
    uint16_t generation;
} nt_audio_voice_t;

#define NT_AUDIO_CLIP_INVALID ((nt_audio_clip_t){0xFFFFU})
#define NT_AUDIO_VOICE_INVALID ((nt_audio_voice_t){0xFFFFU, 0U})

typedef enum nt_audio_state_t {
    NT_AUDIO_SUSPENDED = 0, /* web: before the first user gesture */
    NT_AUDIO_RUNNING,       /* ready to play */
    NT_AUDIO_FAILED,        /* backend init failed */
} nt_audio_state_t;

typedef enum nt_audio_clip_state_t {
    NT_AUDIO_CLIP_NONE = 0,
    NT_AUDIO_CLIP_READY,
    NT_AUDIO_CLIP_FAILED,
} nt_audio_clip_state_t;

#endif /* NT_AUDIO_TYPES_H */
