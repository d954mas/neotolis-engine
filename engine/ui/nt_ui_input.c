#include "ui/nt_ui_input.h"

#include <math.h>
#include <string.h>

#include "clipboard/nt_clipboard.h"
#include "core/nt_assert.h"
#include "font/nt_font.h"
#include "input/nt_input.h"
#include "memory/nt_mem_scratch.h"
#include "ui/nt_ui_anim.h"
#include "ui/nt_ui_clay_impl.h"
#include "ui/nt_ui_debug_hit_zones.h"
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_label.h"
#include "ui/nt_ui_state.h"
#include "utf8/nt_utf8.h"

/* Apply the field's keyboard hint on focus via the input platform layer (web shows the matching
 * soft keyboard; native/stub no-op). This is a UX hint only -- actual input restriction is the
 * allow filter, not this. The enum maps explicitly so the two vocabularies stay decoupled. */
static inline void apply_keyboard_hint(nt_ui_input_keyboard_t kb, bool password) {
    nt_text_input_mode_t mode = NT_TEXT_INPUT_TEXT;
    if (password) {
        mode = NT_TEXT_INPUT_PASSWORD;
    } else {
        switch (kb) {
        case NT_UI_KB_NUMERIC:
            mode = NT_TEXT_INPUT_NUMERIC;
            break;
        case NT_UI_KB_EMAIL:
            mode = NT_TEXT_INPUT_EMAIL;
            break;
        case NT_UI_KB_URL:
            mode = NT_TEXT_INPUT_URL;
            break;
        case NT_UI_KB_PASSWORD:
            mode = NT_TEXT_INPUT_PASSWORD;
            break;
        case NT_UI_KB_TEXT:
        default:
            mode = NT_TEXT_INPUT_TEXT;
            break;
        }
    }
    nt_input_set_text_input_mode(mode);
}

const nt_ui_widget_def_t NT_UI_INPUT_DEF = {
    .name = "nt_input",
    .pill_color = 0xFF70A0D0U,
    ._reserved = 0U,
};

#define NT_UI_INPUT_MASK_CHAR '*'

/* PC-style auto-repeat for held nav/edit keys: an initial delay, then a faster repeat rate (ImGui reference). */
#define NT_UI_INPUT_REPEAT_DELAY 0.275F
#define NT_UI_INPUT_REPEAT_RATE 0.05F

/* Per-field retained cell: caret byte-offset into the game buffer, horizontal scroll px so the
 * caret stays visible, and the accumulated blink phase. The STRING stays game-owned. */
#define NT_UI_INPUT_STATE_SALT 0x10D70001U
/* Distinct salt for the bg/border cross-fade anim slot so it never collides with a game's own
 * nt_ui_anim on the field id, nor with the input state-pool slot. */
#define NT_UI_INPUT_ANIM_SALT 0x10D70002U
typedef struct {
    uint32_t caret;      /* caret byte offset (the active selection end); always on a codepoint boundary */
    uint32_t anchor;     /* selection anchor byte offset; anchor == caret = empty selection */
    float scroll_x;      /* horizontal scroll px (text origin shifts left by this) */
    float blink;         /* accumulated seconds since the caret last reset its phase */
    float repeat_t;      /* seconds until the held repeat_key fires again (initial delay, then rate) */
    uint16_t repeat_key; /* the nt_key_t currently auto-repeating (0 = none) */
    uint8_t drag;        /* 1 while a press is dragging to extend the selection */
} nt_ui_input_state_t;

static inline uint32_t input_state_id(uint32_t id) { return nt_ui_derived_id(id, NT_UI_INPUT_STATE_SALT); }

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

/* Default allow when props->allow == NULL: any printable (drop C0/C1 control + DEL). */
static bool allow_printable(uint32_t cp) { return cp >= 0x20U && cp != 0x7FU && !(cp >= 0x80U && cp <= 0x9FU); }

// #endregion

static inline bool shift_held(void) { return nt_input_key_is_down(NT_KEY_LSHIFT) || nt_input_key_is_down(NT_KEY_RSHIFT); }
static inline bool ctrl_held(void) { return nt_input_key_is_down(NT_KEY_LCTRL) || nt_input_key_is_down(NT_KEY_RCTRL); }

/* PC-style auto-repeat core. Last-pressed wins (a newer repeatable key re-arms to it).
 * Edges arrive as params so the unit test can drive timing with fake input. */
