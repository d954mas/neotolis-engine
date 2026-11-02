/* Frame inspector: opens capture files (render architecture, "Capture text") and shows what each recorded
 * frame did. Usage: frame_inspector [file...]; Open... adds more. Every frame of a file is parsed on open. */

#include "app/nt_app.h"
#include "atlas/nt_atlas.h"
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
#include "ui/nt_ui.h"
#include "ui/nt_ui_input.h"
#include "ui/nt_ui_label.h"
#include "ui/nt_ui_scale.h"
#include "ui/nt_ui_scroll.h"
#include "ui/nt_ui_vlist.h"
#include "window/nt_window.h"

#include <ctype.h>
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
#define TRACK_LABEL_W 300.0F

// #region palette
static const Clay_Color k_bg = {24, 26, 30, 255};
static const Clay_Color k_panel = {32, 35, 41, 255};
static const Clay_Color k_hover = {48, 52, 62, 255};
static const Clay_Color k_select = {44, 74, 120, 255};
static const Clay_Color k_bar = {110, 116, 130, 255};
static const Clay_Color k_bar_bad = {200, 70, 60, 255};
static const Clay_Color k_bar_warn = {220, 150, 50, 255};
static const Clay_Color k_bar_sel = {90, 160, 255, 255};
static const Clay_Color k_hover_bar = {170, 176, 190, 255};
static const Clay_Color k_draw_bg = {34, 52, 82, 255}; /* draws and clears, as in Spector */
static const Clay_Color k_select_border = {150, 200, 255, 255};
#define TEXT_(r, g, b) {.font_id = 0, .font_size = FONT, .color = NT_RGBA8(r, g, b, 255), .wrap_mode = CLAY_TEXT_WRAP_NONE}
static const nt_ui_label_style_t k_text = TEXT_(220, 222, 228);
static const nt_ui_label_style_t k_dim = TEXT_(140, 145, 155);
static const nt_ui_label_style_t k_head = TEXT_(255, 255, 255);
static const nt_ui_label_style_t k_link = TEXT_(120, 180, 255);
static const nt_ui_label_style_t k_bad = TEXT_(255, 110, 100);
static const nt_ui_label_style_t k_warn = TEXT_(240, 180, 80);
static const nt_ui_label_style_t k_note = TEXT_(140, 210, 140);
static const nt_ui_label_style_t k_marker = TEXT_(230, 210, 120);
static const nt_ui_label_style_t k_backend = TEXT_(150, 165, 190);
static const nt_ui_label_style_t k_state = TEXT_(130, 215, 140);
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

static struct {
    fi_file_t files[MAX_FILES];
    uint32_t file_count;
    uint32_t file, frame; /* the selected frame */
    uint32_t hover_file, hover_frame;
    uint32_t selected; /* the selected row id: a record or FI_NODE_BASE + node */
    view_t view;
    bool draws_only, problems, state_all;
    char filter[64];
    fi_query_t query;
    char query_label[96];
    int page;
    uint32_t *group_draws;  /* per NOTE/MARKER_PUSH record: draw requests inside it */
    uint32_t *group_gl;     /* ... GL draw calls they issued */
    uint32_t *group_merged; /* ... draws merged into a previous one */
    /* Short names for object handles (pipe#3), numbered per kind in order of first appearance. */
    uint32_t *alias_object;
    uint8_t *alias_kind;
    uint32_t *alias_number;
    uint32_t alias_count;
    uint32_t history[HISTORY]; /* rows jumped from, for Back */
    uint32_t history_count;
    uint32_t future[HISTORY];
    uint32_t future_count;
    double axis_first; /* the frame axis: first visible frame index and how many are visible (0: fit) */
    double axis_count;
    double drag_first;
    uint8_t *collapsed; /* per logical row of the selected frame */
    uint32_t *vis;      /* visible row ids */
    uint32_t *vis_row;  /* their logical row, FI_NONE in flat lists */
    uint16_t *vis_depth;
    uint32_t vis_count;
    bool dirty;
    bool follow; /* scroll the tree to the selection */
    bool open_requested;
    uint32_t close_file; /* the file whose x was clicked, closed after the frame's UI; FI_NONE */
    uint32_t cell_seq;
    float fb_w;
} s;

static nt_ui_context_t *s_ctx;
static void *s_ui_arena;
static nt_resource_t s_atlas;
static nt_resource_t s_font_res;
static nt_font_t s_font;
static bool s_bound_atlas, s_bound_font;
static nt_material_t s_sprite_material, s_text_material;
static nt_program_ref_t s_sprite_program, s_text_program;

static fi_frame_t *frame_sel(void) { return &s.files[s.file].frames[s.frame]; }
static bool frame_ok(const fi_frame_t *fr) { return fr->view.events != NULL; }
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

static bool is_draw_call(uint32_t call) {
    const char *name = nt_gfx_gl_call_name(call);
    return name != NULL && strncmp(name, "glDraw", 6) == 0;
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
static void alias_of(uint32_t kind, uint32_t object, char *buf, size_t size) {
    for (uint32_t k = 0; k < s.alias_count; k++) {
        if (s.alias_object[k] == object && s.alias_kind[k] == kind) {
            (void)snprintf(buf, size, "%s#%u", alias_prefix(kind), s.alias_number[k]);
            return;
        }
    }
    (void)snprintf(buf, size, "%s:%u", alias_prefix(kind), object);
}

static uint32_t kind_by_name(const char *name, size_t n) {
    for (uint32_t k = 1; k < FI_OBJECTS; k++) {
        const char *known = nt_gfx_object_kind_name(k);
        if (known != NULL && strlen(known) == n && memcmp(known, name, n) == 0) {
            return k;
        }
    }
    return 0;
}

static void append(char *buf, size_t size, size_t *at, const char *src, size_t n) {
    for (size_t i = 0; i < n && *at + 1 < size; i++) {
        buf[(*at)++] = src[i];
    }
    buf[*at] = '\0';
}

static bool token_is(const char *t, size_t n, const char *prefix) { return n >= strlen(prefix) && memcmp(t, prefix, strlen(prefix)) == 0; }

/* The record's capture-text line without what the row shows elsewhere: the kind tag of operations and
 * GL calls, op=STATE, the result (its own column) and the link to the row's parent. GL arguments read
 * as a call: glBindVertexArray(3). */
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

    bool after_call = false;
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
        i = j + 1;
        const bool tag = i - n - 1 == 0 && (e->kind == NT_GFX_EVENT_OP || call);
        const bool drop = tag || token_is(t, n, "result=") || (call && token_is(t, n, "op=")) || (n == strlen(parent_link) && memcmp(t, parent_link, n) == 0);
        if (drop) {
            continue;
        }
        if (token_is(t, n, "op=") || token_is(t, n, "call=")) {
            const size_t skip = t[0] == 'o' ? 3 : 5;
            if (at > 0 && buf[at - 1] != ' ') {
                append(buf, size, &at, " ", 1);
            }
            append(buf, size, &at, t + skip, n - skip);
            after_call = t[0] == 'c';
            continue;
        }
        if (after_call && token_is(t, n, "args=[") && t[n - 1] == ']') {
            /* GL arguments read as a call, enums by name: glEnable(GL_SCISSOR_TEST). */
            append(buf, size, &at, "(", 1);
            uint32_t index = 0;
            for (const char *a = t + 6; a < t + n - 1;) {
                const char *comma = memchr(a, ',', (size_t)(t + n - 1 - a));
                const char *end = comma != NULL ? comma : t + n - 1;
                char number[24] = {0};
                memcpy(number, a, (size_t)(end - a) < sizeof(number) - 1U ? (size_t)(end - a) : sizeof(number) - 1U);
                char decoded[48];
                const char *name = arg_enum(e->detail, index++, (uint32_t)strtoul(number, NULL, 10), decoded, sizeof(decoded));
                append(buf, size, &at, name != NULL ? name : a, name != NULL ? strlen(name) : (size_t)(end - a));
                if (comma != NULL) {
                    append(buf, size, &at, ",", 1);
                }
                a = end + 1;
            }
            append(buf, size, &at, ")", 1);
            after_call = false;
            continue;
        }
        if (token_is(t, n, "obj=") || token_is(t, n, "vertex_input=")) {
            /* Handles read as short names: obj=PIPELINE:65544 becomes pipe#3. */
            const char *colon = memchr(t, ':', n);
            const bool obj = t[0] == 'o';
            const uint32_t kind = obj && colon != NULL ? kind_by_name(t + 4, (size_t)(colon - t - 4)) : NT_GFX_OBJECT_VERTEX_INPUT;
            const char *digits = obj && colon != NULL ? colon + 1 : t + 13;
            char alias[32];
            alias_of(kind, (uint32_t)strtoul(digits, NULL, 10), alias, sizeof(alias));
            append(buf, size, &at, " ", 1);
            append(buf, size, &at, alias, strlen(alias));
            continue;
        }
        if (e->kind == NT_GFX_EVENT_OP && (token_is(t, n, "first=") || token_is(t, n, "vertices=") || token_is(t, n, "instances=1") || token_is(t, n, "joined="))) {
            if (token_is(t, n, "instances=") && !(n == 11 && t[10] == '1')) {
                append(buf, size, &at, " x", 2);
                append(buf, size, &at, t + 10, n - 10);
            }
            continue; /* draw details live in the selected-command panel */
        }
        after_call = false;
        if (at > 0) {
            append(buf, size, &at, " ", 1);
        }
        append(buf, size, &at, t, n);
    }
}

