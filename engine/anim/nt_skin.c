#include "anim/nt_skin.h"

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_skin_palette_build(const nt_skin_binding_t *binding, const nt_anim_mat34_t *model, uint16_t model_count, nt_anim_mat34_t *out, uint16_t capacity) {
    NT_ASSERT(binding != NULL);
    NT_ASSERT(binding->remap != NULL);
    NT_ASSERT(binding->inverse_bind != NULL);
    NT_ASSERT(model != NULL);
    NT_ASSERT(out != NULL);
    NT_ASSERT(binding->palette_count <= capacity);

/* NT_ASSERT_OFF does not evaluate its expression, so a check loop left behind
 * there would only warn as dead code. */
#if NT_ANIM_CHECKS && (NT_ASSERT_MODE != NT_ASSERT_OFF)
    for (uint16_t p = 0; p < binding->palette_count; ++p) {
        NT_ASSERT(binding->remap[p] < model_count);
    }
#endif

    for (uint16_t p = 0; p < binding->palette_count; ++p) {
        nt_anim_mat34_mul(&model[binding->remap[p]], &binding->inverse_bind[p], &out[p]);
    }
}
