#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_internal.h"
#include "hash/nt_hash.h"
#include "nt_mesh_format.h"
#include "nt_shader_format.h"
#include "nt_texture_format.h"
#include "test_helpers/nt_gfx_fake.h"
#include "unity.h"

#include <math.h>
#include <setjmp.h>
#include <string.h>

/* --- Assert catching (setjmp/longjmp via hookable handler) --- */

static jmp_buf s_assert_jmp;

static void test_assert_handler(const char *expr, const char *file, int line) {
    (void)expr;
    (void)file;
    (void)line;
    longjmp(s_assert_jmp, 1);
}

#define EXPECT_ASSERT(code)                                                                                                                                                                            \
    do {                                                                                                                                                                                               \
        nt_assert_handler = test_assert_handler;                                                                                                                                                       \
        if (setjmp(s_assert_jmp) == 0) {                                                                                                                                                               \
            code;                                                                                                                                                                                      \
            nt_assert_handler = NULL;                                                                                                                                                                  \
            TEST_FAIL_MESSAGE("Expected NT_ASSERT to fire");                                                                                                                                           \
        }                                                                                                                                                                                              \
        nt_assert_handler = NULL;                                                                                                                                                                      \
    } while (0)

/* 4x4 RGBA8 test pixel data (64 bytes) */
static const uint8_t s_test_pixels_4x4[4 * 4 * 4] = {
    255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 255, 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 255,
    255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 255, 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 255,
};

/* 4x4 RGBA16F test data (128 bytes = 4*4*8, 0x3C00 = half-float 1.0) */
static const uint16_t s_test_half_4x4[4 * 4 * 4] = {
    0x3C00, 0, 0, 0x3C00, 0, 0x3C00, 0, 0x3C00, 0, 0, 0x3C00, 0x3C00, 0x3C00, 0x3C00, 0, 0x3C00, 0x3C00, 0, 0, 0x3C00, 0, 0x3C00, 0, 0x3C00, 0, 0, 0x3C00, 0x3C00, 0x3C00, 0x3C00, 0, 0x3C00,
    0x3C00, 0, 0, 0x3C00, 0, 0x3C00, 0, 0x3C00, 0, 0, 0x3C00, 0x3C00, 0x3C00, 0x3C00, 0, 0x3C00, 0x3C00, 0, 0, 0x3C00, 0, 0x3C00, 0, 0x3C00, 0, 0, 0x3C00, 0x3C00, 0x3C00, 0x3C00, 0, 0x3C00,
};

/* 4x4 RG16UI test data (64 bytes = 4*4*4) */
static const uint16_t s_test_rg16ui_4x4[4 * 4 * 2] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
};

void setUp(void) {
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 8, .max_programs = 4, .max_pipelines = 4, .max_buffers = 8, .max_textures = 8, .max_meshes = 8, .max_vertex_inputs = 8, .max_render_targets = 16});
}

void tearDown(void) {
    nt_assert_handler = NULL;
    nt_gfx_shutdown();
    nt_gfx_fake_reset();
}

/* ---- Pool: alloc returns nonzero ---- */

void test_gfx_pool_alloc_returns_nonzero(void) {
    nt_pool_t pool;
    nt_pool_init(&pool, 4);
    uint32_t id = nt_pool_alloc(&pool);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, id);
    nt_pool_shutdown(&pool);
}

/* ---- Pool: two allocs return different ids ---- */

void test_gfx_pool_alloc_unique(void) {
    nt_pool_t pool;
    nt_pool_init(&pool, 4);
    uint32_t a = nt_pool_alloc(&pool);
    uint32_t b = nt_pool_alloc(&pool);
    TEST_ASSERT_NOT_EQUAL_UINT32(a, b);
    nt_pool_shutdown(&pool);
}

/* ---- Pool: free then realloc gives new generation ---- */

void test_gfx_pool_free_and_realloc(void) {
    nt_pool_t pool;
    nt_pool_init(&pool, 4);
    uint32_t first = nt_pool_alloc(&pool);
    nt_pool_free(&pool, first);
    uint32_t second = nt_pool_alloc(&pool);
    /* Same slot index, different generation -> different id */
    TEST_ASSERT_NOT_EQUAL_UINT32(first, second);
    /* Same slot index */
    TEST_ASSERT_EQUAL_UINT32(nt_pool_slot_index(first), nt_pool_slot_index(second));
    nt_pool_shutdown(&pool);
}

/* ---- Pool: valid accepts live handle ---- */

void test_gfx_pool_valid_accepts_live(void) {
    nt_pool_t pool;
    nt_pool_init(&pool, 4);
    uint32_t id = nt_pool_alloc(&pool);
    TEST_ASSERT_TRUE(nt_pool_valid(&pool, id));
    nt_pool_shutdown(&pool);
}

/* ---- Pool: valid rejects zero ---- */

void test_gfx_pool_valid_rejects_zero(void) {
    nt_pool_t pool;
    nt_pool_init(&pool, 4);
    TEST_ASSERT_FALSE(nt_pool_valid(&pool, 0));
    nt_pool_shutdown(&pool);
}

/* ---- Pool: valid rejects stale handle ---- */

void test_gfx_pool_valid_rejects_stale(void) {
    nt_pool_t pool;
    nt_pool_init(&pool, 4);
    uint32_t id = nt_pool_alloc(&pool);
    nt_pool_free(&pool, id);
    TEST_ASSERT_FALSE(nt_pool_valid(&pool, id));
    nt_pool_shutdown(&pool);
}

/* ---- Pool: full pool returns zero ---- */

void test_gfx_pool_full_returns_zero(void) {
    nt_pool_t pool;
    nt_pool_init(&pool, 2);
    nt_pool_alloc(&pool);
    nt_pool_alloc(&pool);
    uint32_t third = nt_pool_alloc(&pool);
    TEST_ASSERT_EQUAL_UINT32(0, third);
    nt_pool_shutdown(&pool);
}

/* ---- Pool: slot_index extracts correctly ---- */

void test_gfx_slot_index_extracts_correctly(void) {
    /* Construct a known handle: generation=3, slot=5 */
    uint32_t id = (3U << NT_POOL_SLOT_SHIFT) | 5U;
    TEST_ASSERT_EQUAL_UINT32(5, nt_pool_slot_index(id));
}

/* ---- High-level: init/shutdown transitions initialized flag ---- */

void test_gfx_init_shutdown(void) {
    /* setUp already called nt_gfx_init, so initialized should be true */
    TEST_ASSERT_TRUE(g_nt_gfx.initialized);
    nt_gfx_shutdown();
    TEST_ASSERT_FALSE(g_nt_gfx.initialized);
    /* Re-init for tearDown */
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 8, .max_programs = 4, .max_pipelines = 4, .max_buffers = 8, .max_textures = 8, .max_meshes = 8, .max_vertex_inputs = 8, .max_render_targets = 16});
}

/* ---- High-level: make/destroy shader ---- */

void test_gfx_make_destroy_shader(void) {
    nt_shader_t shd = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "test"});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, shd.id);
    nt_gfx_destroy_shader(shd);
    /* After destroy, making a new one should still work */
    nt_shader_t shd2 = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "test2"});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, shd2.id);
}

/* ---- High-level: make/destroy buffer ---- */

void test_gfx_make_destroy_buffer(void) {
    nt_buffer_t buf = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .size = 64});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, buf.id);
    nt_gfx_destroy_buffer(buf);
}

/* ---- High-level: nt_gfx_desc_defaults provides usable config ---- */

void test_gfx_defaults_applied(void) {
    /* Shutdown current, re-init with defaults */
    nt_gfx_shutdown();
    nt_gfx_desc_t defaults = nt_gfx_desc_defaults();
    nt_gfx_init(&defaults);
    TEST_ASSERT_TRUE(g_nt_gfx.initialized);

    /* Verify we can allocate more than 4 shaders (proves defaults > test setUp) */
    nt_shader_t shaders[10];
    for (int i = 0; i < 10; i++) {
        shaders[i] = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "v"});
        TEST_ASSERT_NOT_EQUAL_UINT32(0, shaders[i].id);
    }

    /* Re-init for tearDown */
    nt_gfx_shutdown();
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 8, .max_programs = 4, .max_pipelines = 4, .max_buffers = 8, .max_textures = 8, .max_meshes = 8, .max_vertex_inputs = 8, .max_render_targets = 16});
}

/* ---- Pipeline: create with valid shaders, destroy ---- */

void test_gfx_make_destroy_pipeline(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "v"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "f"});
    nt_program_t prog = nt_gfx_make_program(vs, fs);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, vs.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, fs.id);

    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .program = prog,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pip.id);
    nt_gfx_destroy_pipeline(pip);
}

/* ---- Pipeline: survives shader destroy ---- */

void test_gfx_pipeline_survives_shader_destroy(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "v"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "f"});
    nt_program_t prog = nt_gfx_make_program(vs, fs);
    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .program = prog,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pip.id);

    /* Destroy shaders — pipeline should still be bindable */
    nt_gfx_destroy_shader(vs);
    nt_gfx_destroy_shader(fs);

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip);
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_gfx_destroy_pipeline(pip);
}

/* ---- State machine: valid frame cycle ---- */

void test_gfx_state_machine_valid_cycle(void) {
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    /* Reaching here without crash means state machine accepted the sequence */
}

/* ---- Double destroy: shader ---- */

void test_gfx_double_destroy_shader(void) {
    nt_shader_t shd = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "test"});
    nt_gfx_destroy_shader(shd);
    /* Second destroy on stale handle — must be no-op */
    nt_gfx_destroy_shader(shd);
}

/* ---- Double destroy: buffer ---- */

void test_gfx_double_destroy_buffer(void) {
    nt_buffer_t buf = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .size = 64});
    nt_gfx_destroy_buffer(buf);
    nt_gfx_destroy_buffer(buf);
}

/* ---- Pipeline: an unlinked program is a developer error ---- */

void test_gfx_pipeline_asserts_unready_program(void) { EXPECT_ASSERT(nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = NT_PROGRAM_INVALID})); }

/* ---- Program: helpers ---- */

static nt_shader_t make_test_vs(void) { return nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "v"}); }

static nt_shader_t make_test_fs(void) { return nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "f"}); }

/* Draws require a bound vertex input; an indexed one satisfies every draw
 * variant (draw_indexed asserts a captured index type). */
static void bind_test_vertex_input(void) {
    static const float verts[9] = {0};
    static const uint16_t indices[3] = {0, 1, 2};
    nt_buffer_t vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_IMMUTABLE, .data = verts, .size = sizeof(verts)});
    nt_buffer_t ibo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_INDEX, .usage = NT_USAGE_IMMUTABLE, .data = indices, .size = sizeof(indices), .index_type = NT_INDEX_UINT16});
    nt_gfx_bind_vertex_input(nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){
        .layout = {.attr_count = 1, .stride = 12, .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 3}}},
        .vertex_buffer = vbo,
        .index_buffer = ibo,
    }));
}

/* ---- Program: no dedup — same pair links twice ---- */

void test_gfx_make_program_does_not_dedup(void) {
    nt_gfx_fake_reset();
    nt_shader_t vs = make_test_vs();
    nt_shader_t fs = make_test_fs();

    nt_program_t a = nt_gfx_make_program(vs, fs);
    nt_program_t b = nt_gfx_make_program(vs, fs);

    TEST_ASSERT_NOT_EQUAL_UINT32(0, a.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, b.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(a.id, b.id);
    /* Distinct handles must also produce distinct backend programs. */
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_program_create_count());

    nt_gfx_destroy_program(a);
    nt_gfx_destroy_program(b);
}

/* ---- Program: valid and ready after create ---- */

void test_gfx_program_valid_and_ready(void) {
    nt_program_t prog = nt_gfx_make_program(make_test_vs(), make_test_fs());
    TEST_ASSERT_TRUE(nt_gfx_program_valid(prog));
    TEST_ASSERT_TRUE(nt_gfx_program_ready(prog));
    nt_gfx_destroy_program(prog);
}

/* ---- Program: destroy invalidates; a stale nonzero handle asserts ---- */

void test_gfx_destroy_program_invalidates(void) {
    nt_program_t prog = nt_gfx_make_program(make_test_vs(), make_test_fs());
    nt_gfx_destroy_program(prog);
    TEST_ASSERT_FALSE(nt_gfx_program_valid(prog));
    TEST_ASSERT_FALSE(nt_gfx_program_ready(prog));
    /* A second destroy requires clearing the owner's handle to NT_PROGRAM_INVALID. */
}

/* ---- Program: destroyed slot is reusable ---- */

void test_gfx_program_slot_reused_after_destroy(void) {
    nt_shader_t vs = make_test_vs();
    nt_shader_t fs = make_test_fs();
    for (int i = 0; i < 8; i++) { /* twice the pool capacity */
        nt_program_t prog = nt_gfx_make_program(vs, fs);
        TEST_ASSERT_TRUE(nt_gfx_program_ready(prog));
        nt_gfx_destroy_program(prog);
    }
}

/* ---- Program: outlives its stages ---- */

void test_gfx_program_survives_shader_destroy(void) {
    nt_shader_t vs = make_test_vs();
    nt_shader_t fs = make_test_fs();
    nt_program_t prog = nt_gfx_make_program(vs, fs);

    nt_gfx_destroy_shader(vs);
    nt_gfx_destroy_shader(fs);

    TEST_ASSERT_TRUE(nt_gfx_program_ready(prog));
    nt_gfx_destroy_program(prog);
}

/* ---- Program: exhausting the pool is a configuration error ---- */

void test_gfx_program_pool_full_asserts(void) {
    nt_shader_t vs = make_test_vs();
    nt_shader_t fs = make_test_fs();
    for (int i = 0; i < 4; i++) { /* setUp: max_programs = 4 */
        TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_make_program(vs, fs).id);
    }
    EXPECT_ASSERT(nt_gfx_make_program(vs, fs));
}

/* ---- Program: invalid stage handle asserts ---- */

void test_gfx_make_program_asserts_invalid_shader(void) {
    nt_shader_t vs = make_test_vs();
    nt_shader_t fs = make_test_fs();
    EXPECT_ASSERT(nt_gfx_make_program((nt_shader_t){0}, fs));
    EXPECT_ASSERT(nt_gfx_make_program(vs, (nt_shader_t){0}));
}

