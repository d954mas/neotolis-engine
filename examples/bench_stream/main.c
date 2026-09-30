/* Dynamic-upload lifetime benchmark (#351).
 *
 * Measures what it costs to write a buffer that an earlier draw of the same
 * frame already read. Each frame runs `episodes` upload->draw pairs, the shape of
 * repeated mesh draw_list calls (instanced family) or sprite/text flushes (batch
 * family), and each arm stores those uploads differently. Arms run in ABBA order
 * per case; every measured window is reported as one JSON line.
 *
 * Config: `bench_stream.cfg` (key=value lines, see s_cfg defaults). Web fetches
 * it next to the page and POSTs each window to `results`; native reads argv[1]
 * and appends to argv[2] (default bench_stream_results.jsonl). */

#include "app/nt_app.h"
#include "core/nt_core.h"
#include "core/nt_platform.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "http/nt_http.h"
#include "input/nt_input.h"
#include "time/nt_time.h"
#include "window/nt_window.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef NT_PLATFORM_WEB
#include "platform/web/nt_platform_web.h"
#endif

#define MAX_EPISODES 128
#define MAX_INSTANCES 1024 /* per episode */
#define MAX_QUADS 128      /* per episode; MAX_EPISODES * MAX_QUADS * 4 vertices fit uint16 indices */
#define MAX_LIST 8
#define RUNS_PER_EPISODE 4 /* instanced draws per upload, like mesh runs of one draw_list chunk */
#define CMDS_PER_EPISODE 2 /* indexed draws per upload, like sprite cmds of one flush */
#define INST_STRIDE 64     /* mat4x3 + color, NT_INSTANCE_STRIDE_MAX */
#define HIST_BINS 20000    /* 25 us bins up to 500 ms */
#define HIST_BIN_NS 25000ULL

_Static_assert(MAX_EPISODES *MAX_QUADS * 4 <= 65536, "batch append arms need uint16 indices");

typedef struct {
    float row[3][4];
    float color[4];
} inst_t;
_Static_assert(sizeof(inst_t) == INST_STRIDE, "instance stride");

typedef struct {
    float pos[3];
    uint8_t color[4];
} batch_vtx_t;

typedef enum { FAMILY_INST, FAMILY_BATCH } family_t;

/* Instanced arms. RING = master's append into one buffer between draws. */
/* FRAMEn = a prepare phase: the whole frame's instance data in one upload before the first draw, into one of n
 * buffers rotated per frame (STREAM; FRAME3_DYN = same with DYNAMIC). */
typedef enum {
    INST_RING,
    INST_RING_DYN,
    INST_POOL,
    INST_POOL_STREAM,
    INST_ORPHAN,
    INST_UPFRONT,
    INST_RING_PERSIST, /* master's ring: the cursor survives frames and wraps at 4 frames of data */
    INST_FRAME1,
    INST_FRAME2,
    INST_FRAME3,
    INST_FRAME3_DYN,
    INST_FRAME8,
    INST_FRAME1_SYNC_START, /* frame1 + a deliberate wait on the previous frame at frame start */
    INST_FRAME1_SYNC_MID,   /* frame1 + the same wait after the load draws, where ring's first upload waits */
    INST_ARM_COUNT
} inst_arm_t;
static const char *const s_inst_arm_names[INST_ARM_COUNT] = {"ring",   "ring_dyn", "pool",   "pool_stream", "orphan", "upfront",           "ring_persist",
                                                             "frame1", "frame2",   "frame3", "frame3_dyn",  "frame8", "frame1_sync_start", "frame1_sync_mid"};

/* Batch arms over a vertex input with baked VBO+IBO. ZERO rewrites a buffer sized to one upload; ZERO_PART rewrites
 * the start of a large one, like the shape batch; ORPHAN = sprite/text. */
typedef enum { BATCH_ZERO, BATCH_ZERO_PART, BATCH_ORPHAN, BATCH_POOL, BATCH_APPEND, BATCH_UPFRONT, BATCH_ARM_COUNT } batch_arm_t;
static const char *const s_batch_arm_names[BATCH_ARM_COUNT] = {"zero", "zero_part", "orphan", "pool", "append", "upfront"};

typedef struct {
    uint32_t families; /* bit per family_t */
    uint32_t episodes[MAX_LIST];
    uint32_t episode_count;
    uint32_t instances[MAX_LIST];
    uint32_t instance_count;
    uint32_t quads[MAX_LIST];
    uint32_t quad_count;
    uint32_t inst_arms; /* bit per inst_arm_t */
    uint32_t batch_arms;
    uint32_t reps; /* ABBA passes per case */
    uint32_t load; /* blended fullscreen triangles per frame, keeps the GPU busy like a real scene */
    float warmup_s;
    float window_s;
} bench_cfg_t;

static bench_cfg_t s_cfg = {
    .families = (1U << FAMILY_INST) | (1U << FAMILY_BATCH),
    .episodes = {1, 5, 20, 120},
    .episode_count = 4,
    .instances = {64, 1024},
    .instance_count = 2,
    .quads = {32, 128},
    .quad_count = 2,
    .inst_arms = (1U << INST_ARM_COUNT) - 1U,
    .batch_arms = (1U << BATCH_ARM_COUNT) - 1U,
    .reps = 1,
    .load = 0,
    .warmup_s = 1.0F,
    .window_s = 4.0F,
};

/* One scheduled window. */
typedef struct {
    family_t family;
    uint32_t arm;
    uint32_t episodes;
    uint32_t size; /* instances or quads per episode */
    uint32_t rep;
} window_t;

#define MAX_WINDOWS 1024
static window_t s_windows[MAX_WINDOWS];
static uint32_t s_window_count;
static uint32_t s_window_index;

