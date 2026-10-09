#define main nt_asteroids_example_main
// NOLINTNEXTLINE(bugprone-suspicious-include): exercise the real example code, including its static helpers.
#include "../../examples/asteroids/main.c"
#undef main

#include "input/nt_input_internal.h"
#include "test_helpers/nt_gfx_fake.h"
#include "unity.h"

#include <stdlib.h>

void setUp(void) {}
void tearDown(void) {}

static void test_rng_streams_are_deterministic_and_bounded(void) {
    ast_rng_t a = rng_make(AST_SEED, 7);
    ast_rng_t b = rng_make(AST_SEED, 7);
    ast_rng_t other = rng_make(AST_SEED, 8);
    ast_rng_t bounded = rng_make(AST_SEED, 9);
    bool differs = false;
    for (uint32_t i = 0; i < 1000; i++) {
        const uint32_t value = rng_u32(&a);
        TEST_ASSERT_EQUAL_UINT32(value, rng_u32(&b));
        differs = differs || value != rng_u32(&other);
        TEST_ASSERT_LESS_THAN_UINT32(13, rng_index(&bounded, 13));
        const float uniform = rng_uniform(&bounded, -2.0F, 3.0F);
        TEST_ASSERT_TRUE(uniform >= -2.0F && uniform < 3.0F);
        TEST_ASSERT_TRUE(isfinite(rng_normal(&bounded, 0.0F, 1.0F)));
    }
    TEST_ASSERT_TRUE(differs);
}

/* Simplex noise is continuous: a wrong simplex corner ordering shows up as a
 * jump between points 1e-3 apart in some of the six tetrahedron orderings. */
static void test_simplex3_is_continuous_in_every_simplex_ordering(void) {
    ast_rng_t rng = rng_make(AST_SEED, 42);
    float worst = 0.0F;
    for (uint32_t i = 0; i < 20000; i++) {
        const float p[3] = {rng_uniform(&rng, -20.0F, 20.0F), rng_uniform(&rng, -20.0F, 20.0F), rng_uniform(&rng, -20.0F, 20.0F)};
        const float base = simplex3(p[0], p[1], p[2]);
        for (uint32_t axis = 0; axis < 3; axis++) {
            float q[3] = {p[0], p[1], p[2]};
            q[axis] += 0.001F;
            worst = fmaxf(worst, fabsf(simplex3(q[0], q[1], q[2]) - base));
        }
    }
    TEST_ASSERT_TRUE(worst < 0.02F);
}

/* Coarse LODs index only their own vertex prefix, every triangle faces out,
 * and each LOD is a closed surface: 2 * edges == 3 * faces. */
static void test_sphere_lods_are_closed_outward_vertex_prefixes(void) {
    for (uint32_t lod = 0; lod < AST_SUBDIVISIONS; lod++) {
        TEST_ASSERT_EQUAL_UINT32((10U * (1U << (2U * lod))) + 2U, s_lod_vertices[lod]);
        TEST_ASSERT_EQUAL_UINT32(60U << (2U * lod), s_lod_indices[lod]);
        const uint16_t *indices = s_sphere_indices[lod];
        for (uint32_t i = 0; i < s_lod_indices[lod]; i += 3) {
            for (uint32_t corner = 0; corner < 3; corner++) {
                TEST_ASSERT_LESS_THAN_UINT32(s_lod_vertices[lod], indices[i + corner]);
            }
            float u[3];
            float v[3];
            float n[3];
            glm_vec3_sub(s_sphere[indices[i + 1U]], s_sphere[indices[i]], u);
            glm_vec3_sub(s_sphere[indices[i + 2U]], s_sphere[indices[i]], v);
            glm_vec3_cross(u, v, n);
            TEST_ASSERT_TRUE(glm_vec3_dot(n, s_sphere[indices[i]]) > 0.0F);
        }
        /* Euler characteristic of a sphere: V - E + F == 2. */
        const uint32_t faces = s_lod_indices[lod] / 3U;
        TEST_ASSERT_EQUAL_UINT32(2, s_lod_vertices[lod] + faces - (faces * 3U / 2U));
    }
    for (uint32_t i = 0; i < AST_FINE_VERTICES; i++) {
        TEST_ASSERT_TRUE(fabsf((glm_vec3_norm(s_sphere[i])) - (1.0F)) <= 0.00001F);
    }
}

static rock_vertex_t s_test_lods[AST_SUBDIVISIONS][AST_FINE_VERTICES];

static void build_test_rock(uint32_t variant) {
    rock_vertex_t *lods[AST_SUBDIVISIONS] = {s_test_lods[0], s_test_lods[1], s_test_lods[2], s_test_lods[3]};
    rock_build(variant, lods);
}

static float test_vertex_radius(const rock_vertex_t *vertex) {
    const float p[3] = {(float)vertex->position[0] / 32767.0F, (float)vertex->position[1] / 32767.0F, (float)vertex->position[2] / 32767.0F};
    return glm_vec3_norm((float *)p);
}

static void test_rock_shapes_fill_the_shader_radius_range_and_are_seeded(void) {
    static rock_vertex_t first[AST_FINE_VERTICES];
    build_test_rock(3);
    memcpy(first, s_test_lods[3], sizeof(first));
    build_test_rock(3);
    TEST_ASSERT_EQUAL_MEMORY(first, s_test_lods[3], sizeof(first));
    float low = FLT_MAX;
    float high = 0.0F;
    for (uint32_t i = 0; i < AST_FINE_VERTICES; i++) {
        low = fminf(low, test_vertex_radius(&first[i]));
        high = fmaxf(high, test_vertex_radius(&first[i]));
    }
    TEST_ASSERT_TRUE(fabsf(low - 0.525F) <= 0.0002F);
    TEST_ASSERT_TRUE(fabsf(high - 0.9F) <= 0.0002F);
    build_test_rock(4);
    uint32_t moved = 0;
    for (uint32_t i = 0; i < AST_FINE_VERTICES; i++) {
        moved += fabsf(test_vertex_radius(&first[i]) - test_vertex_radius(&s_test_lods[3][i])) > 0.001F ? 1U : 0U;
    }
    TEST_ASSERT_GREATER_THAN_UINT32(AST_FINE_VERTICES / 2U, moved);
}

/* Coarse LODs share the finest positions as a prefix; normals follow each LOD's own faces. */
static void test_rock_lods_share_positions_and_have_unit_normals(void) {
    build_test_rock(11);
    for (uint32_t lod = 0; lod < AST_SUBDIVISIONS; lod++) {
        for (uint32_t i = 0; i < s_lod_vertices[lod]; i++) {
            const rock_vertex_t *vertex = &s_test_lods[lod][i];
            TEST_ASSERT_EQUAL_MEMORY(s_test_lods[AST_SUBDIVISIONS - 1U][i].position, vertex->position, sizeof(vertex->position));
            TEST_ASSERT_EQUAL_INT16(0, vertex->position[3]);
            const float normal[3] = {(float)vertex->normal[0] / 127.0F, (float)vertex->normal[1] / 127.0F, (float)vertex->normal[2] / 127.0F};
            TEST_ASSERT_TRUE(fabsf(glm_vec3_norm((float *)normal) - 1.0F) <= 0.02F);
            TEST_ASSERT_EQUAL_INT8(0, vertex->normal[3]);
        }
    }
}

