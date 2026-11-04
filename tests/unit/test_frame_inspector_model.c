#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fi_model.h"
#include "fi_query.h"
#include "fs/nt_fs.h"
#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_capture_text.h"
#include "test_helpers/nt_gfx_fake.h"
#include "unity.h"

static fi_file_t s_file;
static nt_pipeline_t s_pipeline;
static nt_vertex_input_t s_vi;
static nt_buffer_t s_vbo;

void setUp(void) {
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    desc.capture_capacity = 512;
    desc.capture_string_capacity = 16384;
    desc.frame_capacity[NT_GFX_FRAME_VERTEX] = 4096;
    desc.frame_capacity[NT_GFX_FRAME_UNIFORM] = 4096;
    nt_gfx_init(&desc);
    static const float verts[36] = {0};
    static const uint16_t indices[12] = {0};
    s_vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .data = verts, .size = sizeof(verts)});
    nt_buffer_t ibo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_INDEX, .usage = NT_USAGE_IMMUTABLE, .data = indices, .size = sizeof(indices), .index_type = NT_INDEX_UINT16});
    s_vi = nt_gfx_make_vertex_input(
        &(nt_vertex_input_desc_t){.layout = {.attr_count = 1, .stride = 12, .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 3}}}, .vertex_buffer = s_vbo, .index_buffer = ibo});
    s_pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = nt_gfx_fake_make_program(NULL, 0)});
    nt_gfx_begin_frame();
}

void tearDown(void) {
    fi_file_close(&s_file);
    nt_gfx_shutdown();
    nt_gfx_fake_reset();
}

// #region helpers
/* Records one frame of body and returns its capture text (malloc'd). */
static char *record(void (*body)(void), size_t *len) {
    nt_gfx_capture_request();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    body();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    const nt_gfx_capture_view_t view = nt_gfx_capture_read();
    TEST_ASSERT_NOT_NULL(view.events);
    *len = nt_gfx_capture_format(&view, NULL, 0);
    char *text = (char *)malloc(*len + 1);
    TEST_ASSERT_NOT_NULL(text);
    nt_gfx_capture_format(&view, text, *len + 1);
    return text;
}

static const fi_frame_t *load(fi_file_t *file, char *text, size_t len) {
    fi_file_load(file, "test", text, len);
    TEST_ASSERT_EQUAL_UINT32(1, file->frame_count);
    TEST_ASSERT_NULL(file->frames[0].error);
    return &file->frames[0];
}

/* Formats hand-made records into capture text and loads it into file. */
static const fi_frame_t *load_events(fi_file_t *file, const nt_gfx_event_t *ev, uint32_t count, const nt_gfx_counters_t *counters) {
    nt_gfx_capture_view_t view = {.events = ev, .count = count};
    if (counters != NULL) {
        view.counters = *counters;
    }
    const size_t len = nt_gfx_capture_format(&view, NULL, 0);
    char *text = (char *)malloc(len + 1);
    TEST_ASSERT_NOT_NULL(text);
    nt_gfx_capture_format(&view, text, len + 1);
    return load(file, text, len);
}

/* Every counter equals its recount from the records. */
static void assert_counters_match(const fi_frame_t *fr) {
    fi_counter_row_t rows[FI_COUNTER_ROWS];
    const uint32_t n = fi_counter_rows(fr, rows);
    TEST_ASSERT_GREATER_THAN_UINT32(0, n);
    for (uint32_t k = 0; k < n; k++) {
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(rows[k].counter, rows[k].recount, rows[k].name);
    }
}

static const fi_frame_t *capture(void (*body)(void)) {
    size_t len = 0;
    char *text = record(body, &len);
    return load(&s_file, text, len);
}

/* The nth (0-based) top-level or nested OP record of an operation. */
static uint32_t find_op(const fi_frame_t *fr, nt_gfx_operation_t op, uint32_t nth) {
    for (uint32_t i = 0; i < fr->view.count; i++) {
        if (fr->view.events[i].kind == NT_GFX_EVENT_OP && fr->view.events[i].operation == op && nth-- == 0) {
            return i;
        }
    }
    TEST_FAIL_MESSAGE("operation not recorded");
    return FI_NONE;
}

static uint32_t find_kind(const fi_frame_t *fr, nt_gfx_event_kind_t kind, uint32_t nth) {
    for (uint32_t i = 0; i < fr->view.count; i++) {
        if (fr->view.events[i].kind == kind && nth-- == 0) {
            return i;
        }
    }
    TEST_FAIL_MESSAGE("record kind not recorded");
    return FI_NONE;
}

static void draw_setup(void) {
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(s_pipeline);
    nt_gfx_bind_vertex_input(s_vi);
}
// #endregion

// #region tree
static void tree_body(void) {
    NT_GFX_MARKER_PUSH("scene");
    NT_GFX_NOTE("mesh", NT_NOTE_U32("material", 1));
    nt_gfx_update_buffer(s_vbo, 0, (const float[3]){0}, 12);
    draw_setup();
    static const float block[4] = {1, 2, 3, 4};
    nt_gfx_bind_uniform_block(4, block, sizeof(block));
    nt_gfx_draw(0, 3);
    NT_GFX_NOTE("sprite", NT_NOTE_STR("drop", "page not ready"));
    nt_gfx_end_pass();
    NT_GFX_MARKER_POP();
}

/* A note covers the operations after it up to the next note or marker; a drop note stands alone. */
static void test_notes_cover_the_operations_after_them(void) {
    const fi_frame_t *fr = capture(tree_body);
    const uint32_t marker = find_kind(fr, NT_GFX_EVENT_MARKER_PUSH, 0);
    const uint32_t mesh = find_kind(fr, NT_GFX_EVENT_NOTE, 0);
    const uint32_t sprite = find_kind(fr, NT_GFX_EVENT_NOTE, 1);
    TEST_ASSERT_EQUAL_UINT32(marker, fr->parent[mesh]);
    TEST_ASSERT_EQUAL_UINT32(marker, fr->parent[sprite]);
    TEST_ASSERT_EQUAL_UINT32(mesh, fr->parent[find_kind(fr, NT_GFX_EVENT_NOTE_FIELD, 0)]);
    TEST_ASSERT_EQUAL_UINT32(mesh, fr->parent[find_op(fr, NT_GFX_OP_BUFFER_UPLOAD, 0)]);
    TEST_ASSERT_EQUAL_UINT32(mesh, fr->parent[find_op(fr, NT_GFX_OP_DRAW, 0)]);
    TEST_ASSERT_EQUAL_UINT32(sprite, fr->parent[find_op(fr, NT_GFX_OP_END_PASS, 0)]);
    TEST_ASSERT_EQUAL_STRING("scene > mesh", fr->group_key[fr->group[find_op(fr, NT_GFX_OP_DRAW, 0)]]);
    TEST_ASSERT_EQUAL_UINT64(1, fi_cell(fr, (fi_query_t){.kind = FI_Q_DROP}).n);
    TEST_ASSERT_EQUAL_UINT64(1, fi_cell(fr, (fi_query_t){.kind = FI_Q_GROUP, .a = fr->group[sprite], .b = FI_METRIC_DROPS}).n);
}

static void same_label_body(void) {
    NT_GFX_MARKER_PUSH("ui");
    NT_GFX_MARKER_POP();
    NT_GFX_NOTE("ui", NT_NOTE_U32("n", 1));
}

/* A marker and a note of one label are two groups, and their keys tell them apart. */
static void test_a_marker_and_a_note_of_one_label_differ(void) {
    const fi_frame_t *fr = capture(same_label_body);
    const uint32_t marker = fr->group[find_kind(fr, NT_GFX_EVENT_MARKER_PUSH, 0)];
    const uint32_t note = fr->group[find_kind(fr, NT_GFX_EVENT_NOTE, 0)];
    TEST_ASSERT_NOT_EQUAL_UINT32(marker, note);
    TEST_ASSERT_EQUAL_STRING("ui", fr->group_key[marker]);
    TEST_ASSERT_EQUAL_STRING("> ui", fr->group_key[note]);
    TEST_ASSERT_EQUAL_UINT32(note, fi_group_in(fr, fr, note));
    TEST_ASSERT_EQUAL_UINT32(marker, fi_group_in(fr, fr, marker));
}

static void control_label_body(void) { NT_GFX_NOTE("a\nb", NT_NOTE_U32("n", 1)); }

/* A label's line break shows as \x0a: a group key is one line wherever it is printed. */
static void test_a_group_key_is_one_line(void) {
    const fi_frame_t *fr = capture(control_label_body);
    TEST_ASSERT_EQUAL_STRING("> a\\x0ab", fr->group_key[fr->group[find_kind(fr, NT_GFX_EVENT_NOTE, 0)]]);
}

/* The snapshot opens begin_frame; the frame storage upload of the uniform block belongs to end_frame. */
static void test_phases_follow_the_frame(void) {
    const fi_frame_t *fr = capture(tree_body);
    TEST_ASSERT_EQUAL_UINT32(FI_NODE_BASE + FI_NODE_SNAPSHOT, fr->parent[0]);
    const uint32_t storage = find_op(fr, NT_GFX_OP_BUFFER_UPLOAD, 1);
    TEST_ASSERT_TRUE(storage >= fr->view.execute_begin && storage < fr->view.execute_end);
    TEST_ASSERT_EQUAL_UINT32(FI_NODE_BASE + FI_NODE_END_FRAME, fr->parent[storage]);
    TEST_ASSERT_EQUAL_UINT32(0, fr->group[storage]);
    TEST_ASSERT_EQUAL_UINT64(1, fi_cell(fr, (fi_query_t){.kind = FI_Q_FRAME_STORE}).n);
    TEST_ASSERT_EQUAL_UINT64(16, fi_cell(fr, (fi_query_t){.kind = FI_Q_FRAME_STORE}).sum);
}