typedef enum { STAGE_CONFIG, STAGE_RUN, STAGE_DONE } stage_t;
static stage_t s_stage;

/* Static GPU objects */
static nt_pipeline_t s_inst_pip;
static nt_pipeline_t s_batch_pip;
static nt_pipeline_t s_load_pip;
static nt_buffer_t s_cube_vbo;
static nt_buffer_t s_cube_ibo;
static nt_vertex_input_t s_load_vi;
static nt_hash32_t s_u_vp;

/* Per-window GPU objects, created at window start outside the measured range */
static nt_buffer_t s_bufs[MAX_EPISODES];
static nt_buffer_t s_ibufs[MAX_EPISODES];
static nt_vertex_input_t s_vis[MAX_EPISODES];
static uint32_t s_buf_count;
static uint32_t s_vi_count;

/* CPU payloads: every episode uploads the same bytes; only where they land differs per arm.
 * Indices are per episode because the append arms rebase them into one shared VBO. */
static inst_t s_inst_data[MAX_INSTANCES];
static inst_t s_frame_arena[MAX_EPISODES * MAX_INSTANCES];
static uint32_t s_ring_persist_cursor;
/* SYNC arms: the frame's last draw reads this buffer; a partial write to it waits for that draw (a full-size
 * write would be renamed by Chrome and wait for nothing). */
static nt_buffer_t s_fence_buf; /* FRAMEn: the whole frame packed contiguously */
static batch_vtx_t s_batch_vtx[MAX_QUADS * 4];
static uint16_t s_batch_idx[MAX_EPISODES * MAX_QUADS * 6];

/* Window measurement */
static uint32_t s_hist[HIST_BINS];
static uint64_t s_prev_ns;
static double s_window_start;
static bool s_measuring;
static uint32_t s_frames;
static uint64_t s_sum_ns;
static uint64_t s_sum_gpu_ns;
static uint32_t s_gpu_samples;
static nt_gfx_counters_t s_sum; /* summed last_frame counters over measured frames */
static uint32_t s_frame_counter;

/* Result transport */
#ifndef NT_PLATFORM_WEB
static FILE *s_out_file;
#endif
static nt_http_request_t s_pending[16];
static nt_http_request_t s_cfg_req;

// #region shaders
static const char *s_inst_vs = "precision highp float;\n"
                               "layout(location = 0) in vec3 a_pos;\n"
                               "layout(location = 4) in vec4 i_r0;\n"
                               "layout(location = 5) in vec4 i_r1;\n"
                               "layout(location = 6) in vec4 i_r2;\n"
                               "layout(location = 7) in vec4 i_color;\n"
                               "uniform mat4 u_vp;\n"
                               "out vec4 v_color;\n"
                               "void main() {\n"
                               "    vec4 p = vec4(a_pos, 1.0);\n"
                               "    gl_Position = u_vp * vec4(dot(i_r0, p), dot(i_r1, p), dot(i_r2, p), 1.0);\n"
                               "    v_color = i_color;\n"
                               "}\n";

static const char *s_batch_vs = "precision highp float;\n"
                                "layout(location = 0) in vec3 a_pos;\n"
                                "layout(location = 2) in vec4 a_color;\n"
                                "uniform mat4 u_vp;\n"
                                "out vec4 v_color;\n"
                                "void main() {\n"
                                "    gl_Position = u_vp * vec4(a_pos, 1.0);\n"
                                "    v_color = a_color;\n"
                                "}\n";

static const char *s_color_fs = "precision mediump float;\n"
                                "in vec4 v_color;\n"
                                "out vec4 o_color;\n"
                                "void main() { o_color = v_color; }\n";

/* Fullscreen triangle from gl_VertexID; blended so Mali forward pixel kill cannot drop it. */
static const char *s_load_vs = "precision highp float;\n"
                               "void main() {\n"
                               "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
                               "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
                               "}\n";

static const char *s_load_fs = "precision mediump float;\n"
                               "out vec4 o_color;\n"
                               "void main() { o_color = vec4(0.02, 0.02, 0.03, 0.05); }\n";
// #endregion

// #region config
static uint32_t parse_list(const char *value, uint32_t *out, uint32_t max) {
    uint32_t n = 0;
    const char *p = value;
    while (*p != '\0' && n < max) {
        char *end = NULL;
        unsigned long v = strtoul(p, &end, 10);
        if (end == p) {
            break;
        }
        out[n++] = (uint32_t)v;
        p = (*end == ',') ? end + 1 : end;
    }
    return n;
}

static uint32_t parse_names(const char *value, const char *const *names, uint32_t count) {
    uint32_t mask = 0;
    for (uint32_t i = 0; i < count; i++) {
        const size_t len = strlen(names[i]);
        const char *hit = strstr(value, names[i]);
        while (hit != NULL) {
            const bool starts = hit == value || hit[-1] == ',';
            const bool ends = hit[len] == '\0' || hit[len] == ',' || hit[len] == '\r' || hit[len] == '\n';
            if (starts && ends) {
                mask |= 1U << i;
                break;
            }
            hit = strstr(hit + 1, names[i]);
        }
    }
    return mask;
}

