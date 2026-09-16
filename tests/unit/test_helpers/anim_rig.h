#ifndef NT_TEST_HELPER_ANIM_RIG_H
#define NT_TEST_HELPER_ANIM_RIG_H

#include "anim/nt_anim.h"
#include "math/nt_math.h"

/* The helper distinguishes T*R*S from T*S*R; descendant rotations produce shear
 * in hand at rest, and arm and hand in bind. Two roots exercise independent FK
 * chains. */

#define ANIM_RIG_JOINT_COUNT 9

/* Two meshes over one rig: palette A = joints {0,1,3,4,5} (helper node 2 has no
 * palette entry), palette B = {6,7,8,4}; hand (joint 4) is shared. */
#define ANIM_RIG_PALETTE_A_COUNT 5
#define ANIM_RIG_PALETTE_B_COUNT 4

/* Mutable arrays belong to the instance. Keep its address stable: copying the
 * struct leaves its views pointing into the original instance. */
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

/* cglm computes T*R*S independently, reading pose fields directly to exercise
 * their alignment. Its API takes mutable pointers; this helper does not modify
 * the pose. */
void anim_rig_ref_mat4_from_trs(nt_anim_trs_t *trs, mat4 out);

/* Whole-rig FK over this rig's parents, as a mat4 chain. */
void anim_rig_ref_fk(nt_anim_trs_t *local, mat4 *out);

/* Unity assertion that the 3x4 matrix holds the top three rows of ref. */
void anim_rig_assert_mat34_equals_mat4(const nt_anim_mat34_t *m34, mat4 ref, float tol);

#endif /* NT_TEST_HELPER_ANIM_RIG_H */