/* A program that finished linking in begin_frame is a finished link, not inherited state. */
static void test_a_finished_link_is_not_the_snapshot(void) {
    nt_gfx_event_t ev[4];
    memset(ev, 0, sizeof(ev));
    ev[0].kind = NT_GFX_EVENT_DEFINITION;
    ev[0].operation = NT_GFX_OP_CREATE;
    ev[0].object_kind = NT_GFX_OBJECT_PROGRAM;
    ev[0].object = 0x10001;
    ev[0].data.program.slot = 1;
    ev[1].kind = NT_GFX_EVENT_BACKEND;
    ev[1].operation = NT_GFX_OP_STATE;
    ev[1].detail = NT_GFX_GL_glGetProgramiv;
    ev[2] = ev[0];
    ev[2].object = 0x10002;
    ev[2].data.program.slot = 2;
    ev[3].kind = NT_GFX_EVENT_OP;
    ev[3].operation = NT_GFX_OP_VIEWPORT;
    ev[3].result = NT_GFX_RESULT_ACCEPTED;
    nt_gfx_counters_t counters = {0};
    counters.gl[NT_GFX_GL_glGetProgramiv] = 1;
    counters.accepted[NT_GFX_OP_VIEWPORT] = 1;
    const fi_frame_t *fr = load_events(&s_file, ev, 4, &counters);
    TEST_ASSERT_EQUAL_UINT32(FI_NODE_BASE + FI_NODE_SNAPSHOT, fr->parent[0]);
    TEST_ASSERT_EQUAL_UINT32(FI_NODE_BASE + FI_NODE_PROGRAM_LINKS, fr->parent[1]);
    TEST_ASSERT_EQUAL_UINT32(FI_NODE_BASE + FI_NODE_PROGRAM_LINKS, fr->parent[2]);
    TEST_ASSERT_EQUAL_UINT64(1, fi_cell(fr, (fi_query_t){.kind = FI_Q_RESOURCE, .a = NT_GFX_OBJECT_PROGRAM, .b = FI_RES_LIVE}).n);
}

/* A restored context remakes the frame storage buffers inside begin_frame; the game's buffers come after. */
static void test_a_context_restore_belongs_to_begin_frame(void) {
    nt_gfx_event_t ev[3 + (2 * NT_GFX_FRAME_STREAM_COUNT)];
    memset(ev, 0, sizeof(ev));
    uint32_t n = 0;
    ev[n].kind = NT_GFX_EVENT_OP;
    ev[n].operation = NT_GFX_OP_CONTEXT;
    ev[n++].result = NT_GFX_RESULT_ACCEPTED;
    for (uint32_t k = 0; k < NT_GFX_FRAME_STREAM_COUNT; k++) {
        for (uint32_t op = 0; op < 2; op++) {
            ev[n].kind = NT_GFX_EVENT_OP;
            ev[n].operation = op == 0 ? NT_GFX_OP_DESTROY : NT_GFX_OP_CREATE;
            ev[n].object_kind = NT_GFX_OBJECT_BUFFER;
            ev[n].object = 0x10001 + k;
            ev[n++].result = NT_GFX_RESULT_ACCEPTED;
        }
    }
    ev[n] = ev[n - 1];
    ev[n++].object = 0x20001; /* the game's own buffer */
    nt_gfx_counters_t counters = {0};
    counters.accepted[NT_GFX_OP_CONTEXT] = 1;
    counters.accepted[NT_GFX_OP_DESTROY] = NT_GFX_FRAME_STREAM_COUNT;
    counters.accepted[NT_GFX_OP_CREATE] = NT_GFX_FRAME_STREAM_COUNT + 1;
    const fi_frame_t *fr = load_events(&s_file, ev, n, &counters);
    for (uint32_t i = 0; i + 1U < n; i++) {
        TEST_ASSERT_EQUAL_UINT32(FI_NODE_BASE + FI_NODE_BEGIN_FRAME, fr->parent[i]);
    }
    TEST_ASSERT_EQUAL_UINT32(FI_NODE_BASE + FI_NODE_CALLS, fr->parent[n - 1U]);
}

/* Rows hold every record once, each after its parent and one level deeper. */
static void test_rows_are_a_preorder_of_the_tree(void) {
    const fi_frame_t *fr = capture(tree_body);
    uint32_t *at = (uint32_t *)malloc(sizeof(uint32_t) * (fr->view.count + FI_NODE_COUNT));
    TEST_ASSERT_NOT_NULL(at);
    memset(at, 0xFF, sizeof(uint32_t) * (fr->view.count + FI_NODE_COUNT));
    for (uint32_t r = 0; r < fr->row_count; r++) {
        const uint32_t id = fr->row[r] >= FI_NODE_BASE ? fr->view.count + (fr->row[r] - FI_NODE_BASE) : fr->row[r];
        TEST_ASSERT_EQUAL_UINT32(FI_NONE, at[id]);
        at[id] = r;
    }
    for (uint32_t i = 0; i < fr->view.count; i++) {
        TEST_ASSERT_NOT_EQUAL_UINT32(FI_NONE, at[i]);
        const uint32_t p = fr->parent[i] >= FI_NODE_BASE ? fr->view.count + (fr->parent[i] - FI_NODE_BASE) : fr->parent[i];
        TEST_ASSERT_LESS_THAN_UINT32(at[i], at[p]);
        TEST_ASSERT_EQUAL_UINT16(fr->depth[at[p]] + 1U, fr->depth[at[i]]);
    }
    free(at);
}
// #endregion

// #region merge check
static void merge_body(void) {
    draw_setup();
    nt_gfx_draw(0, 3);               /* opens a chain */
    nt_gfx_draw(3, 3);               /* merges */
    nt_gfx_draw(9, 3);               /* RANGE */
    nt_gfx_draw_indexed(0, 3, 3);    /* FUNCTION */
    nt_gfx_draw_instanced(0, 3, 2);  /* records a command, never merges */
    nt_gfx_draw(0, 3);               /* INSTANCED */
    nt_gfx_set_viewport(0, 0, 4, 4); /* records a command */
    nt_gfx_draw(3, 3);               /* COMMAND */
    nt_gfx_update_buffer(s_vbo, 0, (const float[3]){0}, 12);
    nt_gfx_draw(6, 3); /* immediate work between keeps the merge */
    static const float block[4] = {0};
    nt_gfx_bind_uniform_block(5, block, sizeof(block)); /* COMMAND */
    nt_gfx_draw(9, 3);
    nt_gfx_unbind_uniform_block(5); /* a logical unbind records nothing */
    nt_gfx_draw(12, 3);             /* merges */
    nt_gfx_end_pass();
}

static void test_merge_rule_matches_the_frontend(void) {
    const fi_frame_t *fr = capture(merge_body);
    TEST_ASSERT_EQUAL_UINT32(FI_NONE, fr->merge_mismatch);
    const uint8_t expected[] = {FI_BREAK_COMMAND, FI_BREAK_NONE, FI_BREAK_RANGE, FI_BREAK_INSTANCED, FI_BREAK_COMMAND, FI_BREAK_NONE, FI_BREAK_COMMAND, FI_BREAK_NONE};
    for (uint32_t k = 0; k < sizeof(expected); k++) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(expected[k], fr->brk[find_op(fr, NT_GFX_OP_DRAW, k)], "draw break");
    }
    TEST_ASSERT_EQUAL_UINT8(FI_BREAK_FUNCTION, fr->brk[find_op(fr, NT_GFX_OP_DRAW_INDEXED, 0)]);
    TEST_ASSERT_EQUAL_UINT32(find_op(fr, NT_GFX_OP_VIEWPORT, 0), fr->cause[find_op(fr, NT_GFX_OP_DRAW, 4)]);
    TEST_ASSERT_EQUAL_UINT64(3, fi_cell(fr, (fi_query_t){.kind = FI_Q_MERGED}).n);
    TEST_ASSERT_EQUAL_UINT64(3, fi_cell(fr, (fi_query_t){.kind = FI_Q_BREAK, .a = FI_BREAK_COMMAND}).n);
    /* The frame row's tally and a range over every row give the summary's numbers. */
    const fi_tally_t all = fi_tally(fr, FI_NODE_BASE + FI_NODE_FRAME);
    TEST_ASSERT_EQUAL_UINT32(fr->summary.vertices.n, all.draws);
    TEST_ASSERT_EQUAL_UINT32(fr->summary.merged.n, all.merged);
    TEST_ASSERT_EQUAL_UINT32(fr->summary.draw_calls.n, all.gl_draws);
    uint64_t by_group = 0; /* every GL draw call belongs to one group */
    for (uint32_t g = 0; g < fr->group_count; g++) {
        by_group += fi_cell(fr, (fi_query_t){.kind = FI_Q_GROUP, .a = g, .b = FI_METRIC_DRAW_CALLS}).n;
    }
    TEST_ASSERT_EQUAL_UINT64(fr->summary.draw_calls.n, by_group);
    fi_summary_t range;
    TEST_ASSERT_EQUAL_UINT32(fr->view.count, fi_range(fr, 0, fr->row_count - 1U, &range));
    TEST_ASSERT_EQUAL_UINT64(fr->summary.vertices.sum, range.vertices.sum);
    TEST_ASSERT_EQUAL_UINT64(fr->summary.draw_calls.n, range.draw_calls.n);
}

