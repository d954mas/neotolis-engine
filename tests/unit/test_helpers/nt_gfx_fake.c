#include "test_helpers/nt_gfx_fake.h"
#include "core/nt_assert.h"
#include "graphics/nt_gfx_internal.h"
#include "hash/nt_hash.h"

#include <stdlib.h>
#include <string.h>

// #region sampler units
typedef struct {
    bool used;
    uint32_t sampler_hashes[NT_GFX_MAX_TEXTURE_SLOTS];
    uint8_t sampler_classes[NT_GFX_MAX_TEXTURE_SLOTS];
    uint8_t sampler_count;
} nt_gfx_fake_program_t;

static nt_gfx_fake_program_t s_fake_program_template;
static nt_gfx_fake_program_t *s_fake_program_table;
static uint32_t s_fake_max_programs;

void nt_gfx_fake_set_samplers(const char *const *names, uint8_t count) { nt_gfx_fake_set_samplers_typed(names, NULL, count); }

void nt_gfx_fake_set_samplers_typed(const char *const *names, const uint8_t *sampler_classes, uint8_t count) {
    NT_ASSERT(count <= NT_GFX_MAX_TEXTURE_SLOTS);
    s_fake_program_template = (nt_gfx_fake_program_t){.used = true, .sampler_count = count};
    for (uint8_t i = 0; i < count; i++) {
        s_fake_program_template.sampler_hashes[i] = nt_hash32_str(names[i]).value;
        s_fake_program_template.sampler_classes[i] = sampler_classes != NULL ? sampler_classes[i] : NT_GFX_SAMPLER_CLASS_FLOAT;
    }
}

nt_program_t nt_gfx_fake_make_program(const char *const *names, uint8_t count) { return nt_gfx_fake_make_program_typed(names, NULL, count); }

nt_program_t nt_gfx_fake_make_program_typed(const char *const *names, const uint8_t *sampler_classes, uint8_t count) {
    nt_gfx_fake_set_samplers_typed(names, sampler_classes, count);
    const nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){}"});
    const nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "void main(){}"});
    return nt_gfx_make_program(vs, fs);
}

static uint32_t fake_alloc_slot(nt_gfx_fake_program_t *table, uint32_t capacity) {
    for (uint32_t i = 1; i <= capacity; i++) {
        if (!table[i].used) {
            memset(&table[i], 0, sizeof(table[i]));
            table[i].used = true;
            return i;
        }
    }
    return 0;
}

bool nt_gfx_backend_program_sampler_info(uint32_t program_backend, uint32_t name_hash, nt_gfx_sampler_info_t *out_info) {
    NT_ASSERT(program_backend != 0 && program_backend <= s_fake_max_programs && s_fake_program_table[program_backend].used && "program_sampler_info: requires a live program");
    NT_ASSERT(out_info != NULL && "program_sampler_info: out_info is required");
    const nt_gfx_fake_program_t *rec = &s_fake_program_table[program_backend];
    for (uint8_t i = 0; i < rec->sampler_count; i++) {
        if (rec->sampler_hashes[i] == name_hash) {
            out_info->unit = i;
            out_info->sampler_class = rec->sampler_classes[i];
            return true;
        }
    }
    return false;
}

uint32_t nt_gfx_backend_program_sampler_mask(uint32_t program_backend) {
    NT_ASSERT(program_backend != 0 && program_backend <= s_fake_max_programs && s_fake_program_table[program_backend].used && "program_sampler_mask: requires a live program");
    return (1U << s_fake_program_table[program_backend].sampler_count) - 1U;
}
// #endregion

