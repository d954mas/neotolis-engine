#include "graphics/nt_gfx_capture_text.h"

#include "core/nt_assert.h"
#include "log/nt_log.h"

#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
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

_Static_assert(sizeof(s_kind_names) / sizeof(s_kind_names[0]) == NT_GFX_EVENT_MARKER_POP + 1, "every event kind has a name");
_Static_assert(sizeof(s_object_names) / sizeof(s_object_names[0]) == NT_GFX_OBJECT_INSTANCE_LAYOUT + 1, "every object kind has a name");
_Static_assert(sizeof(s_result_names) / sizeof(s_result_names[0]) == NT_GFX_RESULT_UNKNOWN + 1, "every result has a name");
_Static_assert(sizeof(s_note_type_names) / sizeof(s_note_type_names[0]) == NT_GFX_NOTE_STR + 1, "every note type has a name");

/* Tables are complete by the asserts above (and a test for operations); a value outside one is a broken record. */
static const char *table_name(const char *const *names, size_t count, uint32_t value) {
    NT_ASSERT(value < count && names[value] != NULL && "capture text: value outside its name table");
    return names[value];
}
#define NT_TABLE_NAME_(table, value) table_name((table), sizeof(table) / sizeof((table)[0]), (value))
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
    for (char *c = digits; *c != '\0'; c++) {
        if (*c == ',') {
            *c = '.';
        }
    }
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

static void put_u32(text_t *t, const char *key, uint32_t value) {
    if (value != 0) {
        put(t, " %s=%" PRIu32, key, value);
    }
}

static void put_float_field(text_t *t, const char *key, float value) {
    if (float_bits(value) != 0) {
        put(t, " %s=", key);
        put_float(t, value);
    }
}

/* Integers as stored; count elements, or the prefix up to the last nonzero one when count is unknown. */
static void put_u32_list(text_t *t, const char *key, const uint32_t *values, uint32_t size, int count) {
    uint32_t n = (uint32_t)count;
    if (count < 0) {
        n = size;
        while (n > 0 && values[n - 1] == 0) {
            n--;
        }
    }
    if (n == 0) {
        return;
    }
    put(t, " %s=[", key);
    for (uint32_t i = 0; i < n; i++) {
        put(t, i == 0 ? "%" PRIu32 : ",%" PRIu32, values[i]);
    }
    put(t, "]");
}

static void put_float_list(text_t *t, const char *key, const float *values, uint32_t size, int count) {
    uint32_t n = (uint32_t)count;
    if (count < 0) {
        n = size;
        while (n > 0 && float_bits(values[n - 1]) == 0) {
            n--;
        }
    }
    if (n == 0) {
        return;
    }
    put(t, " %s=[", key);
    for (uint32_t i = 0; i < n; i++) {
        if (i > 0) {
            put(t, ",");
        }
        put_float(t, values[i]);
    }
    put(t, "]");
}
// #endregion

// #region records
typedef enum {
    ARM_NONE,
    ARM_BACKEND,
    ARM_RESOURCE,
    ARM_DRAW,
    ARM_BINDING,
    ARM_PASS,
    ARM_CLEAR,
    ARM_ATTRIBUTE,
    ARM_STATE,
} arm_t;

