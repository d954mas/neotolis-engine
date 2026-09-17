/* Build the small Skeleton & Pose pack: UI atlas, font and the two Khronos rigs.
 * The font is reused from ui_showcase and is distributed under Apache 2.0. */

/* clang-format off */
#include "nt_builder.h"
/* clang-format on */

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

static char s_path[512];

static const char *pack_path(const char *dir, const char *name) {
    (void)snprintf(s_path, sizeof s_path, "%s/%s", dir, name);
    return s_path;
}

/* Skeleton only: the rig of skin 0 up to the scene root, so the showcase shows
 * the rest pose exactly as the importer publishes it. Bindings and meshes are
 * not exported here. */
static bool add_rig_skeleton(NtBuilderContext *ctx, const char *glb_path, const char *resource_id) {
    nt_glb_scene_t scene;
    if (nt_builder_parse_glb_scene(&scene, glb_path) != NT_BUILD_OK) {
        (void)fprintf(stderr, "Failed to parse %s\n", glb_path);
        return false;
    }
    const nt_builder_rig_selection_t sel = {.skin_index = 0, .skeleton_root = UINT32_MAX, .object_node = UINT32_MAX};
    nt_builder_rig_t rig;
    nt_builder_import_rig(&scene, &sel, &rig);
    (void)nt_builder_add_skeleton(ctx, &rig.skeleton, resource_id);
    nt_builder_free_rig(&rig);
    nt_builder_free_glb_scene(&scene);
    return true;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        (void)fprintf(stderr, "Usage: build_skeletal_showcase_packs <pack_dir>\n");
        return 1;
    }

    const char *out_dir = argv[1];
    (void)printf("=== Build Skeleton & Pose Pack -> %s ===\n\n", out_dir);
    (void)MKDIR(out_dir);
    (void)MKDIR(HEADER_DIR);

    char cache_dir[512];
    (void)snprintf(cache_dir, sizeof cache_dir, "%s/_cache", out_dir);
    (void)MKDIR(cache_dir);

    NtBuilderContext *ctx = nt_builder_start_pack(pack_path(out_dir, "skeletal_showcase.ntpack"));
    if (ctx == NULL) {
        (void)fprintf(stderr, "Failed to start skeletal_showcase.ntpack\n");
        return 1;
    }
    nt_builder_set_header_dir(ctx, HEADER_DIR);
    nt_builder_set_cache_dir(ctx, cache_dir);
    nt_builder_set_threads_auto(ctx);

    nt_builder_add_shader(ctx, "assets/shaders/sprite.vert", NT_BUILD_SHADER_VERTEX);
    nt_builder_add_shader(ctx, "assets/shaders/sprite.frag", NT_BUILD_SHADER_FRAGMENT);
    nt_builder_add_shader(ctx, "assets/shaders/slug_text.vert", NT_BUILD_SHADER_VERTEX);
    nt_builder_add_shader(ctx, "assets/shaders/slug_text.frag", NT_BUILD_SHADER_FRAGMENT);

    nt_atlas_opts_t atlas_opts = nt_atlas_opts_defaults();
    atlas_opts.shape = NT_ATLAS_SHAPE_RECT;
    atlas_opts.allowed_transforms = NT_ATLAS_TRANSFORMS_IDENTITY;
    atlas_opts.padding = 1;
    atlas_opts.margin = 1;
    atlas_opts.extrude = 1;
    atlas_opts.premultiplied = true;
    atlas_opts.compress = (nt_basisu_encode_opts_t){0};
    atlas_opts.gen_mipmaps = true;
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, "skeletal_showcase_ui", &atlas_opts);
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
    (void)nt_atlas_commit(atlas);

    nt_builder_add_font(ctx, FONT_PATH, &(nt_font_opts_t){.charset = NT_CHARSET_ASCII, .resource_name = "skeletal_showcase/font"});

    if (!add_rig_skeleton(ctx, "examples/skeletal_showcase/raw/Fox.glb", "skeletal_showcase/fox.nskl") ||
        !add_rig_skeleton(ctx, "examples/skeletal_showcase/raw/CesiumMan.glb", "skeletal_showcase/cesiumman.nskl")) {
        nt_builder_free_pack(ctx);
        return 1;
    }

    const nt_build_result_t result = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);
    if (result != NT_BUILD_OK) {
        (void)fprintf(stderr, "skeletal_showcase.ntpack failed: %d\n", result);
        return 1;
    }

    char header[512];
    char merged[512];
    (void)snprintf(header, sizeof header, "%s/skeletal_showcase.h", HEADER_DIR);
    (void)snprintf(merged, sizeof merged, "%s/skeletal_showcase_assets.h", HEADER_DIR);
    const char *headers[] = {header};
    nt_builder_merge_headers(headers, 1, merged);
    (void)printf("Generated: %s\n", merged);
    (void)printf("\n=== Done ===\n");
    return 0;
}
