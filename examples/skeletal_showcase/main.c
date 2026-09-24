/* Skeletal poses and textured meshes through caller-owned tracks and palettes. */

// #region includes
#include "app/nt_app.h"
#include "atlas/nt_atlas.h"
#include "core/nt_assert.h"
#include "core/nt_core.h"
#include "core/nt_platform.h"
#ifdef NT_DEVAPI_ENABLED
#include "devapi/nt_devapi.h"
#include "devapi/nt_devapi_groups.h"
#ifdef NT_DEVAPI_GROUP_CAPTURE
#include "devapi/nt_devapi_capture.h"
#endif
#ifdef NT_PLATFORM_WEB
#include "devapi/nt_devapi_web.h"
#else
#include "devapi/nt_devapi_net.h"
#endif
#else
#include "devapi/nt_devapi_stub.h"
#endif
#include "drawable_comp/nt_drawable_comp.h"
#include "entity/nt_entity.h"
#include "font/nt_font.h"
#ifndef NT_PLATFORM_WEB
#include "fs/nt_fs.h"
#endif
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "http/nt_http.h"
#include "input/nt_input.h"
#include "log/nt_log.h"
#include "material/nt_material.h"
#include "material/nt_program_ref.h"
#include "material_comp/nt_material_comp.h"
#include "math/nt_math.h"
#include "memory/nt_mem_scratch.h"
#include "mesh_comp/nt_mesh_comp.h"
#include "meshwire/nt_meshwire.h"
#include "nt_mesh_format.h"
#include "nt_pack_format.h"
#include "render/nt_render_defs.h"
#include "renderers/nt_mesh_renderer.h"
#include "renderers/nt_shape_renderer.h"
#include "renderers/nt_skinned_mesh_renderer.h"
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "skeletal/nt_skeletal.h"
#include "skeletal_assets/nt_skeletal_assets.h"
#include "skeletal_gpu/nt_skeletal_gpu.h"
#include "skin_comp/nt_skin_comp.h"
#include "transform_comp/nt_transform_comp.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_button.h"
#include "ui/nt_ui_checkbox.h"
#include "ui/nt_ui_dropdown.h"
#include "ui/nt_ui_inspector.h"
#include "ui/nt_ui_label.h"
#include "ui/nt_ui_modal.h"
#include "ui/nt_ui_scale.h"
#include "ui/nt_ui_scroll.h"
#include "ui/nt_ui_slider.h"
#include "ui/nt_ui_tabbar.h"
#include "window/nt_window.h"

#include "clay.h"
#include "skeletal_showcase_assets.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "showcase_limits.h"

#ifdef NT_PLATFORM_WEB
#include "platform/web/nt_platform_web.h"
#endif
// #endregion

// #region constants and state
#define HUMANOID_JOINT_COUNT 21U
#define UI_ARENA_SIZE ((size_t)2 * 1024 * 1024)
#define SCRATCH_ARENA_SIZE ((size_t)128 * 1024)
#define STAGE_ID "skeletal_showcase/stage"
/* Camera numbers are authored for the humanoid; s_fit_scale rescales them per rig. */
#define CAMERA_MIN 3.5F
#define CAMERA_MAX 18.0F
#define CAMERA_NEAR 0.05F
#define CAMERA_FAR 50.0F
#define CAMERA_PITCH_LIMIT 1.25F

typedef enum {
    RIG_HUMANOID = 0,
    RIG_FOX,
    RIG_CESIUMMAN,
    RIG_COUNT,
} rig_source_t;

static const char *const s_rig_names[RIG_COUNT] = {"Humanoid", "Fox", "CesiumMan"};
/* The rig a clip plays on comes from its view's rig_compat_id, not from the label. */
static const struct {
    const char *name;
    nt_hash64_t id;
} s_clips[] = {
    {"Fox Survey", ASSET_CLIP_SKELETAL_SHOWCASE_FOX_SURVEY_NANM},
    {"Fox Walk", ASSET_CLIP_SKELETAL_SHOWCASE_FOX_WALK_NANM},
    {"Fox Run", ASSET_CLIP_SKELETAL_SHOWCASE_FOX_RUN_NANM},
    {"CesiumMan", ASSET_CLIP_SKELETAL_SHOWCASE_CESIUMMAN_NANM},
    {"Humanoid motion", {0}}, /* id 0: the code-authored s_humanoid_clip */
};
#define CLIP_COUNT ((int)(sizeof s_clips / sizeof s_clips[0]))

static const char *const s_joint_names[HUMANOID_JOINT_COUNT] = {
    "pelvis",        "spine",      "chest",      "neck",      "head",      "left_clavicle", "left_upper_arm", "left_forearm", "left_hand",  "right_clavicle", "right_upper_arm",
    "right_forearm", "right_hand", "left_thigh", "left_shin", "left_foot", "left_toe",      "right_thigh",    "right_shin",   "right_foot", "right_toe",
};

static const uint16_t s_parent[HUMANOID_JOINT_COUNT] = {
    NT_SKELETAL_NO_PARENT, 0, 1, 2, 3, 2, 5, 6, 7, 2, 9, 10, 11, 0, 13, 14, 15, 0, 17, 18, 19,
};

static const uint16_t s_subtree_end[HUMANOID_JOINT_COUNT] = {
    HUMANOID_JOINT_COUNT, 13, 13, 5, 5, 9, 9, 9, 9, 13, 13, 13, 13, 17, 17, 17, 17, 21, 21, 21, 21,
};

/* Identity rest quaternions keep this rest pose easy to inspect. Offsets use
 * q_local = q_offset * q_rest, with XYZ Euler input composed Z*Y*X. */
static const nt_skeletal_trs_t s_rest[HUMANOID_JOINT_COUNT] = {
    {{0.0F, 2.20F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}},   {{0.0F, 0.45F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}},
    {{0.0F, 0.45F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}},   {{0.0F, 0.35F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}},
    {{0.0F, 0.30F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}},   {{-0.22F, 0.18F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}},
    {{-0.70F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}},  {{-0.72F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}},
    {{-0.55F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}},  {{0.22F, 0.18F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}},
    {{0.70F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}},   {{0.72F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}},
    {{0.55F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}},   {{-0.32F, -0.75F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}},
    {{0.0F, -0.90F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}},  {{0.0F, -0.40F, 0.18F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}},
    {{0.0F, -0.15F, 0.30F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}}, {{0.32F, -0.75F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}},
    {{0.0F, -0.90F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}},  {{0.0F, -0.40F, 0.18F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}},
    {{0.0F, -0.15F, 0.30F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}},
};

typedef struct {
    const nt_skeletal_skeleton_t *view; /* active rig; NULL while an imported skeleton is not ready */
    rig_source_t rig_source;
    nt_skeletal_trs_t local[SKELETAL_SHOWCASE_MAX_JOINTS];
    nt_skeletal_mat34_t model[SKELETAL_SHOWCASE_MAX_JOINTS];
    float angles[SKELETAL_SHOWCASE_MAX_JOINTS][3];
    int selected_joint;
    bool show_axes;
    bool combo_open;
    bool rig_combo_open;
} skeletal_pose_scene_state_t;

typedef struct {
    rig_source_t rig;
    const nt_skeletal_skeleton_t *skel; /* NULL while the imported skeleton is not ready */
    int clip;                           /* index into s_clip_resource, -1 = none */
    const nt_skeletal_clip_t *clip_view;
    nt_skeletal_track_t track; /* OCCUPIED while a clip is selected */
    float speed_mag;
    bool reverse;
    bool paused;
    bool loop;
    nt_skeletal_trs_t local[SKELETAL_SHOWCASE_MAX_JOINTS];
    nt_skeletal_mat34_t model[SKELETAL_SHOWCASE_MAX_JOINTS];
} character_player_t;

typedef struct {
    character_player_t player;
    bool rig_combo_open;
    bool clip_combo_open;
} skinned_scene_state_t;

static skeletal_pose_scene_state_t s_skeleton_scene;
static skinned_scene_state_t s_skinned_scene;
static bool s_cpu_reference;
static bool s_show_bones;
static nt_entity_t s_mesh_entities[2];
static nt_entity_t s_order_entities[SKELETAL_SHOWCASE_MAX_INSTANCES];
static nt_skeletal_track_t s_order_tracks[SKELETAL_SHOWCASE_MAX_INSTANCES];
static int s_order_count = 17;
static int s_order_mode;
static bool s_order_shared;
static bool s_order_two_passes;
static bool s_order_paused;
static bool s_order_combo_open;
static const char *const s_order_names[] = {"Grouped", "Alternating meshes", "Alternating materials"};
static struct {
    uint32_t draws[2], expected[2], instances[2];
    uint32_t passes, palettes;
} s_order_stats;
static nt_resource_t s_mesh_resource[RIG_COUNT];
static nt_resource_t s_skin_resource[RIG_COUNT];
static nt_resource_t s_texture_resource[RIG_COUNT + 1];
static nt_material_t s_skin_material[RIG_COUNT + 1];
static nt_material_t s_static_material[RIG_COUNT + 1];
static nt_program_ref_t s_skin_program;
static nt_program_ref_t s_static_program;

typedef struct {
    uint8_t *data;   /* owned decoded MESH, independent of pack and GPU lifetime */
    uint8_t *output; /* last CPU-deformed RAW MESH, retained through context loss */
    uint32_t size;
    uint32_t stride;
    uint32_t position;
    uint32_t joints;
    uint32_t weights;
    nt_mesh_t reference;
    nt_skeletal_mat34_t pose[SKELETAL_SHOWCASE_MAX_JOINTS]; /* model pose of the last deformation */
} cpu_mesh_t;
#define CPU_CLOTHES RIG_COUNT
#define CPU_ORDER_CLOTHES (RIG_COUNT + 1)
#define CPU_SOURCE_COUNT (RIG_COUNT + 2)
static cpu_mesh_t s_cpu_mesh[CPU_SOURCE_COUNT];
static nt_mesh_t s_procedural_mesh[3];
static nt_skin_binding_t s_humanoid_skin[2];
static uint16_t s_humanoid_remap[HUMANOID_JOINT_COUNT];
static nt_skeletal_mat34_t s_humanoid_inverse[2][HUMANOID_JOINT_COUNT];
static nt_skeletal_clip_t s_humanoid_clip;
static const uint16_t s_motion_joints[6] = {2, 4, 6, 10, 13, 17};
static float s_motion_blocks[49][6][4];
static bool s_independent_clothes;
static nt_skeletal_mat34_t s_clothes_model[SKELETAL_SHOWCASE_MAX_JOINTS];
static nt_skeletal_skeleton_t s_humanoid; /* the code-defined rig over the static arrays above */
static uint32_t s_humanoid_joint_ids[HUMANOID_JOINT_COUNT];
static nt_resource_t s_rig_resource[RIG_COUNT]; /* RIG_HUMANOID stays NT_RESOURCE_INVALID */
static nt_resource_t s_clip_resource[CLIP_COUNT];
static float s_humanoid_extent;
static bool s_shell_stage_drag;
static bool s_shell_stage_pan;
static nt_ui_bbox_t s_stage_bbox;
static float s_camera_yaw;
static float s_camera_pitch;
static float s_camera_distance;
static float s_camera_target[3];
static float s_fit_center[3];
static float s_fit_scale = 1.0F; /* rig extent / humanoid extent: scales the camera and the stage primitives */
static bool s_fit_pending;       /* the active scene refits the camera to its rig on its next update */
static bool s_show_controls = true;
static bool s_scene_combo_open;
static int s_active_scene = -1;
static bool s_skip_scene_interaction_this_frame;

static nt_ui_context_t *s_ui;
NT_UI_DECLARE_ARENA(s_ui_arena, UI_ARENA_SIZE);
static nt_buffer_t s_frame_ubo;
static nt_frame_uniforms_t s_frame_uniforms; /* filled each frame; stage parts set its camera */
static nt_resource_t s_atlas;
static nt_resource_t s_atlas_texture;
static nt_resource_t s_font_resource;
static nt_font_t s_font;
static nt_material_t s_sprite_material;
static nt_material_t s_text_material;
static nt_program_ref_t s_sprite_program;
static nt_program_ref_t s_text_program;
static bool s_atlas_bound;
static bool s_font_bound;
static uint32_t s_white_region;
static nt_ui_button_style_t s_button_style;
static nt_ui_slider_style_t s_slider_style;
static nt_ui_checkbox_style_t s_checkbox_style;
static nt_ui_scroll_style_t s_joint_scroll_style;
static nt_ui_dropdown_style_t s_joint_combo_style;
static nt_ui_dropdown_style_t s_scene_combo_style;
static nt_ui_scale_t s_ui_scale;
static float s_camera_fit_width;
static float s_camera_fit_height;

typedef struct {
    const char *title;
    const char *description;
    const char *source;
    void (*reset)(void);
    void (*update)(void);
    void (*cancel_input)(void);
    void (*declare_controls)(void);
    void (*draw)(void);
} skeletal_scene_desc_t;

static void cancel_scene_input(void);
static void cancel_active_scene_input(void);
static void skeleton_cancel_input(void);
static void skeleton_draw(void);
static void reset_scene(void);
static void skeleton_update(void);
static void declare_properties(void);

static void skinned_reset(void);
static void skinned_update(void);
static void skinned_cancel_input(void);
static void skinned_declare_controls(void);
static void skinned_draw(void);
static void ordering_reset(void);
static void ordering_update(void);
static void ordering_cancel_input(void);
static void ordering_declare_controls(void);
static void ordering_draw(void);

static const skeletal_scene_desc_t s_scene_registry[] = {
    {
        .title = "Skeleton & Pose",
        .description = "Pose a code-defined humanoid or an imported Khronos rig with forward kinematics.",
        .source = "Source: examples/skeletal_showcase/main.c",
        .reset = reset_scene,
        .update = skeleton_update,
        .cancel_input = skeleton_cancel_input,
        .declare_controls = declare_properties,
        .draw = skeleton_draw,
    },
    {
        .title = "Skinned Meshes",
        .description = "Play textured meshes or compare GPU and CPU deformation at the same pose.",
        .source = "Source: examples/skeletal_showcase/main.c",
        .reset = skinned_reset,
        .update = skinned_update,
        .cancel_input = skinned_cancel_input,
        .declare_controls = skinned_declare_controls,
        .draw = skinned_draw,
    },
    {
        .title = "Order & Instancing",
        .description = "Adjacent items batch together; palettes upload once before both views.",
        .source = "Source: examples/skeletal_showcase/main.c",
        .reset = ordering_reset,
        .update = ordering_update,
        .cancel_input = ordering_cancel_input,
        .declare_controls = ordering_declare_controls,
        .draw = ordering_draw,
    },
};
#define SKELETAL_SCENE_COUNT ((int)(sizeof s_scene_registry / sizeof s_scene_registry[0]))
static void switch_scene(int next_scene);
static void reset_active_scene(void);
// #endregion

