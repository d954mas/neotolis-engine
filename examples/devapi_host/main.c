#include "app/nt_app.h"
#include "cJSON.h"
#include "clay.h"
#include "core/nt_assert.h"
#include "core/nt_core.h"
#include "devapi/nt_devapi.h"
#include "devapi/nt_devapi_net.h"
#include "input/nt_input.h"
#include "memory/nt_mem_scratch.h"
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

/* A second, SCALED hud ("hud_scaled"): same layout fed through a non-trivial nt_ui_scale viewport
   (LETTERBOX: 300x300 ref into the 800x600 fb -> 2x scale + a 100px horizontal margin). Proves a
   ui.click lands on a SCALED ctx over the live socket — resolve_target maps layout->device via the
   ctx viewport. "scaled_btn" is the togglable observable, mirroring "hud_btn". */
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
    nt_window_poll();
    /* Order matters: nt_devapi_update first runs net_poll (a command may enqueue into the
       devapi input schedule), then ticks that schedule and — only on a real sim-advance — releases
       due events into nt_input's immediate inject buffer. nt_input_poll next samples hardware AND
       applies that whole buffer post-edge-clear, so an injected rising edge survives to this frame's
       update. nt_input itself knows nothing about frames; the devapi layer owns the schedule. */
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
        return 1;
    }

    g_nt_window.width = 800;
    g_nt_window.height = 600;
    nt_window_init();
    /* vsync OFF so time.set_fps{fps:0} truly uncaps the managed loop. */
    nt_window_set_vsync(NT_VSYNC_OFF);
    nt_input_init();

    /* devapi wiring (game-layer consumer; no engine edits). */
    if (nt_devapi_init() != NT_OK) {
        printf("Failed to initialize devapi\n");
        nt_input_shutdown();
        nt_window_shutdown();
        nt_engine_shutdown();
        return 1;
    }
    nt_result_t rr = nt_devapi_register(&k_game_echo, cmd_game_echo, NULL);
    if (rr != NT_OK) {
        printf("[devapi_host] failed to register game.echo: error %d\n", rr);
        nt_devapi_shutdown();
        nt_input_shutdown();
        nt_window_shutdown();
        nt_engine_shutdown();
        return 1;
    }

    /* Probe-able "hud" UI context. nt_mem_scratch backs CLAY's per-element data; nt_ui_module_init is
       self-contained (no gfx/font). The host registers only the CTX — the ui group's commands
       self-register inside nt_devapi_register_ui under the gate. */
    nt_mem_scratch_init((size_t)64U * 1024U);
    nt_ui_module_init();
    const nt_ui_create_desc_t ui_desc = nt_ui_create_desc_defaults();
    s_hud_ctx = nt_ui_create_context(s_hud_arena, sizeof s_hud_arena, &ui_desc);
    NT_ASSERT(s_hud_ctx != NULL && "devapi_host: failed to create hud UI context");
    nt_devapi_ui_register_context("hud", s_hud_ctx);

    s_hud_scaled_ctx = nt_ui_create_context(s_hud_scaled_arena, sizeof s_hud_scaled_arena, &ui_desc);
    NT_ASSERT(s_hud_scaled_ctx != NULL && "devapi_host: failed to create scaled hud UI context");
    nt_devapi_ui_register_context("hud_scaled", s_hud_scaled_ctx);

    uint16_t port = resolve_port();
    if (!nt_devapi_net_start(port)) {
        printf("[devapi_host] failed to start TCP server on port %u (taken?)\n", port);
        nt_devapi_shutdown();
        nt_input_shutdown();
        nt_window_shutdown();
        nt_engine_shutdown();
        return 1;
    }
    printf("[devapi_host] listening on 127.0.0.1:%u\n", port);

    /* Opt-in pre-loop gate so a bot can hand over setup before frame 0.
       Bounded; the host does NOT require a client to start. */
    if (nt_devapi_net_wait_for_client(2000)) {
        printf("[devapi_host] client connected before loop\n");
    } else {
        printf("[devapi_host] no client yet; per-frame accept continues\n");
    }

    nt_app_run(frame);

    nt_devapi_net_stop();
    nt_devapi_shutdown();
    nt_ui_destroy_context(s_hud_ctx);
    nt_ui_destroy_context(s_hud_scaled_ctx);
    nt_ui_module_shutdown();
    nt_mem_scratch_shutdown();
    nt_input_shutdown();
    nt_window_shutdown();
    nt_engine_shutdown();
    return 0;
}
