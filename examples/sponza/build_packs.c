/*
 * Build four .ntpack packs for the Sponza demo (progressive loading):
 *   sponza_core.ntpack -- shaders + manifest + placeholder textures (~60-80 KB)
 *   sponza_geo.ntpack  -- all meshes, float16/int16 (~2-3 MB)
 *   sponza_tex.ntpack  -- all textures, 512px max, ETC1S compressed
 *   sponza_full.ntpack -- full quality meshes + textures + manifest (overlay, UASTC compressed)
 *
 * All packs share resource_ids -- pack stacking (priority) resolves at runtime.
 * Core loads first for instant feedback, geo next for visible geometry, then
 * real textures progressively replace placeholders.
 *
 * Usage: build_sponza_packs <output_dir>
 */

#include "nt_basisu_encoder.h"
#include "nt_builder.h"

#include "cgltf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

#include "sponza_manifest.h"

/* --- Shader type classification from glTF material properties --- */

static uint8_t classify_material(const nt_glb_material_t *mat, const cgltf_material *cm) {
    if (mat == NULL) {
        return SPONZA_SHADER_DIFFUSE;
    }
    /* Alpha mask/blend -> alpha test shader */
    if (cm != NULL && (cm->alpha_mode == cgltf_alpha_mode_mask || cm->alpha_mode == cgltf_alpha_mode_blend)) {
        return SPONZA_SHADER_ALPHA;
    }
    /* Has normal map -> full shader (Blinn-Phong + normal mapping) */
    if (mat->normal_index != UINT32_MAX) {
        return SPONZA_SHADER_FULL;
    }
    /* Diffuse-only */
    return SPONZA_SHADER_DIFFUSE;
}

/* --- Path helper --- */

static char s_path_buf[512];

static const char *pack_path(const char *dir, const char *name) {
    (void)snprintf(s_path_buf, sizeof(s_path_buf), "%s/%s", dir, name);
    return s_path_buf;
}

/* --- Resource ID helpers --- */

static char s_rid_buf[128];

static const char *mesh_rid(uint32_t mesh_index, uint32_t prim_index) {
    (void)snprintf(s_rid_buf, sizeof(s_rid_buf), "sponza/mesh/%u/%u", mesh_index, prim_index);
    return s_rid_buf;
}

static char s_tid_buf[128];

static const char *tex_rid(uint32_t tex_index) {
    (void)snprintf(s_tid_buf, sizeof(s_tid_buf), "sponza/tex/%u", tex_index);
    return s_tid_buf;
}

/* --- Texture role classification --- */

typedef enum { TEX_ROLE_DIFFUSE, TEX_ROLE_NORMAL, TEX_ROLE_SPECULAR, TEX_ROLE_UNKNOWN } tex_role_t;

/* Build a lookup: texture_index -> role (based on how materials reference it) */
static tex_role_t *build_texture_roles(const nt_glb_scene_t *scene) {
    tex_role_t *roles = (tex_role_t *)calloc(scene->texture_count > 0 ? scene->texture_count : 1, sizeof(tex_role_t));
    NT_BUILD_ASSERT(roles && "build_texture_roles: alloc failed");
    for (uint32_t i = 0; i < scene->texture_count; i++) {
        roles[i] = TEX_ROLE_UNKNOWN;
    }
    for (uint32_t mi = 0; mi < scene->material_count; mi++) {
        const nt_glb_material_t *mat = &scene->materials[mi];
        if (mat->diffuse_index < scene->texture_count) {
            roles[mat->diffuse_index] = TEX_ROLE_DIFFUSE;
        }
        if (mat->normal_index < scene->texture_count) {
            roles[mat->normal_index] = TEX_ROLE_NORMAL;
        }
        if (mat->specular_index < scene->texture_count) {
            roles[mat->specular_index] = TEX_ROLE_SPECULAR;
        }
    }
    return roles;
}

/* Check if any material uses this texture as a diffuse with alpha */
static bool texture_needs_alpha(const nt_glb_scene_t *scene, uint32_t tex_index, const cgltf_data *gltf) {
    for (uint32_t mi = 0; mi < scene->material_count; mi++) {
        if (scene->materials[mi].diffuse_index != tex_index) {
            continue;
        }
        if (mi < gltf->materials_count) {
            cgltf_alpha_mode am = gltf->materials[mi].alpha_mode;
            if (am == cgltf_alpha_mode_mask || am == cgltf_alpha_mode_blend) {
                return true;
            }
        }
    }
    return false;
}

