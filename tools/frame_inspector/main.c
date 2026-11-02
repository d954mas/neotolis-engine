/* Frame inspector: opens capture files (render architecture, "Capture text") and shows what each recorded
 * frame did. Usage: frame_inspector file...  Every frame of every file is parsed on open. */

#include "app/nt_app.h"
#include "atlas/nt_atlas.h"
#include "color/nt_color.h"
#include "core/nt_assert.h"
#include "core/nt_core.h"
#include "fi_model.h"
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
#include "ui/nt_ui_tabbar.h"
#include "ui/nt_ui_vlist.h"
#include "window/nt_window.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FILES 16
#define ROW_H 18.0F
#define FONT 14.0F
#define LAYER_RECT 0
#define LAYER_TEXT 1
#define SIDE_W 660

// #region palette
static const Clay_Color k_bg = {24, 26, 30, 255};
static const Clay_Color k_panel = {32, 35, 41, 255};
static const Clay_Color k_hover = {48, 52, 62, 255};
static const Clay_Color k_select = {44, 74, 120, 255};
static const Clay_Color k_bar = {110, 116, 130, 255};
static const Clay_Color k_bar_bad = {200, 70, 60, 255};
static const Clay_Color k_bar_warn = {220, 150, 50, 255};
static const Clay_Color k_bar_sel = {90, 160, 255, 255};
#define TEXT_(r, g, b) {.font_id = 0, .font_size = FONT, .color = NT_RGBA8(r, g, b, 255), .wrap_mode = CLAY_TEXT_WRAP_NONE}
static const nt_ui_label_style_t k_text = TEXT_(220, 222, 228);
static const nt_ui_label_style_t k_dim = TEXT_(140, 145, 155);
static const nt_ui_label_style_t k_head = TEXT_(255, 255, 255);
static const nt_ui_label_style_t k_link = TEXT_(120, 180, 255);
static const nt_ui_label_style_t k_bad = TEXT_(255, 110, 100);
static const nt_ui_label_style_t k_warn = TEXT_(240, 180, 80);
static const nt_ui_label_style_t k_note = TEXT_(140, 210, 140);
static const nt_ui_label_style_t k_marker = TEXT_(230, 210, 120);
static const nt_ui_label_style_t k_backend = TEXT_(160, 175, 200);
static const nt_ui_label_style_t k_wrap = {.font_id = 0, .font_size = FONT, .color = NT_RGBA8(220, 222, 228, 255)};
// #endregion

// #region state
typedef enum { VIEW_LOGICAL, VIEW_RAW, VIEW_GL, VIEW_COUNT } view_t;
typedef enum { TAB_SUMMARY, TAB_DETAILS, TAB_STATE, TAB_COUNTERS, TAB_COUNT } tab_t;
typedef enum { METRIC_DRAWS, METRIC_CALLS, METRIC_RECORDS, METRIC_BYTES, METRIC_COUNT } metric_t;

