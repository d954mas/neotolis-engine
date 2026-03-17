/* clang-format off */
#include "nt_builder_internal.h"
#include "nt_shader_format.h"
/* clang-format on */

#include <ctype.h>

/* --- Comment stripping state machine --- */

typedef enum {
    STRIP_NORMAL = 0,
    STRIP_LINE_COMMENT = 1,
    STRIP_BLOCK_COMMENT = 2,
} strip_state_t;

static void strip_comments(const char *src, uint32_t src_len, char *out, uint32_t *out_len) {
    strip_state_t state = STRIP_NORMAL;
    uint32_t wp = 0;

    for (uint32_t i = 0; i < src_len; i++) {
        char c = src[i];
        char next = 0;
        if (i + 1 < src_len) {
            next = src[i + 1];
        }

        switch (state) {
        case STRIP_NORMAL:
            if (c == '/' && next == '/') {
                state = STRIP_LINE_COMMENT;
                i++; /* skip both chars */
            } else if (c == '/' && next == '*') {
                state = STRIP_BLOCK_COMMENT;
                i++; /* skip both chars */
            } else {
                out[wp++] = c;
            }
            break;

        case STRIP_LINE_COMMENT:
            if (c == '\n') {
                state = STRIP_NORMAL;
                out[wp++] = '\n';
            }
            /* else skip */
            break;

        case STRIP_BLOCK_COMMENT:
            if (c == '*' && next == '/') {
                state = STRIP_NORMAL;
                i++; /* skip both chars */
            }
            /* else skip */
            break;
        }
    }

    out[wp] = '\0';
    *out_len = wp;
}

static void collapse_whitespace(char *buf, uint32_t *len) {
    uint32_t src_len = *len;
    uint32_t wp = 0;
    bool prev_space = false;

    for (uint32_t i = 0; i < src_len; i++) {
        char c = buf[i];

        if (c == '\n') {
            /* Trim trailing spaces before newline */
            while (wp > 0 && (buf[wp - 1] == ' ' || buf[wp - 1] == '\t')) {
                wp--;
            }
            buf[wp++] = '\n';
            prev_space = false;
        } else if (c == ' ' || c == '\t') {
            if (!prev_space && wp > 0 && buf[wp - 1] != '\n') {
                buf[wp++] = ' ';
                prev_space = true;
            }
        } else {
            buf[wp++] = c;
            prev_space = false;
        }
    }

    /* Trim trailing whitespace at end */
    while (wp > 0 && (buf[wp - 1] == ' ' || buf[wp - 1] == '\t' || buf[wp - 1] == '\n')) {
        wp--;
    }

    /* Trim leading whitespace/newlines */
    uint32_t start = 0;
    while (start < wp && (buf[start] == ' ' || buf[start] == '\t' || buf[start] == '\n')) {
        start++;
    }

    if (start > 0 && start < wp) {
        memmove(buf, buf + start, wp - start);
        wp -= start;
    } else if (start >= wp) {
        wp = 0;
    }

    buf[wp] = '\0';
    *len = wp;
}

/* --- File reading --- */

static char *read_file(const char *path, uint32_t *out_size) {
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

/* --- Shader importer --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_build_result_t nt_builder_add_shader_with_id(NtBuilderContext *ctx, const char *path, nt_build_shader_stage_t stage, uint32_t resource_id) {
    if (!ctx || !path) {
        return NT_BUILD_ERR_VALIDATION;
    }

    /* Read entire file */
    uint32_t file_size = 0;
    char *raw_source = read_file(path, &file_size);
    if (!raw_source) {
        (void)fprintf(stderr, "ERROR: %s: failed to read shader file\n", path);
        ctx->has_error = true;
        return NT_BUILD_ERR_IO;
    }

    if (file_size == 0) {
        (void)fprintf(stderr, "ERROR: %s: empty shader file\n", path);
        free(raw_source);
        ctx->has_error = true;
        return NT_BUILD_ERR_VALIDATION;
    }

    /* Strip comments and collapse whitespace */
    char *stripped = (char *)malloc((size_t)file_size + 1);
    if (!stripped) {
        (void)fprintf(stderr, "ERROR: %s: failed to allocate buffer for shader processing\n", path);
        free(raw_source);
        ctx->has_error = true;
        return NT_BUILD_ERR_IO;
    }

    uint32_t stripped_len = 0;
    strip_comments(raw_source, file_size, stripped, &stripped_len);
    free(raw_source);

    collapse_whitespace(stripped, &stripped_len);

    /* #version must not be in source -- runtime prepends platform-appropriate version */
    if (strstr(stripped, "#version") != NULL) {
        (void)fprintf(stderr, "ERROR: %s: #version directive found -- runtime adds it per platform, remove from source\n", path);
        free(stripped);
        ctx->has_error = true;
        return NT_BUILD_ERR_VALIDATION;
    }

    /* Sanity check */
    if (strstr(stripped, "void main") == NULL) {
        (void)fprintf(stderr, "ERROR: %s: missing void main()\n", path);
        free(stripped);
        ctx->has_error = true;
        return NT_BUILD_ERR_VALIDATION;
    }

    /* Build shader asset */
    uint32_t code_size = stripped_len + 1; /* include null terminator */

    NtShaderCodeHeader shader_hdr;
    memset(&shader_hdr, 0, sizeof(shader_hdr));
    shader_hdr.magic = NT_SHADER_CODE_MAGIC;
    shader_hdr.version = NT_SHADER_CODE_VERSION;
    shader_hdr.stage = (stage == NT_BUILD_SHADER_VERTEX) ? NT_SHADER_STAGE_VERTEX : NT_SHADER_STAGE_FRAGMENT;
    shader_hdr._pad = 0;
    shader_hdr.code_size = code_size;

    uint32_t total_asset_size = (uint32_t)sizeof(NtShaderCodeHeader) + code_size;

    /* Append header + source (null-terminated) */
    nt_build_result_t ret = nt_builder_append_data(ctx, &shader_hdr, (uint32_t)sizeof(NtShaderCodeHeader));
    if (ret == NT_BUILD_OK) {
        ret = nt_builder_append_data(ctx, stripped, code_size);
    }

    free(stripped);

    if (ret != NT_BUILD_OK) {
        return ret;
    }

    /* Register asset */
    char *norm_path = nt_builder_normalize_path(path);
    ret = nt_builder_register_asset(ctx, norm_path ? norm_path : path, resource_id, NT_ASSET_SHADER_CODE, NT_SHADER_CODE_VERSION, total_asset_size);
    free(norm_path);

    return ret;
}

nt_build_result_t nt_builder_add_shader(NtBuilderContext *ctx, const char *path, nt_build_shader_stage_t stage) {
    if (!ctx || !path) {
        return NT_BUILD_ERR_VALIDATION;
    }
    char *norm_path = nt_builder_normalize_path(path);
    uint32_t resource_id = nt_builder_fnv1a(norm_path ? norm_path : path);
    free(norm_path);
    return nt_builder_add_shader_with_id(ctx, path, stage, resource_id);
}
