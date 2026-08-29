#include "app/nt_app.h"
#include "core/nt_core.h"
#include "core/nt_platform.h"
#include "devapi/nt_devapi.h"
#include "devapi/nt_devapi_capture.h" /* nt_devapi_capture_on_pre_swap — the public pre-swap producer seam. */
#include "devapi/nt_devapi_net.h"
#include "graphics/nt_gfx.h"
#include "input/nt_input.h"
#include "window/nt_window.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* The capture_host renders a deterministic NON-BLANK pattern (two distinct colors) every frame so the
   pixel-health not-blank check (not a single uniform color) has a real target. devapi_host inits
   NO gfx, so this dedicated host owns a real GL context for the live capture UAT. */

/* Background + foreground clear colors: a known two-tone frame. The foreground sub-rect is cleared
   under scissor on top of the full-frame background, giving >=2 distinct colors (non-uniform). */
static const float k_bg_color[4] = {0.10F, 0.20F, 0.45F, 1.0F};
static const float k_fg_color[4] = {0.90F, 0.55F, 0.10F, 1.0F};

/* Resolve the listen port: NT_DEVAPI_DEFAULT_PORT, overridden by env NT_DEVAPI_PORT. Falls back to the
   default on a missing / unparseable / out-of-range value. */
static uint16_t resolve_port(void) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe) — single-threaded host startup, getenv is fine
    const char *env = getenv("NT_DEVAPI_PORT");
    if (env == NULL || env[0] == '\0') {
        return (uint16_t)NT_DEVAPI_DEFAULT_PORT;
    }
    char *end = NULL;
    long v = strtol(env, &end, 10);
    if (end == env || *end != '\0' || v < 1 || v > 65535) {
        printf("[capture_host] NT_DEVAPI_PORT='%s' invalid, using default %d\n", env, NT_DEVAPI_DEFAULT_PORT);
        return (uint16_t)NT_DEVAPI_DEFAULT_PORT;
    }
    return (uint16_t)v;
}

/* Draw the known two-tone pattern: full-frame background, then a scissored centered sub-rect in the
   foreground color. glClear honors GL_SCISSOR_TEST, so the second pass only clears the sub-rect. */
static void render_pattern(void) {
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {k_bg_color[0], k_bg_color[1], k_bg_color[2], k_bg_color[3]}, .clear_depth = 1.0F});
    nt_gfx_end_pass();

    const int fb_w = (int)g_nt_window.fb_width;
    const int fb_h = (int)g_nt_window.fb_height;
    const int rw = fb_w / 2;
    const int rh = fb_h / 2;
    nt_gfx_set_scissor(fb_w / 4, fb_h / 4, rw, rh); /* GL bottom-left; placement is irrelevant to not-blank. */
    nt_gfx_set_scissor_enabled(true);
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {k_fg_color[0], k_fg_color[1], k_fg_color[2], k_fg_color[3]}, .clear_depth = 1.0F});
    nt_gfx_end_pass();
    nt_gfx_set_scissor_enabled(false); /* leave scissor off so the next frame's bg clear covers the whole FB. */

    nt_gfx_end_frame();
}

static void frame(void) {
    nt_window_poll();
    /* nt_devapi_update advances deferred slots; runs before input poll to match the host loop order. */
    nt_devapi_update();
    nt_input_poll();

    if (nt_app_render_enabled()) {
        render_pattern();
        /* The capture seam runs INSIDE nt_window_swap_buffers (post-render, pre-swap, where
           nt_gfx_read_pixels is GL-valid). Gated implicitly with rendering: render off => no swap =>
           seam doesn't run => a capture stays pending until a real frame draws, never a stale frame. */
        nt_window_swap_buffers();
    }

    if (nt_input_key_is_pressed(NT_KEY_ESCAPE)) {
        nt_app_quit();
    }
}

int main(void) {
    nt_engine_config_t config = {0};
    config.app_name = "capture_host";
    config.version = 1;

    nt_result_t result = nt_engine_init(&config);
    if (result != NT_OK) {
        printf("Failed to initialize engine: error %d\n", result);
        return 1; /* nothing inited yet */
    }

    int status = 1; /* stays 1 until a clean run; each fail point jumps to the matching teardown label. */

    g_nt_window.width = 800; /* the real capture resolution (mirrors devapi_host). */
    g_nt_window.height = 600;
    nt_window_init();
    nt_window_set_vsync(NT_VSYNC_OFF);
    nt_input_init();
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 32, .max_programs = 16, .max_pipelines = 16, .max_buffers = 128, .max_textures = 16, .max_meshes = 64, .max_render_targets = 16, .depth = true});
    /* No nt_fpng_init here: the capture group inits its own encoder dependency in nt_devapi_register_capture. */

    /* devapi wiring: init builds the framework, then this capture-focused host registers ONLY the groups
       it uses -- core (view/ping/info) + discovery (endpoints) + capture -- so it links no
       nt_ui/obs/entity/time/input group libs. The pre-swap capture seam is installed below. */
    if (nt_devapi_init() != NT_OK) {
        printf("Failed to initialize devapi\n");
        goto shutdown_gfx;
    }
    nt_devapi_register_core();
    nt_devapi_register_discovery();
    nt_devapi_register_capture();

    {
        uint16_t port = resolve_port();
        if (!nt_devapi_net_start(port)) {
            printf("[capture_host] failed to start TCP server on port %u (taken?)\n", port);
            goto shutdown_devapi;
        }
        printf("[capture_host] listening on 127.0.0.1:%u (fb %ux%u)\n", port, g_nt_window.fb_width, g_nt_window.fb_height);
    }

    /* Opt-in pre-loop gate so a bot can connect before frame 0; bounded, the host does not require it. */
    if (nt_devapi_net_wait_for_client(2000)) {
        printf("[capture_host] client connected before loop\n");
    } else {
        printf("[capture_host] no client yet; per-frame accept continues\n");
    }

    /* Install the capture seam ONCE: registers it as a window pre-swap hook (so the frame loop just
       renders + swaps, no per-frame seam call) and marks this host capture-capable. A host that skips
       this (e.g. devapi_host) rejects captures with capture_unavailable instead of hanging. */
    nt_devapi_capture_install_seam();

    nt_app_run(frame);
    status = 0; /* clean run */

    nt_devapi_net_stop();
shutdown_devapi:
    nt_devapi_shutdown();
shutdown_gfx:
    nt_gfx_shutdown();
    nt_input_shutdown();
    nt_window_shutdown();
    nt_engine_shutdown();
    return status;
}
