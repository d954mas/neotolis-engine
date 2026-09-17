/* Scene-level rig import: what nt_builder_parse_glb_scene publishes about the
 * node graph, the skins and the animations of a rigged glTF. The fixture is
 * written by rigged_glb.c, so every expectation here names a value documented
 * in rigged_glb.h. */

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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_rigged_fixture_parses);
    RUN_TEST(test_scene_publishes_the_node_graph);
    RUN_TEST(test_scene_publishes_local_trs);
    RUN_TEST(test_scene_publishes_skins_and_animations);
    RUN_TEST(test_parse_asserts_on_a_parent_cycle);
    return UNITY_END();
}
