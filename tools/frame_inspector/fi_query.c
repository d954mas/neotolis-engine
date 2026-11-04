#include "fi_query.h"

#include "core/nt_assert.h"
#include "graphics/nt_gfx_capture_text.h"
#include "log/nt_log.h"

#include <ctype.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// #region value names
/* GL enum names, for values whose name does not depend on the argument (the domain decides 0, 1 and units). */
static const struct {
    uint32_t value;
    const char *name;
} k_gl_enums[] = {
    {0x0200U, "GL_NEVER"},
    {0x0201U, "GL_LESS"},
    {0x0202U, "GL_EQUAL"},
    {0x0203U, "GL_LEQUAL"},
    {0x0204U, "GL_GREATER"},
    {0x0205U, "GL_NOTEQUAL"},
    {0x0206U, "GL_GEQUAL"},
    {0x0207U, "GL_ALWAYS"},
    {0x0300U, "GL_SRC_COLOR"},
    {0x0301U, "GL_ONE_MINUS_SRC_COLOR"},
    {0x0302U, "GL_SRC_ALPHA"},
    {0x0303U, "GL_ONE_MINUS_SRC_ALPHA"},
    {0x0304U, "GL_DST_ALPHA"},
    {0x0305U, "GL_ONE_MINUS_DST_ALPHA"},
    {0x0306U, "GL_DST_COLOR"},
    {0x0307U, "GL_ONE_MINUS_DST_COLOR"},
    {0x0308U, "GL_SRC_ALPHA_SATURATE"},
    {0x0404U, "GL_FRONT"},
    {0x0405U, "GL_BACK"},
    {0x0408U, "GL_FRONT_AND_BACK"},
    {0x0B44U, "GL_CULL_FACE"},
    {0x0B71U, "GL_DEPTH_TEST"},
    {0x0B90U, "GL_STENCIL_TEST"},
    {0x0BE2U, "GL_BLEND"},
    {0x0C11U, "GL_SCISSOR_TEST"},
    {0x0CF5U, "GL_UNPACK_ALIGNMENT"},
    {0x0D05U, "GL_PACK_ALIGNMENT"},
    {0x0DE1U, "GL_TEXTURE_2D"},
    {0x1400U, "GL_BYTE"},
    {0x1401U, "GL_UNSIGNED_BYTE"},
    {0x1402U, "GL_SHORT"},
    {0x1403U, "GL_UNSIGNED_SHORT"},
    {0x1404U, "GL_INT"},
    {0x1405U, "GL_UNSIGNED_INT"},
    {0x1406U, "GL_FLOAT"},
    {0x140BU, "GL_HALF_FLOAT"},
    {0x1800U, "GL_COLOR"},
    {0x1801U, "GL_DEPTH"},
    {0x1802U, "GL_STENCIL"},
    {0x1902U, "GL_DEPTH_COMPONENT"},
    {0x1903U, "GL_RED"},
    {0x1907U, "GL_RGB"},
    {0x1908U, "GL_RGBA"},
    {0x1F00U, "GL_VENDOR"},
    {0x1F01U, "GL_RENDERER"},
    {0x1F02U, "GL_VERSION"},
    {0x2600U, "GL_NEAREST"},
    {0x2601U, "GL_LINEAR"},
    {0x2700U, "GL_NEAREST_MIPMAP_NEAREST"},
    {0x2701U, "GL_LINEAR_MIPMAP_NEAREST"},
    {0x2702U, "GL_NEAREST_MIPMAP_LINEAR"},
    {0x2703U, "GL_LINEAR_MIPMAP_LINEAR"},
    {0x2800U, "GL_TEXTURE_MAG_FILTER"},
    {0x2801U, "GL_TEXTURE_MIN_FILTER"},
    {0x2802U, "GL_TEXTURE_WRAP_S"},
    {0x2803U, "GL_TEXTURE_WRAP_T"},
    {0x2901U, "GL_REPEAT"},
    {0x8001U, "GL_CONSTANT_COLOR"},
    {0x8002U, "GL_ONE_MINUS_CONSTANT_COLOR"},
    {0x8003U, "GL_CONSTANT_ALPHA"},
    {0x8004U, "GL_ONE_MINUS_CONSTANT_ALPHA"},
    {0x8006U, "GL_FUNC_ADD"},
    {0x8007U, "GL_MIN"},
    {0x8008U, "GL_MAX"},
    {0x800AU, "GL_FUNC_SUBTRACT"},
    {0x800BU, "GL_FUNC_REVERSE_SUBTRACT"},
    {0x8037U, "GL_POLYGON_OFFSET_FILL"},
    {0x8051U, "GL_RGB8"},
    {0x8058U, "GL_RGBA8"},
    {0x80E1U, "GL_BGRA"},
    {0x812FU, "GL_CLAMP_TO_EDGE"},
    {0x813CU, "GL_TEXTURE_BASE_LEVEL"},
    {0x813DU, "GL_TEXTURE_MAX_LEVEL"},
    {0x81A5U, "GL_DEPTH_COMPONENT16"},
    {0x81A6U, "GL_DEPTH_COMPONENT24"},
    {0x8227U, "GL_RG"},
    {0x8228U, "GL_RG_INTEGER"},
    {0x8229U, "GL_R8"},
    {0x822BU, "GL_RG8"},
    {0x823AU, "GL_RG16UI"},
    {0x8370U, "GL_MIRRORED_REPEAT"},
    {0x8814U, "GL_RGBA32F"},
    {0x881AU, "GL_RGBA16F"},
    {0x884CU, "GL_TEXTURE_COMPARE_MODE"},
    {0x884DU, "GL_TEXTURE_COMPARE_FUNC"},
    {0x884EU, "GL_COMPARE_REF_TO_TEXTURE"},
    {0x8866U, "GL_QUERY_RESULT"},
    {0x8867U, "GL_QUERY_RESULT_AVAILABLE"},
    {0x8892U, "GL_ARRAY_BUFFER"},
    {0x8893U, "GL_ELEMENT_ARRAY_BUFFER"},
    {0x88BFU, "GL_TIME_ELAPSED"},
    {0x88E0U, "GL_STREAM_DRAW"},
    {0x88E1U, "GL_STREAM_READ"},
    {0x88E4U, "GL_STATIC_DRAW"},
    {0x88E8U, "GL_DYNAMIC_DRAW"},
    {0x88EBU, "GL_PIXEL_PACK_BUFFER"},
    {0x88ECU, "GL_PIXEL_UNPACK_BUFFER"},
    {0x8A11U, "GL_UNIFORM_BUFFER"},
    {0x8A36U, "GL_ACTIVE_UNIFORM_BLOCKS"},
    {0x8A35U, "GL_ACTIVE_UNIFORM_BLOCK_MAX_NAME_LENGTH"},
    {0x8A40U, "GL_UNIFORM_BLOCK_DATA_SIZE"},
    {0x8B30U, "GL_FRAGMENT_SHADER"},
    {0x8B31U, "GL_VERTEX_SHADER"},
    {0x8B81U, "GL_COMPILE_STATUS"},
    {0x8B82U, "GL_LINK_STATUS"},
    {0x8B84U, "GL_INFO_LOG_LENGTH"},
    {0x8B86U, "GL_ACTIVE_UNIFORMS"},
    {0x8B87U, "GL_ACTIVE_UNIFORM_MAX_LENGTH"},
    {0x8C3DU, "GL_RGB9_E5"},
    {0x8C3EU, "GL_UNSIGNED_INT_5_9_9_9_REV"},
    {0x8C41U, "GL_SRGB8"},
    {0x8C43U, "GL_SRGB8_ALPHA8"},
    {0x8CA8U, "GL_READ_FRAMEBUFFER"},
    {0x8CA9U, "GL_DRAW_FRAMEBUFFER"},
    {0x8CACU, "GL_DEPTH_COMPONENT32F"},
    {0x8CD5U, "GL_FRAMEBUFFER_COMPLETE"},
    {0x8CE0U, "GL_COLOR_ATTACHMENT0"},
    {0x8D00U, "GL_DEPTH_ATTACHMENT"},
    {0x8D20U, "GL_STENCIL_ATTACHMENT"},
    {0x8D40U, "GL_FRAMEBUFFER"},
    {0x8D99U, "GL_RGBA_INTEGER"},
    {0x8E8CU, "GL_COMPRESSED_RGBA_BPTC_UNORM"},
    {0x8E8DU, "GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM"},
    {0x8F36U, "GL_COPY_READ_BUFFER"},
    {0x8F37U, "GL_COPY_WRITE_BUFFER"},
    {0x9117U, "GL_SYNC_GPU_COMMANDS_COMPLETE"},
    {0x91B1U, "GL_COMPLETION_STATUS_KHR"},
    {0x9274U, "GL_COMPRESSED_RGB8_ETC2"},
    {0x9275U, "GL_COMPRESSED_SRGB8_ETC2"},
    {0x9278U, "GL_COMPRESSED_RGBA8_ETC2_EAC"},
    {0x9279U, "GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC"},
    {0x93B0U, "GL_COMPRESSED_RGBA_ASTC_4x4_KHR"},
    {0x93D0U, "GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR"},
};

static const char *gl_enum_name(uint32_t v) {
    for (size_t i = 0; i < sizeof(k_gl_enums) / sizeof(k_gl_enums[0]); i++) {
        if (k_gl_enums[i].value == v) {
            return k_gl_enums[i].name;
        }
    }
    return NULL;
}

