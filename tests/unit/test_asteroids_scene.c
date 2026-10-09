#define main nt_asteroids_example_main
// NOLINTNEXTLINE(bugprone-suspicious-include): exercise the real C17 port against independent upstream oracles.
#include "../../examples/asteroids/main.c"
#undef main

#include "../../examples/asteroids/reference_scene_data/runtime_math_vectors.h"
#include "input/nt_input_internal.h"
#include "nt_blob_format.h"
#include "nt_crc32.h"
#include "nt_mesh_format.h"
#include "test_helpers/nt_gfx_fake.h"
#include "unity.h"

#include <stdlib.h>

void setUp(void) {}
void tearDown(void) {}

static void test_world_matrices_match_literal_upstream_samples(void) {
    for (uint32_t i = 0; i < AST_REFERENCE_MATH_SAMPLE_COUNT; i++) {
        const ast_reference_math_sample *sample = &ast_reference_math_samples[i];
        const ast_reference_runtime_instance *instance = &ast_reference_math_inputs[sample->input_index].instance;
        float actual[16];
        asteroid_world_matrix(actual, instance, (double)sample->elapsed_seconds);
        for (uint32_t component = 0; component < 16; component++) {
            float delta = fabsf(actual[component] - sample->model_row_major[component]);
            TEST_ASSERT_TRUE_MESSAGE(delta < 0.00025F, "C17 world matrix differs from pinned HLSL++ source oracle");
        }
    }
}

