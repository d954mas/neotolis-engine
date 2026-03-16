#include "input/nt_input_internal.h"

#define GLFW_INCLUDE_NONE
#include "window/nt_window.h"
#include <GLFW/glfw3.h>

#define g_nt_glfw_window ((GLFWwindow *)g_nt_window.platform_handle)

/* ---- Mouse button mask tracking ---- */

static uint8_t s_cached_buttons;

/* ---- Key mapping ---- */

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

/* ---- GLFW Callbacks ---- */

static void glfw_key_callback(GLFWwindow *window, int key, int scancode, int action, int mods) {
    (void)window;
    (void)scancode;
    (void)mods;
    nt_key_t nt_key = glfw_key_to_nt(key);
    if (nt_key < NT_KEY_COUNT) {
        nt_input_set_key(nt_key, action != GLFW_RELEASE);
    }
}

static void glfw_cursor_pos_callback(GLFWwindow *window, double xpos, double ypos) {
    (void)window;
    /* Use g_nt_window.dpr (fb/window ratio) not content scale — they diverge
       on Windows without DPI awareness. Must match window's coordinate space. */
    float dpr = g_nt_window.dpr;
    float fx = (float)xpos * dpr;
    float fy = (float)ypos * dpr;
    nt_input_pointer_move(0, fx, fy, 1.0F, NT_POINTER_MOUSE, s_cached_buttons);
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

    /* Get cursor position in framebuffer coords */
    double xpos = 0.0;
    double ypos = 0.0;
    glfwGetCursorPos(g_nt_glfw_window, &xpos, &ypos);
    float dpr = g_nt_window.dpr;
    float fx = (float)xpos * dpr;
    float fy = (float)ypos * dpr;

    if (action == GLFW_PRESS) {
        nt_input_pointer_down(0, fx, fy, 1.0F, NT_POINTER_MOUSE, s_cached_buttons);
    } else {
        /* Mouse stays active after release -- send move, not up */
        nt_input_pointer_move(0, fx, fy, 1.0F, NT_POINTER_MOUSE, s_cached_buttons);
    }
}

static void glfw_scroll_callback(GLFWwindow *window, double xoffset, double yoffset) {
    (void)window;
    /* 16x scale per RESEARCH.md to approximate web pixel-based scroll */
    nt_input_wheel((float)xoffset * 16.0F, (float)yoffset * 16.0F);
}

static void glfw_focus_callback(GLFWwindow *window, int focused) {
    (void)window;
    if (!focused) {
        nt_input_clear_all_keys();
        nt_input_clear_all_pointers();
        s_cached_buttons = 0;
    }
}

/* ---- Platform lifecycle ---- */

void nt_input_platform_init(void) {
    glfwSetKeyCallback(g_nt_glfw_window, glfw_key_callback);
    glfwSetCursorPosCallback(g_nt_glfw_window, glfw_cursor_pos_callback);
    glfwSetMouseButtonCallback(g_nt_glfw_window, glfw_mouse_button_callback);
    glfwSetScrollCallback(g_nt_glfw_window, glfw_scroll_callback);
    glfwSetWindowFocusCallback(g_nt_glfw_window, glfw_focus_callback);
}

void nt_input_platform_poll(void) {
    /* glfwPollEvents must run HERE, after nt_input_poll() clears edge flags.
       Callbacks fire during this call and set fresh input state. */
    glfwPollEvents();
}

void nt_input_platform_shutdown(void) { /* No-op. Callbacks are automatically unregistered when GLFW window is destroyed. */ }
