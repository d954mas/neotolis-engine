#include "skeletal_assets/nt_skeletal_assets.h"

#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"
#include "log/nt_log.h"
#include "nt_pack_format.h"
#include "nt_skeletal_format.h"
#include "pool/nt_pool.h"

/* This is the one translation unit that sees both the wire strides and the
 * runtime structs they hold, so it is where the two are pinned together. */
_Static_assert(sizeof(nt_skeletal_step_t) == NT_ANM_STEP_STRIDE, "nt_skeletal_step_t must match the NANM step stride");
_Static_assert(sizeof(nt_skeletal_step_key_t) == NT_ANM_KEY_STRIDE, "nt_skeletal_step_key_t must match the NANM key stride");
_Static_assert(sizeof(nt_skeletal_trs_t) == 40, "the object sampled array is one nt_skeletal_trs_t per sample");
/* nt_anm_object_sampled spells the SAMPLED mode as the byte it travels as. */
_Static_assert(NT_SKELETAL_CHANNEL_SAMPLED == 2, "wire and runtime SAMPLED must agree");

// #region module state
/* One pool for all three asset types: the resource layer already types the
 * handle, so a slot only has to hold its allocation and its view. */
typedef struct {
    void *mem;
    union {
        nt_skeletal_skeleton_t skeleton;
        nt_skin_binding_t binding;
        nt_skeletal_clip_t clip;
    } view;
} nt_skeletal_slot_t;

static struct {
    nt_pool_t pool;
    nt_skeletal_slot_t *slots; /* [capacity + 1], index 0 reserved by nt_pool */
    bool initialized;
} s_assets;
// #endregion

// #region payload readers
/* Payload bytes are little-endian and may sit at any offset inside a pack blob,
 * so every read goes through memcpy. */
static uint16_t rd_u16(const uint8_t *p) {
    uint16_t v = 0;
    memcpy(&v, p, sizeof(v));
    return v;
}

static uint32_t rd_u32(const uint8_t *p) {
    uint32_t v = 0;
    memcpy(&v, p, sizeof(v));
    return v;
}

/* x - x rejects non-finite values without libm; requires strict IEEE math. */
static bool skel_finite(float v) { return (v - v) == 0.0F; }
// #endregion

// #region slots
static uint32_t skel_take_slot(const uint8_t *data, uint32_t size) {
    const uint32_t id = nt_pool_alloc(&s_assets.pool);
    NT_ASSERT(id != 0 && "skeletal asset pool exhausted -- raise nt_skeletal_assets_init(max_assets)");
    if (id == 0) {
        return 0;
    }
    nt_skeletal_slot_t *slot = &s_assets.slots[nt_pool_slot_index(id)];
    slot->mem = malloc(size);
    NT_ASSERT(slot->mem != NULL);
    memcpy(slot->mem, data, size);
    return id;
}

static void skel_release_slot(uint32_t id) {
    NT_ASSERT(s_assets.initialized && "nt_skeletal_assets_shutdown ran before a skeletal deactivator");
    NT_ASSERT(nt_pool_valid(&s_assets.pool, id) && "skeletal handle is not live");
    nt_skeletal_slot_t *slot = &s_assets.slots[nt_pool_slot_index(id)];
    free(slot->mem);
    memset(slot, 0, sizeof(*slot));
    nt_pool_free(&s_assets.pool, id);
}

/* Returns void so a caller may cast straight to the array type it reads; every
 * offset of a validated payload is aligned for the type that starts there. */
static const void *skel_at(uint32_t id, uint64_t offset) { return (const uint8_t *)s_assets.slots[nt_pool_slot_index(id)].mem + offset; }
// #endregion

