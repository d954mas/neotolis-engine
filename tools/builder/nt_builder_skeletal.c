/* clang-format off */
#include "nt_builder_internal.h"
#include "nt_skeletal_format.h"
/* clang-format on */

/*
 * Encoders from the runtime views to the NSKL / NSKN / NANM payloads. The wire
 * layout is the runtime layout, so headers go out as packed structs and the
 * arrays after them are memcpy'd whole. The encoders assert structure only;
 * values are the importer's contract.
 */

/* Every field of these payloads is little-endian and every array is copied as
 * bytes, so a big-endian builder would silently ship swapped packs. */
#if defined(__BYTE_ORDER__)
_Static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "the skeletal encoders write little-endian payloads by memcpy");
#endif

// #region NSKL skeleton
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_BUILD_ASSERT expansions dominate the count
nt_hash64_t nt_builder_encode_skeleton(const nt_skeletal_skeleton_t *skel, uint8_t **out, uint32_t *out_size) {
    NT_BUILD_ASSERT(skel && out && out_size && "invalid encode_skeleton args");
    NT_BUILD_ASSERT(skel->parent && skel->subtree_end && skel->joint_id && skel->rest && "skeleton view has a NULL array");
    NT_BUILD_ASSERT(skel->joint_count >= 1 && "skeleton has no joints");

    /* Exact preorder, mirroring the activator: the parent of joint j is the
     * nearest earlier joint whose subtree range is still open at j, which
     * carries preorder, sibling contiguity, root chaining and the last root
     * closing at joint_count. */
    const uint32_t joint_count = skel->joint_count;
    uint32_t top = NT_SKELETAL_NO_PARENT;
    for (uint32_t j = 0; j < joint_count; j++) {
        NT_BUILD_ASSERT(j < skel->subtree_end[j] && skel->subtree_end[j] <= joint_count && "subtree_end outside [j+1, joint_count]");
        while (top != NT_SKELETAL_NO_PARENT && skel->subtree_end[top] <= j) {
            top = skel->parent[top];
        }
        NT_BUILD_ASSERT((uint32_t)skel->parent[j] == top && "a joint's parent must be the innermost joint whose subtree range is still open");
        NT_BUILD_ASSERT((skel->parent[j] == NT_SKELETAL_NO_PARENT || skel->subtree_end[j] <= skel->subtree_end[skel->parent[j]]) && "child lies outside its parent's subtree range");
        top = j;
    }

    /* The identity is computed here and returned, so what ships and what the
     * caller stamps on clips and bindings are the same number by construction. */
    nt_hash64_t rig_compat_id = {0};
    {
        const uint32_t scratch_size = NT_SKELETAL_RIG_ID_BYTES(joint_count);
        void *scratch = malloc(scratch_size);
        NT_BUILD_ASSERT(scratch && "encode_skeleton: alloc failed (OOM)");
        rig_compat_id = nt_skeletal_rig_compat_id(skel, scratch, scratch_size);
        free(scratch);
    }

    const uint32_t size = (uint32_t)NT_SKL_SIZE(joint_count); /* fits: joint_count is u16 */
    uint8_t *payload = (uint8_t *)malloc(size);
    NT_BUILD_ASSERT(payload && "encode_skeleton: alloc failed (OOM)");

    const NtSklHeader header = {
        .magic = NT_SKL_MAGIC,
        .version = NT_SKELETAL_FORMAT_VERSION,
        .joint_count = (uint16_t)joint_count,
        .rig_compat_id = rig_compat_id.value,
    };
    uint8_t *w = payload;
    memcpy(w, &header, sizeof(header));
    w += sizeof(header);
    memcpy(w, skel->parent, (size_t)2U * joint_count);
    w += (size_t)2U * joint_count;
    memcpy(w, skel->subtree_end, (size_t)2U * joint_count);
    w += (size_t)2U * joint_count;
    memcpy(w, skel->joint_id, (size_t)4U * joint_count);
    w += (size_t)4U * joint_count;
    memcpy(w, skel->rest, (size_t)sizeof(nt_skeletal_trs_t) * joint_count);
    w += (size_t)sizeof(nt_skeletal_trs_t) * joint_count;
    NT_BUILD_ASSERT((uint32_t)(w - payload) == size && "encode_skeleton wrote a different number of bytes than NT_SKL_SIZE");

    *out = payload;
    *out_size = size;
    return rig_compat_id;
}

