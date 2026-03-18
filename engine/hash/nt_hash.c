#include "hash/nt_hash.h"

#include <string.h>

/* ---- Stub implementation (RED phase -- tests should fail) ---- */

nt_result_t nt_hash_init(const nt_hash_desc_t *desc) {
    (void)desc;
    return NT_OK;
}

void nt_hash_shutdown(void) {}

nt_hash32_t nt_hash32(const void *data, uint32_t size) {
    (void)data;
    (void)size;
    return (nt_hash32_t){0};
}

nt_hash64_t nt_hash64(const void *data, uint32_t size) {
    (void)data;
    (void)size;
    return (nt_hash64_t){0};
}

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