static void parse_config(char *text) {
    static const char *const family_names[] = {"inst", "batch"};
    for (char *line = text, *next = NULL; line != NULL; line = next) {
        next = strchr(line, '\n');
        if (next != NULL) {
            *next++ = '\0';
        }
        char *eq = strchr(line, '=');
        if (line[0] == '#' || eq == NULL) {
            continue;
        }
        *eq = '\0';
        const char *key = line;
        const char *value = eq + 1;
        if (strcmp(key, "families") == 0) {
            s_cfg.families = parse_names(value, family_names, 2);
        } else if (strcmp(key, "episodes") == 0) {
            s_cfg.episode_count = parse_list(value, s_cfg.episodes, MAX_LIST);
        } else if (strcmp(key, "instances") == 0) {
            s_cfg.instance_count = parse_list(value, s_cfg.instances, MAX_LIST);
        } else if (strcmp(key, "quads") == 0) {
            s_cfg.quad_count = parse_list(value, s_cfg.quads, MAX_LIST);
        } else if (strcmp(key, "inst_arms") == 0) {
            s_cfg.inst_arms = parse_names(value, s_inst_arm_names, INST_ARM_COUNT);
        } else if (strcmp(key, "batch_arms") == 0) {
            s_cfg.batch_arms = parse_names(value, s_batch_arm_names, BATCH_ARM_COUNT);
        } else if (strcmp(key, "reps") == 0) {
            s_cfg.reps = (uint32_t)strtoul(value, NULL, 10);
        } else if (strcmp(key, "load") == 0) {
            s_cfg.load = (uint32_t)strtoul(value, NULL, 10);
        } else if (strcmp(key, "warmup") == 0) {
            s_cfg.warmup_s = strtof(value, NULL);
        } else if (strcmp(key, "window") == 0) {
            s_cfg.window_s = strtof(value, NULL);
        } else {
            printf("[bench] unknown config key '%s'\n", key);
        }
    }
}

static uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi) {
    if (v < lo) {
        return lo;
    }
    return v > hi ? hi : v;
}

static void push_window(family_t family, uint32_t arm, uint32_t episodes, uint32_t size, uint32_t rep) {
    if (s_window_count == MAX_WINDOWS) {
        return;
    }
    s_windows[s_window_count++] = (window_t){.family = family, .arm = arm, .episodes = episodes, .size = size, .rep = rep};
}

/* ABBA: every case runs its arms forward then backward, so drift (thermal, clocks) cancels in the pair. */
static void push_case(family_t family, uint32_t arm_mask, uint32_t arm_count, uint32_t episodes, uint32_t size) {
    for (uint32_t rep = 0; rep < s_cfg.reps; rep++) {
        for (uint32_t a = 0; a < arm_count; a++) {
            if (arm_mask & (1U << a)) {
                push_window(family, a, episodes, size, rep);
            }
        }
        for (uint32_t a = arm_count; a-- > 0;) {
            if (arm_mask & (1U << a)) {
                push_window(family, a, episodes, size, rep);
            }
        }
    }
}

static void build_schedule(void) {
    s_window_count = 0;
    for (uint32_t e = 0; e < s_cfg.episode_count; e++) {
        const uint32_t episodes = clamp_u32(s_cfg.episodes[e], 1, MAX_EPISODES);
        if (s_cfg.families & (1U << FAMILY_INST)) {
            for (uint32_t i = 0; i < s_cfg.instance_count; i++) {
                const uint32_t n = clamp_u32(s_cfg.instances[i], RUNS_PER_EPISODE, MAX_INSTANCES) / RUNS_PER_EPISODE * RUNS_PER_EPISODE;
                push_case(FAMILY_INST, s_cfg.inst_arms, INST_ARM_COUNT, episodes, n);
            }
        }
        if (s_cfg.families & (1U << FAMILY_BATCH)) {
            for (uint32_t q = 0; q < s_cfg.quad_count; q++) {
                const uint32_t n = clamp_u32(s_cfg.quads[q], CMDS_PER_EPISODE, MAX_QUADS) / CMDS_PER_EPISODE * CMDS_PER_EPISODE;
                push_case(FAMILY_BATCH, s_cfg.batch_arms, BATCH_ARM_COUNT, episodes, n);
            }
        }
    }
    const float seconds = (float)s_window_count * (s_cfg.warmup_s + s_cfg.window_s);
    printf("[bench] %u windows, ~%.0f s, load=%u\n", s_window_count, (double)seconds, s_cfg.load);
}
// #endregion

// #region payloads
static float hash01(uint32_t i) {
    uint32_t h = i * 2654435761U;
    h ^= h >> 15;
    h *= 0x2c1b3c6dU;
    h ^= h >> 12;
    return (float)(h & 0xFFFFU) / 65535.0F;
}

static void build_payloads(void) {
    for (uint32_t i = 0; i < MAX_INSTANCES; i++) {
        const float s = 0.01F + (0.01F * hash01(i * 3U));
        inst_t *d = &s_inst_data[i];
        memset(d, 0, sizeof *d);
        d->row[0][0] = s;
        d->row[1][1] = s;
        d->row[2][2] = s;
        d->row[0][3] = (hash01((i * 3U) + 1U) * 1.9F) - 0.95F;
        d->row[1][3] = (hash01((i * 3U) + 2U) * 1.9F) - 0.95F;
        d->color[0] = hash01(i + 7U);
        d->color[1] = 0.5F;
        d->color[2] = 1.0F - d->color[0];
        d->color[3] = 1.0F;
    }
    for (uint32_t q = 0; q < MAX_QUADS; q++) {
        const float x = (hash01(q * 5U) * 1.9F) - 0.95F;
        const float y = (hash01((q * 5U) + 1U) * 1.9F) - 0.95F;
        const float s = 0.01F + (0.02F * hash01((q * 5U) + 2U));
        const uint8_t c = (uint8_t)(64U + (q & 127U));
        const float corners[4][2] = {{x, y}, {x + s, y}, {x + s, y + s}, {x, y + s}};
        for (uint32_t k = 0; k < 4; k++) {
            s_batch_vtx[(q * 4U) + k] = (batch_vtx_t){.pos = {corners[k][0], corners[k][1], 0.0F}, .color = {c, 200, 255, 255}};
        }
    }
}