/* ---- Program: link failure asserts ---- */

void test_gfx_make_program_asserts_on_link_failure(void) {
    nt_gfx_fake_reset();
    nt_shader_t vs = make_test_vs();
    nt_shader_t fs = make_test_fs();
    nt_gfx_fake_fail_next_program_create();
    EXPECT_ASSERT(nt_gfx_make_program(vs, fs));
    nt_gfx_fake_reset();
}

/* ---- Program: a lost context yields an invalid handle, not an assert ---- */

void test_gfx_make_program_context_lost_returns_invalid(void) {
    nt_shader_t vs = make_test_vs();
    nt_shader_t fs = make_test_fs();

    /* No begin_frame in between: g_nt_gfx.context_lost is still false, so this
     * pins the live backend poll rather than the cached flag. */
    nt_gfx_fake_set_context_lost(true);
    nt_program_t prog = nt_gfx_make_program(vs, fs);
    nt_gfx_fake_set_context_lost(false);

    TEST_ASSERT_EQUAL_UINT32(0, prog.id);
    TEST_ASSERT_FALSE(nt_gfx_program_valid(prog));
}

/* The aftermath of a loss, not the loss itself: the context is back, the stage
 * handles are still live, but their GPU objects are gone until the owner
 * recreates them. That is recoverable, so linking rejects instead of trapping. */
void test_gfx_make_program_rejects_a_stage_left_unready_by_a_loss(void) {
    nt_gfx_fake_reset();
    nt_shader_t vs = make_test_vs();
    nt_shader_t fs = make_test_fs();

    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_frame(); /* wipes the backend tables, latches context_lost */
    nt_gfx_fake_set_context_lost(false);
    nt_gfx_begin_frame(); /* recovery completes; the stages stay unready */

    /* Neither loss gate can explain the rejection below. */
    TEST_ASSERT_FALSE(g_nt_gfx.context_lost);
    TEST_ASSERT_FALSE(nt_gfx_backend_is_context_lost());
    TEST_ASSERT_FALSE(nt_gfx_shader_ready(vs));
    TEST_ASSERT_FALSE(nt_gfx_shader_ready(fs));

    const uint32_t links_before = nt_gfx_fake_program_create_count();
    nt_assert_handler = test_assert_handler;
    if (setjmp(s_assert_jmp) != 0) {
        nt_assert_handler = NULL;
        TEST_FAIL_MESSAGE("a stage left unready by a context loss must return invalid without asserting");
    }
    nt_program_t prog = nt_gfx_make_program(vs, fs);
    nt_assert_handler = NULL;

    TEST_ASSERT_EQUAL_UINT32(0, prog.id);
    TEST_ASSERT_FALSE(nt_gfx_program_valid(prog));
    /* Rejected before the backend, so no GL program leaked on the way out. */
    TEST_ASSERT_EQUAL_UINT32(links_before, nt_gfx_fake_program_create_count());
    nt_gfx_end_frame();
}

void test_gfx_program_link_context_loss_releases_every_slot(void) {
    nt_gfx_fake_reset();
    nt_shader_t vs = make_test_vs();
    nt_shader_t fs = make_test_fs();
    nt_program_t programs[4];

    nt_assert_handler = test_assert_handler;
    if (setjmp(s_assert_jmp) != 0) {
        nt_assert_handler = NULL;
        nt_gfx_fake_set_context_lost(false);
        TEST_FAIL_MESSAGE("Context loss during program link must return invalid without asserting");
    }
    for (uint32_t attempt = 0; attempt < 12; attempt++) {
        nt_gfx_fake_set_context_lost(false);
        nt_gfx_fake_lose_context_on_program_create();
        nt_program_t program = nt_gfx_make_program(vs, fs);
        TEST_ASSERT_EQUAL_UINT32(0, program.id);
        TEST_ASSERT_FALSE(nt_gfx_program_valid(program));
        TEST_ASSERT_FALSE(g_nt_gfx.context_lost);
        TEST_ASSERT_TRUE(nt_gfx_backend_is_context_lost());
        TEST_ASSERT_EQUAL_UINT32(attempt + 1, nt_gfx_fake_program_create_count());
    }

    nt_gfx_fake_set_context_lost(false);
    for (uint32_t i = 0; i < 4; i++) {
        programs[i] = nt_gfx_make_program(vs, fs);
        TEST_ASSERT_TRUE(nt_gfx_program_ready(programs[i]));
    }
    nt_assert_handler = NULL;
    TEST_ASSERT_EQUAL_UINT32(16, nt_gfx_fake_program_create_count());
    for (uint32_t i = 0; i < 4; i++) {
        TEST_ASSERT_TRUE(nt_gfx_program_valid(programs[i]));
        nt_gfx_destroy_program(programs[i]);
    }
    nt_gfx_fake_reset();
}

/* ---- Program: context loss clears readiness, handles stay valid ---- */

void test_gfx_context_loss_keeps_handle_drops_ready(void) {
    nt_program_t prog = nt_gfx_make_program(make_test_vs(), make_test_fs());

    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_frame();
    nt_gfx_fake_set_context_lost(false);

    TEST_ASSERT_TRUE(nt_gfx_program_valid(prog));
    TEST_ASSERT_FALSE(nt_gfx_program_ready(prog));
}

/* ---- Global blocks: registration order does not matter ---- */

/* The registry is the single truth for name -> slot, so it must reach programs
 * that already exist. Engine renderers link in their init, which would
 * otherwise close the window before a game gets to register anything. */
void test_gfx_register_global_block_after_program_is_allowed(void) {
    nt_program_t prog = nt_gfx_make_program(make_test_vs(), make_test_fs());
    TEST_ASSERT_TRUE(nt_gfx_program_ready(prog));

    nt_gfx_register_global_block("Globals", 0);

    const nt_global_block_t *blocks = NULL;
    uint32_t count = 0;
    nt_gfx_get_global_blocks(&blocks, &count);
    TEST_ASSERT_EQUAL_UINT32(1, count);
    TEST_ASSERT_EQUAL_STRING("Globals", blocks[0].name);
    /* The stub verifies registration only; block bindings require real GL. */
}

/* ---- Program: pipelines borrow it, they never link ---- */

void test_gfx_two_pipelines_share_one_program(void) {
    nt_gfx_fake_reset();
    nt_program_t prog = nt_gfx_make_program(make_test_vs(), make_test_fs());

    nt_pipeline_t a = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog, .depth_test = false});
    nt_pipeline_t b = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog, .depth_test = true});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, a.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, b.id);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_program_create_count());

    nt_gfx_destroy_pipeline(a);
    nt_gfx_destroy_pipeline(b);
    nt_gfx_destroy_program(prog);
}

/* ---- Program-owned sampler units ---- */

static nt_program_t make_sampler_program(const char *const *names, uint8_t count) {
    nt_program_t prog = nt_gfx_fake_make_program(names, count);
    TEST_ASSERT_TRUE(nt_gfx_program_ready(prog));
    return prog;
}

void test_gfx_sampler_queries_use_each_program_backend(void) {
    nt_program_t first = make_sampler_program((const char *const[]){"u_a", "u_b"}, 2);
    nt_program_t second = make_sampler_program((const char *const[]){"u_b"}, 1);

    TEST_ASSERT_EQUAL_INT(0, nt_gfx_program_sampler_unit(first, nt_hash32_str("u_a")));
    TEST_ASSERT_EQUAL_INT(1, nt_gfx_program_sampler_unit(first, nt_hash32_str("u_b")));
    TEST_ASSERT_EQUAL_UINT32(0x3U, nt_gfx_program_sampler_mask(first));
    TEST_ASSERT_EQUAL_INT(-1, nt_gfx_program_sampler_unit(first, nt_hash32_str("u_missing")));

    /* The second program's table is its own: same name, its own unit. */
    TEST_ASSERT_EQUAL_INT(0, nt_gfx_program_sampler_unit(second, nt_hash32_str("u_b")));
    TEST_ASSERT_EQUAL_INT(-1, nt_gfx_program_sampler_unit(second, nt_hash32_str("u_a")));
    TEST_ASSERT_EQUAL_UINT32(0x1U, nt_gfx_program_sampler_mask(second));

    nt_gfx_destroy_program(second);
    nt_gfx_destroy_program(first);
}

void test_gfx_new_program_does_not_inherit_a_destroyed_sampler_table(void) {
    nt_program_t old = make_sampler_program((const char *const[]){"u_a", "u_b"}, 2);
    TEST_ASSERT_EQUAL_UINT32(0x3U, nt_gfx_program_sampler_mask(old));
    nt_gfx_destroy_program(old);

    nt_program_t fresh = make_sampler_program((const char *const[]){"u_c"}, 1);
    TEST_ASSERT_EQUAL_INT(0, nt_gfx_program_sampler_unit(fresh, nt_hash32_str("u_c")));
    TEST_ASSERT_EQUAL_INT(-1, nt_gfx_program_sampler_unit(fresh, nt_hash32_str("u_a")));
    TEST_ASSERT_EQUAL_UINT32(0x1U, nt_gfx_program_sampler_mask(fresh));

    nt_gfx_destroy_program(fresh);
}

void test_gfx_backend_texture_binding_batch_visits_only_active_units(void) {
    const nt_gfx_resolved_texture_binding_t bindings[NT_GFX_MAX_TEXTURE_SLOTS] = {
        [1] = {.texture_backend = 41, .sampler_backend = 51},
        [6] = {.texture_backend = 46, .sampler_backend = 56},
    };

    nt_gfx_backend_apply_texture_bindings(bindings, (uint8_t)((1U << 1U) | (1U << 6U)));

    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_bound_texture_slot_at(0));
    TEST_ASSERT_EQUAL_UINT32(41, nt_gfx_fake_bound_texture_at(0));
    TEST_ASSERT_EQUAL_UINT32(6, nt_gfx_fake_bound_texture_slot_at(1));
    TEST_ASSERT_EQUAL_UINT32(46, nt_gfx_fake_bound_texture_at(1));
    TEST_ASSERT_EQUAL_UINT32(51, nt_gfx_fake_last_sampler(1));
    TEST_ASSERT_EQUAL_UINT32(56, nt_gfx_fake_last_sampler(6));
}

/* ---- Program: destroy takes NT_PROGRAM_INVALID, nothing else stale ---- */

/* Games clear their handles on context loss and destroy them again at shutdown,
 * so the invalid value has to pass through untouched. */
void test_gfx_destroy_program_accepts_invalid(void) {
    nt_gfx_destroy_program(NT_PROGRAM_INVALID);
    TEST_PASS();
}

/* A stale non-zero handle means the owner lost track of what it still holds --
 * the one mistake single ownership cannot absorb. */
void test_gfx_destroy_program_asserts_on_a_stale_handle(void) {
    nt_program_t prog = nt_gfx_make_program(make_test_vs(), make_test_fs());
    nt_gfx_destroy_program(prog);

    EXPECT_ASSERT(nt_gfx_destroy_program(prog));
}

/* A program is immutable: a lost context does not repair one, so recovery is a
 * new handle from a new link, never the old handle coming back to life. */
void test_gfx_context_restore_yields_a_new_program_handle(void) {
    nt_gfx_shutdown();
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    desc.max_programs = 1;
    nt_gfx_init(&desc);
    nt_gfx_fake_reset();
    nt_shader_t vs = make_test_vs();
    nt_shader_t fs = make_test_fs();
    nt_program_t old = nt_gfx_make_program(vs, fs);

    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_frame();
    nt_gfx_fake_set_context_lost(false);
    TEST_ASSERT_TRUE(nt_gfx_program_valid(old));
    TEST_ASSERT_FALSE(nt_gfx_program_ready(old));

    nt_gfx_destroy_program(old);
    nt_shader_t pending_vs = make_test_vs();
    nt_shader_t pending_fs = make_test_fs();
    uint32_t links_before = nt_gfx_fake_program_create_count();
    nt_program_t pending = nt_gfx_make_program(pending_vs, pending_fs);
    TEST_ASSERT_TRUE(g_nt_gfx.context_lost);
    TEST_ASSERT_EQUAL_UINT32(0, pending.id);
    TEST_ASSERT_EQUAL_UINT32(links_before, nt_gfx_fake_program_create_count());
    nt_gfx_destroy_shader(pending_vs);
    nt_gfx_destroy_shader(pending_fs);

    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.context_restored);
    nt_gfx_end_frame();
    nt_program_t fresh = nt_gfx_make_program(make_test_vs(), make_test_fs());

    TEST_ASSERT_NOT_EQUAL_UINT32(old.id, fresh.id); /* generation moved on */
    TEST_ASSERT_TRUE(nt_gfx_program_ready(fresh));
    TEST_ASSERT_FALSE(nt_gfx_program_valid(old));
}

/* ---- Program: destroying one reclaims the pipelines built on it ---- */

/* A pipeline outlives its program only as a corpse -- no cache key can select it
 * again -- so the destroy takes them with it instead of leaving pool slots and
 * VAOs pinned until the owning renderer is reset. */
void test_gfx_destroy_program_destroys_its_pipelines(void) {
    nt_shader_t vs = make_test_vs();
    nt_shader_t fs = make_test_fs();
    nt_program_t prog = nt_gfx_make_program(vs, fs);
    nt_pipeline_t a = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog});
    nt_pipeline_t b = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog});

    nt_program_t keep = nt_gfx_make_program(vs, fs);
    nt_pipeline_t untouched = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = keep});

    nt_gfx_destroy_program(prog);

    /* The slots come back: a fresh pipeline reuses one of them. */
    nt_pipeline_t reborn = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = keep});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, reborn.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(a.id, reborn.id); /* generation moved on */
    TEST_ASSERT_NOT_EQUAL_UINT32(b.id, reborn.id);

    /* A pipeline on a different program is untouched. */
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(untouched);
    bind_test_vertex_input();
    nt_gfx_draw(0, 0);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_get_frame_draw_calls());

    /* The dead ones bind to nothing, so the next draw has no pipeline at all. */
    nt_gfx_bind_pipeline(a);
    EXPECT_ASSERT(nt_gfx_draw(0, 0));
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_get_frame_draw_calls());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

/* The bind-time check is not enough on its own: the program can die while its
 * pipeline is already bound, and draw only looks at bound_pipeline. */
