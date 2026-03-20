/* clang-format off */
#include "nt_builder_internal.h"
#include "hash/nt_hash.h"
#include "nt_mesh_format.h"
#include "cgltf.h"
/* clang-format on */

/* --- Stub implementations (full implementation in Task 2) --- */

nt_build_result_t nt_builder_parse_glb_scene(nt_glb_scene_t *scene, const char *path) {
    (void)scene;
    (void)path;
    (void)fprintf(stderr, "ERROR: nt_builder_parse_glb_scene not yet implemented\n");
    return NT_BUILD_ERR_VALIDATION;
}

void nt_builder_free_glb_scene(nt_glb_scene_t *scene) { (void)scene; }

nt_build_result_t nt_builder_add_scene_mesh(NtBuilderContext *ctx, const nt_glb_scene_t *scene, uint32_t mesh_index, uint32_t primitive_index, const char *resource_id, const nt_mesh_opts_t *opts) {
    (void)ctx;
    (void)scene;
    (void)mesh_index;
    (void)primitive_index;
    (void)resource_id;
    (void)opts;
    (void)fprintf(stderr, "ERROR: nt_builder_add_scene_mesh not yet implemented\n");
    return NT_BUILD_ERR_VALIDATION;
}

nt_build_result_t nt_builder_import_scene_mesh(NtBuilderContext *ctx, const nt_glb_scene_t *scene, uint32_t mesh_index, uint32_t primitive_index, const NtStreamLayout *layout, uint32_t stream_count,
                                               nt_tangent_mode_t tangent_mode, uint64_t resource_id) {
    (void)ctx;
    (void)scene;
    (void)mesh_index;
    (void)primitive_index;
    (void)layout;
    (void)stream_count;
    (void)tangent_mode;
    (void)resource_id;
    (void)fprintf(stderr, "ERROR: nt_builder_import_scene_mesh not yet implemented\n");
    return NT_BUILD_ERR_VALIDATION;
}
