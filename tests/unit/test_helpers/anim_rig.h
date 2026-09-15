#ifndef NT_TEST_HELPER_ANIM_RIG_H
#define NT_TEST_HELPER_ANIM_RIG_H

#include "anim/nt_anim.h"

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

typedef struct {
    nt_anim_skeleton_t skel;                  /* view over the helper's static arrays */
    nt_anim_trs_t bind[ANIM_RIG_JOINT_COUNT]; /* bind-pose locals, deliberately != rest */
} anim_rig_t;

/* Fills the view and the bind pose. The arrays behind the view are static and
 * live for the whole process; the caller frees nothing. */
void anim_rig_asymmetric(anim_rig_t *out);

#endif /* NT_TEST_HELPER_ANIM_RIG_H */
