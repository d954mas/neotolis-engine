#ifndef NT_GFX_TEST_NOTES_H
#define NT_GFX_TEST_NOTES_H

#include "graphics/nt_gfx.h"

#include <string.h>

#if NT_GFX_CAPTURE_ENABLED
/* Index of the first NOTE labelled label at or after from in the live capture; its count when none. */
static inline uint32_t nt_test_note(uint32_t from, const char *label) {
    const nt_gfx_capture_view_t v = nt_gfx_capture_read();
    for (uint32_t i = from; i < v.count; i++) {
        if (v.events[i].kind == NT_GFX_EVENT_NOTE && strcmp(v.events[i].note.label, label) == 0) {
            return i;
        }
    }
    return v.count;
}

/* The field named key of the NOTE at index note; NULL when the note has none. */
static inline const nt_gfx_event_t *nt_test_note_field(uint32_t note, const char *key) {
    const nt_gfx_capture_view_t v = nt_gfx_capture_read();
    for (uint32_t i = note + 1U; i < v.count && v.events[i].kind == NT_GFX_EVENT_NOTE_FIELD; i++) {
        if (strcmp(v.events[i].field.key, key) == 0) {
            return &v.events[i];
        }
    }
    return NULL;
}

static inline uint32_t nt_test_note_u32(uint32_t note, const char *key) {
    const nt_gfx_event_t *field = nt_test_note_field(note, key);
    return field != NULL && field->field.type == NT_GFX_NOTE_U32 ? field->field.value.u32 : UINT32_MAX;
}

static inline const char *nt_test_note_str(uint32_t note, const char *key) {
    const nt_gfx_event_t *field = nt_test_note_field(note, key);
    return field != NULL && field->field.type == NT_GFX_NOTE_STR ? field->field.value.str : "";
}

/* Result of the first draw after the NOTE at index note and before the next NOTE; NT_GFX_RESULT_NONE when none. */
static inline nt_gfx_result_t nt_test_next_draw_result(uint32_t note) {
    const nt_gfx_capture_view_t v = nt_gfx_capture_read();
    for (uint32_t i = note + 1U; i < v.count && v.events[i].kind != NT_GFX_EVENT_NOTE; i++) {
        const nt_gfx_event_t *e = &v.events[i];
        if (e->kind == NT_GFX_EVENT_RESULT && e->operation >= NT_GFX_OP_DRAW && e->operation <= NT_GFX_OP_DRAW_INDEXED_INSTANCED) {
            return e->result;
        }
    }
    return NT_GFX_RESULT_NONE;
}
#endif

#endif /* NT_GFX_TEST_NOTES_H */
