/* clang-format off */
#include "nt_builder_internal.h"
#include "nt_skeletal_format.h"
/* clang-format on */

/*
 * Encoders from in-memory import results to the NSKL / NSKN / NANM wire
 * payloads. Fields go out as explicit little-endian stores, so the bytes do not
 * depend on the host the builder runs on and nothing casts a struct onto the
 * payload.
 */

// #region little-endian stores
static uint8_t *skel_wr_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    return p + 2;
}

static uint8_t *skel_wr_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
    return p + 4;
}

static uint8_t *skel_wr_u64(uint8_t *p, uint64_t v) { return skel_wr_u32(skel_wr_u32(p, (uint32_t)(v & 0xFFFFFFFFU)), (uint32_t)(v >> 32)); }

static uint8_t *skel_wr_f32(uint8_t *p, float v) {
    uint32_t bits = 0;
    memcpy(&bits, &v, sizeof(bits));
    return skel_wr_u32(p, bits);
}
// #endregion

// #region value checks
/* x - x rejects NaN and infinities without libm; requires strict IEEE math. */
static bool skel_finite(float v) { return (v - v) == 0.0F; }

static bool skel_finite_n(const float *v, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        if (!skel_finite(v[i])) {
            return false;
        }
    }
    return true;
}

/* Same tolerance as nt_skeletal_mat34_from_trs: the decoded pose must satisfy
 * the kernels' unit-quaternion contract. */
static bool skel_unit_quat(const float *q) {
    const float n = (q[0] * q[0]) + (q[1] * q[1]) + (q[2] * q[2]) + (q[3] * q[3]);
    return skel_finite(n) && (n - 1.0F) < 1e-3F && (1.0F - n) < 1e-3F;
}
// #endregion

// #region NSKL skeleton
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_BUILD_ASSERT expansions dominate the count
void nt_builder_encode_skeleton(const nt_skeletal_skeleton_t *skel, uint8_t **out, uint32_t *out_size) {
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
        NT_BUILD_ASSERT(skel_finite_n(skel->rest[j].t, 3) && skel_finite_n(skel->rest[j].s, 3) && "rest translation or scale is not finite");
        NT_BUILD_ASSERT(skel_unit_quat(skel->rest[j].q) && "rest rotation is not a unit quaternion");
        top = j;
    }

    /* The id travels as content identity and is never recomputed downstream, so
     * this is the only place a rig edited after its id was taken is caught. */
    {
        const uint32_t scratch_size = NT_SKELETAL_RIG_ID_BYTES(joint_count);
        void *scratch = malloc(scratch_size);
        NT_BUILD_ASSERT(scratch && "encode_skeleton: alloc failed (OOM)");
        const nt_hash64_t computed = nt_skeletal_rig_compat_id(skel, scratch, scratch_size);
        free(scratch);
        NT_BUILD_ASSERT(computed.value == skel->rig_compat_id.value && "rig_compat_id does not match its own joints");
    }

    const uint32_t size = NT_SKL_SIZE(joint_count);
    uint8_t *payload = (uint8_t *)malloc(size);
    NT_BUILD_ASSERT(payload && "encode_skeleton: alloc failed (OOM)");

    uint8_t *w = payload;
    w = skel_wr_u32(w, NT_SKL_MAGIC);
    w = skel_wr_u16(w, NT_SKELETAL_FORMAT_VERSION);
    w = skel_wr_u16(w, (uint16_t)joint_count);
    w = skel_wr_u64(w, skel->rig_compat_id.value);
    for (uint32_t j = 0; j < joint_count; j++) {
        w = skel_wr_u16(w, skel->parent[j]);
    }
    for (uint32_t j = 0; j < joint_count; j++) {
        w = skel_wr_u16(w, skel->subtree_end[j]);
    }
    for (uint32_t j = 0; j < joint_count; j++) {
        w = skel_wr_u32(w, skel->joint_id[j]);
    }
    for (uint32_t j = 0; j < joint_count; j++) {
        for (uint32_t c = 0; c < 3; c++) {
            w = skel_wr_f32(w, skel->rest[j].t[c]);
        }
        for (uint32_t c = 0; c < 4; c++) {
            w = skel_wr_f32(w, skel->rest[j].q[c]);
        }
        for (uint32_t c = 0; c < 3; c++) {
            w = skel_wr_f32(w, skel->rest[j].s[c]);
        }
    }
    NT_BUILD_ASSERT((uint32_t)(w - payload) == size && "encode_skeleton wrote a different number of bytes than NT_SKL_SIZE");

    *out = payload;
    *out_size = size;
}