/* Changes a few bytes before every upload so no upload repeats bit-for-bit. */
static void touch_payload(family_t family, uint32_t episode) {
    const uint32_t wobble = (s_frame_counter + episode) & 255U;
    if (family == FAMILY_INST) {
        s_inst_data[0].color[1] = (float)wobble * (1.0F / 255.0F);
    } else {
        s_batch_vtx[0].color[1] = (uint8_t)wobble;
    }
}

/* Indices of `quads` quads whose first vertex is `base_vertex`: the append arms rebase into one shared VBO. */
static void build_indices(uint16_t *out, uint32_t quads, uint32_t base_vertex) {
    for (uint32_t q = 0; q < quads; q++) {
        const uint16_t v = (uint16_t)(base_vertex + (q * 4U));
        const uint16_t quad[6] = {v, (uint16_t)(v + 1U), (uint16_t)(v + 2U), v, (uint16_t)(v + 2U), (uint16_t)(v + 3U)};
        memcpy(&out[(size_t)q * 6U], quad, sizeof quad);
    }
}
// #endregion

// #region gpu objects
static nt_pipeline_t make_pipeline(const char *vs_src, const char *fs_src, nt_blend_state_t blend, const char *label) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vs_src, .label = label});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fs_src, .label = label});
    nt_program_t prog = nt_gfx_make_program(vs, fs);
    return nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog, .blend = blend, .label = label});
}

static nt_vertex_layout_t batch_layout(void) {
    return (nt_vertex_layout_t){
        .attrs = {{.location = NT_ATTR_POSITION, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 0}, {.location = NT_ATTR_COLOR, .type = NT_VERTEX_UINT8, .count = 4, .normalized = true, .offset = 12}},
        .attr_count = 2,
        .stride = (uint16_t)sizeof(batch_vtx_t),
    };
}

static nt_vertex_layout_t inst_layout(void) {
    return (nt_vertex_layout_t){
        .attrs = {{.location = 4, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 0},
                  {.location = 5, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 16},
                  {.location = 6, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 32},
                  {.location = 7, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 48}},
        .attr_count = 4,
        .stride = INST_STRIDE,
    };
}

static void create_static_objects(void) {
    s_u_vp = nt_hash32_str("u_vp");
    s_inst_pip = make_pipeline(s_inst_vs, s_color_fs, nt_blend_opaque(), "bench_inst");
    s_batch_pip = make_pipeline(s_batch_vs, s_color_fs, nt_blend_opaque(), "bench_batch");
    s_load_pip = make_pipeline(s_load_vs, s_load_fs, nt_blend_alpha(), "bench_load");

    static const float cube_pos[8][3] = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}};
    static const uint16_t cube_idx[36] = {0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 4, 5, 0, 5, 1, 3, 2, 6, 3, 6, 7, 0, 3, 7, 0, 7, 4, 1, 5, 6, 1, 6, 2};
    s_cube_vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_IMMUTABLE, .data = cube_pos, .size = sizeof cube_pos, .label = "bench_cube_vbo"});
    s_cube_ibo = nt_gfx_make_buffer(
        &(nt_buffer_desc_t){.type = NT_BUFFER_INDEX, .usage = NT_USAGE_IMMUTABLE, .data = cube_idx, .size = sizeof cube_idx, .index_type = NT_INDEX_UINT16, .label = "bench_cube_ibo"});
    s_load_vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.label = "bench_load_vi"});
    static const uint8_t zeros[1024] = {0};
    s_fence_buf = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_STREAM, .data = zeros, .size = sizeof zeros, .label = "bench_fence"});
}

static nt_buffer_t make_dynamic(nt_buffer_type_t type, nt_buffer_usage_t usage, uint32_t size) {
    return nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = type, .usage = usage, .size = size, .index_type = type == NT_BUFFER_INDEX ? NT_INDEX_UINT16 : NT_INDEX_NONE, .label = "bench_stream"});
}

static void destroy_window_objects(void) {
    for (uint32_t i = 0; i < s_vi_count; i++) {
        nt_gfx_destroy_vertex_input(s_vis[i]);
    }
    for (uint32_t i = 0; i < s_buf_count; i++) {
        nt_gfx_destroy_buffer(s_bufs[i]);
        if (s_ibufs[i].id != 0) {
            nt_gfx_destroy_buffer(s_ibufs[i]);
            s_ibufs[i] = (nt_buffer_t){0};
        }
    }
    s_vi_count = 0;
    s_buf_count = 0;
}

static nt_vertex_input_t make_cube_vi(void) {
    return nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.layout = {.attrs = {{.location = NT_ATTR_POSITION, .type = NT_VERTEX_FLOAT, .count = 3}}, .attr_count = 1, .stride = 12},
                                                              .instance_layout = inst_layout(),
                                                              .vertex_buffer = s_cube_vbo,
                                                              .index_buffer = s_cube_ibo,
                                                              .label = "bench_cube_vi"});
}

static uint32_t frame_buffers(inst_arm_t arm) {
    if (arm == INST_FRAME1 || arm == INST_FRAME1_SYNC_START || arm == INST_FRAME1_SYNC_MID) {
        return 1U;
    }
    if (arm == INST_FRAME8) {
        return 8U;
    }
    return arm == INST_FRAME2 ? 2U : 3U;
}

