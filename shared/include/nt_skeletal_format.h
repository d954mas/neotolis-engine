#ifndef NT_SKELETAL_FORMAT_H
#define NT_SKELETAL_FORMAT_H

#include <stdint.h>

/*
 * Skeletal wire formats (NSKL skeleton, NSKN skin binding, NANM clip) --
 * shared between builder (native) and runtime (WASM).
 *
 * All multi-byte fields are little-endian. The wire layout is NOT the runtime
 * layout: consumers never cast pack bytes to these structs or to runtime
 * structs, they memcpy fields and arrays out after validating counts, offsets
 * and ranges. See docs/spec/skeletal/skeletal-animation.md for the canonical
 * description.
 */

/* FourCC read as a little-endian uint32_t, like NT_PACK_MAGIC. */
#define NT_SKELETAL_FOURCC(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

#define NT_SKL_MAGIC NT_SKELETAL_FOURCC('N', 'S', 'K', 'L')
#define NT_SKN_MAGIC NT_SKELETAL_FOURCC('N', 'S', 'K', 'N')
#define NT_ANM_MAGIC NT_SKELETAL_FOURCC('N', 'A', 'N', 'M')

/* version = major << 8 | minor, shared by all three payloads. A major mismatch
 * rejects the payload. A higher minor stays readable because the only
 * minor-compatible change is a new optional NANM section, and unknown section
 * tags are ignored; nothing that changes decode semantics is a minor bump. */
#define NT_SKELETAL_FORMAT_VERSION 0x0100 /* major 1, minor 0 */
#define NT_SKELETAL_FORMAT_MAJOR(version) ((uint16_t)((uint16_t)(version) >> 8))

// #region NSKL skeleton
/*
 * NtSklHeader -- fixed layout, no sections.
 *
 * Layout (16 bytes):
 *   magic(4) + version(2) + joint_count(2) + rig_compat_id(8)
 *
 * After the header, in this order (joint index order, J = joint_count):
 *   uint16_t parent[J]       NT_SKELETAL_NO_PARENT (0xFFFF) = root, else < j
 *   uint16_t subtree_end[J]  subtree of j = [j, subtree_end[j])
 *   uint32_t joint_id[J]     stable id, nt_hash32_str(node name)
 *   float    rest[J][10]     local rest pose AoS: t[3] q[4] s[3], q unit xyzw
 *
 * Total payload size is exactly NT_SKL_SIZE(joint_count).
 * rig_compat_id is content identity produced by nt_skeletal_rig_compat_id and
 * is carried through as-is, never recomputed from the decoded arrays.
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;         /* 0:  NT_SKL_MAGIC */
    uint16_t version;       /* 4:  NT_SKELETAL_FORMAT_VERSION */
    uint16_t joint_count;   /* 6:  >= 1 */
    uint64_t rig_compat_id; /* 8:  nt_skeletal_rig_compat_id of this rig */
} NtSklHeader;
#pragma pack(pop)

/* 16 header bytes + 2 + 2 + 4 + 40 per joint */
#define NT_SKL_SIZE(joint_count) (16U + (48U * (uint32_t)(joint_count)))
// #endregion

// #region NSKN skin binding
/* Mesh-space convention of the inverse bind matrices. glTF mesh-node space is
 * the only value v1 accepts; the byte exists so a retargeted binding is
 * rejected instead of silently mis-skinned. */
#define NT_SKN_MESH_SPACE_GLTF_NODE 0

/*
 * NtSknHeader -- fixed layout, no sections.
 *
 * Layout (28 bytes):
 *   magic(4) + version(2) + palette_count(2) + rig_compat_id(8) +
 *   mesh_space(1) + _pad(3) + reach(4) + any_pose_radius(4)
 *
 * After the header, in palette index order (P = palette_count):
 *   uint16_t remap[P]              palette entry p -> skeleton joint
 *   float    inverse_bind[P][12]   nt_skeletal_mat34_t row order r[3][4]
 *
 * Total payload size is exactly NT_SKN_SIZE(palette_count). remap[p] is not
 * bounded against a skeleton here -- no skeleton is available at activation;
 * nt_skin_palette_build asserts remap[p] < model_count where both exist.
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;         /* 0:  NT_SKN_MAGIC */
    uint16_t version;       /* 4:  NT_SKELETAL_FORMAT_VERSION */
    uint16_t palette_count; /* 6:  >= 1 */
    uint64_t rig_compat_id; /* 8:  rig this binding is valid with */
    uint8_t mesh_space;     /* 16: NT_SKN_MESH_SPACE_* */
    uint8_t _pad[3];        /* 17: explicit padding */
    float reach;            /* 20: max |inverse_bind[p] * v| over bound vertices, >= 0 */
    float any_pose_radius;  /* 24: conservative skinned radius over any pose, >= 0 */
} NtSknHeader;
#pragma pack(pop)

