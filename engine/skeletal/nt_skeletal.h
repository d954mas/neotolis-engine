#ifndef NT_SKELETAL_H
#define NT_SKELETAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/nt_assert.h"
#include "hash/nt_hash.h"

/*
 * nt_skeletal — pose ABI, skeleton view, 3x4 affine kernels, FK, rig identity,
 * skin binding view and palette build, clip view and sampler, and the track
 * clock.
 *
 * Column vectors: L = T*R*S, G[j] = G[parent[j]]*L[j], roots G = L.
 * Every kernel is void, allocates nothing and retains no pointer past the
 * call. Preconditions are NT_ASSERT contracts, never recoverable results.
 */

#ifndef NT_SKELETAL_CHECKS
#error "NT_SKELETAL_CHECKS must be defined by the nt_skeletal target (0 or 1)"
#endif

/* x - x rejects NaN and infinity without libm; requires strict IEEE math. Double
 * so one inline serves float and double callers. */
static inline bool nt_skeletal_finite(double v) { return (v - v) == 0.0; }

/* Local joint transform, AoS in joint order. Quaternion is unit xyzw; the
 * kernels' unit check assumes float32 inputs. */
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
    for (int c = 0; c < 3; ++c) {
        NT_ASSERT(nt_skeletal_finite((double)trs->t[c]));
        NT_ASSERT(nt_skeletal_finite((double)trs->s[c]));
    }
    NT_ASSERT((xx + yy + zz + (w * w) - 1.0F) < 1e-3F && (1.0F - (xx + yy + zz + (w * w))) < 1e-3F);
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

// #region skin
/*
 * A binding describes how one mesh's vertices attach to a skeleton: a palette of
 * joints the vertices address by palette index, and one inverse bind matrix per
 * palette entry taking mesh space to that joint's space at the bind pose. Mesh
 * space is the primitive's vertex space; the skinned mesh node's transform is
 * ignored, which is the glTF rule for a skinned primitive. Every
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
    float reach;                             /* joint space: the farthest a bound vertex sits from its joint; a skeleton-space radius needs the chain stretch (skeletal spec, Bounds and culling) */
    float any_pose_radius;                   /* skeleton space: sphere for any joint rotation over the rest chain (skeletal spec, Bounds and culling) */
    uint16_t palette_count;
} nt_skin_binding_t;

/* out[p] = model[remap[p]] * inverse_bind[p] for p in [0, palette_count).
 *
 * model is the caller's model-pose buffer of model_count joints and out the
 * caller's palette buffer of capacity entries; out must not overlap model.
 * NT_ASSERT checks palette_count <= capacity, non-overlapping out/model, and
 * remap[p] < model_count independently of NT_SKELETAL_CHECKS.
 *
 * No rig-id argument: the game asserts binding/skeleton compatibility once when
 * it pairs them, not on every frame. */
void nt_skin_palette_build(const nt_skin_binding_t *binding, const nt_skeletal_mat34_t *restrict model, uint16_t model_count, nt_skeletal_mat34_t *restrict out, uint16_t capacity);
// #endregion

// #region clip
/*
 * A clip is an immutable borrowed view with the same ownership contract as
 * nt_skeletal_skeleton_t: the owner (a clip activator or a test fixture) built
 * the arrays, keeps them alive and unchanged until it republishes or destroys
 * them, and the kernels neither store nor free them. The NANM payload
 * (shared/include/nt_skeletal_format.h) holds exactly these tables, so an
 * activator copies the payload and points this view into the copy through
 * nt_skeletal_clip_view.
 *
 * base is the whole local pose the clip starts from: the rig's rest with every
 * constant channel written in. Sampled rows live in sample_count frame blocks
 * of 3*n_t + 4*n_q + 3*n_s floats each, block i at blocks + i * stride and laid
 * out as t rows [n_t][3], then q rows [n_q][4], then s rows [n_s][3]; row k
 * overwrites the component of joint t_joint[k] / q_joint[k] / s_joint[k]. One
 * interpolated sample therefore reads two adjacent blocks and nothing else,
 * instead of striding through the clip once per channel. blocks is NULL when
 * no channel is sampled; a builder invariant, not checked at activation, is
 * that no joint component has two rows.
 */
typedef struct {
    nt_hash64_t rig_compat_id;     /* rig this clip plays on */
    double duration;               /* seconds, >= 0; > 0 when any row exists */
    const nt_skeletal_trs_t *base; /* joint_count entries */
    const float *blocks;           /* sample_count frame blocks, see above */
    const uint16_t *t_joint;       /* joint of sampled t row k */
    const uint16_t *q_joint;       /* joint of sampled q row k */
    const uint16_t *s_joint;       /* joint of sampled s row k */
    /* The builder's three clip bounds (skeletal spec, Bounds and culling). */
    float r_joints;
    float r_root;
    float s_max;
    uint32_t sample_count;  /* samples on the uniform grid over [0, duration], >= 1; >= 2 when any row exists */
    uint16_t joint_count;   /* joints the clip and its poses address */
    uint16_t n_t, n_q, n_s; /* sampled rows per component kind */
} nt_skeletal_clip_t;

