#include "fi_model.h"

#include "core/nt_assert.h"
#include "fs/nt_fs.h"
#include "graphics/nt_gfx_capture_text.h"
#include "nt_texture_format.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FI_NODE_ROOT FI_NODE_COUNT
#define FI_MAX_CONTRIB 8

// #region helpers
static void *fi_alloc(size_t count, size_t size) {
    void *p = calloc(count > 0 ? count : 1, size);
    NT_ASSERT(p != NULL && "frame_inspector: out of memory");
    return p;
}

static bool is_draw(uint32_t op) { return op == NT_GFX_OP_DRAW || op == NT_GFX_OP_DRAW_INSTANCED || op == NT_GFX_OP_DRAW_INDEXED || op == NT_GFX_OP_DRAW_INDEXED_INSTANCED; }
static bool is_instanced(uint32_t op) { return op == NT_GFX_OP_DRAW_INSTANCED || op == NT_GFX_OP_DRAW_INDEXED_INSTANCED; }
static bool is_indexed(uint32_t op) { return op == NT_GFX_OP_DRAW_INDEXED || op == NT_GFX_OP_DRAW_INDEXED_INSTANCED; }
static bool is_gfx(const nt_gfx_event_t *e) { return e->kind <= NT_GFX_EVENT_ARGUMENT; }
static bool in_execute(const fi_frame_t *fr, uint32_t i) { return fr->view.execute_end != 0 && i >= fr->view.execute_begin && i < fr->view.execute_end; }

/* The OP a gfx record belongs to, or FI_NONE. */
static uint32_t owner(const fi_frame_t *fr, uint32_t i) {
    const nt_gfx_event_t *e = &fr->view.events[i];
    return is_gfx(e) && e->link != 0 && e->link - 1U < fr->view.count ? e->link - 1U : FI_NONE;
}

uint64_t fi_definition_bytes(const nt_gfx_event_t *e) {
    if (e->object_kind == NT_GFX_OBJECT_BUFFER) {
        return e->data.resource.size;
    }
    if (e->object_kind != NT_GFX_OBJECT_TEXTURE) {
        return 0;
    }
    const uint32_t levels = e->data.resource.levels > 0 ? e->data.resource.levels : 1;
    uint64_t bytes = 0;
    for (uint32_t l = 0; l < levels && l < 32; l++) {
        bytes += nt_texture_level_bytes((nt_texture_format_t)e->data.resource.format, nt_texture_level_extent(e->data.resource.width, l), nt_texture_level_extent(e->data.resource.height, l));
    }
    return bytes;
}

const char *fi_node_name(fi_node_t node) {
    static const char *const k_names[FI_NODE_COUNT] = {"begin_frame", "snapshot: inherited state", "program links", "calls", "end_frame: frame storage, then the command stream", "after end_frame"};
    return node < FI_NODE_COUNT ? k_names[node] : "?";
}

const char *fi_break_name(fi_break_t brk) {
    static const char *const k_names[FI_BREAK_COUNT] = {
        "merged", "a command recorded between", "a different draw function", "the previous draw is instanced", "the range does not continue", "the summed count exceeds GLsizei"};
    return brk < FI_BREAK_COUNT ? k_names[brk] : "?";
}
// #endregion

// #region load
/* Points line[] at each record line of a document, past its index and indentation. */
static void index_lines(fi_frame_t *fr, const char *doc, const char *end) {
    fr->line = (const char **)fi_alloc(fr->view.count, sizeof(*fr->line));
    fr->text_end = end;
    uint32_t record = 0;
    for (const char *p = doc; p < end && record < fr->view.count;) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *next = nl != NULL ? nl + 1 : end;
        if (p[0] >= '0' && p[0] <= '9') {
            while (p < next && *p >= '0' && *p <= '9') {
                p++;
            }
            while (p < next && *p == ' ') {
                p++;
            }
            fr->line[record++] = p;
        }
        p = next;
    }
}

static void line_col(const char *text, size_t at, uint32_t *line, uint32_t *col) {
    *line = 1;
    size_t start = 0;
    for (size_t i = 0; i < at; i++) {
        if (text[i] == '\n') {
            (*line)++;
            start = i + 1;
        }
    }
    *col = (uint32_t)(at - start) + 1U;
}

static void index_frame(fi_frame_t *fr);

/* A zeroed frame at the end of the file's frames, which double when full. */
static fi_frame_t *push_frame(fi_file_t *file, uint32_t *cap) {
    if (file->frame_count == *cap) {
        *cap = *cap > 0 ? *cap * 2 : 8;
        fi_frame_t *grown = (fi_frame_t *)realloc(file->frames, *cap * sizeof(fi_frame_t));
        NT_ASSERT(grown != NULL && "frame_inspector: out of memory");
        file->frames = grown;
    }
    fi_frame_t *fr = &file->frames[file->frame_count++];
    memset(fr, 0, sizeof(*fr));
    return fr;
}

