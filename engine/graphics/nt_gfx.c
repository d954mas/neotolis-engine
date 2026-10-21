#include "graphics/nt_gfx_frame.h"
#include "graphics/nt_gfx_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "basisu/nt_basisu_transcoder.h"
#include "core/nt_assert.h"
#include "core/nt_builtins.h"
#include "hash/nt_hash.h"
#include "log/nt_log.h"
#include "meshwire/nt_meshwire.h"
#include "nt_mesh_format.h"
#include "nt_shader_format.h"
#include "nt_texture_format.h"

// #region pipeline identity
/* ---- Pipeline identity ----
 * Exact identity of a desc, label excluded: equal keys <=> identical baked state.
 * `bits` packs every enum/bool lane; the float payloads ride along as bit patterns. */
typedef struct {
    uint64_t bits;
    uint32_t float_bits[6]; /* blend.constant_color, polygon_offset_factor, polygon_offset_units */
} nt_gfx_pipeline_key_t;

/* Lane widths. The packer asserts each input fits: an out-of-range value must not
 * truncate onto a valid neighbour and turn into a hit on another desc's pipeline. */
_Static_assert(NT_BLEND_SRC_ALPHA_SATURATE < 16, "blend factor lane is 4 bits");
_Static_assert(NT_BLEND_OP_MAX < 8, "blend op lane is 3 bits");
_Static_assert(NT_DEPTH_NEVER < 8, "depth func lane is 3 bits");
/* Partial tripwire for the packer: catches a desc field that moves a later offset or the
 * size. A field that fits an existing padding hole moves nothing -- review by hand. */
_Static_assert(offsetof(nt_pipeline_desc_t, depth_func) == 8 && offsetof(nt_pipeline_desc_t, cull_mode) == 12 && offsetof(nt_pipeline_desc_t, blend) == 16 &&
                   offsetof(nt_pipeline_desc_t, polygon_offset) == 40 && offsetof(nt_pipeline_desc_t, polygon_offset_factor) == 44 && offsetof(nt_pipeline_desc_t, polygon_offset_units) == 48 &&
                   offsetof(nt_pipeline_desc_t, label) == (sizeof(void *) == 8 ? 56 : 52) && sizeof(nt_pipeline_desc_t) == (sizeof(void *) == 8 ? 64 : 56),
               "nt_pipeline_desc_t changed -- update pipeline_key");

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_ASSERT expansion inflates the metric
static nt_gfx_pipeline_key_t pipeline_key(const nt_pipeline_desc_t *desc) {
    NT_ASSERT(desc->cull_mode <= 2 && "cull_mode out of range");
    NT_ASSERT((uint32_t)desc->depth_func <= NT_DEPTH_NEVER && "depth_func out of range");
    NT_ASSERT(desc->blend.src_rgb <= NT_BLEND_SRC_ALPHA_SATURATE && desc->blend.dst_rgb <= NT_BLEND_SRC_ALPHA_SATURATE && desc->blend.src_alpha <= NT_BLEND_SRC_ALPHA_SATURATE &&
              desc->blend.dst_alpha <= NT_BLEND_SRC_ALPHA_SATURATE && "blend factor out of range");
    NT_ASSERT(desc->blend.op_rgb <= NT_BLEND_OP_MAX && desc->blend.op_alpha <= NT_BLEND_OP_MAX && "blend op out of range");
    /* Identity only: a disabled blend is opaque whatever its (valid) factors say. */
    const nt_blend_state_t blend = desc->blend.enabled ? desc->blend : nt_blend_opaque();
    nt_gfx_pipeline_key_t key;
    key.bits = (uint64_t)desc->program.id | (uint64_t)(desc->depth_test ? 1U : 0U) << 32 | (uint64_t)(desc->depth_write ? 1U : 0U) << 33 | (uint64_t)(desc->polygon_offset ? 1U : 0U) << 34 |
               (uint64_t)(blend.enabled ? 1U : 0U) << 35 | (uint64_t)desc->depth_func << 36 | (uint64_t)desc->cull_mode << 39 | (uint64_t)blend.src_rgb << 41 | (uint64_t)blend.dst_rgb << 45 |
               (uint64_t)blend.src_alpha << 49 | (uint64_t)blend.dst_alpha << 53 | (uint64_t)blend.op_rgb << 57 | (uint64_t)blend.op_alpha << 60;
    /* Bit patterns: exact, so -0.0 vs 0.0 can only over-split, never alias. Disabled offset packs as zero. */
    const float float_bits[6] = {blend.constant_color[0],
                                 blend.constant_color[1],
                                 blend.constant_color[2],
                                 blend.constant_color[3],
                                 desc->polygon_offset ? desc->polygon_offset_factor : 0.0F,
                                 desc->polygon_offset ? desc->polygon_offset_units : 0.0F};
    memcpy(key.float_bits, float_bits, sizeof(key.float_bits));
    return key;
}

/* `bits` first: a miss costs one word, the floats are read only on a match. */
static bool pipeline_key_equal(const nt_gfx_pipeline_key_t *a, const nt_gfx_pipeline_key_t *b) { return a->bits == b->bits && memcmp(a->float_bits, b->float_bits, sizeof(a->float_bits)) == 0; }
// #endregion

/* Activation staging shared by mesh decode (VBO then IBO) and the transcoded
 * Basis chain: grows on demand, freed after NT_STAGE_IDLE_FRAMES idle -- no
 * per-activation heap traffic while a pack streams in. */
#define NT_STAGE_IDLE_FRAMES 60 /* ~1s at 60fps */
static uint8_t *s_stage_buf = NULL;
static uint32_t s_stage_size = 0;
static uint32_t s_stage_idle = 0;

#define NT_GFX_MESH_VERTEX_INPUTS (NT_GFX_MAX_VERTEX_STREAMS + 1) /* per mesh: each frame vertex stream, then the base */

static uint8_t *stage_acquire(uint32_t size) {
    if (size > s_stage_size) {
        free(s_stage_buf);
        s_stage_buf = (uint8_t *)malloc(size);
        if (!s_stage_buf) {
            s_stage_size = 0;
            return NULL;
        }
        s_stage_size = size;
    }
    s_stage_idle = 0;
    return s_stage_buf;
}

static void age_stage_buffer(void) {
    if (s_stage_buf != NULL) {
        s_stage_idle++;
        if (s_stage_idle > NT_STAGE_IDLE_FRAMES) {
            free(s_stage_buf);
            s_stage_buf = NULL;
            s_stage_size = 0;
        }
    }
}

/* Lazy transcoder initialization (one-time, at first v2 texture activation) */
static bool s_transcoder_initialized = false;

/* ---- Buffer metadata (minimal info kept for runtime validation) ---- */

typedef struct {
    uint8_t type;         /* nt_buffer_type_t */
    uint8_t usage;        /* nt_buffer_usage_t */
    uint8_t index_type;   /* 0=none, 1=uint16, 2=uint32 */
    uint8_t frame_stream; /* frame storage stream + 1 (its `used` bounds instance reads); 0 = not frame storage */
    bool live;            /* a backend object on the current context; false = free slot or husk */
    uint32_t size;        /* capacity */
} nt_gfx_buffer_meta_t;

/* ---- Vertex input metadata (destroy cascade + draw-invariant checks) ---- */

typedef struct {
    uint32_t vbo_id; /* full buffer handles: destroy_buffer cascades on exact match */
    uint32_t ibo_id;
    uint16_t vertex_mask;    /* vertex attribute locations; an instance layout may not overlap them */
    uint8_t index_type;      /* captured from the IBO; NT_INDEX_NONE for non-indexed */
    uint8_t instance_layout; /* layout of its last instanced bind; 0 = never bound with instances */
} nt_gfx_vertex_input_meta_t;

/* ---- Instance layout records (CPU only; survive context loss) ---- */

typedef struct {
    nt_vertex_attr_t attrs[NT_GFX_MAX_VERTEX_ATTRS];
    uint32_t extent; /* bytes one instance reads: max attr offset + attr size (may exceed the stride) */
    uint16_t stride;
    uint16_t mask; /* attribute locations */
    uint8_t attr_count;
} nt_gfx_instance_layout_meta_t;

typedef struct {
    uint32_t texture; /* recorded backend names; 0 = nothing recorded in this pass */
    uint32_t sampler;
} nt_gfx_unit_binding_t;

/* A staged range of the uniform frame stream. */
typedef struct {
    uint32_t offset;
    uint32_t size;
} nt_gfx_block_binding_t;

/* ---- Texture metadata (format + dimensions for update_texture validation) ---- */

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t format;    /* nt_texture_format_t */
    uint8_t mip_count; /* 1 = base only, >1 = has mip chain */
    bool live;         /* a backend object on the current context; false = free slot or husk */
    /* Sampler NT_SAMPLER_DEFAULT resolves to. Always non-zero for a live
     * texture: make_texture rejects the texture when the sampler cannot be
     * created. Survives a context loss: sampler ids stay valid across it. */
    nt_sampler_t default_sampler;
} nt_gfx_texture_meta_t;

/* A program's GPU object; NONE is a free slot or a husk a loss left. */
typedef enum {
    NT_GFX_PROGRAM_NONE,
    NT_GFX_PROGRAM_LINKING,
    NT_GFX_PROGRAM_READY,
} nt_gfx_program_state_t;

typedef struct {
    /* Full borrowed texture handles: destroy_texture cascades on exact match. INVALID when absent. */
    nt_texture_t attachments[NT_GFX_RT_ATTACHMENTS];
} nt_gfx_render_target_meta_t;

/* ---- Global state ---- */

nt_gfx_t g_nt_gfx;
/* Diagnostic history survives the runtime state's init/shutdown resets. */
nt_gfx_device_info_t g_nt_gfx_device_info;

const nt_gfx_device_info_t *nt_gfx_device_info(void) { return &g_nt_gfx_device_info; }

static void record_caps(const nt_gfx_gpu_caps_t *caps) {
    (void)snprintf(g_nt_gfx_device_info.caps, sizeof(g_nt_gfx_device_info.caps),
                   "astc=%d bc7=%d etc2=%d float_rt=%d float_linear=%d parallel_compile=%d max_texture=%u ubo_align=%u vary=%u vs_uni=%u fs_uni=%u vs_ubo=%u fs_ubo=%u ubo_size=%u",
                   (int)caps->has_astc, (int)caps->has_bc7, (int)caps->has_etc2, (int)caps->has_float_render_target, (int)caps->has_float_texture_linear, (int)caps->has_parallel_shader_compile,
                   (unsigned)caps->max_texture_size, (unsigned)caps->uniform_buffer_offset_alignment, (unsigned)caps->max_varying_components, (unsigned)caps->max_vertex_uniform_components,
                   (unsigned)caps->max_fragment_uniform_components, (unsigned)caps->max_vertex_uniform_blocks, (unsigned)caps->max_fragment_uniform_blocks, (unsigned)caps->max_uniform_block_size);
}

/* ---- File-scope internal state ---- */

/* Sampler cache entry — packed key, original desc, and current backend handle.
 * Lifetime = engine. desc is preserved across context loss so backend can be
 * lazily recreated without invalidating material-side sampler.id handles. */
typedef struct {
    uint32_t key;           /* hash of (min, mag, wrap_u, wrap_v, compare) */
    uint32_t backend;       /* GL sampler object handle; 0 after context loss */
    nt_sampler_desc_t desc; /* recorded for context-loss recreate */
} nt_gfx_sampler_entry_t;

static struct {
    nt_pool_t shader_pool;
    nt_pool_t program_pool;
    nt_pool_t pipeline_pool;
    nt_pool_t vertex_input_pool;
    nt_pool_t buffer_pool;
    nt_pool_t texture_pool;
    nt_pool_t render_target_pool;

    uint32_t *shader_backends;            /* backend handles parallel to pool slots */
    uint8_t *program_states;              /* nt_gfx_program_state_t per slot */
    nt_gfx_pipeline_key_t *pipeline_keys; /* exact desc identity per live slot; all-zero when free */

    nt_gfx_buffer_meta_t *buffer_metas;   /* minimal buffer metadata for runtime validation */
    nt_gfx_texture_meta_t *texture_metas; /* format + dimensions for update_texture validation */
    nt_gfx_vertex_input_meta_t *vertex_input_metas;
    nt_gfx_render_target_meta_t *render_target_metas;

    /* Sampler cache (dedup by descriptor; samplers are immutable after create
     * and shared across materials/textures). nt_sampler_t.id is 1+slot. */
    nt_gfx_sampler_entry_t sampler_cache[NT_GFX_MAX_SAMPLERS];
    uint32_t sampler_count;

    nt_pool_t mesh_pool;
    nt_gfx_mesh_info_t *mesh_table; /* [capacity+1], index 0 reserved */
    /* Per mesh slot: one vertex input per frame vertex stream, then the base one for any other
     * buffer; made on first use. [capacity+1][NT_GFX_MESH_VERTEX_INPUTS] */
    nt_vertex_input_t *mesh_vertex_inputs;

    /* A layout handle is its slot, 1-based; init and shutdown empty the table. */
    nt_gfx_instance_layout_meta_t instance_layouts[NT_GFX_MAX_INSTANCE_LAYOUTS + 1];
    uint32_t instance_layout_count;

    nt_gfx_render_state_t render_state;
    uint32_t active_render_target;
    uint32_t bound_pipeline;     /* full handle of the bound pipeline, 0 = none */
    uint32_t bound_vertex_input; /* full handle of the bound vertex input, 0 = none */
    /* Last fused bind on the bound vertex input; layout 0 = plain bind. A vertex input change
     * clears the layout; the buffer stays the pass's last validated one (the frame rule keeps it alive). */
    uint32_t bound_instance_layout;
    uint32_t bound_instance_buffer;
    uint32_t bound_instance_offset;
    uint8_t bound_index_type; /* from the bound vertex input; NT_INDEX_NONE = non-indexed or none bound */
    bool texture_set_applied; /* a texture set covers the bound pipeline's program */
    bool scissor_enabled;     /* GL_SCISSOR_TEST as recorded */
    /* Binding mirrors: an equal bind records nothing. Each lives as long as the
     * contract keeps its state. */
    nt_gfx_unit_binding_t bound_units[NT_GFX_MAX_TEXTURE_SLOTS];
    nt_gfx_block_binding_t staged_blocks[NT_GFX_MAX_UNIFORM_BUFFER_SLOTS]; /* the last block each slot staged in the frame */
    uint32_t bound_blocks;                                                 /* slot bits bound in this pass, each to its staged block */
    bool blocks_checked;                                                   /* the bound program's blocks were checked since the last change */
    int scissor_rect[4];                                                   /* GL bottom-left x,y,w,h; carries over passes; w = -1: unknown */
    int viewport_rect[4];                                                  /* GL bottom-left x,y,w,h; w = -1: unknown */
} s_gfx;

_Static_assert(NT_GFX_MAX_TEXTURE_SLOTS <= 8, "texture unit masks are uint8_t");

static void discard_texture_set(void) { s_gfx.texture_set_applied = false; }

static const nt_gfx_texture_meta_t *render_target_attachment_meta(uint32_t rt_slot, int attachment) {
    return &s_gfx.texture_metas[nt_pool_slot_index(s_gfx.render_target_metas[rt_slot].attachments[attachment].id)];
}

/* All attachments share the target size and at least one is present. */
static const nt_gfx_texture_meta_t *render_target_size_meta(uint32_t rt_slot) {
    const bool has_color = s_gfx.render_target_metas[rt_slot].attachments[NT_GFX_RT_COLOR].id != 0;
    return render_target_attachment_meta(rt_slot, has_color ? NT_GFX_RT_COLOR : NT_GFX_RT_DEPTH);
}

/* ---- Lifecycle ---- */

/* A backend failure means a lost context: the backend asserted a live one after asking the
 * browser. The loss event may still be queued, so the first failure latches it at once and every
 * later call takes its lost path; only that first failure logs. */
static nt_gfx_result_t latch_loss(void) {
    if (!g_nt_gfx.context_lost) {
        g_nt_gfx.context_lost = true;
        NT_LOG_ERROR("WebGL context lost");
    }
    return NT_GFX_RESULT_CONTEXT_LOST;
}

/* True while the program has a GPU object (linking or ready). */
static bool program_has_gpu(uint32_t slot) { return s_gfx.program_states[slot] != NT_GFX_PROGRAM_NONE; }

/* The full program handle a pipeline slot borrows: its key packs it into the low 32 bits. */
static uint32_t pipeline_program(uint32_t slot) { return (uint32_t)s_gfx.pipeline_keys[slot].bits; }

// #region frame observation
#if NT_GFX_CAPTURE_ENABLED
nt_gfx_capture_state_t g_nt_gfx_capture;

const char *nt_gfx_gl_call_name(uint32_t call) {
#define NT_GFX_GL_CALL_NAME_(name) #name,
    static const char *const names[NT_GFX_GL_COUNT] = {NULL, NT_GFX_GL_CALLS(NT_GFX_GL_CALL_NAME_)};
#undef NT_GFX_GL_CALL_NAME_
    return call < NT_GFX_GL_COUNT ? names[call] : NULL;
}

void nt_gfx_capture_request(void) {
    NT_ASSERT(g_nt_gfx_capture.capacity > 0);
    g_nt_gfx_capture.request_pending = true;
}

nt_gfx_capture_view_t nt_gfx_capture_read(void) {
    nt_gfx_capture_view_t result = g_nt_gfx_capture.view;
    result.events = result.count > 0 ? g_nt_gfx_capture.events : NULL;
    return result;
}

