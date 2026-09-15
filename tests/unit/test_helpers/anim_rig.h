#ifndef NT_TEST_HELPER_ANIM_RIG_H
#define NT_TEST_HELPER_ANIM_RIG_H

#include "anim/nt_anim.h"
#include "anim/nt_skin.h"

/* Asymmetric 9-joint procedural rig in preorder: two roots, a helper node with
 * no palette entry, and nonuniform scale that shears everything below it.
 *
 *   0 root_a
 *   1  +- spine          (nonuniform scale)
 *   2  |   +- helper
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

typedef struct {
    nt_anim_skeleton_t skel;                  /* view over the helper's static arrays */
    nt_anim_trs_t bind[ANIM_RIG_JOINT_COUNT]; /* bind-pose locals, deliberately != rest */
} anim_rig_t;

/* Fills the view (including rig_compat_id) and the bind pose. The arrays behind
 * the view are static and live for the whole process; the caller frees
 * nothing. */
void anim_rig_asymmetric(anim_rig_t *out);

/* Fills the two bindings of the rig: inverse_bind[p] = inverse(G_bind[remap[p]])
 * over the bind pose, through cglm as the reference path. Both carry the rig's
 * rig_compat_id. The palette arrays are static like the view's. */
void anim_rig_bindings(const anim_rig_t *rig, nt_skin_binding_t *a, nt_skin_binding_t *b);

#endif /* NT_TEST_HELPER_ANIM_RIG_H */
