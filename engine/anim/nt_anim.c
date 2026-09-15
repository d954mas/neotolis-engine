#include "anim/nt_anim.h"

#include <string.h>

/* NT_ASSERT_OFF does not evaluate its expression, so everything that exists
 * only to feed an assert must disappear with it or it warns as unused. */
#if NT_ASSERT_MODE != NT_ASSERT_OFF
#include <stdbool.h>

/* x - x is 0 only for a finite x; keeps the module free of <math.h> and libm. */
static bool nt_anim_is_finite(float x) { return (x - x) == 0.0F; }
#endif

#if NT_ANIM_CHECKS && (NT_ASSERT_MODE != NT_ASSERT_OFF)
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void nt_anim_check_locals(const nt_anim_trs_t *local, uint16_t first, uint16_t count) {
    const uint16_t end = (uint16_t)(first + count);
    for (uint16_t j = first; j < end; ++j) {
        const nt_anim_trs_t *l = &local[j];
        for (int c = 0; c < 3; ++c) {
            NT_ASSERT(nt_anim_is_finite(l->t[c]));
            NT_ASSERT(nt_anim_is_finite(l->s[c]));
        }
        const float dot = (l->q[0] * l->q[0]) + (l->q[1] * l->q[1]) + (l->q[2] * l->q[2]) + (l->q[3] * l->q[3]);
        /* Two-sided instead of fabsf: a NaN dot fails both comparisons. */
        NT_ASSERT((dot - 1.0F) < 1e-3F && (1.0F - dot) < 1e-3F);
    }
}
#endif

void nt_anim_mat34_from_mat4(const float m[16], nt_anim_mat34_t *out) {
    NT_ASSERT(m != NULL);
    NT_ASSERT(out != NULL);

    for (int r = 0; r < 3; ++r) {
        out->r[r][0] = m[r];
        out->r[r][1] = m[4 + r];
        out->r[r][2] = m[8 + r];
        out->r[r][3] = m[12 + r];
    }
}

