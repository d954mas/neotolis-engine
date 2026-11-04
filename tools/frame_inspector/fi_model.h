#ifndef FI_MODEL_H
#define FI_MODEL_H

/* Frame inspector model: capture files parsed into frames, each with its logical tree, merge check
 * and summary. No UI and no gfx backend, so the tests drive it directly. */

#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_capture_text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FI_NONE UINT32_MAX

/* Tree rows at and above FI_NODE_BASE are phases of the frame, not records. */
#define FI_NODE_BASE 0xFFFFFF00U
typedef enum {
    FI_NODE_BEGIN_FRAME,
    FI_NODE_SNAPSHOT,      /* inherited state recorded by the begin_frame that opens the frame */
    FI_NODE_PROGRAM_LINKS, /* links that begin_frame finished */
    FI_NODE_CALLS,
    FI_NODE_END_FRAME, /* frame storage uploads, then the command stream execution */
    FI_NODE_AFTER,
    FI_NODE_FRAME, /* the root: every phase sits under it, so one fold folds the frame */
    FI_NODE_COUNT,
} fi_node_t;

/* Why a plain or indexed draw did not merge into the previous recorded command. */
typedef enum {
    FI_BREAK_NONE, /* merged, or not a mergeable draw */
    FI_BREAK_COMMAND,
    FI_BREAK_FUNCTION,
    FI_BREAK_INSTANCED,
    FI_BREAK_RANGE,
    FI_BREAK_COUNT_LIMIT,
    FI_BREAK_COUNT,
} fi_break_t;

/* What a summary number counts; the same query filters the tree to the records behind it. */
typedef enum {
    FI_Q_NONE,
    FI_Q_OP,          /* a = operation, b = result */
    FI_Q_OP_NESTED,   /* a = operation: issued inside another operation */
    FI_Q_OP_CALLS,    /* a = operation: issued GL calls linked to it */
    FI_Q_OP_SKIPS,    /* a = operation: backend SKIP records linked to it */
    FI_Q_GL_CALL,     /* a = nt_gfx_gl_call_t */
    FI_Q_GL_ANY,      /* every issued GL call */
    FI_Q_DRAW_CALL,   /* issued GL draw calls (glDraw*) */
    FI_Q_SKIP,        /* every backend SKIP record */
    FI_Q_VERTICES,    /* draws that count geometry; sum = vertices x instances */
    FI_Q_INDICES,     /* indexed draws; sum = indices x instances */
    FI_Q_INSTANCES,   /* instanced draws; sum = instances */
    FI_Q_MERGED,      /* draws merged into the previous one (CACHE) */
    FI_Q_BREAK,       /* a = fi_break_t */
    FI_Q_UPLOAD,      /* a = 0 buffer, 1 texture, 2 either: issued calls with payload; sum = bytes */
    FI_Q_FRAME_STORE, /* frame storage uploads of end_frame; sum = bytes */
    FI_Q_RESOURCE,    /* a = nt_gfx_object_kind_t, b = fi_res_t; sum = bytes requested from GL */
    FI_Q_GROUP,       /* a = group, b = fi_metric_t */
    FI_Q_DROP,        /* renderer `drop` note fields */
    FI_Q_PROBLEM,     /* drops and results other than ACCEPTED/CACHE/NONE; a SKIP is the backend cache working */
} fi_query_kind_t;

typedef enum { FI_RES_LIVE, FI_RES_CREATED, FI_RES_DESTROYED, FI_RES_COUNT } fi_res_t;
/* Per group: draw requests, the GL draw calls they issued (merged requests share one), every GL call, bytes, drops. */
typedef enum { FI_METRIC_DRAWS, FI_METRIC_DRAW_CALLS, FI_METRIC_CALLS, FI_METRIC_BYTES, FI_METRIC_DROPS, FI_METRIC_COUNT } fi_metric_t;

typedef struct {
    uint8_t kind; /* fi_query_kind_t */
    uint8_t b;
    uint32_t a;
} fi_query_t;

typedef struct {
    uint64_t n;   /* records */
    uint64_t sum; /* bytes, vertices, ... where the query has a sum */
} fi_cell_t;

