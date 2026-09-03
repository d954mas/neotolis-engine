#include "core/nt_assert.h"
#include "graphics/nt_gfx_internal.h"

/* No-op backend for headless builds and testing.
   Create functions return 1 (nonzero) so make_shader/program/pipeline/buffer succeed. */

#ifdef NT_TEST_ACCESS
#define NT_GFX_STUB_MAX_SLOTS 16
#define NT_GFX_STUB_HISTORY_CAPACITY 16
static uint32_t s_stub_last_sampler[NT_GFX_STUB_MAX_SLOTS];
static uint32_t s_stub_bind_sampler_count;
static uint32_t s_stub_set_scissor_enabled_count;
static uint32_t s_stub_last_pass_target;
static uint32_t s_stub_pass_targets[NT_GFX_STUB_HISTORY_CAPACITY];
static uint32_t s_stub_pass_target_count;
static uint32_t s_stub_bound_textures[NT_GFX_STUB_HISTORY_CAPACITY];
static uint32_t s_stub_bound_texture_count;
static uint32_t s_stub_render_target_create_count;
static uint32_t s_stub_render_target_resize_count;
static uint32_t s_stub_render_target_destroy_count;
static uint32_t s_stub_texture_create_count;
static uint32_t s_stub_program_create_count;
static uint32_t s_stub_pipeline_create_count;
#define NT_GFX_STUB_UNIFORM_NAMES 16
static uint32_t s_stub_uniform_int_hashes[NT_GFX_STUB_UNIFORM_NAMES];
static int s_stub_uniform_int_values[NT_GFX_STUB_UNIFORM_NAMES];
static uint32_t s_stub_uniform_int_count;
static uint32_t s_stub_uniform_vec4_hashes[NT_GFX_STUB_UNIFORM_NAMES];
static float s_stub_uniform_vec4_values[NT_GFX_STUB_UNIFORM_NAMES][4];
static uint32_t s_stub_uniform_vec4_count;
static uint32_t s_stub_bind_pipeline_count;
static uint32_t s_stub_update_texture_count;
static uint32_t s_stub_update_buffer_count;
static uint32_t s_stub_backend_restore_count;
static uint32_t s_stub_gpu_caps_probe_count;
static uint16_t s_stub_last_render_target_width;
static uint16_t s_stub_last_render_target_height;
static nt_render_target_depth_t s_stub_last_render_target_depth;
static nt_texture_desc_t s_stub_last_texture_desc;
static uint32_t s_stub_last_depth_texture_backend;
static uint32_t s_stub_next_texture_backend;
static bool s_stub_context_lost;
static bool s_stub_backend_missing;
static uint8_t s_stub_fail_texture_creates;
static uint8_t s_stub_fail_buffer_creates;
static bool s_stub_fail_next_program_create;
static bool s_stub_lose_context_on_program_create;
static bool s_stub_fail_next_pipeline_create;
static bool s_stub_fail_next_backend_restore;
static bool s_stub_fail_next_render_target_create;
static bool s_stub_fail_next_render_target_resize;
static uint32_t s_stub_last_update_buffer_offset;
static uint32_t s_stub_last_instance_offset;
static uint32_t s_stub_last_instance_vertex_input; /* recorder: last VI handle bind_instance_buffer named */
static nt_blend_state_t s_stub_last_pipeline_blend;
static uint32_t s_stub_vertex_input_create_count;
static uint32_t s_stub_bind_vertex_input_count;
static uint32_t s_stub_last_bound_vertex_input; /* recorder: last handle bind_vertex_input received */
static uint32_t s_stub_last_uniform_program;    /* recorder: last program a uniform write named */
static bool s_stub_fail_next_vertex_input_create;

uint32_t nt_gfx_stub_test_last_sampler(uint32_t slot) {
    if (slot >= NT_GFX_STUB_MAX_SLOTS) {
        return 0;
    }
    return s_stub_last_sampler[slot];
}