void fi_file_load(fi_file_t *file, const char *name, char *text, size_t len) {
    memset(file, 0, sizeof(*file));
    const size_t name_len = strlen(name);
    file->name = (char *)fi_alloc(name_len + 1, 1);
    memcpy(file->name, name, name_len);
    file->text = text;
    file->len = len;
    uint32_t lines = 1;
    for (size_t i = 0; i < len; i++) {
        lines += text[i] == '\n';
    }
    file->events = (nt_gfx_event_t *)fi_alloc(lines, sizeof(nt_gfx_event_t));
    uint32_t used = 0;
    uint32_t cap = 0;
    for (size_t at = 0; at < len;) {
        const nt_gfx_capture_parsed_t r = nt_gfx_capture_parse(text + at, len - at, file->events + used, lines - used);
        fi_frame_t *fr = push_frame(file, &cap);
        fr->view = r.view;
        fr->error = r.error;
        fr->merge_mismatch = FI_NONE;
        if (r.error != NULL) {
            line_col(text, at + r.error_at, &fr->error_line, &fr->error_col);
        }
        if (r.view.events != NULL) {
            used += r.view.count;
            index_lines(fr, text + at, text + at + r.next);
            index_frame(fr);
        }
        if (r.next == 0) {
            break;
        }
        at += r.next;
    }
}

bool fi_file_open(fi_file_t *file, const char *path) {
    const nt_fs_request_t req = nt_fs_read_file(path);
    uint32_t size = 0;
    uint8_t *data = nt_fs_state(req) == NT_FS_STATE_DONE ? nt_fs_take_data(req, &size) : NULL;
    nt_fs_free(req);
    if (data == NULL) {
        return false;
    }
    const char *name = path;
    for (const char *c = path; *c != '\0'; c++) {
        name = *c == '/' || *c == '\\' ? c + 1 : name;
    }
    fi_file_load(file, name, (char *)data, size);
    return true;
}

void fi_file_close(fi_file_t *file) {
    for (uint32_t f = 0; f < file->frame_count; f++) {
        fi_frame_t *fr = &file->frames[f];
        free((void *)fr->line);
        free(fr->row);
        free(fr->depth);
        free(fr->parent);
        free(fr->group);
        free(fr->group_key);
        free(fr->brk);
        free(fr->cause);
        free(fr->summary.group);
    }
    free(file->frames);
    free(file->events);
    free(file->text);
    free(file->name);
    memset(file, 0, sizeof(*file));
}

size_t fi_record_text(const fi_frame_t *frame, uint32_t record, const char **text) {
    const char *p = frame->line[record];
    const char *nl = memchr(p, '\n', (size_t)(frame->text_end - p));
    *text = p;
    return (size_t)((nl != NULL ? nl : frame->text_end) - p);
}
// #endregion

// #region tree
static uint32_t intern_group(fi_frame_t *fr, const char *base, const char *sep, const char *label) {
    char key[FI_KEY_LEN];
    (void)snprintf(key, sizeof(key), "%s%s%s", base, base[0] != '\0' ? sep : "", label);
    for (uint32_t g = 0; g < fr->group_count; g++) {
        if (strcmp(fr->group_key[g], key) == 0) {
            return g;
        }
    }
    memcpy(fr->group_key[fr->group_count], key, sizeof(key));
    return fr->group_count++;
}