/* The badge column: blank when a request did its work; what CACHE means for this operation; skips; failures. */
static const char *result_text(const nt_gfx_event_t *e, const nt_ui_label_style_t **style) {
    *style = &k_warn;
    if (e->kind == NT_GFX_EVENT_SKIP) {
        return "skipped";
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
    if (s.draws_only) {
        const bool draw_op = e->kind == NT_GFX_EVENT_OP && e->operation >= NT_GFX_OP_DRAW && e->operation <= NT_GFX_OP_DRAW_INDEXED_INSTANCED;
        if (!draw_op && !(e->kind == NT_GFX_EVENT_BACKEND && is_draw_call(e->detail))) {
            return false;
        }
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

/* Per group (note or marker): draw requests, GL draws and merges inside it; and short names for handles. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void index_groups_and_aliases(const fi_frame_t *fr) {
    const uint32_t n = fr->view.count + 1U;
    free(s.group_draws);
    free(s.group_gl);
    free(s.group_merged);
    free(s.alias_object);
    free(s.alias_kind);
    free(s.alias_number);
    s.group_draws = (uint32_t *)calloc(n, sizeof(uint32_t));
    s.group_gl = (uint32_t *)calloc(n, sizeof(uint32_t));
    s.group_merged = (uint32_t *)calloc(n, sizeof(uint32_t));
    s.alias_object = (uint32_t *)calloc(n, sizeof(uint32_t));
    s.alias_kind = (uint8_t *)calloc(n, sizeof(uint8_t));
    s.alias_number = (uint32_t *)calloc(n, sizeof(uint32_t));
    NT_ASSERT(s.group_draws != NULL && s.group_gl != NULL && s.group_merged != NULL && s.alias_object != NULL && s.alias_kind != NULL && s.alias_number != NULL);
    s.alias_count = 0;
    uint32_t next[FI_OBJECTS] = {0};
    for (uint32_t i = 0; i < fr->view.count; i++) {
        const nt_gfx_event_t *e = &fr->view.events[i];
        const bool request = e->kind == NT_GFX_EVENT_OP && e->operation >= NT_GFX_OP_DRAW && e->operation <= NT_GFX_OP_DRAW_INDEXED_INSTANCED;
        const bool merged = request && e->result == NT_GFX_RESULT_CACHE;
        const bool issued = e->kind == NT_GFX_EVENT_BACKEND && is_draw_call(e->detail);
        for (uint32_t p = fr->parent[i]; (request || issued) && p < fr->view.count; p = fr->parent[p]) {
            if (fr->view.events[p].kind == NT_GFX_EVENT_NOTE || fr->view.events[p].kind == NT_GFX_EVENT_MARKER_PUSH) {
                s.group_draws[p] += request ? 1U : 0U;
                s.group_merged[p] += merged ? 1U : 0U;
                s.group_gl[p] += issued ? 1U : 0U;
            }
        }
        if (e->kind > NT_GFX_EVENT_ARGUMENT || e->object_kind == 0 || e->object_kind >= FI_OBJECTS || e->object == 0) {
            continue;
        }
        bool known = false;
        for (uint32_t k = 0; k < s.alias_count && !known; k++) {
            known = s.alias_object[k] == e->object && s.alias_kind[k] == e->object_kind;
        }
        if (!known) {
            s.alias_object[s.alias_count] = e->object;
            s.alias_kind[s.alias_count] = e->object_kind;
            s.alias_number[s.alias_count++] = ++next[e->object_kind];
        }
    }
}

static void select_frame(uint32_t file, uint32_t frame) {
    s.file = file;
    s.frame = frame;
    const fi_frame_t *fr = frame_sel();
    const uint32_t rows = fr->row_count + fr->view.count + 1U;
    free(s.collapsed);
    free(s.vis);
    free(s.vis_row);
    free(s.vis_depth);
    index_groups_and_aliases(fr);
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
    s.vis_count = 0;
    s.history_count = 0;
    s.future_count = 0;
    s.dirty = true;
}

/* The tree parent of a row id, FI_NONE at the top. */
static uint32_t parent_of(const fi_frame_t *fr, uint32_t id) {
    if (id < fr->view.count) {
        return fr->parent[id];
    }
    const bool sub = id == FI_NODE_BASE + FI_NODE_SNAPSHOT || id == FI_NODE_BASE + FI_NODE_PROGRAM_LINKS;
    return sub ? FI_NODE_BASE + FI_NODE_BEGIN_FRAME : FI_NONE;
}

/* Selects a row, unfolding its ancestors so the tree shows it. */
static void reveal(uint32_t id) {
    const fi_frame_t *fr = frame_sel();
    for (uint32_t p = parent_of(fr, id); p != FI_NONE; p = parent_of(fr, p)) {
        for (uint32_t r = 0; r < fr->row_count; r++) {
            if (fr->row[r] == p) {
                s.collapsed[r] = 0;
            }
        }
    }
    s.selected = id;
    s.dirty = true;
    s.follow = true;
}

/* A jump (a link, Enter, Backspace): Back returns to the row it left. */
static void select_record(uint32_t id) {
    if (s.selected != FI_NONE && s.selected != id) {
        if (s.history_count == HISTORY) {
            memmove(s.history, s.history + 1, (HISTORY - 1U) * sizeof(uint32_t));
            s.history_count--;
        }
        s.history[s.history_count++] = s.selected;
        s.future_count = 0;
    }
    reveal(id);
}

static void go_back(void) {
    if (s.history_count > 0 && s.future_count < HISTORY) {
        s.future[s.future_count++] = s.selected;
        reveal(s.history[--s.history_count]);
    }
}

static void go_forward(void) {
    if (s.future_count > 0 && s.history_count < HISTORY) {
        s.history[s.history_count++] = s.selected;
        reveal(s.future[--s.future_count]);
    }
}

/* Fold every operation and group, leaving the frame's phases open: the list then reads as an outline. */
static void fold_all(bool fold) {
    const fi_frame_t *fr = frame_sel();
    for (uint32_t r = 0; r < fr->row_count; r++) {
        const uint32_t id = fr->row[r];
        s.collapsed[r] = id >= FI_NODE_BASE ? (uint8_t)(id == FI_NODE_BASE + FI_NODE_SNAPSHOT || id == FI_NODE_BASE + FI_NODE_PROGRAM_LINKS) : (uint8_t)fold;
    }
    s.dirty = true;
}

static void set_query(fi_query_t q, const char *label) {
    s.query = q;
    s.page = PAGE_COMMANDS;
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

static void close_file(uint32_t f) {
    fi_file_close(&s.files[f]);
    memmove(&s.files[f], &s.files[f + 1U], (s.file_count - f - 1U) * sizeof(fi_file_t));
    s.file_count--;
    if (s.file_count > 0) {
        const uint32_t keep = s.file > f ? s.file - 1U : s.file;
        select_frame(s.file == f || keep >= s.file_count ? 0U : keep, s.file == f ? 0U : s.frame);
    }
    s.dirty = true;
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

static bool same_query(fi_query_t a, fi_query_t b) { return a.kind == b.kind && a.a == b.a && a.b == b.b; }

/* A number that filters the tree to the records it counts. */
static void number_styled(fi_query_t q, const char *value, const char *label, const nt_ui_label_style_t *style) {
    if (fi_cell(frame_sel(), q).n == 0) {
        text(value, style == &k_big ? &k_big_dim : &k_dim);
        return;
    }
    if (button(value, style, same_query(s.query, q))) {
        set_query(q, label);
    }
}

static void number(fi_query_t q, uint64_t value, const char *label) {
    char buf[32];
    (void)snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
    number_styled(q, buf, label, &k_link);
}

/* A sentence that filters the tree to the records it describes. */
static void insight(fi_query_t q, const nt_ui_label_style_t *style, const char *fmt, ...) NT_PRINTF_ATTR(3, 4);
static void insight(fi_query_t q, const nt_ui_label_style_t *style, const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (button(buf, style, same_query(s.query, q))) {
        set_query(q, buf);
    }
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

static void axis_clamp(void) {
    const double most = (double)axis_frames();
    if (s.axis_count <= 0.0 || s.axis_count > most) {
        s.axis_count = most;
    }
    s.axis_count = s.axis_count < 4.0 && most >= 4.0 ? 4.0 : s.axis_count;
    s.axis_first = s.axis_first > most - s.axis_count ? most - s.axis_count : s.axis_first;
    s.axis_first = s.axis_first < 0.0 ? 0.0 : s.axis_first;
}

/* Wheel zooms around the cursor, a drag pans; both move every track at once. */
static void axis_input(const nt_ui_events_t *ev, nt_ui_bbox_t box) {
    if (!box.found || box.width <= 0.0F) {
        return;
    }
    const double at = ((double)ev->pos[0] - (double)box.x) / (double)box.width;
    const float wheel = g_nt_input.pointers[g_nt_input.primary].wheel_dy;
    if (ev->hovered && wheel != 0.0F) {
        const double cursor = s.axis_first + (at * s.axis_count);
        s.axis_count *= wheel > 0.0F ? 1.25 : 0.8;
        axis_clamp();
        s.axis_first = cursor - (at * s.axis_count);
        axis_clamp();
    }
    if (ev->pressed_now) {
        s.drag_first = s.axis_first;
    }
    if (ev->pressed) {
        s.axis_first = s.drag_first - ((double)ev->drag_dx / (double)box.width * s.axis_count);
        axis_clamp();
    }
}

/* One track, profiler style: the visible frames side by side over the width, each a filled column whose
 * height is the metric against its maximum over all files. When frames outnumber the columns, a column
 * shows the largest of its frames, so a spike never disappears. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void declare_track(uint32_t f, track_t t, uint64_t max, uint32_t hovered) {
    static const char *const k_names[TRACK_COUNT] = {"draw calls", "GL calls", "upload KiB", "records"};
    NT_ASSERT(t < TRACK_COUNT);
    const fi_file_t *file = &s.files[f];
    const uint32_t id = nt_ui_fmix_id(0x7A3C0001U, (f * TRACK_COUNT) + (uint32_t)t + 1U);
    const nt_ui_bbox_t box = nt_ui_get_bbox(s_ctx, id);
    const nt_ui_events_t ev = nt_ui_events(s_ctx, id, NULL);
    axis_input(&ev, box);
    const float width = box.found ? box.width : s.fb_w - TRACK_LABEL_W;
    const double frame_w = (double)width / s.axis_count;
    const uint32_t step = frame_w >= 2.0 ? 1U : (uint32_t)(2.0 / frame_w) + 1U;
    const uint32_t first = (uint32_t)s.axis_first;
    if (box.found && (ev.hovered || ev.clicked)) {
        const uint32_t k = (uint32_t)(s.axis_first + (((double)ev.pos[0] - (double)box.x) / (double)box.width * s.axis_count));
        if (k < file->frame_count) {
            s.hover_file = ev.hovered ? f : s.hover_file;
            s.hover_frame = ev.hovered ? k : s.hover_frame;
            if (ev.clicked && (ev.drag_dx < 3.0F && ev.drag_dx > -3.0F)) {
                select_frame(f, k);
                s.page = PAGE_COMMANDS;
            }
        }
    }
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childGap = 6, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) {
        CELL_(110) { text(k_names[t], &k_dim); }
        CELL_(80) {
            uint32_t at = f == s.file ? s.frame : FI_NONE;
            at = hovered != FI_NONE ? hovered : at;
            if (at != FI_NONE) {
                textf(hovered != FI_NONE ? &k_head : &k_text, "%llu", (unsigned long long)track_value(&file->frames[at], t));
            }
        }
        CELL_(90) { textf(&k_dim, "max %llu", (unsigned long long)max); }
        const float offset = (float)((s.axis_first - (double)first) * frame_w);
        CLAY({.id = (Clay_ElementId){.id = id},
              .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(TRACK_H)}, .childAlignment = {.y = CLAY_ALIGN_Y_BOTTOM}},
              .backgroundColor = k_bg,
              .clip = {.horizontal = true, .childOffset = {-offset, 0}}}) {
            const double end = s.axis_first + s.axis_count + 1.0;
            for (uint32_t k = first; k < file->frame_count && (double)k < end; k += step) {
                uint32_t pick = k; /* the column's largest frame */
                bool bad = false;
                bool warn = false;
                bool sel = false;
                bool hot = false;
                for (uint32_t j = k; j < k + step && j < file->frame_count; j++) {
                    const fi_frame_t *fr = &file->frames[j];
                    pick = track_value(fr, t) > track_value(&file->frames[pick], t) ? j : pick;
                    bad |= !frame_ok(fr) || fr->error != NULL;
                    warn |= frame_ok(fr) && (fr->merge_mismatch != FI_NONE || fr->view.overflow);
                    sel |= f == s.file && j == s.frame;
                    hot |= j == hovered;
                }
                Clay_Color color = hot ? k_hover_bar : k_bar;
                color = warn ? k_bar_warn : color;
                color = bad ? k_bar_bad : color;
                color = sel ? k_bar_sel : color;
                const float column = (float)(frame_w * step);
                const float gap = column >= 6.0F ? 1.0F : 0.0F;
                const bool label = column >= 34.0F; /* wide columns print their value over the bar */
                const float room = label ? TRACK_H - 16.0F : TRACK_H;
                const float h = bad ? room : 2.0F + ((room - 2.0F) * (float)track_value(&file->frames[pick], t) / (float)(max > 0 ? max : 1));
                CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(column), CLAY_SIZING_FIXED(TRACK_H)},
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

/* Every frame as a row: the numbers to compare at a glance when there are not too many frames. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void declare_frame_table(void) {
    static const float k_w[] = {300, 60, 70, 130, 70, 80, 90, 110, 80};
    static const char *const k_heads[] = {"file", "frame", "seq", "GL draw calls", "draws", "merged", "GL calls", "upload KiB", "problems"};
    CLAY({.layout = {.padding = {0, 0, 12, 2}}}) { text("All frames (click a row to open it)", &k_head); }
    ROW_(4) {
        for (uint32_t c = 0; c < sizeof(k_w) / sizeof(k_w[0]); c++) {
            CELL_(k_w[c]) { text(k_heads[c], &k_dim); }
        }
    }
    char name[64];
    for (uint32_t f = 0; f < s.file_count; f++) {
        short_name(s.files[f].name, name, sizeof(name));
        for (uint32_t k = 0; k < s.files[f].frame_count; k++) {
            const fi_frame_t *fr = &s.files[f].frames[k];
            const uint32_t id = next_id();
            const nt_ui_events_t ev = nt_ui_events(s_ctx, id, NULL);
            if (ev.clicked) {
                select_frame(f, k);
                s.page = PAGE_COMMANDS;
            }
            const bool sel = f == s.file && k == s.frame;
            const nt_ui_label_style_t *style = sel ? &k_head : &k_text;
            CLAY({.id = (Clay_ElementId){.id = id}, .layout = {.childGap = 4}, .backgroundColor = sel ? k_select : (ev.hovered ? k_hover : k_panel)}) {
                CELL_(k_w[0]) { text(name, style); }
                CELL_(k_w[1]) { textf(style, "%u", k + 1U); }
                CELL_(k_w[2]) { textf(style, "%llu", (unsigned long long)fr->view.counters.frame_sequence); }
                if (!frame_ok(fr)) {
                    textf(&k_bad, "rejected: %s", fr->error != NULL ? fr->error : "?");
                } else {
                    CELL_(k_w[3]) { textf(style, "%llu", (unsigned long long)fr->summary.draw_calls.n); }
                    CELL_(k_w[4]) { textf(style, "%llu", (unsigned long long)fr->summary.vertices.n); }
                    CELL_(k_w[5]) { textf(style, "%llu", (unsigned long long)fr->summary.merged.n); }
                    CELL_(k_w[6]) { textf(style, "%llu", (unsigned long long)fr->summary.gl_any.n); }
                    CELL_(k_w[7]) { textf(style, "%.1f", (double)fr->summary.upload[2].sum / 1024.0); }
                    CELL_(k_w[8]) { textf(fr->summary.problems.n > 0 ? &k_bad : style, "%llu", (unsigned long long)fr->summary.problems.n); }
                }
            }
        }
    }
}

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
    /* Hover is known from the previous frame's layout, so every track marks the same column. */
    const uint32_t hover_file = s.hover_file;
    const uint32_t hover_frame = s.hover_frame;
    s.hover_file = FI_NONE;
    const nt_ui_scroll_style_t scroll = nt_ui_scroll_style_defaults();
    nt_ui_scroll_begin(
        s_ctx, NULL, nt_ui_id("frames"), &scroll,
        &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .padding = CLAY_PADDING_ALL(8), .childGap = 4},
                                   .backgroundColor = k_panel});
    ROW_(10) {
        if (s.file_count > 0) {
            textf(&k_dim, "frames %u..%u of %u", (uint32_t)s.axis_first, (uint32_t)(s.axis_first + s.axis_count) - 1U, axis_frames());
            if (button("fit", &k_text, false)) {
                s.axis_count = 0.0;
                s.axis_first = 0.0;
            }
            text("wheel: zoom   drag: pan   hover: read every track   click: open the frame in Commands   blue: current frame   red: rejected   orange: overflow or merge rule stale", &k_dim);
        } else {
            text("Open a capture file (ntcapture text, one or more frames).", &k_dim);
        }
    }
    uint32_t frames = 0;
    for (uint32_t f = 0; f < s.file_count; f++) {
        declare_file_header(f);
        frames += s.files[f].frame_count;
        for (int t = 0; t < TRACK_COUNT; t++) {
            declare_track(f, (track_t)t, max[t], f == hover_file ? hover_frame : FI_NONE);
        }
    }
    if (frames <= 200U) {
        declare_frame_table();
    }
    nt_ui_scroll_end(s_ctx);
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
        return e->operation >= NT_GFX_OP_DRAW && e->operation <= NT_GFX_OP_DRAW_INDEXED_INSTANCED ? &k_head : &k_state;
    case NT_GFX_EVENT_SKIP:
        return &k_warn;
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
    return id < fr->view.count && e->kind == NT_GFX_EVENT_OP && (e->operation == NT_GFX_OP_CLEAR || (e->operation >= NT_GFX_OP_DRAW && e->operation <= NT_GFX_OP_DRAW_INDEXED_INSTANCED));
}

/* The row text after its indentation; a folded group tells what it holds. */
static void row_label(const fi_frame_t *fr, uint32_t k, char *buf, size_t size) {
    const uint32_t id = s.vis[k];
    const int indent = 2 * (int)s.vis_depth[k];
    if (id >= FI_NODE_BASE) {
        (void)snprintf(buf, size, "%*s%s", indent, "", fi_node_name((fi_node_t)(id - FI_NODE_BASE)));
        return;
    }
    char body[512];
    row_text(fr, id, body, sizeof(body));
    const int n = snprintf(buf, size, "%*s%s", indent, "", body);
    const nt_gfx_event_t *e = &fr->view.events[id];
    const bool group = e->kind == NT_GFX_EVENT_NOTE || e->kind == NT_GFX_EVENT_MARKER_PUSH;
    if (group && s.group_draws[id] > 0 && n > 0 && (size_t)n < size) {
        (void)snprintf(buf + n, size - (size_t)n, "   %u draws -> %u GL draws (%u merged)", s.group_draws[id], s.group_gl[id], s.group_merged[id]);
    } else if (fr->brk[id] != FI_BREAK_NONE && n > 0 && (size_t)n < size) {
        (void)snprintf(buf + n, size - (size_t)n, "   new GL draw: %s", fi_break_name((fi_break_t)fr->brk[id]));
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void declare_tree_bar(void) {
    static const char *const k_views[VIEW_COUNT] = {"logical", "raw order", "GL calls"};
    ROW_(6) {
        if (button("<", &k_text, false)) {
            go_back();
        }
        if (button(">", &k_text, false)) {
            go_forward();
        }
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
            text("redundant", &k_warn);
            text("GL call", &k_backend);
            text("note", &k_note);
            text("marker", &k_marker);
            text("failure", &k_bad);
        }
    }
}

static Clay_Color row_background(const fi_frame_t *fr, uint32_t id, bool hovered) {
    if (s.selected == id) {
        return k_select;
    }
    if (hovered) {
        return k_hover;
    }
    return id < FI_NODE_BASE && draws_pixels(fr, id) ? k_draw_bg : k_panel;
}

static bool is_draw_op(const nt_gfx_event_t *e);
static uint32_t previous(const fi_frame_t *fr, uint32_t at, bool (*accept)(const nt_gfx_event_t *));

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void declare_tree(void) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 4}, .clip = {.horizontal = true}}) {
        if (s.file_count == 0) {
            text("No capture open.", &k_dim);
        } else if (!frame_ok(frame_sel())) {
            const fi_frame_t *fr = frame_sel();
            textf(&k_bad, "frame rejected: %s at line %u:%u", fr->error != NULL ? fr->error : "?", fr->error_line, fr->error_col);
        } else {
            const fi_frame_t *fr = frame_sel();
            declare_tree_bar();
            const uint32_t list_id = nt_ui_id("tree");
            const nt_ui_vlist_style_t vstyle = nt_ui_vlist_style_defaults();
            const nt_ui_vlist_range_t range = nt_ui_vlist_begin(s_ctx, NULL, list_id, s.vis_count, ROW_H, NT_UI_AXIS_Y, &vstyle,
                                                                &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}, .backgroundColor = k_panel});
            if (s.follow && s.selected != FI_NONE) {
                const uint32_t at = vis_index_of(s.selected);
                if (at != FI_NONE && (at < range.first + 3U || at + 3U > range.last)) {
                    nt_ui_scroll_to(s_ctx, list_id, 0.0F, (float)(at > 10U ? at - 10U : 0U) * ROW_H);
                }
                s.follow = false;
            }
            char buf[640];
            /* The draw the selected one is compared with, tagged in the gutter. */
            const uint32_t compared = s.selected < fr->view.count && is_draw_op(&fr->view.events[s.selected]) ? previous(fr, s.selected, is_draw_op) : FI_NONE;
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
                } else if (ev.clicked) {
                    s.selected = id;
                }
                row_label(fr, k, buf, sizeof(buf));
                const nt_ui_label_style_t *badge_style = &k_dim;
                const char *badge = id < FI_NODE_BASE ? result_text(&fr->view.events[id], &badge_style) : "";
                const bool call = id < FI_NODE_BASE && (fr->view.events[id].kind == NT_GFX_EVENT_BACKEND || fr->view.events[id].kind == NT_GFX_EVENT_SKIP);
                char number[16] = "";
                if (id < FI_NODE_BASE) {
                    (void)snprintf(number, sizeof(number), call ? "gl %u" : "%u", id);
                }
                const bool selected = s.selected == id;
                CLAY({.id = (Clay_ElementId){.id = row_id},
                      .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ROW_H)}, .padding = {.right = 6}, .childGap = 6, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}},
                      .backgroundColor = row_background(fr, id, ev.hovered),
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
                    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(64), CLAY_SIZING_FIXED(ROW_H)}, .childAlignment = {.x = CLAY_ALIGN_X_RIGHT, .y = CLAY_ALIGN_Y_CENTER}}}) {
                        text(number, call ? &k_dim : &k_text);
                    }
                    CELL_(96) { text(badge, badge_style); }
                    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ROW_H)}, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}, .clip = {.horizontal = true}}) {
                        text(buf, row_style(fr, id));
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
    const nt_gfx_counters_t r = fi_recount(fr);
    const nt_gfx_counters_t *c = &fr->view.counters;
    uint64_t frame_bytes = 0;
    for (uint32_t k = 0; k < NT_GFX_FRAME_STREAM_COUNT; k++) {
        frame_bytes += c->frame_bytes[k];
    }
    bool differ = frame_bytes != fr->summary.frame_store.sum || c->vertices != r.vertices || c->indices != r.indices || c->instances != r.instances;
    differ |= c->buffer_upload_calls != r.buffer_upload_calls || c->buffer_upload_bytes != r.buffer_upload_bytes;
    differ |= c->texture_upload_calls != r.texture_upload_calls || c->texture_upload_bytes != r.texture_upload_bytes;
    for (uint32_t k = 0; k < NT_GFX_OP_COUNT; k++) {
        differ |= c->accepted[k] != r.accepted[k];
    }
    for (uint32_t k = 0; k < NT_GFX_GL_COUNT; k++) {
        differ |= c->gl[k] != r.gl[k];
    }
    return differ;
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
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(220), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .padding = {8, 8, 4, 4}}, .backgroundColor = k_bg}) {
        number_styled(q, value, label, &k_big);
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
    (void)snprintf(detail[0], sizeof(detail[0]), "%llu draws, %llu merged", (unsigned long long)sm->vertices.n, (unsigned long long)sm->merged.n);
    (void)snprintf(detail[1], sizeof(detail[1]), "%llu skipped by cache", (unsigned long long)sm->skips.n);
    (void)snprintf(detail[2], sizeof(detail[2]), "in %llu upload calls", (unsigned long long)sm->upload[2].n);
    (void)snprintf(detail[3], sizeof(detail[3]), "drops and failed requests");
    ROW_(6) {
        card((fi_query_t){.kind = FI_Q_DRAW_CALL}, value[0], "GL draw calls", detail[0]);
        card((fi_query_t){.kind = FI_Q_GL_ANY}, value[1], "GL calls", detail[1]);
        card((fi_query_t){.kind = FI_Q_UPLOAD, .a = 2}, value[2], "uploaded", detail[2]);
        card((fi_query_t){.kind = FI_Q_PROBLEM}, value[3], "problems", detail[3]);
    }
}

