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

/* Gfx command recording group (gfx.record / gfx.frames / gfx.read). While recording is on, every frame
   records; the frame hook stores each finalized frame in a ring of recent frames, and commands read the
   ring at once. */

/* Static ring sizes (-D overridable): records of all stored frames, and stored frames. */
#ifndef NT_DEVAPI_GFX_RING_RECORDS
#define NT_DEVAPI_GFX_RING_RECORDS 163840U
#endif
#ifndef NT_DEVAPI_GFX_RING_FRAMES
#define NT_DEVAPI_GFX_RING_FRAMES 256U
#endif

#define NT_DEVAPI_ERR_FRAME_NOT_READY "frame_not_ready"
#define NT_DEVAPI_ERR_UNKNOWN_FRAME "unknown_frame"

typedef struct {
    nt_gfx_capture_view_t view; /* events point into s_records */
    uint32_t sim_frame;         /* g_nt_app.frame while it recorded */
    bool advanced;              /* the simulation advanced in this frame */
} stored_frame;

static nt_gfx_event_t s_records[NT_DEVAPI_GFX_RING_RECORDS];
static stored_frame s_frames[NT_DEVAPI_GFX_RING_FRAMES];

static struct {
    uint32_t oldest; /* frame ring: index of the oldest stored frame */
    uint32_t count;
    uint32_t head; /* record ring: where the next frame goes */
    bool recording;
    uint64_t pending; /* sequence of the frame recording for the ring; 0 = none */
    bool pending_advanced;
    uint32_t last_sim; /* g_nt_app.frame at the last update */
} s_ring;

// #region ring
static const stored_frame *frame_at(uint32_t i) { return &s_frames[(s_ring.oldest + i) % NT_DEVAPI_GFX_RING_FRAMES]; }

static bool overlaps_stored(uint32_t start, uint32_t count) {
    for (uint32_t i = 0; i < s_ring.count; i++) {
        const stored_frame *f = frame_at(i);
        const uint32_t first = (uint32_t)(f->view.events - s_records);
        if (count > 0 && f->view.count > 0 && start < first + f->view.count && first < start + count) {
            return true;
        }
    }
    return false;
}

/* A frame is contiguous, at head or at 0 when it would pass the end; the oldest frames give way. */
static void store_frame(const nt_gfx_capture_view_t *view, uint32_t sim_frame, bool advanced) {
    NT_ASSERT(view->count <= NT_DEVAPI_GFX_RING_RECORDS && "gfx recording: a frame is larger than the ring -- raise NT_DEVAPI_GFX_RING_RECORDS or lower capture_capacity");
    const uint32_t start = s_ring.head + view->count <= NT_DEVAPI_GFX_RING_RECORDS ? s_ring.head : 0U;
    while (s_ring.count == NT_DEVAPI_GFX_RING_FRAMES || overlaps_stored(start, view->count)) {
        s_ring.oldest = (s_ring.oldest + 1U) % NT_DEVAPI_GFX_RING_FRAMES;
        s_ring.count--;
    }
    if (view->count > 0) {
        memcpy(&s_records[start], view->events, (size_t)view->count * sizeof(nt_gfx_event_t));
    }
    stored_frame *f = &s_frames[(s_ring.oldest + s_ring.count) % NT_DEVAPI_GFX_RING_FRAMES];
    *f = (stored_frame){.view = *view, .sim_frame = sim_frame, .advanced = advanced};
    f->view.events = &s_records[start];
    s_ring.count++;
    s_ring.head = start + view->count;
    if (view->overflow) {
        NT_LOG_WARN_ONCE("gfx recording: a frame overflowed its capture; raise nt_gfx_desc_t.capture_capacity");
    }
}

/* Runs each update after begin_frame and before the transport, so a command in this update sees the frame
   begin_frame just finalized. The sim frame is tracked in every update, so a frame's advance is known
   whenever recording turned on. */
static void gfx_frame_hook(void) {
    if (s_ring.pending != 0) {
        const nt_gfx_capture_view_t done = nt_gfx_capture_read();
        NT_ASSERT(done.counters.frame_sequence == s_ring.pending && "gfx recording: nt_devapi_update runs once per frame, after nt_gfx_begin_frame");
        store_frame(&done, s_ring.last_sim, s_ring.pending_advanced);
        s_ring.pending = 0;
    }
    const bool advanced = g_nt_app.frame != s_ring.last_sim;
    s_ring.last_sim = g_nt_app.frame;
    if (!s_ring.recording) {
        return;
    }
    if (g_nt_gfx.capture_recording) {
        s_ring.pending = g_nt_gfx.counters.frame_sequence;
        s_ring.pending_advanced = advanced;
    }
    nt_gfx_capture_request();
}

