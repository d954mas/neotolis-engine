#include "ui/nt_ui_input.h"

#include <math.h>
#include <string.h>

#include "clipboard/nt_clipboard.h"
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

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
/* Map the field's keyboard hint to a web soft-keyboard via the canvas `inputmode` + `type`
 * Called on focus; mobile browsers pick the keyboard layout from inputmode, and
 * type=password masks the OS-level overlay. kb is nt_ui_input_keyboard_t. */
/* clang-format off */
EM_JS(void, nt_ui_input_web_apply_keyboard, (int kb), {
    var modes = ['text', 'numeric', 'email', 'url', 'text'];
    var el = Module['canvas'] || document.activeElement;
    if (!el) { return; }
    el.setAttribute('inputmode', modes[kb] || 'text');
    if (kb === 4 /* NT_UI_KB_PASSWORD */) {
        el.setAttribute('type', 'password');
    } else {
        el.removeAttribute('type');
    }
})
/* clang-format on */
#endif

/* Apply the mobile keyboard-type hint on focus (web inputmode/type; native is a no-op — the
 * enum is stored but desktop edit behavior is unchanged). */
static inline void apply_keyboard_hint(nt_ui_input_keyboard_t kb, bool password) {
#ifdef __EMSCRIPTEN__
    const int eff = password ? (int)NT_UI_KB_PASSWORD : (int)kb;
    nt_ui_input_web_apply_keyboard(eff);
#else
    (void)kb;
    (void)password;
#endif
}

const nt_ui_widget_def_t NT_UI_INPUT_DEF = {
    .name = "nt_input",
    .pill_color = 0xFF70A0D0U,
    ._reserved = 0U,
};

#define NT_UI_INPUT_MASK_CHAR '*'

/* Per-field retained cell: caret byte-offset into the game buffer, horizontal scroll px so the
 * caret stays visible, and the accumulated blink phase. The STRING stays game-owned. */
#define NT_UI_INPUT_STATE_SALT 0x10D70001U
typedef struct {
    uint32_t caret;  /* caret byte offset (the active selection end); always on a codepoint boundary */
    uint32_t anchor; /* selection anchor byte offset; anchor == caret = empty selection */
    float scroll_x;  /* horizontal scroll px (text origin shifts left by this) */
    float blink;     /* accumulated seconds since the caret last reset its phase */
    uint8_t drag;    /* 1 while a press is dragging to extend the selection */
    uint8_t _pad[3];
} nt_ui_input_state_t;

/* Double-click + long-press cell; generic, keyed by the gesture's widget id. */
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
 * unencodable / control / NUL codepoint (dropped, never spliced). */
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

// #region selection helpers

/* Whitespace test for word-class scanning (double-click select-word). ASCII space class only;
 * a non-whitespace codepoint (incl. any multi-byte letter) is a "word" byte. */
static bool byte_is_word(char c) {
    const unsigned char u = (unsigned char)c;
    return !(u == ' ' || u == '\t' || u == '\n' || u == '\r' || u == '\f' || u == '\v');
}

/* Word bounds around byte offset `off`: scan left/right over a single class (word vs whitespace).
 * Lands on codepoint boundaries automatically since UTF-8 continuation bytes (0x80..0xBF) are all
 * non-whitespace, so a multi-byte letter is one contiguous word run. */
static void word_bounds(const char *buffer, uint32_t len, uint32_t off, uint32_t *out_start, uint32_t *out_end) {
    if (len == 0U) {
        *out_start = 0U;
        *out_end = 0U;
        return;
    }
    uint32_t probe = (off < len) ? off : (len - 1U);
    const bool word = byte_is_word(buffer[probe]);
    uint32_t s = probe;
    while (s > 0U && byte_is_word(buffer[s - 1U]) == word) {
        --s;
    }
    uint32_t e = (off < len) ? off : len;
    while (e < len && byte_is_word(buffer[e]) == word) {
        ++e;
    }
    *out_start = s;
    *out_end = e;
}

