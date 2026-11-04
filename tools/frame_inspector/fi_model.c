#include "fi_model.h"

#include "core/nt_assert.h"
#include "fs/nt_fs.h"
#include "graphics/nt_gfx_capture_text.h"
#include "nt_texture_format.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FI_NODE_ROOT FI_NODE_COUNT
#define FI_MAX_CONTRIB 12

// #region helpers
static void *fi_alloc(size_t count, size_t size) {
    void *p = calloc(count > 0 ? count : 1, size);
    NT_ASSERT(p != NULL && "frame_inspector: out of memory");
    return p;
}

static bool is_instanced(uint32_t op) { return op == NT_GFX_OP_DRAW_INSTANCED || op == NT_GFX_OP_DRAW_INDEXED_INSTANCED; }
static bool is_indexed(uint32_t op) { return op == NT_GFX_OP_DRAW_INDEXED || op == NT_GFX_OP_DRAW_INDEXED_INSTANCED; }
static bool is_gfx(const nt_gfx_event_t *e) { return e->kind <= NT_GFX_EVENT_ARGUMENT; }
static bool in_execute(const fi_frame_t *fr, uint32_t i) { return fr->view.execute_end != 0 && i >= fr->view.execute_begin && i < fr->view.execute_end; }

bool fi_is_draw(const nt_gfx_event_t *e) {
    return e->kind == NT_GFX_EVENT_OP &&
           (e->operation == NT_GFX_OP_DRAW || e->operation == NT_GFX_OP_DRAW_INSTANCED || e->operation == NT_GFX_OP_DRAW_INDEXED || e->operation == NT_GFX_OP_DRAW_INDEXED_INSTANCED);
}

bool fi_is_mergeable_draw(const nt_gfx_event_t *e) { return e->kind == NT_GFX_EVENT_OP && (e->operation == NT_GFX_OP_DRAW || e->operation == NT_GFX_OP_DRAW_INDEXED); }

bool fi_is_draw_call(uint32_t call) {
    return call == NT_GFX_GL_glDrawArrays || call == NT_GFX_GL_glDrawArraysInstanced || call == NT_GFX_GL_glDrawElements || call == NT_GFX_GL_glDrawElementsInstanced;
}

/* The OP a gfx record belongs to, or FI_NONE. */
static uint32_t owner(const fi_frame_t *fr, uint32_t i) {
    const nt_gfx_event_t *e = &fr->view.events[i];
    return is_gfx(e) && e->link != 0 && e->link - 1U < fr->view.count ? e->link - 1U : FI_NONE;
}

/* The top-level operation a record belongs to, itself when it has no owner. */
static uint32_t top_request(const fi_frame_t *fr, uint32_t i) {
    while (owner(fr, i) != FI_NONE) {
        i = owner(fr, i);
    }
    return i;
}

/* Bytes GL was asked to hold for a resource definition: buffer size, texture levels. Depth sizes are those
 * of the GL storage type (DEPTH24 as 32-bit); the runtime never uploads depth, so the shared table has none. */
static uint64_t definition_bytes(const nt_gfx_event_t *e) {
    if (e->object_kind == NT_GFX_OBJECT_BUFFER) {
        return e->data.buffer.size;
    }
    if (e->object_kind != NT_GFX_OBJECT_TEXTURE) {
        return 0;
    }
    const nt_texture_format_t fmt = (nt_texture_format_t)e->data.texture.format;
    const uint64_t depth = fmt == NT_TEXTURE_FORMAT_DEPTH16 ? 2U : 4U;
    const uint32_t levels = e->data.texture.levels > 0 ? e->data.texture.levels : 1;
    uint64_t bytes = 0;
    for (uint32_t l = 0; l < levels && l < 32; l++) {
        const uint32_t w = nt_texture_level_extent(e->data.texture.width, l);
        const uint32_t h = nt_texture_level_extent(e->data.texture.height, l);
        bytes += nt_texture_format_is_depth(fmt) ? depth * w * h : nt_texture_level_bytes(fmt, w, h);
    }
    return bytes;
}

const char *fi_node_name(fi_node_t node) {
    static const char *const k_names[FI_NODE_COUNT] = {
        "begin_frame: inherited state, finished links", "inherited state (snapshot)", "finished program links", "game calls", "end_frame: frame storage upload", "after end_frame", "frame"};
    return node < FI_NODE_COUNT ? k_names[node] : "?";
}

const char *fi_break_name(fi_break_t brk) {
    static const char *const k_names[FI_BREAK_COUNT] = {
        "merged", "a command recorded between", "a different draw function", "the previous draw is instanced", "the range does not continue", "the summed count exceeds GLsizei"};
    return brk < FI_BREAK_COUNT ? k_names[brk] : "?";
}

static uint32_t table_size(uint32_t entries) {
    uint32_t slots = 16U;
    while (slots < 2U * entries) {
        slots *= 2U;
    }
    return slots;
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

void fi_one_line(const char *src, char *buf, size_t size, fi_text_t mode) {
    size_t n = 0;
    for (const char *c = src; *c != '\0' && n + 5U < size; c++) {
        const uint8_t b = (uint8_t)*c;
        const bool quoted = mode != FI_TEXT_PLAIN && (b == '"' || b == '\\');
        const bool token = mode == FI_TEXT_TOKEN && (b == ' ' || b == '#' || b > 0x7eU);
        if (b < 0x20U || b == 0x7fU || quoted || token) {
            n += (size_t)snprintf(buf + n, size - n, "\\x%02x", b);
        } else {
            buf[n++] = *c;
        }
    }
    buf[n] = '\0';
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void fi_file_load(fi_file_t *file, const char *name, char *text, size_t len) {
    memset(file, 0, sizeof(*file));
    const size_t name_size = (strlen(name) * 4U) + 5U;
    file->name = (char *)fi_alloc(name_size, 1);
    fi_one_line(name, file->name, name_size, FI_TEXT_PLAIN);
    file->text = text;
    file->len = len;
    /* Records are the lines that start with their index: a file of empty lines asks for no memory. A document's
     * strings never outgrow its text. */
    uint32_t records = 1;
    for (size_t i = 0; i < len; i++) {
        records += (i == 0 || text[i - 1] == '\n') && text[i] >= '0' && text[i] <= '9';
    }
    file->events = (nt_gfx_event_t *)fi_alloc(records, sizeof(nt_gfx_event_t));
    const uint32_t string_cap = (uint32_t)len + 1U;
    file->strings = (char *)fi_alloc(string_cap, 1);
    uint32_t used = 0;
    uint32_t string_used = 0;
    uint32_t cap = 0;
    uint32_t base_line = 1; /* the line the document starts on */
    for (size_t at = 0; at < len;) {
        const nt_gfx_capture_parsed_t r = nt_gfx_capture_parse(text + at, len - at, file->events + used, records - used, file->strings + string_used, string_cap - string_used);
        fi_frame_t *fr = push_frame(file, &cap);
        fr->view = r.view;
        fr->error = r.error;
        fr->merge_mismatch = FI_NONE;
        if (r.error != NULL) {
            size_t start = 0;
            fr->error_line = base_line;
            for (size_t i = 0; i < r.error_at; i++) {
                if (text[at + i] == '\n') {
                    fr->error_line++;
                    start = i + 1;
                }
            }
            fr->error_col = (uint32_t)(r.error_at - start) + 1U;
        }
        if (r.view.events != NULL) {
            used += r.view.count;
            string_used += r.view.string_bytes;
            index_lines(fr, text + at, text + at + r.next);
            index_frame(fr);
        }
        if (r.next == 0) {
            break;
        }
        for (size_t i = 0; i < r.next; i++) {
            base_line += text[at + i] == '\n';
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
        free(fr->record_row);
        free(fr->parent);
        free(fr->tally);
        free(fr->group);
        free(fr->group_key);
        free(fr->group_parent);
        free(fr->group_is_note);
        free((void *)fr->group_label);
        free(fr->alias_object);
        free(fr->alias_ordinal);
        free((void *)fr->alias_label);
        free(fr->alias_slot);
        free(fr->alias_kind);
        free(fr->def);
        free(fr->gl_first);
        free(fr->gl_ref);
        free(fr->buffer_upload);
        free(fr->brk);
        free(fr->cause);
        free(fr->summary.group);
    }
    free(file->frames);
    free(file->events);
    free(file->strings);
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
/* Interns a group by its parent group, kind and label; slot is an open-addressing table over the groups. */
static uint32_t intern_group(fi_frame_t *fr, uint32_t *slot, uint32_t mask, uint32_t parent, bool note, const char *label) {
    uint32_t h = (parent * 0x9E3779B1U) ^ (note ? 0x85EBCA77U : 0U);
    for (const char *c = label; *c != '\0'; c++) {
        h = (h ^ (uint8_t)*c) * 16777619U;
    }
    for (;; h++) {
        uint32_t *at = &slot[h & mask];
        if (*at == 0) {
            break;
        }
        const uint32_t g = *at - 1U;
        if (fr->group_parent[g] == parent && fr->group_is_note[g] == note && strcmp(fr->group_label[g], label) == 0) {
            return g;
        }
    }
    const uint32_t g = fr->group_count++;
    slot[h & mask] = g + 1U;
    fr->group_parent[g] = parent;
    fr->group_is_note[g] = note;
    fr->group_label[g] = label;
    const char *base = parent != 0 ? fr->group_key[parent] : "";
    const char *sep = note ? " > " : "/";
    char shown[FI_KEY_LEN];
    fi_one_line(label, shown, sizeof(shown), FI_TEXT_PLAIN);
    (void)snprintf(fr->group_key[g], FI_KEY_LEN, "%s%s%s", base, parent != 0 || note ? sep + (parent == 0) : "", shown); /* "> ui" at the top */
    return g;
}

/* The snapshot is the leading run of link-0 INITIAL/DEFINITION records; finished links follow it. */
static uint32_t snapshot_length(const nt_gfx_event_t *ev, uint32_t n) {
    uint32_t end = 0;
    while (end < n && ev[end].link == 0 && (ev[end].kind == NT_GFX_EVENT_INITIAL || ev[end].kind == NT_GFX_EVENT_DEFINITION)) {
        end++;
    }
    return end;
}

/* Parent of every record: the record it belongs to (link, note, marker) or a phase of the frame. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void assign_parents(fi_frame_t *fr, uint32_t *stack, uint32_t *slot, uint32_t mask) {
    const nt_gfx_event_t *ev = fr->view.events;
    const uint32_t n = fr->view.count;
    const uint32_t snapshot_end = snapshot_length(ev, n);
    uint32_t top = 0;
    uint32_t note = FI_NONE;
    uint32_t remake = 0; /* frame storage buffers begin_frame still remakes after a restored context */
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
        const uint32_t base = top > 0 ? fr->group[stack[top - 1]] : 0;
        uint32_t parent = scope;
        switch (e->kind) {
        case NT_GFX_EVENT_MARKER_PUSH:
            fr->group[i] = intern_group(fr, slot, mask, base, false, fr->view.strings + e->note.label);
            stack[top++] = i;
            note = FI_NONE;
            break;
        case NT_GFX_EVENT_MARKER_POP:
            top -= top > 0 ? 1U : 0U;
            note = FI_NONE;
            break;
        case NT_GFX_EVENT_NOTE:
            fr->group[i] = intern_group(fr, slot, mask, base, true, fr->view.strings + e->note.label);
            note = i;
            break;
        case NT_GFX_EVENT_NOTE_FIELD:
            parent = note != FI_NONE && i > 0 && (ev[i - 1].kind == NT_GFX_EVENT_NOTE || ev[i - 1].kind == NT_GFX_EVENT_NOTE_FIELD) ? note : scope;
            break;
        default: {
            const bool op = e->kind == NT_GFX_EVENT_OP;
            /* A restored context remakes each frame storage stream: DESTROY of the old buffer if any, then CREATE. */
            const bool storage = op && e->link == 0 && remake > 0 && e->object_kind == NT_GFX_OBJECT_BUFFER && (e->operation == NT_GFX_OP_DESTROY || e->operation == NT_GFX_OP_CREATE);
            remake -= storage && e->operation == NT_GFX_OP_CREATE ? 1U : 0U;
            remake = storage ? remake : 0U;
            if (op && e->link == 0 && e->operation == NT_GFX_OP_CONTEXT) {
                remake = e->result == NT_GFX_RESULT_ACCEPTED ? NT_GFX_FRAME_STREAM_COUNT : 0U;
            }
            if (owner(fr, i) != FI_NONE && owner(fr, i) < i) {
                parent = owner(fr, i);
            } else if (op && (storage || e->operation == NT_GFX_OP_CONTEXT || e->operation == NT_GFX_OP_TIMER_DISJOINT)) {
                parent = FI_NODE_BASE + FI_NODE_BEGIN_FRAME; /* only begin_frame restores, remakes and checks the timer */
            } else if (!op) {
                parent = FI_NODE_BASE + (i < snapshot_end ? FI_NODE_SNAPSHOT : FI_NODE_PROGRAM_LINKS);
            } else if (phase == FI_NODE_BASE + FI_NODE_END_FRAME) {
                parent = phase;
            } else if (note != FI_NONE) {
                parent = note;
            }
            break;
        }
        }
        fr->parent[i] = parent;
        if (e->kind != NT_GFX_EVENT_MARKER_PUSH && e->kind != NT_GFX_EVENT_NOTE) {
            fr->group[i] = parent < n ? fr->group[parent] : 0;
        }
    }
}

/* The tree parent of a phase: snapshot and finished links sit under begin_frame, the rest under the frame. */
static uint32_t node_parent(uint32_t node) {
    if (node == FI_NODE_SNAPSHOT || node == FI_NODE_PROGRAM_LINKS) {
        return FI_NODE_BEGIN_FRAME;
    }
    return node == FI_NODE_FRAME ? FI_NODE_ROOT : FI_NODE_FRAME;
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
            child = parent;
            parent = n + node_parent(parent - n);
        }
    }
    fr->row = (uint32_t *)fi_alloc(ids, sizeof(uint32_t));
    fr->depth = (uint16_t *)fi_alloc(ids, sizeof(uint16_t));
    fr->record_row = (uint32_t *)fi_alloc(n, sizeof(uint32_t));
    memset(fr->node_row, 0xFF, sizeof(fr->node_row));
    uint32_t sp = 0;
    if (first[n + FI_NODE_ROOT] != FI_NONE) {
        stack[sp] = first[n + FI_NODE_ROOT];
        stack_depth[sp++] = 0;
    }
    while (sp > 0) {
        const uint32_t id = stack[--sp];
        const uint16_t d = stack_depth[sp];
        if (id < n) {
            fr->record_row[id] = fr->row_count;
        } else {
            fr->node_row[id - n] = fr->row_count;
        }
        fr->row[fr->row_count] = id < n ? id : FI_NODE_BASE + (id - n);
        fr->depth[fr->row_count++] = d;
        if (next[id] != FI_NONE) {
            stack[sp] = next[id];
            stack_depth[sp++] = d;
        }
        if (first[id] != FI_NONE) {
            stack[sp] = first[id];
            stack_depth[sp++] = d < UINT16_MAX ? (uint16_t)(d + 1U) : d; /* a damaged file may nest without end */
        }
    }
    free(first);
    free(last);
    free(next);
    free(stack);
    free(stack_depth);
}