// #region NSKL skeleton
static bool skl_validate(const uint8_t *data, uint32_t size, NtSklHeader *out) {
    if (data == NULL || size < sizeof(NtSklHeader)) {
        NT_LOG_WARN("activate_skeleton: payload shorter than the NSKL header");
        return false;
    }
    memcpy(out, data, sizeof(*out));
    if (out->magic != NT_SKL_MAGIC) {
        NT_LOG_WARN("activate_skeleton: bad magic 0x%08X", out->magic);
        return false;
    }
    if (out->version != NT_SKELETAL_FORMAT_VERSION) {
        NT_LOG_WARN("activate_skeleton: NSKL version %u is not %u -- rebuild packs", (unsigned)out->version, (unsigned)NT_SKELETAL_FORMAT_VERSION);
        return false;
    }
    if (out->joint_count == 0) {
        NT_LOG_WARN("activate_skeleton: joint_count 0");
        return false;
    }
    if ((uint64_t)size != NT_SKL_SIZE(out->joint_count)) {
        NT_LOG_WARN("activate_skeleton: size %u is not the %u bytes %u joints need", size, (unsigned)NT_SKL_SIZE(out->joint_count), (unsigned)out->joint_count);
        return false;
    }

    /* Exact preorder: the parent of joint j must be the nearest earlier joint
     * whose subtree range is still open at j. That single rule carries preorder
     * parents, sibling contiguity, root chaining and the last root closing at
     * joint_count, and it is what makes FK's parent-before-child read safe.
     * Each subtree is popped once, so the walk is O(J) amortized. */
    const uint8_t *parents = data + sizeof(NtSklHeader);
    const uint8_t *ends = parents + ((size_t)2U * out->joint_count);
    uint32_t top = NT_SKELETAL_NO_PARENT;
    for (uint32_t j = 0; j < out->joint_count; ++j) {
        const uint16_t parent = rd_u16(parents + ((size_t)2U * j));
        const uint16_t end = rd_u16(ends + ((size_t)2U * j));
        if ((uint32_t)end <= j || (uint32_t)end > out->joint_count) {
            NT_LOG_WARN("activate_skeleton: joint %u has subtree_end %u outside (%u, %u]", j, (unsigned)end, j, (unsigned)out->joint_count);
            return false;
        }
        while (top != NT_SKELETAL_NO_PARENT && (uint32_t)rd_u16(ends + ((size_t)2U * top)) <= j) {
            top = rd_u16(parents + ((size_t)2U * top));
        }
        if ((uint32_t)parent != top) {
            NT_LOG_WARN("activate_skeleton: joint %u names parent %u, but the innermost open subtree is %u", j, (unsigned)parent, top);
            return false;
        }
        if (parent != NT_SKELETAL_NO_PARENT && end > rd_u16(ends + ((size_t)2U * parent))) {
            NT_LOG_WARN("activate_skeleton: joint %u escapes the subtree of its parent %u", j, (unsigned)parent);
            return false;
        }
        top = j;
    }
    return true;
}

uint32_t nt_skeletal_assets_activate_skeleton(const uint8_t *data, uint32_t size) {
    NT_ASSERT(s_assets.initialized && "nt_skeletal_assets_init must run before the skeletal activators");

    NtSklHeader header = {0};
    if (!skl_validate(data, size, &header)) {
        return 0;
    }

    const uint32_t id = skel_take_slot(data, size);
    if (id == 0) {
        return 0;
    }

    const uint32_t joints = header.joint_count;
    s_assets.slots[nt_pool_slot_index(id)].view.skeleton = (nt_skeletal_skeleton_t){
        .rig_compat_id = (nt_hash64_t){.value = header.rig_compat_id},
        .parent = (const uint16_t *)skel_at(id, sizeof(NtSklHeader)),
        .subtree_end = (const uint16_t *)skel_at(id, sizeof(NtSklHeader) + (2ULL * joints)),
        .joint_id = (const uint32_t *)skel_at(id, sizeof(NtSklHeader) + (4ULL * joints)),
        .rest = (const nt_skeletal_trs_t *)skel_at(id, sizeof(NtSklHeader) + (8ULL * joints)),
        .joint_count = header.joint_count,
    };
    return id;
}

