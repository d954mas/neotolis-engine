#include "graphics/nt_gfx_capture_text.h"

#include "core/nt_assert.h"
#include "log/nt_log.h"

#include <inttypes.h>
#include <locale.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// #region names
#define NT_NAME_(prefix, x) [prefix##x] = #x
static const char *const s_operation_names[NT_GFX_OP_COUNT] = {
    NT_NAME_(NT_GFX_OP_, PASS),        NT_NAME_(NT_GFX_OP_, END_PASS),       NT_NAME_(NT_GFX_OP_, CREATE),         NT_NAME_(NT_GFX_OP_, DESTROY),
    NT_NAME_(NT_GFX_OP_, PIPELINE),    NT_NAME_(NT_GFX_OP_, VERTEX_INPUT),   NT_NAME_(NT_GFX_OP_, TEXTURE_SET),    NT_NAME_(NT_GFX_OP_, TEXTURE),
    NT_NAME_(NT_GFX_OP_, SAMPLER),     NT_NAME_(NT_GFX_OP_, VIEWPORT),       NT_NAME_(NT_GFX_OP_, SCISSOR),        NT_NAME_(NT_GFX_OP_, SCISSOR_ENABLE),
    NT_NAME_(NT_GFX_OP_, UBO),         NT_NAME_(NT_GFX_OP_, BUFFER_UPLOAD),  NT_NAME_(NT_GFX_OP_, TEXTURE_UPLOAD), NT_NAME_(NT_GFX_OP_, ATTRIBUTE),
    NT_NAME_(NT_GFX_OP_, DRAW),        NT_NAME_(NT_GFX_OP_, DRAW_INSTANCED), NT_NAME_(NT_GFX_OP_, DRAW_INDEXED),   NT_NAME_(NT_GFX_OP_, DRAW_INDEXED_INSTANCED),
    NT_NAME_(NT_GFX_OP_, CONTEXT),     NT_NAME_(NT_GFX_OP_, STATE),          NT_NAME_(NT_GFX_OP_, READ_PIXELS),    NT_NAME_(NT_GFX_OP_, SEGMENT_BEGIN),
    NT_NAME_(NT_GFX_OP_, SEGMENT_END), NT_NAME_(NT_GFX_OP_, SEGMENT_POLL),   NT_NAME_(NT_GFX_OP_, GPU_TIMING),     NT_NAME_(NT_GFX_OP_, TIMER_DISJOINT),
    NT_NAME_(NT_GFX_OP_, CLEAR),
#if !defined(NT_PLATFORM_WEB)
    NT_NAME_(NT_GFX_OP_, BUFFER_READ), NT_NAME_(NT_GFX_OP_, BUFFER_WAIT),
#endif
};

static const char *const s_kind_names[] = {
    NT_NAME_(NT_GFX_EVENT_, OP),       NT_NAME_(NT_GFX_EVENT_, BACKEND), NT_NAME_(NT_GFX_EVENT_, SKIP),       NT_NAME_(NT_GFX_EVENT_, INITIAL),     NT_NAME_(NT_GFX_EVENT_, DEFINITION),
    NT_NAME_(NT_GFX_EVENT_, ARGUMENT), NT_NAME_(NT_GFX_EVENT_, NOTE),    NT_NAME_(NT_GFX_EVENT_, NOTE_FIELD), NT_NAME_(NT_GFX_EVENT_, MARKER_PUSH), NT_NAME_(NT_GFX_EVENT_, MARKER_POP),
};

static const char *const s_object_names[] = {
    NT_NAME_(NT_GFX_OBJECT_, NONE),   NT_NAME_(NT_GFX_OBJECT_, SHADER),  NT_NAME_(NT_GFX_OBJECT_, PROGRAM), NT_NAME_(NT_GFX_OBJECT_, PIPELINE),      NT_NAME_(NT_GFX_OBJECT_, VERTEX_INPUT),
    NT_NAME_(NT_GFX_OBJECT_, BUFFER), NT_NAME_(NT_GFX_OBJECT_, TEXTURE), NT_NAME_(NT_GFX_OBJECT_, SAMPLER), NT_NAME_(NT_GFX_OBJECT_, RENDER_TARGET), NT_NAME_(NT_GFX_OBJECT_, INSTANCE_LAYOUT),
};

static const char *const s_result_names[] = {
    NT_NAME_(NT_GFX_RESULT_, NONE),         NT_NAME_(NT_GFX_RESULT_, ACCEPTED),       NT_NAME_(NT_GFX_RESULT_, CACHE),   NT_NAME_(NT_GFX_RESULT_, INACTIVE),
    NT_NAME_(NT_GFX_RESULT_, CONTEXT_LOST), NT_NAME_(NT_GFX_RESULT_, INVALID_HANDLE), NT_NAME_(NT_GFX_RESULT_, UNREADY), NT_NAME_(NT_GFX_RESULT_, UNKNOWN),
};

static const char *const s_note_type_names[] = {
    NT_NAME_(NT_GFX_NOTE_, U32), NT_NAME_(NT_GFX_NOTE_, I32), NT_NAME_(NT_GFX_NOTE_, U64), NT_NAME_(NT_GFX_NOTE_, F32), NT_NAME_(NT_GFX_NOTE_, STR),
};
#undef NT_NAME_

static const char *const s_platform_names[] = {[NT_GFX_CAPTURE_NATIVE] = "native", [NT_GFX_CAPTURE_WEB] = "web"};
static const char *const s_layer_names[] = {[NT_GFX_INITIAL_FRONTEND] = "FRONTEND", [NT_GFX_INITIAL_BACKEND] = "BACKEND"};

_Static_assert(sizeof(s_kind_names) / sizeof(s_kind_names[0]) == NT_GFX_EVENT_MARKER_POP + 1, "every event kind has a name");
_Static_assert(sizeof(s_object_names) / sizeof(s_object_names[0]) == NT_GFX_OBJECT_INSTANCE_LAYOUT + 1, "every object kind has a name");
_Static_assert(sizeof(s_result_names) / sizeof(s_result_names[0]) == NT_GFX_RESULT_UNKNOWN + 1, "every result has a name");
_Static_assert(sizeof(s_note_type_names) / sizeof(s_note_type_names[0]) == NT_GFX_NOTE_STR + 1, "every note type has a name");
_Static_assert(sizeof(s_platform_names) / sizeof(s_platform_names[0]) == NT_GFX_CAPTURE_WEB + 1, "every platform has a name");