uint32_t nt_gfx_stub_test_bind_sampler_count(void) { return s_stub_bind_sampler_count; }
uint32_t nt_gfx_stub_test_set_scissor_enabled_count(void) { return s_stub_set_scissor_enabled_count; }
uint32_t nt_gfx_stub_test_last_pass_target(void) { return s_stub_last_pass_target; }
uint32_t nt_gfx_stub_test_pass_target_count(void) { return s_stub_pass_target_count; }
uint32_t nt_gfx_stub_test_pass_target_at(uint32_t index) { return index < s_stub_pass_target_count ? s_stub_pass_targets[index] : 0; }
uint32_t nt_gfx_stub_test_bound_texture_count(void) { return s_stub_bound_texture_count; }
uint32_t nt_gfx_stub_test_bound_texture_at(uint32_t index) { return index < s_stub_bound_texture_count ? s_stub_bound_textures[index] : 0; }
uint32_t nt_gfx_stub_test_render_target_create_count(void) { return s_stub_render_target_create_count; }
uint32_t nt_gfx_stub_test_render_target_resize_count(void) { return s_stub_render_target_resize_count; }
uint32_t nt_gfx_stub_test_render_target_destroy_count(void) { return s_stub_render_target_destroy_count; }
uint32_t nt_gfx_stub_test_texture_create_count(void) { return s_stub_texture_create_count; }
uint32_t nt_gfx_stub_test_program_create_count(void) { return s_stub_program_create_count; }
uint32_t nt_gfx_stub_test_pipeline_create_count(void) { return s_stub_pipeline_create_count; }
uint32_t nt_gfx_stub_test_bind_pipeline_count(void) { return s_stub_bind_pipeline_count; }
uint32_t nt_gfx_stub_test_uniform_int_count(void) { return s_stub_uniform_int_count; }
uint32_t nt_gfx_stub_test_uniform_int_hash_at(uint32_t index) { return index < s_stub_uniform_int_count && index < NT_GFX_STUB_UNIFORM_NAMES ? s_stub_uniform_int_hashes[index] : 0; }
int nt_gfx_stub_test_uniform_int_value_at(uint32_t index) { return index < s_stub_uniform_int_count && index < NT_GFX_STUB_UNIFORM_NAMES ? s_stub_uniform_int_values[index] : -1; }
uint32_t nt_gfx_stub_test_uniform_vec4_count(void) { return s_stub_uniform_vec4_count; }
uint32_t nt_gfx_stub_test_uniform_vec4_hash_at(uint32_t index) { return index < s_stub_uniform_vec4_count && index < NT_GFX_STUB_UNIFORM_NAMES ? s_stub_uniform_vec4_hashes[index] : 0; }
void nt_gfx_stub_test_uniform_vec4_value_at(uint32_t index, float out[4]) {
    const bool valid = index < s_stub_uniform_vec4_count && index < NT_GFX_STUB_UNIFORM_NAMES;
    for (uint32_t i = 0; i < 4; i++) {
        out[i] = valid ? s_stub_uniform_vec4_values[index][i] : 0.0F;
    }
}
uint32_t nt_gfx_stub_test_update_texture_count(void) { return s_stub_update_texture_count; }
uint32_t nt_gfx_stub_test_update_buffer_count(void) { return s_stub_update_buffer_count; }
uint32_t nt_gfx_stub_test_backend_restore_count(void) { return s_stub_backend_restore_count; }
uint32_t nt_gfx_stub_test_gpu_caps_probe_count(void) { return s_stub_gpu_caps_probe_count; }
uint16_t nt_gfx_stub_test_last_render_target_width(void) { return s_stub_last_render_target_width; }
uint16_t nt_gfx_stub_test_last_render_target_height(void) { return s_stub_last_render_target_height; }
nt_render_target_depth_t nt_gfx_stub_test_last_render_target_depth(void) { return s_stub_last_render_target_depth; }
nt_texture_desc_t nt_gfx_stub_test_last_texture_desc(void) { return s_stub_last_texture_desc; }
uint32_t nt_gfx_stub_test_last_depth_texture_backend(void) { return s_stub_last_depth_texture_backend; }
void nt_gfx_stub_test_fail_next_render_target_create(void) { s_stub_fail_next_render_target_create = true; }
void nt_gfx_stub_test_fail_next_render_target_resize(void) { s_stub_fail_next_render_target_resize = true; }
void nt_gfx_stub_test_fail_texture_creates(uint8_t mask) {
    NT_ASSERT(mask <= 3);
    s_stub_fail_texture_creates = mask;
}
void nt_gfx_stub_test_fail_buffer_creates(uint8_t mask) {
    NT_ASSERT(mask <= 3);
    s_stub_fail_buffer_creates = mask;
}
void nt_gfx_stub_test_fail_next_program_create(void) { s_stub_fail_next_program_create = true; }
void nt_gfx_stub_test_lose_context_on_program_create(void) { s_stub_lose_context_on_program_create = true; }
void nt_gfx_stub_test_fail_next_pipeline_create(void) { s_stub_fail_next_pipeline_create = true; }
void nt_gfx_stub_test_fail_next_backend_restore(void) { s_stub_fail_next_backend_restore = true; }
void nt_gfx_stub_test_set_context_lost(bool lost) { s_stub_context_lost = lost; }
uint32_t nt_gfx_stub_test_last_update_buffer_offset(void) { return s_stub_last_update_buffer_offset; }
uint32_t nt_gfx_stub_test_last_instance_offset(void) { return s_stub_last_instance_offset; }
uint32_t nt_gfx_stub_test_last_instance_vertex_input(void) { return s_stub_last_instance_vertex_input; }
nt_blend_state_t nt_gfx_stub_test_last_pipeline_blend(void) { return s_stub_last_pipeline_blend; }
uint32_t nt_gfx_stub_test_vertex_input_create_count(void) { return s_stub_vertex_input_create_count; }
uint32_t nt_gfx_stub_test_bind_vertex_input_count(void) { return s_stub_bind_vertex_input_count; }
uint32_t nt_gfx_stub_test_last_bound_vertex_input(void) { return s_stub_last_bound_vertex_input; }
uint32_t nt_gfx_stub_test_last_uniform_program(void) { return s_stub_last_uniform_program; }
void nt_gfx_stub_test_fail_next_vertex_input_create(void) { s_stub_fail_next_vertex_input_create = true; }