/* Parent of every record: the record it belongs to (link, note, marker) or a phase of the frame. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void assign_parents(fi_frame_t *fr, uint32_t *stack) {
    const nt_gfx_event_t *ev = fr->view.events;
    const uint32_t n = fr->view.count;
    fr->first_op = n;
    for (uint32_t i = 0; i < n; i++) {
        if (ev[i].kind == NT_GFX_EVENT_OP) {
            fr->first_op = i;
            break;
        }
    }
    uint32_t top = 0;
    uint32_t note = FI_NONE;
    for (uint32_t i = 0; i < n; i++) {
        const nt_gfx_event_t *e = &ev[i];
        if (i == fr->view.execute_begin && fr->view.execute_end != 0) {
            note = FI_NONE; /* end_frame's work belongs to no note */
        }
        uint32_t phase = FI_NODE_BASE + FI_NODE_CALLS;
        if (fr->view.execute_end != 0 && i >= fr->view.execute_begin) {
            phase = FI_NODE_BASE + (i < fr->view.execute_end ? FI_NODE_END_FRAME : FI_NODE_AFTER);
        }
        const uint32_t scope = top > 0 ? stack[top - 1] : phase;
        uint32_t parent = scope;
        const char *base = top > 0 ? fr->group_key[fr->group[stack[top - 1]]] : "";
        switch (e->kind) {
        case NT_GFX_EVENT_MARKER_PUSH:
            fr->group[i] = intern_group(fr, base, "/", e->note.label);
            stack[top++] = i;
            note = FI_NONE;
            break;
        case NT_GFX_EVENT_MARKER_POP:
            top -= top > 0 ? 1U : 0U;
            note = FI_NONE;
            break;
        case NT_GFX_EVENT_NOTE:
            fr->group[i] = intern_group(fr, base, " > ", e->note.label);
            note = i;
            break;
        case NT_GFX_EVENT_NOTE_FIELD:
            parent = note != FI_NONE && i > 0 && (ev[i - 1].kind == NT_GFX_EVENT_NOTE || ev[i - 1].kind == NT_GFX_EVENT_NOTE_FIELD) ? note : scope;
            break;
        default:
            if (owner(fr, i) != FI_NONE && owner(fr, i) < i) {
                parent = owner(fr, i);
            } else if (e->kind == NT_GFX_EVENT_OP && (e->operation == NT_GFX_OP_CONTEXT || e->operation == NT_GFX_OP_TIMER_DISJOINT)) {
                parent = FI_NODE_BASE + FI_NODE_BEGIN_FRAME; /* only begin_frame restores and checks the timer */
            } else if (e->kind != NT_GFX_EVENT_OP) {
                const bool snapshot = i < fr->first_op && (e->kind == NT_GFX_EVENT_INITIAL || e->kind == NT_GFX_EVENT_DEFINITION);
                parent = FI_NODE_BASE + (snapshot ? FI_NODE_SNAPSHOT : FI_NODE_PROGRAM_LINKS);
            } else if (phase == FI_NODE_BASE + FI_NODE_END_FRAME) {
                parent = phase;
            } else if (note != FI_NONE) {
                parent = note;
            }
            break;
        }
        fr->parent[i] = parent;
        if (e->kind != NT_GFX_EVENT_MARKER_PUSH && e->kind != NT_GFX_EVENT_NOTE) {
            fr->group[i] = parent < n ? fr->group[parent] : 0;
        }
    }
}

/* Rows in display order: a preorder walk with children in record order, phases first-come. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void build_rows(fi_frame_t *fr) {
    const uint32_t n = fr->view.count;
    const uint32_t ids = n + FI_NODE_COUNT + 1U; /* records, phases, root */
    uint32_t *first = (uint32_t *)fi_alloc(ids, sizeof(uint32_t));
    uint32_t *last = (uint32_t *)fi_alloc(ids, sizeof(uint32_t));
    uint32_t *next = (uint32_t *)fi_alloc(ids, sizeof(uint32_t));
    uint32_t *stack = (uint32_t *)fi_alloc(ids, sizeof(uint32_t));
    uint16_t *stack_depth = (uint16_t *)fi_alloc(ids, sizeof(uint16_t));
    for (uint32_t i = 0; i < ids; i++) {
        first[i] = last[i] = next[i] = FI_NONE;
    }
    bool attached[FI_NODE_COUNT] = {false};
    for (uint32_t i = 0; i < n; i++) {
        uint32_t child = i;
        uint32_t parent = fr->parent[i] < n ? fr->parent[i] : n + (fr->parent[i] - FI_NODE_BASE);
        /* A phase joins its own parent when its first record arrives, so phases keep record order. */
        for (;;) {
            if (last[parent] == FI_NONE) {
                first[parent] = child;
            } else {
                next[last[parent]] = child;
            }
            last[parent] = child;
            if (parent < n || parent == n + FI_NODE_ROOT || attached[parent - n]) {
                break;
            }
            attached[parent - n] = true;
            const uint32_t node = parent - n;
            child = parent;
            parent = n + (node == FI_NODE_SNAPSHOT || node == FI_NODE_PROGRAM_LINKS ? FI_NODE_BEGIN_FRAME : FI_NODE_ROOT);
        }
    }
    fr->row = (uint32_t *)fi_alloc(ids, sizeof(uint32_t));
    fr->depth = (uint16_t *)fi_alloc(ids, sizeof(uint16_t));
    uint32_t sp = 0;
    if (first[n + FI_NODE_ROOT] != FI_NONE) {
        stack[sp] = first[n + FI_NODE_ROOT];
        stack_depth[sp++] = 0;
    }
    while (sp > 0) {
        const uint32_t id = stack[--sp];
        const uint16_t d = stack_depth[sp];
        fr->row[fr->row_count] = id < n ? id : FI_NODE_BASE + (id - n);
        fr->depth[fr->row_count++] = d;
        if (next[id] != FI_NONE) {
            stack[sp] = next[id];
            stack_depth[sp++] = d;
        }
        if (first[id] != FI_NONE) {
            stack[sp] = first[id];
            stack_depth[sp++] = (uint16_t)(d + 1U);
        }
    }
    free(first);
    free(last);
    free(next);
    free(stack);
    free(stack_depth);
}
// #endregion