/* Plain sentences about what the frame did, each a filter for the records it is about. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void declare_insights(const fi_frame_t *fr) {
    const fi_summary_t *sm = &fr->summary;
    section("Insights (click: show the records)");
    if (sm->merged.n > 0) {
        insight((fi_query_t){.kind = FI_Q_MERGED}, &k_link, "%llu draws merged into the previous draw: no new GL draw call", (unsigned long long)sm->merged.n);
    }
    for (uint32_t b = FI_BREAK_COMMAND; b < FI_BREAK_COUNT; b++) {
        if (sm->brk[b].n > 0) {
            insight((fi_query_t){.kind = FI_Q_BREAK, .a = b}, &k_link, "%llu draws not merged: %s", (unsigned long long)sm->brk[b].n, fi_break_name((fi_break_t)b));
        }
    }
    /* Redundant requests are one line of chips, not a sentence each. */
    uint32_t chips = 0;
    for (uint32_t op = 0; op < NT_GFX_OP_COUNT; op++) {
        chips += sm->op[op][NT_GFX_RESULT_CACHE].n > 0 && op != NT_GFX_OP_DRAW && op != NT_GFX_OP_DRAW_INDEXED ? 1U : 0U;
    }
    if (chips > 0) {
        text("Changed nothing (CACHE), per operation:", &k_text);
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
        insight((fi_query_t){.kind = FI_Q_SKIP}, &k_link, "%llu GL calls skipped by the backend cache (SKIP): state was already there", (unsigned long long)sm->skips.n);
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
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(330), CLAY_SIZING_FIT(0)}}, .clip = {.horizontal = true}}) { text(name, &k_text); }
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
    stat("draws (ACCEPTED or CACHE)", (fi_query_t){.kind = FI_Q_VERTICES}, sm->vertices.n);
    stat("merged into the previous draw (CACHE)", (fi_query_t){.kind = FI_Q_MERGED}, sm->merged.n);
    stat("GL draw calls issued", (fi_query_t){.kind = FI_Q_DRAW_CALL}, sm->draw_calls.n);
    for (uint32_t c = 0; c < NT_GFX_GL_COUNT; c++) {
        if (is_draw_call(c) && sm->gl[c].n > 0) {
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
    static const char *const k_heads[FI_METRIC_COUNT] = {"draws", "GL calls", "upload B", "drops"};
    section("By marker and note");
    ROW_(4) {
        CELL_(190) { text("group", &k_dim); }
        for (int m = 0; m < FI_METRIC_COUNT; m++) {
            CELL_(50) { text(k_heads[m], &k_dim); }
        }
    }
    for (uint32_t g = 0; g < fr->group_count; g++) {
        const fi_cell_t *c = &fr->summary.group[(size_t)g * FI_METRIC_COUNT];
        if (c[FI_METRIC_DRAWS].n + c[FI_METRIC_CALLS].n + c[FI_METRIC_DROPS].n == 0) {
            continue;
        }
        ROW_(4) {
            CELL_(190) { textf(&k_text, "%.24s", fr->group_key[g]); }
            for (uint32_t m = 0; m < FI_METRIC_COUNT; m++) {
                char label[FI_KEY_LEN + 16];
                (void)snprintf(label, sizeof(label), "%s of %s", k_heads[m], fr->group_key[g]);
                CELL_(50) { number((fi_query_t){.kind = FI_Q_GROUP, .a = g, .b = (uint8_t)m}, m == FI_METRIC_BYTES ? c[m].sum : c[m].n, label); }
            }
        }
    }
}

static void declare_counters(const fi_frame_t *fr);
static void declare_details(const fi_frame_t *fr);
static void declare_state(const fi_frame_t *fr);
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
    CLAY({.layout = {.childGap = 40}}) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(620), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 3}}) {
            declare_insights(fr);
            declare_draws(fr);
            declare_transfers(fr);
            declare_resources(fr);
            declare_groups(fr);
        }
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(620), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 3}}) {
            declare_funnel(fr);
            declare_gl_calls(fr);
            declare_counters(fr);
        }
    }
    nt_ui_scroll_end(s_ctx);
}