static void test_lod_selection_matches_all_source_subdivisions(void) {
    bool seen[4] = {false};
    for (uint32_t i = 0; i < AST_REFERENCE_MATH_SAMPLE_COUNT; i++) {
        const ast_reference_math_sample *sample = &ast_reference_math_samples[i];
        const ast_reference_runtime_instance *instance = &ast_reference_math_inputs[sample->input_index].instance;
        uint32_t actual = asteroid_lod_index(instance->scale, sample->distance_to_eye, ast_reference_math_min_screen_size, AST_REFERENCE_MATH_SUBDIVISIONS);
        TEST_ASSERT_EQUAL_UINT32(sample->source_subdivision, actual);
        seen[actual] = true;
    }
    for (uint32_t subdivision = 0; subdivision < 4; subdivision++) {
        TEST_ASSERT_TRUE(seen[subdivision]);
    }
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
static void assert_source_pan(bool ui, uint32_t pointer_index) {
    const bool old_ui = s_use_ui;
    s_use_ui = ui;
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
    s_use_ui = old_ui;
}

static void test_pan_matches_source_event_accumulation(void) { assert_source_pan(false, 0); }

static void test_routed_pan_preserves_source_math_for_all_pointer_slots(void) {
    for (uint32_t p = 0; p < NT_INPUT_MAX_POINTERS; p++) {
        assert_source_pan(true, p);
    }
}

static void test_ui_owned_drag_stays_blocked_until_release(void) {
    const bool old_ui = s_use_ui;
    s_use_ui = true;
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
    s_use_ui = old_ui;
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
    const bool old_ui = s_use_ui;
    const bool old_hide_hud = s_hide_hud;
    s_use_ui = true;
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
    s_use_ui = old_ui;
    s_hide_hud = old_hide_hud;
}

static void test_keyboard_resets_win_over_already_sampled_drag(void) {
    const nt_window_t old_window = g_nt_window;
    const camera_orientation_t old_camera = s_camera;
    const camera_orientation_t old_light = s_light;
    const bool old_ui = s_use_ui;
    s_use_ui = true;
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
    s_use_ui = old_ui;
}

typedef struct {
    nt_window_t window;
    camera_orientation_t camera;
    bool use_ui, paused, eye_pivot, ui_built, keyboard_blocked, escape_consumed;
} touch_test_state_t;

static touch_test_state_t begin_touch_test(void) {
    const touch_test_state_t old = {g_nt_window, s_camera, s_use_ui, s_paused, s_eye_pivot, s_ui_built, s_ui_keyboard_blocked, s_ui_escape_consumed};
    nt_input_init();
    g_nt_window.fb_width = 1000;
    g_nt_window.fb_height = 1000;
    s_use_ui = true;
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
    s_use_ui = old.use_ui;
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

static void test_noise_creation_failure_retries_without_publishing_incomplete_set(void) {
    const uint32_t old_level = s_level;
    s_level = 0;
    nt_gfx_desc_t gfx = nt_gfx_desc_defaults();
    nt_gfx_init(&gfx);
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_http_init();
    nt_fs_init();
    nt_resource_init(&(nt_resource_desc_t){0});
    const nt_hash32_t pack = nt_hash32_str("asteroids_test_noise");
    const nt_hash64_t resource = nt_hash64_str("asteroids/noise");
    s_noise_blob = nt_resource_request(resource, NT_ASSET_BLOB);
    const uint32_t pack_header_bytes = NT_PACK_ALIGN_UP((uint32_t)(sizeof(NtPackHeader) + sizeof(NtAssetEntry)), (uint32_t)NT_PACK_DATA_ALIGN);
    const uint32_t texture_bytes = 10U * 3U * 256U * 256U * 4U;
    const uint32_t asset_bytes = (uint32_t)(sizeof(NtBlobAssetHeader) + sizeof(noise_header_t)) + texture_bytes;
    const uint32_t pack_bytes = pack_header_bytes + asset_bytes;
    uint8_t *bytes = calloc(1, pack_bytes);
    TEST_ASSERT_NOT_NULL(bytes);
    NtPackHeader *header = (NtPackHeader *)bytes;
    *header = (NtPackHeader){.magic = NT_PACK_MAGIC, .version = NT_PACK_VERSION, .asset_count = 1, .header_size = pack_header_bytes, .total_size = pack_bytes};
    NtAssetEntry *entry = (NtAssetEntry *)(bytes + sizeof(*header));
    *entry = (NtAssetEntry){.resource_id = resource.value, .offset = pack_header_bytes, .size = asset_bytes, .asset_type = NT_ASSET_BLOB};
    const NtBlobAssetHeader blob_header = {.magic = NT_BLOB_MAGIC, .version = NT_BLOB_VERSION};
    memcpy(bytes + pack_header_bytes, &blob_header, sizeof(blob_header));
    const noise_header_t noise_header = {.magic = 0x4E545341U, .version = 1, .texture_count = 10, .layer_count = 3, .width = 256, .height = 256};
    memcpy(bytes + pack_header_bytes + sizeof(blob_header), &noise_header, sizeof(noise_header));
    header->checksum = nt_crc32(bytes + pack_header_bytes, asset_bytes);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pack, 10));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pack, bytes, pack_bytes));
    nt_resource_step();
    nt_gfx_fake_reset();
    nt_gfx_fake_fail_texture_creates(2);
    TEST_ASSERT_FALSE(resolve_noise());
    TEST_ASSERT_TRUE(nt_gfx_texture_ready(s_noise[0][0]));
    TEST_ASSERT_EQUAL_UINT32(0, s_noise[0][1].id);
    TEST_ASSERT_EQUAL_UINT32(0, s_noise[9][2].id);
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_texture_create_count());
    const uint32_t first_texture = s_noise[0][0].id;
    TEST_ASSERT_TRUE(resolve_noise());
    TEST_ASSERT_EQUAL_UINT32(first_texture, s_noise[0][0].id);
    TEST_ASSERT_EQUAL_UINT32(31, nt_gfx_fake_texture_create_count());
    for (uint32_t texture = 0; texture < 10; texture++) {
        for (uint32_t layer = 0; layer < 3; layer++) {
            TEST_ASSERT_TRUE(nt_gfx_texture_ready(s_noise[texture][layer]));
        }
    }
    TEST_ASSERT_TRUE(resolve_noise());
    TEST_ASSERT_EQUAL_UINT32(31, nt_gfx_fake_texture_create_count());
    drop_noise();
    nt_resource_shutdown();
    nt_fs_shutdown();
    nt_http_shutdown();
    nt_hash_shutdown();
    nt_gfx_shutdown();
    free(bytes);
    s_noise_blob = NT_RESOURCE_INVALID;
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

