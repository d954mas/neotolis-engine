#include "test_helpers/anim_rig.h"

#include <math.h>
#include <string.h>

#include "core/nt_assert.h"
#include "hash/nt_hash.h"
#include "math/nt_math.h"

static const char *const s_names[ANIM_RIG_JOINT_COUNT] = {"root_a", "spine", "helper", "arm", "hand", "leg", "root_b", "tail1", "tail2"};

static const uint16_t s_parent[ANIM_RIG_JOINT_COUNT] = {NT_ANIM_NO_PARENT, 0, 1, 2, 3, 1, NT_ANIM_NO_PARENT, 6, 7};

static const uint16_t s_subtree_end[ANIM_RIG_JOINT_COUNT] = {6, 6, 5, 5, 5, 6, 9, 9, 9};

static const uint16_t s_remap_a[ANIM_RIG_PALETTE_A_COUNT] = {0, 1, 3, 4, 5};
static const uint16_t s_remap_b[ANIM_RIG_PALETTE_B_COUNT] = {6, 7, 8, 4};

static const float k_deg_to_rad = 0.017453292519943295F;

static void set_trs(nt_anim_trs_t *o, float tx, float ty, float tz) {
    o->t[0] = tx;
    o->t[1] = ty;
    o->t[2] = tz;
    o->q[0] = 0.0F;
    o->q[1] = 0.0F;
    o->q[2] = 0.0F;
    o->q[3] = 1.0F;
    o->s[0] = 1.0F;
    o->s[1] = 1.0F;
    o->s[2] = 1.0F;
}

static void set_axis_angle(nt_anim_trs_t *o, float ax, float ay, float az, float degrees) {
    const float half = (degrees * 0.5F) * k_deg_to_rad;
    const float sn = sinf(half);
    o->q[0] = ax * sn;
    o->q[1] = ay * sn;
    o->q[2] = az * sn;
    o->q[3] = cosf(half);
}

static void set_scale(nt_anim_trs_t *o, float sx, float sy, float sz) {
    o->s[0] = sx;
    o->s[1] = sy;
    o->s[2] = sz;
}

void anim_rig_asymmetric(anim_rig_t *out) {
    NT_ASSERT(out != NULL);

    for (uint16_t j = 0; j < ANIM_RIG_JOINT_COUNT; ++j) {
        out->joint_id[j] = nt_hash32_str(s_names[j]).value;
    }

    set_trs(&out->rest[0], 0.0F, 1.0F, 0.0F);
    set_axis_angle(&out->rest[0], 0.0F, 1.0F, 0.0F, 30.0F);
    set_trs(&out->rest[1], 0.0F, 0.5F, 0.0F);
    set_scale(&out->rest[1], 1.0F, 1.2F, 1.0F);
    set_trs(&out->rest[2], 0.1F, 0.0F, 0.0F);
    set_axis_angle(&out->rest[2], 0.0F, 0.0F, 1.0F, 90.0F);
    /* Nonuniform scale under a 90 deg rotation: T*R*S and T*S*R differ here. */
    set_scale(&out->rest[2], 1.0F, 1.3F, 0.7F);
    set_trs(&out->rest[3], 0.0F, 0.4F, 0.0F);
    set_trs(&out->rest[4], 0.0F, 0.3F, 0.0F);
    set_axis_angle(&out->rest[4], 1.0F, 0.0F, 0.0F, 45.0F);
    set_trs(&out->rest[5], -0.2F, -0.5F, 0.0F);
    set_trs(&out->rest[6], 2.0F, 0.0F, 0.0F);
    set_axis_angle(&out->rest[6], 0.0F, 0.0F, 1.0F, 180.0F);
    set_trs(&out->rest[7], 0.0F, 0.0F, 0.3F);
    set_scale(&out->rest[7], 0.5F, 0.5F, 0.5F);
    set_trs(&out->rest[8], 0.0F, 0.0F, 0.3F);

    /* Bind replaces the local rotation of arm, hand and tail1 so that the bind
     * pose is not the rest pose and its inverse binds are not identities. */
    memcpy(out->bind, out->rest, sizeof(out->bind));
    set_axis_angle(&out->bind[3], 0.0F, 0.0F, 1.0F, -60.0F);
    set_axis_angle(&out->bind[4], 0.0F, 1.0F, 0.0F, 20.0F);
    set_axis_angle(&out->bind[7], 1.0F, 0.0F, 0.0F, 30.0F);

    out->skel.parent = s_parent;
    out->skel.subtree_end = s_subtree_end;
    out->skel.joint_id = out->joint_id;
    out->skel.rest = out->rest;
    out->skel.joint_count = ANIM_RIG_JOINT_COUNT;
    out->skel.rig_compat_id = nt_anim_rig_compat_id(&out->skel, out->rig_scratch, (uint32_t)sizeof(out->rig_scratch));
}

/* cglm reference FK: the inverse binds must not come from the kernel they are
 * later used to verify. */
static void ref_mat4_from_trs(const nt_anim_trs_t *trs, mat4 out) {
    vec3 t = {trs->t[0], trs->t[1], trs->t[2]};
    versor q = {trs->q[0], trs->q[1], trs->q[2], trs->q[3]};
    vec3 s = {trs->s[0], trs->s[1], trs->s[2]};

    mat4 mt;
    glm_translate_make(mt, t);
    mat4 mr;
    glm_quat_mat4(q, mr);
    mat4 ms;
    glm_scale_make(ms, s);
    mat4 tr;
    glm_mat4_mul(mt, mr, tr);
    glm_mat4_mul(tr, ms, out);
}

static void ref_fk_chain(const nt_anim_trs_t *local, mat4 *out) {
    for (uint16_t j = 0; j < ANIM_RIG_JOINT_COUNT; ++j) {
        mat4 l;
        ref_mat4_from_trs(&local[j], l);
        const uint16_t p = s_parent[j];
        if (p == NT_ANIM_NO_PARENT) {
            glm_mat4_copy(l, out[j]);
        } else {
            glm_mat4_mul(out[p], l, out[j]);
        }
    }
}

static void fill_inverse_binds(mat4 *g_bind, const uint16_t *remap, uint16_t count, nt_anim_mat34_t *out) {
    for (uint16_t p = 0; p < count; ++p) {
        mat4 inv;
        glm_mat4_inv(g_bind[remap[p]], inv);
        nt_anim_mat34_from_mat4((const float *)inv, &out[p]);
    }
}

void anim_rig_bindings(anim_rig_t *rig, nt_skin_binding_t *a, nt_skin_binding_t *b) {
    NT_ASSERT(rig != NULL);
    NT_ASSERT(a != NULL);
    NT_ASSERT(b != NULL);

    mat4 g_bind[ANIM_RIG_JOINT_COUNT];
    ref_fk_chain(rig->bind, g_bind);

    fill_inverse_binds(g_bind, s_remap_a, ANIM_RIG_PALETTE_A_COUNT, rig->inverse_bind_a);
    fill_inverse_binds(g_bind, s_remap_b, ANIM_RIG_PALETTE_B_COUNT, rig->inverse_bind_b);

    a->rig_compat_id = rig->skel.rig_compat_id;
    a->remap = s_remap_a;
    a->inverse_bind = rig->inverse_bind_a;
    a->palette_count = ANIM_RIG_PALETTE_A_COUNT;

    b->rig_compat_id = rig->skel.rig_compat_id;
    b->remap = s_remap_b;
    b->inverse_bind = rig->inverse_bind_b;
    b->palette_count = ANIM_RIG_PALETTE_B_COUNT;
}