static uint32_t render_target_attachment_format(uint32_t rt_slot, int attachment) {
    return s_gfx.render_target_metas[rt_slot].attachments[attachment].id != 0 ? render_target_attachment_meta(rt_slot, attachment)->format : 0;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- one record schema per owned resource kind
static void capture_resource_definition(nt_gfx_object_kind_t kind, uint32_t id) {
    uint32_t slot = nt_pool_slot_index(id);
    NT_GFX_RECORD(
        NT_GFX_EVENT_DEFINITION, NT_GFX_OP_CREATE, event->object_kind = kind; event->object = id; switch (kind) {
            case NT_GFX_OBJECT_SHADER:
                event->data.resource.backend = s_gfx.shader_backends[slot];
                event->result = NT_GFX_RESULT_UNKNOWN;
                break;
            case NT_GFX_OBJECT_PROGRAM:
                event->data.resource.backend = program_has_gpu(slot) ? slot : 0;
                event->result = NT_GFX_RESULT_UNKNOWN;
                break;
            case NT_GFX_OBJECT_PIPELINE:
                event->data.resource.backend = slot;
                event->data.resource.related[0] = pipeline_program(slot);
                break;
            case NT_GFX_OBJECT_VERTEX_INPUT:
                event->data.resource.backend = slot;
                event->data.resource.related[0] = s_gfx.vertex_input_metas[slot].vbo_id;
                event->data.resource.related[1] = s_gfx.vertex_input_metas[slot].ibo_id;
                event->data.resource.type = s_gfx.vertex_input_metas[slot].index_type;
                event->result = NT_GFX_RESULT_UNKNOWN;
                break;
            case NT_GFX_OBJECT_BUFFER:
                event->data.resource.backend = s_gfx.buffer_metas[slot].live ? slot : 0;
                event->data.resource.size = s_gfx.buffer_metas[slot].size;
                event->data.resource.type = s_gfx.buffer_metas[slot].type;
                event->data.resource.usage = s_gfx.buffer_metas[slot].usage;
                event->data.resource.format = s_gfx.buffer_metas[slot].index_type;
                break;
            case NT_GFX_OBJECT_TEXTURE:
                event->data.resource.backend = s_gfx.texture_metas[slot].live ? slot : 0;
                event->data.resource.width = s_gfx.texture_metas[slot].width;
                event->data.resource.height = s_gfx.texture_metas[slot].height;
                event->data.resource.format = s_gfx.texture_metas[slot].format;
                event->data.resource.levels = s_gfx.texture_metas[slot].mip_count;
                event->data.resource.related[0] = s_gfx.texture_metas[slot].default_sampler.id;
                break;
            case NT_GFX_OBJECT_SAMPLER:
                event->data.resource.backend = s_gfx.sampler_cache[id - 1].backend;
                event->data.resource.flags = s_gfx.sampler_cache[id - 1].key;
                break;
            case NT_GFX_OBJECT_RENDER_TARGET:
                event->data.resource.backend = slot;
                event->data.resource.related[0] = s_gfx.render_target_metas[slot].attachments[NT_GFX_RT_COLOR].id;
                event->data.resource.related[1] = s_gfx.render_target_metas[slot].attachments[NT_GFX_RT_DEPTH].id;
                event->data.resource.width = render_target_size_meta(slot)->width;
                event->data.resource.height = render_target_size_meta(slot)->height;
                event->data.resource.format = render_target_attachment_format(slot, NT_GFX_RT_COLOR);
                event->data.resource.usage = render_target_attachment_format(slot, NT_GFX_RT_DEPTH);
                break;
            case NT_GFX_OBJECT_INSTANCE_LAYOUT:
                event->data.resource.backend = slot;
                event->data.resource.size = s_gfx.instance_layouts[slot].stride;
                event->data.resource.flags = s_gfx.instance_layouts[slot].mask;
                break;
            case NT_GFX_OBJECT_NONE:
                break;
        });
}
#define NT_GFX_DEFINE_RESOURCE(kind, id) capture_resource_definition(kind, id)

static void capture_attributes(nt_gfx_object_kind_t kind, uint32_t id, const nt_vertex_attr_t *attrs, uint8_t attr_count, uint16_t stride, uint32_t buffer, uint32_t divisor) {
    for (uint32_t a = 0; a < attr_count; a++) {
        const nt_vertex_attr_t *attr = &attrs[a];
        NT_GFX_RECORD(NT_GFX_EVENT_DEFINITION, NT_GFX_OP_ATTRIBUTE, event->object_kind = kind; event->object = id; event->data.attribute.buffer = buffer; event->data.attribute.offset = attr->offset;
                      event->data.attribute.stride = stride; event->data.attribute.location = attr->location; event->data.attribute.type = (uint32_t)attr->type;
                      event->data.attribute.count = attr->count; event->data.attribute.normalized = attr->normalized; event->data.attribute.divisor = divisor;);
    }
}

static void capture_instance_layout(uint32_t id) {
    const nt_gfx_instance_layout_meta_t *meta = &s_gfx.instance_layouts[id];
    capture_resource_definition(NT_GFX_OBJECT_INSTANCE_LAYOUT, id);
    capture_attributes(NT_GFX_OBJECT_INSTANCE_LAYOUT, id, meta->attrs, meta->attr_count, meta->stride, 0, 1);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- diagnostic record macros expand at owning sites
static void capture_initial_state(void) {
    /* Pass-scoped mirrors are discarded by the first begin_pass, so only frame-crossing state is recorded. */
    NT_GFX_RECORD(NT_GFX_EVENT_INITIAL, NT_GFX_OP_STATE, event->detail = NT_GFX_INITIAL_FRONTEND; event->data.state.integers[0] = g_nt_gfx.context_lost;);
    NT_GFX_RECORD(NT_GFX_EVENT_INITIAL, NT_GFX_OP_SCISSOR_ENABLE, event->data.state.integers[0] = s_gfx.scissor_enabled);
    /* The carried-over scissor box explains an equal set_scissor that ends CACHE in the capture. */
    if (s_gfx.scissor_rect[2] < 0) {
        NT_GFX_RECORD(NT_GFX_EVENT_INITIAL, NT_GFX_OP_SCISSOR, event->result = NT_GFX_RESULT_UNKNOWN);
    } else {
        NT_GFX_RECORD(NT_GFX_EVENT_INITIAL, NT_GFX_OP_SCISSOR, for (uint32_t i = 0; i < 4; i++) { event->data.state.integers[i] = (uint32_t)s_gfx.scissor_rect[i]; });
    }
    const nt_pool_t *pools[] = {&s_gfx.shader_pool, &s_gfx.program_pool, &s_gfx.pipeline_pool, &s_gfx.vertex_input_pool, &s_gfx.buffer_pool, &s_gfx.texture_pool, &s_gfx.render_target_pool};
    const nt_gfx_object_kind_t kinds[] = {NT_GFX_OBJECT_SHADER, NT_GFX_OBJECT_PROGRAM, NT_GFX_OBJECT_PIPELINE,     NT_GFX_OBJECT_VERTEX_INPUT,
                                          NT_GFX_OBJECT_BUFFER, NT_GFX_OBJECT_TEXTURE, NT_GFX_OBJECT_RENDER_TARGET};
    for (uint32_t p = 0; p < sizeof(pools) / sizeof(pools[0]); p++) {
        for (uint32_t i = 1; i <= pools[p]->capacity; i++) {
            if (nt_pool_slot_alive(pools[p], i)) {
                capture_resource_definition(kinds[p], pools[p]->slots[i].id);
            }
        }
    }
    for (uint32_t i = 1; i <= s_gfx.sampler_count; i++) {
        capture_resource_definition(NT_GFX_OBJECT_SAMPLER, i);
    }
    for (uint32_t i = 1; i <= s_gfx.instance_layout_count; i++) {
        capture_instance_layout(i);
    }
    nt_gfx_backend_capture_initial_state();
}
#else
#define NT_GFX_DEFINE_RESOURCE(kind, id) ((void)0)
#endif

/* The next frame starts at once: gfx work is always inside a frame between init and shutdown. */
static void open_frame(void) { g_nt_gfx.counters = (nt_gfx_counters_t){.frame_sequence = g_nt_gfx.counters.frame_sequence + 1}; }

// #endregion

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_gfx_init(const nt_gfx_desc_t *desc) {
    NT_ASSERT(desc);
    NT_ASSERT(desc->max_shaders > 0 && "nt_gfx_desc_t.max_shaders is 0 -- use nt_gfx_desc_defaults() or set explicitly");
    NT_ASSERT(desc->max_programs > 0 && "nt_gfx_desc_t.max_programs is 0 -- use nt_gfx_desc_defaults() or set explicitly");
    NT_ASSERT(desc->max_pipelines > 0 && "nt_gfx_desc_t.max_pipelines is 0 -- use nt_gfx_desc_defaults() or set explicitly");
    NT_ASSERT(desc->max_buffers > 0 && "nt_gfx_desc_t.max_buffers is 0 -- use nt_gfx_desc_defaults() or set explicitly");
    NT_ASSERT(desc->max_textures > 0 && "nt_gfx_desc_t.max_textures is 0 -- use nt_gfx_desc_defaults() or set explicitly");
    NT_ASSERT(desc->max_meshes > 0 && "nt_gfx_desc_t.max_meshes is 0 -- use nt_gfx_desc_defaults() or set explicitly");
    NT_ASSERT(desc->max_vertex_inputs > 0 && "nt_gfx_desc_t.max_vertex_inputs is 0 -- use nt_gfx_desc_defaults() or set explicitly");
    NT_ASSERT(desc->max_render_targets > 0 && "nt_gfx_desc_t.max_render_targets is 0 -- use nt_gfx_desc_defaults() or set explicitly");
    for (uint32_t i = 0; i < NT_GFX_MAX_GAME_BLOCKS && desc->game_blocks[i] != NULL; i++) {
        /* Looked up in the list before entry i, an earlier equal name or a reserved one answers first. */
        NT_ASSERT(nt_gfx_block_slot(desc->game_blocks, desc->game_blocks[i]) == NT_GFX_SLOT_GAME + i && "nt_gfx_desc_t.game_blocks: a reserved or duplicate name");
    }
    uint16_t max_render_targets = desc->max_render_targets;
    memset(&s_gfx, 0, sizeof(s_gfx));
    s_gfx.scissor_rect[2] = -1;
    s_gfx.viewport_rect[2] = -1;
    memset(&g_nt_gfx, 0, sizeof(g_nt_gfx));
#if NT_GFX_CAPTURE_ENABLED
    memset(&g_nt_gfx_capture, 0, sizeof(g_nt_gfx_capture));
    NT_ASSERT(desc->capture_capacity == 0 || sizeof(nt_gfx_event_t) <= SIZE_MAX / desc->capture_capacity);
    g_nt_gfx_capture.capacity = desc->capture_capacity;
    if (desc->capture_capacity > 0) {
        g_nt_gfx_capture.events = (nt_gfx_event_t *)malloc((size_t)desc->capture_capacity * sizeof(nt_gfx_event_t));
        NT_ASSERT(g_nt_gfx_capture.events != NULL);
    }
#endif
    nt_gfx_frame_init(desc);
    /* Init work, like everything after it, belongs to the first frame. */
    open_frame();

    nt_pool_init(&s_gfx.shader_pool, desc->max_shaders);
    nt_pool_init(&s_gfx.program_pool, desc->max_programs);
    nt_pool_init(&s_gfx.pipeline_pool, desc->max_pipelines);
    nt_pool_init(&s_gfx.vertex_input_pool, desc->max_vertex_inputs);
    nt_pool_init(&s_gfx.buffer_pool, desc->max_buffers);
    nt_pool_init(&s_gfx.texture_pool, desc->max_textures);
    nt_pool_init(&s_gfx.render_target_pool, max_render_targets);

    s_gfx.shader_backends = (uint32_t *)calloc(desc->max_shaders + 1, sizeof(uint32_t));
    s_gfx.program_states = (uint8_t *)calloc(desc->max_programs + 1, sizeof(uint8_t));
    s_gfx.pipeline_keys = (nt_gfx_pipeline_key_t *)calloc(desc->max_pipelines + 1, sizeof(nt_gfx_pipeline_key_t));

    s_gfx.buffer_metas = (nt_gfx_buffer_meta_t *)calloc(desc->max_buffers + 1, sizeof(nt_gfx_buffer_meta_t));
    s_gfx.vertex_input_metas = (nt_gfx_vertex_input_meta_t *)calloc(desc->max_vertex_inputs + 1, sizeof(nt_gfx_vertex_input_meta_t));
    s_gfx.texture_metas = (nt_gfx_texture_meta_t *)calloc(desc->max_textures + 1, sizeof(nt_gfx_texture_meta_t));
    s_gfx.render_target_metas = (nt_gfx_render_target_meta_t *)calloc(max_render_targets + 1, sizeof(nt_gfx_render_target_meta_t));

    /* The init frame only loads: passes wait for the host's begin_frame. */
    s_gfx.render_state = NT_GFX_STATE_ENDED;

    /* Mesh pool + data table */
    nt_pool_init(&s_gfx.mesh_pool, desc->max_meshes);
    s_gfx.mesh_table = (nt_gfx_mesh_info_t *)calloc((size_t)desc->max_meshes + 1, sizeof(nt_gfx_mesh_info_t));
    s_gfx.mesh_vertex_inputs = (nt_vertex_input_t *)calloc(((size_t)desc->max_meshes + 1) * NT_GFX_MESH_VERTEX_INPUTS, sizeof(nt_vertex_input_t));
    /* Init-time OOM on a few KB of tables is not a state a game can recover from. */
    NT_ASSERT(s_gfx.shader_backends && s_gfx.program_states && s_gfx.pipeline_keys && s_gfx.buffer_metas && s_gfx.vertex_input_metas && s_gfx.texture_metas && s_gfx.render_target_metas &&
              s_gfx.mesh_table && s_gfx.mesh_vertex_inputs && "gfx init: out of memory");

    nt_gfx_backend_init(desc);

    /* Detect GPU compressed texture capabilities */
    g_nt_gfx.gpu_caps = nt_gfx_gl_ctx_detect_gpu_caps();
    /* A probe on a lost context reports zero caps: latch it, and the restore probes again. */
    if (nt_gfx_backend_query_context_lost()) {
        (void)latch_loss();
    } else {
        record_caps(&g_nt_gfx.gpu_caps);
    }

    g_nt_gfx.initialized = true;
    nt_gfx_frame_create_buffers();
}

void nt_gfx_shutdown(void) {
    nt_gfx_frame_shutdown();
    /* The open frame is discarded unpublished; teardown calls must not record into the freed array. */
#if NT_GFX_CAPTURE_ENABLED
    g_nt_gfx_capture.recording = false;
    free(g_nt_gfx_capture.events);
#endif
    /* Delete GL objects before backend teardown, which destroys the WebGL context. */

    /* Framebuffers only: the texture loop below releases their attachments. */
    for (uint32_t i = 1; i <= s_gfx.render_target_pool.capacity; i++) {
        if (nt_pool_slot_alive(&s_gfx.render_target_pool, i)) {
            nt_gfx_backend_destroy_render_target(i);
        }
    }

    /* Release cached sampler backends */
    for (uint32_t i = 0; i < s_gfx.sampler_count; i++) {
        if (s_gfx.sampler_cache[i].backend != 0) {
            nt_gfx_backend_destroy_sampler(s_gfx.sampler_cache[i].backend);
        }
    }

    /* The native context survives backend teardown, so release remaining GL objects.
     * Buffer slots include mesh storage. */
    for (uint32_t i = 1; i <= s_gfx.vertex_input_pool.capacity; i++) {
        if (nt_pool_slot_alive(&s_gfx.vertex_input_pool, i)) {
            nt_gfx_backend_destroy_vertex_input(i);
        }
    }
    for (uint32_t i = 1; i <= s_gfx.pipeline_pool.capacity; i++) {
        if (nt_pool_slot_alive(&s_gfx.pipeline_pool, i)) {
            nt_gfx_backend_destroy_pipeline(i);
        }
    }
    for (uint32_t i = 1; i <= s_gfx.program_pool.capacity; i++) {
        if (program_has_gpu(i)) {
            nt_gfx_backend_destroy_program(i);
        }
    }
    for (uint32_t i = 1; i <= s_gfx.shader_pool.capacity; i++) {
        nt_gfx_backend_destroy_shader(s_gfx.shader_backends[i]);
    }
    for (uint32_t i = 1; i <= s_gfx.buffer_pool.capacity; i++) {
        if (s_gfx.buffer_metas[i].live) {
            nt_gfx_backend_destroy_buffer(i);
        }
    }
    for (uint32_t i = 1; i <= s_gfx.texture_pool.capacity; i++) {
        if (s_gfx.texture_metas[i].live) {
            nt_gfx_backend_destroy_texture(i);
        }
    }

    nt_gfx_backend_shutdown();

    nt_pool_shutdown(&s_gfx.shader_pool);
    nt_pool_shutdown(&s_gfx.program_pool);
    nt_pool_shutdown(&s_gfx.pipeline_pool);
    nt_pool_shutdown(&s_gfx.vertex_input_pool);
    nt_pool_shutdown(&s_gfx.buffer_pool);
    nt_pool_shutdown(&s_gfx.texture_pool);
    nt_pool_shutdown(&s_gfx.render_target_pool);
    nt_pool_shutdown(&s_gfx.mesh_pool);

    free(s_gfx.shader_backends);
    free(s_gfx.program_states);
    free(s_gfx.pipeline_keys);
    free(s_gfx.buffer_metas);
    free(s_gfx.vertex_input_metas);
    free(s_gfx.texture_metas);
    free(s_gfx.render_target_metas);
    free(s_gfx.mesh_table);
    free(s_gfx.mesh_vertex_inputs);
    free(s_stage_buf);
    s_stage_buf = NULL;
    s_stage_size = 0;

    memset(&s_gfx, 0, sizeof(s_gfx));
    memset(&g_nt_gfx, 0, sizeof(g_nt_gfx));

#if NT_GFX_CAPTURE_ENABLED
    memset(&g_nt_gfx_capture, 0, sizeof(g_nt_gfx_capture));
#endif
}

const nt_gfx_gpu_caps_t *nt_gfx_gpu_caps(void) { return &g_nt_gfx.gpu_caps; }

static bool texture_filter_uses_linear(nt_texture_filter_t filter) { return filter != NT_FILTER_NEAREST && filter != NT_FILTER_NEAREST_MIPMAP_NEAREST; }

/* Frame rule: recorded commands name live objects until nt_gfx_end_frame executes them. Destroys of
 * stale or invalid handles keep their own contracts. */
#define NT_GFX_ASSERT_FRAME_RULE(pool, id)                                                                                                                                                             \
    NT_ASSERT((s_gfx.render_state < NT_GFX_STATE_PASS || !nt_pool_valid((pool), (id))) && "frame rule: destroy live objects before the first nt_gfx_begin_pass or after nt_gfx_end_frame")

static nt_gfx_result_t destroy_texture(nt_texture_t tex) {
    if (tex.id == 0) {
        return NT_GFX_RESULT_INVALID_HANDLE; /* invalid-zero is a first-class value, as for buffers */
    }
    if (!nt_pool_valid(&s_gfx.texture_pool, tex.id)) {
        NT_LOG_ERROR("destroy_texture: invalid handle");
        return NT_GFX_RESULT_INVALID_HANDLE;
    }
    /* Dependent render targets baked this texture into their framebuffer and can
     * never draw correctly again -- reclaim them now. */
    for (uint32_t i = 1; i <= s_gfx.render_target_pool.capacity; i++) {
        const nt_texture_t *attachments = s_gfx.render_target_metas[i].attachments;
        if (attachments[NT_GFX_RT_COLOR].id == tex.id || attachments[NT_GFX_RT_DEPTH].id == tex.id) {
            nt_gfx_destroy_render_target((nt_render_target_t){s_gfx.render_target_pool.slots[i].id});
        }
    }
    uint32_t slot = nt_pool_slot_index(tex.id);
    if (s_gfx.texture_metas[slot].live) {
        nt_gfx_backend_destroy_texture(slot);
    }
    memset(&s_gfx.texture_metas[slot], 0, sizeof(nt_gfx_texture_meta_t));
    nt_pool_free(&s_gfx.texture_pool, tex.id);
    return NT_GFX_RESULT_ACCEPTED;
}

/* ---- Frame / Pass ---- */

/* A link finishes no earlier than the frame after make_program, so a program never becomes ready inside a frame. */
static void finish_program_links(void) {
    for (uint32_t i = 1; i <= s_gfx.program_pool.capacity; i++) {
        if (s_gfx.program_states[i] != NT_GFX_PROGRAM_LINKING) {
            continue;
        }
        nt_gfx_result_t result = nt_gfx_backend_finish_program(i, g_nt_gfx.gpu_caps.has_parallel_shader_compile);
        if (result == NT_GFX_RESULT_UNREADY) {
            continue;
        }
        /* The backend asserts a live link failure: CONTEXT_LOST is the only other outcome. */
        if (result == NT_GFX_RESULT_CONTEXT_LOST) {
            (void)latch_loss();
            return; /* the begin_frame that takes the loss wipes every pending link */
        }
        NT_ASSERT(result == NT_GFX_RESULT_ACCEPTED && "finish_program: unexpected backend result");
        s_gfx.program_states[i] = NT_GFX_PROGRAM_READY;
        NT_GFX_DEFINE_RESOURCE(NT_GFX_OBJECT_PROGRAM, s_gfx.program_pool.slots[i].id);
    }
}

/* Every backend name died with the context. */
static void wipe_backend_handles(void) {
    for (uint32_t i = 1; i <= s_gfx.shader_pool.capacity; i++) {
        s_gfx.shader_backends[i] = 0;
    }
    for (uint32_t i = 1; i <= s_gfx.program_pool.capacity; i++) {
        s_gfx.program_states[i] = NT_GFX_PROGRAM_NONE;
    }
    /* Owners remake baked handles, so loss frees them; primaries stay husks until owners destroy them.
     * Pipelines free high to low: reuse starts at the low slots make_pipeline scans first. */
    for (uint32_t i = s_gfx.pipeline_pool.capacity; i >= 1; i--) {
        if (nt_pool_slot_alive(&s_gfx.pipeline_pool, i)) {
            nt_pool_free(&s_gfx.pipeline_pool, s_gfx.pipeline_pool.slots[i].id);
        }
        s_gfx.pipeline_keys[i] = (nt_gfx_pipeline_key_t){0};
    }
    for (uint32_t i = 1; i <= s_gfx.vertex_input_pool.capacity; i++) {
        if (nt_pool_slot_alive(&s_gfx.vertex_input_pool, i)) {
            nt_pool_free(&s_gfx.vertex_input_pool, s_gfx.vertex_input_pool.slots[i].id);
        }
        memset(&s_gfx.vertex_input_metas[i], 0, sizeof(nt_gfx_vertex_input_meta_t));
    }
    for (uint32_t i = 1; i <= s_gfx.buffer_pool.capacity; i++) {
        s_gfx.buffer_metas[i].live = false;
    }
    for (uint32_t i = 1; i <= s_gfx.texture_pool.capacity; i++) {
        s_gfx.texture_metas[i].live = false;
    }
    for (uint32_t i = 1; i <= s_gfx.render_target_pool.capacity; i++) {
        if (nt_pool_slot_alive(&s_gfx.render_target_pool, i)) {
            nt_pool_free(&s_gfx.render_target_pool, s_gfx.render_target_pool.slots[i].id);
        }
        memset(&s_gfx.render_target_metas[i], 0, sizeof(nt_gfx_render_target_meta_t));
    }
    /* Mesh table: keep entries active. nt_resource_invalidate() will
     * call deactivate_mesh() which returns slots to mesh pool.
     * destroying a husk buffer skips the backend. */
    /* A new context starts with an unknown scissor box; pass-scoped mirrors reset at begin_pass. */
    s_gfx.scissor_rect[2] = -1;
    /* Sampler cache: zero only the backend ids so material-stored sampler.id slot references
     * stay valid; the backend is lazily recreated on the next nt_gfx_make_sampler hit or bind_texture. */
    for (uint32_t i = 0; i < s_gfx.sampler_count; i++) {
        s_gfx.sampler_cache[i].backend = 0;
    }
    /* Timer-segment GL queries are dead too — let backend re-allocate
     * on next begin_segment via the lazy-find-or-alloc path. */
    nt_gfx_backend_drop_timer_segments();
}

/* The whole restore is one operation; its backend calls sit inside it. */
static nt_gfx_result_t restore_context(void) {
    /* A failed recreate leaves no context, so the query keeps reporting lost for good. */
    if (!nt_gfx_backend_recreate_all_resources()) {
        NT_LOG_ERROR("context restore failed");
        return NT_GFX_RESULT_CONTEXT_LOST;
    }
    /* getExtension enables the float color attachments the game's render targets may need. */
    const nt_gfx_gpu_caps_t caps = nt_gfx_gl_ctx_detect_gpu_caps();
    /* Restoring onto a new loss would publish dead names and the zero caps a lost context reports; stay lost and retry later. */
    if (nt_gfx_backend_query_context_lost()) {
        return NT_GFX_RESULT_CONTEXT_LOST;
    }
    g_nt_gfx.gpu_caps = caps;
    record_caps(&caps);
    g_nt_gfx.context_lost = false;
    s_gfx.scissor_enabled = false;
    g_nt_gfx.context_restored = true;
    NT_LOG_INFO("WebGL context restored -- game must re-create resources");
    return NT_GFX_RESULT_ACCEPTED;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- diagnostic record and assert macros expand at owning sites
void nt_gfx_begin_frame(void) {
    NT_ASSERT(g_nt_gfx.initialized);
    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_ENDED && "begin_frame: the open frame has no nt_gfx_end_frame");
    s_gfx.render_state = NT_GFX_STATE_IDLE;
    age_stage_buffer();
    nt_gfx_frame_begin();
    memset(s_gfx.staged_blocks, 0, sizeof(s_gfx.staged_blocks));
    g_nt_gfx.last_frame = g_nt_gfx.counters;
#if NT_GFX_CAPTURE_ENABLED
    if (g_nt_gfx_capture.recording) {
        g_nt_gfx_capture.view.counters = g_nt_gfx.last_frame;
        g_nt_gfx_capture.recording = false;
    }
#endif
    g_nt_gfx.context_restored = false;
    /* Browser loss events are separate tasks, so this sync holds for the whole iteration. The latch
     * is always taken: a loss and restore between two iterations must still wipe the tables. */
    if (nt_gfx_backend_take_context_loss()) {
        wipe_backend_handles();
        if (!g_nt_gfx.context_lost) {
            g_nt_gfx.context_lost = true;
            NT_LOG_ERROR("WebGL context lost");
        }
    }
    open_frame();
#if NT_GFX_CAPTURE_ENABLED
    /* Opened after the wipe and before the restore: the snapshot shows the tables the frame starts from. */
    if (g_nt_gfx_capture.request_pending) {
        g_nt_gfx_capture.request_pending = false;
        g_nt_gfx_capture.recording = true;
        g_nt_gfx_capture.view = (nt_gfx_capture_view_t){0};
        capture_initial_state();
    }
#endif
    if (g_nt_gfx.context_lost && !nt_gfx_backend_query_context_lost()) {
        NT_GFX_BEGIN(NT_GFX_OP_CONTEXT, NT_GFX_OBJECT_NONE, 0);
        NT_GFX_END(restore_context());
    }
    /* After the CONTEXT operation: the remake is ordinary buffer work of the frame. */
    if (g_nt_gfx.context_restored) {
        nt_gfx_frame_create_buffers();
    }
    if (!g_nt_gfx.context_lost) {
        finish_program_links();
    }
#if NT_GFX_GPU_TIMING_ENABLED
    if (!g_nt_gfx.context_lost) {
        NT_GFX_BEGIN(NT_GFX_OP_TIMER_DISJOINT, NT_GFX_OBJECT_NONE, 0);
        nt_gfx_backend_check_timer_disjoint();
        NT_GFX_END(NT_GFX_RESULT_ACCEPTED);
    }
#endif
}

void nt_gfx_end_frame(void) {
    NT_ASSERT(g_nt_gfx.initialized);
    NT_ASSERT((s_gfx.render_state == NT_GFX_STATE_IDLE || s_gfx.render_state == NT_GFX_STATE_DRAWN) && "end_frame: needs an open frame with no open pass");
    nt_gfx_frame_execute();
    s_gfx.render_state = NT_GFX_STATE_ENDED;
}

/* Cap-checked rgba8 readback + single Y-flip to top-left. A bad size or a short buffer is a caller
 * bug; the devapi capture validates the request it takes from outside. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_ASSERT expansion inflates the metric
static nt_gfx_result_t read_pixels(nt_render_target_t src, int x, int y, int w, int h, uint8_t *out, uint32_t out_cap) {
    NT_ASSERT(w > 0 && h > 0 && "read_pixels: empty rect");
    NT_ASSERT(out != NULL);
    /* In uint64_t so w*h*4 cannot overflow before the comparison. */
    NT_ASSERT((uint64_t)(uint32_t)w * (uint64_t)(uint32_t)h * 4U <= (uint64_t)out_cap && "read_pixels: out_cap is smaller than w*h*4");
    /* A lost context returns uninitialized garbage as a "successful" read — every other GL wrapper
       early-returns on this. The capture producer treats false as failure -> NULL -> capture_failed.
       It also frees every render target, so it comes before the source checks. */
    if (g_nt_gfx.context_lost) {
        return NT_GFX_RESULT_CONTEXT_LOST;
    }
    uint32_t rt_slot = 0;
    if (src.id != 0) {
        NT_ASSERT(nt_pool_valid(&s_gfx.render_target_pool, src.id) && "read_pixels: invalid render target");
        rt_slot = nt_pool_slot_index(src.id);
        /* WebGL reads RGBA/UNSIGNED_BYTE only from a normalized fixed-point color buffer. */
        NT_ASSERT(s_gfx.render_target_metas[rt_slot].attachments[NT_GFX_RT_COLOR].id != 0 && render_target_attachment_meta(rt_slot, NT_GFX_RT_COLOR)->format == NT_TEXTURE_FORMAT_RGBA8 &&
                  "read_pixels: the source needs an RGBA8 color attachment");
        /* w and h are positive here; 64-bit sums cannot overflow. */
        NT_ASSERT(x >= 0 && y >= 0 && (int64_t)x + w <= render_target_size_meta(rt_slot)->width && (int64_t)y + h <= render_target_size_meta(rt_slot)->height &&
                  "read_pixels: rect outside the render target");
    }
    /* WebGL reports a loss once through glGetError and the drain before the read eats it: ask the browser too. */
    if (!nt_gfx_backend_read_pixels(rt_slot, x, y, w, h, out) || nt_gfx_backend_query_context_lost()) {
        /* The buffer is unusable: false -> capture_failed, not an encode of it. */
        return latch_loss();
    }

    /* Single in-place row swap: GL bottom-left -> top-left. Row stride = w*4. */
    size_t stride = (size_t)(uint32_t)w * 4U;
    uint8_t *top = out;
    uint8_t *bot = out + (stride * (size_t)((uint32_t)h - 1U));
    while (top < bot) {
        for (size_t i = 0; i < stride; i++) {
            uint8_t tmp = top[i];
            top[i] = bot[i];
            bot[i] = tmp;
        }
        top += stride;
        bot -= stride;
    }
    return NT_GFX_RESULT_ACCEPTED;
}

bool nt_gfx_read_pixels(nt_render_target_t src, int x, int y, int w, int h, uint8_t *out, uint32_t out_cap) {
    /* Only an ended frame has executed every draw; the window holds it until the swap. */
    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_ENDED && "read_pixels: read between nt_gfx_end_frame and nt_gfx_begin_frame");
    NT_GFX_BEGIN_REQUEST(NT_GFX_OP_READ_PIXELS, src.id != 0 ? NT_GFX_OBJECT_RENDER_TARGET : NT_GFX_OBJECT_NONE, src.id, event->data.state.integers[0] = (uint32_t)x;
                         event->data.state.integers[1] = (uint32_t)y; event->data.state.integers[2] = (uint32_t)w; event->data.state.integers[3] = (uint32_t)h);
    const nt_gfx_result_t result = read_pixels(src, x, y, w, h, out, out_cap);
    NT_GFX_END(result);
    return result == NT_GFX_RESULT_ACCEPTED;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_ASSERT expansion inflates the metric
static nt_gfx_result_t begin_pass(const nt_pass_desc_t *desc) {
    NT_ASSERT((s_gfx.render_state == NT_GFX_STATE_IDLE || s_gfx.render_state == NT_GFX_STATE_DRAWN) && "begin_pass: needs an open frame with no open pass");
    NT_ASSERT(desc != NULL);
    NT_ASSERT((desc->target.id != 0 || !desc->discard_color) && "begin_pass: discarding the window color loses the presented frame");
    /* Bound state is pass-scoped: the pass clear touches draw state. */
    s_gfx.bound_pipeline = 0;
    discard_texture_set();
    s_gfx.bound_vertex_input = 0;
    s_gfx.bound_instance_buffer = 0;
    s_gfx.bound_index_type = NT_INDEX_NONE;
    memset(s_gfx.bound_units, 0, sizeof(s_gfx.bound_units));
    s_gfx.bound_blocks = 0;
    s_gfx.viewport_rect[2] = -1; /* the backend sets the whole target at execution */
    /* A lost frame records nothing but keeps the pass order, so its sequencing and the frame rule still assert. */
    if (g_nt_gfx.context_lost) {
        s_gfx.render_state = NT_GFX_STATE_PASS;
        return NT_GFX_RESULT_CONTEXT_LOST;
    }

    uint32_t render_target_backend = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    if (desc->target.id != 0) {
        NT_ASSERT(nt_pool_valid(&s_gfx.render_target_pool, desc->target.id) && "begin_pass: invalid render target");
        render_target_backend = nt_pool_slot_index(desc->target.id);
        const nt_gfx_texture_meta_t *size = render_target_size_meta(render_target_backend);
        width = size->width;
        height = size->height;
    }

    s_gfx.render_state = NT_GFX_STATE_PASS;
    s_gfx.active_render_target = desc->target.id;
    /* Scissor is pass-scoped: every pass starts with it off. */
    if (s_gfx.scissor_enabled) {
        s_gfx.scissor_enabled = false;
        nt_gfx_frame_set_scissor_enabled(false);
    }
    nt_gfx_frame_begin_pass(desc, render_target_backend, width, height);
    return NT_GFX_RESULT_ACCEPTED;
}

void nt_gfx_begin_pass(const nt_pass_desc_t *desc) {
    NT_GFX_BEGIN_REQUEST(
        NT_GFX_OP_PASS, NT_GFX_OBJECT_NONE, 0, if (desc != NULL) {
            event->data.pass.target = desc->target.id;
            memcpy(event->data.pass.color, desc->clear_color, sizeof(event->data.pass.color));
            event->data.pass.depth = desc->clear_depth;
            event->data.pass.load_color = desc->load_color;
            event->data.pass.load_depth = desc->load_depth;
            event->data.pass.discard_color = desc->discard_color;
            event->data.pass.discard_depth = desc->discard_depth;
        });
    NT_GFX_END(begin_pass(desc));
}

static nt_gfx_result_t end_pass(void) {
    /* Closes a pass also on a lost context; an END without a recorded BEGIN is a backend no-op. */
    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_PASS);
    s_gfx.render_state = NT_GFX_STATE_DRAWN;
    s_gfx.active_render_target = 0;
    nt_gfx_frame_end_pass();
    return NT_GFX_RESULT_ACCEPTED;
}