void nt_gfx_stub_test_reset(void) {
    for (uint32_t i = 0; i < NT_GFX_STUB_MAX_SLOTS; i++) {
        s_stub_last_sampler[i] = 0;
    }
    s_stub_bind_sampler_count = 0;
    s_stub_set_scissor_enabled_count = 0;
    s_stub_last_pass_target = 0;
    s_stub_pass_target_count = 0;
    s_stub_bound_texture_count = 0;
    s_stub_render_target_create_count = 0;
    s_stub_render_target_resize_count = 0;
    s_stub_render_target_destroy_count = 0;
    s_stub_texture_create_count = 0;
    s_stub_program_create_count = 0;
    s_stub_pipeline_create_count = 0;
    s_stub_uniform_int_count = 0;
    s_stub_uniform_vec4_count = 0;
    s_stub_bind_pipeline_count = 0;
    s_stub_update_texture_count = 0;
    s_stub_update_buffer_count = 0;
    s_stub_backend_restore_count = 0;
    s_stub_gpu_caps_probe_count = 0;
    s_stub_last_render_target_width = 0;
    s_stub_last_render_target_height = 0;
    s_stub_last_render_target_depth = NT_RT_DEPTH_NONE;
    s_stub_last_texture_desc = (nt_texture_desc_t){0};
    s_stub_last_depth_texture_backend = 0;
    s_stub_next_texture_backend = 0;
    s_stub_last_update_buffer_offset = 0;
    s_stub_last_instance_offset = 0;
    s_stub_last_instance_vertex_input = 0;
    s_stub_last_pipeline_blend = (nt_blend_state_t){0};
    s_stub_vertex_input_create_count = 0;
    s_stub_bind_vertex_input_count = 0;
    s_stub_last_bound_vertex_input = 0;
    s_stub_last_uniform_program = 0;
    s_stub_fail_next_vertex_input_create = false;
    s_stub_context_lost = false;
    s_stub_backend_missing = false;
    s_stub_fail_texture_creates = 0;
    s_stub_fail_buffer_creates = 0;
    s_stub_fail_next_program_create = false;
    s_stub_lose_context_on_program_create = false;
    s_stub_fail_next_pipeline_create = false;
    s_stub_fail_next_backend_restore = false;
    s_stub_fail_next_render_target_create = false;
    s_stub_fail_next_render_target_resize = false;
}
#endif