static void assert_instance_rgb(const asteroid_instance_t *actual, const ast_reference_runtime_instance *source, bool colors, const float deep[3], const float shallow[3]) {
    TEST_ASSERT_EQUAL_MEMORY(colors ? deep : source->deep, actual->deep, 3U * sizeof(float));
    TEST_ASSERT_EQUAL_MEMORY(colors ? shallow : source->shallow, actual->shallow, 3U * sizeof(float));
}

/* World and LOD math are separately checked against pinned upstream vectors. */
static uint32_t expected_source_instance(asteroid_instance_t *expected, const ast_reference_runtime_instance *source, const complexity_t *complexity, const float deep_palette[AST_SUBDIVISIONS][3],
                                         const float shallow_palette[AST_SUBDIVISIONS][3]) {
    float world[16];
    asteroid_world_matrix(world, source, s_elapsed);
    const float dx = s_camera.eye[0] - world[12];
    const float dy = s_camera.eye[1] - world[13];
    const float dz = s_camera.eye[2] - world[14];
    const uint32_t lod = asteroid_lod_index(source->scale, sqrtf((dx * dx) + (dy * dy) + (dz * dz)), s_min_screen_size, AST_SUBDIVISIONS);
    const uint32_t subset = (lod * complexity->unique_meshes) + source->mesh_index;
    for (uint32_t row = 0; row < 3; row++) {
        for (uint32_t column = 0; column < 4; column++) {
            expected->world_rows[(row * 4U) + column] = world[(column * 4U) + row];
        }
    }
    memcpy(expected->deep, s_lod_colors ? deep_palette[lod] : source->deep, sizeof(source->deep));
    memcpy(expected->shallow, s_lod_colors ? shallow_palette[lod] : source->shallow, sizeof(source->shallow));
    expected->deep[3] = s_subsets[subset].depth_min;
    expected->shallow[3] = s_subsets[subset].depth_max;
    return subset;
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

static void test_grouped_packing_preserves_every_source_instance_and_lod(void) {
    const uint32_t old_level = s_level;
    const bool old_colors = s_lod_colors;
    const double old_elapsed = s_elapsed;
    const camera_orientation_t old_camera = s_camera;
    const ast_reference_runtime_instance *old_asteroids = s_asteroids;
    const geometry_subset_t *old_subsets = s_subsets;
    float old_deep[AST_SUBDIVISIONS][3];
    float old_shallow[AST_SUBDIVISIONS][3];
    memcpy(old_deep, s_lod_deep, sizeof(old_deep));
    memcpy(old_shallow, s_lod_shallow, sizeof(old_shallow));
    const float deep_palette[AST_SUBDIVISIONS][3] = {{0.11F, 0.12F, 0.13F}, {0.21F, 0.22F, 0.23F}, {0.31F, 0.32F, 0.33F}, {0.41F, 0.42F, 0.43F}};
    const float shallow_palette[AST_SUBDIVISIONS][3] = {{0.51F, 0.52F, 0.53F}, {0.61F, 0.62F, 0.63F}, {0.71F, 0.72F, 0.73F}, {0.81F, 0.82F, 0.83F}};
    memcpy(s_lod_deep, deep_palette, sizeof(s_lod_deep));
    memcpy(s_lod_shallow, shallow_palette, sizeof(s_lod_shallow));
    ast_reference_runtime_instance *records = calloc(AST_MAX, sizeof(*records));
    geometry_subset_t *subsets = calloc(4000, sizeof(*subsets));
    asteroid_instance_t *expected = calloc(AST_MAX + 1U, sizeof(*expected));
    bool *seen = calloc(AST_MAX, sizeof(*seen));
    uint32_t *expected_subsets = calloc(AST_MAX, sizeof(*expected_subsets));
    TEST_ASSERT_NOT_NULL(records);
    TEST_ASSERT_NOT_NULL(subsets);
    TEST_ASSERT_NOT_NULL(expected);
    TEST_ASSERT_NOT_NULL(seen);
    TEST_ASSERT_NOT_NULL(expected_subsets);
    nt_gfx_desc_t gfx = nt_gfx_desc_defaults();
    gfx.frame_capacity[NT_GFX_FRAME_VERTEX] = (AST_MAX + 1U) * (uint32_t)sizeof(*expected);
    nt_gfx_init(&gfx);
    s_camera = s_initial_camera;
    s_asteroids = records;
    s_subsets = subsets;
    const uint32_t levels[] = {0, 1, 9, 0};
    for (uint32_t pass = 0; pass < 4; pass++) {
        s_level = levels[pass];
        s_elapsed = pass * 3.25;
        s_lod_colors = pass % 2U != 0;
        const complexity_t *complexity = &s_complexities[s_level];
        for (uint32_t i = 0; i < complexity->instances; i++) {
            records[i] = ast_reference_math_inputs[i % AST_REFERENCE_MATH_INPUT_COUNT].instance;
            records[i].mesh_index = i % complexity->unique_meshes;
            records[i].texture_index = (i / 3U) % complexity->textures;
            records[i].deep[0] = (float)i / (float)AST_MAX;
            records[i].shallow[2] = 1.0F - ((float)i / (float)AST_MAX);
        }
        for (uint32_t i = 0; i < complexity->unique_meshes * AST_SUBDIVISIONS; i++) {
            subsets[i] = (geometry_subset_t){.index_count = 60U << (2U * (i / complexity->unique_meshes)), .depth_min = 0.5F + ((float)i * 0.0001F), .depth_max = 2.0F};
        }
        uint32_t expected_lods[AST_SUBDIVISIONS] = {0};
        uint64_t expected_triangles = 0;
        for (uint32_t source = 0; source < complexity->instances; source++) {
            const uint32_t subset = expected_source_instance(&expected[source], &records[source], complexity, deep_palette, shallow_palette);
            expected_subsets[source] = subset;
            expected_lods[subset / complexity->unique_meshes]++;
            expected_triangles += subsets[subset].index_count / 3U;
        }
        const uint32_t bytes = (complexity->instances + 1U) * (uint32_t)sizeof(*expected);
        nt_gfx_begin_frame();
        const uint32_t base = prepare_instances();
        const asteroid_instance_t *actual = (const asteroid_instance_t *)(g_nt_gfx_frame_storage[NT_GFX_FRAME_VERTEX].staging + base);
        memset(seen, 0, AST_MAX * sizeof(*seen));
        uint64_t previous_key = 0;
        for (uint32_t i = 0; i < complexity->instances; i++) {
            const uint32_t source = s_draw_items[i].source_index;
            TEST_ASSERT_LESS_THAN_UINT32(complexity->instances, source);
            TEST_ASSERT_FALSE(seen[source]);
            seen[source] = true;
            TEST_ASSERT_EQUAL_MEMORY(&expected[source], &actual[i], sizeof(*expected));
            const uint32_t lod = expected_subsets[source] / complexity->unique_meshes;
            TEST_ASSERT_LESS_THAN_UINT32(AST_SUBDIVISIONS, lod);
            assert_instance_rgb(&actual[i], &records[source], s_lod_colors, deep_palette[lod], shallow_palette[lod]);
            TEST_ASSERT_EQUAL_UINT64(((uint64_t)records[source].texture_index << 32U) | expected_subsets[source], s_draw_items[i].sort_key);
            TEST_ASSERT_GREATER_OR_EQUAL_UINT64(previous_key, s_draw_items[i].sort_key);
            previous_key = s_draw_items[i].sort_key;
        }
        assert_planet_instance(&actual[complexity->instances], s_elapsed);
        TEST_ASSERT_EQUAL_MEMORY(expected_lods, s_lod_counts, sizeof(expected_lods));
        TEST_ASSERT_EQUAL_UINT64(expected_triangles, s_triangles);
        TEST_ASSERT_EQUAL_UINT32(bytes, g_nt_gfx_frame_storage[NT_GFX_FRAME_VERTEX].used);
        nt_gfx_end_frame();
    }
    nt_gfx_shutdown();
    free(records);
    free(subsets);
    free(expected);
    free(seen);
    free(expected_subsets);
    s_level = old_level;
    s_lod_colors = old_colors;
    s_elapsed = old_elapsed;
    s_camera = old_camera;
    s_asteroids = old_asteroids;
    s_subsets = old_subsets;
    memcpy(s_lod_deep, old_deep, sizeof(s_lod_deep));
    memcpy(s_lod_shallow, old_shallow, sizeof(s_lod_shallow));
}

static mesh_binding_t make_submission_mesh(bool instanced) {
    uint8_t bytes[sizeof(NtMeshAssetHeader) + sizeof(NtStreamDesc) + 36 + 18] = {0};
    const NtMeshAssetHeader header = {
        .magic = NT_MESH_MAGIC, .version = NT_MESH_VERSION, .stream_count = 1, .index_type = NT_INDEX_UINT16, .vertex_count = 3, .index_count = 9, .vertex_data_size = 36, .index_data_size = 18};
    const NtStreamDesc stream = {.name_hash = 1, .type = NT_STREAM_FLOAT32, .count = 3};
    memcpy(bytes, &header, sizeof(header));
    memcpy(bytes + sizeof(header), &stream, sizeof(stream));
    mesh_binding_t binding = {.mesh = {.id = nt_gfx_activate_mesh(bytes, sizeof(bytes))}};
    TEST_ASSERT_NOT_EQUAL_UINT32(0, binding.mesh.id);
    const nt_gfx_mesh_info_t *info = nt_gfx_get_mesh_info(binding.mesh);
    nt_vertex_input_desc_t desc = {.layout = {.attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 3}}, .attr_count = 1, .stride = 12}, .vertex_buffer = info->vbo, .index_buffer = info->ibo};
    if (instanced) {
        desc.instance_layout = (nt_vertex_layout_t){.attrs = {{.location = 4, .type = NT_VERTEX_FLOAT, .count = 4}}, .attr_count = 1, .stride = sizeof(asteroid_instance_t)};
    }
    binding.input = nt_gfx_make_vertex_input(&desc);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, binding.input.id);
    return binding;
}

