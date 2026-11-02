#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fi_model.h"
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

static const fi_frame_t *load(char *text, size_t len) {
    fi_file_load(&s_file, "test", text, len);
    TEST_ASSERT_EQUAL_UINT32(1, s_file.frame_count);
    TEST_ASSERT_NULL(s_file.frames[0].error);
    return &s_file.frames[0];
}

static const fi_frame_t *capture(void (*body)(void)) {
    size_t len = 0;
    char *text = record(body, &len);
    return load(text, len);
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
    TEST_ASSERT_NOT_EQUAL_UINT32(FI_NONE, fr->merge_mismatch);
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
    const nt_gfx_counters_t c = fi_recount(fr);
    TEST_ASSERT_EQUAL_MEMORY(&fr->view.counters, &c, sizeof(c));
    TEST_ASSERT_GREATER_THAN_UINT64(0, c.vertices);
    TEST_ASSERT_GREATER_THAN_UINT64(0, c.indices);
}

/* Uploads count issued calls with payload by call; a texture definition is sized by its levels. */
static void test_uploads_and_resource_bytes(void) {
    nt_gfx_event_t ev[3];
    memset(ev, 0, sizeof(ev));
    ev[0].kind = NT_GFX_EVENT_DEFINITION;
    ev[0].operation = NT_GFX_OP_CREATE;
    ev[0].object_kind = NT_GFX_OBJECT_TEXTURE;
    ev[0].object = 0x10001;
    ev[0].data.resource.backend = 1;
    ev[0].data.resource.width = 4;
    ev[0].data.resource.height = 4;
    ev[0].data.resource.format = NT_TEXTURE_FORMAT_RGBA8;
    ev[0].data.resource.levels = 3;
    ev[1].kind = NT_GFX_EVENT_OP;
    ev[1].operation = NT_GFX_OP_BUFFER_UPLOAD;
    ev[1].object_kind = NT_GFX_OBJECT_BUFFER;
    ev[1].result = NT_GFX_RESULT_ACCEPTED;
    ev[2].kind = NT_GFX_EVENT_BACKEND;
    ev[2].operation = NT_GFX_OP_STATE;
    ev[2].detail = NT_GFX_GL_glBufferSubData;
    ev[2].link = 2;
    ev[2].data.backend.bytes = 64;
    nt_gfx_capture_view_t view = {.events = ev, .count = 3};
    view.counters.accepted[NT_GFX_OP_BUFFER_UPLOAD] = 1;
    view.counters.gl[NT_GFX_GL_glBufferSubData] = 1;
    view.counters.buffer_upload_calls = 1;
    view.counters.buffer_upload_bytes = 64;
    const size_t len = nt_gfx_capture_format(&view, NULL, 0);
    char *text = (char *)malloc(len + 1);
    TEST_ASSERT_NOT_NULL(text);
    nt_gfx_capture_format(&view, text, len + 1);
    const fi_frame_t *fr = load(text, len);
    TEST_ASSERT_EQUAL_UINT64((4 * 4 + 2 * 2 + 1) * 4, fi_cell(fr, (fi_query_t){.kind = FI_Q_RESOURCE, .a = NT_GFX_OBJECT_TEXTURE, .b = FI_RES_LIVE}).sum);
    TEST_ASSERT_EQUAL_UINT64(64, fi_cell(fr, (fi_query_t){.kind = FI_Q_UPLOAD, .a = 0}).sum);
    TEST_ASSERT_EQUAL_UINT64(1, fi_cell(fr, (fi_query_t){.kind = FI_Q_OP_CALLS, .a = NT_GFX_OP_BUFFER_UPLOAD}).n);
    const nt_gfx_counters_t c = fi_recount(fr);
    TEST_ASSERT_EQUAL_MEMORY(&view.counters, &c, sizeof(c));
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
            const nt_gfx_counters_t c = fi_recount(fr);
            TEST_ASSERT_EQUAL_MEMORY(&fr->view.counters, &c, sizeof(c));
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
    nt_gfx_set_scissor(1, 2, 3, 4);
    nt_gfx_set_viewport(0, 0, 4, 4);
    nt_gfx_bind_pipeline((nt_pipeline_t){.id = 0x00FF00FFU}); /* invalid: the unbind */
    nt_gfx_end_pass();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_vertex_input(s_vi);
    nt_gfx_end_pass();
}