uint32_t fi_parent(const fi_frame_t *frame, uint32_t id) {
    if (id < frame->view.count) {
        return frame->parent[id];
    }
    const uint32_t up = id >= FI_NODE_BASE && id - FI_NODE_BASE < FI_NODE_COUNT ? node_parent(id - FI_NODE_BASE) : FI_NODE_ROOT;
    return up == FI_NODE_ROOT ? FI_NONE : FI_NODE_BASE + up;
}

uint32_t fi_joined(const fi_frame_t *frame, uint32_t record) {
    const nt_gfx_event_t *e = &frame->view.events[record];
    return fi_is_draw(e) && e->result == NT_GFX_RESULT_CACHE && e->detail > 0 && e->detail <= frame->view.count ? e->detail - 1U : FI_NONE;
}

uint32_t fi_gl_draw(const fi_frame_t *frame, uint32_t record) {
    const uint32_t joined = fi_joined(frame, record); /* the draw that wrote the command, never another merge */
    const uint32_t head = joined != FI_NONE ? joined : record;
    for (uint32_t i = head + 1U; i < frame->view.count; i++) {
        const nt_gfx_event_t *e = &frame->view.events[i];
        if (e->kind == NT_GFX_EVENT_BACKEND && e->link == head + 1U && fi_is_draw_call(e->detail)) {
            return i;
        }
    }
    return FI_NONE;
}

uint32_t fi_row_of(const fi_frame_t *frame, uint32_t id) {
    if (id < frame->view.count) {
        return frame->record_row[id];
    }
    return id >= FI_NODE_BASE && id - FI_NODE_BASE < FI_NODE_COUNT ? frame->node_row[id - FI_NODE_BASE] : FI_NONE;
}
// #endregion

// #region handles
/* The alias slot of (kind, object): its index + 1, or the empty slot to fill. */
static uint32_t *alias_slot(const fi_frame_t *fr, uint32_t kind, uint32_t object) {
    uint32_t h = (object * 0x9E3779B1U) ^ (kind * 0x85EBCA77U);
    for (;; h++) {
        uint32_t *slot = &fr->alias_slot[h & fr->alias_mask];
        if (*slot == 0 || (fr->alias_object[*slot - 1U] == object && fr->alias_kind[*slot - 1U] == kind)) {
            return slot;
        }
    }
}

fi_alias_t fi_alias(const fi_frame_t *frame, uint32_t kind, uint32_t object) {
    const uint32_t k = frame->alias_slot != NULL ? *alias_slot(frame, kind, object) : 0U;
    return k != 0 ? (fi_alias_t){frame->alias_label[k - 1U], frame->alias_ordinal[k - 1U]} : (fi_alias_t){"", 0};
}

/* The handles a record names, as kinds and offsets into it: its object, then its HANDLE fields (capture field
 * table), or a note field's handle. Backend records hold slots and GL names, which are not handles. */
enum { FI_HANDLES = 4 };
typedef struct {
    uint8_t kind[FI_HANDLES];
    uint16_t at[FI_HANDLES];
    const char *field[FI_HANDLES]; /* the capture field's name, "obj", or NULL for a note's handle */
    uint32_t count;
} handles_t;

static void add_handle(handles_t *h, uint32_t kind, size_t at, const char *field) {
    h->kind[h->count] = (uint8_t)kind;
    h->field[h->count] = field;
    h->at[h->count++] = (uint16_t)at;
}

static handles_t handles_of(const nt_gfx_event_t *e) {
    handles_t h = {0};
    if (e->kind == NT_GFX_EVENT_NOTE_FIELD && e->field.type == NT_GFX_NOTE_HANDLE && e->field.value.handle.kind < FI_OBJECTS) {
        add_handle(&h, e->field.value.handle.kind, offsetof(nt_gfx_event_t, field.value.handle.id), NULL);
    }
    if (!is_gfx(e)) {
        return h;
    }
    if (e->object_kind != 0 && e->object_kind < FI_OBJECTS) {
        add_handle(&h, e->object_kind, offsetof(nt_gfx_event_t, object), "obj");
    }
    const nt_gfx_capture_field_t *f = NULL;
    const uint32_t n = nt_gfx_capture_fields(e, &f);
    for (uint32_t k = 0; k < n; k++) {
        if (f[k].meaning == NT_GFX_MEANING_HANDLE) {
            NT_ASSERT(h.count < FI_HANDLES && "frame inspector: a record names more handles than FI_HANDLES");
            add_handle(&h, f[k].sub, f[k].offset, f[k].name);
        }
    }
    return h;
}

/* A definition's desc label (its STR field), 0 when its kind has none. */
static nt_gfx_capture_string_t label_of(const nt_gfx_event_t *e) {
    const nt_gfx_capture_field_t *f = NULL;
    const uint32_t n = nt_gfx_capture_fields(e, &f);
    for (uint32_t k = 0; k < n; k++) {
        if (f[k].type == NT_GFX_FIELD_STR) {
            nt_gfx_capture_string_t s = 0;
            memcpy(&s, (const unsigned char *)e + f[k].offset, sizeof(s));
            return s;
        }
    }
    return 0;
}

/* A resource definition's own backend identifier: its slot, or the GL name of a shader or sampler; 0 marks a husk. */
static uint32_t backend_of(const nt_gfx_event_t *e) {
    const nt_gfx_capture_field_t *f = NULL;
    const uint32_t n = nt_gfx_capture_fields(e, &f);
    for (uint32_t k = 0; k < n; k++) {
        if ((f[k].meaning == NT_GFX_MEANING_SLOT && f[k].sub == e->object_kind) || f[k].meaning == NT_GFX_MEANING_GL_NAME) {
            uint32_t v = 0;
            memcpy(&v, (const unsigned char *)e + f[k].offset, sizeof(v));
            return v;
        }
    }
    return 0;
}

static uint32_t handle_at(const nt_gfx_event_t *e, size_t at) {
    uint32_t v = 0;
    memcpy(&v, (const unsigned char *)e + at, sizeof(v));
    return v;
}

static uint32_t string_hash(const char *s) {
    uint32_t h = 2166136261U;
    for (; *s != '\0'; s++) {
        h = (h ^ (uint8_t)*s) * 16777619U;
    }
    return h;
}

/* Ordinals per kind and label, in alias order. */
static void number_aliases(fi_frame_t *fr, uint32_t count) {
    const uint32_t slots = table_size(count);
    uint32_t *first = (uint32_t *)fi_alloc(slots, sizeof(uint32_t));     /* per kind and label: its first alias + 1 */
    uint32_t *seen = (uint32_t *)fi_alloc(count + 1U, sizeof(uint32_t)); /* per first alias: the aliases of its kind and label so far */
    for (uint32_t a = 0; a < count; a++) {
        for (uint32_t h = string_hash(fr->alias_label[a]) ^ (fr->alias_kind[a] * 0x85EBCA77U);; h++) {
            uint32_t *slot = &first[h & (slots - 1U)];
            if (*slot == 0) {
                *slot = a + 1U;
                seen[a] = 1;
                fr->alias_ordinal[a] = 1;
                break;
            }
            const uint32_t b = *slot - 1U;
            if (fr->alias_kind[b] == fr->alias_kind[a] && strcmp(fr->alias_label[b], fr->alias_label[a]) == 0) {
                fr->alias_ordinal[a] = ++seen[b];
                break;
            }
        }
    }
    free(first);
    free(seen);
}

/* Aliases: every handle the records name, in order of first use after the snapshot, then the snapshot's, so a
 * live object the frame never touches shifts no number; a definition's label names its object. */
static void index_aliases(fi_frame_t *fr) {
    const uint32_t n = fr->view.count;
    const nt_gfx_event_t *ev = fr->view.events;
    uint32_t cap = 1;
    for (uint32_t i = 0; i < n; i++) {
        cap += handles_of(&ev[i]).count;
    }
    fr->alias_object = (uint32_t *)fi_alloc(cap, sizeof(uint32_t));
    fr->alias_ordinal = (uint32_t *)fi_alloc(cap, sizeof(uint32_t));
    fr->alias_kind = (uint8_t *)fi_alloc(cap, sizeof(uint8_t));
    fr->alias_label = (const char **)fi_alloc(cap, sizeof(const char *));
    const uint32_t slots = table_size(cap);
    fr->alias_slot = (uint32_t *)fi_alloc(slots, sizeof(uint32_t));
    fr->alias_mask = slots - 1U;
    const uint32_t snapshot = snapshot_length(ev, n);
    uint32_t count = 0;
    for (uint32_t s = 0; s < n; s++) {
        const uint32_t i = s < n - snapshot ? snapshot + s : s - (n - snapshot);
        const nt_gfx_event_t *e = &ev[i];
        const handles_t h = handles_of(e);
        for (uint32_t k = 0; k < h.count; k++) {
            const uint32_t object = handle_at(e, h.at[k]);
            uint32_t *slot = object != 0 ? alias_slot(fr, h.kind[k], object) : NULL;
            if (slot != NULL && *slot == 0) {
                *slot = ++count;
                fr->alias_object[count - 1U] = object;
                fr->alias_kind[count - 1U] = h.kind[k];
                fr->alias_label[count - 1U] = "";
            }
        }
        const nt_gfx_capture_string_t label = e->kind == NT_GFX_EVENT_DEFINITION && e->operation == NT_GFX_OP_CREATE ? label_of(e) : 0U;
        if (label != 0 && e->object_kind != 0 && e->object_kind < FI_OBJECTS && e->object != 0) {
            fr->alias_label[*alias_slot(fr, e->object_kind, e->object) - 1U] = fr->view.strings + label;
        }
    }
    number_aliases(fr, count);
}

/* Each operation's object definition and the bytes uploaded per buffer, in record order. */
static void index_handles(fi_frame_t *fr) {
    index_aliases(fr);
    const uint32_t n = fr->view.count;
    const uint32_t aliases = fr->alias_mask + 1U;
    fr->def = (uint32_t *)fi_alloc(n, sizeof(uint32_t));
    uint32_t *last_def = (uint32_t *)fi_alloc(aliases, sizeof(uint32_t)); /* per alias: its latest CREATE definition + 1 */
    uint32_t *upload = (uint32_t *)fi_alloc(aliases, sizeof(uint32_t));   /* per alias: its buffer upload row + 1 */
    fr->buffer_upload = (fi_buffer_upload_t *)fi_alloc(n, sizeof(fi_buffer_upload_t));
    for (uint32_t i = 0; i < n; i++) {
        const nt_gfx_event_t *e = &fr->view.events[i];
        fr->def[i] = FI_NONE;
        const uint32_t op = owner(fr, i);
        if (e->kind == NT_GFX_EVENT_BACKEND && e->data.backend.bytes > 0 && op != FI_NONE && fr->view.events[op].object_kind == NT_GFX_OBJECT_BUFFER && fr->view.events[op].object != 0) {
            const uint32_t k = *alias_slot(fr, NT_GFX_OBJECT_BUFFER, fr->view.events[op].object) - 1U; /* the op came first */
            if (upload[k] == 0) {
                fr->buffer_upload[fr->buffer_upload_count++] = (fi_buffer_upload_t){.object = fr->view.events[op].object, .first = op};
                upload[k] = fr->buffer_upload_count;
            }
            fr->buffer_upload[upload[k] - 1U].bytes += e->data.backend.bytes;
            fr->buffer_upload[upload[k] - 1U].calls++;
        }
        if (!is_gfx(e) || e->object_kind == 0 || e->object_kind >= FI_OBJECTS || e->object == 0) {
            continue;
        }
        const uint32_t k = *alias_slot(fr, e->object_kind, e->object) - 1U;
        if (e->kind == NT_GFX_EVENT_DEFINITION && e->operation == NT_GFX_OP_CREATE) {
            last_def[k] = i + 1U;
        } else if (e->kind == NT_GFX_EVENT_OP) {
            fr->def[i] = last_def[k] != 0 ? last_def[k] - 1U : FI_NONE;
        }
    }
    free(last_def);
    free(upload);
    for (uint32_t a = 1; a < fr->buffer_upload_count; a++) { /* insertion sort, biggest first */
        const fi_buffer_upload_t v = fr->buffer_upload[a];
        uint32_t b = a;
        while (b > 0 && fr->buffer_upload[b - 1].bytes < v.bytes) {
            fr->buffer_upload[b] = fr->buffer_upload[b - 1];
            b--;
        }
        fr->buffer_upload[b] = v;
    }
}
// #endregion

