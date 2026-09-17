#include "skeletal_assets/nt_skeletal_assets.h"

#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"
#include "log/nt_log.h"
#include "nt_pack_format.h"
#include "nt_skeletal_format.h"

/* The runtime channel modes repeat the wire values so a headless CPU build links
 * no pack code (nt_skeletal.h); this is the one translation unit that sees both,
 * so it is where the two enumerations are pinned together. The clip kind needs
 * no pin: the activator stores the validated wire byte unchanged. The wire side
 * is compared as the byte it travels as, which is also what keeps the two enum
 * types out of one comparison. */
_Static_assert((uint8_t)NT_ANM_CHANNEL_ABSENT == NT_SKELETAL_CHANNEL_ABSENT, "wire and runtime ABSENT must agree");
_Static_assert((uint8_t)NT_ANM_CHANNEL_CONSTANT == NT_SKELETAL_CHANNEL_CONSTANT, "wire and runtime CONSTANT must agree");
_Static_assert((uint8_t)NT_ANM_CHANNEL_SAMPLED == NT_SKELETAL_CHANNEL_SAMPLED, "wire and runtime SAMPLED must agree");
_Static_assert((uint8_t)NT_ANM_CHANNEL_STEP == NT_SKELETAL_CHANNEL_STEP, "wire and runtime STEP must agree");

// #region module state
/* A slot is live exactly while it owns an allocation, so mem doubles as the
 * occupancy flag; a payload whose runtime tables are all empty still takes one
 * byte to keep that true. */
typedef struct {
    void *mem;
    nt_skeletal_skeleton_t view;
} nt_skl_slot_t;

typedef struct {
    void *mem;
    nt_skin_binding_t view;
} nt_skn_slot_t;

typedef struct {
    void *mem;
    nt_skeletal_clip_t view;
} nt_anm_slot_t;

static struct {
    nt_skl_slot_t *skeletons;
    nt_skn_slot_t *bindings;
    nt_anm_slot_t *clips;
    uint16_t max_skeletons;
    uint16_t max_bindings;
    uint16_t max_clips;
    bool initialized;
} s_assets;
// #endregion

// #region wire readers
/* The payload is little-endian on every target; composing the fields by byte
 * keeps the contract that pack bytes are never cast to a struct. */
static uint16_t rd_u16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8)); }
static uint32_t rd_u32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t rd_u64(const uint8_t *p) { return (uint64_t)rd_u32(p) | ((uint64_t)rd_u32(p + 4) << 32); }

static float rd_f32(const uint8_t *p) {
    const uint32_t bits = rd_u32(p);
    float v = 0.0F;
    memcpy(&v, &bits, sizeof(v));
    return v;
}

static void rd_f32n(const uint8_t *p, float *out, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        out[i] = rd_f32(p + ((size_t)4U * i));
    }
}
// #endregion

// #region numeric checks
/* x - x rejects non-finite values without libm; requires strict IEEE math. */
static bool skel_finite(float v) { return (v - v) == 0.0F; }

static bool skel_finite_n(const float *v, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        if (!skel_finite(v[i])) {
            return false;
        }
    }
    return true;
}

/* Same tolerance as nt_skeletal_mat34_from_trs, which is what the kernels check
 * the decoded data against. */
static bool skel_unit_quat(const float *q) {
    const float n = (q[0] * q[0]) + (q[1] * q[1]) + (q[2] * q[2]) + (q[3] * q[3]);
    return skel_finite(n) && (n - 1.0F) < 1e-3F && (1.0F - n) < 1e-3F;
}

/* A component value of comps floats: a rotation must be unit, a translation or
 * scale must leave the fourth wire float at 0. */
static bool skel_value_valid(const float *v, uint32_t comps) {
    if (!skel_finite_n(v, 4)) {
        return false;
    }
    return (comps == 4) ? skel_unit_quat(v) : (v[3] == 0.0F);
}

static bool skel_version_ok(uint16_t version) { return NT_SKELETAL_FORMAT_MAJOR(version) == NT_SKELETAL_FORMAT_MAJOR(NT_SKELETAL_FORMAT_VERSION); }
// #endregion

// #region one-allocation layout
/* Every runtime array holds 4-byte or 2-byte elements, so rounding each block
 * up to 4 bytes keeps float and uint32 access aligned inside one malloc. */
static uint64_t skel_reserve(uint64_t *cursor, uint64_t bytes) {
    const uint64_t at = *cursor;
    *cursor = at + ((bytes + 3U) & ~(uint64_t)3U);
    return at;
}

static void *skel_at(void *base, uint64_t offset) { return (void *)((uint8_t *)base + offset); }
// #endregion

// #region NSKL skeleton
typedef struct {
    const uint8_t *parent;
    const uint8_t *subtree_end;
    const uint8_t *joint_id;
    const uint8_t *rest;
    uint64_t rig_compat_id;
    uint16_t joint_count;
} skl_parse_t;

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool skl_validate(const uint8_t *data, uint32_t size, skl_parse_t *p) {
    if (data == NULL || size < sizeof(NtSklHeader)) {
        NT_LOG_WARN("activate_skeleton: payload shorter than the NSKL header");
        return false;
    }
    if (rd_u32(data) != NT_SKL_MAGIC) {
        NT_LOG_WARN("activate_skeleton: bad magic 0x%08X", rd_u32(data));
        return false;
    }
    const uint16_t version = rd_u16(data + 4);
    if (!skel_version_ok(version)) {
        NT_LOG_WARN("activate_skeleton: NSKL major version %u is not %u -- rebuild packs", (unsigned)NT_SKELETAL_FORMAT_MAJOR(version), (unsigned)NT_SKELETAL_FORMAT_MAJOR(NT_SKELETAL_FORMAT_VERSION));
        return false;
    }
    p->joint_count = rd_u16(data + 6);
    if (p->joint_count == 0) {
        NT_LOG_WARN("activate_skeleton: joint_count 0");
        return false;
    }
    if (size != NT_SKL_SIZE(p->joint_count)) {
        NT_LOG_WARN("activate_skeleton: size %u != %u for %u joints", size, NT_SKL_SIZE(p->joint_count), (unsigned)p->joint_count);
        return false;
    }
    p->rig_compat_id = rd_u64(data + 8);
    p->parent = data + sizeof(NtSklHeader);
    p->subtree_end = p->parent + ((size_t)2U * p->joint_count);
    p->joint_id = p->subtree_end + ((size_t)2U * p->joint_count);
    p->rest = p->joint_id + ((size_t)4U * p->joint_count);

    /* Exact preorder: the parent of joint j must be the nearest earlier joint
     * whose subtree range is still open at j. That single rule carries preorder
     * parents, sibling contiguity, root chaining and the last root closing at
     * joint_count. Each subtree is popped once, so the walk is O(J) amortized. */
    uint32_t top = NT_SKELETAL_NO_PARENT;
    for (uint32_t j = 0; j < p->joint_count; ++j) {
        const uint16_t parent = rd_u16(p->parent + ((size_t)2U * j));
        const uint16_t end = rd_u16(p->subtree_end + ((size_t)2U * j));
        if ((uint32_t)end <= j || (uint32_t)end > p->joint_count) {
            NT_LOG_WARN("activate_skeleton: joint %u has subtree_end %u outside (%u, %u]", j, (unsigned)end, j, (unsigned)p->joint_count);
            return false;
        }
        while (top != NT_SKELETAL_NO_PARENT && (uint32_t)rd_u16(p->subtree_end + ((size_t)2U * top)) <= j) {
            top = rd_u16(p->parent + ((size_t)2U * top));
        }
        if ((uint32_t)parent != top) {
            NT_LOG_WARN("activate_skeleton: joint %u names parent %u, but the innermost open subtree is %u", j, (unsigned)parent, top);
            return false;
        }
        if (parent != NT_SKELETAL_NO_PARENT && end > rd_u16(p->subtree_end + ((size_t)2U * parent))) {
            NT_LOG_WARN("activate_skeleton: joint %u escapes the subtree of its parent %u", j, (unsigned)parent);
            return false;
        }
        top = j;
        float trs[10];
        rd_f32n(p->rest + ((size_t)40U * j), trs, 10);
        if (!skel_finite_n(trs, 10)) {
            NT_LOG_WARN("activate_skeleton: joint %u has a non-finite rest value", j);
            return false;
        }
        if (!skel_unit_quat(trs + 3)) {
            NT_LOG_WARN("activate_skeleton: joint %u has a non-unit rest rotation", j);
            return false;
        }
    }
    return true;
}