// clang-format off: clang-format versions disagree on braces in a macro body
#define FI_NAMES_(...) {(const char *const[]){__VA_ARGS__}, sizeof((const char *const[]){__VA_ARGS__}) / sizeof(const char *)}
// clang-format on
/* Engine enum names by domain, indexed by value. */
static const struct {
    const char *const *names;
    size_t count;
} k_engine_enums[NT_GFX_DOMAIN_COUNT] = {
    [NT_GFX_DOMAIN_SHADER_TYPE] = FI_NAMES_("VERTEX", "FRAGMENT"),
    [NT_GFX_DOMAIN_BUFFER_TYPE] = FI_NAMES_("VERTEX", "INDEX", "READBACK"),
    [NT_GFX_DOMAIN_BUFFER_USAGE] = FI_NAMES_("IMMUTABLE", "DYNAMIC", "STREAM"),
    [NT_GFX_DOMAIN_INDEX_TYPE] = FI_NAMES_("NONE", "U16", "U32"),
    [NT_GFX_DOMAIN_TEXTURE_FORMAT] = FI_NAMES_("INVALID", "RGBA8", "RGB8", "RG8", "R8", "RGBA16F", "RG16UI", "RGBA32F", "DEPTH16", "DEPTH24", "DEPTH32F", "ETC2_RGB8", "ETC2_RGBA8", "BC7_RGBA",
                                               "ASTC_4x4_RGBA", "ETC2_SRGB8", "ETC2_SRGB8_ALPHA8", "BC7_SRGB_ALPHA", "ASTC_4x4_SRGB8_ALPHA8", "SRGB8_ALPHA8", "SRGB8", "RGB9E5"),
    [NT_GFX_DOMAIN_TEXTURE_FILTER] = FI_NAMES_("NEAREST", "LINEAR", "NEAREST_MIPMAP_NEAREST", "LINEAR_MIPMAP_NEAREST", "NEAREST_MIPMAP_LINEAR", "LINEAR_MIPMAP_LINEAR"),
    [NT_GFX_DOMAIN_TEXTURE_WRAP] = FI_NAMES_("CLAMP_TO_EDGE", "REPEAT", "MIRRORED_REPEAT"),
    [NT_GFX_DOMAIN_COMPARE_FUNC] = FI_NAMES_("NONE", "LEQUAL", "LESS", "GEQUAL", "GREATER", "EQUAL", "NOTEQUAL", "ALWAYS", "NEVER"),
    [NT_GFX_DOMAIN_VERTEX_TYPE] = FI_NAMES_("FLOAT", "HALF", "UINT8", "INT8", "UINT16", "INT16"),
    [NT_GFX_DOMAIN_CULL_MODE] = FI_NAMES_("NONE", "BACK", "FRONT"),
    [NT_GFX_DOMAIN_SAMPLER_CLASS] = FI_NAMES_("FLOAT", "SHADOW", "UINT"),
};
#undef FI_NAMES_
_Static_assert(NT_TEXTURE_FORMAT_RGB9E5 == 21 && NT_FILTER_LINEAR_MIPMAP_LINEAR == 5 && NT_WRAP_MIRRORED_REPEAT == 2 && NT_COMPARE_NEVER == 8 && NT_VERTEX_INT16 == 5 && NT_USAGE_STREAM == 2 &&
                   NT_INDEX_UINT32 == 2 && NT_SHADER_FRAGMENT == 1,
               "frame inspector: an engine enum changed; update k_engine_enums");
#if !defined(NT_PLATFORM_WEB)
_Static_assert(NT_BUFFER_READBACK == 2, "frame inspector: a buffer type changed; update k_engine_enums");
#endif

/* Bit n is location n, printed as runs: 1-4,7. */
static const char *locations_text(uint32_t v, char *buf, size_t size) {
    size_t at = 0;
    buf[0] = '\0';
    for (uint32_t bit = 0; bit < 32 && at + 8 < size; bit++) {
        const bool starts_run = (v & (1U << bit)) != 0 && (bit == 0 || (v & (1U << (bit - 1U))) == 0);
        if (!starts_run) {
            continue;
        }
        uint32_t last = bit;
        while (last + 1U < 32 && (v & (1U << (last + 1U))) != 0) {
            last++;
        }
        at += (size_t)snprintf(buf + at, size - at, last > bit ? "%s%u-%u" : "%s%u", at > 0 ? "," : "", bit, last);
    }
    return v != 0 ? buf : NULL;
}

static const char *clear_mask_text(uint32_t v, char *buf, size_t size) {
    (void)snprintf(buf, size, "%s%s%s", (v & 0x4000U) != 0 ? "COLOR|" : "", (v & 0x100U) != 0 ? "DEPTH|" : "", (v & 0x400U) != 0 ? "STENCIL|" : "");
    buf[strlen(buf) > 0 ? strlen(buf) - 1U : 0U] = '\0';
    return v != 0 && (v & ~0x4500U) == 0 ? buf : NULL;
}

/* A value by its domain: an engine or GL enum name, a flag set; NULL when the domain has no name for it. */
static const char *domain_name(uint32_t domain, uint32_t v, char *buf, size_t size) {
    static const char *const k_modes[] = {"GL_POINTS", "GL_LINES", "GL_LINE_LOOP", "GL_LINE_STRIP", "GL_TRIANGLES", "GL_TRIANGLE_STRIP", "GL_TRIANGLE_FAN"};
    static const char *const k_zero_one[] = {"GL_ZERO", "GL_ONE"};
    static const char *const k_bools[] = {"GL_FALSE", "GL_TRUE"};
    if (domain < NT_GFX_DOMAIN_COUNT && k_engine_enums[domain].names != NULL) {
        return v < k_engine_enums[domain].count ? k_engine_enums[domain].names[v] : NULL;
    }
    switch (domain) {
    case NT_GFX_DOMAIN_GL_BLEND_FACTOR:
        return v <= 1U ? k_zero_one[v] : gl_enum_name(v);
    case NT_GFX_DOMAIN_GL_TEXTURE_UNIT:
        (void)snprintf(buf, size, "GL_TEXTURE%u", v - 0x84C0U);
        return v >= 0x84C0U && v < 0x84E0U ? buf : NULL;
    case NT_GFX_DOMAIN_LOCATIONS:
        return locations_text(v, buf, size);
    case FI_DOMAIN_GL_PRIMITIVE:
        return v < sizeof(k_modes) / sizeof(k_modes[0]) ? k_modes[v] : NULL;
    case FI_DOMAIN_GL_CLEAR_MASK:
        return clear_mask_text(v, buf, size);
    case FI_DOMAIN_GL_BOOL:
        return v <= 1U ? k_bools[v] : NULL;
    default:
        return gl_enum_name(v);
    }
}
// #endregion

// #region record text
bool fi_contains_ci(const char *text, size_t len, const char *needle) {
    const size_t n = strlen(needle);
    for (size_t i = 0; n > 0 && i + n <= len; i++) {
        size_t k = 0;
        while (k < n && tolower((unsigned char)text[i + k]) == tolower((unsigned char)needle[k])) {
            k++;
        }
        if (k == n) {
            return true;
        }
    }
    return n == 0;
}

static const char *alias_prefix(uint32_t kind) {
    static const char *const k_short[] = {"obj", "shader", "prog", "pipe", "vi", "buf", "tex", "smp", "rt", "layout"};
    return kind < sizeof(k_short) / sizeof(k_short[0]) ? k_short[kind] : "obj";
}

void fi_alias_text(const fi_frame_t *frame, uint32_t kind, uint32_t object, char *buf, size_t size) {
    const fi_alias_t a = fi_alias(frame, kind, object);
    if (a.ordinal == 0) {
        (void)snprintf(buf, size, "%s:%u", alias_prefix(kind), object);
    } else if (a.label[0] == '\0') {
        (void)snprintf(buf, size, "%s#%u", alias_prefix(kind), a.ordinal);
    } else {
        char label[FI_ALIAS_LEN - 24U]; /* room left for the kind, '/', '#' and the number */
        fi_one_line(a.label, label, sizeof(label), FI_TEXT_TOKEN);
        (void)snprintf(buf, size, "%s/%s#%u", alias_prefix(kind), label, a.ordinal);
    }
}

static void appendf(char *buf, size_t size, size_t *at, const char *fmt, ...) NT_PRINTF_ATTR(4, 5);
static void appendf(char *buf, size_t size, size_t *at, const char *fmt, ...) {
    if (*at + 1U >= size) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(buf + *at, size - *at, fmt, args);
    va_end(args);
    *at += n > 0 ? (size_t)n : 0U;
    *at = *at < size ? *at : size - 1U;
}

