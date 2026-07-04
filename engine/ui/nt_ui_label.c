#include "ui/nt_ui_label.h"

#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "color/nt_color.h" /* nt_color_unpack: packed AABBGGRR -> float[4] for the setters */
#include "core/nt_assert.h"
#include "font/nt_font.h"
#include "memory/nt_mem_scratch.h"
#include "renderers/nt_text_renderer.h" /* sticky decoration setters */
#include "ui/nt_ui_clay_impl.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_rich_text.h" /* NT_UI_RICH_SYNTH_BOLD_WEIGHT: shared synth-bold weight */

const nt_ui_widget_def_t NT_UI_LABEL_DEF = {
    .name = "nt_label",
    .pill_color = 0xFFC88C5AU,
    ._reserved = 0U,
};

/* Per-frame cap on decorated labels; over-cap drops decoration (text still renders), never OOBs. */
#ifndef NT_UI_LABEL_MAX_DECO
#define NT_UI_LABEL_MAX_DECO 128
#endif

static bool label_style_has_decoration(const nt_ui_label_style_t *s) { return s->variant != 0U || s->weight != 0.0F || s->outline_w > 0.0F || (s->shadow_color >> 24) != 0U; }

/* Record a decorated label into the frame-scratch side table, keyed by the emitted text pointer (the
 * walker matches Clay_TextRenderData.stringContents against it). Lazy-allocates the table on first use. */
static void label_record_deco(nt_ui_context_t *ctx, const char *text, const nt_ui_label_style_t *s) {
    if (ctx->label_deco == NULL) {
        ctx->label_deco = nt_mem_scratch_alloc(sizeof(nt_ui_label_deco_t) * NT_UI_LABEL_MAX_DECO, alignof(nt_ui_label_deco_t));
        ctx->label_deco_count = 0U;
    }
    if (ctx->label_deco == NULL || ctx->label_deco_count >= NT_UI_LABEL_MAX_DECO) {
        /* Fail early in debug so a too-small cap surfaces; release stays a safety net (drop decoration,
         * text still renders) rather than writing past the table. Raise NT_UI_LABEL_MAX_DECO if hit. */
        NT_ASSERT(0 && "nt_ui_label: decorated-label cap (NT_UI_LABEL_MAX_DECO) reached or scratch exhausted");
        return;
    }
    nt_ui_label_deco_t *d = &((nt_ui_label_deco_t *)ctx->label_deco)[ctx->label_deco_count++];
    d->text = text;
    d->variant = s->variant;
    d->weight = s->weight;
    d->outline_w = s->outline_w;
    d->outline_color = s->outline_color;
    d->shadow_dx = s->shadow_dx;
    d->shadow_dy = s->shadow_dy;
    d->shadow_color = s->shadow_color;
}

const nt_ui_label_deco_t *nt_ui_label_deco_lookup(const nt_ui_context_t *ctx, const char *text) {
    if (ctx == NULL || ctx->label_deco == NULL || text == NULL) {
        return NULL;
    }
    const nt_ui_label_deco_t *arr = (const nt_ui_label_deco_t *)ctx->label_deco;
    for (uint32_t i = 0; i < ctx->label_deco_count; i++) {
        if (arr[i].text == text) {
            return &arr[i]; /* pointer identity: each label owns a distinct scratch buffer */
        }
    }
    return NULL;
}

