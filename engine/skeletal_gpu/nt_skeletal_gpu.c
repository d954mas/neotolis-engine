#include "skeletal_gpu/nt_skeletal_gpu.h"

#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"

#define NT_SKELETAL_GPU_DEFAULT_WIDTH 2048U
#define NT_SKELETAL_GPU_TEXEL_FLOATS 4U

/* reserve hands out 3 texels per palette entry; the pose ABI must not pad the matrix. */
_Static_assert(sizeof(nt_skeletal_mat34_t) == (size_t)3 * NT_SKELETAL_GPU_TEXEL_FLOATS * sizeof(float), "one palette entry is three RGBA32F texels");

static struct {
    float *staging;     /* width * height texels, 4 floats each */
    uint64_t gfx_frame; /* gfx frame_sequence of the last begin_frame */
    nt_texture_t texture;
    uint16_t width;
    uint16_t height;
    uint16_t cursor_x;
    uint16_t cursor_y;
    bool initialized;
} s_skeletal_gpu;

// #region lifecycle
static void create_texture(void) {
    s_skeletal_gpu.texture = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = s_skeletal_gpu.width,
        .height = s_skeletal_gpu.height,
        .format = NT_TEXTURE_FORMAT_RGBA32F,
        .min_filter = NT_FILTER_NEAREST,
        .mag_filter = NT_FILTER_NEAREST,
        .wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .label = "skeletal_gpu_palettes",
    });
    NT_ASSERT((s_skeletal_gpu.texture.id != 0 || g_nt_gfx.context_lost) && "skeletal_gpu: palette texture creation failed");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_ASSERT expansion inflates the metric
void nt_skeletal_gpu_init(const nt_skeletal_gpu_desc_t *desc) {
    NT_ASSERT(!s_skeletal_gpu.initialized);
    NT_ASSERT(desc != NULL);
    NT_ASSERT(desc->height > 0);
    NT_ASSERT(g_nt_gfx.initialized && "nt_skeletal_gpu_init: nt_gfx_init must run first");

    memset(&s_skeletal_gpu, 0, sizeof(s_skeletal_gpu));
    uint32_t max_size = nt_gfx_gpu_caps()->max_texture_size;
    uint32_t width = desc->width;
    if (width == 0) {
        width = max_size < NT_SKELETAL_GPU_DEFAULT_WIDTH ? max_size : NT_SKELETAL_GPU_DEFAULT_WIDTH;
    }
    NT_ASSERT((g_nt_gfx.context_lost || (width <= max_size && desc->height <= max_size)) && "skeletal_gpu: palette texture exceeds max_texture_size");
    s_skeletal_gpu.width = (uint16_t)width;
    s_skeletal_gpu.height = desc->height;

    size_t texels = (size_t)s_skeletal_gpu.width * s_skeletal_gpu.height;
    s_skeletal_gpu.staging = (float *)calloc(texels, NT_SKELETAL_GPU_TEXEL_FLOATS * sizeof(float));
    NT_ASSERT(s_skeletal_gpu.staging != NULL && "skeletal_gpu: palette staging allocation failed");
    create_texture();
    s_skeletal_gpu.initialized = true;
}

void nt_skeletal_gpu_shutdown(void) {
    if (!s_skeletal_gpu.initialized) {
        return;
    }
    nt_gfx_destroy_texture(s_skeletal_gpu.texture);
    free(s_skeletal_gpu.staging);
    memset(&s_skeletal_gpu, 0, sizeof(s_skeletal_gpu));
}

void nt_skeletal_gpu_restore_gpu(void) {
    if (!s_skeletal_gpu.initialized) {
        return;
    }
    nt_gfx_destroy_texture(s_skeletal_gpu.texture);
    create_texture();
}
// #endregion

// #region frame
void nt_skeletal_gpu_begin_frame(void) {
    NT_ASSERT(s_skeletal_gpu.initialized);
    /* Repeated preparation can overwrite ranges already used by this frame's draws. */
    NT_ASSERT(s_skeletal_gpu.gfx_frame != g_nt_gfx.counters.frame_sequence && "skeletal_gpu: begin_frame twice in one gfx frame");
    s_skeletal_gpu.gfx_frame = g_nt_gfx.counters.frame_sequence;
    s_skeletal_gpu.cursor_x = 0;
    s_skeletal_gpu.cursor_y = 0;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_ASSERT expansion inflates the metric
nt_skeletal_mat34_t *nt_skeletal_gpu_reserve(uint16_t count, nt_deformation_binding_t *out_binding) {
    NT_ASSERT(s_skeletal_gpu.initialized);
    NT_ASSERT(out_binding != NULL);
    NT_ASSERT(count > 0);
    uint32_t texels = 3U * count;
    NT_ASSERT(texels <= s_skeletal_gpu.width && "skeletal_gpu: frame wider than the texture");

    /* Frames never span a row (shader fetches x + 3p + r on one row). */
    if ((uint32_t)s_skeletal_gpu.cursor_x + texels > s_skeletal_gpu.width) {
        s_skeletal_gpu.cursor_x = 0;
        s_skeletal_gpu.cursor_y++;
    }
    NT_ASSERT(s_skeletal_gpu.cursor_y < s_skeletal_gpu.height && "skeletal_gpu: texture capacity exceeded");

    uint16_t x = s_skeletal_gpu.cursor_x;
    uint16_t y = s_skeletal_gpu.cursor_y;
    *out_binding = (nt_deformation_binding_t){.texture = s_skeletal_gpu.texture, .x0 = x, .y0 = y, .x1 = x, .y1 = y, .alpha = 0.0F};
    s_skeletal_gpu.cursor_x = (uint16_t)(x + texels);
    size_t texel = ((size_t)y * s_skeletal_gpu.width) + x;
    return (nt_skeletal_mat34_t *)(s_skeletal_gpu.staging + (texel * NT_SKELETAL_GPU_TEXEL_FLOATS));
}

void nt_skeletal_gpu_flush(void) {
    NT_ASSERT(s_skeletal_gpu.initialized);
    uint16_t rows = (uint16_t)(s_skeletal_gpu.cursor_y + (s_skeletal_gpu.cursor_x > 0 ? 1 : 0));
    if (rows > 0) {
        nt_gfx_update_texture(s_skeletal_gpu.texture, 0, 0, s_skeletal_gpu.width, rows, s_skeletal_gpu.staging);
    }
}
// #endregion