const char *nt_gfx_gl_call_name(uint32_t call) {
#define NT_GFX_GL_CALL_NAME_(name) #name,
    static const char *const names[NT_GFX_GL_COUNT] = {NULL, NT_GFX_GL_CALLS(NT_GFX_GL_CALL_NAME_)};
#undef NT_GFX_GL_CALL_NAME_
    return call < NT_GFX_GL_COUNT ? names[call] : NULL;
}

#define NT_COUNT_(table) ((uint32_t)(sizeof(table) / sizeof((table)[0])))

/* Tables are complete by the asserts above (and a test for operations); a value outside one is a broken record. */
static const char *table_name(const char *const *names, uint32_t count, uint32_t value) {
    NT_ASSERT(value < count && names[value] != NULL && "capture text: value outside its name table");
    return names[value];
}
#define NT_TABLE_NAME_(table, value) table_name((table), NT_COUNT_(table), (value))
// #endregion

// #region layout
/* Field lists shared by format and parse, so the two directions cannot drift. */
typedef enum {
    ARM_NONE,
    ARM_BACKEND,
    ARM_KEY, /* a backend cache skip: the skipped call's cache key */
    ARM_RESOURCE,
    ARM_DRAW,
    ARM_BINDING,
    ARM_PASS,
    ARM_CLEAR,
    ARM_ATTRIBUTE,
    ARM_STATE,
    ARM_COUNT,
} arm_t;

typedef enum { F_U32, F_U64, F_F32, F_BOOL, F_U32S, F_F32S } field_type_t;

typedef struct {
    const char *name;
    uint16_t offset; /* into the record */
    uint8_t type;    /* field_type_t */
    uint8_t size;    /* list capacity; 1 for scalars */
} field_t;

#define NT_FIELD_(arm, member, type, size) #member, (uint16_t)offsetof(nt_gfx_event_t, data.arm.member), (type), (size)
static const field_t k_backend[] = {{NT_FIELD_(backend, args, F_U32S, 12)}, {NT_FIELD_(backend, values, F_F32S, 4)}, {NT_FIELD_(backend, bytes, F_U64, 1)}};
static const field_t k_key[] = {{"key", (uint16_t)offsetof(nt_gfx_event_t, data.backend.args), F_U32S, 12}, {NT_FIELD_(backend, values, F_F32S, 4)}, {NT_FIELD_(backend, bytes, F_U64, 1)}};
static const field_t k_resource[] = {
    {NT_FIELD_(resource, size, F_U64, 1)},   {NT_FIELD_(resource, backend, F_U32, 1)}, {NT_FIELD_(resource, related, F_U32S, 3)}, {NT_FIELD_(resource, type, F_U32, 1)},
    {NT_FIELD_(resource, usage, F_U32, 1)},  {NT_FIELD_(resource, width, F_U32, 1)},   {NT_FIELD_(resource, height, F_U32, 1)},   {NT_FIELD_(resource, format, F_U32, 1)},
    {NT_FIELD_(resource, levels, F_U32, 1)}, {NT_FIELD_(resource, flags, F_U32, 1)},
};
static const field_t k_draw[] = {
    {NT_FIELD_(draw, first, F_U32, 1)}, {NT_FIELD_(draw, count, F_U32, 1)}, {NT_FIELD_(draw, vertices, F_U32, 1)}, {NT_FIELD_(draw, instances, F_U32, 1)}, {NT_FIELD_(draw, vertex_input, F_U32, 1)}};
static const field_t k_binding[] = {{NT_FIELD_(binding, secondary, F_U32, 1)},
                                    {NT_FIELD_(binding, name, F_U32, 1)},
                                    {NT_FIELD_(binding, slot, F_U32, 1)},
                                    {NT_FIELD_(binding, offset, F_U32, 1)},
                                    {NT_FIELD_(binding, size, F_U32, 1)}};
static const field_t k_pass[] = {
    {NT_FIELD_(pass, target, F_U32, 1)},      {NT_FIELD_(pass, color, F_F32S, 4)},         {NT_FIELD_(pass, depth, F_F32, 1)},          {NT_FIELD_(pass, load_color, F_BOOL, 1)},
    {NT_FIELD_(pass, load_depth, F_BOOL, 1)}, {NT_FIELD_(pass, discard_color, F_BOOL, 1)}, {NT_FIELD_(pass, discard_depth, F_BOOL, 1)},
};
static const field_t k_clear[] = {{NT_FIELD_(clear, clear_color, F_F32S, 4)}, {NT_FIELD_(clear, clear_depth, F_F32, 1)}, {NT_FIELD_(clear, color, F_BOOL, 1)}, {NT_FIELD_(clear, depth, F_BOOL, 1)}};
static const field_t k_attribute[] = {
    {NT_FIELD_(attribute, buffer, F_U32, 1)}, {NT_FIELD_(attribute, offset, F_U32, 1)}, {NT_FIELD_(attribute, stride, F_U32, 1)},     {NT_FIELD_(attribute, location, F_U32, 1)},
    {NT_FIELD_(attribute, type, F_U32, 1)},   {NT_FIELD_(attribute, count, F_U32, 1)},  {NT_FIELD_(attribute, normalized, F_U32, 1)}, {NT_FIELD_(attribute, divisor, F_U32, 1)},
};
static const field_t k_state[] = {{NT_FIELD_(state, integers, F_U32S, 14)}, {NT_FIELD_(state, values, F_F32S, 6)}};
#undef NT_FIELD_

#define NT_ARM_(fields, member) (fields), NT_COUNT_(fields), sizeof(((nt_gfx_event_t *)NULL)->data.member)
static const struct {
    const field_t *fields;
    uint32_t count;
    size_t bytes; /* data bytes the arm may set; the rest stays zero */
} k_arms[ARM_COUNT] = {
    [ARM_BACKEND] = {NT_ARM_(k_backend, backend)}, [ARM_KEY] = {NT_ARM_(k_key, backend)}, [ARM_RESOURCE] = {NT_ARM_(k_resource, resource)}, [ARM_DRAW] = {NT_ARM_(k_draw, draw)},
    [ARM_BINDING] = {NT_ARM_(k_binding, binding)}, [ARM_PASS] = {NT_ARM_(k_pass, pass)},  [ARM_CLEAR] = {NT_ARM_(k_clear, clear)},          [ARM_ATTRIBUTE] = {NT_ARM_(k_attribute, attribute)},
    [ARM_STATE] = {NT_ARM_(k_state, state)},
};
#undef NT_ARM_

