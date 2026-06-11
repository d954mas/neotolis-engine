#ifndef NT_UI_INSPECTOR_H
#define NT_UI_INSPECTOR_H

/* Post-walk overlay + sidebar (Clay debug view port). Gated by NT_UI_DEBUG_TOOLS. */

#include <stdbool.h>
#include <stdint.h>

#include "font/nt_font.h"
#include "ui/nt_ui.h"

typedef struct nt_ui_context nt_ui_context_t;

/* Max RECT<->TEXT pipeline alternations the sidebar emits for the demo-shaped scene
 * (pill-bg dropped, colored text only). Each fixed text-bearing chrome row (title, "UI
 * memory" line) costs a RECT-bg + TEXT pair — bump this by 2 here, with the row, not in
 * the test. The perf test asserts against it so a per-row pill regression trips. */
#define NT_UI_INSPECTOR_ALTERNATION_CAP 22U

/* Visible in both modes so callers keep a local instance even when stubs are no-op. */
typedef struct nt_ui_inspector_metrics_t {
    float panel_width;     /* default 400 */
    float row_height;      /* default 30 */
    uint16_t font_size;    /* default 16 */
    uint8_t outer_padding; /* default 10 */
    uint8_t indent_width;  /* default 16 */
} nt_ui_inspector_metrics_t;

#if NT_UI_DEBUG_TOOLS

void nt_ui_inspector_set_active(nt_ui_context_t *ctx, bool on);
bool nt_ui_inspector_is_active(const nt_ui_context_t *ctx);

/* True when active AND pointer is inside the sidebar. step_interaction honors this internally. */
bool nt_ui_inspector_pointer_consumed(const nt_ui_context_t *ctx);

extern const nt_ui_inspector_metrics_t NT_UI_INSPECTOR_METRICS_DEFAULT;

void nt_ui_inspector_set_metrics(nt_ui_context_t *ctx, const nt_ui_inspector_metrics_t *metrics);

/* Optional overlay materials for the inspector — typically depth_test=false so the debug view stays
 * on top without testing the game's 3D depth (a passive overlay, no depth-buffer side effects).
 * Pass NT_MATERIAL_INVALID for either to fall back to the game's sprite/text material. */
void nt_ui_inspector_set_materials(nt_ui_context_t *ctx, nt_material_t sprite, nt_material_t text);

/* Called by nt_ui_end if active; not for game code. */
void nt_ui_inspector_emit_layout(nt_ui_context_t *ctx);

/* Draw inspector/debug layers after the game's regular UI pass. Caller must bind a
 * screen-space view_proj first, e.g. nt_ui_make_screen_view_proj(...). */
void nt_ui_debug_inspector_walk(nt_ui_context_t *ctx, const nt_ui_target_t *target);

/* Call AFTER debug inspector walk, BEFORE nt_gfx_end_pass. label_size <= 0 skips label.
 * Side effect: leaves the sprite/text renderers bound to the inspector (or game) materials and does
 * NOT restore the prior binding — call it as the terminal UI draw of the pass, or rebind your own
 * materials afterward. */
void nt_ui_inspector_overlay_draw(nt_ui_context_t *ctx, const nt_ui_target_t *target, nt_font_t font, float label_size);

#else /* NT_UI_DEBUG_TOOLS */

#include "log/nt_log.h"

/* set_active(true) warns every call — release-build users see why the toggle does nothing.
 * Earlier "once" guards lived in per-TU statics and lied (fired once per TU, not per process). */
static inline void nt_ui_inspector_set_active(nt_ui_context_t *ctx, bool on) {
    (void)ctx;
    if (on) {
        nt_log_warn("nt_ui_inspector: NT_UI_DEBUG_TOOLS=OFF in this build — inspector excluded; toggle is a no-op. Rebuild with -DNT_UI_DEBUG_TOOLS=ON.");
    }
}
static inline bool nt_ui_inspector_is_active(const nt_ui_context_t *ctx) {
    (void)ctx;
    return false;
}
static inline bool nt_ui_inspector_pointer_consumed(const nt_ui_context_t *ctx) {
    (void)ctx;
    return false;
}
static inline void nt_ui_inspector_set_metrics(nt_ui_context_t *ctx, const nt_ui_inspector_metrics_t *metrics) {
    (void)ctx;
    (void)metrics;
    nt_log_warn("nt_ui_inspector: NT_UI_DEBUG_TOOLS=OFF in this build — metrics ignored. Rebuild with -DNT_UI_DEBUG_TOOLS=ON.");
}
static inline void nt_ui_inspector_set_materials(nt_ui_context_t *ctx, nt_material_t sprite, nt_material_t text) {
    (void)ctx;
    (void)sprite;
    (void)text;
}
static inline void nt_ui_inspector_emit_layout(nt_ui_context_t *ctx) { (void)ctx; }
static inline void nt_ui_debug_inspector_walk(nt_ui_context_t *ctx, const nt_ui_target_t *target) {
    (void)ctx;
    (void)target;
}
static inline void nt_ui_inspector_overlay_draw(nt_ui_context_t *ctx, const nt_ui_target_t *target, nt_font_t font, float label_size) {
    (void)ctx;
    (void)target;
    (void)font;
    (void)label_size;
}

#endif /* NT_UI_DEBUG_TOOLS */

#endif /* NT_UI_INSPECTOR_H */
