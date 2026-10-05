#ifndef NT_EXAMPLE_FRAMES_H
#define NT_EXAMPLE_FRAMES_H

/* `--frames N`: fixed-step measured run, per-frame means, last-frame checksum.
 * Empty on web so shipped wasm pays nothing; the checksum repeats only with
 * NT_METRICS_ENABLED=OFF because a live metrics HUD changes the pixels. */

#include "core/nt_platform.h"

#ifdef NT_PLATFORM_WEB

#include "core/nt_types.h"

static inline uint32_t nt_example_arg_u32(int argc, char **argv, const char *name, uint32_t fallback) {
    (void)argc;
    (void)argv;
    (void)name;
    return fallback;
}
static inline void nt_example_frames_init(int argc, char **argv) {
    (void)argc;
    (void)argv;
}
static inline bool nt_example_frames_on(void) { return false; }
static inline void nt_example_frames_begin(void) {}
static inline void nt_example_frames_end(bool ready) { (void)ready; }

#else

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/nt_app.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "time/nt_time.h"
#include "window/nt_window.h"

#define NT_EXAMPLE_FRAMES_WARMUP 60U

static struct {
    uint32_t frames; /* measured frames requested; 0 = tool off */
    uint32_t done;   /* frames since ready, warmup included */
    double begin;
    double ms;
    double draws;
    double gl;
    double uploads;
    double upload_bytes;
    uint64_t stream_peak;
    uint32_t frame_peak[NT_GFX_FRAME_STREAM_COUNT]; /* frame storage bytes, to size nt_gfx_desc_t.frame_capacity */
} s_example_frames;

static inline uint32_t nt_example_arg_u32(int argc, char **argv, const char *name, uint32_t fallback) {
    for (int i = 1; i + 1 < argc; i++) {
        if (strcmp(argv[i], name) == 0) {
            return (uint32_t)strtoul(argv[i + 1], NULL, 10);
        }
    }
    return fallback;
}

/* After nt_window_init. */
static inline void nt_example_frames_init(int argc, char **argv) {
    s_example_frames.frames = nt_example_arg_u32(argc, argv, "--frames", 0);
    if (s_example_frames.frames == 0) {
        return;
    }
    nt_app_set_mode(NT_APP_MODE_MANUAL);
    nt_app_set_step_dt(1.0F / 60.0F);
    nt_window_set_vsync(NT_VSYNC_OFF);
}

/* Live input would make runs differ, so examples skip input polling under the tool. */
static inline bool nt_example_frames_on(void) { return s_example_frames.frames != 0; }

/* Right before nt_gfx_begin_frame. */
static inline void nt_example_frames_begin(void) { s_example_frames.begin = nt_time_now(); }

/* Right after nt_gfx_end_frame, before swap. */
static inline void nt_example_frames_end(bool ready) {
    if (s_example_frames.frames == 0 || !ready) {
        return;
    }
    double ms = (nt_time_now() - s_example_frames.begin) * 1000.0;
    nt_app_step(1); /* MANUAL runs dt 0 until ready, then one fixed step per frame */
    uint32_t index = s_example_frames.done++;
    if (index < NT_EXAMPLE_FRAMES_WARMUP) {
        return;
    }
    const nt_gfx_counters_t *c = &g_nt_gfx.counters; /* this frame: last_frame changes only at the next begin_frame */
    uint64_t gl = 0;
    for (uint32_t i = 0; i < NT_GFX_GL_COUNT; i++) {
        gl += c->gl[i];
    }
    s_example_frames.ms += ms;
    s_example_frames.draws += (double)nt_gfx_draw_calls(c);
    s_example_frames.gl += (double)gl;
    s_example_frames.uploads += (double)c->buffer_upload_calls;
    s_example_frames.upload_bytes += (double)c->buffer_upload_bytes;
    if (c->stream_bytes > s_example_frames.stream_peak) {
        s_example_frames.stream_peak = c->stream_bytes;
    }
    /* This frame's use: the counters publish frame_bytes only at the next begin_frame. */
    for (uint32_t s = 0; s < NT_GFX_FRAME_STREAM_COUNT; s++) {
        if (g_nt_gfx_frame_storage[s].used > s_example_frames.frame_peak[s]) {
            s_example_frames.frame_peak[s] = g_nt_gfx_frame_storage[s].used;
        }
    }
    if (index + 1 < NT_EXAMPLE_FRAMES_WARMUP + s_example_frames.frames) {
        return;
    }

    uint32_t size = g_nt_window.fb_width * g_nt_window.fb_height * 4U;
    uint8_t *pixels = (uint8_t *)malloc(size);
    bool read = pixels != NULL && nt_gfx_read_pixels(0, 0, (int)g_nt_window.fb_width, (int)g_nt_window.fb_height, pixels, size);
    uint32_t checksum = read ? nt_hash32(pixels, size).value : 0;
    free(pixels);

    double n = (double)s_example_frames.frames;
    printf("[frames] n=%u frame_ms=%.3f draws=%.1f gl=%.1f buffer_uploads=%.1f buffer_bytes=%.0f stream_peak=%llu frame_peak=%u/%u/%u checksum=%s%08x\n", s_example_frames.frames,
           s_example_frames.ms / n, s_example_frames.draws / n, s_example_frames.gl / n, s_example_frames.uploads / n, s_example_frames.upload_bytes / n,
           (unsigned long long)s_example_frames.stream_peak, s_example_frames.frame_peak[NT_GFX_FRAME_VERTEX], s_example_frames.frame_peak[NT_GFX_FRAME_INDEX],
           s_example_frames.frame_peak[NT_GFX_FRAME_UNIFORM], read ? "" : "read-failed:", checksum);
    (void)fflush(stdout);
    nt_app_quit();
}

#endif /* NT_PLATFORM_WEB */

#endif /* NT_EXAMPLE_FRAMES_H */
