#include "postfx/nt_postfx_blur.h"

#include "core/nt_assert.h"
#include "log/nt_log.h"

#include <math.h>
#include <string.h>

typedef struct {
    float position[2];
    float uv[2];
} nt_postfx_blur_vertex_t;

_Static_assert(sizeof(nt_postfx_blur_vertex_t) == 16, "blur vertex size");

static const char *s_blur_vs_src = "precision mediump float;\n"
                                   "layout(location = 0) in vec2 a_position;\n"
                                   "layout(location = 3) in vec2 a_uv;\n"
                                   "out vec2 v_uv;\n"
                                   "void main() {\n"
                                   "    v_uv = a_uv;\n"
                                   "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
                                   "}\n";

static const char *s_blur_fs_src = "precision mediump float;\n"
                                   "uniform sampler2D u_source;\n"
                                   "uniform vec4 u_direction;\n"
                                   "uniform int u_radius;\n"
                                   "uniform vec4 u_kernel0;\n"
                                   "uniform vec4 u_kernel1;\n"
                                   "uniform vec4 u_kernel2;\n"
                                   "uniform vec4 u_kernel3;\n"
                                   "uniform vec4 u_kernel4;\n"
                                   "in vec2 v_uv;\n"
                                   "out vec4 frag_color;\n"
                                   "float kernel_at(int i) {\n"
                                   "    if (i < 4) { return u_kernel0[i]; }\n"
                                   "    if (i < 8) { return u_kernel1[i - 4]; }\n"
                                   "    if (i < 12) { return u_kernel2[i - 8]; }\n"
                                   "    if (i < 16) { return u_kernel3[i - 12]; }\n"
                                   "    return u_kernel4[i - 16];\n"
                                   "}\n"
                                   "void main() {\n"
                                   "    vec2 texel = vec2(1.0) / vec2(textureSize(u_source, 0));\n"
                                   "    vec2 step_uv = u_direction.xy * texel;\n"
                                   "    vec4 acc = texture(u_source, v_uv) * kernel_at(0);\n"
                                   "    for (int i = 1; i <= 16; i++) {\n"
                                   "        if (i > u_radius) { break; }\n"
                                   "        float w = kernel_at(i);\n"
                                   "        vec2 d = step_uv * float(i);\n"
                                   "        acc += texture(u_source, v_uv - d) * w;\n"
                                   "        acc += texture(u_source, v_uv + d) * w;\n"
                                   "    }\n"
                                   "    frag_color = acc;\n"
                                   "}\n";

static struct {
    nt_shader_t vs;
    nt_shader_t fs;
    nt_pipeline_t pipeline;
    nt_buffer_t triangle_vbo;
    bool initialized;
#ifdef NT_TEST_ACCESS
    uint32_t draw_count;
#endif
} s_blur;

static const char *const s_kernel_uniforms[5] = {
    "u_kernel0", "u_kernel1", "u_kernel2", "u_kernel3", "u_kernel4",
};

static uint32_t radius_to_int(float radius) {
    if (!isfinite(radius) || radius <= 0.0F || radius > (float)NT_POSTFX_BLUR_MAX_RADIUS) {
        return 0;
    }
    uint32_t r = (uint32_t)ceilf(radius);
    return r <= NT_POSTFX_BLUR_MAX_RADIUS ? r : 0;
}

static float sigma_or_derived(float radius, float sigma) {
    if (!isfinite(sigma) || sigma < 0.0F) {
        return 0.0F;
    }
    if (sigma > 0.0F) {
        return sigma;
    }
    return radius / 3.0F;
}