/* A merge the rule does not predict marks the frame. */
static void test_a_wrong_merge_marks_the_frame(void) {
    size_t len = 0;
    char *text = record(merge_body, &len);
    /* The draw that breaks on its range claims to join the first chain. */
    const char *joined = strstr(text, "result=CACHE joined=@");
    char *range = strstr(text, "result=ACCEPTED first=9 ");
    TEST_ASSERT_NOT_NULL(joined);
    TEST_ASSERT_NOT_NULL(range);
    char head[16] = {0};
    const char *ref = joined + strlen("result=CACHE joined=");
    for (size_t k = 0; k + 1 < sizeof(head) && ref[k] != ' ' && ref[k] != '\n'; k++) {
        head[k] = ref[k];
    }
    char *planted = (char *)malloc(len + 32);
    TEST_ASSERT_NOT_NULL(planted);
    const size_t before = (size_t)(range - text);
    const size_t old = strlen("result=ACCEPTED");
    const int added = snprintf(planted + before, 32, "result=CACHE joined=%s", head);
    memcpy(planted, text, before);
    memcpy(planted + before + added, range + old, len - before - old);
    len = len - old + (size_t)added;
    free(text);
    fi_file_load(&s_file, "test", planted, len);
    TEST_ASSERT_NULL(s_file.frames[0].error);
    const fi_frame_t *fr = &s_file.frames[0];
    TEST_ASSERT_EQUAL_UINT32(find_op(fr, NT_GFX_OP_DRAW, 2), fr->merge_mismatch);
}
// #endregion

// #region summary
/* Every summary number equals the records its query selects, so a click shows exactly what was counted. */
static void test_each_number_counts_the_records_its_query_selects(void) {
    const fi_frame_t *fr = capture(merge_body);
    const fi_query_t queries[] = {
        {.kind = FI_Q_OP, .a = NT_GFX_OP_DRAW, .b = NT_GFX_RESULT_CACHE},
        {.kind = FI_Q_OP, .a = NT_GFX_OP_DRAW, .b = NT_GFX_RESULT_ACCEPTED},
        {.kind = FI_Q_MERGED},
        {.kind = FI_Q_VERTICES},
        {.kind = FI_Q_INSTANCES},
        {.kind = FI_Q_BREAK, .a = FI_BREAK_RANGE},
        {.kind = FI_Q_RESOURCE, .a = NT_GFX_OBJECT_BUFFER, .b = FI_RES_LIVE},
    };
    for (uint32_t k = 0; k < sizeof(queries) / sizeof(queries[0]); k++) {
        uint64_t matched = 0;
        for (uint32_t i = 0; i < fr->view.count; i++) {
            matched += fi_matches(fr, i, queries[k]);
        }
        TEST_ASSERT_GREATER_THAN_UINT64(0, matched);
        TEST_ASSERT_EQUAL_UINT64(matched, fi_cell(fr, queries[k]).n);
    }
    TEST_ASSERT_EQUAL_UINT64(2, fi_cell(fr, (fi_query_t){.kind = FI_Q_INSTANCES}).sum);
}

/* Counters rebuilt from the records equal the recorded ones. */
static void test_recount_equals_the_counters(void) {
    const fi_frame_t *fr = capture(merge_body);
    assert_counters_match(fr);
    TEST_ASSERT_GREATER_THAN_UINT64(0, fr->summary.vertices.sum);
    TEST_ASSERT_GREATER_THAN_UINT64(0, fr->summary.indices.sum);
}

/* Uploads count issued calls with payload by call; a texture definition is sized by its levels. */
static void test_uploads_and_resource_bytes(void) {
    nt_gfx_event_t ev[3];
    memset(ev, 0, sizeof(ev));
    ev[0].kind = NT_GFX_EVENT_DEFINITION;
    ev[0].operation = NT_GFX_OP_CREATE;
    ev[0].object_kind = NT_GFX_OBJECT_TEXTURE;
    ev[0].object = 0x10001;
    ev[0].data.texture.slot = 1;
    ev[0].data.texture.width = 4;
    ev[0].data.texture.height = 4;
    ev[0].data.texture.format = NT_TEXTURE_FORMAT_RGBA8;
    ev[0].data.texture.levels = 3;
    ev[1].kind = NT_GFX_EVENT_OP;
    ev[1].operation = NT_GFX_OP_BUFFER_UPLOAD;
    ev[1].object_kind = NT_GFX_OBJECT_BUFFER;
    ev[1].result = NT_GFX_RESULT_ACCEPTED;
    ev[2].kind = NT_GFX_EVENT_BACKEND;
    ev[2].operation = NT_GFX_OP_STATE;
    ev[2].detail = NT_GFX_GL_glBufferSubData;
    ev[2].link = 2;
    ev[2].data.backend.bytes = 64;
    ev[1].object = 0x20001;
    nt_gfx_counters_t counters = {0};
    counters.accepted[NT_GFX_OP_BUFFER_UPLOAD] = 1;
    counters.gl[NT_GFX_GL_glBufferSubData] = 1;
    counters.buffer_upload_calls = 1;
    counters.buffer_upload_bytes = 64;
    const fi_frame_t *fr = load_events(&s_file, ev, 3, &counters);
    TEST_ASSERT_EQUAL_UINT64((4 * 4 + 2 * 2 + 1) * 4, fi_cell(fr, (fi_query_t){.kind = FI_Q_RESOURCE, .a = NT_GFX_OBJECT_TEXTURE, .b = FI_RES_LIVE}).sum);
    TEST_ASSERT_EQUAL_UINT64(64, fi_cell(fr, (fi_query_t){.kind = FI_Q_UPLOAD, .a = 0}).sum);
    TEST_ASSERT_EQUAL_UINT64(1, fi_cell(fr, (fi_query_t){.kind = FI_Q_OP_CALLS, .a = NT_GFX_OP_BUFFER_UPLOAD}).n);
    assert_counters_match(fr);
    TEST_ASSERT_EQUAL_UINT32(1, fr->buffer_upload_count);
    TEST_ASSERT_EQUAL_UINT32(0x20001, fr->buffer_upload[0].object);
    TEST_ASSERT_EQUAL_UINT64(64, fr->buffer_upload[0].bytes);
    TEST_ASSERT_EQUAL_UINT32(1, fi_alias(fr, NT_GFX_OBJECT_BUFFER, 0x20001).ordinal);
}

/* Only the four draw functions count as GL draw calls: glDrawBuffers selects attachments. */
static void test_draw_buffers_is_not_a_draw_call(void) {
    nt_gfx_event_t ev[2];
    memset(ev, 0, sizeof(ev));
    ev[0].kind = NT_GFX_EVENT_BACKEND;
    ev[0].operation = NT_GFX_OP_STATE;
    ev[0].detail = NT_GFX_GL_glDrawBuffers;
    ev[1] = ev[0];
    ev[1].detail = NT_GFX_GL_glDrawArrays;
    const fi_frame_t *fr = load_events(&s_file, ev, 2, NULL);
    TEST_ASSERT_EQUAL_UINT64(1, fi_cell(fr, (fi_query_t){.kind = FI_Q_DRAW_CALL}).n);
}
/* Real captures (web clears; a native skeletal_showcase frame with merged draws): every frame's counters
 * rebuild from its records and its merges follow the rule. */
static void test_real_captures_recount(void) {
    const char *const paths[] = {NT_CAPTURE_FIXTURE, NT_NATIVE_FIXTURE};
    for (uint32_t p = 0; p < 2; p++) {
        TEST_ASSERT_EQUAL(NT_OK, nt_fs_init());
        const bool opened = fi_file_open(&s_file, paths[p]);
        nt_fs_shutdown();
        TEST_ASSERT_TRUE(opened);
        for (uint32_t f = 0; f < s_file.frame_count; f++) {
            const fi_frame_t *fr = &s_file.frames[f];
            TEST_ASSERT_NULL(fr->error);
            TEST_ASSERT_EQUAL_UINT32(FI_NONE, fr->merge_mismatch);
            assert_counters_match(fr);
            TEST_ASSERT_GREATER_THAN_UINT64(0, fi_cell(fr, (fi_query_t){.kind = FI_Q_GL_CALL, .a = NT_GFX_GL_glClear}).n);
        }
        fi_file_close(&s_file);
    }
    TEST_ASSERT_EQUAL(NT_OK, nt_fs_init());
    TEST_ASSERT_TRUE(fi_file_open(&s_file, NT_NATIVE_FIXTURE));
    nt_fs_shutdown();
    TEST_ASSERT_GREATER_THAN_UINT64(0, fi_cell(&s_file.frames[0], (fi_query_t){.kind = FI_Q_MERGED}).n);
}
// #endregion

