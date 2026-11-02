/* Frame inspector query mode on the native fixture: every command answers, groups add up to the frame, a draw's
 * record tells whether it merged and why not. */

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
    TEST_ASSERT_TRUE(fi_file_open(&s_file, NT_NATIVE_FIXTURE));
    nt_fs_shutdown();
    s_session = (fi_session_t){.files = &s_file, .file_count = 1};
}

void tearDown(void) {
    fi_file_close(&s_file);
    free(s_out.text);
    s_out = (fi_out_t){0};
}

/* The answer to one command line, NUL-terminated. */
static const char *ask(const char *line, bool ok) {
    s_out.len = 0;
    TEST_ASSERT_EQUAL_MESSAGE(ok, fi_query(&s_session, line, &s_out), line);
    TEST_ASSERT_GREATER_THAN_size_t_MESSAGE(0, s_out.len, line);
    return s_out.text;
}

static void test_every_command_answers(void) {
    static const char *const k_lines[] = {"help",          "frames",      "frame 51",   "summary",       "groups",      "record @0",         "state @300",          "state init",
                                          "state end all", "find glDraw", "list draws", "list problems", "list merged", "list group sprite", "list op draw_indexed"};
    for (uint32_t k = 0; k < sizeof(k_lines) / sizeof(k_lines[0]); k++) {
        (void)ask(k_lines[k], true);
    }
    (void)ask("bogus", false);
    (void)ask("frame 7", false);
    (void)ask("record @999999", false);
    (void)ask("list group nothing-like-it", false);
}

/* The total row of groups equals the frame's numbers: every GL draw call belongs to one group. */
static void test_groups_add_up_to_the_frame(void) {
    const char *text = ask("groups", true);
    const char *total = strstr(text, "  total\n");
    TEST_ASSERT_NOT_NULL(total);
    while (total > text && total[-1] != '\n') {
        total--;
    }
    unsigned long long n[5] = {0};
    char *at = (char *)total;
    for (uint32_t k = 0; k < 5; k++) {
        n[k] = strtoull(at, &at, 10);
    }
    const fi_summary_t *sm = &s_file.frames[0].summary;
    TEST_ASSERT_EQUAL_UINT64(sm->vertices.n, n[0]);
    TEST_ASSERT_EQUAL_UINT64(sm->draw_calls.n, n[1]);
    TEST_ASSERT_EQUAL_UINT64(sm->gl_any.n, n[2]);
    TEST_ASSERT_EQUAL_UINT64(sm->upload[2].sum, n[3]);
    TEST_ASSERT_EQUAL_UINT64(sm->drops.n, n[4]);
    TEST_ASSERT_GREATER_THAN_UINT64(0, n[1]);
}

/* A merged draw names the draw it joined; a draw that broke the merge names the break and its cause. */
static void test_record_tells_merged_and_broken_draws(void) {
    const fi_frame_t *fr = &s_file.frames[0];
    uint32_t merged = FI_NONE;
    uint32_t broken = FI_NONE;
    for (uint32_t i = 0; i < fr->view.count; i++) {
        merged = merged == FI_NONE && fi_joined(fr, i) != FI_NONE ? i : merged;
        broken = broken == FI_NONE && fr->brk[i] != FI_BREAK_NONE && fr->cause[i] != FI_NONE ? i : broken;
    }
    TEST_ASSERT_NOT_EQUAL(FI_NONE, merged);
    TEST_ASSERT_NOT_EQUAL(FI_NONE, broken);
    char line[64];
    char want[96];
    (void)snprintf(line, sizeof(line), "record @%u", merged);
    const char *text = ask(line, true);
    (void)snprintf(want, sizeof(want), "MERGED: no new GL draw call");
    TEST_ASSERT_NOT_NULL(strstr(text, want));
    (void)snprintf(want, sizeof(want), "joined @%u ", fi_joined(fr, merged));
    TEST_ASSERT_NOT_NULL(strstr(text, want));
    (void)snprintf(line, sizeof(line), "record @%u", broken);
    text = ask(line, true);
    TEST_ASSERT_NOT_NULL(strstr(text, "NEW GL draw call"));
    (void)snprintf(want, sizeof(want), "not merged: %s\n", fi_break_name((fi_break_t)fr->brk[broken]));
    TEST_ASSERT_NOT_NULL(strstr(text, want));
    (void)snprintf(want, sizeof(want), "because of @%u ", fr->cause[broken]);
    TEST_ASSERT_NOT_NULL(strstr(text, want));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_every_command_answers);
    RUN_TEST(test_groups_add_up_to_the_frame);
    RUN_TEST(test_record_tells_merged_and_broken_draws);
    return UNITY_END();
}