#define NT_GFX_FAKE_HISTORY_CAPACITY 16
static uint32_t s_fake_last_sampler[NT_GFX_MAX_TEXTURE_SLOTS];
static uint32_t s_fake_bind_sampler_count;
static uint32_t s_fake_set_scissor_enabled_count;
static uint32_t s_fake_last_pass_target;
static uint32_t s_fake_pass_targets[NT_GFX_FAKE_HISTORY_CAPACITY];
static uint32_t s_fake_pass_target_count;
static uint32_t s_fake_bound_textures[NT_GFX_FAKE_HISTORY_CAPACITY];
static uint32_t s_fake_bound_texture_slots[NT_GFX_FAKE_HISTORY_CAPACITY];
static uint32_t s_fake_bound_texture_count;
static uint32_t s_fake_render_target_create_count;
static uint32_t s_fake_render_target_resize_count;
static uint32_t s_fake_render_target_destroy_count;
static uint32_t s_fake_texture_create_count;
static uint32_t s_fake_program_create_count;
static uint32_t s_fake_pipeline_create_count;
#define NT_GFX_FAKE_UNIFORM_NAMES 16
static uint32_t s_fake_uniform_int_hashes[NT_GFX_FAKE_UNIFORM_NAMES];
static int s_fake_uniform_int_values[NT_GFX_FAKE_UNIFORM_NAMES];
static uint32_t s_fake_uniform_int_count;
static uint32_t s_fake_uniform_vec4_hashes[NT_GFX_FAKE_UNIFORM_NAMES];
static float s_fake_uniform_vec4_values[NT_GFX_FAKE_UNIFORM_NAMES][4];
static uint32_t s_fake_uniform_vec4_count;
static uint32_t s_fake_bind_pipeline_count;
static uint32_t s_fake_update_texture_count;
static uint32_t s_fake_update_buffer_count;
static uint32_t s_fake_backend_restore_count;
static uint32_t s_fake_gpu_caps_probe_count;
static uint16_t s_fake_last_render_target_width;
static uint16_t s_fake_last_render_target_height;
static nt_render_target_depth_t s_fake_last_render_target_depth;
static nt_texture_desc_t s_fake_last_texture_desc;
static uint32_t s_fake_last_depth_texture_backend;
static uint32_t s_fake_next_texture_backend;
static bool s_fake_context_lost;
static bool s_fake_backend_missing;
static uint8_t s_fake_fail_texture_creates;
static uint8_t s_fake_fail_buffer_creates;
static bool s_fake_fail_next_program_create;
static bool s_fake_lose_context_on_program_create;
static bool s_fake_fail_next_pipeline_create;
static bool s_fake_fail_next_backend_restore;
static bool s_fake_fail_next_render_target_create;
static bool s_fake_fail_next_render_target_resize;
static uint32_t s_fake_last_update_buffer_offset;
static uint32_t s_fake_last_instance_offset;
static uint32_t s_fake_last_instance_vertex_input; /* recorder: last VI handle bind_instance_buffer named */
static nt_blend_state_t s_fake_last_pipeline_blend;
static uint32_t s_fake_vertex_input_create_count;
static uint32_t s_fake_bind_vertex_input_count;
static uint32_t s_fake_last_bound_vertex_input; /* recorder: last handle bind_vertex_input received */
static uint32_t s_fake_last_uniform_program;    /* recorder: last program a uniform write named */
static bool s_fake_fail_next_vertex_input_create;
static bool s_fake_fail_next_sampler_create;

uint32_t nt_gfx_fake_last_sampler(uint32_t slot) {
    if (slot >= NT_GFX_MAX_TEXTURE_SLOTS) {
        return 0;
    }
    return s_fake_last_sampler[slot];
}