#define FI_RESULTS (NT_GFX_RESULT_UNKNOWN + 1)
#define FI_OBJECTS (NT_GFX_OBJECT_INSTANCE_LAYOUT + 1)
#define FI_KEY_LEN 160
#define FI_CONTENT_LABEL 48

typedef struct {
    fi_cell_t op[NT_GFX_OP_COUNT][FI_RESULTS];
    fi_cell_t op_nested[NT_GFX_OP_COUNT];
    fi_cell_t op_calls[NT_GFX_OP_COUNT];
    fi_cell_t op_skips[NT_GFX_OP_COUNT];
    fi_cell_t gl[NT_GFX_GL_COUNT];
    fi_cell_t gl_any, draw_calls, skips;
    fi_cell_t vertices, indices, instances, merged;
    fi_cell_t brk[FI_BREAK_COUNT];
    fi_cell_t upload[3];
    fi_cell_t frame_store;
    fi_cell_t resource[FI_OBJECTS][FI_RES_COUNT];
    fi_cell_t drops, problems;
    fi_cell_t *group; /* [group_count][FI_METRIC_COUNT] */
} fi_summary_t;

/* Under a tree row: draw requests (ACCEPTED or CACHE), those merged, and the GL draw calls issued. */
typedef struct {
    uint32_t draws, merged, gl_draws;
} fi_tally_t;

/* Bytes uploaded into one buffer by issued calls, and the first operation that uploaded them. */
typedef struct {
    uint32_t object, first, calls;
    uint64_t bytes;
} fi_buffer_upload_t;

/* What a GL name stood for at one record: the frontend object it belonged to then, an engine-internal object
 * by its role, or nothing the frame recorded. */
typedef enum { FI_GL_UNKNOWN, FI_GL_OBJECT, FI_GL_UPLOAD_VAO, FI_GL_TIMER_QUERY } fi_gl_owner_t;
typedef struct {
    uint16_t at;   /* the name's byte offset in the record */
    uint8_t owner; /* fi_gl_owner_t */
    uint8_t kind;  /* FI_GL_OBJECT: nt_gfx_object_kind_t */
    uint32_t object;
} fi_gl_ref_t;

typedef struct {
    nt_gfx_capture_view_t view; /* events NULL: the header was rejected */
    const char *error;          /* NULL when the document parsed whole */
    uint32_t error_line, error_col;
    const char **line; /* record i's text line in the file, without its index; ends at '\n' */
    const char *text_end;
    /* Logical tree: rows in display order, each a record index or FI_NODE_BASE + fi_node_t. */
    uint32_t *row;
    uint16_t *depth;
    uint32_t row_count;
    uint32_t *record_row;             /* per record: its row */
    uint32_t node_row[FI_NODE_COUNT]; /* per phase: its row, FI_NONE when the frame has none */
    uint32_t *parent;                 /* per record: a record index or FI_NODE_BASE + fi_node_t */
    fi_tally_t *tally;                /* per record, then per phase (count + node) */
    /* Group: the note or marker that governs a record. Group 0 is "no group". A key reads "scene/ui > mesh":
     * the marker path, then the note; identity is the parent group, the kind and the label, not the text. */
    uint32_t *group;
    char (*group_key)[FI_KEY_LEN];
    uint32_t *group_parent;
    uint8_t *group_is_note;
    const char **group_label;
    uint32_t group_count;
    /* Short names for handles (vi/mesh#3): per kind and label, numbered by first use in the frame's work. */
    uint32_t *alias_object, *alias_ordinal, *alias_slot, alias_mask;
    uint8_t *alias_kind;
    const char **alias_label;                /* the frame's strings, a content name, or "" when unlabeled */
    char (*content_label)[FI_CONTENT_LABEL]; /* per alias: the name of an object shared by its content */
    uint32_t *def;                           /* per operation on an object: the object's latest definition before it, FI_NONE */
    uint32_t *gl_first;                      /* per record: its first gl_ref; record count + 1 entries */
    fi_gl_ref_t *gl_ref;                     /* every GL name of every record, resolved at its record */
    fi_buffer_upload_t *buffer_upload;       /* the biggest first */
    uint32_t buffer_upload_count;
    /* Merge check: per draw the break and the record that caused it (the command between, or the previous draw). */
    uint8_t *brk;
    uint32_t *cause;
    uint32_t merge_mismatch; /* first draw the merge rule predicts wrongly; FI_NONE when it holds */
    fi_summary_t summary;
} fi_frame_t;

