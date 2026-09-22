/* Build the Skeletal showcase packs: the rig pack carries the UI atlas, font
 * and both Khronos rigs (skeleton, skin binding, skinned mesh); the clips pack
 * carries every clip, so the showcase plays clips from one pack on a skeleton
 * mounted from another. The font is reused from ui_showcase and is distributed
 * under Apache 2.0. */

/* clang-format off */
#include "nt_builder.h"
/* clang-format on */

#include "showcase_limits.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define MKDIR(path) mkdir(path, 0755)
#endif

#define HEADER_DIR "examples/skeletal_showcase/generated"
#define FONT_PATH "examples/ui_showcase/raw/font.ttf"
#define SAMPLE_FPS 24.0F

static char s_path[512];

static const char *pack_path(const char *dir, const char *name) {
    (void)snprintf(s_path, sizeof s_path, "%s/%s", dir, name);
    return s_path;
}

/* One clip of a character: the glTF animation name (NULL = the unnamed one)
 * and the resource id it ships under. */
typedef struct {
    const char *animation;
    const char *resource_id;
} clip_desc_t;

typedef struct {
    const char *glb_path;
    const char *skeleton_id;
    const char *binding_id;
    const char *mesh_id;
    bool has_normal; /* the scene API asserts on a layout stream the primitive lacks */
    const clip_desc_t *clips;
    uint32_t clip_count;
} character_desc_t;

