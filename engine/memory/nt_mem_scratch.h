#ifndef NT_MEM_SCRATCH_H
#define NT_MEM_SCRATCH_H

#include "memory/nt_arena.h"

/* The global arena the engine and game share, reset every frame. The game owns its
 * memory like any arena's; allocations live until the next reset or a rewind below
 * them -- pointers from a prior frame are stale.
 *
 *   alignas(max_align_t) static uint8_t s_scratch_mem[256U * 1024U];
 *
 *   int main(void) {
 *       nt_arena_init(&g_nt_mem_scratch, s_scratch_mem, sizeof s_scratch_mem);
 *       while (running) {
 *           nt_arena_reset(&g_nt_mem_scratch); // start of frame, BEFORE any alloc
 *           game_update();                     // CLAY({...}), NT_UI_DATA_*, etc.
 *           nt_ui_walk(...);                   // reads scratch pointers
 *       }
 *   }
 *
 * Reset MUST happen BEFORE any allocation in the current frame -- never
 * after. Calling alloc then reset in the same frame invalidates pointers
 * already handed to systems (nt_ui) that retain them through walk. */
extern nt_arena_t g_nt_mem_scratch;

#endif /* NT_MEM_SCRATCH_H */
