#include "fi_query.h"

#include "core/nt_assert.h"
#include "graphics/nt_gfx_capture_text.h"
#include "log/nt_log.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// #region record text
static bool is_mergeable_draw(const nt_gfx_event_t *e) { return e->kind == NT_GFX_EVENT_OP && (e->operation == NT_GFX_OP_DRAW || e->operation == NT_GFX_OP_DRAW_INDEXED); }

bool fi_contains_ci(const char *text, size_t len, const char *needle) {
    const size_t n = strlen(needle);
    for (size_t i = 0; n > 0 && i + n <= len; i++) {
        size_t k = 0;
        while (k < n && tolower((unsigned char)text[i + k]) == tolower((unsigned char)needle[k])) {
            k++;
        }
        if (k == n) {
            return true;
        }
    }
    return n == 0;
}

static const char *gl_enum_name(uint32_t v) {
    switch (v) {
    case 0x8D40U:
        return "GL_FRAMEBUFFER";
    case 0x8892U:
        return "GL_ARRAY_BUFFER";
    case 0x8893U:
        return "GL_ELEMENT_ARRAY_BUFFER";
    case 0x8A11U:
        return "GL_UNIFORM_BUFFER";
    case 0x88EBU:
        return "GL_PIXEL_PACK_BUFFER";
    case 0x88ECU:
        return "GL_PIXEL_UNPACK_BUFFER";
    case 0x8F36U:
        return "GL_COPY_READ_BUFFER";
    case 0x8F37U:
        return "GL_COPY_WRITE_BUFFER";
    case 0x0BE2U:
        return "GL_BLEND";
    case 0x0B71U:
        return "GL_DEPTH_TEST";
    case 0x0B44U:
        return "GL_CULL_FACE";
    case 0x0C11U:
        return "GL_SCISSOR_TEST";
    case 0x8037U:
        return "GL_POLYGON_OFFSET_FILL";
    case 0x0B90U:
        return "GL_STENCIL_TEST";
    case 0x0CF5U:
        return "GL_UNPACK_ALIGNMENT";
    case 0x0D05U:
        return "GL_PACK_ALIGNMENT";
    case 0x0DE1U:
        return "GL_TEXTURE_2D";
    case 0x8513U:
        return "GL_TEXTURE_CUBE_MAP";
    default:
        return NULL;
    }
}