/* --- Add all textures with per-role format, optional resize, and optional compression --- */
/* color_compress: compression for diffuse/specular/unknown textures (NULL = no compression)
 * normal_compress: compression for normal maps (NULL = no compression) */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void add_textures(NtBuilderContext *ctx, const nt_glb_scene_t *scene, uint32_t max_size, const nt_tex_compress_opts_t *color_compress, const nt_tex_compress_opts_t *normal_compress) {
    tex_role_t *roles = build_texture_roles(scene);
    cgltf_data *gltf = (cgltf_data *)scene->_internal;

    uint32_t added = 0;
    for (uint32_t i = 0; i < scene->texture_count; i++) {
        if (scene->textures[i].data == NULL || scene->textures[i].size == 0) {
            (void)fprintf(stderr, "WARNING: texture %u has no embedded data, skipping\n", i);
            continue;
        }

        nt_tex_opts_t opts = {.format = NT_TEXTURE_FORMAT_RGBA8, .max_size = max_size};
        const nt_tex_compress_opts_t *tex_compress = color_compress;
        switch (roles[i]) {
        case TEX_ROLE_DIFFUSE:
            opts.format = texture_needs_alpha(scene, i, gltf) ? NT_TEXTURE_FORMAT_RGBA8 : NT_TEXTURE_FORMAT_RGB8;
            break;
        case TEX_ROLE_NORMAL:
            /* RG8 for RAW (placeholders), RGB8 for Basis (Basis has no 2-channel mode) */
            opts.format = normal_compress ? NT_TEXTURE_FORMAT_RGB8 : NT_TEXTURE_FORMAT_RG8;
            tex_compress = normal_compress;
            break;
        case TEX_ROLE_SPECULAR:
            opts.format = color_compress ? NT_TEXTURE_FORMAT_RGB8 : NT_TEXTURE_FORMAT_RG8;
            break;
        case TEX_ROLE_UNKNOWN:
            opts.format = NT_TEXTURE_FORMAT_RGBA8;
            break;
        }

        opts.compress = tex_compress;
        nt_builder_add_texture_from_memory(ctx, scene->textures[i].data, scene->textures[i].size, tex_rid(i), &opts);
        added++;
    }
    (void)printf("  Textures added: %u / %u\n", added, scene->texture_count);
    free(roles);
}

/* --- Add placeholder textures (role-specific, same resource IDs) --- */
/* Diffuse textures are NOT included — covered by runtime fallback checkerboard.
 * Normal/specular get role-appropriate 1x1 placeholders so lighting is correct
 * even before real textures load. */

#define CHECKER_SIZE 16
static uint8_t s_checker[CHECKER_SIZE * CHECKER_SIZE * 4];

static void init_checker(void) {
    for (int y = 0; y < CHECKER_SIZE; y++) {
        for (int x = 0; x < CHECKER_SIZE; x++) {
            uint8_t *p = &s_checker[((size_t)y * CHECKER_SIZE + (size_t)x) * 4];
            uint8_t v = ((x ^ y) & 1) ? 60 : 160;
            p[0] = v;
            p[1] = v;
            p[2] = v;
            p[3] = 255;
        }
    }
}

/* add_texture_raw expects RGBA input; format=RGB8 strips to 3 channels at encode time */
static const uint8_t s_flat_normal_rgba[4] = {128, 128, 255, 255}; /* (0,0,1) — flat normal */
static const uint8_t s_rough_rgba[4] = {0, 255, 0, 255};           /* R=0, G=roughness(1.0), B=0 */

