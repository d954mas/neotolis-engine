#include "ui/nt_ui_fit.h"

#include <string.h>

#include "core/nt_assert.h"
#include "font/nt_font.h"
#include "ui/nt_ui_internal.h" /* ctx->fonts[] */

static bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

typedef struct {
    uint32_t lines;
    float max_line_w;
} wrap_result_t;

/* Mirrors Clay CLAY_TEXT_WRAP_WORDS: greedy-pack words to container_w, \n forces
 * break, leading whitespace on wrapped lines is stripped. Returns line count AND
 * max measured line width -- callers need both (long single word with no wrap
 * point still has a width that may exceed container_w). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static wrap_result_t simulate_wrap(nt_font_t font, const char *text, size_t len, float size, float letter_tracking, float container_w) {
    if (len == 0U || container_w <= 0.0F) {
        return (wrap_result_t){.lines = 0U, .max_line_w = 0.0F};
    }
    const float space_w = nt_font_measure_n(font, " ", 1U, size, letter_tracking).width;

    uint32_t lines = 1U;
    float line_w = 0.0F;
    float max_line_w = 0.0F;
    size_t i = 0U;
    while (i < len) {
        /* Skip leading whitespace at start of line; \n still breaks. */
        if (line_w == 0.0F) {
            while (i < len && is_ws(text[i]) && text[i] != '\n') {
                i++;
            }
            if (i >= len) {
                break;
            }
        }

        if (text[i] == '\n') {
            if (line_w > max_line_w) {
                max_line_w = line_w;
            }
            lines++;
            line_w = 0.0F;
            i++;
            continue;
        }

        /* Mid-line whitespace (multiple spaces, tabs): skip one and continue.
         * Without this, i wouldn't advance and we'd infinite-loop. We do NOT
         * add another space_w to line_w -- consecutive whitespace collapses. */
        if (is_ws(text[i])) {
            i++;
            continue;
        }

        const size_t word_start = i;
        while (i < len && !is_ws(text[i])) {
            i++;
        }
        const size_t word_end = i;
        const float word_w = nt_font_measure_n(font, text + word_start, word_end - word_start, size, letter_tracking).width;
        const float prospective = (line_w == 0.0F) ? word_w : (line_w + space_w + word_w);
        if (prospective > container_w && line_w > 0.0F) {
            if (line_w > max_line_w) {
                max_line_w = line_w;
            }
            lines++;
            line_w = word_w;
        } else {
            line_w = prospective;
        }
        /* Trailing whitespace is consumed by the next loop iteration's
         * mid-line whitespace branch (or skipped as leading on next line). */
    }
    if (line_w > max_line_w) {
        max_line_w = line_w;
    }
    return (wrap_result_t){.lines = lines, .max_line_w = max_line_w};
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
uint16_t nt_ui_fit_width(nt_ui_context_t *ctx, uint16_t font_id, const char *text, float container_w, uint16_t size_min, uint16_t size_max, float letter_tracking) {
    NT_ASSERT(ctx != NULL && "nt_ui_fit_width: ctx must be non-NULL");
    NT_ASSERT(font_id < NT_UI_MAX_FONTS && "nt_ui_fit_width: font_id out of range");
    NT_ASSERT(size_min > 0U && size_max >= size_min && "nt_ui_fit_width: 0 < size_min <= size_max");
    NT_ASSERT(container_w > 0.0F && "nt_ui_fit_width: container_w must be > 0");
    if (text == NULL || text[0] == '\0') {
        return size_max;
    }
    nt_font_t font = ctx->fonts[font_id];
    NT_ASSERT(nt_font_valid(font) && "nt_ui_fit_width: font slot empty; bind via nt_ui_set_font first");

    const size_t len = strlen(text);

    /* Fits single line at size_max means: no wrap point fired AND the single
     * line's measured width is within container_w (catches long words too). */
    const wrap_result_t r_max = simulate_wrap(font, text, len, (float)size_max, letter_tracking, container_w);
    if (r_max.lines <= 1U && r_max.max_line_w <= container_w) {
        return size_max;
    }

    uint16_t lo = size_min;
    uint16_t hi = size_max;
    while (lo < hi) {
        const uint16_t mid = (uint16_t)((lo + hi + 1U) / 2U); /* round-up so lo converges */
        const wrap_result_t r = simulate_wrap(font, text, len, (float)mid, letter_tracking, container_w);
        if (r.lines <= 1U && r.max_line_w <= container_w) {
            lo = mid;
        } else {
            hi = (uint16_t)(mid - 1U);
        }
    }
    return lo;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
uint16_t nt_ui_fit_box(nt_ui_context_t *ctx, uint16_t font_id, const char *text, float container_w, float container_h, uint16_t size_min, uint16_t size_max, float letter_tracking,
                       uint16_t line_height) {
    NT_ASSERT(ctx != NULL && "nt_ui_fit_box: ctx must be non-NULL");
    NT_ASSERT(font_id < NT_UI_MAX_FONTS && "nt_ui_fit_box: font_id out of range");
    NT_ASSERT(size_min > 0U && size_max >= size_min && "nt_ui_fit_box: 0 < size_min <= size_max");
    NT_ASSERT(container_w > 0.0F && container_h > 0.0F && "nt_ui_fit_box: container dims must be > 0");
    if (text == NULL || text[0] == '\0') {
        return size_max;
    }
    nt_font_t font = ctx->fonts[font_id];
    NT_ASSERT(nt_font_valid(font) && "nt_ui_fit_box: font slot empty; bind via nt_ui_set_font first");

    nt_font_metrics_t fm = nt_font_get_metrics(font);
    if (fm.units_per_em == 0) {
        return size_max;
    }
    const size_t len = strlen(text);

    /* Fits if: total height (lines * line_height) <= container_h AND every
     * line is <= container_w (long unbreakable word can otherwise overflow). */
    const wrap_result_t r_max = simulate_wrap(font, text, len, (float)size_max, letter_tracking, container_w);
    const float lh_at_max = (line_height > 0U) ? (float)line_height : ((float)fm.line_height * (float)size_max / (float)fm.units_per_em);
    if ((float)r_max.lines * lh_at_max <= container_h && r_max.max_line_w <= container_w) {
        return size_max;
    }

    uint16_t lo = size_min;
    uint16_t hi = size_max;
    while (lo < hi) {
        const uint16_t mid = (uint16_t)((lo + hi + 1U) / 2U);
        const wrap_result_t r = simulate_wrap(font, text, len, (float)mid, letter_tracking, container_w);
        const float lh = (line_height > 0U) ? (float)line_height : ((float)fm.line_height * (float)mid / (float)fm.units_per_em);
        if ((float)r.lines * lh <= container_h && r.max_line_w <= container_w) {
            lo = mid;
        } else {
            hi = (uint16_t)(mid - 1U);
        }
    }
    return lo;
}