/* 28 header bytes + 2 + 48 per palette entry */
#define NT_SKN_SIZE(palette_count) (28U + (50U * (uint32_t)(palette_count)))
// #endregion

// #region NANM clip
/* Clip kind. An additive clip carries the identity of the reference pose its
 * deltas were taken against; an absolute clip carries 0. */
typedef enum {
    NT_ANM_KIND_ABSOLUTE = 0,
    NT_ANM_KIND_ADDITIVE = 1,
} nt_anm_kind_t;

/* Sample codec of the plane, constant and step tables. Q16 is a later type. */
typedef enum {
    NT_ANM_CODEC_F32 = 0,
} nt_anm_codec_t;

/* Per-channel storage mode. index means, per mode:
 *   ABSENT    unused, 0
 *   CONSTANT  entry in CNST (several channels may share one entry)
 *   SAMPLED   row in the plane of this channel's component kind; rows are in
 *             channel order, so index counts the sampled channels of that kind
 *             before this one
 *   STEP      entry in STPT, counting the step channels before this one */
typedef enum {
    NT_ANM_CHANNEL_ABSENT = 0,
    NT_ANM_CHANNEL_CONSTANT = 1,
    NT_ANM_CHANNEL_SAMPLED = 2,
    NT_ANM_CHANNEL_STEP = 3,
} nt_anm_channel_mode_t;

/* Section tags, each present exactly once in a v1 payload, in this order.
 * A missing known tag rejects the payload; an unknown tag is skipped. */
#define NT_ANM_TAG_CHAN NT_SKELETAL_FOURCC('C', 'H', 'A', 'N') /* NtAnmChannel per channel */
#define NT_ANM_TAG_PLNT NT_SKELETAL_FOURCC('P', 'L', 'N', 'T') /* sampled translation rows */
#define NT_ANM_TAG_PLNQ NT_SKELETAL_FOURCC('P', 'L', 'N', 'Q') /* sampled rotation rows */
#define NT_ANM_TAG_PLNS NT_SKELETAL_FOURCC('P', 'L', 'N', 'S') /* sampled scale rows */
#define NT_ANM_TAG_CNST NT_SKELETAL_FOURCC('C', 'N', 'S', 'T') /* float[4] per constant channel */
#define NT_ANM_TAG_STPT NT_SKELETAL_FOURCC('S', 'T', 'P', 'T') /* NtAnmStepTrack per step channel */
#define NT_ANM_TAG_STPK NT_SKELETAL_FOURCC('S', 'T', 'P', 'K') /* NtAnmStepKey, tracks concatenated */

#define NT_ANM_SECTION_COUNT 7

/*
 * NtAnmHeader -- explicit header followed by section_count descriptors.
 *
 * Layout (56 bytes):
 *   magic(4) + version(2) + section_count(2) +
 *   joint_count(2) + kind(1) + codec(1) +
 *   rig_compat_id(8) + additive_ref_id(8) +
 *   duration(4) + sample_count(4) +
 *   r_joints(4) + r_root(4) + s_max(4) + bake_fps_min(4) + bake_reach(4)
 *
 * One uniform sample grid on [0, duration] with sample_count samples carries
 * every SAMPLED channel; STEP channels keep their exact timestamps instead.
 * sample_count == 1 means no plane holds a row (a clip built only from
 * constant and step channels may still have duration > 0).
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;           /* 0:  NT_ANM_MAGIC */
    uint16_t version;         /* 4:  NT_SKELETAL_FORMAT_VERSION */
    uint16_t section_count;   /* 6:  number of NtAnmSection after the header */
    uint16_t joint_count;     /* 8:  >= 1, must match the skeleton at bind time */
    uint8_t kind;             /* 10: nt_anm_kind_t */
    uint8_t codec;            /* 11: nt_anm_codec_t */
    uint64_t rig_compat_id;   /* 12: rig this clip plays on */
    uint64_t additive_ref_id; /* 20: reference pose identity, 0 iff kind == ABSOLUTE */
    float duration;           /* 28: seconds, finite and >= 0 */
    uint32_t sample_count;    /* 32: samples on the uniform grid, >= 1 */
    float r_joints;           /* 36: bounds: max joint-origin distance (§14) */
    float r_root;             /* 40: bounds: max root translation length */
    float s_max;              /* 44: bounds: max model-space linear stretch */
    float bake_fps_min;       /* 48: certificate: lowest bake rate that holds the budget */
    float bake_reach;         /* 52: certificate: reach the bake budget was proved against */
} NtAnmHeader;
#pragma pack(pop)

