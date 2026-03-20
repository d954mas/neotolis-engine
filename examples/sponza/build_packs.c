/*
 * Build two .ntpack packs for the Sponza demo:
 *   sponza_base.ntpack  -- compressed quality (float16 positions, int8 normals)
 *   sponza_full.ntpack  -- full quality (float32, full-res textures)
 *
 * Both packs share resource_ids -- pack stacking (priority) resolves at runtime.
 * Contains: all Sponza meshes, all textures, all 6 shaders, scene manifest blob.
 *
 * Usage: build_sponza_packs <output_dir>
 */

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

/* --- Scene manifest structs (builder-side only) --- */

#pragma pack(push, 1)
typedef struct {
    uint32_t node_count;
    uint32_t _pad;
} ManifestHeader;

typedef struct {
    uint64_t mesh_rid;
    uint64_t diffuse_rid;
    uint64_t normal_rid;
    uint64_t specular_rid;
    float transform[16];
    float base_color[4];
    uint8_t shader_type; /* 0=full, 1=diffuse, 2=alpha_test */
    uint8_t alpha_cutoff_x100;
    uint8_t _pad[6];
} ManifestNode;
#pragma pack(pop)

/* --- Shader type classification from glTF material properties --- */

enum { SHADER_FULL = 0, SHADER_DIFFUSE = 1, SHADER_ALPHA = 2 };

