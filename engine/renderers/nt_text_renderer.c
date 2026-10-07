#include "renderers/nt_text_renderer.h"

#include "core/nt_assert.h"
#include "font/nt_font.h"
#include "font/nt_font_hot.h"
#include "graphics/nt_gfx.h"
#include "log/nt_log.h"
#include "material/nt_material.h"
#include "renderers/nt_renderer_shared.h"

#include "utf8/nt_utf8.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

// #region Vertex format
/* 52 bytes per vertex, matching slug_text.vert contract */
typedef struct {
    float position[3];     /* 12B: world-space quad corner (full 3D) */
    float texcoord[2];     /* 8B: em-space coordinate */
    float glyph_data[2];   /* 8B: packed uint via memcpy (band_row, band_count) */
    float glyph_bounds[4]; /* 16B: bbox x0/y0/x1/y1 in em-space */
    uint32_t color;        /* 4B: RGBA8 0xAABBGGRR, read normalized */
    float depth_bias;      /* 4B: per-glyph clip-space depth bias (subtracted from NDC z in the VS) */
} nt_text_vertex_t;
_Static_assert(sizeof(nt_text_vertex_t) == 52, "text vertex stride must be 52 bytes");
// #endregion

// #region Module state
static struct {
    nt_renderer_pipeline_entry_t pipelines[NT_TEXT_RENDERER_MAX_PIPELINES];
    uint16_t pipeline_count;
    /* Weak: a context loss frees it, and the next resolve recreates it over the current frame buffers. */
    nt_vertex_input_t vertex_input;

    /* One-shot so a load-time skip does not spam; re-armed when a pipeline is built. */
    bool warned_program_not_ready;

    /* set_material; draws read it */
    struct {
        nt_material_t material;
        const nt_material_info_t *info;
        nt_program_t program; /* the program `pipeline` was built on: a replace keeps the material handle */
        nt_pipeline_t pipeline;
        uint64_t frame; /* gfx frame of the selection */
        nt_hash32_t curve_name;
    } current;

    /* Params last written, per program: equal params record nothing, so adjacent draws merge. */
    struct {
        nt_program_t program;
        nt_material_t material;
        float params[NT_MATERIAL_MAX_PARAMS][4];
    } recorded;

    /* Quads of the draw_n in progress: contiguous from first_offset, drawn once. */
    struct {
        uint32_t first_offset;
        uint32_t quads;
    } run;

#ifdef NT_TEST_ACCESS
    /* Observed at every draw_n entry, even when the font has no glyph data (units_per_em == 0), so
     * walker and emit tests pin the model and style the UI built without a real font fixture. */
    float test_last_model[16];
    uint32_t test_draw_n_calls;
    uint32_t test_font_switches; /* draw_n calls whose font differs from the previous call's */
    nt_font_t test_prev_font;
    float test_max_oblique;
    float test_max_weight;
    float test_max_outline_w;
    bool test_saw_underline;
#endif
} s_text;
// #endregion

// #region Pipeline cache
static nt_pipeline_t find_or_create_pipeline(const nt_material_info_t *info) {
    /* One query covers every state: no program yet, a program that died with the
     * context, and a program its owner destroyed. */
    if (!nt_gfx_program_ready(info->program)) {
        nt_renderer_warn_program_not_ready(&s_text.warned_program_not_ready, info);
        return (nt_pipeline_t){0};
    }
    const nt_pipeline_desc_t desc = {
        .program = info->program,
        .depth_test = info->depth_test,
        .depth_write = info->depth_write,
        .depth_func = NT_DEPTH_LEQUAL,
        .blend = info->blend,
        .cull_mode = (uint8_t)info->cull_mode,
        .label = "text_renderer",
    };
    const nt_gfx_pipeline_key_t key = nt_gfx_pipeline_key(&desc);
    const nt_pipeline_t cached = nt_renderer_pipeline_cache_find(s_text.pipelines, s_text.pipeline_count, &key);
    if (cached.id != 0) {
        return cached;
    }
    return nt_renderer_pipeline_cache_insert(s_text.pipelines, &s_text.pipeline_count, NT_TEXT_RENDERER_MAX_PIPELINES, &key, &desc, &s_text.warned_program_not_ready);
}