void nt_skeletal_assets_deactivate_skeleton(uint32_t runtime_handle) { skel_release_slot(runtime_handle); }
// #endregion

// #region NSKN skin binding
static bool skn_validate(const uint8_t *data, uint32_t size, NtSknHeader *out) {
    if (data == NULL || size < sizeof(NtSknHeader)) {
        NT_LOG_WARN("activate_skin_binding: payload shorter than the NSKN header");
        return false;
    }
    memcpy(out, data, sizeof(*out));
    if (out->magic != NT_SKN_MAGIC) {
        NT_LOG_WARN("activate_skin_binding: bad magic 0x%08X", out->magic);
        return false;
    }
    if (out->version != NT_SKELETAL_FORMAT_VERSION) {
        NT_LOG_WARN("activate_skin_binding: NSKN version %u is not %u -- rebuild packs", (unsigned)out->version, (unsigned)NT_SKELETAL_FORMAT_VERSION);
        return false;
    }
    if (out->palette_count == 0) {
        NT_LOG_WARN("activate_skin_binding: palette_count 0");
        return false;
    }
    if ((uint64_t)size != NT_SKN_SIZE(out->palette_count)) {
        NT_LOG_WARN("activate_skin_binding: size %u is not the %u bytes %u palette entries need", size, (unsigned)NT_SKN_SIZE(out->palette_count), (unsigned)out->palette_count);
        return false;
    }
    return true;
}

uint32_t nt_skeletal_assets_activate_skin_binding(const uint8_t *data, uint32_t size) {
    NT_ASSERT(s_assets.initialized && "nt_skeletal_assets_init must run before the skeletal activators");

    NtSknHeader header = {0};
    if (!skn_validate(data, size, &header)) {
        return 0;
    }

    const uint32_t id = skel_take_slot(data, size);
    if (id == 0) {
        return 0;
    }

    s_assets.slots[nt_pool_slot_index(id)].view.binding = (nt_skin_binding_t){
        .rig_compat_id = (nt_hash64_t){.value = header.rig_compat_id},
        .inverse_bind = (const nt_skeletal_mat34_t *)skel_at(id, sizeof(NtSknHeader)),
        .remap = (const uint16_t *)skel_at(id, sizeof(NtSknHeader) + (48ULL * header.palette_count)),
        .palette_count = header.palette_count,
    };
    return id;
}

void nt_skeletal_assets_deactivate_skin_binding(uint32_t runtime_handle) { skel_release_slot(runtime_handle); }
// #endregion

// #region NANM clip
/* Byte offset of every array of a validated payload, in the one order the
 * format defines; the encoder writes them in exactly this sequence. */
typedef struct {
    uint64_t blocks;
    uint64_t ct, cq, cs;
    uint64_t steps;
    uint64_t keys;
    uint64_t object;
    uint64_t t_joint, q_joint, s_joint;
    uint64_t ct_joint, cq_joint, cs_joint;
    uint32_t block_floats;
} anm_offsets_t;

static void anm_offsets(const NtAnmHeader *header, anm_offsets_t *out) {
    out->block_floats = (3U * (uint32_t)header->n_t) + (4U * (uint32_t)header->n_q) + (3U * (uint32_t)header->n_s);

    uint64_t at = sizeof(NtAnmHeader);
    out->blocks = at;
    at += (uint64_t)header->sample_count * out->block_floats * 4ULL;
    out->ct = at;
    at += (uint64_t)header->n_ct * 12ULL;
    out->cq = at;
    at += (uint64_t)header->n_cq * 16ULL;
    out->cs = at;
    at += (uint64_t)header->n_cs * 12ULL;
    out->steps = at;
    at += (uint64_t)header->n_steps * NT_ANM_STEP_STRIDE;
    out->keys = at;
    at += (uint64_t)header->n_keys * NT_ANM_KEY_STRIDE;
    out->object = at;
    if (nt_anm_object_sampled(header)) {
        at += (uint64_t)header->sample_count * sizeof(nt_skeletal_trs_t);
    }
    out->t_joint = at;
    at += (uint64_t)header->n_t * 2ULL;
    out->q_joint = at;
    at += (uint64_t)header->n_q * 2ULL;
    out->s_joint = at;
    at += (uint64_t)header->n_s * 2ULL;
    out->ct_joint = at;
    at += (uint64_t)header->n_ct * 2ULL;
    out->cq_joint = at;
    at += (uint64_t)header->n_cq * 2ULL;
    out->cs_joint = at;
}

