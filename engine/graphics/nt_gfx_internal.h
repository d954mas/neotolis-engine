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

/* destroy_* accepts 0 (no-op, as glDelete*); bind_sampler accepts 0 as an
 * explicit unbind; every other bind requires a live handle -- the front-end
 * owns husk handling.
 * The backend keeps GL-mirror state only: what is logically bound lives in the
 * front-end, which names the program or vertex input a call operates on. */

bool nt_gfx_backend_init(const nt_gfx_desc_t *desc);
void nt_gfx_backend_shutdown(void);
bool nt_gfx_backend_is_context_lost(void);

void nt_gfx_backend_begin_frame(void);
void nt_gfx_backend_end_frame(void);
void nt_gfx_backend_begin_pass(const nt_pass_desc_t *desc, uint32_t render_target_backend);
void nt_gfx_backend_end_pass(void);

uint32_t nt_gfx_backend_create_shader(const nt_shader_desc_t *desc);
void nt_gfx_backend_destroy_shader(uint32_t backend_handle);

/* Links the pair and caches its uniform locations. Returns 0 on link failure. */
uint32_t nt_gfx_backend_create_program(uint32_t vs_backend, uint32_t fs_backend);
void nt_gfx_backend_destroy_program(uint32_t backend_handle);

/* `slot` is the frontend pool slot: the pool owns allocation, the backend
 * table mirrors it 1:1 -- returns exactly `slot`, or 0 on failure. Callers
 * addressing a pipeline's program by slot depend on that identity. */
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
/* slot is the texture unit (0..MAX). backend_handle == 0 unbinds the sampler
 * and reverts to the texture's own filter state. */
void nt_gfx_backend_bind_sampler(uint32_t backend_handle, uint32_t slot);

void nt_gfx_backend_bind_pipeline(uint32_t backend_handle);
/* Re-points the named vertex input's instance attribs at byte_offset. */
void nt_gfx_backend_bind_instance_buffer(uint32_t vertex_input_backend, uint32_t buffer_backend, uint32_t byte_offset);
void nt_gfx_backend_set_vertex_attrib_default(uint8_t location, float x, float y, float z, float w);

