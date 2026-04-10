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

#include <stdatomic.h>

/* Spinlock for thread-safe label registration. Multiple builder worker threads
 * call nt_hash64_str → nt_hash_register_label64 concurrently. A spinlock is
 * acceptable: labels are debug-only, contention is rare, critical sections are
 * short (hash probe + strncpy).
 *
 * Tables grow dynamically. On resize the old table is NOT freed — it moves to
 * a retired list so that pointers previously returned by nt_hash*_label()
 * remain valid until nt_hash_shutdown(). This eliminates the use-after-free
 * that would occur if another thread held a pointer into a freed table. */
static atomic_flag s_label_lock = ATOMIC_FLAG_INIT;

static void label_lock(void) {
    while (atomic_flag_test_and_set_explicit(&s_label_lock, memory_order_acquire)) {
        /* spin */
    }
}

static void label_unlock(void) { atomic_flag_clear_explicit(&s_label_lock, memory_order_release); }

typedef struct {
    uint32_t hash_value;
    char label[96];
} LabelEntry32;

typedef struct {
    uint64_t hash_value;
    char label[96];
} LabelEntry64;

/* Retired table node — keeps old tables alive so outstanding pointers don't dangle. */
typedef struct RetiredTable {
    void *table;
    struct RetiredTable *next;
} RetiredTable;

static LabelEntry32 *s_labels32;
static LabelEntry64 *s_labels64;
static uint32_t s_labels32_capacity;
static uint32_t s_labels64_capacity;
static uint32_t s_labels32_count;
static uint32_t s_labels64_count;
static RetiredTable *s_retired;
static bool s_initialized;

static uint32_t label_initial_capacity(void) {
    uint32_t initial = (uint32_t)NT_HASH_MAX_LABELS;
    if (initial < 16U) {
        initial = 16U;
    }
    return initial * 2U;
}

static uint32_t label_capacity_for_count(uint32_t count, uint32_t current_capacity) {
    uint32_t capacity = current_capacity > 0 ? current_capacity : label_initial_capacity();
    while (count > (capacity / 2U)) {
        NT_ASSERT(capacity <= (UINT32_MAX / 2U));
        capacity *= 2U;
    }
    return capacity;
}

static void retire_table(void *old_table) {
    RetiredTable *node = (RetiredTable *)malloc(sizeof(RetiredTable));
    NT_ASSERT(node && "retire_table: alloc failed");
    node->table = old_table;
    node->next = s_retired;
    s_retired = node;
}

static void label32_store(LabelEntry32 *table, uint32_t capacity, nt_hash32_t hash, const char *label) {
    uint32_t idx = hash.value % capacity;
    for (uint32_t i = 0; i < capacity; i++) {
        uint32_t probe = (idx + i) % capacity;
        if (table[probe].label[0] == '\0' || table[probe].hash_value == hash.value) {
            table[probe].hash_value = hash.value;
            strncpy(table[probe].label, label, 95);
            table[probe].label[95] = '\0';
            return;
        }
    }
    NT_ASSERT(0 && "hash label32 table unexpectedly full");
}

static void label64_store(LabelEntry64 *table, uint32_t capacity, nt_hash64_t hash, const char *label) {
    uint32_t idx = (uint32_t)(hash.value % (uint64_t)capacity);
    for (uint32_t i = 0; i < capacity; i++) {
        uint32_t probe = (idx + i) % capacity;
        if (table[probe].label[0] == '\0' || table[probe].hash_value == hash.value) {
            table[probe].hash_value = hash.value;
            strncpy(table[probe].label, label, 95);
            table[probe].label[95] = '\0';
            return;
        }
    }
    NT_ASSERT(0 && "hash label64 table unexpectedly full");
}

static bool label32_ensure_capacity(uint32_t needed_count) {
    uint32_t new_capacity = label_capacity_for_count(needed_count, s_labels32_capacity);
    if (new_capacity == s_labels32_capacity && s_labels32 != NULL) {
        return true;
    }
    LabelEntry32 *new_table = (LabelEntry32 *)calloc(new_capacity, sizeof(LabelEntry32));
    if (!new_table) {
        return false;
    }
    if (s_labels32) {
        for (uint32_t i = 0; i < s_labels32_capacity; i++) {
            if (s_labels32[i].label[0] != '\0') {
                label32_store(new_table, new_capacity, (nt_hash32_t){s_labels32[i].hash_value}, s_labels32[i].label);
            }
        }
        retire_table(s_labels32);
    }
    s_labels32 = new_table;
    s_labels32_capacity = new_capacity;
    return true;
}

