/* Crash record, native. `<mode> <path>` fills the log ring, then crashes (trap / abort / segv);
 * ctest expects that run to fail. `check <mode> <path>` then reads the stored record. */
#include "crash/nt_crash.h"
#include "graphics/nt_gfx.h"
#include "log/nt_log.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NT_TEST_CRASH_GPU_INFO
#error "NT_TEST_CRASH_GPU_INFO must be defined by the test target"
#endif

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

#if NT_TEST_CRASH_GPU_INFO
static nt_gfx_device_info_t s_device_info;

const nt_gfx_device_info_t *nt_gfx_device_info(void) { return &s_device_info; }
#endif

static void set_game_context(const char *mode, char text[1100]) {
    memset(text, 'x', 1100);
    memcpy(text + 1016, "\xf0\x9f\x8c\xb2\xc3\xa9\xf0\x9f\x8c\xb3tail", 14);
    text[1030] = '\0';
    nt_crash_set_context("menu");
    nt_crash_set_context(text);
    if (strcmp(mode, "trap") == 0) {
        const char context[] = "level=canal\r\nlog:\nreason: custom; lives=7";
        memcpy(text, context, sizeof(context));
        nt_crash_set_context(text);
    } else if (strcmp(mode, "segv") == 0) {
        nt_crash_set_context("");
    }
}

static int crash(const char *mode, const char *path) {
#if defined(_WIN32)
    /* No WER dialog or report collection in the test run; the path under test stays the same. */
    (void)SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    (void)_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    (void)remove(path);
    const nt_gfx_device_info_t *device_info = nt_gfx_device_info();
    CHECK(device_info != NULL);
    CHECK(device_info->vendor[0] == '\0' && device_info->renderer[0] == '\0' && device_info->api_version[0] == '\0');
    nt_crash_init(path, "test_crash_record");
#if NT_TEST_CRASH_GPU_INFO
    /* A later gfx initialization must be visible through the pointer cached by crash init. */
    s_device_info = (nt_gfx_device_info_t){.vendor = "Field Vendor 17", .renderer = "Field Renderer 829", .api_version = "OpenGL 3.3 field-driver-41"};
#endif
    CHECK(nt_gfx_device_info() == device_info);
    char context_source[1100];
    set_game_context(mode, context_source);
    /* Preserve the ownership check even when NONE removes every later log call. */
    volatile char *context_bytes = context_source;
    for (size_t i = 0; i < sizeof(context_source) - 1U; i++) {
        context_bytes[i] = '?';
    }
    context_bytes[sizeof(context_source) - 1U] = '\0';
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
#if NT_TEST_CRASH_GPU_INFO
    s_device_info = (nt_gfx_device_info_t){.vendor = "Reader Vendor 92", .renderer = "Reader Renderer 304", .api_version = "OpenGL 4.6 reader-driver-58"};
#endif
    nt_crash_init(path, "test_crash_record");
    nt_crash_set_context("reader context must not replace the previous run");
    uint32_t n = nt_crash_load_previous(s_record, (uint32_t)sizeof(s_record));
    CHECK(n > 0 && n == strlen(s_record));
    (void)printf("%s\n", s_record);

    const char *head = "game: test_crash_record\nengine: ";
    CHECK(strncmp(s_record, head, strlen(head)) == 0);
    CHECK(strstr(s_record, "\ntime: ") != NULL);
#if NT_TEST_CRASH_GPU_INFO
    CHECK(strstr(s_record, "\ngpu_vendor: Field Vendor 17\ngpu_renderer: Field Renderer 829\ngpu_api: OpenGL 3.3 field-driver-41\n") != NULL);
    CHECK(strstr(s_record, "Reader") == NULL);
#else
    CHECK(strstr(s_record, "\ngpu_vendor:") == NULL);
    CHECK(strstr(s_record, "\ngpu_renderer:") == NULL);
    CHECK(strstr(s_record, "\ngpu_api:") == NULL);
#endif
    CHECK(strstr(s_record, "reader context") == NULL);
    bool is_trap = strcmp(mode, "trap") == 0;
    bool is_segv = strcmp(mode, "segv") == 0;
    if (is_trap) {
        CHECK(strstr(s_record, "\ncontext: level=canal  log: reason: custom; lives=7\ntime: ") != NULL);
        CHECK(strstr(s_record, "\nreason: custom") == NULL);
    } else if (is_segv) {
        CHECK(strstr(s_record, "\ncontext:") == NULL);
    } else {
        const char *context = strstr(s_record, "\ncontext: ");
        CHECK(context != NULL);
        context += strlen("\ncontext: ");
        const char *end = strchr(context, '\n');
        CHECK(end != NULL && end - context == 1022);
        for (size_t i = 0; i < 1016; i++) {
            CHECK(context[i] == 'x');
        }
        CHECK(memcmp(context + 1016, "\xf0\x9f\x8c\xb2\xc3\xa9\ntime: ", 13) == 0);
    }
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

    /* The log tail holds what the compile floor lets through. Up to WARN the ring wrapped: the
       first kept line is whole, line 0 is gone. The token line (ERROR) is last. */
#if NT_LOG_MIN_LEVEL <= 1
    CHECK(strstr(s_record, "\nlog:\nWARN crash probe line ") != NULL);
    CHECK(strstr(s_record, "line 0:") == NULL);
#endif
#if NT_LOG_MIN_LEVEL <= 2
    char token[64];
    (void)snprintf(token, sizeof(token), "\nERROR crash probe token-%s\n", mode);
    size_t token_len = strlen(token);
    CHECK(n >= token_len && strcmp(s_record + n - token_len, token) == 0);
#else
    CHECK(n >= 6 && strcmp(s_record + n - 6, "\nlog:\n") == 0);
#endif

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