/* Order the selection endpoints (anchor/caret) into [lo,hi). */
static void selection_span(const nt_ui_input_state_t *st, uint32_t *lo, uint32_t *hi) {
    if (st->anchor <= st->caret) {
        *lo = st->anchor;
        *hi = st->caret;
    } else {
        *lo = st->caret;
        *hi = st->anchor;
    }
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

// #region double-click + long-press primitive

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

static inline bool shift_held(void) { return nt_input_key_is_down(NT_KEY_LSHIFT) || nt_input_key_is_down(NT_KEY_RSHIFT); }
static inline bool ctrl_held(void) { return nt_input_key_is_down(NT_KEY_LCTRL) || nt_input_key_is_down(NT_KEY_RCTRL); }

bool nt_ui_input_focused(const nt_ui_context_t *ctx, uint32_t id) {
    NT_ASSERT(ctx != NULL && "nt_ui_input_focused: ctx must be non-NULL");
    return id != 0U && ctx->focused_input_id == id;
}

bool nt_ui_input_any_focused(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_input_any_focused: ctx must be non-NULL");
    return ctx->focused_input_id != 0U;
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
    memmove(&buffer[start], &buffer[caret], (size_t)(cur_len - caret) + 1U);
    *changed = true;
    return start;
}

/* Delete the byte range [start,end) in the buffer (selection delete). Returns the new caret
 * (= start). Endpoints must already be codepoint-aligned + ordered. */
static uint32_t delete_range(char *buffer, uint32_t start, uint32_t end, uint32_t cur_len, bool *changed) {
    if (end <= start) {
        return start;
    }
    memmove(&buffer[start], &buffer[end], (size_t)(cur_len - end) + 1U);
    *changed = true;
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

// #region clipboard (Ctrl+C/X/V)

/* Copy the selected byte range to the system clipboard. No-op on an empty selection. */
static void clipboard_copy_selection(const char *buffer, const nt_ui_input_state_t *st) {
    if (st->anchor == st->caret) {
        return;
    }
    uint32_t lo = 0U;
    uint32_t hi = 0U;
    selection_span(st, &lo, &hi);
    const uint32_t n = hi - lo;
    char *tmp = (char *)nt_mem_scratch_alloc(n + 1U, 1U);
    NT_ASSERT(tmp != NULL && "nt_ui_input: scratch alloc failed (clipboard copy)");
    memcpy(tmp, &buffer[lo], n);
    tmp[n] = '\0';
    nt_clipboard_set_text(tmp);
}

/* Decode one codepoint from src[off..len). On success writes *out_cp and returns the bytes
 * consumed (>=1). On a malformed/truncated run writes *out_cp=0 and returns the bytes to skip
 * (>=1) so the caller resyncs without reading OOB. */
static uint32_t decode_one(const char *src, uint32_t off, uint32_t len, uint32_t *out_cp) {
    uint32_t state = NT_UTF8_ACCEPT;
    uint32_t cp = 0U;
    uint32_t consumed = 0U;
    while (off + consumed < len) {
        const uint32_t s = nt_utf8_decode(&state, &cp, (uint8_t)src[off + consumed]);
        ++consumed;
        if (s == NT_UTF8_ACCEPT) {
            *out_cp = cp;
            return consumed;
        }
        if (s == NT_UTF8_REJECT) {
            /* Malformed. The DFA rejects ON the offending byte: if a multi-byte run was interrupted
             * by a non-continuation (consumed > 1), back up one so that byte is re-decoded (it may be
             * a valid lead or ASCII). A lone bad first byte (consumed == 1) is just skipped. */
            *out_cp = 0U;
            return (consumed > 1U) ? (consumed - 1U) : 1U;
        }
    }
    *out_cp = 0U;
    return (consumed > 0U) ? consumed : 1U;
}

/* Splice clipboard text at the caret (replacing any selection). Each codepoint is decoded from the
 * untrusted clipboard bytes, run through the allow-predicate, and inserted via insert_codepoint —
 * which clamps to buffer_size-1 / max_length and drops a non-fitting multi-byte codepoint WHOLE.
 * A malformed lead byte is skipped one byte at a time (never read OOB). Returns the new caret. */
static uint32_t clipboard_paste(char *buffer, size_t buffer_size, const nt_ui_input_style_t *style, nt_ui_input_state_t *st, uint32_t cur_len, bool *changed) {
    const char *clip = nt_clipboard_get_text(); /* engine-owned, valid until the next clipboard call */
    if (clip == NULL || clip[0] == '\0') {
        return st->caret;
    }
    /* Snapshot the clipboard into scratch: get_text storage is only valid until the next call, and
     * insert below does not call clipboard again, but copying keeps the contract explicit + bounded.
     * Cap to buffer_size: insert clamps to buffer_size-1 anyway, so a longer paste can't add more --
     * the cap kills the size_t->uint32_t narrowing on a huge paste. A mid-codepoint cut is harmless
     * (decode_one resyncs; a split trailing run is dropped). */
    size_t raw_len = strlen(clip);
    if (raw_len > buffer_size) {
        raw_len = buffer_size;
    }
    const uint32_t clip_len = (uint32_t)raw_len;
    char *src = (char *)nt_mem_scratch_alloc((size_t)clip_len + 1U, 1U);
    NT_ASSERT(src != NULL && "nt_ui_input: scratch alloc failed (clipboard paste)");
    memcpy(src, clip, (size_t)clip_len); /* clip_len, not +1: clip may be longer than the cap */
    src[clip_len] = '\0';

    /* Replace any selection first. */
    if (st->anchor != st->caret) {
        uint32_t lo = 0U;
        uint32_t hi = 0U;
        selection_span(st, &lo, &hi);
        st->caret = delete_range(buffer, lo, hi, cur_len, changed);
        st->anchor = st->caret;
        cur_len = (uint32_t)strlen(buffer);
    }

    uint32_t i = 0U;
    while (i < clip_len) {
        uint32_t cp = 0U;
        const uint32_t consumed = decode_one(src, i, clip_len, &cp);
        i += consumed;
        if (cp == 0U) {
            continue; /* malformed run skipped */
        }
        const bool ok = (style->allow != NULL) ? style->allow(cp) : allow_printable(cp);
        if (!ok) {
            continue; /* predicate-filtered (e.g. numeric field drops letters) */
        }
        const uint32_t before = st->caret;
        st->caret = insert_codepoint(buffer, buffer_size, style->max_length, st->caret, cur_len, cp, changed);
        if (st->caret == before) {
            break; /* buffer full: stop (clamp), the rest of the paste is dropped */
        }
        st->anchor = st->caret;
        cur_len = (uint32_t)strlen(buffer);
    }
    return st->caret;
}

// #endregion

/* Codepoint count in [0,caret) (one decode pass). password mode measures one mask glyph per
 * codepoint, so the caret tracks the rendered dots not the raw bytes. */
static uint32_t codepoint_count(const char *buffer, uint32_t caret) {
    uint32_t count = 0U;
    uint32_t i = 0U;
    while (i < caret) {
        const uint32_t step = utf8_seq_len(buffer, i, caret);
        i += (step > 0U) ? step : 1U;
        ++count;
    }
    return count;
}

/* Width of `count` mask glyphs (closed form: N glyphs + (N-1) tracking gaps). */
static float mask_width(const nt_ui_input_style_t *style, nt_font_t font, uint32_t count) {
    if (count == 0U) {
        return 0.0F;
    }
    const char m = NT_UI_INPUT_MASK_CHAR;
    const float glyph_w = nt_font_measure_n(font, &m, 1U, style->text.font_size, (float)style->text.letter_tracking).width;
    return ((float)count * glyph_w) + ((float)(count - 1U) * (float)style->text.letter_tracking);
}

/* Caret x (px from text origin) at byte offset `caret` using prefix measure. */
static float caret_x_at(const nt_ui_input_style_t *style, nt_font_t font, const char *buffer, uint32_t caret) {
    if (!style->password) {
        return nt_font_measure_n(font, buffer, (size_t)caret, style->text.font_size, (float)style->text.letter_tracking).width;
    }
    return mask_width(style, font, codepoint_count(buffer, caret));
}

/* Map a click x (px from text origin, scroll already added) to the nearest codepoint boundary.
 * One forward decode pass (hoisted seq_len). Non-password prefix widths come from the font's
 * cached measure_n so kerning + tracking stay exact; password measures fixed-width mask glyphs. */
static uint32_t caret_from_x(const nt_ui_input_style_t *style, nt_font_t font, const char *buffer, uint32_t len, float local_x) {
    if (local_x <= 0.0F || len == 0U) {
        return 0U;
    }
    uint32_t off = 0U;
    uint32_t cp_index = 0U; /* codepoints consumed so far -- avoids re-counting from 0 (password) */
    float prev_w = 0.0F;
    while (off < len) {
        const uint32_t raw = utf8_seq_len(buffer, off, len);
        const uint32_t step = (raw > 0U) ? raw : 1U;
        const uint32_t next = off + step;
        ++cp_index;
        /* Password closed-form by running index; non-password prefix via cached measure_n (kern-exact). */
        const float next_w = style->password ? mask_width(style, font, cp_index) : nt_font_measure_n(font, buffer, (size_t)next, style->text.font_size, (float)style->text.letter_tracking).width;
        if (local_x < (prev_w + ((next_w - prev_w) * 0.5F))) {
            return off; /* click landed in the left half of this glyph */
        }
        prev_w = next_w;
        off = next;
    }
    return len;
}

// #region render

/* A filled rect floated at (x,y) inside the field; used for both the caret and the selection
 * highlight (the highlight sits BEHIND the text by emitting before the label). */
static void emit_rect(nt_ui_context_t *ctx, uint8_t layer, float x, float y, float w, float h, uint32_t color) {
    nt_ui_transform_t tt = nt_ui_transform_defaults();
    tt.offset_x = x;
    tt.offset_y = y;
    nt_ui_element_data_t *rd = NT_MEM_SCRATCH_ALLOC(nt_ui_element_data_t);
    NT_ASSERT(rd != NULL && "nt_ui_input: scratch alloc failed (rect data)");
    *rd = (nt_ui_element_data_t){.user_data = NULL, .layer = layer, .flags = (uint8_t)NT_UI_ELEM_FLAG_HAS_TRANSFORM, .transform = tt, .opacity = 1.0F};
    const Clay_ElementDeclaration rect_decl = {
        .layout = {.sizing = {CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h)}},
        .backgroundColor = nt_ui_unpack_abgr(color),
        .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .clipTo = CLAY_CLIP_TO_ATTACHED_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP}},
    };
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(rect_decl);
    nt_ui_clay_priv_close_element();
}