/* Which data arm a gfx record fills; the render architecture spec, "Frame observation", names them. */
static arm_t record_arm(const nt_gfx_event_t *e) {
    switch (e->kind) {
    case NT_GFX_EVENT_BACKEND:
        return ARM_BACKEND;
    case NT_GFX_EVENT_SKIP:
        return e->detail != 0 ? ARM_BACKEND : ARM_BINDING;
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
        switch (e->operation) {
        case NT_GFX_OP_CREATE:
        case NT_GFX_OP_BUFFER_UPLOAD:
            return ARM_RESOURCE;
        case NT_GFX_OP_DRAW:
        case NT_GFX_OP_DRAW_INSTANCED:
        case NT_GFX_OP_DRAW_INDEXED:
        case NT_GFX_OP_DRAW_INDEXED_INSTANCED:
            return ARM_DRAW;
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
    size_t used = sizeof(e->data);
    switch (arm) {
    case ARM_BACKEND:
        used = sizeof(e->data.backend);
        break;
    case ARM_RESOURCE:
        used = sizeof(e->data.resource);
        break;
    case ARM_DRAW:
        used = sizeof(e->data.draw);
        break;
    case ARM_BINDING:
        used = sizeof(e->data.binding);
        break;
    case ARM_PASS:
        used = sizeof(e->data.pass);
        break;
    case ARM_CLEAR:
        used = sizeof(e->data.clear);
        break;
    case ARM_ATTRIBUTE:
        used = sizeof(e->data.attribute);
        break;
    case ARM_STATE:
    case ARM_NONE:
        break;
    }
    const unsigned char *bytes = (const unsigned char *)&e->data;
    for (size_t i = used; i < sizeof(e->data); i++) {
        NT_ASSERT(bytes[i] == 0 && "capture text: a record writes data outside the arm the text prints");
    }
}

static void put_call(text_t *t, uint32_t detail) {
    const char *call = nt_gfx_gl_call_name(detail);
    NT_ASSERT(call != NULL && "capture text: issued call outside the GL call table");
    put(t, " call=%s", call);
}

static void put_layer(text_t *t, uint32_t detail) {
    NT_ASSERT(detail <= NT_GFX_INITIAL_BACKEND && "capture text: INITIAL/STATE names its layer");
    put(t, " layer=%s", detail == NT_GFX_INITIAL_BACKEND ? "BACKEND" : "FRONTEND");
}

static void put_detail(text_t *t, const nt_gfx_event_t *e, arm_t arm) {
    if (arm == ARM_BACKEND && (e->kind == NT_GFX_EVENT_BACKEND || e->kind == NT_GFX_EVENT_SKIP)) {
        put_call(t, e->detail);
    } else if (e->kind == NT_GFX_EVENT_DEFINITION && e->operation == NT_GFX_OP_STATE) {
        put(t, " resource=%s", NT_TABLE_NAME_(s_object_names, e->detail));
    } else if (e->kind == NT_GFX_EVENT_INITIAL && e->operation == NT_GFX_OP_STATE) {
        put_layer(t, e->detail);
    } else if (arm == ARM_DRAW && e->kind == NT_GFX_EVENT_OP && e->detail != 0) {
        put(t, " joined=@%" PRIu32, e->detail - 1U); /* a merged draw keeps the link of the draw it joined */
    } else {
        put_u32(t, "detail", e->detail);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- one field list per data arm
static void put_arm(text_t *t, const nt_gfx_event_t *e, arm_t arm) {
    const bool issued = e->kind == NT_GFX_EVENT_BACKEND || e->kind == NT_GFX_EVENT_SKIP;
    switch (arm) {
    case ARM_BACKEND:
        put_u32_list(t, e->kind == NT_GFX_EVENT_SKIP ? "key" : "args", e->data.backend.args, 12, issued ? (int)(e->arg_count & 0x0FU) : -1);
        put_float_list(t, "values", e->data.backend.values, 4, issued ? e->arg_count >> 4 : -1);
        if (e->data.backend.bytes != 0) {
            put(t, " bytes=%" PRIu64, e->data.backend.bytes);
        }
        break;
    case ARM_RESOURCE:
        if (e->data.resource.size != 0) {
            put(t, " size=%" PRIu64, e->data.resource.size);
        }
        put_u32(t, "backend", e->data.resource.backend);
        put_u32_list(t, "related", e->data.resource.related, 3, -1);
        put_u32(t, "type", e->data.resource.type);
        put_u32(t, "usage", e->data.resource.usage);
        put_u32(t, "width", e->data.resource.width);
        put_u32(t, "height", e->data.resource.height);
        put_u32(t, "format", e->data.resource.format);
        put_u32(t, "levels", e->data.resource.levels);
        put_u32(t, "flags", e->data.resource.flags);
        break;
    case ARM_DRAW:
        put_u32(t, "first", e->data.draw.first);
        put_u32(t, "count", e->data.draw.count);
        put_u32(t, "vertices", e->data.draw.vertices);
        put_u32(t, "instances", e->data.draw.instances);
        put_u32(t, "vertex_input", e->data.draw.vertex_input);
        break;
    case ARM_BINDING:
        put_u32(t, "secondary", e->data.binding.secondary);
        put_u32(t, "name", e->data.binding.name);
        put_u32(t, "slot", e->data.binding.slot);
        put_u32(t, "offset", e->data.binding.offset);
        put_u32(t, "size", e->data.binding.size);
        break;
    case ARM_PASS:
        put_u32(t, "target", e->data.pass.target);
        put_float_list(t, "color", e->data.pass.color, 4, -1);
        put_float_field(t, "depth", e->data.pass.depth);
        put_u32(t, "load_color", e->data.pass.load_color);
        put_u32(t, "load_depth", e->data.pass.load_depth);
        put_u32(t, "discard_color", e->data.pass.discard_color);
        put_u32(t, "discard_depth", e->data.pass.discard_depth);
        break;
    case ARM_CLEAR:
        put_float_list(t, "clear_color", e->data.clear.clear_color, 4, -1);
        put_float_field(t, "clear_depth", e->data.clear.clear_depth);
        put_u32(t, "color", e->data.clear.color);
        put_u32(t, "depth", e->data.clear.depth);
        break;
    case ARM_ATTRIBUTE:
        put_u32(t, "buffer", e->data.attribute.buffer);
        put_u32(t, "offset", e->data.attribute.offset);
        put_u32(t, "stride", e->data.attribute.stride);
        put_u32(t, "location", e->data.attribute.location);
        put_u32(t, "type", e->data.attribute.type);
        put_u32(t, "count", e->data.attribute.count);
        put_u32(t, "normalized", e->data.attribute.normalized);
        put_u32(t, "divisor", e->data.attribute.divisor);
        break;
    case ARM_STATE:
        put_u32_list(t, "integers", e->data.state.integers, 14, -1);
        put_float_list(t, "values", e->data.state.values, 6, -1);
        break;
    case ARM_NONE:
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
    assert_args_counted(e, arm == ARM_BACKEND && (e->kind == NT_GFX_EVENT_BACKEND || e->kind == NT_GFX_EVENT_SKIP));
    put(t, " op=%s", NT_TABLE_NAME_(s_operation_names, e->operation));
    if (e->object_kind != NT_GFX_OBJECT_NONE || e->object != 0) {
        put(t, " obj=%s:%" PRIu32, NT_TABLE_NAME_(s_object_names, e->object_kind), e->object);
    }
    if (e->result != NT_GFX_RESULT_NONE) {
        put(t, " result=%s", NT_TABLE_NAME_(s_result_names, e->result));
    }
    put_detail(t, e, arm);
    if (e->link != 0) {
        put(t, " link=@%" PRIu32, e->link - 1U);
    }
    put_arm(t, e, arm);
    put(t, "\n");
}
// #endregion

// #region header
static void put_counter(text_t *t, const char *name, uint64_t value) {
    if (value != 0) {
        put(t, "counter %s=%" PRIu64 "\n", name, value);
    }
}

static void put_counters(text_t *t, const nt_gfx_counters_t *c) {
    put_counter(t, "vertices", c->vertices);
    put_counter(t, "indices", c->indices);
    put_counter(t, "instances", c->instances);
    put_counter(t, "buffer_upload_calls", c->buffer_upload_calls);
    put_counter(t, "buffer_upload_bytes", c->buffer_upload_bytes);
    put_counter(t, "texture_upload_calls", c->texture_upload_calls);
    put_counter(t, "texture_upload_bytes", c->texture_upload_bytes);
    put_counter(t, "stream_bytes", c->stream_bytes);
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
    put(&t, "ntcapture 1\n");
    put(&t, "frame sequence=%" PRIu64 " records=%" PRIu32 " overflow=%d execute=%" PRIu32 "..%" PRIu32 "\n", view->counters.frame_sequence, view->count, view->overflow ? 1 : 0, view->execute_begin,
        view->execute_end);
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