bool nt_gfx_backend_init(const nt_gfx_desc_t *desc) {
    (void)desc;
    return true;
}

void nt_gfx_backend_shutdown(void) {}

bool nt_gfx_backend_is_context_lost(void) {
#ifdef NT_TEST_ACCESS
    return s_stub_context_lost || s_stub_backend_missing;
#else
    return false;
#endif
}

void nt_gfx_backend_begin_frame(void) {}

void nt_gfx_backend_end_frame(void) {}

void nt_gfx_backend_begin_pass(const nt_pass_desc_t *desc, uint32_t render_target_backend) {
    (void)desc;
#ifdef NT_TEST_ACCESS
    s_stub_last_pass_target = render_target_backend;
    if (s_stub_pass_target_count < NT_GFX_STUB_HISTORY_CAPACITY) {
        s_stub_pass_targets[s_stub_pass_target_count++] = render_target_backend;
    }
#else
    (void)render_target_backend;
#endif
}

void nt_gfx_backend_end_pass(void) {}

/* Scissor and viewport stub no-ops. State is cached in shared nt_gfx.c
 * so NT_TEST_ACCESS probes can read it back without GL. */
void nt_gfx_backend_set_scissor(int x, int y, int w, int h) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

void nt_gfx_backend_set_scissor_enabled(bool enabled) {
    (void)enabled;
#ifdef NT_TEST_ACCESS
    s_stub_set_scissor_enabled_count++;
#endif
}

void nt_gfx_backend_set_viewport(int x, int y, int w, int h) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

/* Deterministic synthetic readback (no GL). Encodes the GL row index (r) and
 * column index (g) per pixel so the shared-layer Y-flip is observable in CTest:
 * after the flip, out row 0 must carry GL row (h-1). Bottom-left order, matching
 * the real glReadPixels backend. b = 0x40 marker, a = 0xFF (opaque). */
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
    (void)vs_backend;
    (void)fs_backend;
#ifdef NT_TEST_ACCESS
    s_stub_program_create_count++;
    if (s_stub_lose_context_on_program_create) {
        s_stub_lose_context_on_program_create = false;
        s_stub_context_lost = true;
        return 0;
    }
    if (s_stub_fail_next_program_create) {
        s_stub_fail_next_program_create = false;
        return 0;
    }
#endif
    return 1;
}

void nt_gfx_backend_destroy_program(uint32_t backend_handle) { (void)backend_handle; }

uint32_t nt_gfx_backend_create_pipeline(const nt_pipeline_desc_t *desc, uint32_t program_backend, uint32_t slot) {
#ifdef NT_TEST_ACCESS
    if (s_stub_fail_next_pipeline_create) {
        s_stub_fail_next_pipeline_create = false;
        return 0;
    }
    s_stub_pipeline_create_count++;
    s_stub_last_pipeline_blend = desc->blend;
#else
    (void)desc;
#endif
    (void)program_backend;
    return slot;
}

void nt_gfx_backend_destroy_pipeline(uint32_t backend_handle) { (void)backend_handle; }

uint32_t nt_gfx_backend_create_vertex_input(const nt_vertex_input_desc_t *desc, uint32_t vbo_backend, uint32_t ibo_backend, uint32_t slot) {
    (void)desc;
    (void)vbo_backend;
    (void)ibo_backend;
#ifdef NT_TEST_ACCESS
    s_stub_vertex_input_create_count++;
    if (s_stub_fail_next_vertex_input_create) {
        s_stub_fail_next_vertex_input_create = false;
        return 0;
    }
#endif
    return slot;
}

void nt_gfx_backend_destroy_vertex_input(uint32_t backend_handle) { (void)backend_handle; }

void nt_gfx_backend_bind_vertex_input(uint32_t backend_handle) {
    NT_ASSERT(backend_handle != 0 && "bind_vertex_input: requires a live handle");
#ifdef NT_TEST_ACCESS
    s_stub_bind_vertex_input_count++;
    s_stub_last_bound_vertex_input = backend_handle;
#else
    (void)backend_handle;
#endif
}