/* Build the password mask string: one NT_UI_INPUT_MASK_CHAR per codepoint of [buffer,len), into
 * frame scratch (NUL-terminated). Render-only — the game buffer is never modified. Exposed for the
 * test probe (no GL capture) under NT_TEST_ACCESS. */
const char *nt_ui_input_build_display_text(const char *buffer, uint32_t len) {
    const uint32_t count = codepoint_count(buffer, len);
    char *masked = (char *)nt_mem_scratch_alloc(count + 1U, 1U);
    NT_ASSERT(masked != NULL && "nt_ui_input: scratch alloc failed (mask)");
    memset(masked, NT_UI_INPUT_MASK_CHAR, count);
    masked[count] = '\0';
    return masked;
}

/* Picks the hint string the field shows when nothing is entered: the per-field placeholder, but only
 * while empty AND unfocused (focusing clears it so the caret reads against a blank field). Render-
 * only -- the game buffer is never written. NULL/"" placeholder = render nothing. Test probe. */
const char *nt_ui_input_placeholder_for(const char *buffer, const char *placeholder, bool focused) {
    const bool empty = (buffer[0] == '\0');
    const bool has_hint = (placeholder != NULL && placeholder[0] != '\0');
    return (empty && !focused && has_hint) ? placeholder : NULL;
}

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