void nt_anim_pose_rest(const nt_anim_skeleton_t *skel, nt_anim_trs_t *local) {
    NT_ASSERT(skel != NULL);
    NT_ASSERT(skel->rest != NULL);
    NT_ASSERT(local != NULL);
    NT_ASSERT(local != skel->rest);

    memcpy(local, skel->rest, (size_t)skel->joint_count * sizeof(nt_anim_trs_t));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_anim_fk(const nt_anim_skeleton_t *skel, const nt_anim_trs_t *local, nt_anim_mat34_t *model, uint16_t first, uint16_t count) {
    NT_ASSERT(skel != NULL);
    NT_ASSERT(skel->parent != NULL);
    NT_ASSERT(skel->subtree_end != NULL);
    NT_ASSERT(local != NULL);
    NT_ASSERT(model != NULL);
    NT_ASSERT(count >= 1U);
    NT_ASSERT((uint32_t)first + (uint32_t)count <= (uint32_t)skel->joint_count);
    NT_ASSERT(skel->parent[first] == NT_ANIM_NO_PARENT || (uint32_t)first + (uint32_t)count <= (uint32_t)skel->subtree_end[first]);

#if NT_ANIM_CHECKS && (NT_ASSERT_MODE != NT_ASSERT_OFF)
    nt_anim_check_locals(local, first, count);
#endif

    const uint16_t end = (uint16_t)(first + count);
    for (uint16_t j = first; j < end; ++j) {
        nt_anim_mat34_t l;
        nt_anim_mat34_from_trs(&local[j], &l);
        const uint16_t p = skel->parent[j];
        if (p == NT_ANIM_NO_PARENT) {
            model[j] = l;
        } else {
            /* parent[j] < j in preorder, so model[j] never aliases model[p]. */
            nt_anim_mat34_mul(&model[p], &l, &model[j]);
        }
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_anim_socket(const float world[16], const nt_anim_mat34_t *g_joint, const nt_anim_trs_t *socket_local, nt_anim_mat34_t *out) {
    NT_ASSERT(world != NULL);
    NT_ASSERT(g_joint != NULL);
    NT_ASSERT(socket_local != NULL);
    NT_ASSERT(out != NULL);
    NT_ASSERT(out != g_joint);

    nt_anim_mat34_t e;
    nt_anim_mat34_from_mat4(world, &e);

    nt_anim_mat34_t eg;
    nt_anim_mat34_mul(&e, g_joint, &eg);

    nt_anim_mat34_t s;
    nt_anim_mat34_from_trs(socket_local, &s);

    nt_anim_mat34_mul(&eg, &s, out);
}

// #region rig identity

/* Header bytes: "NRIG", schema version, convention id, joint count. */
#define NT_ANIM_RIG_HEADER_BYTES 8U
/* Per joint: u32 id + u16 parent + 10 canonical binary32 of the rest TRS. */
#define NT_ANIM_RIG_JOINT_BYTES 46U

static uint32_t nt_anim_put_u8(uint8_t *bytes, uint32_t offset, uint8_t v) {
    bytes[offset] = v;
    return offset + 1U;
}

static uint32_t nt_anim_put_u16(uint8_t *bytes, uint32_t offset, uint16_t v) {
    bytes[offset] = (uint8_t)(v & 0xFFU);
    bytes[offset + 1U] = (uint8_t)((v >> 8U) & 0xFFU);
    return offset + 2U;
}

static uint32_t nt_anim_put_u32(uint8_t *bytes, uint32_t offset, uint32_t v) {
    bytes[offset] = (uint8_t)(v & 0xFFU);
    bytes[offset + 1U] = (uint8_t)((v >> 8U) & 0xFFU);
    bytes[offset + 2U] = (uint8_t)((v >> 16U) & 0xFFU);
    bytes[offset + 3U] = (uint8_t)((v >> 24U) & 0xFFU);
    return offset + 4U;
}

static uint32_t nt_anim_put_f32(uint8_t *bytes, uint32_t offset, float v) {
    NT_ASSERT(nt_anim_is_finite(v));

    /* -0 and +0 describe the same rest pose, so only +0 is ever hashed. */
    const float canonical = (v == 0.0F) ? 0.0F : v;
    uint32_t bits = 0;
    memcpy(&bits, &canonical, sizeof(bits));
    return nt_anim_put_u32(bytes, offset, bits);
}

/* q and -q are the same rotation: keep the sign that makes the largest
 * component positive, ties broken by the first maximum in x,y,z,w order. */
static void nt_anim_canonical_quat(const float q[4], float out[4]) {
    int best = 0;
    float best_abs = (q[0] < 0.0F) ? -q[0] : q[0];
    for (int c = 1; c < 4; ++c) {
        const float a = (q[c] < 0.0F) ? -q[c] : q[c];
        if (a > best_abs) {
            best_abs = a;
            best = c;
        }
    }

    const float sign = (q[best] < 0.0F) ? -1.0F : 1.0F;
    for (int c = 0; c < 4; ++c) {
        out[c] = q[c] * sign;
    }
}

uint32_t nt_anim_rig_compat_id_size(uint16_t joint_count) { return NT_ANIM_RIG_HEADER_BYTES + (NT_ANIM_RIG_JOINT_BYTES * (uint32_t)joint_count); }

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_hash64_t nt_anim_rig_compat_id(const nt_anim_skeleton_t *skel, void *scratch, uint32_t scratch_size) {
    NT_ASSERT(skel != NULL);
    NT_ASSERT(skel->parent != NULL);
    NT_ASSERT(skel->joint_id != NULL);
    NT_ASSERT(skel->rest != NULL);
    NT_ASSERT(scratch != NULL);

    const uint32_t size = nt_anim_rig_compat_id_size(skel->joint_count);
    NT_ASSERT(scratch_size >= size);

    uint8_t *bytes = (uint8_t *)scratch;
    uint32_t offset = 0;
    offset = nt_anim_put_u8(bytes, offset, (uint8_t)'N');
    offset = nt_anim_put_u8(bytes, offset, (uint8_t)'R');
    offset = nt_anim_put_u8(bytes, offset, (uint8_t)'I');
    offset = nt_anim_put_u8(bytes, offset, (uint8_t)'G');
    offset = nt_anim_put_u8(bytes, offset, (uint8_t)NT_ANIM_RIG_SCHEMA_VERSION);
    offset = nt_anim_put_u8(bytes, offset, (uint8_t)NT_ANIM_RIG_CONVENTION_GLTF);
    offset = nt_anim_put_u16(bytes, offset, skel->joint_count);

    for (uint16_t j = 0; j < skel->joint_count; ++j) {
        const nt_anim_trs_t *rest = &skel->rest[j];
        offset = nt_anim_put_u32(bytes, offset, skel->joint_id[j]);
        offset = nt_anim_put_u16(bytes, offset, skel->parent[j]);
        for (int c = 0; c < 3; ++c) {
            offset = nt_anim_put_f32(bytes, offset, rest->t[c]);
        }
        float q[4];
        nt_anim_canonical_quat(rest->q, q);
        for (int c = 0; c < 4; ++c) {
            offset = nt_anim_put_f32(bytes, offset, q[c]);
        }
        for (int c = 0; c < 3; ++c) {
            offset = nt_anim_put_f32(bytes, offset, rest->s[c]);
        }
    }

    NT_ASSERT(offset == size);
    return nt_hash64(bytes, size);
}

// #endregion