/* GL enums by argument position, for the calls whose arguments are enums. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static const char *arg_enum(uint32_t call, uint32_t index, uint32_t v, char *buf, size_t size) {
    static const char *const k_modes[] = {"GL_POINTS", "GL_LINES", "GL_LINE_LOOP", "GL_LINE_STRIP", "GL_TRIANGLES", "GL_TRIANGLE_STRIP", "GL_TRIANGLE_FAN"};
    static const char *const k_funcs[] = {"GL_NEVER", "GL_LESS", "GL_EQUAL", "GL_LEQUAL", "GL_GREATER", "GL_NOTEQUAL", "GL_GEQUAL", "GL_ALWAYS"};
    static const char *const k_blend[] = {"GL_SRC_COLOR", "GL_ONE_MINUS_SRC_COLOR", "GL_SRC_ALPHA", "GL_ONE_MINUS_SRC_ALPHA",
                                          "GL_DST_ALPHA", "GL_ONE_MINUS_DST_ALPHA", "GL_DST_COLOR", "GL_ONE_MINUS_DST_COLOR"};
    switch (call) {
    case NT_GFX_GL_glEnable:
    case NT_GFX_GL_glDisable:
    case NT_GFX_GL_glBindBuffer:
    case NT_GFX_GL_glBindBufferRange:
    case NT_GFX_GL_glBindFramebuffer:
    case NT_GFX_GL_glBindTexture:
    case NT_GFX_GL_glPixelStorei:
        return index == 0 ? gl_enum_name(v) : NULL;
    case NT_GFX_GL_glActiveTexture:
        (void)snprintf(buf, size, "GL_TEXTURE%u", v - 0x84C0U);
        return index == 0 && v >= 0x84C0U && v < 0x84E0U ? buf : NULL;
    case NT_GFX_GL_glClear:
        (void)snprintf(buf, size, "%s%s%s", (v & 0x4000U) != 0 ? "COLOR|" : "", (v & 0x100U) != 0 ? "DEPTH|" : "", (v & 0x400U) != 0 ? "STENCIL|" : "");
        buf[strlen(buf) > 0 ? strlen(buf) - 1U : 0U] = '\0';
        return index == 0 && v != 0 ? buf : NULL;
    case NT_GFX_GL_glDrawArrays:
    case NT_GFX_GL_glDrawArraysInstanced:
    case NT_GFX_GL_glDrawElements:
    case NT_GFX_GL_glDrawElementsInstanced:
        if (index == 0 && v < 7) {
            return k_modes[v];
        }
        if (index == 2 && (call == NT_GFX_GL_glDrawElements || call == NT_GFX_GL_glDrawElementsInstanced)) {
            return v == 0x1401U ? "GL_UNSIGNED_BYTE" : (v == 0x1403U ? "GL_UNSIGNED_SHORT" : (v == 0x1405U ? "GL_UNSIGNED_INT" : NULL)); // NOLINT(readability-avoid-nested-conditional-operator)
        }
        return NULL;
    case NT_GFX_GL_glDepthFunc:
        return index == 0 && v >= 0x200U && v < 0x208U ? k_funcs[v - 0x200U] : NULL;
    case NT_GFX_GL_glCullFace:
        return index == 0 && v == 0x404U ? "GL_FRONT" : (index == 0 && v == 0x405U ? "GL_BACK" : NULL); // NOLINT(readability-avoid-nested-conditional-operator)
    case NT_GFX_GL_glBlendFuncSeparate:
        if (v <= 1U) {
            return v == 0 ? "GL_ZERO" : "GL_ONE";
        }
        return v >= 0x300U && v < 0x308U ? k_blend[v - 0x300U] : NULL;
    case NT_GFX_GL_glBlendEquationSeparate:
        return v == 0x8006U ? "GL_FUNC_ADD" : (v == 0x800AU ? "GL_FUNC_SUBTRACT" : (v == 0x800BU ? "GL_FUNC_REVERSE_SUBTRACT" : NULL)); // NOLINT(readability-avoid-nested-conditional-operator)
    default:
        return NULL;
    }
}

static const char *alias_prefix(uint32_t kind) {
    static const char *const k_short[] = {"obj", "shader", "prog", "pipe", "vi", "buf", "tex", "smp", "rt", "layout"};
    return kind < sizeof(k_short) / sizeof(k_short[0]) ? k_short[kind] : "obj";
}

void fi_alias_text(const fi_frame_t *frame, uint32_t kind, uint32_t object, char *buf, size_t size) {
    const uint32_t k = fi_alias(frame, kind, object);
    if (k != 0) {
        (void)snprintf(buf, size, "%s#%u", alias_prefix(kind), k);
    } else {
        (void)snprintf(buf, size, "%s:%u", alias_prefix(kind), object);
    }
}

static void append(char *buf, size_t size, size_t *at, const char *src, size_t n) {
    for (size_t i = 0; i < n && *at + 1 < size; i++) {
        buf[(*at)++] = src[i];
    }
    buf[*at] = '\0';
}

static bool token_is(const char *t, size_t n, const char *prefix) { return n >= strlen(prefix) && memcmp(t, prefix, strlen(prefix)) == 0; }

/* The GL call's integer arguments, enums by name: (GL_SCISSOR_TEST). */
static void append_args(const nt_gfx_event_t *e, char *buf, size_t size, size_t *at) {
    const uint32_t count = e->arg_count & 15U;
    for (uint32_t k = 0; k < count && k < 12U; k++) {
        char decoded[48];
        const char *name = arg_enum(e->detail, k, e->data.backend.args[k], decoded, sizeof(decoded));
        char number[16];
        (void)snprintf(number, sizeof(number), "%u", e->data.backend.args[k]);
        append(buf, size, at, k == 0 ? "(" : ",", 1);
        append(buf, size, at, name != NULL ? name : number, strlen(name != NULL ? name : number));
    }
    if (count > 0) {
        append(buf, size, at, ")", 1);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void fi_row_text(const fi_frame_t *frame, uint32_t record, char *buf, size_t size) {
    const nt_gfx_event_t *e = &frame->view.events[record];
    const char *line = NULL;
    const size_t len = fi_record_text(frame, record, &line);
    const bool call = e->kind == NT_GFX_EVENT_BACKEND || e->kind == NT_GFX_EVENT_SKIP;
    char parent_link[24];
    (void)snprintf(parent_link, sizeof(parent_link), "link=@%u", frame->parent[record]);
    size_t at = 0;
    buf[0] = '\0';
    char alias[32];
    for (size_t i = 0; i < len;) {
        size_t j = i;
        bool quoted = false;
        while (j < len && (quoted || line[j] != ' ')) {
            if (line[j] == '\\' && j + 1 < len) {
                j++;
            } else if (line[j] == '"') {
                quoted = !quoted;
            }
            j++;
        }
        const char *t = line + i;
        const size_t n = j - i;
        const bool tag = i == 0 && (e->kind == NT_GFX_EVENT_OP || call);
        i = j + 1;
        const bool draw_detail = e->kind == NT_GFX_EVENT_OP && (token_is(t, n, "first=") || token_is(t, n, "vertices=") || token_is(t, n, "instances=") || token_is(t, n, "joined="));
        if (tag || draw_detail || token_is(t, n, "result=") || token_is(t, n, "args=[") || (call && token_is(t, n, "op=")) || (n == strlen(parent_link) && memcmp(t, parent_link, n) == 0)) {
            continue;
        }
        if (at > 0 && buf[at - 1] != ' ') {
            append(buf, size, &at, " ", 1);
        }
        if (token_is(t, n, "op=") || token_is(t, n, "call=")) {
            const size_t skip = t[0] == 'o' ? 3 : 5;
            append(buf, size, &at, t + skip, n - skip);
            if (e->kind == NT_GFX_EVENT_BACKEND && t[0] == 'c') {
                append_args(e, buf, size, &at);
            }
        } else if (token_is(t, n, "obj=") || token_is(t, n, "vertex_input=")) {
            const bool obj = t[0] == 'o';
            fi_alias_text(frame, obj ? e->object_kind : NT_GFX_OBJECT_VERTEX_INPUT, obj ? e->object : e->data.draw.vertex_input, alias, sizeof(alias));
            append(buf, size, &at, alias, strlen(alias));
        } else if (e->kind == NT_GFX_EVENT_SKIP && token_is(t, n, "key=")) {
            append(buf, size, &at, "cache ", 6); /* a skip records its cache key, not the GL arguments */
            append(buf, size, &at, t, n);
        } else {
            append(buf, size, &at, t, n);
        }
    }
    if (e->kind == NT_GFX_EVENT_OP && (e->operation == NT_GFX_OP_DRAW_INSTANCED || e->operation == NT_GFX_OP_DRAW_INDEXED_INSTANCED)) {
        (void)snprintf(buf + at, size - at, " x%u", e->data.draw.instances);
    } else if (e->kind == NT_GFX_EVENT_OP && e->operation == NT_GFX_OP_SCISSOR_ENABLE) {
        (void)snprintf(buf + at, size - at, " %s", e->data.state.integers[0] != 0 ? "on" : "off");
    }
}

const char *fi_result_word(const nt_gfx_event_t *e) {
    if (e->kind == NT_GFX_EVENT_SKIP) {
        return "GL skipped";
    }
    if (e->kind != NT_GFX_EVENT_OP || e->result == NT_GFX_RESULT_NONE || e->result == NT_GFX_RESULT_ACCEPTED) {
        return "";
    }
    if (e->result == NT_GFX_RESULT_CACHE) {
        if (is_mergeable_draw(e)) {
            return "merged";
        }
        return e->operation == NT_GFX_OP_CREATE ? "found" : "already set";
    }
    const char *name = nt_gfx_result_name(e->result);
    return name != NULL ? name : "?";
}
// #endregion

// #region state text
static void gl_slot_name(const fi_gl_slot_t *slot, char *buf, size_t size) {
    const char *key = gl_enum_name(slot->key[0]);
    char hex[16];
    (void)snprintf(hex, sizeof(hex), "0x%x", slot->key[0]);
    key = key != NULL ? key : hex;
    switch (slot->call) {
    case NT_GFX_GL_NONE:
        (void)snprintf(buf, size, "fixed function");
        break;
    case NT_GFX_GL_glBindTexture:
        (void)snprintf(buf, size, "texture unit %u", slot->key[0]);
        break;
    case NT_GFX_GL_glBindSampler:
        (void)snprintf(buf, size, "sampler unit %u", slot->key[0]);
        break;
    case NT_GFX_GL_glBindBufferRange:
        (void)snprintf(buf, size, "%s #%u", key, slot->key[1]);
        break;
    case NT_GFX_GL_glEnable:
    case NT_GFX_GL_glBindBuffer:
    case NT_GFX_GL_glBindFramebuffer:
    case NT_GFX_GL_glPixelStorei:
        (void)snprintf(buf, size, "%s", key);
        break;
    default:
        (void)snprintf(buf, size, "%s", nt_gfx_gl_call_name(slot->call) + 2);
        break;
    }
}

/* The record that set this slot at the earlier point, FI_NONE when it was not set there. */
static uint32_t slot_before(const fi_gl_slot_t *slot, const fi_gl_state_t *before) {
    for (uint32_t p = 0; p < before->count; p++) {
        const fi_gl_slot_t *old = &before->slot[p];
        if (old->call == slot->call && old->key[0] == slot->key[0] && old->key[1] == slot->key[1]) {
            return old->record;
        }
    }
    return FI_NONE;
}

static bool scissor_on(const fi_frame_t *fr, uint32_t record) { return record != FI_NONE && fr->view.events[record].data.state.integers[0] != 0; }

uint32_t fi_state_lines(const fi_frame_t *frame, const fi_state_view_t *view, fi_state_line_t out[FI_STATE_LINES]) {
    const fi_request_state_t *req = &view->req;
    const fi_request_state_t *was = &view->was_req;
    const bool compared = view->was != FI_NONE;
    /* Grouped by stage: output, program, input, raster. */
    const struct {
        const char *name;
        uint32_t now, before;
    } rows[] = {{"output: pass", req->pass, was->pass},
                {"program: pipeline", req->pipeline, was->pipeline},
                {"program: textures", req->texture_set, was->texture_set},
                {"input: vertex input", req->vertex_input, was->vertex_input},
                {"raster: viewport", req->viewport, was->viewport},
                {"raster: scissor", req->scissor, was->scissor},
                {"raster: scissor on", req->scissor_enable, was->scissor_enable}};
    uint32_t n = 0;
    for (uint32_t k = 0; k < sizeof(rows) / sizeof(rows[0]); k++) {
        fi_state_line_t *l = &out[n++];
        (void)snprintf(l->name, sizeof(l->name), "%s", rows[k].name);
        l->now = rows[k].now;
        l->before = compared ? rows[k].before : rows[k].now;
        l->inherited = l->now != FI_NONE && frame->view.events[l->now].kind == NT_GFX_EVENT_INITIAL;
        l->gl = false;
        /* unset scissor is off: a pass or a restore turned it off */
        l->changed = k + 1U == sizeof(rows) / sizeof(rows[0]) ? scissor_on(frame, l->before) != scissor_on(frame, l->now) : !fi_same_setting(frame, l->before, l->now);
    }
    for (uint32_t k = 0; k < 8; k++) {
        if (req->ubo[k] != FI_NONE || was->ubo[k] != FI_NONE) {
            fi_state_line_t *l = &out[n++];
            (void)snprintf(l->name, sizeof(l->name), "program: uniform slot %u", k);
            l->now = req->ubo[k];
            l->before = compared ? was->ubo[k] : req->ubo[k];
            l->inherited = false;
            l->gl = false;
            l->changed = !fi_same_setting(frame, l->before, l->now);
        }
    }
    for (uint32_t k = 0; k < view->gl.count; k++) {
        const fi_gl_slot_t *slot = &view->gl.slot[k];
        fi_state_line_t *l = &out[n++];
        gl_slot_name(slot, l->name, sizeof(l->name));
        l->now = slot->record;
        l->before = view->was_gl_at != FI_NONE ? slot_before(slot, &view->was_gl) : slot->record;
        l->inherited = slot->inherited;
        l->gl = true;
        l->changed = !fi_gl_same(frame, slot, l->before, l->now);
    }
    return n;
}
// #endregion

// #region output
static void put(fi_out_t *out, const char *fmt, ...) NT_PRINTF_ATTR(2, 3);
static void put(fi_out_t *out, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list again;
    va_copy(again, args);
    const size_t room = out->cap - out->len;
    const int n = vsnprintf(out->cap > 0 ? out->text + out->len : NULL, room, fmt, args);
    if (n > 0 && (size_t)n >= room) { /* it did not fit: grow, then format again */
        size_t cap = out->cap > 0 ? out->cap : 4096U;
        while (cap < out->len + (size_t)n + 1U) {
            cap *= 2U;
        }
        char *text = (char *)realloc(out->text, cap);
        NT_ASSERT(text != NULL);
        out->text = text;
        out->cap = cap;
        (void)vsnprintf(out->text + out->len, out->cap - out->len, fmt, again);
    }
    out->len += n > 0 ? (size_t)n : 0U;
    va_end(again);
    va_end(args);
}

/* @N, its row text and its badge, after a prefix ("inside ", "  "). */
static void put_record(fi_out_t *out, const fi_frame_t *fr, const char *prefix, uint32_t record) {
    char body[600];
    fi_row_text(fr, record, body, sizeof(body));
    const char *word = fi_result_word(&fr->view.events[record]);
    const bool badge = word[0] != '\0';
    put(out, "%s@%u  %s%s%s%s\n", prefix, record, body, badge ? "  [" : "", word, badge ? "]" : "");
}

static double kib(uint64_t bytes) { return (double)bytes / 1024.0; }
// #endregion

// #region commands
typedef enum { LIST_FIND, LIST_DRAWS, LIST_PROBLEMS, LIST_MERGED, LIST_GROUP, LIST_OP } list_t;

static bool keeps(const fi_frame_t *fr, uint32_t i, list_t list, uint32_t arg, const char *needle) {
    const nt_gfx_event_t *e = &fr->view.events[i];
    switch (list) {
    case LIST_FIND: {
        const char *line = NULL;
        const size_t len = fi_record_text(fr, i, &line);
        return fi_contains_ci(line, len, needle);
    }
    case LIST_DRAWS:
        return fi_is_draw(e) || (e->kind == NT_GFX_EVENT_BACKEND && fi_is_draw_call(e->detail));
    case LIST_PROBLEMS:
        return fi_matches(fr, i, (fi_query_t){.kind = FI_Q_PROBLEM});
    case LIST_MERGED:
        return fi_matches(fr, i, (fi_query_t){.kind = FI_Q_MERGED});
    case LIST_GROUP:
        return fr->group[i] == arg;
    case LIST_OP:
        return e->kind == NT_GFX_EVENT_OP && e->operation == arg;
    }
    return false;
}

static void put_list(fi_out_t *out, const fi_frame_t *fr, list_t list, uint32_t arg, const char *text) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < fr->view.count; i++) {
        if (keeps(fr, i, list, arg, text)) {
            put_record(out, fr, "", i);
            n++;
        }
    }
    put(out, "(%u records)\n", n);
}

