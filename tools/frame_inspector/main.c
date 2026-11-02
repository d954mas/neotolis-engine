/* Frame inspector: opens capture files (render architecture, "Capture text") and shows what each recorded
 * frame did. Usage: frame_inspector [file...] [--frame SEQUENCE] [--page frames|commands|info|init|end]
 * [--select RECORD] [--filter TEXT] [--monkey SECONDS [SEED]]; Open... adds more files. --frame takes the
 * capture's "frame sequence=", --select a record's @ number in that frame. Run it by its path: its asset pack
 * is found next to argv[0].
 * Pages: Frames (per-file tracks of draw calls, GL calls, upload and records on one zoomable axis; a click
 * picks a frame, a click again opens it), Commands (the logical tree with a panel for the selection or a
 * Shift range), Information (numbers that filter the tree to their records), Init and End state.
 * --monkey drives random clicks, drags, wheel and keys for that long and prints the frame times. */

#include "app/nt_app.h"
#include "atlas/nt_atlas.h"
#include "clipboard/nt_clipboard.h"
#include "color/nt_color.h"
#include "core/nt_assert.h"
#include "core/nt_core.h"
#include "fi_model.h"
#include "file_dialog/nt_file_dialog.h"
#include "font/nt_font.h"
#include "frame_inspector.h"
#include "fs/nt_fs.h"
#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_capture_text.h"
#include "hash/nt_hash.h"
#include "input/nt_input.h"
#include "log/nt_log.h"
#include "material/nt_material.h"
#include "material/nt_program_ref.h"
#include "math/nt_math.h"
#include "memory/nt_mem_scratch.h"
#include "nt_pack_format.h"
#include "render/nt_render_defs.h"
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "time/nt_time.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_input.h"
#include "ui/nt_ui_label.h"
#include "ui/nt_ui_scale.h"
#include "ui/nt_ui_scroll.h"
#include "ui/nt_ui_vlist.h"
#include "window/nt_window.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FILES 16
#define ROW_H 20.0F
#define FONT 15.0F
#define LAYER_RECT 0
#define LAYER_TEXT 1
#define SIDE_W 560
#define HISTORY 64
#define TRACK_H 40.0F
#define LABEL_NAME_W 110
#define LABEL_VALUE_W 80
#define LABEL_MAX_W 90
#define LABEL_GAP 6
#define TRACK_LABEL_W (float)(LABEL_NAME_W + LABEL_VALUE_W + LABEL_MAX_W + (3 * LABEL_GAP))

// #region palette
static const Clay_Color k_bg = {24, 26, 30, 255};
static const Clay_Color k_panel = {32, 35, 41, 255};
static const Clay_Color k_hover = {60, 66, 80, 255};
static const Clay_Color k_select = {92, 70, 24, 255}; /* amber: never confused with blue draw rows */
static const Clay_Color k_bar = {110, 116, 130, 255};
static const Clay_Color k_bar_bad = {200, 70, 60, 255};
static const Clay_Color k_bar_warn = {220, 150, 50, 255};
static const Clay_Color k_bar_sel = {90, 160, 255, 255};
static const Clay_Color k_hover_bar = {170, 176, 190, 255};
static const Clay_Color k_draw_bg = {34, 52, 82, 255}; /* draws and clears, as in Spector */
static const Clay_Color k_select_border = {250, 200, 90, 255};
// clang-format off
#define TEXT_(r, g, b) {.font_id = 0, .font_size = FONT, .color = NT_RGBA8(r, g, b, 255), .wrap_mode = CLAY_TEXT_WRAP_NONE}
// clang-format on
static const nt_ui_label_style_t k_text = TEXT_(220, 222, 228);
static const nt_ui_label_style_t k_dim = TEXT_(140, 145, 155);
static const nt_ui_label_style_t k_head = TEXT_(255, 255, 255);
static const nt_ui_label_style_t k_link = TEXT_(120, 180, 255);
static const nt_ui_label_style_t k_bad = TEXT_(255, 110, 100);
static const nt_ui_label_style_t k_warn = TEXT_(245, 150, 50);
static const nt_ui_label_style_t k_note = TEXT_(235, 150, 190);
static const nt_ui_label_style_t k_marker = TEXT_(235, 215, 100);
static const nt_ui_label_style_t k_backend = TEXT_(165, 185, 225);
static const nt_ui_label_style_t k_state = TEXT_(130, 215, 140);
static const nt_ui_label_style_t k_skip = TEXT_(110, 200, 200); /* a GL call the backend cache left out */
static const nt_ui_label_style_t k_link_wrap = {.font_id = 0, .font_size = FONT, .color = NT_RGBA8(120, 180, 255, 255)};
static const nt_ui_label_style_t k_changed = TEXT_(215, 150, 255); /* state that differs from the previous draw */
static const nt_ui_label_style_t k_big = {.font_id = 0, .font_size = 24.0F, .color = NT_RGBA8(120, 180, 255, 255), .wrap_mode = CLAY_TEXT_WRAP_NONE};
static const nt_ui_label_style_t k_big_dim = {.font_id = 0, .font_size = 24.0F, .color = NT_RGBA8(140, 145, 155, 255), .wrap_mode = CLAY_TEXT_WRAP_NONE};
static const nt_ui_label_style_t k_wrap = {.font_id = 0, .font_size = FONT, .color = NT_RGBA8(220, 222, 228, 255)};
// #endregion

// #region state
typedef enum { VIEW_LOGICAL, VIEW_RAW, VIEW_GL, VIEW_COUNT } view_t;
/* One task per page, as in Spector: frames over time, the frame's commands, its numbers, its start and end state. */
typedef enum { PAGE_FRAMES, PAGE_COMMANDS, PAGE_INFO, PAGE_INIT, PAGE_END, PAGE_COUNT } page_t;
typedef enum { TRACK_DRAW_CALLS, TRACK_GL_CALLS, TRACK_UPLOAD, TRACK_RECORDS, TRACK_COUNT } track_t;

/* A place to go back to: a page, a frame and the row selected there. */
typedef struct {
    int page;
    uint32_t file, frame, selected;
} place_t;

static struct {
    fi_file_t files[MAX_FILES];
    uint32_t file_count;
    uint32_t file, frame; /* the selected frame */
    uint32_t hover_file, hover_frame;
    uint32_t selected; /* the selected row id: a record or FI_NODE_BASE + node */
    uint32_t anchor;   /* the other end of a Shift range, FI_NONE without one */
    view_t view;
    bool draws_only, problems, state_all;
    char filter[64];
    fi_query_t query;
    char query_label[96];
    int page;
    place_t history[HISTORY]; /* places left by a click or a jump, for Back */
    uint32_t history_count;
    place_t future[HISTORY];
    uint32_t future_count;
    fi_query_t picked_query; /* a number clicked once outside Commands; a second click opens its records */
    uint64_t click_target;   /* the last click's target and time: a second click within 0.4 s opens it */
    double click_time;
    uint32_t flash; /* the row a jump landed on, lit until flash_until */
    double flash_until;
    double axis_first; /* the frame axis: first visible frame (fractional) and how many are visible */
    double axis_count;
    bool axis_fit; /* every frame shows, as files come and go */
    double drag_first;
    double strip_grab;  /* where in the strip's thumb the pointer took it, in frames */
    bool axis_dragged;  /* a track or the strip holds the axis this frame: no spring back */
    float track_wheel;  /* the wheel over the tracks: it zooms the frame axis */
    float track_pan;    /* Shift+wheel or a sideways wheel over the tracks: it pans the axis */
    uint8_t *collapsed; /* per logical row of the selected frame */
    uint32_t *vis;      /* visible row ids */
    uint32_t *vis_row;  /* their logical row, FI_NONE in flat lists */
    uint16_t *vis_depth;
    uint32_t vis_count;
    bool dirty;
    bool follow;     /* scroll the tree to the selection */
    bool scroll_top; /* a new frame opens at its first row */
    /* Side effects the UI asks for, run between frames (run_requests). */
    bool open_requested;
    bool copy_requested;
    uint32_t close_file; /* the file whose x was clicked; FI_NONE */
    uint32_t cell_seq;
    uint32_t frame_epoch; /* changes with the selected frame; keys the caches */
    float fb_w;
} s;

_Alignas(max_align_t) static uint8_t s_scratch_mem[(size_t)4U * 1024U * 1024U]; /* frame scratch: UI text and Clay data */
static nt_ui_context_t *s_ctx;
static void *s_ui_arena;
static nt_resource_t s_atlas;
static nt_resource_t s_font_res;
static nt_font_t s_font;
static bool s_bound_atlas, s_bound_font;
static nt_material_t s_sprite_material, s_text_material;
static nt_program_ref_t s_sprite_program, s_text_program;

/* This frame's keys, read once so every handler sees one set (a monkey run fills it instead). */
static struct {
    bool pressed[NT_KEY_COUNT];
    bool shift, ctrl, alt;
} s_keys;

static void read_keys(void) {
    for (int k = 0; k < NT_KEY_COUNT; k++) {
        s_keys.pressed[k] = nt_input_key_is_pressed((nt_key_t)k);
    }
    s_keys.shift = nt_input_key_is_down(NT_KEY_LSHIFT) || nt_input_key_is_down(NT_KEY_RSHIFT);
    s_keys.ctrl = nt_input_key_is_down(NT_KEY_LCTRL) || nt_input_key_is_down(NT_KEY_RCTRL);
    s_keys.alt = nt_input_key_is_down(NT_KEY_LALT) || nt_input_key_is_down(NT_KEY_RALT);
}

static fi_frame_t *frame_sel(void) { return &s.files[s.file].frames[s.frame]; }
static bool frame_ok(const fi_frame_t *fr) { return fr->view.events != NULL; }
// #endregion

// #region monkey
/* --monkey SECONDS [SEED]: random clicks, double-clicks, drags, wheel and keys every frame, to shake out
 * crashes and slow frames; the run prints frame times and exits. It drops the side effects the UI asks for
 * (run_requests): no dialog, clipboard or closed file. */
static struct {
    bool on;
    double seconds;
    uint32_t seed;
    uint32_t frames;
    double total_ms, worst_ms, started;
    uint32_t press_frames; /* frames left to hold the button */
    uint32_t again;        /* frames until the second click of a double-click */
} s_monkey;

static uint32_t monkey_rand(void) {
    s_monkey.seed ^= s_monkey.seed << 13U;
    s_monkey.seed ^= s_monkey.seed >> 17U;
    s_monkey.seed ^= s_monkey.seed << 5U;
    return s_monkey.seed;
}

/* Replaces this frame's mouse and keys with random ones. */
static void monkey_input(float w, float h) {
    static const nt_key_t k_keys[] = {NT_KEY_ARROW_UP, NT_KEY_ARROW_DOWN, NT_KEY_ARROW_LEFT, NT_KEY_ARROW_RIGHT, NT_KEY_PAGE_UP,   NT_KEY_PAGE_DOWN, NT_KEY_HOME,
                                      NT_KEY_END,      NT_KEY_D,          NT_KEY_P,          NT_KEY_ENTER,       NT_KEY_BACKSPACE, NT_KEY_ESCAPE,    NT_KEY_C};
    nt_pointer_t *p = &g_nt_input.pointers[0];
    g_nt_input.primary = 0;
    p->active = true;
    p->wheel_dy = 0.0F;
    p->dx = 0.0F;
    p->dy = 0.0F;
    nt_button_state_t *b = &p->buttons[NT_BUTTON_LEFT];
    b->is_pressed = false;
    b->is_released = false;
    memset(&s_keys, 0, sizeof(s_keys));
    if (s_monkey.press_frames > 0) {
        s_monkey.press_frames--;
        if (s_monkey.press_frames == 0) {
            b->is_down = false;
            b->is_released = true;
            return;
        }
    }
    if (b->is_down) {
        p->dx = (float)((int)(monkey_rand() % 21U) - 10);
        p->x += p->dx; /* a drag */
        return;
    }
    if (s_monkey.again > 0) {
        s_monkey.again--;
        if (s_monkey.again == 0) {
            b->is_down = true;
            b->is_pressed = true;
            s_monkey.press_frames = 1;
            return;
        }
    }
    const uint32_t r = monkey_rand() % 100U;
    if (r < 35U) {
        p->dx = ((float)(monkey_rand() % 10000U) / 10000.0F * w) - p->x;
        p->dy = ((float)(monkey_rand() % 10000U) / 10000.0F * h) - p->y;
        p->x += p->dx;
        p->y += p->dy;
    } else if (r < 65U) {
        b->is_down = true;
        b->is_pressed = true;
        s_monkey.press_frames = 1U + (monkey_rand() % 4U);                            /* a click, or a short drag */
        s_monkey.again = (monkey_rand() % 4U) == 0 ? 2U + s_monkey.press_frames : 0U; /* sometimes a double-click */
    } else if (r < 75U) {
        p->wheel_dy = (float)((int)(monkey_rand() % 7U) - 3);
    } else {
        s_keys.pressed[k_keys[monkey_rand() % (sizeof(k_keys) / sizeof(k_keys[0]))]] = true;
        const uint32_t mod = monkey_rand() % 8U;
        s_keys.shift = mod == 1U;
        s_keys.ctrl = mod == 2U;
        s_keys.alt = mod == 3U;
    }
}

static void monkey_frame_time(void) {
    const double now = nt_time_now();
    static double last;
    if (last > 0.0) {
        const double ms = (now - last) * 1000.0;
        s_monkey.total_ms += ms;
        s_monkey.worst_ms = ms > s_monkey.worst_ms ? ms : s_monkey.worst_ms;
        if (ms > 50.0) {
            (void)fprintf(stderr, "monkey: slow frame %.1f ms on page %d, frame %u/%u, selected %u, anchor %u\n", ms, s.page, s.file, s.frame, s.selected, s.anchor);
        }
        s_monkey.frames++;
    }
    last = now;
    if (s_monkey.started == 0.0) {
        s_monkey.started = now;
    }
    if (now - s_monkey.started > s_monkey.seconds) {
        nt_app_quit();
    }
}
// #endregion

// #region helpers
static bool contains_ci(const char *text, size_t len, const char *needle) {
    const size_t n = strlen(needle);
    for (size_t i = 0; n > 0 && i + n <= len; i++) {
        size_t k = 0;
        while (k < n && tolower((unsigned char)text[i + k]) == tolower((unsigned char)needle[k])) {
            k++;
        }
        if (k == n) {
            return true;
        }
    }
    return n == 0;
}

static bool is_mergeable_draw(const nt_gfx_event_t *e) { return e->kind == NT_GFX_EVENT_OP && (e->operation == NT_GFX_OP_DRAW || e->operation == NT_GFX_OP_DRAW_INDEXED); }

/* A file name shortened in the middle, so both its start and its extension stay readable. */
static void short_name(const char *name, char *buf, size_t size) {
    const size_t n = strlen(name);
    if (n < 40) {
        (void)snprintf(buf, size, "%s", name);
    } else {
        (void)snprintf(buf, size, "%.18s...%s", name, name + n - 18);
    }
}

static const char *gl_enum_name(uint32_t v) {
    switch (v) {
    case 0x8D40U:
        return "GL_FRAMEBUFFER";
    case 0x8892U:
        return "GL_ARRAY_BUFFER";
    case 0x8893U:
        return "GL_ELEMENT_ARRAY_BUFFER";
    case 0x8A11U:
        return "GL_UNIFORM_BUFFER";
    case 0x88EBU:
        return "GL_PIXEL_PACK_BUFFER";
    case 0x88ECU:
        return "GL_PIXEL_UNPACK_BUFFER";
    case 0x8F36U:
        return "GL_COPY_READ_BUFFER";
    case 0x8F37U:
        return "GL_COPY_WRITE_BUFFER";
    case 0x0BE2U:
        return "GL_BLEND";
    case 0x0B71U:
        return "GL_DEPTH_TEST";
    case 0x0B44U:
        return "GL_CULL_FACE";
    case 0x0C11U:
        return "GL_SCISSOR_TEST";
    case 0x8037U:
        return "GL_POLYGON_OFFSET_FILL";
    case 0x0B90U:
        return "GL_STENCIL_TEST";
    case 0x0CF5U:
        return "GL_UNPACK_ALIGNMENT";
    case 0x0D05U:
        return "GL_PACK_ALIGNMENT";
    case 0x0DE1U:
        return "GL_TEXTURE_2D";
    case 0x8513U:
        return "GL_TEXTURE_CUBE_MAP";
    default:
        return NULL;
    }
}

