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
    return UNITY_END();
}
