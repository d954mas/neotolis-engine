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

// #region tests
void test_rigged_fixture_parses(void) {
    rigged_glb_write(RIG_GLB, NULL);

    nt_glb_scene_t scene;
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_parse_glb_scene(&scene, RIG_GLB));
    TEST_ASSERT_EQUAL_UINT32(RIGGED_GLB_NODE_COUNT, scene.node_count);
    TEST_ASSERT_EQUAL_UINT32(1, scene.mesh_count);

    nt_builder_free_glb_scene(&scene);
}
// #endregion

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_rigged_fixture_parses);
    return UNITY_END();
}
