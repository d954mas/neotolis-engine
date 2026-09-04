#include "graphics/nt_gfx.h"

/* This implementation has no GPU resources or draw state. */
nt_gfx_t g_nt_gfx;

void nt_gfx_register_global_block(const char *name, uint32_t binding_slot) {
    (void)name;
    (void)binding_slot;
}

void nt_gfx_get_global_blocks(const nt_global_block_t **blocks, uint32_t *count) {
    *blocks = NULL;
    *count = 0;
}

void nt_gfx_init(const nt_gfx_desc_t *desc) {
    (void)desc;
    g_nt_gfx = (nt_gfx_t){0};
}

void nt_gfx_shutdown(void) { g_nt_gfx = (nt_gfx_t){0}; }

const nt_gfx_gpu_caps_t *nt_gfx_gpu_caps(void) { return &g_nt_gfx.gpu_caps; }

void nt_gfx_begin_frame(void) {}

void nt_gfx_end_frame(void) {}

void nt_gfx_begin_pass(const nt_pass_desc_t *desc) { (void)desc; }

void nt_gfx_end_pass(void) {}

nt_shader_t nt_gfx_make_shader(const nt_shader_desc_t *desc) {
    (void)desc;
    return (nt_shader_t){0};
}

nt_program_t nt_gfx_make_program(nt_shader_t vs, nt_shader_t fs) {
    (void)vs;
    (void)fs;
    return (nt_program_t){0};
}

nt_pipeline_t nt_gfx_make_pipeline(const nt_pipeline_desc_t *desc) {
    (void)desc;
    return (nt_pipeline_t){0};
}

nt_vertex_input_t nt_gfx_make_vertex_input(const nt_vertex_input_desc_t *desc) {
    (void)desc;
    return (nt_vertex_input_t){0};
}

nt_buffer_t nt_gfx_make_buffer(const nt_buffer_desc_t *desc) {
    (void)desc;
    return (nt_buffer_t){0};
}

nt_texture_t nt_gfx_make_texture(const nt_texture_desc_t *desc) {
    (void)desc;
    return (nt_texture_t){0};
}

nt_sampler_t nt_gfx_make_sampler(const nt_sampler_desc_t *desc) {
    (void)desc;
    return (nt_sampler_t){0};
}

nt_render_target_t nt_gfx_make_render_target(const nt_render_target_desc_t *desc) {
    (void)desc;
    return NT_RENDER_TARGET_INVALID;
}

void nt_gfx_destroy_shader(nt_shader_t shd) { (void)shd; }

void nt_gfx_destroy_program(nt_program_t prog) { (void)prog; }

void nt_gfx_destroy_pipeline(nt_pipeline_t pip) { (void)pip; }

void nt_gfx_destroy_vertex_input(nt_vertex_input_t vi) { (void)vi; }

void nt_gfx_destroy_buffer(nt_buffer_t buf) { (void)buf; }

void nt_gfx_destroy_texture(nt_texture_t tex) { (void)tex; }

void nt_gfx_destroy_render_target(nt_render_target_t rt) { (void)rt; }

bool nt_gfx_resize_render_target(nt_render_target_t rt, uint16_t width, uint16_t height) {
    (void)rt;
    (void)width;
    (void)height;
    return false;
}

nt_texture_t nt_gfx_render_target_color(nt_render_target_t rt) {
    (void)rt;
    return (nt_texture_t){0};
}

nt_texture_t nt_gfx_render_target_depth(nt_render_target_t rt) {
    (void)rt;
    return (nt_texture_t){0};
}

bool nt_gfx_render_target_ready(nt_render_target_t rt) {
    (void)rt;
    return false;
}

bool nt_gfx_texture_ready(nt_texture_t tex) {
    (void)tex;
    return false;
}

bool nt_gfx_shader_ready(nt_shader_t shd) {
    (void)shd;
    return false;
}

bool nt_gfx_program_valid(nt_program_t prog) {
    (void)prog;
    return false;
}

bool nt_gfx_pipeline_valid(nt_pipeline_t pip) {
    (void)pip;
    return false;
}

bool nt_gfx_vertex_input_valid(nt_vertex_input_t vi) {
    (void)vi;
    return false;
}

bool nt_gfx_program_ready(nt_program_t prog) {
    (void)prog;
    return false;
}

nt_program_t nt_gfx_pipeline_program(nt_pipeline_t pip) {
    (void)pip;
    return (nt_program_t){0};
}

bool nt_gfx_texture_size(nt_texture_t tex, uint16_t *out_width, uint16_t *out_height) {
    (void)tex;
    *out_width = 0;
    *out_height = 0;
    return false;
}

nt_texture_format_t nt_gfx_texture_format(nt_texture_t tex) {
    (void)tex;
    return NT_TEXTURE_FORMAT_INVALID;
}

