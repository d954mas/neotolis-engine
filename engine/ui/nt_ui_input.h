#ifndef NT_UI_INPUT_H
#define NT_UI_INPUT_H

/* Single-line text-field widget. The string lives in the GAME (char* + cap, ImGui-style);
 * the engine state pool holds only the transient caret/scroll/blink + the focus arbiter id.
 * Caret motion + hit-test + edit are CODEPOINT-aware (nt_utf8) so multi-byte UTF-8 (e.g.
 * Cyrillic) never corrupts. Typed chars arrive via nt_input_pop_char; physical keys
 * (Backspace/Delete/arrows/Home/End/Enter/Tab/Esc) drive editing + focus. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atlas/nt_atlas.h" /* nt_atlas_region_ref_t */
#include "clay.h"
#include "ui/nt_ui.h"       /* nt_ui_element_data_t */
#include "ui/nt_ui_label.h" /* nt_ui_label_style_t */

typedef struct nt_ui_context nt_ui_context_t;

extern const nt_ui_widget_def_t NT_UI_INPUT_DEF;

/* Codepoint allow-predicate. Returns true to keep the codepoint, false to drop it on
 * insert. NULL props->allow = allow any printable (non-control) codepoint. */
typedef bool (*nt_ui_char_filter_fn)(uint32_t codepoint);

/* Mobile soft-keyboard hint. Maps to web inputmode / type=password; native no-op. */
typedef enum {
    NT_UI_KB_TEXT = 0,
    NT_UI_KB_NUMERIC,
    NT_UI_KB_EMAIL,
    NT_UI_KB_URL,
    NT_UI_KB_PASSWORD,
} nt_ui_input_keyboard_t;

/* Style for the field -- PURELY visual, so many fields can share one look. text reuses
 * nt_ui_label_style_t (font/size/color/align). Colors are packed 0xAABBGGRR. bg_art is optional: set it
 * and the field draws that sprite (9-slice if baked) with bg_color as the TINT; leave it unset (atlas.id
 * == 0) and the field falls back to the flat bg_color rect. Per-field behaviour lives in nt_ui_input_props_t. */
typedef struct {
    nt_ui_label_style_t text;             /* the entered text */
    nt_ui_label_style_t placeholder;      /* dimmed render style for the empty-field hint (the hint STRING is in props) */
    uint32_t bg_color;                    /* idle background: flat fill when no bg_art, else sprite TINT (0 = untinted) */
    uint32_t focused_bg_color;            /* focused background (fill or tint) */
    uint32_t border_color;                /* idle border (0 = none) */
    uint32_t focused_border_color;        /* focused border */
    uint32_t caret_color;                 /* caret rect tint */
    uint32_t selection_color;             /* selection highlight rect tint (0 = a sensible default) */
    nt_atlas_region_ref_t bg_art;         /* idle bg sprite (atlas.id == 0 = none -> flat bg_color) */
    nt_atlas_region_ref_t focused_bg_art; /* focused bg sprite (atlas.id == 0 = none -> flat focused_bg_color) */
    float caret_blink_rate;               /* seconds per full blink cycle; <= 0 = no blink (always on) */
    float caret_width;                    /* caret rect width px; asserted > 0 */
    float border_width;                   /* border thickness px (0 = no border) */
    float pad_x;                          /* horizontal inner padding px (text origin + clip bound) */
    float pad_y;                          /* vertical padding px: UNUSED -- the line is auto-centered, vertical margin = (field_h - line_h)/2 */
} nt_ui_input_style_t;
_Static_assert(sizeof(nt_ui_input_style_t) >= 48, "nt_ui_input_style_t stable ABI");

/* Per-field behaviour -- distinct from the shared visual style, so many fields can share one
 * style but differ in validation/length/keyboard/mask. Zero-init = plain text field. */
typedef struct {
    const char *placeholder;         /* empty-field hint STRING (NULL/"" = none); dimmed via style->placeholder */
    nt_ui_char_filter_fn allow;      /* codepoint filter; NULL = allow printable */
    size_t max_length;               /* max BYTES incl. NUL room; 0 = bound by buffer_size */
    nt_ui_input_keyboard_t keyboard; /* soft-keyboard hint (mobile) */
    bool password;                   /* render a mask glyph per codepoint */
} nt_ui_input_props_t;