typedef struct {
    char *name;
    char *text;
    size_t len;
    nt_gfx_event_t *events;
    char *strings; /* every document's strings, back to back */
    fi_frame_t *frames;
    uint32_t frame_count;
} fi_file_t;

/* Reads and parses a capture file; false when it cannot be read. */
bool fi_file_open(fi_file_t *file, const char *path);
/* Parses text (malloc'd, len bytes; the file takes it) into frames. The file's name keeps control bytes as \xNN. */
void fi_file_load(fi_file_t *file, const char *name, char *text, size_t len);
void fi_file_close(fi_file_t *file);
/* The index-th frame of the files in their order, counting rejected ones: the frame's address. Sequences
 * repeat once files of two runs are joined. False past the last frame or for a rejected header. */
bool fi_frame_at(const fi_file_t *files, uint32_t count, uint64_t index, uint32_t *file, uint32_t *frame);

/* Request state at an operation: for each piece of frontend binding state, the OP (or frontend INITIAL)
 * record that set it, FI_NONE when unset. Pass-scoped pieces reset at every PASS. */
typedef struct {
    uint32_t pass, pipeline, vertex_input, texture_set, viewport, scissor, scissor_enable;
    uint32_t ubo[8];
} fi_request_state_t;
void fi_request_state(const fi_frame_t *frame, uint32_t at, fi_request_state_t *out);

/* GL state at an issued call: each state-setting call keyed by its target (unit, cap, slot) with the
 * record that set it last: a BACKEND record, or the backend INITIAL record it was inherited from. */
typedef struct {
    uint32_t call;   /* nt_gfx_gl_call_t; glEnable stands for glEnable/glDisable */
    uint32_t key[2]; /* the leading arguments that select the state (target, unit, cap) */
    uint32_t record;
    bool inherited;
} fi_gl_slot_t;
#define FI_GL_SLOTS 160
typedef struct {
    fi_gl_slot_t slot[FI_GL_SLOTS];
    uint32_t count;
    bool full; /* more state than slots: the rest is not shown */
} fi_gl_state_t;
void fi_gl_state(const fi_frame_t *frame, uint32_t at, fi_gl_state_t *out);

/* What a state view is compared with. */
typedef enum { FI_COMPARE_NONE, FI_COMPARE_PREVIOUS_DRAW, FI_COMPARE_FRAME_START } fi_compare_t;
/* Request and GL state at a record, next to the state at an earlier record it is compared with. */
typedef struct {
    fi_request_state_t req, was_req;
    fi_gl_state_t gl, was_gl;
    uint32_t gl_at;          /* the record whose GL state is shown: an operation's last GL call, or its stream point */
    uint32_t was, was_gl_at; /* the records compared with, FI_NONE: nothing */
    uint8_t compare;         /* fi_compare_t */
} fi_state_view_t;
/* At a record, compared with the previous draw request and the previous GL draw call. */
void fi_state_at(const fi_frame_t *frame, uint32_t at, fi_state_view_t *out);
/* The state begin_frame left (init), or after the frame's last record compared with it (end); false when the
 * frame recorded no begin_frame state. */
bool fi_state_frame(const fi_frame_t *frame, bool end, fi_state_view_t *out);
/* Whether a draw request cost a GL draw call: merged into an earlier draw (fi_joined), failed, or new with its
 * ordinal among the frame's GL draw calls (what broke the merge is brk and cause of the frame). */
typedef enum { FI_VERDICT_NOT_DRAW, FI_VERDICT_MERGED, FI_VERDICT_FAILED, FI_VERDICT_NEW } fi_verdict_kind_t;
typedef struct {
    uint8_t kind; /* fi_verdict_kind_t */
    uint32_t ordinal;
} fi_verdict_t;
fi_verdict_t fi_verdict(const fi_frame_t *frame, uint32_t record);

