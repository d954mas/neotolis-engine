/* L1 CTest for the nt_log sink hook + nt_log_ring (OBS-01/02), no devapi.
 * Drives the REAL nt_log impl: attach nt_log_ring_sink via nt_log_add_sink,
 * call nt_log_write, then read the ring back via nt_log_ring_tail. */

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

/* clang-format off */
#include "core/nt_assert.h"
#include "log/nt_log.h"
#include "log/nt_log_ring.h"
#include "unity.h"
/* clang-format on */

/* Mirror nt_log.c's private cap (D-01 default 4, -D overridable) for the boundary test. */
#ifndef NT_LOG_MAX_SINKS
#define NT_LOG_MAX_SINKS 4
#endif

#if NT_LOG_RING_ENABLED

/* ---- Assert catching (setjmp/longjmp via hookable handler) ---- */

static jmp_buf s_assert_jmp;

static void test_assert_handler(const char *expr, const char *file, int line) {
    (void)expr;
    (void)file;
    (void)line;
    longjmp(s_assert_jmp, 1);
}

/* clang-format off */
#define EXPECT_ASSERT(code)                                                                    \
    do {                                                                                       \
        nt_assert_handler = test_assert_handler;                                               \
        if (setjmp(s_assert_jmp) == 0) {                                                       \
            code;                                                                              \
            nt_assert_handler = NULL;                                                          \
            TEST_FAIL_MESSAGE("Expected NT_ASSERT to fire");                                   \
        }                                                                                      \
        nt_assert_handler = NULL;                                                              \
    } while (0)
/* clang-format on */

/* The sink registry has no reset API (sinks live for the program). Register the
 * ring sink exactly once, then each test clears the ring in setUp. */
static bool s_ring_attached = false;

void setUp(void) {
    if (!s_ring_attached) {
        nt_log_add_sink(nt_log_ring_sink, NULL);
        s_ring_attached = true;
    }
    nt_log_set_level(NT_LOG_LEVEL_INFO);
    nt_log_ring_init();
}

void tearDown(void) {}

/* ---- Test 1: sink fires after format; ring captures {level, domain, msg} ---- */

static void test_ring_captures_writes(void) {
    nt_log_write(NT_LOG_LEVEL_INFO, "net", "hello %d", 1);
    nt_log_write(NT_LOG_LEVEL_WARN, "gfx", "second");
    nt_log_write(NT_LOG_LEVEL_ERROR, NULL, "third");

    nt_log_ring_entry_t out[8];
    uint16_t got = nt_log_ring_tail(8, NT_LOG_LEVEL_INFO, out);
    TEST_ASSERT_EQUAL_UINT16(3, got);

    /* Newest first. */
    TEST_ASSERT_EQUAL_INT(NT_LOG_LEVEL_ERROR, out[0].level);
    TEST_ASSERT_EQUAL_STRING("", out[0].domain); /* NULL domain stored as "" */
    TEST_ASSERT_EQUAL_STRING("third", out[0].msg);

    TEST_ASSERT_EQUAL_INT(NT_LOG_LEVEL_WARN, out[1].level);
    TEST_ASSERT_EQUAL_STRING("gfx", out[1].domain);
    TEST_ASSERT_EQUAL_STRING("second", out[1].msg);

    TEST_ASSERT_EQUAL_INT(NT_LOG_LEVEL_INFO, out[2].level);
    TEST_ASSERT_EQUAL_STRING("net", out[2].domain);
    TEST_ASSERT_EQUAL_STRING("hello 1", out[2].msg);
}

/* ---- Test 2: domain AND msg are COPIED (survive a later reuse of the buffers) ---- */

static void test_ring_copies_domain_and_msg(void) {
    char domain[NT_LOG_RING_DOMAIN_SIZE];
    char text[64];
    (void)snprintf(domain, sizeof(domain), "dom_first");
    (void)snprintf(text, sizeof(text), "first message");
    nt_log_write(NT_LOG_LEVEL_INFO, domain, "%s", text);

    /* Overwrite the source buffers, then write again (reuses nt_log's internal msg[]). */
    (void)snprintf(domain, sizeof(domain), "dom_second");
    (void)snprintf(text, sizeof(text), "second message");
    nt_log_write(NT_LOG_LEVEL_INFO, domain, "%s", text);

    nt_log_ring_entry_t out[4];
    uint16_t got = nt_log_ring_tail(4, NT_LOG_LEVEL_INFO, out);
    TEST_ASSERT_EQUAL_UINT16(2, got);

    /* The first entry must still read its ORIGINAL text — proof of an owned copy. */
    TEST_ASSERT_EQUAL_STRING("dom_second", out[0].domain);
    TEST_ASSERT_EQUAL_STRING("second message", out[0].msg);
    TEST_ASSERT_EQUAL_STRING("dom_first", out[1].domain);
    TEST_ASSERT_EQUAL_STRING("first message", out[1].msg);
}

