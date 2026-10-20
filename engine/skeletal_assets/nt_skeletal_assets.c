#include "skeletal_assets/nt_skeletal_assets.h"

#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"
#include "log/nt_log.h"
#include "nt_pack_format.h"
#include "nt_skeletal_format.h"
#include "pool/nt_pool.h"

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
    nt_skeletal_slot_t *slots; /* [capacity + 1], index 0 reserved by nt_pool; NULL until init */
} s_assets;
// #endregion

// #region slots
/* Copies the payload into a fresh slot; the caller points the slot's view into
 * the copy. Payload arrays are read in place through typed pointers over the
 * copy; malloc's alignment carries them. */
static uint32_t skel_take_slot(const uint8_t *data, uint32_t size) {
    const uint32_t id = nt_pool_alloc(&s_assets.pool);
    NT_ASSERT(id != 0 && "skeletal asset pool exhausted -- raise nt_skeletal_assets_init(max_assets)");
    nt_skeletal_slot_t *slot = &s_assets.slots[nt_pool_slot_index(id)];
    slot->mem = malloc(size);
    NT_ASSERT(slot->mem != NULL);
    memcpy(slot->mem, data, size);
    return id;
}

static void skel_release_slot(uint32_t id) {
    NT_ASSERT(s_assets.slots != NULL && "nt_skeletal_assets_shutdown ran before a skeletal deactivator");
    NT_ASSERT(nt_pool_valid(&s_assets.pool, id) && "skeletal handle is not live");
    nt_skeletal_slot_t *slot = &s_assets.slots[nt_pool_slot_index(id)];
    free(slot->mem);
    memset(slot, 0, sizeof(*slot));
    nt_pool_free(&s_assets.pool, id);
}

/* The slot behind a ready resource of one asset type, both asserted. */
static const nt_skeletal_slot_t *skel_view_slot(nt_resource_t handle, uint8_t asset_type) {
    NT_ASSERT(nt_resource_get_asset_type(handle) == asset_type && "skeletal view: handle is not a resource of the requested type");
    const uint32_t id = nt_resource_get(handle);
    NT_ASSERT(nt_pool_valid(&s_assets.pool, id) && "skeletal view on an unready asset");
    return &s_assets.slots[nt_pool_slot_index(id)];
}
// #endregion

// #region NSKL skeleton
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void skl_check(const uint8_t *data, uint32_t size, NtSklHeader *out) {
    memcpy(out, data, sizeof(*out));
    NT_ASSERT(out->joint_count != 0 && (uint64_t)size == NT_SKL_SIZE(out->joint_count) && "skeleton size does not match its joint count");

    /* Exact preorder: the parent of joint j must be the nearest earlier joint
     * whose subtree range is still open at j. That single rule carries preorder
     * parents, sibling contiguity, root chaining and the last root closing at
     * joint_count, and it is what makes FK's parent-before-child read safe.
     * Each subtree is popped once, so the walk is O(J) amortized. */
    const uint16_t *parents = (const uint16_t *)(data + sizeof(NtSklHeader));
    const uint16_t *ends = parents + out->joint_count;
    uint32_t top = NT_SKELETAL_NO_PARENT;
    for (uint32_t j = 0; j < out->joint_count; ++j) {
        const uint16_t parent = parents[j];
        const uint16_t end = ends[j];
        NT_ASSERT((uint32_t)end > j && (uint32_t)end <= out->joint_count && "skeleton subtree_end out of range");
        while (top != NT_SKELETAL_NO_PARENT && (uint32_t)ends[top] <= j) {
            top = parents[top];
        }
        NT_ASSERT((uint32_t)parent == top && "skeleton parent is not the innermost open subtree");
        NT_ASSERT((parent == NT_SKELETAL_NO_PARENT || end <= ends[parent]) && "skeleton joint escapes its parent subtree");
        top = j;
    }
}

uint32_t nt_skeletal_assets_activate_skeleton(const uint8_t *data, uint32_t size) {
    NT_ASSERT(s_assets.slots != NULL && "nt_skeletal_assets_init must run before the skeletal activators");

    NtSklHeader header = {0};
    skl_check(data, size, &header);

    const uint32_t id = skel_take_slot(data, size);
    nt_skeletal_slot_t *slot = &s_assets.slots[nt_pool_slot_index(id)];
    const uint8_t *mem = (const uint8_t *)slot->mem;
    const size_t joints = header.joint_count;
    slot->view.skeleton = (nt_skeletal_skeleton_t){
        .rig_compat_id = (nt_hash64_t){.value = header.rig_compat_id},
        .parent = (const uint16_t *)(mem + sizeof(NtSklHeader)),
        .subtree_end = (const uint16_t *)(mem + sizeof(NtSklHeader) + (2U * joints)),
        .joint_id = (const uint32_t *)(mem + sizeof(NtSklHeader) + (4U * joints)),
        .rest = (const nt_skeletal_trs_t *)(mem + sizeof(NtSklHeader) + (8U * joints)),
        .joint_count = header.joint_count,
    };
    return id;
}