/* The selected command: what it is, why it merged or not, its GL calls and the state it ran with. */
static void declare_selection(void) {
    const nt_ui_scroll_style_t scroll = nt_ui_scroll_style_defaults();
    nt_ui_scroll_begin(
        s_ctx, NULL, nt_ui_id("selection"), &scroll,
        &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(SIDE_W), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .padding = CLAY_PADDING_ALL(8), .childGap = 2},
                                   .backgroundColor = k_panel});
    const fi_frame_t *fr = frame_sel();
    declare_details(fr);
    if (s.selected < fr->view.count) {
        declare_state(fr);
        declare_raw(fr, s.selected);
    }
    nt_ui_scroll_end(s_ctx);
}

/* Init state: what the frame inherited (the begin_frame snapshot); End state: what it left. */
static void declare_state_page(bool end) {
    const fi_frame_t *fr = frame_sel();
    const nt_ui_scroll_style_t scroll = nt_ui_scroll_style_defaults();
    nt_ui_scroll_begin(
        s_ctx, NULL, nt_ui_id(end ? "end_state" : "init_state"), &scroll,
        &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .padding = CLAY_PADDING_ALL(12), .childGap = 2},
                                   .backgroundColor = k_panel});
    if (end) {
        text("State after the frame's last record: the engine's bindings and the GL state it left.", &k_dim);
        const uint32_t last = fr->view.count - 1U;
        const uint32_t keep = s.selected;
        const bool all = s.state_all;
        s.selected = last;
        s.state_all = true;
        declare_state(fr);
        s.selected = keep;
        s.state_all = all;
    } else {
        text("What the frame inherited: the snapshot begin_frame recorded before any work, then the links it finished.", &k_dim);
        for (uint32_t i = 0; i < fr->view.count; i++) {
            const uint32_t p = fr->parent[i];
            if (p == FI_NODE_BASE + FI_NODE_SNAPSHOT || p == FI_NODE_BASE + FI_NODE_PROGRAM_LINKS) {
                link_line(fr, "", i);
            }
        }
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
    if (button(buf, &k_link, false)) {
        select_record(record);
        s.page = PAGE_COMMANDS;
    }
}