uint32_t nt_gfx_fake_bind_sampler_count(void) { return s_fake_bind_sampler_count; }
uint32_t nt_gfx_fake_set_scissor_enabled_count(void) { return s_fake_set_scissor_enabled_count; }
uint32_t nt_gfx_fake_last_pass_target(void) { return s_fake_last_pass_target; }
uint32_t nt_gfx_fake_pass_target_count(void) { return s_fake_pass_target_count; }
uint32_t nt_gfx_fake_pass_target_at(uint32_t index) { return index < s_fake_pass_target_count ? s_fake_pass_targets[index] : 0; }
uint32_t nt_gfx_fake_bound_texture_count(void) { return s_fake_bound_texture_count; }
uint32_t nt_gfx_fake_bound_texture_at(uint32_t index) { return index < s_fake_bound_texture_count ? s_fake_bound_textures[index] : 0; }
uint32_t nt_gfx_fake_bound_texture_slot_at(uint32_t index) { return index < s_fake_bound_texture_count ? s_fake_bound_texture_slots[index] : UINT32_MAX; }
uint32_t nt_gfx_fake_render_target_create_count(void) { return s_fake_render_target_create_count; }
uint32_t nt_gfx_fake_render_target_resize_count(void) { return s_fake_render_target_resize_count; }
uint32_t nt_gfx_fake_render_target_destroy_count(void) { return s_fake_render_target_destroy_count; }
uint32_t nt_gfx_fake_texture_create_count(void) { return s_fake_texture_create_count; }
uint32_t nt_gfx_fake_program_create_count(void) { return s_fake_program_create_count; }
uint32_t nt_gfx_fake_pipeline_create_count(void) { return s_fake_pipeline_create_count; }
uint32_t nt_gfx_fake_bind_pipeline_count(void) { return s_fake_bind_pipeline_count; }
uint32_t nt_gfx_fake_uniform_int_count(void) { return s_fake_uniform_int_count; }
uint32_t nt_gfx_fake_uniform_int_hash_at(uint32_t index) { return index < s_fake_uniform_int_count && index < NT_GFX_FAKE_UNIFORM_NAMES ? s_fake_uniform_int_hashes[index] : 0; }
int nt_gfx_fake_uniform_int_value_at(uint32_t index) { return index < s_fake_uniform_int_count && index < NT_GFX_FAKE_UNIFORM_NAMES ? s_fake_uniform_int_values[index] : -1; }
uint32_t nt_gfx_fake_uniform_vec4_count(void) { return s_fake_uniform_vec4_count; }
uint32_t nt_gfx_fake_uniform_vec4_hash_at(uint32_t index) { return index < s_fake_uniform_vec4_count && index < NT_GFX_FAKE_UNIFORM_NAMES ? s_fake_uniform_vec4_hashes[index] : 0; }
void nt_gfx_fake_uniform_vec4_value_at(uint32_t index, float out[4]) {
    const bool valid = index < s_fake_uniform_vec4_count && index < NT_GFX_FAKE_UNIFORM_NAMES;
    for (uint32_t i = 0; i < 4; i++) {
        out[i] = valid ? s_fake_uniform_vec4_values[index][i] : 0.0F;
    }
}
uint32_t nt_gfx_fake_update_texture_count(void) { return s_fake_update_texture_count; }
uint32_t nt_gfx_fake_update_buffer_count(void) { return s_fake_update_buffer_count; }
uint32_t nt_gfx_fake_backend_restore_count(void) { return s_fake_backend_restore_count; }
uint32_t nt_gfx_fake_gpu_caps_probe_count(void) { return s_fake_gpu_caps_probe_count; }
uint16_t nt_gfx_fake_last_render_target_width(void) { return s_fake_last_render_target_width; }
uint16_t nt_gfx_fake_last_render_target_height(void) { return s_fake_last_render_target_height; }
nt_render_target_depth_t nt_gfx_fake_last_render_target_depth(void) { return s_fake_last_render_target_depth; }
nt_texture_desc_t nt_gfx_fake_last_texture_desc(void) { return s_fake_last_texture_desc; }
uint32_t nt_gfx_fake_last_depth_texture_backend(void) { return s_fake_last_depth_texture_backend; }
void nt_gfx_fake_fail_next_render_target_create(void) { s_fake_fail_next_render_target_create = true; }
void nt_gfx_fake_fail_next_render_target_resize(void) { s_fake_fail_next_render_target_resize = true; }
void nt_gfx_fake_fail_texture_creates(uint8_t mask) {
    NT_ASSERT(mask <= 3);
    s_fake_fail_texture_creates = mask;
}
void nt_gfx_fake_fail_buffer_creates(uint8_t mask) {
    NT_ASSERT(mask <= 3);
    s_fake_fail_buffer_creates = mask;
}
void nt_gfx_fake_fail_next_program_create(void) { s_fake_fail_next_program_create = true; }
void nt_gfx_fake_lose_context_on_program_create(void) { s_fake_lose_context_on_program_create = true; }
void nt_gfx_fake_fail_next_pipeline_create(void) { s_fake_fail_next_pipeline_create = true; }
void nt_gfx_fake_fail_next_backend_restore(void) { s_fake_fail_next_backend_restore = true; }
void nt_gfx_fake_set_context_lost(bool lost) { s_fake_context_lost = lost; }
uint32_t nt_gfx_fake_last_update_buffer_offset(void) { return s_fake_last_update_buffer_offset; }
uint32_t nt_gfx_fake_last_instance_offset(void) { return s_fake_last_instance_offset; }
uint32_t nt_gfx_fake_last_instance_vertex_input(void) { return s_fake_last_instance_vertex_input; }
nt_blend_state_t nt_gfx_fake_last_pipeline_blend(void) { return s_fake_last_pipeline_blend; }
uint32_t nt_gfx_fake_vertex_input_create_count(void) { return s_fake_vertex_input_create_count; }
uint32_t nt_gfx_fake_bind_vertex_input_count(void) { return s_fake_bind_vertex_input_count; }
uint32_t nt_gfx_fake_last_bound_vertex_input(void) { return s_fake_last_bound_vertex_input; }
uint32_t nt_gfx_fake_last_uniform_program(void) { return s_fake_last_uniform_program; }
void nt_gfx_fake_fail_next_vertex_input_create(void) { s_fake_fail_next_vertex_input_create = true; }
void nt_gfx_fake_fail_next_sampler_create(void) { s_fake_fail_next_sampler_create = true; }