void nt_gfx_bind_pipeline(nt_pipeline_t pip) { (void)pip; }

void nt_gfx_bind_vertex_input(nt_vertex_input_t vi) { (void)vi; }

bool nt_gfx_apply_texture_bindings(const nt_gfx_texture_binding_t *bindings, uint8_t count) {
    (void)bindings;
    (void)count;
    return false;
}

void nt_gfx_set_scissor(int x, int y, int w, int h) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

void nt_gfx_set_scissor_enabled(bool enabled) { (void)enabled; }

bool nt_gfx_scissor_enabled(void) { return false; }

void nt_gfx_set_viewport(int x, int y, int w, int h) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

nt_sampler_t nt_gfx_get_texture_default_sampler(nt_texture_t tex) {
    (void)tex;
    return (nt_sampler_t){0};
}

void nt_gfx_set_uniform_mat4(nt_hash32_t name, const float *matrix) {
    (void)name;
    (void)matrix;
}

void nt_gfx_set_uniform_vec4(nt_hash32_t name, const float *vec) {
    (void)name;
    (void)vec;
}

void nt_gfx_set_uniform_float(nt_hash32_t name, float val) {
    (void)name;
    (void)val;
}

void nt_gfx_set_uniform_int(nt_hash32_t name, int val) {
    (void)name;
    (void)val;
}

void nt_gfx_draw(uint32_t first_vertex, uint32_t num_vertices) {
    (void)first_vertex;
    (void)num_vertices;
}

void nt_gfx_draw_instanced(uint32_t first_vertex, uint32_t num_vertices, uint32_t instance_count) {
    (void)first_vertex;
    (void)num_vertices;
    (void)instance_count;
}

void nt_gfx_draw_indexed(uint32_t first_index, uint32_t num_indices, uint32_t num_vertices) {
    (void)first_index;
    (void)num_indices;
    (void)num_vertices;
}

void nt_gfx_draw_indexed_instanced(uint32_t first_index, uint32_t num_indices, uint32_t num_vertices, uint32_t instance_count) {
    (void)first_index;
    (void)num_indices;
    (void)num_vertices;
    (void)instance_count;
}

uint32_t nt_gfx_get_frame_draw_calls(void) { return 0; }

// Signature follows the public readback API.
// NOLINTNEXTLINE(readability-non-const-parameter)
bool nt_gfx_read_pixels(int x, int y, int w, int h, uint8_t *out, uint32_t out_cap) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)out;
    (void)out_cap;
    return false;
}

void nt_gfx_bind_instance_buffer(nt_buffer_t buf, uint32_t byte_offset) {
    (void)buf;
    (void)byte_offset;
}

void nt_gfx_set_vertex_attrib_default(uint8_t location, float x, float y, float z, float w) {
    (void)location;
    (void)x;
    (void)y;
    (void)z;
    (void)w;
}

void nt_gfx_bind_uniform_buffer(nt_buffer_t buf, uint32_t slot) {
    (void)buf;
    (void)slot;
}

void nt_gfx_update_buffer(nt_buffer_t buf, uint32_t offset, const void *data, uint32_t size) {
    (void)buf;
    (void)offset;
    (void)data;
    (void)size;
}

void nt_gfx_orphan_buffer(nt_buffer_t buf, const void *data, uint32_t size) {
    (void)buf;
    (void)data;
    (void)size;
}

void nt_gfx_begin_segment(const char *name) { (void)name; }

void nt_gfx_end_segment(void) {}

bool nt_gfx_poll_segment_time_ns(const char *name, uint64_t *out_ns) {
    (void)name;
    *out_ns = 0;
    return false;
}

void nt_gfx_set_gpu_timing_enabled(bool enabled) { (void)enabled; }

bool nt_gfx_is_gpu_timing_supported(void) { return false; }

void nt_gfx_update_texture(nt_texture_t tex, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const void *data) {
    (void)tex;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)data;
}

uint32_t nt_gfx_activate_texture(const uint8_t *data, uint32_t size) {
    (void)data;
    (void)size;
    return 0;
}

uint32_t nt_gfx_activate_mesh(const uint8_t *data, uint32_t size) {
    (void)data;
    (void)size;
    return 0;
}

uint32_t nt_gfx_activate_shader(const uint8_t *data, uint32_t size) {
    (void)data;
    (void)size;
    return 0;
}

void nt_gfx_deactivate_texture(uint32_t handle) { (void)handle; }

void nt_gfx_deactivate_mesh(uint32_t handle) { (void)handle; }

void nt_gfx_deactivate_shader(uint32_t handle) { (void)handle; }

const nt_gfx_mesh_info_t *nt_gfx_get_mesh_info(nt_mesh_t mesh) {
    (void)mesh;
    return NULL;
}

uint16_t nt_gfx_max_meshes(void) { return 0; }