/* For a draw, the answer to "did it cost a GL draw call, and why": merged, or new and what broke the merge. */
static void declare_verdict(const fi_frame_t *fr, uint32_t id) {
    const nt_gfx_event_t *e = &fr->view.events[id];
    if (e->kind != NT_GFX_EVENT_OP || e->operation < NT_GFX_OP_DRAW || e->operation > NT_GFX_OP_DRAW_INDEXED_INSTANCED) {
        return;
    }
    if (e->result == NT_GFX_RESULT_CACHE && e->detail > 0 && e->detail <= fr->view.count) {
        text("MERGED: no new GL draw call; its range joined an earlier draw", &k_warn);
        link_line(fr, "joined ", e->detail - 1U);
        return;
    }
    if (e->result != NT_GFX_RESULT_ACCEPTED) {
        textf(&k_bad, "%s: nothing drawn", nt_gfx_result_name(e->result));
        return;
    }
    uint32_t ordinal = 0;
    for (uint32_t i = 0; i <= id; i++) {
        ordinal += fr->view.events[i].kind == NT_GFX_EVENT_OP && fr->view.events[i].result == NT_GFX_RESULT_ACCEPTED && fr->view.events[i].link == 0 &&
                           fr->view.events[i].operation >= NT_GFX_OP_DRAW && fr->view.events[i].operation <= NT_GFX_OP_DRAW_INDEXED_INSTANCED
                       ? 1U
                       : 0U;
    }
    textf(&k_head, "NEW GL draw call %u of %u", ordinal, nt_gfx_draw_calls(&fr->view.counters));
    if (e->operation == NT_GFX_OP_DRAW_INSTANCED || e->operation == NT_GFX_OP_DRAW_INDEXED_INSTANCED) {
        text("instanced draws never merge: joining them would change gl_InstanceID", &k_dim);
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
    uint32_t shown = 0;
    for (uint32_t i = id + 1U; i < fr->view.count && shown < 40; i++) {
        if (fr->parent[i] == id) {
            if (shown++ == 0) {
                section("GL calls and records it caused");
            }
            link_line(fr, "", i);
        }
    }
}

/* The record exactly as captured, last: everything above is read from it. */
static void declare_raw(const fi_frame_t *fr, uint32_t id) {
    const char *line = NULL;
    const size_t len = fi_record_text(fr, id, &line);
    char buf[1024];
    (void)snprintf(buf, sizeof(buf), "%.*s", (int)(len > 900 ? 900 : len), line);
    section("Record as captured");
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}}}) { text(buf, &k_wrap); }
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