// #region merge check
/* Whether an operation recorded a draw-phase command (nt_gfx.c), apart from plain and indexed draws. */
static bool records_command(const fi_frame_t *fr, const nt_gfx_event_t *e) {
    if (e->kind != NT_GFX_EVENT_OP || e->result != NT_GFX_RESULT_ACCEPTED || e->link != 0) {
        return false;
    }
    switch (e->operation) {
    case NT_GFX_OP_PASS:
    case NT_GFX_OP_END_PASS:
    case NT_GFX_OP_PIPELINE:
    case NT_GFX_OP_VERTEX_INPUT:
    case NT_GFX_OP_TEXTURE_SET:
    case NT_GFX_OP_SCISSOR:
    case NT_GFX_OP_SCISSOR_ENABLE:
    case NT_GFX_OP_VIEWPORT:
    case NT_GFX_OP_DRAW_INSTANCED:
    case NT_GFX_OP_DRAW_INDEXED_INSTANCED:
        return true;
    case NT_GFX_OP_CLEAR:
        return e->data.clear.color || e->data.clear.depth;
    case NT_GFX_OP_UBO:
        return e->object != 0; /* object 0 is the logical unbind */
    case NT_GFX_OP_SEGMENT_BEGIN:
    case NT_GFX_OP_SEGMENT_END:
        return fr->timing;
    default:
        return false;
    }
}

/* Predicts each plain/indexed draw's merge by the frontend rule and checks it against the recorded CACHE. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void check_merges(fi_frame_t *fr) {
    const nt_gfx_event_t *ev = fr->view.events;
    for (uint32_t i = 0; i < fr->view.count; i++) {
        fr->timing |= ev[i].kind == NT_GFX_EVENT_OP && ev[i].operation == NT_GFX_OP_TIMER_DISJOINT;
    }
    uint32_t last = FI_NONE; /* the last recorded command */
    uint32_t head = FI_NONE; /* the draw that opened the chain `last` belongs to */
    uint64_t chain_end = 0;
    uint64_t chain_count = 0;
    for (uint32_t i = 0; i < fr->view.count; i++) {
        const nt_gfx_event_t *e = &ev[i];
        const bool mergeable = e->kind == NT_GFX_EVENT_OP && e->link == 0 && (e->operation == NT_GFX_OP_DRAW || e->operation == NT_GFX_OP_DRAW_INDEXED);
        if (!mergeable || (e->result != NT_GFX_RESULT_ACCEPTED && e->result != NT_GFX_RESULT_CACHE)) {
            if (records_command(fr, e)) {
                last = i;
                head = FI_NONE;
            }
            continue;
        }
        const nt_gfx_event_t *prev = last != FI_NONE ? &ev[last] : NULL;
        const bool same = prev != NULL && head != FI_NONE && prev->operation == e->operation;
        const bool predicted = same && chain_end == e->data.draw.first && chain_count + e->data.draw.count <= INT32_MAX;
        if (e->result == NT_GFX_RESULT_CACHE) {
            if ((!predicted || e->detail != head + 1U) && fr->merge_mismatch == FI_NONE) {
                fr->merge_mismatch = i;
            }
            chain_end += e->data.draw.count;
            chain_count += e->data.draw.count;
            last = i;
            continue;
        }
        if (predicted && fr->merge_mismatch == FI_NONE) {
            fr->merge_mismatch = i;
        }
        fi_break_t brk = FI_BREAK_COMMAND;
        if (prev != NULL && is_instanced(prev->operation)) {
            brk = FI_BREAK_INSTANCED;
        } else if (prev != NULL && head != FI_NONE && prev->operation != e->operation) {
            brk = FI_BREAK_FUNCTION;
        } else if (same && chain_end != e->data.draw.first) {
            brk = FI_BREAK_RANGE;
        } else if (same) {
            brk = FI_BREAK_COUNT_LIMIT;
        }
        fr->brk[i] = (uint8_t)(predicted ? FI_BREAK_NONE : brk);
        fr->cause[i] = last;
        last = head = i;
        chain_end = (uint64_t)e->data.draw.first + e->data.draw.count;
        chain_count = e->data.draw.count;
    }
}
// #endregion

// #region summary
static fi_query_t q_make(fi_query_kind_t kind, uint32_t a, uint32_t b) { return (fi_query_t){.kind = (uint8_t)kind, .a = a, .b = (uint8_t)b}; }

/* The latest definition of a frontend object before record i. */
static const nt_gfx_event_t *definition_of(const fi_frame_t *fr, uint32_t i, uint32_t kind, uint32_t object) {
    for (uint32_t k = i; k-- > 0;) {
        const nt_gfx_event_t *e = &fr->view.events[k];
        if (e->kind == NT_GFX_EVENT_DEFINITION && e->operation == NT_GFX_OP_CREATE && e->object_kind == kind && e->object == object) {
            return e;
        }
    }
    return NULL;
}