/* The 4-wide path and the scalar tail/fallback compute the same fBm. */
static void test_rock_noise_matches_scalar_fbm(void) {
    float noise[AST_FINE_VERTICES];
    rock_noise(5, noise);
    ast_rng_t rng = rng_make(AST_SEED, 0x524F434BU + 5U);
    const float gain = rng_normal(&rng, 0.95F, 0.04F);
    const float offset[3] = {rng_uniform(&rng, 0, 100), rng_uniform(&rng, 0, 100), rng_uniform(&rng, 0, 100)};
    for (uint32_t i = 0; i < AST_FINE_VERTICES; i++) {
        const float expected = fbm3((s_sphere[i][0] * 0.5F) + offset[0], (s_sphere[i][1] * 0.5F) + offset[1], (s_sphere[i][2] * 0.5F) + offset[2], 2.0F, gain);
        TEST_ASSERT_TRUE(fabsf(expected - noise[i]) <= 0.0001F);
    }
}

static void test_instances_are_seeded_and_within_their_level(void) {
    const complexity_t *complexity = &s_complexities[9];
    generate_instances(complexity);
    const asteroid_t sample = s_asteroids[12345];
    generate_instances(complexity);
    TEST_ASSERT_EQUAL_MEMORY(&sample, &s_asteroids[12345], sizeof(sample));
    for (uint32_t i = 0; i < complexity->instances; i++) {
        const asteroid_t *asteroid = &s_asteroids[i];
        TEST_ASSERT_LESS_THAN_UINT32(complexity->unique_meshes, asteroid->mesh_index);
        TEST_ASSERT_LESS_THAN_UINT32(complexity->textures, asteroid->texture_index);
        TEST_ASSERT_TRUE(asteroid->scale >= complexity->scale_ratio * 1.5F - 0.0001F && asteroid->scale <= complexity->scale_ratio * 15.0F + 0.0001F);
        TEST_ASSERT_TRUE(fabsf((glm_vec3_norm((float *)asteroid->spin_axis)) - (1.0F)) <= 0.0001F);
        TEST_ASSERT_TRUE(fabsf((asteroid->scale_translate[15]) - (1.0F)) <= 0.0F);
        for (uint32_t c = 0; c < 3; c++) {
            TEST_ASSERT_TRUE(asteroid->deep[c] >= 0.0F && asteroid->deep[c] <= 1.0F);
            TEST_ASSERT_TRUE(asteroid->shallow[c] >= 0.0F && asteroid->shallow[c] <= 1.0F);
        }
    }
}

/* Row-vector convention: world = spin * scale_translate * orbit. With no spin
 * and a quarter orbit the ring translation (r, h, 0) rotates to (0, h, r). */
static void test_world_matrix_spins_in_place_and_orbits_the_planet(void) {
    asteroid_t asteroid = {.spin_axis = {0, 1, 0}, .orbit_angle = -(float)AST_PI / 2.0F};
    asteroid.scale_translate[0] = 2;
    asteroid.scale_translate[5] = 3;
    asteroid.scale_translate[10] = 4;
    asteroid.scale_translate[12] = 100;
    asteroid.scale_translate[13] = 7;
    asteroid.scale_translate[15] = 1;
    float world[16];
    asteroid_world_matrix(world, &asteroid, 0.0);
    TEST_ASSERT_TRUE(fabsf((world[12]) - (0.0F)) <= 0.001F);
    TEST_ASSERT_TRUE(fabsf((world[13]) - (7.0F)) <= 0.001F);
    TEST_ASSERT_TRUE(fabsf((fabsf(world[14])) - (100.0F)) <= 0.001F);
    TEST_ASSERT_TRUE(fabsf((world[5]) - (3.0F)) <= 0.001F);
}

static void test_instance_attributes_transpose_source_matrix_once(void) {
    const float world[16] = {1, 2, 3, 0, 4, 5, 6, 0, 7, 8, 9, 0, 11, 13, 17, 1};
    asteroid_instance_t instance = {0};
    instance_world_rows(&instance, world);
    const float expected[12] = {1, 4, 7, 11, 2, 5, 8, 13, 3, 6, 9, 17};
    TEST_ASSERT_EQUAL_MEMORY(expected, instance.world_rows, sizeof(expected));
}

static void test_left_handed_camera_preserves_reversed_zero_to_one_depth(void) {
    const camera_orientation_t camera = {{0, 0, 0}, {0, 0, 1}, {0, 1, 0}};
    float view[16];
    float projection[16];
    float vp[16];
    camera_matrices(&camera, 1.6F, view, projection, vp);
    const float near_z = (0.01F * projection[10] + projection[14]) / (0.01F * projection[11]);
    const float far_z = (600.0F * projection[10] + projection[14]) / (600.0F * projection[11]);
    TEST_ASSERT_TRUE(fabsf(near_z - 1.0F) < 0.00001F);
    TEST_ASSERT_TRUE(fabsf(far_z) < 0.00001F);
    TEST_ASSERT_TRUE(fabsf(view[0] - 1.0F) < 0.00001F);
    TEST_ASSERT_TRUE(fabsf(view[5] - 1.0F) < 0.00001F);
    TEST_ASSERT_TRUE(fabsf(view[10] - 1.0F) < 0.00001F);
}

static void test_original_complexity_counts_are_not_batch_counts(void) {
    const uint32_t instances[10] = {1000, 2000, 3000, 4000, 5000, 10000, 15000, 20000, 35000, 50000};
    const uint32_t meshes[10] = {35, 50, 75, 100, 200, 300, 400, 500, 750, 1000};
    for (uint32_t level = 0; level < 10; level++) {
        TEST_ASSERT_EQUAL_UINT32(instances[level], s_complexities[level].instances);
        TEST_ASSERT_EQUAL_UINT32(meshes[level], s_complexities[level].unique_meshes);
    }
}

static void test_lod_clamps_and_subdivision_count_match_source_policy(void) {
    TEST_ASSERT_EQUAL_UINT32(0, asteroid_lod_index(0.01F, 10000, 0.06F, 4));
    TEST_ASSERT_EQUAL_UINT32(3, asteroid_lod_index(100, 1, 0.06F, 4));
    TEST_ASSERT_EQUAL_UINT32(1, asteroid_lod_index(100, 1, 0.06F, 2));
    TEST_ASSERT_EQUAL_UINT32(0, asteroid_lod_index(100, 1, 0.06F, 1));
}

/* Pinned ActionCamera.cpp calls Move(current_world - pressed_world), and
 * Move adds to the current eye/aim. With an identity view and 90-degree FOV,
 * 100 screen pixels at width 1000 produce 0.2 world units per source formula. */