static void cmd_frames(const fi_session_t *q, fi_out_t *out) {
    for (uint32_t f = 0; f < q->file_count; f++) {
        const fi_file_t *file = &q->files[f];
        put(out, "file %s: %u frames\n", file->name, file->frame_count);
        put(out, "  %10s %8s %8s %8s %8s %8s %10s %8s\n", "sequence", "records", "GL draws", "requests", "merged", "GL calls", "upload KiB", "problems");
        for (uint32_t k = 0; k < file->frame_count; k++) {
            const fi_frame_t *fr = &file->frames[k];
            const char mark = f == q->file && k == q->frame ? '*' : ' ';
            if (fr->view.events == NULL) {
                put(out, "%c rejected: %s\n", mark, fr->error != NULL ? fr->error : "?");
                continue;
            }
            const fi_summary_t *sm = &fr->summary;
            put(out, "%c %10llu %8u %8llu %8llu %8llu %8llu %10.1f %8llu%s\n", mark, (unsigned long long)fr->view.counters.frame_sequence, fr->view.count, (unsigned long long)sm->draw_calls.n,
                (unsigned long long)sm->vertices.n, (unsigned long long)sm->merged.n, (unsigned long long)sm->gl_any.n, kib(sm->upload[2].sum), (unsigned long long)sm->problems.n,
                fr->error != NULL ? "  incomplete" : "");
        }
    }
}