// #region pose and camera
static void make_offset_quat(const float angles[3], float out[4]) {
    versor qx;
    versor qy;
    versor qz;
    versor tmp;
    vec3 axis_x = {1.0F, 0.0F, 0.0F};
    vec3 axis_y = {0.0F, 1.0F, 0.0F};
    vec3 axis_z = {0.0F, 0.0F, 1.0F};
    glm_quatv(qx, angles[0], axis_x);
    glm_quatv(qy, angles[1], axis_y);
    glm_quatv(qz, angles[2], axis_z);
    glm_quat_mul(qz, qy, tmp);
    glm_quat_mul(tmp, qx, out);
}

static void reset_camera(void) {
    s_camera_yaw = 0.0F;
    s_camera_pitch = 0.10F;
    s_camera_distance = 6.5F * s_fit_scale;
    memcpy(s_camera_target, s_fit_center, sizeof s_camera_target);
    s_camera_fit_width = 0.0F;
    s_camera_fit_height = 0.0F;
}

/* The scene reports what the stage shows; the shell keeps owning the camera. */
static void set_camera_fit(const float center[3], float scale) {
    memcpy(s_fit_center, center, sizeof s_fit_center);
    s_fit_scale = scale;
    reset_camera();
}

/* Borrowed view of a rig: the pointer may change on reload, so views are
 * refetched once per frame after resource_step. NULL means the imported skeleton is not ready. */
static const nt_skeletal_skeleton_t *rig_view(rig_source_t rig) {
    const nt_skeletal_skeleton_t *view = NULL;
    if (rig == RIG_HUMANOID) {
        view = &s_humanoid;
    } else if (nt_resource_is_ready(s_rig_resource[rig])) {
        view = nt_skeletal_assets_skeleton(s_rig_resource[rig]);
    }
    if (view != NULL) {
        NT_ASSERT(view->joint_count <= SKELETAL_SHOWCASE_MAX_JOINTS && "skeletal_showcase: rig exceeds SKELETAL_SHOWCASE_MAX_JOINTS");
    }
    return view;
}

static void apply_pose(void) {
    const nt_skeletal_skeleton_t *skel = s_skeleton_scene.view;
    for (uint32_t j = 0; j < skel->joint_count; ++j) {
        s_skeleton_scene.local[j] = skel->rest[j];
        versor offset;
        make_offset_quat(s_skeleton_scene.angles[j], offset);
        versor rest_q;
        memcpy(rest_q, skel->rest[j].q, sizeof rest_q);
        glm_quat_mul(offset, rest_q, s_skeleton_scene.local[j].q);
        glm_quat_normalize(s_skeleton_scene.local[j].q);
    }
    nt_skeletal_fk(skel, s_skeleton_scene.local, s_skeleton_scene.model, 0, skel->joint_count);
}

/* Centroid and extent (max joint distance from the centroid) of model[]. */
static float rig_extent(const nt_skeletal_mat34_t *model, uint16_t joint_count, float center[3]) {
    center[0] = 0.0F;
    center[1] = 0.0F;
    center[2] = 0.0F;
    for (uint32_t j = 0; j < joint_count; ++j) {
        for (int k = 0; k < 3; ++k) {
            center[k] += model[j].r[k][3];
        }
    }
    for (int k = 0; k < 3; ++k) {
        center[k] /= (float)joint_count;
    }
    float extent = 0.0F;
    for (uint32_t j = 0; j < joint_count; ++j) {
        const float d[3] = {model[j].r[0][3] - center[0], model[j].r[1][3] - center[1], model[j].r[2][3] - center[2]};
        const float dist = sqrtf((d[0] * d[0]) + (d[1] * d[1]) + (d[2] * d[2]));
        extent = dist > extent ? dist : extent;
    }
    return extent;
}

/* Frames a rig from its rest pose, scaled against the humanoid reference. */
static void fit_rig(const char *name, const nt_skeletal_skeleton_t *skel) {
    nt_skeletal_mat34_t model[SKELETAL_SHOWCASE_MAX_JOINTS];
    nt_skeletal_fk(skel, skel->rest, model, 0, skel->joint_count);
    float center[3];
    const float extent = rig_extent(model, skel->joint_count, center);
    NT_ASSERT(extent > 0.0F && "skeletal_showcase: rig joints all rest at one point");
    const float scale = extent / s_humanoid_extent;
    nt_log_info("skeletal_showcase: rig %s joints=%u centroid=(%.3f, %.3f, %.3f) extent=%.3f scale=%.3f", name, (unsigned)skel->joint_count, (double)center[0], (double)center[1], (double)center[2],
                (double)extent, (double)scale);
    set_camera_fit(center, scale);
}

static void init_humanoid(void) {
    s_humanoid.parent = s_parent;
    s_humanoid.subtree_end = s_subtree_end;
    s_humanoid.joint_id = s_humanoid_joint_ids;
    s_humanoid.rest = s_rest;
    s_humanoid.joint_count = HUMANOID_JOINT_COUNT;
    uint8_t rig_scratch[NT_SKELETAL_RIG_ID_BYTES(HUMANOID_JOINT_COUNT)];
    for (uint32_t j = 0; j < HUMANOID_JOINT_COUNT; ++j) {
        s_humanoid_joint_ids[j] = nt_hash32_str(s_joint_names[j]).value;
    }
    s_humanoid.rig_compat_id = nt_skeletal_rig_compat_id(&s_humanoid, rig_scratch, sizeof rig_scratch);
    /* The humanoid at rest is the reference every other rig is scaled against. */
    nt_skeletal_mat34_t model[HUMANOID_JOINT_COUNT];
    nt_skeletal_fk(&s_humanoid, s_rest, model, 0, HUMANOID_JOINT_COUNT);
    float center[3];
    s_humanoid_extent = rig_extent(model, HUMANOID_JOINT_COUNT, center);
}

static void skeleton_update(void) {
    if (s_skeleton_scene.view == NULL) {
        return;
    }
    apply_pose();
    if (s_fit_pending) {
        s_fit_pending = false;
        fit_rig(s_rig_names[s_skeleton_scene.rig_source], s_skeleton_scene.view);
    }
}

static void set_rest_pose(void) {
    memset(s_skeleton_scene.angles, 0, sizeof s_skeleton_scene.angles);
    apply_pose();
}

/* out[j]: j and its whole ancestor chain rest at the origin. NSKL marks no
 * wrappers, so this also covers a skin joint sitting there (Fox _rootJoint,
 * b_Root_00). Preorder: parent[j] < j. */
static void rig_at_origin(const nt_skeletal_skeleton_t *skel, bool out[SKELETAL_SHOWCASE_MAX_JOINTS]) {
    for (uint32_t j = 0; j < skel->joint_count; ++j) {
        const uint16_t p = skel->parent[j];
        const float *t = skel->rest[j].t;
        out[j] = (p == NT_SKELETAL_NO_PARENT || out[p]) && t[0] == 0.0F && t[1] == 0.0F && t[2] == 0.0F;
    }
}

static void set_test_pose(void) {
    memset(s_skeleton_scene.angles, 0, sizeof s_skeleton_scene.angles);
    if (s_skeleton_scene.rig_source == RIG_HUMANOID) {
        s_skeleton_scene.angles[6][2] = -0.65F;
        s_skeleton_scene.angles[7][2] = -0.80F;
        s_skeleton_scene.angles[10][2] = 0.20F;
        s_skeleton_scene.angles[3][1] = 0.28F;
        s_skeleton_scene.angles[14][2] = -0.45F;
        s_skeleton_scene.angles[15][2] = 0.25F;
    } else {
        /* Imported joints have no names to pick from: bend every third joint
         * that is not origin scaffolding, so the tilt lands on limbs. */
        const nt_skeletal_skeleton_t *skel = s_skeleton_scene.view;
        bool at_origin[SKELETAL_SHOWCASE_MAX_JOINTS];
        rig_at_origin(skel, at_origin);
        for (uint32_t j = 1, n = 0; j < skel->joint_count; ++j) {
            if (!at_origin[j] && (n++ % 3U) == 0U) {
                s_skeleton_scene.angles[j][2] = 0.35F;
            }
        }
    }
    apply_pose();
}

/* Angles are zeroed and the fit recomputed; the camera follows through fit_rig. */
static void select_rig(rig_source_t rig) {
    s_skeleton_scene.rig_source = rig;
    s_skeleton_scene.view = rig_view(rig);
    s_skeleton_scene.selected_joint = 0;
    s_skeleton_scene.combo_open = false;
    s_fit_pending = true;
    memset(s_skeleton_scene.angles, 0, sizeof s_skeleton_scene.angles);
    skeleton_update();
}

static void reset_scene(void) {
    s_skeleton_scene.show_axes = false;
    s_skeleton_scene.combo_open = false;
    s_skeleton_scene.rig_combo_open = false;
    select_rig(s_skeleton_scene.rig_source);
}

static void make_camera_vp(mat4 vp, float aspect, float eye[3]) {
    const float cp = cosf(s_camera_pitch);
    vec3 target = {s_camera_target[0], s_camera_target[1], s_camera_target[2]};
    eye[0] = target[0] + sinf(s_camera_yaw) * cp * s_camera_distance;
    eye[1] = target[1] + sinf(s_camera_pitch) * s_camera_distance;
    eye[2] = target[2] + cosf(s_camera_yaw) * cp * s_camera_distance;
    vec3 up = {0.0F, 1.0F, 0.0F};
    mat4 view;
    mat4 proj;
    glm_lookat((vec3){eye[0], eye[1], eye[2]}, target, up, view);
    glm_perspective(glm_rad(45.0F), aspect, CAMERA_NEAR * s_fit_scale, CAMERA_FAR * s_fit_scale, proj);
    glm_mat4_mul(proj, view, vp);
}

static bool stage_contains(float x, float y) {
    return s_stage_bbox.found && x >= s_stage_bbox.x && x <= s_stage_bbox.x + s_stage_bbox.width && y >= s_stage_bbox.y && y <= s_stage_bbox.y + s_stage_bbox.height;
}

static void fit_camera_to_stage(float stage_w, float stage_h) {
    const float vertical_fov = glm_rad(45.0F);
    const float horizontal_fov = 2.0F * atanf(tanf(vertical_fov * 0.5F) * (stage_w / stage_h));
    /* Frame the full rig with room for joint spheres and perspective at the lower edge. */
    const float span = (s_scene_registry[s_active_scene].draw == skeleton_draw ? 5.20F : 7.0F) * s_fit_scale;
    const float vertical_distance = span / (2.0F * tanf(vertical_fov * 0.5F));
    const float horizontal_distance = span / (2.0F * tanf(horizontal_fov * 0.5F));
    s_camera_distance = vertical_distance > horizontal_distance ? vertical_distance : horizontal_distance;
    if (s_camera_distance < CAMERA_MIN * s_fit_scale) {
        s_camera_distance = CAMERA_MIN * s_fit_scale;
    }
    if (s_camera_distance > CAMERA_MAX * s_fit_scale) {
        s_camera_distance = CAMERA_MAX * s_fit_scale;
    }
}

static void update_stage_camera(const nt_pointer_t *pointer, const nt_ui_scale_t *scale) {
    if (s_skip_scene_interaction_this_frame) {
        s_skip_scene_interaction_this_frame = false;
        return;
    }
    const float pointer_screen[2] = {pointer->x, pointer->y};
    float pointer_layout[2];
    nt_ui_screen_to_layout(s_ui, pointer_screen, pointer_layout);
    const bool over_stage = stage_contains(pointer_layout[0], pointer_layout[1]);
    const float dx = pointer->dx / scale->scale_x;
    const float dy = pointer->dy / scale->scale_y;
    if (s_shell_stage_drag) {
        if (pointer->buttons[NT_BUTTON_LEFT].is_down) {
            s_camera_yaw += dx * 0.008F;
            s_camera_pitch -= dy * 0.006F;
            if (s_camera_pitch > CAMERA_PITCH_LIMIT) {
                s_camera_pitch = CAMERA_PITCH_LIMIT;
            }
            if (s_camera_pitch < -CAMERA_PITCH_LIMIT) {
                s_camera_pitch = -CAMERA_PITCH_LIMIT;
            }
        } else {
            s_shell_stage_drag = false;
        }
    } else if (s_shell_stage_pan) {
        if (pointer->buttons[NT_BUTTON_RIGHT].is_down) {
            const float pan_scale = s_camera_distance * 0.0025F;
            const float right[3] = {cosf(s_camera_yaw), 0.0F, -sinf(s_camera_yaw)};
            s_camera_target[0] -= right[0] * dx * pan_scale;
            s_camera_target[1] += dy * pan_scale;
            s_camera_target[2] -= right[2] * dx * pan_scale;
        } else {
            s_shell_stage_pan = false;
        }
    } else if (pointer->buttons[NT_BUTTON_LEFT].is_pressed && over_stage && !nt_ui_wants_pointer(s_ui)) {
        s_shell_stage_drag = true;
    } else if (pointer->buttons[NT_BUTTON_RIGHT].is_pressed && over_stage && !nt_ui_wants_pointer(s_ui)) {
        s_shell_stage_pan = true;
    }
    if (over_stage && !nt_ui_wants_pointer(s_ui)) {
        s_camera_distance -= pointer->wheel_dy * 0.35F * s_fit_scale;
        if (s_camera_distance < CAMERA_MIN * s_fit_scale) {
            s_camera_distance = CAMERA_MIN * s_fit_scale;
        }
        if (s_camera_distance > CAMERA_MAX * s_fit_scale) {
            s_camera_distance = CAMERA_MAX * s_fit_scale;
        }
    }
}
// #endregion

static const nt_skeletal_clip_t *player_clip_view(int clip) {
    if (s_clips[clip].id.value == 0) {
        return &s_humanoid_clip;
    }
    return nt_resource_is_ready(s_clip_resource[clip]) ? nt_skeletal_assets_clip(s_clip_resource[clip]) : NULL;
}

/* Views are borrowed: refetched once per frame after resource_step, before the UI and the scenes read them. */
static void refresh_views(void) {
    character_player_t *p = &s_skinned_scene.player;
    s_skeleton_scene.view = rig_view(s_skeleton_scene.rig_source);
    p->skel = rig_view(p->rig);
    p->clip_view = p->clip >= 0 ? player_clip_view(p->clip) : NULL;
}

// #region playback
static void player_deselect_clip(character_player_t *p) {
    p->clip = -1;
    p->clip_view = NULL;
    memset(&p->track, 0, sizeof p->track);
}

static void player_select_clip(character_player_t *p, int clip, const nt_skeletal_clip_t *view) {
    NT_ASSERT(view->rig_compat_id.value == p->skel->rig_compat_id.value && "skeletal_showcase: clip pairs with the selected rig");
    p->clip = clip;
    p->clip_view = view;
    p->track = (nt_skeletal_track_t){.time = 0.0, .duration = view->duration, .speed = 0.0F, .flags = NT_SKELETAL_TRACK_OCCUPIED};
    nt_log_info("skeletal_showcase: clip %s duration=%.3f samples=%u", s_clips[clip].name, view->duration, (unsigned)view->sample_count);
}

