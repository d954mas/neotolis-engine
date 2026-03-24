/* clang-format off */
#include "nt_builder_internal.h"
/* clang-format on */

/* --- Include resolver (D-11, D-12, D-13, D-14) --- */

#define NT_INCLUDE_MAX_DEPTH 32
#define NT_INCLUDE_MAX_FILES 64

/* Internal state for recursive include resolution */
typedef struct {
    char *once_paths[NT_INCLUDE_MAX_FILES]; /* already-included files (#pragma once) */
    uint32_t once_count;
    uint32_t depth;
    const NtBuilderContext *ctx;
} nt_include_state_t;

/* --- Dynamic output buffer --- */

typedef struct {
    char *data;
    uint32_t size;
    uint32_t capacity;
} nt_include_buf_t;

static bool include_buf_init(nt_include_buf_t *buf, uint32_t initial_cap) {
    buf->data = (char *)malloc(initial_cap);
    if (!buf->data) {
        return false;
    }
    buf->size = 0;
    buf->capacity = initial_cap;
    return true;
}

static bool include_buf_append(nt_include_buf_t *buf, const char *data, uint32_t len) {
    while (buf->size + len > buf->capacity) {
        uint32_t new_cap = buf->capacity * 2;
        if (new_cap < buf->capacity) {
            return false; /* uint32 overflow */
        }
        char *new_data = (char *)realloc(buf->data, new_cap);
        if (!new_data) {
            return false;
        }
        buf->data = new_data;
        buf->capacity = new_cap;
    }
    memcpy(buf->data + buf->size, data, len);
    buf->size += len;
    return true;
}

/* --- Path helpers --- */

/* Extract directory from a file path. Returns malloc'd string or NULL. */
static char *extract_dir(const char *path) {
    if (!path) {
        return NULL;
    }

    /* Normalize to forward slashes first */
    size_t len = strlen(path);
    char *buf = (char *)malloc(len + 1);
    if (!buf) {
        return NULL;
    }
    memcpy(buf, path, len + 1);
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == '\\') {
            buf[i] = '/';
        }
    }

    /* Find last slash */
    char *last_slash = strrchr(buf, '/');
    if (!last_slash) {
        free(buf);
        return strdup(".");
    }
    *last_slash = '\0';
    return buf;
}

/* Build a full path from dir + filename. Returns malloc'd string. */
static char *join_path(const char *dir, const char *filename) {
    size_t dir_len = strlen(dir);
    size_t file_len = strlen(filename);
    char *result = (char *)malloc(dir_len + 1 + file_len + 1);
    if (!result) {
        return NULL;
    }
    memcpy(result, dir, dir_len); // NOLINT(bugprone-not-null-terminated-result)
    result[dir_len] = '/';
    memcpy(result + dir_len + 1, filename, file_len + 1);
    return result;
}

/* Check if a file exists (fopen + fclose) */
static bool file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) {
        (void)fclose(f);
        return true;
    }
    return false;
}

/* --- File lookup via asset roots (D-10b, D-13) --- */

char *nt_builder_find_file(const char *filename, const char *relative_to_dir, const NtBuilderContext *ctx) {
    if (!filename) {
        return NULL;
    }

    /* 1. Try relative to source directory first */
    if (relative_to_dir) {
        char *candidate = join_path(relative_to_dir, filename);
        if (candidate && file_exists(candidate)) {
            char *normalized = nt_builder_normalize_path(candidate);
            free(candidate);
            return normalized;
        }
        free(candidate);
    }

    /* 2. Try each asset root in order */
    if (ctx) {
        for (uint32_t i = 0; i < ctx->asset_root_count; i++) {
            char *candidate = join_path(ctx->asset_roots[i], filename);
            if (candidate && file_exists(candidate)) {
                char *normalized = nt_builder_normalize_path(candidate);
                free(candidate);
                return normalized;
            }
            free(candidate);
        }
    }

    return NULL;
}

/* --- Check #pragma once set --- */

