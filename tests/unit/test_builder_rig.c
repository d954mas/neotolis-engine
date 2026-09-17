/* Rig import from glTF: what nt_builder_parse_glb_scene publishes about the
 * node graph, the skins and the animations of a rigged glTF, and the rig
 * nt_builder_import_rig derives from it -- joint order, rest pose, matrix
 * decomposition and identity. The fixture is written by rigged_glb.c, so every
 * expectation here names a value documented in rigged_glb.h. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <setjmp.h>
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
#include "hash/nt_hash.h"
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

void setUp(void) {
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
}
void tearDown(void) {}

// #region build-assert trap
/* Same shape as test_builder_skeletal.c: the parse aborts before it allocates,
 * so the jump leaves nothing behind. */
static jmp_buf s_build_assert_jmp;
static const char *s_build_assert_expr;

static void test_build_assert_handler(const char *expr, const char *file, int line) {
    s_build_assert_expr = expr;
    (void)file;
    (void)line;
    longjmp(s_build_assert_jmp, 1);
}

/* Which rule fired is the claim: a death test that only sees "some assert"
 * passes on an unrelated precondition too. */
#define EXPECT_BUILD_ASSERT_MATCH(code, expected)                                                                                                                                                      \
    do {                                                                                                                                                                                               \
        s_build_assert_expr = NULL;                                                                                                                                                                    \
        nt_build_assert_handler = test_build_assert_handler;                                                                                                                                           \
        if (setjmp(s_build_assert_jmp) == 0) {                                                                                                                                                         \
            code;                                                                                                                                                                                      \
            nt_build_assert_handler = NULL;                                                                                                                                                            \
            TEST_FAIL_MESSAGE("expected NT_BUILD_ASSERT to fire: " expected);                                                                                                                          \
        }                                                                                                                                                                                              \
        nt_build_assert_handler = NULL;                                                                                                                                                                \
        TEST_ASSERT_TRUE_MESSAGE(s_build_assert_expr &&strstr(s_build_assert_expr, (expected)), "a different NT_BUILD_ASSERT fired: " expected);                                                       \
    } while (0)
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

// #region tests
void test_rigged_fixture_parses(void) {
    rigged_glb_write(RIG_GLB, NULL);

    nt_glb_scene_t scene;
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&scene, RIG_GLB));
    TEST_ASSERT_EQUAL_UINT32(RIGGED_GLB_NODE_COUNT, scene.node_count);
    TEST_ASSERT_EQUAL_UINT32(1, scene.mesh_count);

    nt_builder_free_glb_scene(&scene);
}

