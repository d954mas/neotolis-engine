#include "audio_assets.h"

#include "audio/nt_audio.h"
#include "core/nt_platform.h"
#include "log/nt_log.h"

#include "save.h"

#ifdef NT_PLATFORM_WEB
#include "http/nt_http.h"
#else
#include "fs/nt_fs.h"
#endif

#include <stdlib.h>

/* Where loose audio files live, per platform (mirrors the .ntpack convention). */
#ifdef NT_PLATFORM_WEB
#ifdef NT_CDN_URL
#define TJ_AUDIO_DIR NT_CDN_URL "/turkic_jam/audio/"
#else
#define TJ_AUDIO_DIR "assets/audio/"
#endif
#else
#define TJ_AUDIO_DIR "audio/"
#endif

#define TJ_CLIP_IS_VALID(c) ((c).index != 0xFFFFU)

/* SFX first (must match tj_sfx_t in the header), MUSIC last. */
enum { TJ_AUDIO_CLICK, TJ_AUDIO_MERGE, TJ_AUDIO_HIT, TJ_AUDIO_VICTORY, TJ_AUDIO_DICE, TJ_AUDIO_EVENT, TJ_AUDIO_DEATH, TJ_AUDIO_MUSIC, TJ_AUDIO_COUNT };

typedef struct {
    const char *path;
    bool music;
    uint32_t req_id; /* fs/http request id, 0 = none */
    nt_audio_clip_t clip;
    bool pending;
    bool done;
} tj_audio_entry_t;

static tj_audio_entry_t s_entries[TJ_AUDIO_COUNT];
static bool s_music_started;
static nt_audio_voice_t s_music_voice = NT_AUDIO_VOICE_INVALID;

/* Two independent buses (0..1); the engine master stays at 1.0 so these are the
   only controls. SFX volume is baked into each play; music is live-adjustable. */
static float s_music_vol = 0.6F;
static float s_sfx_vol = 0.7F;

static float clamp01(float v) {
    if (v < 0.0F) {
        return 0.0F;
    }
    if (v > 1.0F) {
        return 1.0F;
    }
    return v;
}

/* ---- Platform request wrappers (fs on native, http on web) ---- */

static uint32_t req_start(const char *path) {
#ifdef NT_PLATFORM_WEB
    return nt_http_request(path).id;
#else
    return nt_fs_read_file(path).id;
#endif
}

static int req_poll(uint32_t id) { /* 0 pending, 1 done, -1 failed */
#ifdef NT_PLATFORM_WEB
    switch (nt_http_state((nt_http_request_t){id})) {
    case NT_HTTP_STATE_DONE:
        return 1;
    case NT_HTTP_STATE_FAILED:
        return -1;
    default:
        return 0;
    }
#else
    switch (nt_fs_state((nt_fs_request_t){id})) {
    case NT_FS_STATE_DONE:
        return 1;
    case NT_FS_STATE_FAILED:
        return -1;
    default:
        return 0;
    }
#endif
}

static uint8_t *req_take(uint32_t id, uint32_t *size) {
#ifdef NT_PLATFORM_WEB
    return nt_http_take_data((nt_http_request_t){id}, size);
#else
    return nt_fs_take_data((nt_fs_request_t){id}, size);
#endif
}

static void req_free(uint32_t id) {
#ifdef NT_PLATFORM_WEB
    nt_http_free((nt_http_request_t){id});
#else
    nt_fs_free((nt_fs_request_t){id});
#endif
}

/* ---- Public API ---- */