static bool label64_ensure_capacity(uint32_t needed_count) {
    uint32_t new_capacity = label_capacity_for_count(needed_count, s_labels64_capacity);
    if (new_capacity == s_labels64_capacity && s_labels64 != NULL) {
        return true;
    }
    LabelEntry64 *new_table = (LabelEntry64 *)calloc(new_capacity, sizeof(LabelEntry64));
    if (!new_table) {
        return false;
    }
    if (s_labels64) {
        for (uint32_t i = 0; i < s_labels64_capacity; i++) {
            if (s_labels64[i].label[0] != '\0') {
                label64_store(new_table, new_capacity, (nt_hash64_t){s_labels64[i].hash_value}, s_labels64[i].label);
            }
        }
        retire_table(s_labels64);
    }
    s_labels64 = new_table;
    s_labels64_capacity = new_capacity;
    return true;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_hash_register_label32(nt_hash32_t hash, const char *label) {
    NT_ASSERT(label != NULL);
    label_lock();
    if (s_labels32) {
        uint32_t idx = hash.value % s_labels32_capacity;
        for (uint32_t i = 0; i < s_labels32_capacity; i++) {
            uint32_t probe = (idx + i) % s_labels32_capacity;
            if (s_labels32[probe].label[0] == '\0') {
                break;
            }
            if (s_labels32[probe].hash_value == hash.value) {
                label32_store(s_labels32, s_labels32_capacity, hash, label);
                label_unlock();
                return;
            }
        }
    }
    NT_ASSERT(label32_ensure_capacity(s_labels32_count + 1U) && "hash label32 table grow failed");
    label32_store(s_labels32, s_labels32_capacity, hash, label);
    s_labels32_count++;
    label_unlock();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_hash_register_label64(nt_hash64_t hash, const char *label) {
    NT_ASSERT(label != NULL);
    label_lock();
    if (s_labels64) {
        uint32_t idx = (uint32_t)(hash.value % (uint64_t)s_labels64_capacity);
        for (uint32_t i = 0; i < s_labels64_capacity; i++) {
            uint32_t probe = (idx + i) % s_labels64_capacity;
            if (s_labels64[probe].label[0] == '\0') {
                break;
            }
            if (s_labels64[probe].hash_value == hash.value) {
                label64_store(s_labels64, s_labels64_capacity, hash, label);
                label_unlock();
                return;
            }
        }
    }
    NT_ASSERT(label64_ensure_capacity(s_labels64_count + 1U) && "hash label64 table grow failed");
    label64_store(s_labels64, s_labels64_capacity, hash, label);
    s_labels64_count++;
    label_unlock();
}

const char *nt_hash32_label(nt_hash32_t hash) {
    label_lock();
    if (!s_labels32 || s_labels32_capacity == 0) {
        label_unlock();
        return NULL;
    }
    uint32_t idx = hash.value % s_labels32_capacity;
    for (uint32_t i = 0; i < s_labels32_capacity; i++) {
        uint32_t probe = (idx + i) % s_labels32_capacity;
        if (s_labels32[probe].label[0] == '\0') {
            label_unlock();
            return NULL;
        }
        if (s_labels32[probe].hash_value == hash.value) {
            const char *result = s_labels32[probe].label;
            label_unlock();
            return result;
        }
    }
    label_unlock();
    return NULL;
}

const char *nt_hash64_label(nt_hash64_t hash) {
    label_lock();
    if (!s_labels64 || s_labels64_capacity == 0) {
        label_unlock();
        return NULL;
    }
    uint32_t idx = (uint32_t)(hash.value % (uint64_t)s_labels64_capacity);
    for (uint32_t i = 0; i < s_labels64_capacity; i++) {
        uint32_t probe = (idx + i) % s_labels64_capacity;
        if (s_labels64[probe].label[0] == '\0') {
            label_unlock();
            return NULL;
        }
        if (s_labels64[probe].hash_value == hash.value) {
            const char *result = s_labels64[probe].label;
            label_unlock();
            return result;
        }
    }
    label_unlock();
    return NULL;
}

nt_result_t nt_hash_init(const nt_hash_desc_t *desc) {
    (void)desc;
    if (s_initialized) {
        return NT_ERR_INIT_FAILED;
    }
    s_initialized = true;
    return NT_OK;
}

void nt_hash_shutdown(void) {
    label_lock();
    free(s_labels32);
    free(s_labels64);
    s_labels32 = NULL;
    s_labels64 = NULL;
    s_labels32_capacity = 0;
    s_labels64_capacity = 0;
    s_labels32_count = 0;
    s_labels64_count = 0;
    while (s_retired) {
        RetiredTable *next = s_retired->next;
        free(s_retired->table);
        free(s_retired);
        s_retired = next;
    }
    s_initialized = false;
    label_unlock();
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