/* Every summary number record i counts toward; the summary and the tree filter both read this. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static uint32_t contributions(const fi_frame_t *fr, uint32_t i, fi_query_t *q, uint64_t *sum) {
    const nt_gfx_event_t *e = &fr->view.events[i];
    const uint32_t g = fr->group[i];
    const uint32_t op = owner(fr, i);
    uint32_t c = 0;
#define FI_ADD_(kind, a, b, s) (q[c] = q_make((kind), (a), (b)), sum[c++] = (s))
    switch (e->kind) {
    case NT_GFX_EVENT_OP: {
        const uint32_t result = e->result < FI_RESULTS ? e->result : NT_GFX_RESULT_UNKNOWN;
        FI_ADD_(FI_Q_OP, e->operation, result, 0);
        if (e->link != 0) {
            FI_ADD_(FI_Q_OP_NESTED, e->operation, 0, 0);
        }
        if (result != NT_GFX_RESULT_ACCEPTED && result != NT_GFX_RESULT_CACHE && result != NT_GFX_RESULT_NONE) {
            FI_ADD_(FI_Q_PROBLEM, 0, 0, 0);
        }
        const bool counted = result == NT_GFX_RESULT_ACCEPTED || result == NT_GFX_RESULT_CACHE;
        if (is_draw(e->operation) && counted) {
            const uint64_t instances = e->data.draw.instances;
            FI_ADD_(FI_Q_VERTICES, 0, 0, (uint64_t)e->data.draw.vertices * instances);
            if (is_indexed(e->operation)) {
                FI_ADD_(FI_Q_INDICES, 0, 0, (uint64_t)e->data.draw.count * instances);
            }
            if (is_instanced(e->operation)) {
                FI_ADD_(FI_Q_INSTANCES, 0, 0, instances);
            }
            FI_ADD_(FI_Q_GROUP, g, FI_METRIC_DRAWS, 0);
            if (result == NT_GFX_RESULT_CACHE) {
                FI_ADD_(FI_Q_MERGED, 0, 0, 0);
            } else if (fr->brk[i] != FI_BREAK_NONE) {
                FI_ADD_(FI_Q_BREAK, fr->brk[i], 0, 0);
            }
        }
        if (e->operation == NT_GFX_OP_DESTROY && result == NT_GFX_RESULT_ACCEPTED && e->object_kind < FI_OBJECTS) {
            const nt_gfx_event_t *def = definition_of(fr, i, e->object_kind, e->object);
            FI_ADD_(FI_Q_RESOURCE, e->object_kind, FI_RES_DESTROYED, def != NULL ? fi_definition_bytes(def) : 0);
        }
        if (e->operation == NT_GFX_OP_BUFFER_UPLOAD && e->link == 0 && in_execute(fr, i)) {
            FI_ADD_(FI_Q_FRAME_STORE, 0, 0, e->data.resource.size);
        }
        break;
    }
    case NT_GFX_EVENT_BACKEND:
        FI_ADD_(FI_Q_GL_CALL, e->detail, 0, 0);
        FI_ADD_(FI_Q_GROUP, g, FI_METRIC_CALLS, 0);
        if (op != FI_NONE) {
            FI_ADD_(FI_Q_OP_CALLS, fr->view.events[op].operation, 0, 0);
        }
        if (e->data.backend.bytes > 0) {
            const bool buffer = e->detail == NT_GFX_GL_glBufferData || e->detail == NT_GFX_GL_glBufferSubData;
            FI_ADD_(FI_Q_UPLOAD, buffer ? 0U : 1U, 0, e->data.backend.bytes);
            FI_ADD_(FI_Q_GROUP, g, FI_METRIC_BYTES, e->data.backend.bytes);
        }
        break;
    case NT_GFX_EVENT_SKIP:
        if (op != FI_NONE) {
            FI_ADD_(FI_Q_OP_SKIPS, fr->view.events[op].operation, 0, 0);
        }
        FI_ADD_(FI_Q_PROBLEM, 0, 0, 0);
        break;
    case NT_GFX_EVENT_DEFINITION:
        if (e->operation == NT_GFX_OP_CREATE && e->object_kind != NT_GFX_OBJECT_NONE && e->object_kind < FI_OBJECTS) { /* ATTRIBUTE records name their object too */
            if (e->link == 0 && i < fr->first_op && e->data.resource.backend != 0) {                                   /* a husk defines backend 0 */
                FI_ADD_(FI_Q_RESOURCE, e->object_kind, FI_RES_LIVE, fi_definition_bytes(e));
            } else if (op != FI_NONE && fr->view.events[op].operation == NT_GFX_OP_CREATE) {
                FI_ADD_(FI_Q_RESOURCE, e->object_kind, FI_RES_CREATED, fi_definition_bytes(e));
            }
        }
        break;
    case NT_GFX_EVENT_NOTE_FIELD:
        if (e->field.type == NT_GFX_NOTE_STR && strcmp(e->field.key, "drop") == 0) {
            FI_ADD_(FI_Q_DROP, 0, 0, 0);
            FI_ADD_(FI_Q_PROBLEM, 0, 0, 0);
            FI_ADD_(FI_Q_GROUP, g, FI_METRIC_DROPS, 0);
        }
        break;
    default:
        break;
    }
