#ifndef NT_ANIM_H
#define NT_ANIM_H

#include <stddef.h>
#include <stdint.h>

#include "core/nt_assert.h"
#include "hash/nt_hash.h"

/*
 * nt_anim — pose ABI, skeleton view, 3x4 affine kernels, FK and sockets.
 *
 * Column vectors: L = T*R*S, G[j] = G[parent[j]]*L[j], roots G = L.
 * Every function is void, allocates nothing and retains no pointer past the
 * call. Preconditions are NT_ASSERT contracts, never recoverable results.
 */

/* Per-element input validation (finite t/s, unit quaternion). On in Debug,
 * overridable from CMake. */
#ifndef NT_ANIM_CHECKS
#ifdef NT_DEBUG
#define NT_ANIM_CHECKS 1
#else
#define NT_ANIM_CHECKS 0
#endif
#endif

/* Local joint transform, AoS in joint order. Quaternion is unit xyzw. */
typedef struct {
    float t[3];
    float q[4];
    float s[3];
} nt_anim_trs_t;

_Static_assert(sizeof(nt_anim_trs_t) == 40, "pose ABI: nt_anim_trs_t is 40 bytes");
_Static_assert(offsetof(nt_anim_trs_t, t) == 0, "pose ABI: t at offset 0");
_Static_assert(offsetof(nt_anim_trs_t, q) == 12, "pose ABI: q at offset 12");
_Static_assert(offsetof(nt_anim_trs_t, s) == 28, "pose ABI: s at offset 28");
_Static_assert(_Alignof(nt_anim_trs_t) == 4, "pose ABI: alignment 4");

/* Affine 3x4, column-vector convention, rows [m_r0 m_r1 m_r2 m_r3]:
 * r[i][3] is translation. Preserves the shear that hierarchical TRS produces. */
typedef struct {
    float r[3][4];
} nt_anim_mat34_t;

_Static_assert(sizeof(nt_anim_mat34_t) == 48, "pose ABI: nt_anim_mat34_t is 48 bytes");

#define NT_ANIM_NO_PARENT UINT16_MAX

/* Version of the rig identity byte schema; a new value is a new rig identity. */
#define NT_ANIM_RIG_SCHEMA_VERSION 1
/* Unit/axis convention of the hashed rest pose: glTF metres, Y-up, right-handed. */
#define NT_ANIM_RIG_CONVENTION_GLTF 1

/* Immutable borrowed view of one rig. The owner is whoever built the arrays
 * (a skeleton activator or a test fixture); it keeps them alive and unchanged
 * until it republishes or destroys them. Kernels read the view for the
 * duration of the call and never store the pointer, so the caller frees
 * nothing here. Every array is non-NULL and holds joint_count entries when
 * joint_count > 0. */
typedef struct {
    nt_hash64_t rig_compat_id;
    const uint16_t *parent;      /* NT_ANIM_NO_PARENT = root; parent[j] < j (preorder) */
    const uint16_t *subtree_end; /* subtree of j = [j, subtree_end[j]) */
    const uint32_t *joint_id;    /* stable id, nt_hash32_str(node name) */
    const nt_anim_trs_t *rest;   /* local rest pose */
    uint16_t joint_count;
} nt_anim_skeleton_t;

/* out = T*R*S from a unit quaternion; the rotation columns carry the scale and
 * column 3 the translation. Hand-written so no mat4 temporary is needed. */
static inline void nt_anim_mat34_from_trs(const nt_anim_trs_t *trs, nt_anim_mat34_t *out) {
    NT_ASSERT(trs != NULL);
    NT_ASSERT(out != NULL);

    const float x = trs->q[0];
    const float y = trs->q[1];
    const float z = trs->q[2];
    const float w = trs->q[3];

    const float xx = x * x;
    const float yy = y * y;
    const float zz = z * z;
    const float xy = x * y;
    const float xz = x * z;
    const float yz = y * z;
    const float wx = w * x;
    const float wy = w * y;
    const float wz = w * z;

    const float sx = trs->s[0];
    const float sy = trs->s[1];
    const float sz = trs->s[2];

    out->r[0][0] = (1.0F - (2.0F * (yy + zz))) * sx;
    out->r[0][1] = (2.0F * (xy - wz)) * sy;
    out->r[0][2] = (2.0F * (xz + wy)) * sz;
    out->r[0][3] = trs->t[0];

    out->r[1][0] = (2.0F * (xy + wz)) * sx;
    out->r[1][1] = (1.0F - (2.0F * (xx + zz))) * sy;
    out->r[1][2] = (2.0F * (yz - wx)) * sz;
    out->r[1][3] = trs->t[1];

    out->r[2][0] = (2.0F * (xz - wy)) * sx;
    out->r[2][1] = (2.0F * (yz + wx)) * sy;
    out->r[2][2] = (1.0F - (2.0F * (xx + yy))) * sz;
    out->r[2][3] = trs->t[2];
}