uint32_t nt_gfx_backend_create_buffer(const nt_buffer_desc_t *desc) {
    (void)desc;
#ifdef NT_TEST_ACCESS
    bool fail = (s_stub_fail_buffer_creates & 1U) != 0;
    s_stub_fail_buffer_creates >>= 1U;
    if (fail) {
        return 0;
    }
#endif
    return 1;
}

void nt_gfx_backend_destroy_buffer(uint32_t backend_handle) { (void)backend_handle; }

uint32_t nt_gfx_backend_create_texture(const nt_texture_desc_t *desc) {
#ifdef NT_TEST_ACCESS
    s_stub_last_texture_desc = *desc;
    s_stub_texture_create_count++;
    /* One bit per create, consumed in order: mask 1 fails the first, 2 the
     * second, 3 both. */
    bool fail = (s_stub_fail_texture_creates & 1U) != 0;
    s_stub_fail_texture_creates >>= 1U;
    if (fail) {
        return 0;
    }
    return ++s_stub_next_texture_backend;
#else
    (void)desc;
    return 1;
#endif
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
#ifdef NT_TEST_ACCESS
    return ++s_stub_next_texture_backend;
#else
    return 1;
#endif
}

void nt_gfx_backend_destroy_texture(uint32_t backend_handle) { (void)backend_handle; }

uint32_t nt_gfx_backend_create_render_target(const nt_render_target_desc_t *desc, uint32_t color_backend, uint32_t depth_texture_backend) {
    NT_ASSERT(desc != NULL);
    NT_ASSERT(color_backend != 0);
    if (desc == NULL || color_backend == 0) {
        return 0;
    }
    (void)color_backend;
    (void)depth_texture_backend;
#ifdef NT_TEST_ACCESS
    s_stub_render_target_create_count++;
    s_stub_last_render_target_depth = desc ? desc->depth_storage : NT_RT_DEPTH_NONE;
    s_stub_last_render_target_width = desc ? desc->width : 0;
    s_stub_last_render_target_height = desc ? desc->height : 0;
    s_stub_last_depth_texture_backend = depth_texture_backend;
    if (s_stub_fail_next_render_target_create) {
        s_stub_fail_next_render_target_create = false;
        return 0;
    }
    return s_stub_render_target_create_count;
#else
    (void)desc;
    return 1;
#endif
}

bool nt_gfx_backend_resize_render_target(uint32_t backend_handle, const nt_render_target_desc_t *desc, uint32_t color_backend, uint32_t depth_texture_backend) {
    (void)color_backend;
    NT_ASSERT(desc != NULL);
    if (backend_handle == 0 || desc == NULL) {
        return false;
    }
#ifdef NT_TEST_ACCESS
    s_stub_render_target_resize_count++;
    s_stub_last_render_target_depth = desc ? desc->depth_storage : NT_RT_DEPTH_NONE;
    s_stub_last_render_target_width = desc ? desc->width : 0;
    s_stub_last_render_target_height = desc ? desc->height : 0;
    s_stub_last_depth_texture_backend = depth_texture_backend;
    if (s_stub_fail_next_render_target_resize) {
        s_stub_fail_next_render_target_resize = false;
        return false;
    }
    return true;
#else
    (void)depth_texture_backend;
    return true;
#endif
}

void nt_gfx_backend_destroy_render_target(uint32_t backend_handle) {
    (void)backend_handle;
#ifdef NT_TEST_ACCESS
    s_stub_render_target_destroy_count++;
#endif
}

void nt_gfx_backend_bind_texture(uint32_t backend_handle, uint32_t slot) {
    NT_ASSERT(backend_handle != 0 && "bind_texture: requires a live handle");
    (void)slot;
#ifdef NT_TEST_ACCESS
    if (s_stub_bound_texture_count < NT_GFX_STUB_HISTORY_CAPACITY) {
        s_stub_bound_textures[s_stub_bound_texture_count++] = backend_handle;
    }
#else
    (void)backend_handle;
#endif
}