void nt_gfx_fake_reset(void) {
    for (uint32_t i = 0; i < NT_GFX_MAX_TEXTURE_SLOTS; i++) {
        s_fake_last_sampler[i] = 0;
    }
    s_fake_bind_sampler_count = 0;
    s_fake_set_scissor_enabled_count = 0;
    s_fake_last_pass_target = 0;
    s_fake_pass_target_count = 0;
    s_fake_bound_texture_count = 0;
    s_fake_render_target_create_count = 0;
    s_fake_render_target_resize_count = 0;
    s_fake_render_target_destroy_count = 0;
    s_fake_texture_create_count = 0;
    s_fake_program_create_count = 0;
    s_fake_pipeline_create_count = 0;
    s_fake_uniform_int_count = 0;
    s_fake_uniform_vec4_count = 0;
    s_fake_bind_pipeline_count = 0;
    s_fake_update_texture_count = 0;
    s_fake_update_buffer_count = 0;
    s_fake_backend_restore_count = 0;
    s_fake_gpu_caps_probe_count = 0;
    s_fake_last_render_target_width = 0;
    s_fake_last_render_target_height = 0;
    s_fake_last_render_target_depth = NT_RT_DEPTH_NONE;
    s_fake_last_texture_desc = (nt_texture_desc_t){0};
    s_fake_last_depth_texture_backend = 0;
    s_fake_last_update_buffer_offset = 0;
    s_fake_last_instance_offset = 0;
    s_fake_last_instance_vertex_input = 0;
    s_fake_last_pipeline_blend = (nt_blend_state_t){0};
    s_fake_vertex_input_create_count = 0;
    s_fake_bind_vertex_input_count = 0;
    s_fake_last_bound_vertex_input = 0;
    s_fake_last_uniform_program = 0;
    s_fake_fail_next_vertex_input_create = false;
    s_fake_fail_next_sampler_create = false;
    s_fake_context_lost = false;
    s_fake_backend_missing = false;
    s_fake_fail_texture_creates = 0;
    s_fake_fail_buffer_creates = 0;
    s_fake_fail_next_program_create = false;
    s_fake_lose_context_on_program_create = false;
    s_fake_fail_next_pipeline_create = false;
    s_fake_fail_next_backend_restore = false;
    s_fake_fail_next_render_target_create = false;
    s_fake_fail_next_render_target_resize = false;
}

bool nt_gfx_backend_init(const nt_gfx_desc_t *desc) {
    NT_ASSERT(desc != NULL);
    free(s_fake_program_table);
    s_fake_max_programs = desc->max_programs;
    /* Init-only: a mid-test reset must never re-issue a texture id that is still live. */
    s_fake_next_texture_backend = 0;
    nt_gfx_fake_set_samplers(NULL, 0);
    s_fake_program_table = (nt_gfx_fake_program_t *)calloc((size_t)s_fake_max_programs + 1U, sizeof(nt_gfx_fake_program_t));
    NT_ASSERT(s_fake_program_table && "gfx fake backend init: out of memory");
    return true;
}

