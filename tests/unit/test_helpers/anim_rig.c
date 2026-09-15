#include "test_helpers/anim_rig.h"

#include <math.h>
#include <string.h>

#include "core/nt_assert.h"
#include "hash/nt_hash.h"
#include "math/nt_math.h"

static const char *const s_names[ANIM_RIG_JOINT_COUNT] = {"root_a", "spine", "helper", "arm", "hand", "leg", "root_b", "tail1", "tail2"};

static const uint16_t s_parent[ANIM_RIG_JOINT_COUNT] = {NT_ANIM_NO_PARENT, 0, 1, 2, 3, 1, NT_ANIM_NO_PARENT, 6, 7};

static const uint16_t s_subtree_end[ANIM_RIG_JOINT_COUNT] = {6, 6, 5, 5, 5, 6, 9, 9, 9};

static uint32_t s_joint_id[ANIM_RIG_JOINT_COUNT];
static nt_anim_trs_t s_rest[ANIM_RIG_JOINT_COUNT];

/* nt_anim_rig_compat_id_size(ANIM_RIG_JOINT_COUNT) = 8 + 46*9. */
static uint8_t s_rig_scratch[8U + (46U * ANIM_RIG_JOINT_COUNT)];

static const uint16_t s_remap_a[ANIM_RIG_PALETTE_A_COUNT] = {0, 1, 3, 4, 5};
static const uint16_t s_remap_b[ANIM_RIG_PALETTE_B_COUNT] = {6, 7, 8, 4};
static nt_anim_mat34_t s_inverse_bind_a[ANIM_RIG_PALETTE_A_COUNT];
static nt_anim_mat34_t s_inverse_bind_b[ANIM_RIG_PALETTE_B_COUNT];

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
        s_joint_id[j] = nt_hash32_str(s_names[j]).value;
    }

    set_trs(&s_rest[0], 0.0F, 1.0F, 0.0F);
    set_axis_angle(&s_rest[0], 0.0F, 1.0F, 0.0F, 30.0F);
    set_trs(&s_rest[1], 0.0F, 0.5F, 0.0F);
    set_scale(&s_rest[1], 1.0F, 1.2F, 1.0F);
    set_trs(&s_rest[2], 0.1F, 0.0F, 0.0F);
    set_axis_angle(&s_rest[2], 0.0F, 0.0F, 1.0F, 90.0F);
    set_trs(&s_rest[3], 0.0F, 0.4F, 0.0F);
    set_trs(&s_rest[4], 0.0F, 0.3F, 0.0F);
    set_axis_angle(&s_rest[4], 1.0F, 0.0F, 0.0F, 45.0F);
    set_trs(&s_rest[5], -0.2F, -0.5F, 0.0F);
    set_trs(&s_rest[6], 2.0F, 0.0F, 0.0F);
    set_axis_angle(&s_rest[6], 0.0F, 0.0F, 1.0F, 180.0F);
    set_trs(&s_rest[7], 0.0F, 0.0F, 0.3F);
    set_scale(&s_rest[7], 0.5F, 0.5F, 0.5F);
    set_trs(&s_rest[8], 0.0F, 0.0F, 0.3F);

    /* Bind replaces the local rotation of arm, hand and tail1 so that the bind
     * pose is not the rest pose and its inverse binds are not identities. */
    memcpy(out->bind, s_rest, sizeof(out->bind));
    set_axis_angle(&out->bind[3], 0.0F, 0.0F, 1.0F, -60.0F);
    set_axis_angle(&out->bind[4], 0.0F, 1.0F, 0.0F, 20.0F);
    set_axis_angle(&out->bind[7], 1.0F, 0.0F, 0.0F, 30.0F);

    out->skel.parent = s_parent;
    out->skel.subtree_end = s_subtree_end;
    out->skel.joint_id = s_joint_id;
    out->skel.rest = s_rest;
    out->skel.joint_count = ANIM_RIG_JOINT_COUNT;
    out->skel.rig_compat_id = nt_anim_rig_compat_id(&out->skel, s_rig_scratch, (uint32_t)sizeof(s_rig_scratch));
}

/* The engine has no mat34 -> mat4 helper on purpose; the reference path needs
 * one to reach cglm's inverse. */
static void mat4_from_mat34(const nt_anim_mat34_t *m, mat4 out) {
    for (int r = 0; r < 3; ++r) {
        out[0][r] = m->r[r][0];
        out[1][r] = m->r[r][1];
        out[2][r] = m->r[r][2];
        out[3][r] = m->r[r][3];
    }
    out[0][3] = 0.0F;
    out[1][3] = 0.0F;
    out[2][3] = 0.0F;
    out[3][3] = 1.0F;
}

static void fill_inverse_binds(const nt_anim_mat34_t *g_bind, const uint16_t *remap, uint16_t count, nt_anim_mat34_t *out) {
    for (uint16_t p = 0; p < count; ++p) {
        mat4 g;
        mat4_from_mat34(&g_bind[remap[p]], g);
        mat4 inv;
        glm_mat4_inv(g, inv);
        nt_anim_mat34_from_mat4((const float *)inv, &out[p]);
    }
}

void anim_rig_bindings(const anim_rig_t *rig, nt_skin_binding_t *a, nt_skin_binding_t *b) {
    NT_ASSERT(rig != NULL);
    NT_ASSERT(a != NULL);
    NT_ASSERT(b != NULL);

    nt_anim_mat34_t g_bind[ANIM_RIG_JOINT_COUNT];
    nt_anim_fk(&rig->skel, rig->bind, g_bind, 0, ANIM_RIG_JOINT_COUNT);

    fill_inverse_binds(g_bind, s_remap_a, ANIM_RIG_PALETTE_A_COUNT, s_inverse_bind_a);
    fill_inverse_binds(g_bind, s_remap_b, ANIM_RIG_PALETTE_B_COUNT, s_inverse_bind_b);

    a->rig_compat_id = rig->skel.rig_compat_id;
    a->remap = s_remap_a;
    a->inverse_bind = s_inverse_bind_a;
    a->palette_count = ANIM_RIG_PALETTE_A_COUNT;

    b->rig_compat_id = rig->skel.rig_compat_id;
    b->remap = s_remap_b;
    b->inverse_bind = s_inverse_bind_b;
    b->palette_count = ANIM_RIG_PALETTE_B_COUNT;
}