static struct {
    fi_file_t files[MAX_FILES];
    uint32_t file_count;
    uint32_t file, frame; /* the selected frame */
    uint32_t selected;    /* the selected row id: a record or FI_NODE_BASE + node */
    view_t view;
    bool draws_only, problems;
    char filter[64];
    fi_query_t query;
    char query_label[96];
    int tab;
    int metric;
    uint8_t *collapsed; /* per logical row of the selected frame */
    uint32_t *vis;      /* visible row ids */
    uint32_t *vis_row;  /* their logical row, FI_NONE in flat lists */
    uint16_t *vis_depth;
    uint32_t vis_count;
    bool dirty;
    bool follow; /* scroll the tree to the selection */
    uint32_t cell_seq;
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

// #region visible rows
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
static void rebuild_rows(void) {
    s.dirty = false;
    s.vis_count = 0;
    const fi_frame_t *fr = frame_sel();
    if (!frame_ok(fr)) {
        return;
    }
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

static void select_frame(uint32_t file, uint32_t frame) {
    s.file = file;
    s.frame = frame;
    const fi_frame_t *fr = frame_sel();
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
static void select_record(uint32_t id) {
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

static void set_query(fi_query_t q, const char *label) {
    s.query = q;
    (void)snprintf(s.query_label, sizeof(s.query_label), "%s", label);
    s.filter[0] = '\0'; /* a click shows exactly the records it counted */
    s.problems = false;
    s.draws_only = false;
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

/* A number that filters the tree to the records it counts. */
static void number(fi_query_t q, uint64_t value, const char *label) {
    char buf[32];
    (void)snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
    if (fi_cell(frame_sel(), q).n == 0) {
        text(buf, &k_dim);
        return;
    }
    if (button(buf, &k_link, s.query.kind == q.kind && s.query.a == q.a && s.query.b == q.b)) {
        set_query(q, label);
    }
}

#define ROW_(gap) CLAY({.layout = {.layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = (gap), .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}})
#define CELL_(w) CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(w), CLAY_SIZING_FIT(0)}}})
// #endregion

// #region frame strip
static uint64_t gl_total(const fi_frame_t *fr) {
    uint64_t calls = 0;
    for (uint32_t c = 0; c < NT_GFX_GL_COUNT; c++) {
        calls += fr->summary.gl[c].n;
    }
    return calls;
}

static uint64_t metric_value(const fi_frame_t *fr) {
    if (!frame_ok(fr)) {
        return 0;
    }
    switch ((metric_t)s.metric) {
    case METRIC_DRAWS:
        return fr->summary.vertices.n;
    case METRIC_CALLS:
        return gl_total(fr);
    case METRIC_RECORDS:
        return fr->view.count;
    case METRIC_BYTES:
        return fr->summary.upload[0].sum + fr->summary.upload[1].sum;
    case METRIC_COUNT:
        break;
    }
    return 0;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void declare_strip(void) {
    static const char *const k_metrics[METRIC_COUNT] = {"draws", "GL calls", "records", "upload bytes"};
    uint64_t max = 1;
    for (uint32_t f = 0; f < s.file_count; f++) {
        for (uint32_t k = 0; k < s.files[f].frame_count; k++) {
            const uint64_t v = metric_value(&s.files[f].frames[k]);
            max = v > max ? v : max;
        }
    }
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .padding = CLAY_PADDING_ALL(6), .childGap = 4}, .backgroundColor = k_panel}) {
        ROW_(6) {
            text("bar height:", &k_dim);
            for (int m = 0; m < METRIC_COUNT; m++) {
                if (button(k_metrics[m], &k_text, s.metric == m)) {
                    s.metric = m;
                }
            }
            text("red: rejected or incomplete   orange: overflow or merge rule out of date", &k_dim);
        }
        for (uint32_t f = 0; f < s.file_count; f++) {
            const fi_file_t *file = &s.files[f];
            ROW_(2) {
                CELL_(300) { textf(&k_text, "%.34s (%u)", file->name, file->frame_count); }
                CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(44)}, .childGap = 2, .childAlignment = {.y = CLAY_ALIGN_Y_BOTTOM}}, .clip = {.horizontal = true}}) {
                    for (uint32_t k = 0; k < file->frame_count; k++) {
                        const fi_frame_t *fr = &file->frames[k];
                        const uint32_t id = next_id();
                        const nt_ui_events_t ev = nt_ui_events(s_ctx, id, NULL);
                        if (ev.clicked) {
                            select_frame(f, k);
                        }
                        const bool sel = f == s.file && k == s.frame;
                        const bool bad = !frame_ok(fr) || fr->error != NULL;
                        const bool warn = frame_ok(fr) && (fr->merge_mismatch != FI_NONE || fr->view.overflow);
                        const float h = bad ? 44.0F : 4.0F + (40.0F * (float)metric_value(fr) / (float)max);
                        Clay_Color color = ev.hovered ? k_hover : k_bar;
                        color = warn ? k_bar_warn : color;
                        color = bad ? k_bar_bad : color;
                        CLAY({.id = (Clay_ElementId){.id = id}, .layout = {.sizing = {CLAY_SIZING_FIXED(8), CLAY_SIZING_FIXED(h)}}, .backgroundColor = sel ? k_bar_sel : color}) {}
                    }
                }
            }
        }
    }
}
// #endregion

