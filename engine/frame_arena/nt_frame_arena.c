#include "frame_arena/nt_frame_arena.h"

#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"
#include "log/nt_log.h"

static struct {
    uint8_t *staging;
    nt_buffer_t buffer;
    uint64_t gfx_frame; /* gfx frame_sequence of the last begin_frame */
    uint32_t capacity;
    uint32_t cursor;
    uint32_t peak;
    bool uploaded;
    bool initialized;
} s_frame_arena;

// #region lifecycle
static nt_result_t create_buffer(void) {
    s_frame_arena.buffer = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_VERTEX,
        .usage = NT_USAGE_STREAM,
        .size = s_frame_arena.capacity,
        .label = "frame_arena",
    });
    return s_frame_arena.buffer.id != 0 ? NT_OK : NT_ERR_INIT_FAILED;
}

static void destroy_buffer(void) {
    if (s_frame_arena.buffer.id != 0) {
        nt_gfx_destroy_buffer(s_frame_arena.buffer);
    }
    s_frame_arena.buffer = (nt_buffer_t){0};
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_ASSERT expansion inflates the metric
nt_result_t nt_frame_arena_init(const nt_frame_arena_desc_t *desc) {
    NT_ASSERT(!s_frame_arena.initialized);
    NT_ASSERT(desc != NULL);
    NT_ASSERT(desc->capacity > 0 && (desc->capacity % NT_FRAME_ARENA_ALIGN) == 0);
    NT_ASSERT(g_nt_gfx.initialized && "nt_frame_arena_init: nt_gfx_init must run first");

    memset(&s_frame_arena, 0, sizeof(s_frame_arena));
    s_frame_arena.capacity = desc->capacity;
    /* Uploads include padding: initialize it once; later frames may reuse old payload bytes. */
    s_frame_arena.staging = (uint8_t *)calloc(desc->capacity, 1);
    if (!s_frame_arena.staging) {
        NT_LOG_ERROR("failed to allocate frame arena staging");
        return NT_ERR_INIT_FAILED;
    }
    if (create_buffer() != NT_OK) {
        free(s_frame_arena.staging);
        s_frame_arena.staging = NULL;
        NT_LOG_ERROR("failed to create frame arena buffer");
        return NT_ERR_INIT_FAILED;
    }
    s_frame_arena.initialized = true;
    return NT_OK;
}

void nt_frame_arena_shutdown(void) {
    if (!s_frame_arena.initialized) {
        return;
    }
    destroy_buffer();
    free(s_frame_arena.staging);
    memset(&s_frame_arena, 0, sizeof(s_frame_arena));
}

nt_result_t nt_frame_arena_restore_gpu(void) {
    if (!s_frame_arena.initialized) {
        return NT_OK;
    }
    destroy_buffer();
    s_frame_arena.uploaded = false; /* the new buffer is empty: draws wait for the next upload */
    return create_buffer();
}
// #endregion

// #region frame
void nt_frame_arena_begin_frame(void) {
    NT_ASSERT(s_frame_arena.initialized);
    /* Repeated preparation can overwrite ranges already used by this frame's draws. */
    NT_ASSERT(s_frame_arena.gfx_frame != g_nt_gfx.counters.frame_sequence && "frame_arena: begin_frame twice in one gfx frame");
    s_frame_arena.gfx_frame = g_nt_gfx.counters.frame_sequence;
    s_frame_arena.cursor = 0;
    s_frame_arena.uploaded = false;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_ASSERT expansion inflates the metric
void *nt_frame_arena_reserve(uint32_t size, uint32_t *out_offset) {
    NT_ASSERT(out_offset != NULL);
    NT_ASSERT(size > 0);
    NT_ASSERT(!s_frame_arena.uploaded && "frame_arena: reserve after upload; begin_frame first");
    uint32_t offset = s_frame_arena.cursor;
    uint32_t aligned = (size + (NT_FRAME_ARENA_ALIGN - 1U)) & ~(NT_FRAME_ARENA_ALIGN - 1U);
    if (aligned < size || aligned > s_frame_arena.capacity - offset) {
        NT_LOG_ERROR("frame_arena: capacity exceeded: need %u bytes, %u free of %u", size, s_frame_arena.capacity - offset, s_frame_arena.capacity);
        NT_ASSERT(0 && "frame_arena: capacity exceeded");
    }
    s_frame_arena.cursor = offset + aligned;
    *out_offset = offset;
    return s_frame_arena.staging + offset;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_ASSERT expansion inflates the metric
void nt_frame_arena_upload(void) {
    NT_ASSERT(s_frame_arena.buffer.id != 0 && "retry failed GPU restore before uploading");
    NT_ASSERT(!s_frame_arena.uploaded && "frame_arena: second upload in one frame");
    s_frame_arena.uploaded = true;
    if (s_frame_arena.cursor > s_frame_arena.peak) {
        s_frame_arena.peak = s_frame_arena.cursor;
    }
    if (s_frame_arena.cursor > 0) {
        nt_gfx_update_buffer(s_frame_arena.buffer, 0, s_frame_arena.staging, s_frame_arena.cursor);
    }
}

nt_buffer_t nt_frame_arena_buffer(void) {
    NT_ASSERT(s_frame_arena.uploaded && "frame_arena: draw before upload");
    return s_frame_arena.buffer;
}

uint32_t nt_frame_arena_peak(void) {
    NT_ASSERT(s_frame_arena.initialized);
    return s_frame_arena.peak;
}
// #endregion
