/* Frame inspector query mode and the state views it shares with the window, on the native fixture (frame 1009:
 * 47 draw requests, 9 GL draw calls, groups shape/sprite/text). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fi_query.h"
#include "fs/nt_fs.h"
#include "unity.h"

static fi_file_t s_file;
static fi_session_t s_session;
static fi_out_t s_out;

void setUp(void) {
    TEST_ASSERT_EQUAL(NT_OK, nt_fs_init());
    const bool opened = fi_file_open(&s_file, NT_NATIVE_FIXTURE);
    nt_fs_shutdown();
    TEST_ASSERT_TRUE(opened);
    s_session = (fi_session_t){.files = &s_file, .file_count = 1};
}

void tearDown(void) {
    fi_file_close(&s_file);
    free(s_out.text);
    s_out = (fi_out_t){0};
}

/* The answer to one command line, NUL-terminated; a failed command still says why. */
static const char *ask(const char *line, bool ok) {
    s_out.len = 0;
    TEST_ASSERT_EQUAL_MESSAGE(ok, fi_query(&s_session, line, &s_out), line);
    TEST_ASSERT_GREATER_THAN_size_t_MESSAGE(0, s_out.len, line);
    return s_out.text;
}

static void assert_holds(const char *text, const char *want) { TEST_ASSERT_NOT_NULL_MESSAGE(strstr(text, want), want); }

static void test_commands_answer_and_reject_bad_input(void) {
    static const char *const k_ok[] = {"help",       "frames",        "frame 0",     "summary",     "groups",           "record @0",        "state @300",
                                       "state init", "state end all", "find glDraw", "list merged", "list g2.gl_draws", "list break.range", "list drops"};
    for (uint32_t k = 0; k < sizeof(k_ok) / sizeof(k_ok[0]); k++) {
        (void)ask(k_ok[k], true);
    }
    char find[208] = "find "; /* find reads the whole rest, past the 159 characters of an argument */
    memset(find + 5, 'x', 200);
    find[205] = '\0';
    assert_holds(ask(find, true), "records 0\n");
    static const struct {
        const char *line, *why;
    } k_bad[] = {{"bogus", "unknown command"},
                 {"", "unknown command"},
                 {"frame 1", "no readable frame 1"},
                 {"frame 0x", "frame INDEX"},
                 {"frame 0 1", "too many arguments"},
                 {"record", "a record is needed"},
                 {"record @999999", "no record @999999"},
                 {"record @3x", "no record @3x"},
                 {"record @1 @2", "too many arguments"},
                 {"state @5 most", "state @N|init|end [all]"},
                 {"summary extra", "too many arguments"},
                 {"groups g1", "too many arguments"},
                 {"find", "find needs TEXT"},
                 {"list nothing", "list needs a key"},
                 {"list g99.gl_draws", "list needs a key"},
                 {"list g2.nothing", "list needs a key"},
                 {"list merged extra", "list needs a key"},
                 {"record @0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000005",
                  "longer than 159"}};
    for (uint32_t k = 0; k < sizeof(k_bad) / sizeof(k_bad[0]); k++) {
        assert_holds(ask(k_bad[k].line, false), k_bad[k].why);
    }
    (void)ask("frame 1", false); /* a failed frame keeps the frame commands read */
    assert_holds(ask("frames", true), "\n* 0 1009 ");
    const size_t all = strlen(ask("state @700 all", true)); /* tabs and trailing spaces change nothing */
    TEST_ASSERT_EQUAL_size_t(all, strlen(ask("state\t@700  all  ", true)));
    TEST_ASSERT_GREATER_THAN_size_t(strlen(ask("state @700", true)), all);
}