static void assert_source_pan(uint32_t pointer_index) {
    const nt_window_t old_window = g_nt_window;
    const camera_orientation_t old_camera = s_camera;
    const camera_orientation_t identity_camera = {{0, 0, 0}, {0, 0, 1}, {0, 1, 0}};
    nt_input_init();
    g_nt_window.fb_width = 1000;
    g_nt_window.fb_height = 1000;
    s_camera = identity_camera;
    s_eye_pivot = false;
    nt_pointer_t *pointer = &g_nt_input.pointers[pointer_index];
    *pointer = (nt_pointer_t){.x = 500, .y = 500, .active = true};
    pointer->buttons[NT_BUTTON_MIDDLE] = (nt_button_state_t){.is_down = true, .is_pressed = true};
    handle_input(0);
    pointer->buttons[NT_BUTTON_MIDDLE].is_pressed = false;
    pointer->x = 600;
    pointer->dx = 100;
    handle_input(0);
    TEST_ASSERT_TRUE(fabsf(s_camera.eye[0] - 0.2F) < 0.00001F);
    pointer->x = 700;
    handle_input(0);
    const float split_eye_x = s_camera.eye[0];
    TEST_ASSERT_TRUE(fabsf(split_eye_x - 0.6F) < 0.00001F);
    TEST_ASSERT_TRUE(fabsf(s_camera.aim[0] - 0.6F) < 0.00001F);

    s_camera = identity_camera;
    pointer->x = 500;
    pointer->dx = 0;
    pointer->buttons[NT_BUTTON_MIDDLE].is_pressed = true;
    handle_input(0);
    pointer->x = 700;
    pointer->dx = 200;
    pointer->buttons[NT_BUTTON_MIDDLE].is_pressed = false;
    handle_input(0);
    TEST_ASSERT_TRUE(fabsf(s_camera.eye[0] - 0.4F) < 0.00001F);
    TEST_ASSERT_TRUE(fabsf(split_eye_x - s_camera.eye[0] - 0.2F) < 0.00001F);
    nt_input_shutdown();
    s_camera = old_camera;
    g_nt_window = old_window;
}

static void test_pan_matches_source_event_accumulation(void) { assert_source_pan(0); }

static void test_routed_pan_preserves_source_math_for_all_pointer_slots(void) {
    for (uint32_t p = 0; p < NT_INPUT_MAX_POINTERS; p++) {
        assert_source_pan(p);
    }
}

static void test_ui_owned_drag_stays_blocked_until_release(void) {
    nt_input_init();
    nt_pointer_t *pointer = &g_nt_input.pointers[3];
    *pointer = (nt_pointer_t){.active = true, .x = 900, .y = 100};
    pointer->buttons[NT_BUTTON_LEFT].is_down = true;
    s_pointer_owner[3][NT_BUTTON_LEFT] = 2;
    pointer->x = 100;
    pointer->dx = -800;
    TEST_ASSERT_FALSE(scene_pointer(3).buttons[NT_BUTTON_LEFT].is_down);
    TEST_ASSERT_EQUAL_UINT8(2, s_pointer_owner[3][NT_BUTTON_LEFT]);
    pointer->buttons[NT_BUTTON_LEFT].is_down = false;
    (void)scene_pointer(3);
    TEST_ASSERT_EQUAL_UINT8(0, s_pointer_owner[3][NT_BUTTON_LEFT]);
    pointer->buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_down = true, .is_pressed = true};
    TEST_ASSERT_TRUE(scene_pointer(3).buttons[NT_BUTTON_LEFT].is_down);
    TEST_ASSERT_EQUAL_UINT8(1, s_pointer_owner[3][NT_BUTTON_LEFT]);
    nt_input_shutdown();
}

static void test_reset_cancels_pending_camera_motion(void) {
    const camera_orientation_t old_camera = s_camera;
    nt_input_init();
    s_zoom_until = 0.3F;
    s_zoom_elapsed = 0.1F;
    s_zoom_factor = 0.8F;
    s_zoom_direction = 1;
    s_key_active[NT_KEY_W] = true;
    s_key_elapsed[NT_KEY_W] = 0.1F;
    g_nt_input.pointers[1].buttons[NT_BUTTON_LEFT].is_down = true;
    s_pointer_owner[1][NT_BUTTON_LEFT] = 1;
    reset_camera_input(false);
    TEST_ASSERT_EQUAL_MEMORY(&s_initial_camera, &s_camera, sizeof(s_camera));
    TEST_ASSERT_TRUE(s_zoom_until == 0 && s_zoom_elapsed == 0 && s_zoom_factor == 1 && s_zoom_direction == 0);
    TEST_ASSERT_FALSE(s_key_active[NT_KEY_W]);
    TEST_ASSERT_EQUAL_UINT8(2, s_pointer_owner[1][NT_BUTTON_LEFT]);
    nt_input_shutdown();
    s_camera = old_camera;
}

static void test_modal_and_resize_cancel_scene_drag_before_input_early_return(void) {
    const nt_window_t old_window = g_nt_window;
    const camera_orientation_t old_camera = s_camera;
    const bool old_hide_hud = s_hide_hud;
    g_nt_window.fb_width = 1000;
    g_nt_window.fb_height = 1000;
    s_camera = s_initial_camera;
    nt_input_init();
    nt_pointer_t *pointer = &g_nt_input.pointers[0];
    *pointer = (nt_pointer_t){.active = true, .x = 300, .y = 300};
    pointer->buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_down = true, .is_pressed = true};
    handle_input(0);
    TEST_ASSERT_EQUAL_UINT8(1, s_pointer_owner[0][NT_BUTTON_LEFT]);
    s_ui_keyboard_blocked = true;
    pointer->buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_released = true};
    handle_input(0);
    TEST_ASSERT_EQUAL_UINT8(0, s_pointer_owner[0][NT_BUTTON_LEFT]);
    pointer->x = 900;
    pointer->buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_down = true, .is_pressed = true};
    handle_input(0);
    TEST_ASSERT_EQUAL_UINT8(2, s_pointer_owner[0][NT_BUTTON_LEFT]);
    pointer->buttons[NT_BUTTON_LEFT].is_pressed = false;
    nt_input_set_key(NT_KEY_F4, true);
    s_hide_hud = false;
    handle_input(0);
    TEST_ASSERT_TRUE(s_hide_hud);
    nt_input_set_key(NT_KEY_F4, false);
    nt_input_poll();
    s_ui_keyboard_blocked = false;
    pointer->x = 200;
    pointer->dx = -700;
    handle_input(0);
    TEST_ASSERT_EQUAL_MEMORY(&s_initial_camera, &s_camera, sizeof(s_camera));
    TEST_ASSERT_EQUAL_UINT8(2, s_pointer_owner[0][NT_BUTTON_LEFT]);
    pointer->buttons[NT_BUTTON_LEFT] = (nt_button_state_t){0};
    handle_input(0);
    pointer->buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_down = true, .is_pressed = true};
    pointer->dx = 0;
    handle_input(0);
    TEST_ASSERT_EQUAL_UINT8(1, s_pointer_owner[0][NT_BUTTON_LEFT]);
    g_nt_window.fb_width = 0;
    pointer->buttons[NT_BUTTON_LEFT].is_pressed = false;
    handle_input(0);
    TEST_ASSERT_EQUAL_UINT8(2, s_pointer_owner[0][NT_BUTTON_LEFT]);
    g_nt_window.fb_width = 1000;
    pointer->x = 800;
    pointer->dx = 600;
    handle_input(0);
    TEST_ASSERT_EQUAL_MEMORY(&s_initial_camera, &s_camera, sizeof(s_camera));
    nt_input_shutdown();
    g_nt_window = old_window;
    s_camera = old_camera;
    s_hide_hud = old_hide_hud;
}

