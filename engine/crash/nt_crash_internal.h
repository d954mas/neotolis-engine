#ifndef NT_CRASH_INTERNAL_H
#define NT_CRASH_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

/* Tail of WARN/ERROR lines kept for the record. A game sets a larger size with
   target_compile_definitions(nt_crash PRIVATE NT_CRASH_LOG_BYTES=...). */
#ifndef NT_CRASH_LOG_BYTES
#define NT_CRASH_LOG_BYTES 8192
#endif
_Static_assert(NT_CRASH_LOG_BYTES > 0, "NT_CRASH_LOG_BYTES must be positive");

#define NT_CRASH_TEXT_MAX 256

/* pos and full are uint32_t so the web capture reads them as HEAPU32 words. */
typedef struct {
    char ring[NT_CRASH_LOG_BYTES];
    uint32_t pos;  /* next write offset */
    uint32_t full; /* the ring has wrapped at least once */
    char name[NT_CRASH_TEXT_MAX];
    char game_version[NT_CRASH_TEXT_MAX];
    bool initialized;
} nt_crash_state_t;

extern nt_crash_state_t g_nt_crash;

/* Platform backend (web/ or native/). */
void nt_crash_platform_init(void);
uint32_t nt_crash_platform_load(char *out, uint32_t cap);
void nt_crash_platform_clear(void);

#endif // NT_CRASH_INTERNAL_H