void test_scene_publishes_the_node_graph(void) {
    rigged_glb_write(RIG_GLB, NULL);

    nt_glb_scene_t scene;
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&scene, RIG_GLB));
    TEST_ASSERT_EQUAL_UINT32(RIGGED_GLB_NODE_COUNT, scene.node_count);

    static const uint32_t k_parent[RIGGED_GLB_NODE_COUNT] = {
        UINT32_MAX,           RIGGED_GLB_NODE_ROOT, RIGGED_GLB_NODE_HELPER, RIGGED_GLB_NODE_JOINT0, RIGGED_GLB_NODE_JOINT1, RIGGED_GLB_NODE_JOINT0, RIGGED_GLB_NODE_JOINT3,
        RIGGED_GLB_NODE_ROOT, RIGGED_GLB_NODE_ROOT,
    };
    for (uint32_t i = 0; i < RIGGED_GLB_NODE_COUNT; i++) {
        TEST_ASSERT_EQUAL_UINT32(k_parent[i], scene.nodes[i].parent);
    }

    /* Only the two wrapper nodes carry a glTF matrix. */
    for (uint32_t i = 0; i < RIGGED_GLB_NODE_COUNT; i++) {
        bool expected = i == RIGGED_GLB_NODE_ROOT || i == RIGGED_GLB_NODE_HELPER;
        TEST_ASSERT_EQUAL_INT(expected ? 1 : 0, scene.nodes[i].has_matrix ? 1 : 0);
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

void test_scene_publishes_local_trs(void) {
    rigged_glb_write(RIG_GLB, NULL);

    nt_glb_scene_t scene;
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&scene, RIG_GLB));

    /* Joint 0 and joint 1 of the skin carry the rest values published in
     * skeletal-animation.md 3.1, including the -0 the id schema canonicalizes. */
    const nt_glb_node_t *j0 = &scene.nodes[RIGGED_GLB_NODE_JOINT0];
    ASSERT_F32(1.0F, j0->local_t[0]);
    ASSERT_F32(2.0F, j0->local_t[1]);
    ASSERT_F32(3.0F, j0->local_t[2]);
    ASSERT_F32(0.0F, j0->local_q[0]);
    ASSERT_F32(0.0F, j0->local_q[1]);
    ASSERT_F32(0.0F, j0->local_q[2]);
    ASSERT_F32(1.0F, j0->local_q[3]);
    ASSERT_F32(1.0F, j0->local_s[0]);
    ASSERT_F32(1.0F, j0->local_s[1]);
    ASSERT_F32(1.0F, j0->local_s[2]);

    const nt_glb_node_t *j1 = &scene.nodes[RIGGED_GLB_NODE_JOINT1];
    ASSERT_F32(0.0F, j1->local_t[0]);
    ASSERT_F32(-0.0F, j1->local_t[1]);
    ASSERT_F32(0.5F, j1->local_t[2]);
    ASSERT_F32(0.0F, j1->local_q[0]);
    ASSERT_F32(0.0F, j1->local_q[1]);
    ASSERT_F32(-0.70710678F, j1->local_q[2]);
    ASSERT_F32(-0.70710678F, j1->local_q[3]);

    /* A node that is neither a joint nor a wrapper keeps its own TRS. */
    const nt_glb_node_t *mesh_node = &scene.nodes[RIGGED_GLB_NODE_MESH];
    ASSERT_F32(2.0F, mesh_node->local_t[0]);
    ASSERT_F32(0.0F, mesh_node->local_t[1]);
    ASSERT_F32(-1.0F, mesh_node->local_t[2]);
    ASSERT_F32(0.38268343F, mesh_node->local_q[1]);
    ASSERT_F32(0.92387953F, mesh_node->local_q[3]);
    ASSERT_F32(1.5F, mesh_node->local_s[0]);
    ASSERT_F32(1.5F, mesh_node->local_s[1]);
    ASSERT_F32(1.5F, mesh_node->local_s[2]);

    /* Default TRS reaches the caller as identity, not as zeros. */
    const nt_glb_node_t *object = &scene.nodes[RIGGED_GLB_NODE_OBJECT];
    ASSERT_F32(5.0F, object->local_t[2]);
    ASSERT_F32(1.0F, object->local_q[3]);
    ASSERT_F32(1.0F, object->local_s[0]);

    nt_builder_free_glb_scene(&scene);
}

void test_scene_publishes_skins_and_animations(void) {
    rigged_glb_write(RIG_GLB, NULL);

    nt_glb_scene_t scene;
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&scene, RIG_GLB));

    TEST_ASSERT_EQUAL_UINT32(1, scene.skin_count);
    TEST_ASSERT_EQUAL_STRING("RigSkin", scene.skins[0].name);
    TEST_ASSERT_EQUAL_UINT32(RIGGED_GLB_SKIN_JOINT_COUNT, scene.skins[0].joint_count);

    TEST_ASSERT_EQUAL_UINT32(2, scene.animation_count);
    TEST_ASSERT_EQUAL_STRING("Walk", scene.animations[0].name);
    ASSERT_F32(RIGGED_GLB_WALK_DURATION, scene.animations[0].duration);
    TEST_ASSERT_EQUAL_STRING("Idle", scene.animations[1].name);
    ASSERT_F32(RIGGED_GLB_IDLE_DURATION, scene.animations[1].duration);

    nt_builder_free_glb_scene(&scene);
}