static void test_keyboard_resets_win_over_already_sampled_drag(void) {
    const nt_window_t old_window = g_nt_window;
    const camera_orientation_t old_camera = s_camera;
    const camera_orientation_t old_light = s_light;
    g_nt_window.fb_width = 1000;
    g_nt_window.fb_height = 1000;
    s_camera = s_initial_camera;
    s_light = s_initial_light;
    nt_input_init();
    nt_pointer_t *pointer = &g_nt_input.pointers[0];
    *pointer = (nt_pointer_t){.active = true, .x = 300, .y = 300};
    pointer->buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_down = true, .is_pressed = true};
    handle_input(0);
    pointer->buttons[NT_BUTTON_LEFT].is_pressed = false;
    pointer->x = 400;
    pointer->dx = 100;
    handle_input(0);
    TEST_ASSERT_TRUE(fabsf(s_initial_camera.eye[0] - s_camera.eye[0]) > 0.001F);
    nt_input_set_key(NT_KEY_LALT, true);
    nt_input_set_key(NT_KEY_R, true);
    pointer->x = 500;
    handle_input(0);
    TEST_ASSERT_EQUAL_MEMORY(&s_initial_camera, &s_camera, sizeof(s_camera));
    TEST_ASSERT_EQUAL_UINT8(2, s_pointer_owner[0][NT_BUTTON_LEFT]);
    nt_input_clear_all_keys();
    nt_input_poll();
    *pointer = (nt_pointer_t){.active = true, .x = 300, .y = 300};
    pointer->buttons[NT_BUTTON_RIGHT] = (nt_button_state_t){.is_down = true, .is_pressed = true};
    handle_input(0);
    pointer->buttons[NT_BUTTON_RIGHT].is_pressed = false;
    pointer->x = 400;
    pointer->dx = 100;
    handle_input(0);
    TEST_ASSERT_TRUE(fabsf(s_initial_light.eye[0] - s_light.eye[0]) > 0.001F);
    nt_input_set_key(NT_KEY_LCTRL, true);
    nt_input_set_key(NT_KEY_L, true);
    pointer->x = 500;
    handle_input(0);
    TEST_ASSERT_EQUAL_MEMORY(&s_initial_light, &s_light, sizeof(s_light));
    TEST_ASSERT_EQUAL_UINT8(2, s_pointer_owner[0][NT_BUTTON_RIGHT]);
    nt_input_shutdown();
    g_nt_window = old_window;
    s_camera = old_camera;
    s_light = old_light;
}

typedef struct {
    nt_window_t window;
    camera_orientation_t camera;
    bool paused, eye_pivot, ui_built, keyboard_blocked, escape_consumed;
} touch_test_state_t;

static touch_test_state_t begin_touch_test(void) {
    const touch_test_state_t old = {g_nt_window, s_camera, s_paused, s_eye_pivot, s_ui_built, s_ui_keyboard_blocked, s_ui_escape_consumed};
    nt_input_init();
    g_nt_window.fb_width = 1000;
    g_nt_window.fb_height = 1000;
    s_ui_built = false;
    s_ui_keyboard_blocked = false;
    s_ui_escape_consumed = false;
    s_paused = true;
    s_eye_pivot = false;
    memset(s_pointer_owner, 0, sizeof(s_pointer_owner));
    reset_camera_input(false);
    return old;
}

static void end_touch_test(touch_test_state_t old) {
    nt_input_shutdown();
    memset(&s_touch, 0, sizeof(s_touch));
    memset(s_pointer_owner, 0, sizeof(s_pointer_owner));
    s_ui_built = old.ui_built;
    s_ui_keyboard_blocked = old.keyboard_blocked;
    s_ui_escape_consumed = old.escape_consumed;
    g_nt_window = old.window;
    s_camera = old.camera;
    s_paused = old.paused;
    s_eye_pivot = old.eye_pivot;
}

static void test_touch_orbit_pinch_and_transition_rebase(void) {
    const touch_test_state_t old = begin_touch_test();
    nt_pointer_t *first = &g_nt_input.pointers[3];
    nt_pointer_t *second = &g_nt_input.pointers[6];
    *first = (nt_pointer_t){.id = 101, .active = true, .type = NT_POINTER_TOUCH, .x = 350, .y = 350};
    first->buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_down = true, .is_pressed = true};
    handle_input(0);
    TEST_ASSERT_EQUAL_MEMORY(&s_initial_camera, &s_camera, sizeof(s_camera));
    first->buttons[NT_BUTTON_LEFT].is_pressed = false;
    first->x = 440;
    first->dx = 90;
    handle_input(0);
    TEST_ASSERT_TRUE(fabsf(s_camera.eye[0] - s_initial_camera.eye[0]) > 0.001F);
    const camera_orientation_t orbited = s_camera;
    const float distance = glm_vec3_distance(s_camera.eye, s_camera.aim);
    *second = (nt_pointer_t){.id = 202, .active = true, .type = NT_POINTER_TOUCH, .x = 620, .y = 350};
    second->buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_down = true, .is_pressed = true};
    handle_input(0);
    TEST_ASSERT_EQUAL_MEMORY(&orbited, &s_camera, sizeof(s_camera));
    second->buttons[NT_BUTTON_LEFT].is_pressed = false;
    second->x = 800;
    second->dx = 180;
    handle_input(0);
    TEST_ASSERT_TRUE(fabsf((distance / 2) - glm_vec3_distance(s_camera.eye, s_camera.aim)) < 0.001F);
    TEST_ASSERT_EQUAL_MEMORY(orbited.aim, s_camera.aim, sizeof(s_camera.aim));
    TEST_ASSERT_EQUAL_MEMORY(orbited.up, s_camera.up, sizeof(s_camera.up));
    second->x = 442;
    handle_input(0);
    TEST_ASSERT_TRUE(fabsf(400 - glm_vec3_distance(s_camera.eye, s_camera.aim)) < 0.001F);
    second->x = 10000;
    handle_input(0);
    TEST_ASSERT_TRUE(fabsf(60 - glm_vec3_distance(s_camera.eye, s_camera.aim)) < 0.001F);
    const camera_orientation_t pinched = s_camera;
    second->buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_released = true};
    first->x += 30;
    first->dx = 30;
    handle_input(0);
    TEST_ASSERT_EQUAL_MEMORY(&pinched, &s_camera, sizeof(s_camera));
    first->x += 40;
    first->dx = 40;
    handle_input(0);
    TEST_ASSERT_TRUE(fabsf(s_camera.eye[0] - pinched.eye[0]) > 0.001F);
    TEST_ASSERT_TRUE(fabsf(60 - glm_vec3_distance(s_camera.eye, s_camera.aim)) < 0.001F);
    end_touch_test(old);
}

