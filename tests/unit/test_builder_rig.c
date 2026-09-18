/* Rig import from glTF: what nt_builder_parse_glb_scene publishes about the
 * node graph of a rigged glTF, and the rig nt_builder_import_rig derives from
 * it -- joint order, rest pose, matrix decomposition and identity -- plus the
 * skinned mesh and binding exports over that rig. The fixture is written by
 * rigged_glb.c, so every expectation here names a value documented in
 * rigged_glb.h. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Windows SDK must be included early (before stdnoreturn.h from C17 headers) */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* clang-format off */
#include "nt_builder.h"
#include "nt_builder_internal.h"
#include "nt_skeletal_format.h"
#include "hash/nt_hash.h"
#include "log/nt_log.h"
#include "cgltf.h"
#include "test_helpers/build_assert_trap.h"
#include "test_helpers/rigged_glb.h"
#include "unity.h"
/* clang-format on */

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

#define TMP_DIR "build/tests/tmp"
#define RIG_GLB TMP_DIR "/rigged.glb"
#define PACK_PATH TMP_DIR "/builder_rig.ntpack"

// #region log sink
/* Every importer diagnostic precedes its NT_BUILD_ASSERT, so the last WARN or
 * ERROR line names which vertex or node a rule fired on; the count proves a
 * clean asset imports without a diagnostic at all. Registered per test so a
 * failing assertion never leaves a sink behind. */
static uint32_t s_log_warnings;
static char s_log_last[NT_LOG_BUF_SIZE];

static void log_sink(nt_log_level_t level, const char *domain, const char *msg, void *user) {
    (void)domain;
    (void)user;
    if (level >= NT_LOG_LEVEL_WARN) {
        s_log_warnings++;
        (void)snprintf(s_log_last, sizeof(s_log_last), "%s", msg);
    }
}

void setUp(void) {
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    s_log_warnings = 0;
    s_log_last[0] = '\0';
    nt_log_add_sink(log_sink, NULL);
}
/* A Unity failure inside a trapped call leaves the trap installed; the next
 * NT_BUILD_ASSERT must abort, not jump into a dead frame. */
void tearDown(void) {
    nt_log_remove_sink(log_sink, NULL);
    nt_build_assert_handler = NULL;
}
// #endregion

/* The test config excludes Unity's float asserts, and comparing the exact bits
 * is the stronger claim about values that travelled through JSON text anyway. */
static uint32_t f32_bits(float v) {
    uint32_t bits = 0;
    memcpy(&bits, &v, sizeof(bits));
    return bits;
}
#define ASSERT_F32(expected, actual) TEST_ASSERT_EQUAL_HEX32(f32_bits(expected), f32_bits(actual))

/* The payload of the pack's first asset of a type, so a test reads the bytes
 * the build shipped rather than an encoder's return value. Caller frees. */
static uint8_t *read_pack_asset(const char *pack_path, nt_asset_type_t type, uint32_t *out_size) {
    FILE *f = fopen(pack_path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL(0, fseek(f, 0, SEEK_END));
    const long file_size = ftell(f);
    TEST_ASSERT_TRUE(file_size > (long)sizeof(NtPackHeader));
    TEST_ASSERT_EQUAL(0, fseek(f, 0, SEEK_SET));
    uint8_t *file = (uint8_t *)malloc((size_t)file_size);
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL((size_t)file_size, fread(file, 1, (size_t)file_size, f));
    (void)fclose(f);

    NtPackHeader hdr;
    memcpy(&hdr, file, sizeof(hdr));
    for (uint16_t i = 0; i < hdr.asset_count; i++) {
        NtAssetEntry entry;
        memcpy(&entry, file + sizeof(NtPackHeader) + ((size_t)i * sizeof(NtAssetEntry)), sizeof(entry));
        if (entry.asset_type != (uint8_t)type) {
            continue;
        }
        TEST_ASSERT_TRUE((size_t)entry.offset + entry.size <= (size_t)file_size);
        uint8_t *payload = (uint8_t *)malloc(entry.size);
        TEST_ASSERT_NOT_NULL(payload);
        memcpy(payload, file + entry.offset, entry.size);
        free(file);
        *out_size = entry.size;
        return payload;
    }
    free(file);
    TEST_FAIL_MESSAGE("the pack holds no asset of the requested type");
    return NULL;
}

// #region scene tests
void test_scene_publishes_the_node_graph(void) {
    rigged_glb_write(RIG_GLB, NULL);

    nt_glb_scene_t scene;
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&scene, RIG_GLB));
    TEST_ASSERT_EQUAL_UINT32(RIGGED_GLB_NODE_COUNT, scene.node_count);
    TEST_ASSERT_EQUAL_UINT32(1, scene.mesh_count);

    static const uint32_t k_parent[RIGGED_GLB_NODE_COUNT] = {
        UINT32_MAX,           RIGGED_GLB_NODE_ROOT, RIGGED_GLB_NODE_HELPER, RIGGED_GLB_NODE_JOINT0, RIGGED_GLB_NODE_JOINT1, RIGGED_GLB_NODE_JOINT0, RIGGED_GLB_NODE_JOINT3,
        RIGGED_GLB_NODE_ROOT, RIGGED_GLB_NODE_ROOT,
    };
    for (uint32_t i = 0; i < RIGGED_GLB_NODE_COUNT; i++) {
        TEST_ASSERT_EQUAL_UINT32(k_parent[i], scene.nodes[i].parent);
    }

    /* The mesh and the skin sit on one node, and on no other. */
    for (uint32_t i = 0; i < RIGGED_GLB_NODE_COUNT; i++) {
        uint32_t expected = i == RIGGED_GLB_NODE_MESH ? 0U : UINT32_MAX;
        TEST_ASSERT_EQUAL_UINT32(expected, scene.nodes[i].mesh_index);
        TEST_ASSERT_EQUAL_UINT32(expected, scene.nodes[i].skin_index);
    }

    TEST_ASSERT_EQUAL_STRING("Root", scene.nodes[RIGGED_GLB_NODE_ROOT].name);
    TEST_ASSERT_EQUAL_STRING("Helper", scene.nodes[RIGGED_GLB_NODE_HELPER].name);
    TEST_ASSERT_EQUAL_STRING("Joint0", scene.nodes[RIGGED_GLB_NODE_JOINT0].name);
    TEST_ASSERT_EQUAL_STRING("MeshNode", scene.nodes[RIGGED_GLB_NODE_MESH].name);
    TEST_ASSERT_EQUAL_STRING("Object", scene.nodes[RIGGED_GLB_NODE_OBJECT].name);

    nt_builder_free_glb_scene(&scene);
}

/* One knob that fails cgltf_validate, one assert text: the parse stops before
 * any reader trusts the data. */
#define EXPECT_PARSE_ASSERT(field)                                                                                                                                                                     \
    do {                                                                                                                                                                                               \
        rigged_glb_opts_t knob_opts = {0};                                                                                                                                                             \
        knob_opts.field = true;                                                                                                                                                                        \
        rigged_glb_write(RIG_GLB, &knob_opts);                                                                                                                                                         \
        nt_glb_scene_t knob_scene;                                                                                                                                                                     \
        EXPECT_BUILD_ASSERT_MATCH((void)nt_builder_parse_glb_scene(&knob_scene, RIG_GLB), "fails cgltf_validate");                                                                                     \
    } while (0)

void test_parse_asserts_on_a_parent_cycle(void) { EXPECT_PARSE_ASSERT(cycle); }

void test_parse_asserts_on_an_accessor_covering_fewer_vertices(void) { EXPECT_PARSE_ASSERT(weights1_short); }
// #endregion

// #region rig reference
#define RIG_JOINT_COUNT 7

typedef struct {
    const char *name;
    uint16_t parent;
    uint16_t subtree_end;
    uint32_t node;
    float t[3];
    float q[4];
    float s[3];
} rig_joint_ref_t;

/* The rig of the fixture skin in preorder: the two matrix wrappers decomposed,
 * then the five skin joints with glTF children order inside each node --
 * Joint0 lists Joint3 before Joint1, so preorder is not node order. */
static const rig_joint_ref_t k_rig[RIG_JOINT_COUNT] = {
    {"Root", NT_SKELETAL_NO_PARENT, 7, RIGGED_GLB_NODE_ROOT, {0.0F, 0.0F, 0.0F}, {0.0F, 0.70710678F, 0.0F, 0.70710678F}, {1.0F, 1.0F, 1.0F}},
    {"Helper", 0, 7, RIGGED_GLB_NODE_HELPER, {0.25F, -0.5F, 1.0F}, {0.0F, 0.0F, 0.70710678F, 0.70710678F}, {2.0F, 1.0F, 0.5F}},
    {"Joint0", 1, 7, RIGGED_GLB_NODE_JOINT0, {1.0F, 2.0F, 3.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}},
    {"Joint3", 2, 5, RIGGED_GLB_NODE_JOINT3, {-0.5F, 0.25F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}},
    {"Joint4", 3, 5, RIGGED_GLB_NODE_JOINT4, {0.0F, 0.5F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {0.5F, 0.5F, 0.5F}},
    {"Joint1", 2, 7, RIGGED_GLB_NODE_JOINT1, {0.0F, -0.0F, 0.5F}, {0.0F, 0.0F, -0.70710678F, -0.70710678F}, {1.0F, 1.0F, 1.0F}},
    {"Joint2", 5, 7, RIGGED_GLB_NODE_JOINT2, {0.0F, 0.75F, 0.0F}, {0.70710678F, 0.0F, 0.0F, 0.70710678F}, {1.0F, 1.0F, 1.0F}},
};

/* Palette entry p -> node, the skin's own (deliberately shuffled) joint list. */
static const uint32_t k_palette_node[RIGGED_GLB_SKIN_JOINT_COUNT] = RIGGED_GLB_PALETTE_NODES;

/* The rig joint behind palette entry p, found by node in the reference table. */
static uint16_t ref_palette_joint(uint32_t p) {
    for (uint16_t j = 0; j < RIG_JOINT_COUNT; j++) {
        if (k_rig[j].node == k_palette_node[p]) {
            return j;
        }
    }
    TEST_FAIL_MESSAGE("palette entry names a node outside the rig reference");
    return 0;
}

/* Writes the default fixture and imports its only rig. */
static void import_fixture_rig(nt_glb_scene_t *scene, nt_builder_rig_t *rig) {
    rigged_glb_write(RIG_GLB, NULL);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(scene, RIG_GLB));
    nt_builder_import_rig(scene, 0, UINT32_MAX, rig);
}