static nt_vertex_input_t find_or_create_vertex_input(void) {
    if (nt_gfx_vertex_input_valid(s_text.vertex_input)) {
        return s_text.vertex_input;
    }
    const nt_buffer_t vbo = nt_gfx_frame_buffer(NT_GFX_FRAME_VERTEX);
    const nt_buffer_t ibo = nt_gfx_frame_buffer(NT_GFX_FRAME_INDEX);
    if (vbo.id == 0 || ibo.id == 0) {
        return NT_VERTEX_INPUT_INVALID; /* lost context: the frame buffers come back at restore */
    }
    s_text.vertex_input = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){
        .layout =
            {
                .attr_count = 6,
                .stride = (uint16_t)sizeof(nt_text_vertex_t),
                .attrs =
                    {
                        {.location = 0, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 0},                                                     /* a_position */
                        {.location = 1, .type = NT_VERTEX_FLOAT, .count = 2, .offset = 12},                                                    /* a_texcoord */
                        {.location = 2, .type = NT_VERTEX_FLOAT, .count = 2, .offset = 20},                                                    /* a_glyph_data */
                        {.location = 3, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 28},                                                    /* a_glyph_bounds */
                        {.location = 4, .type = NT_VERTEX_UINT8, .count = 4, .normalized = true, .offset = offsetof(nt_text_vertex_t, color)}, /* a_color */
                        {.location = 5, .type = NT_VERTEX_FLOAT, .count = 1, .offset = offsetof(nt_text_vertex_t, depth_bias)},                /* a_depth_bias */
                    },
            },
        .vertex_buffer = vbo,
        .index_buffer = ibo,
        .label = "text_vi",
    });
    return s_text.vertex_input;
}
// #endregion

// #region Lifecycle
void nt_text_renderer_shutdown(void) {
    for (uint16_t i = 0; i < s_text.pipeline_count; i++) {
        nt_gfx_destroy_pipeline(s_text.pipelines[i].pipeline);
    }
    nt_gfx_destroy_vertex_input(s_text.vertex_input);
    memset(&s_text, 0, sizeof(s_text));
}
// #endregion

// #region State setters
/* A failed pipeline or vertex input leaves the selection not drawable (pipeline 0) until the next
 * resolve retries. */
static void resolve_material(nt_material_t mat, const nt_material_info_t *info) {
    /* A zero budget would make all text silently vanish. */
    NT_ASSERT(g_nt_gfx_frame_storage[NT_GFX_FRAME_VERTEX].capacity > 0 && g_nt_gfx_frame_storage[NT_GFX_FRAME_INDEX].capacity > 0 &&
              "text renderer: set nt_gfx_desc_t.frame_capacity[NT_GFX_FRAME_VERTEX] and [NT_GFX_FRAME_INDEX]");
    NT_ASSERT(info->tex_count == 0 && "text material declares textures: every sampler unit belongs to the font");
    s_text.current.material = mat;
    s_text.current.info = info;
    s_text.current.program = info->program;
    s_text.current.pipeline = find_or_create_pipeline(info);
    if (s_text.current.pipeline.id != 0 && find_or_create_vertex_input().id == 0) {
        s_text.current.pipeline = (nt_pipeline_t){0};
    }
    s_text.current.curve_name = nt_hash32_str("u_curve_texture");
}

void nt_text_renderer_set_material(nt_material_t mat) {
    const nt_material_info_t *info = nt_material_get_info(mat);
    /* Assignment, not liveness: on the frame the context dies the program is
     * already dead here, and trapping on that would crash a recoverable event. */
    NT_ASSERT(info != NULL && info->program.id != 0 && "nt_text_renderer_set_material: invalid material or material has no program");
    /* Params are compared when recorded, and resources are not stepped between draws of a frame.
     * A failed resolve (pipeline 0) retries next frame; a destroyed program takes its pipeline
     * with it, so that selection resolves again. */
    const uint64_t frame = g_nt_gfx.counters.frame_sequence;
    if (mat.id == s_text.current.material.id && info->program.id == s_text.current.program.id && frame == s_text.current.frame &&
        (s_text.current.pipeline.id == 0 || nt_gfx_pipeline_valid(s_text.current.pipeline))) {
        return;
    }
    resolve_material(mat, info);
    s_text.current.frame = frame;
}

// #endregion

// #region Vertex generation helpers
static void pack_uint_as_float(float *out, uint32_t val) { memcpy(out, &val, 4); /* bit-preserving uint-to-float, never cast */ }