static void player_select_rig(character_player_t *p, rig_source_t rig) {
    p->rig = rig;
    p->skel = rig_view(rig);
    s_fit_pending = true;
    player_deselect_clip(p);
}

/* The clip's own sample grid: Step and the time slider move on it. */
static double clip_step(const nt_skeletal_clip_t *c) { return c->sample_count > 1 ? c->duration / (double)(c->sample_count - 1) : c->duration; }

/* Looping, the last reachable sample is the one before the wrap point. */
static int clip_last_frame(const character_player_t *p) { return (int)p->clip_view->sample_count - 1 - (p->loop && p->clip_view->sample_count > 1 ? 1 : 0); }

static void player_seek_frame(character_player_t *p, int frame) { p->track.time = (double)frame * clip_step(p->clip_view); }

/* The nearest reachable sample; the clock sits between two while playing. */
static int player_frame(const character_player_t *p) { return (int)fmin(round(p->track.time / clip_step(p->clip_view)), (double)clip_last_frame(p)); }

/* A seek to the adjacent sample in frame arithmetic: a step through advance
 * lands one ulp short of the wrap point for many clip lengths. Paused between
 * two samples, the step goes to the next one in its direction, never past it. */
static void player_step(character_player_t *p) {
    if (p->clip_view == NULL) {
        return;
    }
    p->paused = true;
    const int n = clip_last_frame(p) + 1;
    const double f = p->track.time / clip_step(p->clip_view); /* the epsilon absorbs the rounding of a grid-exact seek */
    const int next = p->reverse ? (int)ceil(f - 1e-6) - 1 : (int)floor(f + 1e-6) + 1;
    player_seek_frame(p, p->loop ? (next + n) % n : (int)fmin(fmax(next, 0), n - 1));
}

static void player_reset(character_player_t *p) {
    player_deselect_clip(p);
    p->speed_mag = 1.0F;
    p->reverse = false;
    p->paused = false;
    p->loop = true;
    s_fit_pending = true;
}

static void player_update(character_player_t *p) {
    if (p->skel == NULL) {
        return;
    }
    if (p->clip_view != NULL) {
        const float speed = p->reverse ? -p->speed_mag : p->speed_mag;
        p->track.speed = p->paused ? 0.0F : speed;
        p->track.flags = NT_SKELETAL_TRACK_OCCUPIED | (p->loop ? NT_SKELETAL_TRACK_LOOPING : 0U);
        nt_skeletal_tracks_advance(&p->track, 1, (double)g_nt_app.dt);
        nt_skeletal_sample(p->clip_view, p->track.time, p->local);
    }
    nt_skeletal_fk(p->skel, p->clip_view != NULL ? p->local : p->skel->rest, p->model, 0, p->skel->joint_count);
    if (s_fit_pending) {
        s_fit_pending = false;
        fit_rig(s_rig_names[p->rig], p->skel);
    }
}
static void skinned_reset(void) {
    player_reset(&s_skinned_scene.player);
    skinned_cancel_input();
}

static void skinned_update(void) {
    character_player_t *p = &s_skinned_scene.player;
    if (s_cpu_reference) {
        p->paused = true;
    }
    const bool fit = s_fit_pending;
    player_update(p);
    if (p->rig == RIG_HUMANOID && s_independent_clothes) {
        /* Independent clothes play the same clip 0.45 s ahead of the body. */
        nt_skeletal_trs_t local[SKELETAL_SHOWCASE_MAX_JOINTS];
        if (p->clip_view != NULL) {
            const double time = p->track.time + 0.45;
            nt_skeletal_sample(p->clip_view, p->loop ? fmod(time, p->track.duration) : fmin(time, p->track.duration), local);
        }
        nt_skeletal_fk(&s_humanoid, p->clip_view != NULL ? local : s_rest, s_clothes_model, 0, HUMANOID_JOINT_COUNT);
    }
    if (fit && !s_fit_pending) {
        s_camera_yaw = 0.9F;
    }
}
// #endregion

// #region ordering
static void ordering_reset(void) {
    s_order_count = 17;
    s_order_mode = 0;
    s_order_shared = false;
    s_order_two_passes = false;
    s_order_paused = false;
    s_order_combo_open = false;
    memset(&s_order_stats, 0, sizeof s_order_stats);
    for (uint32_t i = 0; i < SKELETAL_SHOWCASE_MAX_INSTANCES; ++i) {
        s_order_tracks[i] = (nt_skeletal_track_t){
            .time = fmod((double)i * 0.27, s_humanoid_clip.duration), .duration = s_humanoid_clip.duration, .speed = 1, .flags = NT_SKELETAL_TRACK_OCCUPIED | NT_SKELETAL_TRACK_LOOPING};
    }
    s_fit_pending = true;
}

static void ordering_update(void) {
    for (int i = 0; i < s_order_count; ++i) {
        s_order_tracks[i].speed = s_order_paused ? 0 : 1;
    }
    nt_skeletal_tracks_advance(s_order_tracks, (uint32_t)s_order_count, (double)g_nt_app.dt);
    if (s_fit_pending) {
        const int columns = s_order_count < 16 ? s_order_count : 16;
        const int rows = (s_order_count + 15) / 16;
        const float center[3] = {(float)(columns - 1) * 2.5F, 1.8F, (float)(rows - 1) * 2.5F};
        set_camera_fit(center, (float)(columns > rows ? columns : rows));
        s_camera_yaw = 0.3F;
        s_camera_pitch = 0.45F;
        s_fit_pending = false;
    }
}

static void ordering_cancel_input(void) { s_order_combo_open = false; }
// #endregion

// #region scene meshes
/* Deformation rewrites only positions; the other streams and indices stay as copied. */
static void retain_cpu_output(cpu_mesh_t *source) {
    source->output = malloc(source->size);
    NT_ASSERT(source->output != NULL);
    memcpy(source->output, source->data, source->size);
}

/* The standard MESH activator already checked structure. Decode once per publication;
 * keep exactly its packed attribute bytes for the independent scalar reference. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void copy_mesh_source(cpu_mesh_t *source, const uint8_t *wire) {
    const NtMeshAssetHeader *header = (const NtMeshAssetHeader *)wire;
    const NtStreamDesc *streams = (const NtStreamDesc *)(wire + sizeof(*header));
    const uint32_t prefix = (uint32_t)sizeof(*header) + (header->stream_count * (uint32_t)sizeof(*streams));
    const uint32_t index_size = header->index_type == 1 ? 2U : 4U;
    source->size = prefix + header->vertex_data_size + header->index_count * index_size;
    source->data = malloc(source->size);
    NT_ASSERT(source->data != NULL);
    memcpy(source->data, wire, prefix);
    uint32_t elements[NT_MESH_MAX_STREAMS];
    source->position = source->joints = source->weights = UINT32_MAX;
    for (uint32_t i = 0; i < header->stream_count; ++i) {
        elements[i] = nt_stream_type_size(streams[i].type) * streams[i].count;
        if (streams[i].name_hash == nt_hash32_str("position").value) {
            NT_ASSERT(streams[i].type == NT_STREAM_FLOAT32 && streams[i].count == 3);
            source->position = source->stride;
        }
        if (streams[i].name_hash == nt_hash32_str("joints").value) {
            NT_ASSERT(streams[i].type == NT_STREAM_UINT8 && streams[i].count == 4 && !streams[i].normalized);
            source->joints = source->stride;
        }
        if (streams[i].name_hash == nt_hash32_str("weights").value) {
            NT_ASSERT(streams[i].type == NT_STREAM_UINT8 && streams[i].count == 4 && streams[i].normalized);
            source->weights = source->stride;
        }
        source->stride += elements[i];
    }
    NT_ASSERT(source->position != UINT32_MAX && source->joints != UINT32_MAX && source->weights != UINT32_MAX);
    if (header->vertex_wire == NT_MESH_WIRE_VTX_SOA) {
        const bool decoded = nt_meshwire_reinterleave(source->data + prefix, wire + prefix, header->vertex_count, elements, header->stream_count);
        NT_ASSERT(decoded);
    } else {
        NT_ASSERT(header->vertex_wire == NT_MESH_WIRE_VTX_RAW && "skeletal_showcase: decode the new vertex wire like nt_gfx_activate_mesh");
        memcpy(source->data + prefix, wire + prefix, header->vertex_data_size);
    }
    const uint32_t indices = prefix + header->vertex_data_size;
    if (header->index_wire == NT_MESH_WIRE_IDX_MESHOPT) {
        const bool decoded = nt_meshwire_decode_indices(source->data + indices, header->index_count, index_size, wire + indices, header->index_data_size, header->vertex_count);
        NT_ASSERT(decoded);
    } else {
        NT_ASSERT(header->index_wire == NT_MESH_WIRE_IDX_RAW && "skeletal_showcase: decode the new index wire like nt_gfx_activate_mesh");
        memcpy(source->data + indices, wire + indices, header->index_data_size);
    }
    NtMeshAssetHeader *raw = (NtMeshAssetHeader *)source->data;
    raw->vertex_wire = NT_MESH_WIRE_VTX_RAW;
    raw->index_wire = NT_MESH_WIRE_IDX_RAW;
    raw->index_data_size = header->index_count * index_size;
    retain_cpu_output(source);
}

/* MESH on_post_resolve: every new publication of an imported mesh replaces its copy. */
static void on_mesh_published(const uint8_t *data, uint32_t size, nt_resource_t handle, uint32_t runtime_handle, void *user_data) {
    (void)size;
    (void)runtime_handle;
    (void)user_data;
    for (uint32_t rig = RIG_FOX; rig < RIG_COUNT; ++rig) {
        if (handle.id != s_mesh_resource[rig].id) {
            continue;
        }
        NT_ASSERT(data != NULL && "skeletal_showcase: packs keep their blobs");
        cpu_mesh_t *source = &s_cpu_mesh[rig];
        if (source->reference.id != 0) {
            nt_gfx_deactivate_mesh(source->reference.id);
        }
        free(source->data);
        free(source->output);
        *source = (cpu_mesh_t){0};
        copy_mesh_source(source, data);
        nt_log_info("skeletal_showcase: copied %s MESH (%u bytes)", s_rig_names[rig], source->size);
    }
}

/* Activated meshes are immutable, so the reference is rebuilt only when the pose changed. */
static void deform_cpu_mesh(cpu_mesh_t *source, const nt_skin_binding_t *skin, const nt_skeletal_mat34_t *model, uint16_t joint_count) {
    if (source->reference.id != 0 && memcmp(source->pose, model, joint_count * sizeof *model) == 0) {
        return;
    }
    memcpy(source->pose, model, joint_count * sizeof *model);
    const NtMeshAssetHeader *header = (const NtMeshAssetHeader *)source->data;
    const uint32_t prefix = (uint32_t)sizeof(*header) + (header->stream_count * (uint32_t)sizeof(NtStreamDesc));
    nt_skeletal_mat34_t palette[SKELETAL_SHOWCASE_MAX_PALETTE];
    nt_skin_palette_build(skin, model, joint_count, palette, SKELETAL_SHOWCASE_MAX_PALETTE);
    for (uint32_t i = 0; i < header->vertex_count; ++i) {
        const uint8_t *v = source->data + prefix + ((size_t)i * source->stride);
        const float *position = (const float *)(v + source->position);
        float *out = (float *)(source->output + prefix + ((size_t)i * source->stride) + source->position);
        out[0] = out[1] = out[2] = 0.0F;
        for (uint32_t lane = 0; lane < 4; ++lane) {
            const float weight = (float)v[source->weights + lane] / 255.0F;
            const nt_skeletal_mat34_t *matrix = &palette[v[source->joints + lane]];
            for (uint32_t row = 0; row < 3; ++row) {
                out[row] += weight * (matrix->r[row][0] * position[0] + matrix->r[row][1] * position[1] + matrix->r[row][2] * position[2] + matrix->r[row][3]);
            }
        }
    }
    if (source->reference.id != 0) {
        nt_gfx_deactivate_mesh(source->reference.id);
    }
    source->reference = (nt_mesh_t){nt_gfx_activate_mesh(source->output, source->size)};
    NT_ASSERT(source->reference.id != 0);
}

