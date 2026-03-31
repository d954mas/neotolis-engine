#include "renderers/nt_text_renderer.h"

#include "core/nt_assert.h"
#include "font/nt_font.h"
#include "graphics/nt_gfx.h"
#include "log/nt_log.h"
#include "material/nt_material.h"

#include <string.h>

// #region UTF-8 decoder
/* Hoehrmann DFA UTF-8 decoder (MIT license: http://bjoern.hoehrmann.de/utf-8/decoder/dfa/) */
#define UTF8_ACCEPT 0
#define UTF8_REJECT 12

static const uint8_t s_utf8d[] = {
    // clang-format off
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    10,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3, 11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,
    0,12,24,36,60,96,84,12,12,12,48,72, 12,12,12,12,12,12,12,12,12,12,12,12,
    12,0,12,12,12,12,12,0,12,0,12,12,   12,24,12,12,12,12,12,24,12,24,12,12,
    12,12,12,12,12,12,12,24,12,12,12,12, 12,24,12,12,12,12,12,12,12,24,12,12,
    12,12,12,12,12,12,12,36,12,36,12,12, 12,36,12,12,12,12,12,36,12,36,12,12,
    12,36,12,12,12,12,12,12,12,12,12,12,
    // clang-format on
};

static uint32_t utf8_decode(uint32_t *state, uint32_t *codep, uint32_t byte) {
    uint32_t type = s_utf8d[byte];
    *codep = (*state != UTF8_ACCEPT) ? (byte & 0x3Fu) | (*codep << 6) : (0xFFu >> type) & byte;
    *state = s_utf8d[256 + *state + type];
    return *state;
}
// #endregion

// #region Vertex format
/* 64 bytes per vertex, matching slug_text.vert contract */
typedef struct {
    float position[2];     /* 8B: world-space quad corner */
    float texcoord[2];     /* 8B: em-space coordinate */
    float glyph_data[4];   /* 16B: packed uint via memcpy (curve_offset, band_row, curve_count, band_count) */
    float glyph_bounds[4]; /* 16B: bbox x0/y0/x1/y1 in em-space */
    float color[4];        /* 16B: RGBA float */
} nt_text_vertex_t;
_Static_assert(sizeof(nt_text_vertex_t) == 64, "text vertex stride must be 64 bytes");
// #endregion

// #region Module state
static struct {
    /* GPU resources */
    nt_pipeline_t pipeline;
    nt_buffer_t vbo; /* dynamic vertex buffer */
    nt_buffer_t ibo; /* immutable index buffer (pre-generated quad pattern) */

    /* CPU staging buffer (compile-time arrays per D-14) */
    nt_text_vertex_t vertices[NT_TEXT_RENDERER_MAX_VERTICES];
    uint32_t vertex_count;
    uint32_t glyph_count;

    /* Current state (per D-04, D-05) */
    nt_material_t material;
    nt_font_t font;

    /* Cached pipeline state */
    uint32_t pipeline_material_version; /* version when pipeline was last created */

    /* Dilation uniform */
    float dilation;

    bool initialized;
} s_text;

static uint16_t s_quad_indices[NT_TEXT_RENDERER_MAX_INDICES];
// #endregion

// #region Index buffer generation
static void generate_quad_indices(void) {
    for (uint32_t i = 0; i < NT_TEXT_RENDERER_MAX_GLYPHS; i++) {
        uint16_t base = (uint16_t)(i * 4);
        uint32_t idx = i * 6;
        s_quad_indices[idx + 0] = base;
        s_quad_indices[idx + 1] = (uint16_t)(base + 1);
        s_quad_indices[idx + 2] = (uint16_t)(base + 2);
        s_quad_indices[idx + 3] = (uint16_t)(base + 2);
        s_quad_indices[idx + 4] = (uint16_t)(base + 3);
        s_quad_indices[idx + 5] = base;
    }
}
// #endregion