void test_gfx_draw_asserts_when_bound_program_is_destroyed(void) {
    nt_shader_t vs = make_test_vs();
    nt_shader_t fs = make_test_fs();
    nt_program_t prog = nt_gfx_make_program(vs, fs);
    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog});

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip);
    nt_gfx_destroy_program(prog);
    EXPECT_ASSERT(nt_gfx_draw(0, 3));
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

static void expect_pipeline_blend_assert(nt_blend_state_t blend) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "v"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "f"});
    nt_program_t prog = nt_gfx_make_program(vs, fs);
    EXPECT_ASSERT(nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .program = prog,
        .blend = blend,
    }));
}

static void expect_pipeline_blend_accept(nt_blend_state_t blend) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "v"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "f"});
    nt_program_t prog = nt_gfx_make_program(vs, fs);
    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .program = prog,
        .blend = blend,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pip.id);
    nt_gfx_destroy_pipeline(pip);
    nt_gfx_destroy_shader(fs);
    nt_gfx_destroy_shader(vs);
}

void test_gfx_pipeline_asserts_null_desc(void) { EXPECT_ASSERT(nt_gfx_make_pipeline(NULL)); }

/* Context loss is what zeroes a program's backend, so it must not read as the
 * developer error "program is not linked" -- the handle is still pool-valid. */
void test_gfx_pipeline_context_lost_returns_invalid(void) {
    nt_program_t prog = nt_gfx_make_program(make_test_vs(), make_test_fs());

    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_frame();
    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog});
    nt_gfx_fake_set_context_lost(false);

    TEST_ASSERT_TRUE(nt_gfx_program_valid(prog));
    TEST_ASSERT_FALSE(nt_gfx_program_ready(prog));
    TEST_ASSERT_EQUAL_UINT32(0, pip.id);
}

void test_gfx_pipeline_pool_full_asserts(void) {
    nt_program_t prog = nt_gfx_make_program(make_test_vs(), make_test_fs());
    for (int i = 0; i < 4; i++) { /* setUp: max_pipelines = 4 */
        TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog}).id);
    }
    EXPECT_ASSERT(nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog}));
}

/* A backend failure is not a developer error: it stays an invalid handle so
 * callers can retry on a later frame. */
void test_gfx_pipeline_backend_failure_returns_invalid(void) {
    nt_gfx_fake_reset();
    nt_program_t prog = nt_gfx_make_program(make_test_vs(), make_test_fs());
    nt_gfx_fake_fail_next_pipeline_create();

    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog});
    TEST_ASSERT_EQUAL_UINT32(0, pip.id);

    /* The pool slot went back, so the retry succeeds. */
    nt_pipeline_t retry = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, retry.id);
    nt_gfx_destroy_pipeline(retry);
    nt_gfx_fake_reset();
}

/* ---- Pipeline key: exact identity ---- */

static nt_pipeline_desc_t key_base_desc(uint32_t program_id) {
    nt_pipeline_desc_t d;
    memset(&d, 0, sizeof(d));
    d.program.id = program_id;
    d.depth_test = true;
    d.depth_write = true;
    d.blend = nt_blend_alpha();
    d.cull_mode = 1;
    d.label = "base";
    return d;
}

/* One-step variants of every lane. Programs come out of the pool in creation
 * order, so a lane step on a neighbouring program must never reach the same key. */
static uint32_t key_variants(uint32_t program_id, nt_gfx_pipeline_key_t *out) {
    uint32_t n = 0;
    nt_pipeline_desc_t d = key_base_desc(program_id);
#define VARIANT(stmt)                                                                                                                                                                                  \
    do {                                                                                                                                                                                               \
        d = key_base_desc(program_id);                                                                                                                                                                 \
        stmt;                                                                                                                                                                                          \
        out[n++] = nt_gfx_pipeline_key(&d);                                                                                                                                                            \
    } while (0)
    VARIANT((void)0);
    VARIANT(d.depth_test = false);
    VARIANT(d.depth_write = false);
    VARIANT(d.depth_func = NT_DEPTH_LEQUAL);
    VARIANT(d.cull_mode = 0);
    VARIANT(d.cull_mode = 2);
    VARIANT(d.polygon_offset = true);
    VARIANT(d.blend.enabled = false);
    VARIANT(d.blend.src_rgb = NT_BLEND_ONE);
    VARIANT(d.blend.dst_rgb = NT_BLEND_ONE);
    VARIANT(d.blend.src_alpha = NT_BLEND_ZERO);
    VARIANT(d.blend.dst_alpha = NT_BLEND_ONE);
    VARIANT(d.blend.op_rgb = NT_BLEND_OP_SUBTRACT);
    VARIANT(d.blend.op_alpha = NT_BLEND_OP_MAX);
    VARIANT(d.blend.constant_color[0] = 0.5F);
    VARIANT(d.blend.constant_color[1] = 0.5F);
    VARIANT(d.blend.constant_color[2] = 0.5F);
    VARIANT(d.blend.constant_color[3] = 0.5F);
    VARIANT(d.depth_func = NT_DEPTH_ALWAYS);
    VARIANT(d.polygon_offset = true; d.polygon_offset_factor = 2.0F);
    VARIANT(d.polygon_offset = true; d.polygon_offset_units = 2.0F);
#undef VARIANT
    return n;
}

#define KEY_VARIANT_COUNT 21

void test_gfx_pipeline_key_neighbouring_programs_never_alias(void) {
    nt_gfx_pipeline_key_t keys[4 * KEY_VARIANT_COUNT];
    uint32_t n = 0;
    for (uint32_t p = 65550; p < 65554; p++) {
        const uint32_t per_program = key_variants(p, &keys[n]);
        TEST_ASSERT_EQUAL_UINT32(KEY_VARIANT_COUNT, per_program); /* keys[] is sized for exactly this many */
        n += per_program;
    }
    for (uint32_t i = 0; i < n; i++) {
        for (uint32_t j = 0; j < i; j++) {
            TEST_ASSERT_FALSE_MESSAGE(nt_gfx_pipeline_key_equal(&keys[i], &keys[j]), "two distinct descs share a key");
        }
    }
}

/* A disabled blend is opaque whatever its factors or constant say. */
void test_gfx_pipeline_key_canonicalizes_disabled_blend_and_offset(void) {
    nt_pipeline_desc_t opaque = key_base_desc(7);
    opaque.blend = nt_blend_opaque();
    nt_pipeline_desc_t disabled = key_base_desc(7);
    disabled.blend.enabled = false;
    disabled.blend.constant_color[1] = 0.5F;
    disabled.blend.op_rgb = NT_BLEND_OP_MAX;
    disabled.blend.op_alpha = NT_BLEND_OP_MIN;
    const nt_gfx_pipeline_key_t a = nt_gfx_pipeline_key(&opaque);
    const nt_gfx_pipeline_key_t b = nt_gfx_pipeline_key(&disabled);
    TEST_ASSERT_TRUE(nt_gfx_pipeline_key_equal(&a, &b));

    nt_pipeline_desc_t no_offset = key_base_desc(7);
    no_offset.polygon_offset_factor = 3.0F;
    no_offset.polygon_offset_units = 3.0F;
    const nt_gfx_pipeline_key_t c = nt_gfx_pipeline_key(&no_offset);
    const nt_gfx_pipeline_key_t base = nt_gfx_pipeline_key(&(nt_pipeline_desc_t){.program.id = 7, .depth_test = true, .depth_write = true, .blend = nt_blend_alpha(), .cull_mode = 1});
    TEST_ASSERT_TRUE(nt_gfx_pipeline_key_equal(&c, &base));
}

void test_gfx_pipeline_key_ignores_label_and_splits_on_float_payloads(void) {
    nt_pipeline_desc_t a = key_base_desc(9);
    nt_pipeline_desc_t b = key_base_desc(9);
    b.label = "other";
    const nt_gfx_pipeline_key_t ka = nt_gfx_pipeline_key(&a);
    const nt_gfx_pipeline_key_t kb = nt_gfx_pipeline_key(&b);
    TEST_ASSERT_TRUE(nt_gfx_pipeline_key_equal(&ka, &kb));

    b.blend.constant_color[3] = 0.25F;
    const nt_gfx_pipeline_key_t kc = nt_gfx_pipeline_key(&b);
    TEST_ASSERT_EQUAL_UINT64(ka.bits, kc.bits);
    TEST_ASSERT_FALSE(nt_gfx_pipeline_key_equal(&ka, &kc));

    /* Bit patterns, not values: -0.0 is a different pipeline from +0.0. */
    b = key_base_desc(9);
    b.blend.constant_color[0] = -0.0F;
    const nt_gfx_pipeline_key_t kd = nt_gfx_pipeline_key(&b);
    TEST_ASSERT_FALSE(nt_gfx_pipeline_key_equal(&ka, &kd));
}

/* An out-of-range lane value must trap before it can truncate onto a valid neighbour
 * and hand a cache hit to a desc make_pipeline would have rejected. */
void test_gfx_pipeline_key_asserts_out_of_range_lanes(void) {
    nt_pipeline_desc_t d = key_base_desc(3);
    d.cull_mode = 3;
    EXPECT_ASSERT((void)nt_gfx_pipeline_key(&d));
    d = key_base_desc(3);
    const int bad_depth_func = NT_DEPTH_ALWAYS + 1; /* memcpy: an enum cast of a literal trips the analyzer */
    memcpy(&d.depth_func, &bad_depth_func, sizeof(d.depth_func));
    EXPECT_ASSERT((void)nt_gfx_pipeline_key(&d));
    d = key_base_desc(3);
    d.blend.src_rgb = 16;
    EXPECT_ASSERT((void)nt_gfx_pipeline_key(&d));
    d = key_base_desc(3);
    d.blend.op_alpha = 8;
    EXPECT_ASSERT((void)nt_gfx_pipeline_key(&d));
}

void test_gfx_pipeline_rejects_invalid_blend_factor(void) {
    nt_blend_state_t blend = nt_blend_alpha();
    blend.src_rgb = UINT8_MAX;
    expect_pipeline_blend_assert(blend);

    blend = nt_blend_alpha();
    blend.dst_rgb = UINT8_MAX;
    expect_pipeline_blend_assert(blend);

    /* Ranges hold for a disabled blend too: garbage is a bug, not "don't care". */
    blend = nt_blend_opaque();
    blend.src_rgb = 16;
    expect_pipeline_blend_assert(blend);
}

void test_gfx_pipeline_rejects_invalid_blend_operation(void) {
    nt_blend_state_t blend = nt_blend_alpha();
    blend.op_rgb = UINT8_MAX;
    expect_pipeline_blend_assert(blend);
}

void test_gfx_pipeline_rejects_invalid_alpha_blend_factors(void) {
    nt_blend_state_t blend = nt_blend_alpha();
    blend.src_alpha = UINT8_MAX;
    expect_pipeline_blend_assert(blend);

    blend = nt_blend_alpha();
    blend.dst_alpha = UINT8_MAX;
    expect_pipeline_blend_assert(blend);
}

void test_gfx_pipeline_rejects_invalid_alpha_blend_operation(void) {
    nt_blend_state_t blend = nt_blend_alpha();
    blend.op_alpha = UINT8_MAX;
    expect_pipeline_blend_assert(blend);
}

void test_gfx_pipeline_rejects_invalid_blend_constant_color(void) {
    nt_blend_state_t blend = nt_blend_alpha();
    blend.constant_color[0] = -0.1F;
    expect_pipeline_blend_assert(blend);

    blend = nt_blend_alpha();
    blend.constant_color[1] = 1.1F;
    expect_pipeline_blend_assert(blend);

    blend = nt_blend_alpha();
    blend.constant_color[2] = NAN;
    expect_pipeline_blend_assert(blend);
}

void test_gfx_pipeline_rejects_src_alpha_saturate_as_destination_factor(void) {
    nt_blend_state_t blend = nt_blend_alpha();
    blend.dst_rgb = NT_BLEND_SRC_ALPHA_SATURATE;
    expect_pipeline_blend_assert(blend);

    blend = nt_blend_alpha();
    blend.dst_alpha = NT_BLEND_SRC_ALPHA_SATURATE;
    expect_pipeline_blend_assert(blend);
}

void test_gfx_pipeline_rejects_mixed_constant_color_and_alpha_factors(void) {
    nt_blend_state_t blend = nt_blend_alpha();
    blend.src_rgb = NT_BLEND_CONSTANT_COLOR;
    blend.dst_rgb = NT_BLEND_CONSTANT_ALPHA;
    expect_pipeline_blend_assert(blend);
}

void test_gfx_pipeline_accepts_constant_color_rgb_with_constant_alpha_source_alpha(void) {
    nt_blend_state_t blend = nt_blend_alpha();
    blend.src_rgb = NT_BLEND_CONSTANT_COLOR;
    blend.dst_rgb = NT_BLEND_ZERO;
    blend.src_alpha = NT_BLEND_CONSTANT_ALPHA;
    blend.dst_alpha = NT_BLEND_ZERO;
    expect_pipeline_blend_accept(blend);
}

void test_gfx_pipeline_accepts_src_alpha_saturate_for_source_alpha(void) {
    nt_blend_state_t blend = nt_blend_alpha();
    blend.src_alpha = NT_BLEND_SRC_ALPHA_SATURATE;
    expect_pipeline_blend_accept(blend);
}

/* ---- Texture: make with valid data ---- */

void test_gfx_make_texture_valid(void) {
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .data = s_test_pixels_4x4,
        .format = NT_TEXTURE_FORMAT_RGBA8,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex.id);
    nt_gfx_destroy_texture(tex);
}

void test_gfx_make_texture_requires_explicit_format(void) {
    EXPECT_ASSERT(nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .data = s_test_pixels_4x4,
    }));
}

/* ---- Texture: NULL desc ---- */

void test_gfx_make_texture_null_desc(void) {
    nt_texture_t tex = nt_gfx_make_texture(NULL);
    TEST_ASSERT_EQUAL_UINT32(0, tex.id);
}

/* ---- Texture: NULL data allocates storage for later update_texture ---- */

void test_gfx_make_texture_null_data(void) {
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .format = NT_TEXTURE_FORMAT_RGBA8,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex.id);
    nt_gfx_update_texture(tex, 0, 0, 4, 4, s_test_pixels_4x4);
    nt_gfx_destroy_texture(tex);
}