static uint32_t build_kernel(float radius, float sigma, float out_weights[NT_POSTFX_BLUR_MAX_KERNEL]) {
    NT_ASSERT(out_weights != NULL);
    if (out_weights == NULL) {
        return 0;
    }
    uint32_t r = radius_to_int(radius);
    float s = sigma_or_derived(radius, sigma);
    if (r == 0 || !isfinite(s) || s <= 0.0F) {
        return 0;
    }

    uint32_t count = (r * 2U) + 1U;
    float denom = 2.0F * s * s;
    float sum = 0.0F;
    for (uint32_t i = 0; i < count; i++) {
        int32_t offset = (int32_t)i - (int32_t)r;
        float x = (float)offset;
        float w = expf(-(x * x) / denom);
        if (!isfinite(w)) {
            return 0;
        }
        out_weights[i] = w;
        sum += w;
    }
    if (!isfinite(sum) || sum <= 0.0F) {
        return 0;
    }
    float inv_sum = 1.0F / sum;
    for (uint32_t i = 0; i < count; i++) {
        out_weights[i] *= inv_sum;
    }
    return count;
}

static void pack_kernel_pairs(const float weights[NT_POSTFX_BLUR_MAX_KERNEL], uint32_t radius, float packed[20]) {
    memset(packed, 0, sizeof(float) * 20U);
    for (uint32_t i = 0; i <= radius; i++) {
        packed[i] = weights[radius + i];
    }
}

static void destroy_gpu_resources(void) {
    if (s_blur.triangle_vbo.id != 0) {
        nt_gfx_destroy_buffer(s_blur.triangle_vbo);
    }
    if (s_blur.pipeline.id != 0) {
        nt_gfx_destroy_pipeline(s_blur.pipeline);
    }
    if (s_blur.fs.id != 0) {
        nt_gfx_destroy_shader(s_blur.fs);
    }
    if (s_blur.vs.id != 0) {
        nt_gfx_destroy_shader(s_blur.vs);
    }
    s_blur.triangle_vbo = (nt_buffer_t){0};
    s_blur.pipeline = (nt_pipeline_t){0};
    s_blur.fs = (nt_shader_t){0};
    s_blur.vs = (nt_shader_t){0};
}

static bool make_gpu_resources(void) {
    static const nt_postfx_blur_vertex_t verts[3] = {
        {{-1.0F, -1.0F}, {0.0F, 0.0F}},
        {{3.0F, -1.0F}, {2.0F, 0.0F}},
        {{-1.0F, 3.0F}, {0.0F, 2.0F}},
    };
    s_blur.vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_blur_vs_src, .label = "postfx_blur_vs"});
    s_blur.fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = s_blur_fs_src, .label = "postfx_blur_fs"});
    if (s_blur.vs.id == 0 || s_blur.fs.id == 0) {
        return false;
    }
    s_blur.pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .vertex_shader = s_blur.vs,
        .fragment_shader = s_blur.fs,
        .layout =
            {
                .stride = sizeof(nt_postfx_blur_vertex_t),
                .attr_count = 2,
                .attrs =
                    {
                        {.location = NT_ATTR_POSITION, .format = NT_FORMAT_FLOAT2, .offset = 0},
                        {.location = NT_ATTR_TEXCOORD0, .format = NT_FORMAT_FLOAT2, .offset = 8},
                    },
            },
        .depth_test = false,
        .depth_write = false,
        .depth_func = NT_DEPTH_ALWAYS,
        .cull_mode = 0,
        .blend = false,
        .label = "postfx_blur_pipeline",
    });
    s_blur.triangle_vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_VERTEX,
        .usage = NT_USAGE_IMMUTABLE,
        .data = verts,
        .size = sizeof(verts),
        .label = "postfx_blur_triangle",
    });
    return s_blur.pipeline.id != 0 && s_blur.triangle_vbo.id != 0;
}

nt_result_t nt_postfx_blur_init(void) {
    NT_ASSERT(!s_blur.initialized && "nt_postfx_blur_init: already initialized");
    if (s_blur.initialized) {
        return NT_ERR_INIT_FAILED;
    }
    memset(&s_blur, 0, sizeof(s_blur));
    if (!make_gpu_resources()) {
        NT_LOG_ERROR("postfx_blur init failed");
        destroy_gpu_resources();
        return NT_ERR_INIT_FAILED;
    }
    s_blur.initialized = true;
    return NT_OK;
}

void nt_postfx_blur_shutdown(void) {
    destroy_gpu_resources();
    memset(&s_blur, 0, sizeof(s_blur));
}

