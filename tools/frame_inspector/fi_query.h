#ifndef FI_QUERY_H
#define FI_QUERY_H

/* Frame inspector text: record and state lines the window and the query mode both show, the frame's numbers
 * both list, and the query commands. No UI and no gfx backend. */

#include "fi_model.h"

/* The record as a row, from its fields (capture field table): values decoded by meaning, handles as aliases
 * (pipe/mesh), GL calls as calls (glEnable(GL_SCISSOR_TEST)); without what a tree row shows elsewhere (its
 * result, parent link and a draw's first, vertices and instances). */
void fi_row_text(const fi_frame_t *frame, uint32_t record, char *buf, size_t size);
/* A handle as the views name it: vi/mesh, vi/mesh#3 when the label repeats, vi#3 unlabeled, vi:65537 when the frame never names it (fi_alias); a
 * long label is cut, never the number. buf holds FI_ALIAS_LEN bytes. */
#define FI_ALIAS_LEN 128
void fi_alias_text(const fi_frame_t *frame, uint32_t kind, uint32_t object, char *buf, size_t size);
/* The badge of a record: blank when a request did its work, what CACHE means for its operation, a skip, a failure. */
const char *fi_result_word(const nt_gfx_event_t *e);
bool fi_contains_ci(const char *text, size_t len, const char *needle);

/* One line of a state view: request state first, then GL state (gl). `before` is the setter at the record the
 * view compares with; changed when it leaves another value. */
typedef struct {
    char name[48];
    uint32_t now, before; /* records, FI_NONE: unset */
    bool inherited, changed, gl;
    fi_gl_slot_t slot; /* gl: the slot the line shows */
} fi_state_line_t;
#define FI_STATE_LINES (16U + FI_GL_SLOTS)
/* all: every GL line; otherwise only the GL lines that changed, when the view compares with a GL draw. */
uint32_t fi_state_lines(const fi_frame_t *frame, const fi_state_view_t *view, bool all, fi_state_line_t out[FI_STATE_LINES]);

/* The value a state line shows at `record` (its now or before): the record's row text, or, for one field of an
 * inherited backend record that holds several slots, only that slot's field ("texture=5"). */
void fi_state_value(const fi_frame_t *frame, const fi_state_line_t *line, uint32_t record, char *buf, size_t size);

/* What a state view is compared with, as text: "previous draw", "previous GL draw", "frame start". */
const char *fi_compare_name(uint8_t compare, bool gl);

/* A number of the frame: a key for scripts, its meaning, its value and the query for the records behind it. The
 * Information page shows these numbers (operations as its table); summary prints them, list KEY their records. */
typedef enum { FI_SECTION_FRAME, FI_SECTION_DRAWS, FI_SECTION_UPLOADS, FI_SECTION_RESOURCES, FI_SECTION_OPERATIONS, FI_SECTION_GL } fi_section_t;
typedef struct {
    char key[48];
    char meaning[96];
    uint8_t section; /* fi_section_t */
    fi_query_t q;
    bool sum; /* the value is the query's sum (bytes, vertices), not its record count */
    uint64_t value;
} fi_number_t;
#define FI_NUMBERS (32U + (FI_OBJECTS * FI_RES_COUNT * 2U) + ((uint32_t)NT_GFX_OP_COUNT * (FI_RESULTS + 3U)) + (uint32_t)NT_GFX_GL_COUNT)
/* all: zeros too, so a key stays valid on any frame; without it only the numbers worth showing. */
uint32_t fi_numbers(const fi_frame_t *frame, bool all, fi_number_t out[FI_NUMBERS]);
/* The same number in another frame: keys are the same on every frame. */
uint64_t fi_number_in(const fi_frame_t *frame, const fi_number_t *number);
/* B's numbers with each one's value in A, for Compare: order runs by section and, in each, by the size of
 * the change, biggest first, so equal numbers come last. */
typedef struct {
    fi_number_t number[FI_NUMBERS];
    uint64_t was[FI_NUMBERS];
    uint32_t order[FI_NUMBERS];
    uint32_t count;
} fi_number_diff_t;
void fi_number_diff(const fi_frame_t *a, const fi_frame_t *b, fi_number_diff_t *out);
/* What changed in a changed pair of fi_diff, e.g. "CACHE -> ACCEPTED; break: merged -> a command recorded between"
 * or, for arguments other than a draw's range, A's request: "A: VIEWPORT width=1280 height=720". */
void fi_change_text(const fi_frame_t *a, const fi_frame_t *b, const fi_diff_row_t *row, char *buf, size_t size);
/* What sets a compared group apart, e.g. "only in A; requests 3 -> 4; 2 of 9 requests differ"; empty when
 * nothing does. */
void fi_group_change_text(const fi_frame_t *a, const fi_frame_t *b, const fi_diff_group_t *group, char *buf, size_t size);
/* From row r of a group: the end of the run of same rows there when it shows as one line (three or more), else r. */
uint32_t fi_diff_fold(const fi_diff_t *diff, const fi_diff_group_t *group, uint32_t r);
/* fi_metric_t names for people. */
extern const char *const fi_metric_names[FI_METRIC_COUNT];
/* Whether a group holds draw requests, GL calls or drops: groups and the Information page show only these. */
bool fi_group_has_work(const fi_frame_t *frame, uint32_t group);
/* A group's fi_metric_t value; 0 for group FI_NONE, the frame a compared group is missing from. */
uint64_t fi_group_metric(const fi_frame_t *frame, uint32_t group, uint32_t metric);
/* Whether a compared group differs: only one frame has it, a metric or a request differs. */
bool fi_group_differs(const fi_frame_t *a, const fi_frame_t *b, const fi_diff_group_t *group);

/* Text appended to a malloc'd buffer that grows; the caller frees text. */
typedef struct {
    char *text;
    size_t len, cap;
} fi_out_t;

/* Open files and the frame the commands read. */
typedef struct {
    const fi_file_t *files;
    uint32_t file_count;
    uint32_t file, frame;
} fi_session_t;

/* Whether a word names a command. */
bool fi_query_command(const char *word);
/* Runs one command line and appends its answer; false for an unknown command or a bad argument (the answer
 * says why). The help command lists the commands. */
bool fi_query(fi_session_t *session, const char *line, fi_out_t *out);

#endif /* FI_QUERY_H */