/* Three rings per bone include a half-weight ring, so bending exercises blending. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void make_humanoid_mesh(cpu_mesh_t *source, bool clothes, bool local_space, const nt_skeletal_mat34_t *rest_model) {
    typedef struct {
        float position[3], uv[2];
        uint8_t joints[4], weights[4];
    } vertex_t;
    const uint32_t segments = clothes ? 6U : HUMANOID_JOINT_COUNT - 1U;
    const uint32_t vertices = segments * 24U;
    const uint32_t indices = segments * 96U;
    const uint32_t prefix = (uint32_t)sizeof(NtMeshAssetHeader) + (4U * (uint32_t)sizeof(NtStreamDesc));
    source->stride = sizeof(vertex_t);
    source->position = offsetof(vertex_t, position);
    source->joints = offsetof(vertex_t, joints);
    source->weights = offsetof(vertex_t, weights);
    source->size = prefix + (vertices * source->stride) + (indices * (uint32_t)sizeof(uint16_t));
    source->data = calloc(1, source->size);
    NT_ASSERT(source->data != NULL);
    NtMeshAssetHeader *header = (NtMeshAssetHeader *)source->data;
    *header = (NtMeshAssetHeader){.magic = NT_MESH_MAGIC,
                                  .version = NT_MESH_VERSION,
                                  .stream_count = 4,
                                  .index_type = 1,
                                  .vertex_count = vertices,
                                  .index_count = indices,
                                  .vertex_data_size = vertices * source->stride,
                                  .index_data_size = indices * 2U};
    NtStreamDesc *streams = (NtStreamDesc *)(source->data + sizeof(*header));
    streams[0] = (NtStreamDesc){.name_hash = nt_hash32_str("position").value, .type = NT_STREAM_FLOAT32, .count = 3};
    streams[1] = (NtStreamDesc){.name_hash = nt_hash32_str("uv").value, .type = NT_STREAM_FLOAT32, .count = 2};
    streams[2] = (NtStreamDesc){.name_hash = nt_hash32_str("joints").value, .type = NT_STREAM_UINT8, .count = 4};
    streams[3] = (NtStreamDesc){.name_hash = nt_hash32_str("weights").value, .type = NT_STREAM_UINT8, .count = 4, .normalized = 1};
    vertex_t *v = (vertex_t *)(source->data + prefix);
    uint16_t *idx = (uint16_t *)(source->data + prefix + header->vertex_data_size);
    uint32_t segment = 0;
    for (uint16_t j = 1; j < HUMANOID_JOINT_COUNT; ++j) {
        if (clothes && j != 1 && j != 2 && j != 6 && j != 7 && j != 10 && j != 11) {
            continue;
        }
        const uint16_t parent = s_parent[j];
        float a[3];
        float direction[3];
        for (uint32_t k = 0; k < 3; ++k) {
            a[k] = rest_model[parent].r[k][3];
            direction[k] = rest_model[j].r[k][3] - a[k];
        }
        float axis[3];
        float u[3];
        float w[3];
        glm_vec3_copy(direction, axis);
        glm_vec3_normalize(axis);
        glm_vec3_cross(axis, (vec3){0, 0, 1}, u);
        glm_vec3_normalize(u);
        glm_vec3_cross(axis, u, w);
        const float radius = (j <= 4 ? 0.24F : 0.10F) * (clothes ? 1.35F : 1.0F);
        for (uint32_t ring = 0; ring < 3; ++ring) {
            for (uint32_t side = 0; side < 8; ++side) {
                vertex_t *out = &v[(segment * 24U) + (ring * 8U) + side];
                const float angle = (float)side * (2.0F * GLM_PIf / 8.0F);
                const float fraction = (float)ring * 0.5F;
                for (uint32_t k = 0; k < 3; ++k) {
                    const float authored = a[k] + (fraction * direction[k]) + (radius * ((cosf(angle) * u[k]) + (sinf(angle) * w[k])));
                    const float translation[3] = {0.3F, 0.25F, -0.2F};
                    out->position[k] = local_space ? (authored - translation[k]) / 1.25F : authored;
                    if (segment == 0 && ring == 0 && side == 0) {
                        header->aabb_min[k] = header->aabb_max[k] = out->position[k];
                    } else {
                        header->aabb_min[k] = fminf(header->aabb_min[k], out->position[k]);
                        header->aabb_max[k] = fmaxf(header->aabb_max[k], out->position[k]);
                    }
                }
                out->uv[0] = (float)side / 8.0F;
                out->uv[1] = fraction;
                out->joints[0] = (uint8_t)parent;
                out->joints[1] = (uint8_t)j;
                out->weights[1] = ring == 2 ? 255 : (uint8_t)(ring * 128U);
                out->weights[0] = (uint8_t)(255U - out->weights[1]);
                if (ring < 2) {
                    const uint16_t base = (uint16_t)((segment * 24U) + (ring * 8U));
                    const uint16_t x = (uint16_t)(base + side);
                    const uint16_t y = (uint16_t)(base + ((side + 1U) % 8U));
                    const uint16_t face[6] = {x, y, (uint16_t)(x + 8U), y, (uint16_t)(y + 8U), (uint16_t)(x + 8U)};
                    memcpy(idx + ((size_t)segment * 96U) + ((size_t)ring * 48U) + ((size_t)side * 6U), face, sizeof face);
                }
            }
        }
        ++segment;
    }
    NT_ASSERT(segment == segments);
}

static void init_humanoid_meshes(void) {
    nt_skeletal_mat34_t model[HUMANOID_JOINT_COUNT];
    nt_skeletal_fk(&s_humanoid, s_rest, model, 0, HUMANOID_JOINT_COUNT);
    const nt_skeletal_mat34_t clothes_space = {.r = {{1.25F, 0, 0, 0.3F}, {0, 1.25F, 0, 0.25F}, {0, 0, 1.25F, -0.2F}}};
    for (uint16_t j = 0; j < HUMANOID_JOINT_COUNT; ++j) {
        s_humanoid_remap[j] = j;
        s_humanoid_inverse[0][j] = (nt_skeletal_mat34_t){.r = {{1, 0, 0, -model[j].r[0][3]}, {0, 1, 0, -model[j].r[1][3]}, {0, 0, 1, -model[j].r[2][3]}}};
        nt_skeletal_mat34_mul(&s_humanoid_inverse[0][j], &clothes_space, &s_humanoid_inverse[1][j]);
    }
    for (uint32_t i = 0; i < 2; ++i) {
        s_humanoid_skin[i] = (nt_skin_binding_t){.rig_compat_id = s_humanoid.rig_compat_id, .remap = s_humanoid_remap, .inverse_bind = s_humanoid_inverse[i], .palette_count = HUMANOID_JOINT_COUNT};
    }
    s_humanoid_clip = (nt_skeletal_clip_t){.rig_compat_id = s_humanoid.rig_compat_id,
                                           .duration = 2.0,
                                           .base = s_rest,
                                           .blocks = &s_motion_blocks[0][0][0],
                                           .q_joint = s_motion_joints,
                                           .s_max = 1,
                                           .sample_count = 49,
                                           .joint_count = HUMANOID_JOINT_COUNT,
                                           .n_q = 6};
    for (uint32_t f = 0; f < 49; ++f) {
        const float phase = (float)f * (2.0F * GLM_PIf / 48.0F);
        for (uint32_t row = 0; row < 6; ++row) {
            const float angles[3] = {sinf(phase + (float)row) * (row < 2 ? 0.17F : 0.61F), cosf(phase) * 0.14F, sinf(phase + ((float)row * 0.7F)) * 0.31F};
            make_offset_quat(angles, s_motion_blocks[f][row]);
        }
    }
    make_humanoid_mesh(&s_cpu_mesh[RIG_HUMANOID], false, false, model);
    make_humanoid_mesh(&s_cpu_mesh[CPU_CLOTHES], true, true, model);
    make_humanoid_mesh(&s_cpu_mesh[CPU_ORDER_CLOTHES], true, false, model);
    /* The ordering clothes are never deformed on the CPU. */
    retain_cpu_output(&s_cpu_mesh[RIG_HUMANOID]);
    retain_cpu_output(&s_cpu_mesh[CPU_CLOTHES]);
    const uint32_t sources[3] = {RIG_HUMANOID, CPU_CLOTHES, CPU_ORDER_CLOTHES};
    for (uint32_t i = 0; i < 3; ++i) {
        const cpu_mesh_t *source = &s_cpu_mesh[sources[i]];
        s_procedural_mesh[i] = (nt_mesh_t){nt_gfx_activate_mesh(source->data, source->size)};
        NT_ASSERT(s_procedural_mesh[i].id != 0);
    }
}

static nt_material_t make_mesh_material(nt_resource_t texture, bool skinned) {
    nt_material_create_desc_t desc = {
        .textures = {{.name = "u_texture", .resource = texture}, {.name = "u_skin_matrices"}},
        .texture_count = skinned ? 2 : 1,
        .attr_map = {{.stream_name = "position", .location = 0}, {.stream_name = "uv", .location = 1}, {.stream_name = "joints", .location = 8}, {.stream_name = "weights", .location = 9}},
        .attr_map_count = skinned ? 4 : 2,
        .blend = nt_blend_opaque(),
        .cull_mode = NT_CULL_NONE,
        .color_mode = NT_COLOR_MODE_RGBA8,
        .depth_test = true,
        .depth_write = true,
        .label = "skeletal_surface",
    };
    return nt_material_create(&desc);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void init_mesh_scene(void) {
    nt_result_t result = nt_entity_init(&(nt_entity_desc_t){.max_entities = SKELETAL_SHOWCASE_MAX_INSTANCES + 2});
    NT_ASSERT(result == NT_OK);
    result = nt_transform_comp_init(&(nt_transform_comp_desc_t){.capacity = SKELETAL_SHOWCASE_MAX_INSTANCES + 2});
    NT_ASSERT(result == NT_OK);
    result = nt_mesh_comp_init(&(nt_mesh_comp_desc_t){.capacity = SKELETAL_SHOWCASE_MAX_INSTANCES + 2});
    NT_ASSERT(result == NT_OK);
    result = nt_material_comp_init(&(nt_material_comp_desc_t){.capacity = SKELETAL_SHOWCASE_MAX_INSTANCES + 2});
    NT_ASSERT(result == NT_OK);
    result = nt_drawable_comp_init(&(nt_drawable_comp_desc_t){.capacity = SKELETAL_SHOWCASE_MAX_INSTANCES + 2});
    NT_ASSERT(result == NT_OK);
    result = nt_skin_comp_init(&(nt_skin_comp_desc_t){.capacity = SKELETAL_SHOWCASE_MAX_INSTANCES + 2});
    NT_ASSERT(result == NT_OK);
    result = nt_skeletal_gpu_init(&(nt_skeletal_gpu_desc_t){.width = 3 * SKELETAL_SHOWCASE_MAX_PALETTE, .height = SKELETAL_SHOWCASE_MAX_INSTANCES});
    NT_ASSERT(result == NT_OK);
    /* Static meshes are only the CPU reference: one body and one shirt. */
    result = nt_mesh_renderer_init(&(nt_mesh_renderer_desc_t){.max_instances = 2, .max_pipelines = 8, .max_mesh_layouts = 4});
    NT_ASSERT(result == NT_OK);
    /* The instance ring has room for both ordering passes, so a frame wraps it at most once. */
    result = nt_skinned_mesh_renderer_init(&(nt_skinned_mesh_renderer_desc_t){.max_instances = 2 * SKELETAL_SHOWCASE_MAX_INSTANCES, .max_pipelines = 8, .max_mesh_layouts = 4});
    NT_ASSERT(result == NT_OK);
    for (uint32_t i = 0; i < SKELETAL_SHOWCASE_MAX_INSTANCES + 2U; ++i) {
        const nt_entity_t e = nt_entity_create();
        if (i < 2) {
            s_mesh_entities[i] = e;
        } else {
            const uint32_t index = i - 2U;
            s_order_entities[index] = e;
        }
        bool added = nt_transform_comp_add(e);
        NT_ASSERT(added);
        added = nt_mesh_comp_add(e);
        NT_ASSERT(added);
        added = nt_material_comp_add(e);
        NT_ASSERT(added);
        added = nt_drawable_comp_add(e);
        NT_ASSERT(added);
        added = nt_skin_comp_add(e);
        NT_ASSERT(added);
        if (i >= 2) {
            const uint32_t index = i - 2U;
            const uint32_t row = index / 16U;
            nt_transform_comp_set_position(e, (float)(index % 16U) * 5.0F, 0, (float)row * 5.0F);
        }
    }
    nt_transform_comp_update();
    init_humanoid_meshes();
    s_mesh_resource[RIG_FOX] = nt_resource_request(ASSET_MESH_SKELETAL_SHOWCASE_FOX_MESH, NT_ASSET_MESH);
    s_mesh_resource[RIG_CESIUMMAN] = nt_resource_request(ASSET_MESH_SKELETAL_SHOWCASE_CESIUMMAN_MESH, NT_ASSET_MESH);
    s_skin_resource[RIG_FOX] = nt_resource_request(ASSET_SKIN_BINDING_SKELETAL_SHOWCASE_FOX_NSKN, NT_ASSET_SKIN_BINDING);
    s_skin_resource[RIG_CESIUMMAN] = nt_resource_request(ASSET_SKIN_BINDING_SKELETAL_SHOWCASE_CESIUMMAN_NSKN, NT_ASSET_SKIN_BINDING);
    s_texture_resource[RIG_HUMANOID] = nt_resource_request(ASSET_TEXTURE_SKELETAL_SHOWCASE_WHITE_TEXTURE, NT_ASSET_TEXTURE);
    s_texture_resource[RIG_FOX] = nt_resource_request(ASSET_TEXTURE_SKELETAL_SHOWCASE_FOX_TEXTURE, NT_ASSET_TEXTURE);
    s_texture_resource[RIG_CESIUMMAN] = nt_resource_request(ASSET_TEXTURE_SKELETAL_SHOWCASE_CESIUMMAN_TEXTURE, NT_ASSET_TEXTURE);
    s_texture_resource[RIG_COUNT] = nt_resource_request(ASSET_TEXTURE_SKELETAL_SHOWCASE_TINT_TEXTURE, NT_ASSET_TEXTURE);
    for (uint32_t i = 0; i <= RIG_COUNT; ++i) {
        s_skin_material[i] = make_mesh_material(s_texture_resource[i], true);
        s_static_material[i] = make_mesh_material(s_texture_resource[i], false);
    }
    s_skin_program.vs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SKINNED_VERT, NT_ASSET_SHADER_CODE);
    s_skin_program.fs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_MESH_INST_FRAG, NT_ASSET_SHADER_CODE);
    s_static_program.vs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_MESH_INST_VERT, NT_ASSET_SHADER_CODE);
    s_static_program.fs = s_skin_program.fs;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void restore_mesh_scene(void) {
    nt_result_t result = nt_skinned_mesh_renderer_restore_gpu();
    NT_ASSERT(result == NT_OK);
    result = nt_mesh_renderer_restore_gpu();
    NT_ASSERT(result == NT_OK);
    result = nt_skeletal_gpu_restore_gpu();
    NT_ASSERT(result == NT_OK);
    nt_program_ref_drop(&s_skin_program);
    nt_program_ref_drop(&s_static_program);
    const uint32_t sources[3] = {RIG_HUMANOID, CPU_CLOTHES, CPU_ORDER_CLOTHES};
    for (uint32_t i = 0; i < 3; ++i) {
        nt_gfx_deactivate_mesh(s_procedural_mesh[i].id);
        const cpu_mesh_t *source = &s_cpu_mesh[sources[i]];
        s_procedural_mesh[i] = (nt_mesh_t){nt_gfx_activate_mesh(source->data, source->size)};
        NT_ASSERT(s_procedural_mesh[i].id != 0);
    }
    for (uint32_t i = 0; i < CPU_SOURCE_COUNT; ++i) {
        cpu_mesh_t *source = &s_cpu_mesh[i];
        if (source->reference.id != 0) {
            nt_gfx_deactivate_mesh(source->reference.id);
            source->reference = (nt_mesh_t){nt_gfx_activate_mesh(source->output, source->size)};
            NT_ASSERT(source->reference.id != 0);
        }
    }
}

#ifndef NT_PLATFORM_WEB
static void shutdown_mesh_scene(void) {
    nt_skinned_mesh_renderer_shutdown();
    nt_mesh_renderer_shutdown();
    nt_skeletal_gpu_shutdown();
    for (uint32_t i = 0; i < 3; ++i) {
        nt_gfx_deactivate_mesh(s_procedural_mesh[i].id);
    }
    for (uint32_t i = 0; i < CPU_SOURCE_COUNT; ++i) {
        if (s_cpu_mesh[i].reference.id != 0) {
            nt_gfx_deactivate_mesh(s_cpu_mesh[i].reference.id);
        }
        free(s_cpu_mesh[i].data);
        free(s_cpu_mesh[i].output);
    }
    for (uint32_t i = 0; i <= RIG_COUNT; ++i) {
        nt_material_destroy(s_skin_material[i]);
        nt_material_destroy(s_static_material[i]);
    }
    nt_program_ref_drop(&s_skin_program);
    nt_program_ref_drop(&s_static_program);
    nt_skin_comp_shutdown();
    nt_drawable_comp_shutdown();
    nt_material_comp_shutdown();
    nt_mesh_comp_shutdown();
    nt_transform_comp_shutdown();
    nt_entity_shutdown();
}
#endif
// #endregion