#ifdef NT_TEST_ACCESS
bool nt_ui_input_key_repeat_step(uint16_t *repeat_key, float *repeat_t, nt_key_t k, bool pressed, bool down, float dt);
bool nt_ui_input_key_repeat_step(uint16_t *repeat_key, float *repeat_t, nt_key_t k, bool pressed, bool down, float dt)
#else
static bool key_repeat_step(uint16_t *repeat_key, float *repeat_t, nt_key_t k, bool pressed, bool down, float dt)
#endif
{
    if (pressed) {
        *repeat_key = (uint16_t)k; /* last-pressed wins: re-arm to this key */
        *repeat_t = NT_UI_INPUT_REPEAT_DELAY;
        return true; /* initial action */
    }
    if (down && *repeat_key == (uint16_t)k) {
        *repeat_t -= dt;
        if (*repeat_t <= 0.0F) {
            *repeat_t = NT_UI_INPUT_REPEAT_RATE;
            return true; /* delay/rate elapsed: repeat */
        }
        return false;
    }
    if (*repeat_key == (uint16_t)k) {
        *repeat_key = 0U; /* the armed key is no longer down: disarm */
    }
    return false;
}

#ifdef NT_TEST_ACCESS
static bool key_repeat_step(uint16_t *repeat_key, float *repeat_t, nt_key_t k, bool pressed, bool down, float dt) { return nt_ui_input_key_repeat_step(repeat_key, repeat_t, k, pressed, down, dt); }
#endif

/* Field-side wrapper: reads the live input edges for `k` and steps the per-field accumulator. */
static bool key_repeat_fires(nt_ui_input_state_t *st, nt_key_t k, float dt) { return key_repeat_step(&st->repeat_key, &st->repeat_t, k, nt_input_key_is_pressed(k), nt_input_key_is_down(k), dt); }

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
static uint32_t clipboard_paste(char *buffer, size_t buffer_size, const nt_ui_input_props_t *props, nt_ui_input_state_t *st, uint32_t cur_len, bool *changed) {
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
        const bool ok = (props->allow != NULL) ? props->allow(cp) : allow_printable(cp);
        if (!ok) {
            continue; /* predicate-filtered (e.g. numeric field drops letters) */
        }
        const uint32_t before = st->caret;
        st->caret = insert_codepoint(buffer, buffer_size, props->max_length, st->caret, cur_len, cp, changed);
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
static float caret_x_at(const nt_ui_input_style_t *style, bool password, nt_font_t font, const char *buffer, uint32_t caret) {
    if (!password) {
        return nt_font_measure_n(font, buffer, (size_t)caret, style->text.font_size, (float)style->text.letter_tracking).width;
    }
    return mask_width(style, font, codepoint_count(buffer, caret));
}

/* Map a click x (px from text origin, scroll already added) to the nearest codepoint boundary.
 * One forward decode pass (hoisted seq_len). Non-password prefix widths come from the font's
 * cached measure_n so kerning + tracking stay exact; password measures fixed-width mask glyphs. */
static uint32_t caret_from_x(const nt_ui_input_style_t *style, bool password, nt_font_t font, const char *buffer, uint32_t len, float local_x) {
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
        const float next_w = password ? mask_width(style, font, cp_index) : nt_font_measure_n(font, buffer, (size_t)next, style->text.font_size, (float)style->text.letter_tracking).width;
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
        .userData = (void *)rd, /* carries layer + transform offset; without it the walker ignores both */
        .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .clipTo = CLAY_CLIP_TO_ATTACHED_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_CENTER, .parent = CLAY_ATTACH_POINT_LEFT_CENTER}},
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

#ifdef NT_TEST_ACCESS
bool nt_ui_input_test_state(nt_ui_context_t *ctx, uint32_t id, uint32_t *out_caret, uint8_t *out_drag, float *out_scroll_x) {
    const nt_ui_input_state_t *st = (const nt_ui_input_state_t *)nt_ui_state_find(ctx, input_state_id(id));
    if (st == NULL) {
        return false;
    }
    if (out_caret != NULL) {
        *out_caret = st->caret;
    }
    if (out_drag != NULL) {
        *out_drag = st->drag;
    }
    if (out_scroll_x != NULL) {
        *out_scroll_x = st->scroll_x;
    }
    return true;
}
#endif

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
        .userData = (void *)cd, /* carries layer + transform offset; without it the walker ignores both */
        .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .clipTo = CLAY_CLIP_TO_ATTACHED_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_CENTER, .parent = CLAY_ATTACH_POINT_LEFT_CENTER}},
    };
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(caret_decl);
    nt_ui_clay_priv_close_element();
}

/* BATCHING COST: each field clips (this content-clip + a clipTo scissor per selection/text/caret float),
 * and every scissor boundary flushes the sprite+text batch -- so fields don't batch with each other
 * (~4 draw calls per focused field). This is inherent: the overlay needs floating (overlap) and a clipped
 * float carries its own scissor; the clip itself is REQUIRED (a field can straddle an outer scroll). The
 * cost is accepted -- it doesn't matter at typical field counts. Revisit only for UIs with hundreds of
 * visible fields. */
/* Open a non-floating child that fills the field's content box and clips both axes (pad_x-inset
 * horizontally, full field height vertically). The selection/text/caret floats attach to it and clip to
 * its box; the walker further intersects that with the box's full ancestor chain (so an outer tab scroll
 * clips them too). The line is vertically centered by the floats' LEFT_CENTER attach. Caller must close. */
