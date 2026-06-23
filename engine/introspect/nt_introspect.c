#include "introspect/nt_introspect.h"

#if NT_INTROSPECT_ENABLED

#include "core/nt_assert.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>

const char *nt_introspect_ref_kind_name(nt_ref_kind_t kind) {
    switch (kind) {
    case NT_REF_ENTITY:
        return "entity";
    case NT_REF_RESOURCE:
        return "resource";
    case NT_REF_HANDLE:
        return "handle";
    }
    return "ref";
}

// #region core walk
void nt_entity_introspect(nt_entity_t e, nt_introspect_sink *s) {
    NT_ASSERT(s != NULL);
    s->field_u64(s, "id", e.id);
    s->field_u64(s, "index", nt_entity_index(e));
    s->field_u64(s, "generation", nt_entity_generation(e));
    /* A dead handle is reported, not asserted: nt_log_entity may be handed any handle. */
    if (!nt_entity_is_alive(e)) {
        s->field_bool(s, "alive", false);
        return;
    }
    s->field_bool(s, "enabled", nt_entity_is_enabled(e));
    /* Emit a group for EVERY present component — empty when it has no describe() — so a tool sees the
       whole component set (incl. marker components), not just the describe-capable ones. */
    uint8_t n = nt_entity_storage_count();
    for (uint8_t i = 0; i < n; i++) {
        const nt_comp_storage_reg_t *r = nt_entity_storage_at(i);
        if (r->has(e)) {
            s->begin_group(s, r->name);
            if (r->describe != NULL) {
                r->describe(e, s);
            }
            s->end_group(s);
        }
    }
}
// #endregion

// #region text sink
typedef struct {
    nt_introspect_sink base;
    char *buf;
    size_t cap;
    size_t len; /* current NUL-terminated length, always < cap */
} text_sink_t;

static text_sink_t *as_text(nt_introspect_sink *s) { return (text_sink_t *)s; }

static void text_append(text_sink_t *t, const char *fmt, ...) NT_PRINTF_ATTR(2, 3);
static void text_append(text_sink_t *t, const char *fmt, ...) {
    if (t->len + 1 >= t->cap) {
        return; /* full: only room left is the NUL */
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(t->buf + t->len, t->cap - t->len, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    size_t avail = t->cap - t->len - 1;
    t->len += ((size_t)n <= avail) ? (size_t)n : avail;
}

static void t_begin_group(nt_introspect_sink *s, const char *key) { text_append(as_text(s), "%s{ ", key); }
static void t_end_group(nt_introspect_sink *s) { text_append(as_text(s), "} "); }
static void t_f32(nt_introspect_sink *s, const char *key, float v) { text_append(as_text(s), "%s=%g ", key, (double)v); }
static void t_i64(nt_introspect_sink *s, const char *key, int64_t v) { text_append(as_text(s), "%s=%" PRId64 " ", key, v); }
static void t_u64(nt_introspect_sink *s, const char *key, uint64_t v) { text_append(as_text(s), "%s=%" PRIu64 " ", key, v); }
static void t_bool(nt_introspect_sink *s, const char *key, bool v) { text_append(as_text(s), "%s=%d ", key, v ? 1 : 0); }

static void t_floats(nt_introspect_sink *s, const char *key, const float *v, int count) {
    text_sink_t *t = as_text(s);
    NT_ASSERT(v != NULL); /* describe() boundary: a present component's accessor never returns NULL */
    text_append(t, "%s=[", key);
    for (int i = 0; i < count; i++) {
        if (i > 0) {
            text_append(t, ",");
        }
        text_append(t, "%g", (double)v[i]);
    }
    text_append(t, "] ");
}

static void t_str(nt_introspect_sink *s, const char *key, const char *v) { text_append(as_text(s), "%s=%s ", key, v != NULL ? v : "(null)"); }
static void t_enum(nt_introspect_sink *s, const char *key, const char *token) { text_append(as_text(s), "%s=%s ", key, token != NULL ? token : "?"); }
static void t_ref(nt_introspect_sink *s, const char *key, nt_ref_kind_t kind, uint64_t id) { text_append(as_text(s), "%s=%s#0x%" PRIx64 " ", key, nt_introspect_ref_kind_name(kind), id); }
static void t_asset(nt_introspect_sink *s, uint8_t asset_type, uint32_t handle) { text_append(as_text(s), "handle=%u type=%u ", (unsigned)handle, (unsigned)asset_type); }

static void text_sink_init(text_sink_t *t, char *buf, size_t cap) {
    t->base = (nt_introspect_sink){
        .begin_group = t_begin_group,
        .end_group = t_end_group,
        .field_f32 = t_f32,
        .field_i64 = t_i64,
        .field_u64 = t_u64,
        .field_bool = t_bool,
        .field_floats = t_floats,
        .field_str = t_str,
        .field_enum = t_enum,
        .field_ref = t_ref,
        .field_asset = t_asset,
    };
    t->buf = buf;
    t->cap = cap;
    t->len = 0;
    buf[0] = '\0';
}

void nt_entity_to_string(nt_entity_t e, char *buf, size_t cap) {
    NT_ASSERT(buf != NULL && cap > 0);
    text_sink_t t;
    text_sink_init(&t, buf, cap);
    nt_entity_introspect(e, &t.base);
}

void nt_log_entity(nt_log_level_t level, nt_entity_t e) {
    char buf[NT_LOG_BUF_SIZE];
    nt_entity_to_string(e, buf, sizeof(buf));
    nt_log_write(level, NULL, "%s", buf);
}
// #endregion

#else /* NT_INTROSPECT_ENABLED == 0: zero-footprint stubs */

const char *nt_introspect_ref_kind_name(nt_ref_kind_t kind) {
    (void)kind;
    return "ref";
}
void nt_entity_introspect(nt_entity_t e, nt_introspect_sink *s) {
    (void)e;
    (void)s;
}
void nt_entity_to_string(nt_entity_t e, char *buf, size_t cap) {
    (void)e;
    if (buf != NULL && cap > 0) {
        buf[0] = '\0';
    }
}
void nt_log_entity(nt_log_level_t level, nt_entity_t e) {
    (void)level;
    (void)e;
}

#endif