static void create_inst_objects(inst_arm_t arm, uint32_t episodes, uint32_t instances) {
    const uint32_t slice = instances * INST_STRIDE;
    s_vis[s_vi_count++] = make_cube_vi();
    switch (arm) {
    case INST_RING:
        s_bufs[s_buf_count++] = make_dynamic(NT_BUFFER_VERTEX, NT_USAGE_STREAM, slice * episodes);
        break;
    case INST_RING_DYN:
    case INST_UPFRONT:
        s_bufs[s_buf_count++] = make_dynamic(NT_BUFFER_VERTEX, NT_USAGE_DYNAMIC, slice * episodes);
        break;
    case INST_POOL:
    case INST_POOL_STREAM:
        for (uint32_t e = 0; e < episodes; e++) {
            s_bufs[s_buf_count++] = make_dynamic(NT_BUFFER_VERTEX, arm == INST_POOL ? NT_USAGE_DYNAMIC : NT_USAGE_STREAM, slice);
        }
        break;
    case INST_ORPHAN:
        s_bufs[s_buf_count++] = make_dynamic(NT_BUFFER_VERTEX, NT_USAGE_DYNAMIC, slice);
        break;
    case INST_RING_PERSIST:
        s_bufs[s_buf_count++] = make_dynamic(NT_BUFFER_VERTEX, NT_USAGE_STREAM, slice * episodes * 4U);
        s_ring_persist_cursor = 0;
        break;
    case INST_FRAME1:
    case INST_FRAME2:
    case INST_FRAME3:
    case INST_FRAME3_DYN:
    case INST_FRAME8:
    case INST_FRAME1_SYNC_START:
    case INST_FRAME1_SYNC_MID:
        for (uint32_t k = 0; k < frame_buffers(arm); k++) {
            s_bufs[s_buf_count++] = make_dynamic(NT_BUFFER_VERTEX, arm == INST_FRAME3_DYN ? NT_USAGE_DYNAMIC : NT_USAGE_STREAM, slice * episodes);
        }
        for (uint32_t e = 0; e < episodes; e++) {
            memcpy(&s_frame_arena[(size_t)e * instances], s_inst_data, slice);
        }
        break;
    default:
        NT_ASSERT(0 && "inst arm");
    }
}

static void create_batch_objects(batch_arm_t arm, uint32_t episodes, uint32_t quads) {
    const bool rebased = arm == BATCH_APPEND || arm == BATCH_UPFRONT;
    uint32_t capacity = rebased ? episodes * quads : quads;
    if (arm == BATCH_ZERO_PART) {
        capacity = MAX_EPISODES * MAX_QUADS;
    }
    const uint32_t count = arm == BATCH_POOL ? episodes : 1U;
    for (uint32_t i = 0; i < count; i++) {
        s_bufs[s_buf_count] = make_dynamic(NT_BUFFER_VERTEX, NT_USAGE_DYNAMIC, capacity * 4U * (uint32_t)sizeof(batch_vtx_t));
        s_ibufs[s_buf_count] = make_dynamic(NT_BUFFER_INDEX, NT_USAGE_DYNAMIC, capacity * 6U * (uint32_t)sizeof(uint16_t));
        s_vis[s_vi_count++] =
            nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.layout = batch_layout(), .vertex_buffer = s_bufs[s_buf_count], .index_buffer = s_ibufs[s_buf_count], .label = "bench_batch_vi"});
        s_buf_count++;
    }
    /* Pool/zero/orphan rewrite one quad range; append/upfront rebase each episode past the previous ones. */
    for (uint32_t e = 0; e < episodes; e++) {
        build_indices(&s_batch_idx[(size_t)e * quads * 6U], quads, rebased ? e * quads * 4U : 0U);
    }
}
// #endregion

// #region arms
static void draw_inst_episode(nt_buffer_t buf, uint32_t base, uint32_t instances) {
    const uint32_t run = instances / RUNS_PER_EPISODE;
    for (uint32_t r = 0; r < RUNS_PER_EPISODE; r++) {
        nt_gfx_bind_instance_buffer(buf, base + (r * run * INST_STRIDE));
        nt_gfx_draw_indexed_instanced(0, 36, 8, run);
    }
}

static void fence_wait(void) {
    static const float zero_row[4] = {0};
    nt_gfx_update_buffer(s_fence_buf, 0, zero_row, sizeof zero_row);
}

/* FRAMEn arms: the whole frame in one upload before its first draw. */
static void run_inst_frame(inst_arm_t arm, uint32_t episodes, uint32_t instances) {
    const uint32_t slice = instances * INST_STRIDE;
    const nt_buffer_t buf = s_bufs[s_frame_counter % frame_buffers(arm)];
    for (uint32_t e = 0; e < episodes; e++) {
        s_frame_arena[(size_t)e * instances].color[1] = (float)((s_frame_counter + e) & 255U) * (1.0F / 255.0F);
    }
    if (arm == INST_FRAME1_SYNC_MID) {
        fence_wait();
    }
    nt_gfx_update_buffer(buf, 0, s_frame_arena, slice * episodes);
    for (uint32_t e = 0; e < episodes; e++) {
        draw_inst_episode(buf, e * slice, instances);
    }
    if (arm == INST_FRAME1_SYNC_START || arm == INST_FRAME1_SYNC_MID) {
        nt_gfx_bind_instance_buffer(s_fence_buf, 0); /* zero matrix: degenerate, no pixels */
        nt_gfx_draw_indexed_instanced(0, 36, 8, 1);
    }
}