static bool anm_validate_header(const uint8_t *data, uint32_t size, NtAnmHeader *out) {
    if (data == NULL || size < sizeof(NtAnmHeader)) {
        NT_LOG_WARN("activate_clip: payload shorter than the NANM header");
        return false;
    }
    memcpy(out, data, sizeof(*out));
    if (out->magic != NT_ANM_MAGIC) {
        NT_LOG_WARN("activate_clip: bad magic 0x%08X", out->magic);
        return false;
    }
    if (out->version != NT_SKELETAL_FORMAT_VERSION) {
        NT_LOG_WARN("activate_clip: NANM version %u is not %u -- rebuild packs", (unsigned)out->version, (unsigned)NT_SKELETAL_FORMAT_VERSION);
        return false;
    }
    if (out->joint_count == 0) {
        NT_LOG_WARN("activate_clip: joint_count 0");
        return false;
    }
    if (out->sample_count == 0) {
        NT_LOG_WARN("activate_clip: sample_count 0");
        return false;
    }
    if (!skel_finite(out->duration) || out->duration < 0.0F) {
        NT_LOG_WARN("activate_clip: duration is negative or not finite");
        return false;
    }
    for (uint32_t c = 0; c < 3; ++c) {
        if (out->object_mode[c] > NT_SKELETAL_CHANNEL_STEP) {
            NT_LOG_WARN("activate_clip: object channel %u has unknown mode %u", c, (unsigned)out->object_mode[c]);
            return false;
        }
    }
    if ((uint64_t)size != nt_anm_size(out)) {
        NT_LOG_WARN("activate_clip: size %u is not the %u bytes these counts need", size, (unsigned)nt_anm_size(out));
        return false;
    }
    return true;
}

/* Every table entry the sampler uses as a write index into the caller's pose. */
static bool anm_validate_joint_tables(const uint8_t *data, const NtAnmHeader *header, const anm_offsets_t *off) {
    const uint64_t table[6] = {off->t_joint, off->q_joint, off->s_joint, off->ct_joint, off->cq_joint, off->cs_joint};
    const uint16_t counts[6] = {header->n_t, header->n_q, header->n_s, header->n_ct, header->n_cq, header->n_cs};
    for (uint32_t t = 0; t < 6; ++t) {
        for (uint32_t k = 0; k < counts[t]; ++k) {
            const uint16_t joint = rd_u16(data + table[t] + ((size_t)2U * k));
            if (joint >= header->joint_count) {
                NT_LOG_WARN("activate_clip: joint table %u entry %u names joint %u of %u", t, k, (unsigned)joint, (unsigned)header->joint_count);
                return false;
            }
        }
    }
    return true;
}

