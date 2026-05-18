#ifndef NT_GFX_INTERNAL_H
#define NT_GFX_INTERNAL_H

#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
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
void nt_gfx_backend_orphan_buffer(uint32_t backend_handle, const void *data, uint32_t size);

uint32_t nt_gfx_backend_create_texture(const nt_texture_desc_t *desc);
void nt_gfx_backend_destroy_texture(uint32_t backend_handle);
void nt_gfx_backend_bind_texture(uint32_t backend_handle, uint32_t slot);
void nt_gfx_backend_update_texture(uint32_t backend_handle, uint16_t x, uint16_t y, uint16_t w, uint16_t h, nt_pixel_format_t format, const void *data);

uint32_t nt_gfx_backend_create_sampler(const nt_sampler_desc_t *desc);
void nt_gfx_backend_destroy_sampler(uint32_t backend_handle);
/* slot is the texture unit (0..MAX). backend_handle == 0 unbinds the sampler
 * and reverts to the texture's own filter state. */
void nt_gfx_backend_bind_sampler(uint32_t backend_handle, uint32_t slot);

void nt_gfx_backend_bind_pipeline(uint32_t backend_handle);
void nt_gfx_backend_bind_vertex_buffer(uint32_t backend_handle);
void nt_gfx_backend_bind_index_buffer(uint32_t backend_handle);
void nt_gfx_backend_bind_instance_buffer(uint32_t backend_handle);
void nt_gfx_backend_set_instance_offset(uint32_t byte_offset);
void nt_gfx_backend_set_vertex_attrib_default(uint8_t location, float x, float y, float z, float w);

/* Scissor and viewport (Phase 51 — see nt_gfx.h header comment for convention).
 * Backend implementations:
 *   - gl/nt_gfx_gl.c: glScissor + glEnable/glDisable(GL_SCISSOR_TEST) + glViewport
 *   - stub/nt_gfx_stub.c: no-op (state cached in shared nt_gfx.c for test probes)
 */
void nt_gfx_backend_set_scissor(int x, int y, int w, int h);
void nt_gfx_backend_set_scissor_enabled(bool enabled);
void nt_gfx_backend_set_viewport(int x, int y, int w, int h);

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

/* GPU caps detection — implemented per-backend (gl/nt_gfx_gl_ctx_*.c, stub). */
nt_gfx_gpu_caps_t nt_gfx_gl_ctx_detect_gpu_caps(void);

// #region GPU timer segments
/* Named GPU TIME_ELAPSED segments. begin/end pairs must be sequential —
 * TIME_ELAPSED cannot nest (one query active at a time). Backend hashes
 * the name internally for slot lookup AND emits glPushDebugGroup so the
 * name shows in RenderDoc / Apitrace (KHR_debug; no-op on WebGL2). */
void nt_gfx_backend_begin_segment(const char *name);
void nt_gfx_backend_end_segment(void);
bool nt_gfx_backend_poll_segment_time_ns(const char *name, uint64_t *out_ns);
void nt_gfx_backend_set_gpu_timing_enabled(bool enabled);
bool nt_gfx_backend_is_gpu_timing_supported(void);
/* Called on context loss: forget all segment GL query ids without trying to
 * delete them (context is gone). Next begin_segment lazy-allocates fresh. */
void nt_gfx_backend_drop_timer_segments(void);
// #endregion

#ifdef NT_GFX_STUB_TEST_ACCESS
/* Stub-only test hooks: inspect and reset bind_sampler observations. */
uint32_t nt_gfx_stub_test_last_sampler(uint32_t slot);
uint32_t nt_gfx_stub_test_bind_sampler_count(void);
void nt_gfx_stub_test_reset(void);
#endif

#ifdef NT_GFX_TEST_ACCESS
/* Real-backend test hook: inspect a sampler's GPU backend handle from the
 * dedup cache. Distinct from STUB_TEST_ACCESS — works against the real GL
 * (or any non-stub) backend, so a test running with the real backend can
 * still reach this without enabling stub-only state. */
uint32_t nt_gfx_test_sampler_backend_id(nt_sampler_t s);
#endif

#endif /* NT_GFX_INTERNAL_H */