bool fi_matches(const fi_frame_t *frame, uint32_t record, fi_query_t q);
fi_cell_t fi_cell(const fi_frame_t *frame, fi_query_t q);
/* The summary of the records in rows lo..hi (folded rows included), without groups; returns the record count. */
uint32_t fi_range(const fi_frame_t *frame, uint32_t lo, uint32_t hi, fi_summary_t *out);
/* The row of a record or phase, FI_NONE when the frame has none. */
uint32_t fi_row_of(const fi_frame_t *frame, uint32_t id);
fi_tally_t fi_tally(const fi_frame_t *frame, uint32_t id);
/* A handle as two frames compare it: its label and its number among the frame's objects of its kind and label,
 * counted by first use in the frame's work, then the snapshot's (vi/mesh#3: label "mesh", ordinal 3). */
typedef struct {
    const char *label; /* "" when unlabeled */
    uint32_t ordinal;  /* 0 when the frame never names the handle */
} fi_alias_t;

fi_alias_t fi_alias(const fi_frame_t *frame, uint32_t kind, uint32_t object);
/* src on one line, cut to fit size on a whole escape: control bytes as \xNN, and for a token (an alias label)
 * also space, quote, backslash and non-ASCII bytes, so the text splits on spaces. */
/* What fi_one_line escapes besides control bytes: nothing (a file name), quote and backslash (a quoted string),
 * or also space, '#' and non-ASCII bytes (a token: an alias label or a key, so text splits on spaces and '#' ends a label). */
typedef enum { FI_TEXT_PLAIN, FI_TEXT_QUOTED, FI_TEXT_TOKEN } fi_text_t;
void fi_one_line(const char *src, char *buf, size_t size, fi_text_t mode);
/* A location bit set as runs: bit n is location n, 1-4,7; NULL for none. */
const char *fi_locations_text(uint32_t v, char *buf, size_t size);
/* Group g of frame `from` in frame `to`: the same marker path and note; FI_NONE when `to` has none. */
uint32_t fi_group_in(const fi_frame_t *to, const fi_frame_t *from, uint32_t g);
/* The tree parent of a record or phase; FI_NONE above the frame. */
uint32_t fi_parent(const fi_frame_t *frame, uint32_t id);
/* The draw a merged (CACHE) draw joined; FI_NONE for anything else. */
uint32_t fi_joined(const fi_frame_t *frame, uint32_t record);
/* The GL draw call that drew a draw request: its own, or for a merged draw that of the draw its merges joined;
 * FI_NONE when no GL draw call names it. */
uint32_t fi_gl_draw(const fi_frame_t *frame, uint32_t record);
/* Whether two records that set one piece of request state leave it alike: an inherited value (INITIAL)
 * and a request (OP) compare by value, a texture set by its textures. FI_NONE is unset. */
bool fi_same_setting(const fi_frame_t *frame, uint32_t a, uint32_t b);
/* Whether two records that set one GL slot leave the same value: an inherited cache entry and a call compare
 * by the bound name. */
bool fi_gl_same(const fi_frame_t *frame, const fi_gl_slot_t *slot, uint32_t a, uint32_t b);

/* How an issued call's argument reads (capture encoding, render architecture "Frame observation"). */
typedef enum {
    FI_ARG_PLAIN,
    FI_ARG_SIGNED,
    FI_ARG_POINTER, /* a pointer's presence: data or NULL */
    FI_ARG_ENUM,    /* sub: a value set (nt_gfx_capture_domain_t or FI_DOMAIN_*) */
    FI_ARG_NAME,    /* sub: nt_gfx_gl_object_t */
} fi_arg_kind_t;
/* Value sets only GL call arguments use, after the capture field table's domains. */
enum {
    FI_DOMAIN_GL_PRIMITIVE = NT_GFX_DOMAIN_COUNT,
    FI_DOMAIN_GL_CLEAR_MASK,
    FI_DOMAIN_GL_BOOL,
};
typedef struct {
    uint8_t kind, sub;
} fi_arg_t;
/* Argument k of an issued call; a call or position without an entry is a plain number. */
fi_arg_t fi_call_arg(uint32_t call, uint32_t k);

/* The GL name at byte offset `at` of a record, resolved at that record. */
fi_gl_ref_t fi_gl_ref(const fi_frame_t *frame, uint32_t record, uint32_t at);
/* A role's words: "upload VAO", "timer query"; NULL for an object or an unknown name. */
const char *fi_gl_role(uint8_t owner);