static void transform_point(float out[3], const float model[16], float x, float y) {
    /* mat4 * vec4(x, y, 0, 1) -- full 3D transform */
    out[0] = model[0] * x + model[4] * y + model[12];
    out[1] = model[1] * x + model[5] * y + model[13];
    out[2] = model[2] * x + model[6] * y + model[14];
}

/* The first quad aligns the run to the stride; the rest follow contiguously, since glyph lookups
 * between quads allocate no frame storage. */
static nt_text_vertex_t *alloc_quad(void) {
    uint32_t offset = 0;
    nt_text_vertex_t *v =
        (nt_text_vertex_t *)nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, 4U * (uint32_t)sizeof(nt_text_vertex_t), (s_text.run.quads == 0) ? (uint32_t)sizeof(nt_text_vertex_t) : 1U, &offset);
    if (s_text.run.quads == 0) {
        s_text.run.first_offset = offset;
    }
    s_text.run.quads++;
    return v;
}

static void emit_quad(const nt_glyph_cache_entry_t *g, const float model[16], float scale, float pen_x, float pen_y, uint32_t color, float glyph_bias) {

    /* FP16 rounding can move controls past bbox by at most maxabs/2048.
     * Add 0.5 px for coverage falloff; assumes 1 world unit = 1 screen pixel. */
    const float round_x = fmaxf(fabsf((float)g->bbox_x0), fabsf((float)g->bbox_x1)) / 2048.0F;
    const float round_y = fmaxf(fabsf((float)g->bbox_y0), fabsf((float)g->bbox_y1)) / 2048.0F;
    const float dilate_x = (0.5F / scale) + round_x;
    const float dilate_y = (0.5F / scale) + round_y;

    float x0 = pen_x + ((float)g->bbox_x0 * scale) - 0.5F - (round_x * scale);
    float y0 = pen_y + ((float)g->bbox_y0 * scale) - 0.5F - (round_y * scale);
    float x1 = pen_x + ((float)g->bbox_x1 * scale) + 0.5F + (round_x * scale);
    float y1 = pen_y + ((float)g->bbox_y1 * scale) + 0.5F + (round_y * scale);

    float em_x0 = (float)g->bbox_x0 - dilate_x;
    float em_y0 = (float)g->bbox_y0 - dilate_y;
    float em_x1 = (float)g->bbox_x1 + dilate_x;
    float em_y1 = (float)g->bbox_y1 + dilate_y;

    /* Pack glyph data as uint bit patterns */
    float gd0;
    float gd1;
    pack_uint_as_float(&gd0, (uint32_t)g->band_row);
    pack_uint_as_float(&gd1, (uint32_t)g->band_count);

    /* 4 vertices per quad: BL, BR, TR, TL */
    nt_text_vertex_t *v = alloc_quad();

    /* Shared fields — fill once in v[0], copy to v[1..3]. glyph_bounds is the
     * UNDILATED glyph bbox so the shader's band lookup stays correct. */
    v[0].glyph_data[0] = gd0;
    v[0].glyph_data[1] = gd1;
    v[0].glyph_bounds[0] = (float)g->bbox_x0;
    v[0].glyph_bounds[1] = (float)g->bbox_y0;
    v[0].glyph_bounds[2] = (float)g->bbox_x1;
    v[0].glyph_bounds[3] = (float)g->bbox_y1;
    v[0].color = color;
    v[0].depth_bias = glyph_bias;
    v[1] = v[0];
    v[2] = v[0];
    v[3] = v[0];

    /* Unique per-corner: position + texcoord */
    transform_point(v[0].position, model, x0, y0); /* BL */
    v[0].texcoord[0] = em_x0;
    v[0].texcoord[1] = em_y0;

    transform_point(v[1].position, model, x1, y0); /* BR */
    v[1].texcoord[0] = em_x1;
    v[1].texcoord[1] = em_y0;

    transform_point(v[2].position, model, x1, y1); /* TR */
    v[2].texcoord[0] = em_x1;
    v[2].texcoord[1] = em_y1;

    transform_point(v[3].position, model, x0, y1); /* TL */
    v[3].texcoord[0] = em_x0;
    v[3].texcoord[1] = em_y1;
}
// #endregion

