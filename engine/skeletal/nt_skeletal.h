#ifndef NT_SKELETAL_H
#define NT_SKELETAL_H

#include <stddef.h>
#include <stdint.h>

#include "core/nt_assert.h"
#include "hash/nt_hash.h"

/*
 * nt_skeletal — pose ABI, skeleton view, 3x4 affine kernels, FK, sockets, rig
 * identity, skin binding view and palette build.
 *
 * Column vectors: L = T*R*S, G[j] = G[parent[j]]*L[j], roots G = L.
 * Every kernel is void, allocates nothing and retains no pointer past the
 * call. Preconditions are NT_ASSERT contracts, never recoverable results.
 */

/* Per-element input validation (finite t/s, unit quaternion). On in Debug,
 * overridable from CMake (`NT_SKELETAL_CHECKS`). */
#ifndef NT_SKELETAL_CHECKS
#ifdef NT_DEBUG
#define NT_SKELETAL_CHECKS 1
#else
#define NT_SKELETAL_CHECKS 0
#endif
#endif

/* Checks are plain NT_ASSERTs: without asserts they would only warn as unused. */
#if NT_ASSERT_MODE == NT_ASSERT_OFF
#undef NT_SKELETAL_CHECKS
#define NT_SKELETAL_CHECKS 0
#endif

/* Local joint transform, AoS in joint order. Quaternion is unit xyzw: a
 * quaternion decoded from a lossy codec is renormalized by its decoder, because
 * the unit check tolerance assumes float32 inputs. */
typedef struct {
    float t[3];
    float q[4];
    float s[3];
} nt_skeletal_trs_t;

/* C++ spells these differently and GCC rejects the C keywords there; the ABI is
 * pinned by the C build every consumer shares. */
#ifndef __cplusplus
_Static_assert(sizeof(nt_skeletal_trs_t) == 40, "pose ABI: nt_skeletal_trs_t is 40 bytes");
_Static_assert(offsetof(nt_skeletal_trs_t, t) == 0, "pose ABI: t at offset 0");
_Static_assert(offsetof(nt_skeletal_trs_t, q) == 12, "pose ABI: q at offset 12");
_Static_assert(offsetof(nt_skeletal_trs_t, s) == 28, "pose ABI: s at offset 28");
_Static_assert(_Alignof(nt_skeletal_trs_t) == 4, "pose ABI: alignment 4");
#endif

/* Affine 3x4, column-vector convention, rows [m_r0 m_r1 m_r2 m_r3]:
 * r[i][3] is translation. Preserves the shear that hierarchical TRS produces. */
typedef struct {
    float r[3][4];
} nt_skeletal_mat34_t;

#ifndef __cplusplus
_Static_assert(sizeof(nt_skeletal_mat34_t) == 48, "pose ABI: nt_skeletal_mat34_t is 48 bytes");
_Static_assert(_Alignof(nt_skeletal_mat34_t) == 4, "pose ABI: mat34 alignment 4");
#endif

#define NT_SKELETAL_NO_PARENT UINT16_MAX

/* Immutable borrowed view of one rig. The owner is whoever built the arrays
 * (a skeleton activator or a test fixture); it keeps them alive and unchanged
 * until it republishes or destroys them. Kernels read the view for the
 * duration of the call and never store the pointer, so the caller frees
 * nothing here. Every array is non-NULL and holds joint_count entries. */
typedef struct {
    nt_hash64_t rig_compat_id;
    const uint16_t *parent;        /* NT_SKELETAL_NO_PARENT = root; parent[j] < j (preorder) */
    const uint16_t *subtree_end;   /* subtree of j = [j, subtree_end[j]) */
    const uint32_t *joint_id;      /* stable id, nt_hash32_str(node name) */
    const nt_skeletal_trs_t *rest; /* local rest pose */
    uint16_t joint_count;
} nt_skeletal_skeleton_t;

/* out = T*R*S from a unit quaternion; the rotation columns carry the scale and
 * column 3 the translation. NT_SKELETAL_CHECKS validates finite t/s and unit q. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static inline void nt_skeletal_mat34_from_trs(const nt_skeletal_trs_t *trs, nt_skeletal_mat34_t *out) {
    NT_ASSERT(trs != NULL);
    NT_ASSERT(out != NULL);

    const float x = trs->q[0];
    const float y = trs->q[1];
    const float z = trs->q[2];
    const float w = trs->q[3];

    const float xx = x * x;
    const float yy = y * y;
    const float zz = z * z;
#if NT_SKELETAL_CHECKS
    /* x - x rejects non-finite values without libm; requires strict IEEE math. */
    for (int c = 0; c < 3; ++c) {
        NT_ASSERT((trs->t[c] - trs->t[c]) == 0.0F);
        NT_ASSERT((trs->s[c] - trs->s[c]) == 0.0F);
    }
    const float dot = xx + yy + zz + (w * w);
    NT_ASSERT((dot - 1.0F) < 1e-3F && (1.0F - dot) < 1e-3F);
