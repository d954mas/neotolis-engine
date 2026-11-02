#ifndef FI_QUERY_H
#define FI_QUERY_H

/* Frame inspector text: record and state lines the window and the query mode both show, the frame's numbers
 * both list, and the query commands. No UI and no gfx backend. */

#include "fi_model.h"

/* The record's capture-text line without what a tree row shows elsewhere (its kind tag, result, parent link
 * and draw details); handles as short names (pipe#3), GL calls as calls: glEnable(GL_SCISSOR_TEST). */
void fi_row_text(const fi_frame_t *frame, uint32_t record, char *buf, size_t size);
/* pipe#3 for a handle; kinds are numbered in order of first appearance in the frame. */
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
} fi_state_line_t;
#define FI_STATE_LINES (16U + FI_GL_SLOTS)
/* all: every GL line; otherwise only the GL lines that changed, when the view compares with a GL draw. */
uint32_t fi_state_lines(const fi_frame_t *frame, const fi_state_view_t *view, bool all, fi_state_line_t out[FI_STATE_LINES]);

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
    uint64_t value;
} fi_number_t;
#define FI_NUMBERS (32U + (FI_OBJECTS * FI_RES_COUNT * 2U) + ((uint32_t)NT_GFX_OP_COUNT * (FI_RESULTS + 3U)) + (uint32_t)NT_GFX_GL_COUNT)
/* all: zeros too, so a key stays valid on any frame; without it only the numbers worth showing. */
uint32_t fi_numbers(const fi_frame_t *frame, bool all, fi_number_t out[FI_NUMBERS]);
/* Whether a group holds draw requests, GL calls or drops: groups and the Information page show only these. */
bool fi_group_has_work(const fi_frame_t *frame, uint32_t group);

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