/* The identity schema of skeletal-animation.md 3.1, written here so the test
 * hashes bytes of its own rather than the importer's. */
static uint32_t rig_put_u8(uint8_t *b, uint32_t off, uint8_t v) {
    b[off] = v;
    return off + 1U;
}

static uint32_t rig_put_u16(uint8_t *b, uint32_t off, uint16_t v) {
    b[off] = (uint8_t)(v & 0xFFU);
    b[off + 1U] = (uint8_t)(v >> 8U);
    return off + 2U;
}

static uint32_t rig_put_u32(uint8_t *b, uint32_t off, uint32_t v) {
    for (uint32_t i = 0; i < 4U; i++) {
        b[off + i] = (uint8_t)((v >> (8U * i)) & 0xFFU);
    }
    return off + 4U;
}

static uint32_t rig_put_f32(uint8_t *b, uint32_t off, float v) {
    uint32_t bits = f32_bits(v);
    if (bits == 0x80000000U) {
        bits = 0U; /* -0 -> +0 */
    }
    return rig_put_u32(b, off, bits);
}

/* Largest component positive, ties to the first maximum in x,y,z,w order. */
static void rig_canonical_quat(const float q[4], float out[4]) {
    int best = 0;
    float best_abs = (q[0] < 0.0F) ? -q[0] : q[0];
    for (int c = 1; c < 4; c++) {
        const float a = (q[c] < 0.0F) ? -q[c] : q[c];
        if (a > best_abs) {
            best_abs = a;
            best = c;
        }
    }
    const float sign = (q[best] < 0.0F) ? -1.0F : 1.0F;
    for (int c = 0; c < 4; c++) {
        out[c] = q[c] * sign;
    }
}
// #endregion

// #region rig import tests
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_rig_import_preorder_and_subtree_ranges(void) {
    nt_glb_scene_t scene;
    nt_builder_rig_t rig;
    import_fixture_rig(&scene, &rig);

    TEST_ASSERT_EQUAL_UINT16(RIG_JOINT_COUNT, rig.skeleton.joint_count);
    TEST_ASSERT_EQUAL_UINT16(RIGGED_GLB_SKIN_JOINT_COUNT, rig.palette_count);
    TEST_ASSERT_EQUAL_UINT32(0, rig.skin_index);

    for (uint16_t j = 0; j < RIG_JOINT_COUNT; j++) {
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(nt_hash32_str(k_rig[j].name).value, rig.skeleton.joint_id[j], k_rig[j].name);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(k_rig[j].parent, rig.skeleton.parent[j], k_rig[j].name);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(k_rig[j].subtree_end, rig.skeleton.subtree_end[j], k_rig[j].name);
        for (int c = 0; c < 3; c++) {
            ASSERT_F32(k_rig[j].t[c], rig.skeleton.rest[j].t[c]);
            ASSERT_F32(k_rig[j].s[c], rig.skeleton.rest[j].s[c]);
        }
        for (int c = 0; c < 4; c++) {
            ASSERT_F32(k_rig[j].q[c], rig.skeleton.rest[j].q[c]);
        }
    }

    /* The palette follows the skin's shuffled joint list, not the preorder. */
    for (uint16_t p = 0; p < RIGGED_GLB_SKIN_JOINT_COUNT; p++) {
        TEST_ASSERT_EQUAL_UINT16(ref_palette_joint(p), rig.palette_joint[p]);
    }

    nt_builder_free_rig(&rig);
    nt_builder_free_glb_scene(&scene);
}

void test_rig_compat_id_matches_the_hand_written_schema(void) {
    nt_glb_scene_t scene;
    nt_builder_rig_t rig;
    import_fixture_rig(&scene, &rig);

    uint8_t expected[NT_SKELETAL_RIG_ID_BYTES(RIG_JOINT_COUNT)];
    uint32_t off = 0;
    off = rig_put_u8(expected, off, (uint8_t)'N');
    off = rig_put_u8(expected, off, (uint8_t)'R');
    off = rig_put_u8(expected, off, (uint8_t)'I');
    off = rig_put_u8(expected, off, (uint8_t)'G');
    off = rig_put_u8(expected, off, 1U); /* schema version */
    off = rig_put_u8(expected, off, 1U); /* glTF convention */
    off = rig_put_u16(expected, off, RIG_JOINT_COUNT);
    for (uint16_t j = 0; j < RIG_JOINT_COUNT; j++) {
        off = rig_put_u32(expected, off, nt_hash32_str(k_rig[j].name).value);
        off = rig_put_u16(expected, off, k_rig[j].parent);
        for (int c = 0; c < 3; c++) {
            off = rig_put_f32(expected, off, k_rig[j].t[c]);
        }
        float q[4];
        rig_canonical_quat(k_rig[j].q, q);
        for (int c = 0; c < 4; c++) {
            off = rig_put_f32(expected, off, q[c]);
        }
        for (int c = 0; c < 3; c++) {
            off = rig_put_f32(expected, off, k_rig[j].s[c]);
        }
    }
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(expected), off);
    TEST_ASSERT_EQUAL_HEX64(nt_hash64(expected, off).value, rig.skeleton.rig_compat_id.value);

    nt_builder_free_rig(&rig);
    nt_builder_free_glb_scene(&scene);
}

/* A cut is the explicit way to leave wrapper nodes to the game's E. */
void test_rig_cut_at_helper_drops_the_scene_root(void) {
    rigged_glb_write(RIG_GLB, NULL);

    nt_glb_scene_t scene;
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&scene, RIG_GLB));

    nt_builder_rig_t rig;
    nt_builder_import_rig(&scene, 0, RIGGED_GLB_NODE_HELPER, &rig);

    TEST_ASSERT_EQUAL_UINT16(RIG_JOINT_COUNT - 1, rig.skeleton.joint_count);
    TEST_ASSERT_EQUAL_UINT16(NT_SKELETAL_NO_PARENT, rig.skeleton.parent[0]);
    /* Root is gone, so every joint index moves down by one. */
    for (uint16_t j = 0; j < rig.skeleton.joint_count; j++) {
        TEST_ASSERT_EQUAL_HEX32(nt_hash32_str(k_rig[j + 1].name).value, rig.skeleton.joint_id[j]);
        TEST_ASSERT_EQUAL_UINT16(k_rig[j + 1].subtree_end - 1U, rig.skeleton.subtree_end[j]);
        if (j > 0) {
            TEST_ASSERT_EQUAL_UINT16(k_rig[j + 1].parent - 1U, rig.skeleton.parent[j]);
        }
    }
    for (uint16_t p = 0; p < RIGGED_GLB_SKIN_JOINT_COUNT; p++) {
        TEST_ASSERT_EQUAL_UINT16(ref_palette_joint(p) - 1U, rig.palette_joint[p]);
    }

    nt_builder_free_rig(&rig);
    nt_builder_free_glb_scene(&scene);
}
// #endregion

// #region matrix decomposition
void test_decompose_asserts_on_a_projective_bottom_row(void) {
    static const float k_projective[16] = {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.5F, 0.0F, 0.0F, 0.0F, 1.0F};
    nt_skeletal_trs_t trs;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_decompose_trs(k_projective, "Projective", &trs), "matrix is not affine");
}

void test_decompose_asserts_on_a_non_finite_element(void) {
    static float k_nan[16] = {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
    k_nan[5] = NAN;
    nt_skeletal_trs_t trs;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_decompose_trs(k_nan, "NaN", &trs), "matrix is not finite");
}

/* A mirrored basis is a negative scale, not a rotation. */
void test_decompose_reflection_gives_a_negative_x_scale(void) {
    static const float k_mirror[16] = {-1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 3.0F, 0.0F, 0.0F, 1.0F};
    nt_skeletal_trs_t trs;
    nt_builder_decompose_trs(k_mirror, "Mirror", &trs);

    ASSERT_F32(-1.0F, trs.s[0]);
    ASSERT_F32(1.0F, trs.s[1]);
    ASSERT_F32(1.0F, trs.s[2]);
    ASSERT_F32(0.0F, trs.q[0]);
    ASSERT_F32(0.0F, trs.q[1]);
    ASSERT_F32(0.0F, trs.q[2]);
    ASSERT_F32(1.0F, trs.q[3]);
    ASSERT_F32(3.0F, trs.t[0]);
}

void test_decompose_asserts_on_a_zero_scale(void) {
    static const float k_flat[16] = {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
    nt_skeletal_trs_t trs;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_decompose_trs(k_flat, "Flat", &trs), "matrix scale is degenerate");
}
// #endregion

// #region rig diagnostics
/* One rig knob, one assert text on the import path. */
#define EXPECT_IMPORT_ASSERT(field, expected)                                                                                                                                                          \
    do {                                                                                                                                                                                               \
        rigged_glb_opts_t knob_opts = {0};                                                                                                                                                             \
        knob_opts.field = true;                                                                                                                                                                        \
        rigged_glb_write(RIG_GLB, &knob_opts);                                                                                                                                                         \
        nt_glb_scene_t knob_scene;                                                                                                                                                                     \
        TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&knob_scene, RIG_GLB));                                                                                                              \
        nt_builder_rig_t knob_rig;                                                                                                                                                                     \
        EXPECT_BUILD_ASSERT_MATCH(nt_builder_import_rig(&knob_scene, 0, UINT32_MAX, &knob_rig), expected);                                                                                             \
        nt_builder_free_glb_scene(&knob_scene);                                                                                                                                                        \
    } while (0)

void test_import_asserts_on_a_sheared_matrix(void) { EXPECT_IMPORT_ASSERT(matrix_shear, "matrix is not TRS"); }

/* The recompose budget follows the column length: a 0.01-scale wrapper with a
 * 1e-3 rad shear is off by ~5e-6 absolute, inside a unit-scale budget but far
 * outside its own. */
void test_import_asserts_on_a_sheared_small_scale_matrix(void) { EXPECT_IMPORT_ASSERT(root_small_shear, "matrix is not TRS"); }

/* The same wrapper without shear is a plain uniform scale. */
void test_import_decomposes_a_small_scale_wrapper(void) {
    rigged_glb_opts_t opts = {0};
    opts.root_small_scale = true;
    rigged_glb_write(RIG_GLB, &opts);

    nt_glb_scene_t scene;
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&scene, RIG_GLB));
    nt_builder_rig_t rig;
    nt_builder_import_rig(&scene, 0, UINT32_MAX, &rig);
    for (int c = 0; c < 3; c++) {
        ASSERT_F32(0.01F, rig.skeleton.rest[0].s[c]);
        ASSERT_F32(0.0F, rig.skeleton.rest[0].t[c]);
        ASSERT_F32(0.0F, rig.skeleton.rest[0].q[c]);
    }
    ASSERT_F32(1.0F, rig.skeleton.rest[0].q[3]);
    nt_builder_free_rig(&rig);
    nt_builder_free_glb_scene(&scene);
}