static void run_inst(inst_arm_t arm, uint32_t episodes, uint32_t instances) {
    const uint32_t slice = instances * INST_STRIDE;
    nt_gfx_bind_pipeline(s_inst_pip);
    nt_gfx_bind_vertex_input(s_vis[0]);
    nt_gfx_set_uniform_mat4(s_u_vp, (const float[16]){1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0.5F, 0, 0, 0, 0, 1});
    if (arm >= INST_FRAME1) {
        run_inst_frame(arm, episodes, instances);
        return;
    }
    if (arm == INST_UPFRONT) {
        for (uint32_t e = 0; e < episodes; e++) {
            touch_payload(FAMILY_INST, e);
            nt_gfx_update_buffer(s_bufs[0], e * slice, s_inst_data, slice);
        }
    }
    for (uint32_t e = 0; e < episodes; e++) {
        const inst_t *data = s_inst_data;
        if (arm != INST_UPFRONT) {
            touch_payload(FAMILY_INST, e);
        }
        switch (arm) {
        case INST_RING:
        case INST_RING_DYN:
            nt_gfx_update_buffer(s_bufs[0], e * slice, data, slice);
            draw_inst_episode(s_bufs[0], e * slice, instances);
            break;
        case INST_POOL:
        case INST_POOL_STREAM:
            nt_gfx_update_buffer(s_bufs[e], 0, data, slice);
            draw_inst_episode(s_bufs[e], 0, instances);
            break;
        case INST_ORPHAN:
            nt_gfx_orphan_buffer(s_bufs[0], data, slice);
            draw_inst_episode(s_bufs[0], 0, instances);
            break;
        case INST_RING_PERSIST: {
            if (s_ring_persist_cursor + slice > slice * episodes * 4U) {
                s_ring_persist_cursor = 0;
            }
            const uint32_t at = s_ring_persist_cursor;
            s_ring_persist_cursor += slice;
            nt_gfx_update_buffer(s_bufs[0], at, data, slice);
            draw_inst_episode(s_bufs[0], at, instances);
            break;
        }
        case INST_UPFRONT:
            draw_inst_episode(s_bufs[0], e * slice, instances);
            break;
        default:
            NT_ASSERT(0 && "inst arm");
        }
    }
}

static void draw_batch_episode(uint32_t first_index, uint32_t quads, uint32_t vertex_count) {
    const uint32_t cmd = (quads / CMDS_PER_EPISODE) * 6U;
    for (uint32_t c = 0; c < CMDS_PER_EPISODE; c++) {
        nt_gfx_draw_indexed(first_index + (c * cmd), cmd, vertex_count);
    }
}

static void upload_batch(uint32_t slot, uint32_t episode, uint32_t quads, uint32_t dst_quad, bool orphan) {
    const batch_vtx_t *vtx = s_batch_vtx;
    const uint16_t *idx = &s_batch_idx[(size_t)episode * quads * 6U];
    const uint32_t vbytes = quads * 4U * (uint32_t)sizeof(batch_vtx_t);
    const uint32_t ibytes = quads * 6U * (uint32_t)sizeof(uint16_t);
    if (orphan) {
        nt_gfx_orphan_buffer(s_bufs[slot], vtx, vbytes);
        nt_gfx_orphan_buffer(s_ibufs[slot], idx, ibytes);
    } else {
        nt_gfx_update_buffer(s_bufs[slot], dst_quad * 4U * (uint32_t)sizeof(batch_vtx_t), vtx, vbytes);
        nt_gfx_update_buffer(s_ibufs[slot], dst_quad * 6U * (uint32_t)sizeof(uint16_t), idx, ibytes);
    }
}

static void run_batch(batch_arm_t arm, uint32_t episodes, uint32_t quads) {
    nt_gfx_bind_pipeline(s_batch_pip);
    nt_gfx_set_uniform_mat4(s_u_vp, (const float[16]){1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1});
    if (arm == BATCH_UPFRONT) {
        for (uint32_t e = 0; e < episodes; e++) {
            touch_payload(FAMILY_BATCH, e);
            upload_batch(0, e, quads, e * quads, false);
        }
    }
    uint32_t bound = UINT32_MAX;
    for (uint32_t e = 0; e < episodes; e++) {
        if (arm != BATCH_UPFRONT) {
            touch_payload(FAMILY_BATCH, e);
        }
        const uint32_t slot = arm == BATCH_POOL ? e : 0U;
        if (slot != bound) {
            nt_gfx_bind_vertex_input(s_vis[slot]);
            bound = slot;
        }
        switch (arm) {
        case BATCH_ZERO:
        case BATCH_ZERO_PART:
        case BATCH_POOL:
            upload_batch(slot, e, quads, 0, false);
            draw_batch_episode(0, quads, quads * 4U);
            break;
        case BATCH_ORPHAN:
            upload_batch(slot, e, quads, 0, true);
            draw_batch_episode(0, quads, quads * 4U);
            break;
        case BATCH_APPEND:
            upload_batch(slot, e, quads, e * quads, false);
            draw_batch_episode(e * quads * 6U, quads, (e + 1U) * quads * 4U);
            break;
        case BATCH_UPFRONT:
            draw_batch_episode(e * quads * 6U, quads, (e + 1U) * quads * 4U);
            break;
        default:
            NT_ASSERT(0 && "batch arm");
        }
    }
}
// #endregion

// #region measurement and reporting
static void send_line(const char *line) {
    printf("%s\n", line);
#ifdef NT_PLATFORM_WEB
    for (uint32_t i = 0; i < 16; i++) {
        if (s_pending[i].id == 0) {
            s_pending[i] = nt_http_request_ex("results", &(nt_http_options_t){.method = "POST", .body = line, .body_size = (uint32_t)strlen(line), .content_type = "application/json"});
            return;
        }
    }
    printf("[bench] result queue full, line only in console\n");
#else
    if (s_out_file != NULL) {
        (void)fprintf(s_out_file, "%s\n", line);
        (void)fflush(s_out_file);
    }
#endif
}