void test_parse_asserts_on_a_parent_cycle(void) {
    rigged_glb_opts_t opts = {0};
    opts.cycle = true;
    rigged_glb_write(RIG_GLB, &opts);

    nt_glb_scene_t scene;
    EXPECT_BUILD_ASSERT_MATCH((void)nt_builder_parse_glb_scene(&scene, RIG_GLB), "cycle");
}
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

/* The rig of the fixture skin: the two matrix wrappers decomposed, then the
 * five skin joints in preorder with glTF children order inside each node. */
static const rig_joint_ref_t k_rig[RIG_JOINT_COUNT] = {
    {"Root", NT_SKELETAL_NO_PARENT, 7, RIGGED_GLB_NODE_ROOT, {0.0F, 0.0F, 0.0F}, {0.0F, 0.70710678F, 0.0F, 0.70710678F}, {1.0F, 1.0F, 1.0F}},
    {"Helper", 0, 7, RIGGED_GLB_NODE_HELPER, {0.25F, -0.5F, 1.0F}, {0.0F, 0.0F, 0.70710678F, 0.70710678F}, {2.0F, 1.0F, 0.5F}},
    {"Joint0", 1, 7, RIGGED_GLB_NODE_JOINT0, {1.0F, 2.0F, 3.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}},
    {"Joint1", 2, 5, RIGGED_GLB_NODE_JOINT1, {0.0F, -0.0F, 0.5F}, {0.0F, 0.0F, -0.70710678F, -0.70710678F}, {1.0F, 1.0F, 1.0F}},
    {"Joint2", 3, 5, RIGGED_GLB_NODE_JOINT2, {0.0F, 0.75F, 0.0F}, {0.70710678F, 0.0F, 0.0F, 0.70710678F}, {1.0F, 1.0F, 1.0F}},
    {"Joint3", 2, 7, RIGGED_GLB_NODE_JOINT3, {-0.5F, 0.25F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}},
    {"Joint4", 5, 7, RIGGED_GLB_NODE_JOINT4, {0.0F, 0.5F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {0.5F, 0.5F, 0.5F}},
};

static nt_builder_rig_selection_t rig_selection(void) { return (nt_builder_rig_selection_t){.skin_index = 0, .skeleton_root = UINT32_MAX, .object_node = UINT32_MAX}; }

/* Writes the default fixture and imports its only rig. */
static void import_fixture_rig(nt_glb_scene_t *scene, nt_builder_rig_t *rig) {
    rigged_glb_write(RIG_GLB, NULL);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(scene, RIG_GLB));
    const nt_builder_rig_selection_t sel = rig_selection();
    nt_builder_import_rig(scene, &sel, rig);
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
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, rig.object_node);

    for (uint16_t j = 0; j < RIG_JOINT_COUNT; j++) {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(k_rig[j].node, rig.node_index[j], k_rig[j].name);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(k_rig[j].parent, rig.skeleton.parent[j], k_rig[j].name);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(k_rig[j].subtree_end, rig.skeleton.subtree_end[j], k_rig[j].name);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(nt_hash32_str(k_rig[j].name).value, rig.skeleton.joint_id[j], k_rig[j].name);
        for (int c = 0; c < 3; c++) {
            ASSERT_F32(k_rig[j].t[c], rig.skeleton.rest[j].t[c]);
            ASSERT_F32(k_rig[j].s[c], rig.skeleton.rest[j].s[c]);
        }
        for (int c = 0; c < 4; c++) {
            ASSERT_F32(k_rig[j].q[c], rig.skeleton.rest[j].q[c]);
        }
    }

    /* Palette entry p is skin joint p, which is node p + 2. */
    for (uint16_t p = 0; p < RIGGED_GLB_SKIN_JOINT_COUNT; p++) {
        TEST_ASSERT_EQUAL_UINT16(p + RIGGED_GLB_NODE_JOINT0, rig.palette_joint[p]);
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

void test_add_skeleton_ships_the_imported_rig_id(void) {
    nt_glb_scene_t scene;
    nt_builder_rig_t rig;
    import_fixture_rig(&scene, &rig);

    (void)remove(PACK_PATH);
    NtBuilderContext *ctx = nt_builder_start_pack(PACK_PATH);
    TEST_ASSERT_NOT_NULL(ctx);
    const nt_hash64_t shipped = nt_builder_add_skeleton(ctx, &rig.skeleton, "rigs/fixture.nskl");
    TEST_ASSERT_EQUAL_HEX64(rig.skeleton.rig_compat_id.value, shipped.value);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    nt_builder_free_pack(ctx);

    nt_builder_free_rig(&rig);
    nt_builder_free_glb_scene(&scene);
}

void test_rig_id_separates_one_rest_value_and_one_joint_id(void) {
    nt_glb_scene_t scene;
    nt_builder_rig_t rig;
    import_fixture_rig(&scene, &rig);

    nt_skeletal_trs_t rest[RIG_JOINT_COUNT];
    uint32_t ids[RIG_JOINT_COUNT];
    memcpy(rest, rig.skeleton.rest, sizeof(rest));
    memcpy(ids, rig.skeleton.joint_id, sizeof(ids));
    nt_skeletal_skeleton_t probe = rig.skeleton;
    probe.rest = rest;
    probe.joint_id = ids;

    uint8_t scratch[NT_SKELETAL_RIG_ID_BYTES(RIG_JOINT_COUNT)];
    TEST_ASSERT_EQUAL_HEX64(rig.skeleton.rig_compat_id.value, nt_skeletal_rig_compat_id(&probe, scratch, (uint32_t)sizeof(scratch)).value);

    rest[4].t[1] = 0.75048828125F; /* Joint2, one representable step away */
    TEST_ASSERT_TRUE(nt_skeletal_rig_compat_id(&probe, scratch, (uint32_t)sizeof(scratch)).value != rig.skeleton.rig_compat_id.value);

    rest[4].t[1] = k_rig[4].t[1];
    ids[6] ^= 1U; /* Joint4 renamed */
    TEST_ASSERT_TRUE(nt_skeletal_rig_compat_id(&probe, scratch, (uint32_t)sizeof(scratch)).value != rig.skeleton.rig_compat_id.value);

    nt_builder_free_rig(&rig);
    nt_builder_free_glb_scene(&scene);
}

void test_rig_import_is_byte_identical_on_re_import(void) {
    nt_glb_scene_t scene;
    nt_builder_rig_t first;
    import_fixture_rig(&scene, &first);

    nt_builder_rig_t second;
    const nt_builder_rig_selection_t sel = rig_selection();
    nt_builder_import_rig(&scene, &sel, &second);

    TEST_ASSERT_EQUAL_UINT16(first.skeleton.joint_count, second.skeleton.joint_count);
    TEST_ASSERT_EQUAL_HEX64(first.skeleton.rig_compat_id.value, second.skeleton.rig_compat_id.value);
    const uint16_t joints = first.skeleton.joint_count;
    TEST_ASSERT_EQUAL_INT(0, memcmp(first.skeleton.rest, second.skeleton.rest, (size_t)joints * sizeof(nt_skeletal_trs_t)));
    TEST_ASSERT_EQUAL_INT(0, memcmp(first.skeleton.parent, second.skeleton.parent, (size_t)joints * sizeof(uint16_t)));
    TEST_ASSERT_EQUAL_INT(0, memcmp(first.skeleton.subtree_end, second.skeleton.subtree_end, (size_t)joints * sizeof(uint16_t)));
    TEST_ASSERT_EQUAL_INT(0, memcmp(first.skeleton.joint_id, second.skeleton.joint_id, (size_t)joints * sizeof(uint32_t)));
    TEST_ASSERT_EQUAL_INT(0, memcmp(first.node_index, second.node_index, (size_t)joints * sizeof(uint32_t)));
    TEST_ASSERT_EQUAL_INT(0, memcmp(first.palette_joint, second.palette_joint, (size_t)first.palette_count * sizeof(uint16_t)));

    nt_builder_free_rig(&second);
    nt_builder_free_rig(&first);
    nt_builder_free_glb_scene(&scene);
}

/* A cut is the explicit way to leave wrapper nodes to the game's E. */
void test_rig_cut_at_helper_drops_the_scene_root(void) {
    rigged_glb_write(RIG_GLB, NULL);

    nt_glb_scene_t scene;
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&scene, RIG_GLB));

    nt_builder_rig_selection_t sel = rig_selection();
    sel.skeleton_root = RIGGED_GLB_NODE_HELPER;
    nt_builder_rig_t rig;
    nt_builder_import_rig(&scene, &sel, &rig);

    TEST_ASSERT_EQUAL_UINT16(RIG_JOINT_COUNT - 1, rig.skeleton.joint_count);
    TEST_ASSERT_EQUAL_UINT16(NT_SKELETAL_NO_PARENT, rig.skeleton.parent[0]);
    TEST_ASSERT_EQUAL_UINT32(RIGGED_GLB_NODE_HELPER, rig.node_index[0]);
    TEST_ASSERT_EQUAL_HEX32(nt_hash32_str("Helper").value, rig.skeleton.joint_id[0]);
    for (uint16_t j = 0; j < rig.skeleton.joint_count; j++) {
        TEST_ASSERT_NOT_EQUAL_UINT32(RIGGED_GLB_NODE_ROOT, rig.node_index[j]);
        TEST_ASSERT_EQUAL_UINT32(k_rig[j + 1].node, rig.node_index[j]);
        TEST_ASSERT_EQUAL_UINT16(k_rig[j + 1].subtree_end - 1U, rig.skeleton.subtree_end[j]);
    }
    for (uint16_t p = 0; p < RIGGED_GLB_SKIN_JOINT_COUNT; p++) {
        TEST_ASSERT_EQUAL_UINT16(p + 1U, rig.palette_joint[p]);
    }

    nt_builder_free_rig(&rig);
    nt_builder_free_glb_scene(&scene);
}
// #endregion

