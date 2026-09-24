#ifndef NT_GFX_GL_CALLS_H
#define NT_GFX_GL_CALLS_H

/* The only way the GL backend issues a GL call. Each NT_GL* form counts the
 * call in g_nt_gfx.counters.gl[] (every build) and in capture builds records
 * one BACKEND event; then it issues the call.
 * scripts/check_gl_calls.py rejects any bare gl* call outside this header. */

#include "core/nt_platform.h"
#include "graphics/nt_gfx_internal.h"

#ifdef NT_PLATFORM_WEB
#include <GLES3/gl3.h>
#else
#include <glad/gl.h>
#endif

#include <stdint.h>
#include <string.h>

/* A distinct pointer type, so an offset into a bound buffer records as bytes, not presence. */
typedef const struct nt_gl_offset_tag *nt_gl_offset_t;
static inline nt_gl_offset_t nt_gl_offset(uintptr_t bytes) { return (nt_gl_offset_t)bytes; } // NOLINT(performance-no-int-to-ptr)

// #region counting (every build)
/* A plain increment with a constant index: no call, branch or check per GL call.
 * A frame is always open between init and shutdown, so every call lands in one. */
#define NT_GL_COUNT_(call) ((void)g_nt_gfx.counters.gl[call]++)

/* One count per call with non-NULL data; NULL storage (including NULL orphaning) does not count. */
static inline void nt_gl_count_buffer_upload(const void *data, uint64_t bytes) {
    if (data == NULL) {
        return;
    }
    g_nt_gfx.counters.buffer_upload_calls++;
    g_nt_gfx.counters.buffer_upload_bytes += bytes;
#if NT_GFX_CAPTURE_ENABLED
    if (g_nt_gfx_capture.call != NULL) {
        g_nt_gfx_capture.call->data.backend.bytes = bytes;
    }
#endif
}

static inline void nt_gl_count_texture_upload(const void *data, uint64_t bytes) {
    if (data == NULL) {
        return;
    }
    g_nt_gfx.counters.texture_upload_calls++;
    g_nt_gfx.counters.texture_upload_bytes += bytes;
#if NT_GFX_CAPTURE_ENABLED
    if (g_nt_gfx_capture.call != NULL) {
        g_nt_gfx_capture.call->data.backend.bytes = bytes;
    }
#endif
}
// #endregion

// #region recording (capture builds)
#if NT_GFX_CAPTURE_ENABLED
/* The put helpers run only while a call record is open: the NT_GL_* forms check once per call. */
static inline void nt_gl_put_unsigned(uint64_t value) {
    NT_ASSERT(g_nt_gfx_capture.call_ints < 12);
    g_nt_gfx_capture.call->data.backend.args[g_nt_gfx_capture.call_ints++] = (uint32_t)value;
}
static inline void nt_gl_put_signed(int64_t value) { nt_gl_put_unsigned((uint64_t)value); }
static inline void nt_gl_put_float(float value) {
    NT_ASSERT(g_nt_gfx_capture.call_floats < 4);
    g_nt_gfx_capture.call->data.backend.values[g_nt_gfx_capture.call_floats++] = value;
}
static inline void nt_gl_put_double(double value) { nt_gl_put_float((float)value); }
static inline void nt_gl_put_pointer(const void *pointer) { nt_gl_put_unsigned(pointer != NULL ? 1U : 0U); }
static inline void nt_gl_put_offset(nt_gl_offset_t offset) { nt_gl_put_unsigned((uintptr_t)offset); }
static inline void nt_gl_put_strings(const GLchar *const *strings) { nt_gl_put_pointer((const void *)strings); }
static inline void nt_gl_put_names(GLsizei count, const GLuint *names) {
    nt_gl_put_unsigned((uint32_t)count);
    for (GLsizei i = 0; i < count; i++) {
        nt_gl_put_unsigned(names[i]);
    }
}
static inline void nt_gl_put_uniform(GLint location, uint32_t float_count, const GLfloat *values) {
    nt_gfx_event_t *event = g_nt_gfx_capture.call;
    NT_ASSERT(float_count <= 16);
    event->data.uniform.name = (uint32_t)location;
    event->data.uniform.count = float_count;
    memcpy(event->data.uniform.values, values, float_count * sizeof(float));
}
static inline void nt_gl_close(void) { nt_gfx_capture_commit_call(); }
static inline GLint nt_gl_close_int(GLint result) {
    if (g_nt_gfx_capture.call != NULL) {
        nt_gl_put_signed(result);
    }
    nt_gl_close();
    return result;
}
static inline GLuint nt_gl_close_uint(GLuint result) {
    if (g_nt_gfx_capture.call != NULL) {
        nt_gl_put_unsigned(result);
    }
    nt_gl_close();
    return result;
}