void nt_gfx_backend_update_buffer(uint32_t backend_handle, uint32_t offset, const void *data, uint32_t size) {
    (void)backend_handle;
    (void)data;
    (void)size;
#ifdef NT_TEST_ACCESS
    s_stub_last_update_buffer_offset = offset;
    s_stub_update_buffer_count++;
#else
    (void)offset;
#endif
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
    static uint32_t s_counter;
    return ++s_counter; /* unique id so tests can differentiate samplers */
}

void nt_gfx_backend_destroy_sampler(uint32_t backend_handle) { (void)backend_handle; }

void nt_gfx_backend_bind_sampler(uint32_t backend_handle, uint32_t slot) {
#ifdef NT_TEST_ACCESS
    if (slot < NT_GFX_STUB_MAX_SLOTS) {
        s_stub_last_sampler[slot] = backend_handle;
    }
    s_stub_bind_sampler_count++;
#else
    (void)backend_handle;
    (void)slot;
#endif
}

void nt_gfx_backend_update_texture(uint32_t backend_handle, uint16_t x, uint16_t y, uint16_t w, uint16_t h, nt_texture_format_t format, const void *data) {
#ifdef NT_TEST_ACCESS
    s_stub_update_texture_count++;
#endif
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
#ifdef NT_TEST_ACCESS
    s_stub_bind_pipeline_count++;
#endif
    (void)backend_handle;
}

void nt_gfx_backend_bind_instance_buffer(uint32_t vertex_input_backend, uint32_t buffer_backend, uint32_t byte_offset) {
    NT_ASSERT(vertex_input_backend != 0 && buffer_backend != 0 && "bind_instance_buffer: requires live handles");
    (void)buffer_backend;
#ifdef NT_TEST_ACCESS
    s_stub_last_instance_offset = byte_offset;
    s_stub_last_instance_vertex_input = vertex_input_backend;
#else
    (void)byte_offset;
    (void)vertex_input_backend;
#endif
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
#ifdef NT_TEST_ACCESS
    s_stub_last_uniform_program = program_backend;
#else
    (void)program_backend;
#endif
    (void)name_hash;
    (void)matrix;
}

void nt_gfx_backend_set_uniform_vec4(uint32_t program_backend, uint32_t name_hash, const float *vec) {
#ifdef NT_TEST_ACCESS
    s_stub_last_uniform_program = program_backend;
    if (s_stub_uniform_vec4_count < NT_GFX_STUB_UNIFORM_NAMES) {
        s_stub_uniform_vec4_hashes[s_stub_uniform_vec4_count] = name_hash;
        for (uint32_t i = 0; i < 4; i++) {
            s_stub_uniform_vec4_values[s_stub_uniform_vec4_count][i] = vec[i];
        }
    }
    s_stub_uniform_vec4_count++;
#else
    (void)program_backend;
#endif
    (void)name_hash;
    (void)vec;
}

void nt_gfx_backend_set_uniform_float(uint32_t program_backend, uint32_t name_hash, float val) {
#ifdef NT_TEST_ACCESS
    s_stub_last_uniform_program = program_backend;
#else
    (void)program_backend;
#endif
    (void)name_hash;
    (void)val;
}

void nt_gfx_backend_set_uniform_int(uint32_t program_backend, uint32_t name_hash, int val) {
#ifdef NT_TEST_ACCESS
    s_stub_last_uniform_program = program_backend;
    if (s_stub_uniform_int_count < NT_GFX_STUB_UNIFORM_NAMES) {
        s_stub_uniform_int_hashes[s_stub_uniform_int_count] = name_hash;
        s_stub_uniform_int_values[s_stub_uniform_int_count] = val;
    }
    s_stub_uniform_int_count++;
#else
    (void)program_backend;
#endif
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
#ifdef NT_TEST_ACCESS
    s_stub_backend_restore_count++;
    if (s_stub_fail_next_backend_restore) {
        s_stub_fail_next_backend_restore = false;
        s_stub_backend_missing = true;
        return false;
    }
    s_stub_backend_missing = false;
#endif
    return true;
}

nt_gfx_gpu_caps_t nt_gfx_gl_ctx_detect_gpu_caps(void) {
#ifdef NT_TEST_ACCESS
    s_stub_gpu_caps_probe_count++;
#endif
    return (nt_gfx_gpu_caps_t){.max_texture_size = 4096, .has_float_render_target = true};
}
