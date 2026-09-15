#ifndef NT_TEST_HELPER_ANIM_RIG_H
#define NT_TEST_HELPER_ANIM_RIG_H

#include "anim/nt_anim.h"
#include "math/nt_math.h"

/* Asymmetric 9-joint procedural rig in preorder: two roots, a helper node with
 * no palette entry, and nonuniform scale that shears everything below it. The
 * helper both rotates 90 deg about Z and scales nonuniformly, so its local
 * matrix distinguishes T*R*S from T*S*R and its shear reaches arm and hand.
 *
 *   0 root_a
 *   1  +- spine          (nonuniform scale)
 *   2  |   +- helper     (90 deg Z rotation + nonuniform scale)
 *   3  |   |   +- arm
 *   4  |   |       +- hand
 *   5  |   +- leg
 *   6 root_b
 *   7  +- tail1          (uniform 0.5 scale)
 *   8      +- tail2
 */

#define ANIM_RIG_JOINT_COUNT 9

/* Two meshes over one rig: palette A = joints {0,1,3,4,5} (helper node 2 has no
 * palette entry), palette B = {6,7,8,4}; hand (joint 4) is shared. */
#define ANIM_RIG_PALETTE_A_COUNT 5
#define ANIM_RIG_PALETTE_B_COUNT 4

/* Every mutable array lives in the instance, so two rigs in one process never
 * share storage. The views point into this struct, so a copy would dangle. */
typedef struct {
    nt_anim_skeleton_t skel;                  /* view over this instance's arrays */
    nt_anim_trs_t bind[ANIM_RIG_JOINT_COUNT]; /* bind-pose locals, deliberately != rest */
    uint32_t joint_id[ANIM_RIG_JOINT_COUNT];
    nt_anim_trs_t rest[ANIM_RIG_JOINT_COUNT];
    nt_anim_mat34_t inverse_bind_a[ANIM_RIG_PALETTE_A_COUNT];
    nt_anim_mat34_t inverse_bind_b[ANIM_RIG_PALETTE_B_COUNT];
    uint8_t rig_scratch[NT_ANIM_RIG_ID_BYTES(ANIM_RIG_JOINT_COUNT)];
} anim_rig_t;

/* Fills the view (including rig_compat_id) and the bind pose. The caller owns
 * the whole struct and frees nothing. */
void anim_rig_asymmetric(anim_rig_t *out);

/* Fills the two bindings of the rig: inverse_bind[p] = inverse(G_bind[remap[p]])
 * over the bind pose, through a cglm mat4 FK chain as the independent reference
 * path. Both carry the rig's rig_compat_id and point into rig. */
void anim_rig_bindings(anim_rig_t *rig, nt_skin_binding_t *a, nt_skin_binding_t *b);

/* Replaces o's rotation with the axis-angle one; t and s are untouched. */
void anim_rig_set_axis_angle(nt_anim_trs_t *o, float ax, float ay, float az, float degrees);

/* cglm reference path, shared by every animation test so the kernels are never
 * verified against themselves. The pose pointers are non-const because cglm
 * takes vec3/versor by mutable pointer; nothing here writes through them.
 *
 * out = T*R*S of one joint, reading t/q/s straight out of the AoS pose as
 * vec3/versor: the ABI-alignment exercise that Debug UBSan checks. */
void anim_rig_ref_mat4_from_trs(nt_anim_trs_t *trs, mat4 out);

/* Whole-rig FK over this rig's parents, as a mat4 chain. */
void anim_rig_ref_fk(nt_anim_trs_t *local, mat4 *out);

/* Unity assertion that the 3x4 matrix holds the top three rows of ref. */
void anim_rig_assert_mat34_equals_mat4(const nt_anim_mat34_t *m34, mat4 ref, float tol);

#endif /* NT_TEST_HELPER_ANIM_RIG_H */