/* Valid baseline style: visible caret, sensible padding/blink, no art, allow-all. The caller
 * supplies font ids + colors. Avoids the zero-init trap (caret_width must be > 0). */
nt_ui_input_style_t nt_ui_input_style_defaults(void);

/* Edits a game-owned NUL-terminated UTF-8 buffer in place. buffer_size is the FULL
 * capacity incl. the NUL terminator; inserts clamp to buffer_size-1 and never split a
 * multi-byte codepoint. Only a FOCUSED field consumes typed chars + editing keys.
 *
 * Returns true the frame the buffer mutated (on_change). Pass on_submit non-NULL to receive
 * an Enter event (fires after the frame's edits). enabled=false = inert occluder, no edit.
 *
 * props carries per-field behaviour (allow filter, max_length, keyboard, password) and the
 * placeholder CONTENT (props->placeholder; NULL or "" = no hint). The hint's dimmed appearance
 * comes from style->placeholder; it shows only when the buffer is EMPTY and the field is NOT focused.
 *
 * Engine owns the decl id/userData; data->flags must NOT set HAS_TRANSFORM/HAS_OPACITY. */
bool nt_ui_input_text(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t text_layer, uint32_t id, char *buffer, size_t buffer_size, const nt_ui_input_props_t *props,
                      const nt_ui_input_style_t *style, const Clay_ElementDeclaration *decl, bool enabled, bool *out_submitted);

/* True if `id` currently holds keyboard focus. */
bool nt_ui_input_focused(const nt_ui_context_t *ctx, uint32_t id);

/* True if ANY text field currently holds keyboard focus. Lets the host gate a global Esc=quit so
 * Esc unfocuses a field first (the focus state is the previous frame's, read before nt_ui_begin). */
bool nt_ui_input_any_focused(const nt_ui_context_t *ctx);

/* Stock allow-predicates. numeric = [0-9.+-]; email = alnum + @._%+-; url = alnum +
 * a small URL-safe punctuation set. */
bool nt_ui_filter_numeric(uint32_t codepoint);
bool nt_ui_filter_email(uint32_t codepoint);
bool nt_ui_filter_url(uint32_t codepoint);

/* Generic double-click + long-press detector, keyed by widget id in the state pool.
 * Feed it the press/release edges + the current pointer pos; it tracks last-press time +
 * origin and reports the edges. long_press_secs <= 0 disables the long-press report.
 * Reused by word-select so it is built generically, not inlined in the field. */
typedef struct {
    bool double_clicked; /* a second press landed within the dbl window + radius */
    bool long_pressed;   /* held past long_press_secs without moving past the radius */
} nt_ui_click_gesture_t;

nt_ui_click_gesture_t nt_ui_dblclick_longpress(nt_ui_context_t *ctx, uint32_t id, bool pressed_now, bool released_now, bool held, float pos_x, float pos_y, float dbl_window_secs,
                                               float long_press_secs, float move_radius_px);

#ifdef NT_TEST_ACCESS
/* Test probe: builds the password mask render string (one mask glyph per codepoint of
 * [buffer,len)) into frame scratch. Lets the password branch be asserted without a GL capture. */
const char *nt_ui_input_build_display_text(const char *buffer, uint32_t len);

/* Test probe: the hint string the field would render for the given (buffer, placeholder, focused) --
 * the placeholder only when empty AND unfocused, else NULL. Asserts the empty/placeholder branch
 * (which display string the compose path picks) without a GL capture. */
const char *nt_ui_input_placeholder_for(const char *buffer, const char *placeholder, bool focused);

/* Test probe: read the live caret byte offset + drag flag for `id`'s field state (post-frame, no
 * alloc). Returns false if the field has no state slot yet. Asserts the focus-loss-abandons-drag
 * path and caret math without a GL capture. Either out pointer may be NULL. */
bool nt_ui_input_test_state(nt_ui_context_t *ctx, uint32_t id, uint32_t *out_caret, uint8_t *out_drag);
#endif

#endif /* NT_UI_INPUT_H */
