/* clang-format off */
#include "nt_builder_internal.h"
#include "nt_shader_format.h"
/* clang-format on */

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

/* --- Headless GL context for shader validation (D-07) --- */

static bool s_gl_init_attempted = false;
static bool s_gl_available = false;
static bool s_gl_es3_available = false;
static GLFWwindow *s_gl_window = NULL;

static void shutdown_gl_context(void) {
    if (s_gl_window) {
        glfwDestroyWindow(s_gl_window);
        s_gl_window = NULL;
        glfwTerminate();
    }
}

static void ensure_gl_context(void) {
    if (s_gl_init_attempted) {
        return;
    }
    s_gl_init_attempted = true;

    if (!glfwInit()) {
        NT_LOG_WARN("shader validation skipped: glfwInit failed");
        return;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    s_gl_window = glfwCreateWindow(1, 1, "", NULL, NULL);
    if (!s_gl_window) {
        NT_LOG_WARN("shader validation skipped: no GL context");
        glfwTerminate();
        return;
    }
    glfwMakeContextCurrent(s_gl_window);

    if (!gladLoadGL(glfwGetProcAddress)) {
        NT_LOG_WARN("shader validation skipped: glad load failed");
        glfwDestroyWindow(s_gl_window);
        s_gl_window = NULL;
        glfwTerminate();
        return;
    }

    s_gl_available = true;
    s_gl_es3_available = (GLAD_GL_ARB_ES3_compatibility != 0);

    /* GL 4.3+ has ES3 compatibility in core */
    GLint major = 0;
    GLint minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (major * 10 + minor >= 43) {
        s_gl_es3_available = true;
    }

    NT_LOG_INFO("shader validation: GL %d.%d, ES3 compat: %s", major, minor, s_gl_es3_available ? "yes" : "no");

    (void)atexit(shutdown_gl_context);
}

/* --- GL compile validation (D-01, D-06) --- */

static bool validate_shader_compile(const char *source, GLenum gl_type, const char *version_prefix, const char *path) {
    GLuint shader = glCreateShader(gl_type);
    const char *sources[2] = {version_prefix, source};
    glShaderSource(shader, 2, sources, NULL);
    glCompileShader(shader);

    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        GLint log_len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
        if (log_len > 0) {
            char *log = (char *)malloc((size_t)log_len);
            if (log) {
                glGetShaderInfoLog(shader, log_len, NULL, log);
                NT_LOG_ERROR("shader %s: %s", path, log);
                free(log);
            }
        } else {
            NT_LOG_ERROR("shader %s: compilation failed (no info log)", path);
        }
        glDeleteShader(shader);
        return false;
    }
    glDeleteShader(shader);
    return true;
}

/* --- Dual compile validation (D-02, D-03, D-04) --- */

static nt_build_result_t validate_shader(const char *source, nt_build_shader_stage_t stage, const char *path) {
    if (!s_gl_available) {
        return NT_BUILD_OK; /* D-08: skip if no context */
    }

    GLenum gl_type = (stage == NT_BUILD_SHADER_VERTEX) ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER;

    /* Pass 1: GL 3.30 core (always, per D-02) */
    if (!validate_shader_compile(source, gl_type, "#version 330 core\n", path)) {
        return NT_BUILD_ERR_VALIDATION;
    }

    /* Pass 2: GLSL ES 3.00 (if available -- stricter, per D-03) */
    if (s_gl_es3_available) {
        if (!validate_shader_compile(source, gl_type, "#version 300 es\n", path)) {
            return NT_BUILD_ERR_VALIDATION;
        }
        NT_LOG_INFO("  validated: %s (GL 3.30 + ES 3.00)", path);
    } else {
        NT_LOG_INFO("  validated: %s (GL 3.30 only)", path);
    }
    return NT_BUILD_OK;
}

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
                i++;
            } else if (c == '/' && next == '*') {
                state = STRIP_BLOCK_COMMENT;
                i++;
            } else {
                out[wp++] = c;
            }
            break;

        case STRIP_LINE_COMMENT:
            if (c == '\n') {
                state = STRIP_NORMAL;
                out[wp++] = '\n';
            }
            break;

        case STRIP_BLOCK_COMMENT:
            if (c == '*' && next == '/') {
                state = STRIP_NORMAL;
                i++;
            }
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

    while (wp > 0 && (buf[wp - 1] == ' ' || buf[wp - 1] == '\t' || buf[wp - 1] == '\n')) {
        wp--;
    }

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

