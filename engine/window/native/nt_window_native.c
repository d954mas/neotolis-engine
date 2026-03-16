#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "input/nt_input_internal.h" /* nt_input_buffer_* */
#include "log/nt_log.h"
#include "window/nt_window.h"

static GLFWwindow *s_glfw_window = NULL;

/* ---- Pre-fullscreen size for restore ---- */
static int s_windowed_x;
static int s_windowed_y;
static int s_windowed_w;
static int s_windowed_h;

/* ---- Mouse button mask tracking (moved from nt_input_native.c) ---- */

static uint8_t s_cached_buttons;

/* ---- Key mapping (moved from nt_input_native.c) ---- */

static nt_key_t glfw_key_to_nt(int key) {
    /* Letters A-Z */
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
        return (nt_key_t)(NT_KEY_A + (key - GLFW_KEY_A));
    }
    /* Digits 0-9 */
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) {
        return (nt_key_t)(NT_KEY_0 + (key - GLFW_KEY_0));
    }
    /* Function keys F1-F12 */
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12) {
        return (nt_key_t)(NT_KEY_F1 + (key - GLFW_KEY_F1));
    }

    switch (key) {
    case GLFW_KEY_UP:
        return NT_KEY_ARROW_UP;
    case GLFW_KEY_DOWN:
        return NT_KEY_ARROW_DOWN;
    case GLFW_KEY_LEFT:
        return NT_KEY_ARROW_LEFT;
    case GLFW_KEY_RIGHT:
        return NT_KEY_ARROW_RIGHT;
    case GLFW_KEY_SPACE:
        return NT_KEY_SPACE;
    case GLFW_KEY_ENTER:
        return NT_KEY_ENTER;
    case GLFW_KEY_ESCAPE:
        return NT_KEY_ESCAPE;
    case GLFW_KEY_TAB:
        return NT_KEY_TAB;
    case GLFW_KEY_BACKSPACE:
        return NT_KEY_BACKSPACE;
    case GLFW_KEY_LEFT_SHIFT:
        return NT_KEY_LSHIFT;
    case GLFW_KEY_RIGHT_SHIFT:
        return NT_KEY_RSHIFT;
    case GLFW_KEY_LEFT_CONTROL:
        return NT_KEY_LCTRL;
    case GLFW_KEY_RIGHT_CONTROL:
        return NT_KEY_RCTRL;
    case GLFW_KEY_LEFT_ALT:
        return NT_KEY_LALT;
    case GLFW_KEY_RIGHT_ALT:
        return NT_KEY_RALT;
    case GLFW_KEY_DELETE:
        return NT_KEY_DELETE;
    case GLFW_KEY_INSERT:
        return NT_KEY_INSERT;
    case GLFW_KEY_HOME:
        return NT_KEY_HOME;
    case GLFW_KEY_END:
        return NT_KEY_END;
    case GLFW_KEY_PAGE_UP:
        return NT_KEY_PAGE_UP;
    case GLFW_KEY_PAGE_DOWN:
        return NT_KEY_PAGE_DOWN;
    default:
        return NT_KEY_COUNT; /* Unmapped sentinel */
    }
}

/* ---- GLFW Callbacks — buffer events for nt_input_poll() to drain ---- */

static void glfw_key_callback(GLFWwindow *window, int key, int scancode, int action, int mods) {
    (void)window;
    (void)scancode;
    (void)mods;
    nt_key_t nt_key = glfw_key_to_nt(key);
    if (nt_key < NT_KEY_COUNT) {
        nt_input_buffer_key(nt_key, action != GLFW_RELEASE);
    }
}

static void glfw_cursor_pos_callback(GLFWwindow *window, double xpos, double ypos) {
    (void)window;
    nt_input_buffer_pointer(false, xpos, ypos, s_cached_buttons);
}

static void glfw_mouse_button_callback(GLFWwindow *window, int button, int action, int mods) {
    (void)window;
    (void)mods;

    uint8_t bit = 0;
    switch (button) {
    case GLFW_MOUSE_BUTTON_LEFT:
        bit = 1;
        break;
    case GLFW_MOUSE_BUTTON_RIGHT:
        bit = 2;
        break;
    case GLFW_MOUSE_BUTTON_MIDDLE:
        bit = 4;
        break;
    default:
        return;
    }

    if (action == GLFW_PRESS) {
        s_cached_buttons |= bit;
    } else {
        s_cached_buttons &= (uint8_t)~bit;
    }

    double xpos = 0.0;
    double ypos = 0.0;
    glfwGetCursorPos(s_glfw_window, &xpos, &ypos);

    nt_input_buffer_pointer(action == GLFW_PRESS, xpos, ypos, s_cached_buttons);
}

static void glfw_scroll_callback(GLFWwindow *window, double xoffset, double yoffset) {
    (void)window;
    /* 16x scale per RESEARCH.md to approximate web pixel-based scroll */
    nt_input_buffer_wheel((float)xoffset * 16.0F, (float)yoffset * 16.0F);
}

static void glfw_focus_callback(GLFWwindow *window, int focused) {
    (void)window;
    if (!focused) {
        s_cached_buttons = 0;
        nt_input_buffer_focus_lost();
    }
}

