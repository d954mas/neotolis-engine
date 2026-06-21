/* Native process-RSS probe behind nt_platform_memory_usage().
 * One branch per OS; all OS calls live here so no module touches the syscall. */
#include "core/nt_assert.h"
#include "core/nt_platform.h"

#if defined(NT_PLATFORM_WIN)
#include <windows.h>
/* psapi.h after windows.h: it depends on windows.h types. */
#include <psapi.h>

nt_platform_mem_t nt_platform_memory_usage(void) {
    nt_platform_mem_t mem = {0};
    PROCESS_MEMORY_COUNTERS pmc;
    /* Dev-only probe: a syscall failure is a bug, not "0 bytes used". Fail early (AGENTS.md). */
    BOOL ok = GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
    NT_ASSERT(ok && "GetProcessMemoryInfo failed");
    (void)ok;
    mem.used = (size_t)pmc.WorkingSetSize;
    mem.reserved = mem.used;
    return mem;
}

#elif defined(__linux__)
#include <stdio.h>
#include <unistd.h>

nt_platform_mem_t nt_platform_memory_usage(void) {
    nt_platform_mem_t mem = {0};
    /* statm field 2 = resident pages; × page size = resident bytes. Dev-only probe: open/parse
       failure is a bug, not "0 bytes" — fail early (AGENTS.md). */
    FILE *f = fopen("/proc/self/statm", "r");
    NT_ASSERT(f != NULL && "fopen(/proc/self/statm) failed");
    long pages_total = 0;
    long pages_rss = 0;
    int parsed = fscanf(f, "%ld %ld", &pages_total, &pages_rss);
    fclose(f);
    NT_ASSERT(parsed == 2 && "parsing /proc/self/statm failed");
    (void)parsed;
    long page = sysconf(_SC_PAGESIZE);
    NT_ASSERT(page > 0 && pages_rss >= 0 && "bad page size / rss from /proc/self/statm");
    mem.used = (size_t)pages_rss * (size_t)page;
    mem.reserved = mem.used;
    return mem;
}

#elif defined(__APPLE__)
#include <mach/mach.h>

nt_platform_mem_t nt_platform_memory_usage(void) {
    nt_platform_mem_t mem = {0};
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    /* Dev-only probe: a task_info failure is a bug, not "0 bytes" — fail early (AGENTS.md). */
    kern_return_t kr = task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count);
    NT_ASSERT(kr == KERN_SUCCESS && "task_info(MACH_TASK_BASIC_INFO) failed");
    (void)kr;
    mem.used = (size_t)info.resident_size;
    mem.reserved = mem.used;
    return mem;
}

#else
nt_platform_mem_t nt_platform_memory_usage(void) {
    nt_platform_mem_t mem = {0};
    return mem;
}
#endif
