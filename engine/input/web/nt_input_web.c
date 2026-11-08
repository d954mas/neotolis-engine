#include "input/nt_input_internal.h"

#include <emscripten.h>

/* ---- Soft-keyboard hint ---- */

/* clang-format off */
EM_JS(void, nt_input_web_set_text_input_mode, (int mode), {
    var modes = ['text', 'numeric', 'email', 'url', 'text'];
    var el = Module['canvas'] || document.activeElement;
    if (!el) { return; }
    el.setAttribute('inputmode', modes[mode] || 'text');
    if (mode === 4 /* NT_TEXT_INPUT_PASSWORD */) {
        el.setAttribute('type', 'password');
    } else {
        el.removeAttribute('type');
    }
})
/* clang-format on */

/* Map the field's text-input mode to the web on-screen keyboard (canvas inputmode/type). */
void nt_input_set_text_input_mode(nt_text_input_mode_t mode) { nt_input_web_set_text_input_mode((int)mode); }