nt_hash64_t nt_builder_add_skeleton(NtBuilderContext *ctx, const nt_skeletal_skeleton_t *skel, const char *resource_id) {
    NT_BUILD_ASSERT(ctx && resource_id && "invalid add_skeleton args");
    uint8_t *payload = NULL;
    uint32_t size = 0;
    const nt_hash64_t rig_compat_id = nt_builder_encode_skeleton(skel, &payload, &size);
    uint64_t hash = nt_hash64(payload, size).value;
    nt_builder_add_entry(ctx, resource_id, NT_BUILD_ASSET_SKELETON, NULL, payload, size, hash);
    return rig_compat_id;
}
// #endregion

// #region NSKN skin binding
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_BUILD_ASSERT expansions dominate the count
void nt_builder_encode_skin_binding(const nt_skin_binding_t *binding, uint8_t **out, uint32_t *out_size) {
    NT_BUILD_ASSERT(binding && out && out_size && "invalid encode_skin_binding args");
    NT_BUILD_ASSERT(binding->remap && binding->inverse_bind && "binding view has a NULL array");
    NT_BUILD_ASSERT(binding->palette_count >= 1 && "binding has no palette entries");

    const uint32_t palette_count = binding->palette_count;
    /* Both radii bound a sphere; a NaN or a negative one would cull the
     * character away instead of drawing it. */
    NT_BUILD_ASSERT(nt_builder_finite(binding->reach) && binding->reach >= 0.0F && "binding reach must be finite and non-negative");
    NT_BUILD_ASSERT(nt_builder_finite(binding->any_pose_radius) && binding->any_pose_radius >= 0.0F && "binding any_pose_radius must be finite and non-negative");

    const uint32_t size = (uint32_t)NT_SKN_SIZE(palette_count); /* fits: palette_count is u16 */
    uint8_t *payload = (uint8_t *)malloc(size);
    NT_BUILD_ASSERT(payload && "encode_skin_binding: alloc failed (OOM)");

    const NtSknHeader header = {
        .magic = NT_SKN_MAGIC,
        .version = NT_SKELETAL_FORMAT_VERSION,
        .palette_count = (uint16_t)palette_count,
        .rig_compat_id = binding->rig_compat_id.value,
        .reach = binding->reach,
        .any_pose_radius = binding->any_pose_radius,
    };
    uint8_t *w = payload;
    memcpy(w, &header, sizeof(header));
    w += sizeof(header);
    memcpy(w, binding->inverse_bind, (size_t)sizeof(nt_skeletal_mat34_t) * palette_count);
    w += (size_t)sizeof(nt_skeletal_mat34_t) * palette_count;
    memcpy(w, binding->remap, (size_t)2U * palette_count);
    w += (size_t)2U * palette_count;
    NT_BUILD_ASSERT((uint32_t)(w - payload) == size && "encode_skin_binding wrote a different number of bytes than NT_SKN_SIZE");

    *out = payload;
    *out_size = size;
}

void nt_builder_add_skin_binding(NtBuilderContext *ctx, const nt_skin_binding_t *binding, const char *resource_id) {
    NT_BUILD_ASSERT(ctx && resource_id && "invalid add_skin_binding args");
    uint8_t *payload = NULL;
    uint32_t size = 0;
    nt_builder_encode_skin_binding(binding, &payload, &size);
    uint64_t hash = nt_hash64(payload, size).value;
    nt_builder_add_entry(ctx, resource_id, NT_BUILD_ASSET_SKIN_BINDING, NULL, payload, size, hash);
}
// #endregion

