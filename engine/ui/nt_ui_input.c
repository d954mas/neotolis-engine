#include "ui/nt_ui_input.h"

#include <math.h>
#include <string.h>

#include "core/nt_assert.h"
#include "font/nt_font.h"
#include "input/nt_input.h"
#include "memory/nt_mem_scratch.h"
#include "ui/nt_ui_clay_impl.h"
#include "ui/nt_ui_debug_hit_zones.h"
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_label.h"
#include "ui/nt_ui_state.h"
#include "utf8/nt_utf8.h"

const nt_ui_widget_def_t NT_UI_INPUT_DEF = {
    .name = "nt_input",
    .pill_color = 0xFF70A0D0U,
    ._reserved = 0U,
};

#define NT_UI_INPUT_MASK_CHAR '*'

/* Per-field retained cell: caret byte-offset into the game buffer, horizontal scroll px so the
 * caret stays visible, and the accumulated blink phase. The STRING stays game-owned (D-09). */
#define NT_UI_INPUT_STATE_SALT 0x10D70001U
typedef struct {
    uint32_t caret; /* caret byte offset; always on a codepoint boundary */
    float scroll_x; /* horizontal scroll px (text origin shifts left by this) */
    float blink;    /* accumulated seconds since the caret last reset its phase */
} nt_ui_input_state_t;

/* Double-click + long-press cell (D-16); generic, keyed by the gesture's widget id. */
#define NT_UI_INPUT_GESTURE_SALT 0x10D7C002U
typedef struct {
    float last_press_time; /* gesture-clock time of the previous press (valid only if has_prev) */
    float origin_x, origin_y;
    float clock;        /* monotonic accumulator fed by ctx->frame_dt */
    float press_clock;  /* clock value at the live press (for long-press timing) */
    uint8_t has_prev;   /* 1 once a first press has been seen (clock==0 is a valid time) */
    uint8_t press_live; /* 1 while a press is held without firing long-press yet */
    uint8_t long_fired; /* 1 once long-press fired for the current hold (one-shot) */
    uint8_t _pad[1];
} nt_ui_input_gesture_t;

static inline uint32_t input_state_id(uint32_t id) { return nt_ui_derived_id(id, NT_UI_INPUT_STATE_SALT); }
static inline uint32_t input_gesture_id(uint32_t id) { return nt_ui_derived_id(id, NT_UI_INPUT_GESTURE_SALT); }

// #region utf8 helpers

/* Decode the codepoint length (1..4 bytes) at byte offset `off`. Returns 0 on a malformed
 * lead (caller treats as a single byte to make forward progress without splitting). */
static uint32_t utf8_seq_len(const char *buf, uint32_t off, uint32_t len) {
    uint32_t state = NT_UTF8_ACCEPT;
    uint32_t cp = 0U;
    uint32_t i = off;
    while (i < len) {
        const uint32_t s = nt_utf8_decode(&state, &cp, (uint8_t)buf[i]);
        ++i;
        if (s == NT_UTF8_ACCEPT) {
            return i - off;
        }
        if (s == NT_UTF8_REJECT) {
            return 1U; /* malformed: step one byte */
        }
    }
    return (i > off) ? (i - off) : 0U;
}

/* Byte offset of the codepoint boundary one step LEFT of `off` (continuation bytes are 10xxxxxx). */
static uint32_t utf8_prev_boundary(const char *buf, uint32_t off) {
    if (off == 0U) {
        return 0U;
    }
    uint32_t i = off - 1U;
    while (i > 0U && ((uint8_t)buf[i] & 0xC0U) == 0x80U) {
        --i;
    }
    return i;
}

/* Encode `cp` to UTF-8 in `out` (>= 4 bytes). Returns the byte count, or 0 for an
 * unencodable / control / NUL codepoint (dropped, never spliced -- D-17). */