void test_import_asserts_on_an_unnamed_rig_node(void) { EXPECT_IMPORT_ASSERT(unnamed_node, "no name"); }

void test_import_asserts_on_an_empty_rig_node_name(void) { EXPECT_IMPORT_ASSERT(empty_name, "no name"); }

void test_import_asserts_on_a_non_unit_rest_rotation(void) { EXPECT_IMPORT_ASSERT(bad_rotation, "not a unit quaternion"); }

void test_import_asserts_on_a_matrix_node_with_trs(void) { EXPECT_IMPORT_ASSERT(matrix_and_trs, "both a matrix and TRS"); }

void test_import_asserts_on_a_skin_listing_one_joint_twice(void) { EXPECT_IMPORT_ASSERT(duplicate_skin_joint, "lists one joint twice"); }

void test_import_asserts_on_a_joint_chain_deeper_than_the_cap(void) { EXPECT_IMPORT_ASSERT(deep_chain, "too deep"); }

void test_import_asserts_on_joints_under_several_scene_roots(void) { EXPECT_IMPORT_ASSERT(multi_root, "span several scene roots"); }

/* Two rig nodes named "Joint3": the diagnostic names both, so it runs the
 * collision sort and the node lookup before the assert. */
void test_import_asserts_on_two_rig_nodes_sharing_one_joint_id(void) {
    EXPECT_IMPORT_ASSERT(duplicate_name, "share one joint id");
    TEST_ASSERT_NOT_NULL(strstr(s_log_last, "\"Joint3\" (node[5]) and \"Joint3\" (node[6])"));
}

void test_import_asserts_on_a_missing_skin(void) {
    rigged_glb_write(RIG_GLB, NULL);

    nt_glb_scene_t scene;
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&scene, RIG_GLB));
    nt_builder_rig_t rig;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_import_rig(&scene, 1, UINT32_MAX, &rig), "skin index out of range");
    nt_builder_free_glb_scene(&scene);
}

void test_import_asserts_on_a_joint_outside_the_cut(void) {
    rigged_glb_opts_t opts = {0};
    opts.joint_outside_root = true;
    rigged_glb_write(RIG_GLB, &opts);

    nt_glb_scene_t scene;
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&scene, RIG_GLB));
    nt_builder_rig_t rig;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_import_rig(&scene, 0, RIGGED_GLB_NODE_HELPER, &rig), "outside the skeleton root");
    nt_builder_free_glb_scene(&scene);
}
// #endregion

// #region skinned mesh export
/* The layout under test: POSITION keeps the primitive a mesh, and the two skin
 * streams carry the reduced lanes. Offsets and stride satisfy the WebGL2
 * alignment rules for every type combination used below. */
static void skin_layout(NtStreamLayout out[3], nt_stream_type_t joints_type, nt_stream_type_t weights_type, bool weights_normalized) {
    out[0] = (NtStreamLayout){"position", "POSITION", NT_STREAM_FLOAT32, 3, false, 0};
    out[1] = (NtStreamLayout){"joints", "JOINTS", joints_type, 4, false, 0};
    out[2] = (NtStreamLayout){"weights", "WEIGHTS", weights_type, 4, weights_normalized, 0};
}

static uint8_t *skin_decode(const rigged_glb_opts_t *opts, const NtStreamLayout *layout, float drop_tolerance, uint16_t palette_count) {
    rigged_glb_write(RIG_GLB, opts);
    nt_glb_scene_t scene;
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&scene, RIG_GLB));
    const nt_builder_skin_ctx_t skin = {.palette_count = palette_count, .drop_tolerance = drop_tolerance};
    uint8_t *data = NULL;
    uint32_t size = 0;
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_decode_scene_mesh_skinned(&scene, 0, 0, layout, 3, NT_TANGENT_AUTO, &skin, &data, &size));
    TEST_ASSERT_NOT_NULL(data);
    nt_builder_free_glb_scene(&scene);
    return data;
}

/* Same decode without the success expectation, so a death test can wrap it. */
static void skin_try_decode(const nt_glb_scene_t *scene, const NtStreamLayout *layout, float drop_tolerance, uint16_t palette_count) {
    const nt_builder_skin_ctx_t skin = {.palette_count = palette_count, .drop_tolerance = drop_tolerance};
    uint8_t *data = NULL;
    uint32_t size = 0;
    (void)nt_builder_decode_scene_mesh_skinned(scene, 0, 0, layout, 3, NT_TANGENT_AUTO, &skin, &data, &size);
    free(data);
}

/* The drop tolerance that admits the whole fixture: vertex 1 loses 0.10, so the
 * margin keeps the gate's rounding out of the happy-path tests. */
#define SKIN_FIXTURE_TOLERANCE 0.15F

/* One defect knob, one assert text: the fixture is written, parsed and decoded
 * with the ordinary UINT8/UINT8 layout. */
#define EXPECT_SKIN_ASSERT(field, expected)                                                                                                                                                            \
    do {                                                                                                                                                                                               \
        rigged_glb_opts_t knob_opts = {0};                                                                                                                                                             \
        knob_opts.field = true;                                                                                                                                                                        \
        rigged_glb_write(RIG_GLB, &knob_opts);                                                                                                                                                         \
        nt_glb_scene_t knob_scene;                                                                                                                                                                     \
        TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&knob_scene, RIG_GLB));                                                                                                              \
        NtStreamLayout knob_layout[3];                                                                                                                                                                 \
        skin_layout(knob_layout, NT_STREAM_UINT8, NT_STREAM_UINT8, true);                                                                                                                              \
        EXPECT_BUILD_ASSERT_MATCH(skin_try_decode(&knob_scene, knob_layout, SKIN_FIXTURE_TOLERANCE, RIGGED_GLB_SKIN_JOINT_COUNT), expected);                                                           \
        nt_builder_free_glb_scene(&knob_scene);                                                                                                                                                        \
    } while (0)

static NtMeshAssetHeader mesh_header(const uint8_t *data) {
    NtMeshAssetHeader hdr;
    memcpy(&hdr, data, sizeof(hdr));
    return hdr;
}

/* Plane s of the SOA vertex block: whole attributes, never split per component. */
static const uint8_t *mesh_plane(const uint8_t *data, uint32_t stream_count, uint32_t stream) {
    const NtMeshAssetHeader hdr = mesh_header(data);
    const uint8_t *plane = data + sizeof(NtMeshAssetHeader) + ((size_t)stream_count * sizeof(NtStreamDesc));
    for (uint32_t s = 0; s < stream; s++) {
        NtStreamDesc desc;
        memcpy(&desc, data + sizeof(NtMeshAssetHeader) + ((size_t)s * sizeof(NtStreamDesc)), sizeof(desc));
        plane += (size_t)hdr.vertex_count * nt_stream_type_size(desc.type) * desc.count;
    }
    return plane;
}

/* The wire is byte-addressed, so lanes are copied out rather than aliased. */
static float mesh_lane_f32(const uint8_t *plane, uint32_t lane) {
    float v = 0.0F;
    memcpy(&v, plane + ((size_t)lane * sizeof(float)), sizeof(v));
    return v;
}

static uint16_t mesh_lane_u16(const uint8_t *plane, uint32_t lane) {
    uint16_t v = 0;
    memcpy(&v, plane + ((size_t)lane * sizeof(uint16_t)), sizeof(v));
    return v;
}

/* The four lanes the reduction must keep, and their source weights, as
 * rigged_glb.h documents them. Vertex 1 is the tie: three influences share 0.10
 * and the two lowest joint indices win. */
static const uint32_t k_kept_joint[RIGGED_GLB_VERTEX_COUNT][4] = {{0, 1, 2, 3}, {1, 0, 2, 3}, {0, 1, 2, 3}, {0, 1, 0, 0}};
static const float k_kept_weight[RIGGED_GLB_VERTEX_COUNT][4] = {
    {0.40F, 0.30F, 0.20F, 0.09F},
    {0.40F, 0.30F, 0.10F, 0.10F},
    {0.40F, 0.30F, 0.15F, 0.10F},
    {0.75F, 0.25F, 0.0F, 0.0F},
};

/* The kept lanes renormalized to a unit sum, computed here from the fixture's
 * numbers rather than from the builder's. */
static void skin_expected_weights(uint32_t vertex, float out[4]) {
    double sum = 0.0;
    for (uint32_t c = 0; c < 4U; c++) {
        sum += (double)k_kept_weight[vertex][c];
    }
    for (uint32_t c = 0; c < 4U; c++) {
        out[c] = (float)((double)k_kept_weight[vertex][c] / sum);
    }
}

/* The bytes largest-remainder rounding produces from the renormalized lanes,
 * worked from the fixture's numbers (lane * 255 -> floor, then the leftover
 * to the largest fractions, lower lane first on a tie):
 *
 *   v0  103.03 77.27 51.52 23.18 -> floors sum 254, +1 to lane 2
 *   v1  113.33 85.00 28.33 28.33 -> floors sum 254; lanes 0, 2, 3 tie at a
 *       third, and lane 0's binary32 quotient 0.4/0.9 is the larger by one
 *       ulp, so it takes the +1
 *   v2  107.37 80.53 40.26 26.84 -> floors sum 253, +1 to lane 3, then lane 1
 *   v3  191.25 63.75  0     0    -> floors sum 254, +1 to lane 1 */
static const uint8_t k_kept_u8[RIGGED_GLB_VERTEX_COUNT][4] = {{103, 77, 52, 23}, {114, 85, 28, 28}, {107, 81, 40, 27}, {191, 64, 0, 0}};