// #region Pipeline creation
static void create_pipeline(void) {
    const nt_material_info_t *info = nt_material_get_info(s_text.material);
    if (!info || !info->ready) {
        return;
    }

    if (s_text.pipeline.id != 0) {
        nt_gfx_destroy_pipeline(s_text.pipeline);
    }

    /* Slug vertex layout: 5 attributes, stride = 64 bytes */
    nt_vertex_layout_t layout = {
        .attr_count = 5,
        .stride = 64,
        .attrs =
            {
                {.location = 0, .format = NT_FORMAT_FLOAT2, .offset = 0},  /* a_position */
                {.location = 1, .format = NT_FORMAT_FLOAT2, .offset = 8},  /* a_texcoord */
                {.location = 2, .format = NT_FORMAT_FLOAT4, .offset = 16}, /* a_glyph_data */
                {.location = 3, .format = NT_FORMAT_FLOAT4, .offset = 32}, /* a_glyph_bounds */
                {.location = 4, .format = NT_FORMAT_FLOAT4, .offset = 48}, /* a_color */
            },
    };

    s_text.pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .vertex_shader = (nt_shader_t){info->resolved_vs},
        .fragment_shader = (nt_shader_t){info->resolved_fs},
        .layout = layout,
        .depth_test = true,
        .depth_write = false,
        .depth_func = NT_DEPTH_LEQUAL,
        .blend = true,
        .blend_src = NT_BLEND_ONE,
        .blend_dst = NT_BLEND_ONE_MINUS_SRC_ALPHA,
        .label = "text_renderer",
    });

    /* Bind global UBO slot */
    nt_gfx_set_uniform_block(s_text.pipeline, "Globals", 0);

    s_text.pipeline_material_version = info->version;
}
// #endregion

// #region Lifecycle
void nt_text_renderer_init(void) {
    NT_ASSERT(!s_text.initialized);
    memset(&s_text, 0, sizeof(s_text));

    /* Generate quad indices */
    generate_quad_indices();

    /* Create dynamic vertex buffer */
    s_text.vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_VERTEX,
        .usage = NT_USAGE_DYNAMIC,
        .size = NT_TEXT_RENDERER_MAX_VERTICES * (uint32_t)sizeof(nt_text_vertex_t),
        .label = "text_vbo",
    });

    /* Create immutable index buffer with pre-generated quad pattern */
    s_text.ibo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_INDEX,
        .usage = NT_USAGE_IMMUTABLE,
        .data = s_quad_indices,
        .size = (uint32_t)sizeof(s_quad_indices),
        .index_type = 1, /* uint16 */
        .label = "text_ibo",
    });

    s_text.initialized = true;
}

void nt_text_renderer_shutdown(void) {
    if (!s_text.initialized) {
        return;
    }
    if (s_text.pipeline.id != 0) {
        nt_gfx_destroy_pipeline(s_text.pipeline);
    }
    nt_gfx_destroy_buffer(s_text.vbo);
    nt_gfx_destroy_buffer(s_text.ibo);
    memset(&s_text, 0, sizeof(s_text));
}

void nt_text_renderer_restore_gpu(void) {
    if (!s_text.initialized) {
        return;
    }

    /* Regenerate index data */
    generate_quad_indices();

    /* Recreate buffers */
    s_text.vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_VERTEX,
        .usage = NT_USAGE_DYNAMIC,
        .size = NT_TEXT_RENDERER_MAX_VERTICES * (uint32_t)sizeof(nt_text_vertex_t),
        .label = "text_vbo",
    });

    s_text.ibo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_INDEX,
        .usage = NT_USAGE_IMMUTABLE,
        .data = s_quad_indices,
        .size = (uint32_t)sizeof(s_quad_indices),
        .index_type = 1, /* uint16 */
        .label = "text_ibo",
    });

    /* Recreate pipeline if material was set */
    if (s_text.material.id != 0) {
        s_text.pipeline_material_version = 0; /* force recreation */
        create_pipeline();
    }
}
// #endregion

// #region State setters
void nt_text_renderer_set_material(nt_material_t mat) {
    NT_ASSERT(s_text.initialized);
    if (s_text.material.id == mat.id) {
        return;
    }

    /* Auto-flush on material change (D-19) */
    if (s_text.glyph_count > 0) {
        nt_text_renderer_flush();
    }

    s_text.material = mat;
    s_text.pipeline_material_version = 0; /* force pipeline recreation */
    create_pipeline();
}

void nt_text_renderer_set_font(nt_font_t font) {
    NT_ASSERT(s_text.initialized);
    if (s_text.font.id == font.id) {
        return;
    }

    /* Auto-flush on font change (D-18) */
    if (s_text.glyph_count > 0) {
        nt_text_renderer_flush();
    }

    s_text.font = font;
}
// #endregion

// #region Vertex generation helpers
static void pack_uint_as_float(float *out, uint32_t val) { memcpy(out, &val, 4); /* bit-preserving uint-to-float, never cast (Pitfall 1) */ }