/* GL enums by argument position, for the calls whose arguments are enums. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static const char *arg_enum(uint32_t call, uint32_t index, uint32_t v, char *buf, size_t size) {
    static const char *const k_modes[] = {"GL_POINTS", "GL_LINES", "GL_LINE_LOOP", "GL_LINE_STRIP", "GL_TRIANGLES", "GL_TRIANGLE_STRIP", "GL_TRIANGLE_FAN"};
    static const char *const k_funcs[] = {"GL_NEVER", "GL_LESS", "GL_EQUAL", "GL_LEQUAL", "GL_GREATER", "GL_NOTEQUAL", "GL_GEQUAL", "GL_ALWAYS"};
    static const char *const k_blend[] = {"GL_SRC_COLOR", "GL_ONE_MINUS_SRC_COLOR", "GL_SRC_ALPHA", "GL_ONE_MINUS_SRC_ALPHA",
                                          "GL_DST_ALPHA", "GL_ONE_MINUS_DST_ALPHA", "GL_DST_COLOR", "GL_ONE_MINUS_DST_COLOR"};
    switch (call) {
    case NT_GFX_GL_glEnable:
    case NT_GFX_GL_glDisable:
    case NT_GFX_GL_glBindBuffer:
    case NT_GFX_GL_glBindBufferRange:
    case NT_GFX_GL_glBindFramebuffer:
    case NT_GFX_GL_glBindTexture:
    case NT_GFX_GL_glPixelStorei:
        return index == 0 ? gl_enum_name(v) : NULL;
    case NT_GFX_GL_glActiveTexture:
        (void)snprintf(buf, size, "GL_TEXTURE%u", v - 0x84C0U);
        return index == 0 && v >= 0x84C0U && v < 0x84E0U ? buf : NULL;
    case NT_GFX_GL_glClear:
        (void)snprintf(buf, size, "%s%s%s", (v & 0x4000U) != 0 ? "COLOR|" : "", (v & 0x100U) != 0 ? "DEPTH|" : "", (v & 0x400U) != 0 ? "STENCIL|" : "");
        buf[strlen(buf) > 0 ? strlen(buf) - 1U : 0U] = '\0';
        return index == 0 && v != 0 ? buf : NULL;
    case NT_GFX_GL_glDrawArrays:
    case NT_GFX_GL_glDrawArraysInstanced:
    case NT_GFX_GL_glDrawElements:
    case NT_GFX_GL_glDrawElementsInstanced:
        if (index == 0 && v < 7) {
            return k_modes[v];
        }
        if (index == 2 && (call == NT_GFX_GL_glDrawElements || call == NT_GFX_GL_glDrawElementsInstanced)) {
            return v == 0x1401U ? "GL_UNSIGNED_BYTE" : (v == 0x1403U ? "GL_UNSIGNED_SHORT" : (v == 0x1405U ? "GL_UNSIGNED_INT" : NULL)); // NOLINT(readability-avoid-nested-conditional-operator)
        }
        return NULL;
    case NT_GFX_GL_glDepthFunc:
        return index == 0 && v >= 0x200U && v < 0x208U ? k_funcs[v - 0x200U] : NULL;
    case NT_GFX_GL_glCullFace:
        return index == 0 && v == 0x404U ? "GL_FRONT" : (index == 0 && v == 0x405U ? "GL_BACK" : NULL); // NOLINT(readability-avoid-nested-conditional-operator)
    case NT_GFX_GL_glBlendFuncSeparate:
        if (v <= 1U) {
            return v == 0 ? "GL_ZERO" : "GL_ONE";
        }
        return v >= 0x300U && v < 0x308U ? k_blend[v - 0x300U] : NULL;
    case NT_GFX_GL_glBlendEquationSeparate:
        return v == 0x8006U ? "GL_FUNC_ADD" : (v == 0x800AU ? "GL_FUNC_SUBTRACT" : (v == 0x800BU ? "GL_FUNC_REVERSE_SUBTRACT" : NULL)); // NOLINT(readability-avoid-nested-conditional-operator)
    default:
        return NULL;
    }
}

static const char *alias_prefix(uint32_t kind) {
    static const char *const k_short[] = {"obj", "shader", "prog", "pipe", "vi", "buf", "tex", "smp", "rt", "layout"};
    return kind < sizeof(k_short) / sizeof(k_short[0]) ? k_short[kind] : "obj";
}

/* pipe#3 for a handle: kinds are numbered in order of first appearance in the frame. */
static void alias_of(const fi_frame_t *fr, uint32_t kind, uint32_t object, char *buf, size_t size) {
    const uint32_t k = fi_alias(fr, kind, object);
    if (k != 0) {
        (void)snprintf(buf, size, "%s#%u", alias_prefix(kind), k);
    } else {
        (void)snprintf(buf, size, "%s:%u", alias_prefix(kind), object);
    }
}

static void append(char *buf, size_t size, size_t *at, const char *src, size_t n) {
    for (size_t i = 0; i < n && *at + 1 < size; i++) {
        buf[(*at)++] = src[i];
    }
    buf[*at] = '\0';
}

static bool token_is(const char *t, size_t n, const char *prefix) { return n >= strlen(prefix) && memcmp(t, prefix, strlen(prefix)) == 0; }

/* The GL call's integer arguments, enums by name: (GL_SCISSOR_TEST). */
static void append_args(const nt_gfx_event_t *e, char *buf, size_t size, size_t *at) {
    const uint32_t count = e->arg_count & 15U;
    for (uint32_t k = 0; k < count && k < 12U; k++) {
        char decoded[48];
        const char *name = arg_enum(e->detail, k, e->data.backend.args[k], decoded, sizeof(decoded));
        char number[16];
        (void)snprintf(number, sizeof(number), "%u", e->data.backend.args[k]);
        append(buf, size, at, k == 0 ? "(" : ",", 1);
        append(buf, size, at, name != NULL ? name : number, strlen(name != NULL ? name : number));
    }
    if (count > 0) {
        append(buf, size, at, ")", 1);
    }
}

/* The record's capture-text line without what the row shows elsewhere: the kind tag of operations and
 * GL calls, op=STATE, the result (its own column), the link to the row's parent and draw details (the
 * panel's). Handles read as short names (pipe#3) and GL calls as calls: glEnable(GL_SCISSOR_TEST). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void row_text(const fi_frame_t *fr, uint32_t id, char *buf, size_t size) {
    const nt_gfx_event_t *e = &fr->view.events[id];
    const char *line = NULL;
    const size_t len = fi_record_text(fr, id, &line);
    const bool call = e->kind == NT_GFX_EVENT_BACKEND || e->kind == NT_GFX_EVENT_SKIP;
    char parent_link[24];
    (void)snprintf(parent_link, sizeof(parent_link), "link=@%u", fr->parent[id]);
    size_t at = 0;
    buf[0] = '\0';
    char alias[32];
    for (size_t i = 0; i < len;) {
        size_t j = i;
        bool quoted = false;
        while (j < len && (quoted || line[j] != ' ')) {
            if (line[j] == '\\' && j + 1 < len) {
                j++;
            } else if (line[j] == '"') {
                quoted = !quoted;
            }
            j++;
        }
        const char *t = line + i;
        const size_t n = j - i;
        const bool tag = i == 0 && (e->kind == NT_GFX_EVENT_OP || call);
        i = j + 1;
        const bool draw_detail = e->kind == NT_GFX_EVENT_OP && (token_is(t, n, "first=") || token_is(t, n, "vertices=") || token_is(t, n, "instances=") || token_is(t, n, "joined="));
        if (tag || draw_detail || token_is(t, n, "result=") || token_is(t, n, "args=[") || (call && token_is(t, n, "op=")) || (n == strlen(parent_link) && memcmp(t, parent_link, n) == 0)) {
            continue;
        }
        if (at > 0 && buf[at - 1] != ' ') {
            append(buf, size, &at, " ", 1);
        }
        if (token_is(t, n, "op=") || token_is(t, n, "call=")) {
            const size_t skip = t[0] == 'o' ? 3 : 5;
            append(buf, size, &at, t + skip, n - skip);
            if (e->kind == NT_GFX_EVENT_BACKEND && t[0] == 'c') {
                append_args(e, buf, size, &at);
            }
        } else if (token_is(t, n, "obj=") || token_is(t, n, "vertex_input=")) {
            const bool obj = t[0] == 'o';
            alias_of(fr, obj ? e->object_kind : NT_GFX_OBJECT_VERTEX_INPUT, obj ? e->object : e->data.draw.vertex_input, alias, sizeof(alias));
            append(buf, size, &at, alias, strlen(alias));
        } else if (e->kind == NT_GFX_EVENT_SKIP && token_is(t, n, "key=")) {
            append(buf, size, &at, "cache ", 6); /* a skip records its cache key, not the GL arguments */
            append(buf, size, &at, t, n);
        } else {
            append(buf, size, &at, t, n);
        }
    }
    if (e->kind == NT_GFX_EVENT_OP && (e->operation == NT_GFX_OP_DRAW_INSTANCED || e->operation == NT_GFX_OP_DRAW_INDEXED_INSTANCED)) {
        (void)snprintf(buf + at, size - at, " x%u", e->data.draw.instances);
    } else if (e->kind == NT_GFX_EVENT_OP && e->operation == NT_GFX_OP_SCISSOR_ENABLE) {
        (void)snprintf(buf + at, size - at, " %s", e->data.state.integers[0] != 0 ? "on" : "off");
    }
}

/* The badge column: blank when a request did its work; what CACHE means for this operation; skips; failures. */
static const char *result_text(const nt_gfx_event_t *e, const nt_ui_label_style_t **style) {
    *style = &k_warn;
    if (e->kind == NT_GFX_EVENT_SKIP) {
        *style = &k_skip;
        return "GL skipped";
    }
    if (e->kind != NT_GFX_EVENT_OP || e->result == NT_GFX_RESULT_NONE || e->result == NT_GFX_RESULT_ACCEPTED) {
        return "";
    }
    if (e->result == NT_GFX_RESULT_CACHE) {
        if (is_mergeable_draw(e)) {
            return "merged";
        }
        return e->operation == NT_GFX_OP_CREATE ? "found" : "already set";
    }
    *style = &k_bad;
    const char *name = nt_gfx_result_name(e->result);
    return name != NULL ? name : "?";
}
// #endregion

// #region visible rows
static bool filtering(void) { return s.query.kind != FI_Q_NONE || s.filter[0] != '\0' || s.problems || s.draws_only; }

static bool passes(const fi_frame_t *fr, uint32_t i) {
    const nt_gfx_event_t *e = &fr->view.events[i];
    if (s.view == VIEW_GL && e->kind != NT_GFX_EVENT_BACKEND && e->kind != NT_GFX_EVENT_SKIP) {
        return false;
    }
    if (s.draws_only && !fi_is_draw(e) && !(e->kind == NT_GFX_EVENT_BACKEND && fi_is_draw_call(e->detail))) {
        return false;
    }
    if (s.problems && !fi_matches(fr, i, (fi_query_t){.kind = FI_Q_PROBLEM})) {
        return false;
    }
    if (s.query.kind != FI_Q_NONE && !fi_matches(fr, i, s.query)) {
        return false;
    }
    if (s.filter[0] != '\0') {
        const char *line = NULL;
        const size_t len = fi_record_text(fr, i, &line);
        return contains_ci(line, len, s.filter);
    }
    return true;
}

static void add_row(uint32_t id, uint16_t depth, uint32_t row) {
    s.vis[s.vis_count] = id;
    s.vis_depth[s.vis_count] = depth;
    s.vis_row[s.vis_count++] = row;
}

/* Logical tree with folded subtrees skipped, or a flat list: raw order, GL calls, or what the filters keep. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void rebuild_rows(void) {
    s.dirty = false;
    s.vis_count = 0;
    if (s.file_count == 0 || !frame_ok(frame_sel())) {
        return;
    }
    const fi_frame_t *fr = frame_sel();
    if (s.view == VIEW_LOGICAL && !filtering()) {
        uint32_t skip = UINT32_MAX;
        for (uint32_t r = 0; r < fr->row_count; r++) {
            if (skip != UINT32_MAX && fr->depth[r] > skip) {
                continue;
            }
            skip = s.collapsed[r] != 0 ? fr->depth[r] : UINT32_MAX;
            add_row(fr->row[r], fr->depth[r], r);
        }
    } else if (s.view == VIEW_LOGICAL) {
        for (uint32_t r = 0; r < fr->row_count; r++) {
            if (fr->row[r] < FI_NODE_BASE && passes(fr, fr->row[r])) {
                add_row(fr->row[r], 0, FI_NONE);
            }
        }
    } else {
        for (uint32_t i = 0; i < fr->view.count; i++) {
            if (passes(fr, i)) {
                add_row(i, 0, FI_NONE);
            }
        }
    }
}

static uint32_t vis_index_of(uint32_t id) {
    for (uint32_t k = 0; k < s.vis_count; k++) {
        if (s.vis[k] == id) {
            return k;
        }
    }
    return FI_NONE;
}

static void clear_filters(void);

/* A group filter follows its group into the new frame: group numbers are per frame. */
static void select_frame(uint32_t file, uint32_t frame) {
    const fi_frame_t *was = s.query.kind == FI_Q_GROUP ? frame_sel() : NULL;
    s.file = file;
    s.frame = frame;
    s.frame_epoch++;
    const fi_frame_t *fr = frame_sel();
    if (was != NULL) {
        s.query.a = frame_ok(fr) ? fi_group_in(fr, was, s.query.a) : FI_NONE;
        if (s.query.a == FI_NONE) {
            clear_filters();
        }
    }
    const uint32_t rows = fr->row_count + fr->view.count + 1U;
    free(s.collapsed);
    free(s.vis);
    free(s.vis_row);
    free(s.vis_depth);
    s.collapsed = (uint8_t *)calloc(rows, 1);
    s.vis = (uint32_t *)calloc(rows, sizeof(uint32_t));
    s.vis_row = (uint32_t *)calloc(rows, sizeof(uint32_t));
    s.vis_depth = (uint16_t *)calloc(rows, sizeof(uint16_t));
    NT_ASSERT(s.collapsed != NULL && s.vis != NULL && s.vis_row != NULL && s.vis_depth != NULL);
    /* The snapshot and finished links are long and rarely the question: they start folded. */
    for (uint32_t r = 0; r < fr->row_count; r++) {
        s.collapsed[r] = fr->row[r] == FI_NODE_BASE + FI_NODE_SNAPSHOT || fr->row[r] == FI_NODE_BASE + FI_NODE_PROGRAM_LINKS;
    }
    s.selected = FI_NONE;
    s.anchor = FI_NONE;
    s.vis_count = 0;
    s.picked_query = (fi_query_t){0};
    s.flash = FI_NONE;
    s.dirty = true;
    s.scroll_top = true;
}

/* Selects a row, unfolding its ancestors and lighting it; a jump ends a Shift range. A row the filters or the
 * view hide clears them, so the jump lands on a row that shows. */
static void reveal(uint32_t id) {
    const fi_frame_t *fr = frame_sel();
    if (id < fr->view.count && !passes(fr, id)) {
        clear_filters();
        s.view = VIEW_LOGICAL;
    }
    for (uint32_t p = fi_parent(fr, id); p != FI_NONE; p = fi_parent(fr, p)) {
        const uint32_t r = fi_row_of(fr, p);
        if (r != FI_NONE) {
            s.collapsed[r] = 0;
        }
    }
    s.anchor = FI_NONE;
    s.selected = id;
    s.dirty = true;
    s.follow = true;
    s.flash = id;
    s.flash_until = nt_time_now() + 0.8;
}

static place_t here(void) { return (place_t){.page = s.page, .file = s.file, .frame = s.frame, .selected = s.selected}; }

/* Pushes the current place for Back, unless it is already the last one. */
static void remember(void) {
    const place_t now = here();
    const place_t *top = s.history_count > 0 ? &s.history[s.history_count - 1U] : NULL;
    if (top != NULL && memcmp(top, &now, sizeof(now)) == 0) {
        return;
    }
    if (s.history_count == HISTORY) {
        memmove(s.history, s.history + 1, (HISTORY - 1U) * sizeof(place_t));
        s.history_count--;
    }
    s.history[s.history_count++] = now;
    s.future_count = 0;
}

static void go_to(place_t p) {
    if (p.file != s.file || p.frame != s.frame) {
        select_frame(p.file, p.frame);
    }
    s.page = p.page;
    if (p.selected != FI_NONE) {
        reveal(p.selected);
    } else {
        s.selected = FI_NONE;
    }
}

/* A jump (a link, Enter, Backspace, a second click elsewhere) opens the record in Commands; Back returns. */
static void select_record(uint32_t id) {
    if (s.page != PAGE_COMMANDS || s.selected != id) {
        remember();
    }
    reveal(id);
    s.page = PAGE_COMMANDS;
}

/* History holds file and frame numbers: closing a file drops it (close_file). */
static void go_back(void) {
    if (s.history_count > 0) {
        s.future[s.future_count++] = here();
        go_to(s.history[--s.history_count]);
    }
}

static void go_forward(void) {
    if (s.future_count > 0) {
        s.history[s.history_count++] = here();
        go_to(s.future[--s.future_count]);
    }
}

/* Fold every operation and group, leaving the frame's phases open: the list then reads as an outline. */
static void fold_all(bool fold) {
    const fi_frame_t *fr = frame_sel();
    for (uint32_t r = 0; r < fr->row_count; r++) {
        const uint32_t id = fr->row[r];
        const bool phase = id >= FI_NODE_BASE && id != FI_NODE_BASE + FI_NODE_SNAPSHOT && id != FI_NODE_BASE + FI_NODE_PROGRAM_LINKS;
        s.collapsed[r] = phase ? 0U : (uint8_t)(fold || id == FI_NODE_BASE + FI_NODE_SNAPSHOT || id == FI_NODE_BASE + FI_NODE_PROGRAM_LINKS);
    }
    s.dirty = true;
}

static void set_query(fi_query_t q, const char *label) {
    remember();
    s.query = q;
    s.page = PAGE_COMMANDS;
    s.view = VIEW_LOGICAL; /* the GL-calls view would hide the requests a number counts */
    (void)snprintf(s.query_label, sizeof(s.query_label), "%s", label);
    s.filter[0] = '\0'; /* a click shows exactly the records it counted */
    s.problems = false;
    s.draws_only = false;
    s.dirty = true;
}

static void clear_filters(void) {
    s.query = (fi_query_t){0};
    s.filter[0] = '\0';
    s.problems = false;
    s.draws_only = false;
    s.dirty = true;
}
// #endregion

// #region files
static void add_file(const char *path) {
    if (s.file_count == MAX_FILES) {
        nt_log_error("frame_inspector: %d files are open; close one first", MAX_FILES);
        return;
    }
    fi_file_t *file = &s.files[s.file_count];
    if (!fi_file_open(file, path)) {
        nt_log_error("frame_inspector: cannot read %s", path);
    } else if (file->frame_count == 0) {
        nt_log_error("frame_inspector: %s holds no capture", path);
        fi_file_close(file);
    } else if (++s.file_count == 1) {
        select_frame(0, 0);
        s.page = PAGE_COMMANDS;
    }
}