/* Every number summary prints has a key, and list KEY prints exactly the records it counts. */
static void test_every_number_lists_its_records(void) {
    static fi_number_t numbers[FI_NUMBERS];
    const fi_frame_t *fr = &s_file.frames[0];
    const uint32_t n = fi_numbers(fr, false, numbers);
    TEST_ASSERT_GREATER_THAN_UINT32(20, n);
    char want[96];
    for (uint32_t k = 0; k < n; k++) {
        (void)snprintf(want, sizeof(want), "\n%-36s %10llu  # ", numbers[k].key, (unsigned long long)numbers[k].value);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ask("summary", true), want), numbers[k].key);
        char line[64];
        (void)snprintf(line, sizeof(line), "list %s", numbers[k].key);
        (void)snprintf(want, sizeof(want), "records %llu\n", (unsigned long long)fi_cell(fr, numbers[k].q).n);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(ask(line, true), want), numbers[k].key);
    }
    assert_holds(ask("summary", true), "\ngl_draw_calls                                 9  # ");
    assert_holds(ask("list merged", true), "records 38\n");
    assert_holds(ask("list gl_draw_calls", true), "records 9\n");
    const uint32_t keys = fi_numbers(fr, true, numbers); /* every key, zeros too: each names one number */
    for (uint32_t a = 0; a < keys; a++) {
        TEST_ASSERT_NULL_MESSAGE(strstr(numbers[a].key, "null"), numbers[a].key);
        for (uint32_t b = a + 1U; b < keys; b++) {
            TEST_ASSERT_NOT_EQUAL_INT_MESSAGE(0, strcmp(numbers[a].key, numbers[b].key), numbers[a].key);
        }
    }
}