static bool cmd_frame(fi_session_t *q, const char *arg, fi_out_t *out) {
    char *end = NULL;
    const unsigned long long sequence = strtoull(arg, &end, 10);
    for (uint32_t f = 0; end != arg && f < q->file_count; f++) {
        for (uint32_t k = 0; k < q->files[f].frame_count; k++) {
            const fi_frame_t *fr = &q->files[f].frames[k];
            if (fr->view.events != NULL && fr->view.counters.frame_sequence == sequence) {
                q->file = f;
                q->frame = k;
                put(out, "frame %llu of %s: %u records\n", sequence, q->files[f].name, fr->view.count);
                return true;
            }
        }
    }
    put(out, "error: no frame with sequence %s (see frames)\n", arg);
    return false;
}

static void put_insights(const fi_frame_t *fr, fi_out_t *out) {
    const fi_summary_t *sm = &fr->summary;
    put(out, "\nInsights\n%llu draw requests -> %llu GL draw calls:\n", (unsigned long long)sm->vertices.n, (unsigned long long)sm->draw_calls.n);
    if (sm->merged.n > 0) {
        put(out, "  %llu merged into the previous draw (no GL call)\n", (unsigned long long)sm->merged.n);
    }
    for (uint32_t b = FI_BREAK_COMMAND; b < FI_BREAK_COUNT; b++) {
        if (sm->brk[b].n > 0) {
            put(out, "  %llu new GL draws: %s\n", (unsigned long long)sm->brk[b].n, fi_break_name((fi_break_t)b));
        }
    }
    const uint64_t instanced = sm->op[NT_GFX_OP_DRAW_INSTANCED][NT_GFX_RESULT_ACCEPTED].n + sm->op[NT_GFX_OP_DRAW_INDEXED_INSTANCED][NT_GFX_RESULT_ACCEPTED].n;
    if (instanced > 0) {
        put(out, "  %llu instanced (instanced draws never merge)\n", (unsigned long long)instanced);
    }
    const char *sep = "no-op requests (CACHE: already set): ";
    for (uint32_t op = 0; op < NT_GFX_OP_COUNT; op++) {
        const uint64_t cached = sm->op[op][NT_GFX_RESULT_CACHE].n;
        if (cached > 0 && op != NT_GFX_OP_DRAW && op != NT_GFX_OP_DRAW_INDEXED) {
            put(out, "%s%s %llu", sep, nt_gfx_operation_name(op), (unsigned long long)cached);
            sep = ", ";
        }
    }
    if (sep[0] == ',') {
        put(out, "\n");
    }
    for (uint32_t op = 0; op < NT_GFX_OP_COUNT; op++) {
        for (uint32_t r = NT_GFX_RESULT_INACTIVE; r < FI_RESULTS; r++) {
            if (sm->op[op][r].n > 0) {
                put(out, "%llu %s requests ended %s\n", (unsigned long long)sm->op[op][r].n, nt_gfx_operation_name(op), nt_gfx_result_name(r));
            }
        }
    }
    if (sm->skips.n > 0) {
        put(out, "%llu GL calls left out by the GL cache (SKIP): GL already held that state\n", (unsigned long long)sm->skips.n);
    }
    if (sm->drops.n > 0) {
        put(out, "%llu renderer drops\n", (unsigned long long)sm->drops.n);
    }
}