/* Scissor and viewport (see nt_gfx.h for convention).
 * Backend implementations:
 *   - gl/nt_gfx_gl.c: glScissor + glEnable/glDisable(GL_SCISSOR_TEST) + glViewport
 *   - stub/nt_gfx_stub.c: no-op (state cached in shared nt_gfx.c for test probes)
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

#ifdef NT_TEST_ACCESS
/* Stub-only test hooks: inspect and reset bind_sampler observations. */
uint32_t nt_gfx_stub_test_last_sampler(uint32_t slot);
uint32_t nt_gfx_stub_test_bind_sampler_count(void);
uint32_t nt_gfx_stub_test_set_scissor_enabled_count(void);
uint32_t nt_gfx_stub_test_last_pass_target(void);
uint32_t nt_gfx_stub_test_pass_target_count(void);
uint32_t nt_gfx_stub_test_pass_target_at(uint32_t index);
uint32_t nt_gfx_stub_test_bound_texture_count(void);
uint32_t nt_gfx_stub_test_bound_texture_at(uint32_t index);
uint32_t nt_gfx_stub_test_render_target_create_count(void);
uint32_t nt_gfx_stub_test_render_target_resize_count(void);
uint32_t nt_gfx_stub_test_render_target_destroy_count(void);
uint32_t nt_gfx_stub_test_texture_create_count(void);
uint32_t nt_gfx_stub_test_program_create_count(void);
uint32_t nt_gfx_stub_test_pipeline_create_count(void);
uint32_t nt_gfx_stub_test_bind_pipeline_count(void);
uint32_t nt_gfx_stub_test_uniform_int_count(void);
uint32_t nt_gfx_stub_test_uniform_int_hash_at(uint32_t index);
int nt_gfx_stub_test_uniform_int_value_at(uint32_t index);
uint32_t nt_gfx_stub_test_uniform_vec4_count(void);
uint32_t nt_gfx_stub_test_uniform_vec4_hash_at(uint32_t index);
void nt_gfx_stub_test_uniform_vec4_value_at(uint32_t index, float out[4]);
void nt_gfx_stub_test_fail_next_program_create(void);
void nt_gfx_stub_test_lose_context_on_program_create(void);
void nt_gfx_stub_test_fail_next_pipeline_create(void);
uint16_t nt_gfx_stub_test_last_render_target_width(void);
uint16_t nt_gfx_stub_test_last_render_target_height(void);
nt_render_target_depth_t nt_gfx_stub_test_last_render_target_depth(void);
nt_texture_desc_t nt_gfx_stub_test_last_texture_desc(void);
uint32_t nt_gfx_stub_test_last_depth_texture_backend(void);
uint32_t nt_gfx_stub_test_update_texture_count(void);
uint32_t nt_gfx_stub_test_update_buffer_count(void);
uint32_t nt_gfx_stub_test_backend_restore_count(void);
uint32_t nt_gfx_stub_test_gpu_caps_probe_count(void);
void nt_gfx_stub_test_fail_texture_creates(uint8_t mask);
void nt_gfx_stub_test_fail_buffer_creates(uint8_t mask);
void nt_gfx_stub_test_fail_next_backend_restore(void);
void nt_gfx_stub_test_fail_next_render_target_create(void);
void nt_gfx_stub_test_fail_next_render_target_resize(void);
void nt_gfx_stub_test_set_context_lost(bool lost);
uint32_t nt_gfx_stub_test_last_update_buffer_offset(void);
uint32_t nt_gfx_stub_test_last_instance_offset(void);
uint32_t nt_gfx_stub_test_last_instance_vertex_input(void);
nt_blend_state_t nt_gfx_stub_test_last_pipeline_blend(void);
uint32_t nt_gfx_stub_test_vertex_input_create_count(void);
uint32_t nt_gfx_stub_test_bind_vertex_input_count(void);
uint32_t nt_gfx_stub_test_last_bound_vertex_input(void);
uint32_t nt_gfx_stub_test_last_uniform_program(void);
void nt_gfx_stub_test_fail_next_vertex_input_create(void);
void nt_gfx_stub_test_reset(void);
#endif

#ifdef NT_TEST_ACCESS
/* GL-backend-only counters (defined in gl/nt_gfx_gl.c; link only from tests
 * using the real GL backend). Static = divisor-0 glVertexAttribPointer calls,
 * issued only at vertex-input creation; instance = divisor-1 calls,
 * legitimately per-draw. Steady-state frames must show static == 0. */
void nt_gfx_gl_test_reset_counters(void);
uint32_t nt_gfx_gl_test_static_attrib_pointer_calls(void);
uint32_t nt_gfx_gl_test_instance_attrib_pointer_calls(void);
uint32_t nt_gfx_gl_test_vao_binds(void);
/* Raw GL-mirror reads: a test can pin that destroy cleared an entry without
 * depending on the driver recycling the deleted GL name. */
uint32_t nt_gfx_gl_test_cached_vao(void);
uint32_t nt_gfx_gl_test_cached_program(void);
uint32_t nt_gfx_gl_test_cached_texture(uint32_t slot);
#endif

#ifdef NT_TEST_ACCESS
/* Real-backend test hook: inspect a sampler's GPU backend handle from the
 * dedup cache. Distinct from STUB_TEST_ACCESS — works against the real GL
 * (or any non-stub) backend, so a test running with the real backend can
 * still reach this without enabling stub-only state. */
uint32_t nt_gfx_test_sampler_backend_id(nt_sampler_t s);
uint32_t nt_gfx_test_texture_backend_id(nt_texture_t tex);
uint32_t nt_gfx_test_render_target_backend_id(nt_render_target_t rt);
/* Pass-scoped bound state, read from its owner: the front-end. */
uint32_t nt_gfx_test_bound_pipeline_backend(void);
uint32_t nt_gfx_test_bound_vertex_input(void);
#endif

#endif /* NT_GFX_INTERNAL_H */