void test_skinned_mesh_keeps_the_four_heaviest_influences(void) {
    NtStreamLayout layout[3];
    skin_layout(layout, NT_STREAM_UINT8, NT_STREAM_UINT8, true);
    uint8_t *data = skin_decode(NULL, layout, SKIN_FIXTURE_TOLERANCE, RIGGED_GLB_SKIN_JOINT_COUNT);

    const NtMeshAssetHeader hdr = mesh_header(data);
    TEST_ASSERT_EQUAL_UINT32(RIGGED_GLB_VERTEX_COUNT, hdr.vertex_count);
    TEST_ASSERT_EQUAL_UINT32(RIGGED_GLB_INDEX_COUNT, hdr.index_count);

    const uint8_t *joints = mesh_plane(data, 3, 1);
    const uint8_t *weights = mesh_plane(data, 3, 2);
    for (uint32_t v = 0; v < RIGGED_GLB_VERTEX_COUNT; v++) {
        for (uint32_t c = 0; c < 4U; c++) {
            TEST_ASSERT_EQUAL_UINT8(k_kept_joint[v][c], joints[(v * 4U) + c]);
            TEST_ASSERT_EQUAL_UINT8(k_kept_u8[v][c], weights[(v * 4U) + c]);
        }
    }

    /* The one warning of the export: v0, v1 and v2 were reduced, v1 lost most. */
    TEST_ASSERT_EQUAL_UINT32(1, s_log_warnings);
    TEST_ASSERT_NOT_NULL(strstr(s_log_last, "3 of 4 vertices reduced to four influences, dropping at most 0.1000"));

    free(data);
}

/* 0.5 and 0.5 both scale to 127.5: rounding each to nearest would ship 256,
 * so the leftover after flooring goes to one lane, and the tie goes low. */
void test_skinned_mesh_quantizes_a_half_half_tie_to_the_lower_lane(void) {
    rigged_glb_opts_t opts = {0};
    opts.weights_half = true;
    NtStreamLayout layout[3];
    skin_layout(layout, NT_STREAM_UINT8, NT_STREAM_UINT8, true);
    uint8_t *data = skin_decode(&opts, layout, SKIN_FIXTURE_TOLERANCE, RIGGED_GLB_SKIN_JOINT_COUNT);

    const uint8_t *joints = mesh_plane(data, 3, 1);
    const uint8_t *weights = mesh_plane(data, 3, 2);
    TEST_ASSERT_EQUAL_UINT8(0, joints[12]);
    TEST_ASSERT_EQUAL_UINT8(1, joints[13]);
    TEST_ASSERT_EQUAL_UINT8(128, weights[12]);
    TEST_ASSERT_EQUAL_UINT8(127, weights[13]);
    TEST_ASSERT_EQUAL_UINT8(0, weights[14]);
    TEST_ASSERT_EQUAL_UINT8(0, weights[15]);

    free(data);
}

void test_skinned_mesh_float32_weights_are_the_renormalized_values(void) {
    NtStreamLayout layout[3];
    skin_layout(layout, NT_STREAM_UINT8, NT_STREAM_FLOAT32, false);
    uint8_t *data = skin_decode(NULL, layout, SKIN_FIXTURE_TOLERANCE, RIGGED_GLB_SKIN_JOINT_COUNT);

    const uint8_t *weights = mesh_plane(data, 3, 2);
    for (uint32_t v = 0; v < RIGGED_GLB_VERTEX_COUNT; v++) {
        float expected[4];
        skin_expected_weights(v, expected);
        for (uint32_t c = 0; c < 4U; c++) {
            const float d = mesh_lane_f32(weights, (v * 4U) + c) - expected[c];
            TEST_ASSERT_TRUE(((d < 0.0F) ? -d : d) <= 1e-6F);
        }
    }
    /* Vertex 3 drops nothing and its two weights already sum to one, so the
     * float path stores exactly what the glTF carried. */
    ASSERT_F32(0.75F, mesh_lane_f32(weights, 12));
    ASSERT_F32(0.25F, mesh_lane_f32(weights, 13));
    ASSERT_F32(0.0F, mesh_lane_f32(weights, 14));
    ASSERT_F32(0.0F, mesh_lane_f32(weights, 15));

    free(data);
}

void test_skinned_mesh_float16_weights_round_trip(void) {
    NtStreamLayout layout[3];
    skin_layout(layout, NT_STREAM_UINT16, NT_STREAM_FLOAT16, false);
    uint8_t *data = skin_decode(NULL, layout, SKIN_FIXTURE_TOLERANCE, RIGGED_GLB_SKIN_JOINT_COUNT);

    const uint8_t *joints = mesh_plane(data, 3, 1);
    const uint8_t *weights = mesh_plane(data, 3, 2);
    for (uint32_t v = 0; v < RIGGED_GLB_VERTEX_COUNT; v++) {
        float expected[4];
        skin_expected_weights(v, expected);
        for (uint32_t c = 0; c < 4U; c++) {
            TEST_ASSERT_EQUAL_UINT16(k_kept_joint[v][c], mesh_lane_u16(joints, (v * 4U) + c));
            /* The lane is the shared converter's rounding of the renormalized
             * float, nothing looser. */
            TEST_ASSERT_EQUAL_HEX16(nt_f32_to_f16(expected[c]), mesh_lane_u16(weights, (v * 4U) + c));
        }
    }

    free(data);
}

/* The gate fires on the first vertex over the tolerance, and the diagnostic
 * names it: at 0.005 that is v0 (0.01), at 0.07 v0 passes and v1 (0.10) fires,
 * so the tolerance is compared against each vertex's own dropped mass. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_skinned_mesh_drop_gate_names_the_first_vertex_over_the_tolerance(void) {
    rigged_glb_write(RIG_GLB, NULL);
    nt_glb_scene_t scene;
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&scene, RIG_GLB));
    NtStreamLayout layout[3];
    skin_layout(layout, NT_STREAM_UINT8, NT_STREAM_UINT8, true);

    EXPECT_BUILD_ASSERT_MATCH(skin_try_decode(&scene, layout, 0.005F, RIGGED_GLB_SKIN_JOINT_COUNT), "drops more weight than the tolerance allows");
    TEST_ASSERT_NOT_NULL(strstr(s_log_last, "vertex 0 has 5 influences and loses 0.0100"));

    EXPECT_BUILD_ASSERT_MATCH(skin_try_decode(&scene, layout, NT_BUILDER_SKIN_DROP_TOLERANCE, RIGGED_GLB_SKIN_JOINT_COUNT), "drops more weight than the tolerance allows");
    TEST_ASSERT_NOT_NULL(strstr(s_log_last, "vertex 1 has 5 influences and loses 0.1000"));

    EXPECT_BUILD_ASSERT_MATCH(skin_try_decode(&scene, layout, 0.07F, RIGGED_GLB_SKIN_JOINT_COUNT), "drops more weight than the tolerance allows");
    TEST_ASSERT_NOT_NULL(strstr(s_log_last, "vertex 1 has 5 influences and loses 0.1000"));

    nt_builder_free_glb_scene(&scene);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_skinned_mesh_rejects_an_invalid_skin_layout(void) {
    rigged_glb_write(RIG_GLB, NULL);
    nt_glb_scene_t scene;
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&scene, RIG_GLB));

    NtStreamLayout layout[3];

    skin_layout(layout, NT_STREAM_UINT8, NT_STREAM_UINT8, true);
    layout[1].normalized = true;
    EXPECT_BUILD_ASSERT_MATCH(skin_try_decode(&scene, layout, SKIN_FIXTURE_TOLERANCE, RIGGED_GLB_SKIN_JOINT_COUNT), "JOINTS stream has an invalid type");

    skin_layout(layout, NT_STREAM_FLOAT32, NT_STREAM_UINT8, true);
    EXPECT_BUILD_ASSERT_MATCH(skin_try_decode(&scene, layout, SKIN_FIXTURE_TOLERANCE, RIGGED_GLB_SKIN_JOINT_COUNT), "JOINTS stream has an invalid type");

    /* A palette of 300 entries needs UINT16 lanes, whatever the fixture's own
     * palette size is; the ctx is what the export would carry. */
    skin_layout(layout, NT_STREAM_UINT8, NT_STREAM_UINT8, true);
    EXPECT_BUILD_ASSERT_MATCH(skin_try_decode(&scene, layout, SKIN_FIXTURE_TOLERANCE, 300), "cannot address this palette");

    /* Three FLOAT32 weight lanes keep the stride WebGL2-legal, so the component
     * count is the only rule left to fire. */
    skin_layout(layout, NT_STREAM_UINT8, NT_STREAM_FLOAT32, false);
    layout[2].count = 3;
    EXPECT_BUILD_ASSERT_MATCH(skin_try_decode(&scene, layout, SKIN_FIXTURE_TOLERANCE, RIGGED_GLB_SKIN_JOINT_COUNT), "must declare 4 components");

    skin_layout(layout, NT_STREAM_UINT8, NT_STREAM_UINT8, false);
    EXPECT_BUILD_ASSERT_MATCH(skin_try_decode(&scene, layout, SKIN_FIXTURE_TOLERANCE, RIGGED_GLB_SKIN_JOINT_COUNT), "WEIGHTS stream has an invalid type");

    skin_layout(layout, NT_STREAM_UINT8, NT_STREAM_INT16, true);
    EXPECT_BUILD_ASSERT_MATCH(skin_try_decode(&scene, layout, SKIN_FIXTURE_TOLERANCE, RIGGED_GLB_SKIN_JOINT_COUNT), "WEIGHTS stream has an invalid type");

    nt_builder_free_glb_scene(&scene);
}

/* The skin ctx and the JOINTS/WEIGHTS streams are one decision made in two
 * places, so each half without the other is a config contradiction. */
void test_skinned_mesh_rejects_a_layout_and_skin_that_disagree(void) {
    rigged_glb_write(RIG_GLB, NULL);
    nt_glb_scene_t scene;
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&scene, RIG_GLB));
    const nt_builder_skin_ctx_t skin = {.palette_count = RIGGED_GLB_SKIN_JOINT_COUNT, .drop_tolerance = SKIN_FIXTURE_TOLERANCE};
    uint8_t *data = NULL;
    uint32_t size = 0;

    NtStreamLayout layout[3];
    skin_layout(layout, NT_STREAM_UINT8, NT_STREAM_UINT8, true);
    /* JOINTS without WEIGHTS: the third stream extracts WEIGHTS_1 verbatim. */
    layout[2] = (NtStreamLayout){"weights_raw", "WEIGHTS_1", NT_STREAM_FLOAT32, 4, false, 0};
    EXPECT_BUILD_ASSERT_MATCH((void)nt_builder_decode_scene_mesh_skinned(&scene, 0, 0, layout, 3, NT_TANGENT_AUTO, &skin, &data, &size), "declares both a JOINTS and a WEIGHTS stream");

    /* The skin streams without a skin ctx, the unskinned entry point. */
    skin_layout(layout, NT_STREAM_UINT8, NT_STREAM_UINT8, true);
    EXPECT_BUILD_ASSERT_MATCH((void)nt_builder_decode_scene_mesh(&scene, 0, 0, layout, 3, NT_TANGENT_AUTO, &data, &size), "skin export and the JOINTS/WEIGHTS streams must agree");

    /* A skin ctx over a layout without the streams. */
    EXPECT_BUILD_ASSERT_MATCH((void)nt_builder_decode_scene_mesh_skinned(&scene, 0, 0, layout, 1, NT_TANGENT_AUTO, &skin, &data, &size), "skin export and the JOINTS/WEIGHTS streams must agree");

    nt_builder_free_glb_scene(&scene);
}