static bool is_draw(uint32_t operation) {
    return operation == NT_GFX_OP_DRAW || operation == NT_GFX_OP_DRAW_INSTANCED || operation == NT_GFX_OP_DRAW_INDEXED || operation == NT_GFX_OP_DRAW_INDEXED_INSTANCED;
}

/* Which data arm a gfx record fills; the render architecture spec, "Frame observation", names them. */
static arm_t record_arm(const nt_gfx_event_t *e) {
    switch (e->kind) {
    case NT_GFX_EVENT_BACKEND:
        return ARM_BACKEND;
    case NT_GFX_EVENT_SKIP:
        return e->detail != 0 ? ARM_KEY : ARM_BINDING;
    case NT_GFX_EVENT_ARGUMENT:
        return ARM_BINDING;
    case NT_GFX_EVENT_DEFINITION:
        switch (e->operation) {
        case NT_GFX_OP_ATTRIBUTE:
            return ARM_ATTRIBUTE;
        case NT_GFX_OP_PIPELINE:
            return ARM_STATE;
        case NT_GFX_OP_STATE:
            return ARM_BACKEND;
        default:
            return ARM_RESOURCE;
        }
    case NT_GFX_EVENT_INITIAL:
        switch (e->operation) {
        case NT_GFX_OP_STATE:
            return e->detail == NT_GFX_INITIAL_BACKEND ? ARM_BACKEND : ARM_STATE;
        case NT_GFX_OP_SAMPLER:
        case NT_GFX_OP_TEXTURE:
            return ARM_BACKEND;
        case NT_GFX_OP_PASS:
            return ARM_PASS;
        default:
            return ARM_STATE;
        }
    case NT_GFX_EVENT_OP:
        if (is_draw(e->operation)) {
            return ARM_DRAW;
        }
        switch (e->operation) {
        case NT_GFX_OP_CREATE:
        case NT_GFX_OP_BUFFER_UPLOAD:
            return ARM_RESOURCE;
        case NT_GFX_OP_PASS:
            return ARM_PASS;
        case NT_GFX_OP_CLEAR:
            return ARM_CLEAR;
        case NT_GFX_OP_VERTEX_INPUT:
        case NT_GFX_OP_UBO:
        case NT_GFX_OP_SEGMENT_BEGIN:
        case NT_GFX_OP_SEGMENT_POLL:
            return ARM_BINDING;
        default:
            return ARM_STATE;
        }
    default:
        return ARM_NONE;
    }
}

/* How `detail` prints. It decides the arm of SKIP and INITIAL/STATE, so a reader takes it before the fields. */
typedef enum { DETAIL_PLAIN, DETAIL_CALL, DETAIL_RESOURCE, DETAIL_LAYER, DETAIL_JOINED } detail_kind_t;

static detail_kind_t detail_kind(const nt_gfx_event_t *e) {
    if (e->kind == NT_GFX_EVENT_BACKEND || e->kind == NT_GFX_EVENT_SKIP) {
        return DETAIL_CALL; /* required on BACKEND; a SKIP without one is an inactive texture-set name */
    }
    if (e->operation == NT_GFX_OP_STATE && e->kind == NT_GFX_EVENT_DEFINITION) {
        return DETAIL_RESOURCE;
    }
    if (e->operation == NT_GFX_OP_STATE && e->kind == NT_GFX_EVENT_INITIAL) {
        return DETAIL_LAYER;
    }
    return e->kind == NT_GFX_EVENT_OP && is_draw(e->operation) ? DETAIL_JOINED : DETAIL_PLAIN; /* a merged draw keeps the link of the draw it joined */
}

static bool issued_call(const nt_gfx_event_t *e, arm_t arm) { return arm == ARM_KEY || (arm == ARM_BACKEND && e->kind == NT_GFX_EVENT_BACKEND); }

/* Counters with a fixed name, in print order; the arrays print by name after them. */
static const struct {
    const char *name;
    uint16_t offset;
    uint8_t bytes;
} k_counters[] = {
#define NT_COUNTER_(member) #member, (uint16_t)offsetof(nt_gfx_counters_t, member), (uint8_t)sizeof(((nt_gfx_counters_t *)NULL)->member)
    {NT_COUNTER_(vertices)},
    {NT_COUNTER_(indices)},
    {NT_COUNTER_(instances)},
    {NT_COUNTER_(buffer_upload_calls)},
    {NT_COUNTER_(buffer_upload_bytes)},
    {NT_COUNTER_(texture_upload_calls)},
    {NT_COUNTER_(texture_upload_bytes)},
    {NT_COUNTER_(stream_bytes)},
#undef NT_COUNTER_
};

/* The 4-byte element i of a list, as bits: a float list trims and counts by bits like an integer one. */
static uint32_t list_slot(const unsigned char *at, uint32_t i) {
    uint32_t v = 0;
    memcpy(&v, at + ((size_t)i * sizeof(v)), sizeof(v));
    return v;
}
// #endregion

// #region output
typedef struct {
    char *out;
    size_t cap;
    size_t len; /* bytes the text needs so far; may exceed cap */
} text_t;

static void put(text_t *t, const char *fmt, ...) NT_PRINTF_ATTR(2, 3);
static void put(text_t *t, const char *fmt, ...) {
    const bool room = t->len < t->cap;
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(room ? t->out + t->len : NULL, room ? t->cap - t->len : 0, fmt, ap);
    va_end(ap);
    NT_ASSERT(n >= 0);
    t->len += (size_t)n;
}