static void add_placeholder_textures(NtBuilderContext *ctx, const nt_glb_scene_t *scene) {
    tex_role_t *roles = build_texture_roles(scene);

    uint32_t added = 0;
    for (uint32_t i = 0; i < scene->texture_count; i++) {
        const uint8_t *pixels = NULL;
        nt_tex_opts_t opts = {.format = NT_TEXTURE_FORMAT_RGB8, .max_size = 0};

        switch (roles[i]) {
        case TEX_ROLE_NORMAL:
            pixels = s_flat_normal_rgba;
            break;
        case TEX_ROLE_SPECULAR:
            pixels = s_rough_rgba;
            break;
        default: /* diffuse + unknown — handled by runtime fallback */
            continue;
        }

        nt_builder_add_texture_raw(ctx, pixels, 1, 1, tex_rid(i), &opts);
        added++;
    }
    (void)printf("  Placeholder textures added: %u (normal + specular only)\n", added);
    free(roles);
}

/* --- Add fallback checkerboard texture (used as runtime placeholder for all unloaded textures) --- */

static void add_fallback_checker(NtBuilderContext *ctx) {
    nt_tex_opts_t opts = {.format = NT_TEXTURE_FORMAT_RGBA8, .max_size = 0};
    nt_builder_add_texture_raw(ctx, s_checker, CHECKER_SIZE, CHECKER_SIZE, "sponza/fallback_checker", &opts);
    (void)printf("  Fallback checkerboard added\n");
}

/* --- Add all 6 shader files --- */

static void add_shaders(NtBuilderContext *ctx) {
    static const struct {
        const char *path;
        nt_build_shader_stage_t stage;
    } shaders[] = {
        {"assets/shaders/sponza_full.vert", NT_BUILD_SHADER_VERTEX},      {"assets/shaders/sponza_full.frag", NT_BUILD_SHADER_FRAGMENT}, {"assets/shaders/sponza_diffuse.vert", NT_BUILD_SHADER_VERTEX},
        {"assets/shaders/sponza_diffuse.frag", NT_BUILD_SHADER_FRAGMENT}, {"assets/shaders/sponza_alpha.vert", NT_BUILD_SHADER_VERTEX},  {"assets/shaders/sponza_alpha.frag", NT_BUILD_SHADER_FRAGMENT},
    };

    for (int i = 0; i < 6; i++) {
        nt_builder_add_shader(ctx, shaders[i].path, shaders[i].stage);
    }
    (void)printf("  Shaders added: 6\n");
}

/* --- Build manifest blob (scene layout info, no mesh data dependency) --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void build_manifest_blob(NtBuilderContext *ctx, const nt_glb_scene_t *scene) {
    cgltf_data *gltf = (cgltf_data *)scene->_internal;

    /* First pass: count manifest entries */
    uint32_t manifest_count = 0;
    for (uint32_t ni = 0; ni < scene->node_count; ni++) {
        if (scene->nodes[ni].mesh_index == UINT32_MAX) {
            continue;
        }
        uint32_t mi = scene->nodes[ni].mesh_index;
        manifest_count += scene->meshes[mi].primitive_count;
    }

    /* Allocate manifest */
    uint32_t manifest_size = (uint32_t)sizeof(SponzaManifestHeader) + (manifest_count * (uint32_t)sizeof(SponzaManifestNode));
    uint8_t *manifest_buf = (uint8_t *)calloc(manifest_size > 0 ? manifest_size : 1, 1);
    if (!manifest_buf) {
        return;
    }

    SponzaManifestHeader *hdr = (SponzaManifestHeader *)manifest_buf;
    hdr->node_count = manifest_count;
    SponzaManifestNode *nodes_out = (SponzaManifestNode *)(manifest_buf + sizeof(SponzaManifestHeader));

    /* Second pass: fill manifest entries */
    uint32_t entry_idx = 0;

    for (uint32_t ni = 0; ni < scene->node_count; ni++) {
        if (scene->nodes[ni].mesh_index == UINT32_MAX) {
            continue;
        }
        uint32_t mi = scene->nodes[ni].mesh_index;
        const cgltf_mesh *cgltf_m = &gltf->meshes[mi];

        for (uint32_t pi = 0; pi < scene->meshes[mi].primitive_count; pi++) {
            const cgltf_primitive *prim = &cgltf_m->primitives[pi];
            uint32_t mat_idx = UINT32_MAX;
            if (prim->material != NULL) {
                mat_idx = (uint32_t)(prim->material - gltf->materials);
            }

            const nt_glb_material_t *mat = NULL;
            const cgltf_material *cm = NULL;
            uint8_t shader_type = SPONZA_SHADER_DIFFUSE;
            if (mat_idx < scene->material_count) {
                mat = &scene->materials[mat_idx];
                cm = &gltf->materials[mat_idx];
                shader_type = classify_material(mat, cm);
            }

            SponzaManifestNode *mn = &nodes_out[entry_idx];
            mn->mesh_rid = nt_builder_normalize_and_hash(mesh_rid(mi, pi)).value;

            if (mat != NULL && mat->diffuse_index != UINT32_MAX) {
                mn->diffuse_rid = nt_builder_normalize_and_hash(tex_rid(mat->diffuse_index)).value;
            }
            if (mat != NULL && mat->normal_index != UINT32_MAX) {
                mn->normal_rid = nt_builder_normalize_and_hash(tex_rid(mat->normal_index)).value;
            }
            if (mat != NULL && mat->specular_index != UINT32_MAX) {
                mn->specular_rid = nt_builder_normalize_and_hash(tex_rid(mat->specular_index)).value;
            }

            memcpy(mn->transform, scene->nodes[ni].transform, sizeof(float) * 16);

            if (mat != NULL) {
                memcpy(mn->base_color, mat->base_color, sizeof(float) * 4);
            } else {
                mn->base_color[0] = 1.0F;
                mn->base_color[1] = 1.0F;
                mn->base_color[2] = 1.0F;
                mn->base_color[3] = 1.0F;
            }

            mn->shader_type = shader_type;
            mn->alpha_cutoff_x100 = (mat != NULL) ? (uint8_t)(mat->alpha_cutoff * 100.0F) : 50;

            entry_idx++;
        }
    }

    (void)printf("  Manifest entries: %u\n", entry_idx);

    nt_builder_add_blob(ctx, manifest_buf, manifest_size, "sponza/manifest");
    free(manifest_buf);
}