void nt_gfx_end_pass(void) {
    NT_GFX_BEGIN(NT_GFX_OP_END_PASS, NT_GFX_OBJECT_NONE, 0);
    NT_GFX_END(end_pass());
}

static nt_gfx_result_t clear(const nt_clear_desc_t *desc) {
    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_PASS && "clear requires an open pass");
    if (g_nt_gfx.context_lost) {
        return NT_GFX_RESULT_CONTEXT_LOST;
    }
    NT_ASSERT(desc != NULL);
    if (desc->color || desc->depth) {
        nt_gfx_frame_clear(desc);
    }
    return NT_GFX_RESULT_ACCEPTED;
}

void nt_gfx_clear(const nt_clear_desc_t *desc) {
    NT_GFX_BEGIN_REQUEST(
        NT_GFX_OP_CLEAR, NT_GFX_OBJECT_RENDER_TARGET, s_gfx.active_render_target, if (desc != NULL) {
            memcpy(event->data.clear.clear_color, desc->clear_color, sizeof(event->data.clear.clear_color));
            event->data.clear.clear_depth = desc->clear_depth;
            event->data.clear.color = desc->color;
            event->data.clear.depth = desc->depth;
        });
    NT_GFX_END(clear(desc));
}

/* ---- Resource creation ---- */

static nt_gfx_result_t make_shader(const nt_shader_desc_t *desc, nt_shader_t *out) {
    NT_ASSERT(desc != NULL);

    if (g_nt_gfx.context_lost) {
        return NT_GFX_RESULT_CONTEXT_LOST;
    }
    uint32_t id = nt_pool_alloc(&s_gfx.shader_pool);
    NT_ASSERT(id != 0 && "shader pool full -- raise nt_gfx_desc_t.max_shaders");

    uint32_t backend = nt_gfx_backend_create_shader(desc);
    if (backend == 0) {
        nt_pool_free(&s_gfx.shader_pool, id);
        return latch_loss();
    }

    uint32_t slot = nt_pool_slot_index(id);
    s_gfx.shader_backends[slot] = backend;

    out->id = id;
    NT_GFX_DEFINE_RESOURCE(NT_GFX_OBJECT_SHADER, id);
    return NT_GFX_RESULT_ACCEPTED;
}

nt_shader_t nt_gfx_make_shader(const nt_shader_desc_t *desc) {
    NT_GFX_BEGIN_REQUEST(NT_GFX_OP_CREATE, NT_GFX_OBJECT_SHADER, 0, if (desc != NULL) { event->data.resource.type = (uint32_t)desc->type; });
    nt_shader_t result = {0};
    NT_GFX_END_OBJECT(make_shader(desc, &result), result.id);
    return result;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_ASSERT expansion inflates the metric
static nt_gfx_result_t make_program(nt_shader_t vs, nt_shader_t fs, const char *label, nt_program_t *out) {
    /* First: a stage the loss failed is 0, and the loss latched when it failed. */
    if (g_nt_gfx.context_lost) {
        return NT_GFX_RESULT_CONTEXT_LOST;
    }
    NT_ASSERT(nt_pool_valid(&s_gfx.shader_pool, vs.id) && "make_program: invalid vertex shader handle");
    NT_ASSERT(nt_pool_valid(&s_gfx.shader_pool, fs.id) && "make_program: invalid fragment shader handle");

    uint32_t vs_backend = s_gfx.shader_backends[nt_pool_slot_index(vs.id)];
    uint32_t fs_backend = s_gfx.shader_backends[nt_pool_slot_index(fs.id)];
    /* Rejected, not trapped: a context loss leaves stage handles live but
     * permanently unready, so this is recoverable state and not a caller error.
     * The owner recreates the stages and links again. */
    if (vs_backend == 0 || fs_backend == 0) {
        return NT_GFX_RESULT_UNREADY;
    }

    uint32_t id = nt_pool_alloc(&s_gfx.program_pool);
    NT_ASSERT(id != 0 && "program pool full -- raise nt_gfx_desc_t.max_programs");

    if (!nt_gfx_backend_create_program(vs_backend, fs_backend, nt_pool_slot_index(id), label)) {
        nt_pool_free(&s_gfx.program_pool, id);
        return latch_loss();
    }

    /* Reading link status now would block until the driver finishes; begin_frame finishes it. */
    s_gfx.program_states[nt_pool_slot_index(id)] = NT_GFX_PROGRAM_LINKING;

    out->id = id;
    NT_GFX_DEFINE_RESOURCE(NT_GFX_OBJECT_PROGRAM, id);
    return NT_GFX_RESULT_ACCEPTED;
}

nt_program_t nt_gfx_make_program(nt_shader_t vs, nt_shader_t fs, const char *label) {
    NT_GFX_BEGIN_REQUEST(NT_GFX_OP_CREATE, NT_GFX_OBJECT_PROGRAM, 0, event->data.resource.related[0] = vs.id; event->data.resource.related[1] = fs.id);
    nt_program_t result = {0};
    NT_GFX_END_OBJECT(make_program(vs, fs, label, &result), result.id);
    return result;
}

static bool blend_factor_valid(nt_blend_factor_t factor) { return factor <= NT_BLEND_SRC_ALPHA_SATURATE; }

static bool blend_factor_uses_constant_color(nt_blend_factor_t factor) { return factor == NT_BLEND_CONSTANT_COLOR || factor == NT_BLEND_ONE_MINUS_CONSTANT_COLOR; }

static bool blend_factor_uses_constant_alpha(nt_blend_factor_t factor) { return factor == NT_BLEND_CONSTANT_ALPHA || factor == NT_BLEND_ONE_MINUS_CONSTANT_ALPHA; }

static bool blend_constant_color_valid(const float color[4]) {
    for (uint8_t i = 0; i < 4; i++) {
        if (!(color[i] >= 0.0F && color[i] <= 1.0F)) {
            return false;
        }
    }
    return true;
}

/* Ranges hold whether or not blending is enabled; the WebGL combination rules
 * only matter for a blend that will run. */
static bool blend_state_valid(const nt_blend_state_t *blend) {
    if (!(blend_factor_valid(blend->src_rgb) && blend_factor_valid(blend->dst_rgb) && blend_factor_valid(blend->src_alpha) && blend_factor_valid(blend->dst_alpha) &&
          blend->op_rgb <= NT_BLEND_OP_MAX && blend->op_alpha <= NT_BLEND_OP_MAX)) {
        return false;
    }
    if (!blend->enabled) {
        return true;
    }

    bool uses_constant_color = blend_factor_uses_constant_color(blend->src_rgb) || blend_factor_uses_constant_color(blend->dst_rgb);
    bool uses_constant_alpha = blend_factor_uses_constant_alpha(blend->src_rgb) || blend_factor_uses_constant_alpha(blend->dst_rgb);
    return blend_constant_color_valid(blend->constant_color) && blend->dst_rgb != NT_BLEND_SRC_ALPHA_SATURATE && blend->dst_alpha != NT_BLEND_SRC_ALPHA_SATURATE &&
           !(uses_constant_color && uses_constant_alpha);
}

/* A location used twice (within a layout, or across a vertex input and the instance layout
 * bound with it) means glVertexAttribPointer runs twice on one slot -- last bind wins, silently
 * wrong data. Locations are asserted < NT_GFX_MAX_VERTEX_ATTRS first. */
static uint32_t layout_location_mask(const nt_vertex_layout_t *layout) {
    uint32_t mask = 0;
    for (uint8_t i = 0; i < layout->attr_count; i++) {
        const uint32_t bit = 1U << layout->attrs[i].location;
        NT_ASSERT((mask & bit) == 0 && "attribute location used twice in a layout");
        mask |= bit;
    }
    return mask;
}

/* WebGL2 raises INVALID_OPERATION when an attribute offset or the stride is not a
 * multiple of the attribute's type size; desktop GL tolerates it, so without this
 * a bad layout only fails in the browser. Off hot path (pipelines are cached). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_ASSERT expansion inflates the metric
static void assert_layout_webgl2_rules(const nt_vertex_layout_t *layout) {
    for (uint8_t i = 0; i < layout->attr_count; i++) {
        const nt_vertex_attr_t *attr = &layout->attrs[i];
        NT_ASSERT(attr->location < NT_GFX_MAX_VERTEX_ATTRS && "WebGL2 guarantees only 16 vertex attribute locations");
        NT_ASSERT(attr->count >= 1 && attr->count <= 4);
        NT_ASSERT(nt_vertex_type_size(attr->type) != 0);
        NT_ASSERT(!(attr->normalized && (attr->type == NT_VERTEX_FLOAT || attr->type == NT_VERTEX_HALF)) && "normalized is for integer types only");
        NT_ASSERT((attr->offset % nt_vertex_type_size(attr->type)) == 0 && "WebGL2: attribute offset must be a multiple of its type size");
        NT_ASSERT((layout->stride % nt_vertex_type_size(attr->type)) == 0 && "WebGL2: stride must be a multiple of each attribute's type size");
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_ASSERT expansion inflates the metric
static nt_gfx_result_t make_pipeline(const nt_pipeline_desc_t *desc, nt_pipeline_t *out) {
    /* Everything a caller controls is a developer error and traps. A lost context returns an
     * invalid handle the caller retries on a later frame; the backend create only fills a record. */
    NT_ASSERT(desc != NULL);
    /* Context loss leaves programs without a GPU object, so without this every renderer
     * would trap on the program assert below. */
    if (g_nt_gfx.context_lost) {
        return NT_GFX_RESULT_CONTEXT_LOST;
    }
    /* Equal descs share one pipeline. A hit needs no further checks: its program is live and
     * not discarded (destroy and loss clear its keys), and its blend passed them at creation.
     * A free slot's key is all zero, so the nonzero program handle keeps it from matching. */
    NT_ASSERT(desc->program.id != 0 && "make_pipeline: invalid program");
    const nt_gfx_pipeline_key_t key = pipeline_key(desc);
    for (uint32_t i = 1; i <= s_gfx.pipeline_pool.capacity; i++) {
        if (pipeline_key_equal(&s_gfx.pipeline_keys[i], &key)) {
            out->id = s_gfx.pipeline_pool.slots[i].id;
            return NT_GFX_RESULT_CACHE;
        }
    }
    NT_ASSERT(nt_pool_valid(&s_gfx.program_pool, desc->program.id) && program_has_gpu(nt_pool_slot_index(desc->program.id)) && "make_pipeline: invalid program, or one a context loss discarded");
    NT_ASSERT(blend_state_valid(&desc->blend));

    uint32_t id = nt_pool_alloc(&s_gfx.pipeline_pool);
    NT_ASSERT(id != 0 && "pipeline pool full -- raise nt_gfx_desc_t.max_pipelines");

    uint32_t slot = nt_pool_slot_index(id);
    nt_gfx_backend_create_pipeline(desc, nt_pool_slot_index(desc->program.id), slot);
    s_gfx.pipeline_keys[slot] = key;

    out->id = id;
    NT_GFX_DEFINE_RESOURCE(NT_GFX_OBJECT_PIPELINE, id);
    return NT_GFX_RESULT_ACCEPTED;
}

