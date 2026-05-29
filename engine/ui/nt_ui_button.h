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
#include "ui/nt_ui.h"       /* nt_ui_element_data_t */
#include "ui/nt_ui_label.h" /* leaf sugar child style */

typedef struct nt_ui_context nt_ui_context_t;

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
} nt_ui_button_style_t;

/* Container: begin -> children -> bool end. id from nt_ui_id("...") (never 0).
 * enabled=false short-circuits hover + click (D-56-12) and forces the disabled
 * visual; begin still pushes transform/opacity so end stays balanced. */
void nt_ui_button_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint32_t id, nt_resource_t atlas, const nt_ui_button_style_t *style, bool enabled);
bool nt_ui_button_end(nt_ui_context_t *ctx);

/* Leaf sugar (text-only common case) = begin + centered label child + end.
 * Returns clicked (one-shot on release within bounds). */
bool nt_ui_button(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint32_t id, nt_resource_t atlas, const char *label, const nt_ui_label_style_t *label_style,
                  const nt_ui_button_style_t *style, bool enabled);

#endif /* NT_UI_BUTTON_H */