/* out[0, clip->joint_count) = the clip's local pose at time: a copy of base
 * with every sampled row interpolated on top. time is in [0, duration]
 * seconds; random seek and reverse need no cursor because the grid index is
 * computed, not stepped.
 *
 * Sampled T/S lerp and sampled Q take the shortest-path normalized lerp, and a
 * grid time reproduces its stored block exactly. out is a caller-owned buffer
 * of joint_count entries that must not overlap base. */
void nt_skeletal_sample(const nt_skeletal_clip_t *clip, double time, nt_skeletal_trs_t *out);

/* Points *out at the tables of a NANM payload in place: no validation, no
 * allocation, no copy. The payload must be 4-aligned (asserted), stay alive as
 * long as the view and hold exactly nt_anm_size(header) bytes; the table
 * entries are not range-checked here. The builder measures its own encoder
 * output through the same function. */
void nt_skeletal_clip_view(const uint8_t *payload, nt_skeletal_clip_t *out);
// #endregion

// #region composition
/*
 * Stateless kernels over local poses of joint_count entries; the game chains
 * them in any order. Per-joint factors are plain float arrays of joint_count
 * entries or NULL for 1 everywhere: mix reads them as weights (>= 0, no upper
 * bound), override as a mask (in [0, 1]). The ranges of factors, gains and
 * alpha are asserted independently of NT_SKELETAL_CHECKS, which
 * adds finite weights, finite T/S and unit quaternions of the poses the kernel
 * blends.
 */

/* One input of nt_skeletal_mix. Its influence on joint j is
 * gain * weights[j]; a zero influence reads nothing from the pose. */
typedef struct {
    const nt_skeletal_trs_t *pose; /* joint_count entries */
    const float *weights;          /* joint_count entries >= 0, or NULL for 1 */
    float gain;                    /* 0 or in [2^-60, 2^60] */
} nt_skeletal_mix_input_t;

/* Normalized N-way mix. Per joint with influences w_t and W = sum w_t: W == 0
 * copies defaults[j]; otherwise T/S = sum (w_t / W) * x_t and the rotation is
 * the normalized sum of w_t * q_t, each q_t sign-aligned against the running
 * sum in supplied order (an exactly orthogonal one, the first included, takes
 * the sign that makes w positive, or for w == 0 its largest component). q and
 * -q give the same result on every input. There is no threshold: any
 * influence above zero counts in full. The result depends on input order for
 * widely separated rotations, so the order is part of the call's meaning.
 *
 * A fade ends by setting the gain to 0, not by letting it decay. A non-zero
 * influence is a normal float, and the T/S sums stay in float range.
 * input_count may be 0 (inputs may then be NULL); out overlaps neither
 * defaults nor any input pose. */
void nt_skeletal_mix(const nt_skeletal_mix_input_t *inputs, uint32_t input_count, const nt_skeletal_trs_t *defaults, uint16_t joint_count, nt_skeletal_trs_t *restrict out);

/* out[j] = base[j] blended toward top[j] by a = alpha * mask[j]: T/S lerp, Q
 * shortest-path normalized lerp. a == 0 copies base and a == 1 copies top bit
 * for bit, so a joint the mask leaves out keeps base exactly. alpha is in
 * [0, 1]. out may be base itself; any other overlap of out with base or top is
 * a precondition violation. */
void nt_skeletal_override(const nt_skeletal_trs_t *base, const nt_skeletal_trs_t *top, const float *mask, float alpha, uint16_t joint_count, nt_skeletal_trs_t *out);
// #endregion

// #region tracks
/* Playback state of one clip assignment, owned by the game in a fixed-capacity
 * array. The engine has no player object: assign, release and crossfade ramps
 * are field writes in game code, and this struct holds no clip, pose, resource
 * or GPU pointer; which clip a track plays and how much it weighs are the
 * game's own records. duration is fixed for the assignment and is the clip's,
 * so the clock needs no clip view. */
typedef struct {
    double time;     /* seconds in [0, duration] */
    double duration; /* seconds, >= 0; 0 is a static pose */
    float speed;     /* time scale, may be negative; 0 pauses */
    uint32_t flags;  /* NT_SKELETAL_TRACK_* */
} nt_skeletal_track_t;

/* Slot is in use. Occupancy is not gain: a gain-0 track keeps its cycle
 * synchronized, and only the game releases a slot. */
#define NT_SKELETAL_TRACK_OCCUPIED (1U << 0)
/* Time wraps into [0, duration) instead of clamping to [0, duration]. */
#define NT_SKELETAL_TRACK_LOOPING (1U << 1)

/* Advances the clock of every occupied track by speed * dt and nothing else:
 * no callback, no event, no clip access. A finite dt >= 0 is asserted, and for
 * every occupied track a duration >= 0, a finite speed and a cycle count inside
 * the int64 range. */
void nt_skeletal_tracks_advance(nt_skeletal_track_t *tracks, uint32_t count, double dt);
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