// #region state folds
static void request_body(void) {
    draw_setup();
    nt_gfx_bind_pipeline(s_pipeline); /* CACHE: already bound */
    nt_gfx_set_scissor(1, 2, 3, 4);
    nt_gfx_set_viewport(0, 0, 4, 4);
    nt_gfx_bind_pipeline((nt_pipeline_t){.id = 0x00FF00FFU}); /* invalid: the unbind */
    nt_gfx_end_pass();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_vertex_input(s_vi);
    nt_gfx_end_pass();
}

static void texture_set_body(void) {
    draw_setup();
    nt_gfx_set_scissor(1, 2, 3, 4);
    nt_gfx_set_scissor(5, 6, 7, 8);
    nt_gfx_set_scissor(1, 2, 3, 4);
    nt_gfx_end_pass();
}

/* Two scissor requests with one value are the same setting, another value is not; an inherited and a
 * requested value compare by value. */
static void test_settings_compare_by_value(void) {
    const fi_frame_t *fr = capture(texture_set_body);
    const uint32_t a = find_op(fr, NT_GFX_OP_SCISSOR, 0);
    const uint32_t b = find_op(fr, NT_GFX_OP_SCISSOR, 1);
    const uint32_t c = find_op(fr, NT_GFX_OP_SCISSOR, 2);
    TEST_ASSERT_TRUE(fi_same_setting(fr, a, c));
    TEST_ASSERT_FALSE(fi_same_setting(fr, a, b));
    TEST_ASSERT_FALSE(fi_same_setting(fr, a, FI_NONE));
}

/* A texture set compares by the textures in its ARGUMENT records, not by its count. */
static void test_texture_sets_compare_by_textures(void) {
    nt_gfx_event_t ev[4];
    memset(ev, 0, sizeof(ev));
    for (uint32_t k = 0; k < 4; k += 2) {
        ev[k].kind = NT_GFX_EVENT_OP;
        ev[k].operation = NT_GFX_OP_TEXTURE_SET;
        ev[k].object_kind = NT_GFX_OBJECT_PIPELINE;
        ev[k].object = 0x10001;
        ev[k].result = NT_GFX_RESULT_ACCEPTED;
        ev[k].data.texture_set.count = 1;
        ev[k + 1].kind = NT_GFX_EVENT_ARGUMENT;
        ev[k + 1].operation = NT_GFX_OP_TEXTURE_SET;
        ev[k + 1].object_kind = NT_GFX_OBJECT_TEXTURE;
        ev[k + 1].object = 0x20001 + k;
        ev[k + 1].link = k + 1U;
    }
    nt_gfx_counters_t counters = {0};
    counters.accepted[NT_GFX_OP_TEXTURE_SET] = 2;
    const fi_frame_t *fr = load_events(&s_file, ev, 4, &counters);
    TEST_ASSERT_FALSE(fi_same_setting(fr, 0, 2));
}

/* PASS resets the pass bindings, an invalid pipeline unbinds, the scissor carries over passes. */
static void test_request_state_follows_the_frontend_rules(void) {
    const fi_frame_t *fr = capture(request_body);
    fi_request_state_t st;
    const uint32_t viewport = find_op(fr, NT_GFX_OP_VIEWPORT, 0);
    TEST_ASSERT_EQUAL_UINT8(NT_GFX_RESULT_CACHE, fr->view.events[find_op(fr, NT_GFX_OP_PIPELINE, 1)].result);
    fi_request_state(fr, viewport, &st);
    TEST_ASSERT_EQUAL_UINT32(find_op(fr, NT_GFX_OP_PIPELINE, 0), st.pipeline);
    TEST_ASSERT_EQUAL_UINT32(viewport, st.viewport);
    TEST_ASSERT_EQUAL_UINT32(find_op(fr, NT_GFX_OP_SCISSOR, 0), st.scissor);
    fi_request_state(fr, find_op(fr, NT_GFX_OP_PIPELINE, 2), &st);
    TEST_ASSERT_EQUAL_UINT32(FI_NONE, st.pipeline);
    TEST_ASSERT_EQUAL_UINT32(find_op(fr, NT_GFX_OP_VERTEX_INPUT, 0), st.vertex_input);
    fi_request_state(fr, find_op(fr, NT_GFX_OP_VERTEX_INPUT, 1), &st);
    TEST_ASSERT_EQUAL_UINT32(find_op(fr, NT_GFX_OP_PASS, 1), st.pass);
    TEST_ASSERT_EQUAL_UINT32(FI_NONE, st.viewport);
    TEST_ASSERT_EQUAL_UINT32(find_op(fr, NT_GFX_OP_VERTEX_INPUT, 1), st.vertex_input);
    TEST_ASSERT_EQUAL_UINT32(find_op(fr, NT_GFX_OP_SCISSOR, 0), st.scissor);
}

static const fi_gl_slot_t *gl_slot(const fi_gl_state_t *st, uint32_t call, uint32_t k0, uint32_t k1) {
    for (uint32_t k = 0; k < st->count; k++) {
        if (st->slot[k].call == call && st->slot[k].key[0] == k0 && st->slot[k].key[1] == k1) {
            return &st->slot[k];
        }
    }
    return NULL;
}

/* An inherited cache entry and a bind call of the same name leave the slot alike. */
static void test_gl_slots_compare_by_name(void) {
    nt_gfx_event_t ev[3];
    memset(ev, 0, sizeof(ev));
    ev[0].kind = NT_GFX_EVENT_INITIAL;
    ev[0].operation = NT_GFX_OP_STATE;
    ev[0].detail = NT_GFX_INITIAL_BACKEND;
    ev[0].data.backend_state.program = 6;
    ev[0].data.backend_state.active_texture = 0x84C0;
    for (uint32_t k = 1; k < 3; k++) {
        ev[k].kind = NT_GFX_EVENT_BACKEND;
        ev[k].operation = NT_GFX_OP_STATE;
        ev[k].detail = NT_GFX_GL_glUseProgram;
        ev[k].arg_count = 1;
        ev[k].data.backend.args[0] = 5 + k; /* 6, then 7 */
    }
    nt_gfx_counters_t counters = {0};
    counters.gl[NT_GFX_GL_glUseProgram] = 2;
    const fi_frame_t *fr = load_events(&s_file, ev, 3, &counters);
    const fi_gl_slot_t slot = {.call = NT_GFX_GL_glUseProgram};
    TEST_ASSERT_TRUE(fi_gl_same(fr, &slot, 0, 1));
    TEST_ASSERT_FALSE(fi_gl_same(fr, &slot, 0, 2));
}

/* A texture bind lands on the active unit, inherited as a GL enum; a restored context starts over. */
static void test_gl_state_follows_issued_calls(void) {
    nt_gfx_event_t ev[6];
    memset(ev, 0, sizeof(ev));
    ev[0].kind = NT_GFX_EVENT_INITIAL;
    ev[0].operation = NT_GFX_OP_STATE;
    ev[0].detail = NT_GFX_INITIAL_BACKEND;
    ev[0].data.backend_state.active_texture = 0x84C2; /* GL_TEXTURE2 */
    for (uint32_t k = 1; k < 6; k++) {
        ev[k].kind = NT_GFX_EVENT_BACKEND;
        ev[k].operation = NT_GFX_OP_STATE;
    }
    ev[1].detail = NT_GFX_GL_glBindTexture;
    ev[1].arg_count = 2;
    ev[1].data.backend.args[0] = 0x0DE1;
    ev[1].data.backend.args[1] = 5;
    ev[2].detail = NT_GFX_GL_glActiveTexture;
    ev[2].arg_count = 1;
    ev[2].data.backend.args[0] = 0x84C1;
    ev[3] = ev[1];
    ev[3].data.backend.args[1] = 7;
    ev[4].kind = NT_GFX_EVENT_OP;
    ev[4].operation = NT_GFX_OP_CONTEXT;
    ev[4].result = NT_GFX_RESULT_ACCEPTED;
    ev[5].detail = NT_GFX_GL_glUseProgram;
    ev[5].arg_count = 1;
    ev[5].data.backend.args[0] = 9;
    nt_gfx_counters_t counters = {0};
    counters.gl[NT_GFX_GL_glBindTexture] = 2;
    counters.gl[NT_GFX_GL_glActiveTexture] = 1;
    counters.gl[NT_GFX_GL_glUseProgram] = 1;
    counters.accepted[NT_GFX_OP_CONTEXT] = 1;
    const fi_frame_t *fr = load_events(&s_file, ev, 6, &counters);
    fi_gl_state_t st;
    fi_gl_state(fr, 3, &st);
    TEST_ASSERT_EQUAL_UINT32(1, gl_slot(&st, NT_GFX_GL_glBindTexture, 2, 0x0DE1)->record);
    TEST_ASSERT_EQUAL_UINT32(3, gl_slot(&st, NT_GFX_GL_glBindTexture, 1, 0x0DE1)->record);
    TEST_ASSERT_TRUE(gl_slot(&st, NT_GFX_GL_glUseProgram, 0, 0)->inherited);
    fi_gl_state(fr, 5, &st);
    TEST_ASSERT_EQUAL_UINT32(1, st.count);
    TEST_ASSERT_EQUAL_UINT32(5, gl_slot(&st, NT_GFX_GL_glUseProgram, 0, 0)->record);
}
// #endregion