// #region gl calls
typedef struct {
    const fi_arg_t *args;
    uint8_t count;
    bool repeat;  /* the last argument repeats: gen/delete names, attachment lists */
    bool creates; /* its name arguments are names GL just made */
} fi_call_t;

// clang-format off: clang-format versions disagree on braces in a macro body
#define FI_U_ {FI_ARG_PLAIN, 0}
#define FI_I_ {FI_ARG_SIGNED, 0}
#define FI_P_ {FI_ARG_POINTER, 0}
#define FI_E_ {FI_ARG_ENUM, NT_GFX_DOMAIN_GL}
#define FI_D_(domain) {FI_ARG_ENUM, (domain)}
#define FI_N_(gl_kind) {FI_ARG_NAME, NT_GFX_GL_OBJECT_##gl_kind}
#define FI_CALL_(...) {(const fi_arg_t[]){__VA_ARGS__}, (uint8_t)(sizeof((const fi_arg_t[]){__VA_ARGS__}) / sizeof(fi_arg_t)), false, false}
#define FI_LIST_(...) {(const fi_arg_t[]){__VA_ARGS__}, (uint8_t)(sizeof((const fi_arg_t[]){__VA_ARGS__}) / sizeof(fi_arg_t)), true, false}
#define FI_GEN_(gl_kind) {(const fi_arg_t[]){FI_U_, FI_N_(gl_kind)}, 2, true, true}
#define FI_MAKE_(...) {(const fi_arg_t[]){__VA_ARGS__}, (uint8_t)(sizeof((const fi_arg_t[]){__VA_ARGS__}) / sizeof(fi_arg_t)), false, true}
// clang-format on
/* Arguments in capture order (pointers are presence bits, gen/delete the count then the names, a returned value
 * last). */
static const fi_call_t k_calls[NT_GFX_GL_COUNT] = {
    [NT_GFX_GL_glActiveTexture] = FI_CALL_(FI_D_(NT_GFX_DOMAIN_GL_TEXTURE_UNIT)),
    [NT_GFX_GL_glAttachShader] = FI_CALL_(FI_N_(PROGRAM), FI_N_(SHADER)),
    [NT_GFX_GL_glBeginQuery] = FI_CALL_(FI_E_, FI_N_(QUERY)),
    [NT_GFX_GL_glBindBuffer] = FI_CALL_(FI_E_, FI_N_(BUFFER)),
    [NT_GFX_GL_glBindBufferRange] = FI_CALL_(FI_E_, FI_U_, FI_N_(BUFFER), FI_U_, FI_U_),
    [NT_GFX_GL_glBindFramebuffer] = FI_CALL_(FI_E_, FI_N_(FRAMEBUFFER)),
    [NT_GFX_GL_glBindSampler] = FI_CALL_(FI_U_, FI_N_(SAMPLER)),
    [NT_GFX_GL_glBindTexture] = FI_CALL_(FI_E_, FI_N_(TEXTURE)),
    [NT_GFX_GL_glBindVertexArray] = FI_CALL_(FI_N_(VERTEX_ARRAY)),
    [NT_GFX_GL_glBlendEquationSeparate] = FI_CALL_(FI_E_, FI_E_),
    [NT_GFX_GL_glBlendFuncSeparate] = FI_LIST_(FI_D_(NT_GFX_DOMAIN_GL_BLEND_FACTOR)),
    [NT_GFX_GL_glBufferData] = FI_CALL_(FI_E_, FI_U_, FI_P_, FI_E_),
    [NT_GFX_GL_glBufferSubData] = FI_CALL_(FI_E_, FI_U_, FI_U_, FI_P_),
    [NT_GFX_GL_glCheckFramebufferStatus] = FI_CALL_(FI_E_, FI_E_),
    [NT_GFX_GL_glClear] = FI_CALL_(FI_D_(FI_DOMAIN_GL_CLEAR_MASK)),
    [NT_GFX_GL_glCompileShader] = FI_CALL_(FI_N_(SHADER)),
    [NT_GFX_GL_glCompressedTexImage2D] = FI_CALL_(FI_E_, FI_U_, FI_E_, FI_U_, FI_U_, FI_U_, FI_U_, FI_P_),
    [NT_GFX_GL_glCreateProgram] = FI_MAKE_(FI_N_(PROGRAM)),
    [NT_GFX_GL_glCreateShader] = FI_MAKE_(FI_E_, FI_N_(SHADER)),
    [NT_GFX_GL_glCullFace] = FI_CALL_(FI_E_),
    [NT_GFX_GL_glDeleteBuffers] = FI_LIST_(FI_U_, FI_N_(BUFFER)),
    [NT_GFX_GL_glDeleteFramebuffers] = FI_LIST_(FI_U_, FI_N_(FRAMEBUFFER)),
    [NT_GFX_GL_glDeleteProgram] = FI_CALL_(FI_N_(PROGRAM)),
    [NT_GFX_GL_glDeleteQueries] = FI_LIST_(FI_U_, FI_N_(QUERY)),
    [NT_GFX_GL_glDeleteSamplers] = FI_LIST_(FI_U_, FI_N_(SAMPLER)),
    [NT_GFX_GL_glDeleteShader] = FI_CALL_(FI_N_(SHADER)),
    [NT_GFX_GL_glDeleteTextures] = FI_LIST_(FI_U_, FI_N_(TEXTURE)),
    [NT_GFX_GL_glDeleteVertexArrays] = FI_LIST_(FI_U_, FI_N_(VERTEX_ARRAY)),
    [NT_GFX_GL_glDepthFunc] = FI_CALL_(FI_E_),
    [NT_GFX_GL_glDepthMask] = FI_CALL_(FI_D_(FI_DOMAIN_GL_BOOL)),
    [NT_GFX_GL_glDisable] = FI_CALL_(FI_E_),
    [NT_GFX_GL_glDrawArrays] = FI_CALL_(FI_D_(FI_DOMAIN_GL_PRIMITIVE), FI_U_, FI_U_),
    [NT_GFX_GL_glDrawArraysInstanced] = FI_CALL_(FI_D_(FI_DOMAIN_GL_PRIMITIVE), FI_U_, FI_U_, FI_U_),
    [NT_GFX_GL_glDrawBuffers] = FI_CALL_(FI_U_, FI_P_),
    [NT_GFX_GL_glDrawElements] = FI_CALL_(FI_D_(FI_DOMAIN_GL_PRIMITIVE), FI_U_, FI_E_, FI_U_),
    [NT_GFX_GL_glDrawElementsInstanced] = FI_CALL_(FI_D_(FI_DOMAIN_GL_PRIMITIVE), FI_U_, FI_E_, FI_U_, FI_U_),
    [NT_GFX_GL_glEnable] = FI_CALL_(FI_E_),
    [NT_GFX_GL_glEndQuery] = FI_CALL_(FI_E_),
    [NT_GFX_GL_glFramebufferTexture2D] = FI_CALL_(FI_E_, FI_E_, FI_E_, FI_N_(TEXTURE), FI_U_),
    [NT_GFX_GL_glGenBuffers] = FI_GEN_(BUFFER),
    [NT_GFX_GL_glGenFramebuffers] = FI_GEN_(FRAMEBUFFER),
    [NT_GFX_GL_glGenQueries] = FI_GEN_(QUERY),
    [NT_GFX_GL_glGenSamplers] = FI_GEN_(SAMPLER),
    [NT_GFX_GL_glGenTextures] = FI_GEN_(TEXTURE),
    [NT_GFX_GL_glGenVertexArrays] = FI_GEN_(VERTEX_ARRAY),
    [NT_GFX_GL_glGenerateMipmap] = FI_CALL_(FI_E_),
    [NT_GFX_GL_glGetActiveUniform] = FI_CALL_(FI_N_(PROGRAM)),
    [NT_GFX_GL_glGetActiveUniformBlockName] = FI_CALL_(FI_N_(PROGRAM)),
    [NT_GFX_GL_glGetActiveUniformBlockiv] = FI_CALL_(FI_N_(PROGRAM), FI_U_, FI_E_, FI_P_),
    [NT_GFX_GL_glGetActiveUniformsiv] = FI_CALL_(FI_N_(PROGRAM)),
    [NT_GFX_GL_glGetError] = FI_CALL_(FI_E_),
    [NT_GFX_GL_glGetProgramInfoLog] = FI_CALL_(FI_N_(PROGRAM)),
    [NT_GFX_GL_glGetProgramiv] = FI_CALL_(FI_N_(PROGRAM), FI_E_, FI_P_),
    [NT_GFX_GL_glGetQueryObjectui64v] = FI_CALL_(FI_N_(QUERY), FI_E_, FI_P_),
    [NT_GFX_GL_glGetQueryObjectuiv] = FI_CALL_(FI_N_(QUERY), FI_E_, FI_P_),
    [NT_GFX_GL_glGetShaderInfoLog] = FI_CALL_(FI_N_(SHADER)),
    [NT_GFX_GL_glGetShaderiv] = FI_CALL_(FI_N_(SHADER), FI_E_, FI_P_),
    [NT_GFX_GL_glGetString] = FI_CALL_(FI_E_, FI_P_),
    [NT_GFX_GL_glGetUniformLocation] = FI_CALL_(FI_N_(PROGRAM), FI_P_, FI_I_),
    [NT_GFX_GL_glInvalidateFramebuffer] = FI_LIST_(FI_E_, FI_U_, FI_E_),
    [NT_GFX_GL_glLinkProgram] = FI_CALL_(FI_N_(PROGRAM)),
    [NT_GFX_GL_glPixelStorei] = FI_CALL_(FI_E_, FI_U_),
    [NT_GFX_GL_glReadBuffer] = FI_CALL_(FI_E_),
    [NT_GFX_GL_glReadPixels] = FI_CALL_(FI_I_, FI_I_, FI_U_, FI_U_, FI_E_, FI_E_, FI_P_),
    [NT_GFX_GL_glSamplerParameteri] = FI_CALL_(FI_N_(SAMPLER), FI_E_, FI_E_),
    [NT_GFX_GL_glScissor] = FI_CALL_(FI_I_, FI_I_, FI_U_, FI_U_),
    [NT_GFX_GL_glShaderSource] = FI_CALL_(FI_N_(SHADER), FI_U_, FI_P_, FI_P_),
    [NT_GFX_GL_glTexImage2D] = FI_CALL_(FI_E_, FI_U_, FI_E_, FI_U_, FI_U_, FI_U_, FI_E_, FI_E_, FI_P_),
    [NT_GFX_GL_glTexParameteri] = FI_CALL_(FI_E_, FI_E_, FI_E_),
    [NT_GFX_GL_glTexSubImage2D] = FI_CALL_(FI_E_, FI_U_, FI_U_, FI_U_, FI_U_, FI_U_, FI_E_, FI_E_, FI_P_),
    [NT_GFX_GL_glUniform1i] = FI_CALL_(FI_I_, FI_I_),
    [NT_GFX_GL_glUniformBlockBinding] = FI_CALL_(FI_N_(PROGRAM)),
    [NT_GFX_GL_glUseProgram] = FI_CALL_(FI_N_(PROGRAM)),
    [NT_GFX_GL_glVertexAttribPointer] = FI_CALL_(FI_U_, FI_U_, FI_E_, FI_D_(FI_DOMAIN_GL_BOOL), FI_U_, FI_U_),
    [NT_GFX_GL_glViewport] = FI_CALL_(FI_I_, FI_I_, FI_U_, FI_U_),
};
#undef FI_MAKE_
#undef FI_GEN_
#undef FI_LIST_
#undef FI_CALL_
#undef FI_U_
#undef FI_I_
#undef FI_P_
#undef FI_E_
#undef FI_D_
#undef FI_N_

fi_arg_t fi_call_arg(uint32_t call, uint32_t k) {
    const fi_call_t *c = call < NT_GFX_GL_COUNT ? &k_calls[call] : &(fi_call_t){0};
    if (k < c->count) {
        return c->args[k];
    }
    return c->repeat && c->count > 0 ? c->args[c->count - 1U] : (fi_arg_t){FI_ARG_PLAIN, 0};
}

static bool creates_names(uint32_t call) { return call < NT_GFX_GL_COUNT && k_calls[call].creates; }
// #endregion

