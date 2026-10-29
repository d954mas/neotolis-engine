#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "app/nt_app.h"
#include "core/nt_assert.h"
#include "core/nt_core.h"
#include "devapi/nt_devapi_groups.h"
#include "devapi/nt_devapi_internal.h"
#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_capture_text.h"
#include "log/nt_log.h"

/* Gfx command recording group (gfx.record / gfx.frames / gfx.read). While recording is on, gfx's
   continuous capture records every frame; each nt_devapi_update copies the frame gfx last finished
   into a ring of recent frames before its commands run, and commands read the ring at once. */

/* Static ring sizes (-D overridable): records of all stored frames, and stored frames. */
#ifndef NT_DEVAPI_GFX_RING_RECORDS
#define NT_DEVAPI_GFX_RING_RECORDS 163840U
#endif
#ifndef NT_DEVAPI_GFX_RING_FRAMES
#define NT_DEVAPI_GFX_RING_FRAMES 256U
#endif

/* Sim frames by sequence: the finished frame, its predecessor, the open frame and the next. */
#define NT_DEVAPI_GFX_SIM_FRAMES 4U

#define NT_DEVAPI_ERR_FRAME_NOT_READY "frame_not_ready"
#define NT_DEVAPI_ERR_UNKNOWN_FRAME "unknown_frame"

typedef struct {
    nt_gfx_capture_view_t view; /* events point into s_records */
    uint32_t sim_frame;         /* g_nt_app.frame while it recorded */
    bool advanced;              /* the simulation advanced in this frame */
} stored_frame;

static nt_gfx_event_t s_records[NT_DEVAPI_GFX_RING_RECORDS];
static stored_frame s_frames[NT_DEVAPI_GFX_RING_FRAMES];

typedef struct {
    uint64_t sequence;
    uint32_t frame; /* g_nt_app.frame in that frame */
} sim_entry;

static struct {
    uint32_t oldest; /* frame ring: index of the oldest stored frame */
    uint32_t count;
    uint32_t head; /* record ring: where the next frame goes */
    uint64_t last; /* newest sequence stored or excluded: the next stored frame is last + 1 */
    uint64_t stop; /* after off: the frame recording at off, still to store; 0 = none */
    bool on;       /* this group's recording, not the game's own continuous capture */
    sim_entry sim[NT_DEVAPI_GFX_SIM_FRAMES];
} s_ring;

// #region ring
static const stored_frame *frame_at(uint32_t i) { return &s_frames[(s_ring.oldest + i) % NT_DEVAPI_GFX_RING_FRAMES]; }

/* The oldest frames that give way to a frame at [start, start + count): up to the newest one it overlaps,
   and at least one when the frame ring is full. */
static uint32_t frames_to_drop(uint32_t start, uint32_t count) {
    uint32_t drop = s_ring.count == NT_DEVAPI_GFX_RING_FRAMES ? 1U : 0U;
    for (uint32_t i = 0; i < s_ring.count; i++) {
        const stored_frame *f = frame_at(i);
        const uint32_t first = (uint32_t)(f->view.events - s_records);
        if (start < first + f->view.count && first < start + count) {
            drop = i + 1U;
        }
    }
    return drop;
}

/* A frame is contiguous, at head or at 0 when it would pass the end; the oldest frames give way. */
static void store_frame(const nt_gfx_capture_view_t *view, uint32_t sim_frame, bool advanced) {
    NT_ASSERT(view->count > 0 && "gfx recording: a recorded frame starts with its state snapshot");
    NT_ASSERT(view->count <= NT_DEVAPI_GFX_RING_RECORDS && "gfx recording: a frame is larger than the ring -- raise NT_DEVAPI_GFX_RING_RECORDS or lower capture_capacity");
    const uint32_t start = s_ring.head + view->count <= NT_DEVAPI_GFX_RING_RECORDS ? s_ring.head : 0U;
    const uint32_t drop = frames_to_drop(start, view->count);
    s_ring.oldest = (s_ring.oldest + drop) % NT_DEVAPI_GFX_RING_FRAMES;
    s_ring.count -= drop;
    memcpy(&s_records[start], view->events, (size_t)view->count * sizeof(nt_gfx_event_t));
    stored_frame *f = &s_frames[(s_ring.oldest + s_ring.count) % NT_DEVAPI_GFX_RING_FRAMES];
    *f = (stored_frame){.view = *view, .sim_frame = sim_frame, .advanced = advanced};
    f->view.events = &s_records[start];
    s_ring.count++;
    s_ring.head = start + view->count;
    if (view->overflow) {
        NT_LOG_WARN_ONCE("gfx recording: a frame overflowed its capture; raise nt_gfx_desc_t.capture_capacity");
    }
}