// #region tree
static const nt_ui_label_style_t *row_style(const fi_frame_t *fr, uint32_t id) {
    if (id >= FI_NODE_BASE) {
        return &k_head;
    }
    const nt_gfx_event_t *e = &fr->view.events[id];
    if (fi_matches(fr, id, (fi_query_t){.kind = FI_Q_PROBLEM})) {
        return e->kind == NT_GFX_EVENT_SKIP ? &k_warn : &k_bad;
    }
    switch (e->kind) {
    case NT_GFX_EVENT_BACKEND:
        return &k_backend;
    case NT_GFX_EVENT_NOTE:
    case NT_GFX_EVENT_NOTE_FIELD:
        return &k_note;
    case NT_GFX_EVENT_MARKER_PUSH:
    case NT_GFX_EVENT_MARKER_POP:
        return &k_marker;
    case NT_GFX_EVENT_INITIAL:
    case NT_GFX_EVENT_DEFINITION:
    case NT_GFX_EVENT_ARGUMENT:
        return &k_dim;
    default:
        return &k_text;
    }
}

static void row_label(const fi_frame_t *fr, uint32_t k, char *buf, size_t size) {
    const uint32_t id = s.vis[k];
    const uint32_t r = s.vis_row[k];
    const bool parent = r != FI_NONE && r + 1U < fr->row_count && fr->depth[r + 1U] > fr->depth[r];
    const char *fold = "  ";
    if (parent) {
        fold = s.collapsed[r] != 0 ? "+ " : "- ";
    }
    const int indent = 2 * (int)s.vis_depth[k];
    if (id >= FI_NODE_BASE) {
        (void)snprintf(buf, size, "%*s%s%s", indent, "", fold, fi_node_name((fi_node_t)(id - FI_NODE_BASE)));
        return;
    }
    const char *line = NULL;
    const int len = (int)fi_record_text(fr, id, &line);
    const int n = snprintf(buf, size, "%*s%s%6u  %.*s", indent, "", fold, id, len > 400 ? 400 : len, line);
    if (fr->brk[id] != FI_BREAK_NONE && n > 0 && (size_t)n < size) {
        (void)snprintf(buf + n, size - (size_t)n, "   << not merged: %s", fi_break_name((fi_break_t)fr->brk[id]));
    }
}

static void declare_tree_bar(void) {
    static const char *const k_views[VIEW_COUNT] = {"logical", "raw order", "GL calls"};
    ROW_(6) {
        nt_ui_input_style_t style = nt_ui_input_style_defaults();
        style.text = k_text;
        style.placeholder = k_dim;
        if (nt_ui_input_text(s_ctx, NT_UI_DATA_LAYER(LAYER_RECT), LAYER_TEXT, nt_ui_id("filter"), s.filter, sizeof(s.filter), &(nt_ui_input_props_t){.placeholder = "filter text"}, &style,
                             &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(260), CLAY_SIZING_FIXED(22)}}}, true, NULL)) {
            s.dirty = true;
        }
        for (int v = 0; v < VIEW_COUNT; v++) {
            if (button(k_views[v], &k_text, s.view == (view_t)v)) {
                s.view = (view_t)v;
                s.dirty = true;
            }
        }
        if (button("draws only", &k_text, s.draws_only)) {
            s.draws_only = !s.draws_only;
            s.dirty = true;
        }
        if (button("problems", &k_text, s.problems)) {
            s.problems = !s.problems;
            s.dirty = true;
        }
        if (s.query.kind != FI_Q_NONE) {
            textf(&k_link, "counted: %s", s.query_label);
            if (button("x", &k_text, false)) {
                s.query = (fi_query_t){0};
                s.dirty = true;
            }
        }
        textf(&k_dim, "%u rows", s.vis_count);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void declare_tree(void) {
    const fi_frame_t *fr = frame_sel();
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 4}}) {
        declare_tree_bar();
        if (!frame_ok(fr)) {
            textf(&k_bad, "frame rejected: %s at line %u:%u", fr->error != NULL ? fr->error : "?", fr->error_line, fr->error_col);
        } else {
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
            for (uint32_t k = range.first; k <= range.last && k < s.vis_count; ++k) {
                const uint32_t row_id = nt_ui_vlist_item_id(s_ctx, k);
                const nt_ui_events_t ev = nt_ui_events(s_ctx, row_id, NULL);
                if (ev.clicked) {
                    if (s.selected == s.vis[k] && s.vis_row[k] != FI_NONE) {
                        s.collapsed[s.vis_row[k]] ^= 1U;
                        s.dirty = true;
                    }
                    s.selected = s.vis[k];
                }
                row_label(fr, k, buf, sizeof(buf));
                CLAY({.id = (Clay_ElementId){.id = row_id},
                      .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ROW_H)}, .padding = {.left = 4}, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}},
                      .backgroundColor = s.selected == s.vis[k] ? k_select : (ev.hovered ? k_hover : k_panel),
                      .clip = {.horizontal = true}}) {
                    text(buf, row_style(fr, s.vis[k]));
                }
            }
            nt_ui_vlist_end(s_ctx);
        }
    }
}
// #endregion

