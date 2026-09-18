#ifndef NT_SKELETAL_FORMAT_H
#define NT_SKELETAL_FORMAT_H

#include <stdint.h>

/*
 * Skeletal wire formats (NSKL skeleton, NSKN skin binding, NANM clip) --
 * shared between builder (native) and runtime (WASM).
 *
 * The wire layout IS the runtime layout: the builder writes the tables the
 * sampler reads, and an activator validates the structure, copies the whole
 * payload into one allocation and points its runtime view at it. Only the three
 * headers below travel as structs; every array after a header is bytes the two
 * sides agree on. All fields are little-endian, which every target of this
 * engine is, so headers and arrays are memcpy'd rather than composed byte by
 * byte. See docs/spec/skeletal/skeletal-animation.md §16.
 */

/* FourCC read as a little-endian uint32_t, like NT_PACK_MAGIC. */
#define NT_SKELETAL_FOURCC(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

#define NT_SKL_MAGIC NT_SKELETAL_FOURCC('N', 'S', 'K', 'L')
#define NT_SKN_MAGIC NT_SKELETAL_FOURCC('N', 'S', 'K', 'N')
#define NT_ANM_MAGIC NT_SKELETAL_FOURCC('N', 'A', 'N', 'M')

/* One version for all three payloads, compared exactly: the layout is the
 * runtime layout, so any change to it is a rebuild. */
#define NT_SKELETAL_FORMAT_VERSION 3

// #region NSKL skeleton
/*
 * NtSklHeader (16 bytes), then in joint index order (J = joint_count):
 *   uint16_t parent[J]       NT_SKELETAL_NO_PARENT (0xFFFF) = root, else < j
 *   uint16_t subtree_end[J]  subtree of j = [j, subtree_end[j])
 *   uint32_t joint_id[J]     stable id, nt_hash32_str(node name)
 *   float    rest[J][10]     local rest pose AoS: t[3] q[4] s[3], q unit xyzw
 *
 * Payload size is exactly NT_SKL_SIZE(joint_count). rig_compat_id is content
 * identity computed by nt_skeletal_rig_compat_id in the encoder.
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;         /* 0:  NT_SKL_MAGIC */
    uint16_t version;       /* 4:  NT_SKELETAL_FORMAT_VERSION */
    uint16_t joint_count;   /* 6:  >= 1 */
    uint64_t rig_compat_id; /* 8:  nt_skeletal_rig_compat_id of this rig */
} NtSklHeader;
#pragma pack(pop)

/* 16 header bytes + 2 + 2 + 4 + 40 per joint; 64-bit like nt_anm_size so all
 * three sizes compare the same way. */
#define NT_SKL_SIZE(joint_count) (16ULL + (48ULL * (uint64_t)(joint_count)))
// #endregion

// #region NSKN skin binding
/*
 * NtSknHeader (24 bytes), then in palette index order (P = palette_count):
 *   float    inverse_bind[P][12]   nt_skeletal_mat34_t row order r[3][4]
 *   uint16_t remap[P]              palette entry p -> skeleton joint
 *
 * Payload size is exactly NT_SKN_SIZE(palette_count). remap[p] is not bounded
 * against a skeleton here -- no skeleton is available at activation;
 * nt_skin_palette_build asserts remap[p] < model_count where both exist.
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;         /* 0:  NT_SKN_MAGIC */
    uint16_t version;       /* 4:  NT_SKELETAL_FORMAT_VERSION */
    uint16_t palette_count; /* 6:  >= 1 */
    uint64_t rig_compat_id; /* 8:  rig this binding is valid with */
    float reach;            /* 16: joint space (spec 3.4); skeleton space only through the 14 stretch */
    float any_pose_radius;  /* 20: skeleton space (spec 14) */
} NtSknHeader;
#pragma pack(pop)

/* 24 header bytes + 48 + 2 per palette entry. */
#define NT_SKN_SIZE(palette_count) (24ULL + (50ULL * (uint64_t)(palette_count)))
// #endregion

// #region NANM clip
/* Strides of the two struct arrays; the runtime structs they hold are
 * nt_skeletal_step_t and nt_skeletal_step_key_t, pinned against these in
 * engine/skeletal_assets. */
#define NT_ANM_STEP_STRIDE 12
#define NT_ANM_KEY_STRIDE 20

