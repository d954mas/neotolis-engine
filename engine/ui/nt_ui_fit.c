#include "ui/nt_ui_fit.h"

#include <string.h>

#include "core/nt_assert.h"
#include "font/nt_font.h"
#include "ui/nt_ui_internal.h" /* ctx->fonts[] */

static bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

/* Word-wrap simulation matching Clay's CLAY_TEXT_WRAP_WORDS behavior.
 * Returns line count when `text` is greedy-packed to container_w at given
 * font size. \n is an explicit line break. Leading whitespace on each new
 * line is skipped (Clay convention -- no leading-space gap after wrap). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static uint32_t simulate_wrap_lines(nt_font_t font, const char *text, size_t len, float size, float letter_tracking, float container_w) {
    if (len == 0U || container_w <= 0.0F) {
        return 0U;
    }
    /* Space width: measured once per call, reused for inter-word gaps. */
    const float space_w = nt_font_measure_n(font, " ", 1U, size, letter_tracking).width;

    uint32_t lines = 1U;
    float line_w = 0.0F;
    size_t i = 0U;
    while (i < len) {
        /* Skip leading whitespace on a fresh line. */
        if (line_w == 0.0F) {
            while (i < len && is_ws(text[i]) && text[i] != '\n') {
                i++;
            }
            if (i >= len) {
                break;
            }
        }

        /* Explicit newline starts a new line; don't count empty trailing line. */
        if (text[i] == '\n') {
            lines++;
            line_w = 0.0F;
            i++;
            continue;
        }

        /* Locate next word (run of non-whitespace). */
        const size_t word_start = i;
        while (i < len && !is_ws(text[i])) {
            i++;
        }
        const size_t word_end = i;
        if (word_end == word_start) {
            continue;
        }
        const float word_w = nt_font_measure_n(font, text + word_start, word_end - word_start, size, letter_tracking).width;
        const float prospective = (line_w == 0.0F) ? word_w : (line_w + space_w + word_w);
        if (prospective > container_w && line_w > 0.0F) {
            /* Wrap: word goes to next line, alone. */
            lines++;
            line_w = word_w;
        } else {
            line_w = prospective;
        }

        /* Consume the trailing whitespace (one space) but stop at newline. */
        if (i < len && text[i] != '\n' && is_ws(text[i])) {
            i++;
        }
    }
    return lines;
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

    /* Use the same per-word wrap simulation as Clay's MeasureTextCached path
     * so fit_width returns size_max only when Clay will actually keep text on
     * one line. Plain measure_n on the full string applies kerning across
     * whitespace which Clay does NOT do -- the small discrepancy is enough
     * to flip wrap decisions for borderline-length text. */
    if (simulate_wrap_lines(font, text, len, (float)size_max, letter_tracking, container_w) <= 1U) {
        return size_max;
    }

    /* Binary search: find largest size in [lo, hi] that keeps the text on
     * one line. round-up mid so lo converges to the answer. */
    uint16_t lo = size_min;
    uint16_t hi = size_max;
    while (lo < hi) {
        const uint16_t mid = (uint16_t)((lo + hi + 1U) / 2U);
        if (simulate_wrap_lines(font, text, len, (float)mid, letter_tracking, container_w) <= 1U) {
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

    /* Per-size: wrap line count -> total height. Compare to container_h. */
    /* Inlined inside the binary search; line_height==0 means natural. */
    const uint32_t lines_at_max = simulate_wrap_lines(font, text, len, (float)size_max, letter_tracking, container_w);
    const float lh_at_max = (line_height > 0U) ? (float)line_height : ((float)fm.line_height * (float)size_max / (float)fm.units_per_em);
    if ((float)lines_at_max * lh_at_max <= container_h) {
        return size_max;
    }

    uint16_t lo = size_min;
    uint16_t hi = size_max;
    while (lo < hi) {
        const uint16_t mid = (uint16_t)((lo + hi + 1U) / 2U);
        const uint32_t lines = simulate_wrap_lines(font, text, len, (float)mid, letter_tracking, container_w);
        const float lh = (line_height > 0U) ? (float)line_height : ((float)fm.line_height * (float)mid / (float)fm.units_per_em);
        if ((float)lines * lh <= container_h) {
            lo = mid;
        } else {
            hi = (uint16_t)(mid - 1U);
        }
    }
    return lo;
}