static void emit_content_clip_open(nt_ui_context_t *ctx, uint8_t text_layer) {
    nt_ui_element_data_t *id = NT_MEM_SCRATCH_ALLOC(nt_ui_element_data_t);
    NT_ASSERT(id != NULL && "nt_ui_input: scratch alloc failed (content clip data)");
    *id = (nt_ui_element_data_t){.user_data = NULL, .layer = text_layer, .flags = 0U, .transform = nt_ui_transform_defaults(), .opacity = 1.0F};
    const Clay_ElementDeclaration content_decl = {
        .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}},
        .userData = (void *)id,
        .clip = {.horizontal = true, .vertical = true},
    };
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(content_decl);
}

/* Float the text over the content-clip, sharing the SAME -scroll_x origin as the caret/selection so the
 * overlay z-orders by declaration (selection behind, text, caret on top). clipTo=ATTACHED_PARENT clips it
 * to the content-clip; the walker intersects that with the clip's full ancestor chain, so an outer tab
 * scroll clips it too (a raw floating clipTo would leak the text past the scroll). */
static void emit_text(nt_ui_context_t *ctx, uint8_t text_layer, float x, float y, const char *disp, const nt_ui_label_style_t *ts) {
    nt_ui_transform_t tt = nt_ui_transform_defaults();
    tt.offset_x = x;
    tt.offset_y = y;
    nt_ui_element_data_t *wd = NT_MEM_SCRATCH_ALLOC(nt_ui_element_data_t);
    NT_ASSERT(wd != NULL && "nt_ui_input: scratch alloc failed (text wrapper data)");
    *wd = (nt_ui_element_data_t){.user_data = NULL, .layer = text_layer, .flags = (uint8_t)NT_UI_ELEM_FLAG_HAS_TRANSFORM, .transform = tt, .opacity = 1.0F};
    const Clay_ElementDeclaration text_decl = {
        .layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}},
        .userData = (void *)wd, /* carries layer + transform offset (-scroll_x); without it the text ignores scroll */
        .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .clipTo = CLAY_CLIP_TO_ATTACHED_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_CENTER, .parent = CLAY_ATTACH_POINT_LEFT_CENTER}},
    };
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(text_decl);
    nt_ui_label(ctx, nt_ui_make_element_data(text_layer, NULL), disp, ts);
    nt_ui_clay_priv_close_element();
}

/* Build a frame-scratch bg-sprite payload from an art ref, or NULL when unset/unresolved. The style is
 * const + shared across fields, so we resolve a LOCAL copy (O(1) name find; a pre-resolved ref_idx is a
 * no-op) rather than memoizing into the shared style. slice9_override={0} = use the region's BAKED slice9
 * borders: a 9-slice frame stretches its borders, a plain sprite stretches whole (walker decides). */
static nt_ui_image_payload_t *make_bg_payload(nt_atlas_region_ref_t art) {
    if (art.atlas.id == 0U) {
        return NULL;
    }
    nt_atlas_resolve_ref(&art);
    if (art.region == NT_ATLAS_INVALID_REGION) {
        return NULL;
    }
    nt_ui_image_payload_t *p = NT_MEM_SCRATCH_ALLOC(nt_ui_image_payload_t);
    NT_ASSERT(p != NULL && "nt_ui_input: scratch alloc failed (bg art payload)");
    *p = (nt_ui_image_payload_t){.atlas = art.atlas, .region_index = art.region, .slice9_scale = 1.0F};
    return p;
}

/* Resolve the active interaction state to a visual skin. Order: disabled > focused > hover > idle.
 * A non-idle skin left ALL-ZERO inherits idle, so hover/disabled look like idle until authored. */
static const nt_ui_input_skin_t *resolve_skin(const nt_ui_input_style_t *style, bool enabled, bool focused, bool hovered) {
    int state = NT_UI_INPUT_IDLE;
    if (!enabled) {
        state = NT_UI_INPUT_DISABLED;
    } else if (focused) {
        state = NT_UI_INPUT_FOCUSED;
    } else if (hovered) {
        state = NT_UI_INPUT_HOVER;
    }
    const nt_ui_input_skin_t *s = &style->skin[state];
    if (state != NT_UI_INPUT_IDLE && s->bg_art.atlas.id == 0U && s->bg_color == 0U && s->border_color == 0U) {
        return &style->skin[NT_UI_INPUT_IDLE]; /* unauthored non-idle state: inherit idle */
    }
    return s;
}

// #endregion

// #region input_text step helpers
/* Shared per-frame working set threaded through the four step helpers below (focus_step/edit_step/
 * scroll_step/emit_field). Pointer-passed so the helpers stay static (inlined, one TU) without an
 * unwieldy parameter list. The const inputs (ctx/data/style/props/buffer...) plus the live state slot
 * `st` and the derived per-frame locals (font, line_h, cur_len, focused, in, bb, claimed_now). */
