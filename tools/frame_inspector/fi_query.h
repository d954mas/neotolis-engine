#ifndef FI_QUERY_H
#define FI_QUERY_H

/* Frame inspector text: records, state and a frame's numbers as plain lines. The window shows these lines and
 * the query mode prints them, so an answer and the window agree. No UI and no gfx backend. */

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
uint32_t fi_state_lines(const fi_frame_t *frame, const fi_state_view_t *view, fi_state_line_t out[FI_STATE_LINES]);

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
 * says why). Commands: help, frames, frame SEQUENCE, summary, groups, record @N, state @N|init|end [all],
 * find TEXT, list draws|problems|merged|group NAME|op NAME. */
bool fi_query(fi_session_t *session, const char *line, fi_out_t *out);

#endif /* FI_QUERY_H */