// #region Decoration sentinel quad
/* Sentinel "glyph" with band_count=0: the Slug fragment shaders return coverage=1, so it fills solid for
 * underline/strike. Pixel-space corners (already include pen/scale) flow through the same transform_point
 * path as glyphs — correct under any model matrix (world / 3D), no scissor/viewport hijack. */
static void emit_decoration_quad(const float model[16], float x0, float y0, float x1, float y1, uint32_t color, float glyph_bias) {
    nt_text_vertex_t *v = alloc_quad();

    float band0;
    pack_uint_as_float(&band0, 0U); /* band_count=0 = decoration sentinel */
    v[0].glyph_data[0] = 0.0F;
    v[0].glyph_data[1] = band0;
    /* bounds/texcoord unused: the shader returns before reading them for the sentinel. */
    v[0].glyph_bounds[0] = 0.0F;
    v[0].glyph_bounds[1] = 0.0F;
    v[0].glyph_bounds[2] = 0.0F;
    v[0].glyph_bounds[3] = 0.0F;
    v[0].color = color;
    v[0].depth_bias = glyph_bias;
    v[1] = v[0];
    v[2] = v[0];
    v[3] = v[0];

    transform_point(v[0].position, model, x0, y0); /* BL */
    transform_point(v[1].position, model, x1, y0); /* BR */
    transform_point(v[2].position, model, x1, y1); /* TR */
    transform_point(v[3].position, model, x0, y1); /* TL */
    v[0].texcoord[0] = 0.0F;
    v[0].texcoord[1] = 0.0F;
    v[1].texcoord[0] = 0.0F;
    v[1].texcoord[1] = 0.0F;
    v[2].texcoord[0] = 0.0F;
    v[2].texcoord[1] = 0.0F;
    v[3].texcoord[0] = 0.0F;
    v[3].texcoord[1] = 0.0F;
}
// #endregion

// #region Record
/* Pipeline first: uniforms and the texture set land on its program. */
static void record_state(nt_font_t font) {
    const nt_material_info_t *mi = s_text.current.info;
    nt_gfx_bind_pipeline(s_text.current.pipeline);
    const size_t param_bytes = (size_t)mi->param_count * sizeof(mi->params[0]);
    if (s_text.recorded.program.id != s_text.current.program.id || s_text.recorded.material.id != s_text.current.material.id || memcmp(s_text.recorded.params, mi->params, param_bytes) != 0) {
        nt_renderer_set_material_uniforms(mi);
        s_text.recorded.program = s_text.current.program;
        s_text.recorded.material = s_text.current.material;
        memcpy(s_text.recorded.params, mi->params, param_bytes);
    }
    const nt_gfx_texture_binding_t curve = {.name = s_text.current.curve_name, .texture = nt_font_get_curve_texture(font), .sampler = NT_SAMPLER_DEFAULT};
    nt_gfx_apply_texture_bindings(&curve, 1);
    nt_gfx_bind_vertex_input(s_text.vertex_input);
}

/* Indices are written once the quad count is known; a run that emitted nothing records nothing. */
static void draw_run(nt_font_t font) {
    const uint32_t quads = s_text.run.quads;
    if (quads == 0) {
        return;
    }
    NT_ASSERT(g_nt_gfx_frame_storage[NT_GFX_FRAME_VERTEX].used == s_text.run.first_offset + (quads * 4U * (uint32_t)sizeof(nt_text_vertex_t)) && "text run: allocation not contiguous");
    const uint32_t base = s_text.run.first_offset / (uint32_t)sizeof(nt_text_vertex_t);
    uint32_t offset = 0;
    uint32_t *idx = (uint32_t *)nt_gfx_frame_alloc(NT_GFX_FRAME_INDEX, quads * 6U * 4U, 4U, &offset);
    for (uint32_t q = 0; q < quads; q++) {
        const uint32_t v = base + (q * 4U);
        idx[0] = v;
        idx[1] = v + 1U;
        idx[2] = v + 2U;
        idx[3] = v + 2U;
        idx[4] = v + 3U;
        idx[5] = v;
        idx += 6;
    }
    record_state(font);
    nt_gfx_draw_indexed(offset / 4U, quads * 6U, quads * 4U);
}
// #endregion