/* ---- Texture: zero width ---- */

void test_gfx_make_texture_zero_width(void) {
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 0,
        .height = 4,
        .format = NT_TEXTURE_FORMAT_RGBA8,
        .data = s_test_pixels_4x4,
    });
    TEST_ASSERT_EQUAL_UINT32(0, tex.id);
}

/* ---- Texture: zero height ---- */

void test_gfx_make_texture_zero_height(void) {
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 0,
        .format = NT_TEXTURE_FORMAT_RGBA8,
        .data = s_test_pixels_4x4,
    });
    TEST_ASSERT_EQUAL_UINT32(0, tex.id);
}

/* ---- Texture: NPOT dimensions accepted ---- */

void test_gfx_make_texture_npot(void) {
    /* WebGL 2 / GL 3.3 fully support NPOT textures */
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 3,
        .height = 5,
        .format = NT_TEXTURE_FORMAT_RGBA8,
        .data = s_test_pixels_4x4,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex.id);
    nt_gfx_destroy_texture(tex);
}

/* ---- Texture: mag_filter clamped from mipmap variant ---- */

void test_gfx_make_texture_mag_filter_rejects_mipmap(void) {
    EXPECT_ASSERT(nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .format = NT_TEXTURE_FORMAT_RGBA8,
        .data = s_test_pixels_4x4,
        .mag_filter = NT_FILTER_LINEAR_MIPMAP_LINEAR,
    }));
}

/* ---- Texture: gen_mipmaps with mipmap min_filter ---- */

void test_gfx_make_texture_gen_mipmaps(void) {
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .format = NT_TEXTURE_FORMAT_RGBA8,
        .data = s_test_pixels_4x4,
        .min_filter = NT_FILTER_LINEAR_MIPMAP_LINEAR,
        .gen_mipmaps = true,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex.id);
    nt_gfx_destroy_texture(tex);
}

/* ---- Texture: mipmap min_filter rejected when gen_mipmaps=false ---- */

void test_gfx_make_texture_mipmap_filter_no_mipmaps(void) {
    EXPECT_ASSERT(nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .format = NT_TEXTURE_FORMAT_RGBA8,
        .data = s_test_pixels_4x4,
        .min_filter = NT_FILTER_LINEAR_MIPMAP_LINEAR,
        .gen_mipmaps = false,
    }));
}

/* ---- Texture: bind with valid handle ---- */

void test_gfx_bind_texture_valid(void) {
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .format = NT_TEXTURE_FORMAT_RGBA8,
        .data = s_test_pixels_4x4,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex.id);
    nt_gfx_bind_texture(tex, NT_SAMPLER_DEFAULT, 0);
    nt_gfx_destroy_texture(tex);
}

/* ---- Texture: bind with invalid handle is no-op ---- */

void test_gfx_bind_texture_invalid(void) {
    nt_texture_t tex = {.id = 0};
    nt_gfx_bind_texture(tex, NT_SAMPLER_DEFAULT, 0); /* must not crash */
}

/* ---- Texture: destroy and reuse slot ---- */

void test_gfx_destroy_texture_and_reuse(void) {
    nt_texture_t first = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .format = NT_TEXTURE_FORMAT_RGBA8,
        .data = s_test_pixels_4x4,
    });
    uint32_t first_slot = first.id & 0xFFFF;
    nt_gfx_destroy_texture(first);
    nt_texture_t second = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .format = NT_TEXTURE_FORMAT_RGBA8,
        .data = s_test_pixels_4x4,
    });
    uint32_t second_slot = second.id & 0xFFFF;
    TEST_ASSERT_EQUAL_UINT32(first_slot, second_slot);
    TEST_ASSERT_NOT_EQUAL_UINT32(first.id, second.id);
    nt_gfx_destroy_texture(second);
}

/* ---- Texture: double destroy is safe ---- */

void test_gfx_double_destroy_texture(void) {
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .format = NT_TEXTURE_FORMAT_RGBA8,
        .data = s_test_pixels_4x4,
    });
    nt_gfx_destroy_texture(tex);
    nt_gfx_destroy_texture(tex); /* must be safe no-op */
}

/* ---- Buffer: exhausting the pool is a configuration error ---- */

void test_gfx_buffer_pool_full_asserts(void) {
    nt_buffer_t buffers[8]; /* setUp: max_buffers = 8 */
    for (int i = 0; i < 8; i++) {
        buffers[i] = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_STREAM, .size = 64});
        TEST_ASSERT_NOT_EQUAL_UINT32(0, buffers[i].id);
    }
    EXPECT_ASSERT(nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_STREAM, .size = 64}));
    for (int i = 0; i < 8; i++) {
        nt_gfx_destroy_buffer(buffers[i]);
    }
}

/* ---- Texture: exhausting the pool is a configuration error ---- */

void test_gfx_texture_pool_full_asserts(void) {
    nt_texture_t textures[8];
    for (int i = 0; i < 8; i++) {
        textures[i] = nt_gfx_make_texture(&(nt_texture_desc_t){
            .width = 4,
            .height = 4,
            .format = NT_TEXTURE_FORMAT_RGBA8,
            .data = s_test_pixels_4x4,
        });
        TEST_ASSERT_NOT_EQUAL_UINT32(0, textures[i].id);
    }
    EXPECT_ASSERT(nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .format = NT_TEXTURE_FORMAT_RGBA8,
        .data = s_test_pixels_4x4,
    }));
    for (int i = 0; i < 8; i++) {
        nt_gfx_destroy_texture(textures[i]);
    }
}

/* ---- Activator: texture valid blob ---- */

void test_activate_texture_valid_blob(void) {
    uint32_t w = 2;
    uint32_t h = 2;
    uint32_t pixel_size = w * h * 4;
    uint32_t blob_size = (uint32_t)sizeof(NtTextureAssetHeaderV2) + pixel_size;
    uint8_t blob[sizeof(NtTextureAssetHeaderV2) + 16];
    NtTextureAssetHeaderV2 *hdr = (NtTextureAssetHeaderV2 *)blob;
    memset(blob, 0, sizeof(blob));
    hdr->magic = NT_TEXTURE_MAGIC;
    hdr->version = NT_TEXTURE_VERSION_V2;
    hdr->format = NT_TEXTURE_FORMAT_RGBA8;
    hdr->width = w;
    hdr->height = h;
    hdr->mip_count = 1;
    hdr->compression = NT_TEXTURE_COMPRESSION_RAW;
    hdr->data_size = pixel_size;
    /* Fill pixel data with non-zero */
    memset(blob + sizeof(NtTextureAssetHeaderV2), 0xFF, pixel_size);
    uint32_t handle = nt_gfx_activate_texture(blob, blob_size);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, handle);
    nt_gfx_deactivate_texture(handle);
}

/* ---- Activator: texture bad magic ---- */

void test_activate_texture_bad_magic(void) {
    uint8_t blob[sizeof(NtTextureAssetHeaderV2) + 16];
    NtTextureAssetHeaderV2 *hdr = (NtTextureAssetHeaderV2 *)blob;
    memset(blob, 0, sizeof(blob));
    hdr->magic = 0xDEADBEEF;
    hdr->width = 2;
    hdr->height = 2;
    hdr->mip_count = 1;
    uint32_t handle = nt_gfx_activate_texture(blob, sizeof(blob));
    TEST_ASSERT_EQUAL_UINT32(0, handle);
}

/* ---- Activator: texture too small ---- */

void test_activate_texture_too_small(void) {
    uint8_t blob[4]; /* smaller than header */
    memset(blob, 0, sizeof(blob));
    uint32_t handle = nt_gfx_activate_texture(blob, sizeof(blob));
    TEST_ASSERT_EQUAL_UINT32(0, handle);
}

/* ---- Activator: mesh valid blob (shared fixture = positive control) ---- */

static void fill_valid_mesh_blob(uint8_t *blob) {
    NtMeshAssetHeader *hdr = (NtMeshAssetHeader *)blob;
    hdr->magic = NT_MESH_MAGIC;
    hdr->version = NT_MESH_VERSION;
    hdr->stream_count = 1;
    hdr->index_type = 1;
    hdr->vertex_count = 1;
    hdr->index_count = 3;
    hdr->vertex_data_size = 12;
    hdr->index_data_size = 6;
    NtStreamDesc *sd = (NtStreamDesc *)(blob + sizeof(NtMeshAssetHeader));
    sd->name_hash = 0x12345678;
    sd->type = NT_STREAM_FLOAT32;
    sd->count = 3;
}

#define MESH_BLOB_BYTES (sizeof(NtMeshAssetHeader) + sizeof(NtStreamDesc) + 12 + 6)

void test_activate_mesh_valid_blob(void) {
    uint8_t blob[MESH_BLOB_BYTES];
    memset(blob, 0, sizeof(blob));
    fill_valid_mesh_blob(blob);
    uint32_t handle = nt_gfx_activate_mesh(blob, (uint32_t)sizeof(blob));
    TEST_ASSERT_NOT_EQUAL_UINT32(0, handle);
    nt_gfx_deactivate_mesh(handle);
}

/* ---- Activator: mesh bad magic ---- */

void test_activate_mesh_bad_magic(void) {
    uint8_t blob[sizeof(NtMeshAssetHeader)];
    memset(blob, 0, sizeof(blob));
    NtMeshAssetHeader *hdr = (NtMeshAssetHeader *)blob;
    hdr->magic = 0xDEADBEEF;
    uint32_t handle = nt_gfx_activate_mesh(blob, sizeof(blob));
    TEST_ASSERT_EQUAL_UINT32(0, handle);
}

/* ---- Activator: mesh bad version / inconsistent sizes / 32-bit wrap ---- */

void test_activate_mesh_rejects_bad_stream_type(void) {
    uint8_t blob[MESH_BLOB_BYTES];
    memset(blob, 0, sizeof(blob));
    fill_valid_mesh_blob(blob);
    ((NtStreamDesc *)(blob + sizeof(NtMeshAssetHeader)))->type = 99;
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_activate_mesh(blob, (uint32_t)sizeof(blob)));
}

void test_activate_mesh_rejects_index_size_mismatch(void) {
    uint8_t blob[MESH_BLOB_BYTES];
    memset(blob, 0, sizeof(blob));
    fill_valid_mesh_blob(blob);
    ((NtMeshAssetHeader *)blob)->index_count = 4; /* 4 * 2 != index_data_size 6 */
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_activate_mesh(blob, (uint32_t)sizeof(blob)));
}

void test_activate_mesh_rejects_index_count_with_type_none(void) {
    uint8_t blob[MESH_BLOB_BYTES];
    memset(blob, 0, sizeof(blob));
    fill_valid_mesh_blob(blob);
    ((NtMeshAssetHeader *)blob)->index_type = 0; /* index_count stays 3 */
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_activate_mesh(blob, (uint32_t)sizeof(blob)));
}

void test_activate_mesh_rejects_duplicate_name_hash(void) {
    /* Two streams with one hash would bind one shader location twice (last wins) */
    enum { BLOB2 = sizeof(NtMeshAssetHeader) + (2 * sizeof(NtStreamDesc)) + 20 };
    uint8_t blob[BLOB2];
    memset(blob, 0, sizeof(blob));
    NtMeshAssetHeader *hdr = (NtMeshAssetHeader *)blob;
    hdr->magic = NT_MESH_MAGIC;
    hdr->version = NT_MESH_VERSION;
    hdr->stream_count = 2;
    hdr->index_type = 0;
    hdr->vertex_count = 1;
    hdr->vertex_data_size = 20; /* f32x3 + f32x2 */
    NtStreamDesc *sd = (NtStreamDesc *)(blob + sizeof(NtMeshAssetHeader));
    sd[0] = (NtStreamDesc){.name_hash = 0xABCD1234, .type = NT_STREAM_FLOAT32, .count = 3};
    sd[1] = (NtStreamDesc){.name_hash = 0xABCD1234, .type = NT_STREAM_FLOAT32, .count = 2};
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_activate_mesh(blob, (uint32_t)sizeof(blob)));
}

void test_activate_mesh_rejects_normalized_float_stream(void) {
    uint8_t blob[MESH_BLOB_BYTES];
    memset(blob, 0, sizeof(blob));
    fill_valid_mesh_blob(blob);
    ((NtStreamDesc *)(blob + sizeof(NtMeshAssetHeader)))->normalized = 1;
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_activate_mesh(blob, (uint32_t)sizeof(blob)));
}

void test_activate_mesh_rejects_misaligned_stream(void) {
    /* f32x3 + u8x3 = stride 15, not a multiple of the f32 type size (WebGL2 rule) */
    enum { BLOB_MIS = sizeof(NtMeshAssetHeader) + (2 * sizeof(NtStreamDesc)) + 15 };
    uint8_t blob[BLOB_MIS];
    memset(blob, 0, sizeof(blob));
    NtMeshAssetHeader *hdr = (NtMeshAssetHeader *)blob;
    hdr->magic = NT_MESH_MAGIC;
    hdr->version = NT_MESH_VERSION;
    hdr->stream_count = 2;
    hdr->index_type = 0;
    hdr->vertex_count = 1;
    hdr->vertex_data_size = 15;
    NtStreamDesc *sd = (NtStreamDesc *)(blob + sizeof(NtMeshAssetHeader));
    sd[0] = (NtStreamDesc){.name_hash = 0x11111111, .type = NT_STREAM_FLOAT32, .count = 3};
    sd[1] = (NtStreamDesc){.name_hash = 0x22222222, .type = NT_STREAM_UINT8, .count = 3, .normalized = 1};
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_activate_mesh(blob, (uint32_t)sizeof(blob)));
}