uint32_t nt_skeletal_assets_activate_skeleton(const uint8_t *data, uint32_t size) {
    NT_ASSERT(s_assets.initialized && "nt_skeletal_assets_init must run before the skeletal activators");

    skl_parse_t p = {0};
    if (!skl_validate(data, size, &p)) {
        return 0;
    }

    uint16_t slot = 0;
    while (slot < s_assets.max_skeletons && s_assets.skeletons[slot].mem != NULL) {
        ++slot;
    }
    if (slot == s_assets.max_skeletons) {
        /* An OFF build has no assert to read, so the log is what explains the
         * FAILED asset. */
        NT_LOG_WARN("activate_skeleton: skeleton pool of %u is full", (unsigned)s_assets.max_skeletons);
        NT_ASSERT(false && "skeleton pool exhausted -- raise nt_skeletal_assets_desc_t.max_skeletons");
        return 0;
    }

    const uint32_t joints = p.joint_count;
    uint64_t cursor = 0;
    const uint64_t off_rest = skel_reserve(&cursor, (uint64_t)joints * sizeof(nt_skeletal_trs_t));
    const uint64_t off_id = skel_reserve(&cursor, (uint64_t)joints * 4U);
    const uint64_t off_parent = skel_reserve(&cursor, (uint64_t)joints * 2U);
    const uint64_t off_end = skel_reserve(&cursor, (uint64_t)joints * 2U);

    void *mem = malloc((size_t)cursor);
    if (mem == NULL) {
        NT_LOG_WARN("activate_skeleton: allocation of %u bytes failed", (unsigned)cursor);
        return 0;
    }
    memset(mem, 0, (size_t)cursor);

    nt_skeletal_trs_t *rest = (nt_skeletal_trs_t *)skel_at(mem, off_rest);
    uint32_t *joint_id = (uint32_t *)skel_at(mem, off_id);
    uint16_t *parent = (uint16_t *)skel_at(mem, off_parent);
    uint16_t *subtree_end = (uint16_t *)skel_at(mem, off_end);
    for (uint32_t j = 0; j < joints; ++j) {
        parent[j] = rd_u16(p.parent + ((size_t)2U * j));
        subtree_end[j] = rd_u16(p.subtree_end + ((size_t)2U * j));
        joint_id[j] = rd_u32(p.joint_id + ((size_t)4U * j));
        rd_f32n(p.rest + ((size_t)40U * j), rest[j].t, 3);
        rd_f32n(p.rest + ((size_t)40U * j) + 12U, rest[j].q, 4);
        rd_f32n(p.rest + ((size_t)40U * j) + 28U, rest[j].s, 3);
    }

    s_assets.skeletons[slot].mem = mem;
    s_assets.skeletons[slot].view = (nt_skeletal_skeleton_t){
        .rig_compat_id = (nt_hash64_t){.value = p.rig_compat_id},
        .parent = parent,
        .subtree_end = subtree_end,
        .joint_id = joint_id,
        .rest = rest,
        .joint_count = p.joint_count,
    };
    return (uint32_t)slot + 1U;
}

void nt_skeletal_assets_deactivate_skeleton(uint32_t runtime_handle) {
    NT_ASSERT(s_assets.initialized && "nt_skeletal_assets_shutdown ran before the skeleton deactivator");
    NT_ASSERT(runtime_handle != 0 && runtime_handle <= s_assets.max_skeletons && "skeleton handle out of range");
    nt_skl_slot_t *slot = &s_assets.skeletons[runtime_handle - 1U];
    NT_ASSERT(slot->mem != NULL && "skeleton slot is already free");
    free(slot->mem);
    slot->mem = NULL;
    slot->view = (nt_skeletal_skeleton_t){0};
}
// #endregion

