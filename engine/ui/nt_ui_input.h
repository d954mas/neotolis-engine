#ifndef NT_UI_INPUT_H
#define NT_UI_INPUT_H

/* Single-line text field over a game-owned UTF-8 buffer (char* + capacity). Editing is codepoint-aware;
 * typed chars come from nt_input_pop_char, physical keys drive editing and focus. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atlas/nt_atlas.h" /* nt_atlas_region_ref_t */
#include "clay.h"
#include "ui/nt_ui.h"       /* nt_ui_element_data_t */
#include "ui/nt_ui_label.h" /* nt_ui_label_style_t */

typedef struct nt_ui_context nt_ui_context_t;

extern const nt_ui_widget_def_t NT_UI_INPUT_DEF;

/* Codepoint allow-predicate. Returns true to keep the codepoint, false to drop it. Must not call
 * nt_clipboard_*. NULL props->allow allows any printable (non-control) codepoint. */
typedef bool (*nt_ui_char_filter_fn)(uint32_t codepoint);

/* Mobile soft-keyboard hint. Maps to web inputmode / type=password; native no-op. */
typedef enum {
    NT_UI_KB_TEXT = 0,
    NT_UI_KB_NUMERIC,
    NT_UI_KB_EMAIL,
    NT_UI_KB_URL,
    NT_UI_KB_PASSWORD,
} nt_ui_input_keyboard_t;

/* Interaction state for the field's visual skin. Mirrors button/checkbox: bg/border vary by state,
 * resolved disabled > focused > hover > idle. A non-idle skin left ALL-ZERO inherits idle (so hover/
 * disabled look like idle until authored). */
enum { NT_UI_INPUT_IDLE = 0, NT_UI_INPUT_HOVER, NT_UI_INPUT_FOCUSED, NT_UI_INPUT_DISABLED, NT_UI_INPUT_STATE_COUNT };

/* Per-state background skin. bg_art is optional: set it and the field draws that sprite (9-slice if
 * baked) with bg_color as the TINT; leave it unset (atlas.id == 0) and the field falls back to the flat
 * bg_color rect. Colors are packed 0xAABBGGRR. */
typedef struct {
    nt_atlas_region_ref_t bg_art; /* bg sprite (atlas.id == 0 = none -> flat bg_color) */
    uint32_t bg_color;            /* flat fill when no bg_art, else sprite TINT (0 = none/untinted) */
    uint32_t border_color;        /* vector border color (0 = no border); thickness = style->border_width */
} nt_ui_input_skin_t;

/* Style for the field -- PURELY visual, so many fields can share one look. text reuses
 * nt_ui_label_style_t (font/size/color/align). Per-field behaviour lives in nt_ui_input_props_t. Layers
 * come from data->layer (bg) + the text_layer arg (mirrors button/checkbox; not in the style). */
typedef struct {
    nt_ui_label_style_t text;                         /* the entered text */
    nt_ui_label_style_t placeholder;                  /* dimmed render style for the empty-field hint (the hint STRING is in props) */
    nt_ui_input_skin_t skin[NT_UI_INPUT_STATE_COUNT]; /* [idle, hover, focused, disabled]; non-idle all-zero inherits idle */
    uint32_t caret_color;                             /* caret rect tint */
    uint32_t selection_color;                         /* selection highlight rect tint (0 = a sensible default) */
    float caret_blink_rate;                           /* seconds per full blink cycle; <= 0 = no blink (always on) */
    float caret_width;                                /* caret rect width px; asserted > 0 */
    float border_width;                               /* border thickness px (0 = no border); per-skin border_color sets the color */
    float pad_x;                                      /* horizontal inner padding px (text origin + clip bound) */
    /* Cross-fade rate across skin states (per second; 0 = instant snap). Eased in OKLab, alpha linearly;
     * a sprite bg snaps. */
    float state_speed;
} nt_ui_input_style_t;

/* Per-field behaviour -- distinct from the shared visual style, so many fields can share one
 * style but differ in validation/length/keyboard/mask. Zero-init = plain text field. */
typedef struct {
    const char *placeholder;         /* empty-field hint STRING (NULL/"" = none); dimmed via style->placeholder */
    nt_ui_char_filter_fn allow;      /* codepoint filter; NULL = allow printable */
    size_t max_length;               /* max BYTES incl. NUL room; 0 = bound by buffer_size */
    nt_ui_input_keyboard_t keyboard; /* soft-keyboard hint (mobile) */
    bool password;                   /* render a mask glyph per codepoint */
} nt_ui_input_props_t;

/* Valid baseline style (visible caret, padding, blink, no art); avoids the zero-init trap
 * (caret_width must be > 0). The caller supplies font ids and colors. */
nt_ui_input_style_t nt_ui_input_style_defaults(void);

/* Edits a game-owned NUL-terminated UTF-8 buffer in place. buffer_size is the FULL
 * capacity incl. the NUL terminator; inserts clamp to buffer_size-1 and never split a
 * multi-byte codepoint. Only a FOCUSED field consumes typed chars + editing keys.
 *
 * Returns true the frame the buffer mutated. A non-NULL out_submitted receives Enter (after the
 * frame's edits). enabled=false = inert occluder, no edit. The placeholder shows only while the
 * buffer is empty and the field is unfocused.
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

#endif /* NT_UI_INPUT_H */
