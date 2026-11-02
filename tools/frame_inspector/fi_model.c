#include "fi_model.h"

#include "core/nt_assert.h"
#include "fs/nt_fs.h"
#include "graphics/nt_gfx_capture_text.h"
#include "nt_texture_format.h"

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

/* Bytes GL was asked to hold for a resource definition: buffer size, texture levels. Depth sizes are those
 * of the GL storage type (DEPTH24 as 32-bit); the runtime never uploads depth, so the shared table has none. */
static uint64_t definition_bytes(const nt_gfx_event_t *e) {
    if (e->object_kind == NT_GFX_OBJECT_BUFFER) {
        return e->data.resource.size;
    }
    if (e->object_kind != NT_GFX_OBJECT_TEXTURE) {
        return 0;
    }
    const nt_texture_format_t fmt = (nt_texture_format_t)e->data.resource.format;
    const uint64_t depth = fmt == NT_TEXTURE_FORMAT_DEPTH16 ? 2U : 4U;
    const uint32_t levels = e->data.resource.levels > 0 ? e->data.resource.levels : 1;
    uint64_t bytes = 0;
    for (uint32_t l = 0; l < levels && l < 32; l++) {
        const uint32_t w = nt_texture_level_extent(e->data.resource.width, l);
        const uint32_t h = nt_texture_level_extent(e->data.resource.height, l);
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void fi_file_load(fi_file_t *file, const char *name, char *text, size_t len) {
    memset(file, 0, sizeof(*file));
    const size_t name_len = strlen(name);
    file->name = (char *)fi_alloc(name_len + 1, 1);
    memcpy(file->name, name, name_len);
    file->text = text;
    file->len = len;
    /* Records are the lines that start with their index: a file of empty lines asks for no memory. */
    uint32_t records = 1;
    for (size_t i = 0; i < len; i++) {
        records += (i == 0 || text[i - 1] == '\n') && text[i] >= '0' && text[i] <= '9';
    }
    file->events = (nt_gfx_event_t *)fi_alloc(records, sizeof(nt_gfx_event_t));
    uint32_t used = 0;
    uint32_t cap = 0;
    uint32_t base_line = 1; /* the line the document starts on */
    for (size_t at = 0; at < len;) {
        const nt_gfx_capture_parsed_t r = nt_gfx_capture_parse(text + at, len - at, file->events + used, records - used);
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
        free(fr->alias_number);
        free(fr->alias_slot);
        free(fr->alias_kind);
        free(fr->def);
        free(fr->buffer_upload);
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
    char shown[FI_KEY_LEN]; /* control bytes as \xNN, as capture text prints them: a key stays one line */
    size_t n = 0;
    for (const char *c = label; *c != '\0' && n + 5U < sizeof(shown); c++) {
        const uint8_t b = (uint8_t)*c;
        if (b < 0x20U || b == 0x7fU) {
            n += (size_t)snprintf(shown + n, sizeof(shown) - n, "\\x%02x", b);
        } else {
            shown[n++] = *c;
        }
    }
    shown[n] = '\0';
    (void)snprintf(fr->group_key[g], FI_KEY_LEN, "%s%s%s", base, parent != 0 || note ? sep + (parent == 0) : "", shown); /* "> ui" at the top */
    return g;
}

/* Parent of every record: the record it belongs to (link, note, marker) or a phase of the frame. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void assign_parents(fi_frame_t *fr, uint32_t *stack, uint32_t *slot, uint32_t mask) {
    const nt_gfx_event_t *ev = fr->view.events;
    const uint32_t n = fr->view.count;
    /* The snapshot is the leading run of link-0 INITIAL/DEFINITION records; finished links follow it. */
    uint32_t snapshot_end = 0;
    while (snapshot_end < n && ev[snapshot_end].link == 0 && (ev[snapshot_end].kind == NT_GFX_EVENT_INITIAL || ev[snapshot_end].kind == NT_GFX_EVENT_DEFINITION)) {
        snapshot_end++;
    }
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
            fr->group[i] = intern_group(fr, slot, mask, base, false, e->note.label);
            stack[top++] = i;
            note = FI_NONE;
            break;
        case NT_GFX_EVENT_MARKER_POP:
            top -= top > 0 ? 1U : 0U;
            note = FI_NONE;
            break;
        case NT_GFX_EVENT_NOTE:
            fr->group[i] = intern_group(fr, slot, mask, base, true, e->note.label);
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

uint32_t fi_alias(const fi_frame_t *frame, uint32_t kind, uint32_t object) {
    const uint32_t k = frame->alias_slot != NULL ? *alias_slot(frame, kind, object) : 0U;
    return k != 0 ? frame->alias_number[k - 1U] : 0U;
}

/* Aliases, each DESTROY's definition, and the bytes uploaded per buffer, in one pass over the records. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void index_handles(fi_frame_t *fr) {
    const uint32_t n = fr->view.count;
    fr->alias_object = (uint32_t *)fi_alloc(n, sizeof(uint32_t));
    fr->alias_number = (uint32_t *)fi_alloc(n, sizeof(uint32_t));
    fr->alias_kind = (uint8_t *)fi_alloc(n, sizeof(uint8_t));
    const uint32_t slots = table_size(n);
    fr->alias_slot = (uint32_t *)fi_alloc(slots, sizeof(uint32_t));
    fr->alias_mask = slots - 1U;
    fr->def = (uint32_t *)fi_alloc(n, sizeof(uint32_t));
    uint32_t *last_def = (uint32_t *)fi_alloc(n, sizeof(uint32_t)); /* per alias: its latest CREATE definition */
    uint32_t *upload = (uint32_t *)fi_alloc(n, sizeof(uint32_t));   /* per alias: its buffer upload row + 1 */
    fr->buffer_upload = (fi_buffer_upload_t *)fi_alloc(n, sizeof(fi_buffer_upload_t));
    uint32_t count = 0;
    uint32_t next[FI_OBJECTS] = {0};
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
        uint32_t *slot = alias_slot(fr, e->object_kind, e->object);
        if (*slot == 0) {
            *slot = count + 1U;
            fr->alias_object[count] = e->object;
            fr->alias_kind[count] = e->object_kind;
            last_def[count] = FI_NONE;
            fr->alias_number[count++] = ++next[e->object_kind];
        }
        const uint32_t k = *slot - 1U;
        if (e->kind == NT_GFX_EVENT_DEFINITION && e->operation == NT_GFX_OP_CREATE) {
            last_def[k] = i;
        } else if (e->kind == NT_GFX_EVENT_OP) {
            fr->def[i] = last_def[k];
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

// #region merge check
/* Whether an operation recorded a draw-phase command (nt_gfx.c), apart from plain and indexed draws. */
static bool records_command(const nt_gfx_event_t *e, bool timing) {
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
        return timing; /* segments record commands only when the frame runs GPU timing */
    default:
        return false;
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
            FI_ADD_(FI_Q_RESOURCE, e->object_kind, FI_RES_DESTROYED, def != NULL && def->data.resource.backend != 0 ? definition_bytes(def) : 0); /* a husk held nothing */
        }
        if (e->operation == NT_GFX_OP_BUFFER_UPLOAD && e->link == 0 && in_execute(fr, i)) {
            FI_ADD_(FI_Q_FRAME_STORE, 0, 0, e->data.resource.size);
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
            if (fr->parent[i] == FI_NODE_BASE + FI_NODE_SNAPSHOT && e->data.resource.backend != 0) {                   /* a husk defines backend 0 */
                FI_ADD_(FI_Q_RESOURCE, e->object_kind, FI_RES_LIVE, definition_bytes(e));
            } else if (op != FI_NONE && fr->view.events[op].operation == NT_GFX_OP_CREATE) {
                FI_ADD_(FI_Q_RESOURCE, e->object_kind, FI_RES_CREATED, definition_bytes(e));
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
                const bool same_program = before != FI_NONE && after != FI_NONE && ev[before].data.resource.related[0] == ev[after].data.resource.related[0];
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
            if (set && e->data.binding.slot < 8) {
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
                unit = unit_of(a[3]);
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
    fi_request_state(frame, at, &out->req);
    out->was = previous(frame, at, is_draw_op);
    out->was_req = out->req;
    if (out->was != FI_NONE) {
        fi_request_state(frame, out->was, &out->was_req);
    }
    /* An operation's draw-phase calls issue at end_frame: its GL state is the one after its last call. A merged
     * draw issued nothing; it ran with the call of the draw it joined. */
    const uint32_t joined = fi_joined(frame, at);
    const uint32_t source = joined != FI_NONE ? joined : at;
    out->gl_at = at;
    for (uint32_t i = source + 1U; i < frame->view.count; i++) {
        out->gl_at = frame->parent[i] == source && frame->view.events[i].kind == NT_GFX_EVENT_BACKEND ? i : out->gl_at;
    }
    fi_gl_state(frame, out->gl_at, &out->gl);
    out->was_gl_at = previous(frame, out->gl_at, is_draw_issue);
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

/* The name a GL slot holds after record `r`: a bind call's argument or the backend cache entry it inherited. */
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
            *value = a[0];
            return true;
        case NT_GFX_GL_glBindVertexArray:
            *value = a[1];
            return true;
        case NT_GFX_GL_glBindFramebuffer:
            *value = a[2];
            return true;
        case NT_GFX_GL_glActiveTexture:
            *value = unit_of(a[3]);
            return true;
        case NT_GFX_GL_glBindBuffer:
            *value = a[7];
            return true;
        default:
            return false;
        }
    }
    if (e->kind == NT_GFX_EVENT_INITIAL && e->operation == NT_GFX_OP_TEXTURE) {
        *value = slot->call == NT_GFX_GL_glBindSampler ? a[2] : a[1];
        return true;
    }
    return false;
}

static const uint32_t *viewport_of(const fi_frame_t *frame, uint32_t r) {
    const nt_gfx_event_t *e = &frame->view.events[r];
    if (e->kind == NT_GFX_EVENT_BACKEND && e->detail == NT_GFX_GL_glViewport) {
        return e->data.backend.args;
    }
    return e->kind == NT_GFX_EVENT_INITIAL && e->operation == NT_GFX_OP_VIEWPORT ? e->data.state.integers : NULL;
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
    const uint32_t *ra = viewport_of(frame, a);
    const uint32_t *rb = viewport_of(frame, b);
    if (slot->call == NT_GFX_GL_glViewport && ra != NULL && rb != NULL) {
        return ra[0] == rb[0] && ra[1] == rb[1] && ra[2] == rb[2] && ra[3] == rb[3];
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
    for (uint32_t i = 0; i < n; i++) {
        fr->cause[i] = FI_NONE;
    }
    check_merges(fr);
    build_summary(fr);
}