void nt_gfx_backend_shutdown(void) {
    free(s_fake_program_table);
    s_fake_program_table = NULL;
    s_fake_max_programs = 0;
}

bool nt_gfx_backend_is_context_lost(void) { return s_fake_context_lost || s_fake_backend_missing; }

void nt_gfx_backend_begin_frame(void) {}

void nt_gfx_backend_end_frame(void) {}

void nt_gfx_backend_begin_pass(const nt_pass_desc_t *desc, uint32_t render_target_backend) {
    (void)desc;
    s_fake_last_pass_target = render_target_backend;
    if (s_fake_pass_target_count < NT_GFX_FAKE_HISTORY_CAPACITY) {
        s_fake_pass_targets[s_fake_pass_target_count++] = render_target_backend;
    }
}

void nt_gfx_backend_end_pass(void) {}

/* Scissor and viewport fake no-ops. State is cached in shared nt_gfx.c
 * so NT_TEST_ACCESS probes can read it back without GL. */
void nt_gfx_backend_set_scissor(int x, int y, int w, int h) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

void nt_gfx_backend_set_scissor_enabled(bool enabled) {
    (void)enabled;
    s_fake_set_scissor_enabled_count++;
}

void nt_gfx_backend_set_viewport(int x, int y, int w, int h) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

/* Synthetic readback: r = GL row, g = column, bottom-left order like glReadPixels,
 * so the shared-layer Y-flip is observable (out row 0 must carry GL row h-1). */
bool nt_gfx_backend_read_pixels(int x, int y, int w, int h, void *out_rgba8) {
    (void)x;
    (void)y;
    uint8_t *p = (uint8_t *)out_rgba8;
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            size_t i = (((size_t)row * (size_t)w) + (size_t)col) * 4U;
            p[i + 0U] = (uint8_t)(row & 0xFF);
            p[i + 1U] = (uint8_t)(col & 0xFF);
            p[i + 2U] = 0x40U;
            p[i + 3U] = 0xFFU;
        }
    }
    return true; /* synthetic fill always writes the full buffer. */
}

uint32_t nt_gfx_backend_create_shader(const nt_shader_desc_t *desc) {
    (void)desc;
    return 1;
}

void nt_gfx_backend_destroy_shader(uint32_t backend_handle) { (void)backend_handle; }

uint32_t nt_gfx_backend_create_program(uint32_t vs_backend, uint32_t fs_backend) {
    s_fake_program_create_count++;
    if (s_fake_lose_context_on_program_create) {
        s_fake_lose_context_on_program_create = false;
        s_fake_context_lost = true;
        return 0;
    }
    if (s_fake_fail_next_program_create) {
        s_fake_fail_next_program_create = false;
        return 0;
    }
    (void)vs_backend;
    (void)fs_backend;
    const uint32_t slot = fake_alloc_slot(s_fake_program_table, s_fake_max_programs);
    NT_ASSERT(slot != 0 && "fake program table full");
    s_fake_program_table[slot] = s_fake_program_template;
    return slot;
}

void nt_gfx_backend_destroy_program(uint32_t backend_handle) {
    if (backend_handle == 0) {
        return;
    }
    NT_ASSERT(backend_handle <= s_fake_max_programs && "destroy_program: handle out of range");
    memset(&s_fake_program_table[backend_handle], 0, sizeof(s_fake_program_table[backend_handle]));
}

uint32_t nt_gfx_backend_create_pipeline(const nt_pipeline_desc_t *desc, uint32_t program_backend, uint32_t slot) {
    if (s_fake_fail_next_pipeline_create) {
        s_fake_fail_next_pipeline_create = false;
        return 0;
    }
    s_fake_pipeline_create_count++;
    s_fake_last_pipeline_blend = desc->blend;
    (void)program_backend;
    return slot;
}

void nt_gfx_backend_destroy_pipeline(uint32_t backend_handle) { (void)backend_handle; }

