#if !defined(_WIN32)
#define _GNU_SOURCE /* REG_RIP, dl_iterate_phdr */
#endif

#include "core/nt_assert.h"
#include "core/nt_core.h"
#include "crash/nt_crash_internal.h"
#include "version/nt_version.h"

#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The handlers run inside a crashed process: only async-signal-safe calls, no heap, no stdio.
   They write the record and return, so the OS default (WER, core dump) still follows. */

#if defined(__SANITIZE_ADDRESS__)
#define NT_CRASH_HANDLERS 0
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define NT_CRASH_HANDLERS 0
#endif
#endif
#ifndef NT_CRASH_HANDLERS
#define NT_CRASH_HANDLERS 1 /* under ASan its own report replaces ours */
#endif

static uintptr_t s_exe_lo, s_exe_hi, s_exe_base;
static const char *s_engine; /* cached in init: handlers call no external functions */
static const char *s_preset;

static void out(const char *s, size_t n);

static void out_str(const char *s) { out(s, strlen(s)); }

static void out_hex(uint64_t v) {
    char buf[16];
    int n = 0;
    do {
        buf[sizeof(buf) - 1 - (size_t)n++] = "0123456789abcdef"[v & 0xFU];
        v >>= 4;
    } while (v != 0);
    out(buf + sizeof(buf) - n, (size_t)n);
}

static void out_dec(uint64_t v) {
    char buf[20];
    int n = 0;
    do {
        buf[sizeof(buf) - 1 - (size_t)n++] = (char)('0' + (v % 10U));
        v /= 10U;
    } while (v != 0);
    out(buf + sizeof(buf) - n, (size_t)n);
}

/* Oldest complete line to the last newline; a wrapped ring drops its partial first line. */
static void out_log_tail(void) {
    uint32_t start = g_nt_crash.full ? g_nt_crash.pos : 0;
    uint32_t len = g_nt_crash.full ? NT_CRASH_LOG_BYTES : g_nt_crash.pos;
    uint32_t first = 0;
    if (g_nt_crash.full) {
        while (first < len && g_nt_crash.ring[(start + first) % NT_CRASH_LOG_BYTES] != '\n') {
            first++;
        }
        first++;
    }
    uint32_t end = len;
    while (end > first && g_nt_crash.ring[(start + end - 1) % NT_CRASH_LOG_BYTES] != '\n') {
        end--;
    }
    if (end <= first) {
        return;
    }
    uint32_t a = (start + first) % NT_CRASH_LOG_BYTES;
    uint32_t n = end - first;
    uint32_t chunk = NT_CRASH_LOG_BYTES - a < n ? NT_CRASH_LOG_BYTES - a : n;
    out(g_nt_crash.ring + a, chunk);
    out(g_nt_crash.ring, n - chunk);
}

static void out_record(const char *reason, uint64_t code, bool has_code, uintptr_t pc, bool has_pc, uint64_t time_ms) {
    out_str("game: ");
    out_str(g_nt_crash.game_version);
    out_str("\nengine: ");
    out_str(s_engine);
    out_str(" ");
    out_str(s_preset);
    out_str("\ntime: ");
    out_dec(time_ms);
    out_str("\nreason: ");
    out_str(reason);
    if (has_code) {
        out_str("0x");
        out_hex(code);
    }
    if (has_pc) {
        out_str("\npc: ");
        if (pc >= s_exe_lo && pc < s_exe_hi) {
            out_str("exe+0x");
            out_hex(pc - s_exe_base);
        } else {
            out_str("0x");
            out_hex(pc);
        }
    }
    out_str("\nlog:\n");
    out_log_tail();
}

#if defined(_WIN32)
// #region windows
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* The filter and the UCRT SIGABRT handler run synchronously on the crashing thread, not as
   asynchronous signals, so the Win32 calls below are allowed there. */
// NOLINTBEGIN(bugprone-signal-handler,cert-sig30-c,cert-msc54-cpp)

static wchar_t s_path[NT_CRASH_TEXT_MAX];
static HANDLE s_file = INVALID_HANDLE_VALUE;
static volatile LONG s_writing;

static void out(const char *s, size_t n) {
    while (n > 0) {
        DWORD written = 0;
        if (!WriteFile(s_file, s, (DWORD)n, &written, NULL) || written == 0) {
            return;
        }
        s += written;
        n -= written;
    }
}

static uint64_t now_ms(void) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (t / 10000U) - 11644473600000ULL; /* 100 ns since 1601 -> ms since 1970 */
}

static void write_record(const char *reason, uint64_t code, bool has_code, uintptr_t pc, bool has_pc) {
    if (InterlockedExchange(&s_writing, 1) != 0) {
        return;
    }
    s_file = CreateFileW(s_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (s_file == INVALID_HANDLE_VALUE) {
        return;
    }
    out_record(reason, code, has_code, pc, has_pc, now_ms());
    CloseHandle(s_file);
}

static LONG WINAPI on_exception(EXCEPTION_POINTERS *ep) {
    write_record("exception ", ep->ExceptionRecord->ExceptionCode, true, (uintptr_t)ep->ExceptionRecord->ExceptionAddress, true);
    return EXCEPTION_CONTINUE_SEARCH;
}

/* Returning lets UCRT abort() continue to its fault report (WER); it removes our filter first. */
static void on_abort(int sig) {
    (void)sig;
    write_record("abort", 0, false, 0, false);
}
// NOLINTEND(bugprone-signal-handler,cert-sig30-c,cert-msc54-cpp)

void nt_crash_platform_init(void) {
    s_engine = nt_version_engine();
    s_preset = nt_engine_preset_string();
    int n = MultiByteToWideChar(CP_UTF8, 0, g_nt_crash.name, -1, s_path, NT_CRASH_TEXT_MAX);
    NT_ASSERT(n > 0);
    HMODULE exe = GetModuleHandleW(NULL);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)exe;
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)((const char *)exe + dos->e_lfanew);
    s_exe_base = (uintptr_t)exe;
    s_exe_lo = s_exe_base;
    s_exe_hi = s_exe_base + nt->OptionalHeader.SizeOfImage;
