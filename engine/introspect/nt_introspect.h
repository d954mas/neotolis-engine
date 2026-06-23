#ifndef NT_INTROSPECT_H
#define NT_INTROSPECT_H

#include "core/nt_types.h"
#include "entity/nt_entity.h"
#include "log/nt_log.h"

/* Dev-only entity introspection. A component self-registers a describe() with the entity storage
   registry; nt_entity_introspect walks core fields + each present component into a FORMAT-AGNOSTIC
   sink — text (here, zero-dep, backs logs/nt_entity_to_string) or JSON (in devapi, where cJSON lives). */

/* NT_INTROSPECT_ENABLED=0 (release/OFF mirror) compiles no-op bodies for zero footprint. Set
   build-wide so component describe()/registration see the same value without linking this module. */
#ifndef NT_INTROSPECT_ENABLED
#define NT_INTROSPECT_ENABLED 1
#endif

/* Write side (component apply / entity.set). Defaults to NT_INTROSPECT_ENABLED — a dev build that can
   read can also write; set OFF independently for a read-only deployment tier (the devapi entity-write
   group hard-requires it). Build-wide, like NT_INTROSPECT_ENABLED. */
#ifndef NT_INTROSPECT_WRITE_ENABLED
#define NT_INTROSPECT_WRITE_ENABLED NT_INTROSPECT_ENABLED
#endif

/* Max sink container-NESTING depth (entity object = depth 1, each begin_group adds one). Bounds nesting
   only — a flat entity carrying 100+ components stays at depth 2. */
#ifndef NT_INTROSPECT_MAX_DEPTH
#define NT_INTROSPECT_MAX_DEPTH 8
#endif
_Static_assert(NT_INTROSPECT_MAX_DEPTH > 1 && NT_INTROSPECT_MAX_DEPTH <= UINT8_MAX, "NT_INTROSPECT_MAX_DEPTH must be in (1, UINT8_MAX]");

/* Reference target kind. A ref field is emitted as a typed id token, never expanded inline — keeps
   the view acyclic and leaks no raw pointer; a tool follows the id with a separate query. */
typedef enum {
    NT_REF_ENTITY = 0,
    NT_REF_RESOURCE,
    NT_REF_HANDLE,
} nt_ref_kind_t;

/* Format-agnostic visitor. describe() emits fields/groups through these callbacks; the concrete sink
   renders them. Pick a field by MEANING: a handle/id into another store -> field_ref (typed,
   followable, leaks no pointer); an enumerated value -> field_enum (stable token, never the raw enum);
   a plain scalar -> field_u64/i64/f32/bool; vec3/vec4/mat3/mat4 -> field_floats (the sink reads
   `count`); free text -> field_str. begin_group/end_group nest (bounded by NT_INTROSPECT_MAX_DEPTH). */
typedef struct nt_introspect_sink {
    void (*begin_group)(struct nt_introspect_sink *s, const char *key);
    void (*end_group)(struct nt_introspect_sink *s);
    void (*field_f32)(struct nt_introspect_sink *s, const char *key, float v);
    void (*field_i64)(struct nt_introspect_sink *s, const char *key, int64_t v);
    void (*field_u64)(struct nt_introspect_sink *s, const char *key, uint64_t v);
    void (*field_bool)(struct nt_introspect_sink *s, const char *key, bool v);
    void (*field_floats)(struct nt_introspect_sink *s, const char *key, const float *v, int count);
    void (*field_str)(struct nt_introspect_sink *s, const char *key, const char *v);
    void (*field_enum)(struct nt_introspect_sink *s, const char *key, const char *token);
    void (*field_ref)(struct nt_introspect_sink *s, const char *key, nt_ref_kind_t kind, uint64_t id);
} nt_introspect_sink;

/* Stable token for a ref kind (shared by the text + JSON sinks). */
const char *nt_introspect_ref_kind_name(nt_ref_kind_t kind);

/* Emit core entity fields (id/index/generation/enabled) then each registered component that has(e),
   wrapping each in a group named after its storage. A dead handle emits id + alive:false and stops. */
void nt_entity_introspect(nt_entity_t e, nt_introspect_sink *sink);

/* Render an entity's introspection into a NUL-terminated debug string (truncated to fit cap). */
void nt_entity_to_string(nt_entity_t e, char *buf, size_t cap);

/* Log an entity's text representation at `level` (no JSON path, no cJSON). */
void nt_log_entity(nt_log_level_t level, nt_entity_t e);

/* ---- Write side (the inverse of the read sink) ----
 * A neutral typed value the component's apply() hook receives. devapi parses cJSON into this (the
 * inverse of the JSON sink reading a component out), so apply hooks never see cJSON. Deliberately
 * tiny: scalars + fixed float vectors; no ref/enum/str (refs/ids are read-only, no string field is
 * writable in v1). The kind is the WIRE SHAPE (arity), not the semantic: a 4-float value is NT_WV_VEC4
 * whether the field is a quaternion or an rgba color — devapi parses the shape, the apply() hook owns
 * the semantic (normalize quat / clamp color). */
typedef enum { NT_WV_F32, NT_WV_BOOL, NT_WV_VEC3, NT_WV_VEC4 } nt_write_kind_t;

typedef struct nt_write_value {
    nt_write_kind_t kind;
    union {
        float f32;  /* NT_WV_F32 */
        bool b;     /* NT_WV_BOOL */
        float v[4]; /* NT_WV_VEC3 uses [0..2]; NT_WV_VEC4 uses [0..3] (e.g. quaternion or rgba) */
    } as;
} nt_write_value;

#endif /* NT_INTROSPECT_H */