// #region resources
static void link_programs(void) {
    if (nt_program_ref_update(&s_skin_program)) {
        for (uint32_t i = 0; i <= RIG_COUNT; ++i) {
            nt_material_set_program(s_skin_material[i], s_skin_program.program);
        }
    }
    if (nt_program_ref_update(&s_static_program)) {
        for (uint32_t i = 0; i <= RIG_COUNT; ++i) {
            nt_material_set_program(s_static_material[i], s_static_program.program);
        }
    }
    if (nt_program_ref_update(&s_sprite_program)) {
        nt_material_set_program(s_sprite_material, s_sprite_program.program);
    }
    if (nt_program_ref_update(&s_text_program)) {
        nt_material_set_program(s_text_material, s_text_program.program);
    }
}

static void try_bind_resources(void) {
    if (!s_atlas_bound && nt_resource_is_ready(s_atlas)) {
        s_white_region = nt_atlas_find_region(s_atlas, ASSET_ATLAS_REGION_SKELETAL_SHOWCASE_UI__WHITE.value);
        NT_ASSERT(s_white_region != NT_ATLAS_INVALID_REGION);
        nt_ui_set_atlas_white_region(s_ui, s_atlas, s_white_region);
        s_atlas_bound = true;
    }
    if (!s_font_bound && nt_resource_is_ready(s_font_resource)) {
        nt_font_add(s_font, s_font_resource);
        nt_ui_set_font(s_ui, 0U, s_font);
        s_font_bound = true;
    }
}

static void init_ui_styles(void) {
    memset(&s_button_style, 0, sizeof s_button_style);
    s_button_style.idle.scale = 1.0F;
    s_button_style.idle.opacity = 1.0F;
    s_button_style.idle.bg_tint = 0xFF1C2B42U;
    s_button_style.hover = s_button_style.idle;
    s_button_style.hover.bg_tint = 0xFF2E4C6BU;
    s_button_style.hover.scale = 1.03F;
    s_button_style.pressed = s_button_style.idle;
    s_button_style.pressed.bg_tint = 0xFF3C78A8U;
    s_button_style.pressed.scale = 0.97F;
    s_button_style.disabled = s_button_style.idle;
    s_button_style.disabled.bg_tint = 0xFF1C2B42U;
    s_button_style.disabled.opacity = 0.45F;
    s_button_style.transition_speed = 10.0F;
    s_button_style.slice9_scale = 1.0F;

    s_slider_style = nt_ui_slider_style_defaults();
    s_joint_scroll_style = nt_ui_scroll_style_defaults();
    s_joint_scroll_style.scroll_x = false;
    s_joint_scroll_style.scroll_y = true;
    s_joint_combo_style = nt_ui_dropdown_style_defaults();
    s_joint_combo_style.row_height = 28U;
    s_joint_combo_style.min_width = 250U;
    s_joint_combo_style.max_visible_rows = 8U;
    s_joint_combo_style.trigger_idle.fill = 0xFF1C2B42U;
    s_joint_combo_style.trigger_hover.fill = 0xFF2E4C6BU;
    s_joint_combo_style.trigger_pressed.fill = 0xFF3C78A8U;
    s_joint_combo_style.row_idle.fill = 0xFF1C2B42U;
    s_joint_combo_style.row_hover.fill = 0xFF2E4C6BU;
    s_joint_combo_style.row_pressed.fill = 0xFF3C78A8U;
    s_joint_combo_style.row_selected.fill = 0xFF3C78A8U;
    s_joint_combo_style.panel_fill = 0xFF152238U;
    s_scene_combo_style = s_joint_combo_style;
    s_scene_combo_style.min_width = 210U;
    s_scene_combo_style.max_visible_rows = 8U;
    const nt_atlas_region_ref_t track = nt_atlas_ref(s_atlas, ASSET_ATLAS_REGION_SKELETAL_SHOWCASE_UI_TRACK.value);
    const nt_atlas_region_ref_t fill = nt_atlas_ref(s_atlas, ASSET_ATLAS_REGION_SKELETAL_SHOWCASE_UI_FILL.value);
    const nt_atlas_region_ref_t thumb = nt_atlas_ref(s_atlas, ASSET_ATLAS_REGION_SKELETAL_SHOWCASE_UI_THUMB.value);
    s_slider_style.states[0].track = track;
    s_slider_style.states[0].fill = fill;
    s_slider_style.states[0].thumb = thumb;
    s_slider_style.states[0].track_tint = 0xFFFFFFFFU;
    s_slider_style.states[0].fill_tint = 0xFF70D0FFU;
    s_slider_style.states[0].thumb_tint = 0xFFFFFFFFU;
    for (int i = 1; i < 4; ++i) {
        s_slider_style.states[i] = s_slider_style.states[0];
    }
    s_slider_style.track_w = 175.0F;
    s_slider_style.track_h = 14.0F;
    s_slider_style.thumb_w = 22.0F;
    s_slider_style.thumb_h = 22.0F;
    s_slider_style.fill_mode = NT_UI_FILL_STRETCH;
    s_slider_style.fill_direction = NT_UI_FILL_LTR;
    s_slider_style.orientation = NT_UI_SLIDER_HORIZONTAL;
    s_slider_style.state_speed = 10.0F;
    s_slider_style.value_speed = 0.0F;

    s_checkbox_style = nt_ui_checkbox_style_defaults();
    s_checkbox_style.box_w = 22.0F;
    s_checkbox_style.box_h = 22.0F;
    s_checkbox_style.overlay_w = 18.0F;
    s_checkbox_style.overlay_h = 18.0F;
    s_checkbox_style.text_base = (nt_ui_label_style_t){.font_id = 0U, .font_size = 14.0F, .color = {215.0F, 220.0F, 230.0F, 255.0F}};
    const nt_atlas_region_ref_t box = nt_atlas_ref(s_atlas, ASSET_ATLAS_REGION_SKELETAL_SHOWCASE_UI_BOX_OFF.value);
    const nt_atlas_region_ref_t check = nt_atlas_ref(s_atlas, ASSET_ATLAS_REGION_SKELETAL_SHOWCASE_UI_CHECKMARK.value);
    s_checkbox_style.unchecked[NT_UI_CB_IDLE].box = box;
    s_checkbox_style.checked[NT_UI_CB_IDLE].box = box;
    s_checkbox_style.checked[NT_UI_CB_IDLE].check = check;
    s_checkbox_style.checked[NT_UI_CB_IDLE].check_tint = 0xFF7CE08CU;
    s_checkbox_style.unchecked[NT_UI_CB_DISABLED].opacity = 0.45F;
    s_checkbox_style.checked[NT_UI_CB_DISABLED].opacity = 0.45F;
}
// #endregion

// #region ui
static const nt_ui_label_style_t *label_style(float size, Clay_Color color) {
    static nt_ui_label_style_t style;
    style = (nt_ui_label_style_t){.font_id = 0U, .font_size = size, .color = color};
    return &style;
}

static bool text_button(uint32_t id, const char *text, bool active) {
    const Clay_ElementDeclaration decl = {
        .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(25)}, .padding = CLAY_PADDING_ALL(3), .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}}};
    nt_ui_button_style_t style = s_button_style;
    const Clay_Color color = active ? (Clay_Color){100.0F, 170.0F, 230.0F, 255.0F} : (Clay_Color){215.0F, 220.0F, 230.0F, 255.0F};
    bool clicked = false;
    nt_ui_button_begin(s_ui, NT_UI_DATA_LAYER(3), id, &style, &decl, true, NULL);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), text, &(nt_ui_label_style_t){.font_id = 0U, .font_size = 14.0F, .color = color});
    clicked = nt_ui_button_end(s_ui);
    return clicked;
}

static bool text_button_fixed(uint32_t id, const char *text, bool active, float width, float height) {
    const Clay_ElementDeclaration decl = {
        .layout = {.sizing = {CLAY_SIZING_FIXED(width), CLAY_SIZING_FIXED(height)}, .padding = CLAY_PADDING_ALL(3), .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
    };
    nt_ui_button_style_t style = s_button_style;
    const Clay_Color color = active ? (Clay_Color){100.0F, 170.0F, 230.0F, 255.0F} : (Clay_Color){215.0F, 220.0F, 230.0F, 255.0F};
    nt_ui_button_begin(s_ui, NT_UI_DATA_LAYER(3), id, &style, &decl, true, NULL);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), text, &(nt_ui_label_style_t){.font_id = 0U, .font_size = 14.0F, .color = color});
    return nt_ui_button_end(s_ui);
}

static void cancel_scene_input(void) {
    s_shell_stage_drag = false;
    s_shell_stage_pan = false;
    s_scene_combo_open = false;
}

static void cancel_active_scene_input(void) {
    if (s_active_scene >= 0) {
        s_scene_registry[s_active_scene].cancel_input();
    }
}

static void declare_header(void) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(48)},
                     .layoutDirection = CLAY_LEFT_TO_RIGHT,
                     .childGap = 8,
                     .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER},
                     .padding = CLAY_PADDING_ALL(8)},
          .backgroundColor = {25.0F, 35.0F, 54.0F, 245.0F}}) {
        nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), "Neotolis Skeletal Showcase", &(nt_ui_label_style_t){.font_id = 0U, .font_size = 22.0F, .color = {240.0F, 246.0F, 255.0F, 255.0F}});
        nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), "Scene", label_style(13.0F, (Clay_Color){160.0F, 180.0F, 205.0F, 255.0F}));
        char scene_preview[96];
        (void)snprintf(scene_preview, sizeof scene_preview, "%s v", s_scene_registry[s_active_scene].title);
        if (nt_ui_combo_begin(s_ui, NULL, 4U, nt_ui_id("shell/scene_combo"), scene_preview, &s_scene_combo_style, &s_scene_combo_open)) {
            for (uint32_t scene_index = 0U; scene_index < (uint32_t)SKELETAL_SCENE_COUNT; ++scene_index) {
                if (nt_ui_combo_selectable(s_ui, scene_index, s_scene_registry[scene_index].title, (int)scene_index == s_active_scene)) {
                    switch_scene((int)scene_index);
                }
            }
            nt_ui_combo_end(s_ui);
        }
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(1, 1), CLAY_SIZING_FIXED(1)}}}) {}
        if (text_button_fixed(nt_ui_id("shell/controls"), s_show_controls ? "Hide controls" : "Show controls", false, 132.0F, 32.0F)) {
            s_show_controls = !s_show_controls;
            cancel_scene_input();
            cancel_active_scene_input();
        }
    }
}

/* Imported skeletons carry only joint_id hashes; the humanoid keeps its names. */
static const char *joint_name(uint32_t j, char *buf, size_t size) {
    if (s_skeleton_scene.rig_source == RIG_HUMANOID) {
        return s_joint_names[j];
    }
    (void)snprintf(buf, size, "j%02u %08X", (unsigned)j, (unsigned)s_skeleton_scene.view->joint_id[j]);
    return buf;
}

static void declare_rig_combo(void) {
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), "Rig", label_style(13.0F, (Clay_Color){120.0F, 205.0F, 255.0F, 255.0F}));
    char rig_preview[64];
    (void)snprintf(rig_preview, sizeof rig_preview, "%s v", s_rig_names[s_skeleton_scene.rig_source]);
    if (nt_ui_combo_begin(s_ui, NULL, 4U, nt_ui_id("skeleton/rig_combo"), rig_preview, &s_scene_combo_style, &s_skeleton_scene.rig_combo_open)) {
        for (uint32_t rig = 0; rig < (uint32_t)RIG_COUNT; ++rig) {
            if (nt_ui_combo_selectable(s_ui, rig, s_rig_names[rig], rig == (uint32_t)s_skeleton_scene.rig_source) && rig != (uint32_t)s_skeleton_scene.rig_source) {
                select_rig((rig_source_t)rig);
            }
        }
        nt_ui_combo_end(s_ui);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void declare_pose_controls(const nt_skeletal_skeleton_t *skel) {
    const bool sliders_enabled = !s_skip_scene_interaction_this_frame;
    char name_buf[32];
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), "Pose actions", label_style(13.0F, (Clay_Color){120.0F, 205.0F, 255.0F, 255.0F}));
    CLAY({.layout = {.layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 5}}) {
        if (text_button_fixed(nt_ui_id("skeleton/rest"), "Rest", false, 76.0F, 32.0F)) {
            set_rest_pose();
        }
        if (text_button_fixed(nt_ui_id("skeleton/test"), "Test", false, 76.0F, 32.0F)) {
            set_test_pose();
        }
    }
    if (text_button(nt_ui_id("skeleton/axes"), s_skeleton_scene.show_axes ? "Axes: on" : "Axes: off", s_skeleton_scene.show_axes)) {
        s_skeleton_scene.show_axes = !s_skeleton_scene.show_axes;
    }
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), "Selected joint", label_style(13.0F, (Clay_Color){120.0F, 205.0F, 255.0F, 255.0F}));
    char joint_preview[64];
    (void)snprintf(joint_preview, sizeof joint_preview, "%s v", joint_name((uint32_t)s_skeleton_scene.selected_joint, name_buf, sizeof name_buf));
    if (nt_ui_combo_begin(s_ui, NULL, 4U, nt_ui_id("skeleton/joint_combo"), joint_preview, &s_joint_combo_style, &s_skeleton_scene.combo_open)) {
        for (uint32_t j = 0; j < skel->joint_count; ++j) {
            char joint_label[48];
            int depth = 0;
            uint16_t ancestor = skel->parent[j];
            while (ancestor != NT_SKELETAL_NO_PARENT) {
                ++depth;
                ancestor = skel->parent[ancestor];
            }
            (void)snprintf(joint_label, sizeof joint_label, "%*s%s", depth * 2, "", joint_name(j, name_buf, sizeof name_buf));
            if (nt_ui_combo_selectable(s_ui, j, joint_label, (int)j == s_skeleton_scene.selected_joint)) {
                s_skeleton_scene.selected_joint = (int)j;
            }
        }
        nt_ui_combo_end(s_ui);
    }
    const nt_skeletal_mat34_t *m = &s_skeleton_scene.model[s_skeleton_scene.selected_joint];
    const uint16_t parent = skel->parent[s_skeleton_scene.selected_joint];
    char buf[160];
    (void)snprintf(buf, sizeof buf, "Joint: %s", joint_name((uint32_t)s_skeleton_scene.selected_joint, name_buf, sizeof name_buf));
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), buf, label_style(18.0F, (Clay_Color){240.0F, 246.0F, 255.0F, 255.0F}));
    (void)snprintf(buf, sizeof buf, "Parent: %s", parent == NT_SKELETAL_NO_PARENT ? "none" : joint_name(parent, name_buf, sizeof name_buf));
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), buf, label_style(13.0F, (Clay_Color){175.0F, 185.0F, 205.0F, 255.0F}));
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), "Local rotation offset (degrees)", label_style(13.0F, (Clay_Color){120.0F, 205.0F, 255.0F, 255.0F}));
    static const char *const axes[3] = {"X", "Y", "Z"};
    static const uint32_t angle_ids[3] = {0xD31A7E21U, 0x8C42B917U, 0xF0643AC5U};
    for (int axis = 0; axis < 3; ++axis) {
        float degrees = s_skeleton_scene.angles[s_skeleton_scene.selected_joint][axis] * 57.2957795F;
        char label[32];
        (void)snprintf(label, sizeof label, "%s %+03.0f deg", axes[axis], (double)degrees);
        nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), label, label_style(12.0F, (Clay_Color){190.0F, 205.0F, 225.0F, 255.0F}));
        (void)nt_ui_slider_float(s_ui, NT_UI_DATA_LAYER(3), 4, angle_ids[axis], NULL, &degrees, -180.0F, 180.0F, 1.0F, &s_slider_style,
                                 &(const Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(34)}}}, sliders_enabled);
        s_skeleton_scene.angles[s_skeleton_scene.selected_joint][axis] = degrees * 0.0174532925F;
    }
    (void)snprintf(buf, sizeof buf, "Model position: (%.2f, %.2f, %.2f)", (double)m->r[0][3], (double)m->r[1][3], (double)m->r[2][3]);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), buf, label_style(13.0F, (Clay_Color){220.0F, 225.0F, 235.0F, 255.0F}));
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), "Model matrix (3x4)", label_style(13.0F, (Clay_Color){120.0F, 205.0F, 255.0F, 255.0F}));
    for (int row = 0; row < 3; ++row) {
        (void)snprintf(buf, sizeof buf, "[% .3f % .3f % .3f % .3f]", (double)m->r[row][0], (double)m->r[row][1], (double)m->r[row][2], (double)m->r[row][3]);
        nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), buf, label_style(11.0F, (Clay_Color){190.0F, 200.0F, 220.0F, 255.0F}));
    }
}