void test_activate_mesh_rejects_misaligned_offset(void) {
    /* u8x2 + f32x1 + u8x2: f32 offset 2 misaligned, but stride 8 divides evenly by
     * every type size -- ONLY the offset branch can reject this blob */
    enum { BLOB_OFF = sizeof(NtMeshAssetHeader) + (3 * sizeof(NtStreamDesc)) + 8 };
    uint8_t blob[BLOB_OFF];
    memset(blob, 0, sizeof(blob));
    NtMeshAssetHeader *hdr = (NtMeshAssetHeader *)blob;
    hdr->magic = NT_MESH_MAGIC;
    hdr->version = NT_MESH_VERSION;
    hdr->stream_count = 3;
    hdr->index_type = 0;
    hdr->vertex_count = 1;
    hdr->vertex_data_size = 8;
    NtStreamDesc *sd = (NtStreamDesc *)(blob + sizeof(NtMeshAssetHeader));
    sd[0] = (NtStreamDesc){.name_hash = 0x11111111, .type = NT_STREAM_UINT8, .count = 2, .normalized = 1};
    sd[1] = (NtStreamDesc){.name_hash = 0x22222222, .type = NT_STREAM_FLOAT32, .count = 1};
    sd[2] = (NtStreamDesc){.name_hash = 0x33333333, .type = NT_STREAM_UINT8, .count = 2, .normalized = 1};
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_activate_mesh(blob, (uint32_t)sizeof(blob)));
}

void test_activate_texture_bad_version(void) {
    uint32_t pixel_size = 2 * 2 * 4;
    uint8_t blob[sizeof(NtTextureAssetHeaderV2) + 16];
    memset(blob, 0, sizeof(blob));
    NtTextureAssetHeaderV2 *hdr = (NtTextureAssetHeaderV2 *)blob;
    hdr->magic = NT_TEXTURE_MAGIC;
    hdr->version = NT_TEXTURE_VERSION_V2 + 1;
    hdr->format = NT_TEXTURE_FORMAT_RGBA8;
    hdr->width = 2;
    hdr->height = 2;
    hdr->mip_count = 1;
    hdr->compression = NT_TEXTURE_COMPRESSION_RAW;
    hdr->data_size = pixel_size;
    /* graceful reject, not a trap: stale packs must not be misparsed */
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_activate_texture(blob, (uint32_t)sizeof(NtTextureAssetHeaderV2) + pixel_size));
}

void test_activate_mesh_bad_version(void) {
    uint8_t blob[MESH_BLOB_BYTES];
    memset(blob, 0, sizeof(blob));
    fill_valid_mesh_blob(blob);
    ((NtMeshAssetHeader *)blob)->version = NT_MESH_VERSION + 1;
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_activate_mesh(blob, (uint32_t)sizeof(blob)));
}

void test_activate_mesh_rejects_size_mismatch(void) {
    /* vertex_data_size disagrees with vertex_count * stride */
    uint8_t blob[MESH_BLOB_BYTES];
    memset(blob, 0, sizeof(blob));
    fill_valid_mesh_blob(blob);
    ((NtMeshAssetHeader *)blob)->vertex_count = 2;
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_activate_mesh(blob, (uint32_t)sizeof(blob)));
}

void test_activate_mesh_rejects_32bit_size_wrap(void) {
    /* A 32-bit required-sum wraps to 46 <= blob size and would OOB-read. The blob is
     * arithmetically consistent (0x15555554 * 12 == 0xFFFFFFF0), so every product
     * cross-check passes and ONLY a 64-bit truncation check can reject it. */
    uint8_t blob[MESH_BLOB_BYTES];
    memset(blob, 0, sizeof(blob));
    fill_valid_mesh_blob(blob);
    ((NtMeshAssetHeader *)blob)->vertex_count = 0x15555554U;
    ((NtMeshAssetHeader *)blob)->vertex_data_size = 0xFFFFFFF0U;
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_activate_mesh(blob, (uint32_t)sizeof(blob)));
}

/* ---- Activator: shader valid blob ---- */

void test_activate_shader_valid_blob(void) {
    const char *source = "void main() {}\0";
    uint32_t code_size = (uint32_t)strlen(source) + 1;
    uint32_t blob_size = (uint32_t)sizeof(NtShaderCodeHeader) + code_size;
    uint8_t blob[sizeof(NtShaderCodeHeader) + 32];
    memset(blob, 0, sizeof(blob));

    NtShaderCodeHeader *hdr = (NtShaderCodeHeader *)blob;
    hdr->magic = NT_SHADER_CODE_MAGIC;
    hdr->version = NT_SHADER_CODE_VERSION;
    hdr->stage = NT_SHADER_STAGE_VERTEX;
    hdr->code_size = code_size;
    memcpy(blob + sizeof(NtShaderCodeHeader), source, code_size);

    uint32_t handle = nt_gfx_activate_shader(blob, blob_size);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, handle);
    nt_gfx_deactivate_shader(handle);
}

/* ---- Activator: shader bad magic ---- */

void test_activate_shader_bad_magic(void) {
    uint8_t blob[sizeof(NtShaderCodeHeader)];
    memset(blob, 0, sizeof(blob));
    NtShaderCodeHeader *hdr = (NtShaderCodeHeader *)blob;
    hdr->magic = 0xDEADBEEF;
    uint32_t handle = nt_gfx_activate_shader(blob, sizeof(blob));
    TEST_ASSERT_EQUAL_UINT32(0, handle);
}

/* ---- Activator: shader bad version ---- */

void test_activate_shader_bad_version(void) {
    const char *source = "void main() {}";
    uint32_t code_size = (uint32_t)strlen(source) + 1;
    uint8_t blob[sizeof(NtShaderCodeHeader) + 32];
    memset(blob, 0, sizeof(blob));
    NtShaderCodeHeader *hdr = (NtShaderCodeHeader *)blob;
    hdr->magic = NT_SHADER_CODE_MAGIC;
    hdr->version = NT_SHADER_CODE_VERSION + 1;
    hdr->code_size = code_size;
    memcpy(blob + sizeof(NtShaderCodeHeader), source, code_size);
    /* graceful reject, not a trap: stale packs must not be misparsed */
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_activate_shader(blob, (uint32_t)sizeof(NtShaderCodeHeader) + code_size));
}

void test_activate_shader_rejects_32bit_size_wrap(void) {
    uint8_t blob[sizeof(NtShaderCodeHeader) + 32];
    memset(blob, 0, sizeof(blob));
    NtShaderCodeHeader *hdr = (NtShaderCodeHeader *)blob;
    hdr->magic = NT_SHADER_CODE_MAGIC;
    hdr->version = NT_SHADER_CODE_VERSION;
    /* 32-bit sizeof(header) + code_size would wrap below blob size and OOB-read */
    hdr->code_size = 0xFFFFFFF8U;
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_activate_shader(blob, (uint32_t)sizeof(blob)));
}

void test_activate_shader_rejects_missing_nul(void) {
    uint8_t blob[sizeof(NtShaderCodeHeader) + 8];
    memset(blob, 0, sizeof(blob));
    NtShaderCodeHeader *hdr = (NtShaderCodeHeader *)blob;
    hdr->magic = NT_SHADER_CODE_MAGIC;
    hdr->version = NT_SHADER_CODE_VERSION;
    hdr->code_size = 4;
    /* deliberately no NUL inside code_size -- the guard under test must reject this */
    memcpy(blob + sizeof(NtShaderCodeHeader), "voidmain", 8); // NOLINT(bugprone-not-null-terminated-result)
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_activate_shader(blob, (uint32_t)sizeof(blob)));
}

/* ---- Deactivate mesh clears table ---- */

void test_deactivate_mesh_clears_table(void) {
    uint32_t streams_size = (uint32_t)sizeof(NtStreamDesc);
    uint32_t vdata_size = 12;
    uint32_t idata_size = 6;
    uint32_t blob_size = (uint32_t)sizeof(NtMeshAssetHeader) + streams_size + vdata_size + idata_size;
    uint8_t blob[sizeof(NtMeshAssetHeader) + sizeof(NtStreamDesc) + 12 + 6];
    memset(blob, 0, sizeof(blob));

    NtMeshAssetHeader *hdr = (NtMeshAssetHeader *)blob;
    hdr->magic = NT_MESH_MAGIC;
    hdr->version = NT_MESH_VERSION;
    hdr->stream_count = 1;
    hdr->index_type = 1;
    hdr->vertex_count = 1; /* 12 bytes of vertex data = 1 vertex at stride 12 */
    hdr->index_count = 3;
    hdr->vertex_data_size = vdata_size;
    hdr->index_data_size = idata_size;

    NtStreamDesc *sd = (NtStreamDesc *)(blob + sizeof(NtMeshAssetHeader));
    sd->type = NT_STREAM_FLOAT32;
    sd->count = 3;

    uint32_t handle = nt_gfx_activate_mesh(blob, blob_size);
    TEST_ASSERT_NOT_NULL(nt_gfx_get_mesh_info((nt_mesh_t){.id = handle}));
    nt_gfx_deactivate_mesh(handle);
    TEST_ASSERT_NULL(nt_gfx_get_mesh_info((nt_mesh_t){.id = handle}));
}

/* ---- Mesh info fields ---- */

void test_mesh_info_fields(void) {
    uint32_t streams_size = (uint32_t)sizeof(NtStreamDesc);
    uint32_t vdata_size = 36; /* 3 vertices * 3 floats * 4 bytes */
    uint32_t idata_size = 6;  /* 3 uint16 indices */
    uint32_t blob_size = (uint32_t)sizeof(NtMeshAssetHeader) + streams_size + vdata_size + idata_size;
    uint8_t blob[sizeof(NtMeshAssetHeader) + sizeof(NtStreamDesc) + 36 + 6];
    memset(blob, 0, sizeof(blob));

    NtMeshAssetHeader *hdr = (NtMeshAssetHeader *)blob;
    hdr->magic = NT_MESH_MAGIC;
    hdr->version = NT_MESH_VERSION;
    hdr->stream_count = 1;
    hdr->index_type = 1;
    hdr->vertex_count = 3;
    hdr->index_count = 3;
    hdr->vertex_data_size = vdata_size;
    hdr->index_data_size = idata_size;

    NtStreamDesc *sd = (NtStreamDesc *)(blob + sizeof(NtMeshAssetHeader));
    sd->type = NT_STREAM_FLOAT32;
    sd->count = 3;

    uint32_t handle = nt_gfx_activate_mesh(blob, blob_size);
    const nt_gfx_mesh_info_t *info = nt_gfx_get_mesh_info((nt_mesh_t){.id = handle});
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_EQUAL_UINT32(3, info->vertex_count);
    TEST_ASSERT_EQUAL_UINT32(3, info->index_count);
    TEST_ASSERT_EQUAL_UINT8(1, info->stream_count);
    TEST_ASSERT_EQUAL_UINT8(1, info->index_type);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, info->vbo.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, info->ibo.id);
    nt_gfx_deactivate_mesh(handle);
}

/* ---- Mesh wire decode (v3): SOA vertices, MESHOPT indices ---- */

void test_activate_mesh_soa_wire_decodes(void) {
    /* 2 streams (f32x3 + u8x4), 3 vertices (one triangle), no indices */
    enum { VD = 48 };
    uint8_t blob[sizeof(NtMeshAssetHeader) + (2 * sizeof(NtStreamDesc)) + VD];
    memset(blob, 0, sizeof(blob));
    NtMeshAssetHeader *hdr = (NtMeshAssetHeader *)blob;
    hdr->magic = NT_MESH_MAGIC;
    hdr->version = NT_MESH_VERSION;
    hdr->stream_count = 2;
    hdr->vertex_wire = NT_MESH_WIRE_VTX_SOA;
    hdr->vertex_count = 3;
    hdr->vertex_data_size = VD;
    NtStreamDesc *sd = (NtStreamDesc *)(blob + sizeof(NtMeshAssetHeader));
    sd[0].name_hash = 1;
    sd[0].type = NT_STREAM_FLOAT32;
    sd[0].count = 3;
    sd[1].name_hash = 2;
    sd[1].type = NT_STREAM_UINT8;
    sd[1].count = 4;
    sd[1].normalized = 1;
    /* Distinct plane bytes: position plane p0..p23, then color plane c0..c7 */
    uint8_t *wire = blob + sizeof(NtMeshAssetHeader) + (2 * sizeof(NtStreamDesc));
    for (uint32_t i = 0; i < VD; i++) {
        wire[i] = (uint8_t)(0x10 + i);
    }
    uint32_t handle = nt_gfx_activate_mesh(blob, (uint32_t)sizeof(blob));
    TEST_ASSERT_NOT_EQUAL_UINT32(0, handle);
    /* The UPLOADED bytes must be the interleaved GPU form, not the planes:
     * v = plane0[v*12..] (36 B position plane) + plane1[v*4..] */
    uint8_t expected[VD];
    for (uint32_t v = 0; v < 3; v++) {
        memcpy(expected + ((size_t)v * 16), wire + ((size_t)v * 12), 12);
        memcpy(expected + ((size_t)v * 16) + 12, wire + 36 + ((size_t)v * 4), 4);
    }
    TEST_ASSERT_EQUAL_HEX32(nt_hash32(expected, VD).value, nt_gfx_test_last_mesh_vertex_hash());
    nt_gfx_deactivate_mesh(handle);
}

/* meshopt v1 stream for the 2x5-grid triangle list (24 indices, 10 verts),
 * generated with the vendored codec -- keeps the positive path hermetic */
static const uint8_t k_meshopt_wire_24[] = {
    0xE1, 0xFE, 0x1E, 0x10, 0x0E, 0x10, 0x0E, 0x10, 0x0E, 0x0F, 0x0A, 0x00, //
    0x76, 0x87, 0x56, 0x67, 0x78, 0xA9, 0x86, 0x65, 0x89, 0x68, 0x98, 0x01, //
    0x69, 0x00, 0x00,
};

#define MESHOPT_BLOB_BYTES (sizeof(NtMeshAssetHeader) + sizeof(NtStreamDesc) + 120 + sizeof(k_meshopt_wire_24))

static void fill_meshopt_mesh_blob(uint8_t *blob) {
    NtMeshAssetHeader *hdr = (NtMeshAssetHeader *)blob;
    hdr->magic = NT_MESH_MAGIC;
    hdr->version = NT_MESH_VERSION;
    hdr->stream_count = 1;
    hdr->index_type = 1;
    hdr->index_wire = NT_MESH_WIRE_IDX_MESHOPT;
    hdr->vertex_count = 10;
    hdr->index_count = 24;
    hdr->vertex_data_size = 120;
    hdr->index_data_size = (uint32_t)sizeof(k_meshopt_wire_24);
    NtStreamDesc *sd = (NtStreamDesc *)(blob + sizeof(NtMeshAssetHeader));
    sd->name_hash = 1;
    sd->type = NT_STREAM_FLOAT32;
    sd->count = 3;
    memcpy(blob + sizeof(NtMeshAssetHeader) + sizeof(NtStreamDesc) + 120, k_meshopt_wire_24, sizeof(k_meshopt_wire_24));
}