static uint32_t float_bits(float f) {
    uint32_t bits = 0;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

/* The shortest %g that reads back to the same float32 (9 digits always do); NaN keeps its payload as bits.
 * The decimal point is always '.', whatever the process locale. */
static void put_float(text_t *t, float f) {
    if (isnan(f)) {
        put(t, "nan:0x%08" PRIx32, float_bits(f));
        return;
    }
    char digits[32];
    for (int precision = 6; precision <= 9; precision++) {
        (void)snprintf(digits, sizeof(digits), "%.*g", precision, (double)f);
        if (float_bits(strtof(digits, NULL)) == float_bits(f)) {
            break;
        }
    }
    /* The locale's separator is the one run of other bytes ("," or "/" or multibyte): it becomes '.'. */
    char *out = digits;
    for (const char *c = digits; *c != '\0'; c++) {
        const unsigned char b = (unsigned char)*c; /* ASCII by value: isalnum follows LC_CTYPE */
        if ((b >= '0' && b <= '9') || (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || b == '-' || b == '+') {
            *out++ = *c;
        } else if (out == digits || out[-1] != '.') {
            *out++ = '.';
        }
    }
    *out = '\0';
    put(t, "%s", digits);
}

static void put_string(text_t *t, const char *s, size_t size) {
    put(t, "\"");
    for (size_t i = 0; i < size && s[i] != '\0'; i++) {
        const unsigned char c = (unsigned char)s[i];
        if (c < 0x20U || c > 0x7eU || c == '"' || c == '\\') {
            put(t, "\\x%02x", c);
        } else {
            put(t, "%c", c);
        }
    }
    put(t, "\"");
}

/* count elements, or the prefix up to the last nonzero one when count is negative. */
static void put_list(text_t *t, const char *key, const unsigned char *at, field_type_t type, uint32_t size, int count) {
    NT_ASSERT(count <= (int)size && "capture text: an issued call counts more arguments than its record holds");
    uint32_t n = (uint32_t)count;
    if (count < 0) {
        n = size;
        while (n > 0 && list_slot(at, n - 1U) == 0) {
            n--;
        }
    }
    if (n == 0) {
        return;
    }
    put(t, " %s=[", key);
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t bits = list_slot(at, i);
        if (i > 0) {
            put(t, ",");
        }
        if (type == F_U32S) {
            put(t, "%" PRIu32, bits);
        } else {
            float v = 0.0F;
            memcpy(&v, &bits, sizeof(v));
            put_float(t, v);
        }
    }
    put(t, "]");
}

/* Zero scalars are omitted; an issued call's lists print exactly their counted elements, so a zero argument stays visible. */
static void put_fields(text_t *t, const nt_gfx_event_t *e, arm_t arm) {
    const bool issued = issued_call(e, arm);
    const unsigned char *base = (const unsigned char *)e;
    for (uint32_t i = 0; i < k_arms[arm].count; i++) {
        const field_t *f = &k_arms[arm].fields[i];
        const unsigned char *at = base + f->offset;
        uint64_t wide = 0;
        float real = 0.0F;
        switch ((field_type_t)f->type) {
        case F_U32:
        case F_BOOL: {
            bool flag = false;
            memcpy(&flag, at, sizeof(flag));
            const uint32_t v = f->type == F_BOOL ? (uint32_t)flag : list_slot(at, 0);
            if (v != 0) {
                put(t, " %s=%" PRIu32, f->name, v);
            }
            break;
        }
        case F_U64:
            memcpy(&wide, at, sizeof(wide));
            if (wide != 0) {
                put(t, " %s=%" PRIu64, f->name, wide);
            }
            break;
        case F_F32:
            memcpy(&real, at, sizeof(real));
            if (float_bits(real) != 0) {
                put(t, " %s=", f->name);
                put_float(t, real);
            }
            break;
        case F_U32S:
            put_list(t, f->name, at, F_U32S, f->size, issued ? (int)(e->arg_count & 0x0FU) : -1);
            break;
        case F_F32S:
            put_list(t, f->name, at, F_F32S, f->size, issued ? e->arg_count >> 4 : -1);
            break;
        }
    }
}

/* An issued call sets only the arguments it counts; other records count none. */
static void assert_args_counted(const nt_gfx_event_t *e, bool issued) {
    NT_ASSERT((issued || e->arg_count == 0) && "capture text: only issued calls count arguments");
    bool counted = true;
    for (uint32_t i = issued ? e->arg_count & 0x0FU : 12U; i < 12U; i++) {
        counted = counted && e->data.backend.args[i] == 0;
    }
    for (uint32_t i = issued ? (uint32_t)(e->arg_count >> 4) : 4U; i < 4U; i++) {
        counted = counted && float_bits(e->data.backend.values[i]) == 0;
    }
    NT_ASSERT(counted && "capture text: an issued call sets an argument it does not count");
}

/* Records are zeroed when reserved and each writer fills only the arm the text prints, so the text is lossless. */
static void assert_data_in_arm(const nt_gfx_event_t *e, arm_t arm) {
    const size_t used = arm == ARM_NONE ? sizeof(e->data) : k_arms[arm].bytes;
    const unsigned char *bytes = (const unsigned char *)&e->data;
    for (size_t i = used; i < sizeof(e->data); i++) {
        NT_ASSERT(bytes[i] == 0 && "capture text: a record writes data outside the arm the text prints");
    }
}

static void put_detail(text_t *t, const nt_gfx_event_t *e) {
    switch (detail_kind(e)) {
    case DETAIL_CALL:
        if (e->kind == NT_GFX_EVENT_BACKEND || e->detail != 0) {
            const char *call = nt_gfx_gl_call_name(e->detail);
            NT_ASSERT(call != NULL && "capture text: issued call outside the GL call table");
            put(t, " call=%s", call);
        }
        break;
    case DETAIL_RESOURCE:
        put(t, " resource=%s", NT_TABLE_NAME_(s_object_names, e->detail));
        break;
    case DETAIL_LAYER:
        put(t, " layer=%s", NT_TABLE_NAME_(s_layer_names, e->detail));
        break;
    case DETAIL_JOINED:
        if (e->detail != 0) {
            put(t, " joined=@%" PRIu32, e->detail - 1U);
        }
        break;
    case DETAIL_PLAIN:
        if (e->detail != 0) {
            put(t, " detail=%" PRIu32, e->detail);
        }
        break;
    }
}

static void put_field(text_t *t, const nt_gfx_event_t *e) {
    put(t, " key=");
    put_string(t, e->field.key, sizeof(e->field.key));
    put(t, " type=%s value=", NT_TABLE_NAME_(s_note_type_names, (uint32_t)e->field.type));
    switch (e->field.type) {
    case NT_GFX_NOTE_U32:
        put(t, "%" PRIu32, e->field.value.u32);
        break;
    case NT_GFX_NOTE_I32:
        put(t, "%" PRId32, e->field.value.i32);
        break;
    case NT_GFX_NOTE_U64:
        put(t, "%" PRIu64, e->field.value.u64);
        break;
    case NT_GFX_NOTE_F32:
        put_float(t, e->field.value.f32);
        break;
    case NT_GFX_NOTE_STR:
        put_string(t, e->field.value.str, sizeof(e->field.value.str));
        break;
    }
}

/* Indentation is cosmetic and capped, so text size stays linear in records. */
enum { MAX_INDENT = 32 };

static uint32_t link_depth(const nt_gfx_capture_view_t *view, uint32_t index) {
    uint32_t depth = 0;
    for (uint32_t link = view->events[index].link; link != 0 && depth < MAX_INDENT; link = view->events[link - 1U].link) {
        depth++;
    }
    return depth;
}

static void put_record(text_t *t, const nt_gfx_capture_view_t *view, uint32_t index, uint32_t markers) {
    const nt_gfx_event_t *e = &view->events[index];
    const bool gfx = e->kind <= NT_GFX_EVENT_ARGUMENT;
    uint32_t indent = markers + (gfx ? link_depth(view, index) : 0);
    indent = indent < MAX_INDENT ? indent : MAX_INDENT;
    put(t, "%" PRIu32 " %*s%s", index, (int)(indent * 2U), "", NT_TABLE_NAME_(s_kind_names, (uint32_t)e->kind));
    if (!gfx) {
        if (e->kind == NT_GFX_EVENT_NOTE || e->kind == NT_GFX_EVENT_MARKER_PUSH) {
            put(t, " label=");
            put_string(t, e->note.label, sizeof(e->note.label));
        } else if (e->kind == NT_GFX_EVENT_NOTE_FIELD) {
            put_field(t, e);
        }
        put(t, "\n");
        return;
    }
    const arm_t arm = record_arm(e);
    assert_data_in_arm(e, arm);
    assert_args_counted(e, issued_call(e, arm));
    put(t, " op=%s", NT_TABLE_NAME_(s_operation_names, e->operation));
    if (e->object_kind != NT_GFX_OBJECT_NONE || e->object != 0) {
        put(t, " obj=%s:%" PRIu32, NT_TABLE_NAME_(s_object_names, e->object_kind), e->object);
    }
    if (e->result != NT_GFX_RESULT_NONE) {
        put(t, " result=%s", NT_TABLE_NAME_(s_result_names, e->result));
    }
    put_detail(t, e);
    if (e->link != 0) {
        put(t, " link=@%" PRIu32, e->link - 1U);
    }
    put_fields(t, e, arm);
    put(t, "\n");
}

static void put_counters(text_t *t, const nt_gfx_counters_t *c) {
    for (uint32_t i = 0; i < NT_COUNT_(k_counters); i++) {
        uint64_t value = 0;
        memcpy(&value, (const unsigned char *)c + k_counters[i].offset, k_counters[i].bytes); /* little-endian: a u32 fills the low half */
        if (value != 0) {
            put(t, "counter %s=%" PRIu64 "\n", k_counters[i].name, value);
        }
    }
    for (uint32_t i = 0; i < NT_GFX_OP_COUNT; i++) {
        if (c->accepted[i] != 0) {
            put(t, "counter accepted.%s=%" PRIu32 "\n", NT_TABLE_NAME_(s_operation_names, i), c->accepted[i]);
        }
    }
    for (uint32_t i = 0; i < NT_GFX_FRAME_STREAM_COUNT; i++) {
        if (c->frame_bytes[i] != 0) {
            put(t, "counter frame_bytes.%" PRIu32 "=%" PRIu32 "\n", i, c->frame_bytes[i]);
        }
    }
    for (uint32_t i = 1; i < NT_GFX_GL_COUNT; i++) {
        if (c->gl[i] != 0) {
            put(t, "counter gl.%s=%" PRIu32 "\n", nt_gfx_gl_call_name(i), c->gl[i]);
        }
    }
}
// #endregion

size_t nt_gfx_capture_format(const nt_gfx_capture_view_t *view, char *out, size_t cap) {
    NT_ASSERT(view != NULL && (out != NULL || cap == 0));
    text_t t = {.out = out, .cap = cap, .len = 0};
    if (cap > 0) {
        out[0] = '\0';
    }
    put(&t, "ntcapture 2\n");
    put(&t, "frame sequence=%" PRIu64 " platform=%s records=%" PRIu32 " overflow=%d execute=%" PRIu32 "..%" PRIu32 "\n", view->counters.frame_sequence,
        NT_TABLE_NAME_(s_platform_names, (uint32_t)view->platform), view->count, view->overflow ? 1 : 0, view->execute_begin, view->execute_end);
    put_counters(&t, &view->counters);
    uint32_t markers = 0;
    for (uint32_t i = 0; i < view->count; i++) {
        if (view->events[i].kind == NT_GFX_EVENT_MARKER_POP && markers > 0) {
            markers--;
        }
        put_record(&t, view, i, markers);
        if (view->events[i].kind == NT_GFX_EVENT_MARKER_PUSH) {
            markers++;
        }
    }
    return t.len;
}

// #region parse
/* One line of untrusted text: [p, end), end at its '\n'. The first failure wins. */
typedef struct {
    const char *p;
    const char *end;
    const char *error;
    const char *error_at;
} line_t;

static bool fail(line_t *l, const char *error) {
    if (l->error == NULL) {
        l->error = error;
        l->error_at = l->p;
    }
    return false;
}

static bool take(line_t *l, const char *literal) {
    const size_t n = strlen(literal);
    if ((size_t)(l->end - l->p) < n || memcmp(l->p, literal, n) != 0) {
        return false;
    }
    l->p += n;
    return true;
}

static bool expect(line_t *l, const char *literal) { return take(l, literal) || fail(l, "unexpected text"); }

static bool at_end(line_t *l) { return l->p == l->end || fail(l, "unexpected text"); }

/* `name=` with nothing consumed unless both match, so a longer name with the same prefix stays readable. */
static bool take_assign(line_t *l, const char *name) {
    const size_t n = strlen(name);
    if ((size_t)(l->end - l->p) <= n || memcmp(l->p, name, n) != 0 || l->p[n] != '=') {
        return false;
    }
    l->p += n + 1;
    return true;
}

/* Decimal digits only: no sign, no spaces, no wrap. */
static bool read_uint(line_t *l, uint64_t max, uint64_t *out) {
    const char *start = l->p;
    uint64_t v = 0;
    while (l->p < l->end && *l->p >= '0' && *l->p <= '9') {
        const uint64_t d = (uint64_t)(*l->p - '0');
        if (d > max || v > (max - d) / 10U) {
            l->p = start;
            return fail(l, "number out of range");
        }
        v = (v * 10U) + d;
        l->p++;
    }
    if (l->p == start) {
        return fail(l, "expected a number");
    }
    *out = v;
    return true;
}

static bool read_u32(line_t *l, uint32_t *out) {
    uint64_t v = 0;
    const bool ok = read_uint(l, UINT32_MAX, &v);
    *out = (uint32_t)v;
    return ok;
}

static size_t ident_length(const line_t *l) {
    size_t n = 0;
    while (l->p + n < l->end) {
        const char c = l->p[n];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
            break;
        }
        n++;
    }
    return n;
}

