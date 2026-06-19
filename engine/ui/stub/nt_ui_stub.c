#include <stddef.h>

#include "core/nt_assert.h"
#include "ui/nt_ui.h"

#if !defined(NT_UI_DEBUG_TOOLS) || !NT_UI_DEBUG_TOOLS
#error "nt_ui_stub requires NT_UI_DEBUG_TOOLS=1"
#endif

/* Headless stub of the nt_ui probe + bbox surface for stub-linked tests (e.g. test_devapi_*) without
   pulling Clay or the renderer chain. The empty tree + not-found bbox suffice for discovery /
   bad_params tests; behavioral ui.* tests link the real nt_ui. */

#if NT_UI_DEBUG_TOOLS
uint32_t nt_ui_probe_collect(const nt_ui_context_t *ctx, nt_ui_probe_node_t *out, uint32_t cap, uint32_t *out_count) {
    (void)ctx;
    (void)out;
    (void)cap;
    if (out_count != NULL) {
        *out_count = 0U;
    }
    return 0U;
}
#endif

/* FNV-1a over the bytes — deterministic, no Clay. Real id hashing lives in the impl; the stub only
   needs a stable non-asserting value so resolve paths run. */
uint32_t nt_ui_id(const char *s) {
    NT_ASSERT(s != NULL && "nt_ui_id: string must be non-NULL");
    uint32_t h = 2166136261U;
    for (; *s != '\0'; s++) {
        h ^= (uint32_t)(unsigned char)*s;
        h *= 16777619U;
    }
    return h;
}

bool nt_ui_context_uses_raycast(const nt_ui_context_t *ctx) {
    (void)ctx;
    return false;
}

/* Headless: no Clay layout, so the coordinate-space dims are 0,0 (stub tests never inspect them). */
void nt_ui_context_layout_size(const nt_ui_context_t *ctx, float *out_w, float *out_h) {
    (void)ctx;
    NT_ASSERT(out_w != NULL && out_h != NULL && "nt_ui_context_layout_size: out pointers must be non-NULL");
    *out_w = 0.0F;
    *out_h = 0.0F;
}

/* Headless: no layout, so every id resolves not-found (ui.click/drag -> bad_params in stub tests). */
nt_ui_bbox_t nt_ui_get_bbox(const nt_ui_context_t *ctx, uint32_t id) {
    (void)ctx;
    (void)id;
    return (nt_ui_bbox_t){0};
}