// #region matrix decomposition
/* CesiumMan's two wrapper nodes, the matrices the rig import has to survive on
 * a real Khronos asset. Both are pure rotations, so the pinned bits are the
 * quaternion the largest-diagonal branch produces with w >= 0. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_decompose_pins_the_cesiumman_wrappers(void) {
    /* Z_UP: -90 degrees about X. */
    static const float k_z_up[16] = {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, -1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
    nt_skeletal_trs_t trs;
    nt_builder_decompose_trs(k_z_up, "Z_UP", &trs);
    TEST_ASSERT_EQUAL_HEX32(0xBF3504F3U, f32_bits(trs.q[0]));
    TEST_ASSERT_EQUAL_HEX32(0x00000000U, f32_bits(trs.q[1]));
    TEST_ASSERT_EQUAL_HEX32(0x00000000U, f32_bits(trs.q[2]));
    TEST_ASSERT_EQUAL_HEX32(0x3F3504F3U, f32_bits(trs.q[3]));
    for (int c = 0; c < 3; c++) {
        ASSERT_F32(0.0F, trs.t[c]);
        ASSERT_F32(1.0F, trs.s[c]);
    }

    /* Armature: -90 degrees about Z. */
    static const float k_armature[16] = {0.0F, -1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
    nt_builder_decompose_trs(k_armature, "Armature", &trs);
    TEST_ASSERT_EQUAL_HEX32(0x00000000U, f32_bits(trs.q[0]));
    TEST_ASSERT_EQUAL_HEX32(0x00000000U, f32_bits(trs.q[1]));
    TEST_ASSERT_EQUAL_HEX32(0xBF3504F3U, f32_bits(trs.q[2]));
    TEST_ASSERT_EQUAL_HEX32(0x3F3504F3U, f32_bits(trs.q[3]));
    for (int c = 0; c < 3; c++) {
        ASSERT_F32(0.0F, trs.t[c]);
        ASSERT_F32(1.0F, trs.s[c]);
    }
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
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_decompose_trs(k_flat, "Flat", &trs), "scale");
}

