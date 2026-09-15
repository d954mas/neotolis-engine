#ifndef NT_SKIN_H
#define NT_SKIN_H

#include <stdint.h>

#include "anim/nt_anim.h"
#include "hash/nt_hash.h"

/*
 * nt_skin — skin binding view and palette build.
 *
 * A binding (spec 3.4) describes how one mesh's vertices attach to a skeleton:
 * a palette of joints the vertices address by palette index, and one inverse
 * bind matrix per palette entry taking mesh space to that joint's space at the
 * bind pose. Every mesh exported from the same skin shares one binding, and a
 * binding is only valid with the skeleton whose rig_compat_id it carries.
 *
 * Model, inverse bind and palette matrices all use the nt_anim_mat34_t row
 * layout: affine 3x4, column vectors, rows [m_r0 m_r1 m_r2 m_r3].
 */

/* Immutable borrowed view (same ownership contract as nt_anim_skeleton_t): the
 * owner built the arrays, keeps them alive and unchanged until it republishes
 * or destroys them, and kernels neither store nor free them. Both arrays are
 * non-NULL and hold palette_count entries. */
typedef struct {
    nt_hash64_t rig_compat_id;
    const uint16_t *remap;               /* palette entry p -> skeleton joint */
    const nt_anim_mat34_t *inverse_bind; /* mesh space -> joint space at the bind pose, per palette entry */
    uint16_t palette_count;
} nt_skin_binding_t;

/* out[p] = model[remap[p]] * inverse_bind[p] for p in [0, palette_count).
 *
 * model is the caller's model-pose buffer of model_count joints and out the
 * caller's palette buffer of capacity entries; out must not overlap model.
 * Unconditional per-call contracts: palette_count <= capacity, and out does not
 * overlap model. Under NT_ANIM_CHECKS the per-element contract
 * remap[p] < model_count is asserted too. The skin binding activator rejects
 * remap[p] >= joint_count before publishing a view, so release builds trust the
 * data; until that activator exists, hand-built bindings are unvalidated.
 *
 * No rig-id argument: the game asserts binding/skeleton compatibility once when
 * it pairs them, not on every frame. */
void nt_skin_palette_build(const nt_skin_binding_t *binding, const nt_anim_mat34_t *model, uint16_t model_count, nt_anim_mat34_t *out, uint16_t capacity);

#endif /* NT_SKIN_H */