// #region NSKN skin binding
typedef struct {
    const uint8_t *remap;
    const uint8_t *inverse_bind;
    uint64_t rig_compat_id;
    float reach;
    float any_pose_radius;
    uint16_t palette_count;
} skn_parse_t;

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool skn_validate(const uint8_t *data, uint32_t size, skn_parse_t *p) {
    if (data == NULL || size < sizeof(NtSknHeader)) {
        NT_LOG_WARN("activate_skin_binding: payload shorter than the NSKN header");
        return false;
    }
    if (rd_u32(data) != NT_SKN_MAGIC) {
        NT_LOG_WARN("activate_skin_binding: bad magic 0x%08X", rd_u32(data));
        return false;
    }
    const uint16_t version = rd_u16(data + 4);
    if (!skel_version_ok(version)) {
        NT_LOG_WARN("activate_skin_binding: NSKN major version %u is not %u -- rebuild packs", (unsigned)NT_SKELETAL_FORMAT_MAJOR(version),
                    (unsigned)NT_SKELETAL_FORMAT_MAJOR(NT_SKELETAL_FORMAT_VERSION));
        return false;
    }
    p->palette_count = rd_u16(data + 6);
    if (p->palette_count == 0) {
        NT_LOG_WARN("activate_skin_binding: palette_count 0");
        return false;
    }
    if (size != NT_SKN_SIZE(p->palette_count)) {
        NT_LOG_WARN("activate_skin_binding: size %u != %u for %u palette entries", size, NT_SKN_SIZE(p->palette_count), (unsigned)p->palette_count);
        return false;
    }
    if (data[16] != NT_SKN_MESH_SPACE_GLTF_NODE) {
        NT_LOG_WARN("activate_skin_binding: mesh_space %u is not glTF mesh-node space", (unsigned)data[16]);
        return false;
    }
    p->reach = rd_f32(data + 20);
    p->any_pose_radius = rd_f32(data + 24);
    if (!skel_finite(p->reach) || p->reach < 0.0F || !skel_finite(p->any_pose_radius) || p->any_pose_radius < 0.0F) {
        NT_LOG_WARN("activate_skin_binding: reach or any_pose_radius is negative or not finite");
        return false;
    }
    p->rig_compat_id = rd_u64(data + 8);
    p->remap = data + sizeof(NtSknHeader);
    p->inverse_bind = p->remap + ((size_t)2U * p->palette_count);

    for (uint32_t e = 0; e < p->palette_count; ++e) {
        float m[12];
        rd_f32n(p->inverse_bind + ((size_t)48U * e), m, 12);
        if (!skel_finite_n(m, 12)) {
            NT_LOG_WARN("activate_skin_binding: palette entry %u has a non-finite inverse bind matrix", e);
            return false;
        }
    }
    return true;
}

uint32_t nt_skeletal_assets_activate_skin_binding(const uint8_t *data, uint32_t size) {
    NT_ASSERT(s_assets.initialized && "nt_skeletal_assets_init must run before the skeletal activators");

    skn_parse_t p = {0};
    if (!skn_validate(data, size, &p)) {
        return 0;
    }

    uint16_t slot = 0;
    while (slot < s_assets.max_bindings && s_assets.bindings[slot].mem != NULL) {
        ++slot;
    }
    if (slot == s_assets.max_bindings) {
        NT_LOG_WARN("activate_skin_binding: skin binding pool of %u is full", (unsigned)s_assets.max_bindings);
        NT_ASSERT(false && "skin binding pool exhausted -- raise nt_skeletal_assets_desc_t.max_skin_bindings");
        return 0;
    }

    const uint32_t entries = p.palette_count;
    uint64_t cursor = 0;
    const uint64_t off_matrices = skel_reserve(&cursor, (uint64_t)entries * sizeof(nt_skeletal_mat34_t));
    const uint64_t off_remap = skel_reserve(&cursor, (uint64_t)entries * 2U);

    void *mem = malloc((size_t)cursor);
    if (mem == NULL) {
        NT_LOG_WARN("activate_skin_binding: allocation of %u bytes failed", (unsigned)cursor);
        return 0;
    }
    memset(mem, 0, (size_t)cursor);

    nt_skeletal_mat34_t *inverse_bind = (nt_skeletal_mat34_t *)skel_at(mem, off_matrices);
    uint16_t *remap = (uint16_t *)skel_at(mem, off_remap);
    for (uint32_t e = 0; e < entries; ++e) {
        remap[e] = rd_u16(p.remap + ((size_t)2U * e));
        rd_f32n(p.inverse_bind + ((size_t)48U * e), &inverse_bind[e].r[0][0], 12);
    }

    s_assets.bindings[slot].mem = mem;
    s_assets.bindings[slot].view = (nt_skin_binding_t){
        .rig_compat_id = (nt_hash64_t){.value = p.rig_compat_id},
        .remap = remap,
        .inverse_bind = inverse_bind,
        .reach = p.reach,
        .any_pose_radius = p.any_pose_radius,
        .palette_count = p.palette_count,
    };
    return (uint32_t)slot + 1U;
}

void nt_skeletal_assets_deactivate_skin_binding(uint32_t runtime_handle) {
    NT_ASSERT(s_assets.initialized && "nt_skeletal_assets_shutdown ran before the skin binding deactivator");
    NT_ASSERT(runtime_handle != 0 && runtime_handle <= s_assets.max_bindings && "skin binding handle out of range");
    nt_skn_slot_t *slot = &s_assets.bindings[runtime_handle - 1U];
    NT_ASSERT(slot->mem != NULL && "skin binding slot is already free");
    free(slot->mem);
    slot->mem = NULL;
    slot->view = (nt_skin_binding_t){0};
}
// #endregion

// #region NANM validation
/* Section table slots, in the fixed v1 order. SEC_PLNT + kind is the plane of
 * component kind 0/1/2, which is what the CHAN walk indexes. */
enum {
    SEC_CHAN = 0,
    SEC_PLNT = 1,
    SEC_PLNQ = 2,
    SEC_PLNS = 3,
    SEC_CNST = 4,
    SEC_STPT = 5,
    SEC_STPK = 6,
};

typedef struct {
    uint32_t offset;
    uint32_t count;
    uint32_t stride;
} anm_section_t;