// #region summary
static void section(const char *title) {
    CLAY({.layout = {.padding = {0, 0, 8, 2}}}) { text(title, &k_head); }
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

static void stat(const char *name, fi_query_t q, uint64_t value) {
    ROW_(8) {
        CELL_(320) { text(name, &k_text); }
        number(q, value, name);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void declare_funnel(const fi_frame_t *fr) {
    static const float k_w[] = {180, 90, 90, 70, 80, 60, 70};
    static const char *const k_heads[] = {"operation", "requested", "ACCEPTED", "CACHE", "GL calls", "SKIP", "nested"};
    section("Operations: requests, results, the GL calls they issued");
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
    stat("draws that drew (ACCEPTED or merged)", (fi_query_t){.kind = FI_Q_VERTICES}, sm->vertices.n);
    ROW_(8) {
        CELL_(320) { text("recorded draw calls (counter)", &k_text); }
        textf(&k_text, "%u", nt_gfx_draw_calls(&fr->view.counters));
    }
    stat("merged into the previous draw (CACHE)", (fi_query_t){.kind = FI_Q_MERGED}, sm->merged.n);
    for (uint32_t c = 0; c < NT_GFX_GL_COUNT; c++) {
        if (is_draw_call(c) && sm->gl[c].n > 0) {
            (void)snprintf(name, sizeof(name), "issued %s", nt_gfx_gl_call_name(c));
            stat(name, (fi_query_t){.kind = FI_Q_GL_CALL, .a = c}, sm->gl[c].n);
        }
    }
    stat("vertices (x instances)", (fi_query_t){.kind = FI_Q_VERTICES}, sm->vertices.sum);
    stat("indices (x instances)", (fi_query_t){.kind = FI_Q_INDICES}, sm->indices.sum);
    stat("instances of instanced draws", (fi_query_t){.kind = FI_Q_INSTANCES}, sm->instances.sum);
    for (uint32_t b = FI_BREAK_COMMAND; b < FI_BREAK_COUNT; b++) {
        if (sm->brk[b].n > 0) {
            (void)snprintf(name, sizeof(name), "not merged: %s", fi_break_name((fi_break_t)b));
            stat(name, (fi_query_t){.kind = FI_Q_BREAK, .a = b}, sm->brk[b].n);
        }
    }
}

static void declare_transfers(const fi_frame_t *fr) {
    const fi_summary_t *sm = &fr->summary;
    section("Transfers");
    stat("buffer upload calls", (fi_query_t){.kind = FI_Q_UPLOAD, .a = 0}, sm->upload[0].n);
    stat("buffer upload bytes", (fi_query_t){.kind = FI_Q_UPLOAD, .a = 0}, sm->upload[0].sum);
    stat("texture upload calls", (fi_query_t){.kind = FI_Q_UPLOAD, .a = 1}, sm->upload[1].n);
    stat("texture upload bytes", (fi_query_t){.kind = FI_Q_UPLOAD, .a = 1}, sm->upload[1].sum);
    stat("frame storage uploads at end_frame", (fi_query_t){.kind = FI_Q_FRAME_STORE}, sm->frame_store.n);
    stat("frame storage bytes", (fi_query_t){.kind = FI_Q_FRAME_STORE}, sm->frame_store.sum);
    ROW_(8) {
        CELL_(320) { text("draw-phase command stream bytes", &k_text); }
        textf(&k_text, "%u (counter)", fr->view.counters.stream_bytes);
    }
}

static void declare_resources(const fi_frame_t *fr) {
    static const char *const k_res[FI_RES_COUNT] = {"live at frame start", "created", "destroyed"};
    section("Resources: bytes requested from GL, from definitions");
    for (uint32_t kind = 1; kind < FI_OBJECTS; kind++) {
        for (uint32_t r = 0; r < FI_RES_COUNT; r++) {
            const fi_cell_t c = fr->summary.resource[kind][r];
            if (c.n == 0) {
                continue;
            }
            char name[96];
            (void)snprintf(name, sizeof(name), "%s %s", nt_gfx_object_kind_name(kind), k_res[r]);
            ROW_(8) {
                CELL_(320) { text(name, &k_text); }
                number((fi_query_t){.kind = FI_Q_RESOURCE, .a = kind, .b = (uint8_t)r}, c.n, name);
                if (c.sum > 0) {
                    textf(&k_dim, "%.1f KiB requested from GL", (double)c.sum / 1024.0);
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
        CELL_(330) { text("group", &k_dim); }
        for (int m = 0; m < FI_METRIC_COUNT; m++) {
            CELL_(70) { text(k_heads[m], &k_dim); }
        }
    }
    for (uint32_t g = 0; g < fr->group_count; g++) {
        const fi_cell_t *c = &fr->summary.group[(size_t)g * FI_METRIC_COUNT];
        if (c[FI_METRIC_DRAWS].n + c[FI_METRIC_CALLS].n + c[FI_METRIC_DROPS].n == 0) {
            continue;
        }
        ROW_(4) {
            CELL_(330) { textf(&k_text, "%.42s", fr->group_key[g]); }
            for (uint32_t m = 0; m < FI_METRIC_COUNT; m++) {
                char label[FI_KEY_LEN + 16];
                (void)snprintf(label, sizeof(label), "%s of %s", k_heads[m], fr->group_key[g]);
                CELL_(70) { number((fi_query_t){.kind = FI_Q_GROUP, .a = g, .b = (uint8_t)m}, m == FI_METRIC_BYTES ? c[m].sum : c[m].n, label); }
            }
        }
    }
}

static void declare_summary(const fi_frame_t *fr) {
    const fi_summary_t *sm = &fr->summary;
    uint64_t ops = 0;
    for (uint32_t op = 0; op < NT_GFX_OP_COUNT; op++) {
        for (uint32_t r = 0; r < FI_RESULTS; r++) {
            ops += sm->op[op][r].n;
        }
    }
    textf(&k_head, "frame %llu  %s  %u records  %llu operations  %llu passes", (unsigned long long)fr->view.counters.frame_sequence, fr->view.platform == NT_GFX_CAPTURE_WEB ? "web" : "native",
          fr->view.count, (unsigned long long)ops, (unsigned long long)sm->op[NT_GFX_OP_PASS][NT_GFX_RESULT_ACCEPTED].n);
    textf(&k_head, "draws %llu -> recorded %u, merged %llu   GL calls %llu   uploads %llu calls, %.1f KiB", (unsigned long long)sm->vertices.n, nt_gfx_draw_calls(&fr->view.counters),
          (unsigned long long)sm->merged.n, (unsigned long long)gl_total(fr), (unsigned long long)(sm->upload[0].n + sm->upload[1].n), (double)(sm->upload[0].sum + sm->upload[1].sum) / 1024.0);
    declare_banners(fr);
    ROW_(8) {
        text("problems (drops, results other than ACCEPTED/CACHE, SKIP records):", &k_text);
        number((fi_query_t){.kind = FI_Q_PROBLEM}, sm->problems.n, "problems");
    }
    declare_funnel(fr);
    declare_draws(fr);
    declare_transfers(fr);
    declare_resources(fr);
    declare_gl_calls(fr);
    declare_groups(fr);
}
// #endregion

// #region details
static void link_line(const fi_frame_t *fr, const char *what, uint32_t record) {
    const char *line = NULL;
    const size_t len = fi_record_text(fr, record, &line);
    char buf[512];
    (void)snprintf(buf, sizeof(buf), "%s@%u  %.*s", what, record, (int)(len > 300 ? 300 : len), line);
    if (button(buf, &k_link, false)) {
        select_record(record);
    }
}

static void declare_details(const fi_frame_t *fr) {
    const uint32_t id = s.selected;
    if (id == FI_NONE) {
        text("Select a row. Click a selected row to fold or unfold it.", &k_dim);
        return;
    }
    if (id >= FI_NODE_BASE) {
        text(fi_node_name((fi_node_t)(id - FI_NODE_BASE)), &k_head);
        return;
    }
    const nt_gfx_event_t *e = &fr->view.events[id];
    const char *line = NULL;
    const size_t len = fi_record_text(fr, id, &line);
    char buf[1024];
    (void)snprintf(buf, sizeof(buf), "@%u  %.*s", id, (int)(len > 900 ? 900 : len), line);
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}}}) { text(buf, &k_wrap); }
    textf(&k_dim, "group: %s", fr->group_key[fr->group[id]]);
    if (fr->parent[id] < FI_NODE_BASE) {
        link_line(fr, "inside ", fr->parent[id]);
    } else {
        textf(&k_dim, "phase: %s", fi_node_name((fi_node_t)(fr->parent[id] - FI_NODE_BASE)));
    }
    if (e->kind == NT_GFX_EVENT_OP && e->result == NT_GFX_RESULT_CACHE && (e->operation == NT_GFX_OP_DRAW || e->operation == NT_GFX_OP_DRAW_INDEXED) && e->detail > 0 && e->detail <= fr->view.count) {
        link_line(fr, "merged into the draw ", e->detail - 1U);
    }
    if (fr->brk[id] != FI_BREAK_NONE) {
        textf(&k_warn, "not merged into the previous command: %s", fi_break_name((fi_break_t)fr->brk[id]));
        if (fr->cause[id] != FI_NONE) {
            link_line(fr, "previous command ", fr->cause[id]);
        }
    }
    uint32_t shown = 0;
    for (uint32_t i = id + 1U; i < fr->view.count && shown < 40; i++) {
        if (fr->parent[i] == id) {
            if (shown++ == 0) {
                text("contains:", &k_dim);
            }
            link_line(fr, "", i);
        }
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

static bool is_draw_op(const nt_gfx_event_t *e) { return e->kind == NT_GFX_EVENT_OP && e->link == 0 && e->operation >= NT_GFX_OP_DRAW && e->operation <= NT_GFX_OP_DRAW_INDEXED_INSTANCED; }
static bool is_draw_issue(const nt_gfx_event_t *e) { return e->kind == NT_GFX_EVENT_BACKEND && is_draw_call(e->detail); }

static void state_line(const fi_frame_t *fr, const char *name, uint32_t record, bool changed) {
    ROW_(6) {
        CELL_(180) { text(name, changed ? &k_warn : &k_text); }
        if (record == FI_NONE) {
            text("unset", &k_dim);
        } else {
            link_line(fr, "", record);
        }
    }
}

static void gl_slot_name(const fi_gl_slot_t *slot, char *buf, size_t size) {
    if (slot->call == NT_GFX_GL_NONE) {
        (void)snprintf(buf, size, "fixed function");
    } else if (slot->call == NT_GFX_GL_glBindTexture) {
        (void)snprintf(buf, size, "texture unit %u", slot->key[0]);
    } else if (slot->call == NT_GFX_GL_glBindSampler) {
        (void)snprintf(buf, size, "sampler unit %u", slot->key[0]);
    } else if (slot->call == NT_GFX_GL_glBindBufferRange) {
        (void)snprintf(buf, size, "range 0x%x #%u", slot->key[0], slot->key[1]);
    } else if (slot->call == NT_GFX_GL_glEnable || slot->call == NT_GFX_GL_glBindBuffer || slot->call == NT_GFX_GL_glBindFramebuffer || slot->call == NT_GFX_GL_glPixelStorei) {
        (void)snprintf(buf, size, "%s 0x%x", slot->call == NT_GFX_GL_glEnable ? "cap" : nt_gfx_gl_call_name(slot->call) + 2, slot->key[0]);
    } else {
        (void)snprintf(buf, size, "%s", nt_gfx_gl_call_name(slot->call) + 2);
    }
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
    section("Request state (frontend bindings)");
    if (prev_draw != FI_NONE) {
        link_line(fr, "orange: changed since the draw ", prev_draw);
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
        state_line(fr, rows[k].name, rows[k].now, rows[k].now != rows[k].before);
    }
    char name[48];
    for (uint32_t k = 0; k < 8; k++) {
        if (req.ubo[k] != FI_NONE || prev_req.ubo[k] != FI_NONE) {
            (void)snprintf(name, sizeof(name), "uniform slot %u", k);
            state_line(fr, name, req.ubo[k], req.ubo[k] != prev_req.ubo[k]);
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
    section("GL state (issued calls; dim: inherited from the snapshot)");
    if (prev_issue != FI_NONE) {
        link_line(fr, "orange: changed since ", prev_issue);
    }
    for (uint32_t k = 0; k < gl.count; k++) {
        const fi_gl_slot_t *slot = &gl.slot[k];
        bool changed = prev_issue != FI_NONE;
        for (uint32_t p = 0; p < prev_gl.count && changed; p++) {
            const fi_gl_slot_t *old = &prev_gl.slot[p];
            changed = !(old->call == slot->call && old->key[0] == slot->key[0] && old->key[1] == slot->key[1] && old->record == slot->record);
        }
        gl_slot_name(slot, name, sizeof(name));
        ROW_(6) {
            const nt_ui_label_style_t *style = slot->inherited ? &k_dim : &k_text;
            CELL_(180) { text(name, changed ? &k_warn : style); }
            link_line(fr, "", slot->record);
        }
    }
}
// #endregion

// #region counters
static void counter_row(const char *name, uint64_t counter, uint64_t recount, bool overflow) {
    ROW_(8) {
        CELL_(300) { text(name, &k_text); }
        CELL_(110) { textf(&k_text, "%llu", (unsigned long long)counter); }
        CELL_(110) { textf(&k_text, "%llu", (unsigned long long)recount); }
        if (overflow) {
            text("n/a: events truncated", &k_dim);
        } else if (counter != recount) {
            text("MISMATCH", &k_bad);
        }
    }
}

static void declare_counters(const fi_frame_t *fr) {
    const nt_gfx_counters_t *c = &fr->view.counters;
    const nt_gfx_counters_t r = fi_recount(fr);
    const bool over = fr->view.overflow;
    ROW_(8) {
        CELL_(300) { text("counter", &k_dim); }
        CELL_(110) { text("frame", &k_dim); }
        CELL_(110) { text("from records", &k_dim); }
    }
    counter_row("vertices", c->vertices, r.vertices, over);
    counter_row("indices", c->indices, r.indices, over);
    counter_row("instances", c->instances, r.instances, over);
    counter_row("buffer upload calls", c->buffer_upload_calls, r.buffer_upload_calls, over);
    counter_row("buffer upload bytes", c->buffer_upload_bytes, r.buffer_upload_bytes, over);
    counter_row("texture upload calls", c->texture_upload_calls, r.texture_upload_calls, over);
    counter_row("texture upload bytes", c->texture_upload_bytes, r.texture_upload_bytes, over);
    uint64_t frame_bytes = 0;
    for (uint32_t k = 0; k < NT_GFX_FRAME_STREAM_COUNT; k++) {
        frame_bytes += c->frame_bytes[k];
    }
    counter_row("frame storage bytes", frame_bytes, fr->summary.frame_store.sum, over);
    ROW_(8) {
        CELL_(300) { text("stream bytes", &k_text); }
        CELL_(110) { textf(&k_text, "%u", c->stream_bytes); }
        text("counter only: command sizes are not recorded", &k_dim);
    }
    char name[64];
    for (uint32_t op = 0; op < NT_GFX_OP_COUNT; op++) {
        if (c->accepted[op] != 0 || r.accepted[op] != 0) {
            (void)snprintf(name, sizeof(name), "accepted.%s", nt_gfx_operation_name(op));
            counter_row(name, c->accepted[op], r.accepted[op], over);
        }
    }
    for (uint32_t k = 1; k < NT_GFX_GL_COUNT; k++) {
        if (c->gl[k] != 0 || r.gl[k] != 0) {
            (void)snprintf(name, sizeof(name), "gl.%s", nt_gfx_gl_call_name(k));
            counter_row(name, c->gl[k], r.gl[k], over);
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
    if (nt_ui_input_any_focused(s_ctx)) {
        return;
    }
    const bool shift = nt_input_key_is_down(NT_KEY_LSHIFT) || nt_input_key_is_down(NT_KEY_RSHIFT);
    const bool ctrl = nt_input_key_is_down(NT_KEY_LCTRL) || nt_input_key_is_down(NT_KEY_RCTRL);
    const fi_frame_t *fr = frame_sel();
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
        if (e->kind == NT_GFX_EVENT_OP && e->result == NT_GFX_RESULT_CACHE && e->detail > 0 && e->detail <= fr->view.count) {
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
        s.query = (fi_query_t){0};
        s.filter[0] = '\0';
        s.problems = false;
        s.draws_only = false;
        s.dirty = true;
    }
}
// #endregion

// #region frame
static void declare_ui(void) {
    static const char *const k_tabs[TAB_COUNT] = {"Summary", "Details", "State", "Counters"};
    const fi_frame_t *fr = frame_sel();
    s.cell_seq = 0;
    CLAY({.id = CLAY_ID("root"),
          .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .padding = CLAY_PADDING_ALL(6), .childGap = 6},
          .backgroundColor = k_bg}) {
        declare_strip();
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .childGap = 6}}) {
            declare_tree();
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(SIDE_W), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 4}}) {
                nt_ui_tabbar_style_t tabs = nt_ui_tabbar_style_defaults();
                tabs.dir = NT_UI_TABBAR_HORIZONTAL;
                tabs.font_size = FONT;
                tabs.tab_extent = 110;
                (void)nt_ui_tabbar(s_ctx, NT_UI_DATA_LAYER(LAYER_RECT), LAYER_TEXT, nt_ui_id("tabs"), k_tabs, NULL, TAB_COUNT, &s.tab, &tabs);
                const nt_ui_scroll_style_t scroll = nt_ui_scroll_style_defaults();
                nt_ui_scroll_begin(
                    s_ctx, NULL, nt_ui_id("side"), &scroll,
                    &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .padding = CLAY_PADDING_ALL(8), .childGap = 2},
                                               .backgroundColor = k_panel});
                if (!frame_ok(fr)) {
                    textf(&k_bad, "rejected: %s at line %u:%u", fr->error != NULL ? fr->error : "?", fr->error_line, fr->error_col);
                } else if (s.tab == TAB_SUMMARY) {
                    declare_summary(fr);
                } else if (s.tab == TAB_DETAILS) {
                    declare_details(fr);
                } else if (s.tab == TAB_STATE) {
                    declare_state(fr);
                } else {
                    declare_counters(fr);
                }
                nt_ui_scroll_end(s_ctx);
            }
        }
        ROW_(12) {
            textf(&k_dim, "%s  frame %u/%u", s.files[s.file].name, s.frame + 1U, s.files[s.file].frame_count);
            text("Up/Down PgUp/PgDn Home/End  Left/Right fold  Backspace parent  Enter merge/cause  D/P next draw/pass (Shift back)  Ctrl+Left/Right frame  Esc clear", &k_dim);
        }
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
int main(int argc, char *argv[]) {
    if (argc < 2 || argc - 1 > MAX_FILES) {
        (void)fprintf(stderr, "usage: frame_inspector file... (1 to %d capture files)\n", MAX_FILES);
        return 1;
    }
    nt_engine_config_t config = {.app_name = "frame_inspector"};
    if (nt_engine_init(&config) != NT_OK) {
        return 1;
    }
    nt_fs_init();
    for (int a = 1; a < argc; a++) {
        if (!fi_file_open(&s.files[s.file_count], argv[a])) {
            (void)fprintf(stderr, "frame_inspector: cannot read %s\n", argv[a]);
        } else if (s.files[s.file_count].frame_count == 0) {
            (void)fprintf(stderr, "frame_inspector: %s holds no capture\n", argv[a]);
            fi_file_close(&s.files[s.file_count]);
        } else {
            s.file_count++;
        }
    }
    if (s.file_count == 0) {
        nt_fs_shutdown();
        nt_engine_shutdown();
        return 1;
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
    ui.max_elements = 8192U;
    ui.state_slots = 1024U;
    const size_t arena = nt_ui_min_arena_size(&ui);
    s_ui_arena = malloc(arena);
    NT_ASSERT(s_ui_arena != NULL && ((uintptr_t)s_ui_arena % NT_UI_ARENA_ALIGN) == 0);
    s_ctx = nt_ui_create_context(s_ui_arena, arena, &ui);
    NT_ASSERT(s_ctx != NULL);

    /* The pack sits next to the executable, so the inspector opens files from any directory. */
    char pack[1024];
    const char *slash = strrchr(argv[0], '/');
    const char *back = strrchr(argv[0], '\\');
    const char *end = back != NULL && (slash == NULL || back > slash) ? back : slash;
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
    select_frame(0, 0);

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
