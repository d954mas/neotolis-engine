#include "graphics/nt_gfx_internal.h"

/* No-op backend for headless builds and testing.
   Create functions return 1 (nonzero) so make_shader/pipeline/buffer succeed. */

bool nt_gfx_backend_init(const nt_gfx_desc_t *desc) {
    (void)desc;
    return true;
}

void nt_gfx_backend_shutdown(void) {}

bool nt_gfx_backend_is_context_lost(void) { return false; }

void nt_gfx_backend_begin_frame(void) {}

void nt_gfx_backend_end_frame(void) {}

void nt_gfx_backend_begin_pass(const nt_pass_desc_t *desc) { (void)desc; }

void nt_gfx_backend_end_pass(void) {}

uint32_t nt_gfx_backend_create_shader(const nt_shader_desc_t *desc) {
    (void)desc;
    return 1;
}

void nt_gfx_backend_destroy_shader(uint32_t backend_handle) { (void)backend_handle; }

uint32_t nt_gfx_backend_create_pipeline(const nt_pipeline_desc_t *desc, uint32_t vs_backend, uint32_t fs_backend) {
    (void)desc;
    (void)vs_backend;
    (void)fs_backend;
    return 1;
}

void nt_gfx_backend_destroy_pipeline(uint32_t backend_handle) { (void)backend_handle; }

uint32_t nt_gfx_backend_create_buffer(const nt_buffer_desc_t *desc) {
    (void)desc;
    return 1;
}

void nt_gfx_backend_destroy_buffer(uint32_t backend_handle) { (void)backend_handle; }

uint32_t nt_gfx_backend_create_texture(const nt_texture_desc_t *desc) {
    (void)desc;
    return 1;
}

void nt_gfx_backend_destroy_texture(uint32_t backend_handle) { (void)backend_handle; }

void nt_gfx_backend_bind_texture(uint32_t backend_handle, uint32_t slot) {
    (void)backend_handle;
    (void)slot;
}

void nt_gfx_backend_update_buffer(uint32_t backend_handle, const void *data, uint32_t size) {
    (void)backend_handle;
    (void)data;
    (void)size;
}

void nt_gfx_backend_bind_pipeline(uint32_t backend_handle) { (void)backend_handle; }

void nt_gfx_backend_bind_vertex_buffer(uint32_t backend_handle) { (void)backend_handle; }

void nt_gfx_backend_bind_index_buffer(uint32_t backend_handle) { (void)backend_handle; }

void nt_gfx_backend_bind_instance_buffer(uint32_t backend_handle) { (void)backend_handle; }

void nt_gfx_backend_set_uniform_mat4(const char *name, const float *matrix) {
    (void)name;
    (void)matrix;
}

void nt_gfx_backend_set_uniform_vec4(const char *name, const float *vec) {
    (void)name;
    (void)vec;
}

void nt_gfx_backend_set_uniform_float(const char *name, float val) {
    (void)name;
    (void)val;
}

void nt_gfx_backend_set_uniform_int(const char *name, int val) {
    (void)name;
    (void)val;
}

void nt_gfx_backend_draw(uint32_t first_vertex, uint32_t num_vertices) {
    (void)first_vertex;
    (void)num_vertices;
}

void nt_gfx_backend_draw_indexed(uint32_t first_index, uint32_t num_indices) {
    (void)first_index;
    (void)num_indices;
}

void nt_gfx_backend_draw_instanced(uint32_t first_vertex, uint32_t num_vertices, uint32_t instance_count) {
    (void)first_vertex;
    (void)num_vertices;
    (void)instance_count;
}

void nt_gfx_backend_draw_indexed_instanced(uint32_t first_index, uint32_t num_indices, uint32_t instance_count) {
    (void)first_index;
    (void)num_indices;
    (void)instance_count;
}

bool nt_gfx_backend_recreate_all_resources(void) { return true; }