/* The STEP tracks partition the key table exactly: the joint tracks in table
 * order, then the object STEP channels in t, q, s order. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool anm_validate_steps(const uint8_t *data, const NtAnmHeader *header, const anm_offsets_t *off) {
    uint64_t total = 0;
    for (uint32_t s = 0; s < header->n_steps; ++s) {
        const uint8_t *track = data + off->steps + ((size_t)NT_ANM_STEP_STRIDE * s);
        const uint32_t first = rd_u32(track);
        const uint32_t count = rd_u32(track + 4);
        const uint16_t joint = rd_u16(track + 8);
        const uint8_t channel = track[10];
        if (joint >= header->joint_count) {
            NT_LOG_WARN("activate_clip: step track %u names joint %u of %u", s, (unsigned)joint, (unsigned)header->joint_count);
            return false;
        }
        if (channel > 2U) {
            NT_LOG_WARN("activate_clip: step track %u names component %u", s, (unsigned)channel);
            return false;
        }
        if (count == 0U || (uint64_t)first != total) {
            NT_LOG_WARN("activate_clip: step track %u holds keys [%u, +%u), expected %u keys onward", s, first, count, (unsigned)total);
            return false;
        }
        total += count;
    }
    for (uint32_t c = 0; c < 3; ++c) {
        if (header->object_mode[c] != NT_SKELETAL_CHANNEL_STEP) {
            continue;
        }
        if (header->object_step_count[c] == 0U || (uint64_t)header->object_step_first[c] != total) {
            NT_LOG_WARN("activate_clip: object channel %u holds keys [%u, +%u), expected %u keys onward", c, header->object_step_first[c], header->object_step_count[c], (unsigned)total);
            return false;
        }
        total += header->object_step_count[c];
    }
    if (total != (uint64_t)header->n_keys) {
        NT_LOG_WARN("activate_clip: step tracks cover %u of the %u keys", (unsigned)total, header->n_keys);
        return false;
    }
    return true;
}

uint32_t nt_skeletal_assets_activate_clip(const uint8_t *data, uint32_t size) {
    NT_ASSERT(s_assets.initialized && "nt_skeletal_assets_init must run before the skeletal activators");

    NtAnmHeader header = {0};
    if (!anm_validate_header(data, size, &header)) {
        return 0;
    }

    anm_offsets_t off = {0};
    anm_offsets(&header, &off);
    if (!anm_validate_joint_tables(data, &header, &off) || !anm_validate_steps(data, &header, &off)) {
        return 0;
    }
    /* Interpolation reads two adjacent grid entries, so anything on the grid
     * needs a second sample and an interval to step through. */
    if ((off.block_floats != 0U || nt_anm_object_sampled(&header)) && (header.sample_count < 2U || !(header.duration > 0.0F))) {
        NT_LOG_WARN("activate_clip: sampled data needs at least two samples over a positive duration");
        return 0;
    }

    const uint32_t id = skel_take_slot(data, size);
    if (id == 0) {
        return 0;
    }

    /* The grid step is exact only in double, and a clip with one sample or no
     * duration has no interval to step through. */
    const double inv_step = (header.sample_count > 1U && header.duration > 0.0F) ? ((double)(header.sample_count - 1U) / (double)header.duration) : 0.0;
    const nt_skeletal_step_key_t *keys = (header.n_keys != 0U) ? (const nt_skeletal_step_key_t *)skel_at(id, off.keys) : NULL;

    nt_skeletal_clip_t view = {
        .rig_compat_id = (nt_hash64_t){.value = header.rig_compat_id},
        .additive_ref_id = (nt_hash64_t){.value = header.additive_ref_id},
        .duration = (double)header.duration,
        .inv_step = inv_step,
        .blocks = (off.block_floats != 0U) ? (const float *)skel_at(id, off.blocks) : NULL,
        .t_joint = (const uint16_t *)skel_at(id, off.t_joint),
        .q_joint = (const uint16_t *)skel_at(id, off.q_joint),
        .s_joint = (const uint16_t *)skel_at(id, off.s_joint),
        .ct_joint = (const uint16_t *)skel_at(id, off.ct_joint),
        .ct = (const float *)skel_at(id, off.ct),
        .cq_joint = (const uint16_t *)skel_at(id, off.cq_joint),
        .cq = (const float *)skel_at(id, off.cq),
        .cs_joint = (const uint16_t *)skel_at(id, off.cs_joint),
        .cs = (const float *)skel_at(id, off.cs),
        .steps = (header.n_steps != 0U) ? (const nt_skeletal_step_t *)skel_at(id, off.steps) : NULL,
        .keys = keys,
        .sample_count = header.sample_count,
        .block_floats = off.block_floats,
        .n_steps = header.n_steps,
        .joint_count = header.joint_count,
        .n_t = header.n_t,
        .n_q = header.n_q,
        .n_s = header.n_s,
        .n_ct = header.n_ct,
        .n_cq = header.n_cq,
        .n_cs = header.n_cs,
    };

    view.object.sampled = nt_anm_object_sampled(&header) ? (const nt_skeletal_trs_t *)skel_at(id, off.object) : NULL;
    view.object.keys = keys;
    view.object.duration = view.duration;
    view.object.inv_step = inv_step;
    view.object.sample_count = header.sample_count;
    memcpy(&view.object.constant, header.object_constant, sizeof(view.object.constant));
    for (uint32_t c = 0; c < 3; ++c) {
        view.object.mode[c] = header.object_mode[c];
        view.object.step_first[c] = header.object_step_first[c];
        view.object.step_count[c] = header.object_step_count[c];
    }

    s_assets.slots[nt_pool_slot_index(id)].view.clip = view;
    return id;
}