nt_pipeline_t nt_gfx_make_pipeline(const nt_pipeline_desc_t *desc) {
    NT_GFX_BEGIN_REQUEST(NT_GFX_OP_CREATE, NT_GFX_OBJECT_PIPELINE, 0, if (desc != NULL) { event->data.resource.related[0] = desc->program.id; });
    nt_pipeline_t result = {0};
    NT_GFX_END_OBJECT(make_pipeline(desc, &result), result.id);
    return result;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_ASSERT expansion inflates the metric
static nt_gfx_result_t make_vertex_input(const nt_vertex_input_desc_t *desc, nt_vertex_input_t *out) {
    /* Caller errors trap; only a lost context returns an invalid handle. */
    NT_ASSERT(desc != NULL);
    if (g_nt_gfx.context_lost) {
        return NT_GFX_RESULT_CONTEXT_LOST;
    }
    NT_ASSERT(desc->layout.attr_count <= NT_GFX_MAX_VERTEX_ATTRS && "too many vertex attrs");
    /* WebGL2 caps vertexAttribPointer stride at 255 bytes (INVALID_VALUE beyond).
     * Reachable only from game-declared layouts -- mesh-pack strides max out at 128. */
    NT_ASSERT(desc->layout.stride <= 255 && "WebGL2 caps vertex stride at 255 bytes");
    /* After the attr_count asserts -- the loops read attr_count entries. */
    assert_layout_webgl2_rules(&desc->layout);
    const uint32_t vertex_mask = layout_location_mask(&desc->layout);
    /* attr_count 0 with no buffers is the attribute-less gl_VertexID path. */
    NT_ASSERT((desc->layout.attr_count > 0) == (desc->vertex_buffer.id != 0) && "make_vertex_input: vertex_buffer is required iff layout has attrs");

    uint32_t vbo_backend = 0;
    if (desc->vertex_buffer.id != 0) {
        NT_ASSERT(nt_pool_valid(&s_gfx.buffer_pool, desc->vertex_buffer.id) && "make_vertex_input: invalid vertex_buffer handle");
        uint32_t vbo_slot = nt_pool_slot_index(desc->vertex_buffer.id);
        NT_ASSERT(s_gfx.buffer_metas[vbo_slot].type == NT_BUFFER_VERTEX && "make_vertex_input: vertex_buffer is not vertex type");
        vbo_backend = vbo_slot;
        /* A pool-valid buffer with no backend outlived a context loss; baking
         * it would produce a VAO that silently draws nothing. */
        NT_ASSERT(s_gfx.buffer_metas[vbo_slot].live && "make_vertex_input: vertex_buffer has no live backend -- recreate it after context restore");
    }
    uint32_t ibo_backend = 0;
    uint8_t index_type = NT_INDEX_NONE;
    if (desc->index_buffer.id != 0) {
        NT_ASSERT(nt_pool_valid(&s_gfx.buffer_pool, desc->index_buffer.id) && "make_vertex_input: invalid index_buffer handle");
        uint32_t ibo_slot = nt_pool_slot_index(desc->index_buffer.id);
        NT_ASSERT(s_gfx.buffer_metas[ibo_slot].type == NT_BUFFER_INDEX && "make_vertex_input: index_buffer is not index type");
        index_type = s_gfx.buffer_metas[ibo_slot].index_type;
        /* Without the declared type the backend would silently draw UINT16. */
        NT_ASSERT(index_type != NT_INDEX_NONE && "make_vertex_input: index_buffer was created without an index_type");
        ibo_backend = ibo_slot;
        NT_ASSERT(s_gfx.buffer_metas[ibo_slot].live && "make_vertex_input: index_buffer has no live backend -- recreate it after context restore");
    }

    uint32_t id = nt_pool_alloc(&s_gfx.vertex_input_pool);
    NT_ASSERT(id != 0 && "vertex input pool full -- raise nt_gfx_desc_t.max_vertex_inputs");

    uint32_t slot = nt_pool_slot_index(id);
    if (!nt_gfx_backend_create_vertex_input(desc, vbo_backend, ibo_backend, slot)) {
        nt_pool_free(&s_gfx.vertex_input_pool, id);
        return latch_loss();
    }

    s_gfx.vertex_input_metas[slot] = (nt_gfx_vertex_input_meta_t){
        .vbo_id = desc->vertex_buffer.id,
        .ibo_id = desc->index_buffer.id,
        .vertex_mask = (uint16_t)vertex_mask,
        .index_type = index_type,
    };

    out->id = id;
    NT_GFX_DEFINE_RESOURCE(NT_GFX_OBJECT_VERTEX_INPUT, id);
#if NT_GFX_CAPTURE_ENABLED
    capture_attributes(NT_GFX_OBJECT_VERTEX_INPUT, id, desc->layout.attrs, desc->layout.attr_count, desc->layout.stride, desc->vertex_buffer.id, 0);
#endif
    return NT_GFX_RESULT_ACCEPTED;
}

nt_vertex_input_t nt_gfx_make_vertex_input(const nt_vertex_input_desc_t *desc) {
    NT_GFX_BEGIN_REQUEST(
        NT_GFX_OP_CREATE, NT_GFX_OBJECT_VERTEX_INPUT, 0, if (desc != NULL) {
            event->data.resource.related[0] = desc->vertex_buffer.id;
            event->data.resource.related[1] = desc->index_buffer.id;
        });
    nt_vertex_input_t result = {0};
    NT_GFX_END_OBJECT(make_vertex_input(desc, &result), result.id);
    return result;
}

static bool instance_layout_equal(const nt_gfx_instance_layout_meta_t *meta, const nt_vertex_layout_t *layout) {
    if (meta->stride != layout->stride || meta->attr_count != layout->attr_count) {
        return false;
    }
    for (uint8_t i = 0; i < layout->attr_count; i++) {
        const nt_vertex_attr_t *a = &meta->attrs[i];
        const nt_vertex_attr_t *b = &layout->attrs[i];
        if (a->location != b->location || a->type != b->type || a->count != b->count || a->normalized != b->normalized || a->offset != b->offset) {
            return false;
        }
    }
    return true;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_ASSERT expansion inflates the metric
static nt_gfx_result_t make_instance_layout(const nt_vertex_layout_t *layout, nt_instance_layout_t *out) {
    /* nt_gfx_init empties the registry, so a handle made before it would name nothing. */
    NT_ASSERT(g_nt_gfx.initialized && "make_instance_layout: call after nt_gfx_init");
    NT_ASSERT(layout != NULL);
    NT_ASSERT(layout->attr_count >= 1 && layout->attr_count <= NT_GFX_MAX_VERTEX_ATTRS && "instance layout needs 1..NT_GFX_MAX_VERTEX_ATTRS attrs");
    /* GL reads stride 0 as tightly packed per attribute, which the draw range check cannot bound. */
    NT_ASSERT(layout->stride > 0 && layout->stride <= 255 && "instance layout stride must be 1..255 (WebGL2 cap)");
    assert_layout_webgl2_rules(layout);
    const uint32_t mask = layout_location_mask(layout);
    for (uint32_t i = 1; i <= s_gfx.instance_layout_count; i++) {
        if (instance_layout_equal(&s_gfx.instance_layouts[i], layout)) {
            out->id = i;
            return NT_GFX_RESULT_CACHE;
        }
    }
    NT_ASSERT(s_gfx.instance_layout_count < NT_GFX_MAX_INSTANCE_LAYOUTS && "instance layout table full -- raise NT_GFX_MAX_INSTANCE_LAYOUTS");
    const uint32_t slot = ++s_gfx.instance_layout_count;
    nt_gfx_instance_layout_meta_t *meta = &s_gfx.instance_layouts[slot];
    *meta = (nt_gfx_instance_layout_meta_t){.stride = layout->stride, .mask = (uint16_t)mask, .attr_count = layout->attr_count};
    for (uint8_t i = 0; i < layout->attr_count; i++) {
        const nt_vertex_attr_t *attr = &layout->attrs[i];
        const uint32_t end = attr->offset + (nt_vertex_type_size(attr->type) * attr->count);
        meta->attrs[i] = *attr;
        meta->extent = end > meta->extent ? end : meta->extent;
    }
    nt_gfx_backend_register_instance_layout(slot, layout);
    out->id = slot;
#if NT_GFX_CAPTURE_ENABLED
    capture_instance_layout(out->id);
#endif
    return NT_GFX_RESULT_ACCEPTED;
}

nt_instance_layout_t nt_gfx_make_instance_layout(const nt_vertex_layout_t *layout) {
    NT_GFX_BEGIN(NT_GFX_OP_CREATE, NT_GFX_OBJECT_INSTANCE_LAYOUT, 0);
    nt_instance_layout_t result = {0};
    NT_GFX_END_OBJECT(make_instance_layout(layout, &result), result.id);
    return result;
}

static nt_gfx_result_t make_buffer(const nt_buffer_desc_t *desc, nt_buffer_t *out) {
    NT_ASSERT(desc != NULL);
    /* Same recoverable contract as the other make_* creators -- without this
     * a lost-frame creation yields a pool-valid buffer with a dead GL name. */
    if (g_nt_gfx.context_lost) {
        return NT_GFX_RESULT_CONTEXT_LOST;
    }

    /* Pool exhaustion is a configuration error, not a backend allocation failure. */
    uint32_t id = nt_pool_alloc(&s_gfx.buffer_pool);
    NT_ASSERT(id != 0 && "buffer pool full -- raise nt_gfx_desc_t.max_buffers");

    uint32_t slot = nt_pool_slot_index(id);
    if (!nt_gfx_backend_create_buffer(desc, slot)) {
        nt_pool_free(&s_gfx.buffer_pool, id);
        return latch_loss();
    }

    s_gfx.buffer_metas[slot].live = true;
    s_gfx.buffer_metas[slot].type = (uint8_t)desc->type;
    s_gfx.buffer_metas[slot].usage = (uint8_t)desc->usage;
    s_gfx.buffer_metas[slot].index_type = desc->index_type;
    s_gfx.buffer_metas[slot].size = desc->size;

    out->id = id;
    NT_GFX_DEFINE_RESOURCE(NT_GFX_OBJECT_BUFFER, id);
    return NT_GFX_RESULT_ACCEPTED;
}

nt_buffer_t nt_gfx_make_buffer(const nt_buffer_desc_t *desc) {
    NT_GFX_BEGIN_REQUEST(
        NT_GFX_OP_CREATE, NT_GFX_OBJECT_BUFFER, 0, if (desc != NULL) {
            event->data.resource.size = desc->size;
            event->data.resource.type = (uint32_t)desc->type;
            event->data.resource.usage = (uint32_t)desc->usage;
            event->data.resource.format = desc->index_type;
            event->data.resource.flags = desc->data != NULL;
        });
    nt_buffer_t result = {0};
    NT_GFX_END_OBJECT(make_buffer(desc, &result), result.id);
    return result;
}

static bool texture_compressed_format_supported(nt_texture_format_t format) {
    switch (format) {
    case NT_TEXTURE_FORMAT_ETC2_RGB8:
    case NT_TEXTURE_FORMAT_ETC2_RGBA8:
        return g_nt_gfx.gpu_caps.has_etc2;
    case NT_TEXTURE_FORMAT_BC7_RGBA:
        return g_nt_gfx.gpu_caps.has_bc7;
    case NT_TEXTURE_FORMAT_ASTC_4x4_RGBA:
        return g_nt_gfx.gpu_caps.has_astc;
    default:
        return true;
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static nt_gfx_result_t make_texture(const nt_texture_desc_t *desc, nt_texture_t *out) {
    NT_ASSERT(desc != NULL);
    /* First: a lost probe reports zero caps, so the caps asserts below would fire falsely. */
    if (g_nt_gfx.context_lost) {
        return NT_GFX_RESULT_CONTEXT_LOST;
    }
    nt_texture_desc_t local_desc = *desc;

    // #region descriptor contract
    NT_ASSERT(local_desc.width > 0 && local_desc.height > 0 && "make_texture: zero dimension");
    /* Caps are the game's to check: a texture above the GPU limit is a caller bug. */
    NT_ASSERT(local_desc.width <= g_nt_gfx.gpu_caps.max_texture_size && local_desc.height <= g_nt_gfx.gpu_caps.max_texture_size && "make_texture: size exceeds gpu_caps.max_texture_size");

    NT_ASSERT(nt_texture_format_valid(local_desc.format) && "make_texture: format is required");

    /* Mipmaps require initial data — GL cannot generate from empty storage */
    NT_ASSERT((!local_desc.gen_mipmaps || local_desc.data) && "make_texture: gen_mipmaps requires data");

    if (nt_texture_format_is_compressed(local_desc.format)) {
        NT_ASSERT(local_desc.data != NULL && "make_texture: compressed format requires data");
        NT_ASSERT(!local_desc.gen_mipmaps && "make_texture: compressed format cannot generate mipmaps");
        /* WebGL BPTC requires block-aligned base dimensions, including tiny textures. */
        NT_ASSERT((local_desc.format != NT_TEXTURE_FORMAT_BC7_RGBA || (local_desc.width % 4 == 0 && local_desc.height % 4 == 0)) && "make_texture: BC7 base dimensions must be multiples of 4");
        NT_ASSERT(texture_compressed_format_supported(local_desc.format) && "make_texture: compressed format is not supported by this GPU -- check gpu_caps");
    }
    /* Ahead of the generic level rule so the depth-specific message wins. */
    if (nt_texture_format_is_depth(local_desc.format)) {
        NT_ASSERT(local_desc.data == NULL && "depth texture upload is not supported");
        NT_ASSERT(local_desc.min_filter == NT_FILTER_NEAREST && "depth texture requires NEAREST min_filter without compare mode");
        NT_ASSERT(local_desc.mag_filter == NT_FILTER_NEAREST && "depth texture requires NEAREST mag_filter without compare mode");
        NT_ASSERT(!local_desc.gen_mipmaps && "depth texture mipmaps are not supported");
        NT_ASSERT(local_desc.level_count <= 1 && "depth texture mip levels are not supported");
    }
    if (local_desc.level_count > 1) {
        NT_ASSERT(local_desc.data != NULL && "make_texture: level_count > 1 requires data");
        NT_ASSERT(!local_desc.gen_mipmaps && "make_texture: level_count > 1 cannot be combined with gen_mipmaps");
        NT_ASSERT(local_desc.level_count <= nt_texture_full_chain_levels(local_desc.width, local_desc.height) && "make_texture: level_count exceeds the full mip chain");
    }

    /* Integer textures: NEAREST only, no mipmaps */
    if (local_desc.format == NT_TEXTURE_FORMAT_RG16UI) {
        NT_ASSERT(local_desc.min_filter == NT_FILTER_NEAREST && "integer texture requires NEAREST min_filter");
        NT_ASSERT(local_desc.mag_filter == NT_FILTER_NEAREST && "integer texture requires NEAREST mag_filter");
        NT_ASSERT(!local_desc.gen_mipmaps && "integer texture does not support mipmaps");
        /* Integer storage samples NEAREST only, so extra levels are dead VRAM. */
        NT_ASSERT(local_desc.level_count <= 1 && "integer texture mip levels are not sampleable");
    }
    if (local_desc.format == NT_TEXTURE_FORMAT_RGBA16F) {
        NT_ASSERT((!local_desc.gen_mipmaps || g_nt_gfx.gpu_caps.has_float_render_target) && "RGBA16F mipmaps require float rendering support");
    }
    if (local_desc.format == NT_TEXTURE_FORMAT_RGBA32F) {
        NT_ASSERT((g_nt_gfx.gpu_caps.has_float_texture_linear || !texture_filter_uses_linear(local_desc.min_filter)) && "RGBA32F min_filter requires float texture linear support");
        NT_ASSERT((g_nt_gfx.gpu_caps.has_float_texture_linear || local_desc.mag_filter == NT_FILTER_NEAREST) && "RGBA32F mag_filter requires float texture linear support");
        /* WebGL mip generation requires both filterable and color-renderable storage. */
        NT_ASSERT((!local_desc.gen_mipmaps || (g_nt_gfx.gpu_caps.has_float_texture_linear && g_nt_gfx.gpu_caps.has_float_render_target)) &&
                  "RGBA32F mipmaps require float filtering and rendering support");
    }

    /* mag_filter: only NEAREST or LINEAR allowed */
    NT_ASSERT(local_desc.mag_filter <= NT_FILTER_LINEAR && "make_texture: mag_filter must be NEAREST or LINEAR");
    // #endregion

    /* Pool exhaustion is a configuration error, not a backend allocation failure. */
    uint32_t id = nt_pool_alloc(&s_gfx.texture_pool);
    NT_ASSERT(id != 0 && "texture pool full -- raise nt_gfx_desc_t.max_textures");

    uint32_t slot = nt_pool_slot_index(id);
    if (!nt_gfx_backend_create_texture(&local_desc, slot)) {
        nt_pool_free(&s_gfx.texture_pool, id);
        return latch_loss();
    }

    nt_sampler_desc_t sampler_desc = {
        .min_filter = local_desc.min_filter,
        .mag_filter = local_desc.mag_filter,
        .wrap_u = local_desc.wrap_u,
        .wrap_v = local_desc.wrap_v,
        .label = NULL,
    };
    nt_sampler_t default_sampler = nt_gfx_make_sampler(&sampler_desc);
    /* A loss the default sampler met has latched; a texture made on the dead context would be published live. */
    if (g_nt_gfx.context_lost) {
        nt_gfx_backend_destroy_texture(slot);
        nt_pool_free(&s_gfx.texture_pool, id);
        return NT_GFX_RESULT_CONTEXT_LOST;
    }

    s_gfx.texture_metas[slot].live = true;
    s_gfx.texture_metas[slot].width = local_desc.width;
    s_gfx.texture_metas[slot].height = local_desc.height;
    s_gfx.texture_metas[slot].format = (uint8_t)local_desc.format;
    /* Requested levels; driver upload failures are not polled. */
    uint8_t mip_count = local_desc.level_count > 1 ? local_desc.level_count : 1;
    if (local_desc.gen_mipmaps && local_desc.data) {
        mip_count = nt_texture_full_chain_levels(local_desc.width, local_desc.height);
    }
    s_gfx.texture_metas[slot].mip_count = mip_count;
    s_gfx.texture_metas[slot].default_sampler = default_sampler;

    out->id = id;
    NT_GFX_DEFINE_RESOURCE(NT_GFX_OBJECT_TEXTURE, id);
    return NT_GFX_RESULT_ACCEPTED;
}

nt_texture_t nt_gfx_make_texture(const nt_texture_desc_t *desc) {
    NT_GFX_BEGIN_REQUEST(
        NT_GFX_OP_CREATE, NT_GFX_OBJECT_TEXTURE, 0, if (desc != NULL) {
            event->data.resource.width = desc->width;
            event->data.resource.height = desc->height;
            event->data.resource.format = (uint32_t)desc->format;
            event->data.resource.levels = desc->level_count;
            event->data.resource.flags = desc->gen_mipmaps;
        });
    nt_texture_t result = {0};
    NT_GFX_END_OBJECT(make_texture(desc, &result), result.id);
    return result;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static nt_gfx_result_t make_render_target(const nt_render_target_desc_t *desc, nt_render_target_t *out) {
    NT_ASSERT(desc != NULL);
    if (g_nt_gfx.context_lost) {
        return NT_GFX_RESULT_CONTEXT_LOST;
    }
    NT_ASSERT((desc->color.id != 0 || desc->depth.id != 0) && "make_render_target: needs a color or depth attachment");

    const nt_texture_t attachments[NT_GFX_RT_ATTACHMENTS] = {desc->color, desc->depth};
    uint32_t backends[NT_GFX_RT_ATTACHMENTS] = {0};
    nt_texture_format_t formats[NT_GFX_RT_ATTACHMENTS] = {0};
    const nt_gfx_texture_meta_t *size = NULL;
    for (int i = 0; i < NT_GFX_RT_ATTACHMENTS; i++) {
        if (attachments[i].id == 0) {
            continue;
        }
        NT_ASSERT(nt_pool_valid(&s_gfx.texture_pool, attachments[i].id) && "make_render_target: invalid attachment texture handle");
        uint32_t tex_slot = nt_pool_slot_index(attachments[i].id);
        const nt_gfx_texture_meta_t *meta = &s_gfx.texture_metas[tex_slot];
        backends[i] = tex_slot;
        formats[i] = meta->format;
        /* A pool-valid texture with no backend outlived a context loss; the
         * framebuffer would attach a dead name. */
        NT_ASSERT(meta->live && "make_render_target: attachment texture has no live backend -- recreate it after context restore");
        NT_ASSERT(meta->mip_count <= 1 && "make_render_target: attachment texture must have a single level");
        NT_ASSERT((size == NULL || (meta->width == size->width && meta->height == size->height)) && "make_render_target: attachment sizes differ");
        size = meta;
    }
    NT_ASSERT((desc->color.id == 0 || nt_gfx_texture_format(desc->color) == NT_TEXTURE_FORMAT_RGBA8 || nt_gfx_texture_format(desc->color) == NT_TEXTURE_FORMAT_RGBA16F) &&
              "make_render_target: color texture must be RGBA8 or RGBA16F");
    /* After the context_lost check: a lost probe reports zero caps. The game checks the bit and picks its format. */
    NT_ASSERT((desc->color.id == 0 || nt_gfx_texture_format(desc->color) != NT_TEXTURE_FORMAT_RGBA16F || g_nt_gfx.gpu_caps.has_float_render_target) &&
              "make_render_target: an RGBA16F color attachment requires gpu_caps.has_float_render_target");
    NT_ASSERT((desc->depth.id == 0 || nt_texture_format_is_depth(nt_gfx_texture_format(desc->depth))) && "make_render_target: depth texture must be DEPTH*");

    uint32_t id = nt_pool_alloc(&s_gfx.render_target_pool);
    NT_ASSERT(id != 0 && "render target pool full; raise nt_gfx_desc_t.max_render_targets");
    uint32_t slot = nt_pool_slot_index(id);
    if (!nt_gfx_backend_create_render_target(backends, slot, desc->label, size->width, size->height, formats[NT_GFX_RT_COLOR], formats[NT_GFX_RT_DEPTH])) {
        nt_pool_free(&s_gfx.render_target_pool, id);
        return latch_loss();
    }

    s_gfx.render_target_metas[slot] = (nt_gfx_render_target_meta_t){.attachments = {desc->color, desc->depth}};
    out->id = id;
    NT_GFX_DEFINE_RESOURCE(NT_GFX_OBJECT_RENDER_TARGET, id);
    return NT_GFX_RESULT_ACCEPTED;
}

nt_render_target_t nt_gfx_make_render_target(const nt_render_target_desc_t *desc) {
    NT_GFX_BEGIN_REQUEST(
        NT_GFX_OP_CREATE, NT_GFX_OBJECT_RENDER_TARGET, 0, if (desc != NULL) {
            event->data.resource.related[0] = desc->color.id;
            event->data.resource.related[1] = desc->depth.id;
        });
    nt_render_target_t result = {0};
    NT_GFX_END_OBJECT(make_render_target(desc, &result), result.id);
    return result;
}

/* ---- Resource destruction ---- */

static nt_gfx_result_t destroy_shader(nt_shader_t shd) {
    if (shd.id == 0) {
        return NT_GFX_RESULT_INVALID_HANDLE; /* invalid-zero is a first-class value, as for programs */
    }
    if (!nt_pool_valid(&s_gfx.shader_pool, shd.id)) {
        NT_LOG_ERROR("destroy_shader: invalid handle");
        return NT_GFX_RESULT_INVALID_HANDLE;
    }
    uint32_t slot = nt_pool_slot_index(shd.id);
    nt_gfx_backend_destroy_shader(s_gfx.shader_backends[slot]);
    s_gfx.shader_backends[slot] = 0;
    nt_pool_free(&s_gfx.shader_pool, shd.id);
    return NT_GFX_RESULT_ACCEPTED;
}

void nt_gfx_destroy_shader(nt_shader_t shd) {
    NT_GFX_ASSERT_FRAME_RULE(&s_gfx.shader_pool, shd.id);
    NT_GFX_BEGIN(NT_GFX_OP_DESTROY, NT_GFX_OBJECT_SHADER, shd.id);
    NT_GFX_END(destroy_shader(shd));
}

/* Live slot only: the program cascade is the one caller. */
static nt_gfx_result_t destroy_pipeline(uint32_t slot) {
    nt_gfx_backend_destroy_pipeline(slot);
    s_gfx.pipeline_keys[slot] = (nt_gfx_pipeline_key_t){0};
    nt_pool_free(&s_gfx.pipeline_pool, s_gfx.pipeline_pool.slots[slot].id);
    return NT_GFX_RESULT_ACCEPTED;
}

static nt_gfx_result_t destroy_program(nt_program_t prog) {
    /* NT_PROGRAM_INVALID is a first-class value -- games clear their handles on
     * context loss and destroy them again at shutdown. Not an error. */
    if (prog.id == 0) {
        return NT_GFX_RESULT_INVALID_HANDLE;
    }
    /* A stale non-zero handle means the owner lost track of which programs it
     * still holds -- the one mistake this ownership model cannot absorb. */
    NT_ASSERT(nt_pool_valid(&s_gfx.program_pool, prog.id) && "destroy_program: stale handle -- clear the handle to NT_PROGRAM_INVALID when you destroy it");
    /* Dependent pipelines cannot draw again; reclaim their slots now, high to low (see the loss
     * wipe). Each records its own DESTROY; the program's frame-rule check already covers them. */
    for (uint32_t i = s_gfx.pipeline_pool.capacity; i >= 1; i--) {
        if (pipeline_program(i) != prog.id) {
            continue;
        }
        NT_GFX_BEGIN(NT_GFX_OP_DESTROY, NT_GFX_OBJECT_PIPELINE, s_gfx.pipeline_pool.slots[i].id);
        NT_GFX_END(destroy_pipeline(i));
    }
    uint32_t slot = nt_pool_slot_index(prog.id);
    if (program_has_gpu(slot)) {
        nt_gfx_backend_destroy_program(slot);
    }
    s_gfx.program_states[slot] = NT_GFX_PROGRAM_NONE;
    nt_pool_free(&s_gfx.program_pool, prog.id);
    return NT_GFX_RESULT_ACCEPTED;
}

void nt_gfx_destroy_program(nt_program_t prog) {
    NT_GFX_ASSERT_FRAME_RULE(&s_gfx.program_pool, prog.id);
    NT_GFX_BEGIN(NT_GFX_OP_DESTROY, NT_GFX_OBJECT_PROGRAM, prog.id);
    NT_GFX_END(destroy_program(prog));
}

static nt_gfx_result_t destroy_vertex_input(nt_vertex_input_t vi) {
    /* The destroy_buffer cascade makes stale handles routine here, so both
     * INVALID and stale are tolerated no-ops (same contract as pipelines). */
    if (!nt_pool_valid(&s_gfx.vertex_input_pool, vi.id)) {
        return NT_GFX_RESULT_INVALID_HANDLE;
    }
    uint32_t slot = nt_pool_slot_index(vi.id);
    nt_gfx_backend_destroy_vertex_input(slot);
    memset(&s_gfx.vertex_input_metas[slot], 0, sizeof(nt_gfx_vertex_input_meta_t));
    nt_pool_free(&s_gfx.vertex_input_pool, vi.id);
    return NT_GFX_RESULT_ACCEPTED;
}

void nt_gfx_destroy_vertex_input(nt_vertex_input_t vi) {
    NT_GFX_ASSERT_FRAME_RULE(&s_gfx.vertex_input_pool, vi.id);
    NT_GFX_BEGIN(NT_GFX_OP_DESTROY, NT_GFX_OBJECT_VERTEX_INPUT, vi.id);
    NT_GFX_END(destroy_vertex_input(vi));
}

static nt_gfx_result_t destroy_buffer(nt_buffer_t buf) {
    if (buf.id == 0) {
        return NT_GFX_RESULT_INVALID_HANDLE; /* invalid-zero is a first-class value, as for programs */
    }
    if (!nt_pool_valid(&s_gfx.buffer_pool, buf.id)) {
        NT_LOG_ERROR("destroy_buffer: invalid handle");
        return NT_GFX_RESULT_INVALID_HANDLE;
    }
    /* Dependent vertex inputs baked this buffer into their VAO state and can
     * never draw correctly again -- reclaim them now. Mesh deactivation
     * reaches no renderer, so renderer caches rely on this cascade. The backend
     * detaches the buffer from VAOs that only held instances from it. */
    for (uint32_t i = 1; i <= s_gfx.vertex_input_pool.capacity; i++) {
        const nt_gfx_vertex_input_meta_t *meta = &s_gfx.vertex_input_metas[i];
        if (meta->vbo_id == buf.id || meta->ibo_id == buf.id) {
            nt_gfx_destroy_vertex_input((nt_vertex_input_t){s_gfx.vertex_input_pool.slots[i].id});
        }
    }
    uint32_t slot = nt_pool_slot_index(buf.id);
    if (s_gfx.buffer_metas[slot].live) {
        nt_gfx_backend_destroy_buffer(slot);
    }
    memset(&s_gfx.buffer_metas[slot], 0, sizeof(nt_gfx_buffer_meta_t));
    nt_pool_free(&s_gfx.buffer_pool, buf.id);
    return NT_GFX_RESULT_ACCEPTED;
}

void nt_gfx_destroy_buffer(nt_buffer_t buf) {
    NT_GFX_ASSERT_FRAME_RULE(&s_gfx.buffer_pool, buf.id);
    NT_GFX_BEGIN(NT_GFX_OP_DESTROY, NT_GFX_OBJECT_BUFFER, buf.id);
    NT_GFX_END(destroy_buffer(buf));
}

void nt_gfx_destroy_texture(nt_texture_t tex) {
    NT_GFX_ASSERT_FRAME_RULE(&s_gfx.texture_pool, tex.id);
    NT_GFX_BEGIN(NT_GFX_OP_DESTROY, NT_GFX_OBJECT_TEXTURE, tex.id);
    NT_GFX_END(destroy_texture(tex));
}

static nt_gfx_result_t destroy_render_target(nt_render_target_t rt) {
    /* The destroy_texture cascade and context loss make stale handles routine
     * here, so both INVALID and stale are tolerated no-ops (as for vertex inputs). */
    if (!nt_pool_valid(&s_gfx.render_target_pool, rt.id)) {
        return NT_GFX_RESULT_INVALID_HANDLE;
    }
    uint32_t slot = nt_pool_slot_index(rt.id);
    nt_gfx_backend_destroy_render_target(slot);
    memset(&s_gfx.render_target_metas[slot], 0, sizeof(nt_gfx_render_target_meta_t));
    nt_pool_free(&s_gfx.render_target_pool, rt.id);
    return NT_GFX_RESULT_ACCEPTED;
}

void nt_gfx_destroy_render_target(nt_render_target_t rt) {
    NT_GFX_ASSERT_FRAME_RULE(&s_gfx.render_target_pool, rt.id);
    NT_GFX_BEGIN(NT_GFX_OP_DESTROY, NT_GFX_OBJECT_RENDER_TARGET, rt.id);
    NT_GFX_END(destroy_render_target(rt));
}

nt_texture_t nt_gfx_render_target_color(nt_render_target_t rt) {
    if (!nt_pool_valid(&s_gfx.render_target_pool, rt.id)) {
        return (nt_texture_t){0};
    }
    return s_gfx.render_target_metas[nt_pool_slot_index(rt.id)].attachments[NT_GFX_RT_COLOR];
}

bool nt_gfx_render_target_valid(nt_render_target_t rt) { return nt_pool_valid(&s_gfx.render_target_pool, rt.id); }

bool nt_gfx_shader_ready(nt_shader_t shd) {
    if (!nt_pool_valid(&s_gfx.shader_pool, shd.id)) {
        return false;
    }
    return s_gfx.shader_backends[nt_pool_slot_index(shd.id)] != 0;
}

bool nt_gfx_program_valid(nt_program_t prog) { return nt_pool_valid(&s_gfx.program_pool, prog.id); }

bool nt_gfx_pipeline_valid(nt_pipeline_t pip) { return nt_pool_valid(&s_gfx.pipeline_pool, pip.id); }

bool nt_gfx_vertex_input_valid(nt_vertex_input_t vi) { return nt_pool_valid(&s_gfx.vertex_input_pool, vi.id); }

bool nt_gfx_program_ready(nt_program_t prog) {
    if (!nt_pool_valid(&s_gfx.program_pool, prog.id)) {
        return false;
    }
    return s_gfx.program_states[nt_pool_slot_index(prog.id)] == NT_GFX_PROGRAM_READY;
}

nt_program_t nt_gfx_pipeline_program(nt_pipeline_t pip) {
    if (!nt_pool_valid(&s_gfx.pipeline_pool, pip.id)) {
        return NT_PROGRAM_INVALID;
    }
    nt_program_t prog = {pipeline_program(nt_pool_slot_index(pip.id))};
    return prog;
}

bool nt_gfx_texture_ready(nt_texture_t tex) {
    if (!nt_pool_valid(&s_gfx.texture_pool, tex.id)) {
        return false;
    }
    return s_gfx.texture_metas[nt_pool_slot_index(tex.id)].live;
}

bool nt_gfx_texture_size(nt_texture_t tex, uint16_t *out_width, uint16_t *out_height) {
    NT_ASSERT(out_width != NULL && out_height != NULL);
    *out_width = 0;
    *out_height = 0;
    if (!nt_pool_valid(&s_gfx.texture_pool, tex.id)) {
        return false;
    }
    uint32_t slot = nt_pool_slot_index(tex.id);
    *out_width = s_gfx.texture_metas[slot].width;
    *out_height = s_gfx.texture_metas[slot].height;
    return true;
}

nt_texture_format_t nt_gfx_texture_format(nt_texture_t tex) {
    if (!nt_pool_valid(&s_gfx.texture_pool, tex.id)) {
        return NT_TEXTURE_FORMAT_INVALID;
    }
    return (nt_texture_format_t)s_gfx.texture_metas[nt_pool_slot_index(tex.id)].format;
}
/* ---- Draw state ---- */

static nt_gfx_result_t bind_pipeline(nt_pipeline_t pip) {
    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_PASS && "bind_pipeline: must be called inside a pass");
    if (g_nt_gfx.context_lost) {
        return NT_GFX_RESULT_CONTEXT_LOST;
    }
    /* begin_pass resets the mirror and the frame rule keeps the bound object alive, so an equal nonzero id is live. */
    if (s_gfx.bound_pipeline != 0 && pip.id == s_gfx.bound_pipeline) {
        return NT_GFX_RESULT_CACHE;
    }
    if (!nt_pool_valid(&s_gfx.pipeline_pool, pip.id)) {
        /* Clearing the mirror is the whole unbind: later draws trap on it. The bound
         * vertex input is orthogonal state. */
        s_gfx.bound_pipeline = 0;
        discard_texture_set();
        NT_LOG_ERROR("bind_pipeline: invalid handle");
        return NT_GFX_RESULT_INVALID_HANDLE;
    }
    uint32_t slot = nt_pool_slot_index(pip.id);
    uint32_t program = pipeline_program(slot);
    /* The set is program state: another pipeline on the same program keeps it,
     * and a program without samplers needs none. */
    if (s_gfx.bound_pipeline == 0 || pipeline_program(nt_pool_slot_index(s_gfx.bound_pipeline)) != program) {
        const uint32_t program_slot = nt_pool_slot_index(program);
        NT_ASSERT(s_gfx.program_states[program_slot] == NT_GFX_PROGRAM_READY && "bind_pipeline: program is not ready -- gate draws on nt_gfx_program_ready");
        const bool samples = nt_gfx_backend_program_sampler_mask(program_slot) != 0;
        s_gfx.texture_set_applied = !samples;
        s_gfx.blocks_checked = false;
    }
    /* Loss frees pipeline slots, so a live slot always has a backend. */
    s_gfx.bound_pipeline = pip.id;
    nt_gfx_frame_bind_pipeline(slot);
    return NT_GFX_RESULT_ACCEPTED;
}

void nt_gfx_bind_pipeline(nt_pipeline_t pip) {
    NT_GFX_BEGIN(NT_GFX_OP_PIPELINE, NT_GFX_OBJECT_PIPELINE, pip.id);
    NT_GFX_END(bind_pipeline(pip));
}

/* Validates a vertex input that is not the bound one and publishes it; 0 = invalid handle, already logged. */
static uint32_t publish_vertex_input(nt_vertex_input_t vi) {
    /* Owners revalidate cached handles and recreate them after a loss, so a stale one here is a bug. */
    NT_ASSERT(nt_pool_valid(&s_gfx.vertex_input_pool, vi.id) && "bind_vertex_input: invalid or stale vertex input");
    uint32_t slot = nt_pool_slot_index(vi.id);
    /* Loss frees vertex-input slots, so a live slot always has a backend. */
    s_gfx.bound_vertex_input = vi.id;
    s_gfx.bound_instance_layout = 0;
    /* NT_INDEX_NONE for a non-indexed vertex input: cleared, not stale. */
    s_gfx.bound_index_type = s_gfx.vertex_input_metas[slot].index_type;
    return slot;
}

/* Instances must lie in the instance buffer, and in a frame stream inside this frame's allocations:
 * bytes past `used` are not uploaded and would draw the previous frame's data. */
static void assert_instances_in_range(uint32_t instance_count) {
    if (s_gfx.bound_instance_layout == 0 || instance_count == 0) {
        return;
    }
    const nt_gfx_instance_layout_meta_t *layout = &s_gfx.instance_layouts[s_gfx.bound_instance_layout];
    const nt_gfx_buffer_meta_t *buffer = &s_gfx.buffer_metas[nt_pool_slot_index(s_gfx.bound_instance_buffer)];
    const uint64_t limit = buffer->frame_stream != 0 ? g_nt_gfx_frame_storage[buffer->frame_stream - 1U].used : buffer->size;
    const uint64_t end = (uint64_t)s_gfx.bound_instance_offset + ((uint64_t)(instance_count - 1U) * layout->stride) + layout->extent;
    NT_ASSERT(end <= limit && "draw: instances lie outside the instance buffer or this frame's allocations in its stream");
}

static nt_gfx_result_t bind_vertex_input(nt_vertex_input_t vi) {
    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_PASS && "bind_vertex_input: must be called inside a pass");
    if (g_nt_gfx.context_lost) {
        return NT_GFX_RESULT_CONTEXT_LOST;
    }
    /* The form asserts before the skip. The bound vertex input was validated this pass and the
     * frame rule keeps it alive. */
    const bool same_vi = s_gfx.bound_vertex_input != 0 && vi.id == s_gfx.bound_vertex_input;
    const uint32_t slot = same_vi ? nt_pool_slot_index(vi.id) : publish_vertex_input(vi);
    /* Its VAO keeps divisor-1 attribs enabled at another bind's offset; a plain draw would read them. */
    NT_ASSERT(s_gfx.vertex_input_metas[slot].instance_layout == 0 && "bind_vertex_input: the vertex input was bound with instances -- keep using nt_gfx_bind_vertex_input_instanced");
    if (same_vi) {
        return NT_GFX_RESULT_CACHE;
    }
    nt_gfx_frame_bind_vertex_input(slot);
    return NT_GFX_RESULT_ACCEPTED;
}

void nt_gfx_bind_vertex_input(nt_vertex_input_t vi) {
    NT_GFX_BEGIN(NT_GFX_OP_VERTEX_INPUT, NT_GFX_OBJECT_VERTEX_INPUT, vi.id);
    NT_GFX_END(bind_vertex_input(vi));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_ASSERT expansion, not real branching
static nt_gfx_result_t bind_vertex_input_instanced(nt_vertex_input_t vi, nt_instance_layout_t layout, nt_buffer_t instance_buffer, uint32_t byte_offset) {
    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_PASS && "bind_vertex_input_instanced: must be called inside a pass");
    if (g_nt_gfx.context_lost) {
        return NT_GFX_RESULT_CONTEXT_LOST;
    }
    const bool same_vi = s_gfx.bound_vertex_input != 0 && vi.id == s_gfx.bound_vertex_input;
    /* A nonzero bound layout passed every check below, so an equal tuple needs none of them. */
    if (same_vi && s_gfx.bound_instance_layout != 0 && layout.id == s_gfx.bound_instance_layout && instance_buffer.id == s_gfx.bound_instance_buffer && byte_offset == s_gfx.bound_instance_offset) {
        return NT_GFX_RESULT_CACHE;
    }
    const uint32_t slot = same_vi ? nt_pool_slot_index(vi.id) : publish_vertex_input(vi);
    const uint32_t layout_slot = layout.id;
    NT_ASSERT(layout_slot >= 1 && layout_slot <= s_gfx.instance_layout_count && "invalid instance layout");
    nt_gfx_vertex_input_meta_t *meta = &s_gfx.vertex_input_metas[slot];
    /* The overlap check runs only when the vertex input changes instance layout. */
    if (meta->instance_layout != layout_slot) {
        NT_ASSERT((s_gfx.instance_layouts[layout_slot].mask & meta->vertex_mask) == 0 && "bind_vertex_input_instanced: instance and vertex attribs share a location");
    }
    const uint32_t buffer_slot = nt_pool_slot_index(instance_buffer.id);
    /* Runs of vertex inputs drawn from one buffer validate it once per pass; 0 means none bound yet. */
    if (instance_buffer.id != s_gfx.bound_instance_buffer || instance_buffer.id == 0) {
        NT_ASSERT(nt_pool_valid(&s_gfx.buffer_pool, instance_buffer.id) && "bind_vertex_input_instanced: invalid or stale instance buffer");
        NT_ASSERT(s_gfx.buffer_metas[buffer_slot].type == NT_BUFFER_VERTEX && "bind_vertex_input_instanced: instance buffer is not vertex type");
        NT_ASSERT(s_gfx.buffer_metas[buffer_slot].live && "bind_vertex_input_instanced: instance buffer has no live backend -- recreate it after context restore");
    }
    NT_ASSERT((byte_offset & 3U) == 0 && "bind_vertex_input_instanced: offset must be 4-byte aligned (WebGL2 attrib rule)");
    meta->instance_layout = (uint8_t)layout_slot;
    s_gfx.bound_instance_layout = layout.id;
    s_gfx.bound_instance_buffer = instance_buffer.id;
    s_gfx.bound_instance_offset = byte_offset;
    /* The first instance: plain draws read only it and need no check of their own. */
    assert_instances_in_range(1);
    nt_gfx_frame_bind_vertex_input_instanced(slot, layout_slot, buffer_slot, byte_offset);
    return NT_GFX_RESULT_ACCEPTED;
}

void nt_gfx_bind_vertex_input_instanced(nt_vertex_input_t vi, nt_instance_layout_t layout, nt_buffer_t instance_buffer, uint32_t byte_offset) {
    NT_GFX_BEGIN_REQUEST(NT_GFX_OP_VERTEX_INPUT, NT_GFX_OBJECT_VERTEX_INPUT, vi.id, event->data.binding.secondary = instance_buffer.id; event->data.binding.slot = layout.id;
                         event->data.binding.offset = byte_offset);
    NT_GFX_END(bind_vertex_input_instanced(vi, layout, instance_buffer, byte_offset));
}

void nt_gfx_set_buffer_frame_stream(nt_buffer_t buf, uint32_t stream) { s_gfx.buffer_metas[nt_pool_slot_index(buf.id)].frame_stream = (uint8_t)(stream + 1U); }

static bool texture_sampler_compatible(uint32_t texture_slot, const nt_sampler_desc_t *desc) {
    const nt_gfx_texture_meta_t *meta = &s_gfx.texture_metas[texture_slot];
    uint8_t format = meta->format;
    bool compares = desc->compare_func != NT_COMPARE_NONE;
    bool depth = nt_texture_format_is_depth((nt_texture_format_t)format);
    /* Comparison against a non-depth texture makes every lookup undefined in
     * GL, whichever sampler type the shader declares. */
    if (compares && !depth) {
        return false;
    }
    /* Filtering raw depth averages texels into a depth that exists in none of
     * them; with comparison on, LINEAR blends the 0/1 comparison results
     * instead. Integer storage has no such escape and stays NEAREST. */
    bool nearest_only = format == (uint8_t)NT_TEXTURE_FORMAT_RG16UI || (depth && !compares);
    if (nearest_only && (desc->min_filter != NT_FILTER_NEAREST || desc->mag_filter != NT_FILTER_NEAREST)) {
        return false;
    }
    if (format == (uint8_t)NT_TEXTURE_FORMAT_RGBA32F && !g_nt_gfx.gpu_caps.has_float_texture_linear && (texture_filter_uses_linear(desc->min_filter) || desc->mag_filter != NT_FILTER_NEAREST)) {
        return false;
    }
    /* Mip filters need no gate: GL_TEXTURE_MAX_LEVEL is set from the level
     * count at creation, so every live texture is mip-complete. */
    return true;
}

static bool texture_matches_sampler_class(uint32_t texture_slot, const nt_sampler_desc_t *sampler, uint8_t sampler_class) {
    const uint8_t format = s_gfx.texture_metas[texture_slot].format;
    const bool depth = nt_texture_format_is_depth((nt_texture_format_t)format);
    const bool integer = format == (uint8_t)NT_TEXTURE_FORMAT_RG16UI;
    const bool compares = sampler->compare_func != NT_COMPARE_NONE;
    switch ((nt_gfx_sampler_class_t)sampler_class) {
    case NT_GFX_SAMPLER_CLASS_FLOAT:
        return !integer && !compares;
    case NT_GFX_SAMPLER_CLASS_SHADOW:
        return depth && compares;
    case NT_GFX_SAMPLER_CLASS_UINT:
        return integer && !compares;
    }
    return false;
}

/* A failed recreate cannot replace an explicit override with texture defaults. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- contract asserts expand into nested handler branches
static nt_gfx_result_t resolve_sampler_backend(uint32_t texture_slot, nt_sampler_t sampler, uint8_t sampler_class, uint32_t *out_backend) {
    nt_sampler_t effective = sampler.id != 0 ? sampler : s_gfx.texture_metas[texture_slot].default_sampler;
    NT_ASSERT(effective.id != 0 && "apply_texture_bindings: live texture without a default sampler");
    NT_ASSERT(effective.id <= s_gfx.sampler_count && "apply_texture_bindings: invalid sampler handle");
    nt_gfx_sampler_entry_t *e = &s_gfx.sampler_cache[effective.id - 1];
    NT_ASSERT(texture_sampler_compatible(texture_slot, &e->desc) && "apply_texture_bindings: sampler is incompatible with texture storage");
    NT_ASSERT(texture_matches_sampler_class(texture_slot, &e->desc, sampler_class) && "apply_texture_bindings: texture and sampler do not match the program sampler type");
    if (e->backend == 0) {
        /* Lazy recreate after context-loss recovery — desc was preserved. */
        e->backend = nt_gfx_backend_create_sampler(&e->desc);
        if (e->backend == 0) {
            return latch_loss();
        }
        NT_GFX_DEFINE_RESOURCE(NT_GFX_OBJECT_SAMPLER, effective.id);
    }
    *out_backend = e->backend;
    return NT_GFX_RESULT_ACCEPTED;
}

static bool texture_is_active_attachment(nt_texture_t texture) {
    if (s_gfx.active_render_target == 0) {
        return false;
    }
    const nt_gfx_render_target_meta_t *target = &s_gfx.render_target_metas[nt_pool_slot_index(s_gfx.active_render_target)];
    return texture.id == target->attachments[NT_GFX_RT_COLOR].id || texture.id == target->attachments[NT_GFX_RT_DEPTH].id;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- contract asserts expand into nested handler branches
static nt_gfx_result_t apply_texture_bindings(const nt_gfx_texture_binding_t *bindings, uint8_t count) {
    discard_texture_set();
    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_PASS && "apply_texture_bindings: must be called inside a pass");
    if (g_nt_gfx.context_lost) {
        return NT_GFX_RESULT_CONTEXT_LOST;
    }
    NT_ASSERT(s_gfx.bound_pipeline != 0 && "apply_texture_bindings: no pipeline bound");
    NT_ASSERT((bindings != NULL || count == 0) && "apply_texture_bindings: NULL bindings with nonzero count");
    const uint32_t program = pipeline_program(nt_pool_slot_index(s_gfx.bound_pipeline));
    const uint32_t program_backend = nt_pool_slot_index(program);
    const uint8_t required_mask = (uint8_t)nt_gfx_backend_program_sampler_mask(program_backend);

    /* Resolve everything first: a failure late in the set must leave no bind behind. */
    uint32_t texture_backends[NT_GFX_MAX_TEXTURE_SLOTS];
    uint32_t sampler_backends[NT_GFX_MAX_TEXTURE_SLOTS];
    uint8_t applied_mask = 0;
    for (uint8_t i = 0; i < count; i++) {
        nt_gfx_sampler_info_t info = {0};
        NT_GFX_RECORD(NT_GFX_EVENT_ARGUMENT, NT_GFX_OP_TEXTURE_SET, event->object_kind = NT_GFX_OBJECT_TEXTURE; event->object = bindings[i].texture.id;
                      event->data.binding.name = bindings[i].name.value; event->data.binding.secondary = bindings[i].sampler.id;);
        if (!nt_gfx_backend_program_sampler_info(program_backend, bindings[i].name.value, &info)) {
            NT_GFX_RECORD(NT_GFX_EVENT_SKIP, NT_GFX_OP_TEXTURE_SET, event->result = NT_GFX_RESULT_INACTIVE; event->data.binding.name = bindings[i].name.value;);
            continue;
        }
        const uint8_t bit = (uint8_t)(1U << info.unit);
        NT_ASSERT((applied_mask & bit) == 0 && "apply_texture_bindings: active sampler appears more than once");
        /* TRAP omits assert text; the log names the sampler that has no texture. */
        if (!nt_pool_valid(&s_gfx.texture_pool, bindings[i].texture.id)) {
            NT_LOG_ERROR_ONCE("apply_texture_bindings: program=%08x sampler=%08x has no texture; register an explicit placeholder for async resources", program, bindings[i].name.value);
        }
        NT_ASSERT(nt_pool_valid(&s_gfx.texture_pool, bindings[i].texture.id) && "apply_texture_bindings: invalid texture handle");
        NT_ASSERT(!texture_is_active_attachment(bindings[i].texture) && "apply_texture_bindings: cannot sample the active render target's attachment");
        const uint32_t texture_slot = nt_pool_slot_index(bindings[i].texture.id);
        NT_ASSERT(s_gfx.texture_metas[texture_slot].live && "apply_texture_bindings: texture has no live backend -- recreate it after context restore");
        uint32_t sampler_backend = 0;
        const nt_gfx_result_t sampler_result = resolve_sampler_backend(texture_slot, bindings[i].sampler, info.sampler_class, &sampler_backend);
        if (sampler_result != NT_GFX_RESULT_ACCEPTED) {
            return sampler_result; /* the lazy recreate met a loss: draws take their lost path first */
        }
        texture_backends[info.unit] = texture_slot;
        sampler_backends[info.unit] = sampler_backend;
        applied_mask |= bit;
    }

    if (applied_mask != required_mask) {
        NT_LOG_ERROR_ONCE("apply_texture_bindings: program=%08x sampler coverage missing=%02x", program, (uint8_t)(required_mask & (uint8_t)~applied_mask));
    }
    NT_ASSERT(applied_mask == required_mask && "apply_texture_bindings: active sampler coverage is incomplete");
    bool recorded = false;
    for (uint8_t unit = 0; unit < NT_GFX_MAX_TEXTURE_SLOTS; unit++) {
        nt_gfx_unit_binding_t *bound = &s_gfx.bound_units[unit];
        if ((applied_mask & (uint8_t)(1U << unit)) == 0 || (bound->texture == texture_backends[unit] && bound->sampler == sampler_backends[unit])) {
            continue;
        }
        *bound = (nt_gfx_unit_binding_t){texture_backends[unit], sampler_backends[unit]};
        nt_gfx_frame_bind_texture_unit(texture_backends[unit], sampler_backends[unit], unit);
        recorded = true;
    }
    s_gfx.texture_set_applied = true;
    return recorded ? NT_GFX_RESULT_ACCEPTED : NT_GFX_RESULT_CACHE;
}

void nt_gfx_apply_texture_bindings(const nt_gfx_texture_binding_t *bindings, uint8_t count) {
    NT_GFX_BEGIN_REQUEST(NT_GFX_OP_TEXTURE_SET, NT_GFX_OBJECT_PIPELINE, s_gfx.bound_pipeline, event->data.state.integers[0] = count);
    NT_GFX_END(apply_texture_bindings(bindings, count));
}

#ifdef NT_TEST_ACCESS
bool nt_gfx_test_program_sampler_info(nt_program_t prog, nt_hash32_t name, nt_gfx_sampler_info_t *out_info) {
    NT_ASSERT(nt_gfx_program_ready(prog) && "test_program_sampler_info: program is not linked");
    NT_ASSERT(out_info != NULL && "test_program_sampler_info: out_info is required");
    return nt_gfx_backend_program_sampler_info(nt_pool_slot_index(prog.id), name.value, out_info);
}

int nt_gfx_test_program_sampler_unit(nt_program_t prog, nt_hash32_t name) {
    nt_gfx_sampler_info_t info = {0};
    return nt_gfx_test_program_sampler_info(prog, name, &info) ? info.unit : -1;
}

uint32_t nt_gfx_test_program_sampler_mask(nt_program_t prog) {
    NT_ASSERT(nt_gfx_program_ready(prog) && "test_program_sampler_mask: program is not linked");
    return nt_gfx_backend_program_sampler_mask(nt_pool_slot_index(prog.id));
}
#endif

nt_sampler_t nt_gfx_get_texture_default_sampler(nt_texture_t tex) {
    if (!nt_pool_valid(&s_gfx.texture_pool, tex.id)) {
        return NT_SAMPLER_INVALID;
    }
    return s_gfx.texture_metas[nt_pool_slot_index(tex.id)].default_sampler;
}

#ifdef NT_TEST_ACCESS
uint32_t nt_gfx_test_sampler_backend_id(nt_sampler_t s) {
    if (s.id == 0 || s.id > s_gfx.sampler_count) {
        return 0;
    }
    return s_gfx.sampler_cache[s.id - 1].backend;
}

uint32_t nt_gfx_test_texture_backend_id(nt_texture_t tex) {
    if (!nt_pool_valid(&s_gfx.texture_pool, tex.id)) {
        return 0;
    }
    const uint32_t slot = nt_pool_slot_index(tex.id);
    return s_gfx.texture_metas[slot].live ? slot : 0;
}

uint32_t nt_gfx_test_buffer_backend_id(nt_buffer_t buf) {
    if (!nt_pool_valid(&s_gfx.buffer_pool, buf.id)) {
        return 0;
    }
    const uint32_t slot = nt_pool_slot_index(buf.id);
    return s_gfx.buffer_metas[slot].live ? slot : 0;
}

uint32_t nt_gfx_test_render_target_backend_id(nt_render_target_t rt) {
    if (!nt_pool_valid(&s_gfx.render_target_pool, rt.id)) {
        return 0;
    }
    return nt_pool_slot_index(rt.id);
}

void nt_gfx_test_scissor_rect(int out[4]) {
    NT_ASSERT(out != NULL);
    out[0] = s_gfx.scissor_rect[0];
    out[1] = s_gfx.scissor_rect[1];
    out[2] = s_gfx.scissor_rect[2];
    out[3] = s_gfx.scissor_rect[3];
}

void nt_gfx_test_viewport_rect(int out[4]) {
    NT_ASSERT(out != NULL);
    out[0] = s_gfx.viewport_rect[0];
    out[1] = s_gfx.viewport_rect[1];
    out[2] = s_gfx.viewport_rect[2];
    out[3] = s_gfx.viewport_rect[3];
}

bool nt_gfx_test_scissor_enabled(void) { return s_gfx.scissor_enabled; }

uint32_t nt_gfx_test_bound_pipeline(void) { return s_gfx.bound_pipeline; }

nt_pipeline_t nt_gfx_test_pipeline_of_backend(uint32_t backend_handle) { return (nt_pipeline_t){s_gfx.pipeline_pool.slots[backend_handle].id}; }

uint32_t nt_gfx_test_bound_vertex_input(void) { return s_gfx.bound_vertex_input; }

bool nt_gfx_test_texture_set_applied(void) { return s_gfx.texture_set_applied; }

uint32_t nt_gfx_test_stage_size(void) { return s_stage_size; }

const void *nt_gfx_test_stage_ptr(void) { return s_stage_buf; }
#endif

/* ---- Sampler (deduplicated cache) ---- */

/* One clamp for the whole sampler path: key, GL object and recorded desc derive
 * from the result, so they cannot disagree. Pack headers cast raw bytes into
 * these enums, so out of range takes the zero value the backend maps it to. */
static inline nt_sampler_desc_t sampler_normalize(const nt_sampler_desc_t *desc) {
    nt_sampler_desc_t out = *desc;
    if ((uint32_t)out.min_filter > (uint32_t)NT_FILTER_LINEAR_MIPMAP_LINEAR) {
        out.min_filter = NT_FILTER_NEAREST;
    }
    if ((uint32_t)out.mag_filter > (uint32_t)NT_FILTER_LINEAR) {
        out.mag_filter = NT_FILTER_NEAREST;
    }
    if ((uint32_t)out.wrap_u > (uint32_t)NT_WRAP_MIRRORED_REPEAT) {
        out.wrap_u = NT_WRAP_CLAMP_TO_EDGE;
    }
    if ((uint32_t)out.wrap_v > (uint32_t)NT_WRAP_MIRRORED_REPEAT) {
        out.wrap_v = NT_WRAP_CLAMP_TO_EDGE;
    }
    if ((uint32_t)out.compare_func > (uint32_t)NT_COMPARE_NEVER) {
        out.compare_func = NT_COMPARE_NONE;
    }
    return out;
}

/* Normalized descs only: equal key must mean an identical GL sampler. */
static inline uint32_t sampler_pack_key(const nt_sampler_desc_t *desc) {
    return (uint32_t)desc->min_filter | ((uint32_t)desc->mag_filter << 3) | ((uint32_t)desc->wrap_u << 4) | ((uint32_t)desc->wrap_v << 6) | ((uint32_t)desc->compare_func << 8);
}
_Static_assert(NT_FILTER_LINEAR_MIPMAP_LINEAR < 8 && NT_FILTER_LINEAR < 2 && NT_WRAP_MIRRORED_REPEAT < 4 && NT_COMPARE_NEVER < 16,
               "sampler_pack_key field widths — a new enum value would overlap the next field");

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — cache hit / miss / lazy-recreate paths
static nt_gfx_result_t make_sampler(const nt_sampler_desc_t *desc, nt_sampler_t *out) {
    NT_ASSERT(desc != NULL);
    /* Unsigned: nt_compare_func_t and nt_texture_filter_t are signed under the
     * MSVC ABI, where a negative cast would pass an upper-bound-only check. */
    NT_ASSERT((uint32_t)desc->mag_filter <= NT_FILTER_LINEAR && "sampler mag_filter must be NEAREST or LINEAR");
    NT_ASSERT((uint32_t)desc->compare_func <= NT_COMPARE_NEVER && "sampler compare_func out of range");

    nt_sampler_desc_t normalized = sampler_normalize(desc);
    uint32_t key = sampler_pack_key(&normalized);

    for (uint32_t i = 0; i < s_gfx.sampler_count; i++) {
        if (s_gfx.sampler_cache[i].key == key) {
            /* Hit: the id is stable; a backend a loss zeroed is recreated here or at bind. */
            out->id = i + 1;
            if (s_gfx.sampler_cache[i].backend == 0 && !g_nt_gfx.context_lost) {
                s_gfx.sampler_cache[i].backend = nt_gfx_backend_create_sampler(&s_gfx.sampler_cache[i].desc);
                if (s_gfx.sampler_cache[i].backend == 0) {
                    return latch_loss();
                }
                NT_GFX_DEFINE_RESOURCE(NT_GFX_OBJECT_SAMPLER, i + 1);
            }
            return NT_GFX_RESULT_CACHE;
        }
    }

    if (g_nt_gfx.context_lost) {
        return NT_GFX_RESULT_CONTEXT_LOST;
    }
    NT_ASSERT(s_gfx.sampler_count < NT_GFX_MAX_SAMPLERS && "sampler cache full; raise NT_GFX_MAX_SAMPLERS");
    uint32_t backend = nt_gfx_backend_create_sampler(&normalized);
    if (backend == 0) {
        return latch_loss();
    }
    uint32_t slot = s_gfx.sampler_count++;
    s_gfx.sampler_cache[slot].key = key;
    s_gfx.sampler_cache[slot].backend = backend;
    s_gfx.sampler_cache[slot].desc = normalized;
    NT_GFX_DEFINE_RESOURCE(NT_GFX_OBJECT_SAMPLER, slot + 1);
    out->id = slot + 1;
    return NT_GFX_RESULT_ACCEPTED;
}

nt_sampler_t nt_gfx_make_sampler(const nt_sampler_desc_t *desc) {
    NT_GFX_BEGIN_REQUEST(
        NT_GFX_OP_CREATE, NT_GFX_OBJECT_SAMPLER, 0, if (desc != NULL) {
            const nt_sampler_desc_t normalized = sampler_normalize(desc);
            event->data.resource.flags = sampler_pack_key(&normalized);
        });
    nt_sampler_t result = {0};
    NT_GFX_END_OBJECT(make_sampler(desc, &result), result.id);
    return result;
}

/* ---- Scissor and viewport ----
 *
 * All three wrappers early-return on context loss — backend is dead, cached
 * state must not drift. Callers re-issue from a clean frame after restore. */

static nt_gfx_result_t set_scissor(int x, int y, int w, int h) {
    /* Negative width/height is undefined in GL — assert early per AGENTS.md "fail early". */
    NT_ASSERT(w >= 0);
    NT_ASSERT(h >= 0);
    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_PASS && "set_scissor: must be called inside a pass");
    if (g_nt_gfx.context_lost) {
        return NT_GFX_RESULT_CONTEXT_LOST;
    }
    if (s_gfx.scissor_rect[0] == x && s_gfx.scissor_rect[1] == y && s_gfx.scissor_rect[2] == w && s_gfx.scissor_rect[3] == h) {
        return NT_GFX_RESULT_CACHE;
    }
    s_gfx.scissor_rect[0] = x;
    s_gfx.scissor_rect[1] = y;
    s_gfx.scissor_rect[2] = w;
    s_gfx.scissor_rect[3] = h;
    nt_gfx_frame_set_scissor(x, y, w, h);
    return NT_GFX_RESULT_ACCEPTED;
}

