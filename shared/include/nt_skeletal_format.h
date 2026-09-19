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
 * byte. See docs/spec/skeletal/skeletal-animation.md, Builder, codec, wire formats.
 */

/* FourCC read as a little-endian uint32_t, like NT_PACK_MAGIC. */
#define NT_SKELETAL_FOURCC(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

#define NT_SKL_MAGIC NT_SKELETAL_FOURCC('N', 'S', 'K', 'L')
#define NT_SKN_MAGIC NT_SKELETAL_FOURCC('N', 'S', 'K', 'N')
#define NT_ANM_MAGIC NT_SKELETAL_FOURCC('N', 'A', 'N', 'M')

/* One version for all three payloads, compared exactly: the layout is the
 * runtime layout, so any change to it is a rebuild. */
#define NT_SKELETAL_FORMAT_VERSION 5

// #region NSKL skeleton
/*
 * NtSklHeader (16 bytes), then in joint index order (J = joint_count):
 *   uint16_t parent[J]       NT_SKELETAL_NO_PARENT (0xFFFF) = root, else < j
 *   uint16_t subtree_end[J]  subtree of j = [j, subtree_end[j])
 *   uint32_t joint_id[J]     stable id, nt_hash32_str(node name)
 *   float    rest[J][10]     local rest pose AoS: t[3] q[4] s[3], q unit xyzw
 *
 * Payload size is exactly NT_SKL_SIZE(joint_count). rig_compat_id is
 * nt_skeletal_rig_compat_id of this rig, filled by the producer; the encoder
 * writes it as given.
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
    float reach;            /* 16: joint space; a skeleton-space radius needs the chain stretch */
    float any_pose_radius;  /* 20: skeleton space */
} NtSknHeader;
#pragma pack(pop)

/* 24 header bytes + 48 + 2 per palette entry. */
#define NT_SKN_SIZE(palette_count) (24ULL + (50ULL * (uint64_t)(palette_count)))
// #endregion

// #region NANM clip
/*
 * NtAnmHeader (44 bytes), then, each array a multiple of 4 bytes so the next
 * one stays aligned and the uint16 tables need only the 2 they end on:
 *
 *   float    base[joint_count][10]                 local pose AoS t[3] q[4] s[3]
 *   float    blocks[sample_count][stride]           stride = 3n_t + 4n_q + 3n_s
 *   uint16_t t_joint[n_t], q_joint[n_q], s_joint[n_s]
 *
 * Payload size is exactly nt_anm_size(header). base is the rig's rest pose
 * with every constant channel written in: a sample starts as a copy of it and
 * the sampled rows overwrite their joint components. Sampled rows share one
 * uniform grid of sample_count samples over [0, duration]; the runtime holds no
 * authored keys, so a stepped source is evaluated onto the grid by the builder.
 * A clip with no row ships sample_count 1 and may still carry a duration.
 *
 * The three bounds are the builder's measurements over the clip's own poses
 * (skeletal spec, Bounds and culling); the encoder writes what it is given.
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;         /* 0:   NT_ANM_MAGIC */
    uint16_t version;       /* 4:   NT_SKELETAL_FORMAT_VERSION */
    uint16_t joint_count;   /* 6:   >= 1, must match the skeleton at bind time */
    uint32_t sample_count;  /* 8:   samples on the uniform grid, >= 1; >= 2 when any row exists */
    float duration;         /* 12:  seconds, finite and >= 0; > 0 when any row exists */
    uint64_t rig_compat_id; /* 16:  rig this clip plays on */
    float r_joints;         /* 24:  skeleton space, finite and >= 0 */
    float r_root;           /* 28:  skeleton space, finite and >= 0 */
    float s_max;            /* 32:  unitless, finite and >= 0 */
    uint16_t n_t;           /* 36:  sampled joint rows per component kind */
    uint16_t n_q;           /* 38 */
    uint16_t n_s;           /* 40 */
    uint16_t _pad;          /* 42:  zero */
} NtAnmHeader;
#pragma pack(pop)

/* Exact payload size of a clip with these counts, in 64 bits: a corrupt header
 * must not wrap the size a consumer compares against. */
static inline uint64_t nt_anm_size(const NtAnmHeader *header) {
    const uint64_t stride = (3ULL * header->n_t) + (4ULL * header->n_q) + (3ULL * header->n_s);
    uint64_t size = sizeof(NtAnmHeader);
    size += 40ULL * header->joint_count;
    size += (uint64_t)header->sample_count * stride * 4ULL;
    size += 2ULL * ((uint64_t)header->n_t + header->n_q + header->n_s);
    return size;
}
// #endregion

/* C++ spells these differently and GCC rejects the C keyword there; the wire
 * layout is pinned by the C build every consumer shares. */
#ifndef __cplusplus
_Static_assert(sizeof(NtSklHeader) == 16, "NtSklHeader must be 16 bytes");
_Static_assert(sizeof(NtSknHeader) == 24, "NtSknHeader must be 24 bytes");
_Static_assert(sizeof(NtAnmHeader) == 44, "NtAnmHeader must be 44 bytes");
#endif

#endif /* NT_SKELETAL_FORMAT_H */