#ifdef NT_PLATFORM_WEB
/* WebGL has no debug callback; a private type keeps the _Generic association unconditional. */
typedef const struct nt_gl_no_callback_tag *nt_gl_callback_t;
#else
typedef GLDEBUGPROC nt_gl_callback_t;
#endif
static inline void nt_gl_put_callback(nt_gl_callback_t callback) { nt_gl_put_unsigned(callback != NULL ? 1U : 0U); }

// clang-format off
#define NT_GL_PUT_(arg)                                                                                                                                                                                \
    _Generic((arg),                                                                                                                                                                                    \
        _Bool: nt_gl_put_unsigned,                                                                                                                                                                     \
        unsigned char: nt_gl_put_unsigned,                                                                                                                                                             \
        unsigned short: nt_gl_put_unsigned,                                                                                                                                                            \
        unsigned int: nt_gl_put_unsigned,                                                                                                                                                              \
        unsigned long: nt_gl_put_unsigned,                                                                                                                                                             \
        unsigned long long: nt_gl_put_unsigned,                                                                                                                                                        \
        char: nt_gl_put_signed,                                                                                                                                                                        \
        signed char: nt_gl_put_signed,                                                                                                                                                                 \
        short: nt_gl_put_signed,                                                                                                                                                                       \
        int: nt_gl_put_signed,                                                                                                                                                                         \
        long: nt_gl_put_signed,                                                                                                                                                                        \
        long long: nt_gl_put_signed,                                                                                                                                                                   \
        float: nt_gl_put_float,                                                                                                                                                                        \
        double: nt_gl_put_double,                                                                                                                                                                      \
        nt_gl_offset_t: nt_gl_put_offset,                                                                                                                                                              \
        const GLchar **: nt_gl_put_strings,                                                                                                                                                            \
        const GLchar *const *: nt_gl_put_strings,                                                                                                                                                      \
        nt_gl_callback_t: nt_gl_put_callback,                                                                                                                                                          \
        default: nt_gl_put_pointer)(arg)
// clang-format on
#define NT_GL_NARGS_(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, n, ...) n
#define NT_GL_NARGS(...) NT_GL_NARGS_(__VA_ARGS__, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define NT_GL_CAT_(a, b) a##b
#define NT_GL_CAT(a, b) NT_GL_CAT_(a, b)
#define NT_GL_EACH_1(a) NT_GL_PUT_(a)
#define NT_GL_EACH_2(a, ...) NT_GL_PUT_(a), NT_GL_EACH_1(__VA_ARGS__)
#define NT_GL_EACH_3(a, ...) NT_GL_PUT_(a), NT_GL_EACH_2(__VA_ARGS__)
#define NT_GL_EACH_4(a, ...) NT_GL_PUT_(a), NT_GL_EACH_3(__VA_ARGS__)
#define NT_GL_EACH_5(a, ...) NT_GL_PUT_(a), NT_GL_EACH_4(__VA_ARGS__)
#define NT_GL_EACH_6(a, ...) NT_GL_PUT_(a), NT_GL_EACH_5(__VA_ARGS__)
#define NT_GL_EACH_7(a, ...) NT_GL_PUT_(a), NT_GL_EACH_6(__VA_ARGS__)
#define NT_GL_EACH_8(a, ...) NT_GL_PUT_(a), NT_GL_EACH_7(__VA_ARGS__)
#define NT_GL_EACH_9(a, ...) NT_GL_PUT_(a), NT_GL_EACH_8(__VA_ARGS__)
#define NT_GL_EACH_10(a, ...) NT_GL_PUT_(a), NT_GL_EACH_9(__VA_ARGS__)
#define NT_GL_LAST_3(a, b, c) c
#define NT_GL_LAST_4(a, b, c, d) d

#define NT_GL_OPEN_(call) (NT_GL_COUNT_(call), nt_gfx_capture_open_call(call))
#define NT_GL_RECORDING_() (g_nt_gfx_capture.call != NULL)
#define NT_GL_ARGS_(...) (NT_GL_RECORDING_() ? (NT_GL_CAT(NT_GL_EACH_, NT_GL_NARGS(__VA_ARGS__))(__VA_ARGS__)) : (void)0)
#define NT_GL_NAMES_(count, names) (NT_GL_RECORDING_() ? nt_gl_put_names((count), (names)) : (void)0)
#define NT_GL_UNIFORM_VALUES_(float_count, location, ...)                                                                                                                                              \
    (NT_GL_RECORDING_() ? nt_gl_put_uniform((location), (float_count), NT_GL_CAT(NT_GL_LAST_, NT_GL_NARGS(location, __VA_ARGS__))(location, __VA_ARGS__)) : (void)0)