typedef struct {
    const uint8_t *data;
    uint32_t size;
    anm_section_t sec[NT_ANM_SECTION_COUNT];
    uint64_t rig_compat_id;
    uint64_t additive_ref_id;
    uint32_t channel_count;
    uint32_t sample_count;
    uint32_t n_steps;    /* STEP tracks on joints, object curve excluded */
    uint16_t n[3];       /* sampled joint rows per component kind */
    uint16_t n_const[3]; /* constant joint channels per component kind */
    uint16_t joint_count;
    uint8_t kind;
    uint8_t obj_mode[3];
    uint16_t obj_index[3];
    float duration;
    float r_joints, r_root, s_max;
    float bake_fps_min, bake_reach;
} anm_parse_t;

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool anm_validate_header(const uint8_t *data, uint32_t size, anm_parse_t *p) {
    if (data == NULL || size < sizeof(NtAnmHeader)) {
        NT_LOG_WARN("activate_clip: payload shorter than the NANM header");
        return false;
    }
    if (rd_u32(data) != NT_ANM_MAGIC) {
        NT_LOG_WARN("activate_clip: bad magic 0x%08X", rd_u32(data));
        return false;
    }
    const uint16_t version = rd_u16(data + 4);
    if (!skel_version_ok(version)) {
        NT_LOG_WARN("activate_clip: NANM major version %u is not %u -- rebuild packs", (unsigned)NT_SKELETAL_FORMAT_MAJOR(version), (unsigned)NT_SKELETAL_FORMAT_MAJOR(NT_SKELETAL_FORMAT_VERSION));
        return false;
    }
    p->data = data;
    p->size = size;
    p->joint_count = rd_u16(data + 8);
    if (p->joint_count == 0) {
        NT_LOG_WARN("activate_clip: joint_count 0");
        return false;
    }
    p->channel_count = NT_ANM_CHANNEL_COUNT(p->joint_count);
    p->kind = data[10];
    if (p->kind > NT_ANM_KIND_ADDITIVE) {
        NT_LOG_WARN("activate_clip: unknown clip kind %u", (unsigned)p->kind);
        return false;
    }
    if (data[11] != NT_ANM_CODEC_F32) {
        NT_LOG_WARN("activate_clip: codec %u is not F32", (unsigned)data[11]);
        return false;
    }
    p->rig_compat_id = rd_u64(data + 12);
    p->additive_ref_id = rd_u64(data + 20);
    if ((p->kind == NT_ANM_KIND_ABSOLUTE) != (p->additive_ref_id == 0)) {
        NT_LOG_WARN("activate_clip: additive_ref_id must be set exactly for an additive clip");
        return false;
    }
    p->duration = rd_f32(data + 28);
    if (!skel_finite(p->duration) || p->duration < 0.0F) {
        NT_LOG_WARN("activate_clip: duration is negative or not finite");
        return false;
    }
    p->sample_count = rd_u32(data + 32);
    if (p->sample_count == 0) {
        NT_LOG_WARN("activate_clip: sample_count 0");
        return false;
    }
    p->r_joints = rd_f32(data + 36);
    p->r_root = rd_f32(data + 40);
    p->s_max = rd_f32(data + 44);
    p->bake_fps_min = rd_f32(data + 48);
    p->bake_reach = rd_f32(data + 52);
    const float numbers[5] = {p->r_joints, p->r_root, p->s_max, p->bake_fps_min, p->bake_reach};
    for (uint32_t i = 0; i < 5; ++i) {
        if (!skel_finite(numbers[i]) || numbers[i] < 0.0F) {
            NT_LOG_WARN("activate_clip: a bounds or certificate number is negative or not finite");
            return false;
        }
    }
    return true;
}

/* Known tags in the order a v1 payload must carry them. */
static const uint32_t k_anm_tags[NT_ANM_SECTION_COUNT] = {
    NT_ANM_TAG_CHAN, NT_ANM_TAG_PLNT, NT_ANM_TAG_PLNQ, NT_ANM_TAG_PLNS, NT_ANM_TAG_CNST, NT_ANM_TAG_STPT, NT_ANM_TAG_STPK,
};

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool anm_validate_sections(anm_parse_t *p) {
    const uint16_t section_count = rd_u16(p->data + 6);
    const uint64_t table_end = (uint64_t)sizeof(NtAnmHeader) + ((uint64_t)section_count * sizeof(NtAnmSection));
    if (table_end > p->size) {
        NT_LOG_WARN("activate_clip: %u section descriptors do not fit the payload", (unsigned)section_count);
        return false;
    }

    uint32_t seen = 0;
    uint32_t next_known = 0;
    for (uint32_t s = 0; s < section_count; ++s) {
        const uint8_t *desc = p->data + sizeof(NtAnmHeader) + ((size_t)s * sizeof(NtAnmSection));
        const uint32_t tag = rd_u32(desc);
        const anm_section_t sec = {.offset = rd_u32(desc + 4), .count = rd_u32(desc + 8), .stride = rd_u32(desc + 12)};
        if ((sec.offset % 4U) != 0U || (uint64_t)sec.offset < table_end) {
            NT_LOG_WARN("activate_clip: section %u offset %u is misaligned or inside the table", s, sec.offset);
            return false;
        }
        /* 64-bit: a corrupt count * stride must not wrap the bound into a pass. */
        if ((uint64_t)sec.offset + ((uint64_t)sec.count * sec.stride) > (uint64_t)p->size) {
            NT_LOG_WARN("activate_clip: section %u (%u x %u at %u) overflows the payload", s, sec.count, sec.stride, sec.offset);
            return false;
        }
        uint32_t known = NT_ANM_SECTION_COUNT;
        for (uint32_t k = 0; k < NT_ANM_SECTION_COUNT; ++k) {
            if (k_anm_tags[k] == tag) {
                known = k;
                break;
            }
        }
        if (known == NT_ANM_SECTION_COUNT) {
            continue; /* minor-version rule: an unknown tag is skipped */
        }
        if ((seen & (1U << known)) != 0U) {
            NT_LOG_WARN("activate_clip: section tag 0x%08X appears twice", tag);
            return false;
        }
        if (known < next_known) {
            NT_LOG_WARN("activate_clip: section tag 0x%08X is out of the v1 order", tag);
            return false;
        }
        seen |= 1U << known;
        next_known = known + 1U;
        p->sec[known] = sec;
    }
    if (seen != (1U << NT_ANM_SECTION_COUNT) - 1U) {
        NT_LOG_WARN("activate_clip: a required section tag is missing (seen mask 0x%02X)", seen);
        return false;
    }
    return true;
}

/* Element sizes must hold before any entry is read: a section that passes the
 * offset/count/stride bound can still declare a stride that makes a 16-byte
 * constant or a 20-byte key read past the payload. */
static bool anm_validate_strides(const anm_parse_t *p) {
    if (p->sec[SEC_CHAN].count != p->channel_count || p->sec[SEC_CHAN].stride != sizeof(NtAnmChannel)) {
        NT_LOG_WARN("activate_clip: CHAN holds %u x %u, expected %u x %u", p->sec[SEC_CHAN].count, p->sec[SEC_CHAN].stride, p->channel_count, (unsigned)sizeof(NtAnmChannel));
        return false;
    }
    for (uint32_t kind = 0; kind < 3; ++kind) {
        const uint32_t comps = (kind == 1U) ? 4U : 3U;
        if ((uint64_t)p->sec[SEC_PLNT + kind].stride != (uint64_t)p->sample_count * comps * 4U) {
            NT_LOG_WARN("activate_clip: plane %u stride %u does not hold %u samples", kind, p->sec[SEC_PLNT + kind].stride, p->sample_count);
            return false;
        }
    }
    if (p->sec[SEC_CNST].stride != 16U || p->sec[SEC_STPT].stride != sizeof(NtAnmStepTrack) || p->sec[SEC_STPK].stride != sizeof(NtAnmStepKey)) {
        NT_LOG_WARN("activate_clip: a constant or step section has the wrong stride");
        return false;
    }
    return true;
}

