#include "nt_builder_internal.h"

/*
 * FNV-1a 32-bit hash.
 * Offset basis: 0x811C9DC5, Prime: 0x01000193
 * Standard algorithm for fast, well-distributed string hashing.
 */

#define FNV1A_OFFSET_BASIS 0x811C9DC5U
#define FNV1A_PRIME 0x01000193U

uint32_t nt_builder_fnv1a(const char *str) {
    if (!str) {
        return FNV1A_OFFSET_BASIS;
    }
    uint32_t hash = FNV1A_OFFSET_BASIS;
    for (const char *p = str; *p != '\0'; p++) {
        hash ^= (uint8_t)*p;
        hash *= FNV1A_PRIME;
    }
    return hash;
}

char *nt_builder_normalize_path(const char *path) {
    if (!path) {
        return NULL;
    }
    size_t len = strlen(path);
    char *normalized = (char *)malloc(len + 1);
    if (!normalized) {
        return NULL;
    }
    for (size_t i = 0; i <= len; i++) {
        normalized[i] = (char)((path[i] == '\\') ? '/' : path[i]);
    }
    return normalized;
}

uint32_t nt_builder_hash(const char *str) {
    char *normalized = nt_builder_normalize_path(str);
    if (!normalized) {
        return 0;
    }
    uint32_t hash = nt_builder_fnv1a(normalized);
    free(normalized);
    return hash;
}

uint16_t nt_builder_float32_to_float16(float value) {
    union {
        float f;
        uint32_t u;
    } conv;
    conv.f = value;

    uint32_t sign = (conv.u >> 16) & 0x8000U;
    int32_t exponent = (int32_t)((conv.u >> 23) & 0xFFU) - 127 + 15;
    uint32_t mantissa = conv.u & 0x007FFFFFU;

    if (exponent <= 0) {
        /* Underflow: clamp to zero */
        return (uint16_t)sign;
    }
    if (exponent >= 31) {
        /* Overflow: clamp to max finite value */
        return (uint16_t)(sign | 0x7BFFU);
    }
    return (uint16_t)(sign | ((uint32_t)exponent << 10) | (mantissa >> 13));
}
