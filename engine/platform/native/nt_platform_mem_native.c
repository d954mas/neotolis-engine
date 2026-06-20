/* Native process-RSS probe behind nt_platform_memory_usage().
 * One branch per OS; all OS calls live here so no module touches the syscall. */
#include "core/nt_platform.h"

#if defined(NT_PLATFORM_WIN)
#include <windows.h>
/* psapi.h after windows.h: it depends on windows.h types. */
#include <psapi.h>

nt_platform_mem_t nt_platform_memory_usage(void) {
    nt_platform_mem_t mem = {0};
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        mem.used = (size_t)pmc.WorkingSetSize;
    }
    mem.reserved = mem.used;
    return mem;
}

#elif defined(__linux__)
#include <stdio.h>
#include <unistd.h>

nt_platform_mem_t nt_platform_memory_usage(void) {
    nt_platform_mem_t mem = {0};
    /* statm field 2 = resident pages; × page size = resident bytes. */
    FILE *f = fopen("/proc/self/statm", "r");
    if (f != NULL) {
        long pages_total = 0;
        long pages_rss = 0;
        if (fscanf(f, "%ld %ld", &pages_total, &pages_rss) == 2) {
            long page = sysconf(_SC_PAGESIZE);
            if (page > 0 && pages_rss > 0) {
                mem.used = (size_t)pages_rss * (size_t)page;
            }
        }
        fclose(f);
    }
    mem.reserved = mem.used;
    return mem;
}

#elif defined(__APPLE__)
#include <mach/mach.h>

nt_platform_mem_t nt_platform_memory_usage(void) {
    nt_platform_mem_t mem = {0};
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) == KERN_SUCCESS) {
        mem.used = (size_t)info.resident_size;
    }
    mem.reserved = mem.used;
    return mem;
}

#else
nt_platform_mem_t nt_platform_memory_usage(void) {
    nt_platform_mem_t mem = {0};
    return mem;
}
#endif