/* The CNST entry a CONSTANT channel of comps components points at. */
static bool anm_validate_constant(const anm_parse_t *p, uint32_t index, uint32_t comps) {
    float v[4];
    rd_f32n(p->data + p->sec[SEC_CNST].offset + ((size_t)16U * index), v, 4);
    if (!skel_value_valid(v, comps)) {
        NT_LOG_WARN("activate_clip: constant entry %u is not finite, not unit or has a non-zero fourth component", index);
        return false;
    }
    return true;
}

/* The STPT track a STEP channel of comps components points at, with its keys.
 * keys_seen is the running key total: tracks partition STPK in channel order,
 * so every key is reached exactly once and no key is left unvalidated. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool anm_validate_step_track(const anm_parse_t *p, uint32_t index, uint32_t comps, uint32_t *keys_seen) {
    const uint8_t *track = p->data + p->sec[SEC_STPT].offset + ((size_t)8U * index);
    const uint32_t first = rd_u32(track);
    const uint32_t count = rd_u32(track + 4);
    if (count == 0) {
        NT_LOG_WARN("activate_clip: step track %u has no keys", index);
        return false;
    }
    if (first != *keys_seen) {
        NT_LOG_WARN("activate_clip: step track %u starts at key %u, expected %u", index, first, *keys_seen);
        return false;
    }
    /* Bounded before the keys are read; the partition total is only complete
     * once every track has been walked. */
    if ((uint64_t)first + count > (uint64_t)p->sec[SEC_STPK].count) {
        NT_LOG_WARN("activate_clip: step track %u spans keys [%u, %u) outside STPK", index, first, first + count);
        return false;
    }
    float previous = 0.0F;
    for (uint32_t k = 0; k < count; ++k) {
        const uint8_t *key = p->data + p->sec[SEC_STPK].offset + ((size_t)20U * (first + k));
        const float time = rd_f32(key);
        if (!skel_finite(time)) {
            NT_LOG_WARN("activate_clip: step track %u key %u has a non-finite time", index, k);
            return false;
        }
        const bool ordered = (k == 0) ? (time == 0.0F) : (time > previous);
        if (!ordered) {
            NT_LOG_WARN("activate_clip: step track %u key %u breaks the first-at-0, strictly increasing rule", index, k);
            return false;
        }
        float v[4];
        rd_f32n(key + 4, v, 4);
        if (!skel_value_valid(v, comps)) {
            NT_LOG_WARN("activate_clip: step track %u key %u is not finite, not unit or has a non-zero fourth component", index, k);
            return false;
        }
        previous = time;
    }
    if (previous > p->duration) {
        NT_LOG_WARN("activate_clip: step track %u ends past the clip duration", index);
        return false;
    }
    *keys_seen += count;
    return true;
}

