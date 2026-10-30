#ifndef NT_MEM_SCRATCH_H
#define NT_MEM_SCRATCH_H

#include <stddef.h>
#include <stdint.h>

/* Global nt_arena_t (memory/nt_arena.h) the engine and game share, reset every frame.
 * All allocations live until the next reset -- pointers from a prior frame are stale.
 * No per-alloc free.
 *
 * Frame loop placement:
 *
 *   int main(void) {
 *       nt_mem_scratch_init(256U * 1024U);
 *       while (running) {
 *           nt_mem_scratch_reset();   // start of frame, BEFORE any alloc
 *           game_update();            // CLAY({...}), NT_UI_DATA_*, etc.
 *           nt_ui_walk(...);          // reads scratch pointers
 *           // render done; scratch may sit unused until next reset
 *       }
 *       nt_mem_scratch_shutdown();
 *   }
 *
 * Reset MUST happen BEFORE any allocation in the current frame -- never
 * after. Calling alloc then reset in the same frame invalidates pointers
 * already handed to systems (nt_ui) that retain them through walk. */
void nt_mem_scratch_init(size_t size_bytes);
void nt_mem_scratch_shutdown(void);

/* Pointers handed out before the reset become stale. */
void nt_mem_scratch_reset(void);

/* align: power of 2, <= _Alignof(max_align_t). Asserts on overflow / out-of-space. */
void *nt_mem_scratch_alloc(size_t size, size_t align);

/* Checked-multiply variant: asserts elem_size*count doesn't overflow. */
void *nt_mem_scratch_alloc_array(size_t elem_size, size_t count, size_t align);

/* mark: an earlier nt_mem_scratch_used(). Frees everything allocated after it, by any
 * caller -- rewind only around allocations whose consumers end before the rewind. */
void nt_mem_scratch_rewind(size_t mark);

/* Peak `used` since init. Survives resets so callers can size scratch budget. */
size_t nt_mem_scratch_high_water_mark(void);

#define NT_MEM_SCRATCH_ALLOC(T) ((T *)nt_mem_scratch_alloc(sizeof(T), _Alignof(T)))

#define NT_MEM_SCRATCH_ALLOC_ARRAY(T, count) ((T *)nt_mem_scratch_alloc_array(sizeof(T), (size_t)(count), _Alignof(T)))

/* Current `used` byte count; resets each frame. Doubles as a rewind mark. */
size_t nt_mem_scratch_used(void);

#endif /* NT_MEM_SCRATCH_H */