#if NT_CRASH_HANDLERS
    ULONG guarantee = 32U * 1024U; /* the filter still runs on an overflowed stack */
    (void)SetThreadStackGuarantee(&guarantee);
    (void)SetUnhandledExceptionFilter(on_exception);
    (void)signal(SIGABRT, on_abort);
#endif
}

uint32_t nt_crash_platform_load(char *out_buf, uint32_t cap) {
    out_buf[0] = '\0';
    HANDLE f = CreateFileW(s_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        return 0;
    }
    uint32_t total = 0;
    DWORD got = 0;
    while (total < cap - 1U && ReadFile(f, out_buf + total, cap - 1U - total, &got, NULL) && got > 0) {
        total += got;
    }
    CloseHandle(f);
    out_buf[total] = '\0';
    return (uint32_t)strlen(out_buf);
}

void nt_crash_platform_clear(void) { (void)DeleteFileW(s_path); }
// #endregion
#else
// #region linux
#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

static int s_fd = -1;

static void out(const char *s, size_t n) {
    while (n > 0) {
        ssize_t w = write(s_fd, s, n);
        if (w < 0 && errno == EINTR) {
            continue;
        }
        if (w <= 0) {
            return;
        }
        s += w;
        n -= (size_t)w;
    }
}

static const int k_signals[] = {SIGILL, SIGSEGV, SIGBUS, SIGFPE, SIGABRT, SIGTRAP};
static const char *const k_signal_names[] = {"signal SIGILL", "signal SIGSEGV", "signal SIGBUS", "signal SIGFPE", "signal SIGABRT", "signal SIGTRAP"};
static char s_altstack[64 * 1024];

static uintptr_t fault_pc(const void *uctx) {
    const ucontext_t *uc = (const ucontext_t *)uctx;
#if defined(__x86_64__)
    return (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
#elif defined(__aarch64__)
    return (uintptr_t)uc->uc_mcontext.pc;
#else
#error "nt_crash: fault PC is implemented for x86_64 and aarch64"
#endif
}

/* The signal is blocked inside the handler, so the raise stays pending and kills the process
   under SIG_DFL (SA_RESETHAND) on return, with the original context, whatever sent it. */
static void on_signal(int sig, siginfo_t *info, void *uctx) {
    (void)info;
    const char *reason = "signal";
    for (size_t i = 0; i < sizeof(k_signals) / sizeof(k_signals[0]); i++) {
        if (k_signals[i] == sig) {
            reason = k_signal_names[i];
        }
    }
    s_fd = open(g_nt_crash.name, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (s_fd >= 0) {
        struct timespec ts;
        (void)clock_gettime(CLOCK_REALTIME, &ts);
        out_record(reason, 0, false, fault_pc(uctx), true, (uint64_t)ts.tv_sec * 1000U + (uint64_t)ts.tv_nsec / 1000000U);
        (void)close(s_fd);
    }
    (void)raise(sig);
}

static int find_exe(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size;
    (void)data;
    uintptr_t lo = UINTPTR_MAX;
    uintptr_t hi = 0;
    for (ElfW(Half) i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type == PT_LOAD) {
            uintptr_t a = (uintptr_t)(info->dlpi_addr + ph->p_vaddr);
            lo = a < lo ? a : lo;
            hi = a + ph->p_memsz > hi ? a + ph->p_memsz : hi;
        }
    }
    s_exe_base = (uintptr_t)info->dlpi_addr;
    s_exe_lo = lo;
    s_exe_hi = hi;
    return 1; /* the first entry is the executable */
}

void nt_crash_platform_init(void) {
    s_engine = nt_version_engine();
    s_preset = nt_engine_preset_string();
    (void)dl_iterate_phdr(find_exe, NULL);
#if NT_CRASH_HANDLERS
    /* Without room for the kernel's signal frame, skip the alternate stack: faults are still
       caught, only a main-thread stack overflow is not. */
    long need = sysconf(_SC_SIGSTKSZ);
    if (need <= (long)sizeof(s_altstack)) {
        stack_t ss = {.ss_sp = s_altstack, .ss_size = sizeof(s_altstack)};
        (void)sigaltstack(&ss, NULL);
    }
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = on_signal;
    sa.sa_flags = (int)(SA_SIGINFO | SA_ONSTACK | SA_RESETHAND); /* SA_RESETHAND is 0x80000000u */
    (void)sigemptyset(&sa.sa_mask);
    for (size_t i = 0; i < sizeof(k_signals) / sizeof(k_signals[0]); i++) {
        (void)sigaddset(&sa.sa_mask, k_signals[i]);
    }
    for (size_t i = 0; i < sizeof(k_signals) / sizeof(k_signals[0]); i++) {
        (void)sigaction(k_signals[i], &sa, NULL);
    }
#endif
}

uint32_t nt_crash_platform_load(char *out_buf, uint32_t cap) {
    out_buf[0] = '\0';
    int fd = open(g_nt_crash.name, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return 0;
    }
    uint32_t total = 0;
    while (total < cap - 1U) {
        ssize_t got = read(fd, out_buf + total, cap - 1U - total);
        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got <= 0) {
            break;
        }
        total += (uint32_t)got;
    }
    (void)close(fd);
    out_buf[total] = '\0';
    return (uint32_t)strlen(out_buf);
}

void nt_crash_platform_clear(void) { (void)unlink(g_nt_crash.name); }
// #endregion
#endif