void nt_ui_label_deco_apply(const nt_ui_label_deco_t *d, float opacity) {
    const float zero[4] = {0.0F, 0.0F, 0.0F, 0.0F};
    /* A label has one font_id (no B/I family), so bold is always synthesized to weight (cascade
     * degenerates to synth). Explicit weight overrides; else the BOLD bit picks the shared synth weight. */
    float weight = d->weight;
    if (weight == 0.0F && (d->variant & NT_UI_LABEL_VARIANT_BOLD) != 0U) {
        weight = NT_UI_RICH_SYNTH_BOLD_WEIGHT;
    }
    nt_text_renderer_set_weight(weight);
    /* Fold parent opacity into outline/shadow alpha to match the walker's fill fade (the walker
     * pre-multiplies only textColor.a) — else a fading panel keeps opaque outline/shadow. */
    if (d->outline_w > 0.0F) {
        float c[4];
        nt_color_unpack(d->outline_color, c);
        c[3] *= opacity;
        nt_text_renderer_set_outline(d->outline_w, c);
    } else {
        nt_text_renderer_set_outline(0.0F, zero);
    }
    if ((d->shadow_color >> 24) != 0U) { /* alpha > 0 -> active */
        float c[4];
        nt_color_unpack(d->shadow_color, c);
        c[3] *= opacity;
        nt_text_renderer_set_shadow(d->shadow_dx, d->shadow_dy, 0.0F, c);
    } else {
        nt_text_renderer_set_shadow(0.0F, 0.0F, 0.0F, zero);
    }
    nt_text_renderer_set_underline((d->variant & NT_UI_LABEL_VARIANT_UNDERLINE) != 0U);
    nt_text_renderer_set_strikethrough((d->variant & NT_UI_LABEL_VARIANT_STRIKE) != 0U);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_label(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, const char *text, const nt_ui_label_style_t *style) {
    NT_ASSERT(ctx != NULL && "nt_ui_label: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_label: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(style != NULL && "nt_ui_label: style must be non-NULL");
    NT_ASSERT(text != NULL && "nt_ui_label: text must be non-NULL (use \"\" for empty)");
    NT_ASSERT(style->font_id < NT_UI_MAX_FONTS && "nt_ui_label: font_id out of registry range");
    NT_ASSERT(nt_font_valid(ctx->fonts[style->font_id]) && "nt_ui_label: font slot empty; call nt_ui_set_font first");
    /* Upper bound headroom for the +0.5F round before cast to uint16_t. */
    NT_ASSERT(isfinite(style->font_size) && style->font_size > 0.0F && style->font_size <= (float)UINT16_MAX - 0.5F && "nt_ui_label: font_size must be finite in (0, UINT16_MAX - 0.5]");

    const uint16_t clay_font_size = (uint16_t)(style->font_size + 0.5F);

    /* Clay stores .chars by pointer and the walker reads after declaration returns; copy
     * into per-frame scratch so callers can pass snprintf'd stack buffers safely. */
    const size_t text_len = strlen(text);
    char *owned = (char *)nt_mem_scratch_alloc(text_len + 1U, _Alignof(char));
    NT_ASSERT(owned != NULL && "nt_ui_label: scratch alloc failed (label text)");
    memcpy(owned, text, text_len + 1U);
    /* Record decoration keyed by the owned text pointer BEFORE CLAY_TEXT copies the Clay_String by value
     * (Clay keeps .chars by pointer, so the walker matches this exact buffer at emit). */
    if (label_style_has_decoration(style)) {
        label_record_deco(ctx, owned, style);
    }
    Clay_String s = {.length = (int32_t)text_len, .chars = owned};
    CLAY_TEXT(s, CLAY_TEXT_CONFIG({
                     .userData = (void *)data,
                     .textColor = style->color,
                     .fontId = style->font_id,
                     .fontSize = clay_font_size,
                     .letterSpacing = style->letter_tracking,
                     .lineHeight = style->line_height,
                     .wrapMode = (Clay_TextElementConfigWrapMode)style->wrap_mode,
                     .textAlignment = (Clay_TextAlignment)style->align,
                 }));

    /* CLAY_TEXT doesn't push on the open stack — use last_emitted_element_id. */
    nt_ui_widget_register(ctx, nt_ui_internal_last_emitted_element_id(), &NT_UI_LABEL_DEF, NULL, true);
}

void nt_ui_label_sized(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, const char *text, const nt_ui_label_style_t *style, float font_size_override) {
    NT_ASSERT(ctx != NULL && "nt_ui_label_sized: ctx must be non-NULL");
    NT_ASSERT(style != NULL && "nt_ui_label_sized: style must be non-NULL");
    NT_ASSERT(isfinite(font_size_override) && font_size_override > 0.0F && font_size_override <= (float)UINT16_MAX - 0.5F &&
              "nt_ui_label_sized: font_size_override must be finite in (0, UINT16_MAX - 0.5]");
    nt_ui_label_style_t local = *style;
    local.font_size = font_size_override;
    nt_ui_label(ctx, data, text, &local);
}