static void put_stat(fi_out_t *out, const char *name, uint64_t value) {
    if (value > 0) {
        put(out, "  %-40s %llu\n", name, (unsigned long long)value);
    }
}

static void put_numbers(const fi_frame_t *fr, fi_out_t *out) {
    static const char *const k_res[FI_RES_COUNT] = {"live at frame start", "created", "destroyed"};
    const fi_summary_t *sm = &fr->summary;
    put(out, "\nDraws\n");
    put_stat(out, "draw requests (ACCEPTED or CACHE)", sm->vertices.n);
    put_stat(out, "merged into the previous draw (CACHE)", sm->merged.n);
    put_stat(out, "GL draw calls issued", sm->draw_calls.n);
    for (uint32_t c = 0; c < NT_GFX_GL_COUNT; c++) {
        if (fi_is_draw_call(c) && sm->gl[c].n > 0) {
            put(out, "    %-38s %llu\n", nt_gfx_gl_call_name(c), (unsigned long long)sm->gl[c].n);
        }
    }
    put_stat(out, "vertices (x instances)", sm->vertices.sum);
    put_stat(out, "indices (x instances)", sm->indices.sum);
    put_stat(out, "instances of instanced draws", sm->instances.sum);
    put(out, "\nUploads (bytes sent to GL)\n");
    put_stat(out, "buffer upload calls", sm->upload[0].n);
    put_stat(out, "buffer upload bytes", sm->upload[0].sum);
    put_stat(out, "texture upload calls", sm->upload[1].n);
    put_stat(out, "texture upload bytes", sm->upload[1].sum);
    put_stat(out, "  of them frame storage at end_frame", sm->frame_store.sum);
    put(out, "  %-40s %u (counter only)\n", "command stream bytes", fr->view.counters.stream_bytes);
    if (fr->buffer_upload_count > 0) {
        put(out, "\nUploads by buffer\n");
    }
    char alias[32];
    for (uint32_t k = 0; k < fr->buffer_upload_count; k++) {
        const fi_buffer_upload_t *u = &fr->buffer_upload[k];
        fi_alias_text(fr, NT_GFX_OBJECT_BUFFER, u->object, alias, sizeof(alias));
        put(out, "  %-10s %8.1f KiB in %u call%s, first @%u\n", alias, kib(u->bytes), u->calls, u->calls == 1 ? "" : "s", u->first);
    }
    put(out, "\nResources (bytes requested from GL, from definitions)\n");
    for (uint32_t kind = 1; kind < FI_OBJECTS; kind++) {
        for (uint32_t r = 0; r < FI_RES_COUNT; r++) {
            const fi_cell_t c = sm->resource[kind][r];
            if (c.n > 0) {
                char name[64];
                (void)snprintf(name, sizeof(name), "%s %s", nt_gfx_object_kind_name(kind), k_res[r]);
                put(out, "  %-40s %llu  %.1f KiB\n", name, (unsigned long long)c.n, kib(c.sum));
            }
        }
    }
}