/* Each group row sums into the frame: 9 GL draw calls as the engine counted them, 5 of them for sprites. */
static void test_groups_add_up_to_the_frame(void) {
    const char *text = ask("groups", true);
    assert_holds(text, "\ng2 19 5 23 0 0  # > sprite\n");
    unsigned long long sum[5] = {0};
    unsigned long long total[5] = {0};
    for (const char *l = text; *l != '\0'; l = strchr(l, '\n') + 1) {
        const bool row = l[0] == 'g';
        if (row || strncmp(l, "total ", 6) == 0) {
            char *at = strchr(l, ' ');
            for (uint32_t k = 0; k < 5; k++) {
                const unsigned long long v = strtoull(at, &at, 10);
                sum[k] += row ? v : 0U;
                total[k] = row ? total[k] : v;
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT64(nt_gfx_draw_calls(&s_file.frames[0].view.counters), sum[1]);
    TEST_ASSERT_EQUAL_UINT64(9, sum[1]);
    TEST_ASSERT_EQUAL_UINT64(47, sum[0]);
    TEST_ASSERT_EQUAL_UINT64(s_file.frames[0].summary.gl_any.n, sum[2]);
    TEST_ASSERT_EQUAL_UINT64(93952, sum[3]);
    TEST_ASSERT_EQUAL_UINT64(sum[1], total[1]);
}

/* @224 starts a new GL draw because a vertex input was set before it; @233 joins it; @204 is instanced. */
static void test_record_tells_merged_and_broken_draws(void) {
    const char *text = ask("record @224", true);
    assert_holds(text, "new_gl_draw 3 of 9\nnot_merged  # a command recorded between\nbecause @223  VERTEX_INPUT");
    assert_holds(text, "caused @680  glDrawElements");
    assert_holds(text, "\ninside @216  NOTE label=\"sprite\"\ngroup g2  # > sprite\n");
    text = ask("record @233", true);
    assert_holds(text, "merged  # no new GL draw call");
    assert_holds(text, "joined @224 ");
    assert_holds(text, "\ngl_draw @680  glDrawElements(");
    assert_holds(ask("record @204", true), "new_gl_draw 1 of 9\nnot_merged instanced");
}

/* A texture of the snapshot and of the sprite group: its definition, its GL name, and its records by group. */
static void test_object_lists_where_it_is_named(void) {
    assert_holds(ask("record @128", true), "\nobject texture tex#1  # object @128.texture\nobject sampler smp/linear_mip_linear_repeat#1  # object @128.sampler\n");
    const char *text = ask("object @128.texture", true);
    assert_holds(text, "object tex#1  # TEXTURE 65541\ndefinition @74  DEFINITION CREATE tex#1 slot=5 ");
    assert_holds(text, "\ngl_name 5 from @128\n@74  DEFINITION CREATE tex#1 ");
    assert_holds(text, "\n@183  DEFINITION STATE TEXTURE slot=5 gl_name=5 tex#1\n@218  NOTE_FIELD page=tex#1\n");
    assert_holds(text, "\n@689  glBindTexture(GL_TEXTURE_2D,5 tex#1)\n");
    assert_holds(text, "\nrecords 44\n");
    static char texture[8192];
    (void)snprintf(texture, sizeof(texture), "%s", text);
    TEST_ASSERT_EQUAL_STRING(texture, ask("object @74", true)); /* a definition names its own object first */
    assert_holds(ask("object @657.0", true), "object prog/shape/fill#1  # PROGRAM ");
    TEST_ASSERT_NULL(strstr(ask("record @79", true), "object gl_name")); /* a definition's own GL name is not a second object */
    assert_holds(ask("object @000000000000000000000000009", false), "error: the frame has no record @000000000000000000000000009");
    /* a note's handle is selected by value: its key is game text, never a command token */
    assert_holds(ask("record @218", true), "\nobject value tex#1  # object @218.value\n");
    assert_holds(ask("object @128.unit", false), "error: record @128 names no object by unit");
    assert_holds(ask("object @1", false), "error: record @1 names no object;");
    assert_holds(ask("object", false), "error: object needs @N[.FIELD]");
}

/* A draw's state compares with the previous draw; its GL state is its own last call, or the call of the draw a
 * merged draw joined. The frame's end compares with its start. */
static void test_state_views(void) {
    const fi_frame_t *fr = &s_file.frames[0];
    static fi_state_view_t v;
    fi_state_at(fr, 635, &v);
    TEST_ASSERT_EQUAL_UINT32(623, v.was);
    TEST_ASSERT_EQUAL_UINT32(712, v.gl_at);
    TEST_ASSERT_EQUAL_UINT32(709, v.was_gl_at);
    TEST_ASSERT_EQUAL_UINT8(FI_COMPARE_PREVIOUS_DRAW, v.compare);
    TEST_ASSERT_EQUAL_UINT32(9, fi_verdict(fr, 635).ordinal);
    fi_state_at(fr, 233, &v);
    TEST_ASSERT_EQUAL_UINT32(224, v.was);
    TEST_ASSERT_EQUAL_UINT32(680, v.gl_at);
    TEST_ASSERT_TRUE(fi_state_frame(fr, false, &v));
    TEST_ASSERT_EQUAL_UINT32(FI_NONE, v.was);
    TEST_ASSERT_EQUAL_UINT32(194, v.gl_at);
    TEST_ASSERT_TRUE(fi_state_frame(fr, true, &v));
    TEST_ASSERT_EQUAL_UINT32(194, v.was);
    TEST_ASSERT_EQUAL_UINT32(fr->view.count - 1U, v.gl_at);
    TEST_ASSERT_EQUAL_UINT8(FI_COMPARE_FRAME_START, v.compare);
    static fi_state_line_t lines[FI_STATE_LINES];
    const uint32_t n = fi_state_lines(fr, &v, true, lines);
    uint32_t scissor = FI_NONE;
    for (uint32_t k = 0; k < n; k++) {
        scissor = lines[k].gl && strcmp(lines[k].name, "GL_SCISSOR_TEST") == 0 ? k : scissor;
    }
    TEST_ASSERT_NOT_EQUAL(FI_NONE, scissor);
    TEST_ASSERT_TRUE(lines[scissor].changed); /* enabled at the start, disabled at the end */
}

/* Files of two runs joined repeat a sequence: the index still reaches the second frame. */
static void test_a_repeated_sequence_has_two_addresses(void) {
    const size_t len = s_file.len;
    char *text = (char *)malloc((len * 2) + 1);
    TEST_ASSERT_NOT_NULL(text);
    memcpy(text, s_file.text, len);
    memcpy(text + len, s_file.text, len);
    fi_file_close(&s_file);
    fi_file_load(&s_file, "joined\n= ok", text, len * 2); /* a name's line break cannot end an answer */
    TEST_ASSERT_EQUAL_UINT32(2, s_file.frame_count);
    assert_holds(ask("frames", true), "\n* 0 1009 714 9 47 38 67 93952 0\n  1 1009 714 9 47 38 67 93952 0\n");
    assert_holds(ask("frame 1", true), "frame 1  # sequence 1009, file 0 joined\\x0a= ok, 714 records\n");
    assert_holds(ask("frames", true), "\nfile 0 joined\\x0a= ok  # 2 frames\n");
    TEST_ASSERT_EQUAL_UINT32(1, s_session.frame);
    assert_holds(ask("frames", true), "\n  0 1009 714 9 47 38 67 93952 0\n* 1 1009 ");
}

/* The fixture as frame 0 and an edited copy as frame 1 of one file. Each edit puts `to` in place of the first
 * `from` after `at`; the two are one length, so the rest of the line stays. */
typedef struct {
    const char *at, *from, *to;
} edit_t;

static void load_variant(const edit_t *edits, uint32_t count) {
    const size_t len = s_file.len;
    char *text = (char *)malloc((len * 2) + 1);
    TEST_ASSERT_NOT_NULL(text);
    memcpy(text, s_file.text, len);
    memcpy(text + len, s_file.text, len);
    text[len * 2] = '\0';
    for (uint32_t k = 0; k < count; k++) {
        char *at = strstr(text + len, edits[k].at);
        TEST_ASSERT_NOT_NULL(at);
        char *from = strstr(at, edits[k].from);
        TEST_ASSERT_TRUE(from != NULL && from < strchr(at + 1, '\n'));
        TEST_ASSERT_EQUAL_size_t(strlen(edits[k].from), strlen(edits[k].to));
        memcpy(from, edits[k].to, strlen(edits[k].to));
    }
    fi_file_close(&s_file);
    fi_file_load(&s_file, "pair", text, len * 2);
    TEST_ASSERT_EQUAL_UINT32(2, s_file.frame_count);
}

/* Compare's A and B columns are each frame's own numbers, the biggest change first; the edited requests are
 * the only differences, each saying what changed. */
static void test_compare_tells_what_differs(void) {
    static const edit_t k_edits[] = {
        {"\n233 OP op=DRAW_INDEXED", " count=6 ", " count=7 "}, {"\n209 OP op=DRAW_INDEXED_INSTANCED", "instances=42", "instances=99"}, {"\n211 OP op=VIEWPORT", "height=800", "height=801"}};
    load_variant(k_edits, 3);
    (void)ask("frame 1", true);
    static fi_number_t a_numbers[FI_NUMBERS];
    static fi_number_t b_numbers[FI_NUMBERS];
    const fi_frame_t *a = &s_file.frames[0];
    const fi_frame_t *b = &s_file.frames[1];
    const uint32_t n = fi_numbers(a, true, a_numbers);
    TEST_ASSERT_EQUAL_UINT32(n, fi_numbers(b, true, b_numbers));
    for (uint32_t k = 0; k < n; k++) { /* keys line up, and a number read in the other frame is that frame's own */
        TEST_ASSERT_EQUAL_STRING(a_numbers[k].key, b_numbers[k].key);
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(a_numbers[k].value, fi_number_in(a, &b_numbers[k]), a_numbers[k].key);
    }
    const char *text = ask("compare 0", true);
    const char *vertices = strstr(text, "\nvertices                                   5251       5650        +399  # ");
    const char *indices = strstr(text, "\nindices                                   19176      19519        +343  # ");
    const char *instances = strstr(text, "\ninstances                                    63        120         +57  # ");
    TEST_ASSERT_TRUE(vertices != NULL && indices > vertices && instances > indices);
    assert_holds(text, "\ng2.commands                                   1  # > sprite: requests that differ, of ");
    assert_holds(text, "\ndiffer 5  # ");
    text = ask("compare 0 g2", true);
    assert_holds(text, "\ngroup g2  # > sprite; 1 of 124 requests differ\n");
    assert_holds(text, "\n~ @233    @233    DRAW_INDEXED pipe/skeletal_showcase/sprite#1 count=7 vi/sprite_vi#1  # count 6 -> 7\n");
    assert_holds(text, "\nrows 124 differ 1\n");
    text = ask("compare 0 g1", true);
    assert_holds(text, "\n= 9 same  # @200..@208 in A, @200..@208 in B\n~ @209    @209    ");
    assert_holds(text, " x99  # instances 42 -> 99\n= @210    @210    SCISSOR_ENABLE off\n");
    assert_holds(text, "  # A: VIEWPORT width=1280 height=800\n");
    assert_holds(ask("compare 1", true), "\ndiffer 0  # ");
    static const struct {
        const char *line, *why;
    } k_bad[] = {{"compare", "compare INDEX [GROUP] (INDEX:"},
                 {"compare x", "compare INDEX [GROUP] (INDEX:"},
                 {"compare 9", "no readable frame 9"},
                 {"compare 0 g99", "no group g99"},
                 {"compare 0 g2 g3", "compare INDEX [GROUP]; help"}};
    for (uint32_t k = 0; k < sizeof(k_bad) / sizeof(k_bad[0]); k++) {
        assert_holds(ask(k_bad[k].line, false), k_bad[k].why);
    }
}

/* The same skeletal_showcase frame on native and web: every group pairs, every request aligns, and only the
 * viewports and scissors differ, because the two windows differ in size. */
static void test_native_and_web_align(void) {
    static fi_file_t files[2];
    TEST_ASSERT_EQUAL(NT_OK, nt_fs_init());
    const bool opened = fi_file_open(&files[0], NT_NATIVE_FIXTURE) && fi_file_open(&files[1], NT_WEB_FIXTURE);
    nt_fs_shutdown();
    TEST_ASSERT_TRUE(opened);
    const fi_frame_t *a = &files[0].frames[0];
    const fi_frame_t *b = &files[1].frames[0];
    TEST_ASSERT_EQUAL(NT_GFX_CAPTURE_WEB, b->view.platform);
    fi_diff_t d;
    fi_diff(a, b, &d);
    uint32_t changed = 0;
    for (uint32_t g = 0; g < d.group_count; g++) {
        TEST_ASSERT_NOT_EQUAL_UINT32(FI_NONE, d.group[g].a);
        TEST_ASSERT_NOT_EQUAL_UINT32(FI_NONE, d.group[g].b);
        TEST_ASSERT_TRUE(d.group[g].aligned);
    }
    for (uint32_t r = 0; r < d.row_count; r++) {
        TEST_ASSERT_TRUE(d.row[r].kind == FI_DIFF_SAME || d.row[r].kind == FI_DIFF_CHANGED);
        if (d.row[r].kind == FI_DIFF_CHANGED) {
            const uint8_t op = b->view.events[d.row[r].b].operation;
            TEST_ASSERT_TRUE(op == NT_GFX_OP_VIEWPORT || op == NT_GFX_OP_SCISSOR);
            TEST_ASSERT_EQUAL_UINT8(FI_CHANGE_ARGS, d.row[r].change);
            changed++;
        }
    }
    TEST_ASSERT_GREATER_THAN_UINT32(0, changed);
    fi_diff_free(&d);
    fi_file_close(&files[0]);
    fi_file_close(&files[1]);
}

/* Renamed notes make a new group: B's is only in B, A's old one is a.gN only in A, and both list their requests. */
static void test_compare_names_groups_only_one_frame_has(void) {
    static const edit_t k_renamed[] = {{"\n200 NOTE", "\"shape\"", "\"shapf\""}, {"\n205 NOTE", "\"shape\"", "\"shapf\""}};
    load_variant(k_renamed, 2);
    (void)ask("frame 1", true);
    const fi_frame_t *a = &s_file.frames[0];
    uint32_t g = 0;
    while (g < a->group_count && strcmp(a->group_key[g], "> shape") != 0) {
        g++;
    }
    TEST_ASSERT_LESS_THAN_UINT32(a->group_count, g);
    char line[64];
    const char *text = ask("compare 0", true);
    (void)snprintf(line, sizeof(line), "\na.g%u.requests ", g);
    assert_holds(text, line);
    assert_holds(text, "  # only in A: > shape\n");
    assert_holds(text, "  # only in B: > shapf\n");
    fi_diff_t d;
    fi_diff(a, &s_file.frames[1], &d);
    uint32_t differ = 0;
    for (uint32_t k = 0; k < d.group_count; k++) {
        differ += fi_group_differs(a, &s_file.frames[1], &d.group[k]) ? 1U : 0U;
    }
    TEST_ASSERT_EQUAL_UINT32(2, differ); /* the old name and the new */
    fi_diff_free(&d);
    (void)snprintf(line, sizeof(line), "compare 0 a.g%u", g);
    text = ask(line, true);
    assert_holds(text, "  # > shape; only in A; requests ");
    assert_holds(text, "\n- @200    -       ");
}

/* A labeled handle shows kind, label and number as one token: a space or control byte shows escaped, and a label
 * too long for the alias is cut on a whole escape, never into the number. */
static void test_aliases_show_labels(void) {
    char text[512];
    char label[210];
    memset(label, 'x', sizeof(label) - 1U);
    memcpy(label + 98, "\\x0a", 4); /* its escape is the last that fits */
    label[sizeof(label) - 1U] = '\0';
    const int len = snprintf(text, sizeof(text),
                             "ntcapture 4\nframe sequence=1 platform=native records=4 overflow=0 execute=0..0\n0 DEFINITION op=CREATE obj=BUFFER:65537 label=\"mesh one\\x0a\"\n"
                             "1 DEFINITION op=CREATE obj=BUFFER:65538 label=\"%s\"\n2 DEFINITION op=CREATE obj=BUFFER:65539 label=\"mesh one\\x0a\"\n"
                             "3 OP op=UBO obj=BUFFER:65538\n",
                             label);
    fi_file_t file = {0};
    char *owned = (char *)malloc((size_t)len);
    TEST_ASSERT_NOT_NULL(owned);
    memcpy(owned, text, (size_t)len);
    fi_file_load(&file, "labels", owned, (size_t)len);
    TEST_ASSERT_NULL(file.frames[0].error);
    char alias[FI_ALIAS_LEN];
    fi_alias_text(&file.frames[0], NT_GFX_OBJECT_BUFFER, 65537, alias, sizeof(alias));
    TEST_ASSERT_EQUAL_STRING("buf/mesh\\x20one\\x0a#1", alias); /* two buffers share the label: the number tells them apart */
    fi_alias_text(&file.frames[0], NT_GFX_OBJECT_BUFFER, 65539, alias, sizeof(alias));
    TEST_ASSERT_EQUAL_STRING("buf/mesh\\x20one\\x0a#2", alias);
    fi_alias_text(&file.frames[0], NT_GFX_OBJECT_BUFFER, 65538, alias, sizeof(alias));
    TEST_ASSERT_EQUAL_STRING("\\x0a#1", alias + strlen(alias) - 6U); /* the only one with its label: still numbered */
    TEST_ASSERT_EQUAL_MEMORY("buf/xxxx", alias, 8);
    fi_file_close(&file);
}

/* Rows name every captured value by its field: packed backend state, pipeline state, engine enums, flag sets,
 * handles in notes, GL call arguments and cache keys; the record page keeps the line as captured. */
static void test_rows_decode_captured_values(void) {
    assert_holds(ask("record @114", true), "@114  INITIAL STATE BACKEND program=19 prog/skeletal_showcase/sprite#1 vertex_array=7 vi/sprite_vi#1 active_texture=GL_TEXTURE0 "
                                           "width=1280 height=800 upload_vertex_array=1 upload VAO array_buffer=3 buf/frame_vertex#1\n");
    assert_holds(ask("record @114", true), "\ncaptured INITIAL op=STATE layer=BACKEND program=19 vertex_array=7 active_texture=33984 ");
    assert_holds(ask("record @117", true), "INITIAL PIPELINE program_gl=19 prog/skeletal_showcase/sprite#1 depth_func=GL_LEQUAL blend src_rgb=GL_ONE dst_rgb=GL_ONE_MINUS_SRC_ALPHA ");
    assert_holds(ask("record @79", true), "DEFINITION CREATE smp/linear_mip_linear_repeat#1 gl_name=2 min_filter=LINEAR_MIPMAP_LINEAR mag_filter=LINEAR wrap_u=REPEAT wrap_v=REPEAT\n");
    assert_holds(ask("record @90", true), "DEFINITION CREATE layout/32B:1-5~78d4#1 slot=3 stride=32 locations=1-5\n");
    assert_holds(ask("record @36", true), " vertex_buffer=buf/shape_line_template#1 index_buffer=buf/shape_line_template#2 index_type=U16 ");
    assert_holds(ask("record @218", true), "@218  NOTE_FIELD page=tex#1\n");
    assert_holds(ask("record @639", true), "@639  BUFFER_UPLOAD buf/frame_index#1 size=11184 data\n");
    assert_holds(ask("record @642", true), "@642  glBufferSubData(GL_ELEMENT_ARRAY_BUFFER,0,11184,data) bytes=11184\n");
    assert_holds(ask("record @678", true), "@678  glBindSampler cache sampler=2 smp/linear_mip_linear_repeat#1  [GL skipped]\n");
    assert_holds(ask("record @662", true), "@662  glVertexAttribPointer cache vertex_array=3 vi/shape_fill_vi#1 buffer=3 buf/frame_vertex#1 layout=1  [GL skipped]\n");
    assert_holds(ask("record @663", true), "@663  glBindBufferRange cache slot=2 offset=256 size=96  [GL skipped]\n");
    assert_holds(ask("record @118", true), "@118  DEFINITION PIPELINE slot=1 program_slot=1 depth_func=GL_LEQUAL ");
    assert_holds(ask("record @195", true), "@195  PASS target=default framebuffer color=[0.035,0.05,0.08,1] depth=1");
}

/* A GL call issued at end_frame has the engine state of the request that recorded it, compared with the draw
 * before that request; its GL state stays its own. An inherited backend record shows only the slot's field. */
static void test_state_of_a_gl_call_is_its_request_state(void) {
    const char *text = ask("state @685 all", true);
    assert_holds(text, "\ncompared previous draw @260  ");
    assert_holds(text, "\n  output: pass               @195  PASS ");
    assert_holds(text, "\n* program: pipeline          @265  PIPELINE pipe/skeletal_showcase/text#1\n");
    assert_holds(text, "\n* UseProgram                 @681  glUseProgram(20 prog/skeletal_showcase/text#1)\n");
    assert_holds(text, "\n    was @128  INITIAL TEXTURE texture=5 tex#1\n");
    assert_holds(text, "\n    was @128  INITIAL TEXTURE sampler=2 smp/linear_mip_linear_repeat#1\n");
    assert_holds(text, "\n  GL_FRAMEBUFFER             inherited @114  INITIAL STATE BACKEND framebuffer=0\n");
    /* The inherited pass record holds the clear color and depth together; its line shows both. */
    assert_holds(ask("state init all", true), "INITIAL PASS color=[0.035,0.05,0.08,1] depth=1\n");
    /* An operation with no link (a frame storage upload) is its own request. */
    static char upload[4096];
    (void)snprintf(upload, sizeof(upload), "%s", strstr(ask("state @639", true), "\n# engine state"));
    const char *call = strstr(ask("state @642", true), "\n# engine state");
    TEST_ASSERT_NOT_NULL(call);
    TEST_ASSERT_EQUAL_STRING_LEN(upload, call, strstr(upload, "\n# GL state") - upload);
}

/* A command that issued no GL call (@228, a pipeline already set) runs in the stream after the draw before it: its GL
 * state is that draw's last call, not the state before execution. */
static void test_a_command_without_calls_takes_the_stream_point(void) {
    const char *text = ask("state @228 all", true);
    assert_holds(text, "\n# GL state at @680\n");
    assert_holds(text, "\ncompared previous GL draw @680  glDrawElements("); /* the draw it sits on: nothing changed */
    assert_holds(text, "\n  UseProgram                 @673  glUseProgram(19 prog/skeletal_showcase/sprite#1)\n");
    /* @221, an accepted texture set whose execution only skipped (the GL cache held its bindings) */
    assert_holds(ask("state @221", true), "\n# GL state at @676");
}

/* Every row of the native and web fixtures is built from fields: no generic list survives outside an issued call. */
static void test_every_row_names_its_fields(void) {
    fi_file_t web = {0};
    TEST_ASSERT_EQUAL(NT_OK, nt_fs_init());
    const bool opened = fi_file_open(&web, NT_WEB_FIXTURE);
    nt_fs_shutdown();
    TEST_ASSERT_TRUE(opened);
    const fi_frame_t *frames[2] = {&s_file.frames[0], &web.frames[0]};
    for (uint32_t f = 0; f < 2; f++) {
        for (uint32_t i = 0; i < frames[f]->view.count; i++) {
            char row[600];
            fi_row_text(frames[f], i, row, sizeof(row));
            TEST_ASSERT_TRUE_MESSAGE(row[0] != '\0', row);
            TEST_ASSERT_TRUE_MESSAGE(strstr(row, "=[") == NULL || strstr(row, "color=[") != NULL, row); /* only colors are lists */
            TEST_ASSERT_NULL_MESSAGE(strstr(row, "integers"), row);
            TEST_ASSERT_NULL_MESSAGE(strstr(row, "key="), row);
        }
    }
    fi_file_close(&web);
}

/* Every GL name of the native and web fixtures reads as the object it belonged to, or as a role. */
static void test_every_gl_name_has_an_owner(void) {
    fi_file_t web = {0};
    TEST_ASSERT_EQUAL(NT_OK, nt_fs_init());
    const bool opened = fi_file_open(&web, NT_WEB_FIXTURE);
    nt_fs_shutdown();
    TEST_ASSERT_TRUE(opened);
    const fi_frame_t *frames[2] = {&s_file.frames[0], &web.frames[0]};
    uint32_t names = 0;
    for (uint32_t f = 0; f < 2; f++) {
        const fi_frame_t *fr = frames[f];
        for (uint32_t k = 0; k < fr->gl_first[fr->view.count]; k++) {
            TEST_ASSERT_NOT_EQUAL(FI_GL_UNKNOWN, fr->gl_ref[k].owner);
            names++;
        }
    }
    TEST_ASSERT_GREATER_THAN_UINT32(200, names);
    fi_file_close(&web);
}
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_commands_answer_and_reject_bad_input);
    RUN_TEST(test_every_number_lists_its_records);
    RUN_TEST(test_groups_add_up_to_the_frame);
    RUN_TEST(test_record_tells_merged_and_broken_draws);
    RUN_TEST(test_object_lists_where_it_is_named);
    RUN_TEST(test_state_views);
    RUN_TEST(test_a_repeated_sequence_has_two_addresses);
    RUN_TEST(test_compare_tells_what_differs);
    RUN_TEST(test_native_and_web_align);
    RUN_TEST(test_compare_names_groups_only_one_frame_has);
    RUN_TEST(test_aliases_show_labels);
    RUN_TEST(test_rows_decode_captured_values);
    RUN_TEST(test_state_of_a_gl_call_is_its_request_state);
    RUN_TEST(test_a_command_without_calls_takes_the_stream_point);
    RUN_TEST(test_every_row_names_its_fields);
    RUN_TEST(test_every_gl_name_has_an_owner);
    return UNITY_END();
}
