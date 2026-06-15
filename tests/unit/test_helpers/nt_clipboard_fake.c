/* In-memory clipboard impl for hermetic UI tests: set/get round-trip a bounded static
 * buffer with no OS/GLFW dependency (the real native backend is headless-flaky, the stub
 * never round-trips). Linked by the nt_ui test exes so Ctrl+C/X/V paths are deterministic. */

#include "clipboard/nt_clipboard.h"

#include <string.h>

#define NT_CLIPBOARD_FAKE_CAP 4096

static char s_buf[NT_CLIPBOARD_FAKE_CAP] = {0};
static bool s_fake_available = true; /* test-controllable; default mirrors a real backend */

/* Test-only: flip availability to exercise the clipboard-unavailable code paths. */
void nt_clipboard_fake_set_available(bool available) { s_fake_available = available; }

bool nt_clipboard_available(void) { return s_fake_available; }

const char *nt_clipboard_get_text(void) { return s_buf; }

void nt_clipboard_set_text(const char *utf8) {
    if (utf8 == NULL) {
        s_buf[0] = '\0';
        return;
    }
    size_t n = strlen(utf8);
    if (n >= NT_CLIPBOARD_FAKE_CAP) {
        n = NT_CLIPBOARD_FAKE_CAP - 1U;
    }
    memcpy(s_buf, utf8, n);
    s_buf[n] = '\0';
}