void test_activate_mesh_meshopt_wire_decodes(void) {
    uint8_t blob[MESHOPT_BLOB_BYTES];
    memset(blob, 0, sizeof(blob));
    fill_meshopt_mesh_blob(blob);
    uint32_t handle = nt_gfx_activate_mesh(blob, (uint32_t)sizeof(blob));
    TEST_ASSERT_NOT_EQUAL_UINT32(0, handle);
    const nt_gfx_mesh_info_t *info = nt_gfx_get_mesh_info((nt_mesh_t){.id = handle});
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_EQUAL_UINT32(24, info->index_count);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, info->ibo.id);
    /* The UPLOADED bytes must be the decoded canonical triangle list */
    static const uint16_t expected_idx[24] = {0, 1, 5, 5, 1, 6, 6, 1, 2, 6, 2, 7, 7, 2, 3, 7, 3, 8, 8, 3, 4, 8, 4, 9};
    TEST_ASSERT_EQUAL_HEX32(nt_hash32(expected_idx, sizeof(expected_idx)).value, nt_gfx_test_last_mesh_index_hash());
    nt_gfx_deactivate_mesh(handle);
}

void test_activate_mesh_rejects_corrupt_meshopt_stream(void) {
    uint8_t blob[MESHOPT_BLOB_BYTES];
    memset(blob, 0, sizeof(blob));
    fill_meshopt_mesh_blob(blob);
    /* Garbage in place of the codec header byte -- decode must fail, activate must return 0 */
    blob[sizeof(NtMeshAssetHeader) + sizeof(NtStreamDesc) + 120] = 0x00;
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_activate_mesh(blob, (uint32_t)sizeof(blob)));
}

void test_activate_mesh_rejects_bad_wire_tags(void) {
    uint8_t blob[MESH_BLOB_BYTES];
    memset(blob, 0, sizeof(blob));
    fill_valid_mesh_blob(blob);
    ((NtMeshAssetHeader *)blob)->vertex_wire = 2;
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_activate_mesh(blob, (uint32_t)sizeof(blob)));
    ((NtMeshAssetHeader *)blob)->vertex_wire = 0;
    ((NtMeshAssetHeader *)blob)->index_wire = 2;
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_activate_mesh(blob, (uint32_t)sizeof(blob)));
}

void test_activate_mesh_rejects_raw_non_triangle_count(void) {
    /* GL draws only GL_TRIANGLES -- the safety net must reject a partial
     * trailing triangle even in RAW form */
    uint8_t blob[MESH_BLOB_BYTES + 2];
    memset(blob, 0, sizeof(blob));
    fill_valid_mesh_blob(blob);
    NtMeshAssetHeader *hdr = (NtMeshAssetHeader *)blob;
    hdr->index_count = 4;
    hdr->index_data_size = 8;
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_activate_mesh(blob, (uint32_t)sizeof(blob)));
}

void test_activate_mesh_rejects_raw_non_triangle_vertex_count(void) {
    /* non-indexed: vertex_count is the draw count -- 4 vertices would lose one */
    enum { VD4 = 48 };
    uint8_t blob[sizeof(NtMeshAssetHeader) + sizeof(NtStreamDesc) + VD4];
    memset(blob, 0, sizeof(blob));
    NtMeshAssetHeader *hdr = (NtMeshAssetHeader *)blob;
    hdr->magic = NT_MESH_MAGIC;
    hdr->version = NT_MESH_VERSION;
    hdr->stream_count = 1;
    hdr->vertex_count = 4;
    hdr->vertex_data_size = VD4;
    NtStreamDesc *sd = (NtStreamDesc *)(blob + sizeof(NtMeshAssetHeader));
    sd->name_hash = 1;
    sd->type = NT_STREAM_FLOAT32;
    sd->count = 3;
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_activate_mesh(blob, (uint32_t)sizeof(blob)));
}

void test_activate_mesh_rejects_meshopt_without_indices(void) {
    uint8_t blob[MESH_BLOB_BYTES];
    memset(blob, 0, sizeof(blob));
    fill_valid_mesh_blob(blob);
    NtMeshAssetHeader *hdr = (NtMeshAssetHeader *)blob;
    hdr->index_wire = NT_MESH_WIRE_IDX_MESHOPT;
    hdr->index_type = 0;
    hdr->index_count = 0;
    hdr->index_data_size = 6; /* wire bytes present but no index_type */
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_activate_mesh(blob, (uint32_t)sizeof(blob)));
}

void test_activate_mesh_rejects_meshopt_non_triangle_count(void) {
    uint8_t blob[MESH_BLOB_BYTES];
    memset(blob, 0, sizeof(blob));
    fill_valid_mesh_blob(blob);
    NtMeshAssetHeader *hdr = (NtMeshAssetHeader *)blob;
    hdr->index_wire = NT_MESH_WIRE_IDX_MESHOPT;
    hdr->index_count = 4;
    hdr->index_data_size = 6;
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_activate_mesh(blob, (uint32_t)sizeof(blob)));
}

void test_activate_mesh_rejects_meshopt_wire_larger_than_decoded(void) {
    uint8_t blob[MESH_BLOB_BYTES + 2];
    memset(blob, 0, sizeof(blob));
    fill_valid_mesh_blob(blob);
    NtMeshAssetHeader *hdr = (NtMeshAssetHeader *)blob;
    hdr->index_wire = NT_MESH_WIRE_IDX_MESHOPT;
    hdr->index_data_size = 8; /* > decoded 3 * 2 */
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_activate_mesh(blob, (uint32_t)sizeof(blob)));
}

void test_activate_mesh_rejects_meshopt_undersized_wire(void) {
    /* a tiny wire block claiming thousands of indices must be rejected BEFORE
     * the decoded-buffer allocation (codec minimum is 1 byte per triangle) */
    uint8_t blob[MESH_BLOB_BYTES];
    memset(blob, 0, sizeof(blob));
    fill_valid_mesh_blob(blob);
    NtMeshAssetHeader *hdr = (NtMeshAssetHeader *)blob;
    hdr->index_wire = NT_MESH_WIRE_IDX_MESHOPT;
    hdr->index_count = 3000; /* decoded 6000 B, wire 6 B < 1 + 1000 + 16 */
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_activate_mesh(blob, (uint32_t)sizeof(blob)));
}

void test_activate_mesh_rejects_meshopt_decoded_size_overflow(void) {
    uint8_t blob[MESH_BLOB_BYTES];
    memset(blob, 0, sizeof(blob));
    fill_valid_mesh_blob(blob);
    NtMeshAssetHeader *hdr = (NtMeshAssetHeader *)blob;
    hdr->index_wire = NT_MESH_WIRE_IDX_MESHOPT;
    hdr->index_type = 2;
    /* decoded = 0xC0000000 * 4 > UINT32_MAX: a 32-bit size_t malloc would wrap */
    hdr->index_count = 0xC0000000U;
    hdr->index_data_size = 6;
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_activate_mesh(blob, (uint32_t)sizeof(blob)));
}

/* ---- Uniform buffer tests ---- */

void test_make_uniform_buffer(void) {
    nt_buffer_t buf = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_UNIFORM,
        .usage = NT_USAGE_DYNAMIC,
        .size = 256,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, buf.id);
    nt_gfx_destroy_buffer(buf);
}

void test_bind_uniform_buffer(void) {
    nt_buffer_t buf = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_UNIFORM,
        .usage = NT_USAGE_DYNAMIC,
        .size = 256,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, buf.id);
    nt_gfx_bind_uniform_buffer(buf, 0); /* must not crash */
    nt_gfx_destroy_buffer(buf);
}

void test_update_uniform_buffer(void) {
    nt_buffer_t buf = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_UNIFORM,
        .usage = NT_USAGE_DYNAMIC,
        .size = 256,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, buf.id);
    uint8_t data[256];
    memset(data, 0xAB, sizeof(data));
    nt_gfx_update_buffer(buf, 0, data, 256);
    nt_gfx_destroy_buffer(buf);
}

void test_update_buffer_at_offset(void) {
    nt_buffer_t buf = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_VERTEX,
        .usage = NT_USAGE_STREAM,
        .size = 256,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, buf.id);
    uint8_t data[128];
    memset(data, 0xCD, sizeof(data));
    /* offset + size == capacity: must pass the range assert */
    nt_gfx_update_buffer(buf, 128, data, 128);
    nt_gfx_destroy_buffer(buf);
}

void test_update_buffer_rejects_out_of_range(void) {
    nt_buffer_t buf = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_VERTEX,
        .usage = NT_USAGE_STREAM,
        .size = 256,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, buf.id);
    uint8_t data[64];
    memset(data, 0xEF, sizeof(data));
    EXPECT_ASSERT(nt_gfx_update_buffer(buf, 257, data, 0));          /* offset past capacity */
    EXPECT_ASSERT(nt_gfx_update_buffer(buf, 224, data, 64));         /* offset + size past capacity */
    EXPECT_ASSERT(nt_gfx_update_buffer(buf, 0xFFFFFFFFU, data, 64)); /* overflow-prone pair */
    EXPECT_ASSERT(nt_gfx_update_buffer(buf, 0, NULL, 64));           /* NULL data, nonzero size */
    nt_gfx_destroy_buffer(buf);
}

void test_update_buffer_rejects_immutable(void) {
    uint8_t initial[64] = {0};
    nt_buffer_t buf = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_VERTEX,
        .usage = NT_USAGE_IMMUTABLE,
        .size = 64,
        .data = initial,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, buf.id);
    EXPECT_ASSERT(nt_gfx_update_buffer(buf, 0, initial, 64));
    nt_gfx_destroy_buffer(buf);
}

void test_destroy_uniform_buffer(void) {
    nt_buffer_t buf1 = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_UNIFORM,
        .usage = NT_USAGE_DYNAMIC,
        .size = 256,
    });
    uint32_t slot1 = buf1.id & 0xFFFF;
    nt_gfx_destroy_buffer(buf1);

    nt_buffer_t buf2 = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_UNIFORM,
        .usage = NT_USAGE_DYNAMIC,
        .size = 256,
    });
    uint32_t slot2 = buf2.id & 0xFFFF;
    TEST_ASSERT_EQUAL_UINT32(slot1, slot2);         /* reused slot */
    TEST_ASSERT_NOT_EQUAL_UINT32(buf1.id, buf2.id); /* different generation */
    nt_gfx_destroy_buffer(buf2);
}

/* ---- Global block registration ---- */

void test_register_global_block(void) {
    nt_gfx_register_global_block("Globals", 0);
    nt_gfx_register_global_block("Lighting", 1);

    const nt_global_block_t *blocks;
    uint32_t count;
    nt_gfx_get_global_blocks(&blocks, &count);
    TEST_ASSERT_EQUAL_UINT32(2, count);
    TEST_ASSERT_EQUAL_STRING("Globals", blocks[0].name);
    TEST_ASSERT_EQUAL_UINT32(0, blocks[0].binding_slot);
    TEST_ASSERT_TRUE(blocks[0].active);
    TEST_ASSERT_EQUAL_STRING("Lighting", blocks[1].name);
    TEST_ASSERT_EQUAL_UINT32(1, blocks[1].binding_slot);
    TEST_ASSERT_TRUE(blocks[1].active);
}

void test_register_global_block_max(void) {
    for (uint32_t i = 0; i < NT_GFX_MAX_GLOBAL_BLOCKS; i++) {
        nt_gfx_register_global_block("Block", i);
    }
    const nt_global_block_t *blocks;
    uint32_t count;
    nt_gfx_get_global_blocks(&blocks, &count);
    TEST_ASSERT_EQUAL_UINT32(NT_GFX_MAX_GLOBAL_BLOCKS, count);
}

void test_register_global_block_cleared_on_shutdown(void) {
    nt_gfx_register_global_block("Globals", 0);
    nt_gfx_shutdown();
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 8, .max_programs = 4, .max_pipelines = 4, .max_buffers = 8, .max_textures = 8, .max_meshes = 8, .max_vertex_inputs = 8, .max_render_targets = 16});
    const nt_global_block_t *blocks;
    uint32_t count;
    nt_gfx_get_global_blocks(&blocks, &count);
    TEST_ASSERT_EQUAL_UINT32(0, count);
}

/* ---- RGBA16F texture creation ---- */

void test_gfx_make_texture_rgba16f(void) {
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .format = NT_TEXTURE_FORMAT_RGBA16F,
        .data = s_test_half_4x4,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex.id);
    nt_gfx_destroy_texture(tex);
}

/* ---- RG16UI texture creation ---- */

void test_gfx_make_texture_rg16ui(void) {
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .format = NT_TEXTURE_FORMAT_RG16UI,
        .data = s_test_rg16ui_4x4,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex.id);
    nt_gfx_destroy_texture(tex);
}

/* ---- RG16UI: assert fires on non-NEAREST filter ---- */

void test_gfx_make_texture_rg16ui_rejects_linear(void) {
    EXPECT_ASSERT(nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .format = NT_TEXTURE_FORMAT_RG16UI,
        .min_filter = NT_FILTER_LINEAR,
        .data = s_test_rg16ui_4x4,
    }));
}

/* ---- RG16UI: assert fires on gen_mipmaps ---- */

void test_gfx_make_texture_rg16ui_rejects_mipmaps(void) {
    EXPECT_ASSERT(nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .format = NT_TEXTURE_FORMAT_RG16UI,
        .gen_mipmaps = true,
        .data = s_test_rg16ui_4x4,
    }));
}

void test_gfx_make_depth_texture_uses_explicit_format(void) {
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .format = NT_TEXTURE_FORMAT_DEPTH16,
        .min_filter = NT_FILTER_NEAREST,
        .mag_filter = NT_FILTER_NEAREST,
        .wrap_u = NT_WRAP_REPEAT,
        .wrap_v = NT_WRAP_MIRRORED_REPEAT,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex.id);
    nt_gfx_destroy_texture(tex);
}