static void test_touch_coincident_fingers_replacement_and_third_finger(void) {
    const touch_test_state_t old = begin_touch_test();
    for (uint32_t p = 2; p < 4; p++) {
        g_nt_input.pointers[p] = (nt_pointer_t){.id = 101 + p, .active = true, .type = NT_POINTER_TOUCH, .x = 400, .y = 400};
        g_nt_input.pointers[p].buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_down = true, .is_pressed = true};
    }
    handle_input(0);
    g_nt_input.pointers[2].buttons[NT_BUTTON_LEFT].is_pressed = false;
    g_nt_input.pointers[3].buttons[NT_BUTTON_LEFT].is_pressed = false;
    g_nt_input.pointers[3].x = 500;
    handle_input(0);
    TEST_ASSERT_EQUAL_MEMORY(&s_initial_camera, &s_camera, sizeof(s_camera));
    g_nt_input.pointers[3].x = 600;
    handle_input(0);
    const camera_orientation_t pinched = s_camera;
    g_nt_input.pointers[3].id = 999;
    g_nt_input.pointers[3].x = 900;
    handle_input(0);
    TEST_ASSERT_EQUAL_MEMORY(&pinched, &s_camera, sizeof(s_camera));
    g_nt_input.pointers[5] = (nt_pointer_t){.id = 505, .active = true, .type = NT_POINTER_TOUCH, .x = 100, .y = 100};
    g_nt_input.pointers[5].buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_down = true, .is_pressed = true};
    g_nt_input.pointers[3].x = 700;
    handle_input(0);
    TEST_ASSERT_EQUAL_UINT32(3, s_touch.count);
    TEST_ASSERT_EQUAL_MEMORY(&pinched, &s_camera, sizeof(s_camera));
    g_nt_input.pointers[5].buttons[NT_BUTTON_LEFT] = (nt_button_state_t){0};
    g_nt_input.pointers[3].x = 800;
    handle_input(0);
    TEST_ASSERT_EQUAL_MEMORY(&pinched, &s_camera, sizeof(s_camera));
    memset(g_nt_input.pointers, 0, sizeof(g_nt_input.pointers));
    handle_input(0);
    TEST_ASSERT_EQUAL_UINT32(0, s_touch.count);
    TEST_ASSERT_TRUE(s_touch.span == 0);
    end_touch_test(old);
}

static void test_touch_ui_owned_finger_does_not_join_scene_pinch(void) {
    const touch_test_state_t old = begin_touch_test();
    nt_pointer_t *scene = &g_nt_input.pointers[1];
    nt_pointer_t *ui = &g_nt_input.pointers[7];
    *scene = (nt_pointer_t){.id = 101, .active = true, .type = NT_POINTER_TOUCH, .x = 350, .y = 350};
    scene->buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_down = true, .is_pressed = true};
    handle_input(0);
    scene->buttons[NT_BUTTON_LEFT].is_pressed = false;
    *ui = (nt_pointer_t){.id = 707, .active = true, .type = NT_POINTER_TOUCH, .x = 600, .y = 350};
    ui->buttons[NT_BUTTON_LEFT].is_down = true;
    s_pointer_owner[7][NT_BUTTON_LEFT] = 2;
    handle_input(0);
    ui->x = 900;
    ui->dx = 300;
    handle_input(0);
    TEST_ASSERT_EQUAL_UINT32(1, s_touch.count);
    TEST_ASSERT_EQUAL_UINT8(2, s_pointer_owner[7][NT_BUTTON_LEFT]);
    TEST_ASSERT_EQUAL_MEMORY(&s_initial_camera, &s_camera, sizeof(s_camera));
    ui->buttons[NT_BUTTON_LEFT] = (nt_button_state_t){0};
    handle_input(0);
    ui->buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_down = true, .is_pressed = true};
    handle_input(0);
    TEST_ASSERT_EQUAL_UINT32(2, s_touch.count);
    TEST_ASSERT_EQUAL_MEMORY(&s_initial_camera, &s_camera, sizeof(s_camera));
    ui->buttons[NT_BUTTON_LEFT].is_pressed = false;
    ui->x = 1100;
    handle_input(0);
    TEST_ASSERT_TRUE(glm_vec3_distance(s_camera.eye, s_camera.aim) < glm_vec3_distance((float *)s_initial_camera.eye, (float *)s_initial_camera.aim));
    end_touch_test(old);
}

static void test_touch_pinch_respects_eye_pivot_and_stationary_fingers(void) {
    const touch_test_state_t old = begin_touch_test();
    s_eye_pivot = true;
    for (uint32_t p = 0; p < 2; p++) {
        g_nt_input.pointers[p] = (nt_pointer_t){.id = 100 + p, .active = true, .type = NT_POINTER_TOUCH, .x = 300 + (200 * (float)p), .y = 350};
        g_nt_input.pointers[p].buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_down = true, .is_pressed = true};
    }
    handle_input(0);
    g_nt_input.pointers[0].buttons[NT_BUTTON_LEFT].is_pressed = false;
    g_nt_input.pointers[1].buttons[NT_BUTTON_LEFT].is_pressed = false;
    g_nt_input.pointers[1].x = 700;
    handle_input(0);
    TEST_ASSERT_EQUAL_MEMORY(s_initial_camera.eye, s_camera.eye, sizeof(s_camera.eye));
    TEST_ASSERT_TRUE(fabsf(s_camera.aim[0] - s_initial_camera.aim[0]) > 0.001F);
    const camera_orientation_t pinched = s_camera;
    for (uint32_t frame_index = 0; frame_index < 100; frame_index++) {
        handle_input(0);
    }
    TEST_ASSERT_EQUAL_MEMORY(&pinched, &s_camera, sizeof(s_camera));
    end_touch_test(old);
}

static void test_touch_modal_resize_reset_and_escape_cancel_gesture(void) {
    const touch_test_state_t old = begin_touch_test();
    for (uint32_t cause = 0; cause < 4; cause++) {
        memset(g_nt_input.pointers, 0, sizeof(g_nt_input.pointers));
        reset_camera_input(false);
        for (uint32_t p = 0; p < 2; p++) {
            g_nt_input.pointers[p] = (nt_pointer_t){.id = 100 + p, .active = true, .type = NT_POINTER_TOUCH, .x = 300 + (200 * (float)p), .y = 350};
            g_nt_input.pointers[p].buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_down = true, .is_pressed = true};
        }
        handle_input(0);
        TEST_ASSERT_EQUAL_UINT32(2, s_touch.count);
        g_nt_input.pointers[0].buttons[NT_BUTTON_LEFT].is_pressed = false;
        g_nt_input.pointers[1].buttons[NT_BUTTON_LEFT].is_pressed = false;
        if (cause == 0) {
            s_ui_keyboard_blocked = true;
        } else if (cause == 1) {
            g_nt_window.fb_width = 0;
        } else if (cause == 2) {
            reset_camera_input(false);
        } else {
            s_ui_escape_consumed = true;
        }
        handle_input(0);
        TEST_ASSERT_EQUAL_UINT32(0, s_touch.count);
        TEST_ASSERT_EQUAL_UINT8(2, s_pointer_owner[0][NT_BUTTON_LEFT]);
        TEST_ASSERT_EQUAL_UINT8(2, s_pointer_owner[1][NT_BUTTON_LEFT]);
        s_ui_keyboard_blocked = false;
        s_ui_escape_consumed = false;
        g_nt_window.fb_width = 1000;
        g_nt_input.pointers[1].x = 900;
        handle_input(0);
        TEST_ASSERT_EQUAL_MEMORY(&s_initial_camera, &s_camera, sizeof(s_camera));
    }
    end_touch_test(old);
}