void test_skinned_mesh_rejects_a_negative_weight(void) { EXPECT_SKIN_ASSERT(negative_weight, "influence weight is negative"); }

void test_skinned_mesh_rejects_a_nan_weight(void) { EXPECT_SKIN_ASSERT(nan_weight, "influence weight is not finite"); }

void test_skinned_mesh_rejects_a_vertex_without_weight(void) { EXPECT_SKIN_ASSERT(zero_weights, "sum to zero"); }

void test_skinned_mesh_rejects_a_repeated_joint(void) { EXPECT_SKIN_ASSERT(duplicate_joint, "weights one joint twice"); }

void test_skinned_mesh_rejects_an_index_past_the_palette(void) { EXPECT_SKIN_ASSERT(index_ge_palette, "outside the palette"); }

void test_skinned_mesh_rejects_an_unpaired_set(void) { EXPECT_SKIN_ASSERT(unpaired_sets, "unpaired JOINTS_n/WEIGHTS_n set"); }

void test_skinned_mesh_rejects_a_gap_in_the_set_numbering(void) { EXPECT_SKIN_ASSERT(nonconsecutive_sets, "sets are not consecutive"); }

void test_skinned_mesh_rejects_float_joint_indices(void) { EXPECT_SKIN_ASSERT(joints_float_type, "JOINTS accessor has an invalid type"); }

void test_skinned_mesh_rejects_unnormalized_byte_weights(void) { EXPECT_SKIN_ASSERT(weights_bad_type, "WEIGHTS accessor has an invalid type"); }

void test_skinned_mesh_rejects_a_morph_target(void) { EXPECT_SKIN_ASSERT(morph_target, "morph targets"); }

/* Exporters pad unused lanes with any index; only a lane with weight counts,
 * on the mesh path and on the binding's reach scan alike. */
void test_skinned_mesh_ignores_the_index_of_a_zero_weight_lane(void) {
    rigged_glb_opts_t opts = {0};
    opts.zero_weight_lane_index = true;
    NtStreamLayout layout[3];
    skin_layout(layout, NT_STREAM_UINT8, NT_STREAM_UINT8, true);
    uint8_t *data = skin_decode(&opts, layout, SKIN_FIXTURE_TOLERANCE, RIGGED_GLB_SKIN_JOINT_COUNT);

    const uint8_t *joints = mesh_plane(data, 3, 1);
    for (uint32_t c = 0; c < 4U; c++) {
        TEST_ASSERT_EQUAL_UINT8(k_kept_joint[3][c], joints[12 + c]);
    }
    free(data);

    nt_glb_scene_t scene;
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&scene, RIG_GLB));
    nt_builder_rig_t rig;
    nt_builder_import_rig(&scene, 0, UINT32_MAX, &rig);
    (void)remove(PACK_PATH);
    NtBuilderContext *ctx = nt_builder_start_pack(PACK_PATH);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_builder_add_scene_skin_binding(ctx, &rig, "rigs/fixture.nskn");
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    nt_builder_free_pack(ctx);
    nt_builder_free_rig(&rig);
    nt_builder_free_glb_scene(&scene);
}

void test_skinned_mesh_accepts_a_primitive_without_indices(void) {
    rigged_glb_opts_t opts = {0};
    opts.no_indices = true;
    NtStreamLayout layout[3];
    skin_layout(layout, NT_STREAM_UINT8, NT_STREAM_UINT8, true);
    uint8_t *data = skin_decode(&opts, layout, SKIN_FIXTURE_TOLERANCE, RIGGED_GLB_SKIN_JOINT_COUNT);

    const NtMeshAssetHeader hdr = mesh_header(data);
    TEST_ASSERT_EQUAL_UINT32(3, hdr.vertex_count);
    TEST_ASSERT_EQUAL_UINT32(0, hdr.index_count);

    /* The reduction ran over the same vertices it runs over when indices exist. */
    const uint8_t *joints = mesh_plane(data, 3, 1);
    for (uint32_t v = 0; v < 3U; v++) {
        for (uint32_t c = 0; c < 4U; c++) {
            TEST_ASSERT_EQUAL_UINT8(k_kept_joint[v][c], joints[(v * 4U) + c]);
        }
    }

    free(data);
}

void test_add_scene_skinned_mesh_ships_the_decoded_bytes(void) {
    nt_glb_scene_t scene;
    nt_builder_rig_t rig;
    import_fixture_rig(&scene, &rig);

    NtStreamLayout layout[3];
    skin_layout(layout, NT_STREAM_UINT8, NT_STREAM_UINT8, true);
    const nt_mesh_opts_t opts = {.layout = layout, .stream_count = 3, .tangent_mode = NT_TANGENT_AUTO};

    (void)remove(PACK_PATH);
    NtBuilderContext *ctx = nt_builder_start_pack(PACK_PATH);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_builder_add_scene_skinned_mesh(ctx, &rig, 0, 0, SKIN_FIXTURE_TOLERANCE, "meshes/quad.mesh", &opts);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    nt_builder_free_pack(ctx);

    uint32_t size = 0;
    uint8_t *packed = read_pack_asset(PACK_PATH, NT_ASSET_MESH, &size);
    const nt_builder_skin_ctx_t skin = {.palette_count = rig.palette_count, .drop_tolerance = SKIN_FIXTURE_TOLERANCE};
    uint8_t *direct = NULL;
    uint32_t direct_size = 0;
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_decode_scene_mesh_skinned(&scene, 0, 0, layout, 3, NT_TANGENT_AUTO, &skin, &direct, &direct_size));
    TEST_ASSERT_EQUAL_UINT32(direct_size, size);
    TEST_ASSERT_EQUAL_MEMORY(direct, packed, size);

    free(direct);
    free(packed);
    nt_builder_free_rig(&rig);
    nt_builder_free_glb_scene(&scene);
}

void test_add_scene_skinned_mesh_asserts_when_no_node_binds_the_skin(void) {
    rigged_glb_opts_t opts = {0};
    opts.mesh_other_skin = true;
    rigged_glb_write(RIG_GLB, &opts);

    nt_glb_scene_t scene;
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&scene, RIG_GLB));
    /* The mesh node really moved to the second skin; rig 0 is a skin no node uses. */
    TEST_ASSERT_EQUAL_UINT32(1, scene.nodes[RIGGED_GLB_NODE_MESH].skin_index);
    nt_builder_rig_t rig;
    nt_builder_import_rig(&scene, 0, UINT32_MAX, &rig);
    TEST_ASSERT_EQUAL_UINT32(0, rig.skin_index);

    NtStreamLayout layout[3];
    skin_layout(layout, NT_STREAM_UINT8, NT_STREAM_UINT8, true);
    const nt_mesh_opts_t mesh_opts = {.layout = layout, .stream_count = 3, .tangent_mode = NT_TANGENT_AUTO};

    (void)remove(PACK_PATH);
    NtBuilderContext *ctx = nt_builder_start_pack(PACK_PATH);
    TEST_ASSERT_NOT_NULL(ctx);
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_add_scene_skinned_mesh(ctx, &rig, 0, 0, SKIN_FIXTURE_TOLERANCE, "meshes/quad.mesh", &mesh_opts), "not skinned by this rig's skin");
    nt_builder_free_pack(ctx);

    nt_builder_free_rig(&rig);
    nt_builder_free_glb_scene(&scene);
}

void test_add_scene_skin_binding_asserts_when_no_node_binds_the_skin(void) {
    rigged_glb_opts_t opts = {0};
    opts.mesh_other_skin = true;
    rigged_glb_write(RIG_GLB, &opts);

    nt_glb_scene_t scene;
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&scene, RIG_GLB));
    nt_builder_rig_t rig;
    nt_builder_import_rig(&scene, 0, UINT32_MAX, &rig);

    (void)remove(PACK_PATH);
    NtBuilderContext *ctx = nt_builder_start_pack(PACK_PATH);
    TEST_ASSERT_NOT_NULL(ctx);
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_add_scene_skin_binding(ctx, &rig, "rigs/fixture.nskn"), "no mesh is skinned by this rig's skin");
    nt_builder_free_pack(ctx);

    nt_builder_free_rig(&rig);
    nt_builder_free_glb_scene(&scene);
}
// #endregion

// #region skin binding export
/* Every source influence of the fixture, as rigged_glb.h documents it: the
 * binding's reach is measured before any reduction, so all five count. */
#define SKIN_SOURCE_INFLUENCES 5
static const uint32_t k_src_joint[RIGGED_GLB_VERTEX_COUNT][SKIN_SOURCE_INFLUENCES] = {{0, 1, 2, 3, 4}, {1, 0, 3, 2, 4}, {0, 1, 2, 3, 4}, {0, 1, 0, 0, 0}};
static const float k_src_weight[RIGGED_GLB_VERTEX_COUNT][SKIN_SOURCE_INFLUENCES] = {
    {0.40F, 0.30F, 0.20F, 0.09F, 0.01F},
    {0.40F, 0.30F, 0.10F, 0.10F, 0.10F},
    {0.40F, 0.30F, 0.15F, 0.10F, 0.05F},
    {0.75F, 0.25F, 0.0F, 0.0F, 0.0F},
};
static const float k_position[RIGGED_GLB_VERTEX_COUNT][3] = {{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}};

/* Inverse bind p is the pure translation the fixture writes into its accessor. */
static void ref_inverse_bind_t(uint32_t p, double out[3]) {
    out[0] = -((double)p + 1.0);
    out[1] = 0.5 * (double)p;
    out[2] = -1.0;
}

/* max |inverse_bind[p] * v| over the influences above, computed here from the
 * fixture's numbers rather than from the builder's scan. Both the influences
 * and the inverse binds are addressed by palette index, so the skin's joint
 * order does not enter here; it enters any_pose_radius below. identity_ibm
 * covers the skin that carries no inverseBindMatrices. */
