#ifndef FI_MODEL_H
#define FI_MODEL_H

/* Frame inspector model: capture files parsed into frames, each with its logical tree, merge check
 * and summary. No UI and no gfx backend, so the tests drive it directly. */

#include "graphics/nt_gfx.h"

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
    /* Short names for handles (pipe#3): per kind, numbered in order of first appearance. */
    uint32_t *alias_object, *alias_number, *alias_slot, alias_mask;
    uint8_t *alias_kind;
    uint32_t *def;                     /* per operation on an object: the object's latest definition before it, FI_NONE */
    fi_buffer_upload_t *buffer_upload; /* the biggest first */
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
    fi_frame_t *frames;
    uint32_t frame_count;
} fi_file_t;

/* Reads and parses a capture file; false when it cannot be read. */
bool fi_file_open(fi_file_t *file, const char *path);
/* Parses text (malloc'd, len bytes; the file takes it) into frames. */
void fi_file_load(fi_file_t *file, const char *name, char *text, size_t len);
void fi_file_close(fi_file_t *file);

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
    uint32_t gl_at;          /* the record whose GL state is shown: an operation's last GL call */
    uint32_t was, was_gl_at; /* the records compared with, FI_NONE: nothing */
    uint8_t compare;         /* fi_compare_t */
} fi_state_view_t;
/* At a record, compared with the previous draw request and the previous GL draw call. */
void fi_state_at(const fi_frame_t *frame, uint32_t at, fi_state_view_t *out);
/* The state begin_frame left (init), or after the frame's last record compared with it (end); false when the
 * frame recorded no begin_frame state. */
bool fi_state_frame(const fi_frame_t *frame, bool end, fi_state_view_t *out);
/* Whether a draw request cost a GL draw call: merged into an earlier draw (joined), failed, or new; a new one has
 * its ordinal among the frame's GL draw calls and, unless instanced, what broke the merge (brk, cause). */
typedef enum { FI_VERDICT_NOT_DRAW, FI_VERDICT_MERGED, FI_VERDICT_FAILED, FI_VERDICT_NEW } fi_verdict_kind_t;
typedef struct {
    uint8_t kind; /* fi_verdict_kind_t */
    uint8_t brk;  /* fi_break_t */
    bool instanced;
    uint32_t joined, cause, ordinal;
} fi_verdict_t;
fi_verdict_t fi_verdict(const fi_frame_t *frame, uint32_t record);

bool fi_matches(const fi_frame_t *frame, uint32_t record, fi_query_t q);
fi_cell_t fi_cell(const fi_frame_t *frame, fi_query_t q);
/* The summary of the records in rows lo..hi (folded rows included), without groups; returns the record count. */
uint32_t fi_range(const fi_frame_t *frame, uint32_t lo, uint32_t hi, fi_summary_t *out);
/* The row of a record or phase, FI_NONE when the frame has none. */
uint32_t fi_row_of(const fi_frame_t *frame, uint32_t id);
fi_tally_t fi_tally(const fi_frame_t *frame, uint32_t id);
/* The alias number of a handle (pipe#3 is 3), 0 when the frame never names it. */
uint32_t fi_alias(const fi_frame_t *frame, uint32_t kind, uint32_t object);
/* Group g of frame `from` in frame `to`: the same marker path and note; FI_NONE when `to` has none. */
uint32_t fi_group_in(const fi_frame_t *to, const fi_frame_t *from, uint32_t g);
/* The tree parent of a record or phase; FI_NONE above the frame. */
uint32_t fi_parent(const fi_frame_t *frame, uint32_t id);
/* The draw a merged (CACHE) draw joined; FI_NONE for anything else. */
uint32_t fi_joined(const fi_frame_t *frame, uint32_t record);
/* Whether two records that set one piece of request state leave it alike: an inherited value (INITIAL)
 * and a request (OP) compare by value, a texture set by its textures. FI_NONE is unset. */
bool fi_same_setting(const fi_frame_t *frame, uint32_t a, uint32_t b);
/* Whether two records that set one GL slot leave the same value: an inherited cache entry and a call compare
 * by the bound name. */
bool fi_gl_same(const fi_frame_t *frame, const fi_gl_slot_t *slot, uint32_t a, uint32_t b);
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

const char *fi_node_name(fi_node_t node);
const char *fi_break_name(fi_break_t brk);

#endif /* FI_MODEL_H */
