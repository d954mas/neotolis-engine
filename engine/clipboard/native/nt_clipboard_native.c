#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "clipboard/nt_clipboard.h"

/* GLFW accepts a NULL window for the global clipboard (glfw3.h §clipboard). */

const char *nt_clipboard_get_text(void) {
    const char *s = glfwGetClipboardString(NULL);
    return s != NULL ? s : "";
}

void nt_clipboard_set_text(const char *utf8) { glfwSetClipboardString(NULL, utf8 != NULL ? utf8 : ""); }

bool nt_clipboard_available(void) { return true; }