static void transform_point(float out[2], const float model[16], float x, float y) {
    /* mat4 * vec4(x, y, 0, 1) -- extract x,y from result */
    out[0] = model[0] * x + model[4] * y + model[12];
    out[1] = model[1] * x + model[5] * y + model[13];
}

static void emit_quad(const nt_glyph_cache_entry_t *g, const float model[16], float scale, float pen_x, const float color[4], uint8_t band_count) {
    if (s_text.glyph_count >= NT_TEXT_RENDERER_MAX_GLYPHS) {
        nt_text_renderer_flush();
    }

    /* Local quad corners (scaled from font units to target size) */
    float x0 = pen_x + (float)g->bbox_x0 * scale;
    float y0 = (float)g->bbox_y0 * scale;
    float x1 = pen_x + (float)g->bbox_x1 * scale;
    float y1 = (float)g->bbox_y1 * scale;

    /* Em-space coordinates (unscaled, for shader) */
    float em_x0 = (float)g->bbox_x0;
    float em_y0 = (float)g->bbox_y0;
    float em_x1 = (float)g->bbox_x1;
    float em_y1 = (float)g->bbox_y1;

    /* Pack glyph data as uint bit patterns */
    float gd0, gd1, gd2, gd3;
    pack_uint_as_float(&gd0, g->curve_offset);
    pack_uint_as_float(&gd1, (uint32_t)g->band_row);
    pack_uint_as_float(&gd2, (uint32_t)g->curve_count);
    pack_uint_as_float(&gd3, (uint32_t)band_count);

    /* 4 vertices per quad: BL, BR, TR, TL */
    uint32_t vi = s_text.vertex_count;
    nt_text_vertex_t *v = &s_text.vertices[vi];

    /* Vertex 0: bottom-left */
    transform_point(v[0].position, model, x0, y0);
    v[0].texcoord[0] = em_x0;
    v[0].texcoord[1] = em_y0;
    v[0].glyph_data[0] = gd0;
    v[0].glyph_data[1] = gd1;
    v[0].glyph_data[2] = gd2;
    v[0].glyph_data[3] = gd3;
    v[0].glyph_bounds[0] = em_x0;
    v[0].glyph_bounds[1] = em_y0;
    v[0].glyph_bounds[2] = em_x1;
    v[0].glyph_bounds[3] = em_y1;
    memcpy(v[0].color, color, 16);

    /* Vertex 1: bottom-right */
    transform_point(v[1].position, model, x1, y0);
    v[1].texcoord[0] = em_x1;
    v[1].texcoord[1] = em_y0;
    v[1].glyph_data[0] = gd0;
    v[1].glyph_data[1] = gd1;
    v[1].glyph_data[2] = gd2;
    v[1].glyph_data[3] = gd3;
    v[1].glyph_bounds[0] = em_x0;
    v[1].glyph_bounds[1] = em_y0;
    v[1].glyph_bounds[2] = em_x1;
    v[1].glyph_bounds[3] = em_y1;
    memcpy(v[1].color, color, 16);

    /* Vertex 2: top-right */
    transform_point(v[2].position, model, x1, y1);
    v[2].texcoord[0] = em_x1;
    v[2].texcoord[1] = em_y1;
    v[2].glyph_data[0] = gd0;
    v[2].glyph_data[1] = gd1;
    v[2].glyph_data[2] = gd2;
    v[2].glyph_data[3] = gd3;
    v[2].glyph_bounds[0] = em_x0;
    v[2].glyph_bounds[1] = em_y0;
    v[2].glyph_bounds[2] = em_x1;
    v[2].glyph_bounds[3] = em_y1;
    memcpy(v[2].color, color, 16);

    /* Vertex 3: top-left */
    transform_point(v[3].position, model, x0, y1);
    v[3].texcoord[0] = em_x0;
    v[3].texcoord[1] = em_y1;
    v[3].glyph_data[0] = gd0;
    v[3].glyph_data[1] = gd1;
    v[3].glyph_data[2] = gd2;
    v[3].glyph_data[3] = gd3;
    v[3].glyph_bounds[0] = em_x0;
    v[3].glyph_bounds[1] = em_y0;
    v[3].glyph_bounds[2] = em_x1;
    v[3].glyph_bounds[3] = em_y1;
    memcpy(v[3].color, color, 16);

    s_text.vertex_count += 4;
    s_text.glyph_count++;
}
// #endregion