static bool is_in_once_set(const nt_include_state_t *state, const char *path) {
    for (uint32_t i = 0; i < state->once_count; i++) {
        if (strcmp(state->once_paths[i], path) == 0) {
            return true;
        }
    }
    return false;
}

static bool add_to_once_set(nt_include_state_t *state, const char *path) {
    if (state->once_count >= NT_INCLUDE_MAX_FILES) {
        NT_LOG_ERROR("include file limit reached (%d max)", NT_INCLUDE_MAX_FILES);
        return false;
    }
    state->once_paths[state->once_count] = strdup(path);
    if (!state->once_paths[state->once_count]) {
        return false;
    }
    state->once_count++;
    return true;
}

/* --- Recursive include resolver --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity,misc-no-recursion)
static char *resolve_recursive(const char *source, uint32_t src_len, const char *source_dir, const char *file_path, nt_include_state_t *state, uint32_t *out_len) {
    if (state->depth >= NT_INCLUDE_MAX_DEPTH) {
        NT_LOG_ERROR("include depth limit exceeded (%u)", NT_INCLUDE_MAX_DEPTH);
        return NULL;
    }

    nt_include_buf_t out;
    if (!include_buf_init(&out, src_len + 256)) {
        return NULL;
    }

    state->depth++;

    /* Locals freed in cleanup label */
    char *filename = NULL;
    char *found_path = NULL;
    char *inc_source = NULL;
    char *inc_dir = NULL;
    char *resolved = NULL;
    bool ok = true;

    const char *p = source;
    const char *end = source + src_len;

    while (p < end && ok) {
        /* Find end of current line */
        const char *line_start = p;
        const char *line_end = p;
        while (line_end < end && *line_end != '\n') {
            line_end++;
        }
        uint32_t line_len = (uint32_t)(line_end - line_start);

        /* Check if line starts with // (line comment -- skip include resolution) */
        const char *trimmed = line_start;
        while (trimmed < line_end && (*trimmed == ' ' || *trimmed == '\t')) {
            trimmed++;
        }

        bool is_commented = (trimmed + 1 < line_end && trimmed[0] == '/' && trimmed[1] == '/');

        /* Check for #pragma once */
        if (!is_commented && line_len >= 12) {
            const char *t = trimmed;
            if (strncmp(t, "#pragma", 7) == 0 && (t[7] == ' ' || t[7] == '\t')) {
                const char *after_pragma = t + 8;
                while (after_pragma < line_end && (*after_pragma == ' ' || *after_pragma == '\t')) {
                    after_pragma++;
                }
                if (strncmp(after_pragma, "once", 4) == 0 && (after_pragma[4] == '\0' || after_pragma[4] == ' ' || after_pragma[4] == '\t' || after_pragma[4] == '/' || after_pragma + 4 >= line_end)) {
                    /* Register file in once set and skip this line from output */
                    if (file_path && !is_in_once_set(state, file_path)) {
                        ok = add_to_once_set(state, file_path);
                    }
                    p = (line_end < end) ? line_end + 1 : line_end;
                    continue;
                }
            }
        }

        /* Check for #include "filename" */
        if (!is_commented && trimmed[0] == '#') {
            const char *t = trimmed + 1;
            while (t < line_end && (*t == ' ' || *t == '\t')) {
                t++;
            }
            if (strncmp(t, "include", 7) == 0 && (t[7] == ' ' || t[7] == '\t' || t[7] == '"')) {
                const char *after_include = t + 7;
                while (after_include < line_end && (*after_include == ' ' || *after_include == '\t')) {
                    after_include++;
                }
                if (*after_include == '"') {
                    /* Extract filename from quotes */
                    const char *fname_start = after_include + 1;
                    const char *fname_end = fname_start;
                    while (fname_end < line_end && *fname_end != '"') {
                        fname_end++;
                    }
                    if (fname_end < line_end && *fname_end == '"') {
                        uint32_t fname_len = (uint32_t)(fname_end - fname_start);
                        filename = (char *)malloc(fname_len + 1);
                        if (!filename) {
                            ok = false;
                            break;
                        }
                        memcpy(filename, fname_start, fname_len);
                        filename[fname_len] = '\0';

                        /* Find the file */
                        found_path = nt_builder_find_file(filename, source_dir, state->ctx);
                        if (!found_path) {
                            NT_LOG_ERROR("include file not found: \"%s\" (searched relative to %s and %u asset roots)", filename, source_dir ? source_dir : "(none)",
                                         state->ctx ? state->ctx->asset_root_count : 0);
                            ok = false;
                            break;
                        }
                        free(filename);
                        filename = NULL;

                        /* Check pragma once -- skip if already included */
                        if (is_in_once_set(state, found_path)) {
                            free(found_path);
                            found_path = NULL;
                            p = (line_end < end) ? line_end + 1 : line_end;
                            continue;
                        }

                        /* Read the included file */
                        uint32_t inc_size = 0;
                        inc_source = nt_builder_read_file(found_path, &inc_size);
                        if (!inc_source) {
                            NT_LOG_ERROR("failed to read include file: %s", found_path);
                            ok = false;
                            break;
                        }

                        /* Extract directory for nested relative includes */
                        inc_dir = extract_dir(found_path);

                        /* Recurse */
                        uint32_t resolved_len = 0;
                        resolved = resolve_recursive(inc_source, inc_size, inc_dir, found_path, state, &resolved_len);
                        free(inc_source);
                        inc_source = NULL;
                        free(inc_dir);
                        inc_dir = NULL;
                        free(found_path);
                        found_path = NULL;

                        if (!resolved) {
                            ok = false;
                            break;
                        }

                        /* Append resolved content */
                        ok = include_buf_append(&out, resolved, resolved_len);
                        free(resolved);
                        resolved = NULL;
                        if (!ok) {
                            break;
                        }

                        /* Ensure trailing newline after included content */
                        if (out.size > 0 && out.data[out.size - 1] != '\n') {
                            ok = include_buf_append(&out, "\n", 1);
                            if (!ok) {
                                break;
                            }
                        }

                        /* Advance past the #include line */
                        p = (line_end < end) ? line_end + 1 : line_end;
                        continue;
                    }
                } else if (*after_include == '<') {
                    /* Angle bracket includes not supported -- pass through verbatim */
                }
            }
        }

        /* Default: copy line verbatim (including newline) */
        ok = include_buf_append(&out, line_start, line_len);
        if (!ok) {
            break;
        }
        if (line_end < end) {
            ok = include_buf_append(&out, "\n", 1);
            if (!ok) {
                break;
            }
            p = line_end + 1;
        } else {
            p = line_end;
        }
    }

    /* Cleanup temporaries */
    free(filename);
    free(found_path);
    free(inc_source);
    free(inc_dir);
    free(resolved);
    state->depth--;

    if (!ok) {
        free(out.data);
        return NULL;
    }

    /* Null-terminate */
    if (!include_buf_append(&out, "\0", 1)) {
        free(out.data);
        return NULL;
    }
    out.size--; /* Don't count null in reported size */

    *out_len = out.size;
    return out.data;
}

/* --- Public entry point --- */

char *nt_builder_resolve_includes(const char *source, uint32_t source_len, const char *source_path, const NtBuilderContext *ctx, uint32_t *out_len) {
    if (!source || !out_len) {
        return NULL;
    }

    /* If source has no #include directives at all, return a copy as-is (fast path) */
    if (!strstr(source, "#include")) {
        char *copy = (char *)malloc(source_len + 1);
        if (!copy) {
            return NULL;
        }
        memcpy(copy, source, source_len);
        copy[source_len] = '\0';
        *out_len = source_len;
        return copy;
    }

    nt_include_state_t state;
    memset(&state, 0, sizeof(state));
    state.ctx = ctx;

    /* Extract source directory for relative includes */
    char *source_dir = extract_dir(source_path);

    char *result = resolve_recursive(source, source_len, source_dir, source_path, &state, out_len);

    free(source_dir);

    /* Free once set */
    for (uint32_t i = 0; i < state.once_count; i++) {
        free(state.once_paths[i]);
    }

    return result;
}