void nt_skeletal_assets_deactivate_clip(uint32_t runtime_handle) { skel_release_slot(runtime_handle); }
// #endregion

// #region lifecycle and views
void nt_skeletal_assets_init(uint16_t max_assets) {
    NT_ASSERT(!s_assets.initialized && "nt_skeletal_assets_init called twice");

    nt_pool_init(&s_assets.pool, max_assets);
    s_assets.slots = (nt_skeletal_slot_t *)calloc((size_t)max_assets + 1U, sizeof(nt_skeletal_slot_t));
    NT_ASSERT(s_assets.slots);
    s_assets.initialized = true;
}

void nt_skeletal_assets_shutdown(void) {
    for (uint32_t i = 1; i <= s_assets.pool.capacity; ++i) {
        if (nt_pool_slot_alive(&s_assets.pool, i)) {
            free(s_assets.slots[i].mem);
        }
    }
    free(s_assets.slots);
    nt_pool_shutdown(&s_assets.pool);
    memset(&s_assets, 0, sizeof(s_assets));
}

const nt_skeletal_skeleton_t *nt_skeletal_assets_skeleton(nt_resource_t skeleton) {
    NT_ASSERT(nt_resource_get_asset_type(skeleton) == NT_ASSET_SKELETON && "nt_skeletal_assets_skeleton: handle is not a skeleton resource");
    const uint32_t id = nt_resource_get(skeleton);
    NT_ASSERT(nt_pool_valid(&s_assets.pool, id) && "nt_skeletal_assets_skeleton on an unready skeleton");
    return &s_assets.slots[nt_pool_slot_index(id)].view.skeleton;
}

const nt_skin_binding_t *nt_skeletal_assets_skin_binding(nt_resource_t binding) {
    NT_ASSERT(nt_resource_get_asset_type(binding) == NT_ASSET_SKIN_BINDING && "nt_skeletal_assets_skin_binding: handle is not a skin binding resource");
    const uint32_t id = nt_resource_get(binding);
    NT_ASSERT(nt_pool_valid(&s_assets.pool, id) && "nt_skeletal_assets_skin_binding on an unready binding");
    return &s_assets.slots[nt_pool_slot_index(id)].view.binding;
}

const nt_skeletal_clip_t *nt_skeletal_assets_clip(nt_resource_t clip) {
    NT_ASSERT(nt_resource_get_asset_type(clip) == NT_ASSET_CLIP && "nt_skeletal_assets_clip: handle is not a clip resource");
    const uint32_t id = nt_resource_get(clip);
    NT_ASSERT(nt_pool_valid(&s_assets.pool, id) && "nt_skeletal_assets_clip on an unready clip");
    return &s_assets.slots[nt_pool_slot_index(id)].view.clip;
}
// #endregion