static double ref_reach(bool identity_ibm) {
    double best = 0.0;
    for (uint32_t v = 0; v < RIGGED_GLB_VERTEX_COUNT; v++) {
        for (uint32_t i = 0; i < SKIN_SOURCE_INFLUENCES; i++) {
            if (k_src_weight[v][i] == 0.0F) {
                continue;
            }
            double t[3] = {0.0, 0.0, 0.0};
            if (!identity_ibm) {
                ref_inverse_bind_t(k_src_joint[v][i], t);
            }
            double len2 = 0.0;
            for (uint32_t c = 0; c < 3U; c++) {
                const double e = (double)k_position[v][c] + t[c];
                len2 += e * e;
            }
            const double d = sqrt(len2);
            if (d > best) {
                best = d;
            }
        }
    }
    return best;
}

/* The recurrence of skeletal-animation.md 14 over the rig reference table. */
static double ref_any_pose_radius(double reach) {
    double stretch[RIG_JOINT_COUNT];
    double distance[RIG_JOINT_COUNT];
    for (uint32_t j = 0; j < RIG_JOINT_COUNT; j++) {
        double m = 0.0;
        for (uint32_t c = 0; c < 3U; c++) {
            const double s = fabs((double)k_rig[j].s[c]);
            if (s > m) {
                m = s;
            }
        }
        if (k_rig[j].parent == NT_SKELETAL_NO_PARENT) {
            stretch[j] = m;
            distance[j] = 0.0;
        } else {
            const uint16_t parent = k_rig[j].parent;
            double t2 = 0.0;
            for (uint32_t c = 0; c < 3U; c++) {
                t2 += (double)k_rig[j].t[c] * (double)k_rig[j].t[c];
            }
            stretch[j] = stretch[parent] * m;
            distance[j] = distance[parent] + (stretch[parent] * sqrt(t2));
        }
    }
    double best = 0.0;
    for (uint32_t p = 0; p < RIGGED_GLB_SKIN_JOINT_COUNT; p++) {
        const uint32_t j = ref_palette_joint(p);
        const double r = distance[j] + (stretch[j] * reach);
        if (r > best) {
            best = r;
        }
    }
    return best;
}

/* The NSKN payload the pack shipped, plus a binding view into it. */
typedef struct {
    uint8_t *payload;
    uint32_t size;
    NtSknHeader header;
    nt_skeletal_mat34_t inverse_bind[RIGGED_GLB_SKIN_JOINT_COUNT];
    uint16_t remap[RIGGED_GLB_SKIN_JOINT_COUNT];
    nt_skin_binding_t binding; /* view over the two arrays above */
} skn_result_t;

/* The wire layout is the runtime layout, so the arrays are copied out of the
 * payload unchanged and the view is built over them. */
static void skn_read(skn_result_t *out) {
    out->payload = read_pack_asset(PACK_PATH, NT_ASSET_SKIN_BINDING, &out->size);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)NT_SKN_SIZE(RIGGED_GLB_SKIN_JOINT_COUNT), out->size);
    memcpy(&out->header, out->payload, sizeof(out->header));
    TEST_ASSERT_EQUAL_UINT16(RIGGED_GLB_SKIN_JOINT_COUNT, out->header.palette_count);
    memcpy(out->inverse_bind, out->payload + sizeof(NtSknHeader), sizeof(out->inverse_bind));
    memcpy(out->remap, out->payload + sizeof(NtSknHeader) + sizeof(out->inverse_bind), sizeof(out->remap));
    out->binding = (nt_skin_binding_t){
        .rig_compat_id = (nt_hash64_t){out->header.rig_compat_id},
        .remap = out->remap,
        .inverse_bind = out->inverse_bind,
        .reach = out->header.reach,
        .any_pose_radius = out->header.any_pose_radius,
        .palette_count = out->header.palette_count,
    };
}

/* Writes the fixture, imports the rig and packs its binding; skinned_first adds
 * the mesh before the binding, to pin that reach does not depend on the order. */
static void skn_export(const rigged_glb_opts_t *opts, bool skinned_first, nt_glb_scene_t *scene, nt_builder_rig_t *rig, skn_result_t *out) {
    rigged_glb_write(RIG_GLB, opts);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(scene, RIG_GLB));
    nt_builder_import_rig(scene, 0, UINT32_MAX, rig);

    (void)remove(PACK_PATH);
    NtBuilderContext *ctx = nt_builder_start_pack(PACK_PATH);
    TEST_ASSERT_NOT_NULL(ctx);
    if (skinned_first) {
        NtStreamLayout layout[3];
        skin_layout(layout, NT_STREAM_UINT8, NT_STREAM_UINT8, true);
        const nt_mesh_opts_t mesh_opts = {.layout = layout, .stream_count = 3, .tangent_mode = NT_TANGENT_AUTO};
        nt_builder_add_scene_skinned_mesh(ctx, rig, 0, 0, SKIN_FIXTURE_TOLERANCE, "meshes/quad.mesh", &mesh_opts);
    }
    nt_builder_add_scene_skin_binding(ctx, rig, "rigs/fixture.nskn");
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    nt_builder_free_pack(ctx);

    skn_read(out);
}

static void assert_close(double expected, double actual) {
    const double scale = (expected < 0.0 ? -expected : expected) + 1.0;
    TEST_ASSERT_TRUE(fabs(expected - actual) <= 1e-6 * scale);
}

void test_skin_binding_bounds_match_an_independent_computation(void) {
    nt_glb_scene_t scene;
    nt_builder_rig_t rig;
    skn_result_t skn;
    skn_export(NULL, false, &scene, &rig, &skn);

    const double reach = ref_reach(false);
    assert_close(reach, (double)skn.header.reach);
    assert_close(ref_any_pose_radius(reach), (double)skn.header.any_pose_radius);
    /* Stored as float, each bound still contains the double it was measured as. */
    TEST_ASSERT_TRUE((double)skn.header.reach >= reach);
    TEST_ASSERT_TRUE((double)skn.header.any_pose_radius >= ref_any_pose_radius(reach));
    TEST_ASSERT_EQUAL_HEX64(rig.skeleton.rig_compat_id.value, skn.header.rig_compat_id);
    for (uint16_t p = 0; p < RIGGED_GLB_SKIN_JOINT_COUNT; p++) {
        TEST_ASSERT_EQUAL_UINT16(ref_palette_joint(p), skn.binding.remap[p]);
    }

    free(skn.payload);
    nt_builder_free_rig(&rig);
    nt_builder_free_glb_scene(&scene);
}

void test_skin_binding_reach_is_the_same_in_either_export_order(void) {
    nt_glb_scene_t scene;
    nt_builder_rig_t rig;
    skn_result_t binding_first;
    skn_export(NULL, false, &scene, &rig, &binding_first);
    nt_builder_free_rig(&rig);
    nt_builder_free_glb_scene(&scene);

    skn_result_t mesh_first;
    skn_export(NULL, true, &scene, &rig, &mesh_first);

    TEST_ASSERT_EQUAL_UINT32(binding_first.size, mesh_first.size);
    TEST_ASSERT_EQUAL_MEMORY(binding_first.payload, mesh_first.payload, mesh_first.size);

    free(binding_first.payload);
    free(mesh_first.payload);
    nt_builder_free_rig(&rig);
    nt_builder_free_glb_scene(&scene);
}

void test_skin_binding_imports_the_inverse_binds_instead_of_deriving_them(void) {
    nt_glb_scene_t scene;
    nt_builder_rig_t rig;
    skn_result_t skn;
    skn_export(NULL, false, &scene, &rig, &skn);

    for (uint32_t p = 0; p < RIGGED_GLB_SKIN_JOINT_COUNT; p++) {
        double t[3];
        ref_inverse_bind_t(p, t);
        const nt_skeletal_mat34_t *ib = &skn.binding.inverse_bind[p];
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) {
                ASSERT_F32((r == c) ? 1.0F : 0.0F, ib->r[r][c]);
            }
            ASSERT_F32((float)t[r], ib->r[r][3]);
        }
    }

    free(skn.payload);
    nt_builder_free_rig(&rig);
    nt_builder_free_glb_scene(&scene);
}

void test_skin_binding_uses_identity_when_the_skin_has_no_inverse_binds(void) {
    rigged_glb_opts_t opts = {0};
    opts.no_ibm = true;
    nt_glb_scene_t scene;
    nt_builder_rig_t rig;
    skn_result_t skn;
    skn_export(&opts, false, &scene, &rig, &skn);

    for (uint32_t p = 0; p < RIGGED_GLB_SKIN_JOINT_COUNT; p++) {
        const nt_skeletal_mat34_t *ib = &skn.binding.inverse_bind[p];
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 4; c++) {
                ASSERT_F32((r == c) ? 1.0F : 0.0F, ib->r[r][c]);
            }
        }
    }
    /* Identity binds measure the vertices from the mesh origin. */
    assert_close(ref_reach(true), (double)skn.header.reach);

    free(skn.payload);
    nt_builder_free_rig(&rig);
    nt_builder_free_glb_scene(&scene);
}

/* One skin knob, one assert text on the binding export path. */
#define EXPECT_BINDING_ASSERT(field, expected)                                                                                                                                                         \
    do {                                                                                                                                                                                               \
        rigged_glb_opts_t knob_opts = {0};                                                                                                                                                             \
        knob_opts.field = true;                                                                                                                                                                        \
        rigged_glb_write(RIG_GLB, &knob_opts);                                                                                                                                                         \
        nt_glb_scene_t knob_scene;                                                                                                                                                                     \
        TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&knob_scene, RIG_GLB));                                                                                                              \
        nt_builder_rig_t knob_rig;                                                                                                                                                                     \
        nt_builder_import_rig(&knob_scene, 0, UINT32_MAX, &knob_rig);                                                                                                                                  \
        (void)remove(PACK_PATH);                                                                                                                                                                       \
        NtBuilderContext *knob_ctx = nt_builder_start_pack(PACK_PATH);                                                                                                                                 \
        TEST_ASSERT_NOT_NULL(knob_ctx);                                                                                                                                                                \
        EXPECT_BUILD_ASSERT_MATCH(nt_builder_add_scene_skin_binding(knob_ctx, &knob_rig, "rigs/fixture.nskn"), expected);                                                                              \
        nt_builder_free_pack(knob_ctx);                                                                                                                                                                \
        nt_builder_free_rig(&knob_rig);                                                                                                                                                                \
        nt_builder_free_glb_scene(&knob_scene);                                                                                                                                                        \
    } while (0)