static uint8_t classify_material(const nt_glb_material_t *mat, const cgltf_material *cm) {
    if (mat == NULL) {
        return SHADER_DIFFUSE;
    }
    /* Alpha mask/blend -> alpha test shader */
    if (cm != NULL && (cm->alpha_mode == cgltf_alpha_mode_mask || cm->alpha_mode == cgltf_alpha_mode_blend)) {
        return SHADER_ALPHA;
    }
    /* Has normal map -> full shader (Blinn-Phong + normal mapping) */
    if (mat->normal_index != UINT32_MAX) {
        return SHADER_FULL;
    }
    /* Diffuse-only */
    return SHADER_DIFFUSE;
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
    if (!roles) {
        return NULL;
    }
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

/* --- Add all textures with per-role format and optional resize --- */

static nt_build_result_t add_textures(NtBuilderContext *ctx, const nt_glb_scene_t *scene, uint32_t max_size) {
    tex_role_t *roles = build_texture_roles(scene);
    if (!roles) {
        return NT_BUILD_ERR_IO;
    }
    cgltf_data *gltf = (cgltf_data *)scene->_internal;

    uint32_t added = 0;
    for (uint32_t i = 0; i < scene->texture_count; i++) {
        if (scene->textures[i].data == NULL || scene->textures[i].size == 0) {
            (void)fprintf(stderr, "WARNING: texture %u has no embedded data, skipping\n", i);
            continue;
        }

        nt_tex_opts_t opts = {.format = NT_TEXTURE_FORMAT_RGBA8, .max_size = max_size};
        switch (roles[i]) {
        case TEX_ROLE_DIFFUSE:
            opts.format = texture_needs_alpha(scene, i, gltf) ? NT_TEXTURE_FORMAT_RGBA8 : NT_TEXTURE_FORMAT_RGB8;
            break;
        case TEX_ROLE_NORMAL:
            opts.format = NT_TEXTURE_FORMAT_RG8;
            break;
        case TEX_ROLE_SPECULAR:
            opts.format = NT_TEXTURE_FORMAT_RG8;
            break;
        case TEX_ROLE_UNKNOWN:
            opts.format = NT_TEXTURE_FORMAT_RGBA8;
            break;
        }

        nt_build_result_t r = nt_builder_add_texture_from_memory_ex(ctx, scene->textures[i].data, scene->textures[i].size, tex_rid(i), &opts);
        if (r != NT_BUILD_OK) {
            (void)fprintf(stderr, "ERROR: failed to add texture %u: %d\n", i, r);
            free(roles);
            return r;
        }
        added++;
    }
    (void)printf("  Textures added: %u / %u\n", added, scene->texture_count);
    free(roles);
    return NT_BUILD_OK;
}

/* --- Add all 6 shader files --- */

static nt_build_result_t add_shaders(NtBuilderContext *ctx) {
    static const struct {
        const char *path;
        nt_build_shader_stage_t stage;
    } shaders[] = {
        {"assets/shaders/sponza_full.vert", NT_BUILD_SHADER_VERTEX},      {"assets/shaders/sponza_full.frag", NT_BUILD_SHADER_FRAGMENT}, {"assets/shaders/sponza_diffuse.vert", NT_BUILD_SHADER_VERTEX},
        {"assets/shaders/sponza_diffuse.frag", NT_BUILD_SHADER_FRAGMENT}, {"assets/shaders/sponza_alpha.vert", NT_BUILD_SHADER_VERTEX},  {"assets/shaders/sponza_alpha.frag", NT_BUILD_SHADER_FRAGMENT},
    };

    for (int i = 0; i < 6; i++) {
        nt_build_result_t r = nt_builder_add_shader(ctx, shaders[i].path, shaders[i].stage);
        if (r != NT_BUILD_OK) {
            (void)fprintf(stderr, "ERROR: failed to add shader %s: %d\n", shaders[i].path, r);
            return r;
        }
    }
    (void)printf("  Shaders added: 6\n");
    return NT_BUILD_OK;
}

/* --- Add all meshes and build scene manifest --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static nt_build_result_t add_meshes_and_manifest(NtBuilderContext *ctx, const nt_glb_scene_t *scene, bool use_base_quality) {
    /* Stream layouts */
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

    /* Access cgltf data for per-primitive material info */
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
    uint32_t manifest_size = (uint32_t)sizeof(ManifestHeader) + manifest_count * (uint32_t)sizeof(ManifestNode);
    uint8_t *manifest_buf = (uint8_t *)calloc(manifest_size > 0 ? manifest_size : 1, 1);
    if (!manifest_buf) {
        return NT_BUILD_ERR_IO;
    }

    ManifestHeader *hdr = (ManifestHeader *)manifest_buf;
    hdr->node_count = manifest_count;
    ManifestNode *nodes_out = (ManifestNode *)(manifest_buf + sizeof(ManifestHeader));

    /* Second pass: add meshes and fill manifest */
    uint32_t entry_idx = 0;
    uint32_t meshes_added = 0;

    for (uint32_t ni = 0; ni < scene->node_count; ni++) {
        if (scene->nodes[ni].mesh_index == UINT32_MAX) {
            continue;
        }
        uint32_t mi = scene->nodes[ni].mesh_index;
        const cgltf_mesh *cgltf_m = &gltf->meshes[mi];

        for (uint32_t pi = 0; pi < scene->meshes[mi].primitive_count; pi++) {
            /* Get per-primitive material index from cgltf */
            const cgltf_primitive *prim = &cgltf_m->primitives[pi];
            uint32_t mat_idx = UINT32_MAX;
            if (prim->material != NULL) {
                mat_idx = (uint32_t)(prim->material - gltf->materials);
            }

            /* Determine shader type */
            const nt_glb_material_t *mat = NULL;
            const cgltf_material *cm = NULL;
            uint8_t shader_type = SHADER_DIFFUSE;
            if (mat_idx < scene->material_count) {
                mat = &scene->materials[mat_idx];
                cm = &gltf->materials[mat_idx];
                shader_type = classify_material(mat, cm);
            }

            /* Choose layout and tangent mode */
            const NtStreamLayout *layout = NULL;
            uint32_t stream_count = 0;
            nt_tangent_mode_t tangent_mode = NT_TANGENT_NONE;

            if (use_base_quality) {
                layout = layout_base;
                stream_count = (shader_type == SHADER_FULL) ? 4 : 3;
                tangent_mode = (shader_type == SHADER_FULL) ? NT_TANGENT_AUTO : NT_TANGENT_NONE;
            } else if (shader_type == SHADER_FULL) {
                layout = layout_full;
                stream_count = 4;
                tangent_mode = NT_TANGENT_AUTO;
            } else {
                layout = layout_diffuse;
                stream_count = 3;
                tangent_mode = NT_TANGENT_NONE;
            }

            /* Add mesh */
            const char *rid = mesh_rid(mi, pi);
            nt_mesh_opts_t opts = {
                .layout = layout,
                .stream_count = stream_count,
                .tangent_mode = tangent_mode,
            };
            nt_build_result_t r = nt_builder_add_scene_mesh(ctx, scene, mi, pi, rid, &opts);
            if (r != NT_BUILD_OK) {
                (void)fprintf(stderr, "ERROR: failed to add mesh %u/%u: %d\n", mi, pi, r);
                free(manifest_buf);
                return r;
            }
            meshes_added++;

            /* Fill manifest entry */
            ManifestNode *mn = &nodes_out[entry_idx];
            mn->mesh_rid = nt_builder_normalize_and_hash(rid).value;

            /* Texture resource IDs */
            if (mat != NULL && mat->diffuse_index != UINT32_MAX) {
                mn->diffuse_rid = nt_builder_normalize_and_hash(tex_rid(mat->diffuse_index)).value;
            }
            if (mat != NULL && mat->normal_index != UINT32_MAX) {
                mn->normal_rid = nt_builder_normalize_and_hash(tex_rid(mat->normal_index)).value;
            }
            if (mat != NULL && mat->specular_index != UINT32_MAX) {
                mn->specular_rid = nt_builder_normalize_and_hash(tex_rid(mat->specular_index)).value;
            }

            /* Transform from node */
            memcpy(mn->transform, scene->nodes[ni].transform, sizeof(float) * 16);

            /* Base color from material */
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

    (void)printf("  Meshes added: %u (manifest entries: %u)\n", meshes_added, entry_idx);

    /* Add manifest blob */
    nt_build_result_t r = nt_builder_add_blob(ctx, manifest_buf, manifest_size, "sponza/manifest");
    free(manifest_buf);
    if (r != NT_BUILD_OK) {
        (void)fprintf(stderr, "ERROR: failed to add manifest blob: %d\n", r);
    }
    return r;
}

/* --- Main --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
int main(int argc, char *argv[]) {
    if (argc < 2) {
        (void)fprintf(stderr, "Usage: build_sponza_packs <output_dir>\n");
        return 1;
    }
    const char *out_dir = argv[1];

    (void)printf("=== Build Sponza Packs -> %s ===\n\n", out_dir);

    MKDIR(out_dir);

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

    /* ---- Pack 1: sponza_base.ntpack (compressed quality) ---- */
    {
        (void)printf("--- Building sponza_base.ntpack ---\n");
        NtBuilderContext *ctx = nt_builder_start_pack(pack_path(out_dir, "sponza_base.ntpack"));
        if (!ctx) {
            (void)fprintf(stderr, "Failed to start base pack\n");
            nt_builder_free_glb_scene(&scene);
            return 1;
        }
        nt_builder_set_force(ctx, true);

        r = add_textures(ctx, &scene, 512);
        if (r != NT_BUILD_OK) {
            nt_builder_free_pack(ctx);
            nt_builder_free_glb_scene(&scene);
            return 1;
        }
        r = add_shaders(ctx);
        if (r != NT_BUILD_OK) {
            nt_builder_free_pack(ctx);
            nt_builder_free_glb_scene(&scene);
            return 1;
        }
        r = add_meshes_and_manifest(ctx, &scene, true);
        if (r != NT_BUILD_OK) {
            nt_builder_free_pack(ctx);
            nt_builder_free_glb_scene(&scene);
            return 1;
        }

        r = nt_builder_finish_pack(ctx);
        nt_builder_free_pack(ctx);
        if (r != NT_BUILD_OK) {
            (void)fprintf(stderr, "Base pack failed: %d\n", r);
            nt_builder_free_glb_scene(&scene);
            return 1;
        }
        (void)printf("Built: sponza_base.ntpack\n\n");
    }

    /* ---- Pack 2: sponza_full.ntpack (full quality) ---- */
    {
        (void)printf("--- Building sponza_full.ntpack ---\n");
        NtBuilderContext *ctx = nt_builder_start_pack(pack_path(out_dir, "sponza_full.ntpack"));
        if (!ctx) {
            (void)fprintf(stderr, "Failed to start full pack\n");
            nt_builder_free_glb_scene(&scene);
            return 1;
        }
        nt_builder_set_force(ctx, true);

        r = add_textures(ctx, &scene, 0);
        if (r != NT_BUILD_OK) {
            nt_builder_free_pack(ctx);
            nt_builder_free_glb_scene(&scene);
            return 1;
        }
        r = add_shaders(ctx);
        if (r != NT_BUILD_OK) {
            nt_builder_free_pack(ctx);
            nt_builder_free_glb_scene(&scene);
            return 1;
        }
        r = add_meshes_and_manifest(ctx, &scene, false);
        if (r != NT_BUILD_OK) {
            nt_builder_free_pack(ctx);
            nt_builder_free_glb_scene(&scene);
            return 1;
        }

        r = nt_builder_finish_pack(ctx);
        nt_builder_free_pack(ctx);
        if (r != NT_BUILD_OK) {
            (void)fprintf(stderr, "Full pack failed: %d\n", r);
            nt_builder_free_glb_scene(&scene);
            return 1;
        }
        (void)printf("Built: sponza_full.ntpack\n\n");
    }

    nt_builder_free_glb_scene(&scene);

    (void)printf("=== Done ===\n");
    return 0;
}