#undef FI_ADD_
    return c;
}

static fi_cell_t *cell_of(const fi_frame_t *fr, fi_query_t q) {
    fi_summary_t *s = (fi_summary_t *)&fr->summary;
    const bool op = q.a < NT_GFX_OP_COUNT;
    switch ((fi_query_kind_t)q.kind) {
    case FI_Q_OP:
        return op && q.b < FI_RESULTS ? &s->op[q.a][q.b] : NULL;
    case FI_Q_OP_NESTED:
        return op ? &s->op_nested[q.a] : NULL;
    case FI_Q_OP_CALLS:
        return op ? &s->op_calls[q.a] : NULL;
    case FI_Q_OP_SKIPS:
        return op ? &s->op_skips[q.a] : NULL;
    case FI_Q_GL_CALL:
        return q.a < NT_GFX_GL_COUNT ? &s->gl[q.a] : NULL;
    case FI_Q_VERTICES:
        return &s->vertices;
    case FI_Q_INDICES:
        return &s->indices;
    case FI_Q_INSTANCES:
        return &s->instances;
    case FI_Q_MERGED:
        return &s->merged;
    case FI_Q_BREAK:
        return q.a < FI_BREAK_COUNT ? &s->brk[q.a] : NULL;
    case FI_Q_UPLOAD:
        return q.a < 2 ? &s->upload[q.a] : NULL;
    case FI_Q_FRAME_STORE:
        return &s->frame_store;
    case FI_Q_RESOURCE:
        return q.a < FI_OBJECTS && q.b < FI_RES_COUNT ? &s->resource[q.a][q.b] : NULL;
    case FI_Q_GROUP:
        return q.a < fr->group_count && q.b < FI_METRIC_COUNT ? &s->group[((size_t)q.a * FI_METRIC_COUNT) + q.b] : NULL;
    case FI_Q_DROP:
        return &s->drops;
    case FI_Q_PROBLEM:
        return &s->problems;
    case FI_Q_NONE:
        return NULL;
    }
    return NULL;
}

fi_cell_t fi_cell(const fi_frame_t *frame, fi_query_t q) {
    const fi_cell_t *c = frame->view.events != NULL ? cell_of(frame, q) : NULL;
    return c != NULL ? *c : (fi_cell_t){0};
}

bool fi_matches(const fi_frame_t *frame, uint32_t record, fi_query_t q) {
    fi_query_t qs[FI_MAX_CONTRIB];
    uint64_t sums[FI_MAX_CONTRIB];
    const uint32_t c = contributions(frame, record, qs, sums);
    for (uint32_t k = 0; k < c; k++) {
        if (qs[k].kind == q.kind && qs[k].a == q.a && qs[k].b == q.b) {
            return true;
        }
    }
    return false;
}

nt_gfx_counters_t fi_recount(const fi_frame_t *frame) {
    const fi_summary_t *s = &frame->summary;
    nt_gfx_counters_t c = {0};
    c.frame_sequence = frame->view.counters.frame_sequence;
    c.stream_bytes = frame->view.counters.stream_bytes;
    memcpy(c.frame_bytes, frame->view.counters.frame_bytes, sizeof(c.frame_bytes));
    for (uint32_t op = 0; op < NT_GFX_OP_COUNT; op++) {
        c.accepted[op] = (uint32_t)s->op[op][NT_GFX_RESULT_ACCEPTED].n;
    }
    for (uint32_t call = 0; call < NT_GFX_GL_COUNT; call++) {
        c.gl[call] = (uint32_t)s->gl[call].n;
    }
    c.vertices = s->vertices.sum;
    c.indices = s->indices.sum;
    c.instances = s->instances.sum;
    c.buffer_upload_calls = s->upload[0].n;
    c.buffer_upload_bytes = s->upload[0].sum;
    c.texture_upload_calls = s->upload[1].n;
    c.texture_upload_bytes = s->upload[1].sum;
    return c;
}

