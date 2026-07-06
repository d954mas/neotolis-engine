# Audio System

Audio is an engine module with a platform-agnostic, handle-based public API —
zero platform types, identical game code on web and desktop. Clips decode
async (OGG Vorbis in packs); a 32-voice pool evicts the oldest non-looping
voice; in the suspended state (before the first web user gesture) play calls
return an invalid handle without error.

Related: [Platform Architecture](../runtime/platform.md), [Resource System](../assets/resource.md), [Frame Lifecycle](../runtime/frame-lifecycle.md)

## Architecture overview

Audio is an **engine module**, analogous to input and platform. Not an ECS component, not game-side code.

```text
engine/
    audio/
        audio.h           // public API — single for all platforms
        audio_types.h     // handles, enums, defines
        audio_web.c       // Web Audio API via JS bridge
        audio_desktop.c   // miniaudio or custom mixer (future)
```

Build system compiles only one implementation file per platform.

## Platform-agnostic design

**Public API contains zero platform-specific types.** Only handles, floats, and bools. Game code is identical across web and desktop.

Key contracts:

- `audio_clip_create` is always potentially async (desktop may complete instantly, but game code does not rely on this)
- `audio_try_resume()` exists on all platforms (no-op on desktop)
- Audio format in packs is OGG Vorbis — both platforms can decode it
- Internal structures are different per-platform, hidden from game code

## Audio state

```c
typedef enum AudioState {
        AUDIO_SUSPENDED, // before first user gesture (web) or init failure
        AUDIO_RUNNING,   // ready to play
        AUDIO_FAILED     // AudioContext/backend creation failed
    } AudioState;
```

All `audio_play` calls in SUSPENDED state return `AUDIO_VOICE_INVALID` without error. Game code continues normally.

## Audio clips

```c
typedef struct AudioClipHandle {
    uint16_t index;
} AudioClipHandle;
#define AUDIO_CLIP_INVALID ((AudioClipHandle){0xFFFF})

typedef enum AudioClipState {
    AUDIO_CLIP_NONE,
    AUDIO_CLIP_DECODING, // decodeAudioData in progress (web) or decoding (desktop)
    AUDIO_CLIP_READY,
    AUDIO_CLIP_FAILED
} AudioClipState;
```

Internal storage (web):

```c
typedef struct AudioClipInternal {
    AudioClipState state;
    uint32_t js_buffer_id; // index into JS-side AudioBuffer array
    float duration;
    uint16_t generation;
} AudioClipInternal;
```

Internal storage (desktop):

```c
typedef struct AudioClipInternal {
    AudioClipState state;
    int16_t *pcm_data; // decoded samples in C heap
    uint32_t sample_count;
    uint32_t sample_rate;
    uint8_t channels;
    float duration;
    uint16_t generation;
} AudioClipInternal;
```

## Audio voices

```c
typedef struct AudioVoiceHandle {
    uint16_t index;
} AudioVoiceHandle;
#define AUDIO_VOICE_INVALID ((AudioVoiceHandle){0xFFFF})

typedef enum AudioVoiceState { VOICE_FREE, VOICE_PLAYING, VOICE_STOPPING } AudioVoiceState;
```

Voice pool with eviction: when all 32 voices are occupied, evict the oldest non-looping voice. If all voices are looping, do not play the new sound.

## Public API

```c
// === Lifecycle ===
void audio_init(void);
void audio_shutdown(void);
void audio_update(void);
AudioState audio_get_state(void);

// === Resume (call on user gesture) ===
void audio_try_resume(void);

// === Clips ===
AudioClipHandle audio_clip_create(const uint8_t *encoded_data, uint32_t size);
void audio_clip_destroy(AudioClipHandle clip);
AudioClipState audio_clip_get_state(AudioClipHandle clip);
float audio_clip_get_duration(AudioClipHandle clip);

// === Playback ===
AudioVoiceHandle audio_play(AudioClipHandle clip, float volume, float pitch, bool loop);
void audio_stop(AudioVoiceHandle voice);
void audio_stop_all(void);

// === Voice control ===
void audio_set_volume(AudioVoiceHandle voice, float volume);
void audio_set_pitch(AudioVoiceHandle voice, float pitch);
bool audio_is_playing(AudioVoiceHandle voice);

// === Global ===
void audio_set_master_volume(float volume);
float audio_get_master_volume(void);
```

## JS bridge contract (web implementation)

C calls to JS:

```c
extern void js_audio_init(void);
extern void js_audio_shutdown(void);
extern void js_audio_resume(void);
extern uint32_t js_audio_decode(uint16_t clip_index, const uint8_t *data, uint32_t size);
extern uint32_t js_audio_play(uint32_t js_buffer_id, float volume, float pitch, bool loop, uint16_t voice_index);
extern void js_audio_stop(uint32_t js_source_id);
extern void js_audio_set_volume(uint32_t js_source_id, float volume);
extern void js_audio_set_pitch(uint32_t js_source_id, float pitch);
extern void js_audio_set_master_volume(float volume);
```

JS calls to C:

```c
EMSCRIPTEN_KEEPALIVE
void audio_on_clip_decoded(uint16_t clip_index, uint32_t js_buffer_id, float duration, uint32_t success);

EMSCRIPTEN_KEEPALIVE
void audio_on_voice_ended(uint16_t voice_index);

EMSCRIPTEN_KEEPALIVE
void audio_on_state_changed(uint32_t running);
```

## Integration with frame loop

In `input_begin_frame` or `platform_step`:

```c
if (audio_get_state() == AUDIO_SUSPENDED && any_pointer_pressed) {
    audio_try_resume();
}
```

`audio_update()` is called each frame for voice state management (safety timeout, future fade management).

## Audio in resource pipeline

```c
// Builder
add_audio("assets/sfx/hit.wav");      // WAV → OGG conversion
add_audio("assets/music/theme.ogg");  // already OGG, pack as-is
```

Loading flow:

```text
Pack loaded → AssetMeta registered (REGISTERED)
  → asset_ensure_loaded() for audio:
      → read blob from pack by offset/size
      → call audio_clip_create(data, size)
      → AudioClipState = DECODING, AssetState = LOADING

  ... JS/native decoding ...

  → audio_on_clip_decoded callback:
      → AudioClipState = READY
      → AssetState = READY
```

## What is intentionally absent

- 3D audio / positional panning (future: add PannerNode on web, positional mixing on desktop)
- Sound groups / buses (game code manages category volumes through own wrappers)
- Effects (reverb, delay)
- Fade-in / fade-out (game code does via audio_set_volume + tween)
- Streaming long tracks (OGG 128kbps ≈ 1MB/min, decodeAudioData handles 5-minute tracks in milliseconds)