/* Modes and indices of every channel, tallying what the runtime tables need.
 * A SAMPLED or STEP index must equal the running count of its table, so the
 * wire rows are in channel order and the decode is one linear pass. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool anm_validate_channels(anm_parse_t *p) {
    const anm_section_t *chan = &p->sec[SEC_CHAN];
    const uint32_t object_first = NT_ANM_OBJECT_CHANNEL(p->joint_count);
    uint32_t sampled[3] = {0, 0, 0};
    uint32_t joint_sampled[3] = {0, 0, 0};
    uint32_t joint_const[3] = {0, 0, 0};
    uint32_t joint_steps = 0;
    uint32_t steps = 0;
    uint32_t keys_seen = 0;
    for (uint32_t c = 0; c < p->channel_count; ++c) {
        const uint8_t *entry = p->data + chan->offset + ((size_t)4U * c);
        const uint8_t mode = entry[0];
        const uint32_t index = rd_u16(entry + 2);
        const uint32_t kind = c % 3U;
        const uint32_t comps = nt_anm_channel_comps(c);
        const bool object = c >= object_first;

        switch (mode) {
        case NT_ANM_CHANNEL_ABSENT:
            if (index != 0) {
                NT_LOG_WARN("activate_clip: absent channel %u carries index %u", c, index);
                return false;
            }
            break;
        case NT_ANM_CHANNEL_CONSTANT:
            if (index >= p->sec[SEC_CNST].count) {
                NT_LOG_WARN("activate_clip: channel %u names constant %u of %u", c, index, p->sec[SEC_CNST].count);
                return false;
            }
            if (!anm_validate_constant(p, index, comps)) {
                return false;
            }
            if (!object) {
                joint_const[kind]++;
            }
            break;
        case NT_ANM_CHANNEL_SAMPLED:
            if (index != sampled[kind]) {
                NT_LOG_WARN("activate_clip: channel %u names plane row %u, expected %u", c, index, sampled[kind]);
                return false;
            }
            sampled[kind]++;
            if (!object) {
                joint_sampled[kind]++;
            }
            break;
        case NT_ANM_CHANNEL_STEP:
            if (index != steps) {
                NT_LOG_WARN("activate_clip: channel %u names step track %u, expected %u", c, index, steps);
                return false;
            }
            if (index >= p->sec[SEC_STPT].count) {
                NT_LOG_WARN("activate_clip: channel %u names step track %u of %u", c, index, p->sec[SEC_STPT].count);
                return false;
            }
            if (!anm_validate_step_track(p, index, comps, &keys_seen)) {
                return false;
            }
            steps++;
            if (!object) {
                joint_steps++;
            }
            break;
        default:
            NT_LOG_WARN("activate_clip: channel %u has unknown mode %u", c, (unsigned)mode);
            return false;
        }
        if (object) {
            p->obj_mode[kind] = mode;
            p->obj_index[kind] = (uint16_t)index;
        }
    }

    for (uint32_t kind = 0; kind < 3; ++kind) {
        if (p->sec[SEC_PLNT + kind].count != sampled[kind]) {
            NT_LOG_WARN("activate_clip: plane %u holds %u rows, but %u channels are sampled", kind, p->sec[SEC_PLNT + kind].count, sampled[kind]);
            return false;
        }
    }
    if (p->sec[SEC_STPT].count != steps) {
        NT_LOG_WARN("activate_clip: STPT holds %u tracks, but %u channels step", p->sec[SEC_STPT].count, steps);
        return false;
    }
    if (keys_seen != p->sec[SEC_STPK].count) {
        NT_LOG_WARN("activate_clip: step tracks cover %u of the %u STPK keys", keys_seen, p->sec[SEC_STPK].count);
        return false;
    }

    for (uint32_t kind = 0; kind < 3; ++kind) {
        p->n[kind] = (uint16_t)joint_sampled[kind];
        p->n_const[kind] = (uint16_t)joint_const[kind];
    }
    p->n_steps = joint_steps;
    return true;
}

/* The sample_count rules and the sample values themselves. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool anm_validate_planes(const anm_parse_t *p) {
    for (uint32_t kind = 0; kind < 3; ++kind) {
        const anm_section_t *plane = &p->sec[SEC_PLNT + kind];
        const uint32_t comps = (kind == 1U) ? 4U : 3U;
        if (plane->count == 0) {
            continue;
        }
        if (p->sample_count < 2U || !(p->duration > 0.0F)) {
            NT_LOG_WARN("activate_clip: plane %u has rows, which needs at least two samples over a positive duration", kind);
            return false;
        }
        for (uint32_t row = 0; row < plane->count; ++row) {
            const uint8_t *samples = p->data + plane->offset + ((size_t)plane->stride * row);
            for (uint32_t i = 0; i < p->sample_count; ++i) {
                float v[4];
                rd_f32n(samples + ((size_t)4U * comps * i), v, comps);
                if (!skel_finite_n(v, comps) || (comps == 4U && !skel_unit_quat(v))) {
                    NT_LOG_WARN("activate_clip: plane %u row %u sample %u is not finite or not a unit quaternion", kind, row, i);
                    return false;
                }
            }
        }
    }
    return true;
}
// #endregion

// #region NANM decode
typedef struct {
    uint64_t blocks;
    uint64_t ct, cq, cs;
    uint64_t steps;
    uint64_t step_times, step_values;
    uint64_t object;
    uint64_t t_joint, q_joint, s_joint;
    uint64_t ct_joint, cq_joint, cs_joint;
    uint64_t total;
    uint32_t block_floats;
    bool object_sampled;
} anm_layout_t;

static void anm_plan_layout(const anm_parse_t *p, anm_layout_t *l) {
    l->block_floats = (3U * (uint32_t)p->n[0]) + (4U * (uint32_t)p->n[1]) + (3U * (uint32_t)p->n[2]);
    l->object_sampled = p->obj_mode[0] == NT_ANM_CHANNEL_SAMPLED || p->obj_mode[1] == NT_ANM_CHANNEL_SAMPLED || p->obj_mode[2] == NT_ANM_CHANNEL_SAMPLED;

    uint64_t cursor = 0;
    l->blocks = skel_reserve(&cursor, (uint64_t)p->sample_count * l->block_floats * 4U);
    l->ct = skel_reserve(&cursor, (uint64_t)p->n_const[0] * 3U * 4U);
    l->cq = skel_reserve(&cursor, (uint64_t)p->n_const[1] * 4U * 4U);
    l->cs = skel_reserve(&cursor, (uint64_t)p->n_const[2] * 3U * 4U);
    l->steps = skel_reserve(&cursor, (uint64_t)p->n_steps * sizeof(nt_skeletal_step_t));
    l->step_times = skel_reserve(&cursor, (uint64_t)p->sec[SEC_STPK].count * 4U);
    l->step_values = skel_reserve(&cursor, (uint64_t)p->sec[SEC_STPK].count * 16U);
    l->object = skel_reserve(&cursor, l->object_sampled ? (uint64_t)p->sample_count * sizeof(nt_skeletal_trs_t) : 0U);
    l->t_joint = skel_reserve(&cursor, (uint64_t)p->n[0] * 2U);
    l->q_joint = skel_reserve(&cursor, (uint64_t)p->n[1] * 2U);
    l->s_joint = skel_reserve(&cursor, (uint64_t)p->n[2] * 2U);
    l->ct_joint = skel_reserve(&cursor, (uint64_t)p->n_const[0] * 2U);
    l->cq_joint = skel_reserve(&cursor, (uint64_t)p->n_const[1] * 2U);
    l->cs_joint = skel_reserve(&cursor, (uint64_t)p->n_const[2] * 2U);
    /* A clip whose tables are all empty still owns one byte, so a live pool slot
     * is exactly a non-NULL allocation. */
    l->total = (cursor == 0) ? 1U : cursor;
}

/* Wire planes are channel-major time series; the runtime blocks are per-sample,
 * so this transposes each row into its slot of every frame block. */
static void anm_decode_blocks(const anm_parse_t *p, const anm_layout_t *l, void *mem) {
    float *blocks = (float *)skel_at(mem, l->blocks);
    uint16_t *joints[3] = {(uint16_t *)skel_at(mem, l->t_joint), (uint16_t *)skel_at(mem, l->q_joint), (uint16_t *)skel_at(mem, l->s_joint)};
    const uint32_t kind_offset[3] = {0U, 3U * (uint32_t)p->n[0], (3U * (uint32_t)p->n[0]) + (4U * (uint32_t)p->n[1])};
    const uint32_t object_first = NT_ANM_OBJECT_CHANNEL(p->joint_count);

    for (uint32_t kind = 0; kind < 3; ++kind) {
        const anm_section_t *plane = &p->sec[SEC_PLNT + kind];
        const uint32_t comps = (kind == 1U) ? 4U : 3U;
        uint32_t row = 0;
        for (uint32_t c = kind; c < object_first; c += 3U) {
            const uint8_t *entry = p->data + p->sec[SEC_CHAN].offset + ((size_t)4U * c);
            if (entry[0] != NT_ANM_CHANNEL_SAMPLED) {
                continue;
            }
            const uint8_t *samples = p->data + plane->offset + ((size_t)plane->stride * rd_u16(entry + 2));
            joints[kind][row] = (uint16_t)(c / 3U);
            for (uint32_t i = 0; i < p->sample_count; ++i) {
                rd_f32n(samples + ((size_t)4U * comps * i), blocks + ((size_t)i * l->block_floats) + kind_offset[kind] + ((size_t)comps * row), comps);
            }
            ++row;
        }
    }
}

/* Constants split by component kind, and one nt_skeletal_step_t per joint STEP
 * channel into the shared key tables. */