/* The frontend objects a record names: its own object, its handle fields, a note's handle, and the GL names it
 * holds resolved at that record. field: the capture field's name, "obj", or an issued call's argument index. */
typedef struct {
    const char *field;
    uint8_t kind;
    uint32_t object;
} fi_named_t;
#define FI_NAMED 28U
uint32_t fi_named_objects(const fi_frame_t *frame, uint32_t record, fi_named_t out[FI_NAMED]);

/* One object through the frame, by one scan: its definition, the GL names it held (a new one after a context
 * restore or a re-creation), and every record that names it, by group and then in record order. */
typedef struct {
    uint32_t name, from; /* a GL name, and the first record that names the object by it */
} fi_gl_span_t;
typedef struct {
    uint8_t kind;
    uint32_t object;
    uint32_t definition; /* its latest CREATE definition, FI_NONE: the frame has none */
    fi_gl_span_t *span;  /* malloc'd */
    uint32_t span_count;
    uint32_t *record; /* malloc'd */
    uint32_t count;
} fi_object_t;
void fi_object(const fi_frame_t *frame, uint32_t kind, uint32_t object, fi_object_t *out);
void fi_object_free(fi_object_t *object);

bool fi_is_draw(const nt_gfx_event_t *e);           /* a draw request of any kind */
bool fi_is_mergeable_draw(const nt_gfx_event_t *e); /* a plain or indexed draw: instanced draws never merge */
bool fi_is_draw_call(uint32_t call);                /* glDrawArrays, glDrawElements and their instanced forms */
/* Each counter next to its recount from the records; the frame storage total and every op and GL call either
 * side counted. stream_bytes has no recount: command sizes are not recorded. */
typedef struct {
    char name[64];
    uint64_t counter, recount;
    bool bytes;
} fi_counter_row_t;
#define FI_COUNTER_ROWS ((uint32_t)NT_GFX_OP_COUNT + (uint32_t)NT_GFX_GL_COUNT + 8U)
uint32_t fi_counter_rows(const fi_frame_t *frame, fi_counter_row_t rows[FI_COUNTER_ROWS]);
/* The record's line text and its length. */
size_t fi_record_text(const fi_frame_t *frame, uint32_t record, const char **text);

/* Two frames compared group by group (groups with requests: B's in order, then those only A has); requests
 * (OP and NOTE records) align like a text diff, paired by kind, operation, aliases (pipe#9) and note label.
 * What a pair compares: render architecture, "Capture text". GL calls are counted, not aligned. */
typedef enum { FI_DIFF_SAME, FI_DIFF_CHANGED, FI_DIFF_ONLY_A, FI_DIFF_ONLY_B } fi_diff_kind_t;
enum { FI_CHANGE_RESULT = 1, FI_CHANGE_MERGE = 2, FI_CHANGE_CALLS = 4, FI_CHANGE_ARGS = 8, FI_CHANGE_ITEMS = 16 /* the request's own records */ };
typedef struct {
    uint32_t a, b;             /* records, FI_NONE on the side without one */
    uint32_t calls_a, calls_b; /* GL calls each issued: the BACKEND records it links */
    uint8_t kind;              /* fi_diff_kind_t */
    uint8_t change;            /* FI_CHANGE_ bits of a changed pair */
} fi_diff_row_t;
typedef struct {
    uint32_t a, b;         /* the group in A and in B, FI_NONE on the side without it */
    uint32_t first, count; /* its rows */
    uint32_t differ;       /* rows that are not SAME */
    bool aligned;          /* false: more than FI_DIFF_EDITS edits apart; after the common ends, A's rows then B's */
} fi_diff_group_t;
typedef struct {
    fi_diff_group_t *group;
    fi_diff_row_t *row;
    uint32_t group_count, row_count;
} fi_diff_t;
#define FI_DIFF_EDITS 1024U
void fi_diff(const fi_frame_t *a, const fi_frame_t *b, fi_diff_t *out);
void fi_diff_free(fi_diff_t *diff);

const char *fi_node_name(fi_node_t node);
const char *fi_break_name(fi_break_t brk);

#endif /* FI_MODEL_H */
