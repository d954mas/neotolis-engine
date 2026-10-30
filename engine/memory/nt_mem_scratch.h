#ifndef NT_MEM_SCRATCH_H
#define NT_MEM_SCRATCH_H

#include <stddef.h>

#include "memory/nt_arena.h"

/* The global arena the engine and game share, reset every frame. Use it through
 * nt_arena_* and its fields; allocations live until the next reset or a rewind
 * below them -- pointers from a prior frame are stale.
 *
 *   int main(void) {
 *       nt_mem_scratch_init(256U * 1024U);
 *       while (running) {
 *           nt_arena_reset(&g_nt_mem_scratch); // start of frame, BEFORE any alloc
 *           game_update();                     // CLAY({...}), NT_UI_DATA_*, etc.
 *           nt_ui_walk(...);                   // reads scratch pointers
 *       }
 *       nt_mem_scratch_shutdown();
 *   }
 *
 * Reset MUST happen BEFORE any allocation in the current frame -- never
 * after. Calling alloc then reset in the same frame invalidates pointers
 * already handed to systems (nt_ui) that retain them through walk. */
extern nt_arena_t g_nt_mem_scratch;

/* Mallocs the arena memory; shutdown frees it. */
void nt_mem_scratch_init(size_t size_bytes);
void nt_mem_scratch_shutdown(void);

#define NT_MEM_SCRATCH_ALLOC(T) ((T *)nt_arena_alloc(&g_nt_mem_scratch, sizeof(T), _Alignof(T)))

#define NT_MEM_SCRATCH_ALLOC_ARRAY(T, count) ((T *)nt_arena_alloc_array(&g_nt_mem_scratch, sizeof(T), (size_t)(count), _Alignof(T)))

#endif /* NT_MEM_SCRATCH_H */