static void declare_properties(void) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 8}}) {
        declare_rig_combo();
        if (s_skeleton_scene.view != NULL) {
            declare_pose_controls(s_skeleton_scene.view);
        } else {
            nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), "loading...", label_style(14.0F, (Clay_Color){255.0F, 200.0F, 120.0F, 255.0F}));
        }
    }
}

static void declare_player_rig_combo(skinned_scene_state_t *scene) {
    character_player_t *p = &scene->player;
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), "Character", label_style(13.0F, (Clay_Color){120.0F, 205.0F, 255.0F, 255.0F}));
    char rig_preview[64];
    (void)snprintf(rig_preview, sizeof rig_preview, "%s v", s_rig_names[p->rig]);
    if (nt_ui_combo_begin(s_ui, NULL, 4U, nt_ui_id("player/rig_combo"), rig_preview, &s_scene_combo_style, &scene->rig_combo_open)) {
        for (uint32_t rig = RIG_HUMANOID; rig < (uint32_t)RIG_COUNT; ++rig) {
            if (nt_ui_combo_selectable(s_ui, rig, s_rig_names[rig], rig == (uint32_t)p->rig) && rig != (uint32_t)p->rig) {
                player_select_rig(p, (rig_source_t)rig);
            }
        }
        nt_ui_combo_end(s_ui);
    }
}

/* Lists the loaded clips made for the selected rig. */
static void declare_player_clip_combo(skinned_scene_state_t *scene) {
    character_player_t *p = &scene->player;
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), "Clip", label_style(13.0F, (Clay_Color){120.0F, 205.0F, 255.0F, 255.0F}));
    char clip_preview[64];
    (void)snprintf(clip_preview, sizeof clip_preview, "%s v", p->clip >= 0 ? s_clips[p->clip].name : "none");
    if (nt_ui_combo_begin(s_ui, NULL, 4U, nt_ui_id("player/clip_combo"), clip_preview, &s_joint_combo_style, &scene->clip_combo_open)) {
        for (int i = 0; i < CLIP_COUNT; ++i) {
            const nt_skeletal_clip_t *view = player_clip_view(i);
            if (view == NULL) {
                continue;
            }
            if (view->rig_compat_id.value != p->skel->rig_compat_id.value) {
                continue;
            }
            if (nt_ui_combo_selectable(s_ui, (uint32_t)i, s_clips[i].name, i == p->clip) && i != p->clip) {
                player_select_clip(p, i, view);
            }
        }
        nt_ui_combo_end(s_ui);
    }
}

static void declare_player_transport(character_player_t *p) {
    const bool enabled = !s_skip_scene_interaction_this_frame;
    char buf[96];
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), "Transport", label_style(13.0F, (Clay_Color){120.0F, 205.0F, 255.0F, 255.0F}));
    CLAY({.layout = {.layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 5}}) {
        if (!s_cpu_reference && text_button_fixed(nt_ui_id("player/play"), p->paused ? "Play" : "Pause", !p->paused, 76.0F, 32.0F)) {
            p->paused = !p->paused;
        }
        if (text_button_fixed(nt_ui_id("player/step"), "Step", false, 76.0F, 32.0F)) {
            player_step(p);
        }
    }
    (void)snprintf(buf, sizeof buf, "%s  %.3f / %.3f s", p->clip >= 0 ? s_clips[p->clip].name : "no clip", p->track.time, p->track.duration);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), buf, label_style(14.0F, (Clay_Color){240.0F, 246.0F, 255.0F, 255.0F}));
    /* The slider scrubs sample indices; a drag pauses so the clock and the drag do not fight. */
    const int last_frame = p->clip_view != NULL ? clip_last_frame(p) : 0;
    int frame = p->clip_view != NULL ? player_frame(p) : 0;
    if (nt_ui_slider_int(s_ui, NT_UI_DATA_LAYER(3), 4, nt_ui_id("player/time"), NULL, &frame, 0, last_frame > 0 ? last_frame : 1, 1, &s_slider_style,
                         &(const Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(34)}}}, enabled && last_frame > 0)) {
        p->paused = true;
        player_seek_frame(p, frame);
    }
    (void)snprintf(buf, sizeof buf, "Speed x%.2f", (double)p->speed_mag);
    nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), buf, label_style(12.0F, (Clay_Color){190.0F, 205.0F, 225.0F, 255.0F}));
    (void)nt_ui_slider_float(s_ui, NT_UI_DATA_LAYER(3), 4, nt_ui_id("player/speed"), NULL, &p->speed_mag, 0.0F, 2.0F, 0.05F, &s_slider_style,
                             &(const Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(34)}}}, enabled);
    const Clay_ElementDeclaration check_row = {.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(30)}}};
    CLAY({.layout = {.layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 16}}) {
        (void)nt_ui_checkbox(s_ui, NT_UI_DATA_LAYER(3), 4, nt_ui_id("player/loop"), "Loop", &p->loop, &s_checkbox_style, &check_row, enabled);
        (void)nt_ui_checkbox(s_ui, NT_UI_DATA_LAYER(3), 4, nt_ui_id("player/reverse"), "Reverse", &p->reverse, &s_checkbox_style, &check_row, enabled);
    }
}

static void skinned_declare_controls(void) {
    character_player_t *p = &s_skinned_scene.player;
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 8}}) {
        declare_player_rig_combo(&s_skinned_scene);
        if (p->skel != NULL) {
            declare_player_clip_combo(&s_skinned_scene);
            declare_player_transport(p);
        }
        const Clay_ElementDeclaration row = {.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(30)}}};
        (void)nt_ui_checkbox(s_ui, NT_UI_DATA_LAYER(3), 4, nt_ui_id("skinned/reference"), "CPU reference (pauses)", &s_cpu_reference, &s_checkbox_style, &row, true);
        (void)nt_ui_checkbox(s_ui, NT_UI_DATA_LAYER(3), 4, nt_ui_id("skinned/bones"), p->rig == RIG_HUMANOID ? "Bones & marker" : "Bones", &s_show_bones, &s_checkbox_style, &row, true);
        if (p->rig == RIG_HUMANOID) {
            (void)nt_ui_checkbox(s_ui, NT_UI_DATA_LAYER(3), 4, nt_ui_id("skinned/independent"), "Independent clothes", &s_independent_clothes, &s_checkbox_style, &row, true);
        }
    }
}

static void ordering_declare_controls(void) {
    char text[128];
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 8}}) {
        (void)snprintf(text, sizeof text, "Instances: %d", s_order_count);
        nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), text, label_style(14, (Clay_Color){145, 215, 255, 255}));
        if (nt_ui_slider_int(s_ui, NT_UI_DATA_LAYER(3), 4, nt_ui_id("ordering/count"), NULL, &s_order_count, 1, SKELETAL_SHOWCASE_MAX_INSTANCES, 1, &s_slider_style,
                             &(const Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(34)}}}, !s_skip_scene_interaction_this_frame)) {
            s_fit_pending = true;
        }
        if (nt_ui_combo_begin(s_ui, NULL, 4, nt_ui_id("ordering/mode"), s_order_names[s_order_mode], &s_joint_combo_style, &s_order_combo_open)) {
            for (uint32_t i = 0; i < 3; ++i) {
                if (nt_ui_combo_selectable(s_ui, i, s_order_names[i], i == (uint32_t)s_order_mode)) {
                    s_order_mode = (int)i;
                }
            }
            nt_ui_combo_end(s_ui);
        }
        const Clay_ElementDeclaration row = {.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(30)}}};
        (void)nt_ui_checkbox(s_ui, NT_UI_DATA_LAYER(3), 4, nt_ui_id("ordering/shared"), "Shared binding", &s_order_shared, &s_checkbox_style, &row, true);
        (void)nt_ui_checkbox(s_ui, NT_UI_DATA_LAYER(3), 4, nt_ui_id("ordering/passes"), "Two passes", &s_order_two_passes, &s_checkbox_style, &row, true);
        (void)nt_ui_checkbox(s_ui, NT_UI_DATA_LAYER(3), 4, nt_ui_id("ordering/paused"), "Pause", &s_order_paused, &s_checkbox_style, &row, true);
        nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), "Last completed frame", label_style(14, (Clay_Color){145, 215, 255, 255}));
        for (uint32_t pass = 0; pass < s_order_stats.passes; ++pass) {
            (void)snprintf(text, sizeof text, "Pass %u: draws %u / expected %u", pass + 1U, s_order_stats.draws[pass], s_order_stats.expected[pass]);
            nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), text, label_style(12, (Clay_Color){240, 246, 255, 255}));
            (void)snprintf(text, sizeof text, "Instances drawn: %u", s_order_stats.instances[pass]);
            nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), text, label_style(12, (Clay_Color){190, 205, 225, 255}));
        }
        (void)snprintf(text, sizeof text, "Palette builds: %u", s_order_stats.palettes);
        nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), text, label_style(12, (Clay_Color){190, 205, 225, 255}));
        (void)snprintf(text, sizeof text, "Palette bytes: %u", s_order_stats.palettes * HUMANOID_JOINT_COUNT * 48U);
        nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), text, label_style(12, (Clay_Color){190, 205, 225, 255}));
        (void)snprintf(text, sizeof text, "Upload bytes: %u", s_order_stats.palettes * (3U * SKELETAL_SHOWCASE_MAX_PALETTE) * 16U);
        nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), text, label_style(12, (Clay_Color){190, 205, 225, 255}));
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void declare_ui(const nt_ui_scale_t *scale) {
    nt_ui_begin(s_ui, scale->logical_w, scale->logical_h, g_nt_app.dt, &g_nt_input.pointers[0], 1);
    nt_ui_set_viewport(s_ui, nt_ui_viewport_from_scale(scale));
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 10, .padding = CLAY_PADDING_ALL(10)}}) {
        declare_header();
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 10}}) {
            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 5, .padding = CLAY_PADDING_ALL(12)}}) {
                const skeletal_scene_desc_t *scene = &s_scene_registry[s_active_scene];
                nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), scene->title, label_style(18.0F, (Clay_Color){145.0F, 215.0F, 255.0F, 255.0F}));
                nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), scene->description, label_style(13.0F, (Clay_Color){165.0F, 180.0F, 200.0F, 255.0F}));
                nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), "Camera: LMB orbit, RMB pan, wheel zoom", label_style(12.0F, (Clay_Color){150.0F, 170.0F, 195.0F, 255.0F}));
                nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), scene->source, label_style(11.0F, (Clay_Color){120.0F, 170.0F, 205.0F, 255.0F}));
                if (s_order_two_passes && scene->draw == ordering_draw) {
                    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT}}) {
                        CLAY({.layout = {.sizing = {CLAY_SIZING_PERCENT(0.5F), CLAY_SIZING_FIT(0)}}}) {
                            nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), "Pass 1", label_style(13, (Clay_Color){145, 215, 255, 255}));
                        }
                        nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), "Pass 2: one tint material", label_style(13, (Clay_Color){145, 215, 255, 255}));
                    }
                }
                CLAY({.id = CLAY_ID(STAGE_ID), .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}}) {}
            }
            if (s_show_controls) {
                CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(290), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 8, .padding = CLAY_PADDING_ALL(12)},
                      .backgroundColor = {25.0F, 35.0F, 54.0F, 245.0F}}) {
                    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(32)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}}}) {
                        nt_ui_label(s_ui, NT_UI_DATA_LAYER(4), "Controls", label_style(16.0F, (Clay_Color){145.0F, 215.0F, 255.0F, 255.0F}));
                        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1)}}}) {}
                        if (text_button_fixed(nt_ui_id("shell/reset"), "Reset", false, 76.0F, 32.0F)) {
                            reset_active_scene();
                        }
                    }
                    const uint32_t controls_scroll_id = nt_ui_fmix_id(nt_ui_id("shell/controls_scroll"), (uint32_t)s_active_scene);
                    nt_ui_scroll_begin(s_ui, NULL, controls_scroll_id, &s_joint_scroll_style, &(const Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}});
                    s_scene_registry[s_active_scene].declare_controls();
                    nt_ui_scroll_end(s_ui);
                }
            }
        }
    }
    nt_ui_end(s_ui);
}
// #endregion

// #region stage rendering
static void draw_ground(float scale) {
    const float grid[4] = {0.16F, 0.22F, 0.30F, 1.0F};
    nt_shape_renderer_set_line_width(0.02F * scale); /* renderer width is retained across frames */
    const float cell = 0.6F * scale;
    const float half_x = 3.4F * scale;
    const float half_z = 3.0F * scale;
    for (int i = -5; i <= 5; ++i) {
        const float p = (float)i * cell;
        nt_shape_renderer_line((float[3]){-half_x, 0.0F, p}, (float[3]){half_x, 0.0F, p}, grid);
        nt_shape_renderer_line((float[3]){p, 0.0F, -half_z}, (float[3]){p, 0.0F, half_z}, grid);
    }
}

