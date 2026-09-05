#ifndef NT_GFX_FAKE_H
#define NT_GFX_FAKE_H

#include "graphics/nt_gfx.h"

/* Subsequent links copy this sampler list; reset clears observations, not this list.
 * Needed directly only where the engine links the program itself (postfx, text). */
void nt_gfx_fake_set_samplers(const char *const *names, uint8_t count);
void nt_gfx_fake_set_samplers_typed(const char *const *names, const uint8_t *sampler_classes, uint8_t count);

/* Links a program whose active samplers are exactly `names`. The fake ignores shader
 * source, so the stages are placeholders. */
nt_program_t nt_gfx_fake_make_program(const char *const *names, uint8_t count);
nt_program_t nt_gfx_fake_make_program_typed(const char *const *names, const uint8_t *sampler_classes, uint8_t count);

/* Test-only backend observations and failure injection. */
uint32_t nt_gfx_fake_last_sampler(uint32_t slot);
uint32_t nt_gfx_fake_bind_sampler_count(void);
uint32_t nt_gfx_fake_set_scissor_enabled_count(void);
uint32_t nt_gfx_fake_last_pass_target(void);
uint32_t nt_gfx_fake_pass_target_count(void);
uint32_t nt_gfx_fake_pass_target_at(uint32_t index);
uint32_t nt_gfx_fake_bound_texture_count(void);
uint32_t nt_gfx_fake_bound_texture_at(uint32_t index);
uint32_t nt_gfx_fake_bound_texture_slot_at(uint32_t index);
uint32_t nt_gfx_fake_render_target_create_count(void);
uint32_t nt_gfx_fake_render_target_resize_count(void);
uint32_t nt_gfx_fake_render_target_destroy_count(void);
uint32_t nt_gfx_fake_texture_create_count(void);
uint32_t nt_gfx_fake_program_create_count(void);
uint32_t nt_gfx_fake_pipeline_create_count(void);
uint32_t nt_gfx_fake_bind_pipeline_count(void);
uint32_t nt_gfx_fake_uniform_int_count(void);
uint32_t nt_gfx_fake_uniform_int_hash_at(uint32_t index);
int nt_gfx_fake_uniform_int_value_at(uint32_t index);
uint32_t nt_gfx_fake_uniform_vec4_count(void);
uint32_t nt_gfx_fake_uniform_vec4_hash_at(uint32_t index);
void nt_gfx_fake_uniform_vec4_value_at(uint32_t index, float out[4]);
void nt_gfx_fake_fail_next_program_create(void);
void nt_gfx_fake_lose_context_on_program_create(void);
void nt_gfx_fake_fail_next_pipeline_create(void);
uint16_t nt_gfx_fake_last_render_target_width(void);
uint16_t nt_gfx_fake_last_render_target_height(void);
nt_render_target_depth_t nt_gfx_fake_last_render_target_depth(void);
nt_texture_desc_t nt_gfx_fake_last_texture_desc(void);
uint32_t nt_gfx_fake_last_depth_texture_backend(void);
uint32_t nt_gfx_fake_update_texture_count(void);
uint32_t nt_gfx_fake_update_buffer_count(void);
uint32_t nt_gfx_fake_backend_restore_count(void);
uint32_t nt_gfx_fake_gpu_caps_probe_count(void);
void nt_gfx_fake_fail_texture_creates(uint8_t mask);
void nt_gfx_fake_fail_buffer_creates(uint8_t mask);
void nt_gfx_fake_fail_next_backend_restore(void);
void nt_gfx_fake_fail_next_render_target_create(void);
void nt_gfx_fake_fail_next_render_target_resize(void);
void nt_gfx_fake_set_context_lost(bool lost);
uint32_t nt_gfx_fake_last_update_buffer_offset(void);
uint32_t nt_gfx_fake_last_instance_offset(void);
uint32_t nt_gfx_fake_last_instance_vertex_input(void);
nt_blend_state_t nt_gfx_fake_last_pipeline_blend(void);
uint32_t nt_gfx_fake_vertex_input_create_count(void);
uint32_t nt_gfx_fake_bind_vertex_input_count(void);
uint32_t nt_gfx_fake_last_bound_vertex_input(void);
uint32_t nt_gfx_fake_last_uniform_program(void);
void nt_gfx_fake_fail_next_vertex_input_create(void);
void nt_gfx_fake_reset(void);

#endif