void test_import_asserts_on_a_sheared_matrix(void) {
    rigged_glb_opts_t opts = {0};
    opts.matrix_shear = true;
    rigged_glb_write(RIG_GLB, &opts);

    nt_glb_scene_t scene;
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&scene, RIG_GLB));
    const nt_builder_rig_selection_t sel = rig_selection();
    nt_builder_rig_t rig;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_import_rig(&scene, &sel, &rig), "matrix is not TRS");
    nt_builder_free_glb_scene(&scene);
}
// #endregion

// #region rig diagnostics
void test_import_asserts_on_a_missing_skin(void) {
    rigged_glb_write(RIG_GLB, NULL);

    nt_glb_scene_t scene;
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&scene, RIG_GLB));
    nt_builder_rig_selection_t sel = rig_selection();
    sel.skin_index = scene.skin_count;
    nt_builder_rig_t rig;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_import_rig(&scene, &sel, &rig), "skin index out of range");
    nt_builder_free_glb_scene(&scene);
}

void test_import_asserts_on_an_unnamed_rig_node(void) {
    rigged_glb_opts_t opts = {0};
    opts.unnamed_node = true;
    rigged_glb_write(RIG_GLB, &opts);

    nt_glb_scene_t scene;
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&scene, RIG_GLB));
    const nt_builder_rig_selection_t sel = rig_selection();
    nt_builder_rig_t rig;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_import_rig(&scene, &sel, &rig), "no name");
    nt_builder_free_glb_scene(&scene);
}