uint32_t nt_gfx_backend_create_vertex_input(const nt_vertex_input_desc_t *desc, uint32_t vbo_backend, uint32_t ibo_backend, uint32_t slot) {
    (void)desc;
    (void)vbo_backend;
    (void)ibo_backend;
    s_fake_vertex_input_create_count++;
    if (s_fake_fail_next_vertex_input_create) {
        s_fake_fail_next_vertex_input_create = false;
        return 0;
    }
    return slot;
}

void nt_gfx_backend_destroy_vertex_input(uint32_t backend_handle) { (void)backend_handle; }

void nt_gfx_backend_bind_vertex_input(uint32_t backend_handle) {
    NT_ASSERT(backend_handle != 0 && "bind_vertex_input: requires a live handle");
    s_fake_bind_vertex_input_count++;
    s_fake_last_bound_vertex_input = backend_handle;
}

uint32_t nt_gfx_backend_create_buffer(const nt_buffer_desc_t *desc) {
    (void)desc;
    bool fail = (s_fake_fail_buffer_creates & 1U) != 0;
    s_fake_fail_buffer_creates >>= 1U;
    if (fail) {
        return 0;
    }
    return 1;
}

void nt_gfx_backend_destroy_buffer(uint32_t backend_handle) { (void)backend_handle; }

uint32_t nt_gfx_backend_create_texture(const nt_texture_desc_t *desc) {
    s_fake_last_texture_desc = *desc;
    s_fake_texture_create_count++;
    /* One bit per create, consumed in order: mask 1 fails the first, 2 the
     * second, 3 both. */
    bool fail = (s_fake_fail_texture_creates & 1U) != 0;
    s_fake_fail_texture_creates >>= 1U;
    if (fail) {
        return 0;
    }
    return ++s_fake_next_texture_backend;
}

uint32_t nt_gfx_backend_create_texture_compressed(const uint8_t *basis_data, uint32_t basis_size, uint32_t base_width, uint32_t base_height, uint32_t level_count, nt_texture_filter_t min_filter,
                                                  nt_texture_filter_t mag_filter, nt_texture_wrap_t wrap_u, nt_texture_wrap_t wrap_v, uint32_t transcode_target) {
    (void)basis_data;
    (void)basis_size;
    (void)base_width;
    (void)base_height;
    (void)level_count;
    (void)min_filter;
    (void)mag_filter;
    (void)wrap_u;
    (void)wrap_v;
    (void)transcode_target;
    return ++s_fake_next_texture_backend;
}

void nt_gfx_backend_destroy_texture(uint32_t backend_handle) { (void)backend_handle; }

uint32_t nt_gfx_backend_create_render_target(const nt_render_target_desc_t *desc, uint32_t color_backend, uint32_t depth_texture_backend) {
    NT_ASSERT(desc != NULL);
    NT_ASSERT(color_backend != 0);
    (void)color_backend;
    (void)depth_texture_backend;
    s_fake_render_target_create_count++;
    s_fake_last_render_target_depth = desc ? desc->depth_storage : NT_RT_DEPTH_NONE;
    s_fake_last_render_target_width = desc ? desc->width : 0;
    s_fake_last_render_target_height = desc ? desc->height : 0;
    s_fake_last_depth_texture_backend = depth_texture_backend;
    if (s_fake_fail_next_render_target_create) {
        s_fake_fail_next_render_target_create = false;
        return 0;
    }
    return s_fake_render_target_create_count;
}

bool nt_gfx_backend_resize_render_target(uint32_t backend_handle, const nt_render_target_desc_t *desc, uint32_t color_backend, uint32_t depth_texture_backend) {
    (void)color_backend;
    NT_ASSERT(desc != NULL);
    /* A target whose recreate failed keeps handle 0; resize must report failure, not crash. */
    if (backend_handle == 0) {
        return false;
    }
    s_fake_render_target_resize_count++;
    s_fake_last_render_target_depth = desc ? desc->depth_storage : NT_RT_DEPTH_NONE;
    s_fake_last_render_target_width = desc ? desc->width : 0;
    s_fake_last_render_target_height = desc ? desc->height : 0;
    s_fake_last_depth_texture_backend = depth_texture_backend;
    if (s_fake_fail_next_render_target_resize) {
        s_fake_fail_next_render_target_resize = false;
        return false;
    }
    return true;
}