void test_skin_binding_rejects_a_short_inverse_bind_accessor(void) { EXPECT_BINDING_ASSERT(ibm_short, "inverseBindMatrices accessor is invalid"); }

void test_skin_binding_rejects_a_non_mat4_inverse_bind_accessor(void) { EXPECT_BINDING_ASSERT(ibm_bad_type, "inverseBindMatrices accessor is invalid"); }

void test_skin_binding_rejects_a_nan_inverse_bind_element(void) { EXPECT_BINDING_ASSERT(ibm_nan, "inverse bind matrix is not finite"); }

void test_skin_binding_rejects_a_projective_inverse_bind(void) { EXPECT_BINDING_ASSERT(ibm_projective, "inverse bind matrix is not affine"); }

/* The reach scan bounds every weighted lane itself; it does not rely on the
 * mesh export having run first. */
void test_skin_binding_rejects_an_index_past_the_palette(void) { EXPECT_BINDING_ASSERT(index_ge_palette, "outside the palette"); }

/* reach is a property of the skin, so geometry the build never exports still
 * counts: the far triangle's first vertex sets it, whether it is a second
 * primitive of the quad's mesh or a second mesh on another node with the same
 * skin, while only the quad (mesh 0, primitive 0) goes into the pack. */
static void check_far_reach(const rigged_glb_opts_t *opts) {
    nt_glb_scene_t scene;
    nt_builder_rig_t rig;
    skn_result_t skn;
    skn_export(opts, true, &scene, &rig, &skn);

    /* (10, 0, 0) through inverse bind 0 = (9, 0, -1), as rigged_glb.h states. */
    const double far_reach = sqrt(82.0);
    TEST_ASSERT_TRUE(far_reach > ref_reach(false));
    assert_close(far_reach, (double)skn.header.reach);
    assert_close(ref_any_pose_radius(far_reach), (double)skn.header.any_pose_radius);

    free(skn.payload);
    nt_builder_free_rig(&rig);
    nt_builder_free_glb_scene(&scene);
}

void test_skin_binding_reach_covers_a_primitive_that_is_not_exported(void) {
    rigged_glb_opts_t opts = {0};
    opts.second_primitive_far = true;
    check_far_reach(&opts);
}

void test_skin_binding_reach_covers_a_second_node_of_the_skin(void) {
    rigged_glb_opts_t opts = {0};
    opts.second_node_far = true;
    check_far_reach(&opts);
}
// #endregion

// #region Khronos sample assets
/* The two rigs examples/skeletal_showcase/raw ships, imported end to end. The
 * fixture pins every rule; these pin that the rules hold on glTF nobody here
 * authored -- CesiumMan's two matrix wrappers above the joints, Fox's 24-entry
 * palette. Every importer diagnostic precedes an NT_BUILD_ASSERT, so a call
 * that returns at all logged no error. */

typedef struct {
    const char *path;
    uint16_t joint_count;   /* nodes on the paths from the scene root to the skin joints */
    uint16_t palette_count; /* entries of skin.joints */
    bool has_normal;        /* the primitives carry a NORMAL accessor */
    const char *joint_name[2];
    uint64_t rig_compat_id; /* golden: the identity this importer gives the asset */
} khronos_rig_t;

/* The two ids are pinned from a run of this importer, so any change to the
 * rest bits, the joint order or the id schema shows up as a mismatch here. */
static const khronos_rig_t k_khronos[2] = {
    {"examples/skeletal_showcase/raw/Fox.glb", 25, 24, false, {"root", NULL}, 0xC0AE326CC534D680ULL},
    {"examples/skeletal_showcase/raw/CesiumMan.glb", 21, 19, true, {"Z_UP", "Armature"}, 0x881DB664F6FDE289ULL},
};

/* Palette entry p of the rig is the joint named like the node the glTF skin
 * lists at p, read straight from the cgltf skin rather than through the
 * builder's tables. */
static void khronos_check_palette_nodes(const nt_glb_scene_t *scene, const nt_builder_rig_t *rig) {
    const cgltf_data *data = (const cgltf_data *)scene->_internal;
    TEST_ASSERT_NOT_NULL(data);
    const cgltf_skin *skin = &data->skins[rig->skin_index];
    TEST_ASSERT_EQUAL_UINT32((uint32_t)skin->joints_count, rig->palette_count);
    for (uint16_t p = 0; p < rig->palette_count; p++) {
        TEST_ASSERT_TRUE(rig->palette_joint[p] < rig->skeleton.joint_count);
        TEST_ASSERT_EQUAL_HEX32(nt_hash32_str(skin->joints[p]->name).value, rig->skeleton.joint_id[rig->palette_joint[p]]);
    }
}

/* What the wrappers decompose to on the real asset: Z_UP is -90 degrees about
 * X, Armature -90 degrees about Z with the exporter's cos(90 deg) noise of
 * -4.37e-8 on the diagonal; both pin to the exact sqrt(1/2) bits with w >= 0.
 * Fox's root has no TRS at all and must come through as bit-exact identity. */
static void khronos_check_rest(const khronos_rig_t *asset, const nt_builder_rig_t *rig) {
    if (asset == &k_khronos[0]) {
        const nt_skeletal_trs_t *root = &rig->skeleton.rest[0];
        for (int c = 0; c < 3; c++) {
            TEST_ASSERT_EQUAL_HEX32(0x00000000U, f32_bits(root->t[c]));
            TEST_ASSERT_EQUAL_HEX32(0x3F800000U, f32_bits(root->s[c]));
            TEST_ASSERT_EQUAL_HEX32(0x00000000U, f32_bits(root->q[c]));
        }
        TEST_ASSERT_EQUAL_HEX32(0x3F800000U, f32_bits(root->q[3]));
        return;
    }
    static const uint32_t k_z_up_q[4] = {0xBF3504F3U, 0x00000000U, 0x00000000U, 0x3F3504F3U};
    static const uint32_t k_armature_q[4] = {0x00000000U, 0x00000000U, 0xBF3504F3U, 0x3F3504F3U};
    for (int c = 0; c < 4; c++) {
        TEST_ASSERT_EQUAL_HEX32(k_z_up_q[c], f32_bits(rig->skeleton.rest[0].q[c]));
        TEST_ASSERT_EQUAL_HEX32(k_armature_q[c], f32_bits(rig->skeleton.rest[1].q[c]));
    }
    for (int j = 0; j < 2; j++) {
        for (int c = 0; c < 3; c++) {
            TEST_ASSERT_EQUAL_HEX32(0x00000000U, f32_bits(rig->skeleton.rest[j].t[c]));
            TEST_ASSERT_EQUAL_HEX32(0x3F800000U, f32_bits(rig->skeleton.rest[j].s[c]));
        }
    }
}

/* Both palettes fit in a byte, so the joint lanes are UINT8; returns the stream
 * count, since NORMAL is present in one asset and absent in the other. */
static uint32_t khronos_layout(NtStreamLayout out[4], bool has_normal) {
    uint32_t n = 0;
    out[n++] = (NtStreamLayout){"position", "POSITION", NT_STREAM_FLOAT32, 3, false, 0};
    if (has_normal) {
        out[n++] = (NtStreamLayout){"normal", "NORMAL", NT_STREAM_FLOAT32, 3, false, 0};
    }
    out[n++] = (NtStreamLayout){"joints", "JOINTS", NT_STREAM_UINT8, 4, false, 0};
    out[n++] = (NtStreamLayout){"weights", "WEIGHTS", NT_STREAM_UINT8, 4, true, 0};
    return n;
}

/* The node whose mesh this rig's skin deforms; its primitives are what the
 * export walks and what the binding measures reach over. */
static uint32_t khronos_skinned_node(const nt_glb_scene_t *scene, uint32_t skin_index) {
    for (uint32_t i = 0; i < scene->node_count; i++) {
        if (scene->nodes[i].skin_index == skin_index && scene->nodes[i].mesh_index != UINT32_MAX) {
            return i;
        }
    }
    TEST_FAIL_MESSAGE("no node instantiates a mesh with the rig's skin");
    return UINT32_MAX;
}

/* The section 14 recurrence over the imported rig's own tables: it pins that
 * the stored bound came from this rig's rest, remap and reach, not the formula
 * itself, which the fixture test pins against k_rig. */
static double khronos_ref_any_pose_radius(const nt_builder_rig_t *rig, double reach) {
    const uint32_t joint_count = rig->skeleton.joint_count;
    double *stretch = (double *)calloc(joint_count, sizeof(double));
    double *distance = (double *)calloc(joint_count, sizeof(double));
    TEST_ASSERT_NOT_NULL(stretch);
    TEST_ASSERT_NOT_NULL(distance);
    for (uint32_t j = 0; j < joint_count; j++) {
        const nt_skeletal_trs_t *rest = &rig->skeleton.rest[j];
        double m = 0.0;
        for (int c = 0; c < 3; c++) {
            m = fmax(m, fabs((double)rest->s[c]));
        }
        const uint16_t parent = rig->skeleton.parent[j];
        if (parent == NT_SKELETAL_NO_PARENT) {
            stretch[j] = m;
            distance[j] = 0.0;
        } else {
            double t2 = 0.0;
            for (int c = 0; c < 3; c++) {
                t2 += (double)rest->t[c] * (double)rest->t[c];
            }
            stretch[j] = stretch[parent] * m;
            distance[j] = distance[parent] + (stretch[parent] * sqrt(t2));
        }
    }
    double best = 0.0;
    for (uint16_t p = 0; p < rig->palette_count; p++) {
        const uint16_t j = rig->palette_joint[p];
        best = fmax(best, distance[j] + (stretch[j] * reach));
    }
    free(stretch);
    free(distance);
    return best;
}

/* The NSKN the pack shipped: the two bounds and a remap that stays inside the
 * rig the same build imported. */
static void khronos_check_binding(const khronos_rig_t *asset, const nt_builder_rig_t *rig) {
    uint32_t size = 0;
    uint8_t *skn = read_pack_asset(PACK_PATH, NT_ASSET_SKIN_BINDING, &size);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)NT_SKN_SIZE(asset->palette_count), size);
    NtSknHeader header;
    memcpy(&header, skn, sizeof(header));
    TEST_ASSERT_EQUAL_UINT16(asset->palette_count, header.palette_count);
    TEST_ASSERT_EQUAL_HEX64(rig->skeleton.rig_compat_id.value, header.rig_compat_id);
    TEST_ASSERT_TRUE_MESSAGE(header.reach > 0.0F, "a skinned vertex sits away from the joint that weights it");
    assert_close(khronos_ref_any_pose_radius(rig, (double)header.reach), (double)header.any_pose_radius);

    const size_t remap_at = sizeof(NtSknHeader) + (sizeof(nt_skeletal_mat34_t) * (size_t)header.palette_count);
    for (uint16_t p = 0; p < header.palette_count; p++) {
        uint16_t remap = 0;
        memcpy(&remap, skn + remap_at + ((size_t)p * sizeof(remap)), sizeof(remap));
        TEST_ASSERT_EQUAL_UINT16(rig->palette_joint[p], remap);
    }
    free(skn);
}