typedef struct {
    nt_ui_context_t *ctx;
    const nt_ui_element_data_t *data;
    uint8_t text_layer;
    uint32_t id;
    char *buffer;
    size_t buffer_size;
    const nt_ui_input_props_t *props;
    const nt_ui_input_style_t *style;
    const Clay_ElementDeclaration *decl;
    bool enabled;
    /* derived/live */
    nt_ui_input_state_t *st;
    nt_font_t font;
    float line_h;
    uint32_t cur_len;
    nt_ui_events_t in;
    nt_ui_bbox_t bb;
    bool focused;
    bool claimed_now;
} input_frame_t;

/* focus-order bookkeeping (Tab wrap) + the single mutating interaction step + focus capture / drag.
 * Resolves f->st, f->in, f->bb, f->claimed_now, f->focused for the rest of the frame. */
static void focus_step(input_frame_t *f) {
    nt_ui_context_t *ctx = f->ctx;
    const uint32_t id = f->id;
    char *buffer = f->buffer;
    const nt_ui_input_props_t *props = f->props;
    const nt_ui_input_style_t *style = f->style;

    // #region focus-order bookkeeping (Tab wrap target)
    if (ctx->focus_first_id == 0U) {
        ctx->focus_first_id = id;
    }
    /* A pending Tab seek set by the previously-declared focused field: this field claims focus.
     * claimed_now suppresses this field's OWN Tab handling so the still-pressed Tab key does not
     * immediately re-advance focus off the field that just received it. Only an ENABLED field claims:
     * a disabled field must leave the seek intact so Tab skips it to the next enabled field. */
    f->claimed_now = false;
    if (f->enabled && ctx->focus_tab_seek != 0U && ctx->focus_tab_seek != id) {
        ctx->focused_input_id = id;
        ctx->focus_tab_seek = 0U;
        f->claimed_now = true;
        apply_keyboard_hint(props->keyboard, props->password); /* Tab into the field also surfaces the keyboard */
    }
    // #endregion

    // #region interaction + focus capture
    const bool enabled = f->enabled;
    const uint32_t cur_len = f->cur_len;
    nt_font_t font = f->font;
    nt_ui_input_state_t *st = (nt_ui_input_state_t *)nt_ui_state(ctx, input_state_id(id), (uint32_t)sizeof(nt_ui_input_state_t), NT_UI_STATE_TAG('i', 'n', 'p', 't'));
    f->st = st;
    /* Contract: the game must not rewrite a FOCUSED field's buffer CONTENT mid-edit. Only length
     * changes are clamped here; a content rewrite that lands the caret mid-codepoint is undefined. */
    if (st->caret > cur_len) {
        st->caret = cur_len; /* the game shortened the buffer behind our back */
    }
    if (st->anchor > cur_len) {
        st->anchor = cur_len; /* keep the selection anchor in-bounds too */
    }

    /* ONE mutating interaction step (the canonical-step contract): the events step with double_click
     * opt-in. Word-select reads double_clicked off this single result — no separate call. long_press is
     * not wanted here (<=0 disables it + hold_progress, keeping the gesture cell dbl-only). The app-wide
     * dbl window + move radius come from the context (nt_ui_set_gesture_constants), not hard-coded. */
    static const nt_ui_events_cfg_t s_field_gesture_cfg = {.long_press_secs = 0.0F, .double_click = true};
    nt_ui_events_t in;
    if (enabled) {
        in = nt_ui_events(ctx, id, &s_field_gesture_cfg);
    } else {
        in = (nt_ui_events_t){0};
        nt_ui_block_pointer(ctx, id, NULL);
        nt_ui_debug_record_disabled_zone(ctx, id, NULL);
        /* A disabled field can't be clicked to refocus and gates out Esc/Tab while focused==false, so
         * a still-declared disabled field would hold focus forever (any_focused stuck true). Drop it
         * the frame it's declared disabled. */
        if (ctx->focused_input_id == id) {
            ctx->focused_input_id = 0U;
        }
    }
    /* The real press-began edge (one-shot, survives a same-frame press+release — `pressed` does not, so a
     * sub-frame tap would otherwise never focus the field / place the caret). */
    const bool pressed_now = in.pressed_now;

    /* Map a pointer x (UI-space) to a codepoint boundary via the prev-frame bbox. */
    const nt_ui_bbox_t bb = nt_ui_get_bbox(ctx, id);
    if (enabled && pressed_now) {
        const bool was_focused = (ctx->focused_input_id == id);
        ctx->focused_input_id = id;
        st->blink = 0.0F;
        if (bb.found) {
            /* On the press frame pos == press point (the pointer has not moved yet). */
            const float local_x = (in.pos[0] - bb.x - style->pad_x) + st->scroll_x;
            st->caret = caret_from_x(style, props->password, font, buffer, cur_len, local_x);
        }
        st->anchor = st->caret; /* a fresh press collapses the selection to the click point */
        st->drag = 1U;
        if (!was_focused) {
            apply_keyboard_hint(props->keyboard, props->password); /* surface the soft keyboard on web */
        }
    }

    /* Double-click select-word rides the same single events step (cfg.double_click). */
    if (enabled && in.double_clicked) {
        uint32_t ws = 0U;
        uint32_t we = 0U;
        word_bounds(buffer, cur_len, st->caret, &ws, &we);
        st->anchor = ws;
        st->caret = we;
        st->drag = 0U; /* word-select replaces the drag selection */
        st->blink = 0.0F;
    }

    /* Drag only lives while THIS field holds focus. If focus moved off mid-drag (Esc unfocused it, or
     * another field claimed it), abandon the drag so a stale flag can't keep moving the caret on an
     * unfocused field. */
    if (ctx->focused_input_id != id) {
        st->drag = 0U;
    }
    /* Held drag extends the selection: the anchor stays at the press point, the caret tracks x. */
    if (enabled && st->drag != 0U && in.pressed && bb.found) {
        const float local_x = (in.pos[0] - bb.x - style->pad_x) + st->scroll_x;
        st->caret = caret_from_x(style, props->password, font, buffer, cur_len, local_x);
        st->blink = 0.0F;
    }
    if (in.released) {
        st->drag = 0U;
    }

    /* Mark this field not-orphan for next begin's focus cleanup whenever it holds focus (even if
     * disabled this frame — it's still declared, so focus should survive). */
    if (ctx->focused_input_id == id) {
        ctx->focused_input_seen = 1U;
    }
    f->in = in;
    f->bb = bb;
    f->focused = enabled && (ctx->focused_input_id == id);
    // #endregion
}