static void build_summary(fi_frame_t *fr) {
    fr->summary.group = (fi_cell_t *)fi_alloc((size_t)fr->group_count * FI_METRIC_COUNT, sizeof(fi_cell_t));
    fi_query_t qs[FI_MAX_CONTRIB];
    uint64_t sums[FI_MAX_CONTRIB];
    for (uint32_t i = 0; i < fr->view.count; i++) {
        const uint32_t c = contributions(fr, i, qs, sums);
        for (uint32_t k = 0; k < c; k++) {
            fi_cell_t *cell = cell_of(fr, qs[k]);
            if (cell != NULL) {
                cell->n++;
                cell->sum += sums[k];
            }
        }
    }
}
// #endregion

// #region state folds
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void fi_request_state(const fi_frame_t *frame, uint32_t at, fi_request_state_t *out) {
    memset(out, 0xFF, sizeof(*out));
    const nt_gfx_event_t *ev = frame->view.events;
    bool frontend = true; /* the snapshot's frontend records come before the backend INITIAL/STATE */
    for (uint32_t i = 0; i <= at && i < frame->view.count; i++) {
        const nt_gfx_event_t *e = &ev[i];
        if (e->kind == NT_GFX_EVENT_INITIAL) {
            frontend &= !(e->operation == NT_GFX_OP_STATE && e->detail == NT_GFX_INITIAL_BACKEND);
            if (frontend && e->operation == NT_GFX_OP_SCISSOR) {
                out->scissor = i;
            } else if (frontend && e->operation == NT_GFX_OP_SCISSOR_ENABLE) {
                out->scissor_enable = i;
            }
            continue;
        }
        if (e->kind != NT_GFX_EVENT_OP || e->link != 0) {
            continue;
        }
        const bool applied = e->result == NT_GFX_RESULT_ACCEPTED || e->result == NT_GFX_RESULT_CACHE;
        switch (e->operation) {
        case NT_GFX_OP_PASS: /* begin_pass discards the pass mirrors whatever it returns */
            out->pass = i;
            out->pipeline = out->vertex_input = out->texture_set = out->viewport = out->scissor_enable = FI_NONE;
            memset(out->ubo, 0xFF, sizeof(out->ubo));
            break;
        case NT_GFX_OP_END_PASS:
            out->pass = FI_NONE;
            break;
        case NT_GFX_OP_PIPELINE:
            if (applied) {
                out->pipeline = i;
            } else if (e->result == NT_GFX_RESULT_INVALID_HANDLE) { /* the unbind */
                out->pipeline = out->texture_set = FI_NONE;
            }
            break;
        case NT_GFX_OP_VERTEX_INPUT:
            out->vertex_input = applied ? i : out->vertex_input;
            break;
        case NT_GFX_OP_TEXTURE_SET:
            out->texture_set = applied ? i : out->texture_set;
            break;
        case NT_GFX_OP_VIEWPORT:
            out->viewport = applied ? i : out->viewport;
            break;
        case NT_GFX_OP_SCISSOR:
            out->scissor = applied ? i : out->scissor;
            break;
        case NT_GFX_OP_SCISSOR_ENABLE:
            out->scissor_enable = applied ? i : out->scissor_enable;
            break;
        case NT_GFX_OP_UBO:
            if (applied && e->data.binding.slot < 8) {
                out->ubo[e->data.binding.slot] = e->object != 0 ? i : FI_NONE;
            }
            break;
        default:
            break;
        }
    }
}

static void gl_set(fi_gl_state_t *st, uint32_t call, uint32_t k0, uint32_t k1, uint32_t record, bool inherited) {
    for (uint32_t k = 0; k < st->count; k++) {
        fi_gl_slot_t *slot = &st->slot[k];
        if (slot->call == call && slot->key[0] == k0 && slot->key[1] == k1) {
            slot->record = record;
            slot->inherited = inherited;
            return;
        }
    }
    if (st->count < FI_GL_SLOTS) {
        st->slot[st->count++] = (fi_gl_slot_t){.call = call, .key = {k0, k1}, .record = record, .inherited = inherited};
    }
}

#define FI_GL_TEXTURE0 0x84C0U
#define FI_GL_TEXTURE_2D 0x0DE1U
#define FI_GL_FRAMEBUFFER 0x8D40U
#define FI_GL_ARRAY_BUFFER 0x8892U