nt_result_t nt_postfx_blur_restore_gpu(void) {
    NT_ASSERT(s_blur.initialized && "nt_postfx_blur_restore_gpu: module is not initialized");
    if (!s_blur.initialized) {
        return NT_ERR_INIT_FAILED;
    }
    destroy_gpu_resources();
    if (!make_gpu_resources()) {
        NT_LOG_ERROR("postfx_blur restore failed");
        destroy_gpu_resources();
        s_blur.initialized = false;
        return NT_ERR_INIT_FAILED;
    }
    return NT_OK;
}

typedef struct {
    nt_texture_t temp_color;
    nt_texture_t temp_depth;
    nt_texture_t dest_color;
} blur_pass_targets_t;

static bool validate_module_and_pass(const nt_postfx_blur_pass_t *pass) {
    NT_ASSERT(s_blur.initialized && "nt_postfx_blur_gaussian: module is not initialized");
    NT_ASSERT(pass != NULL && "nt_postfx_blur_gaussian: NULL pass");
    return s_blur.initialized && pass != NULL;
}

static bool validate_targets_ready(const nt_postfx_blur_pass_t *pass) {
    bool temp_ready = nt_gfx_render_target_ready(pass->temp);
    bool dest_ready = nt_gfx_render_target_ready(pass->dest);
    NT_ASSERT(temp_ready && "nt_postfx_blur_gaussian: temp target is not ready");
    NT_ASSERT(dest_ready && "nt_postfx_blur_gaussian: dest target is not ready");
    return temp_ready && dest_ready;
}

static bool resolve_pass_targets(const nt_postfx_blur_pass_t *pass, blur_pass_targets_t *targets) {
    targets->temp_color = nt_gfx_render_target_color(pass->temp);
    targets->temp_depth = nt_gfx_render_target_depth(pass->temp);
    targets->dest_color = nt_gfx_render_target_color(pass->dest);
    bool source_ready = nt_gfx_texture_ready(pass->source);
    bool colors_valid = targets->temp_color.id != 0 && targets->dest_color.id != 0;
    NT_ASSERT(source_ready && "nt_postfx_blur_gaussian: source texture is not ready");
    NT_ASSERT(colors_valid && "nt_postfx_blur_gaussian: target color attachment is invalid");
    return source_ready && colors_valid;
}

static bool validate_target_sizes(const nt_postfx_blur_pass_t *pass, const blur_pass_targets_t *targets) {
    uint16_t source_width = 0;
    uint16_t source_height = 0;
    uint16_t temp_width = 0;
    uint16_t temp_height = 0;
    uint16_t dest_width = 0;
    uint16_t dest_height = 0;
    bool source_size_valid = nt_gfx_texture_size(pass->source, &source_width, &source_height);
    bool temp_size_valid = nt_gfx_texture_size(targets->temp_color, &temp_width, &temp_height);
    bool dest_size_valid = nt_gfx_texture_size(targets->dest_color, &dest_width, &dest_height);
    bool sizes_valid = source_size_valid && temp_size_valid && dest_size_valid;
    NT_ASSERT(sizes_valid && "nt_postfx_blur_gaussian: texture dimensions unavailable");
    if (!sizes_valid) {
        return false;
    }
    bool sizes_match = source_width == temp_width && source_width == dest_width && source_height == temp_height && source_height == dest_height;
    NT_ASSERT(sizes_match && "nt_postfx_blur_gaussian: source, temp, and dest dimensions must match");
    if (!sizes_match) {
        return false;
    }
    return true;
}

static bool validate_no_aliasing(const nt_postfx_blur_pass_t *pass, const blur_pass_targets_t *targets) {
    bool source_aliases_temp = pass->source.id == targets->temp_color.id || (targets->temp_depth.id != 0 && pass->source.id == targets->temp_depth.id);
    bool targets_alias = pass->temp.id == pass->dest.id;
    NT_ASSERT(!source_aliases_temp && "nt_postfx_blur_gaussian: source aliases temp target");
    NT_ASSERT(!targets_alias && "nt_postfx_blur_gaussian: temp and dest targets alias");
    return !source_aliases_temp && !targets_alias;
}

