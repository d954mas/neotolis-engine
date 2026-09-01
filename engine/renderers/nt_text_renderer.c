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
#include <string.h>

// #region Vertex format
/* 72 bytes per vertex, matching slug_text.vert contract */
typedef struct {
    float position[3];     /* 12B: world-space quad corner (full 3D) */
    float texcoord[2];     /* 8B: em-space coordinate */
    float glyph_data[4];   /* 16B: packed uint via memcpy (curve_offset, band_row, curve_offset_x, band_count) */
    float glyph_bounds[4]; /* 16B: bbox x0/y0/x1/y1 in em-space */
    float color[4];        /* 16B: RGBA float */
    float depth_bias;      /* 4B: per-glyph clip-space depth bias (subtracted from NDC z in the VS) */
} nt_text_vertex_t;
_Static_assert(sizeof(nt_text_vertex_t) == 72, "text vertex stride must be 72 bytes");
// #endregion

// #region Module state
/* Per-run sticky decoration axes bundled so reset/restore is one struct op and a newly-added axis can't
 * be forgotten in reset. Every axis's off-state is 0, so (nt_text_deco_t){0} is the correct clear.
 * Logical-state lifetime: preserved by restore_gpu, cleared by cold init/shutdown and reset_decoration. */
typedef struct {
    float weight_em;        /* synthetic-bold em weight (signed); 0 = natural */
    float outline_w;        /* outline width em beyond the fill weight; 0 = no outline */
    float outline_color[4]; /* outline pass RGBA */
    float shadow_dx;        /* hard-shadow offset em (px = dx * size), like weight/outline */
    float shadow_dy;        /* hard-shadow offset em */
    float shadow_blur;      /* stored, UNUSED (reserved for a future soft-shadow mode) */
    float shadow_color[4];  /* shadow pass RGBA; alpha 0 = no shadow */
    bool underline;         /* emit an underline sentinel quad per line */
    bool strikethrough;     /* emit a strike sentinel quad per line */
    float oblique;          /* synthetic-oblique shear folded into the model in draw_n (faux-italic lean); 0 = upright */
} nt_text_deco_t;

static struct {
    /* GPU resources */
    nt_renderer_pipeline_entry_t pipelines[NT_TEXT_RENDERER_MAX_PIPELINES];
    uint16_t pipeline_count;
    nt_buffer_t vbo;                /* dynamic vertex buffer */
    nt_buffer_t ibo;                /* immutable index buffer (pre-generated quad pattern) */
    nt_vertex_input_t vertex_input; /* slug layout baked over vbo+ibo */

    /* CPU staging buffer (compile-time arrays) */
    nt_text_vertex_t vertices[NT_TEXT_RENDERER_MAX_VERTICES];
    uint32_t vertex_count;
    uint32_t glyph_count;

    /* Current state */
    nt_material_t material;
    /* Resolved once when the batch opens: the staged glyphs belong to it, so a
     * program replaced under them cannot redirect them. Zero, or invalidated by
     * a destroyed program, means flush has nothing to draw through. */
    nt_pipeline_t batch_pipeline;
    nt_font_t font;
    /* Per-glyph clip-space NDC z bias so depth-writing glyph quads don't z-fight at overlapping AA
     * fringes; 0 = off, signed. Logical state: cleared by cold init/shutdown, preserved by restore_gpu. */
    float glyph_depth_bias;
    /* Per-run decoration axes (weight/outline/shadow/underline/strike/oblique); reset as one unit. */
    nt_text_deco_t deco;