void nt_skeletal_assets_deactivate_skeleton(uint32_t runtime_handle) { skel_release_slot(runtime_handle); }
// #endregion

// #region NSKN skin binding
static void skn_check(const uint8_t *data, uint32_t size, NtSknHeader *out) {
    memcpy(out, data, sizeof(*out));
    NT_ASSERT(out->palette_count != 0 && (uint64_t)size == NT_SKN_SIZE(out->palette_count) && "skin binding size does not match its palette count");
}

uint32_t nt_skeletal_assets_activate_skin_binding(const uint8_t *data, uint32_t size) {
    NT_ASSERT(s_assets.slots != NULL && "nt_skeletal_assets_init must run before the skeletal activators");

    NtSknHeader header = {0};
    skn_check(data, size, &header);

    const uint32_t id = skel_take_slot(data, size);
    nt_skeletal_slot_t *slot = &s_assets.slots[nt_pool_slot_index(id)];
    const uint8_t *mem = (const uint8_t *)slot->mem;
    slot->view.binding = (nt_skin_binding_t){
        .rig_compat_id = (nt_hash64_t){.value = header.rig_compat_id},
        .inverse_bind = (const nt_skeletal_mat34_t *)(mem + sizeof(NtSknHeader)),
        .remap = (const uint16_t *)(mem + sizeof(NtSknHeader) + (48U * (size_t)header.palette_count)),
        .reach = header.reach,
        .any_pose_radius = header.any_pose_radius,
        .palette_count = header.palette_count,
    };
    return id;
}

void nt_skeletal_assets_deactivate_skin_binding(uint32_t runtime_handle) { skel_release_slot(runtime_handle); }
// #endregion

// #region NANM clip
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void anm_check_header(const uint8_t *data, uint32_t size, NtAnmHeader *out) {
    memcpy(out, data, sizeof(*out));
    NT_ASSERT(out->joint_count != 0 && out->sample_count != 0 && "clip has no joints or samples");
    /* duration sizes the grid the sampler indexes, so it is structure. */
    NT_ASSERT(nt_skeletal_finite((double)out->duration) && out->duration >= 0.0F && "clip duration is negative or not finite");
    NT_ASSERT((uint64_t)size == nt_anm_size(out) && "clip size does not match its counts");
    /* Interpolation reads two adjacent grid entries, so a row needs a second
     * sample and an interval to step through. */
    const bool sampled = (out->n_t | out->n_q | out->n_s) != 0U;
    NT_ASSERT((!sampled || (out->sample_count >= 2U && out->duration > 0.0F)) && "clip sampled rows need two samples over a positive duration");
}

uint32_t nt_skeletal_assets_activate_clip(const uint8_t *data, uint32_t size) {
    NT_ASSERT(s_assets.slots != NULL && "nt_skeletal_assets_init must run before the skeletal activators");

    NtAnmHeader header = {0};
    anm_check_header(data, size, &header);

    /* Every table entry is a write index the sampler turns into a position in
     * the caller's pose. */
    nt_skeletal_clip_t check;
    nt_skeletal_clip_view(data, &check);
    const uint16_t *const table[3] = {check.t_joint, check.q_joint, check.s_joint};
    const uint16_t counts[3] = {check.n_t, check.n_q, check.n_s};
    for (uint32_t t = 0; t < 3; ++t) {
        for (uint32_t k = 0; k < counts[t]; ++k) {
            NT_ASSERT(table[t][k] < check.joint_count && "clip joint table names a joint out of range");
        }
    }

    const uint32_t id = skel_take_slot(data, size);
    nt_skeletal_slot_t *slot = &s_assets.slots[nt_pool_slot_index(id)];
    nt_skeletal_clip_view((const uint8_t *)slot->mem, &slot->view.clip);
    return id;
}

void nt_skeletal_assets_deactivate_clip(uint32_t runtime_handle) { skel_release_slot(runtime_handle); }
// #endregion

// #region lifecycle and views
void nt_skeletal_assets_init(uint16_t max_assets) {
    NT_ASSERT(s_assets.slots == NULL && "nt_skeletal_assets_init called twice");

    nt_pool_init(&s_assets.pool, max_assets);
    s_assets.slots = (nt_skeletal_slot_t *)calloc((size_t)max_assets + 1U, sizeof(nt_skeletal_slot_t));
    NT_ASSERT(s_assets.slots);
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

const nt_skeletal_skeleton_t *nt_skeletal_assets_skeleton(nt_resource_t skeleton) { return &skel_view_slot(skeleton, NT_ASSET_SKELETON)->view.skeleton; }

const nt_skin_binding_t *nt_skeletal_assets_skin_binding(nt_resource_t binding) { return &skel_view_slot(binding, NT_ASSET_SKIN_BINDING)->view.binding; }

const nt_skeletal_clip_t *nt_skeletal_assets_clip(nt_resource_t clip) { return &skel_view_slot(clip, NT_ASSET_CLIP)->view.clip; }
// #endregion