void nt_gfx_set_scissor(int x, int y, int w, int h) {
    NT_GFX_BEGIN_REQUEST(NT_GFX_OP_SCISSOR, NT_GFX_OBJECT_NONE, 0, event->data.state.integers[0] = (uint32_t)x; event->data.state.integers[1] = (uint32_t)y;
                         event->data.state.integers[2] = (uint32_t)w; event->data.state.integers[3] = (uint32_t)h);
    NT_GFX_END(set_scissor(x, y, w, h));
}

static nt_gfx_result_t set_scissor_enabled(bool enabled) {
    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_PASS && "set_scissor_enabled: must be called inside a pass");
    if (g_nt_gfx.context_lost) {
        return NT_GFX_RESULT_CONTEXT_LOST;
    }
    /* The mirror owns this state end to end, so the dedup lives here and the backend stays raw. */
    if (s_gfx.scissor_enabled == enabled) {
        return NT_GFX_RESULT_CACHE;
    }
    s_gfx.scissor_enabled = enabled;
    nt_gfx_frame_set_scissor_enabled(enabled);
    return NT_GFX_RESULT_ACCEPTED;
}

void nt_gfx_set_scissor_enabled(bool enabled) {
    NT_GFX_BEGIN_REQUEST(NT_GFX_OP_SCISSOR_ENABLE, NT_GFX_OBJECT_NONE, 0, event->data.state.integers[0] = enabled);
    NT_GFX_END(set_scissor_enabled(enabled));
}