void tj_audio_assets_init(void) {
    nt_audio_set_master_volume(1.0F); /* buses (music/sfx) are the real controls */

    s_entries[TJ_AUDIO_CLICK] = (tj_audio_entry_t){.path = TJ_AUDIO_DIR "click.wav", .music = false, .clip = NT_AUDIO_CLIP_INVALID};
    s_entries[TJ_AUDIO_MERGE] = (tj_audio_entry_t){.path = TJ_AUDIO_DIR "merge.wav", .music = false, .clip = NT_AUDIO_CLIP_INVALID};
    s_entries[TJ_AUDIO_HIT] = (tj_audio_entry_t){.path = TJ_AUDIO_DIR "hit.wav", .music = false, .clip = NT_AUDIO_CLIP_INVALID};
    s_entries[TJ_AUDIO_VICTORY] = (tj_audio_entry_t){.path = TJ_AUDIO_DIR "victory.wav", .music = false, .clip = NT_AUDIO_CLIP_INVALID};
    s_entries[TJ_AUDIO_DICE] = (tj_audio_entry_t){.path = TJ_AUDIO_DIR "dice.wav", .music = false, .clip = NT_AUDIO_CLIP_INVALID};
    s_entries[TJ_AUDIO_EVENT] = (tj_audio_entry_t){.path = TJ_AUDIO_DIR "event.wav", .music = false, .clip = NT_AUDIO_CLIP_INVALID};
    s_entries[TJ_AUDIO_DEATH] = (tj_audio_entry_t){.path = TJ_AUDIO_DIR "death.wav", .music = false, .clip = NT_AUDIO_CLIP_INVALID};
    s_entries[TJ_AUDIO_MUSIC] = (tj_audio_entry_t){.path = TJ_AUDIO_DIR "music.mp3", .music = true, .clip = NT_AUDIO_CLIP_INVALID};

    for (int i = 0; i < TJ_AUDIO_COUNT; ++i) {
        s_entries[i].req_id = req_start(s_entries[i].path);
        s_entries[i].pending = s_entries[i].req_id != 0;
        if (!s_entries[i].pending) {
            nt_log_warn("turkic_jam: audio request rejected: %s", s_entries[i].path);
        }
    }
}

void tj_audio_assets_poll(void) {
    for (int i = 0; i < TJ_AUDIO_COUNT; ++i) {
        tj_audio_entry_t *e = &s_entries[i];
        if (!e->pending) {
            continue;
        }
        const int st = req_poll(e->req_id);
        if (st == 0) {
            continue;
        }
        if (st == 1) {
            uint32_t size = 0;
            uint8_t *bytes = req_take(e->req_id, &size);
            if (bytes != NULL && size > 0) {
                e->clip = nt_audio_clip_create(bytes, size);
            }
            free(bytes);
            if (!TJ_CLIP_IS_VALID(e->clip)) {
                nt_log_warn("turkic_jam: audio clip create failed: %s", e->path);
            }
        } else {
            nt_log_warn("turkic_jam: audio load failed: %s", e->path);
        }
        req_free(e->req_id);
        e->pending = false;
        e->done = true;
    }

    /* Music waits for the device to be RUNNING (web: after the first gesture). */
    if (!s_music_started) {
        tj_audio_entry_t *m = &s_entries[TJ_AUDIO_MUSIC];
        if (m->done && TJ_CLIP_IS_VALID(m->clip) && nt_audio_get_state() == NT_AUDIO_RUNNING) {
            s_music_voice = nt_audio_play(m->clip, s_music_vol, 1.0F, true);
            s_music_started = true;
        }
    }
}

void tj_audio_play_sfx(tj_sfx_t id) {
    if ((int)id < 0 || (int)id >= TJ_AUDIO_MUSIC) {
        return; /* music is not a one-shot SFX */
    }
    tj_audio_entry_t *e = &s_entries[id];
    if (e->done && TJ_CLIP_IS_VALID(e->clip)) {
        nt_audio_play(e->clip, s_sfx_vol, 1.0F, false);
    }
}

void tj_audio_play_click(void) { tj_audio_play_sfx(TJ_SFX_CLICK); }

void tj_audio_set_music_volume(float v01) {
    s_music_vol = clamp01(v01);
    if (s_music_started) {
        nt_audio_set_volume(s_music_voice, s_music_vol);
    }
}

void tj_audio_set_sfx_volume(float v01) { s_sfx_vol = clamp01(v01); }

float tj_audio_get_music_volume(void) { return s_music_vol; }
float tj_audio_get_sfx_volume(void) { return s_sfx_vol; }

void tj_audio_load_volumes_from_save(void) {
    s_music_vol = clamp01((float)save_get_int("music_vol", 60) / 100.0F);
    s_sfx_vol = clamp01((float)save_get_int("sfx_vol", 70) / 100.0F);
    if (s_music_started) {
        nt_audio_set_volume(s_music_voice, s_music_vol);
    }
}