#define NT_GL_CLOSE_() nt_gl_close()
#define NT_GL_CLOSE_RESULT_(result) _Generic((result), GLint: nt_gl_close_int, GLuint: nt_gl_close_uint)(result)
#else
#define NT_GL_OPEN_(call) NT_GL_COUNT_(call)
#define NT_GL_ARGS_(...) ((void)0)
#define NT_GL_NAMES_(count, names) ((void)0)
#define NT_GL_UNIFORM_VALUES_(float_count, location, ...) ((void)0)
#define NT_GL_CLOSE_() ((void)0)
#define NT_GL_CLOSE_RESULT_(result) (result)
#endif
// #endregion

/* Capture builds evaluate the arguments twice (record, then call): keep them side-effect-free.
 * The call enum is pasted here, in the outermost macro, because glad defines GL
 * names as macros that would otherwise expand first. Forms:
 *   NT_GL(glFn, args...)                      any call; integers, floats, pointers by type
 *   NT_GL0(glFn)                              argument-less call
 *   NT_GL_RET(glFn, args...) / NT_GL_RET0     value-returning call; the result is recorded after the args
 *   NT_GL_BUFFER_UPLOAD / NT_GL_TEXTURE_UPLOAD(data, bytes, glFn, args...)
 *                                             count and record a CPU payload of `bytes` when data is non-NULL
 *   NT_GL_GEN / NT_GL_DELETE(glFn, n, names)  records the count followed by each object name
 *   NT_GL_UNIFORM(glFn, floats, location, ...) copies a vec4/mat4 value array into the record
 *   NT_GL_ISSUED(name, args...)               counts and records a call issued elsewhere (WebGL JS) */
#define NT_GL(fn, ...) (NT_GL_OPEN_(NT_GFX_GL_##fn), NT_GL_ARGS_(__VA_ARGS__), NT_GL_CLOSE_(), fn(__VA_ARGS__))
#define NT_GL0(fn) (NT_GL_OPEN_(NT_GFX_GL_##fn), NT_GL_CLOSE_(), fn())
#define NT_GL_RET(fn, ...) (NT_GL_OPEN_(NT_GFX_GL_##fn), NT_GL_ARGS_(__VA_ARGS__), NT_GL_CLOSE_RESULT_(fn(__VA_ARGS__)))
#define NT_GL_RET0(fn) (NT_GL_OPEN_(NT_GFX_GL_##fn), NT_GL_CLOSE_RESULT_(fn()))
#define NT_GL_BUFFER_UPLOAD(data, bytes, fn, ...) (NT_GL_OPEN_(NT_GFX_GL_##fn), nt_gl_count_buffer_upload((data), (bytes)), NT_GL_ARGS_(__VA_ARGS__), NT_GL_CLOSE_(), fn(__VA_ARGS__))
#define NT_GL_TEXTURE_UPLOAD(data, bytes, fn, ...) (NT_GL_OPEN_(NT_GFX_GL_##fn), nt_gl_count_texture_upload((data), (bytes)), NT_GL_ARGS_(__VA_ARGS__), NT_GL_CLOSE_(), fn(__VA_ARGS__))
/* The count and every name must fit backend.args[12]; counts are constants at every site. */
#define NT_GL_NAMES_FIT_(count)                                                                                                                                                                        \
    ((void)sizeof(struct {                                                                                                                                                                             \
        _Static_assert((count) >= 0 && (count) < 12, "gen/delete names exceed the record");                                                                                                            \
        int unused;                                                                                                                                                                                    \
    }))
#define NT_GL_GEN(fn, count, names) (NT_GL_NAMES_FIT_(count), NT_GL_OPEN_(NT_GFX_GL_##fn), fn((count), (names)), NT_GL_NAMES_(count, names), NT_GL_CLOSE_())
#define NT_GL_DELETE(fn, count, names) (NT_GL_NAMES_FIT_(count), NT_GL_OPEN_(NT_GFX_GL_##fn), NT_GL_NAMES_(count, names), NT_GL_CLOSE_(), fn((count), (names)))
#define NT_GL_UNIFORM(fn, float_count, location, ...) (NT_GL_OPEN_(NT_GFX_GL_##fn), NT_GL_UNIFORM_VALUES_(float_count, location, __VA_ARGS__), NT_GL_CLOSE_(), fn(location, __VA_ARGS__))
#define NT_GL_ISSUED(name, ...) (NT_GL_OPEN_(NT_GFX_GL_##name), NT_GL_ARGS_(__VA_ARGS__), NT_GL_CLOSE_())

#endif
