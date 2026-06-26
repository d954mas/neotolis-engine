#include "app/nt_app.h"
#include "cJSON.h"
#include "clay.h"
#include "core/nt_assert.h"
#include "core/nt_core.h"
#include "core/nt_platform.h"
#include "devapi/nt_devapi.h"
#include "devapi/nt_devapi_net.h"
#include "drawable_comp/nt_drawable_comp.h"
#include "entity/nt_entity.h"
#include "input/nt_input.h"
#include "log/nt_log.h"
#include "log/nt_log_ring.h"
#include "memory/nt_mem_scratch.h"
#include "metrics/nt_metrics.h"
#include "time/nt_time.h"
#include "transform_comp/nt_transform_comp.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_button.h" /* NT_UI_BUTTON_DEF for the registered-widget role. */
#include "ui/nt_ui_scale.h"  /* nt_ui_compute_scale + nt_ui_viewport_from_scale for the scaled ctx. */
#include "window/nt_window.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* game-layer command: echo {msg} back as {msg}. Registered via the public
   nt_devapi_register path only — zero engine edits. Uses the public cJSON
   API for the result (devapi_add_* is internal to the devapi module). */
static bool cmd_game_echo(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    const cJSON *msg = cJSON_GetObjectItemCaseSensitive(params, "msg");
    if (!cJSON_IsString(msg)) {
        err->code = "bad_params";
        err->message = "game.echo requires a string \"msg\"";
        return false;
    }
    cJSON *added = cJSON_AddStringToObject(result, "msg", msg->valuestring);
    NT_ASSERT(added != NULL);
    (void)added;
    return true;
}

static const nt_devapi_command_desc k_game_echo = {
    .method = "game.echo",
    .group = "game",
    .summary = "echo {msg} back",
    .params_shape = "{msg:string}",
    .result_shape = "{msg:string}",
    .frame_behavior = "any",
    .side_effects = "none",
};

/* Resolve the listen port: NT_DEVAPI_DEFAULT_PORT, overridden by env NT_DEVAPI_PORT.
   Falls back to the default on a missing / unparseable / out-of-range value. */
static uint16_t resolve_port(void) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe) — single-threaded host startup, getenv is fine
    const char *env = getenv("NT_DEVAPI_PORT");
    if (env == NULL || env[0] == '\0') {
        return (uint16_t)NT_DEVAPI_DEFAULT_PORT;
    }
    char *end = NULL;
    long v = strtol(env, &end, 10);
    if (end == env || *end != '\0' || v < 1 || v > 65535) {
        printf("[devapi_host] NT_DEVAPI_PORT='%s' invalid, using default %d\n", env, NT_DEVAPI_DEFAULT_PORT);
        return (uint16_t)NT_DEVAPI_DEFAULT_PORT;
    }
    return (uint16_t)v;
}

/* A small probe-able "hud" UI context — asset-free (layout + the registered-widget slot only; the
   host never calls nt_ui_walk). "hud_btn" carries a togglable enabled flag a synthetic ui.click
   flips, observable via ui.element. */
/* Sized to clear nt_ui_min_arena_size for the default desc (create asserts on a too-small arena). */
#define HUD_ARENA_SIZE ((size_t)2U * 1024U * 1024U)
static NT_UI_DECLARE_ARENA(s_hud_arena, HUD_ARENA_SIZE);
static nt_ui_context_t *s_hud_ctx;
static bool s_hud_btn_on = true; /* the observable: a synthetic click on "hud_btn" toggles it. */

/* A second, SCALED hud ("hud_scaled"): same layout through a non-trivial nt_ui_scale viewport
   (LETTERBOX: 300x300 ref into the 800x600 fb -> 2x scale + 100px horizontal margin), so a ui.click
   on a scaled ctx exercises the layout->device map via the ctx viewport. */
#define SCALED_REF_W 300.0F
#define SCALED_REF_H 300.0F
static NT_UI_DECLARE_ARENA(s_hud_scaled_arena, HUD_ARENA_SIZE);
static nt_ui_context_t *s_hud_scaled_ctx;
static bool s_scaled_btn_on = true;

/* Declare the hud tree once per frame. A click on "hud_btn" (real device or a synthetic ui.click,
   bot==human) flips s_hud_btn_on; the widget re-registers each frame with enabled=s_hud_btn_on so
   the toggle surfaces through the probe's `enabled` field. */