/* Backend snapshot: cached names (render architecture, "Frame observation"), then the calls in issue order. */
void fi_gl_state(const fi_frame_t *frame, uint32_t at, fi_gl_state_t *out) {
    out->count = 0;
    const nt_gfx_event_t *ev = frame->view.events;
    bool backend = false;
    uint32_t unit = 0;
    for (uint32_t i = 0; i <= at && i < frame->view.count; i++) {
        const nt_gfx_event_t *e = &ev[i];
        const uint32_t *a = e->data.backend.args;
        if (e->kind == NT_GFX_EVENT_OP && e->operation == NT_GFX_OP_CONTEXT && e->link == 0) {
            out->count = 0; /* a restored context starts at GL defaults; old names are dead */
            unit = 0;
            continue;
        }
        if (e->kind == NT_GFX_EVENT_INITIAL) {
            if (e->operation == NT_GFX_OP_STATE && e->detail == NT_GFX_INITIAL_BACKEND) {
                backend = true;
                unit = a[3];
                gl_set(out, NT_GFX_GL_glUseProgram, 0, 0, i, true);
                gl_set(out, NT_GFX_GL_glBindVertexArray, 0, 0, i, true);
                gl_set(out, NT_GFX_GL_glBindFramebuffer, FI_GL_FRAMEBUFFER, 0, i, true);
                gl_set(out, NT_GFX_GL_glActiveTexture, 0, 0, i, true);
                gl_set(out, NT_GFX_GL_glBindBuffer, FI_GL_ARRAY_BUFFER, 0, i, true);
            } else if (backend && e->operation == NT_GFX_OP_VIEWPORT) {
                gl_set(out, NT_GFX_GL_glViewport, 0, 0, i, true);
            } else if (backend && e->operation == NT_GFX_OP_PASS) {
                gl_set(out, NT_GFX_GL_glClearColor, 0, 0, i, true);
            } else if (backend && e->operation == NT_GFX_OP_PIPELINE) {
                gl_set(out, NT_GFX_GL_NONE, 0, 0, i, true); /* fixed-function cache in backend enums */
            } else if (backend && e->operation == NT_GFX_OP_TEXTURE) {
                gl_set(out, NT_GFX_GL_glBindTexture, a[0], FI_GL_TEXTURE_2D, i, true);
                gl_set(out, NT_GFX_GL_glBindSampler, a[0], 0, i, true);
            }
            continue;
        }
        if (e->kind != NT_GFX_EVENT_BACKEND) {
            continue;
        }
        switch (e->detail) {
        case NT_GFX_GL_glActiveTexture:
            unit = a[0] - FI_GL_TEXTURE0;
            gl_set(out, e->detail, 0, 0, i, false);
            break;
        case NT_GFX_GL_glBindTexture:
            gl_set(out, e->detail, unit, a[0], i, false);
            break;
        case NT_GFX_GL_glBindSampler:
        case NT_GFX_GL_glBindBuffer:
        case NT_GFX_GL_glBindFramebuffer:
        case NT_GFX_GL_glPixelStorei:
            gl_set(out, e->detail, a[0], 0, i, false);
            break;
        case NT_GFX_GL_glBindBufferRange:
            gl_set(out, e->detail, a[0], a[1], i, false);
            break;
        case NT_GFX_GL_glEnable:
        case NT_GFX_GL_glDisable:
            gl_set(out, NT_GFX_GL_glEnable, a[0], 0, i, false);
            break;
        case NT_GFX_GL_glUseProgram:
        case NT_GFX_GL_glBindVertexArray:
        case NT_GFX_GL_glViewport:
        case NT_GFX_GL_glScissor:
        case NT_GFX_GL_glDepthMask:
        case NT_GFX_GL_glDepthFunc:
        case NT_GFX_GL_glCullFace:
        case NT_GFX_GL_glBlendFuncSeparate:
        case NT_GFX_GL_glBlendEquationSeparate:
        case NT_GFX_GL_glBlendColor:
        case NT_GFX_GL_glPolygonOffset:
        case NT_GFX_GL_glClearColor:
        case NT_GFX_GL_glClearDepth:
        case NT_GFX_GL_glClearDepthf:
            gl_set(out, e->detail, 0, 0, i, false);
            break;
        default:
            break;
        }
    }
}
// #endregion

static void index_frame(fi_frame_t *fr) {
    const uint32_t n = fr->view.count;
    fr->parent = (uint32_t *)fi_alloc(n, sizeof(uint32_t));
    fr->group = (uint32_t *)fi_alloc(n, sizeof(uint32_t));
    fr->brk = (uint8_t *)fi_alloc(n, sizeof(uint8_t));
    fr->cause = (uint32_t *)fi_alloc(n, sizeof(uint32_t));
    uint32_t keys = 1;
    for (uint32_t i = 0; i < n; i++) {
        keys += fr->view.events[i].kind == NT_GFX_EVENT_NOTE || fr->view.events[i].kind == NT_GFX_EVENT_MARKER_PUSH;
    }
    fr->group_key = (char(*)[FI_KEY_LEN])fi_alloc(keys, FI_KEY_LEN);
    (void)snprintf(fr->group_key[0], FI_KEY_LEN, "(no note or marker)");
    fr->group_count = 1;
    uint32_t *stack = (uint32_t *)fi_alloc(n, sizeof(uint32_t));
    assign_parents(fr, stack);
    free(stack);
    build_rows(fr);
    for (uint32_t i = 0; i < n; i++) {
        fr->cause[i] = FI_NONE;
    }
    check_merges(fr);
    build_summary(fr);
}