static bool in_selected_subtree(const nt_skeletal_skeleton_t *skel, int selected, uint32_t j) { return selected >= 0 && j >= (uint32_t)selected && j < (uint32_t)skel->subtree_end[selected]; }

static const float s_scaffold_color[4] = {0.45F, 0.50F, 0.58F, 1.0F};

/* Parent->child links whose parent is (or is not) origin scaffolding: those
 * draw thin and grey, real bones in the subtree colours. */
static void draw_links(const nt_skeletal_skeleton_t *skel, const nt_skeletal_mat34_t *model, const bool at_origin[SKELETAL_SHOWCASE_MAX_JOINTS], int selected_joint, bool scaffold_pass, float scale) {
    static const float bone_colors[2][4] = {{0.35F, 0.70F, 0.95F, 1.0F}, {1.0F, 0.65F, 0.18F, 1.0F}};
    nt_shape_renderer_set_line_width((scaffold_pass ? 0.006F : 0.02F) * scale);
    for (uint32_t j = 0; j < skel->joint_count; ++j) {
        const uint16_t p = skel->parent[j];
        if (p == NT_SKELETAL_NO_PARENT || at_origin[p] != scaffold_pass) {
            continue;
        }
        const float a[3] = {model[p].r[0][3], model[p].r[1][3], model[p].r[2][3]};
        const float b[3] = {model[j].r[0][3], model[j].r[1][3], model[j].r[2][3]};
        const float *color = scaffold_pass ? s_scaffold_color : bone_colors[in_selected_subtree(skel, selected_joint, j) ? 1 : 0];
        nt_shape_renderer_line(a, b, color);
    }
}

/* Bones and joint spheres of model[]; selected_joint < 0 highlights nothing. */
static void draw_skeleton(const nt_skeletal_skeleton_t *skel, const nt_skeletal_mat34_t *model, int selected_joint, bool show_axes, float scale) {
    static const float joint_colors[3][4] = {
        {1.0F, 0.9F, 0.2F, 1.0F},
        {1.0F, 0.55F, 0.15F, 1.0F},
        {0.30F, 0.85F, 0.95F, 1.0F},
    };
    /* The link from origin scaffolding up to the first translated joint would
     * read as a limb, so those joints and links draw thin and grey. */
    bool at_origin[SKELETAL_SHOWCASE_MAX_JOINTS];
    rig_at_origin(skel, at_origin);
    draw_links(skel, model, at_origin, selected_joint, true, scale);
    draw_links(skel, model, at_origin, selected_joint, false, scale);
    for (uint32_t j = 0; j < skel->joint_count; ++j) {
        const float p[3] = {model[j].r[0][3], model[j].r[1][3], model[j].r[2][3]};
        const float *color;
        float radius = 0.075F;
        if ((int)j == selected_joint) {
            color = joint_colors[0];
            radius = 0.105F;
        } else if (at_origin[j]) {
            color = s_scaffold_color;
            radius = 0.04F;
        } else if (in_selected_subtree(skel, selected_joint, j)) {
            color = joint_colors[1];
        } else {
            color = joint_colors[2];
        }
        nt_shape_renderer_sphere(p, radius * scale, color);
        if (show_axes) {
            const float axis_colors[3][4] = {{1.0F, 0.2F, 0.2F, 1.0F}, {0.2F, 1.0F, 0.3F, 1.0F}, {0.2F, 0.5F, 1.0F, 1.0F}};
            const float axis_len = 0.23F * scale;
            for (int axis = 0; axis < 3; ++axis) {
                const float end[3] = {p[0] + (model[j].r[0][axis] * axis_len), p[1] + (model[j].r[1][axis] * axis_len), p[2] + (model[j].r[2][axis] * axis_len)};
                nt_shape_renderer_line(p, end, axis_colors[axis]);
            }
        }
    }
}

static void skeleton_draw(void) {
    const nt_skeletal_skeleton_t *skel = s_skeleton_scene.view;
    if (skel == NULL) {
        return;
    }
    draw_ground(s_fit_scale);
    draw_skeleton(skel, s_skeleton_scene.model, s_skeleton_scene.selected_joint, s_skeleton_scene.show_axes, s_fit_scale);
}

/* Part `part` of `count` equal side-by-side stage rectangles, all with the same
 * camera and one projection for their aspect ratio. The shell restores the full UI viewport. */
static void stage_viewport(uint32_t part, uint32_t count) {
    const nt_ui_viewport_t viewport = nt_ui_viewport_from_scale(&s_ui_scale);
    const int stage_width = (int)(s_stage_bbox.width * s_ui_scale.scale_x) / (int)count;
    const int stage_height = (int)(s_stage_bbox.height * s_ui_scale.scale_y);
    const int width = stage_width > 0 ? stage_width : 1; /* a collapsed stage still gets a finite projection */
    const int height = stage_height > 0 ? stage_height : 1;
    const int x = (int)(viewport.x + (s_stage_bbox.x * s_ui_scale.scale_x)) + ((int)part * width);
    const int y = (int)g_nt_window.fb_height - (int)(viewport.y + ((s_stage_bbox.y + s_stage_bbox.height) * s_ui_scale.scale_y));
    nt_gfx_set_viewport(x, y, width, height);
    nt_gfx_set_scissor(x, y, width, height);
    mat4 vp;
    make_camera_vp(vp, (float)width / (float)height, s_frame_uniforms.camera_pos);
    memcpy(s_frame_uniforms.view_proj, vp, sizeof vp);
    nt_gfx_update_buffer(s_frame_ubo, 0, &s_frame_uniforms, sizeof s_frame_uniforms);
    nt_gfx_bind_uniform_buffer(s_frame_ubo, 0);
    nt_shape_renderer_set_vp((const float *)vp);
    nt_shape_renderer_set_cam_pos(s_frame_uniforms.camera_pos);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void skinned_draw(void) {
    const character_player_t *p = &s_skinned_scene.player;
    const bool humanoid = p->rig == RIG_HUMANOID;
    if (p->skel == NULL || (!humanoid && !nt_resource_is_ready(s_skin_resource[p->rig])) || g_nt_gfx.context_lost || s_stage_bbox.height <= 1.0F) {
        return;
    }
    const nt_skin_binding_t *skins[2] = {humanoid ? &s_humanoid_skin[0] : nt_skeletal_assets_skin_binding(s_skin_resource[p->rig]), &s_humanoid_skin[1]};
    const uint32_t count = humanoid ? 2U : 1U;
    const nt_skeletal_mat34_t *models[2] = {p->model, s_independent_clothes ? s_clothes_model : p->model};
    cpu_mesh_t *sources[2] = {&s_cpu_mesh[p->rig], &s_cpu_mesh[CPU_CLOTHES]};
    const uint32_t textures[2] = {(uint32_t)p->rig, RIG_COUNT};
    for (uint32_t i = 0; i < CPU_SOURCE_COUNT; ++i) {
        cpu_mesh_t *source = &s_cpu_mesh[i];
        if (source != sources[0] && (!humanoid || source != sources[1]) && source->reference.id != 0) {
            nt_gfx_deactivate_mesh(source->reference.id);
            source->reference = NT_MESH_INVALID;
        }
    }
    const bool cpu = s_cpu_reference;
    for (uint32_t i = 0; i < count; ++i) {
        if (cpu && sources[i]->data != NULL) {
            deform_cpu_mesh(sources[i], skins[i], models[i], p->skel->joint_count);
        }
        const nt_entity_t e = s_mesh_entities[i];
        const float angle = humanoid ? 0.3F : 0.0F;
        const float q[4] = {0, sinf(angle * 0.5F), 0, cosf(angle * 0.5F)};
        nt_transform_comp_set_rotation(e, q);
        nt_transform_comp_set_position(e, humanoid ? 0.2F : 0, 0, humanoid ? 0.1F : 0);
        const float scale = humanoid ? 1.05F : 1.0F;
        nt_transform_comp_set_scale(e, scale, scale, scale);
    }
    nt_transform_comp_update();
    nt_render_item_t items[2];
    uint32_t ready = 0;
    if (!cpu) {
        nt_skeletal_gpu_begin_frame();
    }
    for (uint32_t i = 0; i < count; ++i) {
        const nt_mesh_t gpu_mesh = humanoid ? s_procedural_mesh[i] : (nt_mesh_t){nt_resource_get(s_mesh_resource[p->rig])};
        const nt_mesh_t mesh = cpu ? sources[i]->reference : gpu_mesh;
        const nt_material_t material = cpu ? s_static_material[textures[i]] : s_skin_material[textures[i]];
        if ((!cpu && !humanoid && !nt_resource_is_ready(s_mesh_resource[p->rig])) || mesh.id == 0 || !nt_resource_is_ready(s_texture_resource[textures[i]]) ||
            !nt_gfx_program_ready(nt_material_get_info(material)->program)) {
            continue;
        }
        const nt_entity_t e = s_mesh_entities[i];
        *nt_mesh_comp_handle(e) = mesh;
        *nt_material_comp_handle(e) = material;
        items[ready++] = (nt_render_item_t){.entity = e.id, .batch_key = nt_mesh_renderer_batch_key(material, mesh)};
        if (!cpu) {
            nt_skeletal_mat34_t *palette = nt_skeletal_gpu_reserve(skins[i]->palette_count, nt_skin_comp_handle(e));
            nt_skin_palette_build(skins[i], models[i], p->skel->joint_count, palette, skins[i]->palette_count);
        }
    }
    if (cpu) {
        nt_mesh_renderer_draw_list(items, ready);
    } else {
        nt_skeletal_gpu_flush();
        nt_skinned_mesh_renderer_draw_list(items, ready);
    }
    if (s_show_bones) {
        nt_shape_renderer_set_depth(false); /* bones are an overlay: most sit inside the mesh */
        nt_skeletal_mat34_t world;
        nt_skeletal_mat34_t bones[SKELETAL_SHOWCASE_MAX_JOINTS];
        nt_skeletal_mat34_from_mat4(nt_transform_comp_world_matrix(s_mesh_entities[0]), &world);
        for (uint16_t j = 0; j < p->skel->joint_count; ++j) {
            nt_skeletal_mat34_mul(&world, &p->model[j], &bones[j]);
        }
        draw_skeleton(p->skel, bones, -1, false, s_fit_scale);
        if (humanoid) {
            const float point[3] = {0.2F, 0.1F, 0.15F};
            float marker[3];
            for (uint32_t r = 0; r < 3; ++r) {
                marker[r] = bones[8].r[r][3];
                for (uint32_t k = 0; k < 3; ++k) {
                    marker[r] += bones[8].r[r][k] * point[k];
                }
            }
            nt_shape_renderer_sphere(marker, 0.11F, (float[4]){1, 0.2F, 0.2F, 1});
        }
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void ordering_draw(void) {
    if (g_nt_gfx.context_lost || s_stage_bbox.height <= 1.0F || !nt_resource_is_ready(s_texture_resource[RIG_HUMANOID]) || !nt_resource_is_ready(s_texture_resource[RIG_COUNT]) ||
        !nt_gfx_program_ready(s_skin_program.program)) {
        return;
    }
    const uint32_t count = (uint32_t)s_order_count;
    const uint32_t passes = s_order_two_passes ? 2U : 1U;
    s_order_stats.passes = passes;
    s_order_stats.palettes = 0;
    nt_skeletal_gpu_begin_frame();
    for (uint32_t i = 0; i < count; ++i) {
        nt_deformation_binding_t *binding = nt_skin_comp_handle(s_order_entities[i]);
        if (s_order_shared && i > 0) {
            *binding = *nt_skin_comp_handle(s_order_entities[0]);
        } else {
            nt_skeletal_trs_t local[HUMANOID_JOINT_COUNT];
            nt_skeletal_mat34_t model[HUMANOID_JOINT_COUNT];
            nt_skeletal_sample(&s_humanoid_clip, s_order_tracks[i].time, local);
            nt_skeletal_fk(&s_humanoid, local, model, 0, HUMANOID_JOINT_COUNT);
            nt_skeletal_mat34_t *palette = nt_skeletal_gpu_reserve(HUMANOID_JOINT_COUNT, binding);
            nt_skin_palette_build(&s_humanoid_skin[0], model, HUMANOID_JOINT_COUNT, palette, HUMANOID_JOINT_COUNT);
            ++s_order_stats.palettes;
        }
    }
    nt_skeletal_gpu_flush();
    for (uint32_t pass = 0; pass < passes; ++pass) {
        nt_render_item_t items[SKELETAL_SHOWCASE_MAX_INSTANCES];
        for (uint32_t i = 0; i < count; ++i) {
            const nt_entity_t e = s_order_entities[i];
            const nt_mesh_t mesh = s_procedural_mesh[s_order_mode == 1 && (i % 2U) != 0U ? 2 : 0];
            const nt_material_t material = s_skin_material[pass == 1 || (s_order_mode == 2 && (i % 2U) != 0U) ? RIG_COUNT : RIG_HUMANOID];
            *nt_mesh_comp_handle(e) = mesh;
            *nt_material_comp_handle(e) = material;
            items[i] = (nt_render_item_t){.entity = e.id, .batch_key = nt_mesh_renderer_batch_key(material, mesh)};
        }
        stage_viewport(pass, passes);
        const uint32_t draws_before = nt_gfx_draw_calls(&g_nt_gfx.counters);
        const uint64_t instances_before = g_nt_gfx.counters.instances;
        nt_skinned_mesh_renderer_draw_list(items, count);
        s_order_stats.draws[pass] = nt_gfx_draw_calls(&g_nt_gfx.counters) - draws_before;
        s_order_stats.instances[pass] = (uint32_t)(g_nt_gfx.counters.instances - instances_before);
        s_order_stats.expected[pass] = s_order_mode == 0 || (pass == 1 && s_order_mode == 2) ? 1U : count;
    }
}

static void end_stage(void) {
    nt_shape_renderer_flush();
    nt_gfx_set_scissor_enabled(false);
    nt_gfx_set_viewport(0, 0, (int)g_nt_window.fb_width, (int)g_nt_window.fb_height);
}
// #endregion

// #region scene registry callbacks
static void skeleton_cancel_input(void) {
    s_skeleton_scene.combo_open = false;
    s_skeleton_scene.rig_combo_open = false;
}

static void skinned_cancel_input(void) {
    s_skinned_scene.rig_combo_open = false;
    s_skinned_scene.clip_combo_open = false;
}

static void switch_scene(int next_scene) {
    if (next_scene == s_active_scene) {
        return;
    }
    NT_ASSERT(next_scene >= 0 && next_scene < SKELETAL_SCENE_COUNT && "switch_scene: invalid scene index");
    cancel_active_scene_input();
    cancel_scene_input();
    reset_camera();
    s_active_scene = next_scene;
    s_fit_pending = true;

    s_skip_scene_interaction_this_frame = true;
}

static void reset_active_scene(void) {
    s_scene_registry[s_active_scene].reset();
    reset_camera();
    cancel_scene_input();
    cancel_active_scene_input();
    s_skip_scene_interaction_this_frame = true;
}
// #endregion

// #region frame and init
/* Both packs come from the same CDN folder or the local assets dir. */
static void mount_pack(const char *name) {
    char path[256];
#ifdef NT_CDN_URL
    const int len = snprintf(path, sizeof path, "%s/skeletal_showcase/%s.ntpack", NT_CDN_URL, name);
#else
    const int len = snprintf(path, sizeof path, "assets/%s.ntpack", name);
#endif
    NT_ASSERT(len > 0 && len < (int)sizeof path && "skeletal_showcase: pack path truncated");
    const nt_hash32_t pack_id = nt_hash32_str(name);
    (void)nt_resource_mount(pack_id, 100);
    (void)nt_resource_load_auto(pack_id, path);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void frame(void) {
    nt_window_poll();
    nt_gfx_begin_tick();
    if (g_nt_gfx.context_restored) {
        nt_resource_invalidate(NT_ASSET_TEXTURE);
        nt_resource_invalidate(NT_ASSET_FONT);
        nt_resource_invalidate(NT_ASSET_MESH);
        nt_gfx_destroy_buffer(s_frame_ubo);
        s_frame_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_UNIFORM, .usage = NT_USAGE_DYNAMIC, .size = sizeof s_frame_uniforms, .label = "skeletal_frame_uniforms"});
        restore_mesh_scene();
        nt_shape_renderer_restore_gpu();
        (void)nt_sprite_renderer_restore_gpu();
        (void)nt_text_renderer_restore_gpu();
        nt_program_ref_drop(&s_sprite_program);
        nt_program_ref_drop(&s_text_program);
        nt_resource_invalidate(NT_ASSET_SHADER_CODE);
        s_atlas_bound = false;
    }
#ifdef NT_DEVAPI_ENABLED
    nt_devapi_update();
#endif
    nt_input_poll();
    nt_mem_scratch_reset();
#ifndef NT_PLATFORM_WEB
    const bool modal_was_active = nt_ui_modal_active(s_ui);
    if (!modal_was_active && nt_input_key_is_pressed(NT_KEY_ESCAPE)) {
        nt_app_quit();
    }
#endif
    nt_resource_step();
    refresh_views();
    link_programs();
    try_bind_resources();
    if (nt_input_key_is_pressed(NT_KEY_R)) {
        reset_active_scene();
    }

    const float fb_w = (float)(g_nt_window.fb_width > 0 ? g_nt_window.fb_width : 800);
    const float fb_h = (float)(g_nt_window.fb_height > 0 ? g_nt_window.fb_height : 600);
    s_frame_uniforms = (nt_frame_uniforms_t){
        .resolution = {fb_w, fb_h, 1.0F / fb_w, 1.0F / fb_h},
        .time = {0.0F, g_nt_app.dt},
        .near_far = {CAMERA_NEAR * s_fit_scale, CAMERA_FAR * s_fit_scale},
    };

    nt_gfx_begin_frame();
    nt_font_step();
    const bool render_enabled = nt_app_render_enabled();
    if (render_enabled) {
        nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.035F, 0.05F, 0.08F, 1.0F}, .clear_depth = 1.0F});
    }

    const nt_material_info_t *sprite_info = nt_material_get_info(s_sprite_material);
    const nt_material_info_t *text_info = nt_material_get_info(s_text_material);
    const bool ready = s_atlas_bound && s_font_bound && sprite_info != NULL && text_info != NULL && nt_gfx_program_ready(sprite_info->program) && nt_gfx_program_ready(text_info->program);
    if (ready) {
        const float css_w = g_nt_window.width > 0U ? (float)g_nt_window.width : fb_w;
        const float css_h = g_nt_window.height > 0U ? (float)g_nt_window.height : fb_h;
        nt_ui_scale_desc_t scale_desc = {.ref_w = 800.0F, .ref_h = 600.0F, .mode = NT_UI_SCALE_EXPAND};
        s_ui_scale = nt_ui_compute_scale(&scale_desc, css_w, css_h);
        const float dpr_x = css_w > 0.0F ? fb_w / css_w : 1.0F;
        const float dpr_y = css_h > 0.0F ? fb_h / css_h : 1.0F;
        s_ui_scale.scale_x *= dpr_x;
        s_ui_scale.scale_y *= dpr_y;
        s_ui_scale.fb_w = fb_w;
        s_ui_scale.fb_h = fb_h;
        declare_ui(&s_ui_scale);
    }
    s_scene_registry[s_active_scene].update();
    if (ready) {
        s_stage_bbox = nt_ui_get_bbox(s_ui, nt_ui_id(STAGE_ID));
        const bool split = s_order_two_passes && s_scene_registry[s_active_scene].draw == ordering_draw;
        const float camera_width = s_stage_bbox.width / (split ? 2.0F : 1.0F);
        const bool stage_resized = fabsf(s_camera_fit_width - camera_width) > 0.5F || fabsf(s_camera_fit_height - s_stage_bbox.height) > 0.5F;
        if (stage_resized && s_stage_bbox.found && s_stage_bbox.height > 1.0F) {
            fit_camera_to_stage(camera_width, s_stage_bbox.height);
            s_camera_fit_width = camera_width;
            s_camera_fit_height = s_stage_bbox.height;
        }
        update_stage_camera(&g_nt_input.pointers[0], &s_ui_scale);
        if (render_enabled) {
            nt_gfx_set_scissor_enabled(true);
            nt_shape_renderer_set_depth(true);
            stage_viewport(0, 1);
            s_scene_registry[s_active_scene].draw();
            end_stage();

            nt_ui_make_screen_view_proj(s_ui_scale.logical_w, s_ui_scale.logical_h, s_frame_uniforms.view_proj);
            nt_gfx_update_buffer(s_frame_ubo, 0, &s_frame_uniforms, sizeof s_frame_uniforms);
            nt_gfx_bind_uniform_buffer(s_frame_ubo, 0);
            nt_ui_target_t target = nt_ui_scale_make_target(&s_ui_scale);
            nt_ui_walk(s_ui, &target);
            nt_sprite_renderer_flush();
            nt_text_renderer_flush();
        }
    }
    if (render_enabled) {
        nt_gfx_end_pass();
    }
    nt_gfx_end_frame();
    if (render_enabled) {
        nt_window_swap_buffers();
    }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    nt_engine_config_t config = {.app_name = "skeletal_showcase", .version = 1};
    if (nt_engine_init(&config) != NT_OK) {
        return 1;
    }
    g_nt_window.width = 1280;
    g_nt_window.height = 800;
    nt_window_init();
    nt_input_init();
    nt_gfx_desc_t gfx_desc = nt_gfx_desc_defaults();
    gfx_desc.depth = true;
    gfx_desc.max_meshes = 8;
    gfx_desc.max_vertex_inputs = 112;
    gfx_desc.max_shaders = 32;
    gfx_desc.max_programs = 16;
    gfx_desc.max_pipelines = 32;
    gfx_desc.max_buffers = 128;
    gfx_desc.max_textures = 16;
    nt_gfx_init(&gfx_desc);
    nt_gfx_register_global_block("Globals", 0);
    nt_http_init();