static uint32_t float_bits(float f) {
    uint32_t bits = 0;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

/* The capture's float forms: the shortest %g that reads back to the same float32, NaN as its bits, so two
 * different values never print alike. */
static const char *float_text(float f, char out[24]) {
    if (isnan(f)) {
        (void)snprintf(out, 24, "nan:0x%08" PRIx32, float_bits(f));
        return out;
    }
    for (int precision = 6; precision <= 9; precision++) {
        (void)snprintf(out, 24, "%.*g", precision, (double)f);
        if (float_bits(strtof(out, NULL)) == float_bits(f)) {
            break;
        }
    }
    return out;
}

/* Render target 0 is the default framebuffer; every other handle 0 binds nothing. */
static const char *zero_handle(uint32_t kind) { return kind == NT_GFX_OBJECT_RENDER_TARGET ? "default framebuffer" : "unbound"; }

/* One value as a reader names it. */
static void value_text(const fi_frame_t *fr, uint8_t meaning, uint8_t sub, uint32_t v, char *buf, size_t size) {
    char named[64];
    const char *name = NULL;
    switch ((nt_gfx_field_meaning_t)meaning) {
    case NT_GFX_MEANING_HANDLE:
        if (v == 0) {
            (void)snprintf(buf, size, "%s", zero_handle(sub));
        } else {
            fi_alias_text(fr, sub, v, buf, size);
        }
        return;
    case NT_GFX_MEANING_HASH:
        (void)snprintf(buf, size, "0x%08x", v);
        return;
    case NT_GFX_MEANING_VALUE:
        name = domain_name(sub, v, named, sizeof(named));
        break;
    default:
        break;
    }
    if (name != NULL) {
        (void)snprintf(buf, size, "%s", name);
    } else {
        (void)snprintf(buf, size, "%u", v);
    }
}

/* A GL name and what it stood for at its record: "19 prog/skeletal_showcase/text#1", "1 upload VAO", or the bare
 * name when the frame never recorded its owner or the row already names it (a definition's own name). */
static void gl_name_text(const fi_frame_t *fr, const nt_gfx_event_t *e, size_t at, uint32_t v, char *buf, size_t size) {
    const fi_gl_ref_t ref = fi_gl_ref(fr, (uint32_t)(e - fr->view.events), (uint32_t)at);
    const char *role = fi_gl_role(ref.owner);
    char alias[FI_ALIAS_LEN];
    if (ref.owner == FI_GL_OBJECT && !(ref.kind == e->object_kind && ref.object == e->object)) {
        fi_alias_text(fr, ref.kind, ref.object, alias, sizeof(alias));
        (void)snprintf(buf, size, "%u %s", v, alias);
    } else if (role != NULL) {
        (void)snprintf(buf, size, "%u %s", v, role);
    } else {
        (void)snprintf(buf, size, "%u", v);
    }
}

static void arg_text(fi_arg_t a, uint32_t v, char *buf, size_t size) {
    char named[64];
    const char *name = a.kind == FI_ARG_ENUM ? domain_name(a.sub, v, named, sizeof(named)) : NULL;
    if (a.kind == FI_ARG_POINTER) {
        (void)snprintf(buf, size, "%s", v != 0 ? "data" : "NULL");
    } else if (a.kind == FI_ARG_SIGNED) {
        (void)snprintf(buf, size, "%d", (int32_t)v);
    } else if (name != NULL) {
        (void)snprintf(buf, size, "%s", name);
    } else {
        (void)snprintf(buf, size, "%u", v);
    }
}

/* An issued call as code: glBindBufferRange(GL_UNIFORM_BUFFER,0,2,512,256), floats after the integers. */
static void call_text(const fi_frame_t *fr, const nt_gfx_event_t *e, char *buf, size_t size, size_t *at) {
    const uint32_t ints = e->arg_count & 15U;
    const uint32_t floats = e->arg_count >> 4;
    appendf(buf, size, at, "%s(", nt_gfx_gl_call_name(e->detail));
    for (uint32_t k = 0; k < ints; k++) {
        char v[FI_ALIAS_LEN + 16];
        const fi_arg_t a = fi_call_arg(e->detail, k);
        if (a.kind == FI_ARG_NAME && e->data.backend.args[k] != 0) {
            gl_name_text(fr, e, offsetof(nt_gfx_event_t, data.backend.args) + ((size_t)k * sizeof(uint32_t)), e->data.backend.args[k], v, sizeof(v));
        } else {
            arg_text(a, e->data.backend.args[k], v, sizeof(v));
        }
        appendf(buf, size, at, "%s%s", k > 0 ? "," : "", v);
    }
    for (uint32_t k = 0; k < floats; k++) {
        char real[24];
        appendf(buf, size, at, "%s%s", k > 0 || ints > 0 ? "," : "", float_text(e->data.backend.values[k], real));
    }
    appendf(buf, size, at, ")");
    if (e->data.backend.bytes > 0) {
        appendf(buf, size, at, " bytes=%llu", (unsigned long long)e->data.backend.bytes);
    }
}

/* A request's render target 0 is the default framebuffer; other zero fields are left out, as in the capture text. */
static bool shows_zero(const nt_gfx_capture_field_t *field, bool request) { return request && field->meaning == NT_GFX_MEANING_HANDLE && field->sub == NT_GFX_OBJECT_RENDER_TARGET; }

static void u32_field_text(const fi_frame_t *fr, const nt_gfx_event_t *e, const nt_gfx_capture_field_t *field, uint32_t v, bool request, bool draw, char *buf, size_t size, size_t *at) {
    char text[FI_ALIAS_LEN + 16];
    if (field->meaning == NT_GFX_MEANING_BOOL) {
        if (v != 0) {
            appendf(buf, size, at, " %s", field->name);
        }
    } else if (field->meaning == NT_GFX_MEANING_GL_NAME && v != 0) {
        gl_name_text(fr, e, field->offset, v, text, sizeof(text));
        appendf(buf, size, at, " %s=%s", field->name, text);
    } else if (field->meaning == NT_GFX_MEANING_HANDLE && draw) { /* a draw's vertex input: the row's second name */
        value_text(fr, field->meaning, field->sub, v, text, sizeof(text));
        appendf(buf, size, at, " %s", text);
    } else if (v != 0 || shows_zero(field, request)) {
        value_text(fr, field->meaning, field->sub, v, text, sizeof(text));
        appendf(buf, size, at, " %s=%s", field->name, text);
    }
}

static void floats_text(const char *name, const unsigned char *p, uint32_t capacity, char *buf, size_t size, size_t *at) {
    float values[8];
    uint32_t count = capacity < 8U ? capacity : 8U;
    memcpy(values, p, (size_t)count * sizeof(float));
    while (count > 0 && values[count - 1U] == 0.0F) {
        count--;
    }
    if (count == 0) {
        return;
    }
    char real[24];
    appendf(buf, size, at, " %s=[%s", name, float_text(values[0], real));
    for (uint32_t i = 1; i < count; i++) {
        appendf(buf, size, at, ",%s", float_text(values[i], real));
    }
    appendf(buf, size, at, "]");
}

/* One data field, named and decoded. */
static void field_text(const fi_frame_t *fr, const nt_gfx_event_t *e, const nt_gfx_capture_field_t *field, bool request, bool draw, char *buf, size_t size, size_t *at) {
    const unsigned char *p = (const unsigned char *)e + field->offset;
    uint32_t v = 0;
    int32_t s = 0;
    uint64_t wide = 0;
    float real = 0.0F;
    bool flag = false;
    nt_gfx_capture_string_t str = 0;
    char text[FI_ALIAS_LEN];
    switch ((nt_gfx_field_type_t)field->type) {
    case NT_GFX_FIELD_U32:
        memcpy(&v, p, sizeof(v));
        u32_field_text(fr, e, field, v, request, draw, buf, size, at);
        break;
    case NT_GFX_FIELD_I32:
        memcpy(&s, p, sizeof(s));
        if (s != 0) {
            appendf(buf, size, at, " %s=%d", field->name, s);
        }
        break;
    case NT_GFX_FIELD_U64:
        memcpy(&wide, p, sizeof(wide));
        if (wide != 0) {
            appendf(buf, size, at, " %s=%llu", field->name, (unsigned long long)wide);
        }
        break;
    case NT_GFX_FIELD_F32:
        memcpy(&real, p, sizeof(real));
        if (real != 0.0F) {
            char digits[24];
            appendf(buf, size, at, " %s=%s", field->name, float_text(real, digits));
        }
        break;
    case NT_GFX_FIELD_BOOL:
        memcpy(&flag, p, sizeof(flag));
        if (flag) {
            appendf(buf, size, at, " %s", field->name);
        }
        break;
    case NT_GFX_FIELD_F32S:
        floats_text(field->name, p, field->size, buf, size, at);
        break;
    case NT_GFX_FIELD_STR:
        memcpy(&str, p, sizeof(str));
        if (str != 0) {
            fi_one_line(fr->view.strings + str, text, sizeof(text), FI_TEXT_QUOTED);
            appendf(buf, size, at, " %s=\"%s\"", field->name, text);
        }
        break;
    case NT_GFX_FIELD_U32S:
        break; /* issued calls only: call_text */
    }
}

/* The record's data fields by the capture field table, named and decoded; only: the one field to print, NULL for
 * all. A draw shows its count and vertex input: first, vertices and instances are the row's xN and Compare's. */
static void fields_text(const fi_frame_t *fr, const nt_gfx_event_t *e, const char *only, char *buf, size_t size, size_t *at) {
    const nt_gfx_capture_field_t *f = NULL;
    const uint32_t n = nt_gfx_capture_fields(e, &f);
    const bool request = e->kind == NT_GFX_EVENT_OP || e->kind == NT_GFX_EVENT_ARGUMENT;
    const bool draw = fi_is_draw(e);
    for (uint32_t k = 0; k < n; k++) {
        const bool picked = only == NULL || strcmp(f[k].name, only) == 0;
        const bool shown = !draw || f[k].meaning == NT_GFX_MEANING_HANDLE || strcmp(f[k].name, "count") == 0;
        if (picked && shown) {
            field_text(fr, e, &f[k], request, draw, buf, size, at);
        }
    }
}

/* What a note field holds: its key and value, a handle as its alias. */
static void note_field_text(const fi_frame_t *fr, const nt_gfx_event_t *e, char *buf, size_t size, size_t *at) {
    char key[64];
    char value[FI_ALIAS_LEN];
    fi_one_line(fr->view.strings + e->field.key, key, sizeof(key), FI_TEXT_TOKEN);
    switch (e->field.type) {
    case NT_GFX_NOTE_U32:
        (void)snprintf(value, sizeof(value), "%u", e->field.value.u32);
        break;
    case NT_GFX_NOTE_I32:
        (void)snprintf(value, sizeof(value), "%d", e->field.value.i32);
        break;
    case NT_GFX_NOTE_U64:
        (void)snprintf(value, sizeof(value), "%llu", (unsigned long long)e->field.value.u64);
        break;
    case NT_GFX_NOTE_F32:
        (void)float_text(e->field.value.f32, value);
        break;
    case NT_GFX_NOTE_STR: {
        char text[FI_ALIAS_LEN - 2U];
        fi_one_line(fr->view.strings + e->field.value.str, text, sizeof(text), FI_TEXT_QUOTED);
        (void)snprintf(value, sizeof(value), "\"%s\"", text);
        break;
    }
    case NT_GFX_NOTE_HANDLE:
        value_text(fr, NT_GFX_MEANING_HANDLE, (uint8_t)e->field.value.handle.kind, e->field.value.handle.id, value, sizeof(value));
        break;
    }
    appendf(buf, size, at, "NOTE_FIELD %s=%s", key, value);
}

static void row_text(const fi_frame_t *frame, uint32_t record, const char *only, char *buf, size_t size);

void fi_row_text(const fi_frame_t *frame, uint32_t record, char *buf, size_t size) { row_text(frame, record, NULL, buf, size); }

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void row_text(const fi_frame_t *frame, uint32_t record, const char *only, char *buf, size_t size) {
    const nt_gfx_event_t *e = &frame->view.events[record];
    size_t at = 0;
    buf[0] = '\0';
    if (e->kind == NT_GFX_EVENT_NOTE_FIELD) {
        note_field_text(frame, e, buf, size, &at);
        return;
    }
    if (e->kind > NT_GFX_EVENT_ARGUMENT) { /* notes and markers: their captured line is their text */
        const char *line = NULL;
        const size_t len = fi_record_text(frame, record, &line);
        appendf(buf, size, &at, "%.*s", (int)len, line);
        return;
    }
    if (e->kind == NT_GFX_EVENT_BACKEND) {
        call_text(frame, e, buf, size, &at);
        return;
    }
    if (e->kind == NT_GFX_EVENT_SKIP && e->detail != 0) { /* a cache skip: the call it left out, then its key's fields */
        appendf(buf, size, &at, "%s cache", nt_gfx_gl_call_name(e->detail));
        fields_text(frame, e, only, buf, size, &at);
        return;
    }
    if (e->kind != NT_GFX_EVENT_OP) {
        appendf(buf, size, &at, "%s ", nt_gfx_event_kind_name((uint32_t)e->kind));
    }
    appendf(buf, size, &at, "%s", nt_gfx_operation_name(e->operation));
    if (e->object_kind != NT_GFX_OBJECT_NONE) {
        char alias[FI_ALIAS_LEN];
        value_text(frame, NT_GFX_MEANING_HANDLE, e->object_kind, e->object, alias, sizeof(alias));
        appendf(buf, size, &at, " %s", alias);
    }
    if (e->kind == NT_GFX_EVENT_DEFINITION && e->operation == NT_GFX_OP_STATE) {
        appendf(buf, size, &at, " %s", nt_gfx_object_kind_name(e->detail));
    } else if (e->kind == NT_GFX_EVENT_INITIAL && e->operation == NT_GFX_OP_STATE) {
        appendf(buf, size, &at, " %s", e->detail == NT_GFX_INITIAL_BACKEND ? "BACKEND" : "FRONTEND");
    }
    fields_text(frame, e, only, buf, size, &at);
    if (e->kind == NT_GFX_EVENT_OP && (e->operation == NT_GFX_OP_DRAW_INSTANCED || e->operation == NT_GFX_OP_DRAW_INDEXED_INSTANCED)) {
        appendf(buf, size, &at, " x%u", e->data.draw.instances);
    } else if ((e->operation == NT_GFX_OP_SCISSOR_ENABLE || e->operation == NT_GFX_OP_GPU_TIMING) && !e->data.toggle.on) {
        appendf(buf, size, &at, " off");
    }
    if (e->kind == NT_GFX_EVENT_INITIAL && e->result == NT_GFX_RESULT_UNKNOWN) {
        appendf(buf, size, &at, " unknown");
    }
}

const char *fi_result_word(const nt_gfx_event_t *e) {
    if (e->kind == NT_GFX_EVENT_SKIP) {
        return "GL skipped";
    }
    if (e->kind != NT_GFX_EVENT_OP || e->result == NT_GFX_RESULT_NONE || e->result == NT_GFX_RESULT_ACCEPTED) {
        return "";
    }
    if (e->result == NT_GFX_RESULT_CACHE) {
        if (fi_is_mergeable_draw(e)) {
            return "merged";
        }
        return e->operation == NT_GFX_OP_CREATE ? "found" : "already set";
    }
    const char *name = nt_gfx_result_name(e->result);
    return name != NULL ? name : "?";
}
// #endregion

// #region state text
static void gl_slot_name(const fi_gl_slot_t *slot, char *buf, size_t size) {
    const char *key = gl_enum_name(slot->key[0]);
    char hex[16];
    (void)snprintf(hex, sizeof(hex), "0x%x", slot->key[0]);
    key = key != NULL ? key : hex;
    switch (slot->call) {
    case NT_GFX_GL_NONE:
        (void)snprintf(buf, size, "fixed function");
        break;
    case NT_GFX_GL_glBindTexture:
        (void)snprintf(buf, size, "texture unit %u", slot->key[0]);
        break;
    case NT_GFX_GL_glBindSampler:
        (void)snprintf(buf, size, "sampler unit %u", slot->key[0]);
        break;
    case NT_GFX_GL_glBindBufferRange:
        (void)snprintf(buf, size, "%s #%u", key, slot->key[1]);
        break;
    case NT_GFX_GL_glEnable:
    case NT_GFX_GL_glBindBuffer:
    case NT_GFX_GL_glBindFramebuffer:
    case NT_GFX_GL_glPixelStorei:
        (void)snprintf(buf, size, "%s", key);
        break;
    default:
        (void)snprintf(buf, size, "%s", nt_gfx_gl_call_name(slot->call) + 2);
        break;
    }
}

/* The record that set this slot at the earlier point, FI_NONE when it was not set there. */
static uint32_t slot_before(const fi_gl_slot_t *slot, const fi_gl_state_t *before) {
    for (uint32_t p = 0; p < before->count; p++) {
        const fi_gl_slot_t *old = &before->slot[p];
        if (old->call == slot->call && old->key[0] == slot->key[0] && old->key[1] == slot->key[1]) {
            return old->record;
        }
    }
    return FI_NONE;
}

/* The field of an inherited backend record that holds this slot; NULL when the record holds only the slot. */
static const char *slot_field(const fi_frame_t *fr, uint32_t call, uint32_t record) {
    const nt_gfx_event_t *e = &fr->view.events[record];
    if (e->kind == NT_GFX_EVENT_INITIAL && e->operation == NT_GFX_OP_TEXTURE) {
        return call == NT_GFX_GL_glBindSampler ? "sampler" : "texture";
    }
    if (e->kind != NT_GFX_EVENT_INITIAL || e->operation != NT_GFX_OP_STATE) {
        return NULL;
    }
    switch (call) {
    case NT_GFX_GL_glUseProgram:
        return "program";
    case NT_GFX_GL_glBindVertexArray:
        return "vertex_array";
    case NT_GFX_GL_glBindFramebuffer:
        return "framebuffer";
    case NT_GFX_GL_glActiveTexture:
        return "active_texture";
    case NT_GFX_GL_glBindBuffer:
        return "array_buffer";
    default:
        return NULL;
    }
}

void fi_state_value(const fi_frame_t *frame, const fi_state_line_t *line, uint32_t record, char *buf, size_t size) {
    const char *only = line->gl ? slot_field(frame, line->call, record) : NULL;
    row_text(frame, record, only, buf, size);
    if (only != NULL && strchr(buf, '=') == NULL) {
        (void)snprintf(buf + strlen(buf), size - strlen(buf), " %s=0", only); /* a zero field prints nothing on its own */
    }
}

static bool scissor_on(const fi_frame_t *fr, uint32_t record) { return record != FI_NONE && fr->view.events[record].data.toggle.on; }

uint32_t fi_state_lines(const fi_frame_t *frame, const fi_state_view_t *view, bool all, fi_state_line_t out[FI_STATE_LINES]) {
    const fi_request_state_t *req = &view->req;
    const fi_request_state_t *was = &view->was_req;
    const bool compared = view->was != FI_NONE;
    /* Grouped by stage: output, program, input, raster. */
    const struct {
        const char *name;
        uint32_t now, before;
    } rows[] = {{"output: pass", req->pass, was->pass},
                {"program: pipeline", req->pipeline, was->pipeline},
                {"program: textures", req->texture_set, was->texture_set},
                {"input: vertex input", req->vertex_input, was->vertex_input},
                {"raster: viewport", req->viewport, was->viewport},
                {"raster: scissor", req->scissor, was->scissor},
                {"raster: scissor on", req->scissor_enable, was->scissor_enable}};
    uint32_t n = 0;
    for (uint32_t k = 0; k < sizeof(rows) / sizeof(rows[0]); k++) {
        fi_state_line_t *l = &out[n++];
        (void)snprintf(l->name, sizeof(l->name), "%s", rows[k].name);
        l->now = rows[k].now;
        l->before = compared ? rows[k].before : rows[k].now;
        l->inherited = l->now != FI_NONE && frame->view.events[l->now].kind == NT_GFX_EVENT_INITIAL;
        l->gl = false;
        /* unset scissor is off: a pass or a restore turned it off */
        l->changed = k + 1U == sizeof(rows) / sizeof(rows[0]) ? scissor_on(frame, l->before) != scissor_on(frame, l->now) : !fi_same_setting(frame, l->before, l->now);
    }
    for (uint32_t k = 0; k < 8; k++) {
        if (req->ubo[k] != FI_NONE || was->ubo[k] != FI_NONE) {
            fi_state_line_t *l = &out[n++];
            (void)snprintf(l->name, sizeof(l->name), "program: uniform slot %u", k);
            l->now = req->ubo[k];
            l->before = compared ? was->ubo[k] : req->ubo[k];
            l->inherited = false;
            l->gl = false;
            l->changed = !fi_same_setting(frame, l->before, l->now);
        }
    }
    for (uint32_t k = 0; k < view->gl.count; k++) {
        const fi_gl_slot_t *slot = &view->gl.slot[k];
        fi_state_line_t *l = &out[n++];
        gl_slot_name(slot, l->name, sizeof(l->name));
        l->call = slot->call;
        l->now = slot->record;
        l->before = view->was_gl_at != FI_NONE ? slot_before(slot, &view->was_gl) : slot->record;
        l->inherited = slot->inherited;
        l->gl = true;
        l->changed = !fi_gl_same(frame, slot, l->before, l->now);
        n -= !all && !l->changed && view->was_gl_at != FI_NONE ? 1U : 0U;
    }
    return n;
}
// #endregion

// #region output
static void put(fi_out_t *out, const char *fmt, ...) NT_PRINTF_ATTR(2, 3);
static void put(fi_out_t *out, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list again;
    va_copy(again, args);
    const size_t room = out->cap - out->len;
    const int n = vsnprintf(out->cap > 0 ? out->text + out->len : NULL, room, fmt, args);
    if (n > 0 && (size_t)n >= room) { /* it did not fit: grow, then format again */
        size_t cap = out->cap > 0 ? out->cap : 4096U;
        while (cap < out->len + (size_t)n + 1U) {
            cap *= 2U;
        }
        char *text = (char *)realloc(out->text, cap);
        NT_ASSERT(text != NULL);
        out->text = text;
        out->cap = cap;
        (void)vsnprintf(out->text + out->len, out->cap - out->len, fmt, again);
    }
    out->len += n > 0 ? (size_t)n : 0U;
    va_end(again);
    va_end(args);
}

/* @N, its row text and its badge, after a prefix ("inside ", "  "). */
static void put_record(fi_out_t *out, const fi_frame_t *fr, const char *prefix, uint32_t record) {
    char body[600];
    fi_row_text(fr, record, body, sizeof(body));
    const char *word = fi_result_word(&fr->view.events[record]);
    const bool badge = word[0] != '\0';
    put(out, "%s@%u  %s%s%s%s\n", prefix, record, body, badge ? "  [" : "", word, badge ? "]" : "");
}
// #endregion

// #region numbers
const char *fi_compare_name(uint8_t compare, bool gl) {
    switch ((fi_compare_t)compare) {
    case FI_COMPARE_PREVIOUS_DRAW:
        return gl ? "previous GL draw" : "previous draw";
    case FI_COMPARE_FRAME_START:
        return "frame start";
    case FI_COMPARE_NONE:
        break;
    }
    return "";
}

typedef struct {
    fi_number_t *out;
    uint32_t n;
    const fi_frame_t *fr;
    uint8_t section;
    bool all; /* zeros too: every key list accepts */
} numbers_t;

static void number(numbers_t *ns, fi_query_t q, bool sum, const char *key, const char *meaning) {
    const fi_cell_t c = fi_cell(ns->fr, q);
    const uint64_t value = sum ? c.sum : c.n;
    if (!ns->all && value == 0 && c.n == 0) {
        return;
    }
    NT_ASSERT(ns->n < FI_NUMBERS);
    fi_number_t *x = &ns->out[ns->n++];
    (void)snprintf(x->key, sizeof(x->key), "%s", key);
    (void)snprintf(x->meaning, sizeof(x->meaning), "%s", meaning);
    x->section = ns->section;
    x->q = q;
    x->sum = sum;
    x->value = value;
}

static void frame_numbers(numbers_t *ns) {
    static const char *const k_breaks[FI_BREAK_COUNT] = {"", "command", "function", "instanced", "range", "count_limit"};
    char key[48];
    char meaning[96];
    ns->section = FI_SECTION_FRAME;
    number(ns, (fi_query_t){.kind = FI_Q_DRAW_CALL}, false, "gl_draw_calls", "GL draw calls issued");
    number(ns, (fi_query_t){.kind = FI_Q_VERTICES}, false, "draw_requests", "draw requests (ACCEPTED or CACHE)");
    number(ns, (fi_query_t){.kind = FI_Q_MERGED}, false, "merged", "draw requests merged into the previous draw: no GL call");
    for (uint32_t b = FI_BREAK_COMMAND; b < FI_BREAK_COUNT; b++) {
        (void)snprintf(key, sizeof(key), "break.%s", k_breaks[b]);
        (void)snprintf(meaning, sizeof(meaning), "new GL draws: %s", fi_break_name((fi_break_t)b));
        number(ns, (fi_query_t){.kind = FI_Q_BREAK, .a = b}, false, key, meaning);
    }
    number(ns, (fi_query_t){.kind = FI_Q_INSTANCES}, false, "instanced", "instanced draw requests: they never merge");
    number(ns, (fi_query_t){.kind = FI_Q_GL_ANY}, false, "gl_calls", "GL calls issued");
    number(ns, (fi_query_t){.kind = FI_Q_SKIP}, false, "gl_skipped", "GL calls the GL cache left out: GL already held that state");
    number(ns, (fi_query_t){.kind = FI_Q_UPLOAD, .a = 2}, false, "upload_calls", "GL calls that uploaded bytes");
    number(ns, (fi_query_t){.kind = FI_Q_UPLOAD, .a = 2}, true, "upload_bytes", "bytes uploaded to GL");
    number(ns, (fi_query_t){.kind = FI_Q_PROBLEM}, false, "problems", "drops and failed requests");
    number(ns, (fi_query_t){.kind = FI_Q_DROP}, false, "drops", "renderer drops");
    ns->section = FI_SECTION_DRAWS;
    number(ns, (fi_query_t){.kind = FI_Q_VERTICES}, true, "vertices", "vertices drawn (x instances)");
    number(ns, (fi_query_t){.kind = FI_Q_INDICES}, true, "indices", "indices drawn (x instances)");
    number(ns, (fi_query_t){.kind = FI_Q_INSTANCES}, true, "instances", "instances of instanced draws");
    ns->section = FI_SECTION_UPLOADS;
    number(ns, (fi_query_t){.kind = FI_Q_UPLOAD, .a = 0}, false, "buffer_upload_calls", "buffer upload calls");
    number(ns, (fi_query_t){.kind = FI_Q_UPLOAD, .a = 0}, true, "buffer_upload_bytes", "buffer upload bytes");
    number(ns, (fi_query_t){.kind = FI_Q_UPLOAD, .a = 1}, false, "texture_upload_calls", "texture upload calls");
    number(ns, (fi_query_t){.kind = FI_Q_UPLOAD, .a = 1}, true, "texture_upload_bytes", "texture upload bytes");
    number(ns, (fi_query_t){.kind = FI_Q_FRAME_STORE}, true, "frame_store_bytes", "of them frame storage uploaded at end_frame");
}

static void resource_numbers(numbers_t *ns) {
    static const char *const k_res[FI_RES_COUNT] = {"live", "created", "destroyed"};
    static const char *const k_meaning[FI_RES_COUNT] = {"live at frame start", "created", "destroyed"};
    char key[48];
    char meaning[96];
    ns->section = FI_SECTION_RESOURCES;
    for (uint32_t kind = 1; kind < FI_OBJECTS; kind++) {
        for (uint32_t r = 0; r < FI_RES_COUNT; r++) {
            const fi_query_t q = {.kind = FI_Q_RESOURCE, .a = kind, .b = (uint8_t)r};
            (void)snprintf(key, sizeof(key), "res.%s.%s", nt_gfx_object_kind_name(kind), k_res[r]);
            (void)snprintf(meaning, sizeof(meaning), "%s %s", nt_gfx_object_kind_name(kind), k_meaning[r]);
            number(ns, q, false, key, meaning);
            if (ns->all || fi_cell(ns->fr, q).sum > 0) {
                (void)snprintf(key, sizeof(key), "res.%s.%s.bytes", nt_gfx_object_kind_name(kind), k_res[r]);
                (void)snprintf(meaning, sizeof(meaning), "%s %s: bytes requested from GL", nt_gfx_object_kind_name(kind), k_meaning[r]);
                number(ns, q, true, key, meaning);
            }
        }
    }
}

static void operation_numbers(numbers_t *ns) {
    char key[48];
    char meaning[96];
    ns->section = FI_SECTION_OPERATIONS;
    for (uint32_t op = 0; op < NT_GFX_OP_COUNT; op++) {
        const char *name = nt_gfx_operation_name(op);
        for (uint32_t r = 0; r < FI_RESULTS; r++) {
            (void)snprintf(key, sizeof(key), "op.%s.%s", name, nt_gfx_result_name(r));
            (void)snprintf(meaning, sizeof(meaning), "%s requests that ended %s%s", name, nt_gfx_result_name(r), r == NT_GFX_RESULT_CACHE ? " (already set, merged or found)" : "");
            number(ns, (fi_query_t){.kind = FI_Q_OP, .a = op, .b = (uint8_t)r}, false, key, meaning);
        }
        (void)snprintf(key, sizeof(key), "op.%s.gl", name);
        (void)snprintf(meaning, sizeof(meaning), "GL calls %s issued", name);
        number(ns, (fi_query_t){.kind = FI_Q_OP_CALLS, .a = op}, false, key, meaning);
        (void)snprintf(key, sizeof(key), "op.%s.skip", name);
        (void)snprintf(meaning, sizeof(meaning), "GL calls of %s the GL cache left out", name);
        number(ns, (fi_query_t){.kind = FI_Q_OP_SKIPS, .a = op}, false, key, meaning);
        (void)snprintf(key, sizeof(key), "op.%s.nested", name);
        (void)snprintf(meaning, sizeof(meaning), "%s requests issued inside another operation", name);
        number(ns, (fi_query_t){.kind = FI_Q_OP_NESTED, .a = op}, false, key, meaning);
    }
}

static void gl_numbers(numbers_t *ns) {
    const fi_summary_t *sm = &ns->fr->summary;
    char key[48];
    ns->section = FI_SECTION_GL;
    bool listed[NT_GFX_GL_COUNT] = {[NT_GFX_GL_NONE] = true}; /* NONE names no call */
    for (;;) {                                                /* most frequent first */
        uint32_t best = NT_GFX_GL_COUNT;
        for (uint32_t c = 0; c < NT_GFX_GL_COUNT; c++) {
            best = !listed[c] && (ns->all || sm->gl[c].n > 0) && (best == NT_GFX_GL_COUNT || sm->gl[c].n > sm->gl[best].n) ? c : best;
        }
        if (best == NT_GFX_GL_COUNT) {
            return;
        }
        listed[best] = true;
        (void)snprintf(key, sizeof(key), "gl.%s", nt_gfx_gl_call_name(best));
        char meaning[96];
        (void)snprintf(meaning, sizeof(meaning), "%s calls%s", nt_gfx_gl_call_name(best), fi_is_draw_call(best) ? " (GL draw calls)" : "");
        number(ns, (fi_query_t){.kind = FI_Q_GL_CALL, .a = best}, false, key, meaning);
    }
}

uint32_t fi_numbers(const fi_frame_t *frame, bool all, fi_number_t out[FI_NUMBERS]) {
    numbers_t ns = {.out = out, .fr = frame, .all = all};
    frame_numbers(&ns);
    resource_numbers(&ns);
    operation_numbers(&ns);
    gl_numbers(&ns);
    return ns.n;
}

uint64_t fi_number_in(const fi_frame_t *frame, const fi_number_t *number) {
    const fi_cell_t c = fi_cell(frame, number->q);
    return number->sum ? c.sum : c.n;
}

static uint64_t gap(uint64_t x, uint64_t y) { return x > y ? x - y : y - x; }

void fi_number_diff(const fi_frame_t *a, const fi_frame_t *b, fi_number_diff_t *out) {
    out->count = fi_numbers(b, true, out->number);
    for (uint32_t k = 0; k < out->count; k++) { /* fi_numbers lists them by section: sort inside each */
        out->was[k] = fi_number_in(a, &out->number[k]);
        const uint64_t change = gap(out->was[k], out->number[k].value);
        uint32_t at = k;
        for (; at > 0; at--) {
            const uint32_t prev = out->order[at - 1U];
            if (out->number[prev].section != out->number[k].section || gap(out->was[prev], out->number[prev].value) >= change) {
                break;
            }
            out->order[at] = prev;
        }
        out->order[at] = k;
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void fi_change_text(const fi_frame_t *a, const fi_frame_t *b, const fi_diff_row_t *row, char *buf, size_t size) {
    const nt_gfx_event_t *x = &a->view.events[row->a];
    const nt_gfx_event_t *y = &b->view.events[row->b];
    size_t at = 0;
    buf[0] = '\0';
    const char *sep = "";
    if ((row->change & FI_CHANGE_RESULT) != 0) {
        appendf(buf, size, &at, "%s -> %s", nt_gfx_result_name(x->result), nt_gfx_result_name(y->result));
        sep = "; ";
    }
    if ((row->change & FI_CHANGE_MERGE) != 0) {
        appendf(buf, size, &at, "%sbreak: %s -> %s", sep, fi_break_name((fi_break_t)a->brk[row->a]), fi_break_name((fi_break_t)b->brk[row->b]));
        sep = "; ";
    }
    if ((row->change & FI_CHANGE_CALLS) != 0) {
        appendf(buf, size, &at, "%sGL calls %u -> %u", sep, row->calls_a, row->calls_b);
        sep = "; ";
    }
    if ((row->change & FI_CHANGE_ITEMS) != 0) {
        appendf(buf, size, &at, "%s%s differ", sep, x->kind == NT_GFX_EVENT_NOTE ? "fields" : "its records");
        sep = "; ";
    }
    if ((row->change & FI_CHANGE_ARGS) == 0) {
        return;
    }
    if (!fi_is_draw(x)) { /* A's request; B's is on the row */
        char was[400];
        fi_row_text(a, row->a, was, sizeof(was));
        appendf(buf, size, &at, "%sA: %s", sep, was);
        return;
    }
    static const char *const k_names[] = {"first", "count", "vertices", "instances"};
    const uint32_t xs[] = {x->data.draw.first, x->data.draw.count, x->data.draw.vertices, x->data.draw.instances};
    const uint32_t ys[] = {y->data.draw.first, y->data.draw.count, y->data.draw.vertices, y->data.draw.instances};
    for (uint32_t k = 0; k < 4U; k++) {
        if (xs[k] != ys[k]) {
            appendf(buf, size, &at, "%s%s %u -> %u", sep, k_names[k], xs[k], ys[k]);
            sep = "; ";
        }
    }
}

bool fi_group_has_work(const fi_frame_t *frame, uint32_t group) {
    const fi_cell_t *c = &frame->summary.group[(size_t)group * FI_METRIC_COUNT];
    return c[FI_METRIC_DRAWS].n + c[FI_METRIC_CALLS].n + c[FI_METRIC_DROPS].n > 0;
}

uint64_t fi_group_metric(const fi_frame_t *frame, uint32_t group, uint32_t metric) {
    if (group == FI_NONE) {
        return 0;
    }
    const fi_cell_t *c = &frame->summary.group[((size_t)group * FI_METRIC_COUNT) + metric];
    return metric == FI_METRIC_BYTES ? c->sum : c->n;
}

const char *const fi_metric_names[FI_METRIC_COUNT] = {"requests", "GL draws", "GL calls", "upload B", "drops"};

void fi_group_change_text(const fi_frame_t *a, const fi_frame_t *b, const fi_diff_group_t *group, char *buf, size_t size) {
    size_t at = 0;
    buf[0] = '\0';
    const char *sep = "";
    if (group->a == FI_NONE || group->b == FI_NONE) {
        appendf(buf, size, &at, "only in %s", group->a == FI_NONE ? "B" : "A");
        sep = "; ";
    }
    for (uint32_t m = 0; m < FI_METRIC_COUNT; m++) {
        const uint64_t va = fi_group_metric(a, group->a, m);
        const uint64_t vb = fi_group_metric(b, group->b, m);
        if (va != vb) {
            appendf(buf, size, &at, "%s%s %llu -> %llu", sep, fi_metric_names[m], (unsigned long long)va, (unsigned long long)vb);
            sep = "; ";
        }
    }
    if (group->differ > 0) {
        appendf(buf, size, &at, "%s%u of %u requests differ", sep, group->differ, group->count);
        sep = "; ";
    }
    if (!group->aligned) {
        appendf(buf, size, &at, "%stoo different to align: A's requests, then B's", sep);
    }
}

uint32_t fi_diff_fold(const fi_diff_t *diff, const fi_diff_group_t *group, uint32_t r) {
    uint32_t end = r;
    while (end < group->first + group->count && diff->row[end].kind == FI_DIFF_SAME) {
        end++;
    }
    return end - r >= 3U ? end : r;
}

bool fi_group_differs(const fi_frame_t *a, const fi_frame_t *b, const fi_diff_group_t *group) {
    bool differ = group->differ > 0 || group->a == FI_NONE || group->b == FI_NONE;
    for (uint32_t m = 0; m < FI_METRIC_COUNT && !differ; m++) {
        differ = fi_group_metric(a, group->a, m) != fi_group_metric(b, group->b, m);
    }
    return differ;
}
// #endregion

// #region commands
static const char *const k_metrics[FI_METRIC_COUNT] = {"requests", "gl_draws", "gl_calls", "upload_bytes", "drops"};

static void cmd_frames(const fi_session_t *q, fi_out_t *out) {
    put(out, "# frame sequence records gl_draw_calls draw_requests merged gl_calls upload_bytes problems; * marks the frame commands read\n");
    for (uint32_t f = 0, index = 0; f < q->file_count; f++) {
        const fi_file_t *file = &q->files[f];
        put(out, "file %u %s  # %u frames\n", f, file->name, file->frame_count);
        for (uint32_t k = 0; k < file->frame_count; k++, index++) {
            const fi_frame_t *fr = &file->frames[k];
            const char mark = f == q->file && k == q->frame ? '*' : ' ';
            if (fr->view.events == NULL) {
                put(out, "%c %u rejected  # %s\n", mark, index, fr->error != NULL ? fr->error : "?");
                continue;
            }
            const fi_summary_t *sm = &fr->summary;
            const char *note = fr->view.overflow ? "  # events truncated: counters complete, records not" : "";
            note = fr->error != NULL ? "  # incomplete: a record failed to parse" : note;
            put(out, "%c %u %llu %u %llu %llu %llu %llu %llu %llu%s\n", mark, index, (unsigned long long)fr->view.counters.frame_sequence, fr->view.count, (unsigned long long)sm->draw_calls.n,
                (unsigned long long)sm->vertices.n, (unsigned long long)sm->merged.n, (unsigned long long)sm->gl_any.n, (unsigned long long)sm->upload[2].sum, (unsigned long long)sm->problems.n,
                note);
        }
    }
}

static bool parse_number(const char *arg, unsigned long long *value) {
    char *end = NULL;
    *value = strtoull(arg, &end, 10);
    return isdigit((unsigned char)arg[0]) && *end == '\0';
}

static bool cmd_frame(fi_session_t *q, const char *arg, fi_out_t *out) {
    unsigned long long index = 0;
    if (!parse_number(arg, &index)) {
        put(out, "error: frame INDEX (the first column of frames)\n");
        return false;
    }
    if (fi_frame_at(q->files, q->file_count, index, &q->file, &q->frame)) {
        const fi_frame_t *fr = &q->files[q->file].frames[q->frame];
        put(out, "frame %llu  # sequence %llu, file %u %s, %u records\n", index, (unsigned long long)fr->view.counters.frame_sequence, q->file, q->files[q->file].name, fr->view.count);
        return true;
    }
    put(out, "error: no readable frame %s (see frames)\n", arg);
    return false;
}

static const char *const k_sections[] = {"frame", "draws", "uploads", "resources", "operations", "gl calls"};

static void cmd_summary(const fi_frame_t *fr, fi_out_t *out) {
    static fi_number_t numbers[FI_NUMBERS];
    static fi_counter_row_t rows[FI_COUNTER_ROWS];
    put(out, "# key value  # meaning; list KEY shows the records behind a key of the == sections up to counters\n");
    put(out, "frame %llu  # %s, %u records\n", (unsigned long long)fr->view.counters.frame_sequence, fr->view.platform == NT_GFX_CAPTURE_WEB ? "web" : "native", fr->view.count);
    if (fr->error != NULL) {
        put(out, "incomplete %u:%u  # %s; the records before it are read\n", fr->error_line, fr->error_col, fr->error);
    }
    if (fr->view.overflow) {
        put(out, "events_truncated 1  # the counters are complete, the records are not\n");
    }
    if (fr->merge_mismatch != FI_NONE) {
        put(out, "merge_rule_mismatch @%u  # the inspector's merge rule is out of date from this draw\n", fr->merge_mismatch);
    }
    const uint32_t n = fi_numbers(fr, false, numbers);
    for (uint32_t k = 0; k < n; k++) {
        if (k == 0 || numbers[k].section != numbers[k - 1U].section) {
            put(out, "# == %s\n", k_sections[numbers[k].section]);
        }
        put(out, "%-36s %10llu  # %s\n", numbers[k].key, (unsigned long long)numbers[k].value, numbers[k].meaning);
    }
    put(out, "# == counters: the frame's counters against a recount from its records (no list)\n");
    put(out, "%-36s %10u  # counter only: command sizes are not recorded\n", "stream_bytes", fr->view.counters.stream_bytes);
    const uint32_t rows_n = fi_counter_rows(fr, rows);
    uint32_t differ = 0;
    for (uint32_t k = 0; k < rows_n && !fr->view.overflow; k++) {
        if (rows[k].counter != rows[k].recount) {
            put(out, "counter.%-28s %10llu  # the records give %llu\n", rows[k].name, (unsigned long long)rows[k].counter, (unsigned long long)rows[k].recount);
            differ++;
        }
    }
    put(out, "%-36s %10u  # %s\n", "counters_differ", differ, fr->view.overflow ? "events truncated: no check" : "counters that differ from their recount");
}

static void cmd_groups(const fi_frame_t *fr, fi_out_t *out) {
    put(out, "# requests: draw requests; gl_draws: GL draw calls they cost (merged requests share one); gl_calls: every GL call;\n");
    put(out, "# upload_bytes; drops. list gN.METRIC shows the records, e.g. list g1.gl_draws\n");
    put(out, "# group requests gl_draws gl_calls upload_bytes drops  marker path > note\n");
    uint64_t total[FI_METRIC_COUNT] = {0};
    for (uint32_t g = 0; g < fr->group_count; g++) {
        if (!fi_group_has_work(fr, g)) {
            continue;
        }
        const fi_cell_t *c = &fr->summary.group[(size_t)g * FI_METRIC_COUNT];
        put(out, "g%u", g);
        for (uint32_t m = 0; m < FI_METRIC_COUNT; m++) {
            const uint64_t v = m == FI_METRIC_BYTES ? c[m].sum : c[m].n;
            total[m] += v;
            put(out, " %llu", (unsigned long long)v);
        }
        put(out, "  # %s\n", fr->group_key[g]);
    }
    put(out, "total %llu %llu %llu %llu %llu\n", (unsigned long long)total[0], (unsigned long long)total[1], (unsigned long long)total[2], (unsigned long long)total[3], (unsigned long long)total[4]);
}

static bool parse_record(const fi_frame_t *fr, const char *arg, uint32_t *record, fi_out_t *out) {
    unsigned long long v = 0;
    if (arg[0] == '\0') {
        put(out, "error: a record is needed: @N\n");
        return false;
    }
    if (!parse_number(arg[0] == '@' ? arg + 1 : arg, &v) || v >= fr->view.count) {
        put(out, "error: the frame has no record %s (it has %u)\n", arg, fr->view.count);
        return false;
    }
    *record = (uint32_t)v;
    return true;
}

static void put_verdict(const fi_frame_t *fr, uint32_t id, fi_out_t *out) {
    const fi_verdict_t v = fi_verdict(fr, id);
    switch ((fi_verdict_kind_t)v.kind) {
    case FI_VERDICT_MERGED:
        put(out, "merged  # no new GL draw call: its range joined an earlier draw\n");
        if (fi_joined(fr, id) != FI_NONE) {
            put_record(out, fr, "joined ", fi_joined(fr, id));
        }
        if (fi_gl_draw(fr, id) != FI_NONE) {
            put_record(out, fr, "gl_draw ", fi_gl_draw(fr, id));
        }
        break;
    case FI_VERDICT_FAILED:
        put(out, "failed %s  # nothing drawn\n", nt_gfx_result_name(fr->view.events[id].result));
        break;
    case FI_VERDICT_NEW:
        put(out, "new_gl_draw %u of %u\n", v.ordinal, nt_gfx_draw_calls(&fr->view.counters));
        if (!fi_is_mergeable_draw(&fr->view.events[id])) {
            put(out, "not_merged instanced  # joining instanced draws would change gl_InstanceID\n");
        } else if (fr->brk[id] != FI_BREAK_NONE) {
            put(out, "not_merged  # %s\n", fi_break_name((fi_break_t)fr->brk[id]));
            if (fr->cause[id] != FI_NONE) {
                put_record(out, fr, "because ", fr->cause[id]);
            }
        }
        break;
    case FI_VERDICT_NOT_DRAW:
        break;
    }
}

static void cmd_record(const fi_frame_t *fr, uint32_t id, fi_out_t *out) {
    put_record(out, fr, "", id);
    put_verdict(fr, id, out);
    if (fr->parent[id] < FI_NODE_BASE) {
        put_record(out, fr, "inside ", fr->parent[id]);
    } else {
        put(out, "phase %s\n", fi_node_name((fi_node_t)(fr->parent[id] - FI_NODE_BASE)));
    }
    put(out, "group g%u  # %s\n", fr->group[id], fr->group_key[fr->group[id]]);
    fi_named_t named[FI_NAMED];
    const uint32_t n = fi_named_objects(fr, id, named);
    for (uint32_t k = 0; k < n; k++) {
        char alias[FI_ALIAS_LEN];
        fi_alias_text(fr, named[k].kind, named[k].object, alias, sizeof(alias));
        put(out, "object %s %s  # object @%u.%s\n", named[k].field, alias, id, named[k].field);
    }
    for (uint32_t i = id + 1U; i < fr->view.count; i++) {
        if (fr->parent[i] == id) {
            put_record(out, fr, "caused ", i);
        }
    }
    const char *line = NULL;
    const size_t len = fi_record_text(fr, id, &line);
    put(out, "captured %.*s\n", (int)len, line);
}

/* object @N[.FIELD]: the object record N names (by FIELD when it names several), and every record of the frame
 * that names it. */
static bool cmd_object(const fi_frame_t *fr, const char *arg, fi_out_t *out) {
    char at[24];
    const char *dot = strchr(arg, '.');
    const size_t len = dot != NULL ? (size_t)(dot - arg) : strlen(arg);
    if (len >= sizeof(at)) {
        put(out, "error: the frame has no record %.*s\n", (int)len, arg);
        return false;
    }
    (void)snprintf(at, sizeof(at), "%.*s", (int)len, arg);
    uint32_t record = 0;
    if (!parse_record(fr, at, &record, out)) {
        return false;
    }
    fi_named_t named[FI_NAMED];
    const uint32_t n = fi_named_objects(fr, record, named);
    uint32_t pick = dot == NULL && n > 0 ? 0U : FI_NONE;
    for (uint32_t k = 0; dot != NULL && k < n && pick == FI_NONE; k++) {
        pick = strcmp(named[k].field, dot + 1) == 0 ? k : FI_NONE;
    }
    if (pick == FI_NONE) {
        put(out, "error: record @%u names no object%s%s; record @%u lists its objects\n", record, dot != NULL ? " by " : "", dot != NULL ? dot + 1 : "", record);
        return false;
    }
    fi_object_t obj;
    fi_object(fr, named[pick].kind, named[pick].object, &obj);
    char alias[FI_ALIAS_LEN];
    fi_alias_text(fr, obj.kind, obj.object, alias, sizeof(alias));
    put(out, "object %s  # %s %u\n", alias, nt_gfx_object_kind_name(obj.kind), obj.object);
    if (fr->view.overflow || fr->error != NULL) {
        put(out, "incomplete  # the frame %s: records after it are missing\n", fr->view.overflow ? "overflowed" : "parsed only up to an error");
    }
    if (obj.definition != FI_NONE) {
        put_record(out, fr, "definition ", obj.definition);
    } else {
        put(out, "definition none  # the frame holds no CREATE definition of it\n");
    }
    for (uint32_t k = 0; k < obj.span_count; k++) {
        put(out, "gl_name %u from @%u\n", obj.span[k].name, obj.span[k].from);
    }
    put(out, "records %u\n", obj.count);
    uint32_t group = FI_NONE;
    for (uint32_t k = 0; k < obj.count; k++) {
        const uint32_t g = fr->group[obj.record[k]];
        if (g != group) {
            put(out, "group g%u  # %s\n", g, fr->group_key[g]);
            group = g;
        }
        put_record(out, fr, "  ", obj.record[k]);
    }
    fi_object_free(&obj);
    return true;
}

static void put_state_record(fi_out_t *out, const fi_frame_t *fr, const char *prefix, const fi_state_line_t *l, uint32_t record) {
    char body[600];
    fi_state_value(fr, l, record, body, sizeof(body));
    const char *word = fi_result_word(&fr->view.events[record]);
    const bool badge = word[0] != '\0';
    put(out, "%s@%u  %s%s%s%s\n", prefix, record, body, badge ? "  [" : "", word, badge ? "]" : "");
}

static void put_state_line(const fi_frame_t *fr, const fi_state_line_t *l, fi_out_t *out) {
    char prefix[96];
    (void)snprintf(prefix, sizeof(prefix), "%c %-26s %s", l->changed ? '*' : ' ', l->name, l->inherited ? "inherited " : "");
    if (l->now == FI_NONE) {
        put(out, "%sunset\n", prefix);
    } else {
        put_state_record(out, fr, prefix, l, l->now);
    }
    if (l->changed && l->before != FI_NONE) {
        put_state_record(out, fr, "    was ", l, l->before);
    } else if (l->changed) {
        put(out, "    was unset\n");
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool cmd_state(const fi_frame_t *fr, const char *arg, const char *flag, fi_out_t *out) {
    static fi_state_view_t view;
    static fi_state_line_t lines[FI_STATE_LINES];
    const bool all = strcmp(flag, "all") == 0;
    const bool frame_end = strcmp(arg, "end") == 0;
    uint32_t at = 0;
    if (flag[0] != '\0' && !all) {
        put(out, "error: state @N|init|end [all]\n");
        return false;
    }
    if (frame_end || strcmp(arg, "init") == 0) {
        if (!fi_state_frame(fr, frame_end, &view)) {
            put(out, "error: the frame recorded no begin_frame state\n");
            return false;
        }
        put(out, frame_end ? "end  # state after the frame's last record\n" : "init  # what begin_frame inherited: where the game's first call starts\n");
    } else if (parse_record(fr, arg, &at, out)) {
        fi_state_at(fr, at, &view);
        put_record(out, fr, "at ", at);
    } else {
        return false;
    }
    put(out, "# engine state; *: a different value than at the compared record\n");
    if (view.was != FI_NONE) {
        char prefix[64];
        (void)snprintf(prefix, sizeof(prefix), "compared %s ", fi_compare_name(view.compare, false));
        put_record(out, fr, prefix, view.was);
    }
    const uint32_t n = fi_state_lines(fr, &view, all, lines);
    uint32_t k = 0;
    for (; k < n && !lines[k].gl; k++) {
        put_state_line(fr, &lines[k], out);
    }
    put(out, "# GL state at @%u%s\n", view.gl_at, view.was_gl_at != FI_NONE && !all ? "; changes only, add all for the rest" : "");
    if (view.was_gl_at != FI_NONE) {
        char prefix[64];
        (void)snprintf(prefix, sizeof(prefix), "compared %s ", fi_compare_name(view.compare, true));
        put_record(out, fr, prefix, view.was_gl_at);
    }
    for (; k < n; k++) {
        put_state_line(fr, &lines[k], out);
    }
    if (view.gl.full) {
        put(out, "gl_state_full 1  # more GL state than the inspector tracks: the rest is not shown\n");
    }
    return true;
}

static void put_matches(const fi_frame_t *fr, fi_query_t q, const char *needle, fi_out_t *out) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < fr->view.count; i++) {
        bool keep = false;
        if (q.kind != FI_Q_NONE) {
            keep = fi_matches(fr, i, q);
        } else {
            const char *line = NULL;
            const size_t len = fi_record_text(fr, i, &line);
            keep = fi_contains_ci(line, len, needle);
        }
        if (keep) {
            put_record(out, fr, "", i);
            n++;
        }
    }
    put(out, "records %u\n", n);
}

/* The query behind KEY: a number of summary, or gN.METRIC of groups. */
static bool key_query(const fi_frame_t *fr, const char *key, fi_query_t *q) {
    static fi_number_t numbers[FI_NUMBERS];
    const uint32_t n = fi_numbers(fr, true, numbers);
    for (uint32_t k = 0; k < n; k++) {
        if (strcmp(numbers[k].key, key) == 0) {
            *q = numbers[k].q;
            return true;
        }
    }
    const char *dot = strchr(key, '.');
    unsigned long long g = 0;
    char index[16] = "";
    if (key[0] == 'g' && dot != NULL && (size_t)(dot - key) < sizeof(index)) {
        memcpy(index, key + 1, (size_t)(dot - key - 1));
    }
    for (uint32_t m = 0; index[0] != '\0' && parse_number(index, &g) && g < fr->group_count && m < FI_METRIC_COUNT; m++) {
        if (strcmp(dot + 1, k_metrics[m]) == 0) {
            *q = (fi_query_t){.kind = FI_Q_GROUP, .a = (uint32_t)g, .b = (uint8_t)m};
            return true;
        }
    }
    return false;
}

// #region compare
static uint64_t frame_index(const fi_session_t *q, uint32_t file, uint32_t frame) {
    uint64_t index = frame;
    for (uint32_t f = 0; f < file; f++) {
        index += q->files[f].frame_count;
    }
    return index;
}

static void put_side(fi_out_t *out, const char *name, uint64_t index, const fi_file_t *file, const fi_frame_t *fr) {
    put(out, "%s frame %llu  # sequence %llu, file %s, %s, %u records\n", name, (unsigned long long)index, (unsigned long long)fr->view.counters.frame_sequence, file->name,
        fr->view.platform == NT_GFX_CAPTURE_WEB ? "web" : "native", fr->view.count);
}

/* A group of a comparison by the frame that has it: gN in B, a.gN when only A has it. */
static void group_name(const fi_diff_group_t *g, char *buf, size_t size) {
    if (g->b != FI_NONE) {
        (void)snprintf(buf, size, "g%u", g->b);
    } else {
        (void)snprintf(buf, size, "a.g%u", g->a);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void compare_numbers(const fi_frame_t *a, const fi_frame_t *b, const fi_diff_t *d, fi_out_t *out) {
    static fi_number_diff_t nd;
    fi_number_diff(a, b, &nd);
    uint32_t differ = 0;
    uint32_t section = FI_NONE;
    for (uint32_t c = 0; c < nd.count; c++) {
        const uint32_t k = nd.order[c];
        const fi_number_t *x = &nd.number[k];
        if (x->value == nd.was[k]) {
            continue;
        }
        if (x->section != section) {
            section = x->section;
            put(out, "# == %s\n", k_sections[section]);
        }
        put(out, "%-36s %10llu %10llu %+11lld  # %s\n", x->key, (unsigned long long)nd.was[k], (unsigned long long)x->value, (long long)(x->value - nd.was[k]), x->meaning);
        differ++;
    }
    put(out, "# == groups: gN.METRIC A B delta (a.gN: a group only A has); gN.commands N  # requests that differ,\n");
    put(out, "# compare INDEX gN lists them\n");
    char name[24];
    char metric[48];
    for (uint32_t g = 0; g < d->group_count; g++) {
        const fi_diff_group_t *dg = &d->group[g];
        group_name(dg, name, sizeof(name));
        const char *key = dg->b != FI_NONE ? b->group_key[dg->b] : a->group_key[dg->a];
        const char *only = "";
        if (dg->a == FI_NONE || dg->b == FI_NONE) {
            only = dg->a == FI_NONE ? "only in B: " : "only in A: ";
        }
        for (uint32_t m = 0; m < FI_METRIC_COUNT; m++) {
            const uint64_t va = fi_group_metric(a, dg->a, m);
            const uint64_t vb = fi_group_metric(b, dg->b, m);
            if (va != vb) {
                (void)snprintf(metric, sizeof(metric), "%s.%s", name, k_metrics[m]);
                put(out, "%-36s %10llu %10llu %+11lld  # %s%s\n", metric, (unsigned long long)va, (unsigned long long)vb, (long long)(vb - va), only, key);
                differ++;
            }
        }
        if (dg->differ > 0) {
            (void)snprintf(metric, sizeof(metric), "%s.commands", name);
            put(out, "%-36s %10u  # %s%s: requests that differ, of %u rows\n", metric, dg->differ, only, key, dg->count);
            differ++;
        }
    }
    put(out, "differ %u  # numbers and group lines above\n", differ);
}

static void put_diff_row(const fi_frame_t *a, const fi_frame_t *b, const fi_diff_row_t *r, fi_out_t *out) {
    char ra[16] = "-";
    char rb[16] = "-";
    if (r->a != FI_NONE) {
        (void)snprintf(ra, sizeof(ra), "@%u", r->a);
    }
    if (r->b != FI_NONE) {
        (void)snprintf(rb, sizeof(rb), "@%u", r->b);
    }
    char body[600];
    fi_row_text(r->b != FI_NONE ? b : a, r->b != FI_NONE ? r->b : r->a, body, sizeof(body));
    char change[256] = "";
    if (r->kind == FI_DIFF_CHANGED) {
        fi_change_text(a, b, r, change, sizeof(change));
    }
    put(out, "%c %-7s %-7s %s%s%s\n", "=~-+"[r->kind], ra, rb, body, change[0] != '\0' ? "  # " : "", change);
}

/* A group's commands side by side; runs of three or more same rows print as one line. */
static bool compare_group(const fi_frame_t *a, const fi_frame_t *b, const fi_diff_t *d, const char *wanted, fi_out_t *out) {
    char name[24];
    for (uint32_t g = 0; g < d->group_count; g++) {
        const fi_diff_group_t *dg = &d->group[g];
        group_name(dg, name, sizeof(name));
        if (strcmp(name, wanted) != 0) {
            continue;
        }
        put(out, "# = same, ~ changed, - only A, + only B; then the records in A and B and the request  # what changed\n");
        char change[256];
        fi_group_change_text(a, b, dg, change, sizeof(change));
        put(out, "group %s  # %s%s%s\n", name, dg->b != FI_NONE ? b->group_key[dg->b] : a->group_key[dg->a], change[0] != '\0' ? "; " : "", change);
        for (uint32_t r = dg->first; r < dg->first + dg->count;) {
            const uint32_t end = fi_diff_fold(d, dg, r);
            if (end > r) {
                put(out, "= %u same  # @%u..@%u in A, @%u..@%u in B\n", end - r, d->row[r].a, d->row[end - 1U].a, d->row[r].b, d->row[end - 1U].b);
                r = end;
                continue;
            }
            put_diff_row(a, b, &d->row[r], out);
            r++;
        }
        put(out, "rows %u differ %u\n", dg->count, dg->differ);
        return true;
    }
    put(out, "error: no group %s in this comparison (compare INDEX lists them)\n", wanted);
    return false;
}

static bool cmd_compare(const fi_session_t *q, const fi_frame_t *fr, const char *arg, const char *group, fi_out_t *out) {
    unsigned long long index = 0;
    uint32_t file = 0;
    uint32_t frame = 0;
    if (!parse_number(arg, &index)) {
        put(out, "error: compare INDEX [GROUP] (INDEX: the base frame A, the first column of frames)\n");
        return false;
    }
    if (!fi_frame_at(q->files, q->file_count, index, &file, &frame)) {
        put(out, "error: no readable frame %s (see frames)\n", arg);
        return false;
    }
    const fi_frame_t *base = &q->files[file].frames[frame];
    put(out, "# key A B delta  # meaning, for what differs: the biggest change first in each section\n");
    put_side(out, "a", index, &q->files[file], base);
    put_side(out, "b", frame_index(q, q->file, q->frame), &q->files[q->file], fr);
    fi_diff_t d;
    fi_diff(base, fr, &d);
    bool ok = true;
    if (group[0] == '\0') {
        compare_numbers(base, fr, &d, out);
    } else {
        ok = compare_group(base, fr, &d, group, out);
    }
    fi_diff_free(&d);
    return ok;
}
// #endregion

static void cmd_help(fi_out_t *out) {
    put(out, "# every answer is lines of: key value  # meaning. Records read @N. --serve ends each answer with = ok or = error.\n"
             "frames                     # every frame of the open files; * marks the one commands read\n"
             "frame INDEX                # read this frame: the first column of frames\n"
             "summary                    # the frame's numbers, each with a key for list\n"
             "groups                     # per marker and note: requests, GL draws, GL calls, upload bytes, drops\n"
             "record @N                  # the record, merged or a new GL draw and why, its parent, group, objects and GL calls\n"
             "object @N[.FIELD]          # the object record N names (by FIELD of its object lines): its definition, its GL\n"
             "                           # names, and every record of the frame that names it, by group\n"
             "state @N|init|end [all]    # engine and GL state; * marks a change against the previous draw or the start\n"
             "find TEXT                  # records whose captured line holds TEXT (any case)\n"
             "list KEY                   # the records behind a number: a summary key or gN.METRIC of groups\n"
             "compare INDEX [GROUP]      # this frame (B) against frame INDEX (A): numbers and groups that differ; with a\n"
             "                           # group of its list (gN, a.gN), that group's commands aligned\n"
             "# rows: each record's fields by name, values decoded (GL_LEQUAL, U16, tex/atlas); record @N adds the line as\n"
             "# captured, raw. Zero fields are left out; a request's unset render target or sampler reads as its default.\n"
             "# objects: kind/label#N (buf/frame_uniform#1), kind#N when unlabeled; N counts first use in the frame's work,\n"
             "# then the snapshot's, per kind and label. A GL name reads with the object it belonged to at that record\n"
             "# (glUseProgram(19 prog/sprite#1)), or its role (upload VAO, timer query); a bare number: the frame never said.\n"
             "# badges: [merged] joined the previous GL draw, [already set] the engine held that state, [found] a cached\n"
             "# object, [GL skipped] the GL cache held that value (its cache key follows the call name).\n");
}

static const char *const k_commands[] = {"help", "frames", "frame", "summary", "groups", "record", "state", "find", "list", "compare", "object"};

bool fi_query_command(const char *word) {
    for (uint32_t k = 0; k < sizeof(k_commands) / sizeof(k_commands[0]); k++) {
        if (strcmp(word, k_commands[k]) == 0) {
            return true;
        }
    }
    return false;
}

/* The next whitespace-separated word of *line into buf; *line moves past it and the spaces after it. False when
 * the word does not fit. */
static bool next_word(const char **line, char *buf, size_t size) {
    const char *p = *line;
    size_t n = 0;
    while (*p != '\0' && !isspace((unsigned char)*p)) {
        if (n + 1U < size) {
            buf[n++] = *p;
        }
        p++;
    }
    buf[n] = '\0';
    const bool fits = (size_t)(p - *line) < size;
    while (isspace((unsigned char)*p)) {
        p++;
    }
    *line = p;
    return fits;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
bool fi_query(fi_session_t *session, const char *text, fi_out_t *out) {
    char line[1024];
    while (isspace((unsigned char)*text)) {
        text++;
    }
    (void)snprintf(line, sizeof(line), "%s", text);
    for (size_t n = strlen(line); n > 0 && isspace((unsigned char)line[n - 1U]); n--) {
        line[n - 1U] = '\0';
    }
    const char *p = line;
    char command[16];
    char arg[160];
    const bool command_fits = next_word(&p, command, sizeof(command));
    const char *rest = p; /* the argument and what follows it */
    const bool arg_fits = next_word(&p, arg, sizeof(arg));
    if (!command_fits || !fi_query_command(command)) {
        put(out, "error: unknown command %s; help lists them\n", command);
        return false;
    }
    if (!arg_fits && strcmp(command, "find") != 0) { /* find reads the whole rest */
        put(out, "error: an argument is longer than %u characters\n", (unsigned)sizeof(arg) - 1U);
        return false;
    }
    const bool bare = strcmp(command, "help") == 0 || strcmp(command, "frames") == 0 || strcmp(command, "summary") == 0 || strcmp(command, "groups") == 0;
    if ((bare && rest[0] != '\0') || ((strcmp(command, "record") == 0 || strcmp(command, "frame") == 0) && p[0] != '\0')) {
        put(out, "error: too many arguments: %s; help lists the commands\n", rest);
        return false;
    }
    if (strcmp(command, "help") == 0) {
        cmd_help(out);
        return true;
    }
    if (strcmp(command, "frames") == 0) {
        cmd_frames(session, out);
        return true;
    }
    if (strcmp(command, "frame") == 0) {
        return cmd_frame(session, arg, out);
    }
    const fi_frame_t *fr = &session->files[session->file].frames[session->frame];
    if (fr->view.events == NULL) {
        put(out, "error: the frame's header was rejected: %s\n", fr->error != NULL ? fr->error : "?");
        return false;
    }
    if (strcmp(command, "summary") == 0) {
        cmd_summary(fr, out);
        return true;
    }
    if (strcmp(command, "groups") == 0) {
        cmd_groups(fr, out);
        return true;
    }
    if (strcmp(command, "state") == 0) {
        return cmd_state(fr, arg, p, out);
    }
    if (strcmp(command, "compare") == 0) {
        char group[24];
        if (!next_word(&p, group, sizeof(group)) || p[0] != '\0') {
            put(out, "error: compare INDEX [GROUP]; help lists the commands\n");
            return false;
        }
        return cmd_compare(session, fr, arg, group, out);
    }
    uint32_t record = 0;
    if (strcmp(command, "record") == 0) {
        if (!parse_record(fr, arg, &record, out)) {
            return false;
        }
        cmd_record(fr, record, out);
        return true;
    }
    if (strcmp(command, "object") == 0 && arg[0] != '\0' && p[0] == '\0') {
        return cmd_object(fr, arg, out);
    }
    if (strcmp(command, "find") == 0 && rest[0] != '\0') {
        put_matches(fr, (fi_query_t){0}, rest, out);
        return true;
    }
    fi_query_t q = {0};
    if (strcmp(command, "list") == 0 && p[0] == '\0' && key_query(fr, arg, &q)) {
        put_matches(fr, q, "", out);
        return true;
    }
    const char *needs = "a key of summary or gN.METRIC of groups";
    if (strcmp(command, "find") == 0) {
        needs = "TEXT";
    } else if (strcmp(command, "object") == 0) {
        needs = "@N[.FIELD]";
    }
    put(out, "error: %s needs %s; help lists the commands\n", command, needs);
    return false;
}
// #endregion