void test_gfx_make_depth_texture_rejects_linear_without_compare_mode(void) {
    EXPECT_ASSERT(nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .format = NT_TEXTURE_FORMAT_DEPTH24,
        .min_filter = NT_FILTER_LINEAR,
        .mag_filter = NT_FILTER_NEAREST,
    }));
}

void test_gfx_update_depth_texture_rejected(void) {
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .format = NT_TEXTURE_FORMAT_DEPTH24,
        .min_filter = NT_FILTER_NEAREST,
        .mag_filter = NT_FILTER_NEAREST,
    });
    uint32_t depth = 0;

    EXPECT_ASSERT(nt_gfx_update_texture(tex, 0, 0, 1, 1, &depth));
    nt_gfx_destroy_texture(tex);
}

/* ---- GPU caps: max_texture_size accessible ---- */

void test_gfx_gpu_caps_max_texture_size(void) {
    const nt_gfx_gpu_caps_t *caps = nt_gfx_gpu_caps();
    TEST_ASSERT_NOT_NULL(caps);
    TEST_ASSERT_EQUAL_UINT32(4096, caps->max_texture_size);
}

/* ---- update_texture: valid sub-region ---- */

void test_gfx_update_texture_valid(void) {
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .format = NT_TEXTURE_FORMAT_RGBA8,
        .data = s_test_pixels_4x4,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex.id);
    uint8_t sub_data[2 * 2 * 4]; /* 2x2 RGBA8 */
    memset(sub_data, 128, sizeof(sub_data));
    nt_gfx_update_texture(tex, 0, 0, 2, 2, sub_data);
    nt_gfx_destroy_texture(tex);
}

/* ---- update_texture: full region ---- */

void test_gfx_update_texture_full(void) {
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .format = NT_TEXTURE_FORMAT_RGBA8,
        .data = s_test_pixels_4x4,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex.id);
    nt_gfx_update_texture(tex, 0, 0, 4, 4, s_test_pixels_4x4);
    nt_gfx_destroy_texture(tex);
}

/* ---- update_texture: invalid handle ---- */

void test_gfx_update_texture_invalid_handle(void) {
    nt_texture_t tex = {0};
    uint8_t data[16] = {0};
    nt_gfx_update_texture(tex, 0, 0, 1, 1, data); /* should log error, not crash */
}

/* Pipelines are baked objects: loss frees their slots outright (the vertex-
 * input rule), the handle goes stale, the bind is the ordinary invalid path,
 * and every slot is allocatable again. */
void test_gfx_pipeline_slots_freed_by_context_loss(void) {
    nt_program_t prog = nt_gfx_make_program(make_test_vs(), make_test_fs());
    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog});

    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_frame(); /* latches the loss, frees pipeline slots */
    nt_gfx_fake_set_context_lost(false);
    nt_gfx_begin_frame(); /* recovery completes */

    TEST_ASSERT_FALSE(nt_gfx_pipeline_valid(pip));
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip); /* stale: ordinary invalid path, no trap */
    nt_gfx_end_pass();
    nt_gfx_destroy_pipeline(pip); /* stale: tolerated no-op */

    /* Programs survive as husks; relink before building new pipelines. */
    nt_gfx_destroy_program(prog);
    nt_program_t fresh = nt_gfx_make_program(make_test_vs(), make_test_fs());
    nt_pipeline_t pips[4];
    for (uint32_t i = 0; i < 4; i++) {
        pips[i] = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = fresh});
        TEST_ASSERT_NOT_EQUAL_UINT32(0, pips[i].id);
    }
    for (uint32_t i = 0; i < 4; i++) {
        nt_gfx_destroy_pipeline(pips[i]);
    }
    nt_gfx_end_frame();
}

/* Buffers have no auto-restore path, so a husk means the owner skipped the
 * recreate contract -- the one case a bind cannot absorb. */
void test_gfx_bind_uniform_buffer_on_husk_asserts(void) {
    nt_buffer_t ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_UNIFORM,
        .usage = NT_USAGE_DYNAMIC,
        .size = 256,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, ubo.id);

    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_frame(); /* latches the loss, zeroes every backend record */
    nt_gfx_fake_set_context_lost(false);
    nt_gfx_begin_frame(); /* restore succeeds; the buffer stays a husk */
    TEST_ASSERT_FALSE(g_nt_gfx.context_lost);

    EXPECT_ASSERT(nt_gfx_bind_uniform_buffer(ubo, 0));
    nt_gfx_end_frame();
}

/* A texture husk can also be a render-target attachment whose restore failed --
 * a runtime GPU failure, so the bind reports it instead of trapping. */
void test_gfx_bind_texture_on_husk_logs_and_skips_backend(void) {
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .data = s_test_pixels_4x4,
        .format = NT_TEXTURE_FORMAT_RGBA8,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex.id);

    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_frame();
    nt_gfx_fake_set_context_lost(false);
    nt_gfx_begin_frame();
    TEST_ASSERT_FALSE(g_nt_gfx.context_lost);
    TEST_ASSERT_FALSE(nt_gfx_texture_ready(tex));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_test_texture_backend_id(tex));

    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_bound_texture_count());
    nt_gfx_bind_texture(tex, NT_SAMPLER_DEFAULT, 0); /* logs, no trap */
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_bound_texture_count());
    /* The sampler bind rides on the texture bind: skipping one skips both. */
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_bind_sampler_count());
    nt_gfx_end_frame();
}

/* The sampler is validated against the texture of the same call, so an override
 * that cannot sample that storage is rejected whatever the unit held before. */
void test_gfx_bind_texture_rejects_sampler_incompatible_with_its_texture(void) {
    nt_texture_t color = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .data = s_test_pixels_4x4,
        .format = NT_TEXTURE_FORMAT_RGBA8,
    });
    nt_texture_t depth = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .format = NT_TEXTURE_FORMAT_DEPTH24,
        .min_filter = NT_FILTER_NEAREST,
        .mag_filter = NT_FILTER_NEAREST,
    });
    TEST_ASSERT_TRUE(nt_gfx_texture_ready(color));
    TEST_ASSERT_TRUE(nt_gfx_texture_ready(depth));

    nt_sampler_t linear = nt_gfx_make_sampler(&(nt_sampler_desc_t){.min_filter = NT_FILTER_LINEAR, .mag_filter = NT_FILTER_LINEAR});
    nt_gfx_begin_frame();
    nt_gfx_bind_texture(color, linear, 0);
    /* LINEAR on raw depth is incomplete sampling, even on the unit colour just left. */
    EXPECT_ASSERT(nt_gfx_bind_texture(depth, linear, 0));
    nt_gfx_end_frame();
}

/* Render-target attachments are rejected earlier, so an update reaching a husk
 * is always a primary texture the owner never recreated. */
void test_gfx_update_texture_on_husk_asserts(void) {
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .data = s_test_pixels_4x4,
        .format = NT_TEXTURE_FORMAT_RGBA8,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex.id);

    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_frame();
    nt_gfx_fake_set_context_lost(false);
    nt_gfx_begin_frame();
    TEST_ASSERT_FALSE(g_nt_gfx.context_lost);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_test_texture_backend_id(tex));

    EXPECT_ASSERT(nt_gfx_update_texture(tex, 0, 0, 4, 4, s_test_pixels_4x4));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_update_texture_count());
    nt_gfx_end_frame();
}

/* A husk write is the same owner bug as a husk bind, so it traps the same way. */
void test_gfx_update_buffer_on_husk_asserts(void) {
    nt_buffer_t vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_VERTEX,
        .usage = NT_USAGE_DYNAMIC,
        .size = 256,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, vbo.id);

    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_frame();
    nt_gfx_fake_set_context_lost(false);
    nt_gfx_begin_frame();
    TEST_ASSERT_FALSE(g_nt_gfx.context_lost);

    const uint8_t data[64] = {0};
    EXPECT_ASSERT(nt_gfx_update_buffer(vbo, 0, data, sizeof(data)));
    nt_gfx_end_frame();
}

void test_gfx_orphan_buffer_on_husk_asserts(void) {
    nt_buffer_t vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_VERTEX,
        .usage = NT_USAGE_DYNAMIC,
        .size = 256,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, vbo.id);

    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_frame();
    nt_gfx_fake_set_context_lost(false);
    nt_gfx_begin_frame();
    TEST_ASSERT_FALSE(g_nt_gfx.context_lost);

    const uint8_t data[64] = {0};
    EXPECT_ASSERT(nt_gfx_orphan_buffer(vbo, data, sizeof(data)));
    nt_gfx_end_frame();
}

/* ---- Per-frame draw call counter ---- */

/* Restore invalidates earlier render decisions, so the restored frame permits clears but rejects draws. */
void test_gfx_restored_frame_rejects_draws(void) {
    nt_shader_t vs = make_test_vs();
    nt_shader_t fs = make_test_fs();
    nt_program_t prog = nt_gfx_make_program(vs, fs);
    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .program = prog,
    });

    /* Lose it, then let begin_frame see the context back: that frame is the
     * restored one and carries the flag. */
    nt_gfx_fake_set_context_lost(true);
    nt_gfx_begin_frame();
    nt_gfx_fake_set_context_lost(false);
    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.context_restored);

    /* Clearing is still allowed -- the game may want the screen blanked. */
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});

    nt_pipeline_t rebuilt = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .program = nt_gfx_make_program(make_test_vs(), make_test_fs()),
    });
    nt_gfx_bind_pipeline(rebuilt);
    /* Full draw state bound: the ONLY reason these trap is the restored-frame
     * rule, not a missing vertex input. */
    bind_test_vertex_input();
    EXPECT_ASSERT(nt_gfx_draw(0, 0));
    EXPECT_ASSERT(nt_gfx_draw_indexed(0, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_get_frame_draw_calls());

    nt_gfx_end_pass();
    nt_gfx_end_frame();

    /* And the very next frame draws normally. */
    nt_gfx_begin_frame();
    TEST_ASSERT_FALSE(g_nt_gfx.context_restored);
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(rebuilt);
    bind_test_vertex_input();
    nt_gfx_draw(0, 0);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_get_frame_draw_calls());
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    (void)pip;
}

void test_gfx_failed_bind_drops_the_previous_pipeline(void) {
    /* A rejected bind must not leave the previous pipeline live: draw only gates
     * on bound_pipeline, so the new pipeline's vertices would go through it. */
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "v"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "f"});
    nt_program_t prog = nt_gfx_make_program(vs, fs);
    nt_pipeline_desc_t desc = {
        .program = prog,
    };
    nt_pipeline_t live = nt_gfx_make_pipeline(&desc);
    nt_pipeline_t dead = nt_gfx_make_pipeline(&desc);
    nt_gfx_destroy_pipeline(dead); /* handle keeps its id, generation goes stale */

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});

    nt_gfx_bind_pipeline(live);
    bind_test_vertex_input();
    nt_gfx_draw(0, 0);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_get_frame_draw_calls());

    nt_gfx_bind_pipeline(dead); /* rejected: stale handle */
    EXPECT_ASSERT(nt_gfx_draw(0, 0));
    EXPECT_ASSERT(nt_gfx_draw_indexed(0, 0, 0));
    EXPECT_ASSERT(nt_gfx_set_uniform_int(nt_hash32_str("u_tex"), 0));
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_get_frame_draw_calls());

    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_gfx_destroy_pipeline(live);
    nt_gfx_destroy_program(prog);
    nt_gfx_destroy_shader(vs);
    nt_gfx_destroy_shader(fs);
}

void test_gfx_frame_draw_calls(void) {
    /* Separate draw-call counter for nt_debug_overlay consumption.
     * Verifies counter starts at 0, increments by 1 per draw API, resets on begin_frame. */

    /* Minimal pipeline so draws have something bound (test backend accepts any handle). */
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "v"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "f"});
    nt_program_t prog = nt_gfx_make_program(vs, fs);
    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .program = prog,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pip.id);

    /* Counter starts at 0 (setUp called nt_gfx_init which zero-inits g_nt_gfx;
     * the static counter sits in BSS and is also 0 on the first test call). */
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_get_frame_draw_calls());

    nt_gfx_begin_frame();
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_get_frame_draw_calls());

    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip);
    bind_test_vertex_input();

    /* Fire each of the 4 entry points exactly once. */
    nt_gfx_draw(0, 0);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_get_frame_draw_calls());
    nt_gfx_draw_indexed(0, 0, 0);
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_get_frame_draw_calls());
    nt_gfx_draw_instanced(0, 0, 0);
    TEST_ASSERT_EQUAL_UINT32(3, nt_gfx_get_frame_draw_calls());
    nt_gfx_draw_indexed_instanced(0, 0, 0, 0);
    TEST_ASSERT_EQUAL_UINT32(4, nt_gfx_get_frame_draw_calls());

    nt_gfx_end_pass();
    nt_gfx_end_frame();

    /* Counter persists across end_frame (frame_stats does too; reset is begin_frame's job). */
    TEST_ASSERT_EQUAL_UINT32(4, nt_gfx_get_frame_draw_calls());

    /* Next begin_frame must reset to 0. */
    nt_gfx_begin_frame();
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_get_frame_draw_calls());
    nt_gfx_end_frame();

    nt_gfx_destroy_pipeline(pip);
    nt_gfx_destroy_shader(vs);
    nt_gfx_destroy_shader(fs);
}

/* The setter must reach the backend with the caller's key and value unchanged. */
void test_gfx_uniform_records_hash_and_value(void) {
    const float vec[4] = {1.0F, 2.0F, 3.0F, 4.0F};
    nt_program_t prog = nt_gfx_make_program(make_test_vs(), make_test_fs());
    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog});

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip);
    nt_gfx_fake_reset();
    nt_gfx_set_uniform_int(nt_hash32_str("u_slot"), 3);
    nt_gfx_set_uniform_vec4(nt_hash32_str("u_tint"), vec);
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_uniform_int_count());
    TEST_ASSERT_EQUAL_UINT32(nt_hash32_str("u_slot").value, nt_gfx_fake_uniform_int_hash_at(0));
    TEST_ASSERT_EQUAL_INT(3, nt_gfx_fake_uniform_int_value_at(0));

    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_uniform_vec4_count());
    TEST_ASSERT_EQUAL_UINT32(nt_hash32_str("u_tint").value, nt_gfx_fake_uniform_vec4_hash_at(0));
    /* Routed to the bound pipeline's program, not to 0. Which program it is
     * needs distinct GL ids -- test_nt_gfx_bind_mirrors_native covers that. */
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_fake_last_uniform_program());

    /* UNITY_EXCLUDE_FLOAT: compare the exact small integers as ints. */
    float recorded[4];
    nt_gfx_fake_uniform_vec4_value_at(0, recorded);
    for (uint32_t i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_INT32((int32_t)vec[i], (int32_t)recorded[i]);
    }
}