    bool initialized;
    /* One-shot so a load-time discard does not spam; re-armed when a pipeline is
     * built, i.e. when something became drawable again. */
    bool warned_no_pipeline;

#ifdef NT_TEST_ACCESS
    /* Count every set_material / set_font entry regardless of early-out so
     * nt_debug_overlay tests can prove explicit calls. */
    uint32_t test_set_material_calls;
    uint32_t test_set_font_calls;
    /* Captures the model matrix passed to draw_n on every call, even when the
     * font has no glyph data (units_per_em == 0). Lets walker tests pin the
     * matrix construction without needing a real font fixture. */
    float test_last_model[16];
    uint32_t test_draw_n_calls;
    /* Largest oblique seen at a draw_n entry since the last counter reset. Lets emit tests pin the
     * SYNTH_ITALIC -> set_oblique wiring end-to-end even with a glyph-less stub font (draw_n early-outs
     * before emitting, but the lean value is still observed here). */
    float test_max_oblique;
    /* Same observe-at-draw_n-entry pattern for the decoration axes: emit tests (rich SYNTH_BOLD, label
     * decoration) pin that the UI front fed the setter through the walk even though the state is reset
     * right after the run (so a plain sticky-read post-walk would see 0). */
    float test_max_weight;
    float test_max_outline_w;
    bool test_saw_underline;
    bool test_saw_strike;
    /* Counts flushes that issued a draw; empty no-op flushes excluded. */
    uint32_t test_nonempty_flush_calls;
#endif
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

// #region Pipeline cache
/* The key omits depth_func and label because they are constant here; vertex
 * input is a separate owned object, not pipeline state. */
static nt_pipeline_t find_or_create_pipeline(void) {
    const nt_material_info_t *info = nt_material_get_info(s_text.material);
    const nt_program_t program = (info != NULL) ? info->program : NT_PROGRAM_INVALID;
    /* One query covers every state: no program yet, a program that died with the
     * context, and a program its owner destroyed. */
    if (!info || !nt_gfx_program_ready(program)) {
        nt_renderer_warn_program_not_ready(&s_text.warned_no_pipeline, info);
        return (nt_pipeline_t){0};
    }

    const uint64_t key = ((uint64_t)program.id * 0x9E3779B97F4A7C15ULL) + info->render_state_hash;
    const nt_pipeline_t cached = nt_renderer_pipeline_cache_find(s_text.pipelines, s_text.pipeline_count, key);
    if (cached.id != 0) {
        return cached;
    }

    /* Read render state from material — same pattern as mesh_renderer */
    const nt_pipeline_desc_t desc = {
        .program = program,
        .depth_test = info->depth_test,
        .depth_write = info->depth_write,
        .depth_func = NT_DEPTH_LEQUAL,
        .blend = info->blend,
        .cull_mode = (uint8_t)info->cull_mode,
        .label = "text_renderer",
    };
    return nt_renderer_pipeline_cache_insert(s_text.pipelines, &s_text.pipeline_count, NT_TEXT_RENDERER_MAX_PIPELINES, key, &desc, &s_text.warned_no_pipeline);
}
// #endregion

// #region Lifecycle
/* Buffer creation can fail only on a lost context; skip the vertex input
 * then (make_vertex_input traps on invalid buffer handles). Flush retries
 * this after a recoverable backend failure, like the lazy pipeline cache. */
static void create_vertex_input(void) {
    if (s_text.vbo.id == 0 || s_text.ibo.id == 0) {
        return;
    }
    /* Slug vertex layout: 6 attributes, stride = 72 bytes */
    s_text.vertex_input = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){
        .layout =
            {
                .attr_count = 6,
                .stride = 72,
                .attrs =
                    {
                        {.location = 0, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 0},  /* a_position */
                        {.location = 1, .type = NT_VERTEX_FLOAT, .count = 2, .offset = 12}, /* a_texcoord */
                        {.location = 2, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 20}, /* a_glyph_data */
                        {.location = 3, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 36}, /* a_glyph_bounds */
                        {.location = 4, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 52}, /* a_color */
                        {.location = 5, .type = NT_VERTEX_FLOAT, .count = 1, .offset = 68}, /* a_depth_bias */
                    },
            },
        .vertex_buffer = s_text.vbo,
        .index_buffer = s_text.ibo,
        .label = "text_vi",
    });
}

/* GPU resources only — pipelines are built lazily on a cache miss, so this owns just the buffers +
 * index pattern. Must not touch s_text's logical fields; restore_gpu rebuilds these without wiping them. */
static void create_gpu_resources(void) {
    generate_quad_indices();
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
        .index_type = NT_INDEX_UINT16,
        .label = "text_ibo",
    });
    create_vertex_input();
}

static void destroy_gpu_resources(void) {
    for (uint16_t i = 0; i < s_text.pipeline_count; i++) {
        nt_gfx_destroy_pipeline(s_text.pipelines[i].pipeline);
        s_text.pipelines[i] = (nt_renderer_pipeline_entry_t){0};
    }
    s_text.pipeline_count = 0;
    s_text.batch_pipeline = (nt_pipeline_t){0}; /* the cache it pointed into is gone */
    nt_gfx_destroy_vertex_input(s_text.vertex_input);
    nt_gfx_destroy_buffer(s_text.vbo);
    nt_gfx_destroy_buffer(s_text.ibo);
    s_text.vertex_input = NT_VERTEX_INPUT_INVALID;
    s_text.vbo = (nt_buffer_t){0};
    s_text.ibo = (nt_buffer_t){0};
}

