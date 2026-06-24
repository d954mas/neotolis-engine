#ifndef NT_INTROSPECT_H
#define NT_INTROSPECT_H

#include "core/nt_types.h"
#include "entity/nt_entity.h"
#include "log/nt_log.h"

/* Dev-only entity introspection. Components self-register a describe(); nt_entity_introspect walks
   core fields + each present component into a format-agnostic sink (text here, JSON in devapi). */

/* NT_INTROSPECT_ENABLED=0 (release/OFF mirror) compiles no-op bodies for zero footprint. Set
   build-wide so component describe()/registration see the same value without linking this module. */
#ifndef NT_INTROSPECT_ENABLED
#define NT_INTROSPECT_ENABLED 1
#endif

/* Write side (component apply / entity.set). Set OFF independently for a read-only deployment tier.
   Build-wide, like NT_INTROSPECT_ENABLED. */
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

/* Format-agnostic visitor: describe() emits fields/groups, the concrete sink renders them. field_ref
   and field_enum emit a stable token (never a raw pointer or enum value); field_floats reads `count`
   (vec3/vec4/mat3/mat4); begin_group/end_group nest — the resolving JSON sink caps nesting at
   NT_INTROSPECT_MAX_DEPTH (describe() hooks are the only nesting source). */
typedef struct nt_introspect_sink {
    void (*begin_group)(struct nt_introspect_sink *s, const char *key);
    void (*end_group)(struct nt_introspect_sink *s);
    void (*field_f32)(struct nt_introspect_sink *s, const char *key, float v);
    void (*field_i64)(struct nt_introspect_sink *s, const char *key, int64_t v);
    void (*field_u64)(struct nt_introspect_sink *s, const char *key, uint64_t v);
    /* A 64-bit OPAQUE id (hash/handle), rendered as a 0x-hex string so the full 64 bits survive — a JSON
       number is a double and would drop the low bits above 2^53. Use for ids, not countable quantities. */
    void (*field_u64_hex)(struct nt_introspect_sink *s, const char *key, uint64_t v);
    void (*field_bool)(struct nt_introspect_sink *s, const char *key, bool v);
    void (*field_floats)(struct nt_introspect_sink *s, const char *key, const float *v, int count);
    void (*field_str)(struct nt_introspect_sink *s, const char *key, const char *v);
    void (*field_enum)(struct nt_introspect_sink *s, const char *key, const char *token);
    void (*field_ref)(struct nt_introspect_sink *s, const char *key, nt_ref_kind_t kind, uint64_t id);
    /* Describe the current group's asset by its runtime handle (asset_type = nt_asset_type_t): emits a
       `handle` field, and a RESOLVING sink (the devapi JSON sink) also reverse-maps it to its source
       `resource`/`name`. Keyless — writes into the component's own group. Lets a resource-backed
       component (mesh/...) expose its asset WITHOUT depending on the resource system. */
    void (*field_asset)(struct nt_introspect_sink *s, uint8_t asset_type, uint32_t runtime_handle);
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
 * Neutral typed value the component's apply() hook receives; devapi parses cJSON into this, so apply
 * hooks never see cJSON. `kind` is the wire shape (arity), not the semantic: a 4-float value is
 * NT_WV_VEC4 whether it's a quaternion or an rgba color — the apply() hook owns the semantic. */
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