static uint32_t sim_frame_of(uint64_t sequence) {
    const sim_entry *e = &s_ring.sim[sequence % NT_DEVAPI_GFX_SIM_FRAMES];
    NT_ASSERT(e->sequence == sequence && "gfx recording: call nt_devapi_update once per frame while recording");
    return e->frame;
}

static void note_sim_frame(uint64_t sequence) { s_ring.sim[sequence % NT_DEVAPI_GFX_SIM_FRAMES] = (sim_entry){sequence, g_nt_app.frame}; }

/* Every update, between frames: stores the frame gfx finished at the last begin_frame while recording,
   or up to stop after off, then notes the sim frame of the frame opening next (nt_app advanced it
   before this update). Frames recorded while off, by the game or other code, are not stored. */
static void gfx_poll(void) {
    const nt_gfx_capture_view_t view = nt_gfx_capture_read();
    const uint64_t sequence = view.counters.frame_sequence;
    if (sequence > s_ring.last && (s_ring.on || sequence <= s_ring.stop)) {
        NT_ASSERT(sequence == s_ring.last + 1U && "gfx recording: call nt_devapi_update once per frame while recording");
        const uint32_t sim = sim_frame_of(sequence);
        const sim_entry *prev = &s_ring.sim[(sequence - 1U) % NT_DEVAPI_GFX_SIM_FRAMES];
        store_frame(&view, sim, prev->sequence == sequence - 1U && prev->frame != sim); /* unknown before the first update */
        s_ring.last = sequence;
    }
    NT_ASSERT((s_ring.on || s_ring.stop == 0 || sequence <= s_ring.stop) && "gfx recording: the frame recording at off was lost; call nt_devapi_update once per frame");
    if (s_ring.stop != 0 && s_ring.last >= s_ring.stop) {
        s_ring.stop = 0;
    }
    note_sim_frame(g_nt_gfx.counters.frame_sequence + 1U);
}

/* On records from the next frame that begins; off still stores the frame recording now. */
void nt_devapi_gfx_record(bool enabled) {
    if (enabled == s_ring.on) {
        return;
    }
    s_ring.on = enabled;
    const uint64_t current = g_nt_gfx.counters.frame_sequence;
    if (enabled) {
        s_ring.oldest = 0;
        s_ring.count = 0;
        s_ring.head = 0;
        /* Off then on keeps the frame recording at off as the first frame; any other frame in flight is excluded. */
        s_ring.last = s_ring.stop == current && current != 0 ? current - 1U : current;
        s_ring.stop = 0;
    } else {
        s_ring.stop = g_nt_gfx.capture_recording && current > s_ring.last ? current : 0;
    }
    nt_gfx_capture_set_continuous(enabled);
}
// #endregion

// #region commands
static bool cmd_gfx_record(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)result;
    (void)ud;
    const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(params, "enabled");
    if (!cJSON_IsBool(enabled)) {
        return nt_devapi_fail(err, "gfx.record: enabled must be a boolean");
    }
    nt_devapi_gfx_record(cJSON_IsTrue(enabled));
    return true;
}

static void add_frame_entry(cJSON *frames, const stored_frame *f) {
    cJSON *entry = cJSON_CreateObject();
    NT_ASSERT(entry != NULL);
    devapi_add_number(entry, "sequence", (double)f->view.counters.frame_sequence);
    devapi_add_number(entry, "sim_frame", (double)f->sim_frame);
    devapi_add_bool(entry, "advanced", f->advanced);
    devapi_add_number(entry, "records", (double)f->view.count);
    devapi_add_bool(entry, "overflow", f->view.overflow);
    const bool added = cJSON_AddItemToArray(frames, entry);
    NT_ASSERT(added);
}