/* Runs between frames: the dialog blocks the loop until it closes. */
static void open_files(void) {
    static char paths[16384];
    const uint32_t n = nt_file_dialog_open("Open capture files", "*.txt", paths, sizeof(paths));
    const char *p = paths;
    for (uint32_t k = 0; k < n; k++) {
        add_file(p);
        p += strlen(p) + 1U;
    }
}

/* Closing another file keeps the selection; closing this one opens the first frame left. History names
 * files by number, so it goes. */
static void close_file(uint32_t f) {
    const bool current = s.file == f;
    fi_file_close(&s.files[f]);
    memmove(&s.files[f], &s.files[f + 1U], (s.file_count - f - 1U) * sizeof(fi_file_t));
    s.file_count--;
    s.history_count = 0;
    s.future_count = 0;
    if (!current) {
        s.file -= s.file > f ? 1U : 0U;
        return;
    }
    clear_filters();
    if (s.file_count > 0) {
        select_frame(0, 0);
    }
}
// #endregion

// #region widgets
static uint32_t next_id(void) { return nt_ui_fmix_id(0x51A7E001U, ++s.cell_seq); }

static void text(const char *t, const nt_ui_label_style_t *style) { nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), t, style); }

static void textf(const nt_ui_label_style_t *style, const char *fmt, ...) NT_PRINTF_ATTR(2, 3);
static void textf(const nt_ui_label_style_t *style, const char *fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    text(buf, style);
}

static bool button(const char *label, const nt_ui_label_style_t *style, bool active) {
    const uint32_t id = next_id();
    const nt_ui_events_t ev = nt_ui_events(s_ctx, id, NULL);
    CLAY({.id = (Clay_ElementId){.id = id}, .layout = {.padding = {6, 6, 2, 2}}, .backgroundColor = active ? k_select : (ev.hovered ? k_hover : k_panel), .cornerRadius = CLAY_CORNER_RADIUS(3)}) {
        text(label, style);
    }
    return ev.clicked;
}

/* Whether a click repeats the last one on the same target within 0.4 s. Kept here rather than in nt_ui's
 * double-click: its per-id cells never free, and these widgets take ids by position. */
static bool second_click(uint64_t target) {
    const double now = nt_time_now();
    const bool again = s.click_target == target && now - s.click_time < 0.4;
    s.click_target = again ? 0U : target;
    s.click_time = now;
    return again;
}

static bool same_query(fi_query_t a, fi_query_t b) { return a.kind == b.kind && a.a == b.a && a.b == b.b; }

/* Outside Commands a click picks and a second click opens: no page changes under a single click. */
static void open_query(bool clicked, fi_query_t q, const char *label) {
    if (clicked && second_click(((uint64_t)3U << 62U) | ((uint64_t)q.kind << 40U) | ((uint64_t)q.b << 32U) | q.a)) {
        set_query(q, label);
    } else if (clicked) {
        s.picked_query = q;
    }
}

static void open_record(bool clicked, uint32_t record) {
    if (clicked && second_click(((uint64_t)2U << 62U) | record)) {
        select_record(record);
    } else if (clicked) {
        s.selected = record;
    }
}

static bool marked(fi_query_t q) { return same_query(s.query, q) || same_query(s.picked_query, q); }

/* A number that filters the tree to the records it counts. */
static void number(fi_query_t q, uint64_t value, const char *label) {
    char buf[32];
    (void)snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
    if (fi_cell(frame_sel(), q).n == 0) {
        text(buf, &k_dim);
    } else {
        open_query(button(buf, &k_link, marked(q)), q, label);
    }
}

/* A sentence that filters the tree to the records it describes. */
static void insight(fi_query_t q, const nt_ui_label_style_t *style, const char *fmt, ...) NT_PRINTF_ATTR(3, 4);
static void insight(fi_query_t q, const nt_ui_label_style_t *style, const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    open_query(button(buf, style, marked(q)), q, buf);
}

#define ROW_(gap) CLAY({.layout = {.layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = (gap), .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}})
#define COLUMN_(gap) CLAY({.layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = (gap)}})
#define CELL_(w) CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(w), CLAY_SIZING_FIT(0)}}})
// #endregion

// #region frame tracks
static uint64_t track_value(const fi_frame_t *fr, track_t t) {
    if (!frame_ok(fr)) {
        return 0;
    }
    switch (t) {
    case TRACK_DRAW_CALLS:
        return fr->summary.draw_calls.n;
    case TRACK_GL_CALLS:
        return fr->summary.gl_any.n;
    case TRACK_UPLOAD:
        return (fr->summary.upload[2].sum + 1023U) / 1024U;
    case TRACK_RECORDS:
        return fr->view.count;
    case TRACK_COUNT:
        break;
    }
    return 0;
}

static void declare_file_header(uint32_t f) {
    const fi_file_t *file = &s.files[f];
    char name[64];
    short_name(file->name, name, sizeof(name));
    ROW_(8) {
        if (button("x", &k_text, false)) {
            s.close_file = f;
        }
        text(name, &k_head);
        const fi_frame_t *first = &file->frames[0];
        const fi_frame_t *last = &file->frames[file->frame_count - 1U];
        textf(&k_dim, "%s  %u frame%s  (sequence %llu..%llu)", frame_ok(first) && first->view.platform == NT_GFX_CAPTURE_WEB ? "web" : "native", file->frame_count, file->frame_count == 1 ? "" : "s",
              (unsigned long long)first->view.counters.frame_sequence, (unsigned long long)last->view.counters.frame_sequence);
    }
}

/* The frame axis shared by every track of every file: frame k sits in the same column in all files. */
static uint32_t axis_frames(void) {
    uint32_t most = 1;
    for (uint32_t f = 0; f < s.file_count; f++) {
        most = s.files[f].frame_count > most ? s.files[f].frame_count : most;
    }
    return most;
}

/* The axis shows from one frame to all of them, from any point; `fit` keeps all of them as files come and go. */
static void axis_clamp(void) {
    const double most = (double)axis_frames();
    s.axis_count = s.axis_fit || s.axis_count > most ? most : s.axis_count;
    s.axis_count = s.axis_count < 1.0 ? 1.0 : s.axis_count;
    s.axis_first = s.axis_fit ? 0.0 : s.axis_first;
}

/* The start the axis rests at: from the first frame to the one that shows the last frame at the right edge. */
static double axis_hold(double first) {
    const double last = (double)axis_frames() - s.axis_count;
    first = first > last ? last : first;
    return first < 0.0 ? 0.0 : first;
}

/* Past either end a pan meets resistance and shows the empty space there; released, the axis springs back. */
static double axis_overscroll(double first) {
    const double held = axis_hold(first);
    double over = (first - held) * 0.35;
    over = over > s.axis_count * 0.25 ? s.axis_count * 0.25 : over;
    over = over < -s.axis_count * 0.25 ? -s.axis_count * 0.25 : over;
    return held + over;
}

static void axis_settle(void) {
    if (s.axis_dragged) {
        return;
    }
    const double held = axis_hold(s.axis_first);
    const double k = (double)g_nt_app.dt * 14.0;
    s.axis_first += (held - s.axis_first) * (k < 1.0 ? k : 1.0);
    const double left = held - s.axis_first;
    s.axis_first = left < 0.002 && left > -0.002 ? held : s.axis_first;
}

static uint32_t track_id(uint32_t f, track_t t) { return nt_ui_fmix_id(0x7A3C0001U, (f * TRACK_COUNT) + (uint32_t)t + 1U); }

/* A frame column's width share: under 1 in sum, or Clay would squeeze a row that overflows by a rounding. */
#define SHARE_ 0.999

/* Frames per column: a column is at least 2 px, and every track of every file shares a column budget, since
 * Clay has a fixed element count. */
static uint32_t axis_step(float width) {
    const double frame_w = (double)width * SHARE_ / s.axis_count;
    const double budget = (double)width / (3000.0 / (TRACK_COUNT * s.file_count));
    const double min_w = budget > 2.0 ? budget : 2.0;
    return frame_w >= min_w ? 1U : (uint32_t)(min_w / frame_w) + 1U;
}

/* The frame of a column whose bar it draws: the largest of the column's frames, so a spike never disappears. */
static uint32_t column_pick(const fi_file_t *file, track_t t, uint32_t first, uint32_t step) {
    uint32_t pick = first;
    for (uint32_t j = first; j < first + step && j < file->frame_count; j++) {
        pick = track_value(&file->frames[j], t) > track_value(&file->frames[pick], t) ? j : pick;
    }
    return pick;
}

/* A click picks a frame; a second click on it opens it in Commands. */
static void pick_frame(uint32_t f, uint32_t k) {
    if (second_click(((uint64_t)1U << 62U) | ((uint64_t)f << 32U) | k)) {
        remember();
        s.page = PAGE_COMMANDS;
    } else if (f != s.file || k != s.frame) {
        remember();
        select_frame(f, k);
    }
}

/* Input for the axis before any track draws, so every track shows the same axis: the wheel over the tracks
 * zooms around the cursor, Shift+wheel or a sideways wheel pans, a drag on a track pans, a click picks. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void tracks_input(void) {
    const nt_ui_bbox_t box = nt_ui_get_bbox(s_ctx, track_id(0, TRACK_DRAW_CALLS));
    if (!box.found || box.width <= 0.0F) {
        return;
    }
    const nt_pointer_t *pointer = &g_nt_input.pointers[g_nt_input.primary];
    double at = ((double)pointer->x - (double)box.x) / ((double)box.width * SHARE_);
    at = at < 0.0 ? 0.0 : (at > 1.0 ? 1.0 : at); // NOLINT(readability-avoid-nested-conditional-operator)
    if (s.track_wheel != 0.0F) {
        const double cursor = s.axis_first + (at * s.axis_count);
        s.axis_fit = false;
        s.axis_count *= pow(1.25, (double)s.track_wheel); /* a notch is a fifth; a touchpad's fraction is less */
        axis_clamp();
        s.axis_first = cursor - (at * s.axis_count);
    }
    if (s.track_pan != 0.0F) {
        const double tenth = s.axis_count / 10.0;
        s.axis_fit = false;
        s.axis_first = axis_overscroll(s.axis_first + ((double)s.track_pan * (tenth > 1.0 ? tenth : 1.0)));
    }
    const double frame_w = (double)box.width * SHARE_ / s.axis_count;
    const uint32_t step = axis_step(box.width);
    s.hover_file = FI_NONE;
    for (uint32_t f = 0; f < s.file_count; f++) {
        for (int t = 0; t < TRACK_COUNT; t++) {
            const nt_ui_events_t ev = nt_ui_events(s_ctx, track_id(f, (track_t)t), NULL);
            if (ev.pressed_now) {
                s.drag_first = s.axis_first;
            }
            if (ev.pressed) {
                s.axis_fit = false;
                s.axis_first = axis_overscroll(s.drag_first - ((double)ev.drag_dx / frame_w));
                s.axis_dragged = true;
            }
            const double pointed = s.axis_first + (((double)ev.pos[0] - (double)box.x) / frame_w);
            if (!(ev.hovered || ev.clicked) || pointed < 0.0 || (uint32_t)pointed >= s.files[f].frame_count) {
                continue;
            }
            const uint32_t k = column_pick(&s.files[f], (track_t)t, ((uint32_t)pointed / step) * step, step);
            s.hover_file = ev.hovered ? f : s.hover_file;
            s.hover_frame = ev.hovered ? k : s.hover_frame;
            if (ev.clicked && ev.drag_dx < 3.0F && ev.drag_dx > -3.0F) {
                pick_frame(f, k);
            }
        }
    }
}

/* The scrollbar under the tracks: its thumb is the part of the frames that shows. Drag the thumb, or press
 * beside it to centre the axis there. Returns the thumb as shares of the strip. */
static void strip_input(float *thumb_x, float *thumb_w) {
    const uint32_t id = nt_ui_id("axis_strip");
    const nt_ui_bbox_t box = nt_ui_get_bbox(s_ctx, id);
    const nt_ui_events_t ev = nt_ui_events(s_ctx, id, NULL);
    const double most = (double)axis_frames();
    const double first = axis_hold(s.axis_first);
    const float width = box.found && box.width > 0.0F ? box.width : 1.0F;
    *thumb_w = (float)(s.axis_count / most);
    *thumb_w = *thumb_w * width > 8.0F || !box.found ? *thumb_w : 8.0F / width; /* at least 8 px, once the width is known */
    *thumb_w = *thumb_w < 1.0F ? *thumb_w : 1.0F;
    *thumb_x = (float)(first / most);
    *thumb_x = *thumb_x + *thumb_w > 1.0F ? 1.0F - *thumb_w : *thumb_x;
    *thumb_x = *thumb_x > 0.0F ? *thumb_x : 0.0F;
    if (!box.found) {
        return;
    }
    const double at = ((double)ev.pos[0] - (double)box.x) / (double)width * most;
    if (ev.pressed_now) {
        const float x = (ev.pos[0] - box.x) / width;
        s.strip_grab = x >= *thumb_x && x <= *thumb_x + *thumb_w ? at - first : s.axis_count / 2.0;
    }
    if (ev.pressed) {
        s.axis_fit = false;
        s.axis_first = axis_hold(at - s.strip_grab);
        s.axis_dragged = true;
    }
}

/* The label cells in front of every track and of the strip, so they all start at one x. */
static void track_labels(const char *name, const char *value, const nt_ui_label_style_t *value_style, const char *max) {
    CELL_(LABEL_NAME_W) { text(name, &k_dim); }
    CELL_(LABEL_VALUE_W) { text(value, value_style); }
    CELL_(LABEL_MAX_W) { text(max, &k_dim); }
}

/* One track, profiler style: the visible frames side by side over the width, each a filled column whose
 * height is the metric against its maximum over all files. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void declare_track(uint32_t f, track_t t, uint64_t max, uint32_t step) {
    static const char *const k_names[TRACK_COUNT] = {"GL draw calls", "GL calls", "upload KiB", "records"};
    NT_ASSERT(t < TRACK_COUNT);
    const fi_file_t *file = &s.files[f];
    const uint32_t hovered = s.hover_file == f ? s.hover_frame : FI_NONE;
    uint32_t at = f == s.file ? s.frame : FI_NONE;
    at = hovered != FI_NONE ? hovered : at;
    char value[24] = "";
    char most[32];
    if (at != FI_NONE) {
        (void)snprintf(value, sizeof(value), "%llu", (unsigned long long)track_value(&file->frames[at], t));
    }
    (void)snprintf(most, sizeof(most), "max %llu", (unsigned long long)max);
    const nt_ui_bbox_t box = nt_ui_get_bbox(s_ctx, track_id(f, t));
    const float width = box.found ? box.width : s.fb_w - TRACK_LABEL_W;
    const double from_frame = s.axis_first;
    const double to_frame = s.axis_first + s.axis_count;
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childGap = LABEL_GAP, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) {
        track_labels(k_names[t], value, hovered != FI_NONE ? &k_head : &k_text, most);
        /* No clip: Clay counts every clip as a scroll container and has few. Columns hold `step` frames from a
         * multiple of step, the first and last cut to the part that shows. Their widths are shares of the track,
         * not pixels from the last layout: a pixel width from a wider frame (the first one, a shrinking window)
         * would hold the track open. */
        CLAY({.id = (Clay_ElementId){.id = track_id(f, t)},
              .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(TRACK_H)}, .childAlignment = {.y = CLAY_ALIGN_Y_BOTTOM}},
              .backgroundColor = k_bg}) {
            if (from_frame < 0.0) { /* overscrolled past the first frame: the empty space before it */
                const double gap = -from_frame / s.axis_count * SHARE_;
                CLAY({.layout = {.sizing = {CLAY_SIZING_PERCENT((float)(gap < SHARE_ ? gap : SHARE_)), CLAY_SIZING_FIXED(TRACK_H)}}}) {}
            }
            const uint32_t start = from_frame > 0.0 ? (uint32_t)from_frame : 0U;
            for (uint32_t k = (start / step) * step; k < file->frame_count && (double)k < to_frame; k += step) {
                const double lo = (double)k > from_frame ? (double)k : from_frame;
                const double hi = (double)(k + step) < to_frame ? (double)(k + step) : to_frame;
                const float share = (float)((hi - lo) / s.axis_count * SHARE_);
                const float column = share * width;
                if (share <= 0.0F) {
                    continue;
                }
                const uint32_t pick = column_pick(file, t, k, step);
                bool bad = false;
                bool warn = false;
                bool sel = false;
                bool hot = false;
                for (uint32_t j = k; j < k + step && j < file->frame_count; j++) {
                    const fi_frame_t *fr = &file->frames[j];
                    bad |= !frame_ok(fr) || fr->error != NULL;
                    warn |= frame_ok(fr) && (fr->merge_mismatch != FI_NONE || fr->view.overflow);
                    sel |= f == s.file && j == s.frame;
                    hot |= j == hovered;
                }
                Clay_Color color = hot ? k_hover_bar : k_bar;
                color = warn ? k_bar_warn : color;
                color = bad ? k_bar_bad : color;
                color = sel ? k_bar_sel : color;
                const float gap = column >= 6.0F ? 1.0F : 0.0F;
                const bool label = column >= 34.0F; /* wide columns print their value over the bar */
                const float room = label ? TRACK_H - 16.0F : TRACK_H;
                const float h = bad ? room : 2.0F + ((room - 2.0F) * (float)track_value(&file->frames[pick], t) / (float)(max > 0 ? max : 1));
                CLAY({.layout = {.sizing = {CLAY_SIZING_PERCENT(share), CLAY_SIZING_FIXED(TRACK_H)},
                                 .padding = {.right = (uint16_t)gap},
                                 .layoutDirection = CLAY_TOP_TO_BOTTOM,
                                 .childAlignment = {.y = CLAY_ALIGN_Y_BOTTOM}},
                      .backgroundColor = hot ? k_hover : k_bg}) {
                    if (label) {
                        textf(sel ? &k_head : &k_dim, "%llu", (unsigned long long)track_value(&file->frames[pick], t));
                    }
                    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(h)}}, .backgroundColor = color}) {}
                }
            }
        }
    }
}