/* A uniform value belongs to a program, so a write with no pipeline bound has
 * no program to address -- it traps instead of landing nowhere. */
void test_gfx_uniform_without_bound_pipeline_traps(void) {
    const float vec[4] = {1.0F, 2.0F, 3.0F, 4.0F};

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    EXPECT_ASSERT(nt_gfx_set_uniform_vec4(nt_hash32_str("u_tint"), vec));
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

/* Bound state is pass-scoped: a bind or uniform write with no pass open has no
 * draw to reach, so it traps instead of silently landing on the next pass. */
void test_gfx_binds_outside_a_pass_trap(void) {
    static const float verts[9] = {0};
    const float vec[4] = {1.0F, 2.0F, 3.0F, 4.0F};
    nt_program_t prog = nt_gfx_make_program(make_test_vs(), make_test_fs());
    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog});
    nt_buffer_t vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_IMMUTABLE, .data = verts, .size = sizeof(verts)});
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){
        .layout = {.attr_count = 1, .stride = 12, .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 3}}},
        .vertex_buffer = vbo,
    });

    nt_gfx_begin_frame();
    EXPECT_ASSERT(nt_gfx_bind_pipeline(pip));
    EXPECT_ASSERT(nt_gfx_bind_vertex_input(vi));
    EXPECT_ASSERT(nt_gfx_bind_instance_buffer(vbo, 0));
    EXPECT_ASSERT(nt_gfx_set_uniform_vec4(nt_hash32_str("u_tint"), vec));
    nt_gfx_end_frame();
}

/* The pass clear touches draw state, so begin_pass discards the bound pipeline
 * and vertex input rather than letting the next pass inherit them. */
void test_gfx_begin_pass_discards_bound_state(void) {
    nt_program_t prog = nt_gfx_make_program(make_test_vs(), make_test_fs());
    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog});

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip);
    bind_test_vertex_input();
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_test_bound_pipeline());
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_test_bound_vertex_input());
    nt_gfx_end_pass();

    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_test_bound_pipeline());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_test_bound_vertex_input());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

/* Slots are recycled, so only the full handle tells a rebound pipeline apart
 * from the destroyed one that occupied its slot. */
void test_gfx_bound_pipeline_holds_the_generation(void) {
    nt_program_t prog = nt_gfx_make_program(make_test_vs(), make_test_fs());
    nt_pipeline_t old_pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, old_pip.id);
    nt_gfx_destroy_pipeline(old_pip);

    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog});
    TEST_ASSERT_EQUAL_UINT32(nt_pool_slot_index(old_pip.id), nt_pool_slot_index(pip.id));
    TEST_ASSERT_NOT_EQUAL_UINT32(old_pip.id, pip.id);

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip);
    TEST_ASSERT_EQUAL_UINT32(pip.id, nt_gfx_test_bound_pipeline());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_gfx_pool_alloc_returns_nonzero);
    RUN_TEST(test_gfx_pool_alloc_unique);
    RUN_TEST(test_gfx_pool_free_and_realloc);
    RUN_TEST(test_gfx_pool_valid_accepts_live);
    RUN_TEST(test_gfx_pool_valid_rejects_zero);
    RUN_TEST(test_gfx_pool_valid_rejects_stale);
    RUN_TEST(test_gfx_pool_full_returns_zero);
    RUN_TEST(test_gfx_slot_index_extracts_correctly);
    RUN_TEST(test_gfx_init_shutdown);
    RUN_TEST(test_gfx_make_destroy_shader);
    RUN_TEST(test_gfx_make_destroy_buffer);
    RUN_TEST(test_gfx_defaults_applied);
    RUN_TEST(test_gfx_make_destroy_pipeline);
    RUN_TEST(test_gfx_pipeline_survives_shader_destroy);
    RUN_TEST(test_gfx_state_machine_valid_cycle);
    RUN_TEST(test_gfx_double_destroy_shader);
    RUN_TEST(test_gfx_double_destroy_buffer);
    RUN_TEST(test_gfx_pipeline_asserts_unready_program);
    RUN_TEST(test_gfx_two_pipelines_share_one_program);
    RUN_TEST(test_gfx_sampler_queries_use_each_program_backend);
    RUN_TEST(test_gfx_new_program_does_not_inherit_a_destroyed_sampler_table);
    RUN_TEST(test_gfx_backend_texture_binding_batch_visits_only_active_units);
    RUN_TEST(test_gfx_destroy_program_accepts_invalid);
    RUN_TEST(test_gfx_destroy_program_asserts_on_a_stale_handle);
    RUN_TEST(test_gfx_context_restore_yields_a_new_program_handle);
    RUN_TEST(test_gfx_destroy_program_destroys_its_pipelines);
    RUN_TEST(test_gfx_draw_asserts_when_bound_program_is_destroyed);
    RUN_TEST(test_gfx_make_program_does_not_dedup);
    RUN_TEST(test_gfx_program_valid_and_ready);
    RUN_TEST(test_gfx_destroy_program_invalidates);
    RUN_TEST(test_gfx_program_slot_reused_after_destroy);
    RUN_TEST(test_gfx_program_survives_shader_destroy);
    RUN_TEST(test_gfx_program_pool_full_asserts);
    RUN_TEST(test_gfx_make_program_asserts_invalid_shader);
    RUN_TEST(test_gfx_make_program_asserts_on_link_failure);
    RUN_TEST(test_gfx_make_program_context_lost_returns_invalid);
    RUN_TEST(test_gfx_make_program_rejects_a_stage_left_unready_by_a_loss);
    RUN_TEST(test_gfx_program_link_context_loss_releases_every_slot);
    RUN_TEST(test_gfx_context_loss_keeps_handle_drops_ready);
    RUN_TEST(test_gfx_register_global_block_after_program_is_allowed);
    RUN_TEST(test_gfx_pipeline_asserts_null_desc);
    RUN_TEST(test_gfx_pipeline_context_lost_returns_invalid);
    RUN_TEST(test_gfx_pipeline_pool_full_asserts);
    RUN_TEST(test_gfx_pipeline_backend_failure_returns_invalid);
    RUN_TEST(test_activate_mesh_rejects_misaligned_offset);
    RUN_TEST(test_activate_texture_bad_version);
    RUN_TEST(test_activate_mesh_rejects_misaligned_stream);
    RUN_TEST(test_activate_mesh_rejects_duplicate_name_hash);
    RUN_TEST(test_activate_mesh_rejects_normalized_float_stream);
    RUN_TEST(test_activate_mesh_bad_version);
    RUN_TEST(test_activate_mesh_rejects_size_mismatch);
    RUN_TEST(test_activate_mesh_rejects_32bit_size_wrap);
    RUN_TEST(test_activate_mesh_rejects_bad_stream_type);
    RUN_TEST(test_activate_mesh_rejects_index_size_mismatch);
    RUN_TEST(test_activate_mesh_rejects_index_count_with_type_none);
    RUN_TEST(test_activate_shader_bad_version);
    RUN_TEST(test_activate_shader_rejects_32bit_size_wrap);
    RUN_TEST(test_activate_shader_rejects_missing_nul);
    RUN_TEST(test_gfx_pipeline_rejects_invalid_blend_factor);
    RUN_TEST(test_gfx_pipeline_rejects_invalid_blend_operation);
    RUN_TEST(test_gfx_pipeline_rejects_invalid_alpha_blend_factors);
    RUN_TEST(test_gfx_pipeline_rejects_invalid_alpha_blend_operation);
    RUN_TEST(test_gfx_pipeline_rejects_invalid_blend_constant_color);
    RUN_TEST(test_gfx_pipeline_key_neighbouring_programs_never_alias);
    RUN_TEST(test_gfx_pipeline_key_canonicalizes_disabled_blend_and_offset);
    RUN_TEST(test_gfx_pipeline_key_ignores_label_and_splits_on_float_payloads);
    RUN_TEST(test_gfx_pipeline_key_asserts_out_of_range_lanes);
    RUN_TEST(test_gfx_pipeline_rejects_src_alpha_saturate_as_destination_factor);
    RUN_TEST(test_gfx_pipeline_rejects_mixed_constant_color_and_alpha_factors);
    RUN_TEST(test_gfx_pipeline_accepts_constant_color_rgb_with_constant_alpha_source_alpha);
    RUN_TEST(test_gfx_pipeline_accepts_src_alpha_saturate_for_source_alpha);
    /* Texture tests */
    RUN_TEST(test_gfx_make_texture_valid);
    RUN_TEST(test_gfx_make_texture_requires_explicit_format);
    RUN_TEST(test_gfx_make_texture_null_desc);
    RUN_TEST(test_gfx_make_texture_null_data);
    RUN_TEST(test_gfx_make_texture_zero_width);
    RUN_TEST(test_gfx_make_texture_zero_height);
    RUN_TEST(test_gfx_make_texture_npot);
    RUN_TEST(test_gfx_make_texture_mag_filter_rejects_mipmap);
    RUN_TEST(test_gfx_make_texture_gen_mipmaps);
    RUN_TEST(test_gfx_make_texture_mipmap_filter_no_mipmaps);
    RUN_TEST(test_gfx_bind_texture_valid);
    RUN_TEST(test_gfx_bind_texture_invalid);
    RUN_TEST(test_gfx_destroy_texture_and_reuse);
    RUN_TEST(test_gfx_double_destroy_texture);
    RUN_TEST(test_gfx_buffer_pool_full_asserts);
    RUN_TEST(test_gfx_texture_pool_full_asserts);
    /* Activator tests */
    RUN_TEST(test_activate_texture_valid_blob);
    RUN_TEST(test_activate_texture_bad_magic);
    RUN_TEST(test_activate_texture_too_small);
    RUN_TEST(test_activate_mesh_valid_blob);
    RUN_TEST(test_activate_mesh_bad_magic);
    RUN_TEST(test_activate_shader_valid_blob);
    RUN_TEST(test_activate_shader_bad_magic);
    RUN_TEST(test_deactivate_mesh_clears_table);
    RUN_TEST(test_mesh_info_fields);
    RUN_TEST(test_activate_mesh_soa_wire_decodes);
    RUN_TEST(test_activate_mesh_meshopt_wire_decodes);
    RUN_TEST(test_activate_mesh_rejects_corrupt_meshopt_stream);
    RUN_TEST(test_activate_mesh_rejects_bad_wire_tags);
    RUN_TEST(test_activate_mesh_rejects_raw_non_triangle_count);
    RUN_TEST(test_activate_mesh_rejects_raw_non_triangle_vertex_count);
    RUN_TEST(test_activate_mesh_rejects_meshopt_without_indices);
    RUN_TEST(test_activate_mesh_rejects_meshopt_non_triangle_count);
    RUN_TEST(test_activate_mesh_rejects_meshopt_wire_larger_than_decoded);
    RUN_TEST(test_activate_mesh_rejects_meshopt_undersized_wire);
    RUN_TEST(test_activate_mesh_rejects_meshopt_decoded_size_overflow);
    /* Uniform buffer tests */
    RUN_TEST(test_make_uniform_buffer);
    RUN_TEST(test_bind_uniform_buffer);
    RUN_TEST(test_update_uniform_buffer);
    RUN_TEST(test_update_buffer_at_offset);
    RUN_TEST(test_update_buffer_rejects_out_of_range);
    RUN_TEST(test_update_buffer_rejects_immutable);
    RUN_TEST(test_destroy_uniform_buffer);
    /* Global block registration tests */
    RUN_TEST(test_register_global_block);
    RUN_TEST(test_register_global_block_max);
    RUN_TEST(test_register_global_block_cleared_on_shutdown);
    /* New pixel format tests */
    RUN_TEST(test_gfx_make_texture_rgba16f);
    RUN_TEST(test_gfx_make_texture_rg16ui);
    RUN_TEST(test_gfx_make_texture_rg16ui_rejects_linear);
    RUN_TEST(test_gfx_make_texture_rg16ui_rejects_mipmaps);
    RUN_TEST(test_gfx_make_depth_texture_uses_explicit_format);
    RUN_TEST(test_gfx_make_depth_texture_rejects_linear_without_compare_mode);
    RUN_TEST(test_gfx_update_depth_texture_rejected);
    /* GPU caps tests */
    RUN_TEST(test_gfx_gpu_caps_max_texture_size);
    /* Texture update tests */
    RUN_TEST(test_gfx_update_texture_valid);
    RUN_TEST(test_gfx_update_texture_full);
    RUN_TEST(test_gfx_update_texture_invalid_handle);
    RUN_TEST(test_gfx_pipeline_slots_freed_by_context_loss);
    RUN_TEST(test_gfx_bind_uniform_buffer_on_husk_asserts);
    RUN_TEST(test_gfx_bind_texture_on_husk_logs_and_skips_backend);
    RUN_TEST(test_gfx_bind_texture_rejects_sampler_incompatible_with_its_texture);
    RUN_TEST(test_gfx_update_texture_on_husk_asserts);
    RUN_TEST(test_gfx_update_buffer_on_husk_asserts);
    RUN_TEST(test_gfx_orphan_buffer_on_husk_asserts);
    RUN_TEST(test_gfx_bound_pipeline_holds_the_generation);
    RUN_TEST(test_gfx_restored_frame_rejects_draws);
    RUN_TEST(test_gfx_failed_bind_drops_the_previous_pipeline);
    RUN_TEST(test_gfx_frame_draw_calls);
    RUN_TEST(test_gfx_uniform_records_hash_and_value);
    RUN_TEST(test_gfx_uniform_without_bound_pipeline_traps);
    RUN_TEST(test_gfx_binds_outside_a_pass_trap);
    RUN_TEST(test_gfx_begin_pass_discards_bound_state);
    return UNITY_END();
}
