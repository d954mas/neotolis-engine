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
    FI_Q_VERTICES,    /* draws that count geometry; sum = vertices x instances */
    FI_Q_INDICES,     /* indexed draws; sum = indices x instances */
    FI_Q_INSTANCES,   /* instanced draws; sum = instances */
    FI_Q_MERGED,      /* draws merged into the previous one (CACHE) */
    FI_Q_BREAK,       /* a = fi_break_t */
    FI_Q_UPLOAD,      /* a = 0 buffer, 1 texture: issued calls with payload; sum = bytes */
    FI_Q_FRAME_STORE, /* frame storage uploads of end_frame; sum = bytes */
    FI_Q_RESOURCE,    /* a = nt_gfx_object_kind_t, b = fi_res_t; sum = bytes requested from GL */
    FI_Q_GROUP,       /* a = group, b = fi_metric_t */
    FI_Q_DROP,        /* renderer `drop` note fields */
    FI_Q_PROBLEM,     /* drops, results other than ACCEPTED/CACHE, SKIP records */
} fi_query_kind_t;

typedef enum { FI_RES_LIVE, FI_RES_CREATED, FI_RES_DESTROYED, FI_RES_COUNT } fi_res_t;
typedef enum { FI_METRIC_DRAWS, FI_METRIC_CALLS, FI_METRIC_BYTES, FI_METRIC_DROPS, FI_METRIC_COUNT } fi_metric_t;

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
    fi_cell_t vertices, indices, instances, merged;
    fi_cell_t brk[FI_BREAK_COUNT];
    fi_cell_t upload[2];
    fi_cell_t frame_store;
    fi_cell_t resource[FI_OBJECTS][FI_RES_COUNT];
    fi_cell_t drops, problems;
    fi_cell_t *group; /* [group_count][FI_METRIC_COUNT] */
} fi_summary_t;

typedef struct {
    nt_gfx_capture_view_t view; /* events NULL: the header was rejected */
    const char *error;          /* NULL when the document parsed whole */
    uint32_t error_line, error_col;
    const char **line; /* record i's text line in the file, without its index; ends at '\n' */
    const char *text_end;
    uint32_t first_op; /* the first OP record: link-0 INITIAL/DEFINITION before it are the snapshot */
    /* Logical tree: rows in display order, each a record index or FI_NODE_BASE + fi_node_t. */
    uint32_t *row;
    uint16_t *depth;
    uint32_t row_count;
    uint32_t *parent; /* per record: a record index or FI_NODE_BASE + fi_node_t */
    /* Group: the note or marker that governs a record, as marker path + note label. Group 0 is "no group". */
    uint32_t *group;
    char (*group_key)[FI_KEY_LEN];
    uint32_t group_count;
    /* Merge check: per draw the break and the record that caused it (the command between, or the previous draw). */
    uint8_t *brk;
    uint32_t *cause;
    uint32_t merge_mismatch; /* first draw the merge rule predicts wrongly; FI_NONE when it holds */
    bool timing;             /* the frame ran GPU timing, so segments recorded commands */
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
} fi_gl_state_t;
void fi_gl_state(const fi_frame_t *frame, uint32_t at, fi_gl_state_t *out);

bool fi_matches(const fi_frame_t *frame, uint32_t record, fi_query_t q);
fi_cell_t fi_cell(const fi_frame_t *frame, fi_query_t q);
/* The frame's counters rebuilt from its records. stream_bytes and frame_bytes come from the view: command
 * sizes and storage streams are not in the records (the frame storage upload total is FI_Q_FRAME_STORE). */
nt_gfx_counters_t fi_recount(const fi_frame_t *frame);
/* The record's line text and its length. */
size_t fi_record_text(const fi_frame_t *frame, uint32_t record, const char **text);
/* Bytes GL was asked to hold for a resource definition: buffer size, texture levels. */
uint64_t fi_definition_bytes(const nt_gfx_event_t *e);

const char *fi_node_name(fi_node_t node);
const char *fi_break_name(fi_break_t brk);

#endif /* FI_MODEL_H */