static void declare_strip(float thumb_x, float thumb_w) {
    const double first = axis_hold(s.axis_first);
    char range[32];
    (void)snprintf(range, sizeof(range), "%u..%u of %u", (uint32_t)first + 1U, (uint32_t)(first + s.axis_count + 0.5), axis_frames());
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childGap = LABEL_GAP, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) {
        track_labels(range, "", &k_dim, "");
        CLAY({.id = (Clay_ElementId){.id = nt_ui_id("axis_strip")},
              .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(12)}},
              .backgroundColor = k_bg,
              .cornerRadius = CLAY_CORNER_RADIUS(3)}) {
            CLAY({.layout = {.sizing = {CLAY_SIZING_PERCENT(thumb_x), CLAY_SIZING_FIXED(12)}}}) {}
            CLAY({.layout = {.sizing = {CLAY_SIZING_PERCENT(thumb_w), CLAY_SIZING_FIXED(12)}}, .backgroundColor = s.axis_dragged ? k_hover_bar : k_bar, .cornerRadius = CLAY_CORNER_RADIUS(3)}) {}
        }
    }
}

/* Every frame as a row: the numbers to compare at a glance when there are not too many frames. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void declare_frame_table(void) {
    static const float k_w[] = {60, 70, 130, 70, 80, 90, 110, 80};
    static const char *const k_heads[] = {"pos", "frame", "GL draw calls", "requests", "merged", "GL calls", "upload KiB", "problems"};
    CLAY({.layout = {.padding = {0, 0, 12, 2}}}) { text("All frames (click: pick, again: open in Commands)", &k_head); }
    ROW_(4) {
        for (uint32_t c = 0; c < sizeof(k_w) / sizeof(k_w[0]); c++) {
            CELL_(k_w[c]) { text(k_heads[c], &k_dim); }
        }
    }
    char name[64];
    for (uint32_t f = 0; f < s.file_count; f++) {
        short_name(s.files[f].name, name, sizeof(name));
        text(name, &k_head);
        for (uint32_t k = 0; k < s.files[f].frame_count; k++) {
            const fi_frame_t *fr = &s.files[f].frames[k];
            const uint32_t id = next_id();
            const nt_ui_events_t ev = nt_ui_events(s_ctx, id, NULL);
            if (ev.clicked) {
                pick_frame(f, k);
            }
            const bool sel = f == s.file && k == s.frame;
            const nt_ui_label_style_t *style = sel ? &k_head : &k_text;
            CLAY({.id = (Clay_ElementId){.id = id}, .layout = {.childGap = 4}, .backgroundColor = sel ? k_select : (ev.hovered ? k_hover : k_panel)}) {
                CELL_(k_w[0]) { textf(style, "%u", k + 1U); }
                CELL_(k_w[1]) { textf(style, "%llu", (unsigned long long)fr->view.counters.frame_sequence); }
                if (!frame_ok(fr)) {
                    textf(&k_bad, "rejected: %s", fr->error != NULL ? fr->error : "?");
                } else {
                    CELL_(k_w[2]) { textf(style, "%llu", (unsigned long long)fr->summary.draw_calls.n); }
                    CELL_(k_w[3]) { textf(style, "%llu", (unsigned long long)fr->summary.vertices.n); }
                    CELL_(k_w[4]) { textf(style, "%llu", (unsigned long long)fr->summary.merged.n); }
                    CELL_(k_w[5]) { textf(style, "%llu", (unsigned long long)fr->summary.gl_any.n); }
                    CELL_(k_w[6]) { textf(style, "%.1f", (double)fr->summary.upload[2].sum / 1024.0); }
                    CELL_(k_w[7]) { textf(fr->summary.problems.n > 0 ? &k_bad : style, "%llu", (unsigned long long)fr->summary.problems.n); }
                }
            }
        }
    }
}

/* Frames: the tracks and their scrollbar stay put above the frame table, which scrolls: a scroll around the
 * tracks would take a drag that drifts vertically away from the axis. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void declare_frames(void) {
    uint64_t max[TRACK_COUNT] = {0};
    for (uint32_t f = 0; f < s.file_count; f++) {
        for (uint32_t k = 0; k < s.files[f].frame_count; k++) {
            for (int t = 0; t < TRACK_COUNT; t++) {
                const uint64_t v = track_value(&s.files[f].frames[k], (track_t)t);
                max[t] = v > max[t] ? v : max[t];
            }
        }
    }
    axis_clamp();
    s.axis_dragged = false;
    float thumb_x = 0.0F;
    float thumb_w = 1.0F;
    tracks_input();
    strip_input(&thumb_x, &thumb_w);
    const nt_ui_bbox_t box = nt_ui_get_bbox(s_ctx, track_id(0, TRACK_DRAW_CALLS));
    const uint32_t step = axis_step(box.found && box.width > 0.0F ? box.width : s.fb_w - TRACK_LABEL_W);
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .padding = CLAY_PADDING_ALL(8), .childGap = 4}, .backgroundColor = k_panel}) {
        ROW_(10) {
            if (s.file_count > 0) {
                text("files align by position in the file", &k_dim);
                if (button("fit", &k_text, s.axis_fit)) {
                    s.axis_fit = true;
                }
                text("blue: current   red: rejected   orange: overflow or merge rule stale", &k_dim);
            } else {
                text("Open a capture file (ntcapture text, one or more frames).", &k_dim);
            }
        }
        CLAY({.id = CLAY_ID("tracks"), .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 4}}) {
            for (uint32_t f = 0; f < s.file_count; f++) {
                declare_file_header(f);
                for (int t = 0; t < TRACK_COUNT; t++) {
                    declare_track(f, (track_t)t, max[t], step);
                }
            }
            if (s.file_count > 0) {
                declare_strip(thumb_x, thumb_w);
            }
        }
        axis_settle();
        uint32_t frames = 0;
        for (uint32_t f = 0; f < s.file_count; f++) {
            frames += s.files[f].frame_count;
        }
        if (frames <= 200U) {
            const nt_ui_scroll_style_t scroll = nt_ui_scroll_style_defaults();
            nt_ui_scroll_begin(s_ctx, NULL, nt_ui_id("frames"), &scroll,
                               &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 4}});
            declare_frame_table();
            nt_ui_scroll_end(s_ctx);
        }
    }
}
// #endregion

// #region tree
/* Spector's colors: state changes green, redundant work orange (CACHE, backend SKIP), failures red;
 * draws and clears get a blue background (row_background). */
static const nt_ui_label_style_t *row_style(const fi_frame_t *fr, uint32_t id) {
    if (id >= FI_NODE_BASE) {
        return &k_head;
    }
    const nt_gfx_event_t *e = &fr->view.events[id];
    if (fi_matches(fr, id, (fi_query_t){.kind = FI_Q_PROBLEM})) {
        return &k_bad;
    }
    switch (e->kind) {
    case NT_GFX_EVENT_OP:
        if (e->result == NT_GFX_RESULT_CACHE) {
            return &k_warn;
        }
        return fi_is_draw(e) ? &k_head : &k_state;
    case NT_GFX_EVENT_SKIP:
        return &k_skip;
    case NT_GFX_EVENT_BACKEND:
        return &k_backend;
    case NT_GFX_EVENT_NOTE:
    case NT_GFX_EVENT_NOTE_FIELD:
        return &k_note;
    case NT_GFX_EVENT_MARKER_PUSH:
    case NT_GFX_EVENT_MARKER_POP:
        return &k_marker;
    default:
        return &k_dim;
    }
}

static bool draws_pixels(const fi_frame_t *fr, uint32_t id) {
    const nt_gfx_event_t *e = &fr->view.events[id];
    return id < fr->view.count && (fi_is_draw(e) || (e->kind == NT_GFX_EVENT_OP && e->operation == NT_GFX_OP_CLEAR));
}

/* The row text after its indentation; a folded group tells what it holds. */
static void row_label(const fi_frame_t *fr, uint32_t k, char *buf, size_t size) {
    const uint32_t id = s.vis[k];
    const int indent = 2 * (int)s.vis_depth[k];
    if (id >= FI_NODE_BASE) {
        if (id == FI_NODE_BASE + FI_NODE_FRAME) {
            (void)snprintf(buf, size, "%*sframe %llu  (%s, %u records)", indent, "", (unsigned long long)fr->view.counters.frame_sequence, fr->view.platform == NT_GFX_CAPTURE_WEB ? "web" : "native",
                           fr->view.count);
        } else {
            (void)snprintf(buf, size, "%*s%s", indent, "", fi_node_name((fi_node_t)(id - FI_NODE_BASE)));
        }
        return;
    }
    char body[512];
    row_text(fr, id, body, sizeof(body));
    const int n = snprintf(buf, size, "%*s%s", indent, "", body);
    const nt_gfx_event_t *e = &fr->view.events[id];
    const bool group = e->kind == NT_GFX_EVENT_NOTE || e->kind == NT_GFX_EVENT_MARKER_PUSH;
    const fi_tally_t t = fi_tally(fr, id);
    if (group && t.draws > 0 && n > 0 && (size_t)n < size) {
        (void)snprintf(buf + n, size - (size_t)n, "   %u draw request%s -> %u GL draw call%s, %u merged", t.draws, t.draws == 1 ? "" : "s", t.gl_draws, t.gl_draws == 1 ? "" : "s", t.merged);
    } else if (fr->brk[id] != FI_BREAK_NONE && n > 0 && (size_t)n < size) {
        (void)snprintf(buf + n, size - (size_t)n, "   new GL draw: %s", fi_break_name((fi_break_t)fr->brk[id]));
    }
}

/* The whole frame in one strip, as RenderDoc's timeline bar: a column per slice of records, tall where a GL draw
 * was issued, short where draws merged, red at a problem; group bands alternate; the selection is amber. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void declare_timeline(const fi_frame_t *fr) {
    enum { ISSUED = 1, MERGED = 2, PROBLEM = 4, MAX_COLUMNS = 1024 };
    static uint8_t flags[MAX_COLUMNS];
    static uint32_t targets[MAX_COLUMNS];
    static uint32_t cached_epoch;
    static uint32_t cached_columns;
    const uint32_t n = fr->view.count;
    const float width = s.fb_w - (float)SIDE_W - 40.0F;
    uint32_t columns = width > 3.0F ? (uint32_t)(width / 3.0F) : 0U;
    columns = columns < n ? columns : n;
    columns = columns < MAX_COLUMNS ? columns : MAX_COLUMNS;
    if (columns == 0) {
        return;
    }
    if (cached_epoch != s.frame_epoch || cached_columns != columns) { /* one pass over the records per frame and width */
        cached_epoch = s.frame_epoch;
        cached_columns = columns;
        for (uint32_t c = 0; c < columns; c++) {
            const uint32_t from = (uint32_t)(((uint64_t)c * n) / columns);
            const uint32_t to = (uint32_t)(((uint64_t)(c + 1U) * n) / columns);
            flags[c] = 0;
            targets[c] = from;
            for (uint32_t i = from; i < to; i++) {
                const nt_gfx_event_t *e = &fr->view.events[i];
                const bool draw = fi_is_draw(e);
                targets[c] = draw && targets[c] == from ? i : targets[c];
                flags[c] |= (uint8_t)((draw && e->result == NT_GFX_RESULT_ACCEPTED ? ISSUED : 0) | (draw && e->result == NT_GFX_RESULT_CACHE ? MERGED : 0) |
                                      (fi_matches(fr, i, (fi_query_t){.kind = FI_Q_PROBLEM}) ? PROBLEM : 0));
            }
        }
    }
    const uint32_t selected_column = s.selected < n ? (uint32_t)(((uint64_t)s.selected * columns) / n) : FI_NONE;
    const float column_w = width / (float)columns;
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(16)}, .childAlignment = {.y = CLAY_ALIGN_Y_BOTTOM}}, .backgroundColor = k_bg}) {
        for (uint32_t c = 0; c < columns; c++) {
            const uint32_t from = (uint32_t)(((uint64_t)c * n) / columns);
            const bool issued = (flags[c] & ISSUED) != 0;
            const bool merged = (flags[c] & MERGED) != 0;
            const bool problem = (flags[c] & PROBLEM) != 0;
            const bool selected = c == selected_column;
            const uint32_t target = targets[c];
            const uint32_t id = next_id();
            const nt_ui_events_t ev = nt_ui_events(s_ctx, id, NULL);
            if (ev.clicked) {
                select_record(target);
            }
            Clay_Color color = issued ? k_bar_sel : k_bar;
            color = problem ? k_bar_bad : color;
            color = selected ? k_select_border : color;
            float h = merged ? 6.0F : 0.0F;
            h = issued ? 16.0F : h;
            h = problem || selected ? 16.0F : h;
            const Clay_Color band = (fr->group[from] & 1U) != 0 ? k_panel : k_bg;
            CLAY({.id = (Clay_ElementId){.id = id},
                  .layout = {.sizing = {CLAY_SIZING_FIXED(column_w), CLAY_SIZING_FIXED(16)}, .childAlignment = {.y = CLAY_ALIGN_Y_BOTTOM}},
                  .backgroundColor = ev.hovered ? k_hover : band}) {
                CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(h)}}, .backgroundColor = color}) {}
            }
        }
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void declare_tree_bar(void) {
    static const char *const k_views[VIEW_COUNT] = {"logical", "raw order", "GL calls"};
    ROW_(6) {
        if (button("fold all", &k_text, false)) {
            fold_all(true);
        }
        if (button("unfold all", &k_text, false)) {
            fold_all(false);
        }
        text("  view:", &k_dim);
        for (int v = 0; v < VIEW_COUNT; v++) {
            if (button(k_views[v], &k_text, s.view == (view_t)v)) {
                s.view = (view_t)v;
                s.dirty = true;
            }
        }
        text("  only:", &k_dim);
        if (button("draws", &k_text, s.draws_only)) {
            s.draws_only = !s.draws_only;
            s.dirty = true;
        }
        if (button("problems", &k_text, s.problems)) {
            s.problems = !s.problems;
            s.dirty = true;
        }
        nt_ui_input_style_t style = nt_ui_input_style_defaults();
        style.text = k_text;
        style.placeholder = k_dim;
        if (nt_ui_input_text(s_ctx, NT_UI_DATA_LAYER(LAYER_RECT), LAYER_TEXT, nt_ui_id("filter"), s.filter, sizeof(s.filter), &(nt_ui_input_props_t){.placeholder = "find text"}, &style,
                             &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(22)}}}, true, NULL)) {
            s.dirty = true;
        }
    }
    ROW_(6) {
        if (filtering()) {
            textf(&k_link, "showing: %s  (%u of %u records)", s.query.kind != FI_Q_NONE ? s.query_label : "filtered", s.vis_count, frame_sel()->view.count);
            if (button("clear", &k_text, false)) {
                clear_filters();
            }
        } else {
            textf(&k_dim, "%u rows   ", s.vis_count);
            text("draw/clear", &k_head);
            text("state", &k_state);
            text("no-op request", &k_warn);
            text("GL skipped", &k_skip);
            text("GL call", &k_backend);
            text("note", &k_note);
            text("marker", &k_marker);
            text("failure", &k_bad);
        }
    }
}

static bool in_range(const fi_frame_t *fr, uint32_t id);

static Clay_Color row_background(const fi_frame_t *fr, uint32_t id, bool hovered) {
    if (s.flash == id && nt_time_now() < s.flash_until) {
        return k_select_border;
    }
    if (s.selected == id || in_range(fr, id)) {
        return k_select;
    }
    if (hovered) {
        return k_hover;
    }
    return id < FI_NODE_BASE && draws_pixels(fr, id) ? k_draw_bg : k_panel;
}

static void panel_cache(const fi_frame_t *fr, uint32_t at);
static uint32_t panel_prev_draw(void);
static float list_height(uint32_t slot_id);
static nt_ui_vlist_style_t list_style(float height);