/*
 * NtAnmSection (16 bytes) -- offset is measured from the payload start and is
 * a multiple of 4; count * stride is computed in 64 bits and must fit inside
 * the payload. An empty section keeps stride and offset valid with count 0.
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t tag;    /* 0:  NT_ANM_TAG_* */
    uint32_t offset; /* 4:  bytes from payload start, multiple of 4 */
    uint32_t count;  /* 8:  elements (rows for the planes) */
    uint32_t stride; /* 12: bytes per element */
} NtAnmSection;
#pragma pack(pop)

/* CHAN element. Channel c addresses joint c / 3 and component c % 3
 * (0 = translation, 1 = rotation, 2 = scale); the last three channels are the
 * object curve, so c / 3 == joint_count there. */
#pragma pack(push, 1)
typedef struct {
    uint8_t mode;   /* 0: nt_anm_channel_mode_t */
    uint8_t _pad;   /* 1: explicit padding */
    uint16_t index; /* 2: meaning depends on mode */
} NtAnmChannel;
#pragma pack(pop)

/* STPT element: the span of this step channel inside STPK. The tracks partition
 * STPK in channel order -- first_key is the running key total, so track 0 starts
 * at 0 and the last track ends at STPK.count. No key is shared or unreferenced. */
#pragma pack(push, 1)
typedef struct {
    uint32_t first_key; /* 0: index of the first key in STPK */
    uint32_t key_count; /* 4: >= 1 */
} NtAnmStepTrack;
#pragma pack(pop)

/* STPK element: the tracks concatenate in channel order and cover the table
 * exactly. Times are strictly increasing inside a track, start at 0 and end at
 * or before duration. A translation or scale key leaves v[3] at 0; a rotation
 * key holds a unit quaternion. */
#pragma pack(push, 1)
typedef struct {
    float time; /* 0: seconds from clip start */
    float v[4]; /* 4: t[3]/s[3] with v[3] == 0, or unit q xyzw */
} NtAnmStepKey;
#pragma pack(pop)

/* Joint channels plus the three object-curve channels (§7.5). */
#define NT_ANM_CHANNEL_COUNT(joint_count) (3U * ((uint32_t)(joint_count) + 1U))
/* First channel of the object curve. */
#define NT_ANM_OBJECT_CHANNEL(joint_count) (3U * (uint32_t)(joint_count))

/* Components a channel stores: 4 for a rotation, 3 for a translation or scale. */
static inline uint32_t nt_anm_channel_comps(uint32_t channel) { return (channel % 3U == 1U) ? 4U : 3U; }
// #endregion

/* C++ spells these differently and GCC rejects the C keyword there; the wire
 * layout is pinned by the C build every consumer shares. */
#ifndef __cplusplus
_Static_assert(sizeof(NtSklHeader) == 16, "NtSklHeader must be 16 bytes");
_Static_assert(sizeof(NtSknHeader) == 28, "NtSknHeader must be 28 bytes");
_Static_assert(sizeof(NtAnmHeader) == 56, "NtAnmHeader must be 56 bytes");
_Static_assert(sizeof(NtAnmSection) == 16, "NtAnmSection must be 16 bytes");
_Static_assert(sizeof(NtAnmChannel) == 4, "NtAnmChannel must be 4 bytes");
_Static_assert(sizeof(NtAnmStepTrack) == 8, "NtAnmStepTrack must be 8 bytes");
_Static_assert(sizeof(NtAnmStepKey) == 20, "NtAnmStepKey must be 20 bytes");
#endif

#endif /* NT_SKELETAL_FORMAT_H */
