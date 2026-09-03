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

/* destroy_* accepts 0 (no-op, as glDelete*); bind_sampler accepts 0 as an unbind;
 * every other bind requires a live handle -- the front-end owns husk handling.
 * The backend keeps GL-mirror state only; calls name the program or vertex input. */

bool nt_gfx_backend_init(const nt_gfx_desc_t *desc);
void nt_gfx_backend_shutdown(void);
bool nt_gfx_backend_is_context_lost(void);

void nt_gfx_backend_begin_frame(void);
void nt_gfx_backend_end_frame(void);
void nt_gfx_backend_begin_pass(const nt_pass_desc_t *desc, uint32_t render_target_backend);
void nt_gfx_backend_end_pass(void);

uint32_t nt_gfx_backend_create_shader(const nt_shader_desc_t *desc);
void nt_gfx_backend_destroy_shader(uint32_t backend_handle);

/* Links the pair, caches its uniform locations and fixes one texture unit per
 * active sampler element (reflection order, 0..n-1, n <= NT_GFX_MAX_TEXTURE_SLOTS).
 * Returns 0 on link failure. */
uint32_t nt_gfx_backend_create_program(uint32_t vs_backend, uint32_t fs_backend);
void nt_gfx_backend_destroy_program(uint32_t backend_handle);

/* Sampler units are program state, written once at link. -1 = not a sampler of
 * that program; the mask carries every unit the program samples. */
int nt_gfx_backend_program_sampler_unit(uint32_t program_backend, uint32_t name_hash);
uint32_t nt_gfx_backend_program_sampler_mask(uint32_t program_backend);

/* `slot` is the frontend pool slot: the pool owns allocation, the backend table
 * mirrors it 1:1 and the front-end addresses records by that slot -- returns
 * exactly `slot`, or 0 on failure. */
uint32_t nt_gfx_backend_create_pipeline(const nt_pipeline_desc_t *desc, uint32_t program_backend, uint32_t slot);
void nt_gfx_backend_destroy_pipeline(uint32_t backend_handle);

/* Bakes the desc's layouts and the given buffer backends into an owned VAO.
 * Restores the previously bound VAO before returning. Same slot contract as
 * create_pipeline: returns `slot`, or 0 on failure. */
uint32_t nt_gfx_backend_create_vertex_input(const nt_vertex_input_desc_t *desc, uint32_t vbo_backend, uint32_t ibo_backend, uint32_t slot);
void nt_gfx_backend_destroy_vertex_input(uint32_t backend_handle);
void nt_gfx_backend_bind_vertex_input(uint32_t backend_handle);

uint32_t nt_gfx_backend_create_buffer(const nt_buffer_desc_t *desc);
void nt_gfx_backend_destroy_buffer(uint32_t backend_handle);
void nt_gfx_backend_update_buffer(uint32_t backend_handle, uint32_t offset, const void *data, uint32_t size);
void nt_gfx_backend_orphan_buffer(uint32_t backend_handle, const void *data, uint32_t size);

uint32_t nt_gfx_backend_create_texture(const nt_texture_desc_t *desc);
void nt_gfx_backend_destroy_texture(uint32_t backend_handle);
void nt_gfx_backend_bind_texture(uint32_t backend_handle, uint32_t slot);
void nt_gfx_backend_update_texture(uint32_t backend_handle, uint16_t x, uint16_t y, uint16_t w, uint16_t h, nt_texture_format_t format, const void *data);

uint32_t nt_gfx_backend_create_render_target(const nt_render_target_desc_t *desc, uint32_t color_backend, uint32_t depth_texture_backend);
bool nt_gfx_backend_resize_render_target(uint32_t backend_handle, const nt_render_target_desc_t *desc, uint32_t color_backend, uint32_t depth_texture_backend);
void nt_gfx_backend_destroy_render_target(uint32_t backend_handle);

uint32_t nt_gfx_backend_create_sampler(const nt_sampler_desc_t *desc);
void nt_gfx_backend_destroy_sampler(uint32_t backend_handle);
/* slot is the texture unit (0..MAX). Always paired with bind_texture on that
 * unit; backend_handle == 0 (revert to the texture's own filter state) is left
 * for backend ground state, the front-end always resolves a real sampler. */
void nt_gfx_backend_bind_sampler(uint32_t backend_handle, uint32_t slot);

void nt_gfx_backend_bind_pipeline(uint32_t backend_handle);
/* Re-points the named vertex input's instance attribs at byte_offset. */
void nt_gfx_backend_bind_instance_buffer(uint32_t vertex_input_backend, uint32_t buffer_backend, uint32_t byte_offset);
void nt_gfx_backend_set_vertex_attrib_default(uint8_t location, float x, float y, float z, float w);