static void test_memory_labels_distinguish_pending_unavailable_and_diagnostics(void) {
    const uint64_t old_memory = s_mem_used;
    const bool old_sampled = s_memory_sampled;
    const uint32_t old_frames = s_example_frames.frames;
    char label[32];
    s_example_frames.frames = 0;
    s_memory_sampled = false;
    s_mem_used = 0;
    format_memory(label, sizeof(label));
    TEST_ASSERT_EQUAL_STRING("pending", label);
    s_memory_sampled = true;
    format_memory(label, sizeof(label));
    TEST_ASSERT_EQUAL_STRING("unavailable", label);
    s_mem_used = 1572864;
    format_memory(label, sizeof(label));
    TEST_ASSERT_EQUAL_STRING("1.5 MiB", label);
    s_example_frames.frames = 1;
    format_memory(label, sizeof(label));
    TEST_ASSERT_EQUAL_STRING("not sampled", label);
    s_memory_sampled = false;
    format_memory(label, sizeof(label));
    TEST_ASSERT_EQUAL_STRING("not sampled", label);
    s_mem_used = old_memory;
    s_memory_sampled = old_sampled;
    s_example_frames.frames = old_frames;
}

/* Content is generated once for the largest level; a level change only reselects instances. */
static void test_content_is_generated_once_and_levels_reselect_instances(void) {
    const uint32_t old_level = s_level;
    nt_gfx_desc_t gfx = nt_gfx_desc_defaults();
    nt_gfx_init(&gfx);
    nt_gfx_fake_reset();
    const nt_program_t noise_program = nt_gfx_fake_make_program(NULL, 0);
    nt_gfx_begin_frame();
    start_content(noise_program);
    TEST_ASSERT_FALSE(s_generated);
    nt_gfx_end_frame();
    collect_pending_noise();
    TEST_ASSERT_TRUE(s_generated);
    TEST_ASSERT_FALSE(s_noise_pending);
    TEST_ASSERT_FALSE(nt_gfx_render_target_valid(s_noise_atlas));
    /* 50 R8 textures plus the transient atlas. */
    TEST_ASSERT_EQUAL_UINT32(AST_MAX_TEXTURES + 1U, nt_gfx_fake_texture_create_count());
    for (uint32_t t = 0; t < AST_MAX_TEXTURES; t++) {
        TEST_ASSERT_TRUE(nt_gfx_texture_ready(s_noise[t]));
    }
    uint32_t vertices = 0;
    uint32_t indices = 0;
    for (uint32_t lod = 0; lod < AST_SUBDIVISIONS; lod++) {
        TEST_ASSERT_EQUAL_UINT32(indices, s_lod_first_index[lod]);
        vertices += AST_MAX_SHAPES * s_lod_vertices[lod];
        indices += AST_MAX_SHAPES * s_lod_indices[lod];
    }
    TEST_ASSERT_EQUAL_UINT32(indices, s_rocks.index_count);
    TEST_ASSERT_EQUAL_UINT32(vertices, s_rocks.vertex_count);
    TEST_ASSERT_TRUE(nt_gfx_vertex_input_valid(s_rocks.input));
    TEST_ASSERT_TRUE(nt_gfx_vertex_input_valid(s_planet_mesh.input));
    TEST_ASSERT_TRUE(nt_gfx_vertex_input_valid(s_sky_mesh.input));
    const nt_buffer_t rocks = s_rocks.vbo;
    select_level(9);
    select_level(2);
    TEST_ASSERT_EQUAL_UINT32(2, s_level);
    TEST_ASSERT_EQUAL_INT(2, s_ui_staged_level);
    TEST_ASSERT_EQUAL_UINT32(rocks.id, s_rocks.vbo.id);
    TEST_ASSERT_EQUAL_UINT32(AST_MAX_TEXTURES + 1U, nt_gfx_fake_texture_create_count());
    drop_generated();
    TEST_ASSERT_FALSE(s_generated);
    TEST_ASSERT_EQUAL_UINT32(0, s_noise[0].id);
    nt_gfx_shutdown();
    nt_gfx_fake_reset();
    s_level = old_level;
}

static void test_scene_options_are_portable_and_bounded(void) {
    char *arguments[] = {"asteroids", "--complexity", "8", "--paused", "1", "--hide-hud", "1"};
    uint32_t value = 0;
    TEST_ASSERT_TRUE(scene_arg_u32(7, arguments, "--complexity", 9, &value));
    TEST_ASSERT_EQUAL_UINT32(8, value);
    TEST_ASSERT_TRUE(scene_arg_u32(7, arguments, "--paused", 1, &value));
    TEST_ASSERT_EQUAL_UINT32(1, value);
    TEST_ASSERT_TRUE(scene_arg_u32(7, arguments, "--hide-hud", 1, &value));
    TEST_ASSERT_EQUAL_UINT32(1, value);
    TEST_ASSERT_TRUE(scene_arg_u32(7, arguments, "-c", 9, &value));
    TEST_ASSERT_EQUAL_UINT32(1, value);
    char *missing[] = {"asteroids", "--complexity"};
    TEST_ASSERT_FALSE(scene_arg_u32(2, missing, "--complexity", 9, &value));
    char *invalid[] = {"asteroids", "--complexity", "10"};
    TEST_ASSERT_FALSE(scene_arg_u32(3, invalid, "--complexity", 9, &value));
    invalid[2] = "-1";
    TEST_ASSERT_FALSE(scene_arg_u32(3, invalid, "--complexity", 9, &value));
    invalid[2] = "429496729600000";
    TEST_ASSERT_FALSE(scene_arg_u32(3, invalid, "--complexity", 9, &value));
    invalid[2] = "8x";
    TEST_ASSERT_FALSE(scene_arg_u32(3, invalid, "--complexity", 9, &value));
    invalid[2] = "";
    TEST_ASSERT_FALSE(scene_arg_u32(3, invalid, "--complexity", 9, &value));
}

static void test_grouped_sort_preserves_exact_tuple_identity_and_stability(void) {
    asteroid_draw_item_t items[] = {{(1ULL << 32U) | 1U, 0}, {(2ULL << 32U) | 0U, 1}, {(1ULL << 32U) | 2U, 2}, {(1ULL << 32U) | 1U, 3}, {0U, 4}, {(49ULL << 32U) | 3999U, 5}};
    asteroid_draw_item_t scratch[6];
    sort_asteroid_items(NULL, 0, NULL);
    sort_asteroid_items(items, 1, NULL);
    sort_asteroid_items(items, 6, scratch);
    const uint32_t expected[] = {4, 0, 3, 2, 1, 5};
    for (uint32_t i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL_UINT32(expected[i], items[i].source_index);
    }
    TEST_ASSERT_EQUAL_UINT64((49ULL << 32U) | 3999U, items[5].sort_key);
}