/* --- Add all meshes (no manifest) --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void add_meshes(NtBuilderContext *ctx, const nt_glb_scene_t *scene, bool use_base_quality) {
    NtStreamLayout layout_full[] = {
        {"position", "POSITION", NT_STREAM_FLOAT32, 3, false},
        {"normal", "NORMAL", NT_STREAM_FLOAT32, 3, false},
        {"uv0", "TEXCOORD_0", NT_STREAM_FLOAT32, 2, false},
        {"tangent", "TANGENT", NT_STREAM_FLOAT32, 4, false},
    };

    NtStreamLayout layout_diffuse[] = {
        {"position", "POSITION", NT_STREAM_FLOAT32, 3, false},
        {"normal", "NORMAL", NT_STREAM_FLOAT32, 3, false},
        {"uv0", "TEXCOORD_0", NT_STREAM_FLOAT32, 2, false},
    };

    NtStreamLayout layout_base[] = {
        {"position", "POSITION", NT_STREAM_FLOAT16, 3, false},
        {"normal", "NORMAL", NT_STREAM_INT16, 3, true},
        {"uv0", "TEXCOORD_0", NT_STREAM_FLOAT16, 2, false},
        {"tangent", "TANGENT", NT_STREAM_INT16, 4, true},
    };

    cgltf_data *gltf = (cgltf_data *)scene->_internal;
    uint32_t meshes_added = 0;

    for (uint32_t ni = 0; ni < scene->node_count; ni++) {
        if (scene->nodes[ni].mesh_index == UINT32_MAX) {
            continue;
        }
        uint32_t mi = scene->nodes[ni].mesh_index;
        const cgltf_mesh *cgltf_m = &gltf->meshes[mi];

        for (uint32_t pi = 0; pi < scene->meshes[mi].primitive_count; pi++) {
            const cgltf_primitive *prim = &cgltf_m->primitives[pi];
            uint32_t mat_idx = UINT32_MAX;
            if (prim->material != NULL) {
                mat_idx = (uint32_t)(prim->material - gltf->materials);
            }

            const nt_glb_material_t *mat = NULL;
            const cgltf_material *cm = NULL;
            uint8_t shader_type = SPONZA_SHADER_DIFFUSE;
            if (mat_idx < scene->material_count) {
                mat = &scene->materials[mat_idx];
                cm = &gltf->materials[mat_idx];
                shader_type = classify_material(mat, cm);
            }

            const NtStreamLayout *layout = NULL;
            uint32_t stream_count = 0;
            nt_tangent_mode_t tangent_mode = NT_TANGENT_NONE;

            if (use_base_quality) {
                layout = layout_base;
                stream_count = (shader_type == SPONZA_SHADER_FULL) ? 4 : 3;
                tangent_mode = (shader_type == SPONZA_SHADER_FULL) ? NT_TANGENT_AUTO : NT_TANGENT_NONE;
            } else if (shader_type == SPONZA_SHADER_FULL) {
                layout = layout_full;
                stream_count = 4;
                tangent_mode = NT_TANGENT_AUTO;
            } else {
                layout = layout_diffuse;
                stream_count = 3;
                tangent_mode = NT_TANGENT_NONE;
            }

            const char *rid = mesh_rid(mi, pi);
            nt_mesh_opts_t opts = {
                .layout = layout,
                .stream_count = stream_count,
                .tangent_mode = tangent_mode,
            };
            nt_builder_add_scene_mesh(ctx, scene, mi, pi, rid, &opts);
            meshes_added++;
        }
    }

    (void)printf("  Meshes added: %u\n", meshes_added);
}

/* --- Add all meshes and build scene manifest (for full pack) --- */