// #region gl names
/* The GL namespace a frontend object's backend name lives in; NONE when GL has no object for it. */
static uint8_t gl_kind_of(uint32_t kind) {
    switch (kind) {
    case NT_GFX_OBJECT_SHADER:
        return NT_GFX_GL_OBJECT_SHADER;
    case NT_GFX_OBJECT_PROGRAM:
        return NT_GFX_GL_OBJECT_PROGRAM;
    case NT_GFX_OBJECT_BUFFER:
        return NT_GFX_GL_OBJECT_BUFFER;
    case NT_GFX_OBJECT_TEXTURE:
        return NT_GFX_GL_OBJECT_TEXTURE;
    case NT_GFX_OBJECT_SAMPLER:
        return NT_GFX_GL_OBJECT_SAMPLER;
    case NT_GFX_OBJECT_VERTEX_INPUT:
        return NT_GFX_GL_OBJECT_VERTEX_ARRAY;
    case NT_GFX_OBJECT_RENDER_TARGET:
        return NT_GFX_GL_OBJECT_FRAMEBUFFER;
    default:
        return NT_GFX_GL_OBJECT_NONE;
    }
}

const char *fi_gl_role(uint8_t owner) {
    switch ((fi_gl_owner_t)owner) {
    case FI_GL_UPLOAD_VAO:
        return "upload VAO";
    case FI_GL_TIMER_QUERY:
        return "timer query";
    case FI_GL_UNKNOWN:
    case FI_GL_OBJECT:
        return NULL;
    }
    return NULL;
}

/* The GL names a record holds: its GL_NAME fields, then an issued call's name arguments. */
enum { FI_GL_NAMES = 24 };
typedef struct {
    uint16_t at[FI_GL_NAMES];
    uint8_t gl[FI_GL_NAMES];
    uint32_t count;
} gl_names_t;

static void add_gl_name(gl_names_t *g, size_t at, uint8_t gl) {
    NT_ASSERT(g->count < FI_GL_NAMES && "frame inspector: a record holds more GL names than FI_GL_NAMES");
    g->at[g->count] = (uint16_t)at;
    g->gl[g->count++] = gl;
}

static gl_names_t gl_names_of(const nt_gfx_event_t *e) {
    gl_names_t g = {0};
    if (!is_gfx(e)) {
        return g;
    }
    const nt_gfx_capture_field_t *f = NULL;
    const uint32_t n = nt_gfx_capture_fields(e, &f);
    for (uint32_t k = 0; k < n; k++) {
        if (f[k].meaning == NT_GFX_MEANING_GL_NAME) {
            add_gl_name(&g, f[k].offset, f[k].sub);
        }
    }
    const uint32_t ints = e->kind == NT_GFX_EVENT_BACKEND ? e->arg_count & 15U : 0U;
    for (uint32_t k = 0; k < ints; k++) {
        const fi_arg_t a = fi_call_arg(e->detail, k);
        if (a.kind == FI_ARG_NAME) {
            add_gl_name(&g, offsetof(nt_gfx_event_t, data.backend.args) + ((size_t)k * sizeof(uint32_t)), a.sub);
        }
    }
    return g;
}

/* Who a GL name or a backend slot belongs to as the frame recorded it so far. A name's owner is an object
 * directly, an object's backend slot (via_slot: id is the slot, kind its object kind), or a role; a slot's
 * owner is the object (id) that holds it. space 0 marks an empty entry. */
typedef struct {
    uint32_t key, id;
    uint8_t space, owner, kind;
    bool via_slot;
} gl_owner_t;
typedef struct {
    gl_owner_t *entry;
    uint32_t mask;
} gl_table_t;

/* The entry of (space, key), made empty when the table has none: lookups insert too, which the tables' size
 * allows (every name of every record, plus a slot per record). */
static gl_owner_t *gl_entry(gl_table_t *t, uint8_t space, uint32_t key) {
    for (uint32_t h = (key * 0x9E3779B1U) ^ (space * 0x85EBCA77U);; h++) {
        gl_owner_t *e = &t->entry[h & t->mask];
        if (e->space == 0) {
            *e = (gl_owner_t){.key = key, .space = space};
            return e;
        }
        if (e->space == space && e->key == key) {
            return e;
        }
    }
}

typedef struct {
    gl_table_t names, slots;
} gl_owners_t;

static void own_name(gl_owners_t *o, uint8_t gl, uint32_t name, gl_owner_t owner) {
    gl_owner_t *e = gl_entry(&o->names, gl, name);
    e->owner = owner.owner;
    e->kind = owner.kind;
    e->id = owner.id;
    e->via_slot = owner.via_slot;
}

static void own_slot(gl_owners_t *o, uint32_t kind, uint32_t slot, uint32_t object) { gl_entry(&o->slots, (uint8_t)kind, slot)->id = object; }

/* In an operation on an object of a backend definition's kind, the slot is that object's before its frontend
 * definition says so. A husk's 0 slot or name is never looked up. */
static void learn_definition(gl_owners_t *o, const nt_gfx_event_t *e, const nt_gfx_event_t *op) {
    if (e->operation == NT_GFX_OP_STATE) {
        const uint32_t slot = e->data.backend_resource.slot;
        const uint32_t name = e->data.backend_resource.gl_name;
        const uint8_t gl = gl_kind_of(e->detail);
        if (gl == NT_GFX_GL_OBJECT_NONE) {
            return;
        }
        own_name(o, gl, name, (gl_owner_t){.owner = FI_GL_OBJECT, .kind = (uint8_t)e->detail, .id = slot, .via_slot = true});
        if (op != NULL && op->object_kind == e->detail && op->object != 0) {
            own_slot(o, e->detail, slot, op->object);
        }
        return;
    }
    const uint32_t v = backend_of(e);
    if (e->operation != NT_GFX_OP_CREATE || e->object_kind >= FI_OBJECTS || e->object == 0) {
        return;
    }
    if (e->object_kind == NT_GFX_OBJECT_SHADER || e->object_kind == NT_GFX_OBJECT_SAMPLER) { /* backend_of: their GL name */
        own_name(o, gl_kind_of(e->object_kind), v, (gl_owner_t){.owner = FI_GL_OBJECT, .kind = e->object_kind, .id = e->object});
    } else {
        own_slot(o, e->object_kind, v, e->object);
    }
}

/* Names a create call returns (render architecture, "Capture text"). */
static gl_owner_t created_owner(const fi_frame_t *fr, uint32_t i, uint8_t gl, uint32_t name) {
    const nt_gfx_event_t *ev = fr->view.events;
    const uint32_t op = owner(fr, i);
    if (op != FI_NONE && gl_kind_of(ev[op].object_kind) == gl && ev[op].object != 0) {
        return (gl_owner_t){.owner = FI_GL_OBJECT, .kind = ev[op].object_kind, .id = ev[op].object};
    }
    for (uint32_t j = i + 1U; op != FI_NONE && j < fr->view.count && top_request(fr, j) == top_request(fr, i); j++) {
        const nt_gfx_event_t *d = &ev[j];
        if (d->link != ev[i].link || d->kind != NT_GFX_EVENT_DEFINITION || d->operation != NT_GFX_OP_CREATE) {
            continue;
        }
        const gl_names_t g = gl_names_of(d);
        for (uint32_t k = 0; k < g.count; k++) {
            if (g.gl[k] == gl && handle_at(d, g.at[k]) == name) {
                return (gl_owner_t){.owner = FI_GL_OBJECT, .kind = d->object_kind, .id = d->object};
            }
        }
    }
    const bool restore = op != FI_NONE && ev[op].kind == NT_GFX_EVENT_OP && ev[op].operation == NT_GFX_OP_CONTEXT;
    return (gl_owner_t){.owner = restore && gl == NT_GFX_GL_OBJECT_VERTEX_ARRAY ? FI_GL_UPLOAD_VAO : FI_GL_UNKNOWN};
}

/* What record i tells about GL names, applied before its own names resolve. */
static void learn(const fi_frame_t *fr, gl_owners_t *o, uint32_t i) {
    const nt_gfx_event_t *e = &fr->view.events[i];
    if (e->kind == NT_GFX_EVENT_DEFINITION) {
        const uint32_t op = owner(fr, i);
        learn_definition(o, e, op != FI_NONE ? &fr->view.events[op] : NULL);
    } else if (e->kind == NT_GFX_EVENT_INITIAL && e->operation == NT_GFX_OP_STATE && e->detail == NT_GFX_INITIAL_BACKEND && e->data.backend_state.upload_vertex_array != 0) {
        own_name(o, NT_GFX_GL_OBJECT_VERTEX_ARRAY, e->data.backend_state.upload_vertex_array, (gl_owner_t){.owner = FI_GL_UPLOAD_VAO});
    } else if (e->kind == NT_GFX_EVENT_BACKEND && creates_names(e->detail)) {
        const gl_names_t g = gl_names_of(e);
        for (uint32_t k = 0; k < g.count; k++) {
            const uint32_t name = handle_at(e, g.at[k]);
            if (name != 0) {
                own_name(o, g.gl[k], name, created_owner(fr, i, g.gl[k], name));
            }
        }
    }
}

/* The engine's only queries are its timer queries, so a query name reads as one even when made before the frame. */
static fi_gl_ref_t resolve(gl_owners_t *o, uint8_t gl, uint32_t name, uint16_t at) {
    const gl_owner_t *e = gl_entry(&o->names, gl, name);
    fi_gl_ref_t ref = {.at = at, .owner = gl == NT_GFX_GL_OBJECT_QUERY ? (uint8_t)FI_GL_TIMER_QUERY : e->owner, .kind = e->kind, .object = e->id};
    if (e->owner == FI_GL_OBJECT && e->via_slot) {
        ref.object = gl_entry(&o->slots, e->kind, e->id)->id;
        ref.owner = ref.object != 0 ? FI_GL_OBJECT : FI_GL_UNKNOWN;
    }
    return ref;
}

/* Every GL name of every record resolved at its record, in one forward pass: slots and GL names are reused within
 * a frame, so one final map would misname earlier rows (render architecture, "Capture text"). */
static void index_gl_names(fi_frame_t *fr) {
    const uint32_t n = fr->view.count;
    const nt_gfx_event_t *ev = fr->view.events;
    uint32_t total = 0;
    for (uint32_t i = 0; i < n; i++) {
        total += gl_names_of(&ev[i]).count;
    }
    fr->gl_first = (uint32_t *)fi_alloc((size_t)n + 1U, sizeof(uint32_t));
    fr->gl_ref = (fi_gl_ref_t *)fi_alloc(total, sizeof(fi_gl_ref_t));
    const uint32_t slots = table_size(total + n);
    gl_owners_t o = {{(gl_owner_t *)fi_alloc(slots, sizeof(gl_owner_t)), slots - 1U}, {(gl_owner_t *)fi_alloc(slots, sizeof(gl_owner_t)), slots - 1U}};
    const uint32_t snapshot = snapshot_length(ev, n);
    for (uint32_t i = 0; i < snapshot; i++) {
        learn(fr, &o, i);
    }
    uint32_t count = 0;
    for (uint32_t i = 0; i < n; i++) {
        const nt_gfx_event_t *e = &ev[i];
        if (i >= snapshot) {
            learn(fr, &o, i);
        }
        fr->gl_first[i] = count;
        const gl_names_t g = gl_names_of(e);
        for (uint32_t k = 0; k < g.count; k++) {
            const uint32_t name = handle_at(e, g.at[k]);
            if (name != 0) {
                fr->gl_ref[count++] = resolve(&o, g.gl[k], name, g.at[k]);
            }
        }
    }
    fr->gl_first[n] = count;
    free(o.names.entry);
    free(o.slots.entry);
}

fi_gl_ref_t fi_gl_ref(const fi_frame_t *frame, uint32_t record, uint32_t at) {
    for (uint32_t k = frame->gl_first[record]; k < frame->gl_first[record + 1U]; k++) {
        if (frame->gl_ref[k].at == at) {
            return frame->gl_ref[k];
        }
    }
    return (fi_gl_ref_t){.at = (uint16_t)at};
}
// #endregion

