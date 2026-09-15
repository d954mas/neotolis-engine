#include "anim/nt_anim.h"

#include <string.h>

#if NT_ANIM_CHECKS
#include <stdbool.h>

/* x - x is 0 only for a finite x; keeps the module free of <math.h> and libm. */
static bool nt_anim_is_finite(float x) { return (x - x) == 0.0F; }

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

#if NT_ANIM_CHECKS
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
