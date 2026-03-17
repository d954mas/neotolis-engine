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

// NOLINTNEXTLINE(readability-function-cognitive-complexity,clang-analyzer-core.UndefinedBinaryOperatorResult)
char *nt_builder_normalize_path(const char *path) {
    if (!path) {
        return NULL;
    }
    size_t len = strlen(path);
    char *buf = (char *)malloc(len + 1);
    if (!buf) {
        return NULL;
    }

    /* Step 1: copy with backslash -> forward slash */
    for (size_t i = 0; i <= len; i++) {
        buf[i] = (char)((path[i] == '\\') ? '/' : path[i]);
    }

    /* Step 2: canonicalize in-place using a segment stack approach */
    /* Split by '/', resolve '.', '..', collapse '//' */
    char *segments[256];
    uint32_t seg_count = 0;
    char *tok = buf;

    for (;;) {
        while (*tok == '/') {
            tok++;
        }
        if (*tok == '\0') {
            break;
        }

        char *seg_start = tok;
        while (*tok != '/' && *tok != '\0') {
            tok++;
        }

        /* Compute length, then null-terminate */
        size_t seg_len = (size_t)(tok - seg_start);
        if (*tok == '/') {
            *tok = '\0';
            tok++;
        }

        if (seg_len == 1 && seg_start[0] == '.') {
            /* skip '.' */
        } else if (seg_len == 2 && seg_start[0] == '.' && seg_start[1] == '.') {
            if (seg_count > 0 && !(segments[seg_count - 1][0] == '.' && segments[seg_count - 1][1] == '.' && segments[seg_count - 1][2] == '\0')) {
                seg_count--;
            } else if (seg_count < 256) {
                segments[seg_count++] = seg_start;
            }
        } else if (seg_count < 256) {
            segments[seg_count++] = seg_start;
        }
    }

    /* Step 3: reconstruct from segments */
    if (seg_count == 0) {
        buf[0] = '.';
        buf[1] = '\0';
        return buf;
    }

    char *wp = buf;
    for (uint32_t i = 0; i < seg_count; i++) {
        if (i > 0) {
            *wp++ = '/';
        }
        size_t slen = strlen(segments[i]);
        if (segments[i] != wp) {
            memmove(wp, segments[i], slen);
        }
        wp += slen;
    }
    *wp = '\0';
    return buf;
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

/* --- File I/O --- */

char *nt_builder_read_file(const char *path, uint32_t *out_size) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        (void)fclose(file);
        return NULL;
    }
    long file_size = ftell(file);
    if (file_size < 0) {
        (void)fclose(file);
        return NULL;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        (void)fclose(file);
        return NULL;
    }

    char *buf = (char *)malloc((size_t)file_size + 1);
    if (!buf) {
        (void)fclose(file);
        return NULL;
    }

    if (file_size > 0) {
        size_t read_count = fread(buf, 1, (size_t)file_size, file);
        if (read_count != (size_t)file_size) {
            free(buf);
            (void)fclose(file);
            return NULL;
        }
    }

    buf[file_size] = '\0';
    (void)fclose(file);
    *out_size = (uint32_t)file_size;
    return buf;
}

/* --- Float16 conversion --- */

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