static nt_gfx_result_t set_viewport(int x, int y, int w, int h) {
    NT_ASSERT(w >= 0);
    NT_ASSERT(h >= 0);
    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_PASS && "set_viewport: must be called inside a pass");
    if (g_nt_gfx.context_lost) {
        return NT_GFX_RESULT_CONTEXT_LOST;
    }
    if (s_gfx.viewport_rect[0] == x && s_gfx.viewport_rect[1] == y && s_gfx.viewport_rect[2] == w && s_gfx.viewport_rect[3] == h) {
        return NT_GFX_RESULT_CACHE;
    }
    s_gfx.viewport_rect[0] = x;
    s_gfx.viewport_rect[1] = y;
    s_gfx.viewport_rect[2] = w;
    s_gfx.viewport_rect[3] = h;
    nt_gfx_frame_set_viewport(x, y, w, h);
    return NT_GFX_RESULT_ACCEPTED;
}

void nt_gfx_set_viewport(int x, int y, int w, int h) {
    NT_GFX_BEGIN_REQUEST(NT_GFX_OP_VIEWPORT, NT_GFX_OBJECT_NONE, 0, event->data.state.integers[0] = (uint32_t)x; event->data.state.integers[1] = (uint32_t)y;
                         event->data.state.integers[2] = (uint32_t)w; event->data.state.integers[3] = (uint32_t)h);
    NT_GFX_END(set_viewport(x, y, w, h));
}