// #region compare
static bool s_split;     /* compare_body records a viewport between two draws that merge without it */
static uint32_t s_n = 3; /* compare_body's note field */

static void compare_body(void) {
    NT_GFX_NOTE("sprite", NT_NOTE_U32("n", s_n));
    draw_setup();
    nt_gfx_draw(0, 3);
    if (s_split) {
        nt_gfx_set_viewport(0, 0, 4, 4);
    }
    nt_gfx_draw(3, 3);
    nt_gfx_draw(6, 3);
    nt_gfx_end_pass();
}

/* The rows of the diff's only group with requests named `label`, as kind letters: = ~ - +. */
static void diff_letters(const fi_diff_t *d, const fi_frame_t *b, const char *label, char *out) {
    for (uint32_t g = 0; g < d->group_count; g++) {
        if (d->group[g].b != FI_NONE && strcmp(b->group_key[d->group[g].b], label) == 0) {
            for (uint32_t r = 0; r < d->group[g].count; r++) {
                out[r] = "=~-+"[d->row[d->group[g].first + r].kind];
            }
            out[d->group[g].count] = '\0';
            return;
        }
    }
    TEST_FAIL_MESSAGE("group not compared");
}

/* A frame against itself differs nowhere; a viewport recorded between two draws is only in B, and the draw
 * after it changes: it no longer merges, so it ends ACCEPTED and names the break. */
static void test_compare_aligns_requests_and_names_changes(void) {
    s_split = false;
    size_t len = 0;
    char *text = record(compare_body, &len);
    const fi_frame_t *a = load(&s_file, text, len);
    fi_diff_t d;
    fi_diff(a, a, &d);
    for (uint32_t r = 0; r < d.row_count; r++) {
        TEST_ASSERT_EQUAL_UINT8(FI_DIFF_SAME, d.row[r].kind);
    }
    for (uint32_t g = 0; g < d.group_count; g++) {
        TEST_ASSERT_EQUAL_UINT32(d.group[g].a, d.group[g].b);
        TEST_ASSERT_TRUE(d.group[g].aligned);
    }
    fi_diff_free(&d);

    s_split = true;
    fi_file_t split = {0};
    text = record(compare_body, &len);
    const fi_frame_t *b = load(&split, text, len);
    fi_diff(a, b, &d);
    char letters[64];
    diff_letters(&d, b, "> sprite", letters);
    TEST_ASSERT_EQUAL_STRING("=====+~==", letters); /* note, pass, pipeline, vertex input, draw, viewport, draw, draw, end pass */
    const uint32_t viewport = find_op(b, NT_GFX_OP_VIEWPORT, 0);
    const uint32_t moved = find_op(b, NT_GFX_OP_DRAW, 1);
    for (uint32_t r = 0; r < d.row_count; r++) {
        if (d.row[r].kind == FI_DIFF_ONLY_B) {
            TEST_ASSERT_EQUAL_UINT32(viewport, d.row[r].b);
        }
        if (d.row[r].kind == FI_DIFF_CHANGED) {
            TEST_ASSERT_EQUAL_UINT32(moved, d.row[r].b);
            TEST_ASSERT_EQUAL_UINT32(find_op(a, NT_GFX_OP_DRAW, 1), d.row[r].a);
            TEST_ASSERT_EQUAL_UINT8(FI_CHANGE_RESULT | FI_CHANGE_MERGE, d.row[r].change); /* the fake backend issues no GL calls */
        }
    }
    fi_diff_free(&d);
    fi_file_close(&split);

    s_split = false; /* a note field of another value changes the note */
    s_n = 4;
    text = record(compare_body, &len);
    b = load(&split, text, len);
    s_n = 3;
    fi_diff(a, b, &d);
    diff_letters(&d, b, "> sprite", letters);
    TEST_ASSERT_EQUAL_STRING("~=======", letters);
    for (uint32_t r = 0; r < d.row_count; r++) {
        TEST_ASSERT_EQUAL_UINT8(d.row[r].b == find_kind(b, NT_GFX_EVENT_NOTE, 0) ? FI_CHANGE_ITEMS : 0, d.row[r].change);
    }
    fi_diff_free(&d);
    fi_file_close(&split);
}

/* A pipeline, a GL call A's issues and B's does not, two texture sets, then a pass, a vertex input, a draw
 * and a create, each naming handles in its arguments. The second texture set names texture `second` of the
 * two the frame uses, as its alias. */
static uint32_t handles_body(nt_gfx_event_t *ev, uint32_t base, bool call, uint32_t second) {
    const uint32_t pipeline = base + 1U;
    const uint32_t texture[2] = {base + 2U, base + 3U};
    const uint32_t vi = base + 4U;
    uint32_t n = 0;
    ev[n++] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_OP, .operation = NT_GFX_OP_PIPELINE, .object_kind = NT_GFX_OBJECT_PIPELINE, .object = pipeline, .result = NT_GFX_RESULT_ACCEPTED};
    if (call) {
        ev[n++] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_BACKEND, .operation = NT_GFX_OP_PIPELINE, .detail = NT_GFX_GL_glUseProgram, .link = 1};
    }
    for (uint32_t k = 0; k < 2U; k++) {
        const uint32_t op = n;
        ev[n++] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_OP, .operation = NT_GFX_OP_TEXTURE_SET, .object_kind = NT_GFX_OBJECT_PIPELINE, .object = pipeline, .result = NT_GFX_RESULT_ACCEPTED};
        ev[op].data.texture_set.count = 1;
        ev[n++] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_ARGUMENT, .operation = NT_GFX_OP_TEXTURE_SET, .object_kind = NT_GFX_OBJECT_TEXTURE, .object = texture[k == 0 ? 0 : second], .link = op + 1U};
        ev[n - 1U].data.texture_binding.sampler = base + 5U;
    }
    ev[n] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_OP, .operation = NT_GFX_OP_PASS, .result = NT_GFX_RESULT_ACCEPTED};
    ev[n++].data.pass.target = base + 6U;
    ev[n] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_OP, .operation = NT_GFX_OP_VERTEX_INPUT, .object_kind = NT_GFX_OBJECT_VERTEX_INPUT, .object = vi, .result = NT_GFX_RESULT_ACCEPTED};
    ev[n].data.instance_binding.buffer = base + 7U;
    ev[n++].data.instance_binding.layout = base + 8U;
    ev[n] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_OP, .operation = NT_GFX_OP_DRAW, .object_kind = NT_GFX_OBJECT_PIPELINE, .object = pipeline, .result = NT_GFX_RESULT_ACCEPTED};
    ev[n].data.draw.count = 3;
    ev[n++].data.draw.vertex_input = vi;
    ev[n] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_OP, .operation = NT_GFX_OP_CREATE, .object_kind = NT_GFX_OBJECT_PIPELINE, .object = base + 9U, .result = NT_GFX_RESULT_ACCEPTED};
    ev[n].data.pipeline.slot = base;
    ev[n++].data.pipeline.program = base + 10U;
    return n;
}

/* Requests pair and match by alias, whatever their handles: B's handles all differ from A's. A GL call
 * fewer and a texture set naming the other texture are the changes. */
static void test_compare_pairs_handles_by_alias(void) {
    nt_gfx_event_t ev[2][12];
    const uint32_t na = handles_body(ev[0], 10, true, 1);
    const fi_frame_t *a = load_events(&s_file, ev[0], na, NULL);
    fi_file_t other = {0};
    const fi_frame_t *b = load_events(&other, ev[1], handles_body(ev[1], 20, false, 1), NULL);
    fi_diff_t d;
    fi_diff(a, b, &d);
    TEST_ASSERT_EQUAL_UINT32(1, d.group_count);
    char letters[16];
    diff_letters(&d, b, b->group_key[d.group[0].b], letters);
    TEST_ASSERT_EQUAL_STRING("~======", letters); /* the pipeline, two texture sets, pass, vertex input, draw, create */
    TEST_ASSERT_EQUAL_UINT8(FI_CHANGE_CALLS, d.row[0].change);
    TEST_ASSERT_EQUAL_UINT32(1, d.row[0].calls_a);
    TEST_ASSERT_EQUAL_UINT32(0, d.row[0].calls_b);
    fi_diff_free(&d);
    fi_file_close(&other);

    b = load_events(&other, ev[1], handles_body(ev[1], 20, true, 0), NULL);
    fi_diff(a, b, &d);
    diff_letters(&d, b, b->group_key[d.group[0].b], letters);
    TEST_ASSERT_EQUAL_STRING("==~====", letters);
    TEST_ASSERT_EQUAL_UINT8(FI_CHANGE_ITEMS, d.row[2].change);
    fi_diff_free(&d);
    fi_file_close(&other);
}

/* A frame that defines a render target and two textures under handles from base, then a note naming one of the
 * textures (`page`) and, on native, a buffer read of the render target or the default framebuffer. */