static void add_meshes_and_manifest(NtBuilderContext *ctx, const nt_glb_scene_t *scene, bool use_base_quality) {
    add_meshes(ctx, scene, use_base_quality);
    build_manifest_blob(ctx, scene);
}

/* --- Main --- */

static nt_build_result_t build_pack(const char *out_dir, const char *hdr_dir, const char *cache_dir, const char *name, const nt_glb_scene_t *scene,
                                    void (*populate)(NtBuilderContext *, const nt_glb_scene_t *)) {
    (void)printf("--- Building %s ---\n", name);
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path(out_dir, name));
    if (!ctx) {
        (void)fprintf(stderr, "Failed to start %s\n", name);
        return NT_BUILD_ERR_IO;
    }
    nt_builder_set_header_dir(ctx, hdr_dir);
    nt_builder_set_cache_dir(ctx, cache_dir);
    nt_builder_set_threads_auto(ctx);

    populate(ctx, scene);

    nt_build_result_t r = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);
    if (r != NT_BUILD_OK) {
        (void)fprintf(stderr, "%s failed: %d\n", name, r);
        return r;
    }
    (void)printf("Built: %s\n\n", name);
    return NT_BUILD_OK;
}

/* Pack population callbacks */

static void populate_core(NtBuilderContext *ctx, const nt_glb_scene_t *scene) {
    add_shaders(ctx);
    build_manifest_blob(ctx, scene);
    add_fallback_checker(ctx);
    add_placeholder_textures(ctx, scene);
}

static void populate_geo(NtBuilderContext *ctx, const nt_glb_scene_t *scene) { add_meshes(ctx, scene, true); }

static void populate_tex(NtBuilderContext *ctx, const nt_glb_scene_t *scene) {
    nt_tex_compress_opts_t color = nt_tex_compress_etc1s_default();
    nt_tex_compress_opts_t normal = nt_tex_compress_uastc_default();
    add_textures(ctx, scene, 512, &color, &normal);
}