/* The record number, the badge (result) and the row text: the cells every record row shows. */
static void record_cells(const fi_frame_t *fr, uint32_t id, const char *body) {
    char number[16] = "";
    const nt_ui_label_style_t *badge_style = &k_dim;
    const char *badge = "";
    if (id < FI_NODE_BASE) {
        (void)snprintf(number, sizeof(number), "@%u", id);
        badge = result_text(&fr->view.events[id], &badge_style);
    }
    const bool call = id < FI_NODE_BASE && (fr->view.events[id].kind == NT_GFX_EVENT_BACKEND || fr->view.events[id].kind == NT_GFX_EVENT_SKIP);
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(64), CLAY_SIZING_FIXED(ROW_H)}, .childAlignment = {.x = CLAY_ALIGN_X_RIGHT, .y = CLAY_ALIGN_Y_CENTER}}}) { text(number, call ? &k_dim : &k_text); }
    CELL_(96) { text(badge, badge_style); }
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ROW_H)}, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) { text(body, row_style(fr, id)); }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void declare_tree(void) {
    const fi_frame_t *fr = frame_sel();
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 4}, .clip = {.horizontal = true}}) {
        declare_tree_bar();
        declare_timeline(fr);
        const uint32_t list_id = nt_ui_id("tree");
        const uint32_t slot_id = nt_ui_id("tree_slot");
        CLAY({.id = (Clay_ElementId){.id = slot_id}, .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}, .clip = {.vertical = true}}) {
            const float height = list_height(slot_id);
            const nt_ui_vlist_style_t vstyle = list_style(height);
            const nt_ui_vlist_range_t range = nt_ui_vlist_begin(s_ctx, NULL, list_id, s.vis_count, ROW_H, NT_UI_AXIS_Y, &vstyle,
                                                                &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(height)}}, .backgroundColor = k_panel});
            /* Scroll requests wait for a list that exists: on the window's first frame there is none to scroll. */
            const bool listed = nt_ui_get_bbox(s_ctx, slot_id).found;
            if (s.scroll_top && listed) {
                nt_ui_scroll_to(s_ctx, list_id, 0.0F, 0.0F);
                s.scroll_top = false;
            }
            {
                if (s.follow && s.selected != FI_NONE && !s.dirty && listed) { /* after the rows rebuild */
                    const uint32_t at = vis_index_of(s.selected);
                    const uint32_t half = (uint32_t)(height / ROW_H) / 2U;
                    const bool shown = at == FI_NONE || (at >= range.first + 3U && at + 3U <= range.last);
                    if (!shown) {
                        nt_ui_scroll_to(s_ctx, list_id, 0.0F, -(float)(at > half ? at - half : 0U) * ROW_H); /* Clay sign: down is negative */
                    }
                    s.follow = !shown; /* until the row shows: a list that just appeared may not take the first request */
                }
                char buf[640];
                /* The draw the selected one is compared with, tagged in the gutter. */
                uint32_t compared = FI_NONE;
                if (s.selected < fr->view.count && fi_is_draw(&fr->view.events[s.selected])) {
                    panel_cache(fr, s.selected);
                    compared = panel_prev_draw();
                }
                for (uint32_t k = range.first; k <= range.last && k < s.vis_count; ++k) {
                    const uint32_t id = s.vis[k];
                    const uint32_t r = s.vis_row[k];
                    const uint32_t row_id = nt_ui_vlist_item_id(s_ctx, k);
                    const uint32_t fold_id = nt_ui_child_id(row_id, "fold");
                    const nt_ui_events_t ev = nt_ui_events(s_ctx, row_id, NULL);
                    const nt_ui_events_t fold_ev = nt_ui_events(s_ctx, fold_id, NULL);
                    const bool parent = r != FI_NONE && r + 1U < fr->row_count && fr->depth[r + 1U] > fr->depth[r];
                    if (fold_ev.clicked && parent) {
                        s.collapsed[r] ^= 1U;
                        s.dirty = true;
                    } else if (ev.clicked || fold_ev.clicked) { /* a leaf's gutter selects its row */
                        const bool shift = s_keys.shift;
                        if (!shift && id != s.selected) {
                            remember();
                        }
                        s.anchor = shift ? (s.anchor != FI_NONE ? s.anchor : s.selected) : FI_NONE; // NOLINT(readability-avoid-nested-conditional-operator)
                        s.selected = id;
                    }
                    row_label(fr, k, buf, sizeof(buf));
                    const bool selected = s.selected == id;
                    CLAY({.id = (Clay_ElementId){.id = row_id},
                          .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ROW_H)}, .padding = {.right = 6}, .childGap = 6, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}},
                          .backgroundColor = row_background(fr, id, ev.hovered || (fold_ev.hovered && !parent)),
                          .border = {.color = k_select_border, .width = {selected ? 1U : 0U, selected ? 1U : 0U, selected ? 1U : 0U, selected ? 1U : 0U, 0}}}) {
                        CLAY({.id = (Clay_ElementId){.id = fold_id},
                              .layout = {.sizing = {CLAY_SIZING_FIXED(44), CLAY_SIZING_FIXED(ROW_H)}, .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}},
                              .backgroundColor = fold_ev.hovered && parent ? k_hover : (Clay_Color){0}}) {
                            if (id == compared) {
                                text("prev", &k_changed);
                            } else if (parent) {
                                text(s.collapsed[r] != 0 ? "+" : "-", &k_head);
                            }
                        }
                        record_cells(fr, id, buf);
                    }
                }
            }
            nt_ui_vlist_end(s_ctx);
        }
    }
}
// #endregion

// #region summary
static void section(const char *title) {
    CLAY({.layout = {.padding = {0, 0, 10, 2}}}) { text(title, &k_head); }
}

static bool counters_differ(const fi_frame_t *fr) {
    static fi_counter_row_t rows[FI_COUNTER_ROWS];
    const uint32_t n = fi_counter_rows(fr, rows);
    for (uint32_t k = 0; k < n; k++) {
        if (rows[k].counter != rows[k].recount) {
            return true;
        }
    }
    return false;
}

static void declare_banners(const fi_frame_t *fr) {
    if (fr->error != NULL) {
        textf(&k_bad, "incomplete: %s at line %u:%u; the records before it are shown", fr->error, fr->error_line, fr->error_col);
    }
    if (fr->view.overflow) {
        text("events truncated, counters complete", &k_warn);
    }
    if (fr->merge_mismatch != FI_NONE) {
        textf(&k_bad, "inspector merge rule out of date: draw @%u", fr->merge_mismatch);
    }
}

static void card(fi_query_t q, const char *value, const char *label, const char *detail) {
    const uint32_t id = next_id();
    const nt_ui_events_t ev = nt_ui_events(s_ctx, id, NULL);
    const bool any = fi_cell(frame_sel(), q).n > 0;
    if (any) {
        open_query(ev.clicked, q, label);
    }
    Clay_Color background = ev.hovered && any ? k_hover : k_bg;
    background = any && marked(q) ? k_select : background;
    CLAY({.id = (Clay_ElementId){.id = id},
          .layout = {.sizing = {CLAY_SIZING_FIXED(230), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .padding = {8, 8, 4, 4}},
          .backgroundColor = background}) {
        text(value, any ? &k_big : &k_big_dim);
        text(label, &k_text);
        text(detail, &k_dim);
    }
}

static void declare_card(const fi_frame_t *fr) {
    const fi_summary_t *sm = &fr->summary;
    char detail[4][48];
    char value[4][24];
    (void)snprintf(value[0], sizeof(value[0]), "%llu", (unsigned long long)sm->draw_calls.n);
    (void)snprintf(value[1], sizeof(value[1]), "%llu", (unsigned long long)sm->gl_any.n);
    (void)snprintf(value[2], sizeof(value[2]), "%.1f KiB", (double)sm->upload[2].sum / 1024.0);
    (void)snprintf(value[3], sizeof(value[3]), "%llu", (unsigned long long)sm->problems.n);
    (void)snprintf(detail[0], sizeof(detail[0]), "%llu requests, %llu merged", (unsigned long long)sm->vertices.n, (unsigned long long)sm->merged.n);
    (void)snprintf(detail[1], sizeof(detail[1]), "%llu skipped by GL cache", (unsigned long long)sm->skips.n);
    (void)snprintf(detail[2], sizeof(detail[2]), "in %llu upload calls", (unsigned long long)sm->upload[2].n);
    (void)snprintf(detail[3], sizeof(detail[3]), "drops and failed requests");
    ROW_(6) {
        card((fi_query_t){.kind = FI_Q_DRAW_CALL}, value[0], "GL draw calls", detail[0]);
        card((fi_query_t){.kind = FI_Q_GL_ANY}, value[1], "GL calls", detail[1]);
        card((fi_query_t){.kind = FI_Q_UPLOAD, .a = 2}, value[2], "uploaded", detail[2]);
        card((fi_query_t){.kind = FI_Q_PROBLEM}, value[3], "problems", detail[3]);
    }
}

/* Where the draws went, as parts that add up: merged, new because the merge broke, instanced, failed. */
static void declare_draw_equation(const fi_frame_t *fr) {
    const fi_summary_t *sm = &fr->summary;
    const uint64_t instanced = sm->op[NT_GFX_OP_DRAW_INSTANCED][NT_GFX_RESULT_ACCEPTED].n + sm->op[NT_GFX_OP_DRAW_INDEXED_INSTANCED][NT_GFX_RESULT_ACCEPTED].n;
    textf(&k_text, "%llu draw requests -> %llu GL draw calls:", (unsigned long long)sm->vertices.n, (unsigned long long)sm->draw_calls.n);
    if (sm->merged.n > 0) {
        insight((fi_query_t){.kind = FI_Q_MERGED}, &k_link, "  %llu merged into the previous draw (no GL call)", (unsigned long long)sm->merged.n);
    }
    for (uint32_t b = FI_BREAK_COMMAND; b < FI_BREAK_COUNT; b++) {
        if (sm->brk[b].n > 0) {
            insight((fi_query_t){.kind = FI_Q_BREAK, .a = b}, &k_link, "  %llu new GL draws: %s", (unsigned long long)sm->brk[b].n, fi_break_name((fi_break_t)b));
        }
    }
    if (instanced > 0) {
        insight((fi_query_t){.kind = FI_Q_INSTANCES}, &k_link, "  %llu instanced (instanced draws never merge)", (unsigned long long)instanced);
    }
}

/* Plain sentences about what the frame did, each a filter for the records it is about. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void declare_insights(const fi_frame_t *fr) {
    const fi_summary_t *sm = &fr->summary;
    section("Insights (click: show the records)");
    declare_draw_equation(fr);

    /* Redundant requests are one line of chips, not a sentence each. */
    uint32_t chips = 0;
    for (uint32_t op = 0; op < NT_GFX_OP_COUNT; op++) {
        chips += sm->op[op][NT_GFX_RESULT_CACHE].n > 0 && op != NT_GFX_OP_DRAW && op != NT_GFX_OP_DRAW_INDEXED ? 1U : 0U;
    }
    if (chips > 0) {
        text("No-op requests (CACHE: already set), per operation:", &k_text);
    }
    for (uint32_t op = 0; op < NT_GFX_OP_COUNT && chips > 0; op += 4) {
        ROW_(4) {
            for (uint32_t k = op; k < op + 4 && k < NT_GFX_OP_COUNT; k++) {
                const uint64_t cached = sm->op[k][NT_GFX_RESULT_CACHE].n;
                if (cached > 0 && k != NT_GFX_OP_DRAW && k != NT_GFX_OP_DRAW_INDEXED) {
                    insight((fi_query_t){.kind = FI_Q_OP, .a = k, .b = NT_GFX_RESULT_CACHE}, &k_warn, "%s %llu", nt_gfx_operation_name(k), (unsigned long long)cached);
                }
            }
        }
    }
    for (uint32_t op = 0; op < NT_GFX_OP_COUNT; op++) {
        for (uint32_t r = NT_GFX_RESULT_INACTIVE; r < FI_RESULTS; r++) {
            if (sm->op[op][r].n > 0) {
                insight((fi_query_t){.kind = FI_Q_OP, .a = op, .b = (uint8_t)r}, &k_bad, "%llu %s requests ended %s", (unsigned long long)sm->op[op][r].n, nt_gfx_operation_name(op),
                        nt_gfx_result_name(r));
            }
        }
    }
    if (sm->skips.n > 0) {
        insight((fi_query_t){.kind = FI_Q_SKIP}, &k_skip, "%llu GL calls left out by the GL cache (SKIP): GL already held that state", (unsigned long long)sm->skips.n);
    }
    if (sm->drops.n > 0) {
        insight((fi_query_t){.kind = FI_Q_DROP}, &k_bad, "%llu renderer drops", (unsigned long long)sm->drops.n);
    }
    if (!fr->view.overflow && counters_differ(fr)) {
        text("the frame's counters differ from its records: see Counters check", &k_bad);
    }
}

static void stat(const char *name, fi_query_t q, uint64_t value) {
    if (value == 0 && fi_cell(frame_sel(), q).n == 0) {
        return;
    }
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childGap = 6, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) {
        CELL_(330) { text(name, &k_text); }
        CELL_(90) { number(q, value, name); }
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void declare_funnel(const fi_frame_t *fr) {
    static const float k_w[] = {210, 44, 80, 60, 44, 44, 44};
    static const char *const k_heads[] = {"operation", "req", "ACCEPTED", "CACHE", "GL", "SKIP", "nest"};
    section("Operations");
    text("CACHE: already set, merged, or an existing object", &k_dim);
    text("GL: calls issued   SKIP: left out by the GL cache", &k_dim);
    ROW_(4) {
        for (int c = 0; c < 7; c++) {
            CELL_(k_w[c]) { text(k_heads[c], &k_dim); }
        }
        text("other results", &k_dim);
    }
    char label[96];
    for (uint32_t op = 0; op < NT_GFX_OP_COUNT; op++) {
        uint64_t requested = 0;
        for (uint32_t r = 0; r < FI_RESULTS; r++) {
            requested += fr->summary.op[op][r].n;
        }
        if (requested == 0) {
            continue;
        }
        const char *name = nt_gfx_operation_name(op);
        ROW_(4) {
            CELL_(k_w[0]) { text(name, &k_text); }
            CELL_(k_w[1]) { textf(&k_text, "%llu", (unsigned long long)requested); }
            (void)snprintf(label, sizeof(label), "%s ACCEPTED", name);
            CELL_(k_w[2]) { number((fi_query_t){.kind = FI_Q_OP, .a = op, .b = NT_GFX_RESULT_ACCEPTED}, fr->summary.op[op][NT_GFX_RESULT_ACCEPTED].n, label); }
            (void)snprintf(label, sizeof(label), "%s CACHE", name);
            CELL_(k_w[3]) { number((fi_query_t){.kind = FI_Q_OP, .a = op, .b = NT_GFX_RESULT_CACHE}, fr->summary.op[op][NT_GFX_RESULT_CACHE].n, label); }
            (void)snprintf(label, sizeof(label), "GL calls of %s", name);
            CELL_(k_w[4]) { number((fi_query_t){.kind = FI_Q_OP_CALLS, .a = op}, fr->summary.op_calls[op].n, label); }
            (void)snprintf(label, sizeof(label), "SKIP records of %s", name);
            CELL_(k_w[5]) { number((fi_query_t){.kind = FI_Q_OP_SKIPS, .a = op}, fr->summary.op_skips[op].n, label); }
            (void)snprintf(label, sizeof(label), "%s nested in another operation", name);
            CELL_(k_w[6]) { number((fi_query_t){.kind = FI_Q_OP_NESTED, .a = op}, fr->summary.op_nested[op].n, label); }
            for (uint32_t r = 0; r < FI_RESULTS; r++) {
                if (r == NT_GFX_RESULT_ACCEPTED || r == NT_GFX_RESULT_CACHE || fr->summary.op[op][r].n == 0) {
                    continue;
                }
                (void)snprintf(label, sizeof(label), "%s %s", name, nt_gfx_result_name(r));
                text(nt_gfx_result_name(r), r == NT_GFX_RESULT_NONE ? &k_dim : &k_bad);
                number((fi_query_t){.kind = FI_Q_OP, .a = op, .b = (uint8_t)r}, fr->summary.op[op][r].n, label);
            }
        }
    }
}

static void declare_draws(const fi_frame_t *fr) {
    const fi_summary_t *sm = &fr->summary;
    char name[96];
    section("Draws");
    stat("draw requests (ACCEPTED or CACHE)", (fi_query_t){.kind = FI_Q_VERTICES}, sm->vertices.n);
    stat("merged into the previous draw (CACHE)", (fi_query_t){.kind = FI_Q_MERGED}, sm->merged.n);
    stat("GL draw calls issued", (fi_query_t){.kind = FI_Q_DRAW_CALL}, sm->draw_calls.n);
    for (uint32_t c = 0; c < NT_GFX_GL_COUNT; c++) {
        if (fi_is_draw_call(c) && sm->gl[c].n > 0) {
            (void)snprintf(name, sizeof(name), "  %s", nt_gfx_gl_call_name(c));
            stat(name, (fi_query_t){.kind = FI_Q_GL_CALL, .a = c}, sm->gl[c].n);
        }
    }
    stat("vertices (x instances)", (fi_query_t){.kind = FI_Q_VERTICES}, sm->vertices.sum);
    stat("indices (x instances)", (fi_query_t){.kind = FI_Q_INDICES}, sm->indices.sum);
    stat("instances of instanced draws", (fi_query_t){.kind = FI_Q_INSTANCES}, sm->instances.sum);
}

static void declare_transfers(const fi_frame_t *fr) {
    const fi_summary_t *sm = &fr->summary;
    section("Uploads (bytes sent to GL)");
    stat("buffer upload calls", (fi_query_t){.kind = FI_Q_UPLOAD, .a = 0}, sm->upload[0].n);
    stat("buffer upload bytes", (fi_query_t){.kind = FI_Q_UPLOAD, .a = 0}, sm->upload[0].sum);
    stat("texture upload calls", (fi_query_t){.kind = FI_Q_UPLOAD, .a = 1}, sm->upload[1].n);
    stat("texture upload bytes", (fi_query_t){.kind = FI_Q_UPLOAD, .a = 1}, sm->upload[1].sum);
    stat("  of them frame storage at end_frame", (fi_query_t){.kind = FI_Q_FRAME_STORE}, sm->frame_store.sum);
    ROW_(8) {
        CELL_(190) { text("command stream bytes", &k_text); }
        textf(&k_text, "%u (counter only)", fr->view.counters.stream_bytes);
    }
}

/* Which buffers received the uploaded bytes, the biggest first; a click opens the upload. */
static void declare_uploads_by_buffer(const fi_frame_t *fr) {
    enum { SHOWN = 16 };
    if (fr->buffer_upload_count == 0) {
        return;
    }
    section("Uploads by buffer");
    char alias[32];
    char label[96];
    for (uint32_t k = 0; k < fr->buffer_upload_count && k < SHOWN; k++) {
        const fi_buffer_upload_t *u = &fr->buffer_upload[k];
        alias_of(fr, NT_GFX_OBJECT_BUFFER, u->object, alias, sizeof(alias));
        (void)snprintf(label, sizeof(label), "%-10s %8.1f KiB in %u call%s", alias, (double)u->bytes / 1024.0, u->calls, u->calls == 1 ? "" : "s");
        open_record(button(label, &k_link, s.selected == u->first), u->first);
    }
    if (fr->buffer_upload_count > SHOWN) {
        textf(&k_dim, "... %u more buffers", fr->buffer_upload_count - SHOWN);
    }
}

static void declare_resources(const fi_frame_t *fr) {
    static const char *const k_res[FI_RES_COUNT] = {"live at frame start", "created", "destroyed"};
    section("Resources (bytes requested from GL, from definitions)");
    for (uint32_t kind = 1; kind < FI_OBJECTS; kind++) {
        for (uint32_t r = 0; r < FI_RES_COUNT; r++) {
            const fi_cell_t c = fr->summary.resource[kind][r];
            if (c.n == 0) {
                continue;
            }
            char name[96];
            (void)snprintf(name, sizeof(name), "%s %s", nt_gfx_object_kind_name(kind), k_res[r]);
            ROW_(8) {
                CELL_(300) { text(name, &k_text); }
                number((fi_query_t){.kind = FI_Q_RESOURCE, .a = kind, .b = (uint8_t)r}, c.n, name);
                if (c.sum > 0) {
                    textf(&k_dim, "%.1f KiB", (double)c.sum / 1024.0);
                }
            }
        }
    }
}

static void declare_gl_calls(const fi_frame_t *fr) {
    section("GL calls by name");
    uint32_t order[NT_GFX_GL_COUNT];
    uint32_t n = 0;
    for (uint32_t c = 0; c < NT_GFX_GL_COUNT; c++) {
        if (fr->summary.gl[c].n > 0) {
            order[n++] = c;
        }
    }
    for (uint32_t a = 1; a < n; a++) { /* insertion sort, most frequent first */
        const uint32_t v = order[a];
        uint32_t b = a;
        while (b > 0 && fr->summary.gl[order[b - 1]].n < fr->summary.gl[v].n) {
            order[b] = order[b - 1];
            b--;
        }
        order[b] = v;
    }
    for (uint32_t k = 0; k < n; k++) {
        stat(nt_gfx_gl_call_name(order[k]), (fi_query_t){.kind = FI_Q_GL_CALL, .a = order[k]}, fr->summary.gl[order[k]].n);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void declare_groups(const fi_frame_t *fr) {
    static const char *const k_heads[FI_METRIC_COUNT] = {"requests", "GL draws", "GL calls", "upload B", "drops"};
    section("Where the work is: by marker and note");
    text("requests: draw requests to the engine", &k_dim);
    text("GL draws: GL draw calls they cost (merged requests share one)", &k_dim);
    ROW_(4) {
        CELL_(200) { text("marker path > note", &k_dim); }
        for (int m = 0; m < FI_METRIC_COUNT; m++) {
            CELL_(80) { text(k_heads[m], &k_dim); }
        }
    }
    enum { SHOWN = 200 }; /* a frame of per-entity notes would pass Clay's element count */
    uint32_t shown = 0;
    uint32_t hidden = 0;
    for (uint32_t g = 0; g < fr->group_count; g++) {
        const fi_cell_t *c = &fr->summary.group[(size_t)g * FI_METRIC_COUNT];
        if (c[FI_METRIC_DRAWS].n + c[FI_METRIC_CALLS].n + c[FI_METRIC_DROPS].n == 0) {
            continue;
        }
        if (shown == SHOWN) {
            hidden++;
            continue;
        }
        shown++;
        ROW_(4) {
            CELL_(200) { textf(&k_text, "%.24s", fr->group_key[g]); }
            for (uint32_t m = 0; m < FI_METRIC_COUNT; m++) {
                char label[FI_KEY_LEN + 16];
                (void)snprintf(label, sizeof(label), "%s of %s", k_heads[m], fr->group_key[g]);
                CELL_(80) { number((fi_query_t){.kind = FI_Q_GROUP, .a = g, .b = (uint8_t)m}, m == FI_METRIC_BYTES ? c[m].sum : c[m].n, label); }
            }
        }
    }
    if (hidden > 0) {
        textf(&k_dim, "... %u more groups", hidden);
    }
}

static void declare_counters(const fi_frame_t *fr);
static void declare_details(const fi_frame_t *fr);
typedef struct state_view state_view_t;
static void declare_state(const fi_frame_t *fr, const state_view_t *st, bool *all);
static const state_view_t *panel_state(const fi_frame_t *fr, uint32_t at);
static void declare_raw(const fi_frame_t *fr, uint32_t id);
static void link_line(const fi_frame_t *fr, const char *what, uint32_t record);

/* Information: the frame's numbers in one readable column; a click on a number shows its records. */
static void declare_info_page(void) {
    const fi_frame_t *fr = frame_sel();
    const nt_ui_scroll_style_t scroll = nt_ui_scroll_style_defaults();
    nt_ui_scroll_begin(
        s_ctx, NULL, nt_ui_id("info"), &scroll,
        &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .padding = CLAY_PADDING_ALL(12), .childGap = 3},
                                   .backgroundColor = k_panel});
    text("Click a number or a sentence to see its records in Commands.", &k_dim);
    declare_card(fr);
    declare_banners(fr);
    CLAY({.layout = {.childGap = 40, .layoutDirection = s.fb_w < 1400.0F ? CLAY_TOP_TO_BOTTOM : CLAY_LEFT_TO_RIGHT}}) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(640), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 3}}) {
            declare_insights(fr);
            declare_groups(fr);
            declare_uploads_by_buffer(fr);
            declare_draws(fr);
            declare_transfers(fr);
            declare_resources(fr);
        }
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(640), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 3}}) {
            declare_funnel(fr);
            declare_gl_calls(fr);
            declare_counters(fr);
        }
    }
    nt_ui_scroll_end(s_ctx);
}

