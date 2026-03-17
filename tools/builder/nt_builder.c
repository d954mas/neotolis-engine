#include "nt_builder_internal.h"
#include "nt_crc32.h"

/* Stub -- full implementation in Task 2 */

NtBuilderContext *nt_builder_start_pack(const char *output_path) {
    (void)output_path;
    return NULL;
}

nt_build_result_t nt_builder_finish_pack(NtBuilderContext *ctx) {
    (void)ctx;
    return NT_BUILD_ERR_VALIDATION;
}

nt_build_result_t nt_builder_append_data(NtBuilderContext *ctx, const void *data, uint32_t size) {
    (void)ctx;
    (void)data;
    (void)size;
    return NT_BUILD_ERR_VALIDATION;
}

nt_build_result_t nt_builder_register_asset(NtBuilderContext *ctx, const char *path, uint32_t resource_id, nt_asset_type_t type, uint16_t format_version, uint32_t data_size) {
    (void)ctx;
    (void)path;
    (void)resource_id;
    (void)type;
    (void)format_version;
    (void)data_size;
    return NT_BUILD_ERR_VALIDATION;
}

nt_build_result_t nt_builder_add_mesh(NtBuilderContext *ctx, const char *path, const NtStreamLayout *layout, uint32_t stream_count) {
    (void)ctx;
    (void)path;
    (void)layout;
    (void)stream_count;
    return NT_BUILD_ERR_VALIDATION;
}

nt_build_result_t nt_builder_add_mesh_with_id(NtBuilderContext *ctx, const char *path, const NtStreamLayout *layout, uint32_t stream_count, uint32_t resource_id) {
    (void)ctx;
    (void)path;
    (void)layout;
    (void)stream_count;
    (void)resource_id;
    return NT_BUILD_ERR_VALIDATION;
}

nt_build_result_t nt_builder_add_texture(NtBuilderContext *ctx, const char *path) {
    (void)ctx;
    (void)path;
    return NT_BUILD_ERR_VALIDATION;
}

nt_build_result_t nt_builder_add_texture_with_id(NtBuilderContext *ctx, const char *path, uint32_t resource_id) {
    (void)ctx;
    (void)path;
    (void)resource_id;
    return NT_BUILD_ERR_VALIDATION;
}

nt_build_result_t nt_builder_add_shader(NtBuilderContext *ctx, const char *path, nt_build_shader_stage_t stage) {
    (void)ctx;
    (void)path;
    (void)stage;
    return NT_BUILD_ERR_VALIDATION;
}

nt_build_result_t nt_builder_add_shader_with_id(NtBuilderContext *ctx, const char *path, nt_build_shader_stage_t stage, uint32_t resource_id) {
    (void)ctx;
    (void)path;
    (void)stage;
    (void)resource_id;
    return NT_BUILD_ERR_VALIDATION;
}

nt_build_result_t nt_builder_add_meshes(NtBuilderContext *ctx, const char *pattern, const NtStreamLayout *layout, uint32_t stream_count) {
    (void)ctx;
    (void)pattern;
    (void)layout;
    (void)stream_count;
    return NT_BUILD_ERR_VALIDATION;
}

nt_build_result_t nt_builder_add_textures(NtBuilderContext *ctx, const char *pattern) {
    (void)ctx;
    (void)pattern;
    return NT_BUILD_ERR_VALIDATION;
}

nt_build_result_t nt_builder_add_shaders(NtBuilderContext *ctx, const char *pattern, nt_build_shader_stage_t stage) {
    (void)ctx;
    (void)pattern;
    (void)stage;
    return NT_BUILD_ERR_VALIDATION;
}