void nt_gfx_backend_destroy_render_target(uint32_t backend_handle) {
    (void)backend_handle;
    s_fake_render_target_destroy_count++;
}

void nt_gfx_backend_bind_texture(uint32_t backend_handle, uint32_t slot) {
    NT_ASSERT(backend_handle != 0 && "bind_texture: requires a live handle");
    if (s_fake_bound_texture_count < NT_GFX_FAKE_HISTORY_CAPACITY) {
        s_fake_bound_texture_slots[s_fake_bound_texture_count] = slot;
        s_fake_bound_textures[s_fake_bound_texture_count++] = backend_handle;
    }
}

void nt_gfx_backend_update_buffer(uint32_t backend_handle, uint32_t offset, const void *data, uint32_t size) {
    (void)backend_handle;
    (void)data;
    (void)size;
    s_fake_last_update_buffer_offset = offset;
    s_fake_update_buffer_count++;
}

void nt_gfx_backend_orphan_buffer(uint32_t backend_handle, const void *data, uint32_t size) {
    (void)backend_handle;
    (void)data;
    (void)size;
}

void nt_gfx_backend_begin_segment(const char *name) { (void)name; }
void nt_gfx_backend_end_segment(void) {}
void nt_gfx_backend_drop_timer_segments(void) {}

// NOLINTNEXTLINE(readability-non-const-parameter) — out param signature must match real backend
bool nt_gfx_backend_poll_segment_time_ns(const char *name, uint64_t *out_ns) {
    (void)name;
    (void)out_ns;
    return false;
}

void nt_gfx_backend_set_gpu_timing_enabled(bool enabled) { (void)enabled; }

bool nt_gfx_backend_is_gpu_timing_supported(void) { return false; }

uint32_t nt_gfx_backend_create_sampler(const nt_sampler_desc_t *desc) {
    (void)desc;
    if (s_fake_fail_next_sampler_create) {
        s_fake_fail_next_sampler_create = false;
        return 0;
    }
    static uint32_t s_counter;
    return ++s_counter; /* unique id so tests can differentiate samplers */
}

void nt_gfx_backend_destroy_sampler(uint32_t backend_handle) { (void)backend_handle; }

void nt_gfx_backend_bind_sampler(uint32_t backend_handle, uint32_t slot) {
    if (slot < NT_GFX_MAX_TEXTURE_SLOTS) {
        s_fake_last_sampler[slot] = backend_handle;
    }
    s_fake_bind_sampler_count++;
}

void nt_gfx_backend_apply_texture_bindings(const nt_gfx_resolved_texture_binding_t bindings[NT_GFX_MAX_TEXTURE_SLOTS], uint8_t active_mask) {
    for (uint8_t unit = 0; unit < NT_GFX_MAX_TEXTURE_SLOTS; unit++) {
        if ((active_mask & (uint8_t)(1U << unit)) == 0) {
            continue;
        }
        nt_gfx_backend_bind_texture(bindings[unit].texture_backend, unit);
        nt_gfx_backend_bind_sampler(bindings[unit].sampler_backend, unit);
    }
}

void nt_gfx_backend_update_texture(uint32_t backend_handle, uint16_t x, uint16_t y, uint16_t w, uint16_t h, nt_texture_format_t format, const void *data) {
    s_fake_update_texture_count++;
    (void)backend_handle;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)format;
    (void)data;
}

void nt_gfx_backend_bind_pipeline(uint32_t backend_handle) {
    NT_ASSERT(backend_handle != 0 && "bind_pipeline: requires a live handle");
    s_fake_bind_pipeline_count++;
    (void)backend_handle;
}

void nt_gfx_backend_bind_instance_buffer(uint32_t vertex_input_backend, uint32_t buffer_backend, uint32_t byte_offset) {
    NT_ASSERT(vertex_input_backend != 0 && buffer_backend != 0 && "bind_instance_buffer: requires live handles");
    (void)buffer_backend;
    s_fake_last_instance_offset = byte_offset;
    s_fake_last_instance_vertex_input = vertex_input_backend;
}