// #region NANM clip
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_BUILD_ASSERT expansions dominate the count
void nt_builder_encode_clip(const nt_skeletal_clip_t *clip, uint8_t **out, uint32_t *out_size) {
    NT_BUILD_ASSERT(clip && out && out_size && "invalid encode_clip args");
    NT_BUILD_ASSERT(clip->base && "clip has no base pose");
    NT_BUILD_ASSERT(clip->joint_count >= 1 && "clip has no joints");
    NT_BUILD_ASSERT(clip->sample_count >= 1 && "clip needs at least one sample");
    /* The header stores a float; a duration the float cannot hold would move
     * the grid the samples were taken on. */
    const float duration = (float)clip->duration;
    NT_BUILD_ASSERT(nt_builder_finite(duration) && duration >= 0.0F && (double)duration == clip->duration && "duration must be finite, non-negative and exactly representable as float");
    NT_BUILD_ASSERT(nt_builder_finite(clip->r_joints) && clip->r_joints >= 0.0F && nt_builder_finite(clip->r_root) && clip->r_root >= 0.0F && nt_builder_finite(clip->s_max) && clip->s_max >= 0.0F &&
                    "clip bounds must be finite and non-negative");
    NT_BUILD_ASSERT((clip->n_t == 0 || clip->t_joint) && (clip->n_q == 0 || clip->q_joint) && (clip->n_s == 0 || clip->s_joint) && "a joint table is NULL while its row count is not");

    const uint32_t joint_count = clip->joint_count;
    const size_t stride = ((size_t)3U * clip->n_t) + ((size_t)4U * clip->n_q) + ((size_t)3U * clip->n_s);
    NT_BUILD_ASSERT((stride == 0 || (clip->blocks && clip->sample_count >= 2 && clip->duration > 0.0)) && "sampled rows need frame blocks, at least two samples and a positive duration");

    const NtAnmHeader header = {
        .magic = NT_ANM_MAGIC,
        .version = NT_SKELETAL_FORMAT_VERSION,
        .joint_count = (uint16_t)joint_count,
        .sample_count = clip->sample_count,
        .duration = duration,
        .rig_compat_id = clip->rig_compat_id.value,
        .r_joints = clip->r_joints,
        .r_root = clip->r_root,
        .s_max = clip->s_max,
        .n_t = clip->n_t,
        .n_q = clip->n_q,
        .n_s = clip->n_s,
        ._pad = 0,
    };
    const uint64_t size64 = nt_anm_size(&header);
    NT_BUILD_ASSERT(size64 <= UINT32_MAX && "clip payload exceeds 4 GB");
    const uint32_t size = (uint32_t)size64;
    uint8_t *payload = (uint8_t *)malloc(size);
    NT_BUILD_ASSERT(payload && "encode_clip: alloc failed (OOM)");

    /* Every byte is written below, so the payload hash is the clip's identity. */
    uint8_t *w = payload;
    memcpy(w, &header, sizeof(header));
    w += sizeof(header);
    memcpy(w, clip->base, (size_t)joint_count * sizeof(nt_skeletal_trs_t));
    w += (size_t)joint_count * sizeof(nt_skeletal_trs_t);
    if (stride != 0) {
        memcpy(w, clip->blocks, (size_t)clip->sample_count * stride * sizeof(float));
        w += (size_t)clip->sample_count * stride * sizeof(float);
    }
    if (clip->n_t != 0) {
        memcpy(w, clip->t_joint, (size_t)clip->n_t * sizeof(uint16_t));
        w += (size_t)clip->n_t * sizeof(uint16_t);
    }
    if (clip->n_q != 0) {
        memcpy(w, clip->q_joint, (size_t)clip->n_q * sizeof(uint16_t));
        w += (size_t)clip->n_q * sizeof(uint16_t);
    }
    if (clip->n_s != 0) {
        memcpy(w, clip->s_joint, (size_t)clip->n_s * sizeof(uint16_t));
        w += (size_t)clip->n_s * sizeof(uint16_t);
    }
    NT_BUILD_ASSERT((uint32_t)(w - payload) == size && "encode_clip wrote a different number of bytes than its header declares");

    *out = payload;
    *out_size = size;
}

void nt_builder_add_clip(NtBuilderContext *ctx, const nt_skeletal_clip_t *clip, const char *resource_id) {
    NT_BUILD_ASSERT(ctx && resource_id && "invalid add_clip args");
    uint8_t *payload = NULL;
    uint32_t size = 0;
    nt_builder_encode_clip(clip, &payload, &size);
    uint64_t hash = nt_hash64(payload, size).value;
    nt_builder_add_entry(ctx, resource_id, NT_BUILD_ASSET_CLIP, NULL, payload, size, hash);
}
// #endregion