static uint32_t utf8_encode(uint32_t cp, char out[4]) {
    if (cp == 0U || cp > 0x10FFFFU || (cp >= 0xD800U && cp <= 0xDFFFU)) {
        return 0U; /* NUL, out-of-range, or surrogate: never splice */
    }
    if (cp < 0x80U) {
        out[0] = (char)cp;
        return 1U;
    }
    if (cp < 0x800U) {
        out[0] = (char)(0xC0U | (cp >> 6));
        out[1] = (char)(0x80U | (cp & 0x3FU));
        return 2U;
    }
    if (cp < 0x10000U) {
        out[0] = (char)(0xE0U | (cp >> 12));
        out[1] = (char)(0x80U | ((cp >> 6) & 0x3FU));
        out[2] = (char)(0x80U | (cp & 0x3FU));
        return 3U;
    }
    out[0] = (char)(0xF0U | (cp >> 18));
    out[1] = (char)(0x80U | ((cp >> 12) & 0x3FU));
    out[2] = (char)(0x80U | ((cp >> 6) & 0x3FU));
    out[3] = (char)(0x80U | (cp & 0x3FU));
    return 4U;
}

// #endregion

// #region stock predicates

bool nt_ui_filter_numeric(uint32_t cp) { return (cp >= (uint32_t)'0' && cp <= (uint32_t)'9') || cp == (uint32_t)'.' || cp == (uint32_t)'+' || cp == (uint32_t)'-'; }

static bool is_ascii_alnum(uint32_t cp) { return (cp >= (uint32_t)'0' && cp <= (uint32_t)'9') || (cp >= (uint32_t)'a' && cp <= (uint32_t)'z') || (cp >= (uint32_t)'A' && cp <= (uint32_t)'Z'); }

bool nt_ui_filter_email(uint32_t cp) {
    if (is_ascii_alnum(cp)) {
        return true;
    }
    switch (cp) {
    case (uint32_t)'@':
    case (uint32_t)'.':
    case (uint32_t)'_':
    case (uint32_t)'%':
    case (uint32_t)'+':
    case (uint32_t)'-':
        return true;
    default:
        return false;
    }
}

bool nt_ui_filter_url(uint32_t cp) {
    if (is_ascii_alnum(cp)) {
        return true;
    }
    switch (cp) {
    case (uint32_t)':':
    case (uint32_t)'/':
    case (uint32_t)'.':
    case (uint32_t)'-':
    case (uint32_t)'_':
    case (uint32_t)'~':
    case (uint32_t)'?':
    case (uint32_t)'#':
    case (uint32_t)'&':
    case (uint32_t)'=':
    case (uint32_t)'%':
        return true;
    default:
        return false;
    }
}

/* Default allow when style->allow == NULL: any printable (drop C0/C1 control + DEL). */
static bool allow_printable(uint32_t cp) { return cp >= 0x20U && cp != 0x7FU && !(cp >= 0x80U && cp <= 0x9FU); }

// #endregion

