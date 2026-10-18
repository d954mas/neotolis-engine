/* Crash record, native. `<mode> <path>` fills the log ring, then crashes (trap / abort / segv);
 * ctest expects that run to fail. `check <mode> <path>` then reads the stored record. */
#include "crash/nt_crash.h"
#include "log/nt_log.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define CHECK(cond)                                                                                                                                                                                    \
    do {                                                                                                                                                                                               \
        if (!(cond)) {                                                                                                                                                                                 \
            (void)fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);                                                                                                             \
            return 1;                                                                                                                                                                                  \
        }                                                                                                                                                                                              \
    } while (0)

static char s_record[65536];
static volatile int *volatile s_null; /* opaque to the optimizer: a real SIGSEGV, not a trap */

static int crash(const char *mode, const char *path) {
#if defined(_WIN32)
    /* No WER dialog or report collection in the test run; the path under test stays the same. */
    (void)SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    (void)_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    (void)remove(path);
    nt_crash_init(path, "test_crash_record");
    for (int i = 0; i < 300; i++) {
        nt_log_warn("crash probe line %d: проверка кольца", i);
    }
    nt_log_error("crash probe token-%s", mode);
    if (strcmp(mode, "trap") == 0) {
        __builtin_trap();
    }
    if (strcmp(mode, "abort") == 0) {
        abort();
    }
    if (strcmp(mode, "segv") == 0) {
        *s_null = 1;
    }
    return 0; /* an unknown mode exits 0, which the WILL_FAIL step reports */
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- CHECK expansions
static int check(const char *mode, const char *path) {
    nt_crash_init(path, "test_crash_record");
    uint32_t n = nt_crash_load_previous(s_record, (uint32_t)sizeof(s_record));
    CHECK(n > 0 && n == strlen(s_record));
    (void)printf("%s\n", s_record);

    const char *head = "game: test_crash_record\nengine: ";
    CHECK(strncmp(s_record, head, strlen(head)) == 0);
    CHECK(strstr(s_record, "\ntime: ") != NULL);
    bool is_trap = strcmp(mode, "trap") == 0;
    bool is_segv = strcmp(mode, "segv") == 0;
#if defined(_WIN32)
    if (is_trap) {
        CHECK(strstr(s_record, "\nreason: exception 0xc000001d\npc: exe+0x") != NULL);
    } else if (is_segv) {
        CHECK(strstr(s_record, "\nreason: exception 0xc0000005\npc: exe+0x") != NULL);
    } else {
        CHECK(strstr(s_record, "\nreason: abort\nlog:\n") != NULL);
    }
#else
    if (is_trap) {
        CHECK(strstr(s_record, "\nreason: signal SIGILL\npc: exe+0x") != NULL || strstr(s_record, "\nreason: signal SIGTRAP\npc: exe+0x") != NULL);
    } else if (is_segv) {
        CHECK(strstr(s_record, "\nreason: signal SIGSEGV\npc: exe+0x") != NULL);
    } else {
        CHECK(strstr(s_record, "\nreason: signal SIGABRT\npc: ") != NULL);
    }
#endif

    /* The ring wrapped: the first kept line is whole, line 0 is gone, the token line is last. */
    const char *log = strstr(s_record, "\nlog:\nWARN crash probe line ");
    CHECK(log != NULL);
    CHECK(strstr(s_record, "line 0:") == NULL);
    char token[64];
    (void)snprintf(token, sizeof(token), "\nERROR crash probe token-%s\n", mode);
    size_t token_len = strlen(token);
    CHECK(n >= token_len && strcmp(s_record + n - token_len, token) == 0);

    char small[16];
    CHECK(nt_crash_load_previous(small, (uint32_t)sizeof(small)) == sizeof(small) - 1U);
    CHECK(strncmp(small, s_record, sizeof(small) - 1U) == 0);

    nt_crash_clear_previous();
    CHECK(nt_crash_load_previous(s_record, (uint32_t)sizeof(s_record)) == 0 && s_record[0] == '\0');
    return 0;
}

/* Under ASan nt_crash installs no handlers (ASan reports instead): skip both steps. */
#if defined(__SANITIZE_ADDRESS__)
#define SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define SANITIZED 1
#endif
#endif

int main(int argc, char *argv[]) {
#ifdef SANITIZED
    (void)argc;
    (void)argv;
    return 77;
#endif
    if (argc == 4 && strcmp(argv[1], "check") == 0) {
        return check(argv[2], argv[3]);
    }
    if (argc == 3) {
        return crash(argv[1], argv[2]);
    }
    (void)fprintf(stderr, "usage: test_crash_record <trap|abort|segv> <path> | check <mode> <path>\n");
    return 2;
}