void test_import_asserts_on_joints_under_several_scene_roots(void) {
    rigged_glb_opts_t opts = {0};
    opts.multi_root = true;
    rigged_glb_write(RIG_GLB, &opts);

    nt_glb_scene_t scene;
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&scene, RIG_GLB));
    const nt_builder_rig_selection_t sel = rig_selection();
    nt_builder_rig_t rig;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_import_rig(&scene, &sel, &rig), "span several scene roots");
    nt_builder_free_glb_scene(&scene);
}

void test_import_asserts_on_a_joint_outside_the_cut(void) {
    rigged_glb_opts_t opts = {0};
    opts.joint_outside_root = true;
    rigged_glb_write(RIG_GLB, &opts);

    nt_glb_scene_t scene;
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&scene, RIG_GLB));
    nt_builder_rig_selection_t sel = rig_selection();
    sel.skeleton_root = RIGGED_GLB_NODE_HELPER;
    nt_builder_rig_t rig;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_import_rig(&scene, &sel, &rig), "outside the skeleton root");
    nt_builder_free_glb_scene(&scene);
}

void test_import_asserts_on_an_object_node_inside_the_rig(void) {
    rigged_glb_write(RIG_GLB, NULL);

    nt_glb_scene_t scene;
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&scene, RIG_GLB));
    nt_builder_rig_selection_t sel = rig_selection();
    sel.object_node = RIGGED_GLB_NODE_JOINT2;
    nt_builder_rig_t rig;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_import_rig(&scene, &sel, &rig), "inside the rig");
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
        EXPECT_BUILD_ASSERT_MATCH(skin_try_decode(&knob_scene, knob_layout, 0.1F, RIGGED_GLB_SKIN_JOINT_COUNT), expected);                                                                             \
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

