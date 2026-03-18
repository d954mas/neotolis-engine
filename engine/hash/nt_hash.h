#ifndef NT_HASH_H
#define NT_HASH_H

#include <stdint.h>
#include <string.h>

#include "core/nt_types.h"

/* ---- Compile-time label toggle (default off) ---- */

#ifndef NT_HASH_LABELS
#define NT_HASH_LABELS 0
#endif

#ifndef NT_HASH_MAX_LABELS
#define NT_HASH_MAX_LABELS 4096
#endif

/* ---- Typed hash wrappers ---- */

typedef struct {
    uint32_t value;
} nt_hash32_t;

typedef struct {
    uint64_t value;
} nt_hash64_t;

/* ---- Module lifecycle ---- */

typedef struct {
    uint8_t _reserved;
} nt_hash_desc_t;

nt_result_t nt_hash_init(const nt_hash_desc_t *desc);
void nt_hash_shutdown(void);

/* ---- Hash functions ---- */

nt_hash32_t nt_hash32(const void *data, uint32_t size);
nt_hash64_t nt_hash64(const void *data, uint32_t size);

/* ---- Label API ---- */

void nt_hash_register_label32(nt_hash32_t hash, const char *label);
void nt_hash_register_label64(nt_hash64_t hash, const char *label);
const char *nt_hash32_label(nt_hash32_t hash);
const char *nt_hash64_label(nt_hash64_t hash);

/* ---- Inline string helpers ---- */

/* FNV-1a offset basis constants (needed for NULL input return) */
#define NT_FNV1A32_OFFSET 0x811C9DC5U
#define NT_FNV1A64_OFFSET 0xCBF29CE484222325ULL

static inline nt_hash32_t nt_hash32_str(const char *s) {
    if (!s) {
        return (nt_hash32_t){NT_FNV1A32_OFFSET};
    }
    nt_hash32_t h = nt_hash32((const void *)s, (uint32_t)strlen(s));
#if NT_HASH_LABELS
    nt_hash_register_label32(h, s);
#endif
    return h;
}

static inline nt_hash64_t nt_hash64_str(const char *s) {
    if (!s) {
        return (nt_hash64_t){NT_FNV1A64_OFFSET};
    }
    nt_hash64_t h = nt_hash64((const void *)s, (uint32_t)strlen(s));
#if NT_HASH_LABELS
    nt_hash_register_label64(h, s);
#endif
    return h;
}

#endif /* NT_HASH_H */