/* A Shift range spans logical rows, folded ones included. */
static bool range_rows(const fi_frame_t *fr, uint32_t *lo, uint32_t *hi) {
    if (s.anchor == FI_NONE || s.selected == FI_NONE || s.anchor == s.selected) {
        return false;
    }
    const uint32_t a = fi_row_of(fr, s.anchor);
    const uint32_t b = fi_row_of(fr, s.selected);
    if (a == FI_NONE || b == FI_NONE) {
        return false;
    }
    *lo = a < b ? a : b;
    *hi = a < b ? b : a;
    return true;
}

static bool in_range(const fi_frame_t *fr, uint32_t id) {
    uint32_t lo = 0;
    uint32_t hi = 0;
    const uint32_t r = range_rows(fr, &lo, &hi) ? fi_row_of(fr, id) : FI_NONE;
    return r != FI_NONE && r >= lo && r <= hi;
}

/* What the rows from the anchor to the selection did, counted as the summary counts. */
static void declare_range(const fi_frame_t *fr, uint32_t lo, uint32_t hi) {
    static struct {
        uint32_t epoch, lo, hi, records;
        fi_summary_t sum;
    } range = {.epoch = FI_NONE};
    if (range.epoch != s.frame_epoch || range.lo != lo || range.hi != hi) {
        range.epoch = s.frame_epoch;
        range.lo = lo;
        range.hi = hi;
        range.records = fi_range(fr, lo, hi, &range.sum);
    }
    const fi_summary_t *sm = &range.sum;
    textf(&k_head, "%u records selected (Shift+click or Shift+Up/Down)", range.records);
    textf(&k_text, "%llu draw requests -> %llu GL draw calls, %llu merged", (unsigned long long)sm->vertices.n, (unsigned long long)sm->draw_calls.n, (unsigned long long)sm->merged.n);
    textf(&k_text, "%llu GL calls, %llu left out by the GL cache", (unsigned long long)sm->gl_any.n, (unsigned long long)sm->skips.n);
    textf(&k_text, "%.1f KiB uploaded", (double)sm->upload[2].sum / 1024.0);
    textf(sm->problems.n > 0 ? &k_bad : &k_text, "%llu problems", (unsigned long long)sm->problems.n);
    section("Requests by operation");
    for (uint32_t op = 0; op < NT_GFX_OP_COUNT; op++) {
        uint64_t requests = 0;
        for (uint32_t r = 0; r < FI_RESULTS; r++) {
            requests += sm->op[op][r].n;
        }
        if (requests > 0) {
            textf(&k_text, "%-24s %llu", nt_gfx_operation_name(op), (unsigned long long)requests);
        }
    }
}

/* The selected command: what it is, why it merged or not, its GL calls and the state it ran with. */
static void declare_selection(void) {
    const nt_ui_scroll_style_t scroll = nt_ui_scroll_style_defaults();
    nt_ui_scroll_begin(
        s_ctx, NULL, nt_ui_id("selection"), &scroll,
        &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(SIDE_W), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .padding = CLAY_PADDING_ALL(8), .childGap = 2},
                                   .backgroundColor = k_panel});
    const fi_frame_t *fr = frame_sel();
    uint32_t lo = 0;
    uint32_t hi = 0;
    if (range_rows(fr, &lo, &hi)) {
        declare_range(fr, lo, hi);
    } else {
        declare_details(fr);
        if (s.selected < fr->view.count) {
            declare_state(fr, panel_state(fr, s.selected), &s.state_all);
            declare_raw(fr, s.selected);
        }
    }
    nt_ui_scroll_end(s_ctx);
}

/* A list's height from last frame's layout of its GROW slot. The slot clips, so the list never props it open; a growing
 * list instead makes Clay shrink its tall content in float, which at a million pixels takes tens of thousands of steps. */
static float list_height(uint32_t slot_id) {
    const nt_ui_bbox_t slot = nt_ui_get_bbox(s_ctx, slot_id);
    const float h = slot.found ? slot.height : (float)(g_nt_window.fb_height > 0 ? g_nt_window.fb_height : 900) - 200.0F;
    return h > ROW_H ? h : ROW_H;
}

/* The vlist recycles row ids over a ring that must outnumber the rows on screen. */
static nt_ui_vlist_style_t list_style(float height) {
    nt_ui_vlist_style_t style = nt_ui_vlist_style_defaults();
    const uint32_t rows = (uint32_t)(height / ROW_H) + 2U + (2U * (uint32_t)style.overscan);
    style.id_ring = rows + 16U > style.id_ring ? rows + 16U : style.id_ring;
    return style;
}

/* Request and GL state at a record, next to the state at an earlier record it is compared with. */
struct state_view {
    fi_request_state_t req, was_req;
    fi_gl_state_t gl, was_gl;
    uint32_t was, was_gl_at; /* the records compared with, FI_NONE: nothing */
    const char *was_name, *was_gl_name;
    bool link_was; /* a draw worth a link; the frame start is only a point in time */
};

/* The last record begin_frame wrote: the state the game's first call starts from. */
static uint32_t frame_start(const fi_frame_t *fr) {
    const uint32_t top = fi_row_of(fr, FI_NODE_BASE + FI_NODE_BEGIN_FRAME);
    uint32_t last = FI_NONE;
    for (uint32_t r = top + 1U; top != FI_NONE && r < fr->row_count && fr->depth[r] > fr->depth[top]; r++) {
        last = fr->row[r] < FI_NODE_BASE && (last == FI_NONE || fr->row[r] > last) ? fr->row[r] : last;
    }
    return last;
}

/* Init and End state show one view: the state at the frame's start, and at its end compared with the start. */
static void declare_frame_state(const fi_frame_t *fr, bool end) {
    static struct {
        uint32_t epoch, start;
        state_view_t init, end;
    } view = {.epoch = FI_NONE};
    if (view.epoch != s.frame_epoch) {
        view.epoch = s.frame_epoch;
        view.start = frame_start(fr);
        const uint32_t start = view.start;
        if (start != FI_NONE) {
            view.init = (state_view_t){.was = FI_NONE, .was_gl_at = FI_NONE, .was_name = "", .was_gl_name = ""};
            fi_request_state(fr, start, &view.init.req);
            fi_gl_state(fr, start, &view.init.gl);
            view.init.was_req = view.init.req;
            view.end = (state_view_t){.was = start, .was_gl_at = start, .was_name = "frame start", .was_gl_name = "frame start", .was_req = view.init.req, .was_gl = view.init.gl};
            fi_request_state(fr, fr->view.count - 1U, &view.end.req);
            fi_gl_state(fr, fr->view.count - 1U, &view.end.gl);
        }
    }
    const nt_ui_scroll_style_t scroll = nt_ui_scroll_style_defaults();
    nt_ui_scroll_begin(
        s_ctx, NULL, nt_ui_id(end ? "end_state" : "init_state"), &scroll,
        &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .padding = CLAY_PADDING_ALL(12), .childGap = 2},
                                   .backgroundColor = k_panel});
    if (end) {
        text("State after the frame's last record; violet: what the frame changed since its start (Init state).", &k_dim);
    } else {
        text("State the game's first call starts from: what begin_frame inherited. Its records are under begin_frame in Commands.", &k_dim);
    }
    if (view.start == FI_NONE) {
        text("The frame recorded no begin_frame state.", &k_dim);
    } else {
        declare_state(fr, end ? &view.end : &view.init, &s.state_all);
    }
    nt_ui_scroll_end(s_ctx);
}
// #endregion

// #region details
static void link_line(const fi_frame_t *fr, const char *what, uint32_t record) {
    char body[400];
    row_text(fr, record, body, sizeof(body));
    char buf[512];
    (void)snprintf(buf, sizeof(buf), "%s@%u  %s", what, record, body);
    const bool commands = s.page == PAGE_COMMANDS;
    const uint32_t id = next_id();
    const nt_ui_events_t ev = nt_ui_events(s_ctx, id, NULL);
    Clay_Color background = ev.hovered ? k_hover : k_panel;
    background = !commands && s.selected == record ? k_select : background;
    CLAY({.id = (Clay_ElementId){.id = id}, .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .padding = {4, 4, 1, 1}}, .backgroundColor = background}) { text(buf, &k_link_wrap); }
    if (commands && ev.clicked) {
        select_record(record);
    } else if (!commands) {
        open_record(ev.clicked, record);
    }
}

/* What the selected-command panel needs from whole-frame scans, computed once per selection, not per UI frame. */
static struct {
    uint32_t epoch, at;
    state_view_t st; /* compared with the previous draw and the previous GL draw */
    uint32_t gl_at;
    uint32_t ordinal; /* GL draw calls up to and including this draw */
    uint32_t children[40];
    uint32_t child_count, child_total;
} s_panel = {.epoch = FI_NONE};

static bool is_draw_op(const nt_gfx_event_t *e);
static bool is_draw_issue(const nt_gfx_event_t *e);
static uint32_t previous(const fi_frame_t *fr, uint32_t at, bool (*accept)(const nt_gfx_event_t *));

static uint32_t panel_prev_draw(void) { return s_panel.st.was; }

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void panel_cache(const fi_frame_t *fr, uint32_t at) {
    if (s_panel.epoch == s.frame_epoch && s_panel.at == at) {
        return;
    }
    s_panel.epoch = s.frame_epoch;
    s_panel.at = at;
    state_view_t *st = &s_panel.st;
    st->was_name = "previous draw";
    st->was_gl_name = "previous GL draw";
    st->link_was = true;
    fi_request_state(fr, at, &st->req);
    st->was = previous(fr, at, is_draw_op);
    st->was_req = st->req;
    if (st->was != FI_NONE) {
        fi_request_state(fr, st->was, &st->was_req);
    }
    s_panel.child_count = 0;
    s_panel.child_total = 0;
    for (uint32_t i = at + 1U; i < fr->view.count; i++) {
        if (fr->parent[i] == at && s_panel.child_total++ < 40U) {
            s_panel.children[s_panel.child_count++] = i;
        }
    }
    /* An operation's draw-phase calls issue at end_frame: its GL state is the one after its last call. A merged
     * draw issued nothing; it ran with the call of the draw it joined. */
    const uint32_t joined = fi_joined(fr, at);
    const uint32_t source = joined != FI_NONE ? joined : at;
    s_panel.gl_at = at;
    for (uint32_t i = source + 1U; i < fr->view.count; i++) {
        s_panel.gl_at = fr->parent[i] == source && fr->view.events[i].kind == NT_GFX_EVENT_BACKEND ? i : s_panel.gl_at;
    }
    fi_gl_state(fr, s_panel.gl_at, &st->gl);
    st->was_gl_at = previous(fr, s_panel.gl_at, is_draw_issue);
    st->was_gl.count = 0;
    if (st->was_gl_at != FI_NONE) {
        fi_gl_state(fr, st->was_gl_at, &st->was_gl);
    }
    s_panel.ordinal = 0;
    for (uint32_t i = 0; i <= at; i++) {
        s_panel.ordinal += is_draw_op(&fr->view.events[i]) && fr->view.events[i].result == NT_GFX_RESULT_ACCEPTED ? 1U : 0U;
    }
}

static const state_view_t *panel_state(const fi_frame_t *fr, uint32_t at) {
    panel_cache(fr, at);
    return &s_panel.st;
}

/* For a draw, the answer to "did it cost a GL draw call, and why": merged, or new and what broke the merge. */
static void declare_verdict(const fi_frame_t *fr, uint32_t id) {
    const nt_gfx_event_t *e = &fr->view.events[id];
    if (!fi_is_draw(e)) {
        return;
    }
    if (fi_joined(fr, id) != FI_NONE) {
        text("MERGED: no new GL draw call; its range joined an earlier draw", &k_warn);
        link_line(fr, "joined ", fi_joined(fr, id));
        return;
    }
    if (e->result != NT_GFX_RESULT_ACCEPTED) {
        textf(&k_bad, "%s: nothing drawn", nt_gfx_result_name(e->result));
        return;
    }
    panel_cache(fr, id);
    textf(&k_head, "NEW GL draw call %u of %u", s_panel.ordinal, nt_gfx_draw_calls(&fr->view.counters));
    if (e->operation == NT_GFX_OP_DRAW_INSTANCED || e->operation == NT_GFX_OP_DRAW_INDEXED_INSTANCED) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}}}) { text("instanced draws never merge: joining them would change gl_InstanceID", &k_wrap); }
    } else if (fr->brk[id] != FI_BREAK_NONE) {
        textf(&k_warn, "not merged: %s", fi_break_name((fi_break_t)fr->brk[id]));
        if (fr->cause[id] != FI_NONE) {
            link_line(fr, "because of ", fr->cause[id]);
        }
    }
}