void nt_builder_add_skeleton(NtBuilderContext *ctx, const nt_skeletal_skeleton_t *skel, const char *resource_id) {
    NT_BUILD_ASSERT(ctx && resource_id && "invalid add_skeleton args");
    uint8_t *payload = NULL;
    uint32_t size = 0;
    nt_builder_encode_skeleton(skel, &payload, &size);
    uint64_t hash = nt_hash64(payload, size).value;
    nt_builder_add_entry(ctx, resource_id, NT_BUILD_ASSET_SKELETON, NULL, payload, size, hash);
}
// #endregion

// #region NSKN skin binding
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_BUILD_ASSERT expansions dominate the count
void nt_builder_encode_skin_binding(const nt_skin_binding_t *binding, uint8_t **out, uint32_t *out_size) {
    NT_BUILD_ASSERT(binding && out && out_size && "invalid encode_skin_binding args");
    NT_BUILD_ASSERT(binding->remap && binding->inverse_bind && "binding view has a NULL array");
    NT_BUILD_ASSERT(binding->palette_count >= 1 && "binding has no palette entries");
    NT_BUILD_ASSERT(skel_finite(binding->reach) && binding->reach >= 0.0F && "reach must be finite and non-negative");
    NT_BUILD_ASSERT(skel_finite(binding->any_pose_radius) && binding->any_pose_radius >= 0.0F && "any_pose_radius must be finite and non-negative");

    const uint32_t palette_count = binding->palette_count;
    for (uint32_t p = 0; p < palette_count; p++) {
        NT_BUILD_ASSERT(skel_finite_n(&binding->inverse_bind[p].r[0][0], 12) && "inverse bind matrix is not finite");
    }

    const uint32_t size = NT_SKN_SIZE(palette_count);
    uint8_t *payload = (uint8_t *)malloc(size);
    NT_BUILD_ASSERT(payload && "encode_skin_binding: alloc failed (OOM)");

    uint8_t *w = payload;
    w = skel_wr_u32(w, NT_SKN_MAGIC);
    w = skel_wr_u16(w, NT_SKELETAL_FORMAT_VERSION);
    w = skel_wr_u16(w, (uint16_t)palette_count);
    w = skel_wr_u64(w, binding->rig_compat_id.value);
    *w++ = NT_SKN_MESH_SPACE_GLTF_NODE;
    *w++ = 0;
    *w++ = 0;
    *w++ = 0;
    w = skel_wr_f32(w, binding->reach);
    w = skel_wr_f32(w, binding->any_pose_radius);
    for (uint32_t p = 0; p < palette_count; p++) {
        w = skel_wr_u16(w, binding->remap[p]);
    }
    for (uint32_t p = 0; p < palette_count; p++) {
        for (uint32_t row = 0; row < 3; row++) {
            for (uint32_t col = 0; col < 4; col++) {
                w = skel_wr_f32(w, binding->inverse_bind[p].r[row][col]);
            }
        }
    }
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
/* Per-kind element counts collected in the validating pass. */
typedef struct {
    uint32_t sampled_rows[3]; /* rows per component kind: t, q, s */
    uint32_t constants;
    uint32_t step_tracks;
    uint32_t step_keys;
} NtClipTally;

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_BUILD_ASSERT expansions dominate the count
static void clip_validate(const nt_builder_clip_t *clip, NtClipTally *tally) {
    NT_BUILD_ASSERT(clip->channels && "clip has no channel array");
    NT_BUILD_ASSERT(clip->joint_count >= 1 && "clip has no joints");
    NT_BUILD_ASSERT(clip->sample_count >= 1 && "clip needs at least one sample");
    NT_BUILD_ASSERT(skel_finite(clip->duration) && clip->duration >= 0.0F && "duration must be finite and non-negative");
    NT_BUILD_ASSERT(clip->kind <= NT_ANM_KIND_ADDITIVE && "unknown clip kind");
    NT_BUILD_ASSERT((clip->kind == NT_ANM_KIND_ABSOLUTE) == (clip->additive_ref_id == 0) && "additive_ref_id is set exactly for an additive clip");
    NT_BUILD_ASSERT(skel_finite(clip->r_joints) && clip->r_joints >= 0.0F && skel_finite(clip->r_root) && clip->r_root >= 0.0F && skel_finite(clip->s_max) && clip->s_max >= 0.0F &&
                    "clip bounds must be finite and non-negative");
    NT_BUILD_ASSERT(skel_finite(clip->bake_fps_min) && clip->bake_fps_min >= 0.0F && skel_finite(clip->bake_reach) && clip->bake_reach >= 0.0F && "bake certificate must be finite and non-negative");

    const uint32_t channel_count = NT_ANM_CHANNEL_COUNT(clip->joint_count);
    for (uint32_t c = 0; c < channel_count; c++) {
        const nt_builder_anim_channel_t *ch = &clip->channels[c];
        const uint32_t comps = nt_anm_channel_comps(c);
        switch (ch->mode) {
        case NT_ANM_CHANNEL_ABSENT:
            break;
        case NT_ANM_CHANNEL_CONSTANT:
            NT_BUILD_ASSERT(skel_finite_n(ch->constant, comps) && "constant channel value is not finite");
            if (comps == 4) {
                NT_BUILD_ASSERT(skel_unit_quat(ch->constant) && "constant rotation is not a unit quaternion");
            } else {
                NT_BUILD_ASSERT(ch->constant[3] == 0.0F && "constant translation or scale must leave the fourth component at 0");
            }
            tally->constants++;
            break;
        case NT_ANM_CHANNEL_SAMPLED:
            NT_BUILD_ASSERT(ch->samples && "sampled channel has no samples");
            NT_BUILD_ASSERT(clip->sample_count >= 2 && clip->duration > 0.0F && "a sampled channel needs at least two samples over a positive duration");
            for (uint32_t s = 0; s < clip->sample_count; s++) {
                const float *v = &ch->samples[(size_t)s * comps];
                NT_BUILD_ASSERT(skel_finite_n(v, comps) && "sample is not finite");
                if (comps == 4) {
                    NT_BUILD_ASSERT(skel_unit_quat(v) && "sampled rotation is not a unit quaternion");
                }
            }
            tally->sampled_rows[c % 3U]++;
            break;
        case NT_ANM_CHANNEL_STEP: {
            NT_BUILD_ASSERT(ch->step_times && ch->step_values && ch->step_count >= 1 && "step channel has no keys");
            NT_BUILD_ASSERT(ch->step_times[0] == 0.0F && "the first step key must sit at time 0");
            for (uint32_t k = 0; k < ch->step_count; k++) {
                NT_BUILD_ASSERT(skel_finite(ch->step_times[k]) && "step time is not finite");
                NT_BUILD_ASSERT((k == 0 || ch->step_times[k] > ch->step_times[k - 1]) && "step times must increase strictly");
                const float *v = &ch->step_values[(size_t)k * 4U];
                NT_BUILD_ASSERT(skel_finite_n(v, 4) && "step value is not finite");
                if (comps == 4) {
                    NT_BUILD_ASSERT(skel_unit_quat(v) && "step rotation is not a unit quaternion");
                } else {
                    NT_BUILD_ASSERT(v[3] == 0.0F && "a step translation or scale must leave the fourth component at 0");
                }
            }
            NT_BUILD_ASSERT(ch->step_times[ch->step_count - 1] <= clip->duration && "the last step key lies past the clip duration");
            tally->step_tracks++;
            tally->step_keys += ch->step_count;
            break;
        }
        default:
            NT_BUILD_ASSERT(0 && "unknown clip channel mode");
            break;
        }
    }
}

/* Fill one section descriptor and advance the running payload offset. */
static uint32_t clip_section(uint8_t **w, uint32_t tag, uint32_t offset, uint32_t count, uint32_t stride) {
    const uint64_t bytes = (uint64_t)count * (uint64_t)stride;
    NT_BUILD_ASSERT((uint64_t)offset + bytes <= UINT32_MAX && "clip payload exceeds 4 GB");
    *w = skel_wr_u32(*w, tag);
    *w = skel_wr_u32(*w, offset);
    *w = skel_wr_u32(*w, count);
    *w = skel_wr_u32(*w, stride);
    return offset + (uint32_t)bytes;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- one linear pass per wire section
void nt_builder_encode_clip(const nt_builder_clip_t *clip, uint8_t **out, uint32_t *out_size) {
    NT_BUILD_ASSERT(clip && out && out_size && "invalid encode_clip args");

    NtClipTally tally = {{0, 0, 0}, 0, 0, 0};
    clip_validate(clip, &tally);

    const uint32_t channel_count = NT_ANM_CHANNEL_COUNT(clip->joint_count);
    /* The stride is a u32 wire field, so the product is formed in 64 bits and
     * bounded before it is narrowed. */
    uint32_t plane_stride[3] = {0, 0, 0};
    for (uint32_t k = 0; k < 3; k++) {
        const uint64_t stride = (uint64_t)clip->sample_count * ((k == 1U) ? 16U : 12U);
        NT_BUILD_ASSERT(stride <= UINT32_MAX && "clip plane stride exceeds 4 GB");
        plane_stride[k] = (uint32_t)stride;
    }

    // #region header and section table
    /* Every stride is a multiple of 4 and the table ends 4-aligned, so each
     * section offset below inherits the alignment the format requires. */
    const uint32_t table_end = (uint32_t)sizeof(NtAnmHeader) + ((uint32_t)NT_ANM_SECTION_COUNT * (uint32_t)sizeof(NtAnmSection));
    uint8_t *payload = NULL;
    uint32_t size = 0;
    {
        uint64_t total = table_end;
        total += (uint64_t)channel_count * sizeof(NtAnmChannel);
        for (uint32_t k = 0; k < 3; k++) {
            total += (uint64_t)tally.sampled_rows[k] * plane_stride[k];
        }
        total += (uint64_t)tally.constants * 16U;
        total += (uint64_t)tally.step_tracks * sizeof(NtAnmStepTrack);
        total += (uint64_t)tally.step_keys * sizeof(NtAnmStepKey);
        NT_BUILD_ASSERT(total <= UINT32_MAX && "clip payload exceeds 4 GB");
        size = (uint32_t)total;
        payload = (uint8_t *)malloc(size);
        NT_BUILD_ASSERT(payload && "encode_clip: alloc failed (OOM)");
    }

    uint8_t *w = payload;
    w = skel_wr_u32(w, NT_ANM_MAGIC);
    w = skel_wr_u16(w, NT_SKELETAL_FORMAT_VERSION);
    w = skel_wr_u16(w, NT_ANM_SECTION_COUNT);
    w = skel_wr_u16(w, clip->joint_count);
    *w++ = clip->kind;
    *w++ = NT_ANM_CODEC_F32;
    w = skel_wr_u64(w, clip->rig_compat_id);
    w = skel_wr_u64(w, clip->additive_ref_id);
    w = skel_wr_f32(w, clip->duration);
    w = skel_wr_u32(w, clip->sample_count);
    w = skel_wr_f32(w, clip->r_joints);
    w = skel_wr_f32(w, clip->r_root);
    w = skel_wr_f32(w, clip->s_max);
    w = skel_wr_f32(w, clip->bake_fps_min);
    w = skel_wr_f32(w, clip->bake_reach);

    uint32_t offset = table_end;
    offset = clip_section(&w, NT_ANM_TAG_CHAN, offset, channel_count, (uint32_t)sizeof(NtAnmChannel));
    offset = clip_section(&w, NT_ANM_TAG_PLNT, offset, tally.sampled_rows[0], plane_stride[0]);
    offset = clip_section(&w, NT_ANM_TAG_PLNQ, offset, tally.sampled_rows[1], plane_stride[1]);
    offset = clip_section(&w, NT_ANM_TAG_PLNS, offset, tally.sampled_rows[2], plane_stride[2]);
    offset = clip_section(&w, NT_ANM_TAG_CNST, offset, tally.constants, 16U);
    offset = clip_section(&w, NT_ANM_TAG_STPT, offset, tally.step_tracks, (uint32_t)sizeof(NtAnmStepTrack));
    offset = clip_section(&w, NT_ANM_TAG_STPK, offset, tally.step_keys, (uint32_t)sizeof(NtAnmStepKey));
    NT_BUILD_ASSERT(offset == size && "clip section table does not cover the payload exactly");
    // #endregion

    // #region CHAN
    {
        uint32_t sampled[3] = {0, 0, 0};
        uint32_t constants = 0;
        uint32_t steps = 0;
        for (uint32_t c = 0; c < channel_count; c++) {
            const uint8_t mode = clip->channels[c].mode;
            uint32_t index = 0;
            if (mode == NT_ANM_CHANNEL_CONSTANT) {
                index = constants++;
            } else if (mode == NT_ANM_CHANNEL_SAMPLED) {
                index = sampled[c % 3U]++;
            } else if (mode == NT_ANM_CHANNEL_STEP) {
                index = steps++;
            }
            NT_BUILD_ASSERT(index <= UINT16_MAX && "clip channel index does not fit the wire field");
            *w++ = mode;
            *w++ = 0;
            w = skel_wr_u16(w, (uint16_t)index);
        }
    }
    // #endregion

    // #region planes, constants and step tables
    /* Rows follow channel order inside each plane, which is what the SAMPLED
     * index means, so one pass per component kind emits them in order. */
    for (uint32_t kind = 0; kind < 3; kind++) {
        const uint32_t comps = nt_anm_channel_comps(kind);
        for (uint32_t c = kind; c < channel_count; c += 3U) {
            if (clip->channels[c].mode != NT_ANM_CHANNEL_SAMPLED) {
                continue;
            }
            const uint32_t floats = clip->sample_count * comps;
            for (uint32_t i = 0; i < floats; i++) {
                w = skel_wr_f32(w, clip->channels[c].samples[i]);
            }
        }
    }
    for (uint32_t c = 0; c < channel_count; c++) {
        if (clip->channels[c].mode == NT_ANM_CHANNEL_CONSTANT) {
            for (uint32_t i = 0; i < 4; i++) {
                w = skel_wr_f32(w, clip->channels[c].constant[i]);
            }
        }
    }
    {
        uint32_t first_key = 0;
        for (uint32_t c = 0; c < channel_count; c++) {
            if (clip->channels[c].mode != NT_ANM_CHANNEL_STEP) {
                continue;
            }
            w = skel_wr_u32(w, first_key);
            w = skel_wr_u32(w, clip->channels[c].step_count);
            first_key += clip->channels[c].step_count;
        }
    }
    for (uint32_t c = 0; c < channel_count; c++) {
        if (clip->channels[c].mode != NT_ANM_CHANNEL_STEP) {
            continue;
        }
        const nt_builder_anim_channel_t *ch = &clip->channels[c];
        for (uint32_t k = 0; k < ch->step_count; k++) {
            w = skel_wr_f32(w, ch->step_times[k]);
            for (uint32_t i = 0; i < 4; i++) {
                w = skel_wr_f32(w, ch->step_values[((size_t)k * 4U) + i]);
            }
        }
    }
    // #endregion

    NT_BUILD_ASSERT((uint32_t)(w - payload) == size && "encode_clip wrote a different number of bytes than its section table declares");
    *out = payload;
    *out_size = size;
}

void nt_builder_add_clip(NtBuilderContext *ctx, const nt_builder_clip_t *clip, const char *resource_id) {
    NT_BUILD_ASSERT(ctx && resource_id && "invalid add_clip args");
    uint8_t *payload = NULL;
    uint32_t size = 0;
    nt_builder_encode_clip(clip, &payload, &size);
    uint64_t hash = nt_hash64(payload, size).value;
    nt_builder_add_entry(ctx, resource_id, NT_BUILD_ASSET_CLIP, NULL, payload, size, hash);
}
// #endregion