/* ---- Helpers ---- */

static void sync_sizes(void) {
    int win_w = 0;
    int win_h = 0;
    glfwGetWindowSize(s_glfw_window, &win_w, &win_h);

    int fb_w = 0;
    int fb_h = 0;
    glfwGetFramebufferSize(s_glfw_window, &fb_w, &fb_h);

    /* Derive DPR from actual framebuffer/logical ratio, not content scale.
       Content scale may not reflect the real framebuffer size on Windows
       when the process lacks DPI awareness. */
    float device_dpr = (win_w > 0) ? (float)fb_w / (float)win_w : 1.0F;

    if ((uint32_t)win_w == g_nt_window.width && (uint32_t)win_h == g_nt_window.height && device_dpr == g_nt_window.dpr) {
        return;
    }

    nt_window_apply_sizes((float)win_w, (float)win_h, device_dpr);
}

/* ---- Lifecycle ---- */

void nt_window_init(void) {
    if (g_nt_window.width == 0 || g_nt_window.height == 0) {
        nt_log_error("g_nt_window width/height must be set before nt_window_init()");
        __builtin_trap();
    }

    if (!glfwInit()) {
        nt_log_error("GLFW: glfwInit() failed");
        __builtin_trap();
    }

    /* GL 3.3 Core context */
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, g_nt_window.resizable ? GLFW_TRUE : GLFW_FALSE);

    const char *title = g_nt_window.title ? g_nt_window.title : "Neotolis";

    s_glfw_window = glfwCreateWindow((int)g_nt_window.width, (int)g_nt_window.height, title, NULL, NULL);
    if (!s_glfw_window) {
        nt_log_error("GLFW: glfwCreateWindow() failed");
        glfwTerminate();
        __builtin_trap();
    }

    glfwMakeContextCurrent(s_glfw_window);

    /* Query initial sizes via shared DPR logic (respects max_dpr) */
    sync_sizes();

    /* Save initial windowed position/size for fullscreen restore */
    glfwGetWindowPos(s_glfw_window, &s_windowed_x, &s_windowed_y);
    s_windowed_w = (int)g_nt_window.width;
    s_windowed_h = (int)g_nt_window.height;
    g_nt_window.platform_handle = s_glfw_window;

    /* Register GLFW callbacks */
    glfwSetKeyCallback(s_glfw_window, glfw_key_callback);
    glfwSetCursorPosCallback(s_glfw_window, glfw_cursor_pos_callback);
    glfwSetMouseButtonCallback(s_glfw_window, glfw_mouse_button_callback);
    glfwSetScrollCallback(s_glfw_window, glfw_scroll_callback);
    glfwSetWindowFocusCallback(s_glfw_window, glfw_focus_callback);
}

void nt_window_poll(void) {
    glfwPollEvents(); /* Pump OS events → callbacks buffer input */
    sync_sizes();     /* Update DPR/sizes (used by nt_input_poll drain) */
}

void nt_window_shutdown(void) {
    if (s_glfw_window) {
        glfwDestroyWindow(s_glfw_window);
        s_glfw_window = NULL;
    }
    s_cached_buttons = 0;
    glfwTerminate();
}

void nt_window_set_fullscreen(bool fullscreen) {
    /* Skip if already in the requested mode */
    bool is_currently_fullscreen = glfwGetWindowMonitor(s_glfw_window) != NULL;
    if (fullscreen == is_currently_fullscreen) {
        return;
    }

    if (fullscreen) {
        /* Save current windowed position/size ONLY if we were truly windowed */
        if (!is_currently_fullscreen) {
            glfwGetWindowPos(s_glfw_window, &s_windowed_x, &s_windowed_y);
            glfwGetWindowSize(s_glfw_window, &s_windowed_w, &s_windowed_h);
        }

        GLFWmonitor *monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode *mode = glfwGetVideoMode(monitor);
        glfwSetWindowMonitor(s_glfw_window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
        /* Restore windowed mode */
        glfwSetWindowMonitor(s_glfw_window, NULL, s_windowed_x, s_windowed_y, s_windowed_w, s_windowed_h, 0);
    }
}

/* ---- Presentation ---- */

void nt_window_swap_buffers(void) { glfwSwapBuffers(s_glfw_window); }

void nt_window_set_vsync(nt_vsync_t mode) {
    switch (mode) {
    case NT_VSYNC_OFF:
        glfwSwapInterval(0);
        break;
    case NT_VSYNC_ADAPTIVE:
        if (glfwExtensionSupported("WGL_EXT_swap_control_tear") || glfwExtensionSupported("GLX_EXT_swap_control_tear")) {
            glfwSwapInterval(-1);
        } else {
            nt_log_info("adaptive vsync not supported, falling back to vsync on");
            glfwSwapInterval(1);
        }
        break;
    case NT_VSYNC_ON: /* fall through */
    default:
        glfwSwapInterval(1);
        break;
    }
}

/* ---- Close management ---- */

bool nt_window_should_close(void) { return glfwWindowShouldClose(s_glfw_window) != 0; }

void nt_window_request_close(void) { glfwSetWindowShouldClose(s_glfw_window, GLFW_TRUE); }