static bool cmd_gfx_frames(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)params;
    (void)ud;
    if (s_ring.stop != 0) { /* the ring still changes: storing that frame can evict the oldest */
        return nt_devapi_fail_code(err, NT_DEVAPI_ERR_FRAME_NOT_READY, "gfx.frames: the frame in flight at off is not stored yet");
    }
    devapi_add_bool(result, "recording", s_ring.on);
    cJSON *frames = cJSON_AddArrayToObject(result, "frames");
    NT_ASSERT(frames != NULL);
    for (uint32_t i = 0; i < s_ring.count; i++) {
        add_frame_entry(frames, frame_at(i));
    }
    return true;
}

static const stored_frame *find_sequence(uint64_t sequence) {
    for (uint32_t i = 0; i < s_ring.count; i++) {
        if (frame_at(i)->view.counters.frame_sequence == sequence) {
            return frame_at(i);
        }
    }
    return NULL;
}

static bool cmd_gfx_read(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    const cJSON *node = cJSON_GetObjectItemCaseSensitive(params, "sequence");
    const stored_frame *found = NULL;
    if (node != NULL) {
        uint64_t sequence = 0;
        if (!nt_devapi_parse_u53_param_exact(node, err, "gfx.read: sequence must be a non-negative integer below 2^53", &sequence)) {
            return false;
        }
        found = find_sequence(sequence);
    } else if (s_ring.stop != 0 || (s_ring.count == 0 && s_ring.on)) {
        return nt_devapi_fail_code(err, NT_DEVAPI_ERR_FRAME_NOT_READY, "gfx.read: the newest frame is not stored yet");
    } else if (s_ring.count > 0) {
        found = frame_at(s_ring.count - 1U);
    }
    if (found == NULL) {
        return nt_devapi_fail_code(err, NT_DEVAPI_ERR_UNKNOWN_FRAME, "gfx.read: the frame is not stored (evicted, before recording, or never recorded); gfx.frames lists the ring");
    }
    const size_t needed = nt_gfx_capture_format(&found->view, NULL, 0);
    char *text = (char *)malloc(needed + 1U);
    NT_ASSERT(text != NULL);
    (void)nt_gfx_capture_format(&found->view, text, needed + 1U);
    devapi_add_number(result, "sequence", (double)found->view.counters.frame_sequence);
    devapi_add_number(result, "sim_frame", (double)found->sim_frame);
    devapi_add_string(result, "text", text);
    free(text);
    return true;
}
// #endregion

static const nt_devapi_command k_gfx_cmds[] = {
    {
        .method = "gfx.record",
        .summary = "turn gfx command recording on or off; while on, every frame from the next begin_frame records into a ring of recent frames (off keeps the ring and stores "
                   "the frame in flight; off then on clears it; on while on changes nothing)",
        .params_shape = "{enabled:boolean}",
        .result_shape = "{}",
        .fn = cmd_gfx_record,
    },
    {
        .method = "gfx.frames",
        .summary = "list the recorded frames in the ring, oldest first; a frame is listed two frames after it renders; frame_not_ready after off until the frame in flight is stored",
        .params_shape = "{}",
        .result_shape = "{recording:boolean,frames:[{sequence:number,sim_frame:number,advanced:boolean,records:number,overflow:boolean}]}",
        .fn = cmd_gfx_frames,
    },
    {
        .method = "gfx.read",
        .summary = "the capture text of one recorded frame: the newest stored (frame_not_ready while none is stored yet or after off until the frame in flight is) or one by gfx sequence from "
                   "gfx.frames",
        .params_shape = "{sequence?:number}",
        .result_shape = "{sequence:number,sim_frame:number,text:string}",
        .fn = cmd_gfx_read,
    },
};

void nt_devapi_register_gfx(void) {
    memset(&s_ring, 0, sizeof(s_ring));
    s_ring.last = g_nt_gfx.counters.frame_sequence; /* frames finished or in flight before registration are never stored */
    nt_devapi_set_frame_poll(gfx_poll);
    nt_devapi_register(k_gfx_cmds, NT_COUNTOF(k_gfx_cmds), NULL);
}