void nt_gfx_backend_set_vertex_attrib_default(uint8_t location, float x, float y, float z, float w) {
    (void)location;
    (void)x;
    (void)y;
    (void)z;
    (void)w;
}

void nt_gfx_backend_bind_uniform_buffer(uint32_t backend_handle, uint32_t slot) {
    NT_ASSERT(backend_handle != 0 && "bind_uniform_buffer: requires a live handle");
    (void)backend_handle;
    (void)slot;
}

void nt_gfx_backend_set_uniform_block(uint32_t program_backend, const char *block_name, uint32_t slot) {
    (void)program_backend;
    (void)block_name;
    (void)slot;
}

void nt_gfx_backend_set_uniform_mat4(uint32_t program_backend, uint32_t name_hash, const float *matrix) {
    s_fake_last_uniform_program = program_backend;
    (void)name_hash;
    (void)matrix;
}

void nt_gfx_backend_set_uniform_vec4(uint32_t program_backend, uint32_t name_hash, const float *vec) {
    s_fake_last_uniform_program = program_backend;
    if (s_fake_uniform_vec4_count < NT_GFX_FAKE_UNIFORM_NAMES) {
        s_fake_uniform_vec4_hashes[s_fake_uniform_vec4_count] = name_hash;
        for (uint32_t i = 0; i < 4; i++) {
            s_fake_uniform_vec4_values[s_fake_uniform_vec4_count][i] = vec[i];
        }
    }
    s_fake_uniform_vec4_count++;
    (void)name_hash;
    (void)vec;
}

void nt_gfx_backend_set_uniform_float(uint32_t program_backend, uint32_t name_hash, float val) {
    s_fake_last_uniform_program = program_backend;
    (void)name_hash;
    (void)val;
}

void nt_gfx_backend_set_uniform_int(uint32_t program_backend, uint32_t name_hash, int val) {
    nt_gfx_sampler_info_t sampler_info = {0};
    NT_ASSERT(!nt_gfx_backend_program_sampler_info(program_backend, name_hash, &sampler_info) && "sampler uniforms are immutable; use nt_gfx_apply_texture_bindings");
    s_fake_last_uniform_program = program_backend;
    if (s_fake_uniform_int_count < NT_GFX_FAKE_UNIFORM_NAMES) {
        s_fake_uniform_int_hashes[s_fake_uniform_int_count] = name_hash;
        s_fake_uniform_int_values[s_fake_uniform_int_count] = val;
    }
    s_fake_uniform_int_count++;
    (void)name_hash;
    (void)val;
}

void nt_gfx_backend_draw(uint32_t first_vertex, uint32_t num_vertices) {
    (void)first_vertex;
    (void)num_vertices;
}

void nt_gfx_backend_draw_indexed(uint32_t first_index, uint32_t num_indices, uint8_t index_type) {
    (void)first_index;
    (void)num_indices;
    (void)index_type;
}

void nt_gfx_backend_draw_instanced(uint32_t first_vertex, uint32_t num_vertices, uint32_t instance_count) {
    (void)first_vertex;
    (void)num_vertices;
    (void)instance_count;
}

void nt_gfx_backend_draw_indexed_instanced(uint32_t first_index, uint32_t num_indices, uint32_t instance_count, uint8_t index_type) {
    (void)first_index;
    (void)num_indices;
    (void)instance_count;
    (void)index_type;
}

bool nt_gfx_backend_recreate_all_resources(void) {
    /* The front-end drops every stage and program handle on loss without a
     * destroy call, so the tables are released here, as in the GL backend. */
    if (s_fake_program_table) {
        memset(s_fake_program_table, 0, ((size_t)s_fake_max_programs + 1U) * sizeof(nt_gfx_fake_program_t));
    }
    s_fake_backend_restore_count++;
    if (s_fake_fail_next_backend_restore) {
        s_fake_fail_next_backend_restore = false;
        s_fake_backend_missing = true;
        return false;
    }
    s_fake_backend_missing = false;
    return true;
}

nt_gfx_gpu_caps_t nt_gfx_gl_ctx_detect_gpu_caps(void) {
    s_fake_gpu_caps_probe_count++;
    return (nt_gfx_gpu_caps_t){.max_texture_size = 4096, .has_float_render_target = true};
}
