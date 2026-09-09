#define NT_LOG_DOMAIN "floor"
#include "log/nt_log.h"
#include "unity.h"

#include <string.h>

static unsigned s_arguments;
static unsigned s_messages;
static nt_log_level_t s_level;
static char s_domain[16];
static char s_text[64];

static int argument(void) {
    s_arguments++;
    return 7;
}

static void record(nt_log_level_t level, const char *domain, const char *message, void *user) {
    (void)user;
    s_messages++;
    s_level = level;
    strncpy(s_domain, domain, sizeof(s_domain) - 1);
    strncpy(s_text, message, sizeof(s_text) - 1);
}

void setUp(void) {
    s_arguments = 0;
    s_messages = 0;
    s_domain[0] = '\0';
    s_text[0] = '\0';
    nt_log_set_level(NT_LOG_LEVEL_INFO);
    nt_log_add_sink(record, NULL);
}

void tearDown(void) { nt_log_remove_sink(record, NULL); }

#define CHECK_LEVEL(lower, upper, level)                                                                                                                                                               \
    do {                                                                                                                                                                                               \
        const bool enabled = NT_LOG_MIN_LEVEL <= (level);                                                                                                                                              \
        for (unsigned i = 0; i < 2; i++) {                                                                                                                                                             \
            nt_log_##lower("plain-" #lower " %d", argument());                                                                                                                                         \
            NT_LOG_##upper("domain-" #lower " %d", argument());                                                                                                                                        \
            nt_log_##lower##_once("once-" #lower " %d", argument());                                                                                                                                   \
            NT_LOG_##upper##_ONCE("domain-once-" #lower " %d", argument());                                                                                                                            \
            bool first = nt_log_##lower##_unique("unique-" #lower " %d", argument());                                                                                                                  \
            bool named = NT_LOG_##upper##_UNIQUE("domain-unique-" #lower " %d", argument());                                                                                                           \
            TEST_ASSERT_EQUAL(enabled &&i == 0, first);                                                                                                                                                \
            TEST_ASSERT_EQUAL(enabled &&i == 0, named);                                                                                                                                                \
        }                                                                                                                                                                                              \
        TEST_ASSERT_EQUAL_UINT(enabled ? 10U : 0U, s_arguments);                                                                                                                                       \
        TEST_ASSERT_EQUAL_UINT(enabled ? 8U : 0U, s_messages);                                                                                                                                         \
        if (enabled) {                                                                                                                                                                                 \
            TEST_ASSERT_EQUAL_INT(level, s_level);                                                                                                                                                     \
            TEST_ASSERT_EQUAL_STRING("floor", s_domain);                                                                                                                                               \
            TEST_ASSERT_EQUAL_STRING("domain-" #lower " 7", s_text);                                                                                                                                   \
        }                                                                                                                                                                                              \
    } while (0)

static void test_info_floor(void) { CHECK_LEVEL(info, INFO, NT_LOG_LEVEL_INFO); }
static void test_warn_floor(void) { CHECK_LEVEL(warn, WARN, NT_LOG_LEVEL_WARN); }
static void test_error_floor(void) { CHECK_LEVEL(error, ERROR, NT_LOG_LEVEL_ERROR); }

static void test_direct_calls_obey_floor(void) {
    for (int level = NT_LOG_LEVEL_INFO; level < NT_LOG_LEVEL_NONE; level++) {
        const unsigned before = s_messages;
        nt_log_write((nt_log_level_t)level, "direct", "direct %d", argument());
        const bool wrote = nt_log_write_unique((nt_log_level_t)level, "direct", "direct unique %d", level);
        TEST_ASSERT_EQUAL(level >= NT_LOG_MIN_LEVEL, wrote);
        TEST_ASSERT_EQUAL_UINT(before + (level >= NT_LOG_MIN_LEVEL ? 2U : 0U), s_messages);
    }
    TEST_ASSERT_EQUAL_UINT(3, s_arguments);
}

static void test_runtime_filter_does_not_restore_compiled_out_levels(void) {
    nt_log_set_level(NT_LOG_LEVEL_NONE);
    nt_log_error("runtime-filtered %d", argument());
    TEST_ASSERT_EQUAL_UINT(NT_LOG_MIN_LEVEL < 3 ? 1U : 0U, s_arguments);
    TEST_ASSERT_EQUAL_UINT(0, s_messages);
    nt_log_set_level(NT_LOG_LEVEL_INFO);
    nt_log_info("floor-still-applies %d", argument());
    TEST_ASSERT_EQUAL_UINT(NT_LOG_MIN_LEVEL == 0 ? 1U : 0U, s_messages);
}

int main(void) {
    (void)argument;
    UNITY_BEGIN();
    RUN_TEST(test_info_floor);
    RUN_TEST(test_warn_floor);
    RUN_TEST(test_error_floor);
    RUN_TEST(test_direct_calls_obey_floor);
    RUN_TEST(test_runtime_filter_does_not_restore_compiled_out_levels);
    return UNITY_END();
}