/* An independent recomputation of one instance: world rows, LOD colors and the sort subset. */
static uint32_t expected_instance(asteroid_instance_t *expected, const asteroid_t *source, const float deep_palette[AST_SUBDIVISIONS][3], const float shallow_palette[AST_SUBDIVISIONS][3]) {
    float world[16];
    asteroid_world_matrix(world, source, s_elapsed);
    const float dx = s_camera.eye[0] - world[12];
    const float dy = s_camera.eye[1] - world[13];
    const float dz = s_camera.eye[2] - world[14];
    const uint32_t lod = asteroid_lod_index(source->scale, sqrtf((dx * dx) + (dy * dy) + (dz * dz)), s_min_screen_size, AST_SUBDIVISIONS);
    memset(expected, 0, sizeof(*expected));
    for (uint32_t row = 0; row < 3; row++) {
        for (uint32_t column = 0; column < 4; column++) {
            expected->world_rows[(row * 4U) + column] = world[(column * 4U) + row];
        }
    }
    memcpy(expected->deep, s_lod_colors ? deep_palette[lod] : source->deep, sizeof(source->deep));
    memcpy(expected->shallow, s_lod_colors ? shallow_palette[lod] : source->shallow, sizeof(source->shallow));
    return (lod * AST_MAX_SHAPES) + source->mesh_index;
}

static void assert_planet_instance(const asteroid_instance_t *actual, double elapsed) {
    const float angle = (float)(-0.1 * elapsed);
    const float rows[12] = {45.0F * cosf(angle), 0, 45.0F * sinf(angle), 0, 0, 45, 0, 0, -45.0F * sinf(angle), 0, 45.0F * cosf(angle), 0};
    for (uint32_t component = 0; component < 12; component++) {
        TEST_ASSERT_TRUE(fabsf(rows[component] - actual->world_rows[component]) < 0.00001F);
    }
    const float zero_colors[8] = {0};
    TEST_ASSERT_EQUAL_MEMORY(zero_colors, actual->deep, sizeof(zero_colors));
}

static void test_grouped_packing_preserves_every_instance_and_lod(void) {
    const uint32_t old_level = s_level;
    const bool old_colors = s_lod_colors;
    const double old_elapsed = s_elapsed;
    const camera_orientation_t old_camera = s_camera;
    float old_deep[AST_SUBDIVISIONS][3];
    float old_shallow[AST_SUBDIVISIONS][3];
    memcpy(old_deep, s_lod_deep, sizeof(old_deep));
    memcpy(old_shallow, s_lod_shallow, sizeof(old_shallow));
    const float deep_palette[AST_SUBDIVISIONS][3] = {{0.11F, 0.12F, 0.13F}, {0.21F, 0.22F, 0.23F}, {0.31F, 0.32F, 0.33F}, {0.41F, 0.42F, 0.43F}};
    const float shallow_palette[AST_SUBDIVISIONS][3] = {{0.51F, 0.52F, 0.53F}, {0.61F, 0.62F, 0.63F}, {0.71F, 0.72F, 0.73F}, {0.81F, 0.82F, 0.83F}};
    memcpy(s_lod_deep, deep_palette, sizeof(s_lod_deep));
    memcpy(s_lod_shallow, shallow_palette, sizeof(s_lod_shallow));
    asteroid_instance_t *expected = calloc(AST_MAX + 1U, sizeof(*expected));
    bool *seen = calloc(AST_MAX, sizeof(*seen));
    uint32_t *expected_subsets = calloc(AST_MAX, sizeof(*expected_subsets));
    TEST_ASSERT_NOT_NULL(expected);
    TEST_ASSERT_NOT_NULL(seen);
    TEST_ASSERT_NOT_NULL(expected_subsets);
    nt_gfx_desc_t gfx = nt_gfx_desc_defaults();
    gfx.frame_capacity[AST_STREAM_INSTANCES] = (AST_MAX + 1U) * (uint32_t)sizeof(*expected);
    nt_gfx_init(&gfx);
    s_camera = s_initial_camera;
    const uint32_t levels[] = {0, 1, 9, 0};
    for (uint32_t pass = 0; pass < 4; pass++) {
        s_level = levels[pass];
        s_elapsed = pass * 3.25;
        s_lod_colors = pass % 2U != 0;
        const complexity_t *complexity = &s_complexities[s_level];
        generate_instances(complexity);
        uint32_t expected_lods[AST_SUBDIVISIONS] = {0};
        uint64_t expected_triangles = 0;
        for (uint32_t source = 0; source < complexity->instances; source++) {
            const uint32_t subset = expected_instance(&expected[source], &s_asteroids[source], deep_palette, shallow_palette);
            expected_subsets[source] = subset;
            expected_lods[subset / AST_MAX_SHAPES]++;
            expected_triangles += s_lod_indices[subset / AST_MAX_SHAPES] / 3U;
        }
        const uint32_t bytes = (complexity->instances + 1U) * (uint32_t)sizeof(*expected);
        nt_gfx_begin_frame();
        const uint32_t base = prepare_instances();
        const asteroid_instance_t *actual = (const asteroid_instance_t *)(g_nt_gfx_frame_storage[AST_STREAM_INSTANCES].staging + base);
        memset(seen, 0, AST_MAX * sizeof(*seen));
        uint64_t previous_key = 0;
        for (uint32_t i = 0; i < complexity->instances; i++) {
            const uint32_t source = s_draw_items[i].source_index;
            TEST_ASSERT_LESS_THAN_UINT32(complexity->instances, source);
            TEST_ASSERT_FALSE(seen[source]);
            seen[source] = true;
            TEST_ASSERT_EQUAL_MEMORY(&expected[source], &actual[i], sizeof(*expected));
            TEST_ASSERT_EQUAL_UINT64(((uint64_t)s_asteroids[source].texture_index << 32U) | expected_subsets[source], s_draw_items[i].sort_key);
            TEST_ASSERT_GREATER_OR_EQUAL_UINT64(previous_key, s_draw_items[i].sort_key);
            previous_key = s_draw_items[i].sort_key;
        }
        assert_planet_instance(&actual[complexity->instances], s_elapsed);
        TEST_ASSERT_EQUAL_MEMORY(expected_lods, s_lod_counts, sizeof(expected_lods));
        TEST_ASSERT_EQUAL_UINT64(expected_triangles, s_triangles);
        TEST_ASSERT_EQUAL_UINT32(bytes, g_nt_gfx_frame_storage[AST_STREAM_INSTANCES].used);
        nt_gfx_end_frame();
    }
    nt_gfx_shutdown();
    free(expected);
    free(seen);
    free(expected_subsets);
    s_level = old_level;
    s_lod_colors = old_colors;
    s_elapsed = old_elapsed;
    s_camera = old_camera;
    memcpy(s_lod_deep, old_deep, sizeof(s_lod_deep));
    memcpy(s_lod_shallow, old_shallow, sizeof(s_lod_shallow));
}

/* Runs: (texture 0, LOD 0 mesh 1) x2, (texture 0, LOD 1 mesh 0) x3,
 * (texture 1, LOD 0 mesh 1) x994, (texture 1, LOD 1 mesh 0) x1, then planet and sky. */
