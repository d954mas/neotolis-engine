#include "platform/web/nt_platform_web.h"

#include <emscripten.h>

/* clang-format off */
EM_JS(void, nt_platform_web_loading_complete, (void), {
    var el = document.getElementById('spinner');
    if (el) el.style.display = 'none';
})
/* clang-format on */