/* ---- Test 3: overflow caps at NT_LOG_RING_DEPTH, oldest evicted ---- */

static void test_ring_overflow_caps(void) {
    const int total = NT_LOG_RING_DEPTH + 5;
    for (int i = 0; i < total; i++) {
        nt_log_write(NT_LOG_LEVEL_INFO, "d", "entry %d", i);
    }

    /* Ask for more than depth — capped at stored count (== depth). */
    static nt_log_ring_entry_t out[NT_LOG_RING_DEPTH];
    uint16_t got = nt_log_ring_tail((uint16_t)NT_LOG_RING_DEPTH, NT_LOG_LEVEL_INFO, out);
    TEST_ASSERT_EQUAL_UINT16((uint16_t)NT_LOG_RING_DEPTH, got);

    /* Newest is the last written; oldest survivor is entry #5 (0..4 evicted). */
    char expect_newest[32];
    char expect_oldest[32];
    (void)snprintf(expect_newest, sizeof(expect_newest), "entry %d", total - 1);
    (void)snprintf(expect_oldest, sizeof(expect_oldest), "entry %d", total - NT_LOG_RING_DEPTH);
    TEST_ASSERT_EQUAL_STRING(expect_newest, out[0].msg);
    TEST_ASSERT_EQUAL_STRING(expect_oldest, out[NT_LOG_RING_DEPTH - 1].msg);
}

/* ---- Test 4: tail(n) with n > stored returns only stored; n capped at depth ---- */

static void test_ring_tail_caps_n(void) {
    nt_log_write(NT_LOG_LEVEL_INFO, "d", "only");
    nt_log_ring_entry_t out[8];

    /* n larger than stored count. */
    uint16_t got = nt_log_ring_tail(8, NT_LOG_LEVEL_INFO, out);
    TEST_ASSERT_EQUAL_UINT16(1, got);
    TEST_ASSERT_EQUAL_STRING("only", out[0].msg);

    /* n == 0 returns nothing. */
    got = nt_log_ring_tail(0, NT_LOG_LEVEL_INFO, out);
    TEST_ASSERT_EQUAL_UINT16(0, got);
}

/* ---- Test 5: level filter returns only entries >= min_level, newest-first ---- */

static void test_ring_level_filter(void) {
    nt_log_write(NT_LOG_LEVEL_INFO, "d", "i0");
    nt_log_write(NT_LOG_LEVEL_WARN, "d", "w0");
    nt_log_write(NT_LOG_LEVEL_INFO, "d", "i1");
    nt_log_write(NT_LOG_LEVEL_ERROR, "d", "e0");

    nt_log_ring_entry_t out[8];
    uint16_t got = nt_log_ring_tail(8, NT_LOG_LEVEL_WARN, out);
    TEST_ASSERT_EQUAL_UINT16(2, got); /* w0 + e0, INFO filtered out */
    TEST_ASSERT_EQUAL_STRING("e0", out[0].msg);
    TEST_ASSERT_EQUAL_STRING("w0", out[1].msg);

    /* Bounded n applies AFTER the filter. */
    got = nt_log_ring_tail(1, NT_LOG_LEVEL_WARN, out);
    TEST_ASSERT_EQUAL_UINT16(1, got);
    TEST_ASSERT_EQUAL_STRING("e0", out[0].msg);
}

/* ---- Test 6: clear empties the ring ---- */

static void test_ring_clear(void) {
    nt_log_write(NT_LOG_LEVEL_INFO, "d", "x");
    nt_log_ring_clear();
    nt_log_ring_entry_t out[4];
    TEST_ASSERT_EQUAL_UINT16(0, nt_log_ring_tail(4, NT_LOG_LEVEL_INFO, out));
}

/* ---- TST-2: oversize message truncation lands on a UTF-8 codepoint boundary (HOT-1) ----
   nt_log_write truncates a >NT_LOG_BUF_SIZE (512) message and appends "...". The stored ring line is
   serialized as a JSON string; cJSON rejects invalid UTF-8, so the marker must never orphan a split
   multibyte sequence. */

/* A NUL-terminated string is valid UTF-8 iff every multibyte sequence is complete + well-formed. We
   only need to prove the truncation tail is clean: walk the whole string and reject any sequence that
   runs past the end or carries a malformed continuation. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool is_valid_utf8(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    while (*p != 0U) {
        unsigned char c = *p;
        int extra;
        if (c < 0x80U) {
            extra = 0;
        } else if ((c & 0xE0U) == 0xC0U) {
            extra = 1;
        } else if ((c & 0xF0U) == 0xE0U) {
            extra = 2;
        } else if ((c & 0xF8U) == 0xF0U) {
            extra = 3;
        } else {
            return false; /* stray continuation byte or invalid lead */
        }
        p++;
        for (int i = 0; i < extra; i++) {
            if ((*p & 0xC0U) != 0x80U) {
                return false; /* missing/short continuation -> orphaned lead byte */
            }
            p++;
        }
    }
    return true;
}

