#include "app/nt_app.h"
#include "time/nt_time.h"

/* clang-format off */
#include <glad/gl.h>   /* Must precede glfw3.h -- glad replaces system GL header */
#include <GLFW/glfw3.h>
/* clang-format on */

/* Use __builtin_fminf to bypass Windows UCRT DLL import issue with ASan */
#define nt_fminf(a, b) __builtin_fminf((a), (b))

/* GLFW window handle owned by nt_window_native.c */
extern GLFWwindow *g_nt_glfw_window;

/* ---- File-scope statics (zero-initialized by C standard) ---- */

static nt_app_frame_fn s_frame_fn;
static bool s_should_quit;

/* Spin-wait margin: sleep the bulk, spin-wait the last 2ms for precision */
#define NT_SPIN_MARGIN 0.002

/* ---- API ---- */

void nt_app_run(nt_app_frame_fn fn) {
    s_frame_fn = fn;
    s_should_quit = false;

    /* Apply vsync setting */
    switch (g_nt_app.vsync) {
    case NT_VSYNC_OFF:
        glfwSwapInterval(0);
        break;
    case NT_VSYNC_ADAPTIVE:
        glfwSwapInterval(-1);
        break;
    case NT_VSYNC_ON: /* fall through */
    default:
        glfwSwapInterval(1);
        break;
    }

    double prev_time = nt_time_now();

    while (!s_should_quit && !glfwWindowShouldClose(g_nt_glfw_window)) {
        glfwPollEvents();

        double now = nt_time_now();
        float dt = nt_fminf((float)(now - prev_time), g_nt_app.max_dt);
        prev_time = now;

        g_nt_app.dt = dt;
        g_nt_app.time += dt;
        g_nt_app.frame++;
        s_frame_fn();

        glfwSwapBuffers(g_nt_glfw_window);

        /* Frame rate cap: single sleep + spin-wait */
        if (g_nt_app.target_dt > 0.0F) {
            double target = prev_time + (double)g_nt_app.target_dt;
            double remaining = target - nt_time_now();
            if (remaining > NT_SPIN_MARGIN) {
                nt_time_sleep(remaining - NT_SPIN_MARGIN);
            }
            while (nt_time_now() < target) { /* spin */
            }
        }
    }
}

void nt_app_quit(void) { s_should_quit = true; }
