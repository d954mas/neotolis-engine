/* clang-format off */
#include "nt_builder_internal.h"
#include "nt_shader_format.h"
/* clang-format on */

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>
#include <stdlib.h> /* getenv (headless display probe) */

/* --- Headless GL context for shader validation --- */

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

#if defined(__linux__)
    /* Headless (no display): skip -- glfwInit partial-connects X11 then fails, and GLFW's
     * init-failure cleanup leaks the X11 connection (LSan). Validation needs a real context anyway. */
    // NOLINTNEXTLINE(concurrency-mt-unsafe) -- the builder is single-threaded
    if (getenv("DISPLAY") == NULL && getenv("WAYLAND_DISPLAY") == NULL) {
        NT_LOG_WARN("shader validation skipped: no display (headless)");
        return;
    }
#endif

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

/* --- GL compile validation --- */

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

/* --- Dual compile validation --- */

static nt_build_result_t validate_shader(const char *source, nt_build_shader_stage_t stage, const char *path) {
    if (!s_gl_available) {
        NT_LOG_WARN("  %s: GL compile validation skipped (no GL context)", path);
        return NT_BUILD_OK; /* skip if no context */
    }

    GLenum gl_type = (stage == NT_BUILD_SHADER_VERTEX) ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER;

    /* Pass 1: GL 3.30 core (always) */
    if (!validate_shader_compile(source, gl_type, "#version 330 core\n", path)) {
        return NT_BUILD_ERR_VALIDATION;
    }

    /* Pass 2: GLSL ES 3.00 (if available — stricter) */
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

/* --- Shader encode to independent buffer (thread-safe except for GL validation) --- */
/* Takes already-resolved shader text (from add_shader), strips comments,
 * collapses whitespace, validates, and returns contiguous buffer. */

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- UTF-8 scalar ranges and the output bound are checked together
static uint32_t shader_label_copy(char out[NT_SHADER_LABEL_CAP], const char *label) {
    const size_t len = strlen(label);
    uint32_t wp = 0;
    for (size_t rp = 0; rp < len && wp < NT_SHADER_LABEL_CAP - 1;) {
        const unsigned char byte = (unsigned char)label[rp];
        if (byte == '\\') {
            out[wp++] = '/';
            rp++;
            continue;
        }
        if (byte < 0x20 || byte == 0x7F) {
            out[wp++] = '?';
            rp++;
            continue;
        }
        if (byte < 0x80) {
            out[wp++] = (char)byte;
            rp++;
            continue;
        }
        uint8_t width = 0;
        if (byte >= 0xC2 && byte <= 0xDF) {
            width = 2;
        } else if (byte >= 0xE0 && byte <= 0xEF) {
            width = 3;
        } else if (byte >= 0xF0 && byte <= 0xF4) {
            width = 4;
        }
        bool valid = width != 0 && rp + width <= len;
        for (uint8_t i = 1; valid && i < width; i++) {
            valid = ((unsigned char)label[rp + i] & 0xC0) == 0x80;
        }
        if (valid && ((byte == 0xE0 && (unsigned char)label[rp + 1] < 0xA0) || (byte == 0xED && (unsigned char)label[rp + 1] > 0x9F) || (byte == 0xF0 && (unsigned char)label[rp + 1] < 0x90) ||
                      (byte == 0xF4 && (unsigned char)label[rp + 1] > 0x8F))) {
            valid = false;
        }
        if (!valid) {
            out[wp++] = '?';
            rp++;
        } else if (wp + width <= NT_SHADER_LABEL_CAP - 1) {
            memcpy(out + wp, label + rp, width);
            wp += width;
            rp += width;
        } else {
            break;
        }
    }
    out[wp] = '\0';
    return wp;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_builder_encode_shader_to_buf(const uint8_t *resolved_text, uint32_t text_len, nt_build_shader_stage_t stage, const char *label, uint8_t **out_data, uint32_t *out_size) {
    NT_BUILD_ASSERT(resolved_text && text_len > 0 && "shader encode: empty source");
    NT_BUILD_ASSERT(label && "shader encode: label is NULL");

    char *stripped = (char *)malloc((size_t)text_len + 1);
    NT_BUILD_ASSERT(stripped && "shader encode: malloc failed");

    uint32_t stripped_len = 0;
    strip_comments((const char *)resolved_text, text_len, stripped, &stripped_len);

    collapse_whitespace(stripped, &stripped_len);

    if (strstr(stripped, "#version") != NULL) {
        free(stripped);
        NT_BUILD_ASSERT(0 && "shader: #version directive found -- runtime adds it per platform, remove from source");
    }
    if (strstr(stripped, "void main") == NULL) {
        free(stripped);
        NT_BUILD_ASSERT(0 && "shader: missing void main()");
    }

    char label_text[NT_SHADER_LABEL_CAP];
    const uint32_t label_len = shader_label_copy(label_text, label);
    const uint32_t marker_len = (uint32_t)strlen(NT_SHADER_LABEL_PREFIX);
    const uint32_t labeled_len = marker_len + label_len + 1 + stripped_len;
    char *labeled = (char *)malloc((size_t)labeled_len + 1);
    NT_BUILD_ASSERT(labeled && "shader encode: malloc failed");
    memcpy(labeled, NT_SHADER_LABEL_PREFIX, sizeof(NT_SHADER_LABEL_PREFIX));
    memcpy(labeled + marker_len, label_text, label_len);
    labeled[marker_len + label_len] = '\n';
    memcpy(labeled + marker_len + label_len + 1, stripped, stripped_len + 1);
    free(stripped);
    stripped = labeled;
    stripped_len = labeled_len;

    /* GL compile validation at encode time */
    ensure_gl_context();
    nt_build_result_t val_result = validate_shader(stripped, stage, "shader");
    if (val_result != NT_BUILD_OK) {
        free(stripped);
        NT_BUILD_ASSERT(0 && "shader: GL compile failed -- see error above");
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
    uint8_t *buf = (uint8_t *)malloc(total_asset_size);
    NT_BUILD_ASSERT(buf && "shader encode: malloc failed");

    memcpy(buf, &shader_hdr, sizeof(NtShaderCodeHeader));
    memcpy(buf + sizeof(NtShaderCodeHeader), stripped, code_size);

    free(stripped);

    *out_data = buf;
    *out_size = total_asset_size;
}