/* The focused edit pipeline: drain the typed-char ring, then run the physical editing keys
 * (delete/arrows/home/end), selection chords (Shift/Ctrl+A), clipboard (Ctrl+C/X/V), submit + Tab/Esc.
 * No-op when the field is not focused. Sets the changed/submitted out-flags; cur_len re-derived in f->cur_len. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void edit_step(input_frame_t *f, bool *changed, bool *submitted) {
    if (!f->focused) {
        return;
    }
    nt_ui_context_t *ctx = f->ctx;
    const uint32_t id = f->id;
    char *buffer = f->buffer;
    const size_t buffer_size = f->buffer_size;
    const nt_ui_input_props_t *props = f->props;
    nt_ui_input_state_t *st = f->st;
    uint32_t cur_len = f->cur_len;
    const bool claimed_now = f->claimed_now;
    {
        // #region drain typed chars (FIFO from the input ring)
        uint32_t cp = 0U;
        while (nt_input_pop_char(&cp)) {
            const bool ok = (props->allow != NULL) ? props->allow(cp) : allow_printable(cp);
            if (!ok) {
                continue;
            }
            /* A non-empty selection is replaced by the typed char (standard editor semantics). */
            if (st->anchor != st->caret) {
                uint32_t lo = 0U;
                uint32_t hi = 0U;
                selection_span(st, &lo, &hi);
                st->caret = delete_range(buffer, lo, hi, cur_len, changed);
                st->anchor = st->caret;
                cur_len = (uint32_t)strlen(buffer);
            }
            st->caret = insert_codepoint(buffer, buffer_size, props->max_length, st->caret, cur_len, cp, changed);
            st->anchor = st->caret;
            cur_len = (uint32_t)strlen(buffer);
            st->blink = 0.0F;
        }
        // #endregion

        // #region physical editing keys
        const bool sel = (st->anchor != st->caret);
        const bool shift = shift_held();
        const float dt = ctx->frame_dt;
        if (key_repeat_fires(st, NT_KEY_BACKSPACE, dt)) {
            if (sel) {
                uint32_t lo = 0U;
                uint32_t hi = 0U;
                selection_span(st, &lo, &hi);
                st->caret = delete_range(buffer, lo, hi, cur_len, changed);
                st->anchor = st->caret;
            } else {
                st->caret = delete_left(buffer, st->caret, cur_len, changed);
                st->anchor = st->caret;
            }
            cur_len = (uint32_t)strlen(buffer);
            st->blink = 0.0F;
        }
        if (key_repeat_fires(st, NT_KEY_DELETE, dt)) {
            if (sel) {
                uint32_t lo = 0U;
                uint32_t hi = 0U;
                selection_span(st, &lo, &hi);
                st->caret = delete_range(buffer, lo, hi, cur_len, changed);
                st->anchor = st->caret;
            } else {
                delete_right(buffer, st->caret, cur_len, changed);
            }
            cur_len = (uint32_t)strlen(buffer);
            st->blink = 0.0F;
        }
        if (key_repeat_fires(st, NT_KEY_ARROW_LEFT, dt)) {
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
        if (key_repeat_fires(st, NT_KEY_ARROW_RIGHT, dt)) {
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
        if (key_repeat_fires(st, NT_KEY_HOME, dt)) {
            st->caret = 0U;
            if (!shift) {
                st->anchor = st->caret;
            }
            st->blink = 0.0F;
        }
        if (key_repeat_fires(st, NT_KEY_END, dt)) {
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
                st->caret = delete_range(buffer, lo, hi, cur_len, changed);
                st->anchor = st->caret;
                cur_len = (uint32_t)strlen(buffer);
                st->blink = 0.0F;
            }
        }
        if (ctrl_held() && nt_input_key_is_pressed(NT_KEY_V)) {
            st->caret = clipboard_paste(buffer, buffer_size, props, st, cur_len, changed);
            cur_len = (uint32_t)strlen(buffer);
            st->blink = 0.0F;
        }
        // #endregion
        if (nt_input_key_is_pressed(NT_KEY_ENTER)) {
            *submitted = true;
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
    f->cur_len = cur_len;
}

/* Keep the caret visible: compute the inner content width and slide st->scroll_x so the caret's right
 * edge stays inside it (FIXED width from decl, else the prev-frame bbox for a GROW/responsive field).
 * Also accumulates the blink phase while focused. */
static void scroll_step(input_frame_t *f) {
    nt_ui_context_t *ctx = f->ctx;
    const nt_ui_input_style_t *style = f->style;
    const nt_ui_input_props_t *props = f->props;
    const Clay_ElementDeclaration *decl = f->decl;
    char *buffer = f->buffer;
    nt_ui_input_state_t *st = f->st;
    nt_font_t font = f->font;
    const nt_ui_bbox_t bb = f->bb;

    /* Blink phase accumulates only while focused; reset on any edit/caret move above. */
    if (f->focused) {
        st->blink += ctx->frame_dt;
    }

    // #region keep caret visible (horizontal scroll)
    /* Inner width = the content box (field width minus pad_x on both sides). FIXED width is known this
     * frame from decl; a GROW/responsive field has no decl width, so fall back to the previous frame's
     * measured bbox (one-frame IM lag) instead of disabling scroll and letting long text escape the clip. */
    float inner_w = 0.0F;
    if (decl != NULL && decl->layout.sizing.width.type == CLAY__SIZING_TYPE_FIXED) {
        inner_w = decl->layout.sizing.width.size.minMax.min - (style->pad_x * 2.0F);
    } else if (bb.found) {
        inner_w = bb.width - (style->pad_x * 2.0F);
    }
    if (inner_w > 0.0F) {
        const float caret_px = caret_x_at(style, props->password, font, buffer, st->caret);
        /* Reserve the caret width on the right edge: the content clip is the inner box, so a caret at
         * end-of-line sitting exactly on the clip edge would be scissored away. Scroll so its RIGHT
         * edge (caret_px + caret_width) stays inside inner_w, keeping it visible. */
        const float caret_right = caret_px + style->caret_width;
        if (caret_right - st->scroll_x > inner_w) {
            st->scroll_x = caret_right - inner_w;
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
}

/* Compose + emit: the field root (flat bg / sprite bg / border per the resolved skin), the content-clip
 * child, the selection highlight, the text or placeholder, and the blinking caret. Pure render — no edit. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void emit_field(input_frame_t *f) {
    nt_ui_context_t *ctx = f->ctx;
    const nt_ui_element_data_t *data = f->data;
    const uint8_t text_layer = f->text_layer;
    const uint32_t id = f->id;
    char *buffer = f->buffer;
    const nt_ui_input_props_t *props = f->props;
    const nt_ui_input_style_t *style = f->style;
    const Clay_ElementDeclaration *decl = f->decl;
    const bool enabled = f->enabled;
    nt_ui_input_state_t *st = f->st;
    nt_font_t font = f->font;
    const float line_h = f->line_h;
    const uint32_t cur_len = f->cur_len;
    const bool focused = f->focused;
    const uint8_t layer = (data != NULL) ? data->layer : 0U;
    void *user = (data != NULL) ? data->user_data : NULL;

    // #region compose (bg rect + text/placeholder + caret)
    Clay_ElementDeclaration root = (decl != NULL) ? *decl : (Clay_ElementDeclaration){0};
    root.id = (Clay_ElementId){.id = id};
    root.userData = (void *)nt_ui_make_element_data(layer, user);
    /* Pick the per-state skin (disabled > focused > hover > idle); the bg/border below all read from it. */
    const nt_ui_input_skin_t *skin = resolve_skin(style, enabled, focused, f->in.hovered);

    /* Optional sprite background: when bg_art resolves the field draws the sprite (9-slice if the region
     * has baked borders) and bg_color becomes its TINT (zero = untinted) -- the same .image +
     * backgroundColor convention as nt_ui_panel/button. Unset/unresolved = the flat bg_color rect. The
     * sprite art SNAPS across states (button precedent); only the flat color cross-fades. */
    nt_ui_image_payload_t *bg_art_p = make_bg_payload(skin->bg_art);
    if (bg_art_p != NULL) {
        root.image = (Clay_ImageElementConfig){.imageData = bg_art_p};
    }

    const bool has_bg = (skin->bg_color != 0U);
    const bool has_border = (style->border_width > 0.0F && skin->border_color != 0U);
    if (style->state_speed > 0.0F && bg_art_p == NULL) {
        /* Eased cross-fade (flat-color path only): the anim slot retains the previous frame's resolved
         * channels, so a skin-state change ramps bg/border from the old color to the new over state_speed.
         * Pack the 8 resolved channels (bg RGBA + border RGBA) into the slot's free float lanes; tint_t
         * carries an unused 0 (the lerp is direct per-channel, not a from/to scalar). */
        const Clay_Color bg_t = has_bg ? nt_ui_unpack_abgr(skin->bg_color) : (Clay_Color){0};
        const Clay_Color bd_t = has_border ? nt_ui_unpack_abgr(skin->border_color) : (Clay_Color){0};
        /* Lane map (nt_ui_anim_target_t constraints: scale_xyz > 0, opacity/tint_t in [0,1], off/rot finite):
         * bg RGB -> off_xyz, bg A/255 -> opacity; border RGB -> rot_xyz, border A/255 -> tint_t. scale = 1
         * (unused, satisfies the > 0 assert). The slot retains last frame's channels so the lerp cross-fades. */
        nt_ui_anim_target_t tgt;
        memset(&tgt, 0, sizeof tgt); /* memset, not = {0}: emscripten -Werror rejects {0} on aggregate-first */
        tgt.scale_x = 1.0F;
        tgt.scale_y = 1.0F;
        tgt.scale_z = 1.0F;
        tgt.off_x = bg_t.r;
        tgt.off_y = bg_t.g;
        tgt.off_z = bg_t.b;
        tgt.opacity = bg_t.a / 255.0F;
        tgt.rot_x = bd_t.r;
        tgt.rot_y = bd_t.g;
        tgt.rot_z = bd_t.b;
        tgt.tint_t = bd_t.a / 255.0F;
        const nt_ui_anim_interaction_t *a = nt_ui_anim(ctx, nt_ui_derived_id(id, NT_UI_INPUT_ANIM_SALT), &tgt, style->state_speed, 0.0F);
        if (has_bg) {
            root.backgroundColor = (Clay_Color){.r = a->off_x, .g = a->off_y, .b = a->off_z, .a = a->opacity * 255.0F};
        }
        if (has_border) {
            root.border = (Clay_BorderElementConfig){
                .color = (Clay_Color){.r = a->rot_x, .g = a->rot_y, .b = a->rot_z, .a = a->tint_t * 255.0F},
                .width = {.left = (uint16_t)style->border_width, .right = (uint16_t)style->border_width, .top = (uint16_t)style->border_width, .bottom = (uint16_t)style->border_width}};
        }
    } else {
        /* state_speed == 0 (default) or sprite-bg: instant snap (no cross-fade). */
        if (has_bg) {
            root.backgroundColor = nt_ui_unpack_abgr(skin->bg_color);
        }
        if (has_border) {
            root.border = (Clay_BorderElementConfig){
                .color = nt_ui_unpack_abgr(skin->border_color),
                .width = {.left = (uint16_t)style->border_width, .right = (uint16_t)style->border_width, .top = (uint16_t)style->border_width, .bottom = (uint16_t)style->border_width}};
        }
    }
    /* Horizontal inset only: pad_x positions the text origin + bounds the clip. Vertical padding is 0 on
     * purpose — the single line is vertically CENTERED and the text/caret float clip to this box on BOTH
     * axes (Clay floating clipTo), so a pad_y inset would shrink the clip below the line height and shave
     * descenders. The vertical margin is the natural (field_height - line_height)/2 from centering. */
    root.layout.padding = (Clay_Padding){.left = (uint16_t)style->pad_x, .right = (uint16_t)style->pad_x, .top = 0, .bottom = 0};

    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(root);
    nt_ui_widget_register(ctx, id, &NT_UI_INPUT_DEF, NULL, enabled);

    /* Content-clip child: pad_x-inset horizontally (scissors long text), full field height vertically.
     * The selection/text/caret attach to it and are vertically centered, so descenders are never shaved
     * by a pad_y inset. Without this child the floats would clip to the root's border box and scrolled
     * text would leak past pad_x. */
    emit_content_clip_open(ctx, text_layer);

    /* Selection highlight behind the text: emit before the label so it renders underneath. */
    if (focused && st->anchor != st->caret) {
        uint32_t lo = 0U;
        uint32_t hi = 0U;
        selection_span(st, &lo, &hi);
        const float x0 = caret_x_at(style, props->password, font, buffer, lo) - st->scroll_x;
        const float x1 = caret_x_at(style, props->password, font, buffer, hi) - st->scroll_x;
        const float sel_w = x1 - x0;
        if (sel_w > 0.0F) {
            const uint32_t scol = (style->selection_color != 0U) ? style->selection_color : 0x80E0B080U;
            emit_rect(ctx, text_layer, x0, 0.0F, sel_w, line_h, scol);
        }
    }

    const bool empty = (cur_len == 0U);
    if (!empty) {
        nt_ui_label_style_t ts = style->text;
        /* Password fields render one mask glyph per codepoint instead of the real bytes; the mask
         * is render-only (the game buffer is untouched). build_display_text is shared with the test
         * probe so the password branch is asserted without a GL capture. */
        const char *disp = (props->password) ? nt_ui_input_build_display_text(buffer, cur_len) : buffer;
        /* Float the text at -scroll_x inside the content clip so it tracks the caret/selection when
         * the line scrolls; an inline label would stay at x=0 and desync. */
        emit_text(ctx, text_layer, -st->scroll_x, 0.0F, disp, &ts);
    } else {
        /* Empty: render the dimmed hint via the same clipped emit path as the real text (so it
         * clips/scrolls identically). Render-only -- the game buffer is never touched. */
        const char *hint = nt_ui_input_placeholder_for(buffer, props->placeholder, focused);
        if (hint != NULL) {
            nt_ui_label_style_t ps = style->placeholder;
            emit_text(ctx, text_layer, -st->scroll_x, 0.0F, hint, &ps);
        }
    }

    if (focused) {
        const float caret_px = caret_x_at(style, props->password, font, buffer, st->caret) - st->scroll_x;
        const bool blink_on = (style->caret_blink_rate <= 0.0F) || (fmodf(st->blink, style->caret_blink_rate) < (style->caret_blink_rate * 0.5F));
        if (blink_on) {
            emit_caret(ctx, text_layer, caret_px, 0.0F, style->caret_width, line_h, style->caret_color);
        }
    }

    nt_ui_clay_priv_close_element(); /* content clip */
    nt_ui_clay_priv_close_element(); /* root */
    // #endregion
}
// #endregion

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
bool nt_ui_input_text(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t text_layer, uint32_t id, char *buffer, size_t buffer_size, const nt_ui_input_props_t *props,
                      const nt_ui_input_style_t *style, const Clay_ElementDeclaration *decl, bool enabled, bool *out_submitted) {
    // #region entry asserts
    NT_ASSERT(ctx != NULL && "nt_ui_input_text: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_input_text: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(props != NULL && "nt_ui_input_text: props must be non-NULL");
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
    nt_font_t font = ctx->fonts[style->text.font_id];
    /* Font line box (ascent-descent) at the field's font size: the single line, caret and selection are
     * all this tall and vertically centered in the content box, so descenders fit and the caret matches. */
    const nt_font_metrics_t fm = nt_font_get_metrics(font);
    const float line_h = (fm.units_per_em > 0) ? ((float)(fm.ascent - fm.descent) * (style->text.font_size / (float)fm.units_per_em)) : style->text.font_size;

    /* The string is game-owned; the engine assumes a valid NUL-terminated buffer. */
    NT_ASSERT(memchr(buffer, '\0', buffer_size) != NULL && "nt_ui_input_text: buffer must be NUL-terminated within buffer_size");

    /* Thin orchestrator over the four static step helpers (focus → edit → scroll → emit); the per-frame
     * working set is threaded through the input_frame_t. ABI + semantics unchanged from the inline body. */
    input_frame_t f;
    memset(&f, 0, sizeof f); /* memset, not = {0}: emscripten -Werror rejects {0} on aggregate-first */
    f.ctx = ctx;
    f.data = data;
    f.text_layer = text_layer;
    f.id = id;
    f.buffer = buffer;
    f.buffer_size = buffer_size;
    f.props = props;
    f.style = style;
    f.decl = decl;
    f.enabled = enabled;
    f.font = font;
    f.line_h = line_h;
    f.cur_len = (uint32_t)strlen(buffer);

    focus_step(&f);

    bool changed = false;
    bool submitted = false;
    edit_step(&f, &changed, &submitted);
    scroll_step(&f);
    emit_field(&f);

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
    s.skin[NT_UI_INPUT_IDLE] = (nt_ui_input_skin_t){.bg_color = 0xFF202020U, .border_color = 0xFF505050U};
    s.skin[NT_UI_INPUT_FOCUSED] = (nt_ui_input_skin_t){.bg_color = 0xFF303030U, .border_color = 0xFF80B0E0U};
    s.skin[NT_UI_INPUT_DISABLED] = (nt_ui_input_skin_t){.bg_color = 0xFF181818U, .border_color = 0xFF383838U}; /* dimmer than idle */
    /* skin[NT_UI_INPUT_HOVER] left zero -> inherits idle (author fills it to differentiate). */
    s.caret_color = 0xFFFFFFFFU;
    s.selection_color = 0x80E0B080U; /* translucent blue-grey highlight behind the text */
    s.caret_blink_rate = 1.0F;
    s.caret_width = 2.0F;
    s.border_width = 1.0F;
    s.pad_x = 6.0F;
    s.pad_y = 4.0F;
    return s;
}
