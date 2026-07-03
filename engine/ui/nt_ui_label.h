#ifndef NT_UI_LABEL_H
#define NT_UI_LABEL_H

/* Stateless text widget. text is copied into per-frame scratch — callers may pass transient buffers. */

#include <stdint.h>

#include "clay.h"
#include "ui/nt_ui.h" /* nt_ui_element_data_t */

typedef struct nt_ui_context nt_ui_context_t;

extern const nt_ui_widget_def_t NT_UI_LABEL_DEF;

/* Decoration variant bits (DECO-05). A label has a SINGLE font_id (no B/I family), so bold is always
 * synthesized to weight -- there is no real-bold-member to select -- while underline/strike are plain
 * decoration toggles. All flow to the sticky renderer setters per draw (D-14). */
#define NT_UI_LABEL_VARIANT_BOLD (1U << 0)
#define NT_UI_LABEL_VARIANT_UNDERLINE (1U << 1)
#define NT_UI_LABEL_VARIANT_STRIKE (1U << 2)

typedef struct {
    uint16_t font_id;         /* asserted < NT_UI_MAX_FONTS */
    float font_size;          /* px; asserted > 0 */
    Clay_Color color;         /* 0..255 (Clay convention) */
    uint16_t line_height;     /* 0 = auto from font metrics */
    uint16_t letter_tracking; /* maps to Clay letterSpacing */
    uint8_t wrap_mode;        /* Clay_TextElementConfigWrapMode; 0 = WORDS */
    uint8_t align;            /* Clay_TextAlignment; 0 = LEFT */
    /* ---- Decoration (DECO-05); zero-init = plain text. Colors packed AABBGGRR (nt_color_pack) to stay
     * compact -- a Clay_Color per axis would blow the struct up 16 B each. ---- */
    uint8_t variant;        /* NT_UI_LABEL_VARIANT_* (bold/underline/strike) */
    uint8_t _pad;           /* alignment pad */
    float weight;           /* em synthetic weight; 0 + variant BOLD -> default synth-bold weight */
    float outline_w;        /* em outline width beyond the fill; 0 = no outline */
    uint32_t outline_color; /* AABBGGRR */
    float shadow_dx;        /* em shadow offset (renderer scales by size/units_per_em) */
    float shadow_dy;        /* em */
    uint32_t shadow_color;  /* AABBGGRR; alpha 0 = no shadow */
} nt_ui_label_style_t;
/* Bound RAISED from 32 to 64 deliberately for the DECO-05 decoration fields (variant + weight + outline
 * w/color + shadow dx/dy/color). Labels are immediate-mode (style passed by pointer, usually static-const),
 * NOT a dense array, so the larger struct is not a density concern. */
_Static_assert(sizeof(nt_ui_label_style_t) <= 64, "nt_ui_label_style_t fits in 64 B (raised for DECO-05 decoration fields)");

/* data may be NULL (= no layer, no user_data); built with NT_UI_DATA_LAYER / _FULL. */
void nt_ui_label(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, const char *text, const nt_ui_label_style_t *style);

/* Keeps style static-const; pair with nt_ui_fit_*. */
void nt_ui_label_sized(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, const char *text, const nt_ui_label_style_t *style, float font_size_override);

#endif /* NT_UI_LABEL_H */