#endif
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
static inline void nt_skeletal_mat34_mul(const nt_skeletal_mat34_t *restrict a, const nt_skeletal_mat34_t *restrict b, nt_skeletal_mat34_t *restrict out) {
    NT_ASSERT(a != NULL);
    NT_ASSERT(b != NULL);
    NT_ASSERT(out != NULL);
/* Unlike the NULL checks, this one does not fold away: it runs per joint and
 * per palette entry, so it is a checked-build contract. */
#if NT_SKELETAL_CHECKS
    NT_ASSERT(out != a && out != b);
#endif

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
 * m and out must not overlap. The plain float pointer avoids casting pose
 * buffers to aligned mat4*. */
void nt_skeletal_mat34_from_mat4(const float m[16], nt_skeletal_mat34_t *out);

/* Forward kinematics over [first, first + count): model[j] = model[parent[j]] *
 * mat34_from_trs(local[j]), roots take the local matrix unchanged.
 * count >= 1 and first + count <= joint_count are required.
 *
 * local and model are caller-owned, must not overlap, and model holds
 * joint_count entries even when the range is a subtree. A range that starts at
 * a root may span several roots; a range that starts inside a subtree must stay
 * inside it, so every in-range parent is either in the range or is
 * parent[first]. Precondition, not guarded because it is unverifiable: when
 * parent[first] != NT_SKELETAL_NO_PARENT, model[parent[first]] is already current. */
void nt_skeletal_fk(const nt_skeletal_skeleton_t *skel, const nt_skeletal_trs_t *restrict local, nt_skeletal_mat34_t *restrict model, uint16_t first, uint16_t count);

/* out = E * G[j] * socket_local, where E (world) is a cglm column-major mat4
 * mapping skeleton space to world. out must not alias g_joint. */
void nt_skeletal_socket(const float world[16], const nt_skeletal_mat34_t *g_joint, const nt_skeletal_trs_t *socket_local, nt_skeletal_mat34_t *out);

// #region skin
/*
 * A binding describes how one mesh's vertices attach to a skeleton: a palette of
 * joints the vertices address by palette index, and one inverse bind matrix per
 * palette entry taking mesh space to that joint's space at the bind pose. Every
 * mesh exported from the same skin shares one binding, and a binding is only
 * valid with the skeleton whose rig_compat_id it carries.
 *
 * Model, inverse bind and palette matrices all use the nt_skeletal_mat34_t layout.
 */

/* Immutable borrowed view (same ownership contract as nt_skeletal_skeleton_t): the
 * owner built the arrays, keeps them alive and unchanged until it republishes
 * or destroys them, and kernels neither store nor free them. Both arrays are
 * non-NULL and hold palette_count entries. */
typedef struct {
    nt_hash64_t rig_compat_id;
    const uint16_t *remap;                   /* palette entry p -> skeleton joint */
    const nt_skeletal_mat34_t *inverse_bind; /* mesh space -> joint space at the bind pose, per palette entry */
    uint16_t palette_count;
} nt_skin_binding_t;

/* out[p] = model[remap[p]] * inverse_bind[p] for p in [0, palette_count).
 *
 * model is the caller's model-pose buffer of model_count joints and out the
 * caller's palette buffer of capacity entries; out must not overlap model.
 * Unconditional per-call contracts: palette_count <= capacity, and out does not
 * overlap model. Under NT_SKELETAL_CHECKS the per-element contract
 * remap[p] < model_count is asserted too: release builds trust remap because the
 * binding activator validates it before publishing a view.
 *
 * No rig-id argument: the game asserts binding/skeleton compatibility once when
 * it pairs them, not on every frame. */
void nt_skin_palette_build(const nt_skin_binding_t *binding, const nt_skeletal_mat34_t *restrict model, uint16_t model_count, nt_skeletal_mat34_t *restrict out, uint16_t capacity);
// #endregion

/* Bytes the rig identity hashes over: 8 header + 46 per joint. */
#define NT_SKELETAL_RIG_ID_BYTES(joint_count) (8U + (46U * (uint32_t)(joint_count)))

/* rig_compat_id of the skeleton: hash64 over the canonical little-endian byte
 * schema (tag "NRIG", schema version, convention id, joint count, then per
 * joint in index order the stable id, the parent index and the rest TRS as
 * canonical binary32). scratch is caller storage of at least
 * NT_SKELETAL_RIG_ID_BYTES(skel->joint_count) bytes (asserted); the function writes
 * the schema into it and retains no pointer. 46 B per joint is 3 MB at
 * UINT16_MAX joints, so the scratch belongs on the heap or in a sized pool,
 * never on the WASM stack or in the frame scratch arena. Activation/build time
 * only, never a frame operation. Ignores skel->rig_compat_id, which is the
 * output slot this value fills. */
nt_hash64_t nt_skeletal_rig_compat_id(const nt_skeletal_skeleton_t *skel, void *scratch, uint32_t scratch_size);

#endif /* NT_SKELETAL_H */