// #region Draw
/* Pen advance (kern, tracking, newline) is identical every pass, so passes register exactly. Walked
 * once per active pass (shadow, outline, fill), grouped and never interleaved: per-glyph interleave
 * breaks premultiplied-alpha compositing when glyphs overlap. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void emit_glyph_pass(const nt_text_style_t *style, const uint8_t *p, const uint8_t *end, const float model[16], float scale, float line_advance, nt_font_slot_t *slot, int16_t key_offset,
                            uint32_t color, float off_x, float off_y, float *glyph_bias) {
    uint32_t state = NT_UTF8_ACCEPT;
    uint32_t codepoint = 0;
    uint32_t prev_cp = 0;
    float pen_x = 0.0F;
    float pen_y = 0.0F;
    bool had_glyph_on_line = false;

    for (; p < end; p++) {
        if (nt_utf8_decode(&state, &codepoint, *p) != NT_UTF8_ACCEPT) {
            if (state == NT_UTF8_REJECT) {
                state = NT_UTF8_ACCEPT; /* recover: skip bad byte, continue parsing */
            }
            continue;
        }
        if (codepoint == '\r') {
            prev_cp = 0;
            continue;
        }
        if (codepoint == '\n') {
            pen_x = 0.0F;
            pen_y -= line_advance;
            prev_cp = 0;
            had_glyph_on_line = false;
            continue;
        }
        if (had_glyph_on_line) {
            pen_x += style->letter_tracking;
        }
        if (prev_cp != 0) {
            pen_x += (float)nt_font_get_kern_in_slot(slot, prev_cp, codepoint) * scale;
        }

        const nt_glyph_cache_entry_t *g = nt_font_lookup_glyph_offset(slot, codepoint, key_offset);
        if (!g) {
            prev_cp = codepoint;
            continue;
        }
        if (g->bbox_x1 > g->bbox_x0) {
            emit_quad(g, model, scale, pen_x + off_x, pen_y + off_y, color, *glyph_bias);
            *glyph_bias += style->glyph_depth_bias;
        }
        pen_x += (float)g->advance * scale;
        prev_cp = codepoint;
        had_glyph_on_line = true;
    }
}

/* One underline/strike sentinel quad per LINE (continuous per same-style segment; within one
 * draw_n the whole run is one style, so the segment boundary is the newline). Y and thickness come from
 * the scaled v5 metrics. Exact vertical sign is a visual-QA concern. */
static void emit_line_deco_quads(const nt_text_style_t *style, const float model[16], float scale, float x1, float pen_y, nt_font_metrics_t metrics, float *glyph_bias) {
    if (style->underline) {
        float top = pen_y + ((float)metrics.underline_position * scale); /* underline_position = top edge, below baseline */
        float bot = pen_y + ((float)(metrics.underline_position - metrics.underline_thickness) * scale);
        emit_decoration_quad(model, 0.0F, bot, x1, top, style->color, *glyph_bias);
        *glyph_bias += style->glyph_depth_bias;
    }
    if (style->strikethrough) {
        float bot = pen_y + ((float)metrics.strikeout_position * scale); /* strikeout_position = above baseline */
        float top = pen_y + ((float)(metrics.strikeout_position + metrics.strikeout_size) * scale);
        emit_decoration_quad(model, 0.0F, bot, x1, top, style->color, *glyph_bias);
        *glyph_bias += style->glyph_depth_bias;
    }
}

/* Walk the run once (advance only) to find each line's pixel extent, then emit its decoration quads. */
static void emit_line_decorations(const nt_text_style_t *style, const uint8_t *p, const uint8_t *end, const float model[16], float scale, float line_advance, nt_font_slot_t *slot,
                                  nt_font_metrics_t metrics, int16_t key_offset, float *glyph_bias) {
    uint32_t state = NT_UTF8_ACCEPT;
    uint32_t codepoint = 0;
    uint32_t prev_cp = 0;
    float pen_x = 0.0F;
    float pen_y = 0.0F;
    bool had_glyph_on_line = false;

    for (; p < end; p++) {
        if (nt_utf8_decode(&state, &codepoint, *p) != NT_UTF8_ACCEPT) {
            if (state == NT_UTF8_REJECT) {
                state = NT_UTF8_ACCEPT;
            }
            continue;
        }
        if (codepoint == '\r') {
            prev_cp = 0;
            continue;
        }
        if (codepoint == '\n') {
            if (had_glyph_on_line) {
                emit_line_deco_quads(style, model, scale, pen_x, pen_y, metrics, glyph_bias);
            }
            pen_x = 0.0F;
            pen_y -= line_advance;
            prev_cp = 0;
            had_glyph_on_line = false;
            continue;
        }
        if (had_glyph_on_line) {
            pen_x += style->letter_tracking;
        }
        if (prev_cp != 0) {
            pen_x += (float)nt_font_get_kern_in_slot(slot, prev_cp, codepoint) * scale;
        }

        /* advance is weight-independent; reuse the fill variant (resident from the fill pass) so no extra variant is warmed */
        const nt_glyph_cache_entry_t *g = nt_font_lookup_glyph_offset(slot, codepoint, key_offset);
        if (!g) {
            prev_cp = codepoint;
            continue;
        }
        pen_x += (float)g->advance * scale;
        prev_cp = codepoint;
        had_glyph_on_line = true;
    }
    if (had_glyph_on_line) {
        emit_line_deco_quads(style, model, scale, pen_x, pen_y, metrics, glyph_bias);
    }
}