static void poll_http(void) {
    nt_http_update();
    for (uint32_t i = 0; i < 16; i++) {
        if (s_pending[i].id == 0) {
            continue;
        }
        const nt_http_state_t st = nt_http_state(s_pending[i]);
        if (st == NT_HTTP_STATE_DONE || st == NT_HTTP_STATE_FAILED) {
            if (st == NT_HTTP_STATE_FAILED || nt_http_status(s_pending[i]) != 200) {
                printf("[bench] result POST failed\n");
            }
            nt_http_free(s_pending[i]);
            s_pending[i] = (nt_http_request_t){0};
        }
    }
}

#ifndef NT_PLATFORM_WEB
static bool uploads_pending(void) {
    for (uint32_t i = 0; i < 16; i++) {
        if (s_pending[i].id != 0) {
            return true;
        }
    }
    return false;
}
#endif

static double hist_percentile_ms(double q) {
    const uint64_t target = (uint64_t)((double)s_frames * q);
    uint64_t seen = 0;
    for (uint32_t b = 0; b < HIST_BINS; b++) {
        seen += s_hist[b];
        if (seen > target) {
            return ((double)b + 0.5) * (double)HIST_BIN_NS / 1e6;
        }
    }
    return (double)HIST_BINS * (double)HIST_BIN_NS / 1e6;
}

static void begin_window(void) {
    const window_t *w = &s_windows[s_window_index];
    destroy_window_objects();
    if (w->family == FAMILY_INST) {
        create_inst_objects((inst_arm_t)w->arm, w->episodes, w->size);
    } else {
        create_batch_objects((batch_arm_t)w->arm, w->episodes, w->size);
    }
    s_window_start = nt_time_now();
    s_measuring = false;
}

static void start_measuring(void) {
    memset(s_hist, 0, sizeof s_hist);
    memset(&s_sum, 0, sizeof s_sum);
    s_frames = 0;
    s_sum_ns = 0;
    s_sum_gpu_ns = 0;
    s_gpu_samples = 0;
    s_measuring = true;
}

static void accumulate_frame(uint64_t dt_ns) {
    const uint64_t bin = dt_ns / HIST_BIN_NS;
    s_hist[bin < HIST_BINS ? bin : HIST_BINS - 1U]++;
    s_frames++;
    s_sum_ns += dt_ns;
    const nt_gfx_counters_t *c = &g_nt_gfx.last_frame;
    s_sum.buffer_upload_calls += c->buffer_upload_calls;
    s_sum.buffer_upload_bytes += c->buffer_upload_bytes;
    s_sum.instances += c->instances;
    for (uint32_t i = 0; i < NT_GFX_OP_COUNT; i++) {
        s_sum.accepted[i] += c->accepted[i];
    }
    for (uint32_t i = 0; i < NT_GFX_GL_COUNT; i++) {
        s_sum.gl[i] += c->gl[i];
    }
    uint64_t gpu_ns = 0;
    if (nt_gfx_poll_segment_time_ns("bench", &gpu_ns)) {
        s_sum_gpu_ns += gpu_ns;
        s_gpu_samples++;
    }
}

static void report_window(void) {
    const window_t *w = &s_windows[s_window_index];
    const double frames = s_frames > 0 ? (double)s_frames : 1.0;
    uint64_t gl_total = 0;
    for (uint32_t i = 0; i < NT_GFX_GL_COUNT; i++) {
        gl_total += s_sum.gl[i];
    }
    const double mean_ms = (double)s_sum_ns / frames / 1e6;
    char line[1024];
    (void)snprintf(line, sizeof line,
                   "{\"index\":%u,\"of\":%u,\"family\":\"%s\",\"arm\":\"%s\",\"episodes\":%u,\"size\":%u,\"rep\":%u,\"load\":%u,"
                   "\"frames\":%u,\"fps\":%.2f,\"ms_mean\":%.3f,\"ms_p50\":%.3f,\"ms_p95\":%.3f,\"ms_p99\":%.3f,\"gpu_ms\":%.3f,"
                   "\"draws\":%.1f,\"uploads\":%.1f,\"upload_kb\":%.1f,\"gl_calls\":%.1f,\"gl_buffer_data\":%.1f,\"gl_buffer_sub_data\":%.1f,\"gl_attrib_pointer\":%.1f}",
                   s_window_index, s_window_count, w->family == FAMILY_INST ? "inst" : "batch", w->family == FAMILY_INST ? s_inst_arm_names[w->arm] : s_batch_arm_names[w->arm], w->episodes, w->size,
                   w->rep, s_cfg.load, s_frames, mean_ms > 0.0 ? 1000.0 / mean_ms : 0.0, mean_ms, hist_percentile_ms(0.5), hist_percentile_ms(0.95), hist_percentile_ms(0.99),
                   s_gpu_samples > 0 ? (double)s_sum_gpu_ns / (double)s_gpu_samples / 1e6 : -1.0, (double)nt_gfx_draw_calls(&s_sum) / frames, (double)s_sum.buffer_upload_calls / frames,
                   (double)s_sum.buffer_upload_bytes / frames / 1024.0, (double)gl_total / frames, (double)s_sum.gl[NT_GFX_GL_glBufferData] / frames,
                   (double)s_sum.gl[NT_GFX_GL_glBufferSubData] / frames, (double)s_sum.gl[NT_GFX_GL_glVertexAttribPointer] / frames);
    send_line(line);
}
// #endregion

// #region frame
static void load_config(void) {
#ifdef NT_PLATFORM_WEB
    s_cfg_req = nt_http_request("bench_stream.cfg");
#else
    s_stage = STAGE_RUN;
#endif
}