static const fi_frame_t *read_and_note_frame(fi_file_t *file, nt_gfx_event_t *ev, uint32_t base, uint32_t page, bool screen) {
    uint32_t n = 0;
    memset(ev, 0, 6 * sizeof(*ev));
    ev[n++] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_DEFINITION, .operation = NT_GFX_OP_CREATE, .object_kind = NT_GFX_OBJECT_RENDER_TARGET, .object = base};
    for (uint32_t k = 0; k < 2U; k++) { /* labeled: an unlabeled object is named by its first use, so it pairs by role */
        ev[n] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_DEFINITION, .operation = NT_GFX_OP_CREATE, .object_kind = NT_GFX_OBJECT_TEXTURE, .object = base + 1U + k};
        ev[n++].data.texture.label = 6U + (2U * k);
    }
    ev[n].note.kind = NT_GFX_EVENT_NOTE;
    ev[n++].note.label = 1;
    ev[n].field.kind = NT_GFX_EVENT_NOTE_FIELD;
    ev[n].field.type = NT_GFX_NOTE_HANDLE;
    ev[n].field.key = 1;
    ev[n].field.value.handle.kind = NT_GFX_OBJECT_TEXTURE;
    ev[n++].field.value.handle.id = base + 1U + page;
#if !defined(NT_PLATFORM_WEB)
    ev[n] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_OP, .operation = NT_GFX_OP_BUFFER_READ, .object_kind = NT_GFX_OBJECT_BUFFER, .object = base + 3U, .result = NT_GFX_RESULT_ACCEPTED};
    ev[n].data.rect.width = 4;
    ev[n].data.rect.height = 4;
    ev[n++].data.rect.source = screen ? 0U : base;
#endif
    nt_gfx_capture_view_t view = {.events = ev, .count = n, .strings = "\0page\0a\0b", .string_bytes = 10};
    const size_t len = nt_gfx_capture_format(&view, NULL, 0);
    char *text = (char *)malloc(len + 1);
    TEST_ASSERT_NOT_NULL(text);
    nt_gfx_capture_format(&view, text, len + 1);
    return load(file, text, len);
}

/* A note's texture handle and a buffer read's render target compare by alias: two runs under other handles are
 * the same, another texture or the default framebuffer is a change. */
static void test_compare_names_note_handles_and_read_sources(void) {
    nt_gfx_event_t ev[2][6];
    const fi_frame_t *a = read_and_note_frame(&s_file, ev[0], 0x10001U, 0, false);
    char row[256];
    fi_row_text(a, 4, row, sizeof(row));
    TEST_ASSERT_EQUAL_STRING("NOTE_FIELD page=tex/a", row);
    fi_file_t other = {0};
    const struct {
        uint32_t page;
        bool screen;
        const char *letters;
    } k_cases[] = {
        {0, false, "=="},
        {1, false, "~="},
#if !defined(NT_PLATFORM_WEB)
        {0, true, "=~"},
#endif
    };
    for (uint32_t k = 0; k < sizeof(k_cases) / sizeof(k_cases[0]); k++) {
        const fi_frame_t *b = read_and_note_frame(&other, ev[1], 0x20005U, k_cases[k].page, k_cases[k].screen);
        fi_diff_t d;
        fi_diff(a, b, &d);
        char letters[16];
        diff_letters(&d, b, b->group_key[d.group[0].b], letters);
        TEST_ASSERT_EQUAL_STRING(k_cases[k].letters, letters);
        fi_diff_free(&d);
        fi_file_close(&other);
    }
#if !defined(NT_PLATFORM_WEB)
    fi_row_text(a, 5, row, sizeof(row));
    TEST_ASSERT_EQUAL_STRING("BUFFER_READ buf#1 width=4 height=4 source=rt#1", row);
    const fi_frame_t *b = read_and_note_frame(&other, ev[1], 0x20005U, 0, true);
    fi_row_text(b, 5, row, sizeof(row));
    TEST_ASSERT_EQUAL_STRING("BUFFER_READ buf#1 width=4 height=4 source=default framebuffer", row);
    fi_file_close(&other);
#endif
}

/* A record of a nested operation (a default sampler made inside a texture create, its GL call) has the engine
 * state of the top-level operation. */
static void test_nested_records_take_the_top_request_state(void) {
    nt_gfx_event_t ev[5];
    memset(ev, 0, sizeof(ev));
    ev[0] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_OP, .operation = NT_GFX_OP_VIEWPORT, .result = NT_GFX_RESULT_ACCEPTED};
    ev[0].data.rect.width = 4;
    ev[1] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_OP, .operation = NT_GFX_OP_CREATE, .object_kind = NT_GFX_OBJECT_TEXTURE, .object = 0x10001, .result = NT_GFX_RESULT_ACCEPTED};
    ev[2] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_OP, .operation = NT_GFX_OP_CREATE, .object_kind = NT_GFX_OBJECT_SAMPLER, .object = 1, .result = NT_GFX_RESULT_ACCEPTED, .link = 2};
    ev[3] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_BACKEND, .operation = NT_GFX_OP_STATE, .detail = NT_GFX_GL_glGenSamplers, .link = 3, .arg_count = 2};
    ev[3].data.backend.args[0] = 1;
    ev[3].data.backend.args[1] = 7;
    ev[4] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_OP, .operation = NT_GFX_OP_VIEWPORT, .result = NT_GFX_RESULT_ACCEPTED};
    ev[4].data.rect.width = 8;
    nt_gfx_counters_t counters = {0};
    counters.gl[NT_GFX_GL_glGenSamplers] = 1;
    counters.accepted[NT_GFX_OP_VIEWPORT] = 2;
    counters.accepted[NT_GFX_OP_CREATE] = 2;
    const fi_frame_t *fr = load_events(&s_file, ev, 5, &counters);
    static fi_state_view_t top;
    static fi_state_view_t call;
    fi_state_at(fr, 1, &top);
    fi_state_at(fr, 3, &call);
    TEST_ASSERT_EQUAL_UINT32(0, top.req.viewport);
    TEST_ASSERT_EQUAL_UINT32(0, call.req.viewport); /* not the viewport the frame sets after the create */
    TEST_ASSERT_EQUAL_UINT32(3, call.gl_at);        /* a GL call is its own GL point */
    static fi_state_view_t nested;
    fi_state_at(fr, 2, &nested);
    TEST_ASSERT_EQUAL_UINT32(3, nested.gl_at); /* a nested operation's own GL point, not its parent's */
    fi_file_close(&s_file);
}

/* A note's handle counts as a use: the texture it names first is #1 of the unlabeled textures. */
static void test_note_handles_number_their_objects(void) {
    nt_gfx_event_t ev[3];
    memset(ev, 0, sizeof(ev));
    ev[0].note.kind = NT_GFX_EVENT_NOTE;
    ev[1].field.kind = NT_GFX_EVENT_NOTE_FIELD;
    ev[1].field.type = NT_GFX_NOTE_HANDLE;
    ev[1].field.value.handle.kind = NT_GFX_OBJECT_TEXTURE;
    ev[1].field.value.handle.id = 0x10002;
    ev[2] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_ARGUMENT, .operation = NT_GFX_OP_TEXTURE_SET, .object_kind = NT_GFX_OBJECT_TEXTURE, .object = 0x10001};
    nt_gfx_capture_view_t view = {.events = ev, .count = 3, .strings = "", .string_bytes = 1};
    const size_t len = nt_gfx_capture_format(&view, NULL, 0);
    char *text = (char *)malloc(len + 1);
    TEST_ASSERT_NOT_NULL(text);
    nt_gfx_capture_format(&view, text, len + 1);
    const fi_frame_t *fr = load(&s_file, text, len);
    TEST_ASSERT_EQUAL_UINT32(1, fi_alias(fr, NT_GFX_OBJECT_TEXTURE, 0x10002).ordinal);
    TEST_ASSERT_EQUAL_UINT32(2, fi_alias(fr, NT_GFX_OBJECT_TEXTURE, 0x10001).ordinal);
    fi_file_close(&s_file);
}

/* Row text keeps apart what the records keep apart: two floats one ulp apart, a quoted string's backslash and
 * quote, and a label that holds '#' next to an alias number. */
static void test_rows_keep_values_apart(void) {
    nt_gfx_event_t ev[6];
    memset(ev, 0, sizeof(ev));
    ev[0] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_DEFINITION, .operation = NT_GFX_OP_CREATE, .object_kind = NT_GFX_OBJECT_BUFFER, .object = 0x10001};
    ev[0].data.buffer.label = 1; /* "mesh#1": one buffer, so its alias has no number of its own */
    ev[1] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_OP, .operation = NT_GFX_OP_PASS, .result = NT_GFX_RESULT_ACCEPTED};
    const uint32_t next_after_half = 0x3F000001U; /* the float32 one ulp above 0.5 */
    memcpy(&ev[1].data.pass.depth, &next_after_half, sizeof(float));
    ev[2].note.kind = NT_GFX_EVENT_NOTE;
    ev[3].field.kind = NT_GFX_EVENT_NOTE_FIELD;
    ev[3].field.type = NT_GFX_NOTE_STR;
    ev[3].field.key = 8;
    ev[3].field.value.str = 10;
    ev[4] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_OP, .operation = NT_GFX_OP_UBO, .object_kind = NT_GFX_OBJECT_BUFFER, .object = 0x10001, .result = NT_GFX_RESULT_ACCEPTED};
    ev[5] = ev[1];
    const uint32_t quiet_nan = 0x7FC00001U; /* one NaN payload of many */
    memcpy(&ev[5].data.pass.depth, &quiet_nan, sizeof(float));
    static const char k_strings[] = "\0mesh#1\0v\0a\\b\"";
    nt_gfx_capture_view_t view = {.events = ev, .count = 6, .strings = k_strings, .string_bytes = sizeof(k_strings)};
    const size_t len = nt_gfx_capture_format(&view, NULL, 0);
    char *text = (char *)malloc(len + 1);
    TEST_ASSERT_NOT_NULL(text);
    nt_gfx_capture_format(&view, text, len + 1);
    const fi_frame_t *fr = load(&s_file, text, len);
    char row[256];
    fi_row_text(fr, 1, row, sizeof(row));
    TEST_ASSERT_EQUAL_STRING("PASS target=default framebuffer depth=0.50000006", row);
    fi_row_text(fr, 3, row, sizeof(row));
    TEST_ASSERT_EQUAL_STRING("NOTE_FIELD v=\"a\\x5cb\\x22\"", row);
    fi_row_text(fr, 4, row, sizeof(row));
    TEST_ASSERT_EQUAL_STRING("UBO buf/mesh\\x231", row);
    fi_row_text(fr, 5, row, sizeof(row));
    TEST_ASSERT_EQUAL_STRING("PASS target=default framebuffer depth=nan:0x7fc00001", row);
    fi_file_close(&s_file);
}