/* ASCII overflow: a >512-byte line gets the "..." marker (truncation fires). */
static void test_truncation_marker_ascii(void) {
    char big[1024];
    memset(big, 'a', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    nt_log_write(NT_LOG_LEVEL_INFO, "d", "%s", big);

    nt_log_ring_entry_t out[2];
    uint16_t got = nt_log_ring_tail(2, NT_LOG_LEVEL_INFO, out);
    TEST_ASSERT_EQUAL_UINT16(1, got);
    size_t len = strlen(out[0].msg);
    TEST_ASSERT_TRUE(len >= 3);
    TEST_ASSERT_EQUAL_STRING("...", out[0].msg + len - 3);
}

/* Multibyte overflow: a >512-byte line of repeated "é" (0xC3 0xA9) truncates mid-sequence; the stored
   line must stay valid UTF-8 (no trailing continuation byte, no orphaned lead byte before the marker).
   FAILS against the old continuation-only walk-back, PASSES after the HOT-1 lead-byte strip. */
static void test_truncation_marker_utf8_boundary(void) {
    /* 400 * 2 bytes = 800 bytes > 512: truncation point lands inside an "é" for at least one offset. */
    char big[1024];
    size_t w = 0;
    for (int i = 0; i < 400; i++) {
        big[w++] = (char)0xC3;
        big[w++] = (char)0xA9;
    }
    big[w] = '\0';
    nt_log_write(NT_LOG_LEVEL_INFO, "d", "%s", big);

    nt_log_ring_entry_t out[2];
    uint16_t got = nt_log_ring_tail(2, NT_LOG_LEVEL_INFO, out);
    TEST_ASSERT_EQUAL_UINT16(1, got);
    TEST_ASSERT_TRUE_MESSAGE(is_valid_utf8(out[0].msg), "truncated UTF-8 log line is invalid (orphaned lead/continuation byte)");
    /* Marker still present. */
    size_t len = strlen(out[0].msg);
    TEST_ASSERT_TRUE(len >= 3);
    TEST_ASSERT_EQUAL_STRING("...", out[0].msg + len - 3);
}

/* ---- Test 7: nt_log_add_sink overflow is a host-call assert (4-sink boundary) ----
 * The ring sink already consumed one slot; fill the rest, then a final add asserts. */

static void noop_sink(nt_log_level_t level, const char *domain, const char *msg, void *user) {
    (void)level;
    (void)domain;
    (void)msg;
    (void)user;
}

static void test_add_sink_overflow_asserts(void) {
    /* One slot is taken by the ring sink (setUp). Fill remaining slots to the cap. */
    for (int i = 1; i < NT_LOG_MAX_SINKS; i++) {
        nt_log_add_sink(noop_sink, NULL);
    }
    /* The next add exceeds NT_LOG_MAX_SINKS — host-call invariant fires. */
    EXPECT_ASSERT(nt_log_add_sink(noop_sink, NULL));
}

int main(void) {
    (void)setvbuf(stdout, NULL, _IONBF, 0);
    UNITY_BEGIN();
    RUN_TEST(test_ring_captures_writes);
    RUN_TEST(test_ring_copies_domain_and_msg);
    RUN_TEST(test_ring_overflow_caps);
    RUN_TEST(test_ring_tail_caps_n);
    RUN_TEST(test_ring_level_filter);
    RUN_TEST(test_ring_clear);
    RUN_TEST(test_truncation_marker_ascii);
    RUN_TEST(test_truncation_marker_utf8_boundary);
    RUN_TEST(test_add_sink_overflow_asserts); /* keep LAST: permanently fills the sink registry */
    return UNITY_END();
}

#else /* NT_LOG_RING_ENABLED == 0 — OFF/release mirror: prove the no-op stub links + reads empty */

void setUp(void) {}
void tearDown(void) {}

static void test_ring_offmirror_noop(void) {
    nt_log_ring_init();
    nt_log_ring_sink(NT_LOG_LEVEL_ERROR, "d", "ignored", NULL);
    nt_log_ring_entry_t out[2];
    TEST_ASSERT_EQUAL_UINT16(0, nt_log_ring_tail(2, NT_LOG_LEVEL_INFO, out));
    nt_log_ring_clear();
}

int main(void) {
    (void)setvbuf(stdout, NULL, _IONBF, 0);
    UNITY_BEGIN();
    RUN_TEST(test_ring_offmirror_noop);
    return UNITY_END();
}

#endif /* NT_LOG_RING_ENABLED */