static void declare_hud(void) {
    const float fb_w = (float)(g_nt_window.fb_width > 0 ? g_nt_window.fb_width : 800);
    const float fb_h = (float)(g_nt_window.fb_height > 0 ? g_nt_window.fb_height : 600);
    /* Feed ALL pointer slots, not just [0]: a synthetic ui.click lands in the first FREE slot, which is
       NOT 0 when a real mouse already holds slot 0. Deactivated slots keep stale x/y but their button
       edge bits are cleared, so they fire no buttons and never spuriously hit a widget. */
    nt_ui_begin(s_hud_ctx, fb_w, fb_h, g_nt_app.dt, g_nt_input.pointers, NT_INPUT_MAX_POINTERS);
    CLAY({.id = CLAY_ID("hud_root"), .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(20), .childGap = 12}}) {
        CLAY({.id = CLAY_ID("hud_btn"), .layout = {.sizing = {CLAY_SIZING_FIXED(160), CLAY_SIZING_FIXED(40)}}}) {}
        CLAY({.id = CLAY_ID("hud_btn_b"), .layout = {.sizing = {CLAY_SIZING_FIXED(160), CLAY_SIZING_FIXED(40)}}}) {}
    }
    /* Register AFTER declare so the ids exist this frame; enabled reflects the current toggle. */
    nt_ui_widget_register(s_hud_ctx, nt_ui_id("hud_btn"), &NT_UI_BUTTON_DEF, NULL, s_hud_btn_on);
    nt_ui_widget_register(s_hud_ctx, nt_ui_id("hud_btn_b"), &NT_UI_BUTTON_DEF, NULL, true);
    if (nt_ui_step_interaction(s_hud_ctx, nt_ui_id("hud_btn")).clicked) {
        s_hud_btn_on = !s_hud_btn_on;
    }
    nt_ui_end(s_hud_ctx);
}

/* Declare the scaled hud tree once per frame. Identical layout to the hud, but the ctx viewport is
   overridden to the nt_ui_scale content rect so the ctx converts the raw device pointer device->layout
   internally — a synthetic ui.click resolved layout->device by the devapi lands on the widget. The
   viewport MUST be set after nt_ui_begin and before the first hit-test (here: step_interaction). */
static void declare_hud_scaled(void) {
    const float fb_w = (float)(g_nt_window.fb_width > 0 ? g_nt_window.fb_width : 800);
    const float fb_h = (float)(g_nt_window.fb_height > 0 ? g_nt_window.fb_height : 600);
    const nt_ui_scale_desc_t sdesc = {.ref_w = SCALED_REF_W, .ref_h = SCALED_REF_H, .mode = NT_UI_SCALE_LETTERBOX};
    const nt_ui_scale_t scale = nt_ui_compute_scale(&sdesc, fb_w, fb_h);
    /* begin with the LOGICAL dims (Clay layout space); the viewport bridges logical<->device. */
    nt_ui_begin(s_hud_scaled_ctx, scale.logical_w, scale.logical_h, g_nt_app.dt, g_nt_input.pointers, NT_INPUT_MAX_POINTERS);
    nt_ui_set_viewport(s_hud_scaled_ctx, nt_ui_viewport_from_scale(&scale));
    CLAY({.id = CLAY_ID("scaled_root"), .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(20), .childGap = 12}}) {
        CLAY({.id = CLAY_ID("scaled_btn"), .layout = {.sizing = {CLAY_SIZING_FIXED(80), CLAY_SIZING_FIXED(30)}}}) {}
    }
    nt_ui_widget_register(s_hud_scaled_ctx, nt_ui_id("scaled_btn"), &NT_UI_BUTTON_DEF, NULL, s_scaled_btn_on);
    if (nt_ui_step_interaction(s_hud_scaled_ctx, nt_ui_id("scaled_btn")).clicked) {
        s_scaled_btn_on = !s_scaled_btn_on;
    }
    nt_ui_end(s_hud_scaled_ctx);
}

/* Host-owned disconnect recovery: the engine resets only devapi-owned state on a client drop, so a
   bot that drops mid-MANUAL leaves the host frozen. On the connected->disconnected edge, force RUN so
   the bare host stays usable. A graceful bot restores mode itself; this only catches an ungraceful drop. */