static void declare_details(const fi_frame_t *fr) {
    const uint32_t id = s.selected;
    if (id == FI_NONE) {
        text("Click a row to see it here; -/+ folds.", &k_dim);
        return;
    }
    if (id >= FI_NODE_BASE) {
        text(fi_node_name((fi_node_t)(id - FI_NODE_BASE)), &k_head);
        return;
    }
    char body[600];
    row_text(fr, id, body, sizeof(body));
    textf(&k_head, "@%u", id);
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}}}) { text(body, &k_wrap); }
    declare_verdict(fr, id);
    if (fr->parent[id] < FI_NODE_BASE) {
        link_line(fr, "inside ", fr->parent[id]);
    } else {
        textf(&k_dim, "phase: %s", fi_node_name((fi_node_t)(fr->parent[id] - FI_NODE_BASE)));
    }
    textf(&k_dim, "group: %s", fr->group_key[fr->group[id]]);
    panel_cache(fr, id);
    for (uint32_t k = 0; k < s_panel.child_count; k++) {
        if (k == 0) {
            section("GL calls and records it caused");
        }
        link_line(fr, "", s_panel.children[k]);
    }
    if (s_panel.child_total > s_panel.child_count) {
        textf(&k_dim, "... %u more", s_panel.child_total - s_panel.child_count);
    }
}

/* The record exactly as captured, last: everything above is read from it. */
static void declare_raw(const fi_frame_t *fr, uint32_t id) {
    const char *line = NULL;
    const size_t len = fi_record_text(fr, id, &line);
    char buf[1024];
    (void)snprintf(buf, sizeof(buf), "%.*s%s", (int)(len > 900 ? 900 : len), line, len > 900 ? " ..." : "");
    section("Record as captured");
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}}}) { text(buf, &k_wrap); }
    const nt_gfx_event_t *e = &fr->view.events[id];
    if (e->kind <= NT_GFX_EVENT_ARGUMENT && e->object_kind != 0 && e->object_kind < FI_OBJECTS && e->object != 0) {
        char alias[32];
        alias_of(fr, e->object_kind, e->object, alias, sizeof(alias));
        textf(&k_dim, "obj %u is %s", e->object, alias);
    }
}
// #endregion

// #region state
/* The last record before `at` that the predicate accepts, FI_NONE without one. */
static uint32_t previous(const fi_frame_t *fr, uint32_t at, bool (*accept)(const nt_gfx_event_t *)) {
    for (uint32_t i = at; i-- > 0;) {
        if (accept(&fr->view.events[i])) {
            return i;
        }
    }
    return FI_NONE;
}

static bool is_draw_op(const nt_gfx_event_t *e) { return e->link == 0 && fi_is_draw(e); }
static bool is_draw_issue(const nt_gfx_event_t *e) { return e->kind == NT_GFX_EVENT_BACKEND && fi_is_draw_call(e->detail); }

static void state_line(const fi_frame_t *fr, const char *name, uint32_t record, uint32_t before, bool inherited, bool changed) {
    ROW_(6) {
        const nt_ui_label_style_t *style = inherited ? &k_dim : &k_text;
        CELL_(210) { text(name, changed ? &k_changed : style); }
        if (record == FI_NONE) {
            text("unset", &k_dim);
        } else {
            link_line(fr, inherited ? "inherited " : "", record);
        }
    }
    if (changed && before != FI_NONE) {
        ROW_(6) {
            CELL_(210) { text("  was", &k_changed); }
            link_line(fr, "", before);
        }
    }
}

static void gl_slot_name(const fi_gl_slot_t *slot, char *buf, size_t size) {
    const char *key = gl_enum_name(slot->key[0]);
    char hex[16];
    (void)snprintf(hex, sizeof(hex), "0x%x", slot->key[0]);
    key = key != NULL ? key : hex;
    switch (slot->call) {
    case NT_GFX_GL_NONE:
        (void)snprintf(buf, size, "fixed function");
        break;
    case NT_GFX_GL_glBindTexture:
        (void)snprintf(buf, size, "texture unit %u", slot->key[0]);
        break;
    case NT_GFX_GL_glBindSampler:
        (void)snprintf(buf, size, "sampler unit %u", slot->key[0]);
        break;
    case NT_GFX_GL_glBindBufferRange:
        (void)snprintf(buf, size, "%s #%u", key, slot->key[1]);
        break;
    case NT_GFX_GL_glEnable:
    case NT_GFX_GL_glBindBuffer:
    case NT_GFX_GL_glBindFramebuffer:
    case NT_GFX_GL_glPixelStorei:
        (void)snprintf(buf, size, "%s", key);
        break;
    default:
        (void)snprintf(buf, size, "%s", nt_gfx_gl_call_name(slot->call) + 2);
        break;
    }
}

/* The record that set this slot at the earlier point, FI_NONE when it was not set there. */
static uint32_t slot_before(const fi_gl_slot_t *slot, const fi_gl_state_t *before) {
    for (uint32_t p = 0; p < before->count; p++) {
        const fi_gl_slot_t *old = &before->slot[p];
        if (old->call == slot->call && old->key[0] == slot->key[0] && old->key[1] == slot->key[1]) {
            return old->record;
        }
    }
    return FI_NONE;
}

static bool scissor_on(const fi_frame_t *fr, uint32_t record) { return record != FI_NONE && fr->view.events[record].data.state.integers[0] != 0; }

/* Request and GL state of a view; violet marks a value that differs from the record it is compared with.
 * `all` shows unchanged GL state too. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void declare_state(const fi_frame_t *fr, const state_view_t *st, bool *all) {
    const fi_request_state_t *req = &st->req;
    const fi_request_state_t *was_req = &st->was_req;
    const bool compared = st->was != FI_NONE;
    char title[96];
    char prefix[48];
    (void)snprintf(title, sizeof(title), "Engine state  (violet: a different value than at the %s)", st->was_name);
    section(compared ? title : "Engine state");
    if (compared && st->link_was) {
        (void)snprintf(prefix, sizeof(prefix), "%s ", st->was_name);
        link_line(fr, prefix, st->was);
    }
    /* Grouped by stage: output, program, input, raster. */
    const struct {
        const char *name;
        uint32_t now, before;
    } rows[] = {{"output: pass", req->pass, was_req->pass},
                {"program: pipeline", req->pipeline, was_req->pipeline},
                {"program: textures", req->texture_set, was_req->texture_set},
                {"input: vertex input", req->vertex_input, was_req->vertex_input},
                {"raster: viewport", req->viewport, was_req->viewport},
                {"raster: scissor", req->scissor, was_req->scissor},
                {"raster: scissor on", req->scissor_enable, was_req->scissor_enable}};
    for (uint32_t k = 0; k < sizeof(rows) / sizeof(rows[0]); k++) {
        const uint32_t before = compared ? rows[k].before : rows[k].now;
        bool changed = !fi_same_setting(fr, before, rows[k].now);
        if (k + 1U == sizeof(rows) / sizeof(rows[0])) { /* unset scissor is off: a pass or a restore turned it off */
            changed = scissor_on(fr, before) != scissor_on(fr, rows[k].now);
        }
        state_line(fr, rows[k].name, rows[k].now, before, rows[k].now != FI_NONE && fr->view.events[rows[k].now].kind == NT_GFX_EVENT_INITIAL, changed);
    }
    char name[48];
    for (uint32_t k = 0; k < 8; k++) {
        if (req->ubo[k] != FI_NONE || was_req->ubo[k] != FI_NONE) {
            (void)snprintf(name, sizeof(name), "program: uniform slot %u", k);
            const uint32_t before = compared ? was_req->ubo[k] : req->ubo[k];
            state_line(fr, name, req->ubo[k], before, false, !fi_same_setting(fr, before, req->ubo[k]));
        }
    }
    const fi_gl_state_t *gl_now = &st->gl;
    const fi_gl_state_t *prev_gl = &st->was_gl;
    const uint32_t prev_issue = st->was_gl_at;
    (void)snprintf(title, sizeof(title), "GL state  (violet: a different value than at the %s)", st->was_gl_name);
    section(prev_issue != FI_NONE ? title : "GL state");
    if (prev_issue != FI_NONE) {
        if (button(*all ? "show changes only" : "show all GL state", &k_text, false)) {
            *all = !*all;
        }
        if (st->link_was) {
            (void)snprintf(prefix, sizeof(prefix), "%s ", st->was_gl_name);
            link_line(fr, prefix, prev_issue);
        }
    }
    uint32_t shown = 0;
    for (uint32_t k = 0; k < gl_now->count; k++) {
        const fi_gl_slot_t *slot = &gl_now->slot[k];
        const uint32_t before = prev_issue != FI_NONE ? slot_before(slot, prev_gl) : slot->record;
        const bool changed = !fi_gl_same(fr, slot, before, slot->record);
        if (!*all && !changed && prev_issue != FI_NONE) {
            continue;
        }
        gl_slot_name(slot, name, sizeof(name));
        state_line(fr, name, slot->record, before, slot->inherited, changed);
        shown++;
    }
    if (gl_now->full) {
        text("more GL state than the inspector tracks: the rest is not shown", &k_warn);
    }
    if (shown == 0) {
        textf(&k_dim, "no GL state changed since the %s", st->was_gl_name);
    }
}
// #endregion

// #region counters
static void counter_row(const char *name, uint64_t counter, uint64_t recount, bool bytes) {
    ROW_(8) {
        CELL_(190) { text(name, counter != recount ? &k_bad : &k_text); }
        CELL_(170) {
            if (bytes) {
                textf(&k_text, "%llu (%.1f KiB)", (unsigned long long)counter, (double)counter / 1024.0);
            } else {
                textf(&k_text, "%llu", (unsigned long long)counter);
            }
        }
        if (counter != recount) {
            textf(&k_bad, "records give %llu", (unsigned long long)recount);
        }
    }
}

static void declare_counters(const fi_frame_t *fr) {
    const nt_gfx_counters_t *c = &fr->view.counters;
    static fi_counter_row_t rows[FI_COUNTER_ROWS];
    const uint32_t n = fi_counter_rows(fr, rows);
    section("Counters check");
    text("The frame's counters against a recount from its", &k_dim);
    text("records: a difference is a capture or inspector bug.", &k_dim);
    if (fr->view.overflow) {
        text("events truncated, counters complete: no check", &k_warn);
    }
    ROW_(8) {
        CELL_(190) { text("stream bytes", &k_text); }
        textf(&k_text, "%u (counter only: command sizes are not recorded)", c->stream_bytes);
    }
    for (int pass = 0; pass < 2; pass++) { /* differences first */
        for (uint32_t k = 0; k < n; k++) {
            const bool differs = !fr->view.overflow && rows[k].counter != rows[k].recount;
            if (differs == (pass == 0) && (rows[k].counter != 0 || rows[k].recount != 0)) {
                counter_row(rows[k].name, rows[k].counter, fr->view.overflow ? rows[k].counter : rows[k].recount, rows[k].bytes);
            }
        }
    }
}
// #endregion

// #region keys
static bool is_op_in(const fi_frame_t *fr, uint32_t id, uint32_t lo, uint32_t hi) {
    return id < fr->view.count && fr->view.events[id].kind == NT_GFX_EVENT_OP && fr->view.events[id].operation >= lo && fr->view.events[id].operation <= hi;
}

/* Moves the selection to the next visible row (dir +1/-1) that is an operation in [lo, hi]. */
static void jump(int dir, uint32_t lo, uint32_t hi) {
    const fi_frame_t *fr = frame_sel();
    const uint32_t at = vis_index_of(s.selected);
    int64_t k = (int64_t)at;
    if (at == FI_NONE) {
        k = dir > 0 ? -1 : (int64_t)s.vis_count;
    }
    for (k += dir; k >= 0 && k < (int64_t)s.vis_count; k += dir) {
        if (is_op_in(fr, s.vis[k], lo, hi)) {
            s.anchor = FI_NONE;
            s.selected = s.vis[k];
            s.follow = true;
            return;
        }
    }
}

static void move(int64_t delta) {
    if (s.vis_count == 0) {
        return;
    }
    const bool shift = s_keys.shift;
    if (!shift) {
        s.anchor = FI_NONE;
    } else if (s.anchor == FI_NONE) {
        s.anchor = s.selected;
    }
    const uint32_t at = vis_index_of(s.selected);
    int64_t k = (int64_t)at + delta;
    if (at == FI_NONE) { /* from before the first row going down, from after the last going up */
        k = delta > 0 ? delta - 1 : (int64_t)s.vis_count + delta;
    }
    k = k < 0 ? 0 : k;
    k = k >= (int64_t)s.vis_count ? (int64_t)s.vis_count - 1 : k;
    s.selected = s.vis[k];
    s.follow = true;
}

/* The selected record and the records it caused, as capture text, for an issue or a PR. */
static void copy_selection(const fi_frame_t *fr) {
    static char out[32768];
    size_t at = 0;
    for (uint32_t i = s.selected; i < fr->view.count && at + 64U < sizeof(out); i++) {
        if (i != s.selected && fr->parent[i] != s.selected) {
            continue;
        }
        const char *line = NULL;
        const size_t len = fi_record_text(fr, i, &line);
        const size_t room = sizeof(out) - at - 32U; /* the prefix and a cut mark */
        const int n = snprintf(out + at, sizeof(out) - at, "%s@%u %.*s%s\n", i == s.selected ? "" : "  ", i, (int)(len < room ? len : room), line, len < room ? "" : " ...");
        at += n > 0 && (size_t)n < sizeof(out) - at ? (size_t)n : sizeof(out) - at - 1U;
    }
    nt_clipboard_set_text(out);
}

static void step_frame(int dir) {
    int64_t f = s.file;
    int64_t k = (int64_t)s.frame + dir;
    while (k < 0 && f > 0) {
        f--;
        k += s.files[f].frame_count;
    }
    while (f < (int64_t)s.file_count && k >= (int64_t)s.files[f].frame_count) {
        k -= s.files[f].frame_count;
        f++;
    }
    if (k >= 0 && f < (int64_t)s.file_count) {
        remember();
        select_frame((uint32_t)f, (uint32_t)k);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void handle_keys(void) {
    if (s.file_count == 0 || nt_ui_input_any_focused(s_ctx)) {
        return;
    }
    const bool shift = s_keys.shift;
    const bool ctrl = s_keys.ctrl;
    const fi_frame_t *fr = frame_sel();
    const bool alt = s_keys.alt;
    if (ctrl && (s_keys.pressed[NT_KEY_ARROW_LEFT] || s_keys.pressed[NT_KEY_ARROW_RIGHT])) {
        step_frame(s_keys.pressed[NT_KEY_ARROW_LEFT] ? -1 : 1);
        return;
    }
    if (alt && (s_keys.pressed[NT_KEY_ARROW_LEFT] || s_keys.pressed[NT_KEY_ARROW_RIGHT])) {
        if (s_keys.pressed[NT_KEY_ARROW_LEFT]) {
            go_back();
        } else {
            go_forward();
        }
        return;
    }
    if (!frame_ok(fr) || s.page != PAGE_COMMANDS) { /* the rest act on the command list */
        return;
    }
    if (s_keys.pressed[NT_KEY_ARROW_DOWN]) {
        move(1);
    }
    if (s_keys.pressed[NT_KEY_ARROW_UP]) {
        move(-1);
    }
    if (s_keys.pressed[NT_KEY_PAGE_DOWN]) {
        move(30);
    }
    if (s_keys.pressed[NT_KEY_PAGE_UP]) {
        move(-30);
    }
    if (s_keys.pressed[NT_KEY_HOME]) {
        move(-(int64_t)s.vis_count);
    }
    if (s_keys.pressed[NT_KEY_END]) {
        move((int64_t)s.vis_count);
    }
    const uint32_t at = vis_index_of(s.selected);
    if (at != FI_NONE && s.vis_row[at] != FI_NONE && (s_keys.pressed[NT_KEY_ARROW_LEFT] || s_keys.pressed[NT_KEY_ARROW_RIGHT])) {
        s.collapsed[s.vis_row[at]] = s_keys.pressed[NT_KEY_ARROW_LEFT] ? 1U : 0U;
        s.dirty = true;
    }
    if (s_keys.pressed[NT_KEY_BACKSPACE] && s.selected != FI_NONE && fi_parent(fr, s.selected) != FI_NONE) {
        select_record(fi_parent(fr, s.selected));
    }
    if (s_keys.pressed[NT_KEY_ENTER] && s.selected < fr->view.count) {
        if (fi_joined(fr, s.selected) != FI_NONE) {
            select_record(fi_joined(fr, s.selected));
        } else if (fr->cause[s.selected] != FI_NONE) {
            select_record(fr->cause[s.selected]);
        }
    }
    if (s_keys.pressed[NT_KEY_D]) {
        jump(shift ? -1 : 1, NT_GFX_OP_DRAW, NT_GFX_OP_DRAW_INDEXED_INSTANCED);
    }
    if (s_keys.pressed[NT_KEY_P]) {
        jump(shift ? -1 : 1, NT_GFX_OP_PASS, NT_GFX_OP_PASS);
    }
    if (s_keys.pressed[NT_KEY_ESCAPE]) {
        clear_filters();
    }
    if (ctrl && s_keys.pressed[NT_KEY_C] && s.selected < fr->view.count) {
        s.copy_requested = true;
    }
}

/* What the keys do on this page, or the hovered frame's numbers. */
static void declare_status(void) {
    if (s.page == PAGE_FRAMES && s.hover_file != FI_NONE && s.hover_file < s.file_count) {
        const fi_frame_t *fr = &s.files[s.hover_file].frames[s.hover_frame];
        textf(&k_text, "frame %llu: %llu GL draw calls, %llu GL calls, %llu KiB uploaded, %u records  (click: pick, again: open)", (unsigned long long)fr->view.counters.frame_sequence,
              (unsigned long long)track_value(fr, TRACK_DRAW_CALLS), (unsigned long long)track_value(fr, TRACK_GL_CALLS), (unsigned long long)track_value(fr, TRACK_UPLOAD), fr->view.count);
        return;
    }
    if (s.file_count == 0) {
        text("Open... picks capture files; frame_inspector file... opens them at start.", &k_dim);
        return;
    }
    switch ((page_t)s.page) {
    case PAGE_FRAMES:
        text("click: pick a frame, again: open it in Commands   wheel: zoom   drag, Shift+wheel or the strip: pan   fit: all frames   Ctrl+Left/Right: previous/next frame", &k_dim);
        break;
    case PAGE_COMMANDS: {
        const fi_frame_t *fr = frame_sel();
        const bool draw = s.selected < fr->view.count && is_mergeable_draw(&fr->view.events[s.selected]);
        textf(&k_dim, "%sUp/Down select  -/+ or Left/Right fold  D/P next draw/pass (Shift: back)  Backspace parent  Alt+Left/Right back/fwd (pages too)  Ctrl+C copy  Esc clear filter",
              draw ? "Enter: the draw it joined or what broke the merge   " : "");
        break;
    }
    case PAGE_INFO:
        text("click: pick a number, again: its records in Commands   Alt+Left: back", &k_dim);
        break;
    case PAGE_INIT:
    case PAGE_END:
    case PAGE_COUNT:
        text("click: pick a record, again: open it in Commands   Alt+Left: back", &k_dim);
        break;
    }
}
// #endregion

// #region frame
/* The selected frame, with previous/next. */
static void declare_frame_picker(void) {
    text("  ", &k_dim);
    if (button("<", &k_text, false)) {
        step_frame(-1);
    }
    if (button(">", &k_text, false)) {
        step_frame(1);
    }
    const fi_frame_t *fr = frame_sel();
    textf(&k_head, "frame %llu", (unsigned long long)fr->view.counters.frame_sequence);
    textf(&k_dim, "(%u/%u in %s, %s)", s.frame + 1U, s.files[s.file].frame_count, s.files[s.file].name, frame_ok(fr) && fr->view.platform == NT_GFX_CAPTURE_WEB ? "web" : "native");
    if (frame_ok(fr)) {
        declare_banners(fr);
    }
}

static void declare_menu(void) {
    static const char *const k_pages[PAGE_COUNT] = {"Frames", "Commands", "Information", "Init state", "End state"};
    ROW_(6) {
        if (button("Open...", &k_link, false)) {
            s.open_requested = true;
        }
        if (button("< back", s.history_count > 0 ? &k_text : &k_dim, false)) {
            go_back();
        }
        if (button("fwd >", s.future_count > 0 ? &k_text : &k_dim, false)) {
            go_forward();
        }
        text("  ", &k_dim);
        for (int p = 0; p < PAGE_COUNT; p++) {
            if (button(k_pages[p], &k_head, s.page == p) && s.file_count > 0) {
                s.page = p;
            }
        }
        if (s.file_count > 0) {
            declare_frame_picker();
        }
    }
}

static void declare_ui(void) {
    s.cell_seq = 0;
    CLAY({.id = CLAY_ID("root"),
          .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .padding = CLAY_PADDING_ALL(6), .childGap = 6},
          .backgroundColor = k_bg}) {
        declare_menu();
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .childGap = 6}}) {
            if (s.file_count == 0 || s.page == PAGE_FRAMES) {
                CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}}) { declare_frames(); }
            } else if (!frame_ok(frame_sel())) {
                const fi_frame_t *fr = frame_sel();
                textf(&k_bad, "frame rejected: %s at line %u:%u", fr->error != NULL ? fr->error : "?", fr->error_line, fr->error_col);
            } else if (s.page == PAGE_COMMANDS) {
                declare_tree();
                declare_selection();
            } else if (s.page == PAGE_INFO) {
                declare_info_page();
            } else {
                declare_frame_state(frame_sel(), s.page == PAGE_END);
            }
        }
        ROW_(12) { declare_status(); }
    }
}