/* Float the text at (x,y) inside the field, clipped to the field box, so it shares the SAME
 * pad_x - scroll_x origin as the caret/selection rects (an inline label would ignore scroll_x and
 * desync once the text scrolls). The label is the single child of this floating clip wrapper. */
static void emit_text(nt_ui_context_t *ctx, uint8_t text_layer, float x, float y, const char *disp, const nt_ui_label_style_t *ts) {
    nt_ui_transform_t tt = nt_ui_transform_defaults();
    tt.offset_x = x;
    tt.offset_y = y;
    nt_ui_element_data_t *wd = NT_MEM_SCRATCH_ALLOC(nt_ui_element_data_t);
    NT_ASSERT(wd != NULL && "nt_ui_input: scratch alloc failed (text wrapper data)");
    *wd = (nt_ui_element_data_t){.user_data = NULL, .layer = text_layer, .flags = (uint8_t)NT_UI_ELEM_FLAG_HAS_TRANSFORM, .transform = tt, .opacity = 1.0F};
    const Clay_ElementDeclaration text_decl = {
        .layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}},
        .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .clipTo = CLAY_CLIP_TO_ATTACHED_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP}},
    };
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(text_decl);
    nt_ui_label(ctx, nt_ui_make_element_data(text_layer, NULL), disp, ts);
    nt_ui_clay_priv_close_element();
}

