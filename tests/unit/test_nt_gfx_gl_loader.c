#define nt_gfx_gl_ctx_detect_gpu_caps loader_detect_gpu_caps
#include "graphics/gl/nt_gfx_gl_ctx.h"
#include "unity.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>
#include <string.h>

static int s_version;
static int s_extension;
static uint32_t s_resolves;
static GLFWglproc s_resolved;

static void GLAD_API_PTR invalidate_stub(GLenum target, GLsizei count, const GLenum *attachments) {
    (void)target;
    (void)count;
    (void)attachments;
}

static int load_stub(GLADloadfunc load) {
    (void)load;
    GLAD_GL_ARB_invalidate_subdata = s_extension;
    /* An unsupported GLAD extension leaves a previous entry point untouched. */
    if (s_extension != 0) {
        glad_glInvalidateFramebuffer = invalidate_stub;
    }
    return s_version;
}

static GLFWglproc resolve_stub(const char *name) {
    TEST_ASSERT_EQUAL_STRING("glInvalidateFramebuffer", name);
    s_resolves++;
    return s_resolved;
}

/* Compile the real loader with only its two external dependencies substituted. */
#define gladLoadGL load_stub
#define glfwGetProcAddress resolve_stub
// NOLINTNEXTLINE(bugprone-suspicious-include) -- exercises the production loader with external dependencies substituted
#include "graphics/gl/nt_gfx_gl_ctx_native.c"
#undef glfwGetProcAddress
#undef gladLoadGL

void setUp(void) {
    s_resolves = 0;
    s_resolved = (GLFWglproc)invalidate_stub;
    glad_glInvalidateFramebuffer = NULL;
}
void tearDown(void) { nt_gfx_gl_ctx_destroy(); }

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- Unity assertions expand to branches
static void test_capabilities_and_entry_points_are_reloaded_per_context(void) {
    nt_gfx_desc_t desc = {0};
    s_version = GLAD_MAKE_VERSION(3, 3);
    s_extension = 1;
    TEST_ASSERT_TRUE(nt_gfx_gl_ctx_create(&desc));
    TEST_ASSERT_TRUE(glad_glInvalidateFramebuffer == invalidate_stub);
    TEST_ASSERT_EQUAL_UINT32(1, s_resolves);

    s_extension = 0;
    TEST_ASSERT_TRUE(nt_gfx_gl_ctx_create(&desc));
    TEST_ASSERT_TRUE(glad_glInvalidateFramebuffer == NULL);
    TEST_ASSERT_EQUAL_UINT32(1, s_resolves);

    s_version = GLAD_MAKE_VERSION(4, 3);
    TEST_ASSERT_TRUE(nt_gfx_gl_ctx_create(&desc));
    TEST_ASSERT_TRUE(glad_glInvalidateFramebuffer == invalidate_stub);
    TEST_ASSERT_EQUAL_UINT32(2, s_resolves);

    s_resolved = NULL;
    TEST_ASSERT_TRUE(nt_gfx_gl_ctx_create(&desc));
    TEST_ASSERT_TRUE(glad_glInvalidateFramebuffer == NULL);
    TEST_ASSERT_EQUAL_UINT32(3, s_resolves);

    s_version = 0;
    s_extension = 1;
    TEST_ASSERT_FALSE(nt_gfx_gl_ctx_create(&desc));
    TEST_ASSERT_TRUE(glad_glInvalidateFramebuffer == NULL);
    TEST_ASSERT_EQUAL_UINT32(3, s_resolves);

    s_version = GLAD_MAKE_VERSION(3, 3);
    s_resolved = (GLFWglproc)invalidate_stub;
    TEST_ASSERT_TRUE(nt_gfx_gl_ctx_create(&desc));
    TEST_ASSERT_TRUE(glad_glInvalidateFramebuffer == invalidate_stub);
    nt_gfx_gl_ctx_destroy();
    TEST_ASSERT_TRUE(glad_glInvalidateFramebuffer == NULL);
    s_extension = 0;
    TEST_ASSERT_TRUE(nt_gfx_gl_ctx_create(&desc));
    TEST_ASSERT_TRUE(glad_glInvalidateFramebuffer == NULL);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_capabilities_and_entry_points_are_reloaded_per_context);
    return UNITY_END();
}