// #region double-click + long-press primitive (D-16)

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_ui_click_gesture_t nt_ui_dblclick_longpress(nt_ui_context_t *ctx, uint32_t id, bool pressed_now, bool released_now, bool held, float pos_x, float pos_y, float dbl_window_secs,
                                               float long_press_secs, float move_radius_px) {
    NT_ASSERT(ctx != NULL && "nt_ui_dblclick_longpress: ctx must be non-NULL");
    NT_ASSERT(id != 0U && "nt_ui_dblclick_longpress: id must be non-zero");
    nt_ui_input_gesture_t *g = (nt_ui_input_gesture_t *)nt_ui_state(ctx, input_gesture_id(id), (uint32_t)sizeof(nt_ui_input_gesture_t), NT_UI_STATE_TAG('i', 'g', 's', 't'));
    g->clock += ctx->frame_dt; /* monotonic; dt may be 0 in headless tests (caller drives time via dt) */
    nt_ui_click_gesture_t out = {false, false};

    if (pressed_now) {
        const float dx = pos_x - g->origin_x;
        const float dy = pos_y - g->origin_y;
        const bool in_radius = (dx * dx + dy * dy) <= (move_radius_px * move_radius_px);
        const bool in_window = (g->has_prev != 0U) && ((g->clock - g->last_press_time) <= dbl_window_secs);
        if (in_window && in_radius) {
            out.double_clicked = true;
            g->has_prev = 0U; /* consume so a triple-press isn't two double-clicks */
        } else {
            g->last_press_time = g->clock;
            g->has_prev = 1U;
        }
        g->origin_x = pos_x;
        g->origin_y = pos_y;
        g->press_clock = g->clock;
        g->press_live = 1U;
        g->long_fired = 0U;
    }

    if (held && g->press_live != 0U && g->long_fired == 0U && long_press_secs > 0.0F) {
        const float dx = pos_x - g->origin_x;
        const float dy = pos_y - g->origin_y;
        const bool moved = (dx * dx + dy * dy) > (move_radius_px * move_radius_px);
        if (moved) {
            g->press_live = 0U; /* a drag cancels the long-press candidate */
        } else if ((g->clock - g->press_clock) >= long_press_secs) {
            out.long_pressed = true;
            g->long_fired = 1U; /* one-shot per hold */
        }
    }

    if (released_now) {
        g->press_live = 0U;
        g->long_fired = 0U;
    }
    return out;
}

// #endregion

bool nt_ui_input_focused(const nt_ui_context_t *ctx, uint32_t id) {
    NT_ASSERT(ctx != NULL && "nt_ui_input_focused: ctx must be non-NULL");
    return id != 0U && ctx->focused_input_id == id;
}

// #region edit ops

/* Splice the UTF-8 encoding of `cp` into buffer at `caret`, clamping to buffer_size-1 and the
 * style max_length. A multi-byte codepoint that does not fit is dropped WHOLE (never split).
 * Returns the new caret byte-offset; sets *changed when the buffer mutated. */
static uint32_t insert_codepoint(char *buffer, size_t buffer_size, size_t max_len, uint32_t caret, uint32_t cur_len, uint32_t cp, bool *changed) {
    char enc[4];
    const uint32_t n = utf8_encode(cp, enc);
    if (n == 0U) {
        return caret; /* unencodable: drop, no mutation */
    }
    /* Effective byte cap: full capacity leaves room for the NUL; max_len (if set) caps tighter. */
    size_t cap = buffer_size - 1U;
    if (max_len != 0U && (max_len - 1U) < cap) {
        cap = max_len - 1U;
    }
    if ((size_t)cur_len + n > cap) {
        return caret; /* would overflow: drop the whole codepoint */
    }
    /* Shift the tail right by n (incl. the NUL), then write the encoding at the caret. */
    memmove(&buffer[caret + n], &buffer[caret], (size_t)(cur_len - caret) + 1U);
    memcpy(&buffer[caret], enc, n);
    *changed = true;
    return caret + n;
}

/* Delete the codepoint to the LEFT of caret (Backspace). Returns the new caret. */
static uint32_t delete_left(char *buffer, uint32_t caret, uint32_t cur_len, bool *changed) {
    if (caret == 0U) {
        return 0U;
    }
    const uint32_t start = utf8_prev_boundary(buffer, caret);
    const uint32_t n = caret - start;
    memmove(&buffer[start], &buffer[caret], (size_t)(cur_len - caret) + 1U);
    *changed = true;
    (void)n;
    return start;
}

/* Delete the codepoint to the RIGHT of caret (Delete). Caret unchanged. */
static void delete_right(char *buffer, uint32_t caret, uint32_t cur_len, bool *changed) {
    if (caret >= cur_len) {
        return;
    }
    const uint32_t n = utf8_seq_len(buffer, caret, cur_len);
    const uint32_t step = (n > 0U) ? n : 1U;
    memmove(&buffer[caret], &buffer[caret + step], (size_t)(cur_len - (caret + step)) + 1U);
    *changed = true;
}

// #endregion