/* out = a*b, both read as affine 4x4 with the implicit row [0 0 0 1].
 * out must alias neither a nor b. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static inline void nt_anim_mat34_mul(const nt_anim_mat34_t *a, const nt_anim_mat34_t *b, nt_anim_mat34_t *out) {
    NT_ASSERT(a != NULL);
    NT_ASSERT(b != NULL);
    NT_ASSERT(out != NULL);
    NT_ASSERT(out != a && out != b);

    for (int i = 0; i < 3; ++i) {
        const float a0 = a->r[i][0];
        const float a1 = a->r[i][1];
        const float a2 = a->r[i][2];
        out->r[i][0] = (a0 * b->r[0][0]) + (a1 * b->r[1][0]) + (a2 * b->r[2][0]);
        out->r[i][1] = (a0 * b->r[0][1]) + (a1 * b->r[1][1]) + (a2 * b->r[2][1]);
        out->r[i][2] = (a0 * b->r[0][2]) + (a1 * b->r[1][2]) + (a2 * b->r[2][2]);
        out->r[i][3] = (a0 * b->r[0][3]) + (a1 * b->r[1][3]) + (a2 * b->r[2][3]) + a->r[i][3];
    }
}

/* out = the top three rows of a cglm column-major mat4 (m[col*4 + row]).
 * The plain float pointer is deliberate: pose buffers are never cast to mat4*. */
void nt_anim_mat34_from_mat4(const float m[16], nt_anim_mat34_t *out);

/* Copies the rest pose into the caller's local buffer of joint_count entries.
 * local is caller-owned and must not be skel->rest. */
void nt_anim_pose_rest(const nt_anim_skeleton_t *skel, nt_anim_trs_t *local);

/* Forward kinematics over [first, first + count): model[j] = model[parent[j]] *
 * mat34_from_trs(local[j]), roots take the local matrix unchanged.
 *
 * local and model are caller-owned, must not overlap, and model holds
 * joint_count entries even when the range is a subtree. A range that starts at
 * a root may span several roots; a range that starts inside a subtree must stay
 * inside it, so every in-range parent is either in the range or is
 * parent[first]. Precondition, not guarded because it is unverifiable: when
 * parent[first] != NT_ANIM_NO_PARENT, model[parent[first]] is already current. */
void nt_anim_fk(const nt_anim_skeleton_t *skel, const nt_anim_trs_t *local, nt_anim_mat34_t *model, uint16_t first, uint16_t count);

/* out = E * G[j] * socket_local, where E (world) is a cglm column-major mat4
 * mapping skeleton space to world. out must not alias g_joint. */
void nt_anim_socket(const float world[16], const nt_anim_mat34_t *g_joint, const nt_anim_trs_t *socket_local, nt_anim_mat34_t *out);

/* Bytes the rig identity hashes over: 8 header + 46 per joint. */
uint32_t nt_anim_rig_compat_id_size(uint16_t joint_count);

/* rig_compat_id of the skeleton: hash64 over the canonical little-endian byte
 * schema (tag "NRIG", schema version, convention id, joint count, then per
 * joint in index order the stable id, the parent index and the rest TRS as
 * canonical binary32). scratch is caller storage of at least
 * nt_anim_rig_compat_id_size(skel->joint_count) bytes (asserted); the function
 * writes the schema into it and retains no pointer. Activation/build time only,
 * never a frame operation. Ignores skel->rig_compat_id, which is the output
 * slot this value fills. */
nt_hash64_t nt_anim_rig_compat_id(const nt_anim_skeleton_t *skel, void *scratch, uint32_t scratch_size);

#endif /* NT_ANIM_H */