#if NT_GFX_CAPTURE_ENABLED
static void assert_submission_capture(uint32_t base) {
    const uint32_t expected[] = {0, 2, 5, 999, 1000};
    const nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    TEST_ASSERT_FALSE(capture.overflow);
    uint32_t binds = 0;
    uint32_t textures = 0;
    for (uint32_t i = 0; i < capture.count; i++) {
        const nt_gfx_event_t *event = &capture.events[i];
        if (event->kind == NT_GFX_EVENT_BEGIN && event->operation == NT_GFX_OP_INSTANCE_BUFFER) {
            TEST_ASSERT_LESS_THAN_UINT32(5, binds);
            TEST_ASSERT_EQUAL_UINT32(base + (expected[binds] * (uint32_t)sizeof(asteroid_instance_t)), event->data.binding.offset);
            const nt_vertex_input_t input = binds == 4 ? s_planet_mesh.input : s_rock_meshes[binds % 2U].input;
            TEST_ASSERT_EQUAL_UINT32(input.id, event->data.binding.secondary);
            binds++;
        }
        if (event->kind == NT_GFX_EVENT_ARGUMENT && event->operation == NT_GFX_OP_TEXTURE_SET) {
            if (textures < 6) {
                TEST_ASSERT_EQUAL_UINT32(s_noise[textures / 3U][textures % 3U].id, event->object);
                TEST_ASSERT_EQUAL_UINT32(s_noise_names[textures % 3U].value, event->data.binding.name);
            }
            textures++;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(5, binds);
    TEST_ASSERT_EQUAL_UINT32(13, textures);
}
#endif

static void test_grouped_submission_draws_exact_runs_textures_offsets_and_scene_tail(void) {
    const uint32_t old_level = s_level;
    const geometry_subset_t *old_subsets = s_subsets;
    const mesh_binding_t old_rock[2] = {s_rock_meshes[0], s_rock_meshes[1]};
    const mesh_binding_t old_planet = s_planet_mesh;
    const mesh_binding_t old_sky = s_sky_mesh;
    nt_pipeline_t old_pipelines[3];
    nt_texture_t old_noise[2][3];
    nt_hash32_t old_names[3];
    memcpy(old_pipelines, s_pipelines, sizeof(old_pipelines));
    memcpy(old_noise, s_noise, sizeof(old_noise));
    memcpy(old_names, s_noise_names, sizeof(old_names));
    const geometry_subset_t subsets[2] = {{.chunk_index = 0, .first_index = 0, .index_count = 3, .vertex_count = 3}, {.chunk_index = 1, .first_index = 3, .index_count = 6, .vertex_count = 3}};
    s_level = 0;
    s_subsets = subsets;
    nt_gfx_desc_t gfx = nt_gfx_desc_defaults();
    gfx.frame_capacity[NT_GFX_FRAME_VERTEX] = 128U * 1024U;
    gfx.frame_capacity[NT_GFX_FRAME_UNIFORM] = 4096;
    gfx.capture_capacity = 1024;
    nt_gfx_init(&gfx);
    const char *const samplers[] = {"u_noise0", "u_noise1", "u_noise2"};
    const nt_program_t rocks = nt_gfx_fake_make_program(samplers, 3);
    s_pipelines[0] = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = rocks});
    /* Textureless tail programs isolate asteroid bindings while executing both real tail draws. */
    s_pipelines[1] = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = nt_gfx_fake_make_program(NULL, 0)});
    s_pipelines[2] = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = nt_gfx_fake_make_program(NULL, 0)});
    s_rock_meshes[0] = make_submission_mesh(true);
    s_rock_meshes[1] = make_submission_mesh(true);
    s_planet_mesh = make_submission_mesh(true);
    s_sky_mesh = make_submission_mesh(false);
    const uint8_t pixel[4] = {255, 255, 255, 255};
    for (uint32_t texture = 0; texture < 2; texture++) {
        for (uint32_t layer = 0; layer < 3; layer++) {
            s_noise_names[layer] = nt_hash32_str(samplers[layer]);
            s_noise[texture][layer] = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 1, .height = 1, .format = NT_TEXTURE_FORMAT_RGBA8, .data = pixel});
            TEST_ASSERT_TRUE(nt_gfx_texture_ready(s_noise[texture][layer]));
        }
    }
    const uint32_t ends[] = {2, 5, 999, 1000};
    uint32_t begin = 0;
    for (uint32_t run = 0; run < 4; run++) {
        for (uint32_t i = begin; i < ends[run]; i++) {
            s_draw_items[i] = (asteroid_draw_item_t){.sort_key = ((uint64_t)(run / 2U) << 32U) | (run % 2U), .source_index = 999U - i};
        }
        begin = ends[run];
    }
    nt_gfx_begin_frame();
    nt_gfx_end_frame();
    nt_gfx_fake_reset();
    nt_gfx_fake_draw_trace_reset(true);
