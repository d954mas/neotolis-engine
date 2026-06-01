#ifndef NT_UI_BUTTON_H
#define NT_UI_BUTTON_H

/* Interactive container button (D-56-10). A near-clone of nt_ui_panel_begin/end
 * whose ONE structural addition is the Clay element .id (so the engine hit-test
 * finds it via Clay_GetElementData). The button runs the state machine +
 * transitions automatically (D-56-14): query interaction (nt_ui_get_interaction),
 * pick state (disabled?:pressed?:hover?:idle), ease the picked state's
 * scale/offset/opacity via the anim cache (nt_ui_anim), and apply via
 * push_transform + push_opacity + the slice9 bg region/tint. Content
 * (label / icon / icon+text) is composed as CHILDREN between begin/end. This is
 * a thin convenience wrapper over nt_ui_get_interaction (D-56-21): everything it
 * does, a game could do by hand. Per-state descriptors are intrinsic to
 * interaction (universal), NOT a central variant taxonomy (D-56-02, Model D). */

#include <stdbool.h>
#include <stdint.h>

#include "clay.h"
#include "resource/nt_resource.h"
#include "ui/nt_ui.h" /* nt_ui_element_data_t */

typedef struct nt_ui_context nt_ui_context_t;

/* EXPERIMENTAL (Phase 56 ext): API surface may change in v1.9. Used by
 * ui_buttons_demo and inspector internals. Game code adopting this should
 * pin the engine version.
 *
 * Inspector descriptor: pill name "nt_button" + color shown in the element
 * tree. Engine widget defs use the "nt_" prefix to disambiguate from Clay's
 * own config-type pills (Clay's "Image" / "Text" / etc. in the same row).
 * Game code may compare pointer identity against this symbol if it needs to
 * filter by "is this a button" without parsing names. */
extern const nt_ui_widget_def_t NT_UI_BUTTON_DEF;

/* Per-state CONTAINER visual only (D-56-11). Layout/sizing/padding live on the
 * Clay begin element (game-supplied), NOT in the style -- matches panel. */
typedef struct {
    uint32_t bg_region; /* slice9 region index (sprite-swap per state); 0 = same as idle */
    uint32_t bg_tint;   /* 0xAABBGGRR */
    float scale;        /* render scale, e.g. pressed -> 0.95 */
    float offset_x, offset_y;
    float opacity; /* per-state opacity (INHERITS to content, D-56-13) */
} nt_ui_btn_state_t;

typedef struct {
    nt_ui_btn_state_t idle, hover, pressed, disabled;
    float transition_speed; /* 0 = instant; >0 = eased via the anim cache */
    /* Phase 56 ext: touch-target hit-zone inflation in layout pixels
     * {left, right, top, bottom}. {0,0,0,0} = no padding (visual = hit).
     * Inflated in layout space BEFORE the inverse-affine, so the padded zone
     * rotates with the widget. Each component >= 0 (asserted). Forwarded
     * straight to nt_ui_get_interaction_padded on the enabled path. */
    int16_t hit_padding_lrtb[4];
} nt_ui_button_style_t;
/* Catches accidental growth past the documented field set. 4 nt_ui_btn_state_t
 * (24 B each = 96) + transition_speed (4) + hit_padding_lrtb (8) = 108 B.
 * Concrete number rather than expression to flag any silent padding shift. */
_Static_assert(sizeof(nt_ui_button_style_t) == 108, "nt_ui_button_style_t expected size 108 B (4*24 state + 4 speed + 8 padding); update on intentional field changes");

/* Container: begin -> children -> bool end. id from nt_ui_id("...") (never 0).
 * enabled=false short-circuits hover + click (D-56-12) and forces the disabled
 * visual; begin still pushes transform/opacity so end stays balanced.
 *
 * decl (Phase 56 ext, P3-2): optional Clay_ElementDeclaration. NULL = FIT default
 * (button shrinks to children). Pass a decl with .layout.sizing to drive a
 * FIXED/GROW size, padding, child alignment, etc. The engine OWNS .id, .image,
 * .backgroundColor, .userData fields -- caller MUST leave them zero/NULL or the
 * asserts fire. All other fields (layout, scroll, floating, border, clip) flow
 * through verbatim. */
/* State-dependent content (label "Save" -> "Saving..." on press, icon swap):
 * query the UNIVERSAL interaction service BEFORE button_begin. The same
 * pattern works for any interactive widget (toggle, slider, drag-handle,
 * custom). nt_ui_get_interaction is per-id-per-frame cached -- duplicate
 * calls are free.
 *
 *   nt_ui_interaction_t in = nt_ui_get_interaction(ctx, btn_id);
 *   nt_ui_button_begin(ctx, ..., btn_id, atlas, &style, &decl, true);
 *     nt_ui_label(ctx, NULL, in.pressed ? "Saving..." : "Save", &lbl);
 *   clicked = nt_ui_button_end(ctx);
 */
void nt_ui_button_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint32_t id, nt_resource_t atlas, const nt_ui_button_style_t *style, const Clay_ElementDeclaration *decl, bool enabled);
bool nt_ui_button_end(nt_ui_context_t *ctx);

#endif /* NT_UI_BUTTON_H */