static void anm_decode_constants_and_steps(const anm_parse_t *p, const anm_layout_t *l, void *mem) {
    float *values[3] = {(float *)skel_at(mem, l->ct), (float *)skel_at(mem, l->cq), (float *)skel_at(mem, l->cs)};
    uint16_t *joints[3] = {(uint16_t *)skel_at(mem, l->ct_joint), (uint16_t *)skel_at(mem, l->cq_joint), (uint16_t *)skel_at(mem, l->cs_joint)};
    nt_skeletal_step_t *steps = (nt_skeletal_step_t *)skel_at(mem, l->steps);
    uint32_t written[3] = {0, 0, 0};
    uint32_t step = 0;

    const uint32_t object_first = NT_ANM_OBJECT_CHANNEL(p->joint_count);
    for (uint32_t c = 0; c < object_first; ++c) {
        const uint8_t *entry = p->data + p->sec[SEC_CHAN].offset + ((size_t)4U * c);
        const uint32_t kind = c % 3U;
        const uint32_t comps = nt_anm_channel_comps(c);
        const uint32_t index = rd_u16(entry + 2);
        if (entry[0] == NT_ANM_CHANNEL_CONSTANT) {
            rd_f32n(p->data + p->sec[SEC_CNST].offset + ((size_t)16U * index), values[kind] + ((size_t)comps * written[kind]), comps);
            joints[kind][written[kind]] = (uint16_t)(c / 3U);
            written[kind]++;
        } else if (entry[0] == NT_ANM_CHANNEL_STEP) {
            const uint8_t *track = p->data + p->sec[SEC_STPT].offset + ((size_t)8U * index);
            steps[step].first = rd_u32(track);
            steps[step].count = rd_u32(track + 4);
            steps[step].joint = (uint16_t)(c / 3U);
            steps[step].channel = (uint8_t)kind;
            ++step;
        }
    }

    float *times = (float *)skel_at(mem, l->step_times);
    float *step_values = (float *)skel_at(mem, l->step_values);
    for (uint32_t k = 0; k < p->sec[SEC_STPK].count; ++k) {
        const uint8_t *key = p->data + p->sec[SEC_STPK].offset + ((size_t)20U * k);
        times[k] = rd_f32(key);
        rd_f32n(key + 4, step_values + ((size_t)4U * k), 4);
    }
}

/* Component kind 0/1/2 of one TRS: translation, rotation, scale. */
static float *trs_component(nt_skeletal_trs_t *trs, uint32_t kind) {
    if (kind == 0U) {
        return trs->t;
    }
    return (kind == 1U) ? trs->q : trs->s;
}

/* The object curve shares the clip's grid and key tables but keeps its own
 * modes, constant value, sampled array and key ranges (§7.5). */
static void anm_decode_object(const anm_parse_t *p, const anm_layout_t *l, void *mem, double inv_step, nt_skeletal_object_curve_t *out) {
    nt_skeletal_trs_t *sampled = l->object_sampled ? (nt_skeletal_trs_t *)skel_at(mem, l->object) : NULL;
    out->sampled = sampled;
    out->step_times = (p->sec[SEC_STPK].count != 0) ? (const float *)skel_at(mem, l->step_times) : NULL;
    out->step_values = (p->sec[SEC_STPK].count != 0) ? (const float *)skel_at(mem, l->step_values) : NULL;
    out->duration = (double)p->duration;
    out->inv_step = inv_step;
    out->sample_count = p->sample_count;

    for (uint32_t kind = 0; kind < 3; ++kind) {
        const uint32_t c = NT_ANM_OBJECT_CHANNEL(p->joint_count) + kind;
        const uint32_t comps = nt_anm_channel_comps(c);
        const uint32_t index = p->obj_index[kind];
        out->mode[kind] = p->obj_mode[kind];

        if (p->obj_mode[kind] == NT_ANM_CHANNEL_CONSTANT) {
            rd_f32n(p->data + p->sec[SEC_CNST].offset + ((size_t)16U * index), trs_component(&out->constant, kind), comps);
        } else if (p->obj_mode[kind] == NT_ANM_CHANNEL_SAMPLED) {
            const anm_section_t *plane = &p->sec[SEC_PLNT + kind];
            const uint8_t *row = p->data + plane->offset + ((size_t)plane->stride * index);
            for (uint32_t i = 0; i < p->sample_count; ++i) {
                rd_f32n(row + ((size_t)4U * comps * i), trs_component(&sampled[i], kind), comps);
            }
        } else if (p->obj_mode[kind] == NT_ANM_CHANNEL_STEP) {
            const uint8_t *track = p->data + p->sec[SEC_STPT].offset + ((size_t)8U * index);
            out->step_first[kind] = rd_u32(track);
            out->step_count[kind] = rd_u32(track + 4);
        }
    }
}

/* The view over the decoded allocation: a table the clip does not use points at
 * its empty region, except the ones the sampler tests for NULL. */
static nt_skeletal_clip_t anm_make_view(const anm_parse_t *p_in, const anm_layout_t *layout, void *mem) {
    const anm_parse_t p = *p_in;
    const anm_layout_t l = *layout;

    /* The grid step is exact only in double, and a clip with one sample or no
     * duration has no interval to step through. */
    const double inv_step = (p.sample_count > 1U && p.duration > 0.0F) ? ((double)(p.sample_count - 1U) / (double)p.duration) : 0.0;

    nt_skeletal_clip_t view = {
        .rig_compat_id = (nt_hash64_t){.value = p.rig_compat_id},
        .additive_ref_id = (nt_hash64_t){.value = p.additive_ref_id},
        .duration = (double)p.duration,
        .inv_step = inv_step,
        .blocks = (l.block_floats != 0U) ? (const float *)skel_at(mem, l.blocks) : NULL,
        .t_joint = (const uint16_t *)skel_at(mem, l.t_joint),
        .q_joint = (const uint16_t *)skel_at(mem, l.q_joint),
        .s_joint = (const uint16_t *)skel_at(mem, l.s_joint),
        .ct_joint = (const uint16_t *)skel_at(mem, l.ct_joint),
        .ct = (const float *)skel_at(mem, l.ct),
        .cq_joint = (const uint16_t *)skel_at(mem, l.cq_joint),
        .cq = (const float *)skel_at(mem, l.cq),
        .cs_joint = (const uint16_t *)skel_at(mem, l.cs_joint),
        .cs = (const float *)skel_at(mem, l.cs),
        .steps = (p.n_steps != 0U) ? (const nt_skeletal_step_t *)skel_at(mem, l.steps) : NULL,
        .step_times = (p.sec[SEC_STPK].count != 0U) ? (const float *)skel_at(mem, l.step_times) : NULL,
        .step_values = (p.sec[SEC_STPK].count != 0U) ? (const float *)skel_at(mem, l.step_values) : NULL,
        .sample_count = p.sample_count,
        .block_floats = l.block_floats,
        .n_steps = p.n_steps,
        .r_joints = p.r_joints,
        .r_root = p.r_root,
        .s_max = p.s_max,
        .bake_fps_min = p.bake_fps_min,
        .bake_reach = p.bake_reach,
        .joint_count = p.joint_count,
        .n_t = p.n[0],
        .n_q = p.n[1],
        .n_s = p.n[2],
        .n_ct = p.n_const[0],
        .n_cq = p.n_const[1],
        .n_cs = p.n_const[2],
        .kind = p.kind,
    };
    anm_decode_object(&p, &l, mem, inv_step, &view.object);
    return view;
}