/* Scissor and viewport (see nt_gfx.h for convention).
 * Backend implementations:
 *   - gl/nt_gfx_gl.c: glScissor + glEnable/glDisable(GL_SCISSOR_TEST) + glViewport
 *   - test backend: no-op (state cached in shared nt_gfx.c for test probes)
 * One owner per state: the scissor-enable dedup is front-end (nt_gfx.c mirrors it),
 * the viewport and clear-value dedup is the backend GL cache.
 */
void nt_gfx_backend_set_scissor(int x, int y, int w, int h);
void nt_gfx_backend_set_scissor_enabled(bool enabled);
void nt_gfx_backend_set_viewport(int x, int y, int w, int h);

/* Framebuffer readback. Writes w*h rgba8 pixels into out_rgba8 in raw GL
 * bottom-left order; the single Y-flip to top-left is done in the shared
 * nt_gfx.c layer. Returns false on read failure so the caller never encodes garbage. */
bool nt_gfx_backend_read_pixels(int x, int y, int w, int h, void *out_rgba8);

void nt_gfx_backend_bind_uniform_buffer(uint32_t backend_handle, uint32_t slot);
void nt_gfx_backend_set_uniform_block(uint32_t program_backend, const char *block_name, uint32_t slot);

/* Uniform locations and values are program state, so the write names its
 * program; it must be the one currently bound. */
void nt_gfx_backend_set_uniform_mat4(uint32_t program_backend, uint32_t name_hash, const float *matrix);
void nt_gfx_backend_set_uniform_vec4(uint32_t program_backend, uint32_t name_hash, const float *vec);
void nt_gfx_backend_set_uniform_float(uint32_t program_backend, uint32_t name_hash, float val);
void nt_gfx_backend_set_uniform_int(uint32_t program_backend, uint32_t name_hash, int val);

void nt_gfx_backend_draw(uint32_t first_vertex, uint32_t num_vertices);
void nt_gfx_backend_draw_indexed(uint32_t first_index, uint32_t num_indices, uint8_t index_type);
void nt_gfx_backend_draw_instanced(uint32_t first_vertex, uint32_t num_vertices, uint32_t instance_count);
void nt_gfx_backend_draw_indexed_instanced(uint32_t first_index, uint32_t num_indices, uint32_t instance_count, uint8_t index_type);

bool nt_gfx_backend_recreate_all_resources(void);

/* Compressed texture creation (per-mip transcode + glCompressedTexImage2D upload) */
uint32_t nt_gfx_backend_create_texture_compressed(const uint8_t *basis_data, uint32_t basis_size, uint32_t base_width, uint32_t base_height, uint32_t level_count, nt_texture_filter_t min_filter,
                                                  nt_texture_filter_t mag_filter, nt_texture_wrap_t wrap_u, nt_texture_wrap_t wrap_v,
                                                  uint32_t transcode_target /* nt_basisu_format_t cast to uint32_t */);

/* GPU caps detection — implemented per-backend. */
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

#ifdef NT_TEST_ACCESS
/* GL-backend-only counters (defined in gl/nt_gfx_gl.c; link only from tests
 * using the real GL backend). Static = divisor-0 glVertexAttribPointer calls,
 * issued only at vertex-input creation; instance = divisor-1 calls,
 * legitimately per-draw. Steady-state frames must show static == 0. */
void nt_gfx_gl_test_reset_counters(void);
uint32_t nt_gfx_gl_test_static_attrib_pointer_calls(void);
uint32_t nt_gfx_gl_test_instance_attrib_pointer_calls(void);
uint32_t nt_gfx_gl_test_vao_binds(void);
/* glBindSampler calls that reached GL; a deduplicated bind does not count. */
uint32_t nt_gfx_gl_test_sampler_binds(void);
/* Raw GL-mirror reads: a test can pin that destroy cleared an entry without
 * depending on the driver recycling the deleted GL name. */
uint32_t nt_gfx_gl_test_cached_vao(void);
uint32_t nt_gfx_gl_test_cached_program(void);
uint32_t nt_gfx_gl_test_cached_texture(uint32_t slot);
uint32_t nt_gfx_gl_test_cached_sampler(uint32_t slot);
#endif

#ifdef NT_TEST_ACCESS
/* Backend handles owned by the frontend caches. */
uint32_t nt_gfx_test_sampler_backend_id(nt_sampler_t s);
uint32_t nt_gfx_test_texture_backend_id(nt_texture_t tex);
uint32_t nt_gfx_test_render_target_backend_id(nt_render_target_t rt);
/* Pass-scoped bound state, read from its owner: the front-end. */
uint32_t nt_gfx_test_bound_pipeline(void);
uint32_t nt_gfx_test_bound_vertex_input(void);
#endif

#endif /* NT_GFX_INTERNAL_H */