/* ---- Draw calls ---- */

/* Every draw reads vertex-input state; attribute-less draws bind an empty one. */
static void assert_vertex_input_bound(void) { NT_ASSERT(s_gfx.bound_vertex_input != 0 && "draw: no vertex input bound -- bind one with nt_gfx_bind_vertex_input"); }

static void assert_texture_set_applied(void) { NT_ASSERT(s_gfx.texture_set_applied && "draw: bound program requires a texture set -- call nt_gfx_apply_texture_bindings"); }

/* WebGL2 rejects a draw whose program reads a block with a smaller or no bound range (native GL
 * reads past it), so the first draw after a program or block change checks every block the
 * program uses. Runs before a merge: a merged draw must not inherit an unchecked binding. */
static void assert_blocks_bound(void) {
    if (s_gfx.blocks_checked) {
        return;
    }
    const uint32_t *needed = nt_gfx_backend_program_block_sizes(nt_pool_slot_index(pipeline_program(nt_pool_slot_index(s_gfx.bound_pipeline))));
    for (uint32_t slot = 0; slot < NT_GFX_MAX_UNIFORM_BUFFER_SLOTS; slot++) {
        const uint32_t bound_size = (s_gfx.bound_blocks >> slot) & 1U ? s_gfx.staged_blocks[slot].size : 0;
        NT_ASSERT(bound_size >= needed[slot] && "draw: a uniform block the program uses is unbound or smaller than the block -- bind it in this pass");
    }
    s_gfx.blocks_checked = true;
}

/* The bound vertex input carries its own index type; NT_INDEX_NONE here means
 * the caller draws indexed on a non-indexed input. */
static void assert_indexed_draw_has_index_type(void) { NT_ASSERT(s_gfx.bound_index_type != NT_INDEX_NONE && "draw_indexed: bound vertex input is non-indexed"); }

/* Draws are whole GL_TRIANGLES lists, which a merge relies on. */
static void assert_whole_triangles(uint32_t count) { NT_ASSERT(count % 3U == 0U && "draw: vertex or index count is not a whole number of triangles"); }

static nt_gfx_result_t draw(uint32_t first_vertex, uint32_t num_vertices) {
    assert_whole_triangles(num_vertices);
    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_PASS);
    if (g_nt_gfx.context_lost) {
        return NT_GFX_RESULT_CONTEXT_LOST;
    }
    NT_ASSERT(s_gfx.bound_pipeline != 0);
    assert_texture_set_applied();
    assert_vertex_input_bound();
    assert_blocks_bound();

    g_nt_gfx.counters.vertices += num_vertices;
    /* A merged call records nothing new: CACHE keeps it out of accepted[] and draw_calls. */
    return nt_gfx_frame_draw_merging(NT_GFX_CMD_DRAW, first_vertex, num_vertices, NT_INDEX_NONE) ? NT_GFX_RESULT_CACHE : NT_GFX_RESULT_ACCEPTED;
}

void nt_gfx_draw(uint32_t first_vertex, uint32_t num_vertices) {
    NT_GFX_BEGIN_REQUEST(NT_GFX_OP_DRAW, NT_GFX_OBJECT_PIPELINE, s_gfx.bound_pipeline, event->data.draw.first = first_vertex; event->data.draw.count = num_vertices;
                         event->data.draw.vertices = num_vertices; event->data.draw.instances = 1; event->data.draw.vertex_input = s_gfx.bound_vertex_input);
    NT_GFX_END(draw(first_vertex, num_vertices));
}

static nt_gfx_result_t draw_instanced(uint32_t first_vertex, uint32_t num_vertices, uint32_t instance_count) {
    assert_whole_triangles(num_vertices);
    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_PASS);
    if (g_nt_gfx.context_lost) {
        return NT_GFX_RESULT_CONTEXT_LOST;
    }
    NT_ASSERT(s_gfx.bound_pipeline != 0);
    assert_texture_set_applied();
    assert_vertex_input_bound();
    assert_blocks_bound();
    assert_instances_in_range(instance_count);

    g_nt_gfx.counters.vertices += (uint64_t)num_vertices * instance_count;
    g_nt_gfx.counters.instances += instance_count;
    nt_gfx_frame_draw(first_vertex, num_vertices, instance_count);
    return NT_GFX_RESULT_ACCEPTED;
}

void nt_gfx_draw_instanced(uint32_t first_vertex, uint32_t num_vertices, uint32_t instance_count) {
    NT_GFX_BEGIN_REQUEST(NT_GFX_OP_DRAW_INSTANCED, NT_GFX_OBJECT_PIPELINE, s_gfx.bound_pipeline, event->data.draw.first = first_vertex; event->data.draw.count = num_vertices;
                         event->data.draw.vertices = num_vertices; event->data.draw.instances = instance_count; event->data.draw.vertex_input = s_gfx.bound_vertex_input);
    NT_GFX_END(draw_instanced(first_vertex, num_vertices, instance_count));
}

static nt_gfx_result_t draw_indexed(uint32_t first_index, uint32_t num_indices, uint32_t num_vertices) {
    assert_whole_triangles(num_indices);
    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_PASS);
    if (g_nt_gfx.context_lost) {
        return NT_GFX_RESULT_CONTEXT_LOST;
    }
    NT_ASSERT(s_gfx.bound_pipeline != 0);
    assert_texture_set_applied();
    assert_vertex_input_bound();
    assert_blocks_bound();
    assert_indexed_draw_has_index_type();

    g_nt_gfx.counters.vertices += num_vertices;
    g_nt_gfx.counters.indices += num_indices;
    /* A merged call records nothing new: CACHE keeps it out of accepted[] and draw_calls. */
    return nt_gfx_frame_draw_merging(NT_GFX_CMD_DRAW_INDEXED, first_index, num_indices, s_gfx.bound_index_type) ? NT_GFX_RESULT_CACHE : NT_GFX_RESULT_ACCEPTED;
}

void nt_gfx_draw_indexed(uint32_t first_index, uint32_t num_indices, uint32_t num_vertices) {
    NT_GFX_BEGIN_REQUEST(NT_GFX_OP_DRAW_INDEXED, NT_GFX_OBJECT_PIPELINE, s_gfx.bound_pipeline, event->data.draw.first = first_index; event->data.draw.count = num_indices;
                         event->data.draw.vertices = num_vertices; event->data.draw.instances = 1; event->data.draw.vertex_input = s_gfx.bound_vertex_input);
    NT_GFX_END(draw_indexed(first_index, num_indices, num_vertices));
}

static nt_gfx_result_t draw_indexed_instanced(uint32_t first_index, uint32_t num_indices, uint32_t num_vertices, uint32_t instance_count) {
    assert_whole_triangles(num_indices);
    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_PASS);
    if (g_nt_gfx.context_lost) {
        return NT_GFX_RESULT_CONTEXT_LOST;
    }
    NT_ASSERT(s_gfx.bound_pipeline != 0);
    assert_texture_set_applied();
    assert_vertex_input_bound();
    assert_blocks_bound();
    assert_indexed_draw_has_index_type();
    assert_instances_in_range(instance_count);

    g_nt_gfx.counters.vertices += (uint64_t)num_vertices * instance_count;
    g_nt_gfx.counters.indices += (uint64_t)num_indices * instance_count;
    g_nt_gfx.counters.instances += instance_count;
    nt_gfx_frame_draw_indexed(first_index, num_indices, instance_count, s_gfx.bound_index_type);
    return NT_GFX_RESULT_ACCEPTED;
}

void nt_gfx_draw_indexed_instanced(uint32_t first_index, uint32_t num_indices, uint32_t num_vertices, uint32_t instance_count) {
    NT_GFX_BEGIN_REQUEST(NT_GFX_OP_DRAW_INDEXED_INSTANCED, NT_GFX_OBJECT_PIPELINE, s_gfx.bound_pipeline, event->data.draw.first = first_index; event->data.draw.count = num_indices;
                         event->data.draw.vertices = num_vertices; event->data.draw.instances = instance_count; event->data.draw.vertex_input = s_gfx.bound_vertex_input);
    NT_GFX_END(draw_indexed_instanced(first_index, num_indices, num_vertices, instance_count));
}

/* ---- Uniform blocks ---- */

void nt_gfx_bind_uniform_block(uint32_t slot, const void *data, uint32_t size) {
    NT_ASSERT(slot < NT_GFX_MAX_UNIFORM_BUFFER_SLOTS && data != NULL && size > 0 && size % 16U == 0 && "bind_uniform_block: slot, data or size (a nonzero multiple of 16)");
    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_PASS && "bind_uniform_block: must be called inside a pass");
    const bool lost = g_nt_gfx.context_lost;
    const nt_gfx_frame_storage_t *storage = &g_nt_gfx_frame_storage[NT_GFX_FRAME_UNIFORM];
    /* Staged bytes stay unchanged until the next begin_frame, so bytes equal to the slot's last
     * staged block bind that range again without a copy. */
    uint32_t offset = s_gfx.staged_blocks[slot].offset;
    const bool staged = !lost && s_gfx.staged_blocks[slot].size == size && memcmp(storage->staging + offset, data, size) == 0;
    if (!lost && !staged) {
        /* A disabled stream (zero capacity) reaches the allocator, which stops on the overflow. */
        memcpy(nt_gfx_frame_alloc(NT_GFX_FRAME_UNIFORM, size, g_nt_gfx.gpu_caps.uniform_buffer_offset_alignment, &offset), data, size);
        s_gfx.staged_blocks[slot] = (nt_gfx_block_binding_t){offset, size};
    }
    const bool equal = staged && ((s_gfx.bound_blocks >> slot) & 1U) != 0;
    NT_GFX_BEGIN_REQUEST(NT_GFX_OP_UBO, NT_GFX_OBJECT_BUFFER, storage->buffer.id, event->data.binding.slot = slot; event->data.binding.offset = offset; event->data.binding.size = size);
    if (lost) {
        NT_GFX_END(NT_GFX_RESULT_CONTEXT_LOST);
        return;
    }
    if (!equal) {
        s_gfx.bound_blocks |= 1U << slot;
        s_gfx.blocks_checked = false;
        nt_gfx_frame_bind_uniform_buffer(nt_pool_slot_index(storage->buffer.id), slot, offset, size);
    }
    NT_GFX_END(equal ? NT_GFX_RESULT_CACHE : NT_GFX_RESULT_ACCEPTED);
}

static nt_gfx_result_t unbind_uniform_block(uint32_t slot) {
    NT_ASSERT(slot < NT_GFX_MAX_UNIFORM_BUFFER_SLOTS && "unbind_uniform_block: slot");
    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_PASS && "unbind_uniform_block: must be called inside a pass");
    if (g_nt_gfx.context_lost) {
        return NT_GFX_RESULT_CONTEXT_LOST;
    }
    if (((s_gfx.bound_blocks >> slot) & 1U) == 0) {
        return NT_GFX_RESULT_CACHE;
    }
    /* The unbind is logical: GL rejects an empty range, and the draw check reads the mirror. */
    s_gfx.bound_blocks &= ~(1U << slot);
    s_gfx.blocks_checked = false;
    return NT_GFX_RESULT_ACCEPTED;
}

void nt_gfx_unbind_uniform_block(uint32_t slot) {
    NT_GFX_BEGIN_REQUEST(NT_GFX_OP_UBO, NT_GFX_OBJECT_BUFFER, 0, event->data.binding.slot = slot);
    NT_GFX_END(unbind_uniform_block(slot));
}

/* ---- Buffer update ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — NT_ASSERT expansion, not real branching
static nt_gfx_result_t buffer_update(nt_buffer_t buf, uint32_t offset, const void *data, uint32_t size) {
    if (g_nt_gfx.context_lost) {
        return NT_GFX_RESULT_CONTEXT_LOST;
    }
    if (!nt_pool_valid(&s_gfx.buffer_pool, buf.id)) {
        NT_LOG_ERROR("update_buffer: invalid handle");
        return NT_GFX_RESULT_INVALID_HANDLE;
    }
    uint32_t slot = nt_pool_slot_index(buf.id);
    NT_ASSERT(s_gfx.buffer_metas[slot].usage != NT_USAGE_IMMUTABLE && "update_buffer: cannot update immutable buffer");
    NT_ASSERT((data != NULL || size == 0) && "update_buffer: NULL data with nonzero size");
    NT_ASSERT(offset <= s_gfx.buffer_metas[slot].size && "update_buffer: offset exceeds buffer capacity");
    NT_ASSERT(size <= s_gfx.buffer_metas[slot].size - offset && "update_buffer: offset + size exceeds buffer capacity");
    NT_ASSERT(s_gfx.buffer_metas[slot].live && "update_buffer: buffer has no live backend -- recreate it after context restore");
    nt_gfx_backend_update_buffer(slot, offset, data, size);
    return NT_GFX_RESULT_ACCEPTED;
}

void nt_gfx_update_buffer(nt_buffer_t buf, uint32_t offset, const void *data, uint32_t size) {
    NT_GFX_BEGIN_REQUEST(NT_GFX_OP_BUFFER_UPLOAD, NT_GFX_OBJECT_BUFFER, buf.id, event->data.resource.size = size; event->data.resource.related[0] = offset; event->data.resource.flags = data != NULL);
    NT_GFX_END(buffer_update(buf, offset, data, size));
}

static nt_gfx_result_t begin_segment(const char *name) {
    NT_ASSERT(s_gfx.render_state != NT_GFX_STATE_ENDED && "begin_segment: must be called inside a frame");
    if (g_nt_gfx.context_lost) {
        return NT_GFX_RESULT_CONTEXT_LOST;
    }
#if NT_GFX_GPU_TIMING_ENABLED
    nt_gfx_frame_begin_segment(name);
#else
    (void)name;
#endif
    return NT_GFX_RESULT_ACCEPTED;
}

void nt_gfx_begin_segment(const char *name) {
    NT_ASSERT(name != NULL);
    NT_GFX_BEGIN_REQUEST(NT_GFX_OP_SEGMENT_BEGIN, NT_GFX_OBJECT_NONE, 0, event->data.binding.name = nt_hash32_str(name).value);
    NT_GFX_END(begin_segment(name));
}

static nt_gfx_result_t end_segment(void) {
    NT_ASSERT(s_gfx.render_state != NT_GFX_STATE_ENDED && "end_segment: must be called inside a frame");
#if NT_GFX_GPU_TIMING_ENABLED
    nt_gfx_frame_end_segment();
#endif
    return NT_GFX_RESULT_ACCEPTED;
}

void nt_gfx_end_segment(void) {
    NT_GFX_BEGIN(NT_GFX_OP_SEGMENT_END, NT_GFX_OBJECT_NONE, 0);
    NT_GFX_END(end_segment());
}

static nt_gfx_result_t poll_segment_time_ns(const char *name, uint64_t *out_ns) {
    if (g_nt_gfx.context_lost) {
        return NT_GFX_RESULT_CONTEXT_LOST;
    }
    return nt_gfx_backend_poll_segment_time_ns(name, out_ns) ? NT_GFX_RESULT_ACCEPTED : NT_GFX_RESULT_UNREADY;
}

bool nt_gfx_poll_segment_time_ns(const char *name, uint64_t *out_ns) {
    NT_ASSERT(name != NULL && out_ns != NULL);
    NT_GFX_BEGIN_REQUEST(NT_GFX_OP_SEGMENT_POLL, NT_GFX_OBJECT_NONE, 0, event->data.binding.name = nt_hash32_str(name).value);
    const nt_gfx_result_t result = poll_segment_time_ns(name, out_ns);
    NT_GFX_END(result);
    return result == NT_GFX_RESULT_ACCEPTED;
}

void nt_gfx_set_gpu_timing_enabled(bool enabled) {
    /* The backend closes an active segment now, so a toggle may not split the recorded segments of a frame. */
    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_ENDED && "set_gpu_timing_enabled: toggle between nt_gfx_end_frame and nt_gfx_begin_frame");
    NT_GFX_BEGIN_REQUEST(NT_GFX_OP_GPU_TIMING, NT_GFX_OBJECT_NONE, 0, event->data.state.integers[0] = enabled);
    nt_gfx_backend_set_gpu_timing_enabled(enabled);
    NT_GFX_END(NT_GFX_RESULT_ACCEPTED);
}

bool nt_gfx_is_gpu_timing_supported(void) { return nt_gfx_backend_is_gpu_timing_supported(); }

/* ---- Texture update ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static nt_gfx_result_t update_texture(nt_texture_t tex, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const void *data) {
    if (g_nt_gfx.context_lost) {
        return NT_GFX_RESULT_CONTEXT_LOST;
    }
    if (!nt_pool_valid(&s_gfx.texture_pool, tex.id)) {
        NT_LOG_ERROR("update_texture: invalid handle");
        return NT_GFX_RESULT_INVALID_HANDLE;
    }
    uint32_t slot = nt_pool_slot_index(tex.id);
    uint8_t stored_format = s_gfx.texture_metas[slot].format;
    NT_ASSERT(nt_texture_format_valid((nt_texture_format_t)stored_format) && "update_texture: invalid stored format");
    NT_ASSERT(data != NULL && "update_texture: NULL data pointer");
    NT_ASSERT(w > 0 && h > 0 && "update_texture: zero-size region");
    NT_ASSERT(!nt_texture_format_is_compressed((nt_texture_format_t)stored_format) && "update_texture: compressed textures cannot be sub-updated");
    NT_ASSERT(s_gfx.texture_metas[slot].mip_count <= 1 && "update_texture: multi-level textures cannot be sub-updated -- recreate the texture");
    NT_ASSERT(!nt_texture_format_is_depth((nt_texture_format_t)stored_format) && "update_texture: depth texture updates are not supported");
    NT_ASSERT(x + w <= s_gfx.texture_metas[slot].width && "update_texture: x+w exceeds texture width");
    NT_ASSERT(y + h <= s_gfx.texture_metas[slot].height && "update_texture: y+h exceeds texture height");
    /* A husk here is a texture whose owner skipped the recreate contract -- a programmer error. */
    NT_ASSERT(s_gfx.texture_metas[slot].live && "update_texture: texture has no live backend -- recreate it after context restore");
    nt_gfx_backend_update_texture(slot, x, y, w, h, (nt_texture_format_t)stored_format, data);
    return NT_GFX_RESULT_ACCEPTED;
}

void nt_gfx_update_texture(nt_texture_t tex, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const void *data) {
    NT_GFX_BEGIN_REQUEST(NT_GFX_OP_TEXTURE_UPLOAD, NT_GFX_OBJECT_TEXTURE, tex.id, event->data.state.integers[0] = x; event->data.state.integers[1] = y; event->data.state.integers[2] = w;
                         event->data.state.integers[3] = h);
    NT_GFX_END(update_texture(tex, x, y, w, h, data));
}

/* ---- Mesh side table helpers ---- */

/* ---- Asset activators ---- */

/* Per-codec order: ETC1S unpacks into ETC2 exactly (opaque at half the bytes of BC7/ASTC);
 * UASTC is a subset of ASTC, BC7 approximates it and an ETC2 target re-encodes it. */
static nt_texture_format_t basis_target_format(const nt_gfx_gpu_caps_t *caps, nt_basisu_codec_t codec, bool has_alpha, uint32_t width, uint32_t height) {
    /* WebGL BPTC requires block-aligned level-0 dimensions; the smaller levels of a halved chain are accepted as they come. */
    const bool bc7 = NT_BASISU_HAS_BC7 && caps->has_bc7 && width % 4 == 0 && height % 4 == 0;
    const bool astc = NT_BASISU_HAS_ASTC && caps->has_astc;
    const bool etc2 = NT_BASISU_HAS_ETC2 && caps->has_etc2;
    const nt_texture_format_t etc2_format = has_alpha ? NT_TEXTURE_FORMAT_ETC2_RGBA8 : NT_TEXTURE_FORMAT_ETC2_RGB8;
    if (codec == NT_BASISU_CODEC_ETC1S) {
        if (etc2) {
            return etc2_format;
        }
        if (bc7) {
            return NT_TEXTURE_FORMAT_BC7_RGBA;
        }
        if (astc) {
            return NT_TEXTURE_FORMAT_ASTC_4x4_RGBA;
        }
        return NT_TEXTURE_FORMAT_RGBA8;
    }
    NT_ASSERT(codec == NT_BASISU_CODEC_UASTC_LDR);
    if (astc) {
        return NT_TEXTURE_FORMAT_ASTC_4x4_RGBA;
    }
    if (bc7) {
        return NT_TEXTURE_FORMAT_BC7_RGBA;
    }
    if (etc2) {
        return etc2_format;
    }
    return NT_TEXTURE_FORMAT_RGBA8;
}