#ifdef NT_TEST_ACCESS
static void observe_call(const nt_text_style_t *style, const float model[16]) {
    memcpy(s_text.test_last_model, model, sizeof s_text.test_last_model);
    s_text.test_draw_n_calls++;
    if (style->font.id != s_text.test_prev_font.id) {
        s_text.test_font_switches++;
        s_text.test_prev_font = style->font;
    }
    s_text.test_max_oblique = fmaxf(s_text.test_max_oblique, style->oblique);
    s_text.test_max_weight = fmaxf(s_text.test_max_weight, style->weight_em);
    s_text.test_max_outline_w = fmaxf(s_text.test_max_outline_w, style->outline_w);
    s_text.test_saw_underline = s_text.test_saw_underline || style->underline;
}
#endif

/* Values the font math cannot take: offset and quantize would turn a NaN into garbage geometry. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- flat finite-value and feature preconditions.
static void assert_style(const nt_text_style_t *style) {
    NT_ASSERT(style->font.id != 0 && "nt_text_renderer_draw_n: style has no font");
    NT_ASSERT(isfinite(style->size) && isfinite(style->letter_tracking) && isfinite(style->line_leading) && "nt_text_renderer_draw_n: non-finite size or spacing");
    NT_ASSERT(isfinite(style->weight_em) && isfinite(style->outline_w) && isfinite(style->shadow_dx) && isfinite(style->shadow_dy) && "nt_text_renderer_draw_n: non-finite decoration");
    NT_ASSERT(isfinite(style->oblique) && isfinite(style->glyph_depth_bias) && "nt_text_renderer_draw_n: non-finite oblique or depth bias");
#if !NT_FONT_EMBOLDEN_ENABLED
    NT_ASSERT(style->weight_em == 0.0F && style->outline_w <= 0.0F && "weight and outline require NT_FONT_EMBOLDEN_ENABLED=ON");
#endif
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_text_renderer_draw_n(const nt_text_style_t *style, const float model[16], const char *utf8, size_t len) {
    NT_ASSERT(style != NULL && "nt_text_renderer_draw_n: style is required");
#ifdef NT_TEST_ACCESS
    observe_call(style, model);
#endif
    NT_ASSERT(s_text.current.frame == g_nt_gfx.counters.frame_sequence && "nt_text_renderer_draw_n: call nt_text_renderer_set_material in this frame");
    assert_style(style);
    if (len == 0U || utf8 == NULL || s_text.current.pipeline.id == 0) {
        return;
    }

    nt_font_metrics_t metrics = nt_font_get_metrics(style->font);
    if (metrics.units_per_em == 0) {
        return; /* no resource loaded yet (or all unmounted) */
    }
    if (!nt_gfx_texture_ready(nt_font_get_curve_texture(style->font))) {
        return;
    }
    const float size = style->size;
    float scale = size / (float)metrics.units_per_em;

    nt_font_slot_t *slot = nt_font_get_slot(style->font);
    NT_ASSERT(slot != NULL);

    /* Fold synthetic-oblique into the model once (constant for the whole call): col0/col1 of the already
     * Y-flipped model ARE the text-local axes, so out.col1 += oblique*col0 leans x by oblique*y about the
     * baseline — reproduces a caller-baked lean for ANY caller's matrix. */
    const float *m = model;
    float oblique_model[16];
    if (style->oblique != 0.0F) {
        memcpy(oblique_model, model, sizeof oblique_model);
        for (int r = 0; r < 4; ++r) {
            oblique_model[4 + r] += style->oblique * model[r];
        }
        m = oblique_model;
    }

    /* Natural line advance from font metrics, plus user-supplied leading. */
    const float natural_line_advance = (metrics.line_height != 0) ? ((float)metrics.line_height * scale) : size;
    const float line_advance = natural_line_advance + style->line_leading;

    const uint8_t *p = (const uint8_t *)utf8;
    const uint8_t *end = p + len;

    /* Per-pass embolden cache key from the weight (font units): fill uses the weight, outline
     * grows by outline_w, and the shadow reuses the outline key. */