uint32_t nt_skeletal_assets_activate_clip(const uint8_t *data, uint32_t size) {
    NT_ASSERT(s_assets.initialized && "nt_skeletal_assets_init must run before the skeletal activators");

    anm_parse_t p = {0};
    if (!anm_validate_header(data, size, &p) || !anm_validate_sections(&p) || !anm_validate_strides(&p) || !anm_validate_channels(&p) || !anm_validate_planes(&p)) {
        return 0;
    }

    uint16_t slot = 0;
    while (slot < s_assets.max_clips && s_assets.clips[slot].mem != NULL) {
        ++slot;
    }
    if (slot == s_assets.max_clips) {
        NT_LOG_WARN("activate_clip: clip pool of %u is full", (unsigned)s_assets.max_clips);
        NT_ASSERT(false && "clip pool exhausted -- raise nt_skeletal_assets_desc_t.max_clips");
        return 0;
    }

    anm_layout_t layout = {0};
    anm_plan_layout(&p, &layout);
    if (layout.total > UINT32_MAX) {
        NT_LOG_WARN("activate_clip: decoded clip needs more than 4 GB");
        return 0;
    }
    void *mem = malloc((size_t)layout.total);
    if (mem == NULL) {
        NT_LOG_WARN("activate_clip: allocation of %u bytes failed", (unsigned)layout.total);
        return 0;
    }
    memset(mem, 0, (size_t)layout.total);

    anm_decode_blocks(&p, &layout, mem);
    anm_decode_constants_and_steps(&p, &layout, mem);

    s_assets.clips[slot].mem = mem;
    s_assets.clips[slot].view = anm_make_view(&p, &layout, mem);
    return (uint32_t)slot + 1U;
}

void nt_skeletal_assets_deactivate_clip(uint32_t runtime_handle) {
    NT_ASSERT(s_assets.initialized && "nt_skeletal_assets_shutdown ran before the clip deactivator");
    NT_ASSERT(runtime_handle != 0 && runtime_handle <= s_assets.max_clips && "clip handle out of range");
    nt_anm_slot_t *slot = &s_assets.clips[runtime_handle - 1U];
    NT_ASSERT(slot->mem != NULL && "clip slot is already free");
    free(slot->mem);
    slot->mem = NULL;
    slot->view = (nt_skeletal_clip_t){0};
}
// #endregion

// #region lifecycle and views
nt_result_t nt_skeletal_assets_init(const nt_skeletal_assets_desc_t *desc) {
    NT_ASSERT(!s_assets.initialized && "nt_skeletal_assets_init called twice");
    const nt_skeletal_assets_desc_t d = (desc != NULL) ? *desc : nt_skeletal_assets_desc_defaults();

    s_assets.skeletons = (d.max_skeletons != 0U) ? (nt_skl_slot_t *)calloc(d.max_skeletons, sizeof(nt_skl_slot_t)) : NULL;
    s_assets.bindings = (d.max_skin_bindings != 0U) ? (nt_skn_slot_t *)calloc(d.max_skin_bindings, sizeof(nt_skn_slot_t)) : NULL;
    s_assets.clips = (d.max_clips != 0U) ? (nt_anm_slot_t *)calloc(d.max_clips, sizeof(nt_anm_slot_t)) : NULL;
    if ((d.max_skeletons != 0U && s_assets.skeletons == NULL) || (d.max_skin_bindings != 0U && s_assets.bindings == NULL) || (d.max_clips != 0U && s_assets.clips == NULL)) {
        free(s_assets.skeletons);
        free(s_assets.bindings);
        free(s_assets.clips);
        s_assets.skeletons = NULL;
        s_assets.bindings = NULL;
        s_assets.clips = NULL;
        NT_LOG_WARN("nt_skeletal_assets_init: pool allocation failed");
        return NT_ERR_INIT_FAILED;
    }

    s_assets.max_skeletons = d.max_skeletons;
    s_assets.max_bindings = d.max_skin_bindings;
    s_assets.max_clips = d.max_clips;
    s_assets.initialized = true;
    return NT_OK;
}

void nt_skeletal_assets_shutdown(void) {
    for (uint16_t i = 0; i < s_assets.max_skeletons; ++i) {
        free(s_assets.skeletons[i].mem);
    }
    for (uint16_t i = 0; i < s_assets.max_bindings; ++i) {
        free(s_assets.bindings[i].mem);
    }
    for (uint16_t i = 0; i < s_assets.max_clips; ++i) {
        free(s_assets.clips[i].mem);
    }
    free(s_assets.skeletons);
    free(s_assets.bindings);
    free(s_assets.clips);
    memset(&s_assets, 0, sizeof(s_assets));
}

const nt_skeletal_skeleton_t *nt_skeletal_assets_skeleton(nt_resource_t skeleton) {
    NT_ASSERT(nt_resource_get_asset_type(skeleton) == NT_ASSET_SKELETON && "nt_skeletal_assets_skeleton: handle is not a skeleton resource");
    const uint32_t handle = nt_resource_get(skeleton);
    NT_ASSERT(handle != 0 && handle <= s_assets.max_skeletons && s_assets.skeletons[handle - 1U].mem != NULL && "nt_skeletal_assets_skeleton on an unready skeleton");
    return &s_assets.skeletons[handle - 1U].view;
}

const nt_skin_binding_t *nt_skeletal_assets_skin_binding(nt_resource_t binding) {
    NT_ASSERT(nt_resource_get_asset_type(binding) == NT_ASSET_SKIN_BINDING && "nt_skeletal_assets_skin_binding: handle is not a skin binding resource");
    const uint32_t handle = nt_resource_get(binding);
    NT_ASSERT(handle != 0 && handle <= s_assets.max_bindings && s_assets.bindings[handle - 1U].mem != NULL && "nt_skeletal_assets_skin_binding on an unready binding");
    return &s_assets.bindings[handle - 1U].view;
}

const nt_skeletal_clip_t *nt_skeletal_assets_clip(nt_resource_t clip) {
    NT_ASSERT(nt_resource_get_asset_type(clip) == NT_ASSET_CLIP && "nt_skeletal_assets_clip: handle is not a clip resource");
    const uint32_t handle = nt_resource_get(clip);
    NT_ASSERT(handle != 0 && handle <= s_assets.max_clips && s_assets.clips[handle - 1U].mem != NULL && "nt_skeletal_assets_clip on an unready clip");
    return &s_assets.clips[handle - 1U].view;
}
// #endregion