/* PASS resets the pass bindings, an invalid pipeline unbinds, the scissor carries over passes. */
static void test_request_state_follows_the_frontend_rules(void) {
    const fi_frame_t *fr = capture(request_body);
    fi_request_state_t st;
    const uint32_t viewport = find_op(fr, NT_GFX_OP_VIEWPORT, 0);
    fi_request_state(fr, viewport, &st);
    TEST_ASSERT_EQUAL_UINT32(find_op(fr, NT_GFX_OP_PIPELINE, 0), st.pipeline);
    TEST_ASSERT_EQUAL_UINT32(viewport, st.viewport);
    TEST_ASSERT_EQUAL_UINT32(find_op(fr, NT_GFX_OP_SCISSOR, 0), st.scissor);
    fi_request_state(fr, find_op(fr, NT_GFX_OP_PIPELINE, 1), &st);
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

/* A texture bind lands on the active unit; a restored context starts over. */
static void test_gl_state_follows_issued_calls(void) {
    nt_gfx_event_t ev[5];
    memset(ev, 0, sizeof(ev));
    ev[0].kind = NT_GFX_EVENT_INITIAL;
    ev[0].operation = NT_GFX_OP_STATE;
    ev[0].detail = NT_GFX_INITIAL_BACKEND;
    ev[0].data.backend.args[0] = 3;
    for (uint32_t k = 1; k < 5; k++) {
        ev[k].kind = NT_GFX_EVENT_BACKEND;
        ev[k].operation = NT_GFX_OP_STATE;
    }
    ev[1].detail = NT_GFX_GL_glActiveTexture;
    ev[1].arg_count = 1;
    ev[1].data.backend.args[0] = 0x84C1;
    ev[2].detail = NT_GFX_GL_glBindTexture;
    ev[2].arg_count = 2;
    ev[2].data.backend.args[0] = 0x0DE1;
    ev[2].data.backend.args[1] = 7;
    ev[3].kind = NT_GFX_EVENT_OP;
    ev[3].operation = NT_GFX_OP_CONTEXT;
    ev[3].result = NT_GFX_RESULT_ACCEPTED;
    ev[4].detail = NT_GFX_GL_glUseProgram;
    ev[4].arg_count = 1;
    ev[4].data.backend.args[0] = 9;
    const nt_gfx_capture_view_t view = {.events = ev, .count = 5};
    const size_t len = nt_gfx_capture_format(&view, NULL, 0);
    char *text = (char *)malloc(len + 1);
    TEST_ASSERT_NOT_NULL(text);
    nt_gfx_capture_format(&view, text, len + 1);
    const fi_frame_t *fr = load(text, len);
    fi_gl_state_t st;
    fi_gl_state(fr, 2, &st);
    TEST_ASSERT_EQUAL_UINT32(2, gl_slot(&st, NT_GFX_GL_glBindTexture, 1, 0x0DE1)->record);
    TEST_ASSERT_TRUE(gl_slot(&st, NT_GFX_GL_glUseProgram, 0, 0)->inherited);
    fi_gl_state(fr, 4, &st);
    TEST_ASSERT_EQUAL_UINT32(1, st.count);
    TEST_ASSERT_EQUAL_UINT32(4, gl_slot(&st, NT_GFX_GL_glUseProgram, 0, 0)->record);
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
    text[10] = '9'; /* "ntcapture 2" becomes an unknown version */
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
    TEST_ASSERT_LESS_THAN_UINT64(fr->view.counters.vertices, fi_recount(fr).vertices);
}
// #endregion

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_notes_cover_the_operations_after_them);
    RUN_TEST(test_phases_follow_the_frame);
    RUN_TEST(test_rows_are_a_preorder_of_the_tree);
    RUN_TEST(test_merge_rule_matches_the_frontend);
    RUN_TEST(test_a_wrong_merge_marks_the_frame);
    RUN_TEST(test_each_number_counts_the_records_its_query_selects);
    RUN_TEST(test_recount_equals_the_counters);
    RUN_TEST(test_uploads_and_resource_bytes);
    RUN_TEST(test_real_captures_recount);
    RUN_TEST(test_request_state_follows_the_frontend_rules);
    RUN_TEST(test_gl_state_follows_issued_calls);
    RUN_TEST(test_damage_stays_inside_its_document);
    RUN_TEST(test_an_overflowed_frame_opens);
    return UNITY_END();
}