/* Joint indices fit UINT8; TEXCOORD is omitted because the showcase only draws bones. */
static uint32_t skinned_layout(NtStreamLayout out[4], bool has_normal) {
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
 * export walks. */
static uint32_t skinned_node(const nt_glb_scene_t *scene, uint32_t skin_index) {
    for (uint32_t i = 0; i < scene->node_count; i++) {
        if (scene->nodes[i].skin_index == skin_index && scene->nodes[i].mesh_index != UINT32_MAX) {
            return i;
        }
    }
    NT_BUILD_ASSERT(0 && "skeletal_showcase: no node instantiates a mesh with the rig's skin");
    return UINT32_MAX;
}

static void print_clip_report(const char *resource_id, const nt_builder_clip_report_t *report) {
    (void)printf("  clip %s: sample_count=%u duration=%.6f cpu_error_lin=%.6f (t=%.6f joint=%u) cpu_error_t=%.6f (t=%.6f joint=%u)\n", resource_id, report->sample_count, (double)report->duration,
                 (double)report->cpu_error_lin, report->worst_time_lin, report->worst_joint_lin, (double)report->cpu_error_t, report->worst_time_t, report->worst_joint_t);
}

/* The rig of skin 0 up to the scene root is imported once and feeds every
 * export of the character: skeleton, binding and primitive 0 of the skinned
 * mesh into rig_ctx, the clips into clip_ctx. */
static void add_character(NtBuilderContext *rig_ctx, NtBuilderContext *clip_ctx, const character_desc_t *desc) {
    nt_glb_scene_t scene;
    const nt_build_result_t parsed = nt_builder_parse_glb_scene(&scene, desc->glb_path);
    NT_BUILD_ASSERT(parsed == NT_BUILD_OK && "skeletal_showcase: glb parse failed");
    nt_builder_rig_t rig;
    nt_builder_import_rig(&scene, 0, &rig);
    NT_BUILD_ASSERT(rig.skeleton.joint_count <= SKELETAL_SHOWCASE_MAX_JOINTS && "skeletal_showcase: rig exceeds the pose buffers of main.c");
    nt_builder_add_skeleton(rig_ctx, &rig.skeleton, desc->skeleton_id);
    nt_builder_add_scene_skin_binding(rig_ctx, &rig, desc->binding_id);

    NtStreamLayout layout[4];
    const uint32_t stream_count = skinned_layout(layout, desc->has_normal);
    const nt_mesh_opts_t mesh_opts = {.layout = layout, .stream_count = stream_count, .tangent_mode = NT_TANGENT_AUTO};
    const uint32_t mesh = scene.nodes[skinned_node(&scene, rig.skin_index)].mesh_index;
    nt_builder_add_scene_skinned_mesh(rig_ctx, &rig, mesh, 0, NT_BUILDER_SKIN_DROP_TOLERANCE, desc->mesh_id, &mesh_opts);

    for (uint32_t c = 0; c < desc->clip_count; c++) {
        nt_builder_clip_report_t report;
        nt_builder_add_scene_clip(clip_ctx, &scene, desc->clips[c].animation, &rig, SAMPLE_FPS, desc->clips[c].resource_id, &report);
        print_clip_report(desc->clips[c].resource_id, &report);
    }

    nt_builder_free_rig(&rig);
    nt_builder_free_glb_scene(&scene);
}

static const clip_desc_t k_fox_clips[] = {
    {"Survey", "skeletal_showcase/fox/survey.nanm"},
    {"Walk", "skeletal_showcase/fox/walk.nanm"},
    {"Run", "skeletal_showcase/fox/run.nanm"},
};

static const clip_desc_t k_cesiumman_clips[] = {
    {NULL, "skeletal_showcase/cesiumman.nanm"},
};

static const character_desc_t k_fox = {
    .glb_path = "examples/skeletal_showcase/raw/Fox.glb",
    .skeleton_id = "skeletal_showcase/fox.nskl",
    .binding_id = "skeletal_showcase/fox.nskn",
    .mesh_id = "skeletal_showcase/fox.mesh",
    .has_normal = false,
    .clips = k_fox_clips,
    .clip_count = (uint32_t)(sizeof k_fox_clips / sizeof k_fox_clips[0]),
};

static const character_desc_t k_cesiumman = {
    .glb_path = "examples/skeletal_showcase/raw/CesiumMan.glb",
    .skeleton_id = "skeletal_showcase/cesiumman.nskl",
    .binding_id = "skeletal_showcase/cesiumman.nskn",
    .mesh_id = "skeletal_showcase/cesiumman.mesh",
    .has_normal = true,
    .clips = k_cesiumman_clips,
    .clip_count = (uint32_t)(sizeof k_cesiumman_clips / sizeof k_cesiumman_clips[0]),
};

static bool finish(NtBuilderContext *ctx, const char *name) {
    const nt_build_result_t result = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);
    if (result != NT_BUILD_OK) {
        (void)fprintf(stderr, "%s failed: %d\n", name, result);
        return false;
    }
    return true;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        (void)fprintf(stderr, "Usage: build_skeletal_showcase_packs <pack_dir>\n");
        return 1;
    }

    const char *out_dir = argv[1];
    (void)printf("=== Build Skeletal showcase packs -> %s ===\n\n", out_dir);
    (void)MKDIR(out_dir);
    (void)MKDIR(HEADER_DIR);

    char cache_dir[512];
    (void)snprintf(cache_dir, sizeof cache_dir, "%s/_cache", out_dir);
    (void)MKDIR(cache_dir);

    NtBuilderContext *rig_ctx = nt_builder_start_pack(pack_path(out_dir, "skeletal_showcase.ntpack"));
    if (rig_ctx == NULL) {
        (void)fprintf(stderr, "Failed to start skeletal_showcase.ntpack\n");
        return 1;
    }
    nt_builder_set_header_dir(rig_ctx, HEADER_DIR);
    nt_builder_set_cache_dir(rig_ctx, cache_dir);
    nt_builder_set_threads_auto(rig_ctx);

    /* No cache dir: the cache serves atlas and texture work only. */
    NtBuilderContext *clip_ctx = nt_builder_start_pack(pack_path(out_dir, "skeletal_showcase_clips.ntpack"));
    if (clip_ctx == NULL) {
        (void)fprintf(stderr, "Failed to start skeletal_showcase_clips.ntpack\n");
        nt_builder_free_pack(rig_ctx);
        return 1;
    }
    nt_builder_set_header_dir(clip_ctx, HEADER_DIR);

    nt_builder_add_shader(rig_ctx, "assets/shaders/sprite.vert", NT_BUILD_SHADER_VERTEX);
    nt_builder_add_shader(rig_ctx, "assets/shaders/sprite.frag", NT_BUILD_SHADER_FRAGMENT);
    nt_builder_add_shader(rig_ctx, "assets/shaders/slug_text.vert", NT_BUILD_SHADER_VERTEX);
    nt_builder_add_shader(rig_ctx, "assets/shaders/slug_text.frag", NT_BUILD_SHADER_FRAGMENT);

    nt_atlas_opts_t atlas_opts = nt_atlas_opts_defaults();
    atlas_opts.shape = NT_ATLAS_SHAPE_RECT;
    atlas_opts.allowed_transforms = NT_ATLAS_TRANSFORMS_IDENTITY;
    atlas_opts.padding = 1;
    atlas_opts.margin = 1;
    atlas_opts.extrude = 1;
    atlas_opts.premultiplied = true;
    atlas_opts.compress = (nt_basisu_encode_opts_t){0};
    atlas_opts.gen_mipmaps = true;
    NtAtlasBuild *atlas = nt_atlas_begin(rig_ctx, "skeletal_showcase_ui", &atlas_opts);
    static const uint8_t white[4] = {255, 255, 255, 255};
    nt_atlas_sprite_opts_t white_opts = nt_atlas_sprite_opts_defaults();
    white_opts.name = "_white";
    nt_atlas_add_raw(atlas, white, 1, 1, &white_opts);
    nt_atlas_sprite_opts_t art = nt_atlas_sprite_opts_defaults();
    art.name = "track";
    art.slice9_left = 8;
    art.slice9_right = 8;
    art.slice9_top = 8;
    art.slice9_bottom = 8;
    nt_atlas_add(atlas, "examples/ui_showcase/raw/bar_track.png", &art);
    art.name = "fill";
    nt_atlas_add(atlas, "examples/ui_showcase/raw/bar_fill_smooth.png", &art);
    art = nt_atlas_sprite_opts_defaults();
    art.name = "thumb";
    nt_atlas_add(atlas, "examples/ui_showcase/raw/bar_thumb.png", &art);
    /* Checkbox: box (off) + checkmark overlay. */
    art = nt_atlas_sprite_opts_defaults();
    art.name = "box_off";
    nt_atlas_add(atlas, "examples/ui_showcase/raw/box_off.png", &art);
    art = nt_atlas_sprite_opts_defaults();
    art.name = "checkmark";
    nt_atlas_add(atlas, "examples/ui_showcase/raw/checkmark.png", &art);
    (void)nt_atlas_commit(atlas);

    nt_builder_add_font(rig_ctx, FONT_PATH, &(nt_font_opts_t){.charset = NT_CHARSET_ASCII, .resource_name = "skeletal_showcase/font"});

    add_character(rig_ctx, clip_ctx, &k_fox);
    add_character(rig_ctx, clip_ctx, &k_cesiumman);

    if (!finish(rig_ctx, "skeletal_showcase.ntpack")) {
        nt_builder_free_pack(clip_ctx);
        return 1;
    }
    if (!finish(clip_ctx, "skeletal_showcase_clips.ntpack")) {
        return 1;
    }

    char rig_header[512];
    char clips_header[512];
    char merged[512];
    (void)snprintf(rig_header, sizeof rig_header, "%s/skeletal_showcase.h", HEADER_DIR);
    (void)snprintf(clips_header, sizeof clips_header, "%s/skeletal_showcase_clips.h", HEADER_DIR);
    (void)snprintf(merged, sizeof merged, "%s/skeletal_showcase_assets.h", HEADER_DIR);
    const char *headers[] = {rig_header, clips_header};
    nt_builder_merge_headers(headers, 2, merged);
    (void)printf("Generated: %s\n", merged);
    (void)printf("\n=== Done ===\n");
    return 0;
}