#if NT_FONT_EMBOLDEN_ENABLED
    const float upm = (float)metrics.units_per_em;
    const int16_t fill_key = nt_font_quantize_weight(style->weight_em * upm);
    /* The shadow silhouette follows the outline width, not its alpha, so a fading outline cannot swap it. */
    const int16_t outline_key = (int16_t)(style->outline_w > 0.0F ? nt_font_quantize_weight((style->weight_em + style->outline_w) * upm) : fill_key);
    const int16_t shadow_key = outline_key;
    const bool outline_active = (style->outline_w > 0.0F && (style->outline_color >> 24) != 0U);
#else
    const int16_t fill_key = 0;
    const int16_t shadow_key = 0;
#endif
    const bool shadow_active = (style->shadow_color >> 24) != 0U;

    float glyph_bias = 0.0F; /* accumulates across ALL passes so they separate in depth-written world text */

    /* Painter order: shadow (behind) → outline → fill (top), each grouped over the whole run. One draw
     * keeps it: triangles of a draw blend in index order. */
    s_text.run.quads = 0;
    if (shadow_active) {
        emit_glyph_pass(style, p, end, m, scale, line_advance, slot, shadow_key, style->shadow_color, style->shadow_dx * size, style->shadow_dy * size, &glyph_bias);
    }
#if NT_FONT_EMBOLDEN_ENABLED
    if (outline_active) {
        emit_glyph_pass(style, p, end, m, scale, line_advance, slot, outline_key, style->outline_color, 0.0F, 0.0F, &glyph_bias);
    }
#endif
    emit_glyph_pass(style, p, end, m, scale, line_advance, slot, fill_key, style->color, 0.0F, 0.0F, &glyph_bias);

    /* Underline/strike sentinel quads last (on top of fill), one continuous quad per line. */
    if (style->underline || style->strikethrough) {
        emit_line_decorations(style, p, end, m, scale, line_advance, slot, metrics, fill_key, &glyph_bias);
    }
    draw_run(style->font);
}

void nt_text_renderer_draw(const nt_text_style_t *style, const float model[16], const char *utf8) { nt_text_renderer_draw_n(style, model, utf8, utf8 ? strlen(utf8) : 0U); }
// #endregion

// #region Test accessors
#ifdef NT_TEST_ACCESS
uint32_t nt_text_renderer_test_font_switches(void) { return s_text.test_font_switches; }
void nt_text_renderer_test_reset_call_counters(void) {
    s_text.test_font_switches = 0;
    s_text.test_prev_font = (nt_font_t){0};
    s_text.test_draw_n_calls = 0;
    s_text.test_max_oblique = 0.0F;
    s_text.test_max_weight = 0.0F;
    s_text.test_max_outline_w = 0.0F;
    s_text.test_saw_underline = false;
}
const float *nt_text_renderer_test_last_model(void) { return s_text.test_last_model; }
uint32_t nt_text_renderer_test_draw_n_calls(void) { return s_text.test_draw_n_calls; }
float nt_text_renderer_test_max_oblique(void) { return s_text.test_max_oblique; }
float nt_text_renderer_test_max_weight(void) { return s_text.test_max_weight; }
float nt_text_renderer_test_max_outline_width(void) { return s_text.test_max_outline_w; }
bool nt_text_renderer_test_saw_underline(void) { return s_text.test_saw_underline; }
uint32_t nt_text_renderer_test_material_id(void) { return s_text.current.material.id; }

uint16_t nt_text_renderer_test_pipeline_cache_count(void) { return s_text.pipeline_count; }
#endif
// #endregion