static void put_operations(const fi_frame_t *fr, fi_out_t *out) {
    const fi_summary_t *sm = &fr->summary;
    put(out, "\nOperations (CACHE: already set, merged, or an existing object; GL: calls issued; SKIP: left out by the GL cache)\n");
    put(out, "  %-24s %6s %8s %6s %6s %6s %6s  other results\n", "operation", "req", "ACCEPTED", "CACHE", "GL", "SKIP", "nest");
    for (uint32_t op = 0; op < NT_GFX_OP_COUNT; op++) {
        uint64_t requested = 0;
        for (uint32_t r = 0; r < FI_RESULTS; r++) {
            requested += sm->op[op][r].n;
        }
        if (requested == 0) {
            continue;
        }
        put(out, "  %-24s %6llu %8llu %6llu %6llu %6llu %6llu", nt_gfx_operation_name(op), (unsigned long long)requested, (unsigned long long)sm->op[op][NT_GFX_RESULT_ACCEPTED].n,
            (unsigned long long)sm->op[op][NT_GFX_RESULT_CACHE].n, (unsigned long long)sm->op_calls[op].n, (unsigned long long)sm->op_skips[op].n, (unsigned long long)sm->op_nested[op].n);
        for (uint32_t r = 0; r < FI_RESULTS; r++) {
            if (r != NT_GFX_RESULT_ACCEPTED && r != NT_GFX_RESULT_CACHE && sm->op[op][r].n > 0) {
                put(out, "  %s %llu", nt_gfx_result_name(r), (unsigned long long)sm->op[op][r].n);
            }
        }
        put(out, "\n");
    }
    put(out, "\nGL calls by name\n");
    bool listed[NT_GFX_GL_COUNT] = {0};
    for (;;) { /* most frequent first */
        uint32_t best = NT_GFX_GL_COUNT;
        for (uint32_t c = 0; c < NT_GFX_GL_COUNT; c++) {
            best = !listed[c] && sm->gl[c].n > 0 && (best == NT_GFX_GL_COUNT || sm->gl[c].n > sm->gl[best].n) ? c : best;
        }
        if (best == NT_GFX_GL_COUNT) {
            break;
        }
        listed[best] = true;
        put_stat(out, nt_gfx_gl_call_name(best), sm->gl[best].n);
    }
}

static void put_counters(const fi_frame_t *fr, fi_out_t *out) {
    static fi_counter_row_t rows[FI_COUNTER_ROWS];
    const uint32_t n = fi_counter_rows(fr, rows);
    put(out, "\nCounters check (the frame's counters against a recount from its records)\n");
    if (fr->view.overflow) {
        put(out, "  events truncated, counters complete: no check\n");
        return;
    }
    uint32_t differ = 0;
    for (uint32_t k = 0; k < n; k++) {
        if (rows[k].counter != rows[k].recount) {
            put(out, "  %-30s counter %llu, records give %llu\n", rows[k].name, (unsigned long long)rows[k].counter, (unsigned long long)rows[k].recount);
            differ++;
        }
    }
    if (differ == 0) {
        put(out, "  every counter matches its recount\n");
    }
}

static void cmd_summary(const fi_frame_t *fr, fi_out_t *out) {
    const fi_summary_t *sm = &fr->summary;
    put(out, "frame %llu (%s, %u records)\n", (unsigned long long)fr->view.counters.frame_sequence, fr->view.platform == NT_GFX_CAPTURE_WEB ? "web" : "native", fr->view.count);
    if (fr->error != NULL) {
        put(out, "incomplete: %s at line %u:%u; the records before it are read\n", fr->error, fr->error_line, fr->error_col);
    }
    if (fr->view.overflow) {
        put(out, "events truncated, counters complete\n");
    }
    if (fr->merge_mismatch != FI_NONE) {
        put(out, "inspector merge rule out of date: draw @%u\n", fr->merge_mismatch);
    }
    put(out, "%llu GL draw calls (%llu requests, %llu merged)\n", (unsigned long long)sm->draw_calls.n, (unsigned long long)sm->vertices.n, (unsigned long long)sm->merged.n);
    put(out, "%llu GL calls (%llu skipped by GL cache)\n", (unsigned long long)sm->gl_any.n, (unsigned long long)sm->skips.n);
    put(out, "%.1f KiB uploaded in %llu upload calls\n", kib(sm->upload[2].sum), (unsigned long long)sm->upload[2].n);
    put(out, "%llu problems (drops and failed requests)\n", (unsigned long long)sm->problems.n);
    put_insights(fr, out);
    put_numbers(fr, out);
    put_operations(fr, out);
    put_counters(fr, out);
}

static void cmd_groups(const fi_frame_t *fr, fi_out_t *out) {
    put(out, "requests: draw requests to the engine; GL draws: GL draw calls they cost (merged requests share one)\n");
    put(out, "%9s %9s %9s %10s %6s  %s\n", "requests", "GL draws", "GL calls", "upload B", "drops", "marker path > note");
    uint64_t total[FI_METRIC_COUNT] = {0};
    for (uint32_t g = 0; g < fr->group_count; g++) {
        const fi_cell_t *c = &fr->summary.group[(size_t)g * FI_METRIC_COUNT];
        if (c[FI_METRIC_DRAWS].n + c[FI_METRIC_CALLS].n + c[FI_METRIC_DROPS].n == 0) {
            continue;
        }
        for (uint32_t m = 0; m < FI_METRIC_COUNT; m++) {
            total[m] += m == FI_METRIC_BYTES ? c[m].sum : c[m].n;
        }
        put(out, "%9llu %9llu %9llu %10llu %6llu  %s\n", (unsigned long long)c[FI_METRIC_DRAWS].n, (unsigned long long)c[FI_METRIC_DRAW_CALLS].n, (unsigned long long)c[FI_METRIC_CALLS].n,
            (unsigned long long)c[FI_METRIC_BYTES].sum, (unsigned long long)c[FI_METRIC_DROPS].n, fr->group_key[g]);
    }
    put(out, "%9llu %9llu %9llu %10llu %6llu  total\n", (unsigned long long)total[FI_METRIC_DRAWS], (unsigned long long)total[FI_METRIC_DRAW_CALLS], (unsigned long long)total[FI_METRIC_CALLS],
        (unsigned long long)total[FI_METRIC_BYTES], (unsigned long long)total[FI_METRIC_DROPS]);
}