static uint16_t s_offset; /* layout_body's attribute offset */

static void layout_body(void) { /* destroyed again, so the next frame's snapshot is this one's */
    const nt_buffer_t vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .size = 48});
    nt_gfx_destroy_vertex_input(nt_gfx_make_vertex_input(
        &(nt_vertex_input_desc_t){.layout = {.attr_count = 1, .stride = 16, .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 3, .offset = s_offset}}}, .vertex_buffer = vbo}));
    nt_gfx_destroy_buffer(vbo);
}

/* A buffer and a vertex input created in each frame, under other handles: equal layouts pair as same, an
 * attribute at another offset changes the vertex input's create through its DEFINITION records. */
static void test_compare_checks_what_a_create_defines(void) {
    size_t len = 0;
    char *text = record(layout_body, &len);
    const fi_frame_t *a = load(&s_file, text, len);
    fi_file_t other = {0};
    for (uint16_t offset = 0; offset <= 4U; offset += 4U) {
        s_offset = offset;
        text = record(layout_body, &len);
        const fi_frame_t *b = load(&other, text, len);
        TEST_ASSERT_NOT_EQUAL_UINT32(a->view.events[find_op(a, NT_GFX_OP_CREATE, 0)].object, b->view.events[find_op(b, NT_GFX_OP_CREATE, 0)].object);
        fi_diff_t d;
        fi_diff(a, b, &d);
        char letters[8];
        diff_letters(&d, b, b->group_key[d.group[0].b], letters);
        TEST_ASSERT_EQUAL_STRING(offset == 0 ? "=====" : "==~==", letters); /* timer, two creates, two destroys */
        TEST_ASSERT_EQUAL_UINT8(offset == 0 ? 0 : FI_CHANGE_ITEMS, d.row[2].change);
        fi_diff_free(&d);
        fi_file_close(&other);
    }
}

static bool s_extra; /* labeled_body creates a buffer labeled "extra" before "shared" */

static void labeled_body(void) {
    const nt_buffer_t extra = s_extra ? nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .size = 16, .label = "extra"}) : (nt_buffer_t){0};
    nt_gfx_destroy_buffer(nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .size = 48, .label = "shared"}));
    if (s_extra) {
        nt_gfx_destroy_buffer(extra);
    }
}

/* B's snapshot holds one more live buffer of the same label that B never uses: numbered after the frame's work,
 * it shifts nothing, so the frames' requests are equal and pair. */
static void test_compare_ignores_an_unused_live_object(void) {
    s_extra = false;
    size_t len = 0;
    char *text = record(labeled_body, &len);
    const fi_frame_t *a = load(&s_file, text, len);
    const nt_buffer_t unused = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .size = 16, .label = "shared"});
    text = record(labeled_body, &len);
    nt_gfx_destroy_buffer(unused);
    fi_file_t other = {0};
    const fi_frame_t *b = load(&other, text, len);
    fi_diff_t d;
    fi_diff(a, b, &d);
    char letters[8];
    diff_letters(&d, b, b->group_key[d.group[0].b], letters);
    TEST_ASSERT_EQUAL_STRING("===", letters); /* timer, create, destroy */
    fi_diff_free(&d);
    fi_file_close(&other);
}

/* B creates and uses a buffer of another label before the shared one: only that buffer is new, and the
 * shared buffer still pairs. */
static void test_compare_pairs_objects_by_label(void) {
    s_extra = false;
    size_t len = 0;
    char *text = record(labeled_body, &len);
    const fi_frame_t *a = load(&s_file, text, len);
    s_extra = true;
    text = record(labeled_body, &len);
    s_extra = false;
    fi_file_t other = {0};
    const fi_frame_t *b = load(&other, text, len);
    fi_diff_t d;
    fi_diff(a, b, &d);
    char letters[8];
    diff_letters(&d, b, b->group_key[d.group[0].b], letters);
    TEST_ASSERT_EQUAL_STRING("=+==+", letters); /* timer, create extra, create shared, destroy shared, destroy extra */
    fi_diff_free(&d);
    fi_file_close(&other);
}

static const char *s_labels[2] = {"mesh", "mesh"}; /* pair_body's buffers */
static uint32_t s_second_size = 48;

static void pair_body(void) {
    const nt_buffer_t first = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .size = 16, .label = s_labels[0]});
    const nt_buffer_t second = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .size = s_second_size, .label = s_labels[1]});
    nt_gfx_destroy_buffer(second);
    nt_gfx_destroy_buffer(first);
}

/* The rows of pair_body in A (default labels and size) against B recorded with `labels` and `second_size`. */
static void pair_letters(const char *labels[2], uint32_t second_size, char *letters) {
    size_t len = 0;
    char *text = record(pair_body, &len);
    const fi_frame_t *a = load(&s_file, text, len);
    s_labels[0] = labels[0];
    s_labels[1] = labels[1];
    s_second_size = second_size;
    text = record(pair_body, &len);
    s_labels[0] = "mesh";
    s_labels[1] = "mesh";
    s_second_size = 48;
    fi_file_t other = {0};
    const fi_frame_t *b = load(&other, text, len);
    fi_diff_t d;
    fi_diff(a, b, &d);
    diff_letters(&d, b, b->group_key[d.group[0].b], letters);
    fi_diff_free(&d);
    fi_file_close(&other);
    fi_file_close(&s_file);
}

/* Objects of one label pair by their number within it: a changed second "mesh" is that object changed. A
 * renamed object is another object: nothing of it pairs. The alias shows kind, label and number. */
static void test_compare_pairs_one_label_by_number(void) {
    char letters[8];
    pair_letters((const char *[2]){"mesh", "mesh"}, 64, letters);
    TEST_ASSERT_EQUAL_STRING("==~==", letters); /* timer, create, create, destroy, destroy */

    pair_letters((const char *[2]){"mesh", "renamed"}, 48, letters);
    TEST_ASSERT_NULL(strchr(letters, '~'));
    uint32_t same = 0;
    for (const char *c = letters; *c != '\0'; c++) {
        same += *c == '=';
    }
    TEST_ASSERT_EQUAL_UINT32(3, same); /* timer, the first mesh's create and destroy */

    size_t len = 0;
    char *text = record(pair_body, &len);
    const fi_frame_t *fr = load(&s_file, text, len);
    const fi_alias_t second = fi_alias(fr, NT_GFX_OBJECT_BUFFER, fr->view.events[find_op(fr, NT_GFX_OP_CREATE, 1)].object);
    TEST_ASSERT_EQUAL_STRING("mesh", second.label);
    TEST_ASSERT_EQUAL_UINT32(2, second.ordinal);
}

/* A request may name handles the frame never defines (a hand-made or cut capture): each still gets an alias. */
static void test_handles_without_definitions_get_aliases(void) {
    nt_gfx_event_t ev = {.kind = NT_GFX_EVENT_OP, .operation = NT_GFX_OP_VERTEX_INPUT, .object_kind = NT_GFX_OBJECT_VERTEX_INPUT, .object = 7, .result = NT_GFX_RESULT_ACCEPTED};
    ev.data.instance_binding.buffer = 8;
    ev.data.instance_binding.layout = 9;
    const fi_frame_t *fr = load_events(&s_file, &ev, 1, NULL);
    TEST_ASSERT_EQUAL_UINT32(1, fi_alias(fr, NT_GFX_OBJECT_VERTEX_INPUT, 7).ordinal);
    TEST_ASSERT_EQUAL_UINT32(1, fi_alias(fr, NT_GFX_OBJECT_BUFFER, 8).ordinal);
    TEST_ASSERT_EQUAL_UINT32(1, fi_alias(fr, NT_GFX_OBJECT_INSTANCE_LAYOUT, 9).ordinal);
    fi_file_close(&s_file);

    nt_gfx_event_t texture = {.kind = NT_GFX_EVENT_DEFINITION, .operation = NT_GFX_OP_CREATE, .object_kind = NT_GFX_OBJECT_TEXTURE, .object = 7};
    texture.data.texture.sampler = 3;
    fr = load_events(&s_file, &texture, 1, NULL);
    TEST_ASSERT_EQUAL_UINT32(1, fi_alias(fr, NT_GFX_OBJECT_SAMPLER, 3).ordinal);
}