/* --- Shader import (called from finish_pack) --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_build_result_t nt_builder_import_shader(NtBuilderContext *ctx, const char *path, nt_build_shader_stage_t stage, uint64_t resource_id) {
    if (!ctx || !path) {
        return NT_BUILD_ERR_VALIDATION;
    }

    uint32_t file_size = 0;
    char *raw_source = nt_builder_read_file(path, &file_size);
    if (!raw_source) {
        NT_LOG_ERROR("%s: failed to read shader file", path);
        return NT_BUILD_ERR_IO;
    }

    if (file_size == 0) {
        NT_LOG_ERROR("%s: empty shader file", path);
        free(raw_source);
        return NT_BUILD_ERR_VALIDATION;
    }

    /* Resolve #include directives (D-11, D-12, D-13) */
    uint32_t resolved_len = 0;
    char *resolved = nt_builder_resolve_includes(raw_source, file_size, path, ctx, &resolved_len);
    free(raw_source);
    if (!resolved) {
        NT_LOG_ERROR("%s: include resolution failed", path);
        return NT_BUILD_ERR_VALIDATION;
    }

    char *stripped = (char *)malloc((size_t)resolved_len + 1);
    if (!stripped) {
        free(resolved);
        return NT_BUILD_ERR_IO;
    }

    uint32_t stripped_len = 0;
    strip_comments(resolved, resolved_len, stripped, &stripped_len);
    free(resolved);

    collapse_whitespace(stripped, &stripped_len);

    if (strstr(stripped, "#version") != NULL) {
        NT_LOG_ERROR("%s: #version directive found -- runtime adds it per platform, remove from source", path);
        free(stripped);
        return NT_BUILD_ERR_VALIDATION;
    }

    if (strstr(stripped, "void main") == NULL) {
        NT_LOG_ERROR("%s: missing void main()", path);
        free(stripped);
        return NT_BUILD_ERR_VALIDATION;
    }

    /* GL compile validation (D-01: validate at import time) */
    ensure_gl_context();
    nt_build_result_t val_result = validate_shader(stripped, stage, path);
    if (val_result != NT_BUILD_OK) {
        free(stripped);
        return val_result; /* D-05: compile failure = pack build error */
    }

    uint32_t code_size = stripped_len + 1;

    NtShaderCodeHeader shader_hdr;
    memset(&shader_hdr, 0, sizeof(shader_hdr));
    shader_hdr.magic = NT_SHADER_CODE_MAGIC;
    shader_hdr.version = NT_SHADER_CODE_VERSION;
    shader_hdr.stage = (stage == NT_BUILD_SHADER_VERTEX) ? NT_SHADER_STAGE_VERTEX : NT_SHADER_STAGE_FRAGMENT;
    shader_hdr._pad = 0;
    shader_hdr.code_size = code_size;

    uint32_t total_asset_size = (uint32_t)sizeof(NtShaderCodeHeader) + code_size;

    nt_build_result_t ret = nt_builder_append_data(ctx, &shader_hdr, (uint32_t)sizeof(NtShaderCodeHeader));
    if (ret == NT_BUILD_OK) {
        ret = nt_builder_append_data(ctx, stripped, code_size);
    }

    free(stripped);

    if (ret != NT_BUILD_OK) {
        return ret;
    }

    return nt_builder_register_asset(ctx, resource_id, NT_ASSET_SHADER_CODE, NT_SHADER_CODE_VERSION, total_asset_size);
}