static void poll_config(void) {
    const nt_http_state_t st = nt_http_state(s_cfg_req);
    if (st != NT_HTTP_STATE_DONE && st != NT_HTTP_STATE_FAILED) {
        return;
    }
    if (st == NT_HTTP_STATE_DONE && nt_http_status(s_cfg_req) == 200) {
        uint32_t size = 0;
        uint8_t *data = nt_http_take_data(s_cfg_req, &size);
        char *text = malloc(size + 1U);
        if (text != NULL && data != NULL) {
            memcpy(text, data, size);
            text[size] = '\0';
            parse_config(text);
        }
        free(text);
        free(data);
    } else {
        printf("[bench] no bench_stream.cfg, using defaults\n");
    }
    nt_http_free(s_cfg_req);
    build_schedule();
    s_stage = STAGE_RUN;
    begin_window();
}

static void render(void) {
    /* Green clear = finished; the only on-screen signal, results travel as JSON lines. */
    const float green = s_stage == STAGE_DONE ? 0.35F : 0.05F;
    nt_gfx_begin_segment("bench");
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.05F, green, 0.10F, 1.0F}, .clear_depth = 1.0F});
    nt_gfx_set_viewport(0, 0, (int)g_nt_window.fb_width, (int)g_nt_window.fb_height);
    if (s_stage == STAGE_RUN) {
        const window_t *cur = &s_windows[s_window_index];
        if (cur->family == FAMILY_INST && cur->arm == INST_FRAME1_SYNC_START) {
            fence_wait();
        }
        if (s_cfg.load > 0) {
            nt_gfx_bind_pipeline(s_load_pip);
            nt_gfx_bind_vertex_input(s_load_vi);
            for (uint32_t i = 0; i < s_cfg.load; i++) {
                nt_gfx_draw(0, 3);
            }
        }
        const window_t *w = &s_windows[s_window_index];
        if (w->family == FAMILY_INST) {
            run_inst((inst_arm_t)w->arm, w->episodes, w->size);
        } else {
            run_batch((batch_arm_t)w->arm, w->episodes, w->size);
        }
    }
    nt_gfx_end_pass();
    nt_gfx_end_segment();
}

static void advance_window(void) {
    const double elapsed = nt_time_now() - s_window_start;
    if (!s_measuring && elapsed >= (double)s_cfg.warmup_s) {
        start_measuring();
        s_window_start = nt_time_now();
        return;
    }
    if (s_measuring && elapsed >= (double)s_cfg.window_s) {
        report_window();
        s_window_index++;
        if (s_window_index == s_window_count) {
            destroy_window_objects();
            send_line("{\"done\":true}");
            s_stage = STAGE_DONE;
            printf("[bench] done\n");
            return;
        }
        begin_window();
    }
}

static void frame(void) {
    const uint64_t now = nt_time_nanos();
    const uint64_t dt_ns = s_prev_ns != 0 ? now - s_prev_ns : 0;
    s_prev_ns = now;

    nt_window_poll();
    nt_gfx_begin_frame();
    nt_input_poll();
    poll_http();
    s_frame_counter++;

    if (s_stage == STAGE_CONFIG) {
        poll_config();
    } else if (s_stage == STAGE_RUN) {
        /* last_frame and dt describe the frame that just closed, which ran this window's arm. */
        if (s_measuring && dt_ns > 0) {
            accumulate_frame(dt_ns);
        }
        advance_window();
    }

    render();
    nt_window_swap_buffers();

#ifndef NT_PLATFORM_WEB
    if (nt_input_key_is_pressed(NT_KEY_ESCAPE) || (s_stage == STAGE_DONE && !uploads_pending())) {
        nt_app_quit();
    }
#endif
}
// #endregion

int main(int argc, char **argv) {
    nt_engine_config_t config = {0};
    config.app_name = "bench_stream";
    config.version = 1;
    if (nt_engine_init(&config) != NT_OK) {
        printf("Failed to initialize engine\n");
        return 1;
    }

#ifndef NT_PLATFORM_WEB
    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        if (f != NULL) {
            static char text[4096];
            const size_t n = fread(text, 1, sizeof text - 1U, f);
            text[n] = '\0';
            (void)fclose(f);
            parse_config(text);
        } else {
            printf("[bench] cannot read %s, using defaults\n", argv[1]);
        }
    }
    const char *out_path = argc > 2 ? argv[2] : "bench_stream_results.jsonl";
    s_out_file = fopen(out_path, "ab");
    printf("[bench] results -> %s\n", out_path);
#else
    (void)argc;
    (void)argv;
#endif

    g_nt_window.width = 960;
    g_nt_window.height = 540;
    nt_window_init();
    nt_window_set_vsync(NT_VSYNC_OFF);
    nt_input_init();
    nt_http_init();
    nt_gfx_desc_t gfx = nt_gfx_desc_defaults();
    gfx.max_buffers = (MAX_EPISODES * 2U) + 16U;
    gfx.max_vertex_inputs = MAX_EPISODES + 16U;
    nt_gfx_init(&gfx);

    build_payloads();
    create_static_objects();

#ifdef NT_PLATFORM_WEB
    nt_platform_web_loading_complete();
#endif

    load_config();
    if (s_stage == STAGE_RUN) {
        build_schedule();
        begin_window();
    }

    g_nt_app.target_dt = 0;
    nt_app_run(frame);

#ifndef NT_PLATFORM_WEB
    destroy_window_objects();
    if (s_out_file != NULL) {
        (void)fclose(s_out_file);
    }
    nt_gfx_shutdown();
    nt_http_shutdown();
    nt_input_shutdown();
    nt_window_shutdown();
    nt_engine_shutdown();
#endif
    return 0;
}