/* The first MESH of the pack is primitive 0 of the skinned node. Its lanes are
 * the reduction's output on thousands of real vertices, which the fixture
 * cannot show: every index addresses the palette and every quantized vertex
 * sums to exactly 255. */
static void khronos_check_mesh(const khronos_rig_t *asset, uint32_t stream_count) {
    uint32_t size = 0;
    uint8_t *packed = read_pack_asset(PACK_PATH, NT_ASSET_MESH, &size);
    const NtMeshAssetHeader header = mesh_header(packed);
    TEST_ASSERT_EQUAL_UINT8(stream_count, header.stream_count);
    TEST_ASSERT_TRUE(header.vertex_count > 0U);

    const uint8_t *joints = mesh_plane(packed, stream_count, stream_count - 2U);
    const uint8_t *weights = mesh_plane(packed, stream_count, stream_count - 1U);
    for (uint32_t v = 0; v < header.vertex_count; v++) {
        uint32_t sum = 0;
        for (uint32_t c = 0; c < 4U; c++) {
            TEST_ASSERT_TRUE(joints[((size_t)v * 4U) + c] < asset->palette_count);
            sum += weights[((size_t)v * 4U) + c];
        }
        TEST_ASSERT_EQUAL_UINT32(255, sum);
    }
    free(packed);
}

static void khronos_import_case(const khronos_rig_t *asset) {
    nt_glb_scene_t scene;
    TEST_ASSERT_EQUAL_MESSAGE(NT_BUILD_OK, nt_builder_parse_glb_scene(&scene, asset->path), asset->path);

    nt_builder_rig_t rig;
    nt_builder_import_rig(&scene, 0, UINT32_MAX, &rig);

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(asset->joint_count, rig.skeleton.joint_count, asset->path);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(asset->palette_count, rig.palette_count, asset->path);
    TEST_ASSERT_EQUAL_UINT16(NT_SKELETAL_NO_PARENT, rig.skeleton.parent[0]);
    TEST_ASSERT_EQUAL_HEX64_MESSAGE(asset->rig_compat_id, rig.skeleton.rig_compat_id.value, asset->path);
    for (uint32_t j = 0; j < 2U; j++) {
        if (asset->joint_name[j] != NULL) {
            TEST_ASSERT_EQUAL_HEX32_MESSAGE(nt_hash32_str(asset->joint_name[j]).value, rig.skeleton.joint_id[j], asset->joint_name[j]);
        }
    }
    khronos_check_palette_nodes(&scene, &rig);
    khronos_check_rest(asset, &rig);

    NtStreamLayout layout[4];
    const uint32_t stream_count = khronos_layout(layout, asset->has_normal);
    const nt_mesh_opts_t mesh_opts = {.layout = layout, .stream_count = stream_count, .tangent_mode = NT_TANGENT_AUTO};

    const uint32_t node = khronos_skinned_node(&scene, rig.skin_index);
    const uint32_t mesh = scene.nodes[node].mesh_index;
    const uint32_t primitive_count = scene.meshes[mesh].primitive_count;
    TEST_ASSERT_TRUE(primitive_count > 0U);

    /* The default budget carries both assets: neither vertex list loses weight
     * to the reduction, because both primitives declare one influence set. */
    (void)remove(PACK_PATH);
    NtBuilderContext *ctx = nt_builder_start_pack(PACK_PATH);
    TEST_ASSERT_NOT_NULL(ctx);
    for (uint32_t p = 0; p < primitive_count; p++) {
        char rid[64];
        (void)snprintf(rid, sizeof(rid), "meshes/khronos_%u.mesh", p);
        nt_builder_add_scene_skinned_mesh(ctx, &rig, mesh, p, NT_BUILDER_SKIN_DROP_TOLERANCE, rid, &mesh_opts);
    }
    nt_builder_add_scene_skin_binding(ctx, &rig, "rigs/khronos.nskn");
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    nt_builder_free_pack(ctx);

    khronos_check_binding(asset, &rig);
    khronos_check_mesh(asset, stream_count);

    /* Identity is a property of the asset, not of one traversal. */
    nt_glb_scene_t again;
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&again, asset->path));
    nt_builder_rig_t reimported;
    nt_builder_import_rig(&again, 0, UINT32_MAX, &reimported);
    TEST_ASSERT_EQUAL_HEX64(rig.skeleton.rig_compat_id.value, reimported.skeleton.rig_compat_id.value);
    nt_builder_free_rig(&reimported);
    nt_builder_free_glb_scene(&again);

    nt_builder_free_rig(&rig);
    nt_builder_free_glb_scene(&scene);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, s_log_warnings, "a clean asset imports without a WARN or ERROR line");
}

void test_fox_imports_rig_binding_and_skinned_mesh(void) { khronos_import_case(&k_khronos[0]); }

void test_cesiumman_imports_rig_binding_and_skinned_mesh(void) { khronos_import_case(&k_khronos[1]); }
// #endregion

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_scene_publishes_the_node_graph);
    RUN_TEST(test_parse_asserts_on_a_parent_cycle);
    RUN_TEST(test_parse_asserts_on_an_accessor_covering_fewer_vertices);
    RUN_TEST(test_rig_import_preorder_and_subtree_ranges);
    RUN_TEST(test_rig_compat_id_matches_the_hand_written_schema);
    RUN_TEST(test_rig_cut_at_helper_drops_the_scene_root);
    RUN_TEST(test_decompose_reflection_gives_a_negative_x_scale);
    RUN_TEST(test_decompose_asserts_on_a_zero_scale);
    RUN_TEST(test_decompose_asserts_on_a_projective_bottom_row);
    RUN_TEST(test_decompose_asserts_on_a_non_finite_element);
    RUN_TEST(test_import_asserts_on_a_sheared_matrix);
    RUN_TEST(test_import_asserts_on_a_sheared_small_scale_matrix);
    RUN_TEST(test_import_decomposes_a_small_scale_wrapper);
    RUN_TEST(test_import_asserts_on_a_missing_skin);
    RUN_TEST(test_import_asserts_on_an_unnamed_rig_node);
    RUN_TEST(test_import_asserts_on_an_empty_rig_node_name);
    RUN_TEST(test_import_asserts_on_two_rig_nodes_sharing_one_joint_id);
    RUN_TEST(test_import_asserts_on_a_non_unit_rest_rotation);
    RUN_TEST(test_import_asserts_on_a_matrix_node_with_trs);
    RUN_TEST(test_import_asserts_on_a_skin_listing_one_joint_twice);
    RUN_TEST(test_import_asserts_on_a_joint_chain_deeper_than_the_cap);
    RUN_TEST(test_import_asserts_on_joints_under_several_scene_roots);
    RUN_TEST(test_import_asserts_on_a_joint_outside_the_cut);
    RUN_TEST(test_skinned_mesh_keeps_the_four_heaviest_influences);
    RUN_TEST(test_skinned_mesh_quantizes_a_half_half_tie_to_the_lower_lane);
    RUN_TEST(test_skinned_mesh_float32_weights_are_the_renormalized_values);
    RUN_TEST(test_skinned_mesh_float16_weights_round_trip);
    RUN_TEST(test_skinned_mesh_drop_gate_names_the_first_vertex_over_the_tolerance);
    RUN_TEST(test_skinned_mesh_rejects_an_invalid_skin_layout);
    RUN_TEST(test_skinned_mesh_rejects_a_layout_and_skin_that_disagree);
    RUN_TEST(test_skinned_mesh_rejects_a_negative_weight);
    RUN_TEST(test_skinned_mesh_rejects_a_nan_weight);
    RUN_TEST(test_skinned_mesh_rejects_a_vertex_without_weight);
    RUN_TEST(test_skinned_mesh_rejects_a_repeated_joint);
    RUN_TEST(test_skinned_mesh_rejects_an_index_past_the_palette);
    RUN_TEST(test_skinned_mesh_rejects_an_unpaired_set);
    RUN_TEST(test_skinned_mesh_rejects_a_gap_in_the_set_numbering);
    RUN_TEST(test_skinned_mesh_rejects_float_joint_indices);
    RUN_TEST(test_skinned_mesh_rejects_unnormalized_byte_weights);
    RUN_TEST(test_skinned_mesh_rejects_a_morph_target);
    RUN_TEST(test_skinned_mesh_ignores_the_index_of_a_zero_weight_lane);
    RUN_TEST(test_skinned_mesh_accepts_a_primitive_without_indices);
    RUN_TEST(test_add_scene_skinned_mesh_ships_the_decoded_bytes);
    RUN_TEST(test_add_scene_skinned_mesh_asserts_when_no_node_binds_the_skin);
    RUN_TEST(test_add_scene_skin_binding_asserts_when_no_node_binds_the_skin);
    RUN_TEST(test_skin_binding_bounds_match_an_independent_computation);
    RUN_TEST(test_skin_binding_reach_is_the_same_in_either_export_order);
    RUN_TEST(test_skin_binding_imports_the_inverse_binds_instead_of_deriving_them);
    RUN_TEST(test_skin_binding_uses_identity_when_the_skin_has_no_inverse_binds);
    RUN_TEST(test_skin_binding_rejects_a_short_inverse_bind_accessor);
    RUN_TEST(test_skin_binding_rejects_a_non_mat4_inverse_bind_accessor);
    RUN_TEST(test_skin_binding_rejects_a_nan_inverse_bind_element);
    RUN_TEST(test_skin_binding_rejects_a_projective_inverse_bind);
    RUN_TEST(test_skin_binding_rejects_an_index_past_the_palette);
    RUN_TEST(test_skin_binding_reach_covers_a_primitive_that_is_not_exported);
    RUN_TEST(test_skin_binding_reach_covers_a_second_node_of_the_skin);
    RUN_TEST(test_fox_imports_rig_binding_and_skinned_mesh);
    RUN_TEST(test_cesiumman_imports_rig_binding_and_skinned_mesh);
    return UNITY_END();
}