/* Matches inside the changed middle: A's VIEWPORTs (v) and SCISSORs (s) "svvs" against B's "vvsv" keep the
 * one longest common run "vvs", in order. */
static void test_compare_keeps_the_longest_common_run(void) {
    nt_gfx_event_t ev[8];
    const char *ops = "svvsvvsv";
    for (uint32_t i = 0; i < 8U; i++) {
        ev[i] = (nt_gfx_event_t){.kind = NT_GFX_EVENT_OP, .operation = ops[i] == 'v' ? NT_GFX_OP_VIEWPORT : NT_GFX_OP_SCISSOR, .result = NT_GFX_RESULT_ACCEPTED};
    }
    const fi_frame_t *a = load_events(&s_file, ev, 4, NULL);
    fi_file_t other = {0};
    const fi_frame_t *b = load_events(&other, ev + 4, 4, NULL);
    fi_diff_t d;
    fi_diff(a, b, &d);
    char letters[8];
    diff_letters(&d, b, b->group_key[d.group[0].b], letters);
    TEST_ASSERT_EQUAL_STRING("-===+", letters);
    TEST_ASSERT_EQUAL_UINT32(0, d.row[0].a);
    TEST_ASSERT_EQUAL_UINT32(3, d.row[4].b);
    fi_diff_free(&d);
    fi_file_close(&other);
}

/* One group of `viewports` VIEWPORT requests in A against B's `same` VIEWPORTs then `scissors` SCISSORs. */
static void diff_runs(uint32_t viewports, uint32_t same, uint32_t scissors, fi_file_t files[2], fi_diff_t *d) {
    const uint32_t total = viewports + same + scissors;
    nt_gfx_event_t *ev = (nt_gfx_event_t *)calloc(total, sizeof(nt_gfx_event_t));
    TEST_ASSERT_NOT_NULL(ev);
    for (uint32_t i = 0; i < total; i++) {
        ev[i].kind = NT_GFX_EVENT_OP;
        ev[i].operation = i < viewports + same ? NT_GFX_OP_VIEWPORT : NT_GFX_OP_SCISSOR;
        ev[i].result = NT_GFX_RESULT_ACCEPTED;
    }
    const fi_frame_t *a = load_events(&files[0], ev, viewports, NULL);
    const fi_frame_t *b = load_events(&files[1], ev + viewports, same + scissors, NULL);
    fi_diff(a, b, d);
    free(ev);
    TEST_ASSERT_EQUAL_UINT32(1, d->group_count);
    TEST_ASSERT_EQUAL_UINT32((viewports > same ? viewports : same) + scissors, d->group[0].count);
}

/* FI_DIFF_EDITS edits still align; one more lists A's requests, then B's, never paired. Requests only one
 * side adds need no search, however many. */
static void test_compare_aligns_up_to_its_edit_limit(void) {
    static fi_file_t files[2];
    fi_diff_t d;
    const uint32_t half = FI_DIFF_EDITS / 2U;
    diff_runs(half, 0, half, files, &d);
    TEST_ASSERT_TRUE(d.group[0].aligned);
    TEST_ASSERT_EQUAL_UINT32(FI_DIFF_EDITS, d.group[0].differ);
    fi_diff_free(&d);
    fi_file_close(&files[0]);
    fi_file_close(&files[1]);

    diff_runs(half + 1U, 0, half, files, &d);
    TEST_ASSERT_FALSE(d.group[0].aligned);
    TEST_ASSERT_EQUAL_UINT8(FI_DIFF_ONLY_A, d.row[0].kind);
    TEST_ASSERT_EQUAL_UINT8(FI_DIFF_ONLY_B, d.row[half + 1U].kind);
    fi_diff_free(&d);
    fi_file_close(&files[0]);
    fi_file_close(&files[1]);

    diff_runs(FI_DIFF_EDITS + 8U, FI_DIFF_EDITS + 8U, FI_DIFF_EDITS + 8U, files, &d);
    TEST_ASSERT_TRUE(d.group[0].aligned);
    TEST_ASSERT_EQUAL_UINT32(FI_DIFF_EDITS + 8U, d.group[0].differ);
    TEST_ASSERT_EQUAL_UINT8(FI_DIFF_SAME, d.row[0].kind);
    TEST_ASSERT_EQUAL_UINT8(FI_DIFF_ONLY_B, d.row[d.group[0].count - 1U].kind);
    fi_diff_free(&d);
    fi_file_close(&files[0]);
    fi_file_close(&files[1]);
}
// #endregion

// #region damaged files
/* A broken record keeps the frame's prefix; a broken header rejects only its own document. */
static void test_damage_stays_inside_its_document(void) {
    size_t len = 0;
    char *one = record(merge_body, &len);
    char *text = (char *)malloc((len * 2) + 1);
    TEST_ASSERT_NOT_NULL(text);
    memcpy(text, one, len);
    memcpy(text + len, one, len);
    free(one);
    char *second = text + len;
    strstr(second, "op=VIEWPORT")[3] = 'X';
    fi_file_load(&s_file, "test", text, len * 2);
    TEST_ASSERT_EQUAL_UINT32(2, s_file.frame_count);
    TEST_ASSERT_NULL(s_file.frames[0].error);
    TEST_ASSERT_NOT_NULL(s_file.frames[1].error);
    TEST_ASSERT_NOT_NULL(s_file.frames[1].view.events);
    TEST_ASSERT_LESS_THAN_UINT32(s_file.frames[0].view.count, s_file.frames[1].view.count);
    TEST_ASSERT_GREATER_THAN_UINT32(1, s_file.frames[1].error_line);

    fi_file_close(&s_file);
    one = record(merge_body, &len);
    text = (char *)malloc((len * 2) + 1);
    TEST_ASSERT_NOT_NULL(text);
    memcpy(text, one, len);
    memcpy(text + len, one, len);
    free(one);
    text[10] = '9'; /* "ntcapture 4" becomes an unknown version */
    fi_file_load(&s_file, "test", text, len * 2);
    TEST_ASSERT_EQUAL_UINT32(2, s_file.frame_count);
    TEST_ASSERT_NULL(s_file.frames[0].view.events);
    TEST_ASSERT_NULL(s_file.frames[1].error);
}

static void overflow_body(void) {
    draw_setup();
    for (uint32_t i = 0; i < 600; i++) {
        nt_gfx_draw(0, 3);
    }
    nt_gfx_end_pass();
}

/* An overflowed frame opens with its records; its counters stay complete, so recount falls short. */
static void test_an_overflowed_frame_opens(void) {
    const fi_frame_t *fr = capture(overflow_body);
    TEST_ASSERT_TRUE(fr->view.overflow);
    TEST_ASSERT_GREATER_THAN_UINT32(0, fr->row_count);
    fi_counter_row_t rows[FI_COUNTER_ROWS];
    (void)fi_counter_rows(fr, rows);
    TEST_ASSERT_EQUAL_STRING("vertices", rows[0].name);
    TEST_ASSERT_LESS_THAN_UINT64(rows[0].counter, rows[0].recount);
}
// #endregion

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_notes_cover_the_operations_after_them);
    RUN_TEST(test_a_marker_and_a_note_of_one_label_differ);
    RUN_TEST(test_a_finished_link_is_not_the_snapshot);
    RUN_TEST(test_a_context_restore_belongs_to_begin_frame);
    RUN_TEST(test_phases_follow_the_frame);
    RUN_TEST(test_rows_are_a_preorder_of_the_tree);
    RUN_TEST(test_merge_rule_matches_the_frontend);
    RUN_TEST(test_a_wrong_merge_marks_the_frame);
    RUN_TEST(test_each_number_counts_the_records_its_query_selects);
    RUN_TEST(test_recount_equals_the_counters);
    RUN_TEST(test_uploads_and_resource_bytes);
    RUN_TEST(test_draw_buffers_is_not_a_draw_call);
    RUN_TEST(test_real_captures_recount);
    RUN_TEST(test_request_state_follows_the_frontend_rules);
    RUN_TEST(test_settings_compare_by_value);
    RUN_TEST(test_texture_sets_compare_by_textures);
    RUN_TEST(test_gl_slots_compare_by_name);
    RUN_TEST(test_gl_state_follows_issued_calls);
    RUN_TEST(test_damage_stays_inside_its_document);
    RUN_TEST(test_a_group_key_is_one_line);
    RUN_TEST(test_an_overflowed_frame_opens);
    RUN_TEST(test_compare_aligns_requests_and_names_changes);
    RUN_TEST(test_compare_pairs_handles_by_alias);
    RUN_TEST(test_compare_names_note_handles_and_read_sources);
    RUN_TEST(test_nested_records_take_the_top_request_state);
    RUN_TEST(test_note_handles_number_their_objects);
    RUN_TEST(test_rows_keep_values_apart);
    RUN_TEST(test_compare_checks_what_a_create_defines);
    RUN_TEST(test_compare_ignores_an_unused_live_object);
    RUN_TEST(test_compare_pairs_objects_by_label);
    RUN_TEST(test_compare_pairs_one_label_by_number);
    RUN_TEST(test_handles_without_definitions_get_aliases);
    RUN_TEST(test_compare_keeps_the_longest_common_run);
    RUN_TEST(test_compare_aligns_up_to_its_edit_limit);
    return UNITY_END();
}
