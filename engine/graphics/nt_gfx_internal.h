#ifndef NT_GFX_INTERNAL_H
#define NT_GFX_INTERNAL_H

#include "graphics/nt_gfx.h"
#include "pool/nt_pool.h"

/* ---- Render state machine ---- */

typedef enum {
    NT_GFX_STATE_IDLE = 0,
    NT_GFX_STATE_FRAME,
    NT_GFX_STATE_PASS,
} nt_gfx_render_state_t;

/* ---- Backend function signatures (implemented by each backend) ---- */

bool nt_gfx_backend_init(const nt_gfx_desc_t *desc);
void nt_gfx_backend_shutdown(void);
bool nt_gfx_backend_is_context_lost(void);

void nt_gfx_backend_begin_frame(void);
void nt_gfx_backend_end_frame(void);
void nt_gfx_backend_begin_pass(const nt_pass_desc_t *desc);
void nt_gfx_backend_end_pass(void);

uint32_t nt_gfx_backend_create_shader(const nt_shader_desc_t *desc);
void nt_gfx_backend_destroy_shader(uint32_t backend_handle);

uint32_t nt_gfx_backend_create_pipeline(const nt_pipeline_desc_t *desc, uint32_t vs_backend, uint32_t fs_backend);
void nt_gfx_backend_destroy_pipeline(uint32_t backend_handle);

uint32_t nt_gfx_backend_create_buffer(const nt_buffer_desc_t *desc);
void nt_gfx_backend_destroy_buffer(uint32_t backend_handle);
void nt_gfx_backend_update_buffer(uint32_t backend_handle, const void *data, uint32_t size);
void nt_gfx_backend_update_texture(uint32_t backend_handle, uint32_t x, uint32_t y, uint32_t w, uint32_t h, nt_pixel_format_t format, const void *data);

uint32_t nt_gfx_backend_create_texture(const nt_texture_desc_t *desc);
void nt_gfx_backend_destroy_texture(uint32_t backend_handle);
void nt_gfx_backend_bind_texture(uint32_t backend_handle, uint32_t slot);

void nt_gfx_backend_bind_pipeline(uint32_t backend_handle);
void nt_gfx_backend_bind_vertex_buffer(uint32_t backend_handle);
void nt_gfx_backend_bind_index_buffer(uint32_t backend_handle);
void nt_gfx_backend_bind_instance_buffer(uint32_t backend_handle);
void nt_gfx_backend_set_instance_offset(uint32_t byte_offset);
void nt_gfx_backend_set_vertex_attrib_default(uint8_t location, float x, float y, float z, float w);
void nt_gfx_backend_bind_uniform_buffer(uint32_t backend_handle, uint32_t slot);
void nt_gfx_backend_set_uniform_block(uint32_t pipeline_backend, const char *block_name, uint32_t slot);

void nt_gfx_backend_set_uniform_mat4(const char *name, const float *matrix);
void nt_gfx_backend_set_uniform_vec4(const char *name, const float *vec);
void nt_gfx_backend_set_uniform_float(const char *name, float val);
void nt_gfx_backend_set_uniform_int(const char *name, int val);

void nt_gfx_backend_draw(uint32_t first_vertex, uint32_t num_vertices);
void nt_gfx_backend_draw_indexed(uint32_t first_index, uint32_t num_indices, uint8_t index_type);
void nt_gfx_backend_draw_instanced(uint32_t first_vertex, uint32_t num_vertices, uint32_t instance_count);
void nt_gfx_backend_draw_indexed_instanced(uint32_t first_index, uint32_t num_indices, uint32_t instance_count, uint8_t index_type);

bool nt_gfx_backend_recreate_all_resources(void);

/* Compressed texture creation (per-mip transcode + glCompressedTexImage2D upload) */
uint32_t nt_gfx_backend_create_texture_compressed(const uint8_t *basis_data, uint32_t basis_size, uint32_t base_width, uint32_t base_height, uint32_t level_count, nt_texture_filter_t min_filter,
                                                  nt_texture_filter_t mag_filter, nt_texture_wrap_t wrap_u, nt_texture_wrap_t wrap_v,
                                                  uint32_t transcode_target /* nt_basisu_format_t cast to uint32_t */);

/* GPU caps detection (implemented per-platform in gl/nt_gfx_gl_ctx_*.c and stub) */
nt_gfx_gpu_caps_t nt_gfx_gl_ctx_detect_gpu_caps(void);

#endif /* NT_GFX_INTERNAL_H */