// #endregion

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
bool nt_ui_input_text(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t text_layer, uint32_t id, char *buffer, size_t buffer_size, const char *placeholder,
                      const nt_ui_input_style_t *style, const Clay_ElementDeclaration *decl, bool enabled, bool *out_submitted) {
    // #region entry asserts
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
        apply_keyboard_hint(style->keyboard, style->password); /* Tab into the field also surfaces the keyboard */
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
        /* A disabled field can't be clicked to refocus and gates out Esc/Tab while focused==false, so
         * a still-declared disabled field would hold focus forever (any_focused stuck true). Drop it
         * the frame it's declared disabled. */
        if (ctx->focused_input_id == id) {
            ctx->focused_input_id = 0U;
        }
    }

    nt_ui_input_state_t *st = (nt_ui_input_state_t *)nt_ui_state(ctx, input_state_id(id), (uint32_t)sizeof(nt_ui_input_state_t), NT_UI_STATE_TAG('i', 'n', 'p', 't'));
    /* Contract: the game must not rewrite a FOCUSED field's buffer CONTENT mid-edit. Only length
     * changes are clamped here; a content rewrite that lands the caret mid-codepoint is undefined. */
    if (st->caret > cur_len) {
        st->caret = cur_len; /* the game shortened the buffer behind our back */
    }
    if (st->anchor > cur_len) {
        st->anchor = cur_len; /* keep the selection anchor in-bounds too */
    }

    /* Map a pointer x (UI-space) to a codepoint boundary via the prev-frame bbox. */
    const nt_ui_bbox_t bb = nt_ui_get_bbox(ctx, id);
    if (enabled && in.pressed_now) {
        const bool was_focused = (ctx->focused_input_id == id);
        ctx->focused_input_id = id;
        st->blink = 0.0F;
        if (bb.found) {
            const float local_x = (in.press_pos[0] - bb.x - style->pad_x) + st->scroll_x;
            st->caret = caret_from_x(style, font, buffer, cur_len, local_x);
        }
        st->anchor = st->caret; /* a fresh press collapses the selection to the click point */
        st->drag = 1U;
        if (!was_focused) {
            apply_keyboard_hint(style->keyboard, style->password); /* surface the soft keyboard on web */
        }
    }

    /* Generic dbl-click / long-press edges; double-click select-word rides this. */
    const nt_ui_click_gesture_t gesture = nt_ui_dblclick_longpress(ctx, id, enabled && in.pressed_now, enabled && in.released_now, enabled && in.pressed, in.pos[0], in.pos[1], 0.30F, 0.50F, 6.0F);
    if (enabled && gesture.double_clicked) {
        uint32_t ws = 0U;
        uint32_t we = 0U;
        word_bounds(buffer, cur_len, st->caret, &ws, &we);
        st->anchor = ws;
        st->caret = we;
        st->drag = 0U; /* word-select replaces the drag selection */
        st->blink = 0.0F;
    }

    /* Held drag extends the selection: the anchor stays at the press point, the caret tracks x. */
    if (enabled && st->drag != 0U && in.pressed && bb.found) {
        const float local_x = (in.pos[0] - bb.x - style->pad_x) + st->scroll_x;
        st->caret = caret_from_x(style, font, buffer, cur_len, local_x);
        st->blink = 0.0F;
    }
    if (in.released_now) {
        st->drag = 0U;
    }

    /* Mark this field not-orphan for next begin's focus cleanup whenever it holds focus (even if
     * disabled this frame — it's still declared, so focus should survive). */
    if (ctx->focused_input_id == id) {
        ctx->focused_input_seen = 1U;
    }
    const bool focused = enabled && (ctx->focused_input_id == id);
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
            /* A non-empty selection is replaced by the typed char (standard editor semantics). */
            if (st->anchor != st->caret) {
                uint32_t lo = 0U;
                uint32_t hi = 0U;
                selection_span(st, &lo, &hi);
                st->caret = delete_range(buffer, lo, hi, cur_len, &changed);
                st->anchor = st->caret;
                cur_len = (uint32_t)strlen(buffer);
            }
            st->caret = insert_codepoint(buffer, buffer_size, style->max_length, st->caret, cur_len, cp, &changed);
            st->anchor = st->caret;
            cur_len = (uint32_t)strlen(buffer);
            st->blink = 0.0F;
        }
        // #endregion

        // #region physical editing keys
        const bool sel = (st->anchor != st->caret);
        const bool shift = shift_held();
        if (nt_input_key_is_pressed(NT_KEY_BACKSPACE)) {
            if (sel) {
                uint32_t lo = 0U;
                uint32_t hi = 0U;
                selection_span(st, &lo, &hi);
                st->caret = delete_range(buffer, lo, hi, cur_len, &changed);
                st->anchor = st->caret;
            } else {
                st->caret = delete_left(buffer, st->caret, cur_len, &changed);
                st->anchor = st->caret;
            }
            cur_len = (uint32_t)strlen(buffer);
            st->blink = 0.0F;
        }
        if (nt_input_key_is_pressed(NT_KEY_DELETE)) {
            if (sel) {
                uint32_t lo = 0U;
                uint32_t hi = 0U;
                selection_span(st, &lo, &hi);
                st->caret = delete_range(buffer, lo, hi, cur_len, &changed);
                st->anchor = st->caret;
            } else {
                delete_right(buffer, st->caret, cur_len, &changed);
            }
            cur_len = (uint32_t)strlen(buffer);
            st->blink = 0.0F;
        }
        if (nt_input_key_is_pressed(NT_KEY_ARROW_LEFT)) {
            /* Shift extends (move only the caret); a plain arrow with a selection collapses to its
             * left edge, otherwise steps one codepoint left. */
            if (!shift && sel) {
                uint32_t lo = 0U;
                uint32_t hi = 0U;
                selection_span(st, &lo, &hi);
                st->caret = lo;
            } else {
                st->caret = utf8_prev_boundary(buffer, st->caret);
            }
            if (!shift) {
                st->anchor = st->caret;
            }
            st->blink = 0.0F;
        }
        if (nt_input_key_is_pressed(NT_KEY_ARROW_RIGHT)) {
            if (!shift && sel) {
                uint32_t lo = 0U;
                uint32_t hi = 0U;
                selection_span(st, &lo, &hi);
                st->caret = hi;
            } else if (st->caret < cur_len) {
                const uint32_t step = (utf8_seq_len(buffer, st->caret, cur_len) > 0U) ? utf8_seq_len(buffer, st->caret, cur_len) : 1U;
                st->caret += step;
            }
            if (!shift) {
                st->anchor = st->caret;
            }
            st->blink = 0.0F;
        }
        if (nt_input_key_is_pressed(NT_KEY_HOME)) {
            st->caret = 0U;
            if (!shift) {
                st->anchor = st->caret;
            }
            st->blink = 0.0F;
        }
        if (nt_input_key_is_pressed(NT_KEY_END)) {
            st->caret = cur_len;
            if (!shift) {
                st->anchor = st->caret;
            }
            st->blink = 0.0F;
        }
        if (ctrl_held() && nt_input_key_is_pressed(NT_KEY_A)) {
            st->anchor = 0U; /* Ctrl+A selects the whole buffer */
            st->caret = cur_len;
            st->blink = 0.0F;
        }
        // #region clipboard chords (Ctrl+C/X/V)
        if (ctrl_held() && nt_input_key_is_pressed(NT_KEY_C)) {
            clipboard_copy_selection(buffer, st); /* copy selection; no-op if empty */
        }
        if (ctrl_held() && nt_input_key_is_pressed(NT_KEY_X) && nt_clipboard_available()) {
            /* No-op cut when the clipboard is unavailable: deleting locally without a store is data loss. */
            clipboard_copy_selection(buffer, st);
            if (st->anchor != st->caret) {
                uint32_t lo = 0U;
                uint32_t hi = 0U;
                selection_span(st, &lo, &hi);
                st->caret = delete_range(buffer, lo, hi, cur_len, &changed);
                st->anchor = st->caret;
                cur_len = (uint32_t)strlen(buffer);
                st->blink = 0.0F;
            }
        }
        if (ctrl_held() && nt_input_key_is_pressed(NT_KEY_V)) {
            st->caret = clipboard_paste(buffer, buffer_size, style, st, cur_len, &changed);
            cur_len = (uint32_t)strlen(buffer);
            st->blink = 0.0F;
        }
        // #endregion
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

    /* Selection highlight behind the text: emit before the label so it renders underneath. */
    if (focused && st->anchor != st->caret) {
        uint32_t lo = 0U;
        uint32_t hi = 0U;
        selection_span(st, &lo, &hi);
        const float x0 = caret_x_at(style, font, buffer, lo) - st->scroll_x;
        const float x1 = caret_x_at(style, font, buffer, hi) - st->scroll_x;
        const float sel_w = x1 - x0;
        if (sel_w > 0.0F) {
            const uint32_t scol = (style->selection_color != 0U) ? style->selection_color : 0x80E0B080U;
            emit_rect(ctx, text_layer, style->pad_x + x0, style->pad_y, sel_w, style->text.font_size, scol);
        }
    }

    const bool empty = (cur_len == 0U);
    if (!empty) {
        nt_ui_label_style_t ts = style->text;
        /* Password fields render one mask glyph per codepoint instead of the real bytes; the mask
         * is render-only (the game buffer is untouched). build_display_text is shared with the test
         * probe so the password branch is asserted without a GL capture. */
        const char *disp = (style->password) ? nt_ui_input_build_display_text(buffer, cur_len) : buffer;
        /* Float the text at pad_x - scroll_x (clipped to the field) so it tracks the caret/selection
         * when the line scrolls; an inline label would stay at x=0 and desync. */
        emit_text(ctx, text_layer, style->pad_x - st->scroll_x, style->pad_y, disp, &ts);
    } else {
        /* Empty: render the dimmed hint via the same clipped emit path as the real text (so it
         * clips/scrolls identically). Render-only -- the game buffer is never touched. */
        const char *hint = nt_ui_input_placeholder_for(buffer, placeholder, focused);
        if (hint != NULL) {
            nt_ui_label_style_t ps = style->placeholder;
            emit_text(ctx, text_layer, style->pad_x - st->scroll_x, style->pad_y, hint, &ps);
        }
    }

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
    s.selection_color = 0x80E0B080U; /* translucent blue-grey highlight behind the text */
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