static void recover_on_disconnect(void) {
    static bool was_connected = false;
    bool now = nt_devapi_net_has_client();
    if (was_connected && !now) {
        g_nt_app.mode = NT_APP_MODE_RUN;
        g_nt_app.paused = false;
        g_nt_app.pending_steps = 0;
    }
    was_connected = now;
}

static void frame(void) {
    /* Host owns measurement; nt_metrics only stores. */
    static double s_last_begin = 0.0;
    double now = nt_time_now();
    float frame_ms = (s_last_begin > 0.0) ? (float)((now - s_last_begin) * 1000.0) : -1.0F;
    s_last_begin = now;
    double cpu_begin = now;

    nt_window_poll();
    /* nt_devapi_update must run before nt_input_poll so injected rising edges survive the edge-clear. */
    nt_devapi_update();
    recover_on_disconnect(); /* host policy: unfreeze after an (ungraceful) bot drop. */
    nt_input_poll();

    /* Build the hud tree AFTER input_poll so this frame's (possibly injected) pointer drives the
       interaction step. nt_devapi's ui.tree/ui.element read the LAST completed frame — exactly this
       nt_ui_end's baked tables. Scratch is reset each frame for the per-element CLAY data. */
    nt_mem_scratch_reset();
    declare_hud();
    declare_hud_scaled();

    /* No real renderer here (the host issues no draw — nt_ui_walk is unnecessary for the probe). Swap
       only under the render flag so draw_calls stays 0 / render.* stays honest. */
    if (nt_app_render_enabled()) {
        nt_window_swap_buffers();
    }

    float cpu_ms = (float)((nt_time_now() - cpu_begin) * 1000.0);

    /* Seed a user int + float channel so perf.* has something to observe. The user counter is named
       frame_cpu_ms, not cpu_ms, so it does not shadow the fixed cpu_ms channel on the wire. */
    static uint64_t s_frame_counter = 0;
    s_frame_counter++;
    nt_metrics_count("frames", s_frame_counter);
    nt_metrics_count_f("frame_cpu_ms", (double)cpu_ms);

    /* This host inits no gfx, so gpu_ms is the "no timer" sentinel and draw_calls is 0. */
    /* Throttled mem probe: nt_platform_memory_usage() walks the allocator (mallinfo is O(allocations)
       on web); in-use bytes drift slowly, so sample every 30 frames and push the cached value. */
    static uint64_t s_mem_used;
    static uint32_t s_mem_tick;
    if ((s_mem_tick++ % 30U) == 0U) {
        s_mem_used = nt_platform_memory_usage().used;
    }
    nt_metrics_frame_t mf = {
        .frame_ms = frame_ms,
        .cpu_ms = cpu_ms,
        .gpu_ms = -1.0F,
        .draw_calls = 0,
        .mem_used = s_mem_used,
        .scratch_hwm = (uint32_t)nt_mem_scratch_high_water_mark(),
        .scratch_used = (uint32_t)nt_mem_scratch_used(),
    };
    nt_metrics_sample(&mf);

    /* No auto-exit: the driver owns quit (ESC for interactive, else subprocess kill; the bot's socket
       timeouts catch a hung host). A frame-count cap would also kill long stability sims. */
    if (nt_input_key_is_pressed(NT_KEY_ESCAPE)) {
        nt_app_quit();
    }
}

