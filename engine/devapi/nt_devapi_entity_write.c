#include "core/nt_assert.h"
#include "core/nt_types.h"
#include "devapi/nt_devapi_internal.h"
#include "entity/nt_entity.h"
#include "introspect/nt_introspect.h"

/* entity.set: a dev-only DEBUG write. Sets writable component fields through the component's apply()
   hook (the real setter — dirty flag / color packing / quaternion normalize), so it keeps
   engine invariants but bypasses game logic: a debug/tuning tool, not a control path. */

/* Batch field cap: a bot must not force an unbounded validate/apply loop. */
#ifndef NT_DEVAPI_ENTITY_WRITE_MAX_FIELDS
#define NT_DEVAPI_ENTITY_WRITE_MAX_FIELDS 16
#endif
/* Sizes the keys/vals stack arrays; a non-positive override would form a zero/negative-length array. */
_Static_assert(NT_DEVAPI_ENTITY_WRITE_MAX_FIELDS > 0, "NT_DEVAPI_ENTITY_WRITE_MAX_FIELDS must be > 0");

/* Parse a cJSON value into a typed nt_write_value by WIRE SHAPE only (number->F32, bool->BOOL,
   array[3]->VEC3, array[4]->VEC4). Range/semantic (color [0,1], unit quaternion) is the apply() hook's job. */
static bool parse_write_value(const cJSON *node, nt_write_value *out, nt_devapi_error *err) {
    if (node == NULL) {
        return nt_devapi_fail(err, "entity.set: missing value");
    }
    if (cJSON_IsBool(node)) {
        out->kind = NT_WV_BOOL;
        out->as.b = cJSON_IsTrue(node);
        return true;
    }
    if (cJSON_IsNumber(node)) {
        float f = 0.0F;
        if (!nt_devapi_parse_f32_param(node, err, "entity.set: value must be finite", &f)) {
            return false;
        }
        out->kind = NT_WV_F32;
        out->as.f32 = f;
        return true;
    }
    if (cJSON_IsArray(node)) {
        int n = cJSON_GetArraySize(node);
        if (n != 3 && n != 4) {
            return nt_devapi_fail(err, "entity.set: array value must have 3 or 4 numbers");
        }
        for (int i = 0; i < n; i++) {
            float f = 0.0F;
            if (!nt_devapi_parse_f32_param(cJSON_GetArrayItem(node, i), err, "entity.set: array value must be finite numbers", &f)) {
                return false;
            }
            out->as.v[i] = f;
        }
        out->kind = (n == 3) ? NT_WV_VEC3 : NT_WV_VEC4;
        return true;
    }
    return nt_devapi_fail(err, "entity.set: value must be a number, bool, or array of 3 or 4 numbers");
}

/* Resolve {id, component} to a live entity carrying a writable component (mirrors entity.list
   resolution: total/query API only, never an asserting accessor on bot input). */
static bool resolve_target(const cJSON *params, nt_entity_t *out_e, nt_comp_apply_fn *out_apply, const char **out_component, nt_devapi_error *err) {
    uint32_t id = 0;
    if (!nt_devapi_parse_u32_param_exact(cJSON_GetObjectItemCaseSensitive(params, "id"), UINT32_MAX, err, "entity.set: id must be an integer in [0, UINT32_MAX]", &id)) {
        return false;
    }
    nt_entity_t e = {.id = id};
    if (!nt_entity_is_alive(e)) {
        return nt_devapi_fail(err, "entity.set: entity dead or stale");
    }
    const cJSON *jcomp = cJSON_GetObjectItemCaseSensitive(params, "component");
    if (!cJSON_IsString(jcomp) || jcomp->valuestring == NULL) {
        return nt_devapi_fail(err, "entity.set: component must be a string");
    }
    nt_comp_id_t cid = nt_entity_storage_find(jcomp->valuestring);
    if (cid == NT_COMP_ID_INVALID) {
        return nt_devapi_fail(err, "entity.set: unknown component");
    }
    if (!nt_entity_has_comp(e, cid)) {
        return nt_devapi_fail(err, "entity.set: entity has no such component");
    }
    const nt_comp_storage_reg_t *reg = nt_entity_storage_at(cid);
    if (reg->apply == NULL) {
        return nt_devapi_fail(err, "entity.set: component is read-only");
    }
    *out_e = e;
    *out_apply = reg->apply;
    *out_component = jcomp->valuestring;
    return true;
}

static bool apply_single(nt_entity_t e, nt_comp_apply_fn apply, const cJSON *params, cJSON *applied, nt_devapi_error *err) {
    const cJSON *jfield = cJSON_GetObjectItemCaseSensitive(params, "field");
    if (!cJSON_IsString(jfield) || jfield->valuestring == NULL) {
        return nt_devapi_fail(err, "entity.set: field must be a string (or use fields:{...})");
    }
    nt_write_value wv;
    if (!parse_write_value(cJSON_GetObjectItemCaseSensitive(params, "value"), &wv, err)) {
        return false;
    }
    /* Dry-run validate before the real write, so a hook that could fail mid-mutation never leaves a
       partial state — the same whole-or-nothing contract the batch path uses, applied uniformly. */
    const char *msg = NULL;
    if (!apply(e, jfield->valuestring, &wv, true, &msg)) {
        return nt_devapi_fail(err, msg != NULL ? msg : "entity.set: invalid field");
    }
    msg = NULL;
    bool ok = apply(e, jfield->valuestring, &wv, false, &msg);
    NT_ASSERT(ok && "entity.set: dry-run-validated field failed on apply");
    (void)ok;
    cJSON_bool added = cJSON_AddItemToArray(applied, cJSON_CreateString(jfield->valuestring));
    NT_ASSERT(added);
    (void)added;
    return true;
}