// #region objects
/* Short names for an issued call's arguments: the field of fi_named_t. */
static const char *const k_arg_names[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11"};
_Static_assert(sizeof(k_arg_names) / sizeof(k_arg_names[0]) == sizeof(((nt_gfx_event_t *)0)->data.backend.args) / sizeof(uint32_t), "frame inspector: a name per issued call argument");

_Static_assert(FI_NAMED >= FI_HANDLES + FI_GL_NAMES, "frame inspector: FI_NAMED holds every handle and GL name of a record");

static void add_named(fi_named_t *out, uint32_t *n, const char *field, uint32_t kind, uint32_t object) {
    if (object != 0) {
        out[(*n)++] = (fi_named_t){.field = field, .kind = (uint8_t)kind, .object = object};
    }
}

/* The field a GL name at byte offset `at` sits in: an issued call's argument index, else its capture field. */
static const char *gl_name_field(const nt_gfx_event_t *e, size_t at) {
    const size_t args = offsetof(nt_gfx_event_t, data.backend.args);
    if (e->kind == NT_GFX_EVENT_BACKEND && at >= args) {
        return k_arg_names[(at - args) / sizeof(uint32_t)];
    }
    const nt_gfx_capture_field_t *f = NULL;
    const uint32_t count = nt_gfx_capture_fields(e, &f);
    for (uint32_t k = 0;; k++) {
        NT_ASSERT(k < count && "frame inspector: a GL name outside its record's fields");
        if (f[k].offset == at) {
            return f[k].name;
        }
    }
}

uint32_t fi_named_objects(const fi_frame_t *frame, uint32_t record, fi_named_t out[FI_NAMED]) {
    const nt_gfx_event_t *e = &frame->view.events[record];
    uint32_t n = 0;
    const handles_t h = handles_of(e);
    for (uint32_t k = 0; k < h.count; k++) {
        add_named(out, &n, h.field[k] != NULL ? h.field[k] : frame->view.strings + e->field.key, h.kind[k], handle_at(e, h.at[k]));
    }
    for (uint32_t k = frame->gl_first[record]; k < frame->gl_first[record + 1U]; k++) {
        const fi_gl_ref_t *ref = &frame->gl_ref[k];
        if (ref->owner != FI_GL_OBJECT || (ref->kind == e->object_kind && ref->object == e->object)) { /* a definition's own name */
            continue;
        }
        add_named(out, &n, gl_name_field(e, ref->at), ref->kind, ref->object);
    }
    return n;
}

void fi_object(const fi_frame_t *frame, uint32_t kind, uint32_t object, fi_object_t *out) {
    const uint32_t n = frame->view.count;
    memset(out, 0, sizeof(*out));
    out->kind = (uint8_t)kind;
    out->object = object;
    out->definition = FI_NONE;
    uint32_t *hits = (uint32_t *)fi_alloc(n, sizeof(uint32_t));
    out->span = (fi_gl_span_t *)fi_alloc(frame->gl_first[n], sizeof(fi_gl_span_t)); /* at most one per GL name */
    uint32_t *per_group = (uint32_t *)fi_alloc((size_t)frame->group_count + 1U, sizeof(uint32_t));
    uint32_t count = 0;
    for (uint32_t i = 0; i < n; i++) {
        const nt_gfx_event_t *e = &frame->view.events[i];
        if (e->kind == NT_GFX_EVENT_DEFINITION && e->operation == NT_GFX_OP_CREATE && e->object_kind == kind && e->object == object) {
            out->definition = i;
        }
        fi_named_t named[FI_NAMED];
        const uint32_t m = fi_named_objects(frame, i, named);
        bool names = false;
        for (uint32_t k = 0; k < m; k++) {
            names = names || (named[k].kind == kind && named[k].object == object);
        }
        for (uint32_t k = frame->gl_first[i]; k < frame->gl_first[i + 1U]; k++) {
            const fi_gl_ref_t *ref = &frame->gl_ref[k];
            const uint32_t name = handle_at(e, ref->at);
            const bool held = ref->owner == FI_GL_OBJECT && ref->kind == kind && ref->object == object;
            if (held && (out->span_count == 0 || out->span[out->span_count - 1U].name != name)) {
                out->span[out->span_count++] = (fi_gl_span_t){.name = name, .from = i};
            }
        }
        if (names) {
            hits[count++] = i;
            per_group[frame->group[i] + 1U]++;
        }
    }
    for (uint32_t g = 1; g <= frame->group_count; g++) { /* counting sort by group, stable in record order */
        per_group[g] += per_group[g - 1U];
    }
    out->record = (uint32_t *)fi_alloc(count, sizeof(uint32_t));
    out->count = count;
    for (uint32_t k = 0; k < count; k++) {
        out->record[per_group[frame->group[hits[k]]]++] = hits[k];
    }
    free(per_group);
    free(hits);
}

void fi_object_free(fi_object_t *object) {
    free(object->record);
    free(object->span);
    memset(object, 0, sizeof(*object));
}
// #endregion

// #region merge check
/* A request the command stream carries to end_frame (nt_gfx.c), whatever its result. */
static bool streamed(const nt_gfx_event_t *e) {
    if (e->kind != NT_GFX_EVENT_OP || e->link != 0) {
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
    case NT_GFX_OP_DRAW:
    case NT_GFX_OP_DRAW_INDEXED:
    case NT_GFX_OP_DRAW_INSTANCED:
    case NT_GFX_OP_DRAW_INDEXED_INSTANCED:
    case NT_GFX_OP_CLEAR:
    case NT_GFX_OP_UBO:
    case NT_GFX_OP_SEGMENT_BEGIN:
    case NT_GFX_OP_SEGMENT_END:
        return true;
    default:
        return false;
    }
}

/* Whether an operation recorded a draw-phase command, apart from plain and indexed draws. */
static bool records_command(const nt_gfx_event_t *e, bool timing) {
    if (!streamed(e) || e->result != NT_GFX_RESULT_ACCEPTED || fi_is_mergeable_draw(e)) {
        return false;
    }
    switch (e->operation) {
    case NT_GFX_OP_CLEAR:
        return e->data.clear.color || e->data.clear.depth;
    case NT_GFX_OP_UBO:
        return e->object != 0; /* object 0 is the logical unbind */
    case NT_GFX_OP_SEGMENT_BEGIN:
    case NT_GFX_OP_SEGMENT_END:
        return timing; /* segments record commands only when the frame runs GPU timing */
    default:
        return true;
    }
}

/* Predicts each plain/indexed draw's merge by the frontend rule and checks it against the recorded CACHE. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void check_merges(fi_frame_t *fr) {
    const nt_gfx_event_t *ev = fr->view.events;
    bool timing = false;
    for (uint32_t i = 0; i < fr->view.count; i++) {
        timing |= ev[i].kind == NT_GFX_EVENT_OP && ev[i].operation == NT_GFX_OP_TIMER_DISJOINT;
    }
    uint32_t last = FI_NONE; /* the last recorded command */
    uint32_t head = FI_NONE; /* the draw that opened the chain `last` belongs to */
    uint64_t chain_end = 0;
    uint64_t chain_count = 0;
    for (uint32_t i = 0; i < fr->view.count; i++) {
        const nt_gfx_event_t *e = &ev[i];
        const bool mergeable = e->kind == NT_GFX_EVENT_OP && e->link == 0 && (e->operation == NT_GFX_OP_DRAW || e->operation == NT_GFX_OP_DRAW_INDEXED);
        if (!mergeable || (e->result != NT_GFX_RESULT_ACCEPTED && e->result != NT_GFX_RESULT_CACHE)) {
            if (records_command(e, timing)) {
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

/* Every summary number record i counts toward; the summary, the tree filter, tallies and ranges all read this. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static uint32_t contributions(const fi_frame_t *fr, uint32_t i, fi_query_t *q, uint64_t *sum) {
    const nt_gfx_event_t *e = &fr->view.events[i];
    const uint32_t g = fr->group[i];
    const uint32_t op = owner(fr, i);
    uint32_t c = 0;
#define FI_ADD_(kind, a, b, s)                                                                                                                                                                         \
    do {                                                                                                                                                                                               \
        NT_ASSERT(c < FI_MAX_CONTRIB);                                                                                                                                                                 \
        q[c] = q_make((kind), (a), (b));                                                                                                                                                               \
        sum[c++] = (s);                                                                                                                                                                                \
    } while (0)
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
        if (fi_is_draw(e) && counted) {
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
            const nt_gfx_event_t *def = fr->def[i] != FI_NONE ? &fr->view.events[fr->def[i]] : NULL;
            FI_ADD_(FI_Q_RESOURCE, e->object_kind, FI_RES_DESTROYED, def != NULL && backend_of(def) != 0 ? definition_bytes(def) : 0); /* a husk held nothing */
        }
        if (e->operation == NT_GFX_OP_BUFFER_UPLOAD && e->link == 0 && in_execute(fr, i)) {
            FI_ADD_(FI_Q_FRAME_STORE, 0, 0, e->data.upload.size);
        }
        break;
    }
    case NT_GFX_EVENT_BACKEND:
        FI_ADD_(FI_Q_GL_CALL, e->detail, 0, 0);
        FI_ADD_(FI_Q_GL_ANY, 0, 0, 0);
        if (fi_is_draw_call(e->detail)) {
            FI_ADD_(FI_Q_DRAW_CALL, 0, 0, 0);
            FI_ADD_(FI_Q_GROUP, g, FI_METRIC_DRAW_CALLS, 0);
        }
        FI_ADD_(FI_Q_GROUP, g, FI_METRIC_CALLS, 0);
        if (op != FI_NONE) {
            FI_ADD_(FI_Q_OP_CALLS, fr->view.events[op].operation, 0, 0);
        }
        if (e->data.backend.bytes > 0) {
            const bool buffer = e->detail == NT_GFX_GL_glBufferData || e->detail == NT_GFX_GL_glBufferSubData;
            FI_ADD_(FI_Q_UPLOAD, buffer ? 0U : 1U, 0, e->data.backend.bytes);
            FI_ADD_(FI_Q_UPLOAD, 2, 0, e->data.backend.bytes);
            FI_ADD_(FI_Q_GROUP, g, FI_METRIC_BYTES, e->data.backend.bytes);
        }
        break;
    case NT_GFX_EVENT_SKIP:
        FI_ADD_(FI_Q_SKIP, 0, 0, 0);
        if (op != FI_NONE) {
            FI_ADD_(FI_Q_OP_SKIPS, fr->view.events[op].operation, 0, 0);
        }
        break;
    case NT_GFX_EVENT_DEFINITION:
        if (e->operation == NT_GFX_OP_CREATE && e->object_kind != NT_GFX_OBJECT_NONE && e->object_kind < FI_OBJECTS) { /* ATTRIBUTE records name their object too */
            if (fr->parent[i] == FI_NODE_BASE + FI_NODE_SNAPSHOT && backend_of(e) != 0) {                              /* a husk defines backend 0 */
                FI_ADD_(FI_Q_RESOURCE, e->object_kind, FI_RES_LIVE, definition_bytes(e));
            } else if (op != FI_NONE && fr->view.events[op].operation == NT_GFX_OP_CREATE) {
                FI_ADD_(FI_Q_RESOURCE, e->object_kind, FI_RES_CREATED, definition_bytes(e));
            }
        }
        break;
    case NT_GFX_EVENT_NOTE_FIELD:
        if (e->field.type == NT_GFX_NOTE_STR && strcmp(fr->view.strings + e->field.key, "drop") == 0) {
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

/* The cell a query adds to; groups only where the summary has them. */
static fi_cell_t *cell_in(fi_summary_t *s, uint32_t group_count, fi_query_t q) {
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
    case FI_Q_GL_ANY:
        return &s->gl_any;
    case FI_Q_DRAW_CALL:
        return &s->draw_calls;
    case FI_Q_SKIP:
        return &s->skips;
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
        return q.a < 3 ? &s->upload[q.a] : NULL;
    case FI_Q_FRAME_STORE:
        return &s->frame_store;
    case FI_Q_RESOURCE:
        return q.a < FI_OBJECTS && q.b < FI_RES_COUNT ? &s->resource[q.a][q.b] : NULL;
    case FI_Q_GROUP:
        return s->group != NULL && q.a < group_count && q.b < FI_METRIC_COUNT ? &s->group[((size_t)q.a * FI_METRIC_COUNT) + q.b] : NULL;
    case FI_Q_DROP:
        return &s->drops;
    case FI_Q_PROBLEM:
        return &s->problems;
    case FI_Q_NONE:
        return NULL;
    }
    return NULL;
}

static void add_contributions(const fi_frame_t *fr, fi_summary_t *s, const fi_query_t *qs, const uint64_t *sums, uint32_t c) {
    for (uint32_t k = 0; k < c; k++) {
        fi_cell_t *cell = cell_in(s, fr->group_count, qs[k]);
        if (cell != NULL) {
            cell->n++;
            cell->sum += sums[k];
        }
    }
}

fi_cell_t fi_cell(const fi_frame_t *frame, fi_query_t q) {
    const fi_cell_t *c = frame->view.events != NULL ? cell_in((fi_summary_t *)&frame->summary, frame->group_count, q) : NULL;
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

uint32_t fi_range(const fi_frame_t *frame, uint32_t lo, uint32_t hi, fi_summary_t *out) {
    memset(out, 0, sizeof(*out));
    uint32_t records = 0;
    for (uint32_t r = lo; r <= hi && r < frame->row_count; r++) {
        if (frame->row[r] < FI_NODE_BASE) {
            fi_query_t qs[FI_MAX_CONTRIB];
            uint64_t sums[FI_MAX_CONTRIB];
            add_contributions(frame, out, qs, sums, contributions(frame, frame->row[r], qs, sums));
            records++;
        }
    }
    return records;
}

fi_tally_t fi_tally(const fi_frame_t *frame, uint32_t id) {
    const uint32_t n = frame->view.count;
    if (id < n) {
        return frame->tally[id];
    }
    return id >= FI_NODE_BASE && id - FI_NODE_BASE < FI_NODE_COUNT ? frame->tally[n + (id - FI_NODE_BASE)] : (fi_tally_t){0};
}

/* Whether group a of frame x and group b of frame y have the same marker path and note. */
static bool same_group(const fi_frame_t *x, uint32_t a, const fi_frame_t *y, uint32_t b) {
    for (; a != 0 && b != 0; a = x->group_parent[a], b = y->group_parent[b]) {
        if (x->group_is_note[a] != y->group_is_note[b] || strcmp(x->group_label[a], y->group_label[b]) != 0) {
            return false;
        }
    }
    return a == b;
}

uint32_t fi_group_in(const fi_frame_t *to, const fi_frame_t *from, uint32_t g) {
    for (uint32_t k = 0; k < to->group_count && g < from->group_count; k++) {
        if (same_group(to, k, from, g)) {
            return k;
        }
    }
    return FI_NONE;
}

/* The frame's counters rebuilt from its records; stream and storage sizes come from the view. */
static nt_gfx_counters_t recount(const fi_frame_t *frame) {
    const fi_summary_t *s = &frame->summary;
    nt_gfx_counters_t c;
    memset(&c, 0, sizeof(c)); /* padding too: counters compare as bytes */
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

uint32_t fi_counter_rows(const fi_frame_t *frame, fi_counter_row_t rows[FI_COUNTER_ROWS]) {
    const nt_gfx_counters_t *c = &frame->view.counters;
    const nt_gfx_counters_t r = recount(frame);
    uint64_t frame_bytes = 0;
    for (uint32_t k = 0; k < NT_GFX_FRAME_STREAM_COUNT; k++) {
        frame_bytes += c->frame_bytes[k];
    }
    uint32_t n = 0;
#define FI_ROW_(label, a, b, is_bytes) (rows[n++] = (fi_counter_row_t){.name = label, .counter = (a), .recount = (b), .bytes = (is_bytes)})
    FI_ROW_("vertices", c->vertices, r.vertices, false);
    FI_ROW_("indices", c->indices, r.indices, false);
    FI_ROW_("instances", c->instances, r.instances, false);
    FI_ROW_("buffer_upload_calls", c->buffer_upload_calls, r.buffer_upload_calls, false);
    FI_ROW_("buffer_upload_bytes", c->buffer_upload_bytes, r.buffer_upload_bytes, true);
    FI_ROW_("texture_upload_calls", c->texture_upload_calls, r.texture_upload_calls, false);
    FI_ROW_("texture_upload_bytes", c->texture_upload_bytes, r.texture_upload_bytes, true);
    FI_ROW_("frame_store_bytes", frame_bytes, frame->summary.frame_store.sum, true);
#undef FI_ROW_
    for (uint32_t op = 0; op < NT_GFX_OP_COUNT; op++) {
        if (c->accepted[op] != 0 || r.accepted[op] != 0) {
            rows[n] = (fi_counter_row_t){.counter = c->accepted[op], .recount = r.accepted[op]};
            (void)snprintf(rows[n++].name, sizeof(rows[0].name), "accepted.%s", nt_gfx_operation_name(op));
        }
    }
    for (uint32_t k = 1; k < NT_GFX_GL_COUNT; k++) {
        if (c->gl[k] != 0 || r.gl[k] != 0) {
            rows[n] = (fi_counter_row_t){.counter = c->gl[k], .recount = r.gl[k]};
            (void)snprintf(rows[n++].name, sizeof(rows[0].name), "gl.%s", nt_gfx_gl_call_name(k));
        }
    }
    return n;
}

/* The summary, and per tree row what its subtree drew, from the same contributions. */
static void build_summary(fi_frame_t *fr) {
    const uint32_t n = fr->view.count;
    fr->summary.group = (fi_cell_t *)fi_alloc((size_t)fr->group_count * FI_METRIC_COUNT, sizeof(fi_cell_t));
    fr->tally = (fi_tally_t *)fi_alloc((size_t)n + FI_NODE_COUNT, sizeof(fi_tally_t));
    fi_query_t qs[FI_MAX_CONTRIB];
    uint64_t sums[FI_MAX_CONTRIB];
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t c = contributions(fr, i, qs, sums);
        add_contributions(fr, &fr->summary, qs, sums, c);
        fi_tally_t add = {0};
        for (uint32_t k = 0; k < c; k++) {
            add.draws += qs[k].kind == FI_Q_VERTICES ? 1U : 0U; /* every counted draw adds vertices */
            add.merged += qs[k].kind == FI_Q_MERGED ? 1U : 0U;
            add.gl_draws += qs[k].kind == FI_Q_DRAW_CALL ? 1U : 0U;
        }
        if (add.draws + add.gl_draws == 0) {
            continue;
        }
        uint32_t p = fr->parent[i];
        for (;;) {
            const uint32_t at = p < n ? p : n + (p - FI_NODE_BASE);
            fr->tally[at].draws += add.draws;
            fr->tally[at].merged += add.merged;
            fr->tally[at].gl_draws += add.gl_draws;
            if (p < n) {
                p = fr->parent[p];
            } else if (p - FI_NODE_BASE != FI_NODE_FRAME) {
                p = FI_NODE_BASE + node_parent(p - FI_NODE_BASE);
            } else {
                break;
            }
        }
    }
}
// #endregion

// #region state folds
/* An OP sets a piece of state only when ACCEPTED: CACHE found it set already, so the earlier setter stays. */
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
        const bool set = e->result == NT_GFX_RESULT_ACCEPTED;
        switch (e->operation) {
        case NT_GFX_OP_CONTEXT: /* a restored context starts with scissor off */
            out->scissor_enable = set ? FI_NONE : out->scissor_enable;
            break;
        case NT_GFX_OP_PASS: /* begin_pass discards the pass mirrors whatever it returns */
            out->pass = set ? i : FI_NONE;
            out->pipeline = out->vertex_input = out->texture_set = out->viewport = out->scissor_enable = FI_NONE;
            memset(out->ubo, 0xFF, sizeof(out->ubo));
            break;
        case NT_GFX_OP_END_PASS:
            out->pass = FI_NONE;
            break;
        case NT_GFX_OP_PIPELINE:
            if (set) {
                /* The texture set is program state: a pipeline on another program needs a new one (nt_gfx bind_pipeline). */
                const uint32_t before = out->pipeline != FI_NONE ? frame->def[out->pipeline] : FI_NONE;
                const uint32_t after = frame->def[i];
                const bool same_program = before != FI_NONE && after != FI_NONE && ev[before].data.pipeline.program == ev[after].data.pipeline.program;
                out->texture_set = same_program ? out->texture_set : FI_NONE;
                out->pipeline = i;
            } else if (e->result == NT_GFX_RESULT_INVALID_HANDLE) { /* the unbind */
                out->pipeline = out->texture_set = FI_NONE;
            }
            break;
        case NT_GFX_OP_VERTEX_INPUT:
            out->vertex_input = set ? i : out->vertex_input;
            break;
        case NT_GFX_OP_TEXTURE_SET:
            out->texture_set = set ? i : out->texture_set;
            break;
        case NT_GFX_OP_VIEWPORT:
            out->viewport = set ? i : out->viewport;
            break;
        case NT_GFX_OP_SCISSOR:
            out->scissor = set ? i : out->scissor;
            break;
        case NT_GFX_OP_SCISSOR_ENABLE:
            out->scissor_enable = set ? i : out->scissor_enable;
            break;
        case NT_GFX_OP_UBO:
            if (set && e->data.ubo.slot < 8) {
                out->ubo[e->data.ubo.slot] = e->object != 0 ? i : FI_NONE;
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
    } else {
        st->full = true;
    }
}

static void gl_drop(fi_gl_state_t *st, uint32_t call, uint32_t k0) {
    for (uint32_t k = 0; k < st->count; k++) {
        if (st->slot[k].call == call && st->slot[k].key[0] == k0) {
            st->slot[k] = st->slot[--st->count];
            return;
        }
    }
}

#define FI_GL_TEXTURE0 0x84C0U
#define FI_GL_TEXTURE_2D 0x0DE1U
#define FI_GL_FRAMEBUFFER 0x8D40U
#define FI_GL_ARRAY_BUFFER 0x8892U
#define FI_GL_ELEMENT_ARRAY_BUFFER 0x8893U

/* A texture unit from its GL enum (GL_TEXTURE0 + unit). */
static uint32_t unit_of(uint32_t gl_enum) { return gl_enum >= FI_GL_TEXTURE0 ? gl_enum - FI_GL_TEXTURE0 : 0U; }

/* Backend snapshot: cached names (render architecture, "Frame observation"), then the calls in issue order. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void fi_gl_state(const fi_frame_t *frame, uint32_t at, fi_gl_state_t *out) {
    out->count = 0;
    out->full = false;
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
                unit = unit_of(e->data.backend_state.active_texture);
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
                gl_set(out, NT_GFX_GL_glBindTexture, e->data.texture_unit.unit, FI_GL_TEXTURE_2D, i, true);
                gl_set(out, NT_GFX_GL_glBindSampler, e->data.texture_unit.unit, 0, i, true);
            }
            continue;
        }
        if (e->kind != NT_GFX_EVENT_BACKEND) {
            continue;
        }
        switch (e->detail) {
        case NT_GFX_GL_glActiveTexture:
            unit = unit_of(a[0]);
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
        case NT_GFX_GL_glBindVertexArray:
            gl_drop(out, NT_GFX_GL_glBindBuffer, FI_GL_ELEMENT_ARRAY_BUFFER); /* the element buffer is VAO state */
            gl_set(out, e->detail, 0, 0, i, false);
            break;
        case NT_GFX_GL_glUseProgram:
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

void fi_state_at(const fi_frame_t *frame, uint32_t at, fi_state_view_t *out) {
    *out = (fi_state_view_t){.compare = FI_COMPARE_PREVIOUS_DRAW};
    /* A record of an operation (a GL call at end_frame, an argument, a definition) has the engine state of its
     * top-level operation, wherever the record lands. */
    const uint32_t request = top_request(frame, at);
    fi_request_state(frame, request, &out->req);
    out->was = previous(frame, request, is_draw_op);
    out->was_req = out->req;
    if (out->was != FI_NONE) {
        fi_request_state(frame, out->was, &out->was_req);
    }
    /* An operation's draw-phase calls issue at end_frame: its GL state is the one after its last call. A merged
     * draw issued nothing; it ran with the call of the draw it joined. An operation and a GL call are their own GL point
     * (a nested operation too); an argument or a definition is its operation's. */
    const nt_gfx_event_kind_t kind = frame->view.events[at].kind;
    const bool own_point = kind == NT_GFX_EVENT_OP || kind == NT_GFX_EVENT_BACKEND || kind == NT_GFX_EVENT_SKIP || owner(frame, at) == FI_NONE;
    const uint32_t gl_anchor = own_point ? at : owner(frame, at);
    const uint32_t joined = fi_joined(frame, gl_anchor);
    const uint32_t source = joined != FI_NONE ? joined : gl_anchor;
    out->gl_at = gl_anchor;
    for (uint32_t i = source + 1U; i < frame->view.count; i++) {
        out->gl_at = frame->parent[i] == source && frame->view.events[i].kind == NT_GFX_EVENT_BACKEND ? i : out->gl_at;
    }
    /* A streamed request whose execution issued nothing (a state already set, or cache skips only) ran at end_frame
     * after the frame storage upload and the requests before it: its GL state is the last call those issued, and it
     * compares with the draw it sits on. Operations recorded at end_frame (the upload) top at execute_begin or later. */
    const bool silent = gl_anchor == request && streamed(&frame->view.events[request]) && !in_execute(frame, out->gl_at);
    uint32_t point = FI_NONE;
    bool commands = false; /* past the upload: a game request's calls */
    for (uint32_t i = frame->view.execute_begin; silent && i < frame->view.execute_end; i++) {
        const uint32_t top = top_request(frame, i);
        const bool upload = top >= frame->view.execute_begin;
        if (frame->view.events[i].kind != NT_GFX_EVENT_BACKEND || (upload && commands)) {
            continue;
        }
        if (!upload && top > request) {
            break;
        }
        commands = commands || !upload;
        point = i;
    }
    out->gl_at = point != FI_NONE ? point : out->gl_at;
    fi_gl_state(frame, out->gl_at, &out->gl);
    out->was_gl_at = previous(frame, out->gl_at + (point != FI_NONE ? 1U : 0U), is_draw_issue);
    if (out->was_gl_at != FI_NONE) {
        fi_gl_state(frame, out->was_gl_at, &out->was_gl);
    }
}

/* The last record begin_frame wrote: the state the game's first call starts from. */
static uint32_t frame_start(const fi_frame_t *fr) {
    const uint32_t top = fi_row_of(fr, FI_NODE_BASE + FI_NODE_BEGIN_FRAME);
    uint32_t last = FI_NONE;
    for (uint32_t r = top + 1U; top != FI_NONE && r < fr->row_count && fr->depth[r] > fr->depth[top]; r++) {
        last = fr->row[r] < FI_NODE_BASE && (last == FI_NONE || fr->row[r] > last) ? fr->row[r] : last;
    }
    return last;
}

bool fi_state_frame(const fi_frame_t *frame, bool end, fi_state_view_t *out) {
    const uint32_t start = frame_start(frame);
    if (start == FI_NONE) {
        return false;
    }
    *out = (fi_state_view_t){.was = FI_NONE, .was_gl_at = FI_NONE, .gl_at = start};
    fi_request_state(frame, start, &out->req);
    fi_gl_state(frame, start, &out->gl);
    out->was_req = out->req;
    if (end) {
        const uint32_t last = frame->view.count - 1U;
        out->was = start;
        out->was_gl_at = start;
        out->gl_at = last;
        out->compare = FI_COMPARE_FRAME_START;
        out->was_gl = out->gl;
        fi_request_state(frame, last, &out->req);
        fi_gl_state(frame, last, &out->gl);
    }
    return true;
}

fi_verdict_t fi_verdict(const fi_frame_t *frame, uint32_t record) {
    const nt_gfx_event_t *e = &frame->view.events[record];
    fi_verdict_t v = {.kind = FI_VERDICT_NOT_DRAW};
    if (!fi_is_draw(e)) {
        return v;
    }
    if (e->result == NT_GFX_RESULT_CACHE) {
        v.kind = FI_VERDICT_MERGED;
    } else if (e->result != NT_GFX_RESULT_ACCEPTED) {
        v.kind = FI_VERDICT_FAILED;
    } else {
        v.kind = FI_VERDICT_NEW;
        for (uint32_t i = 0; i <= record; i++) {
            v.ordinal += (i == record || is_draw_op(&frame->view.events[i])) && frame->view.events[i].result == NT_GFX_RESULT_ACCEPTED ? 1U : 0U;
        }
    }
    return v;
}

bool fi_frame_at(const fi_file_t *files, uint32_t count, uint64_t index, uint32_t *file, uint32_t *frame) {
    for (uint32_t f = 0; f < count; index -= files[f++].frame_count) {
        if (index < files[f].frame_count) {
            const bool readable = files[f].frames[index].view.events != NULL;
            if (readable) {
                *file = f;
                *frame = (uint32_t)index;
            }
            return readable;
        }
    }
    return false;
}
// #endregion

/* Records are parsed into zeroed storage, so equal arguments are equal bytes. */
static bool same_data(const nt_gfx_event_t *x, const nt_gfx_event_t *y) {
    uint8_t xb[sizeof(x->data)];
    uint8_t yb[sizeof(y->data)];
    memcpy(xb, &x->data, sizeof(xb));
    memcpy(yb, &y->data, sizeof(yb));
    return memcmp(xb, yb, sizeof(xb)) == 0;
}

bool fi_same_setting(const fi_frame_t *frame, uint32_t a, uint32_t b) {
    if (a == b) {
        return true;
    }
    if (a == FI_NONE || b == FI_NONE) {
        return false;
    }
    const nt_gfx_event_t *x = &frame->view.events[a];
    const nt_gfx_event_t *y = &frame->view.events[b];
    const bool unknown_x = x->kind == NT_GFX_EVENT_INITIAL && x->result == NT_GFX_RESULT_UNKNOWN;
    const bool unknown_y = y->kind == NT_GFX_EVENT_INITIAL && y->result == NT_GFX_RESULT_UNKNOWN;
    const bool request = (x->kind == NT_GFX_EVENT_INITIAL || x->kind == NT_GFX_EVENT_OP) && (y->kind == NT_GFX_EVENT_INITIAL || y->kind == NT_GFX_EVENT_OP);
    if (unknown_x || unknown_y || !(x->kind == y->kind || request) || x->operation != y->operation || x->object_kind != y->object_kind || x->object != y->object || x->detail != y->detail) {
        return false;
    }
    if (!same_data(x, y)) {
        return false;
    }
    if (x->operation != NT_GFX_OP_TEXTURE_SET) {
        return true;
    }
    /* A texture set names its textures in the ARGUMENT records after it. */
    uint32_t i = a + 1U;
    uint32_t k = b + 1U;
    for (; i < frame->view.count && frame->parent[i] == a && frame->view.events[i].kind == NT_GFX_EVENT_ARGUMENT; i++, k++) {
        const nt_gfx_event_t *p = &frame->view.events[i];
        const nt_gfx_event_t *q = k < frame->view.count && frame->parent[k] == b ? &frame->view.events[k] : NULL;
        if (q == NULL || q->kind != NT_GFX_EVENT_ARGUMENT || p->object != q->object || !same_data(p, q)) {
            return false;
        }
    }
    return k >= frame->view.count || frame->parent[k] != b || frame->view.events[k].kind != NT_GFX_EVENT_ARGUMENT;
}

/* The GL name (or texture unit) a slot holds after record r; false when r sets the slot without a name. */
static bool gl_value(const fi_frame_t *frame, const fi_gl_slot_t *slot, uint32_t r, uint32_t *value) {
    const nt_gfx_event_t *e = &frame->view.events[r];
    const uint32_t *a = e->data.backend.args;
    if (e->kind == NT_GFX_EVENT_BACKEND) {
        switch (e->detail) {
        case NT_GFX_GL_glUseProgram:
        case NT_GFX_GL_glBindVertexArray:
            *value = a[0];
            return true;
        case NT_GFX_GL_glActiveTexture:
            *value = unit_of(a[0]);
            return true;
        case NT_GFX_GL_glBindFramebuffer:
        case NT_GFX_GL_glBindBuffer:
        case NT_GFX_GL_glBindTexture:
        case NT_GFX_GL_glBindSampler:
            *value = a[1];
            return true;
        default:
            return false;
        }
    }
    if (e->kind == NT_GFX_EVENT_INITIAL && e->operation == NT_GFX_OP_STATE && e->detail == NT_GFX_INITIAL_BACKEND) {
        switch (slot->call) {
        case NT_GFX_GL_glUseProgram:
            *value = e->data.backend_state.program;
            return true;
        case NT_GFX_GL_glBindVertexArray:
            *value = e->data.backend_state.vertex_array;
            return true;
        case NT_GFX_GL_glBindFramebuffer:
            *value = e->data.backend_state.framebuffer;
            return true;
        case NT_GFX_GL_glActiveTexture:
            *value = unit_of(e->data.backend_state.active_texture);
            return true;
        case NT_GFX_GL_glBindBuffer:
            *value = e->data.backend_state.array_buffer;
            return true;
        default:
            return false;
        }
    }
    if (e->kind == NT_GFX_EVENT_INITIAL && e->operation == NT_GFX_OP_TEXTURE) {
        *value = slot->call == NT_GFX_GL_glBindSampler ? e->data.texture_unit.sampler : e->data.texture_unit.texture;
        return true;
    }
    return false;
}

/* The rectangle a viewport call or the inherited viewport holds; false for any other record. */
static bool viewport_of(const fi_frame_t *frame, uint32_t r, int32_t out[4]) {
    const nt_gfx_event_t *e = &frame->view.events[r];
    if (e->kind == NT_GFX_EVENT_BACKEND && e->detail == NT_GFX_GL_glViewport) {
        for (uint32_t k = 0; k < 4; k++) {
            out[k] = (int32_t)e->data.backend.args[k];
        }
        return true;
    }
    if (e->kind == NT_GFX_EVENT_INITIAL && e->operation == NT_GFX_OP_VIEWPORT) {
        out[0] = e->data.rect.x;
        out[1] = e->data.rect.y;
        out[2] = e->data.rect.width;
        out[3] = e->data.rect.height;
        return true;
    }
    return false;
}

bool fi_gl_same(const fi_frame_t *frame, const fi_gl_slot_t *slot, uint32_t a, uint32_t b) {
    if (a == b) {
        return true;
    }
    if (a == FI_NONE || b == FI_NONE) {
        return false;
    }
    uint32_t va = 0;
    uint32_t vb = 0;
    if (gl_value(frame, slot, a, &va) && gl_value(frame, slot, b, &vb)) {
        return va == vb;
    }
    /* The inherited viewport holds the rectangle the call takes as arguments. */
    int32_t ra[4];
    int32_t rb[4];
    if (slot->call == NT_GFX_GL_glViewport && viewport_of(frame, a, ra) && viewport_of(frame, b, rb)) {
        return memcmp(ra, rb, sizeof(ra)) == 0;
    }
    const nt_gfx_event_t *x = &frame->view.events[a];
    const nt_gfx_event_t *y = &frame->view.events[b];
    return x->kind == y->kind && x->detail == y->detail && x->arg_count == y->arg_count && same_data(x, y);
}

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
    fr->group_parent = (uint32_t *)fi_alloc(keys, sizeof(uint32_t));
    fr->group_is_note = (uint8_t *)fi_alloc(keys, sizeof(uint8_t));
    fr->group_label = (const char **)fi_alloc(keys, sizeof(const char *));
    (void)snprintf(fr->group_key[0], FI_KEY_LEN, "(no note or marker)");
    fr->group_label[0] = "";
    fr->group_count = 1;
    const uint32_t slots = table_size(keys);
    uint32_t *slot = (uint32_t *)fi_alloc(slots, sizeof(uint32_t));
    uint32_t *stack = (uint32_t *)fi_alloc(n, sizeof(uint32_t));
    assign_parents(fr, stack, slot, slots - 1U);
    free(stack);
    free(slot);
    build_rows(fr);
    index_handles(fr);
    index_gl_names(fr);
    for (uint32_t i = 0; i < n; i++) {
        fr->cause[i] = FI_NONE;
    }
    check_merges(fr);
    build_summary(fr);
}

// #region compare
/* What pairs two requests across frames: a gfx request by its handles as aliases, a note by its label. */
typedef struct {
    uint8_t kind, op, object_kind;
    fi_alias_t object, vertex_input;
    const char *label;
} req_key_t;

/* One frame's requests bucketed by group in record order, with the GL calls each issued (as op_calls counts them). */
typedef struct {
    const fi_frame_t *fr;
    uint32_t *start; /* per group: its first entry in order; start[group_count] ends the last */
    uint32_t *order;
    req_key_t *key; /* per entry of order */
    uint32_t *calls;
} side_t;

static bool is_request(const nt_gfx_event_t *e) { return e->kind == NT_GFX_EVENT_OP || e->kind == NT_GFX_EVENT_NOTE; }

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void side_build(side_t *s, const fi_frame_t *fr) {
    const uint32_t n = fr->view.count;
    s->fr = fr;
    s->start = (uint32_t *)fi_alloc((size_t)fr->group_count + 1U, sizeof(uint32_t));
    s->calls = (uint32_t *)fi_alloc(n, sizeof(uint32_t));
    for (uint32_t i = 0; i < n; i++) {
        const nt_gfx_event_t *e = &fr->view.events[i];
        const uint32_t op = e->kind == NT_GFX_EVENT_BACKEND ? owner(fr, i) : FI_NONE;
        if (op != FI_NONE) {
            s->calls[op]++;
        }
        s->start[fr->group[i] + 1U] += is_request(e) ? 1U : 0U;
    }
    for (uint32_t g = 0; g < fr->group_count; g++) {
        s->start[g + 1U] += s->start[g];
    }
    const uint32_t total = s->start[fr->group_count];
    s->order = (uint32_t *)fi_alloc(total, sizeof(uint32_t));
    s->key = (req_key_t *)fi_alloc(total, sizeof(req_key_t));
    uint32_t *at = (uint32_t *)fi_alloc(fr->group_count, sizeof(uint32_t));
    memcpy(at, s->start, (size_t)fr->group_count * sizeof(uint32_t));
    for (uint32_t i = 0; i < n; i++) {
        const nt_gfx_event_t *e = &fr->view.events[i];
        if (!is_request(e)) {
            continue;
        }
        const uint32_t k = at[fr->group[i]]++;
        s->order[k] = i;
        req_key_t *key = &s->key[k];
        key->kind = (uint8_t)e->kind;
        if (e->kind == NT_GFX_EVENT_NOTE) {
            key->label = fr->view.strings + e->note.label;
            continue;
        }
        key->op = e->operation;
        key->object_kind = e->object_kind;
        key->object = fi_alias(fr, e->object_kind, e->object);
        key->vertex_input = fi_is_draw(e) ? fi_alias(fr, NT_GFX_OBJECT_VERTEX_INPUT, e->data.draw.vertex_input) : (fi_alias_t){"", 0};
    }
    free(at);
}

static void side_free(side_t *s) {
    free(s->start);
    free(s->order);
    free(s->key);
    free(s->calls);
}

static bool same_alias(fi_alias_t x, fi_alias_t y) { return x.ordinal == y.ordinal && strcmp(x.label, y.label) == 0; }

static bool same_key(const req_key_t *x, const req_key_t *y) {
    if (x->kind != y->kind) {
        return false;
    }
    if (x->kind == NT_GFX_EVENT_NOTE) {
        return strcmp(x->label, y->label) == 0;
    }
    return x->op == y->op && x->object_kind == y->object_kind && same_alias(x->object, y->object) && same_alias(x->vertex_input, y->vertex_input);
}

/* A record as two frames compare it, without what same_record compares on its own (handles as aliases, strings
 * as text) or not at all: links, joined draws, GL names, the result, the label a create defines (its alias holds it). */
static nt_gfx_event_t canonical(const nt_gfx_event_t *e) {
    nt_gfx_event_t c;
    memcpy(&c, e, sizeof(c));
    const handles_t h = handles_of(&c);
    for (uint32_t k = 0; k < h.count; k++) {
        memset((unsigned char *)&c + h.at[k], 0, sizeof(uint32_t));
    }
    if (c.kind == NT_GFX_EVENT_NOTE || c.kind == NT_GFX_EVENT_MARKER_PUSH) {
        c.note.label = 0;
    } else if (c.kind == NT_GFX_EVENT_NOTE_FIELD) {
        c.field.key = 0;
        if (c.field.type == NT_GFX_NOTE_STR) {
            c.field.value.str = 0;
        }
    }
    if (!is_gfx(&c)) {
        return c;
    }
    c.link = 0;
    c.result = 0;
    if (fi_is_draw(&c)) {
        c.detail = 0;
    }
    /* Slots and GL names are backend state (among compared records only definitions carry them); strings compare
     * as text and a definition's label is its alias. */
    const nt_gfx_capture_field_t *f = NULL;
    const uint32_t n = nt_gfx_capture_fields(&c, &f);
    for (uint32_t k = 0; k < n; k++) {
        if (f[k].meaning == NT_GFX_MEANING_SLOT || f[k].meaning == NT_GFX_MEANING_GL_NAME || f[k].type == NT_GFX_FIELD_STR) {
            memset((unsigned char *)&c + f[k].offset, 0, sizeof(uint32_t));
        }
    }
    return c;
}

static bool same_string(const fi_frame_t *fa, nt_gfx_capture_string_t x, const fi_frame_t *fb, nt_gfx_capture_string_t y) { return strcmp(fa->view.strings + x, fb->view.strings + y) == 0; }

static bool same_record(const fi_frame_t *fa, uint32_t i, const fi_frame_t *fb, uint32_t j) {
    const nt_gfx_event_t *e = &fa->view.events[i];
    const nt_gfx_event_t *f = &fb->view.events[j];
    const nt_gfx_event_t x = canonical(e);
    const nt_gfx_event_t y = canonical(f);
    uint8_t xb[sizeof(x)];
    uint8_t yb[sizeof(y)];
    memcpy(xb, &x, sizeof(xb));
    memcpy(yb, &y, sizeof(yb));
    if (memcmp(xb, yb, sizeof(xb)) != 0) {
        return false;
    }
    if (e->kind == NT_GFX_EVENT_NOTE || e->kind == NT_GFX_EVENT_MARKER_PUSH) {
        return same_string(fa, e->note.label, fb, f->note.label);
    }
    if (e->kind == NT_GFX_EVENT_NOTE_FIELD &&
        (!same_string(fa, e->field.key, fb, f->field.key) || (e->field.type == NT_GFX_NOTE_STR && !same_string(fa, e->field.value.str, fb, f->field.value.str)))) {
        return false;
    }
    const handles_t h = handles_of(e); /* f has the same kind, operation, object kind and note type */
    for (uint32_t k = 0; k < h.count; k++) {
        if (!same_alias(fi_alias(fa, h.kind[k], handle_at(e, h.at[k])), fi_alias(fb, h.kind[k], handle_at(f, h.at[k])))) {
            return false;
        }
    }
    return true;
}

/* After record i, the next record request r wrote itself: a NOTE_FIELD of note r, an ARGUMENT or a
 * resource or attribute DEFINITION of operation r. The backend's records (GL calls, GL names) are left out:
 * backends differ. FI_NONE past its last. */
static uint32_t next_item(const fi_frame_t *fr, uint32_t r, uint32_t i) {
    const bool note = fr->view.events[r].kind == NT_GFX_EVENT_NOTE;
    for (i++; i < fr->view.count; i++) {
        const nt_gfx_event_t *e = &fr->view.events[i];
        if (note || owner(fr, i) != r) {
            return note && e->kind == NT_GFX_EVENT_NOTE_FIELD ? i : FI_NONE;
        }
        if (e->kind == NT_GFX_EVENT_ARGUMENT || (e->kind == NT_GFX_EVENT_DEFINITION && (e->operation == NT_GFX_OP_CREATE || e->operation == NT_GFX_OP_ATTRIBUTE))) {
            return i;
        }
    }
    return FI_NONE;
}

/* What differs between two paired requests. */
static uint8_t change_of(const side_t *sa, uint32_t ra, const side_t *sb, uint32_t rb) {
    const fi_frame_t *fa = sa->fr;
    const fi_frame_t *fb = sb->fr;
    uint8_t change = 0;
    if (fa->view.events[ra].kind == NT_GFX_EVENT_OP) {
        change |= fa->view.events[ra].result != fb->view.events[rb].result ? FI_CHANGE_RESULT : 0U;
        change |= fa->brk[ra] != fb->brk[rb] ? FI_CHANGE_MERGE : 0U;
        change |= sa->calls[ra] != sb->calls[rb] ? FI_CHANGE_CALLS : 0U;
        change |= same_record(fa, ra, fb, rb) ? 0U : FI_CHANGE_ARGS;
    }
    for (uint32_t i = next_item(fa, ra, ra), j = next_item(fb, rb, rb); i != FI_NONE || j != FI_NONE; i = next_item(fa, ra, i), j = next_item(fb, rb, j)) {
        if (i == FI_NONE || j == FI_NONE || !same_record(fa, i, fb, j)) {
            return change | FI_CHANGE_ITEMS;
        }
    }
    return change;
}

/* Myers' greedy diff of x and y: per edit count d, the furthest x on each diagonal k, kept so the path can be
 * traced back; d edits take (d + 1)^2 entries of trace. False when more than `most` edits separate them. */
static bool myers(const req_key_t *x, int32_t n, const req_key_t *y, int32_t m, int32_t most, int32_t *trace, int32_t *edits) {
    for (int32_t d = 0; d <= most; d++) {
        int32_t *v = trace + ((size_t)d * (size_t)d) + d;
        const int32_t *prev = d > 0 ? trace + ((size_t)(d - 1) * (size_t)(d - 1)) + (d - 1) : NULL;
        for (int32_t k = -d; k <= d; k += 2) {
            int32_t xx = 0;
            if (prev != NULL) {
                xx = k == -d || (k != d && prev[k - 1] < prev[k + 1]) ? prev[k + 1] : prev[k - 1] + 1;
            }
            int32_t yy = xx - k;
            while (xx < n && yy < m && same_key(&x[xx], &y[yy])) {
                xx++;
                yy++;
            }
            v[k] = xx;
            if (xx >= n && yy >= m) {
                *edits = d;
                return true;
            }
        }
    }
    return false;
}

typedef struct {
    const side_t *a, *b;
    uint32_t a0, b0; /* the group's first entries in each side's order */
    fi_diff_row_t *rows;
    uint32_t count;
    uint32_t differ;
} emit_t;

/* Writes row `at`: entry i of A and entry j of B; -1 for the side without one. */
static void emit(emit_t *o, uint32_t at, int32_t i, int32_t j) {
    fi_diff_row_t *r = &o->rows[at];
    *r = (fi_diff_row_t){.a = FI_NONE, .b = FI_NONE, .kind = FI_DIFF_SAME};
    if (i >= 0) {
        r->a = o->a->order[o->a0 + (uint32_t)i];
        r->calls_a = o->a->calls[r->a];
    }
    if (j >= 0) {
        r->b = o->b->order[o->b0 + (uint32_t)j];
        r->calls_b = o->b->calls[r->b];
    }
    if (i < 0 || j < 0) {
        r->kind = i < 0 ? FI_DIFF_ONLY_B : FI_DIFF_ONLY_A;
    } else {
        r->change = change_of(o->a, r->a, o->b, r->b);
        r->kind = r->change != 0 ? FI_DIFF_CHANGED : FI_DIFF_SAME;
    }
    o->differ += r->kind != FI_DIFF_SAME ? 1U : 0U;
}

/* The rows of one group pair: the common start, the middle aligned (or A's then B's when one side has none or
 * they are too different), the common end. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool align(emit_t *o, int32_t n, int32_t m, int32_t *trace) {
    const req_key_t *x = o->a->key + o->a0;
    const req_key_t *y = o->b->key + o->b0;
    int32_t head = 0;
    while (head < n && head < m && same_key(&x[head], &y[head])) {
        emit(o, o->count++, head, head);
        head++;
    }
    int32_t tail = 0;
    while (tail < n - head && tail < m - head && same_key(&x[n - 1 - tail], &y[m - 1 - tail])) {
        tail++;
    }
    const int32_t n2 = n - head - tail;
    const int32_t m2 = m - head - tail;
    int32_t edits = -1; /* one side empty: its rows need no search */
    const int32_t most = n2 + m2 < (int32_t)FI_DIFF_EDITS ? n2 + m2 : (int32_t)FI_DIFF_EDITS;
    const bool aligned = n2 == 0 || m2 == 0 || myers(x + head, n2, y + head, m2, most, trace, &edits);
    if (edits >= 0) { /* traced back from the end, so the rows fill from the last: matches + edits of them */
        uint32_t at = o->count + (uint32_t)((n2 + m2 + edits) / 2);
        o->count = at;
        int32_t xx = n2;
        int32_t yy = m2;
        for (int32_t d = edits; d > 0; d--) {
            const int32_t *prev = trace + ((size_t)(d - 1) * (size_t)(d - 1)) + (d - 1);
            const int32_t k = xx - yy;
            const bool down = k == -d || (k != d && prev[k - 1] < prev[k + 1]);
            const int32_t pk = down ? k + 1 : k - 1;
            const int32_t px = prev[pk];
            const int32_t py = px - pk;
            while (xx > (down ? px : px + 1) && yy > (down ? py + 1 : py)) {
                xx--;
                yy--;
                emit(o, --at, head + xx, head + yy);
            }
            if (down) {
                yy--;
                emit(o, --at, -1, head + yy);
            } else {
                xx--;
                emit(o, --at, head + xx, -1);
            }
        }
        while (xx > 0 && yy > 0) {
            xx--;
            yy--;
            emit(o, --at, head + xx, head + yy);
        }
    } else {
        for (int32_t i = 0; i < n2; i++) {
            emit(o, o->count++, head + i, -1);
        }
        for (int32_t j = 0; j < m2; j++) {
            emit(o, o->count++, -1, head + j);
        }
    }
    for (int32_t t = tail; t > 0; t--) {
        emit(o, o->count++, n - t, m - t);
    }
    return aligned;
}

static uint32_t key_hash(const char *key) {
    uint32_t h = 2166136261U;
    for (const char *c = key; *c != '\0'; c++) {
        h = (h ^ (uint8_t)*c) * 16777619U;
    }
    return h;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void fi_diff(const fi_frame_t *a, const fi_frame_t *b, fi_diff_t *out) {
    side_t sa;
    side_t sb;
    side_build(&sa, a);
    side_build(&sb, b);
    /* A's groups by key text; a hit counts only when the identity matches too, as in fi_group_in. */
    const uint32_t slots = table_size(a->group_count);
    uint32_t *slot = (uint32_t *)fi_alloc(slots, sizeof(uint32_t));
    for (uint32_t g = 0; g < a->group_count; g++) {
        uint32_t h = key_hash(a->group_key[g]);
        while (slot[h & (slots - 1U)] != 0) {
            h++;
        }
        slot[h & (slots - 1U)] = g + 1U;
    }
    uint8_t *paired = (uint8_t *)fi_alloc(a->group_count, 1);
    *out = (fi_diff_t){0};
    out->group = (fi_diff_group_t *)fi_alloc((size_t)a->group_count + b->group_count, sizeof(fi_diff_group_t));
    out->row = (fi_diff_row_t *)fi_alloc((size_t)sa.start[a->group_count] + sb.start[b->group_count], sizeof(fi_diff_row_t));
    uint32_t widest = 0;
    for (uint32_t g = 0; g < b->group_count; g++) {
        const uint32_t m = sb.start[g + 1U] - sb.start[g];
        uint32_t found = FI_NONE;
        for (uint32_t h = key_hash(b->group_key[g]); slot[h & (slots - 1U)] != 0 && found == FI_NONE; h++) {
            const uint32_t k = slot[h & (slots - 1U)] - 1U;
            found = same_group(a, k, b, g) ? k : FI_NONE;
        }
        const uint32_t n = found != FI_NONE ? sa.start[found + 1U] - sa.start[found] : 0U;
        if (n + m == 0) {
            continue;
        }
        if (found != FI_NONE) {
            paired[found] = 1;
        }
        out->group[out->group_count++] = (fi_diff_group_t){.a = found, .b = g};
        widest = n + m > widest ? n + m : widest;
    }
    for (uint32_t g = 0; g < a->group_count; g++) {
        const uint32_t n = sa.start[g + 1U] - sa.start[g];
        if (paired[g] == 0 && n > 0) {
            out->group[out->group_count++] = (fi_diff_group_t){.a = g, .b = FI_NONE};
            widest = n > widest ? n : widest;
        }
    }
    const size_t most = (widest < FI_DIFF_EDITS ? widest : FI_DIFF_EDITS) + 1U;
    int32_t *trace = (int32_t *)fi_alloc(most * most, sizeof(int32_t));
    emit_t o = {.a = &sa, .b = &sb, .rows = out->row};
    for (uint32_t k = 0; k < out->group_count; k++) {
        fi_diff_group_t *dg = &out->group[k];
        o.a0 = dg->a != FI_NONE ? sa.start[dg->a] : 0U;
        o.b0 = dg->b != FI_NONE ? sb.start[dg->b] : 0U;
        const int32_t n = dg->a != FI_NONE ? (int32_t)(sa.start[dg->a + 1U] - o.a0) : 0;
        const int32_t m = dg->b != FI_NONE ? (int32_t)(sb.start[dg->b + 1U] - o.b0) : 0;
        dg->first = o.count;
        o.differ = 0;
        dg->aligned = align(&o, n, m, trace);
        dg->count = o.count - dg->first;
        dg->differ = o.differ;
    }
    out->row_count = o.count;
    free(trace);
    free(paired);
    free(slot);
    side_free(&sa);
    side_free(&sb);
}

void fi_diff_free(fi_diff_t *diff) {
    free(diff->group);
    free(diff->row);
    *diff = (fi_diff_t){0};
}
// #endregion
