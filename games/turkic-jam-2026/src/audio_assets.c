#include "audio_assets.h"

#include "audio/nt_audio.h"
#include "core/nt_platform.h"
#include "log/nt_log.h"

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

enum { TJ_AUDIO_CLICK, TJ_AUDIO_MUSIC, TJ_AUDIO_COUNT };

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
    nt_audio_set_master_volume(0.8F);

    s_entries[TJ_AUDIO_CLICK] = (tj_audio_entry_t){.path = TJ_AUDIO_DIR "click.wav", .music = false, .clip = NT_AUDIO_CLIP_INVALID};
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
            nt_audio_play(m->clip, 0.6F, 1.0F, true);
            s_music_started = true;
        }
    }
}

void tj_audio_play_click(void) {
    tj_audio_entry_t *c = &s_entries[TJ_AUDIO_CLICK];
    if (c->done && TJ_CLIP_IS_VALID(c->clip)) {
        nt_audio_play(c->clip, 0.7F, 1.0F, false);
    }
}