static void populate_full(NtBuilderContext *ctx, const nt_glb_scene_t *scene) {
    /* Overlay pack: full-res textures + meshes + manifest.
     * Shaders come from sponza_core (always loaded first). */
    nt_tex_compress_opts_t color = nt_tex_compress_uastc_default();
    nt_tex_compress_opts_t normal = nt_tex_compress_uastc_default();
    add_textures(ctx, scene, 0, &color, &normal);
    add_meshes_and_manifest(ctx, scene, false);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
int main(int argc, char *argv[]) {
    if (argc < 2) {
        (void)fprintf(stderr, "Usage: build_sponza_packs <pack_dir>\n");
        return 1;
    }
    const char *out_dir = argv[1];
    const char *header_dir = "examples/sponza/generated";
    char cache_dir[512];
    (void)snprintf(cache_dir, sizeof(cache_dir), "%s/_cache", out_dir);

    (void)printf("=== Build Sponza Packs -> %s ===\n\n", out_dir);

    MKDIR(out_dir);
    MKDIR(cache_dir);
    MKDIR(header_dir);
    init_checker();

    /* Parse Sponza scene */
    nt_glb_scene_t scene;
    nt_build_result_t r = nt_builder_parse_glb_scene(&scene, "assets/sponza/Sponza.glb");
    if (r != NT_BUILD_OK) {
        (void)fprintf(stderr, "ERROR: failed to parse Sponza.glb: %d\n", r);
        return 1;
    }

    (void)printf("\nScene summary:\n");
    (void)printf("  Meshes: %u, Materials: %u, Textures: %u, Nodes: %u\n", scene.mesh_count, scene.material_count, scene.texture_count, scene.node_count);

    /* Dump texture info for dedup analysis */
    tex_role_t *roles = build_texture_roles(&scene);
    if (roles) {
        static const char *role_names[] = {"diffuse", "normal", "specular", "unknown"};
        (void)printf("\n  Texture details:\n");
        for (uint32_t i = 0; i < scene.texture_count; i++) {
            const nt_glb_texture_t *t = &scene.textures[i];
            (void)printf("    [%2u] %-8s %6u bytes  %s  %s\n", i, role_names[roles[i]], t->size, t->mime_type ? t->mime_type : "?", t->name ? t->name : "(unnamed)");
        }
        free(roles);
    }
    (void)printf("\n");

    /* ---- Pack 1: sponza_core.ntpack (shaders + manifest + placeholder textures) ---- */
    r = build_pack(out_dir, header_dir, cache_dir, "sponza_core.ntpack", &scene, populate_core);
    if (r != NT_BUILD_OK) {
        nt_builder_free_glb_scene(&scene);
        return 1;
    }

    /* ---- Pack 2: sponza_geo.ntpack (all meshes, base quality) ---- */
    r = build_pack(out_dir, header_dir, cache_dir, "sponza_geo.ntpack", &scene, populate_geo);
    if (r != NT_BUILD_OK) {
        nt_builder_free_glb_scene(&scene);
        return 1;
    }

    /* ---- Pack 3: sponza_tex.ntpack (all textures, 512px max, ETC1S compressed) ---- */
    r = build_pack(out_dir, header_dir, cache_dir, "sponza_tex.ntpack", &scene, populate_tex);
    if (r != NT_BUILD_OK) {
        nt_builder_free_glb_scene(&scene);
        return 1;
    }

    /* ---- Pack 4: sponza_full.ntpack (full quality, UASTC compressed) ---- */
    r = build_pack(out_dir, header_dir, cache_dir, "sponza_full.ntpack", &scene, populate_full);
    if (r != NT_BUILD_OK) {
        nt_builder_free_glb_scene(&scene);
        return 1;
    }

    /* Generate combined header from per-pack .h files */
    char core_hdr[512];
    char geo_hdr[512];
    char tex_hdr[512];
    char full_hdr[512];
    (void)snprintf(core_hdr, sizeof(core_hdr), "%s/sponza_core.h", header_dir);
    (void)snprintf(geo_hdr, sizeof(geo_hdr), "%s/sponza_geo.h", header_dir);
    (void)snprintf(tex_hdr, sizeof(tex_hdr), "%s/sponza_tex.h", header_dir);
    (void)snprintf(full_hdr, sizeof(full_hdr), "%s/sponza_full.h", header_dir);
    const char *pack_headers[] = {core_hdr, geo_hdr, tex_hdr, full_hdr};
    char combined_header[512];
    (void)snprintf(combined_header, sizeof(combined_header), "%s/sponza_assets.h", header_dir);
    nt_builder_merge_headers(pack_headers, 4, combined_header);

    nt_builder_free_glb_scene(&scene);

    /* Print pack size summary (D-22) */
    (void)printf("\n=== Pack Size Summary ===\n");
    static const char *pack_names[] = {"sponza_core.ntpack", "sponza_geo.ntpack", "sponza_tex.ntpack", "sponza_full.ntpack"};
    for (int i = 0; i < 4; i++) {
        FILE *f = fopen(pack_path(out_dir, pack_names[i]), "rb");
        if (f) {
            (void)fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            (void)fclose(f);
            (void)printf("  %-24s %8.1f KB\n", pack_names[i], (double)sz / 1024.0);
        }
    }

    nt_basisu_encoder_shutdown();
    (void)printf("\n=== Done ===\n");
    return 0;
}