// #region Draw
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_text_renderer_draw(const char *utf8, const float model[16], float size, const float color[4]) {
    NT_ASSERT(s_text.initialized);
    if (!utf8 || !*utf8) {
        return;
    }
    if (s_text.font.id == 0) {
        NT_LOG_WARN("nt_text_renderer_draw: no font set");
        return;
    }

    nt_font_metrics_t metrics = nt_font_get_metrics(s_text.font);
    if (metrics.units_per_em == 0) {
        return;
    }
    float scale = size / (float)metrics.units_per_em;
    uint8_t band_count = nt_font_get_band_count(s_text.font);

    /* Cache generation before shaping (Pitfall 3) */
    uint32_t gen_before = nt_font_get_cache_generation(s_text.font);

    uint32_t state = UTF8_ACCEPT;
    uint32_t codepoint = 0;
    uint32_t prev_cp = 0;
    float pen_x = 0.0f;

    for (const uint8_t *p = (const uint8_t *)utf8; *p; p++) {
        if (utf8_decode(&state, &codepoint, *p) != UTF8_ACCEPT) {
            continue;
        }

        /* Apply kern pair */
        if (prev_cp != 0) {
            int16_t kern = nt_font_get_kern(s_text.font, prev_cp, codepoint);
            pen_x += (float)kern * scale;
        }

        const nt_glyph_cache_entry_t *g = nt_font_lookup_glyph(s_text.font, codepoint);
        if (!g) {
            prev_cp = codepoint;
            continue;
        }

        /* Emit quad if glyph has visible bbox */
        if (g->bbox_x1 > g->bbox_x0) {
            emit_quad(g, model, scale, pen_x, color, band_count);
        }

        pen_x += (float)g->advance * scale;
        prev_cp = codepoint;
    }

    /* Check cache generation after loop (Pitfall 3) */
    uint32_t gen_after = nt_font_get_cache_generation(s_text.font);
    if (gen_after != gen_before) {
        NT_LOG_WARN("font cache flushed during text shaping -- batch may contain stale glyph data");
    }
}
// #endregion

// #region Flush
void nt_text_renderer_flush(void) {
    if (s_text.glyph_count == 0) {
        return;
    }
    if (s_text.pipeline.id == 0) {
        /* Pipeline not ready yet -- try to create it */
        if (s_text.material.id != 0) {
            create_pipeline();
        }
        if (s_text.pipeline.id == 0) {
            NT_LOG_WARN("nt_text_renderer_flush: no pipeline -- discarding %u glyphs", s_text.glyph_count);
            s_text.vertex_count = 0;
            s_text.glyph_count = 0;
            return;
        }
    }

    /* Upload staging buffer to GPU */
    nt_gfx_update_buffer(s_text.vbo, s_text.vertices, s_text.vertex_count * (uint32_t)sizeof(nt_text_vertex_t));

    /* Bind pipeline and resources */
    nt_gfx_bind_pipeline(s_text.pipeline);
    nt_gfx_bind_vertex_buffer(s_text.vbo);
    nt_gfx_bind_index_buffer(s_text.ibo);

    /* Bind font textures */
    if (s_text.font.id != 0) {
        nt_gfx_bind_texture(nt_font_get_curve_texture(s_text.font), 0);
        nt_gfx_bind_texture(nt_font_get_band_texture(s_text.font), 1);
        nt_gfx_set_uniform_int("u_curve_tex_width", (int)nt_font_get_curve_texture_width(s_text.font));
    }

    /* Set uniforms */
    nt_gfx_set_uniform_float("u_dilation", s_text.dilation);

    /* Single draw call per flush */
    nt_gfx_draw_indexed(0, s_text.glyph_count * 6, s_text.vertex_count);

    /* Reset staging buffer */
    s_text.vertex_count = 0;
    s_text.glyph_count = 0;
}
// #endregion

// #region Test accessors
#ifdef NT_TEXT_RENDERER_TEST_ACCESS
uint32_t nt_text_renderer_test_vertex_count(void) { return s_text.vertex_count; }
uint32_t nt_text_renderer_test_glyph_count(void) { return s_text.glyph_count; }
const void *nt_text_renderer_test_vertices(void) { return s_text.vertices; }
bool nt_text_renderer_test_initialized(void) { return s_text.initialized; }
#endif
// #endregion
