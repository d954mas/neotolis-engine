#include "app/nt_app.h"
#include "cJSON.h"
#include "core/nt_assert.h"
#include "core/nt_core.h"
#include "devapi/nt_devapi.h"
#include "devapi/nt_devapi_net.h"
#include "input/nt_input.h"
#include "window/nt_window.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Safety auto-exit so a CI run can't hang forever if no client ever drives a quit.
#define DEVAPI_HOST_MAX_FRAMES 100000u

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

static void frame(void) {
    nt_window_poll();
    /* Poll devapi at frame start, before input: a command only queues an input
       injection, nt_input_poll() then samples hardware, and a later apply step
       overlays the queued injection so it wins (one frame-start touch-point). */
    nt_devapi_update();
    nt_input_poll();

    /* Draw + the host's own swap go TOGETHER under the render flag — never skip-draw-but-swap
       (that would present a stale buffer). Render off => draw_calls stays 0. This host has no
       real draw yet; the placeholder marks where draw would issue before the swap. */
    if (nt_app_render_enabled()) {
        /* placeholder: real draw / nt_ui_walk goes here */
        nt_window_swap_buffers();
    }

    if (nt_input_key_is_pressed(NT_KEY_ESCAPE) || g_nt_app.frame >= DEVAPI_HOST_MAX_FRAMES) {
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
    nt_input_shutdown();
    nt_window_shutdown();
    nt_engine_shutdown();
    return 0;
}