static void test_grouped_submission_draws_exact_runs_textures_offsets_and_scene_tail(void) {
    const uint32_t old_level = s_level;
    nt_pipeline_t old_pipelines[3];
    memcpy(old_pipelines, s_pipelines, sizeof(old_pipelines));
    const nt_hash32_t old_name = s_noise_name;
    s_level = 0;
    nt_gfx_desc_t gfx = nt_gfx_desc_defaults();
    gfx.frame_capacity[AST_STREAM_INSTANCES] = 128U * 1024U;
    gfx.frame_capacity[NT_GFX_FRAME_UNIFORM] = 4096;
    nt_gfx_init(&gfx);
    const char *const samplers[] = {"u_noise"};
    s_noise_name = nt_hash32_str(samplers[0]);
    s_pipelines[0] = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = nt_gfx_fake_make_program(samplers, 1)});
    /* Textureless tail programs isolate asteroid bindings while executing both real tail draws. */
    s_pipelines[1] = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = nt_gfx_fake_make_program(NULL, 0)});
    s_pipelines[2] = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = nt_gfx_fake_make_program(NULL, 0)});
    const nt_program_t noise_program = nt_gfx_fake_make_program(NULL, 0);
    nt_gfx_begin_frame();
    start_content(noise_program);
    nt_gfx_end_frame();
    collect_pending_noise();
    s_level = 0;
    const uint32_t unique = AST_MAX_SHAPES;
    const uint32_t subsets[] = {1, unique, 1, unique};
    const uint32_t ends[] = {2, 5, 999, 1000};
    uint32_t begin = 0;
    for (uint32_t run = 0; run < 4; run++) {
        for (uint32_t i = begin; i < ends[run]; i++) {
            s_draw_items[i] = (asteroid_draw_item_t){.sort_key = ((uint64_t)(run / 2U) << 32U) | subsets[run], .source_index = 999U - i};
        }
        begin = ends[run];
    }
    nt_gfx_begin_frame();
    nt_gfx_end_frame();
    nt_gfx_fake_reset();
    nt_gfx_fake_draw_trace_reset(true);
    nt_gfx_begin_frame();
    uint32_t base = 0;
    (void)nt_gfx_frame_alloc(AST_STREAM_INSTANCES, 96, 4, &base);
    (void)nt_gfx_frame_alloc(AST_STREAM_INSTANCES, 1001U * (uint32_t)sizeof(asteroid_instance_t), 4, &base);
    TEST_ASSERT_EQUAL_UINT32(96, base);
    nt_gfx_begin_pass(&(nt_pass_desc_t){0});
    const nt_frame_uniforms_t globals = {0};
    draw_world(base, &globals);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_FALSE(nt_gfx_fake_draw_trace_overflowed());
    TEST_ASSERT_EQUAL_UINT32(6, nt_gfx_fake_draw_trace_count());
    const uint32_t counts[] = {2, 3, 994, 1, 1, 1};
    const uint32_t indices[] = {60, 240, 60, 240, s_planet_mesh.index_count, 36};
    const uint32_t starts[] = {60, s_lod_first_index[1], 60, s_lod_first_index[1], 0, 0};
    for (uint32_t i = 0; i < 6; i++) {
        const nt_gfx_fake_draw_t draw = nt_gfx_fake_draw_trace_at(i);
        TEST_ASSERT_EQUAL_UINT32(counts[i], draw.instance_count);
        TEST_ASSERT_EQUAL_UINT32(indices[i], draw.num_indices);
        TEST_ASSERT_EQUAL_UINT32(starts[i], draw.first_index);
        TEST_ASSERT_EQUAL_UINT8(NT_INDEX_UINT32, draw.index_type);
        const uint32_t pipeline = i < 4 ? 0 : i - 3U;
        TEST_ASSERT_EQUAL_UINT32(s_pipelines[pipeline].id, draw.pipeline.id);
    }
    TEST_ASSERT_EQUAL_UINT32(6, nt_gfx_draw_calls(&g_nt_gfx.counters));
    TEST_ASSERT_EQUAL_UINT64(1001, g_nt_gfx.counters.instances);
    /* One noise binding per texture transition. */
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_bound_texture_count());
    TEST_ASSERT_NOT_EQUAL_UINT32(nt_gfx_fake_bound_texture_at(0), nt_gfx_fake_bound_texture_at(1));
    TEST_ASSERT_EQUAL_UINT32(base + (1000U * (uint32_t)sizeof(asteroid_instance_t)), nt_gfx_fake_last_instance_offset());
    drop_generated();
    nt_gfx_shutdown();
    nt_gfx_fake_reset();
    s_level = old_level;
    s_noise_name = old_name;
    memcpy(s_pipelines, old_pipelines, sizeof(old_pipelines));
}

int main(void) {
    noise_init();
    sphere_init();
    UNITY_BEGIN();
    RUN_TEST(test_rng_streams_are_deterministic_and_bounded);
    RUN_TEST(test_simplex3_is_continuous_in_every_simplex_ordering);
    RUN_TEST(test_sphere_lods_are_closed_outward_vertex_prefixes);
    RUN_TEST(test_rock_shapes_fill_the_shader_radius_range_and_are_seeded);
    RUN_TEST(test_rock_lods_share_positions_and_have_unit_normals);
    RUN_TEST(test_rock_noise_matches_scalar_fbm);
    RUN_TEST(test_instances_are_seeded_and_within_their_level);
    RUN_TEST(test_world_matrix_spins_in_place_and_orbits_the_planet);
    RUN_TEST(test_instance_attributes_transpose_source_matrix_once);
    RUN_TEST(test_left_handed_camera_preserves_reversed_zero_to_one_depth);
    RUN_TEST(test_original_complexity_counts_are_not_batch_counts);
    RUN_TEST(test_lod_clamps_and_subdivision_count_match_source_policy);
    RUN_TEST(test_pan_matches_source_event_accumulation);
    RUN_TEST(test_routed_pan_preserves_source_math_for_all_pointer_slots);
    RUN_TEST(test_ui_owned_drag_stays_blocked_until_release);
    RUN_TEST(test_reset_cancels_pending_camera_motion);
    RUN_TEST(test_modal_and_resize_cancel_scene_drag_before_input_early_return);
    RUN_TEST(test_keyboard_resets_win_over_already_sampled_drag);
    RUN_TEST(test_touch_orbit_pinch_and_transition_rebase);
    RUN_TEST(test_touch_coincident_fingers_replacement_and_third_finger);
    RUN_TEST(test_touch_ui_owned_finger_does_not_join_scene_pinch);
    RUN_TEST(test_touch_pinch_respects_eye_pivot_and_stationary_fingers);
    RUN_TEST(test_touch_modal_resize_reset_and_escape_cancel_gesture);
    RUN_TEST(test_memory_labels_distinguish_pending_unavailable_and_diagnostics);
    RUN_TEST(test_content_is_generated_once_and_levels_reselect_instances);
    RUN_TEST(test_scene_options_are_portable_and_bounded);
    RUN_TEST(test_grouped_sort_preserves_exact_tuple_identity_and_stability);
    RUN_TEST(test_grouped_packing_preserves_every_instance_and_lod);
    RUN_TEST(test_grouped_submission_draws_exact_runs_textures_offsets_and_scene_tail);
    return UNITY_END();
}