/* The drop tolerance that admits the whole fixture: vertex 1 loses 0.10. */
#define SKIN_FIXTURE_TOLERANCE 0.1F

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
        float expected[4];
        skin_expected_weights(v, expected);
        uint32_t sum = 0;
        for (uint32_t c = 0; c < 4U; c++) {
            TEST_ASSERT_EQUAL_UINT8(k_kept_joint[v][c], joints[(v * 4U) + c]);
            /* Largest-remainder rounding stays within one step of the exact
             * lane; the sum below is what makes the choice of step visible. */
            const int32_t stored = (int32_t)weights[(v * 4U) + c];
            const int32_t ideal = (int32_t)((expected[c] * 255.0F) + 0.5F);
            TEST_ASSERT_INT_WITHIN(1, ideal, stored);
            sum += (uint32_t)stored;
        }
        TEST_ASSERT_EQUAL_UINT32(255, sum);
    }

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
            const float d = nt_f16_to_f32(mesh_lane_u16(weights, (v * 4U) + c)) - expected[c];
            /* binary16 carries 11 significant bits over [0.5, 1]. */
            TEST_ASSERT_TRUE(((d < 0.0F) ? -d : d) <= 1.0F / 2048.0F);
        }
    }

    free(data);
}

void test_skinned_mesh_rejects_a_drop_above_the_tolerance(void) {
    rigged_glb_write(RIG_GLB, NULL);
    nt_glb_scene_t scene;
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&scene, RIG_GLB));
    NtStreamLayout layout[3];
    skin_layout(layout, NT_STREAM_UINT8, NT_STREAM_UINT8, true);

    const nt_builder_skeletal_profile_t profile = nt_builder_skeletal_profile_defaults();
    ASSERT_F32(0.02F, profile.skin_drop_tolerance);
    EXPECT_BUILD_ASSERT_MATCH(skin_try_decode(&scene, layout, profile.skin_drop_tolerance, RIGGED_GLB_SKIN_JOINT_COUNT), "drops more weight than the tolerance allows");

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

void test_skinned_mesh_rejects_a_negative_weight(void) { EXPECT_SKIN_ASSERT(negative_weight, "influence weight is negative"); }

void test_skinned_mesh_rejects_a_nan_weight(void) { EXPECT_SKIN_ASSERT(nan_weight, "influence weight is not finite"); }

void test_skinned_mesh_rejects_a_vertex_without_weight(void) { EXPECT_SKIN_ASSERT(zero_weights, "sum to zero"); }

void test_skinned_mesh_rejects_a_repeated_joint(void) { EXPECT_SKIN_ASSERT(duplicate_joint, "weights one joint twice"); }

void test_skinned_mesh_rejects_an_index_past_the_palette(void) { EXPECT_SKIN_ASSERT(index_ge_palette, "outside the palette"); }

void test_skinned_mesh_rejects_an_unpaired_set(void) { EXPECT_SKIN_ASSERT(unpaired_sets, "unpaired JOINTS_n/WEIGHTS_n set"); }

void test_skinned_mesh_rejects_float_joint_indices(void) { EXPECT_SKIN_ASSERT(joints_float_type, "JOINTS accessor has an invalid type"); }