int main(void) {
    nt_engine_config_t config = {0};
    config.app_name = "devapi_host";
    config.version = 1;

    nt_result_t result = nt_engine_init(&config);
    if (result != NT_OK) {
        printf("Failed to initialize engine: error %d\n", result);
        return 1; /* nothing inited yet */
    }

    /* Partial-init teardown ladder: each fail point jumps to the label that releases exactly what was
       inited so far. The cleanup is written once and shared with the clean-exit path, so a future
       subsystem can't be silently left out of an error path. status stays 1 until a clean run. */
    int status = 1;

    g_nt_window.width = 800;
    g_nt_window.height = 600;
    nt_window_init();
    /* vsync OFF so time.set_fps{fps:0} truly uncaps the managed loop. */
    nt_window_set_vsync(NT_VSYNC_OFF);
    nt_input_init();

    /* Obs wiring: host pushes frames into nt_metrics; the log ring captures nt_log_write for log.tail.
       The obs group's commands are registered by nt_devapi_register_default() above. */
    nt_log_ring_init();
    nt_log_add_sink(nt_log_ring_sink, NULL);
    nt_metrics_init();

    /* devapi wiring (game-layer consumer; no engine edits). */
    if (nt_devapi_init() != NT_OK) {
        printf("Failed to initialize devapi\n");
        goto shutdown_base;
    }
    nt_devapi_register_default(); /* full host: register every compiled-in group. */
    result = nt_devapi_register(&k_game_echo, cmd_game_echo, NULL);
    if (result != NT_OK) {
        printf("[devapi_host] failed to register game.echo: error %d\n", result);
        goto shutdown_devapi;
    }

    /* Probe-able "hud" UI context. nt_mem_scratch backs CLAY's per-element data; nt_ui_module_init is
       self-contained (no gfx/font). The host registers the CTX here; the ui group's commands are
       registered by nt_devapi_register_default() above. */
    nt_mem_scratch_init((size_t)64U * 1024U);
    nt_ui_module_init();
    {
        const nt_ui_create_desc_t ui_desc = nt_ui_create_desc_defaults();
        s_hud_ctx = nt_ui_create_context(s_hud_arena, sizeof s_hud_arena, &ui_desc);
        NT_ASSERT(s_hud_ctx != NULL && "devapi_host: failed to create hud UI context");

        s_hud_scaled_ctx = nt_ui_create_context(s_hud_scaled_arena, sizeof s_hud_scaled_arena, &ui_desc);
        NT_ASSERT(s_hud_scaled_ctx != NULL && "devapi_host: failed to create scaled hud UI context");
#ifdef NT_DEVAPI_GROUP_UI
        /* The ui group's host-facing registrar exists only when GROUP_UI is built; without it the host
           still renders the hud, it is just not probe-able over devapi ui.*. */
        nt_devapi_ui_register_context("hud", s_hud_ctx);
        nt_devapi_ui_register_context("hud_scaled", s_hud_scaled_ctx);
#endif
    }

    /* Seed entities so entity.list / entity.set have live data; the comp inits register each
       component's describe()/apply(). */
    nt_entity_init(&(nt_entity_desc_t){.max_entities = 64});
    nt_transform_comp_init(&(nt_transform_comp_desc_t){.capacity = 64});
    nt_drawable_comp_init(&(nt_drawable_comp_desc_t){.capacity = 64});
    {
        nt_entity_t seed_a = nt_entity_create(); /* transform only */
        nt_transform_comp_add(seed_a);
        nt_transform_comp_set_position(seed_a, 1.0F, 2.0F, 3.0F);
        nt_entity_t seed_b = nt_entity_create(); /* drawable only */
        nt_drawable_comp_add(seed_b);
        nt_entity_t seed_c = nt_entity_create(); /* both */
        nt_transform_comp_add(seed_c);
        nt_drawable_comp_add(seed_c);
    }

    {
        uint16_t port = resolve_port();
        if (!nt_devapi_net_start(port)) {
            printf("[devapi_host] failed to start TCP server on port %u (taken?)\n", port);
            goto shutdown_scene;
        }
        printf("[devapi_host] listening on 127.0.0.1:%u\n", port);
        /* Seed the log ring so log.tail has at least one entry the moment a bot connects. */
        nt_log_info("devapi_host listening on 127.0.0.1:%u", port);
    }

    /* Opt-in pre-loop gate so a bot can hand over setup before frame 0.
       Bounded; the host does NOT require a client to start. */
    if (nt_devapi_net_wait_for_client(2000)) {
        printf("[devapi_host] client connected before loop\n");
    } else {
        printf("[devapi_host] no client yet; per-frame accept continues\n");
    }

    nt_app_run(frame);
    status = 0; /* clean run */

    nt_devapi_net_stop();
shutdown_scene: /* reverse-init: scene (ui borrows scratch; comps borrow entity) before devapi before base. */
    nt_ui_destroy_context(s_hud_ctx);
    nt_ui_destroy_context(s_hud_scaled_ctx);
    nt_ui_module_shutdown();
    nt_mem_scratch_shutdown();
    nt_drawable_comp_shutdown();
    nt_transform_comp_shutdown();
    nt_entity_shutdown();
shutdown_devapi:
    nt_devapi_shutdown();
shutdown_base:
    nt_input_shutdown();
    nt_window_shutdown();
    nt_engine_shutdown();
    return status;
}