/* Activate a v2 texture (RAW or Basis Universal compressed) */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
uint32_t nt_gfx_activate_texture(const uint8_t *data, uint32_t size) {
    /* make_texture would refuse the result anyway; bail before transcoding
     * the whole chain into staging. */
    if (g_nt_gfx.context_lost) {
        return 0;
    }
    const NtTextureAssetHeaderV2 *hdr2 = (const NtTextureAssetHeaderV2 *)data;

    // #region header (pack invariants assert; device limits reject)
    NT_ASSERT(nt_texture_pixel_format_valid((nt_texture_pixel_format_t)hdr2->format) && "texture pixel format invalid");
    NT_ASSERT(hdr2->default_min_filter <= NT_FILTER_LINEAR_MIPMAP_LINEAR && hdr2->default_mag_filter <= NT_FILTER_LINEAR && hdr2->default_wrap_u <= NT_WRAP_MIRRORED_REPEAT &&
              hdr2->default_wrap_v <= NT_WRAP_MIRRORED_REPEAT && "texture sampler defaults out of range");
    if (hdr2->width > g_nt_gfx.gpu_caps.max_texture_size || hdr2->height > g_nt_gfx.gpu_caps.max_texture_size) {
        NT_LOG_ERROR("activate_texture: %ux%u exceeds GPU max_texture_size %u", hdr2->width, hdr2->height, g_nt_gfx.gpu_caps.max_texture_size);
        return 0;
    }
    NT_ASSERT(hdr2->width != 0 && hdr2->height != 0 && hdr2->width <= UINT16_MAX && hdr2->height <= UINT16_MAX && "texture dimensions out of range");
    /* Subtraction safe: the resource core checked size >= sizeof header. */
    NT_ASSERT(hdr2->data_size <= size - sizeof(NtTextureAssetHeaderV2) && "texture blob truncated");
    const uint16_t width = (uint16_t)hdr2->width;
    const uint16_t height = (uint16_t)hdr2->height;
    // #endregion

    /* RAW compression: uncompressed pixel data after header */
    if (hdr2->compression == NT_TEXTURE_COMPRESSION_RAW) {
        nt_texture_format_t pixel_fmt = (nt_texture_format_t)hdr2->format;
        uint64_t expected = (uint64_t)hdr2->width * (uint64_t)hdr2->height * nt_texture_bpp(pixel_fmt);
        NT_ASSERT(expected <= UINT32_MAX && hdr2->data_size >= (uint32_t)expected && "texture RAW data_size below its pixels");
        nt_texture_desc_t desc = {
            .width = width,
            .height = height,
            .data = data + sizeof(NtTextureAssetHeaderV2),
            .format = pixel_fmt,
            .min_filter = (nt_texture_filter_t)hdr2->default_min_filter,
            .mag_filter = (nt_texture_filter_t)hdr2->default_mag_filter,
            .wrap_u = (nt_texture_wrap_t)hdr2->default_wrap_u,
            .wrap_v = (nt_texture_wrap_t)hdr2->default_wrap_v,
            .gen_mipmaps = (hdr2->flags & NT_TEXTURE_FLAG_GEN_MIPMAPS) != 0,
            .label = NULL,
        };
        return nt_gfx_make_texture(&desc).id;
    }

    NT_ASSERT(hdr2->compression == NT_TEXTURE_COMPRESSION_BASIS && "texture compression unknown");

    /* Lazy transcoder init */
    if (!s_transcoder_initialized) {
        nt_basisu_transcoder_global_init();
        s_transcoder_initialized = true;
    }

    const uint8_t *basis_data = data + sizeof(NtTextureAssetHeaderV2);
    uint32_t basis_size = hdr2->data_size;

    // #region blob cross-check
    nt_basisu_info_t info;
    if (!nt_basisu_info(basis_data, basis_size, &info)) {
        NT_LOG_ERROR("activate_texture: Basis blob rejected (bad data, unsupported codec or stub transcoder linked)");
        return 0;
    }
    NT_ASSERT(info.width == hdr2->width && info.height == hdr2->height && "texture header size differs from its Basis blob");
    NT_ASSERT(info.level_count == hdr2->mip_count && info.level_count == nt_texture_full_chain_levels(width, height) && "texture Basis chain is not the full mip chain");
    // #endregion

    // #region target and staging
    const nt_texture_format_t target = basis_target_format(nt_gfx_gpu_caps(), info.codec, info.has_alpha, width, height);

    uint64_t chain_bytes = 0;
    for (uint32_t level = 0; level < info.level_count; level++) {
        chain_bytes += nt_texture_level_bytes(target, nt_texture_level_extent(width, level), nt_texture_level_extent(height, level));
    }
    /* Only the RGBA8 fallback can reach 2^32, at 32768x32768. */
    if (chain_bytes > UINT32_MAX) {
        NT_LOG_ERROR("activate_texture: the chain needs %llu bytes, more than a uint32 holds", (unsigned long long)chain_bytes);
        return 0;
    }
    uint8_t *stage = stage_acquire((uint32_t)chain_bytes);
    if (stage == NULL) {
        NT_LOG_ERROR("activate_texture: staging allocation of %u bytes failed", (uint32_t)chain_bytes);
        return 0;
    }
    // #endregion

    // #region transcode the whole chain into staging
    if (!nt_basisu_transcode_chain(basis_data, basis_size, &info, target, stage, (uint32_t)chain_bytes)) {
        NT_LOG_ERROR("activate_texture: Basis transcode of the %u-level chain failed", (unsigned)info.level_count);
        return 0;
    }
    // #endregion

    nt_texture_desc_t desc = {
        .width = width,
        .height = height,
        .data = stage,
        .format = target,
        .min_filter = (nt_texture_filter_t)hdr2->default_min_filter,
        .mag_filter = (nt_texture_filter_t)hdr2->default_mag_filter,
        .wrap_u = (nt_texture_wrap_t)hdr2->default_wrap_u,
        .wrap_v = (nt_texture_wrap_t)hdr2->default_wrap_v,
        .level_count = (uint8_t)info.level_count,
        .gen_mipmaps = false,
        .label = NULL,
    };
    return nt_gfx_make_texture(&desc).id;
}

/* Per-stream pack invariants: type/count/normalized validity, unique locations, and the
 * WebGL2 offset/stride alignment rules. Returns the vertex stride.
 * Out of line: inlined, -O3 unrolls its loop into the caller for ~0.7 KB of wasm. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static NT_NOINLINE uint32_t mesh_stream_stride(const NtStreamDesc *streams, uint8_t stream_count) {
    uint32_t stride = 0;
    uint32_t location_mask = 0;
    for (uint8_t i = 0; i < stream_count; i++) {
        NT_ASSERT(streams[i].location < NT_GFX_MAX_VERTEX_ATTRS && (location_mask & (1U << streams[i].location)) == 0 && "mesh stream location out of range or duplicated");
        location_mask |= 1U << streams[i].location;
        NT_ASSERT(nt_stream_type_size(streams[i].type) != 0 && streams[i].count >= 1 && streams[i].count <= 4 && "mesh stream type/count invalid");
        NT_ASSERT((streams[i].normalized == 0 || (streams[i].type != NT_STREAM_FLOAT32 && streams[i].type != NT_STREAM_FLOAT16)) && "mesh stream normalized on a float type");
        NT_ASSERT(stride % nt_stream_type_size(streams[i].type) == 0 && "mesh stream offset misaligned");
        stride += nt_stream_type_size(streams[i].type) * streams[i].count;
    }
    for (uint8_t i = 0; i < stream_count; i++) {
        NT_ASSERT(stride % nt_stream_type_size(streams[i].type) == 0 && "mesh stride misaligned for a stream type");
    }
    return stride;
}

/* Pack invariants for a mesh blob; the builder writes them, the resource core checked the header.
 * 64-bit sums so a size field cannot wrap a check into a pass. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void mesh_blob_check(const uint8_t *data, uint32_t size) {
    const NtMeshAssetHeader *hdr = (const NtMeshAssetHeader *)data;
    NT_ASSERT(hdr->index_type <= 2 && hdr->vertex_wire <= NT_MESH_WIRE_VTX_SOA && hdr->index_wire <= NT_MESH_WIRE_IDX_MESHOPT && "mesh index type or wire tag invalid");
    NT_ASSERT(hdr->stream_count != 0 && hdr->stream_count <= NT_MESH_MAX_STREAMS && "mesh stream_count invalid");
    const uint32_t streams_size = (uint32_t)hdr->stream_count * (uint32_t)sizeof(NtStreamDesc);
    NT_ASSERT((uint64_t)sizeof(NtMeshAssetHeader) + streams_size + hdr->vertex_data_size + hdr->index_data_size <= size && "mesh blob truncated");
    const uint32_t stride = mesh_stream_stride((const NtStreamDesc *)(data + sizeof(NtMeshAssetHeader)), hdr->stream_count);
    NT_ASSERT((uint64_t)hdr->vertex_count * stride == hdr->vertex_data_size && "mesh vertex_data_size != vertex_count * stride");
    static const uint32_t idx_elem_sizes[3] = {0, 2, 4};
    const uint32_t idx_elem = idx_elem_sizes[hdr->index_type];
    NT_ASSERT((hdr->index_type != 0 || hdr->index_count == 0) && "mesh index_count with index_type none");
    if (hdr->index_wire == NT_MESH_WIRE_IDX_MESHOPT) {
        NT_ASSERT(hdr->index_type != 0 && hdr->index_count != 0 && hdr->index_count % 3 == 0 && "mesh MESHOPT wire without triangle indices");
        /* Decoded size feeds malloc, and a wasm size_t wraps at 4 GB. */
        const uint64_t decoded_size = (uint64_t)hdr->index_count * idx_elem;
        NT_ASSERT(decoded_size <= UINT32_MAX && hdr->index_data_size != 0 && hdr->index_data_size <= decoded_size && "mesh MESHOPT index sizes inconsistent");
        /* Codec minimum (header + 1 code byte per triangle + 16-byte tail) bounds the decode allocation. */
        NT_ASSERT(hdr->index_data_size >= 1ULL + (hdr->index_count / 3) + 16ULL && "mesh MESHOPT index_data_size below codec minimum");
    } else {
        NT_ASSERT((uint64_t)hdr->index_count * idx_elem == hdr->index_data_size && "mesh index_data_size != index_count * element size");
    }
    /* GL draws meshes only as GL_TRIANGLES; a trailing partial triangle would be dropped. */
    const uint32_t draw_count = (hdr->index_count > 0) ? hdr->index_count : hdr->vertex_count;
    NT_ASSERT(draw_count % 3 == 0 && "mesh draw count is not a multiple of 3");
}

/* Uploads the IBO from the wire index block (decoding MESHOPT first).
 * *out_ibo stays {0} for non-indexed meshes; returns false on failure
 * (nothing left to clean up). */
static bool mesh_make_ibo(const NtMeshAssetHeader *hdr, const uint8_t *index_data, nt_buffer_t *out_ibo) {
    *out_ibo = (nt_buffer_t){0};
    if (hdr->index_type == 0 || hdr->index_count == 0) {
        return true;
    }
    uint32_t idx_elem = (hdr->index_type == 1) ? 2U : 4U;
    const uint8_t *gpu_index_data = index_data;
    uint32_t gpu_index_size = hdr->index_data_size;
    if (hdr->index_wire == NT_MESH_WIRE_IDX_MESHOPT) {
        gpu_index_size = hdr->index_count * idx_elem; /* fits u32 -- validated */
        uint8_t *idx_tmp = stage_acquire(gpu_index_size);
        if (!idx_tmp) {
            NT_LOG_ERROR("activate_mesh: index decode alloc failed (%u bytes)", gpu_index_size);
            return false;
        }
        const bool decoded = nt_meshwire_decode_indices(idx_tmp, hdr->index_count, idx_elem, index_data, hdr->index_data_size, hdr->vertex_count);
        NT_ASSERT(decoded && "mesh MESHOPT index stream corrupt");
        gpu_index_data = idx_tmp;
    }
    *out_ibo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_INDEX,
        .usage = NT_USAGE_IMMUTABLE,
        .data = gpu_index_data,
        .size = gpu_index_size,
        .index_type = hdr->index_type,
        .label = NULL,
    });
    if (out_ibo->id == 0) {
        return false; /* only a loss fails make_buffer, and it latched */
    }
    return true;
}

/* A failed activation frees the buffers it made; no recorded command can name them yet, so the frame rule does not apply. */
static void destroy_unpublished_buffer(nt_buffer_t buf) {
    NT_GFX_BEGIN(NT_GFX_OP_DESTROY, NT_GFX_OBJECT_BUFFER, buf.id);
    NT_GFX_END(destroy_buffer(buf));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
uint32_t nt_gfx_activate_mesh(const uint8_t *data, uint32_t size) {
    mesh_blob_check(data, size);
    const NtMeshAssetHeader *hdr = (const NtMeshAssetHeader *)data;
    uint32_t streams_size = (uint32_t)hdr->stream_count * (uint32_t)sizeof(NtStreamDesc);
    const uint8_t *vertex_data = data + sizeof(NtMeshAssetHeader) + streams_size;
    const uint8_t *index_data = vertex_data + hdr->vertex_data_size;

    /* Wire → GPU decode through the shared staging buffer (activation is the
       amortized load path; the same buffer then serves the index decode --
       make_buffer has copied the vertices to the GPU by that point). */
    const uint8_t *gpu_vertex_data = vertex_data;
    if (hdr->vertex_wire == NT_MESH_WIRE_VTX_SOA && hdr->vertex_data_size > 0) {
        uint8_t *soa_tmp = stage_acquire(hdr->vertex_data_size);
        if (!soa_tmp) {
            NT_LOG_ERROR("activate_mesh: SOA decode alloc failed (%u bytes)", hdr->vertex_data_size);
            return 0;
        }
        const NtStreamDesc *streams = (const NtStreamDesc *)(data + sizeof(NtMeshAssetHeader));
        uint32_t elem_sizes[NT_MESH_MAX_STREAMS];
        for (uint8_t i = 0; i < hdr->stream_count; i++) {
            elem_sizes[i] = nt_stream_type_size(streams[i].type) * streams[i].count;
        }
        const bool interleaved = nt_meshwire_reinterleave(soa_tmp, vertex_data, hdr->vertex_count, elem_sizes, hdr->stream_count);
        NT_ASSERT(interleaved && "mesh SOA streams inconsistent");
        gpu_vertex_data = soa_tmp;
    }

    nt_buffer_t vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_VERTEX,
        .usage = NT_USAGE_IMMUTABLE,
        .data = gpu_vertex_data,
        .size = hdr->vertex_data_size,
        .label = NULL,
    });
    if (vbo.id == 0) {
        return 0; /* only a loss fails make_buffer, and it latched */
    }

    nt_buffer_t ibo;
    if (!mesh_make_ibo(hdr, index_data, &ibo)) {
        destroy_unpublished_buffer(vbo);
        return 0;
    }

    uint32_t mesh_id = nt_pool_alloc(&s_gfx.mesh_pool);
    NT_ASSERT(mesh_id != 0 && "activate_mesh: mesh pool full -- raise nt_gfx_desc_t.max_meshes");

    uint32_t slot = nt_pool_slot_index(mesh_id);
    memset(&s_gfx.mesh_table[slot], 0, sizeof(nt_gfx_mesh_info_t));
    memset(&s_gfx.mesh_vertex_inputs[(size_t)slot * NT_GFX_MESH_VERTEX_INPUTS], 0, NT_GFX_MESH_VERTEX_INPUTS * sizeof(nt_vertex_input_t));

    s_gfx.mesh_table[slot].vbo = vbo;
    s_gfx.mesh_table[slot].ibo = ibo;
    s_gfx.mesh_table[slot].vertex_count = hdr->vertex_count;
    s_gfx.mesh_table[slot].index_count = hdr->index_count;
    s_gfx.mesh_table[slot].stream_count = hdr->stream_count;
    s_gfx.mesh_table[slot].index_type = hdr->index_type;

    /* Copy stream descriptors from pack data for render module vertex layout building */
    const NtStreamDesc *src_streams = (const NtStreamDesc *)(data + sizeof(NtMeshAssetHeader));
    memcpy(s_gfx.mesh_table[slot].streams, src_streams, (size_t)hdr->stream_count * sizeof(NtStreamDesc));

    /* Compute stride: sum of all stream sizes (type_size * count) */
    uint16_t stride = 0;
    for (uint8_t i = 0; i < hdr->stream_count; i++) {
        stride += (uint16_t)(nt_stream_type_size(src_streams[i].type) * src_streams[i].count);
    }
    s_gfx.mesh_table[slot].stride = stride;

    return mesh_id;
}

uint32_t nt_gfx_activate_shader(const uint8_t *data, uint32_t size) {
    const NtShaderCodeHeader *hdr = (const NtShaderCodeHeader *)data;
    NT_ASSERT(hdr->stage <= NT_SHADER_STAGE_FRAGMENT && "shader stage invalid");
    /* 64-bit sum: a corrupt code_size must not wrap the truncation check into a pass */
    NT_ASSERT((uint64_t)sizeof(NtShaderCodeHeader) + hdr->code_size <= size && "shader blob truncated");
    /* The source is consumed as a C string -- the builder always writes the trailing NUL */
    NT_ASSERT(hdr->code_size != 0 && data[sizeof(NtShaderCodeHeader) + hdr->code_size - 1] == 0 && "shader source not NUL-terminated");
    const char *source = (const char *)(data + sizeof(NtShaderCodeHeader));
    nt_shader_type_t type = (hdr->stage == NT_SHADER_STAGE_VERTEX) ? NT_SHADER_VERTEX : NT_SHADER_FRAGMENT;
    nt_shader_t shd = nt_gfx_make_shader(&(nt_shader_desc_t){
        .type = type,
        .source = source,
        .label = NULL,
    });
    return shd.id;
}

/* ---- Asset deactivators ---- */

void nt_gfx_deactivate_texture(uint32_t handle) {
    nt_texture_t tex = {.id = handle};
    nt_gfx_destroy_texture(tex);
}

void nt_gfx_deactivate_mesh(uint32_t handle) {
    if (handle == 0) {
        return;
    }
    if (!nt_pool_valid(&s_gfx.mesh_pool, handle)) {
        NT_LOG_ERROR("deactivate_mesh: invalid or stale handle");
        return;
    }
    uint32_t index = nt_pool_slot_index(handle);
    /* The vertex buffer's destroy cascade takes the mesh's vertex inputs along. */
    nt_gfx_destroy_buffer(s_gfx.mesh_table[index].vbo);
    if (s_gfx.mesh_table[index].ibo.id != 0) {
        nt_gfx_destroy_buffer(s_gfx.mesh_table[index].ibo);
    }
    memset(&s_gfx.mesh_table[index], 0, sizeof(nt_gfx_mesh_info_t));
    nt_pool_free(&s_gfx.mesh_pool, handle);
}

void nt_gfx_deactivate_shader(uint32_t handle) {
    nt_shader_t shd = {.id = handle};
    nt_gfx_destroy_shader(shd);
}

/* ---- Mesh info query ---- */

const nt_gfx_mesh_info_t *nt_gfx_get_mesh_info(nt_mesh_t mesh) {
    if (!nt_pool_valid(&s_gfx.mesh_pool, mesh.id)) {
        return NULL;
    }
    uint32_t index = nt_pool_slot_index(mesh.id);
    return &s_gfx.mesh_table[index];
}

/* Every stream at the location the builder resolved; activation checked the types. */
static nt_vertex_input_t make_mesh_vertex_input(const nt_gfx_mesh_info_t *info) {
    static const nt_vertex_type_t k_types[] = {
        [NT_STREAM_UINT8] = NT_VERTEX_UINT8, [NT_STREAM_INT8] = NT_VERTEX_INT8,    [NT_STREAM_UINT16] = NT_VERTEX_UINT16,
        [NT_STREAM_INT16] = NT_VERTEX_INT16, [NT_STREAM_FLOAT16] = NT_VERTEX_HALF, [NT_STREAM_FLOAT32] = NT_VERTEX_FLOAT,
    };
    nt_vertex_input_desc_t desc = {.layout = {.attr_count = info->stream_count, .stride = info->stride}, .vertex_buffer = info->vbo, .index_buffer = info->ibo, .label = "mesh"};
    uint16_t offset = 0;
    for (uint8_t i = 0; i < info->stream_count; i++) {
        const NtStreamDesc *stream = &info->streams[i];
        desc.layout.attrs[i] = (nt_vertex_attr_t){.location = stream->location, .type = k_types[stream->type], .count = stream->count, .normalized = stream->normalized != 0, .offset = offset};
        offset = (uint16_t)(offset + (nt_stream_type_size(stream->type) * stream->count));
    }
    return nt_gfx_make_vertex_input(&desc);
}

nt_vertex_input_t nt_gfx_mesh_vertex_input(nt_mesh_t mesh, nt_buffer_t instance_buffer) {
    NT_ASSERT(nt_pool_valid(&s_gfx.mesh_pool, mesh.id) && "mesh_vertex_input: invalid or stale mesh");
    NT_ASSERT(nt_pool_valid(&s_gfx.buffer_pool, instance_buffer.id) && "mesh_vertex_input: invalid or stale instance buffer (an unsized frame stream has none)");
    const uint32_t slot = nt_pool_slot_index(mesh.id);
    const uint32_t frame_stream = s_gfx.buffer_metas[nt_pool_slot_index(instance_buffer.id)].frame_stream;
    const uint32_t k = frame_stream > NT_GFX_FRAME_VERTEX ? frame_stream - 1U - NT_GFX_FRAME_VERTEX : NT_GFX_MAX_VERTEX_STREAMS;
    nt_vertex_input_t *vi = &s_gfx.mesh_vertex_inputs[((size_t)slot * NT_GFX_MESH_VERTEX_INPUTS) + k];
    /* A loss frees every vertex input; the next use after the restore makes it again. */
    if (!nt_pool_valid(&s_gfx.vertex_input_pool, vi->id)) {
        *vi = make_mesh_vertex_input(&s_gfx.mesh_table[slot]);
    }
    return *vi;
}