static bool is_draw_op(const nt_gfx_event_t *e) { return e->kind == NT_GFX_EVENT_OP && e->link == 0 && e->operation >= NT_GFX_OP_DRAW && e->operation <= NT_GFX_OP_DRAW_INDEXED_INSTANCED; }
static bool is_draw_issue(const nt_gfx_event_t *e) { return e->kind == NT_GFX_EVENT_BACKEND && is_draw_call(e->detail); }

/* Whether two setters leave the same value: the same request or call with the same arguments. */
static bool same_value(const fi_frame_t *fr, uint32_t a, uint32_t b) {
    if (a == b) {
        return true;
    }
    if (a == FI_NONE || b == FI_NONE) {
        return false;
    }
    const nt_gfx_event_t *x = &fr->view.events[a];
    const nt_gfx_event_t *y = &fr->view.events[b];
    /* Records are parsed into zeroed storage, so equal arguments are equal bytes. */
    uint8_t xb[sizeof(x->data)];
    uint8_t yb[sizeof(y->data)];
    memcpy(xb, &x->data, sizeof(xb));
    memcpy(yb, &y->data, sizeof(yb));
    return x->kind == y->kind && x->operation == y->operation && x->object_kind == y->object_kind && x->object == y->object && x->detail == y->detail && memcmp(xb, yb, sizeof(xb)) == 0;
}