#if NT_GFX_CAPTURE_ENABLED
    nt_gfx_capture_request();
#endif
    nt_gfx_begin_frame();
    uint32_t base = 0;
    (void)nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, 96, 4, &base);
    (void)nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, 1001U * (uint32_t)sizeof(asteroid_instance_t), 4, &base);
    TEST_ASSERT_EQUAL_UINT32(96, base);
    nt_gfx_begin_pass(&(nt_pass_desc_t){0});
    const nt_frame_uniforms_t globals = {0};
    draw_world(base, &globals);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_FALSE(nt_gfx_fake_draw_trace_overflowed());
    TEST_ASSERT_EQUAL_UINT32(6, nt_gfx_fake_draw_trace_count());
    const uint32_t counts[] = {2, 3, 994, 1, 1, 1};
    const uint32_t indices[] = {3, 6, 3, 6, 9, 9};
    const uint32_t starts[] = {0, 3, 0, 3, 0, 0};
    for (uint32_t i = 0; i < 6; i++) {
        const nt_gfx_fake_draw_t draw = nt_gfx_fake_draw_trace_at(i);
        TEST_ASSERT_EQUAL_UINT32(counts[i], draw.instance_count);
        TEST_ASSERT_EQUAL_UINT32(indices[i], draw.num_indices);
        TEST_ASSERT_EQUAL_UINT32(starts[i], draw.first_index);
        TEST_ASSERT_EQUAL_UINT8(NT_INDEX_UINT16, draw.index_type);
        const uint32_t pipeline = i < 4 ? 0 : i - 3U;
        TEST_ASSERT_EQUAL_UINT32(s_pipelines[pipeline].id, draw.pipeline.id);
    }
    TEST_ASSERT_EQUAL_UINT32(6, nt_gfx_draw_calls(&g_nt_gfx.counters));
    TEST_ASSERT_EQUAL_UINT64(1001, g_nt_gfx.counters.instances);
    TEST_ASSERT_EQUAL_UINT32(6, nt_gfx_fake_bound_texture_count());
    for (uint32_t i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL_UINT32(i % 3U, nt_gfx_fake_bound_texture_slot_at(i));
    }
    TEST_ASSERT_NOT_EQUAL_UINT32(nt_gfx_fake_bound_texture_at(0), nt_gfx_fake_bound_texture_at(3));
    TEST_ASSERT_EQUAL_UINT32(base + (1000U * (uint32_t)sizeof(asteroid_instance_t)), nt_gfx_fake_last_instance_offset());
#if NT_GFX_CAPTURE_ENABLED
    assert_submission_capture(base);
#endif
    nt_gfx_shutdown();
    nt_gfx_fake_reset();
    s_level = old_level;
    s_subsets = old_subsets;
    s_rock_meshes[0] = old_rock[0];
    s_rock_meshes[1] = old_rock[1];
    s_planet_mesh = old_planet;
    s_sky_mesh = old_sky;
    memcpy(s_pipelines, old_pipelines, sizeof(old_pipelines));
    memcpy(s_noise, old_noise, sizeof(old_noise));
    memcpy(s_noise_names, old_names, sizeof(old_names));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_world_matrices_match_literal_upstream_samples);
    RUN_TEST(test_lod_selection_matches_all_source_subdivisions);
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
    RUN_TEST(test_noise_creation_failure_retries_without_publishing_incomplete_set);
    RUN_TEST(test_scene_options_are_portable_and_bounded);
    RUN_TEST(test_grouped_sort_preserves_exact_tuple_identity_and_stability);
    RUN_TEST(test_grouped_packing_preserves_every_source_instance_and_lod);
    RUN_TEST(test_grouped_submission_draws_exact_runs_textures_offsets_and_scene_tail);
    return UNITY_END();
}
