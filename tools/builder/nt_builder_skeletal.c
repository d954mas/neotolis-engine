/* clang-format off */
#include "nt_builder_internal.h"
#include "nt_skeletal_format.h"
/* clang-format on */

/*
 * Encoders from in-memory import results to the NSKL / NSKN / NANM payloads.
 * The wire layout is the runtime layout, so headers go out as packed structs
 * and the arrays after them are memcpy'd whole.
 */

/* Every field of these payloads is little-endian and every array is copied as
 * bytes, so a big-endian builder would silently ship swapped packs. */
#if defined(__BYTE_ORDER__)
_Static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "the skeletal encoders write little-endian payloads by memcpy");
#endif

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
    return (n - 1.0F) < 1e-3F && (1.0F - n) < 1e-3F;
}
// #endregion

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
        NT_BUILD_ASSERT(skel_finite_n(skel->rest[j].t, 3) && skel_finite_n(skel->rest[j].s, 3) && "rest translation or scale is not finite");
        NT_BUILD_ASSERT(skel_unit_quat(skel->rest[j].q) && "rest rotation is not a unit quaternion");
        top = j;
    }

    /* Joint ids are how clips and retarget maps name joints, so two joints
     * sharing one id would make a rig that cannot be addressed. */
    for (uint32_t j = 1; j < joint_count; j++) {
        for (uint32_t k = 0; k < j; k++) {
            NT_BUILD_ASSERT(skel->joint_id[j] != skel->joint_id[k] && "two joints share one joint_id");
        }
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
    for (uint32_t p = 0; p < palette_count; p++) {
        NT_BUILD_ASSERT(skel_finite_n(&binding->inverse_bind[p].r[0][0], 12) && "inverse bind matrix is not finite");
    }
    /* Both radii bound a sphere; a NaN or a negative one would cull the
     * character away instead of drawing it. */
    NT_BUILD_ASSERT(skel_finite(binding->reach) && binding->reach >= 0.0F && "binding reach must be finite and non-negative");
    NT_BUILD_ASSERT(skel_finite(binding->any_pose_radius) && binding->any_pose_radius >= 0.0F && "binding any_pose_radius must be finite and non-negative");

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
/* Per-kind element counts collected in the validating pass; object channels are
 * counted only in n_keys, because their modes live in the header, their
 * constants and key ranges in the object record and their samples in their own
 * array, not in the joint tables. */
typedef struct {
    uint16_t sampled[3]; /* joint rows per component kind: t, q, s */
    uint16_t constant[3];
    uint32_t steps;
    uint64_t keys; /* summed in 64 bits, narrowed once the total is known to fit the wire field */
} NtClipTally;

/* Components a channel stores: 4 for a rotation, 3 for a translation or scale. */
static uint32_t clip_comps(uint32_t channel) { return (channel % 3U == 1U) ? 4U : 3U; }

/* Component kind 0/1/2 of one TRS: translation, rotation, scale. */
static float *clip_trs_component(nt_skeletal_trs_t *trs, uint32_t kind) {
    if (kind == 0U) {
        return trs->t;
    }
    return (kind == 1U) ? trs->q : trs->s;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_BUILD_ASSERT expansions dominate the count
static void clip_validate(const nt_builder_clip_t *clip, NtClipTally *tally) {
    NT_BUILD_ASSERT(clip->channels && "clip has no channel array");
    NT_BUILD_ASSERT(clip->joint_count >= 1 && "clip has no joints");
    NT_BUILD_ASSERT(clip->sample_count >= 1 && "clip needs at least one sample");
    NT_BUILD_ASSERT(skel_finite(clip->duration) && clip->duration >= 0.0F && "duration must be finite and non-negative");

    const uint32_t object_first = 3U * (uint32_t)clip->joint_count;
    const uint32_t channel_count = object_first + 3U;
    for (uint32_t c = 0; c < channel_count; c++) {
        const nt_builder_anim_channel_t *ch = &clip->channels[c];
        const uint32_t comps = clip_comps(c);
        const uint32_t kind = c % 3U;
        const bool object = c >= object_first;
        switch (ch->mode) {
        case NT_SKELETAL_CHANNEL_ABSENT:
            break;
        case NT_SKELETAL_CHANNEL_CONSTANT:
            NT_BUILD_ASSERT(skel_finite_n(ch->constant, comps) && "constant channel value is not finite");
            if (comps == 4) {
                NT_BUILD_ASSERT(skel_unit_quat(ch->constant) && "constant rotation is not a unit quaternion");
            }
            if (!object) {
                tally->constant[kind]++;
            }
            break;
        case NT_SKELETAL_CHANNEL_SAMPLED:
            NT_BUILD_ASSERT(ch->samples && "sampled channel has no samples");
            NT_BUILD_ASSERT(clip->sample_count >= 2 && clip->duration > 0.0F && "a sampled channel needs at least two samples over a positive duration");
            for (uint32_t s = 0; s < clip->sample_count; s++) {
                const float *v = &ch->samples[(size_t)s * comps];
                NT_BUILD_ASSERT(skel_finite_n(v, comps) && "sample is not finite");
                if (comps == 4) {
                    NT_BUILD_ASSERT(skel_unit_quat(v) && "sampled rotation is not a unit quaternion");
                }
            }
            if (!object) {
                tally->sampled[kind]++;
            }
            break;
        case NT_SKELETAL_CHANNEL_STEP: {
            NT_BUILD_ASSERT(ch->step_times && ch->step_values && ch->step_count >= 1 && "step channel has no keys");
            NT_BUILD_ASSERT(ch->step_times[0] >= 0.0F && "the first step key precedes the clip");
            for (uint32_t k = 0; k < ch->step_count; k++) {
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
            if (!object) {
                tally->steps++;
            }
            tally->keys += ch->step_count;
            break;
        }
        default:
            NT_BUILD_ASSERT(0 && "unknown clip channel mode");
            break;
        }
    }
}

static void clip_fill_header(const nt_builder_clip_t *clip, const NtClipTally *tally, NtAnmHeader *header) {
    memset(header, 0, sizeof(*header));
    header->magic = NT_ANM_MAGIC;
    header->version = NT_SKELETAL_FORMAT_VERSION;
    header->joint_count = clip->joint_count;
    header->sample_count = clip->sample_count;
    header->duration = clip->duration;
    header->rig_compat_id = clip->rig_compat_id.value;
    header->additive_ref_id = clip->additive_ref_id.value;
    header->n_t = tally->sampled[0];
    header->n_q = tally->sampled[1];
    header->n_s = tally->sampled[2];
    header->n_ct = tally->constant[0];
    header->n_cq = tally->constant[1];
    header->n_cs = tally->constant[2];
    header->n_steps = tally->steps;
    NT_BUILD_ASSERT(tally->keys <= UINT32_MAX && "clip step keys exceed the u32 wire field");
    header->n_keys = (uint32_t)tally->keys;

    const uint32_t object_first = 3U * (uint32_t)clip->joint_count;
    for (uint32_t kind = 0; kind < 3; kind++) {
        header->object_mode[kind] = clip->channels[object_first + kind].mode;
    }
}

/* The object curve's values and key ranges. The key partition is one ascending
 * walk over the channels, so the joint tracks come first and the object STEP
 * channels follow in t, q, s order. */
static void clip_fill_object(const nt_builder_clip_t *clip, NtAnmObject *object) {
    memset(object, 0, sizeof(*object));

    const uint32_t object_first = 3U * (uint32_t)clip->joint_count;
    const uint32_t constant_offset[3] = {0U, 3U, 7U};
    uint32_t first_key = 0;
    for (uint32_t c = 0; c < object_first + 3U; c++) {
        const nt_builder_anim_channel_t *ch = &clip->channels[c];
        if (c >= object_first) {
            const uint32_t kind = c - object_first;
            if (ch->mode == NT_SKELETAL_CHANNEL_CONSTANT) {
                memcpy(&object->constant[constant_offset[kind]], ch->constant, clip_comps(c) * sizeof(float));
            } else if (ch->mode == NT_SKELETAL_CHANNEL_STEP) {
                object->step_first[kind] = first_key;
                object->step_count[kind] = ch->step_count;
            }
        }
        if (ch->mode == NT_SKELETAL_CHANNEL_STEP) {
            first_key += ch->step_count;
        }
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- one linear pass per wire array
void nt_builder_encode_clip(const nt_builder_clip_t *clip, uint8_t **out, uint32_t *out_size) {
    NT_BUILD_ASSERT(clip && out && out_size && "invalid encode_clip args");

    NtClipTally tally = {{0, 0, 0}, {0, 0, 0}, 0, 0};
    clip_validate(clip, &tally);

    NtAnmHeader header;
    clip_fill_header(clip, &tally, &header);

    const uint64_t size64 = nt_anm_size(&header);
    NT_BUILD_ASSERT(size64 <= UINT32_MAX && "clip payload exceeds 4 GB");
    const uint32_t size = (uint32_t)size64;
    uint8_t *payload = (uint8_t *)malloc(size);
    NT_BUILD_ASSERT(payload && "encode_clip: alloc failed (OOM)");
    /* The payload hash is the clip's dedup key, so every pad byte and every
     * slot no channel drives has to be deterministic. */
    memset(payload, 0, size);

    const uint32_t object_first = 3U * (uint32_t)clip->joint_count;
    uint8_t *w = payload;
    memcpy(w, &header, sizeof(header));
    w += sizeof(header);

    // #region frame blocks
    /* Sample-major: one interpolated sample reads two adjacent blocks, so the
     * channel-major import arrays are transposed here, once, offline. */
    for (uint32_t i = 0; i < clip->sample_count; i++) {
        for (uint32_t kind = 0; kind < 3; kind++) {
            const uint32_t comps = clip_comps(kind);
            for (uint32_t c = kind; c < object_first; c += 3U) {
                if (clip->channels[c].mode != NT_SKELETAL_CHANNEL_SAMPLED) {
                    continue;
                }
                memcpy(w, &clip->channels[c].samples[(size_t)i * comps], comps * sizeof(float));
                w += comps * sizeof(float);
            }
        }
    }
    // #endregion

    // #region constants, steps and keys
    for (uint32_t kind = 0; kind < 3; kind++) {
        const uint32_t comps = clip_comps(kind);
        for (uint32_t c = kind; c < object_first; c += 3U) {
            if (clip->channels[c].mode != NT_SKELETAL_CHANNEL_CONSTANT) {
                continue;
            }
            memcpy(w, clip->channels[c].constant, comps * sizeof(float));
            w += comps * sizeof(float);
        }
    }
    uint32_t first_key = 0;
    for (uint32_t c = 0; c < object_first; c++) {
        if (clip->channels[c].mode != NT_SKELETAL_CHANNEL_STEP) {
            continue;
        }
        const nt_skeletal_step_t step = {
            .first = first_key,
            .count = clip->channels[c].step_count,
            .joint = (uint16_t)(c / 3U),
            .channel = (uint8_t)(c % 3U),
            .pad = 0,
        };
        memcpy(w, &step, sizeof(step));
        w += sizeof(step);
        first_key += clip->channels[c].step_count;
    }
    for (uint32_t c = 0; c < object_first + 3U; c++) {
        const nt_builder_anim_channel_t *ch = &clip->channels[c];
        if (ch->mode != NT_SKELETAL_CHANNEL_STEP) {
            continue;
        }
        for (uint32_t k = 0; k < ch->step_count; k++) {
            nt_skeletal_step_key_t key;
            key.time = ch->step_times[k];
            memcpy(key.v, &ch->step_values[(size_t)k * 4U], sizeof(key.v));
            memcpy(w, &key, sizeof(key));
            w += sizeof(key);
        }
    }
    // #endregion

    // #region object record, samples and joint tables
    if (nt_anm_has_object(&header)) {
        NtAnmObject object;
        clip_fill_object(clip, &object);
        memcpy(w, &object, sizeof(object));
        w += sizeof(object);
    }
    if (nt_anm_object_sampled(&header)) {
        for (uint32_t i = 0; i < clip->sample_count; i++) {
            nt_skeletal_trs_t sample;
            memset(&sample, 0, sizeof(sample));
            for (uint32_t kind = 0; kind < 3; kind++) {
                const nt_builder_anim_channel_t *ch = &clip->channels[object_first + kind];
                if (ch->mode != NT_SKELETAL_CHANNEL_SAMPLED) {
                    continue;
                }
                const uint32_t comps = clip_comps(kind);
                memcpy(clip_trs_component(&sample, kind), &ch->samples[(size_t)i * comps], comps * sizeof(float));
            }
            memcpy(w, &sample, sizeof(sample));
            w += sizeof(sample);
        }
    }
    const uint8_t table_mode[2] = {NT_SKELETAL_CHANNEL_SAMPLED, NT_SKELETAL_CHANNEL_CONSTANT};
    for (uint32_t t = 0; t < 2; t++) {
        for (uint32_t kind = 0; kind < 3; kind++) {
            for (uint32_t c = kind; c < object_first; c += 3U) {
                if (clip->channels[c].mode != table_mode[t]) {
                    continue;
                }
                const uint16_t joint = (uint16_t)(c / 3U);
                memcpy(w, &joint, sizeof(joint));
                w += sizeof(joint);
            }
        }
    }
    // #endregion

    NT_BUILD_ASSERT((uint32_t)(w - payload) == size && "encode_clip wrote a different number of bytes than its header declares");
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