void nt_devapi_gfx_record(bool enabled) {
    if (enabled == s_ring.recording) {
        return;
    }
    s_ring.recording = enabled;
    s_ring.pending = 0; /* off: the frame recording now finishes unstored */
    if (enabled) {
        s_ring.oldest = 0;
        s_ring.count = 0;
        s_ring.head = 0;
        nt_gfx_capture_request();
    }
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

static bool cmd_gfx_frames(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)params;
    (void)err;
    (void)ud;
    devapi_add_bool(result, "recording", s_ring.recording);
    cJSON *frames = cJSON_AddArrayToObject(result, "frames");
    NT_ASSERT(frames != NULL);
    for (uint32_t i = 0; i < s_ring.count; i++) {
        const stored_frame *f = frame_at(i);
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
    return true;
}

static bool sim_frame_after(uint32_t a, uint32_t b) { return (int32_t)(a - b) > 0; }

/* Not stored: frame_not_ready when the request is newer than anything stored while recording, so it can
   still arrive; unknown_frame otherwise. */
static bool fail_not_stored(nt_devapi_error *err, bool newer) {
    if (s_ring.recording && newer) {
        return nt_devapi_fail_code(err, NT_DEVAPI_ERR_FRAME_NOT_READY, "gfx.read: the frame has not finished recording yet");
    }
    return nt_devapi_fail_code(err, NT_DEVAPI_ERR_UNKNOWN_FRAME, "gfx.read: the frame is not stored (evicted, before recording, or never recorded); gfx.frames lists the ring");
}

/* The stored frame of a gfx sequence; *newer when it is the frame recording for the ring or a later one. */
static const stored_frame *find_sequence(uint32_t sequence, bool *newer) {
    for (uint32_t i = 0; i < s_ring.count; i++) {
        if (frame_at(i)->view.counters.frame_sequence == sequence) {
            return frame_at(i);
        }
    }
    const uint64_t next = s_ring.pending != 0 ? s_ring.pending : g_nt_gfx.counters.frame_sequence + 1U;
    *newer = sequence >= next;
    return NULL;
}

/* The stored frame in which the sim advanced to sim_frame; *newer when that frame is still to come or recording. */
static const stored_frame *find_sim_frame(uint32_t sim_frame, bool *newer) {
    for (uint32_t i = 0; i < s_ring.count; i++) {
        if (frame_at(i)->advanced && frame_at(i)->sim_frame == sim_frame) {
            return frame_at(i);
        }
    }
    const bool recording_it = sim_frame == s_ring.last_sim && s_ring.pending != 0 && s_ring.pending_advanced;
    *newer = sim_frame_after(sim_frame, s_ring.last_sim) || recording_it;
    return NULL;
}

static bool cmd_gfx_read(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    const cJSON *sequence_node = cJSON_GetObjectItemCaseSensitive(params, "sequence");
    const cJSON *sim_node = cJSON_GetObjectItemCaseSensitive(params, "sim_frame");
    const char *bad = "gfx.read: give at most one of sequence or sim_frame, a non-negative integer";
    if (sequence_node != NULL && sim_node != NULL) {
        return nt_devapi_fail(err, bad);
    }
    const cJSON *node = sequence_node != NULL ? sequence_node : sim_node;
    uint32_t value = 0;
    if (node != NULL && !nt_devapi_parse_u32_param_exact(node, UINT32_MAX, err, bad, &value)) {
        return false;
    }
    bool newer = true;
    const stored_frame *found = NULL;
    if (sequence_node != NULL) {
        found = find_sequence(value, &newer);
    } else if (sim_node != NULL) {
        found = find_sim_frame(value, &newer);
    } else if (s_ring.count > 0) {
        found = frame_at(s_ring.count - 1U);
    }
    if (found == NULL) {
        return fail_not_stored(err, newer);
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
        .summary = "turn gfx command recording on or off; while on, every frame records into a ring of recent frames (turning it on clears the ring, off keeps it)",
        .params_shape = "{enabled:boolean}",
        .result_shape = "{}",
        .fn = cmd_gfx_record,
    },
    {
        .method = "gfx.frames",
        .summary = "list the recorded frames in the ring, oldest first",
        .params_shape = "{}",
        .result_shape = "{recording:boolean,frames:[{sequence:number,sim_frame:number,advanced:boolean,records:number,overflow:boolean}]}",
        .fn = cmd_gfx_frames,
    },
    {
        .method = "gfx.read",
        .summary = "the capture text of one recorded frame: the newest, by gfx sequence, or the frame where the simulation advanced to sim_frame; frame_not_ready while it is still recording",
        .params_shape = "{sequence?:number,sim_frame?:number}",
        .result_shape = "{sequence:number,sim_frame:number,text:string}",
        .fn = cmd_gfx_read,
    },
};

void nt_devapi_register_gfx(void) {
    memset(&s_ring, 0, sizeof(s_ring));
    s_ring.last_sim = g_nt_app.frame;
    nt_devapi_set_frame_hook(gfx_frame_hook);
    nt_devapi_register(k_gfx_cmds, NT_COUNTOF(k_gfx_cmds), NULL);
}
