#include "app/nt_app.h"
#include "core/nt_core.h"
#include "core/nt_platform.h"
#include "time/nt_time.h"
#include "window/nt_window.h"
#include <stdio.h>

#ifdef NT_PLATFORM_WEB
#include "platform/web/nt_platform_web.h"
#endif

static nt_accumulator_t s_acc;
static int s_physics_ticks;

static void frame(void) {
    nt_window_poll();
    float dt = g_nt_app.dt;

    int steps = nt_accumulator_update(&s_acc, dt);
    for (int i = 0; i < steps; i++) {
        s_physics_ticks++;
    }

    /* Log every 60 frames */
    if (g_nt_app.frame % 60 == 0) {
        printf("[frame %u] dt=%.6f time=%.3f physics=%d\n", g_nt_app.frame, (double)dt, (double)g_nt_app.time, s_physics_ticks);
    }

    /* Native: exit after 300 frames */
    if (g_nt_app.frame >= 300) {
        nt_app_quit();
    }
}

int main(void) {
    nt_engine_config_t config = {0};
    config.app_name = "hello";
    config.version = 1;

    nt_result_t result = nt_engine_init(&config);
    if (result != NT_OK) {
        printf("Failed to initialize engine: error %d\n", result);
        return 1;
    }

    nt_window_init();
#ifdef NT_PLATFORM_WEB
    nt_platform_web_loading_complete();
#endif

    printf("Hello from Neotolis Engine %s!\n", nt_engine_version_string());
    printf("Window: %ux%u fb: %ux%u dpr: %.1f\n", g_nt_window.width, g_nt_window.height, g_nt_window.fb_width, g_nt_window.fb_height, (double)g_nt_window.dpr);

    nt_accumulator_init(&s_acc, 1.0F / 60.0F, 4);
    g_nt_app.target_dt = 1.0F / 30.0F;

    nt_app_run(frame);

#ifndef NT_PLATFORM_WEB
    /* On native nt_app_run() blocks until nt_app_quit(). Cleanup here. */
    nt_engine_shutdown();
#endif
    return 0;
}
