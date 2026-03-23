#include "hash/nt_hash.h"

#include "core/nt_assert.h"

#include <stdbool.h>
#include <string.h>

/* ---- xxHash official (single-header, inlined) ---- */
#define XXH_INLINE_ALL
#define XXH_NO_XXH3
#include "xxhash.h"

/* ---- Hash functions (xxHash backend) ---- */

nt_hash32_t nt_hash32(const void *data, uint32_t size) {
    NT_ASSERT(data != NULL);
    return (nt_hash32_t){XXH32(data, (size_t)size, 0)};
}

nt_hash64_t nt_hash64(const void *data, uint32_t size) {
    NT_ASSERT(data != NULL);
    return (nt_hash64_t){XXH64(data, (size_t)size, 0)};
}

/* ---- String helpers ---- */

nt_hash32_t nt_hash32_str(const char *s) {
    NT_ASSERT(s != NULL);
    nt_hash32_t h = nt_hash32((const void *)s, (uint32_t)strlen(s));
#if NT_HASH_LABELS
    nt_hash_register_label32(h, s);
#endif
    return h;
}

nt_hash64_t nt_hash64_str(const char *s) {
    NT_ASSERT(s != NULL);
    nt_hash64_t h = nt_hash64((const void *)s, (uint32_t)strlen(s));
#if NT_HASH_LABELS
    nt_hash_register_label64(h, s);
#endif
    return h;
}

/* ---- Label system ---- */

#if NT_HASH_LABELS

#define LABEL_TABLE_SIZE ((uint32_t)(NT_HASH_MAX_LABELS) * 2U)

typedef struct {
    uint32_t hash_value;
    char label[96];
} LabelEntry32;

typedef struct {
    uint64_t hash_value;
    char label[96];
} LabelEntry64;

static LabelEntry32 s_labels32[LABEL_TABLE_SIZE];
static LabelEntry64 s_labels64[LABEL_TABLE_SIZE];
static bool s_initialized;

void nt_hash_register_label32(nt_hash32_t hash, const char *label) {
    uint32_t idx = hash.value % LABEL_TABLE_SIZE;
    for (uint32_t i = 0; i < LABEL_TABLE_SIZE; i++) {
        uint32_t probe = (idx + i) % LABEL_TABLE_SIZE;
        if (s_labels32[probe].label[0] == '\0' || s_labels32[probe].hash_value == hash.value) {
            s_labels32[probe].hash_value = hash.value;
            strncpy(s_labels32[probe].label, label, 95);
            s_labels32[probe].label[95] = '\0';
            return;
        }
    }
    NT_ASSERT(0 && "hash label32 table full -- increase NT_HASH_MAX_LABELS");
}

void nt_hash_register_label64(nt_hash64_t hash, const char *label) {
    uint32_t idx = (uint32_t)(hash.value % (uint64_t)LABEL_TABLE_SIZE);
    for (uint32_t i = 0; i < LABEL_TABLE_SIZE; i++) {
        uint32_t probe = (idx + i) % LABEL_TABLE_SIZE;
        if (s_labels64[probe].label[0] == '\0' || s_labels64[probe].hash_value == hash.value) {
            s_labels64[probe].hash_value = hash.value;
            strncpy(s_labels64[probe].label, label, 95);
            s_labels64[probe].label[95] = '\0';
            return;
        }
    }
    NT_ASSERT(0 && "hash label64 table full -- increase NT_HASH_MAX_LABELS");
}

const char *nt_hash32_label(nt_hash32_t hash) {
    uint32_t idx = hash.value % LABEL_TABLE_SIZE;
    for (uint32_t i = 0; i < LABEL_TABLE_SIZE; i++) {
        uint32_t probe = (idx + i) % LABEL_TABLE_SIZE;
        if (s_labels32[probe].label[0] == '\0') {
            return NULL;
        }
        if (s_labels32[probe].hash_value == hash.value) {
            return s_labels32[probe].label;
        }
    }
    return NULL;
}

const char *nt_hash64_label(nt_hash64_t hash) {
    uint32_t idx = (uint32_t)(hash.value % (uint64_t)LABEL_TABLE_SIZE);
    for (uint32_t i = 0; i < LABEL_TABLE_SIZE; i++) {
        uint32_t probe = (idx + i) % LABEL_TABLE_SIZE;
        if (s_labels64[probe].label[0] == '\0') {
            return NULL;
        }
        if (s_labels64[probe].hash_value == hash.value) {
            return s_labels64[probe].label;
        }
    }
    return NULL;
}

nt_result_t nt_hash_init(const nt_hash_desc_t *desc) {
    (void)desc;
    if (s_initialized) {
        return NT_ERR_INIT_FAILED;
    }
    /* BSS is zero-initialized on startup; only shutdown needs memset for re-init */
    s_initialized = true;
    return NT_OK;
}

void nt_hash_shutdown(void) {
    memset(s_labels32, 0, sizeof(s_labels32));
    memset(s_labels64, 0, sizeof(s_labels64));
    s_initialized = false;
}

#else /* NT_HASH_LABELS == 0 */

void nt_hash_register_label32(nt_hash32_t hash, const char *label) {
    (void)hash;
    (void)label;
}

void nt_hash_register_label64(nt_hash64_t hash, const char *label) {
    (void)hash;
    (void)label;
}

const char *nt_hash32_label(nt_hash32_t hash) {
    (void)hash;
    return NULL;
}

const char *nt_hash64_label(nt_hash64_t hash) {
    (void)hash;
    return NULL;
}

static bool s_initialized;

nt_result_t nt_hash_init(const nt_hash_desc_t *desc) {
    (void)desc;
    if (s_initialized) {
        return NT_ERR_INIT_FAILED;
    }
    s_initialized = true;
    return NT_OK;
}

void nt_hash_shutdown(void) { s_initialized = false; }

#endif /* NT_HASH_LABELS */