static bool parse_record(const fi_frame_t *fr, const char *arg, uint32_t *record, fi_out_t *out) {
    const char *p = arg[0] == '@' ? arg + 1 : arg;
    char *end = NULL;
    const unsigned long v = strtoul(p, &end, 10);
    if (end == p || v >= fr->view.count) {
        put(out, "error: the frame has no record %s (records @0..@%u)\n", arg, fr->view.count - 1U);
        return false;
    }
    *record = (uint32_t)v;
    return true;
}

/* For a draw: did it cost a GL draw call, and why. */
static void put_verdict(const fi_frame_t *fr, uint32_t id, fi_out_t *out) {
    const nt_gfx_event_t *e = &fr->view.events[id];
    if (!fi_is_draw(e)) {
        return;
    }
    const uint32_t joined = fi_joined(fr, id);
    if (joined != FI_NONE) {
        put(out, "MERGED: no new GL draw call; its range joined an earlier draw\n");
        put_record(out, fr, "  joined ", joined);
    } else if (e->result != NT_GFX_RESULT_ACCEPTED) {
        put(out, "%s: nothing drawn\n", nt_gfx_result_name(e->result));
    } else {
        put(out, "NEW GL draw call %u of %u\n", fi_draw_ordinal(fr, id), nt_gfx_draw_calls(&fr->view.counters));
        if (e->operation == NT_GFX_OP_DRAW_INSTANCED || e->operation == NT_GFX_OP_DRAW_INDEXED_INSTANCED) {
            put(out, "  instanced draws never merge: joining them would change gl_InstanceID\n");
        } else if (fr->brk[id] != FI_BREAK_NONE) {
            put(out, "  not merged: %s\n", fi_break_name((fi_break_t)fr->brk[id]));
            if (fr->cause[id] != FI_NONE) {
                put_record(out, fr, "  because of ", fr->cause[id]);
            }
        }
    }
}

static void cmd_record(const fi_frame_t *fr, uint32_t id, fi_out_t *out) {
    put_record(out, fr, "", id);
    put_verdict(fr, id, out);
    if (fr->parent[id] < FI_NODE_BASE) {
        put_record(out, fr, "inside ", fr->parent[id]);
    } else {
        put(out, "phase: %s\n", fi_node_name((fi_node_t)(fr->parent[id] - FI_NODE_BASE)));
    }
    put(out, "group: %s\n", fr->group_key[fr->group[id]]);
    uint32_t children = 0;
    for (uint32_t i = id + 1U; i < fr->view.count; i++) {
        if (fr->parent[i] == id) {
            put_record(out, fr, children++ == 0 ? "GL calls and records it caused:\n  " : "  ", i);
        }
    }
    const char *line = NULL;
    const size_t len = fi_record_text(fr, id, &line);
    put(out, "as captured: %.*s\n", (int)len, line);
    const nt_gfx_event_t *e = &fr->view.events[id];
    if (e->kind <= NT_GFX_EVENT_ARGUMENT && e->object_kind != 0 && e->object_kind < FI_OBJECTS && e->object != 0) {
        char alias[32];
        fi_alias_text(fr, e->object_kind, e->object, alias, sizeof(alias));
        put(out, "obj %u is %s\n", e->object, alias);
    }
}