static void try_bind(void) {
    if (!s_bound_atlas && nt_resource_is_ready(s_atlas)) {
        const uint32_t white = nt_atlas_find_region(s_atlas, ASSET_ATLAS_REGION_FRAME_INSPECTOR_ATLAS__WHITE.value);
        NT_ASSERT(white != NT_ATLAS_INVALID_REGION);
        nt_ui_set_atlas_white_region(s_ctx, s_atlas, white);
        s_bound_atlas = true;
    }
    if (!s_bound_font && nt_resource_is_ready(s_font_res)) {
        nt_font_add(s_font, s_font_res);
        nt_ui_set_font(s_ctx, 0U, s_font);
        s_bound_font = true;
    }
    if (nt_program_ref_update(&s_sprite_program)) {
        nt_material_set_program(s_sprite_material, s_sprite_program.program);
    }
    if (nt_program_ref_update(&s_text_program)) {
        nt_material_set_program(s_text_material, s_text_program.program);
    }
}

/* Side effects the UI asked for, run between frames; a monkey run drops them. */
static void run_requests(void) {
    if (!s_monkey.on && s.open_requested) {
        open_files(); /* the dialog blocks the loop until it closes */
    }
    if (!s_monkey.on && s.copy_requested && s.file_count > 0) {
        copy_selection(frame_sel());
    }
    if (!s_monkey.on && s.close_file != FI_NONE) {
        close_file(s.close_file);
    }
    s.open_requested = false;
    s.copy_requested = false;
    s.close_file = FI_NONE;
}

static void frame(void) {
    run_requests();
    nt_window_poll();
    nt_gfx_begin_frame();
    nt_input_poll();
    read_keys();
    if (s_monkey.on) {
        monkey_frame_time();
        monkey_input(s.fb_w > 0.0F ? s.fb_w : 1600.0F, (float)(g_nt_window.fb_height > 0 ? g_nt_window.fb_height : 900));
    }
    nt_arena_reset(&g_nt_mem_scratch);
    if (s.dirty) {
        rebuild_rows(); /* keys move over the rows the last click left */
    }
    handle_keys();
    nt_resource_step();
    try_bind();
    nt_font_step();
    if (s.dirty) {
        rebuild_rows();
    }
    const float fb_w = (float)(g_nt_window.fb_width > 0 ? g_nt_window.fb_width : 1600);
    const float fb_h = (float)(g_nt_window.fb_height > 0 ? g_nt_window.fb_height : 900);
    s.fb_w = fb_w;
    const nt_ui_scale_t scale = nt_ui_compute_scale(&(nt_ui_scale_desc_t){.ref_w = fb_w, .ref_h = fb_h, .mode = NT_UI_SCALE_EXPAND}, fb_w, fb_h);
    const nt_ui_scale_ortho_t ortho = nt_ui_scale_ortho(&scale);
    mat4 proj;
    mat4 view;
    glm_ortho(ortho.left, ortho.right, ortho.bottom, ortho.top, -1.0F, 1.0F, proj);
    glm_mat4_identity(view);
    nt_frame_uniforms_t uniforms = {0};
    memcpy(uniforms.view_proj, proj, 64);
    memcpy(uniforms.view, view, 64);
    memcpy(uniforms.proj, proj, 64);
    uniforms.resolution[0] = fb_w;
    uniforms.resolution[1] = fb_h;
    uniforms.resolution[2] = 1.0F / fb_w;
    uniforms.resolution[3] = 1.0F / fb_h;
    uniforms.near_far[0] = -1.0F;
    uniforms.near_far[1] = 1.0F;

    const nt_material_info_t *sprite = nt_material_get_info(s_sprite_material);
    const nt_material_info_t *txt = nt_material_get_info(s_text_material);
    const bool can_render = s_bound_atlas && s_bound_font && sprite != NULL && nt_gfx_program_ready(sprite->program) && txt != NULL && nt_gfx_program_ready(txt->program);
    /* The wheel over the tracks (last frame's layout) belongs to the frame axis: the UI gets the pointer without it. */
    nt_pointer_t pointer = g_nt_input.pointers[g_nt_input.primary];
    const nt_ui_bbox_t tracks = nt_ui_get_bbox(s_ctx, CLAY_ID("tracks").id);
    const bool over = s.page == PAGE_FRAMES && tracks.found && pointer.x >= tracks.x && pointer.x < tracks.x + tracks.width && pointer.y >= tracks.y && pointer.y < tracks.y + tracks.height;
    s.track_wheel = over && !s_keys.shift ? pointer.wheel_dy : 0.0F;
    s.track_pan = over ? pointer.wheel_dx - (s_keys.shift ? pointer.wheel_dy : 0.0F) : 0.0F;
    if (over) {
        pointer.wheel_dx = 0.0F;
        pointer.wheel_dy = 0.0F;
    }
    if (can_render) {
        nt_ui_begin(s_ctx, scale.logical_w, scale.logical_h, g_nt_app.dt, &pointer, 1, 0);
        nt_ui_set_viewport(s_ctx, nt_ui_viewport_from_scale(&scale));
        declare_ui();
        nt_ui_end(s_ctx);
    }
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {k_bg.r / 255.0F, k_bg.g / 255.0F, k_bg.b / 255.0F, 1.0F}, .clear_depth = 1.0F});
    if (can_render) {
        nt_gfx_bind_uniform_block(NT_GFX_SLOT_GLOBALS, &uniforms, sizeof(uniforms));
        const nt_ui_target_t target = nt_ui_scale_make_target(&scale);
        nt_ui_walk(s_ctx, &target);
    }
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_window_swap_buffers();
}
// #endregion

// #region main
/* The frame of a file whose capture sequence is `sequence` (the "frame sequence=" of its header). */
static bool find_sequence(uint64_t sequence, uint32_t *file, uint32_t *frame) {
    for (uint32_t f = 0; f < s.file_count; f++) {
        for (uint32_t k = 0; k < s.files[f].frame_count; k++) {
            if (s.files[f].frames[k].view.counters.frame_sequence == sequence) {
                *file = f;
                *frame = k;
                return true;
            }
        }
    }
    return false;
}

/* Files to open, --monkey SECONDS [SEED], and where the window opens: --frame SEQUENCE (the capture's frame
 * sequence), --page frames|commands|info|init|end, --select RECORD (its @ number), --filter TEXT. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void parse_args(int argc, char *argv[]) {
    static const char *const k_pages[PAGE_COUNT] = {"frames", "commands", "info", "init", "end"};
    const char *frame = NULL;
    const char *page = NULL;
    const char *select = NULL;
    const char *filter = NULL;
    for (int a = 1; a < argc; a++) {
        const char *arg = argv[a];
        const bool value = a + 1 < argc;
        if (strcmp(arg, "--frame") == 0 && value) {
            frame = argv[++a];
        } else if (strcmp(arg, "--page") == 0 && value) {
            page = argv[++a];
        } else if (strcmp(arg, "--select") == 0 && value) {
            select = argv[++a];
        } else if (strcmp(arg, "--filter") == 0 && value) {
            filter = argv[++a];
        } else if (strcmp(arg, "--monkey") == 0 && value) {
            s_monkey.on = true;
            s_monkey.seconds = strtod(argv[++a], NULL);
            s_monkey.seed = 1U;
            if (a + 1 < argc && isdigit((unsigned char)argv[a + 1][0])) {
                s_monkey.seed = (uint32_t)strtoul(argv[++a], NULL, 10);
            }
            s_monkey.seed = s_monkey.seed != 0 ? s_monkey.seed : 1U;
        } else if (strncmp(arg, "--", 2) == 0) {
            nt_log_error("frame_inspector: unknown option %s (--frame N, --page frames|commands|info|init|end, --select N, --filter TEXT, --monkey S [SEED])", arg);
        } else {
            add_file(arg);
        }
    }
    if (s.file_count == 0) {
        return;
    }
    uint32_t file = 0;
    uint32_t at = 0;
    if (frame != NULL && find_sequence(strtoull(frame, NULL, 10), &file, &at)) {
        select_frame(file, at);
    } else if (frame != NULL) {
        nt_log_error("frame_inspector: no frame with sequence %s in the open files", frame);
    }
    if (filter != NULL) {
        (void)snprintf(s.filter, sizeof(s.filter), "%s", filter);
        s.dirty = true;
    }
    const fi_frame_t *fr = frame_sel();
    const uint32_t record = select != NULL ? (uint32_t)strtoul(select[0] == '@' ? select + 1 : select, NULL, 10) : FI_NONE;
    if (select != NULL && frame_ok(fr) && record < fr->view.count) {
        reveal(record);
        s.page = PAGE_COMMANDS;
    } else if (select != NULL) {
        nt_log_error("frame_inspector: frame %llu has no record %s", (unsigned long long)fr->view.counters.frame_sequence, select);
    }
    for (int p = 0; page != NULL && p < PAGE_COUNT; p++) {
        s.page = strcmp(page, k_pages[p]) == 0 ? p : s.page;
    }
}

static nt_material_t make_material(const char *label) {
    return nt_material_create(&(nt_material_create_desc_t){.blend = nt_blend_alpha_premultiplied(), .depth_test = false, .depth_write = false, .cull_mode = NT_CULL_NONE, .label = label});
}

int main(int argc, char *argv[]) {
    nt_engine_config_t config = {.app_name = "frame_inspector"};
    s.anchor = FI_NONE;
    s.axis_fit = true;
    s.flash = FI_NONE;
    s.close_file = FI_NONE;
    s.hover_file = FI_NONE;
    if (nt_engine_init(&config) != NT_OK) {
        return 1;
    }
    nt_fs_init();
    parse_args(argc, argv);

    g_nt_window.width = 1600;
    g_nt_window.height = 900;
    nt_window_init();
    nt_input_init();
    nt_gfx_desc_t gfx = nt_gfx_desc_defaults();
    gfx.frame_capacity[NT_GFX_FRAME_VERTEX] = 4U * 1024U * 1024U;
    gfx.frame_capacity[NT_GFX_FRAME_INDEX] = 1024U * 1024U;
    nt_gfx_init(&gfx);
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_resource_init(&(nt_resource_desc_t){0});
    nt_arena_init(&g_nt_mem_scratch, s_scratch_mem, sizeof s_scratch_mem);
    nt_resource_register_type(NT_ASSET_TEXTURE, &(nt_resource_type_desc_t){.activate = nt_gfx_activate_texture, .deactivate = nt_gfx_deactivate_texture});
    nt_resource_register_type(NT_ASSET_SHADER_CODE, &(nt_resource_type_desc_t){.activate = nt_gfx_activate_shader, .deactivate = nt_gfx_deactivate_shader});
    nt_atlas_init();
    nt_material_init(&(nt_material_desc_t){.max_materials = 2});
    nt_font_init(&(nt_font_desc_t){.max_fonts = 1});
    nt_ui_module_init();
    nt_ui_create_desc_t ui = nt_ui_create_desc_defaults();
    ui.max_elements = 16384U;
    ui.state_slots = 2048U;
    const size_t arena = nt_ui_min_arena_size(&ui);
    s_ui_arena = malloc(arena);
    NT_ASSERT(s_ui_arena != NULL && ((uintptr_t)s_ui_arena % NT_UI_ARENA_ALIGN) == 0);
    s_ctx = nt_ui_create_context(s_ui_arena, arena, &ui);
    NT_ASSERT(s_ctx != NULL);

    /* The pack sits next to the executable, so the inspector opens files from any directory. */
    char pack[1024];
    const char *end = NULL;
    for (const char *c = argv[0]; *c != '\0'; c++) {
        end = *c == '/' || *c == '\\' ? c : end;
    }
    (void)snprintf(pack, sizeof(pack), "%.*sassets/frame_inspector.ntpack", end != NULL ? (int)(end - argv[0] + 1) : 0, argv[0]);
    const nt_hash32_t pack_id = nt_hash32_str("frame_inspector");
    nt_resource_mount(pack_id, 100);
    nt_resource_load_auto(pack_id, pack);
    s_sprite_program = (nt_program_ref_t){.vs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SPRITE_VERT, NT_ASSET_SHADER_CODE),
                                          .fs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SPRITE_FRAG, NT_ASSET_SHADER_CODE),
                                          .label = "frame_inspector/sprite"};
    s_text_program = (nt_program_ref_t){.vs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_VERT, NT_ASSET_SHADER_CODE),
                                        .fs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_FRAG, NT_ASSET_SHADER_CODE),
                                        .label = "frame_inspector/text"};
    s_atlas = nt_resource_request(ASSET_ATLAS_FRAME_INSPECTOR_ATLAS, NT_ASSET_ATLAS);
    s_font_res = nt_resource_request(ASSET_FONT_FRAME_INSPECTOR_MONO, NT_ASSET_FONT);
    s_sprite_material = make_material("frame_inspector_sprite");
    s_text_material = make_material("frame_inspector_text");
    nt_ui_set_sprite_material(s_ctx, s_sprite_material);
    nt_ui_set_text_material(s_ctx, s_text_material);
    s_font = nt_font_create(&(nt_font_create_desc_t){.max_glyphs = 128, .measure_cache_size = 1024});
    nt_resource_set_activate_time_budget(0);

    nt_app_run(frame);
    if (s_monkey.on) {
        (void)printf("monkey: %u frames, %.2f ms average, %.2f ms worst\n", s_monkey.frames, s_monkey.frames > 0 ? s_monkey.total_ms / s_monkey.frames : 0.0, s_monkey.worst_ms);
    }

    nt_ui_destroy_context(s_ctx);
    free(s_ui_arena);
    nt_ui_module_shutdown();
    nt_text_renderer_shutdown();
    nt_sprite_renderer_shutdown();
    nt_font_destroy(s_font);
    nt_font_shutdown();
    nt_material_destroy(s_sprite_material);
    nt_material_destroy(s_text_material);
    for (uint32_t f = 0; f < s.file_count; f++) {
        fi_file_close(&s.files[f]);
    }
    free(s.collapsed);
    free(s.vis);
    free(s.vis_row);
    free(s.vis_depth);
    nt_gfx_shutdown();
    nt_input_shutdown();
    nt_window_shutdown();
    nt_fs_shutdown();
    nt_engine_shutdown();
    return 0;
}
// #endregion