/* Caret x (px from text origin) at byte offset `caret` using prefix measure. password masks
 * each codepoint to one fixed glyph so the caret tracks the rendered dots, not the raw bytes. */
static float caret_x_at(const nt_ui_input_style_t *style, nt_font_t font, const char *buffer, uint32_t caret) {
    if (!style->password) {
        return nt_font_measure_n(font, buffer, (size_t)caret, style->text.font_size, (float)style->text.letter_tracking).width;
    }
    /* Count codepoints in [0,caret), measure that many mask chars. */
    uint32_t count = 0U;
    uint32_t i = 0U;
    while (i < caret) {
        i = i + ((utf8_seq_len(buffer, i, caret) > 0U) ? utf8_seq_len(buffer, i, caret) : 1U);
        ++count;
    }
    float w = 0.0F;
    for (uint32_t k = 0U; k < count; ++k) {
        const char m = NT_UI_INPUT_MASK_CHAR;
        w += nt_font_measure_n(font, &m, 1U, style->text.font_size, (float)style->text.letter_tracking).width;
        if (k + 1U < count) {
            w += (float)style->text.letter_tracking;
        }
    }
    return w;
}

/* Map a click x (px from text origin, scroll already added) to the nearest codepoint boundary. */
static uint32_t caret_from_x(const nt_ui_input_style_t *style, nt_font_t font, const char *buffer, uint32_t len, float local_x) {
    if (local_x <= 0.0F || len == 0U) {
        return 0U;
    }
    uint32_t off = 0U;
    float prev_w = 0.0F;
    while (off < len) {
        const uint32_t step = (utf8_seq_len(buffer, off, len) > 0U) ? utf8_seq_len(buffer, off, len) : 1U;
        const uint32_t next = off + step;
        const float next_w = caret_x_at(style, font, buffer, next);
        if (local_x < (prev_w + ((next_w - prev_w) * 0.5F))) {
            return off; /* click landed in the left half of this glyph */
        }
        prev_w = next_w;
        off = next;
    }
    return len;
}

// #region render

static void emit_caret(nt_ui_context_t *ctx, uint8_t layer, float x, float y, float w, float h, uint32_t color) {
    nt_ui_transform_t tt = nt_ui_transform_defaults();
    tt.offset_x = x;
    tt.offset_y = y;
    nt_ui_element_data_t *cd = NT_MEM_SCRATCH_ALLOC(nt_ui_element_data_t);
    NT_ASSERT(cd != NULL && "nt_ui_input: scratch alloc failed (caret data)");
    *cd = (nt_ui_element_data_t){.user_data = NULL, .layer = layer, .flags = (uint8_t)NT_UI_ELEM_FLAG_HAS_TRANSFORM, .transform = tt, .opacity = 1.0F};
    const Clay_ElementDeclaration caret_decl = {
        .layout = {.sizing = {CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h)}},
        .backgroundColor = nt_ui_unpack_abgr(color),
        .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .clipTo = CLAY_CLIP_TO_ATTACHED_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP}},
    };
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(caret_decl);
    nt_ui_clay_priv_close_element();
}