#ifndef NT_PLATFORM_WEB
    nt_fs_init();
#endif
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_resource_init(&(nt_resource_desc_t){0});
    nt_mem_scratch_init(SCRATCH_ARENA_SIZE);
    nt_resource_register_type(NT_ASSET_TEXTURE, &(nt_resource_type_desc_t){.activate = nt_gfx_activate_texture, .deactivate = nt_gfx_deactivate_texture});
    nt_resource_register_type(NT_ASSET_MESH, &(nt_resource_type_desc_t){.activate = nt_gfx_activate_mesh, .deactivate = nt_gfx_deactivate_mesh, .on_post_resolve = on_mesh_published});
    nt_resource_register_type(NT_ASSET_SHADER_CODE, &(nt_resource_type_desc_t){.activate = nt_gfx_activate_shader, .deactivate = nt_gfx_deactivate_shader});
    /* Capacity counts every skeletal asset of the mounted packs: an NSKL and an NSKN per imported rig plus the clips. */
    nt_skeletal_assets_init((uint32_t)((2 * (RIG_COUNT - 1)) + CLIP_COUNT - 1));
    nt_resource_register_type(NT_ASSET_SKELETON, &(nt_resource_type_desc_t){.activate = nt_skeletal_assets_activate_skeleton, .deactivate = nt_skeletal_assets_deactivate_skeleton});
    nt_resource_register_type(NT_ASSET_SKIN_BINDING, &(nt_resource_type_desc_t){.activate = nt_skeletal_assets_activate_skin_binding, .deactivate = nt_skeletal_assets_deactivate_skin_binding});
    nt_resource_register_type(NT_ASSET_CLIP, &(nt_resource_type_desc_t){.activate = nt_skeletal_assets_activate_clip, .deactivate = nt_skeletal_assets_deactivate_clip});
    nt_atlas_init();
    nt_material_init(&(nt_material_desc_t){.max_materials = 16});
    nt_font_init(&(nt_font_desc_t){.max_fonts = 1});
    nt_shape_renderer_init();
    nt_sprite_renderer_desc_t sprite_desc = nt_sprite_renderer_desc_defaults();
    nt_sprite_renderer_init(&sprite_desc);
    nt_text_renderer_init();
    nt_ui_module_init();
    nt_ui_create_desc_t ui_desc = nt_ui_create_desc_defaults();
    ui_desc.max_elements = 1024;
    s_ui = nt_ui_create_context(s_ui_arena, sizeof s_ui_arena, &ui_desc);
    NT_ASSERT(s_ui != NULL);
    s_frame_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_UNIFORM, .usage = NT_USAGE_DYNAMIC, .size = sizeof(nt_frame_uniforms_t), .label = "skeletal_frame_uniforms"});

    mount_pack("skeletal_showcase");
    mount_pack("skeletal_showcase_clips");
    s_sprite_program.vs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SPRITE_VERT, NT_ASSET_SHADER_CODE);
    s_sprite_program.fs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SPRITE_FRAG, NT_ASSET_SHADER_CODE);
    s_text_program.vs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_VERT, NT_ASSET_SHADER_CODE);
    s_text_program.fs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_FRAG, NT_ASSET_SHADER_CODE);
    s_atlas = nt_resource_request(ASSET_ATLAS_SKELETAL_SHOWCASE_UI, NT_ASSET_ATLAS);
    s_atlas_texture = nt_resource_request(ASSET_TEXTURE_SKELETAL_SHOWCASE_UI_TEX0, NT_ASSET_TEXTURE);
    s_font_resource = nt_resource_request(ASSET_FONT_SKELETAL_SHOWCASE_FONT, NT_ASSET_FONT);
    s_rig_resource[RIG_FOX] = nt_resource_request(ASSET_SKELETON_SKELETAL_SHOWCASE_FOX_NSKL, NT_ASSET_SKELETON);
    s_rig_resource[RIG_CESIUMMAN] = nt_resource_request(ASSET_SKELETON_SKELETAL_SHOWCASE_CESIUMMAN_NSKL, NT_ASSET_SKELETON);
    for (int i = 0; i < CLIP_COUNT; ++i) {
        if (s_clips[i].id.value != 0) {
            s_clip_resource[i] = nt_resource_request(s_clips[i].id, NT_ASSET_CLIP);
        }
    }
    s_sprite_material = nt_material_create(&(nt_material_create_desc_t){
        .textures = {{.name = "u_texture", .resource = s_atlas_texture}}, .texture_count = 1, .blend = nt_blend_alpha_premultiplied(), .cull_mode = NT_CULL_NONE, .label = "skeletal_showcase_sprite"});
    s_text_material = nt_material_create(&(nt_material_create_desc_t){.blend = nt_blend_alpha_premultiplied(),
                                                                      .cull_mode = NT_CULL_NONE,
                                                                      .params[0] = {.name = "u_alpha_cutoff", .value = {NT_TEXT_ALPHA_CUTOFF_DEFAULT}},
                                                                      .param_count = 1,
                                                                      .label = "skeletal_showcase_text"});
    s_font = nt_font_create(&(nt_font_create_desc_t){.curve_texture_width = 1024, .curve_texture_height = 512, .band_texture_height = 256, .band_count = 8, .measure_cache_size = 256});
    nt_ui_set_sprite_material(s_ui, s_sprite_material);
    nt_ui_set_text_material(s_ui, s_text_material);

#ifdef NT_DEVAPI_ENABLED
    if (nt_devapi_init() != NT_OK) {
        nt_log_error("skeletal_showcase: devapi init failed");
        return 1;
    }
    nt_devapi_register_default();
#ifdef NT_DEVAPI_GROUP_UI
    nt_devapi_ui_register_context("showcase", s_ui);
#endif
#ifndef NT_PLATFORM_WEB
    if (!nt_devapi_net_start((uint16_t)NT_DEVAPI_DEFAULT_PORT)) {
        nt_log_error("skeletal_showcase: devapi port %u unavailable", (unsigned)NT_DEVAPI_DEFAULT_PORT);
        nt_devapi_shutdown();
        return 1;
    }
#else
    nt_devapi_web_install_shim();
#endif
#ifdef NT_DEVAPI_GROUP_CAPTURE
    nt_devapi_capture_install_seam();
#endif
#endif

    init_humanoid();
    init_mesh_scene();
    ordering_reset();
    init_ui_styles();
    s_skinned_scene.player.rig = RIG_FOX;
    skinned_reset();
    switch_scene(0);
#ifdef NT_PLATFORM_WEB
    nt_platform_web_loading_complete();
#endif
    nt_app_run(frame);

#ifndef NT_PLATFORM_WEB
#ifdef NT_DEVAPI_ENABLED
    nt_devapi_net_stop();
    nt_devapi_shutdown();
#endif
    nt_ui_destroy_context(s_ui);
    nt_ui_module_shutdown();
    nt_text_renderer_shutdown();
    nt_sprite_renderer_shutdown();
    nt_shape_renderer_shutdown();
    nt_font_destroy(s_font);
    nt_font_shutdown();
    shutdown_mesh_scene();
    nt_material_destroy(s_sprite_material);
    nt_material_destroy(s_text_material);
    nt_material_shutdown();
    nt_mem_scratch_shutdown();
    nt_resource_shutdown();
    nt_skeletal_assets_shutdown();
    nt_fs_shutdown();
    nt_http_shutdown();
    nt_hash_shutdown();
    nt_gfx_destroy_buffer(s_frame_ubo);
    nt_gfx_shutdown();
    nt_input_shutdown();
    nt_window_shutdown();
    nt_engine_shutdown();
#endif
    return 0;
}
// #endregion