void nt_text_renderer_init(void) {
    NT_ASSERT(!s_text.initialized);
    memset(&s_text, 0, sizeof(s_text)); /* cold start: clear everything, GPU + logical */

    /* Pre-flush hook so font-cache evictions flush our staging while texture offsets are still valid.
     * Safe when staging is empty (flush early-returns on glyph_count == 0). */
    nt_font_set_pre_flush_callback(nt_text_renderer_flush);

    create_gpu_resources();
    s_text.initialized = true;
}

void nt_text_renderer_shutdown(void) {
    if (!s_text.initialized) {
        return;
    }
    destroy_gpu_resources();
    nt_font_set_pre_flush_callback(NULL);
    memset(&s_text, 0, sizeof(s_text));
}

nt_result_t nt_text_renderer_restore_gpu(void) {
    if (!s_text.initialized) {
        return NT_OK;
    }
    /* Context-loss recovery: rebuild ONLY GPU resources. Logical state (material, font,
     * glyph_depth_bias, oblique, sticky decoration) is left untouched, so it survives by default — no
     * save/restore list to forget when a field is added. */
    destroy_gpu_resources();
    create_gpu_resources();
    s_text.vertex_count = 0; /* in-flight staging is dropped across context loss */
    s_text.glyph_count = 0;  /* The first quad of the next batch resolves a new pipeline. */
    if (s_text.vbo.id == 0 || s_text.ibo.id == 0) {
        /* Restore contract: failure releases partial GPU resources. */
        destroy_gpu_resources();
        return NT_ERR_INIT_FAILED;
    }
    /* The vertex input is not part of the verdict: flush retries it lazily. */
    return NT_OK;
}
// #endregion

// #region State setters
void nt_text_renderer_set_material(nt_material_t mat) {
    NT_ASSERT(s_text.initialized);
#ifdef NT_TEST_ACCESS
    s_text.test_set_material_calls++;
#endif
    /* Validate BEFORE same-handle early return: stale handle (destroyed material,
     * bumped generation) must assert even if the id still matches what we cached. */
    const nt_material_info_t *info = nt_material_get_info(mat);
    NT_ASSERT(info != NULL && "nt_text_renderer_set_material: invalid material handle");
    /* Assignment, not liveness: on the frame the context dies the program is
     * already dead here, and trapping on that would crash a recoverable event.
     * make_pipeline polls the lost context and hands back an invalid pipeline. */
    NT_ASSERT(info->program.id != 0 && "nt_text_renderer_set_material: material has no program");

    if (s_text.material.id == mat.id) {
        return;
    }

    if (s_text.glyph_count > 0) {
        nt_text_renderer_flush();
    }

    s_text.material = mat;
}