static bool name_is(const char *name, const char *s, size_t n) { return name != NULL && strlen(name) == n && memcmp(name, s, n) == 0; }

/* A name in this build's table; a name only another build knows is unknown here. */
static bool read_name(line_t *l, const char *const *names, uint32_t count, uint32_t *out) {
    const size_t n = ident_length(l);
    for (uint32_t i = 0; i < count; i++) {
        if (name_is(names[i], l->p, n)) {
            *out = i;
            l->p += n;
            return true;
        }
    }
    return fail(l, "unknown name");
}
#define NT_READ_NAME_(l, table, out) read_name((l), (table), NT_COUNT_(table), (out))

static bool read_call(line_t *l, uint32_t *out) {
    const size_t n = ident_length(l);
    for (uint32_t i = 1; i < NT_GFX_GL_COUNT; i++) {
        if (name_is(nt_gfx_gl_call_name(i), l->p, n)) {
            *out = i;
            l->p += n;
            return true;
        }
    }
    return fail(l, "unknown GL call");
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* The 8 hex digits after "nan:0x"; false unless they are NaN bits. */
static bool nan_bits(const char *hex, float *out) {
    uint32_t bits = 0;
    for (size_t i = 0; i < 8; i++) {
        const int d = hex_digit(hex[i]);
        if (d < 0) {
            return false;
        }
        bits = (bits << 4) | (uint32_t)d;
    }
    memcpy(out, &bits, sizeof(*out));
    return isnan(*out);
}

/* The formatter's forms only: decimal %g, inf, -inf and nan:0x<8 hex digits>. strtof reads the process
 * locale's separator, so the bounded token copy takes it in place of '.'. */
static bool read_float(line_t *l, float *out) {
    const char *start = l->p;
    size_t n = 0;
    while (start + n < l->end && start[n] != ' ' && start[n] != ',' && start[n] != ']') {
        n++;
    }
    if (n == 14 && memcmp(start, "nan:0x", 6) == 0) {
        if (!nan_bits(start + 6, out)) {
            return fail(l, "bad float");
        }
        l->p += n;
        return true;
    }
    const char *point = localeconv()->decimal_point; // NOLINT(concurrency-mt-unsafe): read only, like the formatter's strtof
    const size_t point_len = strlen(point);
    const bool word = (n == 3 && memcmp(start, "inf", 3) == 0) || (n == 4 && memcmp(start, "-inf", 4) == 0);
    char buf[64]; /* 32 token bytes, the one '.' of them replaced by at most 8 */
    size_t len = 0;
    bool point_seen = false;
    if (n == 0 || n > 32 || point_len > 8) {
        return fail(l, "bad float");
    }
    for (size_t i = 0; i < n; i++) {
        const char c = start[i];
        if (c == '.' && !word && !point_seen) {
            point_seen = true;
            memcpy(buf + len, point, point_len);
            len += point_len;
        } else if (word || (c >= '0' && c <= '9') || c == '-' || c == '+' || c == 'e') {
            buf[len++] = c;
        } else {
            return fail(l, "bad float");
        }
    }
    buf[len] = '\0';
    char *stop = NULL;
    *out = strtof(buf, &stop);
    if (stop != buf + len || (!word && isinf(*out))) { /* a decimal past FLT_MAX is not formatter output */
        return fail(l, "bad float");
    }
    l->p += n;
    return true;
}

/* A quoted string: printable bytes and \xNN for the rest; no NUL, at most size - 1 bytes. */
static bool read_string(line_t *l, char *dst, size_t size) {
    if (!take(l, "\"")) {
        return fail(l, "expected a string");
    }
    size_t n = 0;
    for (;;) {
        if (l->p == l->end) {
            return fail(l, "unterminated string");
        }
        unsigned char c = (unsigned char)*l->p;
        size_t step = 1;
        if (c == '"') {
            l->p++;
            return true;
        }
        if (c == '\\') {
            const int hi = l->end - l->p >= 4 && l->p[1] == 'x' ? hex_digit(l->p[2]) : -1;
            const int lo = hi >= 0 ? hex_digit(l->p[3]) : -1;
            if (lo < 0 || (hi == 0 && lo == 0)) {
                return fail(l, "bad escape");
            }
            c = (unsigned char)((hi << 4) | lo);
            step = 4;
        } else if (c < 0x20U || c > 0x7eU) {
            return fail(l, "bad string byte");
        }
        if (n + 1 >= size) {
            return fail(l, "string too long");
        }
        dst[n++] = (char)c;
        l->p += step;
    }
}

/* `[a,b,...]` into consecutive 4-byte slots. */
static bool read_list(line_t *l, unsigned char *at, field_type_t type, uint32_t size, uint32_t *count) {
    if (!expect(l, "[")) {
        return false;
    }
    uint32_t n = 0;
    do {
        uint32_t bits = 0;
        float real = 0.0F;
        if (n == size) {
            return fail(l, "list too long");
        }
        if (type == F_U32S ? !read_u32(l, &bits) : !read_float(l, &real)) {
            return false;
        }
        if (type == F_F32S) {
            memcpy(&bits, &real, sizeof(bits));
        }
        memcpy(at + ((size_t)n * sizeof(bits)), &bits, sizeof(bits));
        n++;
    } while (take(l, ","));
    *count = n;
    return expect(l, "]");
}

/* ` name=`, consuming nothing unless it matches. */
static bool take_field(line_t *l, const char *name) {
    if (l->p == l->end || l->p[0] != ' ') {
        return false;
    }
    l->p++;
    if (!take_assign(l, name)) {
        l->p--;
        return false;
    }
    return true;
}

/* Fields in table order, each optional; an issued call's lists set its arg_count. */
static bool read_fields(line_t *l, nt_gfx_event_t *e, arm_t arm) {
    const bool issued = issued_call(e, arm);
    unsigned char *base = (unsigned char *)e;
    for (uint32_t i = 0; i < k_arms[arm].count; i++) {
        const field_t *f = &k_arms[arm].fields[i];
        unsigned char *at = base + f->offset;
        uint64_t v = 0;
        uint32_t n = 0;
        bool ok = true;
        if (!take_field(l, f->name)) {
            continue;
        }
        switch ((field_type_t)f->type) {
        case F_U32:
            ok = read_uint(l, UINT32_MAX, &v);
            memcpy(at, &(uint32_t){(uint32_t)v}, sizeof(uint32_t));
            break;
        case F_U64:
            ok = read_uint(l, UINT64_MAX, &v);
            memcpy(at, &v, sizeof(v));
            break;
        case F_BOOL:
            ok = read_uint(l, 1, &v);
            *(bool *)at = v != 0;
            break;
        case F_F32: {
            float real = 0.0F;
            ok = read_float(l, &real);
            memcpy(at, &real, sizeof(real));
            break;
        }
        case F_U32S:
        case F_F32S:
            ok = read_list(l, at, (field_type_t)f->type, f->size, &n);
            if (ok && issued) {
                e->arg_count = (uint8_t)(f->type == F_U32S ? (e->arg_count & 0xF0U) | n : (e->arg_count & 0x0FU) | (n << 4));
            }
            break;
        }
        if (!ok) {
            return false;
        }
    }
    return at_end(l);
}

/* `@i`: an earlier OP record (a draw for joined), stored as i + 1. */
static bool read_ref(line_t *l, const nt_gfx_event_t *events, uint32_t index, bool draw, uint32_t *out) {
    const char *start = l->p;
    uint32_t target = 0;
    if (!read_u32(l, &target)) {
        return false;
    }
    if (target >= index || events[target].kind != NT_GFX_EVENT_OP || (draw && !is_draw(events[target].operation))) {
        l->p = start;
        return fail(l, draw ? "joined must name an earlier draw" : "link must name an earlier operation");
    }
    *out = target + 1U;
    return true;
}

static bool read_detail(line_t *l, nt_gfx_event_t *e, const nt_gfx_event_t *events, uint32_t index) {
    uint32_t v = 0;
    switch (detail_kind(e)) {
    case DETAIL_CALL:
        if (take(l, " call=")) {
            return read_call(l, &e->detail);
        }
        return e->kind != NT_GFX_EVENT_BACKEND || fail(l, "an issued call needs call=");
    case DETAIL_RESOURCE:
        if (!expect(l, " resource=") || !NT_READ_NAME_(l, s_object_names, &v)) {
            return false;
        }
        e->detail = v;
        return true;
    case DETAIL_LAYER:
        if (!expect(l, " layer=") || !NT_READ_NAME_(l, s_layer_names, &v)) {
            return false;
        }
        e->detail = v;
        return true;
    case DETAIL_JOINED:
        return !take(l, " joined=@") || read_ref(l, events, index, true, &e->detail);
    case DETAIL_PLAIN:
        return !take(l, " detail=") || read_u32(l, &e->detail);
    }
    return false;
}

static bool read_gfx(line_t *l, nt_gfx_event_t *events, uint32_t index) {
    nt_gfx_event_t *e = &events[index];
    uint32_t v = 0;
    if (!expect(l, " op=") || !NT_READ_NAME_(l, s_operation_names, &v)) {
        return false;
    }
    e->operation = (uint8_t)v;
    if (take(l, " obj=")) {
        if (!NT_READ_NAME_(l, s_object_names, &v) || !expect(l, ":") || !read_u32(l, &e->object)) {
            return false;
        }
        e->object_kind = (uint8_t)v;
    }
    if (take(l, " result=")) {
        if (!NT_READ_NAME_(l, s_result_names, &v)) {
            return false;
        }
        e->result = (uint8_t)v;
    }
    if (!read_detail(l, e, events, index) || (take(l, " link=@") && !read_ref(l, events, index, false, &e->link))) {
        return false;
    }
    return read_fields(l, e, record_arm(e));
}

static bool read_note_field(line_t *l, nt_gfx_event_t *e) {
    uint32_t type = 0;
    if (!expect(l, " key=") || !read_string(l, e->field.key, sizeof(e->field.key)) || !expect(l, " type=") || !NT_READ_NAME_(l, s_note_type_names, &type) || !expect(l, " value=")) {
        return false;
    }
    e->field.type = (nt_gfx_note_type_t)type;
    uint64_t v = 0;
    switch (e->field.type) {
    case NT_GFX_NOTE_U32:
        return read_u32(l, &e->field.value.u32);
    case NT_GFX_NOTE_I32: {
        const bool negative = take(l, "-");
        if (!read_uint(l, negative ? (uint64_t)INT32_MAX + 1U : (uint64_t)INT32_MAX, &v)) {
            return false;
        }
        e->field.value.i32 = negative ? (int32_t)(-(int64_t)v) : (int32_t)v;
        return true;
    }
    case NT_GFX_NOTE_U64:
        return read_uint(l, UINT64_MAX, &e->field.value.u64);
    case NT_GFX_NOTE_F32:
        return read_float(l, &e->field.value.f32);
    case NT_GFX_NOTE_STR:
        return read_string(l, e->field.value.str, sizeof(e->field.value.str));
    }
    return false;
}

static bool read_record(line_t *l, nt_gfx_event_t *events, uint32_t index) {
    nt_gfx_event_t *e = &events[index];
    memset(e, 0, sizeof(*e));
    const char *start = l->p;
    uint32_t number = 0;
    uint32_t kind = 0;
    if (!read_u32(l, &number)) {
        return false;
    }
    if (number != index) {
        l->p = start;
        return fail(l, "record index out of order");
    }
    if (!expect(l, " ")) {
        return false;
    }
    while (take(l, " ")) {
    }
    if (!NT_READ_NAME_(l, s_kind_names, &kind)) {
        return false;
    }
    e->kind = (nt_gfx_event_kind_t)kind;
    switch (e->kind) {
    case NT_GFX_EVENT_NOTE:
    case NT_GFX_EVENT_MARKER_PUSH:
        return expect(l, " label=") && read_string(l, e->note.label, sizeof(e->note.label)) && at_end(l);
    case NT_GFX_EVENT_NOTE_FIELD:
        if (index == 0 || (events[index - 1U].kind != NT_GFX_EVENT_NOTE && events[index - 1U].kind != NT_GFX_EVENT_NOTE_FIELD)) {
            return fail(l, "a note field must follow its note");
        }
        return read_note_field(l, e) && at_end(l);
    case NT_GFX_EVENT_MARKER_POP:
        return at_end(l);
    default:
        return read_gfx(l, events, index);
    }
}

static bool read_counter(line_t *l, nt_gfx_counters_t *c) {
    uint64_t v = 0;
    for (uint32_t i = 0; i < NT_COUNT_(k_counters); i++) {
        if (take_assign(l, k_counters[i].name)) {
            if (!read_uint(l, k_counters[i].bytes == sizeof(uint64_t) ? UINT64_MAX : UINT32_MAX, &v)) {
                return false;
            }
            memcpy((unsigned char *)c + k_counters[i].offset, &v, k_counters[i].bytes); /* little-endian: a u32 takes the low half */
            return at_end(l);
        }
    }
    uint32_t *slot = NULL;
    uint32_t i = 0;
    if (take(l, "accepted.")) {
        if (!NT_READ_NAME_(l, s_operation_names, &i)) {
            return false;
        }
        slot = &c->accepted[i];
    } else if (take(l, "frame_bytes.")) {
        if (!read_u32(l, &i)) {
            return false;
        }
        if (i >= NT_GFX_FRAME_STREAM_COUNT) {
            return fail(l, "frame stream beyond this build's NT_GFX_MAX_VERTEX_STREAMS");
        }
        slot = &c->frame_bytes[i];
    } else if (take(l, "gl.")) {
        if (!read_call(l, &i)) {
            return false;
        }
        slot = &c->gl[i];
    } else {
        return fail(l, "unknown counter");
    }
    return expect(l, "=") && read_u32(l, slot) && at_end(l);
}

/* Opens the line at `at`; a line without '\n' is a truncated file. */
static bool open_line(line_t *l, const char *at, const char *end) {
    const char *nl = (const char *)memchr(at, '\n', (size_t)(end - at));
    l->p = at;
    l->end = nl != NULL ? nl : end;
    return nl != NULL || fail(l, "truncated: the last line has no newline");
}

static bool is_document_start(const char *at, const char *end) { return (size_t)(end - at) >= 10 && memcmp(at, "ntcapture ", 10) == 0; }

/* The first line from `at` (a line start) that opens a document. Strings escape '\n', so every line break is a record boundary. */
static const char *next_document(const char *at, const char *end) {
    while (at < end && !is_document_start(at, end)) {
        const char *nl = (const char *)memchr(at, '\n', (size_t)(end - at));
        at = nl != NULL ? nl + 1 : end;
    }
    return at;
}

static bool read_header(line_t *l, const char **at, const char *end, nt_gfx_capture_view_t *view, uint32_t *records) {
    uint32_t platform = 0;
    uint64_t overflow = 0;
    if (!open_line(l, *at, end) || !expect(l, "ntcapture ")) {
        return false;
    }
    if (!take(l, "2") || l->p != l->end) {
        return fail(l, "unsupported ntcapture version");
    }
    *at = l->end + 1;
    if (!open_line(l, *at, end) || !expect(l, "frame sequence=") || !read_uint(l, UINT64_MAX, &view->counters.frame_sequence) || !expect(l, " platform=") ||
        !NT_READ_NAME_(l, s_platform_names, &platform) || !expect(l, " records=") || !read_u32(l, records) || !expect(l, " overflow=") || !read_uint(l, 1, &overflow) || !expect(l, " execute=") ||
        !read_u32(l, &view->execute_begin) || !expect(l, "..") || !read_u32(l, &view->execute_end) || !at_end(l)) {
        return false;
    }
    if (view->execute_begin > view->execute_end || view->execute_end > *records) {
        return fail(l, "execute range outside the records");
    }
    view->platform = (nt_gfx_capture_platform_t)platform;
    view->overflow = overflow != 0;
    *at = l->end + 1;
    while ((size_t)(end - *at) >= 8 && memcmp(*at, "counter ", 8) == 0) {
        if (!open_line(l, *at, end) || !expect(l, "counter ") || !read_counter(l, &view->counters)) {
            return false;
        }
        *at = l->end + 1;
    }
    return true;
}

static const char *line_start(const char *text, const char *at) {
    while (at > text && at[-1] != '\n') {
        at--;
    }
    return at;
}

/* The records= lines from *at into events[0..cap); returns how many parsed, the first failure in l. */
static uint32_t read_records(line_t *l, const char **at, const char *end, nt_gfx_event_t *events, uint32_t cap, uint32_t records) {
    uint32_t count = 0;
    for (; count < records; count++) {
        l->p = *at;
        if (*at == end || is_document_start(*at, end)) {
            (void)fail(l, "truncated: fewer records than records=");
        } else if (count == cap) {
            (void)fail(l, "capacity: more records than the array holds");
        } else if (open_line(l, *at, end) && read_record(l, events, count)) {
            *at = l->end + 1;
            continue;
        }
        return count;
    }
    if (*at < end && !is_document_start(*at, end)) {
        l->p = *at;
        (void)fail(l, "a record beyond records=");
    }
    return count;
}

nt_gfx_capture_parsed_t nt_gfx_capture_parse(const char *text, size_t len, nt_gfx_event_t *events, uint32_t cap) {
    NT_ASSERT((text != NULL || len == 0) && events != NULL);
    nt_gfx_capture_parsed_t r = {0};
    if (len == 0) {
        r.error = "empty document";
        return r;
    }
    const char *end = text + len;
    const char *at = text;
    line_t l = {.p = text, .end = text};
    nt_gfx_capture_view_t view = {0};
    uint32_t records = 0;
    if (!read_header(&l, &at, end, &view, &records)) {
        /* Rejected; the search for the next document starts after the version line, which may itself be bad. */
        const char *first = (const char *)memchr(text, '\n', len);
        r.next = (size_t)(next_document(first != NULL ? first + 1 : end, end) - text);
        r.error = l.error;
        r.error_at = (size_t)(l.error_at - text);
        return r;
    }
    const uint32_t count = read_records(&l, &at, end, events, cap, records);
    /* A valid prefix stays a usable view: the execution range is clipped to it. */
    view.events = events;
    view.count = count;
    view.execute_begin = view.execute_begin < count ? view.execute_begin : count;
    view.execute_end = view.execute_end < count ? view.execute_end : count;
    r.view = view;
    if (l.error != NULL) {
        r.error = l.error;
        r.error_at = (size_t)(l.error_at - text);
        at = next_document(line_start(text, l.error_at), end);
    }
    r.next = (size_t)(at - text);
    return r;
}
// #endregion
