/* clang-format off */
#include "nt_builder_internal.h"
#include "hash/nt_hash.h"
/* clang-format on */

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
        while (*tok == '/') { // NOLINT(clang-analyzer-core.UndefinedBinaryOperatorResult)
            tok++;
        }
        if (*tok == '\0') {
            break;
        }

        char *seg_start = tok;
        while (*tok != '/' && *tok != '\0') { // NOLINT(clang-analyzer-core.UndefinedBinaryOperatorResult)
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

nt_hash64_t nt_builder_normalize_and_hash(const char *str) {
    char *normalized = nt_builder_normalize_path(str);
    if (!normalized) {
        return (nt_hash64_t){0};
    }
    nt_hash64_t h = nt_hash64_str(normalized);
    free(normalized);
    return h;
}

/* --- File I/O --- */

char *nt_builder_read_file_bounded(const char *path, uint32_t max_size, uint32_t *out_size, bool *out_too_large) {
    *out_size = 0;
    *out_too_large = false;
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }

#ifdef _WIN32
    if (_fseeki64(file, 0, SEEK_END) != 0) {
#else
    if (fseek(file, 0, SEEK_END) != 0) {
#endif
        (void)fclose(file);
        return NULL;
    }
#ifdef _WIN32
    int64_t file_size = _ftelli64(file);
#else
    int64_t file_size = ftell(file);
#endif
    if (file_size < 0) {
        (void)fclose(file);
        return NULL;
    }
#ifdef _WIN32
    if (_fseeki64(file, 0, SEEK_SET) != 0) {
#else
    if (fseek(file, 0, SEEK_SET) != 0) {
#endif
        (void)fclose(file);
        return NULL;
    }
    if ((uint64_t)file_size > max_size) {
        (void)fclose(file);
        *out_too_large = true;
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

char *nt_builder_read_file(const char *path, uint32_t *out_size) {
    bool too_large = false;
    return nt_builder_read_file_bounded(path, UINT32_MAX, out_size, &too_large);
}

/* --- Input log (depfile) --- */

/* One builder process is one CMake build step that may write several packs,
 * so the inputs belong to the process, not to a pack context. */
static char **s_inputs;
static uint32_t s_input_count;
static uint32_t s_input_capacity;

void nt_builder_record_input(const char *path) {
    /* CMake resolves relative depfile paths against the binary dir, not the
     * builder's working directory. */
#ifdef _WIN32
    char *abs_path = _fullpath(NULL, path, 0);
#else
    char *abs_path = realpath(path, NULL);
#endif
    NT_BUILD_ASSERT(abs_path && "record_input: cannot resolve path");
    for (char *c = abs_path; *c != '\0'; c++) {
        if (*c == '\\') {
            *c = '/';
        }
    }
    if (s_input_count == s_input_capacity) {
        uint32_t capacity = s_input_capacity ? s_input_capacity * 2U : 256U;
        char **grown = (char **)realloc((void *)s_inputs, (size_t)capacity * sizeof(char *));
        NT_BUILD_ASSERT(grown && "record_input: out of memory");
        s_inputs = grown;
        s_input_capacity = capacity;
    }
    s_inputs[s_input_count++] = abs_path;
}

static int input_strcmp(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

static void write_depfile_path(FILE *file, const char *path) {
    for (const char *c = path; *c != '\0'; c++) {
        if (*c == ' ' || *c == '#') {
            (void)fputc('\\', file);
        } else if (*c == '$') {
            (void)fputc('$', file);
        }
        (void)fputc(*c, file);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_builder_write_depfile(const char *path) {
    NT_BUILD_ASSERT(path && "write_depfile: path is required");
    if (s_input_count > 1) {
        qsort((void *)s_inputs, s_input_count, sizeof(char *), input_strcmp);
    }
    FILE *file = fopen(path, "wb");
    NT_BUILD_ASSERT(file && "write_depfile: cannot open output");
    /* Ninja keys the deps to the build step's own output; the target is informational. */
    write_depfile_path(file, path);
    (void)fputc(':', file);
    for (uint32_t i = 0; i < s_input_count; i++) {
        if (i > 0 && strcmp(s_inputs[i], s_inputs[i - 1]) == 0) {
            continue;
        }
        (void)fputs(" \\\n  ", file);
        write_depfile_path(file, s_inputs[i]);
    }
    (void)fputc('\n', file);
    bool write_failed = ferror(file) != 0;
    write_failed |= fclose(file) != 0;
    NT_BUILD_ASSERT(!write_failed && "write_depfile: write failed");
}