static void validate_kernel_parameters(const nt_postfx_blur_pass_t *pass) {
    bool radius_valid = isfinite(pass->radius) && pass->radius > 0.0F && pass->radius <= (float)NT_POSTFX_BLUR_MAX_RADIUS;
    bool sigma_valid = isfinite(pass->sigma) && pass->sigma >= 0.0F;
    NT_ASSERT(radius_valid && "nt_postfx_blur_gaussian: invalid radius");
    NT_ASSERT(sigma_valid && "nt_postfx_blur_gaussian: invalid sigma");
}

static bool build_validated_kernel(const nt_postfx_blur_pass_t *pass, uint32_t *out_radius, float out_weights[NT_POSTFX_BLUR_MAX_KERNEL]) {
    uint32_t count = build_kernel(pass->radius, pass->sigma, out_weights);
    NT_ASSERT(count != 0 && "nt_postfx_blur_gaussian: invalid kernel");
    if (count == 0) {
        return false;
    }
    uint32_t radius = (count - 1U) / 2U;
    NT_ASSERT(radius <= NT_POSTFX_BLUR_MAX_RADIUS);
    *out_radius = radius;
    return true;
}

static bool validate_pass(const nt_postfx_blur_pass_t *pass, uint32_t *out_radius, float out_weights[NT_POSTFX_BLUR_MAX_KERNEL]) {
    if (!validate_module_and_pass(pass)) {
        return false;
    }
    if (!validate_targets_ready(pass)) {
        return false;
    }
    blur_pass_targets_t targets;
    if (!resolve_pass_targets(pass, &targets)) {
        return false;
    }
    if (!validate_target_sizes(pass, &targets)) {
        return false;
    }
    if (!validate_no_aliasing(pass, &targets)) {
        return false;
    }
    validate_kernel_parameters(pass);
    return build_validated_kernel(pass, out_radius, out_weights);
}

static void upload_kernel(uint32_t radius, const float packed[20]) {
    nt_gfx_set_uniform_int("u_radius", (int)radius);
    for (uint32_t i = 0; i < 5; i++) {
        nt_gfx_set_uniform_vec4(s_kernel_uniforms[i], &packed[(size_t)i * 4U]);
    }
}

static void draw_blur_pass(nt_texture_t source, nt_render_target_t target, const float direction[4], uint32_t radius, const float packed[20]) {
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target, .clear_color = {0.0F, 0.0F, 0.0F, 0.0F}, .clear_depth = 1.0F});
    nt_gfx_bind_pipeline(s_blur.pipeline);
    nt_gfx_bind_vertex_buffer(s_blur.triangle_vbo);
    nt_gfx_bind_texture(source, 0);
    nt_gfx_set_uniform_int("u_source", 0);
    nt_gfx_set_uniform_vec4("u_direction", direction);
    upload_kernel(radius, packed);
    nt_gfx_draw(0, 3);
    nt_gfx_end_pass();
#ifdef NT_TEST_ACCESS
    s_blur.draw_count++;
#endif
}

void nt_postfx_blur_gaussian(const nt_postfx_blur_pass_t *pass) {
    float weights[NT_POSTFX_BLUR_MAX_KERNEL];
    uint32_t radius = 0;
    if (!validate_pass(pass, &radius, weights)) {
        return;
    }
    float packed[20];
    pack_kernel_pairs(weights, radius, packed);

    static const float horizontal[4] = {1.0F, 0.0F, 0.0F, 0.0F};
    static const float vertical[4] = {0.0F, 1.0F, 0.0F, 0.0F};
    draw_blur_pass(pass->source, pass->temp, horizontal, radius, packed);
    draw_blur_pass(nt_gfx_render_target_color(pass->temp), pass->dest, vertical, radius, packed);
}

#ifdef NT_TEST_ACCESS
uint32_t nt_postfx_blur_test_build_kernel(float radius, float sigma, float out_weights[NT_POSTFX_BLUR_MAX_KERNEL]) { return build_kernel(radius, sigma, out_weights); }

uint32_t nt_postfx_blur_test_draw_count(void) { return s_blur.draw_count; }

void nt_postfx_blur_test_reset_counters(void) { s_blur.draw_count = 0; }
#endif