void nt_text_renderer_set_font(nt_font_t font) {
    NT_ASSERT(s_text.initialized);
#ifdef NT_TEST_ACCESS
    s_text.test_set_font_calls++;
#endif
    if (s_text.font.id == font.id) {
        return;
    }

    /* Auto-flush on font change */
    if (s_text.glyph_count > 0) {
        nt_text_renderer_flush();
    }

    s_text.font = font;
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

static void emit_quad(const nt_glyph_cache_entry_t *g, const float model[16], float scale, float pen_x, float pen_y, const float color[4], uint8_t band_count, float glyph_bias) {
    if (s_text.glyph_count >= NT_TEXT_RENDERER_MAX_GLYPHS) {
        nt_text_renderer_flush();
    }
    /* Glyph lookup may have flushed staging through the font cache callback. */
    if (s_text.glyph_count == 0) {
        s_text.batch_pipeline = (s_text.material.id != 0) ? find_or_create_pipeline() : (nt_pipeline_t){0};
    }

    /* 0.5 px screen-space dilation ("Decade of Slug" improvement): oversize
     * the quad so fragments sample em-coords just past bbox where coverage
     * falls cleanly to 0. Assumes 1 world unit = 1 screen pixel (nt_ui ortho). */
    const float dilate_px = 0.5F;
    const float dilate_em = dilate_px / scale;

    float x0 = pen_x + ((float)g->bbox_x0 * scale) - dilate_px;
    float y0 = pen_y + ((float)g->bbox_y0 * scale) - dilate_px;
    float x1 = pen_x + ((float)g->bbox_x1 * scale) + dilate_px;
    float y1 = pen_y + ((float)g->bbox_y1 * scale) + dilate_px;

    float em_x0 = (float)g->bbox_x0 - dilate_em;
    float em_y0 = (float)g->bbox_y0 - dilate_em;
    float em_x1 = (float)g->bbox_x1 + dilate_em;
    float em_y1 = (float)g->bbox_y1 + dilate_em;

    /* Pack glyph data as uint bit patterns */
    float gd0;
    float gd1;
    float gd2;
    float gd3;
    pack_uint_as_float(&gd0, g->curve_offset);
    pack_uint_as_float(&gd1, (uint32_t)g->band_row);
    pack_uint_as_float(&gd2, g->curve_offset_x);
    pack_uint_as_float(&gd3, (uint32_t)band_count);

    /* 4 vertices per quad: BL, BR, TR, TL */
    uint32_t vi = s_text.vertex_count;
    nt_text_vertex_t *v = &s_text.vertices[vi];

    /* Shared fields — fill once in v[0], copy to v[1..3]. glyph_bounds is the
     * UNDILATED glyph bbox so the shader's band lookup stays correct. */
    v[0].glyph_data[0] = gd0;
    v[0].glyph_data[1] = gd1;
    v[0].glyph_data[2] = gd2;
    v[0].glyph_data[3] = gd3;
    v[0].glyph_bounds[0] = (float)g->bbox_x0;
    v[0].glyph_bounds[1] = (float)g->bbox_y0;
    v[0].glyph_bounds[2] = (float)g->bbox_x1;
    v[0].glyph_bounds[3] = (float)g->bbox_y1;
    memcpy(v[0].color, color, 16);
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

    s_text.vertex_count += 4;
    s_text.glyph_count++;
}
// #endregion

// #region Decoration sentinel quad
/* Sentinel "glyph" with band_count=0: slug_text.frag early-returns coverage=1, so it fills solid for
 * underline/strike. Pixel-space corners (already include pen/scale) flow through the same transform_point
 * path as glyphs — correct under any model matrix (world / 3D), no scissor/viewport hijack. */
static void emit_decoration_quad(const float model[16], float x0, float y0, float x1, float y1, const float color[4], float glyph_bias) {
    if (s_text.glyph_count >= NT_TEXT_RENDERER_MAX_GLYPHS) {
        nt_text_renderer_flush();
    }
    if (s_text.glyph_count == 0) {
        s_text.batch_pipeline = (s_text.material.id != 0) ? find_or_create_pipeline() : (nt_pipeline_t){0};
    }
    nt_text_vertex_t *v = &s_text.vertices[s_text.vertex_count];

    float band0;
    pack_uint_as_float(&band0, 0U); /* band_count=0 = decoration sentinel */
    v[0].glyph_data[0] = 0.0F;
    v[0].glyph_data[1] = 0.0F;
    v[0].glyph_data[2] = 0.0F;
    v[0].glyph_data[3] = band0;
    /* bounds/texcoord unused: the shader returns before reading them for the sentinel. */
    v[0].glyph_bounds[0] = 0.0F;
    v[0].glyph_bounds[1] = 0.0F;
    v[0].glyph_bounds[2] = 0.0F;
    v[0].glyph_bounds[3] = 0.0F;
    memcpy(v[0].color, color, 16);
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

    s_text.vertex_count += 4;
    s_text.glyph_count++;
}
// #endregion

// #region Draw
/* Pen advance (kern, tracking, newline) is identical every pass, so passes register exactly. Walked
 * once per active pass (shadow, outline, fill), grouped and never interleaved: per-glyph interleave
 * breaks premultiplied-alpha compositing when glyphs overlap. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void emit_glyph_pass(const uint8_t *p, const uint8_t *end, const float model[16], float scale, float letter_tracking, float line_advance, uint8_t band_count, nt_font_slot_t *slot,
                            int16_t key_offset, const float color[4], float off_x, float off_y, float *glyph_bias) {
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
            pen_x += letter_tracking;
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
            emit_quad(g, model, scale, pen_x + off_x, pen_y + off_y, color, band_count, *glyph_bias);
            *glyph_bias += s_text.glyph_depth_bias;
        }
        pen_x += (float)g->advance * scale;
        prev_cp = codepoint;
        had_glyph_on_line = true;
    }
}

/* One underline/strike sentinel quad per LINE (continuous per same-style segment; within one
 * draw_n the whole run is one style, so the segment boundary is the newline). Y and thickness come from
 * the scaled v5 metrics. Exact vertical sign is a visual-QA concern. */
static void emit_line_deco_quads(const float model[16], float scale, float x1, float pen_y, nt_font_metrics_t metrics, const float color[4], float *glyph_bias) {
    if (s_text.deco.underline) {
        float top = pen_y + ((float)metrics.underline_position * scale); /* underline_position = top edge, below baseline */
        float bot = pen_y + ((float)(metrics.underline_position - metrics.underline_thickness) * scale);
        emit_decoration_quad(model, 0.0F, bot, x1, top, color, *glyph_bias);
        *glyph_bias += s_text.glyph_depth_bias;
    }
    if (s_text.deco.strikethrough) {
        float bot = pen_y + ((float)metrics.strikeout_position * scale); /* strikeout_position = above baseline */
        float top = pen_y + ((float)(metrics.strikeout_position + metrics.strikeout_size) * scale);
        emit_decoration_quad(model, 0.0F, bot, x1, top, color, *glyph_bias);
        *glyph_bias += s_text.glyph_depth_bias;
    }
}

/* Walk the run once (advance only) to find each line's pixel extent, then emit its decoration quads. */
static void emit_line_decorations(const uint8_t *p, const uint8_t *end, const float model[16], float scale, float letter_tracking, float line_advance, nt_font_slot_t *slot, nt_font_metrics_t metrics,
                                  int16_t key_offset, const float color[4], float *glyph_bias) {
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
                emit_line_deco_quads(model, scale, pen_x, pen_y, metrics, color, glyph_bias);
            }
            pen_x = 0.0F;
            pen_y -= line_advance;
            prev_cp = 0;
            had_glyph_on_line = false;
            continue;
        }
        if (had_glyph_on_line) {
            pen_x += letter_tracking;
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
        emit_line_deco_quads(model, scale, pen_x, pen_y, metrics, color, glyph_bias);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_text_renderer_draw_n(const char *utf8, size_t len, const float model[16], float size, const float color[4], float letter_tracking, float line_leading) {
    NT_ASSERT(s_text.initialized);
#ifdef NT_TEST_ACCESS
    memcpy(s_text.test_last_model, model, sizeof s_text.test_last_model);
    s_text.test_draw_n_calls++;
    if (s_text.deco.oblique > s_text.test_max_oblique) {
        s_text.test_max_oblique = s_text.deco.oblique; /* observe the lean even when the stub font emits nothing */
    }
    if (s_text.deco.weight_em > s_text.test_max_weight) {
        s_text.test_max_weight = s_text.deco.weight_em;
    }
    if (s_text.deco.outline_w > s_text.test_max_outline_w) {
        s_text.test_max_outline_w = s_text.deco.outline_w;
    }
    s_text.test_saw_underline = s_text.test_saw_underline || s_text.deco.underline;
    s_text.test_saw_strike = s_text.test_saw_strike || s_text.deco.strikethrough;
#endif
    if (len == 0U || utf8 == NULL) {
        return;
    }
    NT_ASSERT(s_text.font.id != 0 && "nt_text_renderer_draw_n: call set_font before draw");

    nt_font_metrics_t metrics = nt_font_get_metrics(s_text.font);
    if (metrics.units_per_em == 0) {
        return; /* no resource loaded yet (or all unmounted) */
    }
    if (!nt_gfx_texture_ready(nt_font_get_curve_texture(s_text.font)) || !nt_gfx_texture_ready(nt_font_get_band_texture(s_text.font))) {
        return;
    }
    float scale = size / (float)metrics.units_per_em;
    uint8_t band_count = nt_font_get_band_count(s_text.font);

    nt_font_slot_t *slot = nt_font_get_slot(s_text.font);
    NT_ASSERT(slot != NULL);

    /* Fold synthetic-oblique into the model once (constant for the whole call): col0/col1 of the already
     * Y-flipped model ARE the text-local axes, so out.col1 += oblique*col0 leans x by oblique*y about the
     * baseline — reproduces a caller-baked lean for ANY caller's matrix. */
    const float *m = model;
    float oblique_model[16];
    if (s_text.deco.oblique != 0.0F) {
        memcpy(oblique_model, model, sizeof oblique_model);
        for (int r = 0; r < 4; ++r) {
            oblique_model[4 + r] += s_text.deco.oblique * model[r];
        }
        m = oblique_model;
    }

    /* Natural line advance from font metrics, plus user-supplied leading. */
    const float natural_line_advance = (metrics.line_height != 0) ? ((float)metrics.line_height * scale) : size;
    const float line_advance = natural_line_advance + line_leading;

    const uint8_t *p = (const uint8_t *)utf8;
    const uint8_t *end = p + len;

    /* Per-pass embolden cache key from the sticky weight (font units). Fill uses the weight; outline
     * grows by outline_w; shadow reuses the outermost visible variant (outline if active, else fill) so
     * it adds NO new cache entry. */
    const float upm = (float)metrics.units_per_em;
    const int16_t fill_key = nt_font_quantize_weight(s_text.deco.weight_em * upm);
    /* alpha 0 -> invisible: no outline pass, no extra cache variant, and the shadow tracks the fill silhouette (not the dilated outline). */
    const bool outline_active = (s_text.deco.outline_w > 0.0F && s_text.deco.outline_color[3] > 0.0F);
    const int16_t outline_key = (int16_t)(outline_active ? nt_font_quantize_weight((s_text.deco.weight_em + s_text.deco.outline_w) * upm) : fill_key);
    const bool shadow_active = (s_text.deco.shadow_color[3] > 0.0F);

    float glyph_bias = 0.0F; /* accumulates across ALL passes so they separate in depth-written world text */

    /* Painter order: shadow (behind) → outline → fill (top), each grouped over the whole run. A staging
     * self-flush mid-pass does NOT reorder: passes emit in global order and flush draws the FIFO prefix,
     * so no shadow/outline quad is ever drawn after a later pass's quad. */
    if (shadow_active) {
        const int16_t shadow_key = (int16_t)(outline_active ? outline_key : fill_key);
        emit_glyph_pass(p, end, m, scale, letter_tracking, line_advance, band_count, slot, shadow_key, s_text.deco.shadow_color, s_text.deco.shadow_dx * size, s_text.deco.shadow_dy * size,
                        &glyph_bias);
    }
    if (outline_active) {
        emit_glyph_pass(p, end, m, scale, letter_tracking, line_advance, band_count, slot, outline_key, s_text.deco.outline_color, 0.0F, 0.0F, &glyph_bias);
    }
    emit_glyph_pass(p, end, m, scale, letter_tracking, line_advance, band_count, slot, fill_key, color, 0.0F, 0.0F, &glyph_bias);

    /* Underline/strike sentinel quads last (on top of fill), one continuous quad per line. */
    if (s_text.deco.underline || s_text.deco.strikethrough) {
        emit_line_decorations(p, end, m, scale, letter_tracking, line_advance, slot, metrics, fill_key, color, &glyph_bias);
    }
}

void nt_text_renderer_set_glyph_depth_bias(float bias_per_glyph) {
    NT_ASSERT(s_text.initialized);
    NT_ASSERT(isfinite(bias_per_glyph) && "nt_text_renderer_set_glyph_depth_bias: bias must be finite");
    s_text.glyph_depth_bias = bias_per_glyph; /* signed: subtracted from NDC z in the VS */
}

void nt_text_renderer_set_oblique(float shear) {
    NT_ASSERT(s_text.initialized);
    NT_ASSERT(isfinite(shear) && "nt_text_renderer_set_oblique: shear must be finite");
    s_text.deco.oblique = shear; /* folded into the model in draw_n; no flush -- CPU-baked per vertex, mixes in a batch */
}

/* HARD isfinite guards (real if, not NT_ASSERT) — NT_ASSERT is a no-op in shipping and a NaN would
 * poison offset_points / quantize where NaN != 0.0F. */
void nt_text_renderer_set_weight(float weight_em) {
    NT_ASSERT(s_text.initialized);
    if (!isfinite(weight_em)) {
        NT_ASSERT(0 && "nt_text_renderer_set_weight: weight must be finite");
        return;
    }
    s_text.deco.weight_em = weight_em; /* no clamp (locked decision); folded into the fill key_offset */
}

void nt_text_renderer_set_outline(float width, const float color[4]) {
    NT_ASSERT(s_text.initialized);
    NT_ASSERT(color != NULL);
    if (!isfinite(width) || !isfinite(color[0]) || !isfinite(color[1]) || !isfinite(color[2]) || !isfinite(color[3])) {
        NT_ASSERT(0 && "nt_text_renderer_set_outline: width/color must be finite");
        return;
    }
    s_text.deco.outline_w = width;
    memcpy(s_text.deco.outline_color, color, sizeof s_text.deco.outline_color);
}

void nt_text_renderer_set_shadow(float dx, float dy, float blur, const float color[4]) {
    NT_ASSERT(s_text.initialized);
    NT_ASSERT(color != NULL);
    if (!isfinite(dx) || !isfinite(dy) || !isfinite(blur) || !isfinite(color[0]) || !isfinite(color[1]) || !isfinite(color[2]) || !isfinite(color[3])) {
        NT_ASSERT(0 && "nt_text_renderer_set_shadow: offset/blur/color must be finite");
        return;
    }
    s_text.deco.shadow_dx = dx;
    s_text.deco.shadow_dy = dy;
    s_text.deco.shadow_blur = blur; /* stored, UNUSED (reserved for a future soft-shadow mode) */
    memcpy(s_text.deco.shadow_color, color, sizeof s_text.deco.shadow_color);
}

void nt_text_renderer_set_underline(bool enabled) {
    NT_ASSERT(s_text.initialized);
    s_text.deco.underline = enabled;
}

void nt_text_renderer_set_strikethrough(bool enabled) {
    NT_ASSERT(s_text.initialized);
    s_text.deco.strikethrough = enabled;
}

void nt_text_renderer_reset_decoration(void) {
    NT_ASSERT(s_text.initialized);
    s_text.deco = (nt_text_deco_t){0}; /* one struct clear — every axis's off-state is 0, so a newly-added axis can't leak */
}

void nt_text_renderer_draw(const char *utf8, const float model[16], float size, const float color[4], float letter_tracking, float line_leading) {
    nt_text_renderer_draw_n(utf8, utf8 ? strlen(utf8) : 0U, model, size, color, letter_tracking, line_leading);
}
// #endregion

// #region Flush
void nt_text_renderer_flush(void) {
    if (s_text.glyph_count == 0) {
        return;
    }
    /* A recoverable creation failure (backend VAO alloc) must not disable
     * text until the next restore -- retry lazily, like the pipeline cache. */
    if (!nt_gfx_vertex_input_valid(s_text.vertex_input)) {
        create_vertex_input();
    }
    /* Resolved when the batch opened. Re-checked rather than trusted: destroying
     * a program destroys the pipelines built on it, and that can happen between
     * the first glyph and here. The vertex input stays invalid when the retry
     * above failed (context still lost / backend failure). */
    const nt_pipeline_t pipeline = s_text.batch_pipeline;
    if (!nt_gfx_pipeline_valid(pipeline) || !nt_gfx_vertex_input_valid(s_text.vertex_input)) {
        /* Unready programs were reported at batch open; destruction of a captured
         * pipeline or backend allocation failure still needs a warning. */
        if (!s_text.warned_no_pipeline) {
            NT_LOG_WARN("nt_text_renderer_flush: no usable pipeline or vertex input -- discarding %u glyphs", s_text.glyph_count);
            s_text.warned_no_pipeline = true;
        }
        s_text.vertex_count = 0;
        s_text.glyph_count = 0;
        s_text.batch_pipeline = (nt_pipeline_t){0};
        return;
    }

    /* Upload staging buffer to GPU. Orphan-style upload (glBufferData with
     * GL_DYNAMIC_DRAW) so the driver allocates fresh storage and avoids
     * stalling on the previous frame's draw of the same VBO. */
    nt_gfx_orphan_buffer(s_text.vbo, s_text.vertices, s_text.vertex_count * (uint32_t)sizeof(nt_text_vertex_t));

    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_bind_vertex_input(s_text.vertex_input);

    /* Bind font textures */
    if (s_text.font.id != 0) {
        nt_gfx_bind_texture(nt_font_get_curve_texture(s_text.font), 0);
        nt_gfx_bind_texture(nt_font_get_band_texture(s_text.font), 1);
        nt_gfx_set_uniform_int("u_curve_texture", 0);
        nt_gfx_set_uniform_int("u_band_texture", 1);
        nt_gfx_set_uniform_int("u_curve_tex_width", (int)nt_font_get_curve_texture_width(s_text.font));
    }

    /* Re-read material vec4 params every flush (e.g. u_alpha_cutoff) — same contract sprite/mesh
     * renderers honor; without this the shader sees uninitialized uniforms. */
    if (s_text.material.id != 0) {
        const nt_material_info_t *mi = nt_material_get_info(s_text.material);
        if (mi != NULL) {
            for (uint8_t p = 0; p < mi->param_count; p++) {
                if (mi->param_names[p] != NULL) {
                    nt_gfx_set_uniform_vec4(mi->param_names[p], mi->params[p]);
                }
            }
        }
    }

    /* Single draw call per flush */
    nt_gfx_draw_indexed(0, s_text.glyph_count * 6, s_text.vertex_count);
#ifdef NT_TEST_ACCESS
    s_text.test_nonempty_flush_calls++;
#endif

    /* Reset staging buffer. The next glyph opens a new batch and resolves its
     * own pipeline, so an overflow flush mid-draw cannot pin the rest of the
     * run to this one. */
    s_text.vertex_count = 0;
    s_text.glyph_count = 0;
    s_text.batch_pipeline = (nt_pipeline_t){0};
    /* Recovered: a later distinct failure warns again instead of going silent. */
    s_text.warned_no_pipeline = false;
}
// #endregion

// #region Test accessors
#ifdef NT_TEST_ACCESS
uint32_t nt_text_renderer_test_vertex_count(void) { return s_text.vertex_count; }
uint32_t nt_text_renderer_test_glyph_count(void) { return s_text.glyph_count; }
const void *nt_text_renderer_test_vertices(void) { return s_text.vertices; }
bool nt_text_renderer_test_initialized(void) { return s_text.initialized; }
uint32_t nt_text_renderer_test_set_material_calls(void) { return s_text.test_set_material_calls; }
uint32_t nt_text_renderer_test_set_font_calls(void) { return s_text.test_set_font_calls; }
void nt_text_renderer_test_reset_call_counters(void) {
    s_text.test_set_material_calls = 0;
    s_text.test_set_font_calls = 0;
    s_text.test_draw_n_calls = 0;
    s_text.test_max_oblique = 0.0F;
    s_text.test_max_weight = 0.0F;
    s_text.test_max_outline_w = 0.0F;
    s_text.test_saw_underline = false;
    s_text.test_saw_strike = false;
    s_text.test_nonempty_flush_calls = 0;
}
uint32_t nt_text_renderer_test_nonempty_flush_calls(void) { return s_text.test_nonempty_flush_calls; }
const float *nt_text_renderer_test_last_model(void) { return s_text.test_last_model; }
uint32_t nt_text_renderer_test_draw_n_calls(void) { return s_text.test_draw_n_calls; }
float nt_text_renderer_test_glyph_depth_bias(void) { return s_text.glyph_depth_bias; }
float nt_text_renderer_test_oblique(void) { return s_text.deco.oblique; }
float nt_text_renderer_test_weight(void) { return s_text.deco.weight_em; }
float nt_text_renderer_test_outline_width(void) { return s_text.deco.outline_w; }
float nt_text_renderer_test_outline_color_a(void) { return s_text.deco.outline_color[3]; }
float nt_text_renderer_test_shadow_color_a(void) { return s_text.deco.shadow_color[3]; }
float nt_text_renderer_test_shadow_dx(void) { return s_text.deco.shadow_dx; }
bool nt_text_renderer_test_underline(void) { return s_text.deco.underline; }
float nt_text_renderer_test_max_oblique(void) { return s_text.test_max_oblique; }
float nt_text_renderer_test_max_weight(void) { return s_text.test_max_weight; }
float nt_text_renderer_test_max_outline_width(void) { return s_text.test_max_outline_w; }
bool nt_text_renderer_test_saw_underline(void) { return s_text.test_saw_underline; }
bool nt_text_renderer_test_saw_strike(void) { return s_text.test_saw_strike; }
uint32_t nt_text_renderer_test_material_id(void) { return s_text.material.id; }

uint16_t nt_text_renderer_test_pipeline_cache_count(void) { return s_text.pipeline_count; }
#endif
// #endregion