static void state_line(const fi_frame_t *fr, const char *name, uint32_t record, uint32_t before, bool inherited) {
    const bool changed = before != record && !same_value(fr, before, record);
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

/* Request and GL state at the selection; orange marks a change since the previous draw. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void declare_state(const fi_frame_t *fr) {
    if (s.selected >= fr->view.count) {
        text("Select a record: the state shown is the one in effect after it.", &k_dim);
        return;
    }
    const uint32_t at = s.selected;
    fi_request_state_t req;
    fi_request_state_t prev_req;
    fi_request_state(fr, at, &req);
    const uint32_t prev_draw = previous(fr, at, is_draw_op);
    if (prev_draw != FI_NONE) {
        fi_request_state(fr, prev_draw, &prev_req);
    } else {
        prev_req = req;
    }
    section("Engine state (bindings it ran with)");
    if (prev_draw != FI_NONE) {
        link_line(fr, "violet: a different value than at the previous draw ", prev_draw);
    }
    const struct {
        const char *name;
        uint32_t now, before;
    } rows[] = {{"pass", req.pass, prev_req.pass},
                {"pipeline", req.pipeline, prev_req.pipeline},
                {"vertex input", req.vertex_input, prev_req.vertex_input},
                {"texture set", req.texture_set, prev_req.texture_set},
                {"viewport", req.viewport, prev_req.viewport},
                {"scissor", req.scissor, prev_req.scissor},
                {"scissor enable", req.scissor_enable, prev_req.scissor_enable}};
    for (uint32_t k = 0; k < sizeof(rows) / sizeof(rows[0]); k++) {
        state_line(fr, rows[k].name, rows[k].now, prev_draw != FI_NONE ? rows[k].before : rows[k].now, rows[k].now != FI_NONE && fr->view.events[rows[k].now].kind == NT_GFX_EVENT_INITIAL);
    }
    char name[48];
    for (uint32_t k = 0; k < 8; k++) {
        if (req.ubo[k] != FI_NONE || prev_req.ubo[k] != FI_NONE) {
            (void)snprintf(name, sizeof(name), "uniform slot %u", k);
            state_line(fr, name, req.ubo[k], prev_draw != FI_NONE ? prev_req.ubo[k] : req.ubo[k], false);
        }
    }
    /* An operation's draw-phase calls issue at end_frame: its GL state is the one after its last call. */
    uint32_t gl_at = at;
    for (uint32_t i = at + 1U; i < fr->view.count; i++) {
        gl_at = fr->parent[i] == at && fr->view.events[i].kind == NT_GFX_EVENT_BACKEND ? i : gl_at;
    }
    static fi_gl_state_t gl;
    static fi_gl_state_t prev_gl;
    fi_gl_state(fr, gl_at, &gl);
    const uint32_t prev_issue = previous(fr, gl_at, is_draw_issue);
    prev_gl.count = 0;
    if (prev_issue != FI_NONE) {
        fi_gl_state(fr, prev_issue, &prev_gl);
    }
    section("GL state");
    if (button(s.state_all ? "show changes only" : "show all GL state", &k_text, false)) {
        s.state_all = !s.state_all;
    }
    if (prev_issue != FI_NONE) {
        link_line(fr, "violet: different since ", prev_issue);
    }
    uint32_t shown = 0;
    for (uint32_t k = 0; k < gl.count; k++) {
        const fi_gl_slot_t *slot = &gl.slot[k];
        const uint32_t before = prev_issue != FI_NONE ? slot_before(slot, &prev_gl) : slot->record;
        const bool changed = before != slot->record && !same_value(fr, before, slot->record);
        if (!s.state_all && !changed && prev_issue != FI_NONE) {
            continue;
        }
        gl_slot_name(slot, name, sizeof(name));
        state_line(fr, name, slot->record, before, slot->inherited);
        shown++;
    }
    if (shown == 0) {
        text("no GL state changed since the previous draw", &k_dim);
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

typedef struct {
    char name[64];
    uint64_t counter, recount;
    bool bytes;
} counter_t;

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void declare_counters(const fi_frame_t *fr) {
    const nt_gfx_counters_t *c = &fr->view.counters;
    const nt_gfx_counters_t r = fi_recount(fr);
    static counter_t rows[(size_t)NT_GFX_OP_COUNT + (size_t)NT_GFX_GL_COUNT + 16U];
    uint32_t n = 0;
#define FI_COUNTER_(label, a, b, is_bytes) (rows[n++] = (counter_t){.name = label, .counter = (a), .recount = (b), .bytes = (is_bytes)})
    FI_COUNTER_("vertices", c->vertices, r.vertices, false);
    FI_COUNTER_("indices", c->indices, r.indices, false);
    FI_COUNTER_("instances", c->instances, r.instances, false);
    FI_COUNTER_("buffer upload calls", c->buffer_upload_calls, r.buffer_upload_calls, false);
    FI_COUNTER_("buffer upload bytes", c->buffer_upload_bytes, r.buffer_upload_bytes, true);
    FI_COUNTER_("texture upload calls", c->texture_upload_calls, r.texture_upload_calls, false);
    FI_COUNTER_("texture upload bytes", c->texture_upload_bytes, r.texture_upload_bytes, true);
    uint64_t frame_bytes = 0;
    for (uint32_t k = 0; k < NT_GFX_FRAME_STREAM_COUNT; k++) {
        frame_bytes += c->frame_bytes[k];
    }
    FI_COUNTER_("frame storage bytes", frame_bytes, fr->summary.frame_store.sum, true);
#undef FI_COUNTER_
    for (uint32_t op = 0; op < NT_GFX_OP_COUNT; op++) {
        if (c->accepted[op] != 0 || r.accepted[op] != 0) {
            rows[n] = (counter_t){.counter = c->accepted[op], .recount = r.accepted[op]};
            (void)snprintf(rows[n++].name, sizeof(rows[0].name), "accepted.%s", nt_gfx_operation_name(op));
        }
    }
    for (uint32_t k = 1; k < NT_GFX_GL_COUNT; k++) {
        if (c->gl[k] != 0 || r.gl[k] != 0) {
            rows[n] = (counter_t){.counter = c->gl[k], .recount = r.gl[k]};
            (void)snprintf(rows[n++].name, sizeof(rows[0].name), "gl.%s", nt_gfx_gl_call_name(k));
        }
    }
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
    const uint32_t at = vis_index_of(s.selected);
    int64_t k = at == FI_NONE ? 0 : (int64_t)at + delta;
    k = k < 0 ? 0 : k;
    k = k >= (int64_t)s.vis_count ? (int64_t)s.vis_count - 1 : k;
    s.selected = s.vis[k];
    s.follow = true;
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
        select_frame((uint32_t)f, (uint32_t)k);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void handle_keys(void) {
    if (s.file_count == 0 || nt_ui_input_any_focused(s_ctx)) {
        return;
    }
    const bool shift = nt_input_key_is_down(NT_KEY_LSHIFT) || nt_input_key_is_down(NT_KEY_RSHIFT);
    const bool ctrl = nt_input_key_is_down(NT_KEY_LCTRL) || nt_input_key_is_down(NT_KEY_RCTRL);
    const fi_frame_t *fr = frame_sel();
    const bool alt = nt_input_key_is_down(NT_KEY_LALT) || nt_input_key_is_down(NT_KEY_RALT);
    if (alt && (nt_input_key_is_pressed(NT_KEY_ARROW_LEFT) || nt_input_key_is_pressed(NT_KEY_ARROW_RIGHT))) {
        if (nt_input_key_is_pressed(NT_KEY_ARROW_LEFT)) {
            go_back();
        } else {
            go_forward();
        }
        return;
    }
    if (ctrl && (nt_input_key_is_pressed(NT_KEY_ARROW_LEFT) || nt_input_key_is_pressed(NT_KEY_ARROW_RIGHT))) {
        step_frame(nt_input_key_is_pressed(NT_KEY_ARROW_LEFT) ? -1 : 1);
        return;
    }
    if (!frame_ok(fr)) {
        return;
    }
    if (nt_input_key_is_pressed(NT_KEY_ARROW_DOWN)) {
        move(1);
    }
    if (nt_input_key_is_pressed(NT_KEY_ARROW_UP)) {
        move(-1);
    }
    if (nt_input_key_is_pressed(NT_KEY_PAGE_DOWN)) {
        move(30);
    }
    if (nt_input_key_is_pressed(NT_KEY_PAGE_UP)) {
        move(-30);
    }
    if (nt_input_key_is_pressed(NT_KEY_HOME)) {
        move(-(int64_t)s.vis_count);
    }
    if (nt_input_key_is_pressed(NT_KEY_END)) {
        move((int64_t)s.vis_count);
    }
    const uint32_t at = vis_index_of(s.selected);
    if (at != FI_NONE && s.vis_row[at] != FI_NONE && (nt_input_key_is_pressed(NT_KEY_ARROW_LEFT) || nt_input_key_is_pressed(NT_KEY_ARROW_RIGHT))) {
        s.collapsed[s.vis_row[at]] = nt_input_key_is_pressed(NT_KEY_ARROW_LEFT) ? 1U : 0U;
        s.dirty = true;
    }
    if (nt_input_key_is_pressed(NT_KEY_BACKSPACE) && s.selected != FI_NONE && parent_of(fr, s.selected) != FI_NONE) {
        select_record(parent_of(fr, s.selected));
    }
    if (nt_input_key_is_pressed(NT_KEY_ENTER) && s.selected < fr->view.count) {
        const nt_gfx_event_t *e = &fr->view.events[s.selected];
        if (is_mergeable_draw(e) && e->result == NT_GFX_RESULT_CACHE && e->detail > 0 && e->detail <= fr->view.count) {
            select_record(e->detail - 1U);
        } else if (fr->cause[s.selected] != FI_NONE) {
            select_record(fr->cause[s.selected]);
        }
    }
    if (nt_input_key_is_pressed(NT_KEY_D)) {
        jump(shift ? -1 : 1, NT_GFX_OP_DRAW, NT_GFX_OP_DRAW_INDEXED_INSTANCED);
    }
    if (nt_input_key_is_pressed(NT_KEY_P)) {
        jump(shift ? -1 : 1, NT_GFX_OP_PASS, NT_GFX_OP_PASS);
    }
    if (nt_input_key_is_pressed(NT_KEY_ESCAPE)) {
        clear_filters();
    }
}

/* What the keys do on this page, or the hovered frame's numbers. */
static void declare_status(void) {
    if (s.page == PAGE_FRAMES && s.hover_file != FI_NONE && s.hover_file < s.file_count) {
        const fi_frame_t *fr = &s.files[s.hover_file].frames[s.hover_frame];
        textf(&k_text, "frame %llu: %llu GL draw calls, %llu GL calls, %llu KiB uploaded, %u records  (click: open it)", (unsigned long long)fr->view.counters.frame_sequence,
              (unsigned long long)track_value(fr, TRACK_DRAW_CALLS), (unsigned long long)track_value(fr, TRACK_GL_CALLS), (unsigned long long)track_value(fr, TRACK_UPLOAD), fr->view.count);
        return;
    }
    if (s.file_count == 0) {
        text("Open... picks capture files; frame_inspector file... opens them at start.", &k_dim);
        return;
    }
    switch ((page_t)s.page) {
    case PAGE_FRAMES:
        text("wheel: zoom   drag: pan   fit: all frames   click a frame or a row: open it in Commands   Ctrl+Left/Right: previous/next frame", &k_dim);
        break;
    case PAGE_COMMANDS: {
        const fi_frame_t *fr = frame_sel();
        const bool draw = s.selected < fr->view.count && is_mergeable_draw(&fr->view.events[s.selected]);
        textf(&k_dim, "%sUp/Down select  -/+ or Left/Right fold  D/P next draw/pass (Shift: back)  Backspace parent  Alt+Left/Right back/forward  Esc clear filter",
              draw ? "Enter: the draw it joined or what broke the merge   " : "");
        break;
    }
    case PAGE_INFO:
        text("click a number or a sentence: its records in Commands", &k_dim);
        break;
    case PAGE_INIT:
    case PAGE_END:
    case PAGE_COUNT:
        text("click a record: open it in Commands", &k_dim);
        break;
    }
}
// #endregion

// #region frame
static void declare_menu(void) {
    static const char *const k_pages[PAGE_COUNT] = {"Frames", "Commands", "Information", "Init state", "End state"};
    ROW_(6) {
        if (button("Open...", &k_link, false)) {
            s.open_requested = true;
        }
        text("  ", &k_dim);
        for (int p = 0; p < PAGE_COUNT; p++) {
            if (button(k_pages[p], &k_head, s.page == p) && s.file_count > 0) {
                s.page = p;
            }
        }
        if (s.file_count > 0) {
            const fi_frame_t *fr = frame_sel();
            text("  ", &k_dim);
            if (button("<", &k_text, false)) {
                step_frame(-1);
            }
            textf(&k_head, "frame %llu", (unsigned long long)fr->view.counters.frame_sequence);
            if (button(">", &k_text, false)) {
                step_frame(1);
            }
            textf(&k_dim, "%u/%u of %s, %s", s.frame + 1U, s.files[s.file].frame_count, s.files[s.file].name, frame_ok(fr) && fr->view.platform == NT_GFX_CAPTURE_WEB ? "web" : "native");
            if (frame_ok(fr)) {
                declare_banners(fr);
            }
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
                declare_state_page(s.page == PAGE_END);
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

static void frame(void) {
    if (s.open_requested) {
        s.open_requested = false;
        open_files();
    }
    nt_window_poll();
    nt_gfx_begin_frame();
    nt_input_poll();
    nt_mem_scratch_reset();
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
    if (can_render) {
        nt_ui_begin(s_ctx, scale.logical_w, scale.logical_h, g_nt_app.dt, &g_nt_input.pointers[g_nt_input.primary], 1, 0);
        nt_ui_set_viewport(s_ctx, nt_ui_viewport_from_scale(&scale));
        declare_ui();
        nt_ui_end(s_ctx);
    }
    if (s.close_file != FI_NONE) {
        close_file(s.close_file);
        s.close_file = FI_NONE;
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
static nt_material_t make_material(const char *label) {
    return nt_material_create(&(nt_material_create_desc_t){.blend = nt_blend_alpha_premultiplied(), .depth_test = false, .depth_write = false, .cull_mode = NT_CULL_NONE, .label = label});
}

int main(int argc, char *argv[]) {
    nt_engine_config_t config = {.app_name = "frame_inspector"};
    s.close_file = FI_NONE;
    s.hover_file = FI_NONE;
    if (nt_engine_init(&config) != NT_OK) {
        return 1;
    }
    nt_fs_init();
    for (int a = 1; a < argc; a++) {
        add_file(argv[a]);
    }

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
    nt_mem_scratch_init((size_t)4U * 1024U * 1024U);
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
    free(s.group_draws);
    free(s.group_gl);
    free(s.group_merged);
    free(s.alias_object);
    free(s.alias_kind);
    free(s.alias_number);
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