// #endregion

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
bool nt_ui_input_text(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t text_layer, uint32_t id, char *buffer, size_t buffer_size, const nt_ui_input_style_t *style,
                      const Clay_ElementDeclaration *decl, bool enabled, bool *out_submitted) {
    // #region entry asserts (STYLE-VALIDATION-01)
    NT_ASSERT(ctx != NULL && "nt_ui_input_text: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_input_text: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(style != NULL && "nt_ui_input_text: style must be non-NULL");
    NT_ASSERT(id != 0U && "nt_ui_input_text: id 0 is the no-widget sentinel");
    NT_ASSERT(buffer != NULL && "nt_ui_input_text: buffer must be non-NULL (game owns the string)");
    NT_ASSERT(buffer_size > 0U && "nt_ui_input_text: buffer_size must be > 0 (room for the NUL)");
    NT_ASSERT(style->text.font_id < NT_UI_MAX_FONTS && "nt_ui_input_text: style.text.font_id out of range");
    NT_ASSERT(isfinite(style->caret_width) && style->caret_width > 0.0F && "nt_ui_input_text: style.caret_width must be finite > 0");
    NT_ASSERT(style->text.font_size > 0.0F && "nt_ui_input_text: style.text.font_size must be > 0");
    if (decl != NULL) {
        NT_ASSERT(decl->id.id == 0U && "nt_ui_input_text: decl->id must be 0 (id is the explicit param)");
        NT_ASSERT(decl->userData == NULL && "nt_ui_input_text: decl->userData must be NULL (data param controls)");
    }
    if (data != NULL) {
        NT_ASSERT((data->flags & (NT_UI_ELEM_FLAG_HAS_TRANSFORM | NT_UI_ELEM_FLAG_HAS_OPACITY)) == 0U && "nt_ui_input_text: data->flags must not set HAS_TRANSFORM/HAS_OPACITY");
    }
    // #endregion

    if (out_submitted != NULL) {
        *out_submitted = false;
    }
    const uint8_t layer = (data != NULL) ? data->layer : 0U;
    void *user = (data != NULL) ? data->user_data : NULL;
    nt_font_t font = ctx->fonts[style->text.font_id];

    /* The string is game-owned; the engine assumes a valid NUL-terminated buffer. */
    NT_ASSERT(memchr(buffer, '\0', buffer_size) != NULL && "nt_ui_input_text: buffer must be NUL-terminated within buffer_size");
    uint32_t cur_len = (uint32_t)strlen(buffer);

    // #region focus-order bookkeeping (Tab wrap target)
    if (ctx->focus_first_id == 0U) {
        ctx->focus_first_id = id;
    }
    /* A pending Tab seek set by the previously-declared focused field: this field claims focus.
     * claimed_now suppresses this field's OWN Tab handling so the still-pressed Tab key does not
     * immediately re-advance focus off the field that just received it. */
    bool claimed_now = false;
    if (ctx->focus_tab_seek != 0U && ctx->focus_tab_seek != id) {
        ctx->focused_input_id = id;
        ctx->focus_tab_seek = 0U;
        claimed_now = true;
    }
    // #endregion

    // #region interaction + focus capture
    nt_ui_interaction_t in;
    if (enabled) {
        in = nt_ui_step_interaction(ctx, id);
    } else {
        in = (nt_ui_interaction_t){0};
        nt_ui_block_pointer(ctx, id, NULL);
        nt_ui_debug_record_disabled_zone(ctx, id, NULL);
    }

    nt_ui_input_state_t *st = (nt_ui_input_state_t *)nt_ui_state(ctx, input_state_id(id), (uint32_t)sizeof(nt_ui_input_state_t), NT_UI_STATE_TAG('i', 'n', 'p', 't'));
    if (st->caret > cur_len) {
        st->caret = cur_len; /* the game shortened the buffer behind our back */
    }

    if (enabled && in.pressed_now) {
        ctx->focused_input_id = id;
        st->blink = 0.0F;
        /* Place the caret at the click via hit-test against the prev-frame bbox. */
        const nt_ui_bbox_t bb = nt_ui_get_bbox(ctx, id);
        if (bb.found) {
            const float local_x = (in.press_pos[0] - bb.x - style->pad_x) + st->scroll_x;
            st->caret = caret_from_x(style, font, buffer, cur_len, local_x);
        }
    }
    const bool focused = enabled && (ctx->focused_input_id == id);

    /* Generic dbl-click / long-press edges (D-16); 61-06 layers word-select on these. */
    (void)nt_ui_dblclick_longpress(ctx, id, enabled && in.pressed_now, enabled && in.released_now, enabled && in.pressed, in.pos[0], in.pos[1], 0.30F, 0.50F, 6.0F);
    // #endregion

    bool changed = false;
    bool submitted = false;

    if (focused) {
        // #region drain typed chars (FIFO from the input ring)
        uint32_t cp = 0U;
        while (nt_input_pop_char(&cp)) {
            const bool ok = (style->allow != NULL) ? style->allow(cp) : allow_printable(cp);
            if (!ok) {
                continue;
            }
            st->caret = insert_codepoint(buffer, buffer_size, style->max_length, st->caret, cur_len, cp, &changed);
            cur_len = (uint32_t)strlen(buffer);
            st->blink = 0.0F;
        }
        // #endregion

        // #region physical editing keys
        if (nt_input_key_is_pressed(NT_KEY_BACKSPACE)) {
            st->caret = delete_left(buffer, st->caret, cur_len, &changed);
            cur_len = (uint32_t)strlen(buffer);
            st->blink = 0.0F;
        }
        if (nt_input_key_is_pressed(NT_KEY_DELETE)) {
            delete_right(buffer, st->caret, cur_len, &changed);
            cur_len = (uint32_t)strlen(buffer);
            st->blink = 0.0F;
        }
        if (nt_input_key_is_pressed(NT_KEY_ARROW_LEFT)) {
            st->caret = utf8_prev_boundary(buffer, st->caret);
            st->blink = 0.0F;
        }
        if (nt_input_key_is_pressed(NT_KEY_ARROW_RIGHT) && st->caret < cur_len) {
            const uint32_t step = (utf8_seq_len(buffer, st->caret, cur_len) > 0U) ? utf8_seq_len(buffer, st->caret, cur_len) : 1U;
            st->caret += step;
            st->blink = 0.0F;
        }
        if (nt_input_key_is_pressed(NT_KEY_HOME)) {
            st->caret = 0U;
            st->blink = 0.0F;
        }
        if (nt_input_key_is_pressed(NT_KEY_END)) {
            st->caret = cur_len;
            st->blink = 0.0F;
        }
        if (nt_input_key_is_pressed(NT_KEY_ENTER)) {
            submitted = true;
        }
        if (nt_input_key_is_pressed(NT_KEY_TAB) && !claimed_now) {
            /* Advance focus to the next field declared this frame; wrap to the first. claimed_now
             * fields skip this so the field that just received Tab focus does not re-advance. */
            ctx->focus_tab_seek = id;
            ctx->focused_input_id = ctx->focus_first_id; /* fallback: wrap (overridden if a later field claims) */
        }
        if (nt_input_key_is_pressed(NT_KEY_ESCAPE)) {
            ctx->focused_input_id = 0U; /* unfocus first; modal close (if any) handles the next Esc */
        }
        // #endregion
    }

    /* Blink phase accumulates only while focused; reset on any edit/caret move above. */
    if (focused) {
        st->blink += ctx->frame_dt;
    }

    // #region keep caret visible (horizontal scroll)
    const float inner_w = (decl != NULL && decl->layout.sizing.width.type == CLAY__SIZING_TYPE_FIXED) ? (decl->layout.sizing.width.size.minMax.min - (style->pad_x * 2.0F)) : 0.0F;
    if (inner_w > 0.0F) {
        const float caret_px = caret_x_at(style, font, buffer, st->caret);
        if (caret_px - st->scroll_x > inner_w) {
            st->scroll_x = caret_px - inner_w;
        } else if (caret_px < st->scroll_x) {
            st->scroll_x = caret_px;
        }
        if (st->scroll_x < 0.0F) {
            st->scroll_x = 0.0F;
        }
    } else {
        st->scroll_x = 0.0F;
    }
    // #endregion

    // #region compose (bg rect + text/placeholder + caret)
    Clay_ElementDeclaration root = (decl != NULL) ? *decl : (Clay_ElementDeclaration){0};
    root.id = (Clay_ElementId){.id = id};
    root.userData = (void *)nt_ui_make_element_data(layer, user);
    const uint32_t bg = focused ? style->focused_bg_color : style->bg_color;
    if (bg != 0U) {
        root.backgroundColor = nt_ui_unpack_abgr(bg);
    }
    const uint32_t bcol = focused ? style->focused_border_color : style->border_color;
    if (style->border_width > 0.0F && bcol != 0U) {
        root.border = (Clay_BorderElementConfig){
            .color = nt_ui_unpack_abgr(bcol),
            .width = {.left = (uint16_t)style->border_width, .right = (uint16_t)style->border_width, .top = (uint16_t)style->border_width, .bottom = (uint16_t)style->border_width}};
    }
    /* Engine owns inner padding so the caret/scroll math matches the text origin. */
    root.layout.padding = (Clay_Padding){.left = (uint16_t)style->pad_x, .right = (uint16_t)style->pad_x, .top = (uint16_t)style->pad_y, .bottom = (uint16_t)style->pad_y};

    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(root);
    nt_ui_widget_register(ctx, id, &NT_UI_INPUT_DEF, NULL);

    const bool empty = (cur_len == 0U);
    if (!empty) {
        nt_ui_label_style_t ts = style->text;
        if (style->password) {
            /* Render a mask string of the same codepoint count into scratch. */
            uint32_t count = 0U;
            uint32_t i = 0U;
            while (i < cur_len) {
                i += (utf8_seq_len(buffer, i, cur_len) > 0U) ? utf8_seq_len(buffer, i, cur_len) : 1U;
                ++count;
            }
            char *masked = (char *)nt_mem_scratch_alloc(count + 1U, 1U);
            NT_ASSERT(masked != NULL && "nt_ui_input: scratch alloc failed (mask)");
            memset(masked, NT_UI_INPUT_MASK_CHAR, count);
            masked[count] = '\0';
            nt_ui_label(ctx, nt_ui_make_element_data(text_layer, NULL), masked, &ts);
        } else {
            nt_ui_label(ctx, nt_ui_make_element_data(text_layer, NULL), buffer, &ts);
        }
    }
    /* Placeholder text is a 61-07 demo concern: T0 has no placeholder string param (the buffer IS
     * the value). style->placeholder carries the dimmed style for when that lands. */
    (void)style->placeholder;

    if (focused) {
        const bool blink_on = (style->caret_blink_rate <= 0.0F) || (fmodf(st->blink, style->caret_blink_rate) < (style->caret_blink_rate * 0.5F));
        if (blink_on) {
            const float caret_px = caret_x_at(style, font, buffer, st->caret) - st->scroll_x;
            const float caret_h = style->text.font_size;
            emit_caret(ctx, text_layer, style->pad_x + caret_px, style->pad_y, style->caret_width, caret_h, style->caret_color);
        }
    }

    nt_ui_clay_priv_close_element();
    // #endregion

    if (out_submitted != NULL) {
        *out_submitted = submitted;
    }
    return changed;
}

nt_ui_input_style_t nt_ui_input_style_defaults(void) {
    nt_ui_input_style_t s;
    memset(&s, 0, sizeof s); /* memset, not = {0}: emscripten -Werror rejects {0} on aggregate-first */
    s.text = (nt_ui_label_style_t){.font_id = 0, .font_size = 16, .color = {255.0F, 255.0F, 255.0F, 255.0F}};
    s.placeholder = (nt_ui_label_style_t){.font_id = 0, .font_size = 16, .color = {128.0F, 128.0F, 128.0F, 255.0F}};
    s.bg_color = 0xFF202020U;
    s.focused_bg_color = 0xFF303030U;
    s.border_color = 0xFF505050U;
    s.focused_border_color = 0xFF80B0E0U;
    s.caret_color = 0xFFFFFFFFU;
    s.caret_blink_rate = 1.0F;
    s.caret_width = 2.0F;
    s.border_width = 1.0F;
    s.pad_x = 6.0F;
    s.pad_y = 4.0F;
    s.max_length = 0U;
    s.allow = NULL;
    s.keyboard = NT_UI_KB_TEXT;
    s.password = false;
    return s;
}