/* Phase 1 of the batch: parse + dry_run-validate every field into keys/vals, mutating nothing.
   Returns the field count, or -1 (sets *err) on the first bad field — so the batch aborts clean. */
static int batch_validate(nt_entity_t e, nt_comp_apply_fn apply, const cJSON *jfields, const char **keys, nt_write_value *vals, nt_devapi_error *err) {
    int count = 0;
    const cJSON *fld = NULL;
    cJSON_ArrayForEach(fld, jfields) {
        if (!parse_write_value(fld, &vals[count], err)) {
            return -1;
        }
        const char *msg = NULL;
        if (!apply(e, fld->string, &vals[count], true, &msg)) {
            nt_devapi_fail(err, msg != NULL ? msg : "entity.set: invalid field");
            return -1;
        }
        keys[count] = fld->string;
        count++;
    }
    return count;
}

/* Whole-or-nothing: validate EVERY field first (batch_validate); only if all pass, apply all (the
   second pass is infallible). Avoids a half-applied TRS (new position, old rotation). */
static bool apply_batch(nt_entity_t e, nt_comp_apply_fn apply, const cJSON *jfields, cJSON *applied, nt_devapi_error *err) {
    if (!cJSON_IsObject(jfields)) {
        return nt_devapi_fail(err, "entity.set: fields must be an object");
    }
    int n = cJSON_GetArraySize(jfields);
    if (n == 0 || n > NT_DEVAPI_ENTITY_WRITE_MAX_FIELDS) {
        return nt_devapi_fail(err, "entity.set: fields must hold 1.." NT_DEVAPI_STR(NT_DEVAPI_ENTITY_WRITE_MAX_FIELDS) " entries");
    }
    const char *keys[NT_DEVAPI_ENTITY_WRITE_MAX_FIELDS];
    nt_write_value vals[NT_DEVAPI_ENTITY_WRITE_MAX_FIELDS];
    int count = batch_validate(e, apply, jfields, keys, vals, err);
    if (count < 0) {
        return false; /* nothing mutated */
    }
    for (int i = 0; i < count; i++) {
        const char *msg = NULL;
        bool ok = apply(e, keys[i], &vals[i], false, &msg);
        NT_ASSERT(ok && "entity.set: dry-run-validated field failed on apply");
        (void)ok;
        cJSON_bool added = cJSON_AddItemToArray(applied, cJSON_CreateString(keys[i]));
        NT_ASSERT(added);
        (void)added;
    }
    return true;
}

/* entity.set{id, component, field, value} OR {id, component, fields:{...}} -> write via apply(). */
static bool cmd_entity_set(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    nt_entity_t e;
    nt_comp_apply_fn apply = NULL;
    const char *component = NULL;
    if (!resolve_target(params, &e, &apply, &component, err)) {
        return false;
    }

    const cJSON *jfields = cJSON_GetObjectItemCaseSensitive(params, "fields");
    const cJSON *jfield = cJSON_GetObjectItemCaseSensitive(params, "field");
    if (jfields != NULL && jfield != NULL) {
        return nt_devapi_fail(err, "entity.set: use field+value OR fields, not both");
    }

    cJSON *applied = cJSON_AddArrayToObject(result, "fields");
    NT_ASSERT(applied != NULL);
    bool ok = (jfields != NULL) ? apply_batch(e, apply, jfields, applied, err) : apply_single(e, apply, params, applied, err);
    if (!ok) {
        return false;
    }

    /* The envelope already carries top-level ok; the result just reports what was written. */
    devapi_add_string(result, "component", component);
    return true;
}

static const nt_devapi_command k_entity_write_cmds[] = {
    {
        .method = "entity.set",
        .summary = "DEBUG/INSPECTION write: set writable component field(s) on a live entity through the component's setter "
                   "(maintains dirty flags / color packing / quaternion normalize); validates type/arity/range -> bad_params. "
                   "Bypasses game logic -- a debug/tuning tool, not a control path. Single {field,value} or whole-or-nothing {fields:{...}}. "
                   "Transform world_matrix recomputes on the next update.",
        .params_shape = "{id:number, component:string, field?:string, value?:<json>, fields?:object}",
        .result_shape = "{component:string, fields:[string]}",
        .fn = cmd_entity_set,
    },
};

void nt_devapi_register_entity_write(void) { nt_devapi_register(k_entity_write_cmds, NT_COUNTOF(k_entity_write_cmds), NULL); }
