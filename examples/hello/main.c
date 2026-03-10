#include "app/nt_app.h"
#include "core/nt_core.h"
#include "platform/web/nt_web.h"
#include "time/nt_time.h"
#include <stdio.h>

static nt_accumulator_t s_acc;
static int s_physics_ticks;

static void frame(void) {
    float dt = g_nt_app.dt;

    /* Fixed update via accumulator */
    nt_accumulator_add(&s_acc, dt);
    while (nt_accumulator_step(&s_acc)) {
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

static void shutdown(void) {
    printf("Shutdown: %d physics ticks in %.1fs\n", s_physics_ticks, (double)g_nt_app.time);
    nt_engine_shutdown();
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

    nt_web_loading_complete();

    printf("Hello from Neotolis Engine %s!\n", nt_engine_version_string());

    nt_accumulator_init(&s_acc, 1.0F / 60.0F, 4);

    nt_app_on_shutdown(shutdown);
    nt_app_run(frame);
    return 0;
}