/*
 * NtAnmHeader (56 bytes), then, each array a multiple of 4 bytes so the next
 * one stays aligned and the uint16 tables need only the 2 they end on:
 *
 *   float    blocks[sample_count][block_floats]   block_floats = 3n_t+4n_q+3n_s
 *   float    ct[n_ct][3], cq[n_cq][4], cs[n_cs][3]
 *   struct   steps[n_steps]                       NT_ANM_STEP_STRIDE bytes each
 *   struct   keys[n_keys]                         NT_ANM_KEY_STRIDE bytes each
 *   struct   object                               NtAnmObject, if nt_anm_has_object
 *   float    object_sampled[sample_count][10]     only if nt_anm_object_sampled
 *   uint16_t t_joint[n_t], q_joint[n_q], s_joint[n_s]
 *   uint16_t ct_joint[n_ct], cq_joint[n_cq], cs_joint[n_cs]
 *
 * Payload size is exactly nt_anm_size(header). Sampled channels share one
 * uniform grid of sample_count samples over [0, duration]; STEP channels keep
 * their authored timestamps in keys. The joint step tracks partition keys in
 * table order, followed by the object STEP channels in t, q, s order.
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;           /* 0:   NT_ANM_MAGIC */
    uint16_t version;         /* 4:   NT_SKELETAL_FORMAT_VERSION */
    uint16_t joint_count;     /* 6:   >= 1, must match the skeleton at bind time */
    uint32_t sample_count;    /* 8:   samples on the uniform grid, >= 1 */
    float duration;           /* 12:  seconds, finite and >= 0 */
    uint64_t rig_compat_id;   /* 16:  rig this clip plays on */
    uint64_t additive_ref_id; /* 24:  reference pose identity, 0 = absolute */
    uint16_t n_t;             /* 32:  sampled joint rows per component kind */
    uint16_t n_q;             /* 34 */
    uint16_t n_s;             /* 36 */
    uint16_t n_ct;            /* 38:  constant joint channels per component kind */
    uint16_t n_cq;            /* 40 */
    uint16_t n_cs;            /* 42 */
    uint32_t n_steps;         /* 44:  joint STEP tracks */
    uint32_t n_keys;          /* 48:  keys of every STEP track, joints and object */
    uint8_t object_mode[3];   /* 52:  nt_skeletal_channel_mode_t per channel: t, q, s */
    uint8_t _pad;             /* 55:  zero, keeps the arrays after the header 4-aligned */
} NtAnmHeader;
#pragma pack(pop)

/* The object curve's values and key ranges, in the payload only when the curve
 * exists; the modes stay in the header because they decide that. */
#pragma pack(push, 1)
typedef struct {
    float constant[10];     /* 0:  t[3] q[4] s[3], only the CONSTANT channels used */
    uint32_t step_first[3]; /* 40: per channel: first key, 0 unless the mode is STEP */
    uint32_t step_count[3]; /* 52: per channel: keys, 0 unless the mode is STEP */
} NtAnmObject;
#pragma pack(pop)

/* Mode 0 is NT_SKELETAL_CHANNEL_ABSENT: a curve with no driven channel needs no
 * record at all. */
static inline int nt_anm_has_object(const NtAnmHeader *header) { return (header->object_mode[0] != 0U) || (header->object_mode[1] != 0U) || (header->object_mode[2] != 0U); }

/* The object curve carries a sampled array exactly when one of its channels is
 * sampled; mode 2 is NT_SKELETAL_CHANNEL_SAMPLED. */
static inline int nt_anm_object_sampled(const NtAnmHeader *header) { return (header->object_mode[0] == 2U) || (header->object_mode[1] == 2U) || (header->object_mode[2] == 2U); }

/* Exact payload size of a clip with these counts, in 64 bits: a corrupt header
 * must not wrap the size a consumer compares against. */
static inline uint64_t nt_anm_size(const NtAnmHeader *header) {
    const uint64_t block_floats = (3ULL * header->n_t) + (4ULL * header->n_q) + (3ULL * header->n_s);
    uint64_t size = sizeof(NtAnmHeader);
    size += (uint64_t)header->sample_count * block_floats * 4ULL;
    size += ((3ULL * header->n_ct) + (4ULL * header->n_cq) + (3ULL * header->n_cs)) * 4ULL;
    size += (uint64_t)header->n_steps * NT_ANM_STEP_STRIDE;
    size += (uint64_t)header->n_keys * NT_ANM_KEY_STRIDE;
    if (nt_anm_has_object(header)) {
        size += sizeof(NtAnmObject);
    }
    if (nt_anm_object_sampled(header)) {
        size += (uint64_t)header->sample_count * 10ULL * 4ULL;
    }
    size += 2ULL * ((uint64_t)header->n_t + header->n_q + header->n_s + header->n_ct + header->n_cq + header->n_cs);
    return size;
}
// #endregion

/* C++ spells these differently and GCC rejects the C keyword there; the wire
 * layout is pinned by the C build every consumer shares. */
#ifndef __cplusplus
_Static_assert(sizeof(NtSklHeader) == 16, "NtSklHeader must be 16 bytes");
_Static_assert(sizeof(NtSknHeader) == 24, "NtSknHeader must be 24 bytes");
_Static_assert(sizeof(NtAnmHeader) == 56, "NtAnmHeader must be 56 bytes");
_Static_assert(sizeof(NtAnmObject) == 64, "NtAnmObject must be 64 bytes");
#endif

#endif /* NT_SKELETAL_FORMAT_H */