void test_skinned_mesh_rejects_a_morph_target(void) { EXPECT_SKIN_ASSERT(morph_target, "morph targets"); }

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
    nt_builder_skeletal_profile_t profile = nt_builder_skeletal_profile_defaults();
    profile.skin_drop_tolerance = SKIN_FIXTURE_TOLERANCE;

    (void)remove(PACK_PATH);
    NtBuilderContext *ctx = nt_builder_start_pack(PACK_PATH);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_builder_add_scene_skinned_mesh(ctx, &scene, 0, 0, &rig, &profile, "meshes/quad.mesh", &opts);
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
    const nt_builder_rig_selection_t sel = rig_selection();
    nt_builder_rig_t rig;
    nt_builder_import_rig(&scene, &sel, &rig);

    NtStreamLayout layout[3];
    skin_layout(layout, NT_STREAM_UINT8, NT_STREAM_UINT8, true);
    const nt_mesh_opts_t mesh_opts = {.layout = layout, .stream_count = 3, .tangent_mode = NT_TANGENT_AUTO};
    nt_builder_skeletal_profile_t profile = nt_builder_skeletal_profile_defaults();
    profile.skin_drop_tolerance = SKIN_FIXTURE_TOLERANCE;

    (void)remove(PACK_PATH);
    NtBuilderContext *ctx = nt_builder_start_pack(PACK_PATH);
    TEST_ASSERT_NOT_NULL(ctx);
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_add_scene_skinned_mesh(ctx, &scene, 0, 0, &rig, &profile, "meshes/quad.mesh", &mesh_opts), "not skinned by this rig's skin");
    nt_builder_free_pack(ctx);

    nt_builder_free_rig(&rig);
    nt_builder_free_glb_scene(&scene);
}
// #endregion

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_rigged_fixture_parses);
    RUN_TEST(test_scene_publishes_the_node_graph);
    RUN_TEST(test_scene_publishes_local_trs);
    RUN_TEST(test_scene_publishes_skins_and_animations);
    RUN_TEST(test_parse_asserts_on_a_parent_cycle);
    RUN_TEST(test_rig_import_preorder_and_subtree_ranges);
    RUN_TEST(test_rig_compat_id_matches_the_hand_written_schema);
    RUN_TEST(test_add_skeleton_ships_the_imported_rig_id);
    RUN_TEST(test_rig_id_separates_one_rest_value_and_one_joint_id);
    RUN_TEST(test_rig_import_is_byte_identical_on_re_import);
    RUN_TEST(test_rig_cut_at_helper_drops_the_scene_root);
    RUN_TEST(test_decompose_pins_the_cesiumman_wrappers);
    RUN_TEST(test_decompose_reflection_gives_a_negative_x_scale);
    RUN_TEST(test_decompose_asserts_on_a_zero_scale);
    RUN_TEST(test_import_asserts_on_a_sheared_matrix);
    RUN_TEST(test_import_asserts_on_a_missing_skin);
    RUN_TEST(test_import_asserts_on_an_unnamed_rig_node);
    RUN_TEST(test_import_asserts_on_joints_under_several_scene_roots);
    RUN_TEST(test_import_asserts_on_a_joint_outside_the_cut);
    RUN_TEST(test_import_asserts_on_an_object_node_inside_the_rig);
    RUN_TEST(test_skinned_mesh_keeps_the_four_heaviest_influences);
    RUN_TEST(test_skinned_mesh_float32_weights_are_the_renormalized_values);
    RUN_TEST(test_skinned_mesh_float16_weights_round_trip);
    RUN_TEST(test_skinned_mesh_rejects_a_drop_above_the_tolerance);
    RUN_TEST(test_skinned_mesh_rejects_an_invalid_skin_layout);
    RUN_TEST(test_skinned_mesh_rejects_a_negative_weight);
    RUN_TEST(test_skinned_mesh_rejects_a_nan_weight);
    RUN_TEST(test_skinned_mesh_rejects_a_vertex_without_weight);
    RUN_TEST(test_skinned_mesh_rejects_a_repeated_joint);
    RUN_TEST(test_skinned_mesh_rejects_an_index_past_the_palette);
    RUN_TEST(test_skinned_mesh_rejects_an_unpaired_set);
    RUN_TEST(test_skinned_mesh_rejects_float_joint_indices);
    RUN_TEST(test_skinned_mesh_rejects_a_morph_target);
    RUN_TEST(test_skinned_mesh_accepts_a_primitive_without_indices);
    RUN_TEST(test_add_scene_skinned_mesh_ships_the_decoded_bytes);
    RUN_TEST(test_add_scene_skinned_mesh_asserts_when_no_node_binds_the_skin);
    return UNITY_END();
}
