/* Frame inspector query mode and the state views it shares with the window, on the native fixture (frame 51:
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
    assert_holds(ask("frames", true), "\n* 0 51 ");
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
    assert_holds(ask("record @204", true), "new_gl_draw 1 of 9\nnot_merged instanced");
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
    assert_holds(ask("frames", true), "\n* 0 51 714 9 47 38 67 93952 0\n  1 51 714 9 47 38 67 93952 0\n");
    assert_holds(ask("frame 1", true), "frame 1  # sequence 51, file 0 joined\\x0a= ok, 714 records\n");
    assert_holds(ask("frames", true), "\nfile 0 joined\\x0a= ok  # 2 frames\n");
    TEST_ASSERT_EQUAL_UINT32(1, s_session.frame);
    assert_holds(ask("frames", true), "\n  0 51 714 9 47 38 67 93952 0\n* 1 51 ");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_commands_answer_and_reject_bad_input);
    RUN_TEST(test_every_number_lists_its_records);
    RUN_TEST(test_groups_add_up_to_the_frame);
    RUN_TEST(test_record_tells_merged_and_broken_draws);
    RUN_TEST(test_state_views);
    RUN_TEST(test_a_repeated_sequence_has_two_addresses);
    return UNITY_END();
}