static void put_state_line(const fi_frame_t *fr, const fi_state_line_t *l, fi_out_t *out) {
    char prefix[96];
    (void)snprintf(prefix, sizeof(prefix), "%c %-26s %s", l->changed ? '*' : ' ', l->name, l->inherited ? "inherited " : "");
    if (l->now == FI_NONE) {
        put(out, "%sunset\n", prefix);
    } else {
        put_record(out, fr, prefix, l->now);
    }
    if (l->changed && l->before != FI_NONE) {
        put_record(out, fr, "    was ", l->before);
    } else if (l->changed) {
        put(out, "    was unset\n");
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool cmd_state(const fi_frame_t *fr, const char *arg, bool all, fi_out_t *out) {
    static fi_state_view_t view;
    static fi_state_line_t lines[FI_STATE_LINES];
    const bool frame_end = strcmp(arg, "end") == 0;
    uint32_t at = 0;
    if (frame_end || strcmp(arg, "init") == 0) {
        if (!fi_state_frame(fr, frame_end, &view)) {
            put(out, "the frame recorded no begin_frame state\n");
            return true;
        }
        put(out, frame_end ? "End state: after the frame's last record\n" : "Init state: what begin_frame inherited, where the game's first call starts\n");
    } else if (parse_record(fr, arg, &at, out)) {
        fi_state_at(fr, at, &view);
        put_record(out, fr, "State at ", at);
    } else {
        return false;
    }
    const uint32_t n = fi_state_lines(fr, &view, lines);
    put(out, "\nEngine state%s\n", view.was != FI_NONE ? " (*: a different value than at the compared record)" : "");
    if (view.was != FI_NONE) {
        char prefix[64];
        (void)snprintf(prefix, sizeof(prefix), "compared with the %s ", view.was_name);
        put_record(out, fr, prefix, view.was);
    }
    uint32_t k = 0;
    for (; k < n && !lines[k].gl; k++) {
        put_state_line(fr, &lines[k], out);
    }
    put(out, "\nGL state at @%u%s\n", view.gl_at, view.was_gl_at != FI_NONE && !all ? " (changes only; add \"all\" for the rest)" : "");
    if (view.was_gl_at != FI_NONE) {
        char prefix[64];
        (void)snprintf(prefix, sizeof(prefix), "compared with the %s ", view.was_gl_name);
        put_record(out, fr, prefix, view.was_gl_at);
    }
    uint32_t shown = 0;
    for (; k < n; k++) {
        if (all || lines[k].changed || view.was_gl_at == FI_NONE) {
            put_state_line(fr, &lines[k], out);
            shown++;
        }
    }
    if (view.gl.full) {
        put(out, "more GL state than the inspector tracks: the rest is not shown\n");
    }
    if (shown == 0) {
        put(out, "no GL state changed since the %s\n", view.was_gl_name);
    }
    return true;
}

/* The group named NAME: its key exactly, or the one key that contains it. */
static bool find_group(const fi_frame_t *fr, const char *name, uint32_t *group, fi_out_t *out) {
    uint32_t found = 0;
    for (uint32_t g = 0; g < fr->group_count; g++) {
        if (strcmp(fr->group_key[g], name) == 0) {
            *group = g;
            return true;
        }
        if (fi_contains_ci(fr->group_key[g], strlen(fr->group_key[g]), name)) {
            *group = g;
            found++;
        }
    }
    if (found == 1) {
        return true;
    }
    put(out, "error: %s groups match \"%s\" (see groups)\n", found == 0 ? "no" : "several", name);
    for (uint32_t g = 0; found > 1 && g < fr->group_count; g++) {
        if (fi_contains_ci(fr->group_key[g], strlen(fr->group_key[g]), name)) {
            put(out, "  %s\n", fr->group_key[g]);
        }
    }
    return false;
}

static bool cmd_list(const fi_frame_t *fr, const char *what, const char *name, fi_out_t *out) {
    static const char *const k_simple[] = {"draws", "problems", "merged"};
    for (uint32_t k = 0; k < 3; k++) {
        if (strcmp(what, k_simple[k]) == 0) {
            put_list(out, fr, (list_t)(LIST_DRAWS + k), 0, "");
            return true;
        }
    }
    if (strcmp(what, "group") == 0 && name[0] != '\0') {
        uint32_t g = 0;
        if (!find_group(fr, name, &g, out)) {
            return false;
        }
        put(out, "group %s\n", fr->group_key[g]);
        put_list(out, fr, LIST_GROUP, g, "");
        return true;
    }
    for (uint32_t op = 0; strcmp(what, "op") == 0 && op < NT_GFX_OP_COUNT; op++) {
        const char *op_name = nt_gfx_operation_name(op);
        if (op_name != NULL && fi_contains_ci(op_name, strlen(op_name), name) && strlen(op_name) == strlen(name)) {
            put_list(out, fr, LIST_OP, op, "");
            return true;
        }
    }
    put(out, "error: list draws|problems|merged|group NAME|op NAME\n");
    return false;
}

static void cmd_help(fi_out_t *out) {
    put(out, "frames                       every frame of the open files; * marks the one commands read\n"
             "frame SEQUENCE               read the frame with this capture sequence\n"
             "summary                      the Information page: cards, insights, draws, uploads, operations, counters\n"
             "groups                       per marker and note: requests, GL draws, GL calls, upload bytes, drops\n"
             "record @N                    the record, merged or new GL draw and why, its parent, group and GL calls\n"
             "state @N|init|end [all]      engine and GL state; * marks a change against the previous draw or the start\n"
             "find TEXT                    records whose captured line holds TEXT (any case)\n"
             "list draws|problems|merged|group NAME|op NAME\n");
}

static const char *const k_commands[] = {"help", "frames", "frame", "summary", "groups", "record", "state", "find", "list"};

bool fi_query_command(const char *word) {
    for (uint32_t k = 0; k < sizeof(k_commands) / sizeof(k_commands[0]); k++) {
        if (strcmp(word, k_commands[k]) == 0) {
            return true;
        }
    }
    return false;
}

/* The next space-separated word of *line into buf; *line moves past it and the spaces after it. */
static void next_word(const char **line, char *buf, size_t size) {
    const char *p = *line;
    size_t n = 0;
    while (*p != '\0' && *p != ' ') {
        if (n + 1U < size) {
            buf[n++] = *p;
        }
        p++;
    }
    buf[n] = '\0';
    while (*p == ' ') {
        p++;
    }
    *line = p;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
bool fi_query(fi_session_t *session, const char *line, fi_out_t *out) {
    while (*line == ' ') {
        line++;
    }
    char command[16];
    char arg[64];
    next_word(&line, command, sizeof(command));
    const char *rest = line; /* the argument and what follows it */
    next_word(&line, arg, sizeof(arg));
    if (strcmp(command, "help") == 0 || command[0] == '\0') {
        cmd_help(out);
        return command[0] != '\0';
    }
    if (strcmp(command, "frames") == 0) {
        cmd_frames(session, out);
        return true;
    }
    if (strcmp(command, "frame") == 0) {
        return cmd_frame(session, arg, out);
    }
    if (!fi_query_command(command)) {
        put(out, "error: unknown command %s; help lists them\n", command);
        return false;
    }
    const fi_frame_t *fr = &session->files[session->file].frames[session->frame];
    if (fr->view.events == NULL) {
        put(out, "error: the frame's header was rejected: %s\n", fr->error != NULL ? fr->error : "?");
        return false;
    }
    if (strcmp(command, "summary") == 0) {
        cmd_summary(fr, out);
        return true;
    }
    if (strcmp(command, "groups") == 0) {
        cmd_groups(fr, out);
        return true;
    }
    if (strcmp(command, "state") == 0) {
        return cmd_state(fr, arg, strcmp(line, "all") == 0, out);
    }
    uint32_t record = 0;
    if (strcmp(command, "record") == 0) {
        if (!parse_record(fr, arg, &record, out)) {
            return false;
        }
        cmd_record(fr, record, out);
        return true;
    }
    if (strcmp(command, "find") == 0) {
        put_list(out, fr, LIST_FIND, 0, rest);
        return true;
    }
    return cmd_list(fr, arg, line, out);
}
// #endregion
