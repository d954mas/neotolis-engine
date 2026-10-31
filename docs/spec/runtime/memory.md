# Memory Policy

Memory rules: no heap allocation in the hot path, preallocated component
storages, resident asset metadata, transient pack blobs, fixed-capacity bump
arenas (`nt_arena_t`) and one global per-frame arena (`g_nt_mem_scratch`). Capacities split into compile-time hard caps
and init-time component capacities chosen by the game.

Related: [Frame Lifecycle](frame-lifecycle.md), [Component Storage](../data/component-storage.md), [Entity System](../data/entity.md)

## High-level memory rules

- no heap allocation in hot path if avoidable
- component storages preallocated
- asset metadata always resident
- pack blobs transient
- frame temporary memory reset every frame
- renderer staging/batch buffers explicitly sized
- resource pools managed centrally

## Memory categories

### Permanent memory

Lifetime ≈ engine/application lifetime.

Examples: entity tables, component storages, asset metadata, shader metadata, persistent runtime pools.

### Pack/blob transient memory

Lifetime ≈ load operation or recent-use cache window.

Examples: loaded pack blob, manifest read buffer, temporary decompression buffer if ever needed.

### Arenas

Lifetime: from allocation until the owner's next `nt_arena_reset` or a rewind
to a mark taken before the allocation.

`nt_arena_t` (`engine/memory/nt_arena.h`) is a fixed-capacity bump allocator
over memory its owner provides: a static array, a block from another arena or
the owner's own `malloc`. The arena never allocates, grows or frees; the owner
frees the memory. A game or module creates one when its allocations need a
lifetime independent of frame scratch (an event log, a load-time workspace).

```c
alignas(max_align_t) static uint8_t buf[64 * 1024];
nt_arena_t arena;
nt_arena_init(&arena, buf, sizeof(buf)); // base aligned to max_align_t
void *p = nt_arena_alloc(&arena, size, align);
nt_arena_reset(&arena);                  // p is stale
```

Allocation aligns `align` (a power of 2, at most `_Alignof(max_align_t)`)
relative to `base`. Running out of space is a configuration error and asserts.
`used` and `high_water` are readable fields; `high_water` is the peak `used`
since init and survives reset and rewind. Typed macros pass `sizeof(T)` and
`_Alignof(T)`: `NT_ARENA_ALLOC(arena, T)`, `NT_ARENA_ALLOC_ARRAY(arena, T, count)`.

### Scoped rewind

Lifetime: from allocation until the rewind that closes its scope.

A mark is an earlier `used` of the same arena. `nt_arena_rewind(arena, mark)`
returns the head to the mark in O(1); marks nest. A mark is invalid after a reset or an
earlier rewind below it; the assert catches one only while it lies past the
current head. A rewind frees everything allocated after the mark by any caller,
so it closes only a scope whose allocations are consumed before it and that
runs no foreign code which could keep its own allocations. Example: the
rich-text solve rewinds its glyph atoms after placing them; it calls no game
callback, since object sizes are data.

### Frame scratch memory

Lifetime: from allocation until the next `nt_arena_reset(&g_nt_mem_scratch)` (typically the start of the next frame, see [Frame Lifecycle — engine frame order](frame-lifecycle.md)), or a scoped rewind.

Examples: `nt_ui_element_data_t` attached to CLAY elements via `NT_UI_DATA_*`
macros, widget payloads read by `nt_ui_walk`, game per-frame temporaries.

`g_nt_mem_scratch` (`engine/memory/nt_mem_scratch.h`) is one global
`nt_arena_t` that any engine module or game code may allocate from. It is used
through the `nt_arena_*` functions and fields like any arena, and the game owns
its memory like any arena's (a static buffer or its own malloc):

```c
alignas(max_align_t) static uint8_t s_scratch_mem[256 * 1024];  // the game picks the size
nt_arena_init(&g_nt_mem_scratch, s_scratch_mem, sizeof s_scratch_mem);  // boot
// per frame (see frame-lifecycle.md):
nt_arena_reset(&g_nt_mem_scratch);               // start of frame
nt_arena_alloc(&g_nt_mem_scratch, size, align);  // anywhere during the frame
// pointers stay valid until the next reset or a rewind below them
```

### Frame storage

Lifetime: from `nt_gfx_frame_alloc`, between `nt_gfx_begin_frame` and `nt_gfx_end_frame`, until the next `nt_gfx_begin_frame`.

Per-frame GPU data (vertices, instances, indices, uniform blocks) is
bump-allocated from gfx frame storage instead of scratch: each stream is a CPU
staging copy plus a GPU buffer of `nt_gfx_desc_t.frame_capacity[stream]` bytes,
so a byte of capacity costs a byte of heap and a byte of GPU memory, and a
zero capacity disables the stream. The game
sizes it from `nt_gfx_counters_t.frame_bytes`; overflow stops the program. See
[Frame storage](../render/architecture.md#frame-storage).

## Capacity policy

Capacities split into two tiers.

**Compile-time hard caps.** Sized at build time. Used where a single global
sparse table or fixed pool needs a known upper bound. Cannot be overridden
without recompiling the engine.

```c
#define NT_MAX_ENTITIES 65536 /* sparse side, generation tables */
#define NT_MAX_PACKS 16
#define NT_MAX_SLOTS 2048
#define NT_MAX_ASSETS 2048
#define NT_MAX_RENDER_ITEMS 16384
#define NT_MAX_AUDIO_CLIPS 256
#define NT_MAX_AUDIO_VOICES 32
#define NT_MAX_POINTERS 8
```

**Init-time component capacities.** Each component module exposes a
descriptor and a defaults helper. The game picks the size at init based
on its scene density. Allocation happens once in init (`calloc`), never
grows after — the "preallocated, no heap in hot path" rule from [High-level memory rules](#high-level-memory-rules)
still holds.

```c
nt_sprite_comp_init(&(nt_sprite_comp_desc_t){ .capacity = 4096 });
/* or use defaults: */
nt_sprite_comp_init(&nt_sprite_comp_desc_defaults());
```

Suggested baseline defaults (override per game):

| Component       | Default | Notes                          |
|-----------------|---------|--------------------------------|
| transform_comp  | 256     | Adjust for scene density       |
| drawable_comp   | 256     |                                |
| mesh_comp       | 256     |                                |
| material_comp   | 256     |                                |
| sprite_comp     | 256     |                                |
| text_comp       | 64      |                                |
| shadow_comp     | 256     |                                |

Component capacity must not exceed `NT_MAX_ENTITIES` — the sparse side is
sized off the entity table, not the component capacity.

## Runtime settings

```c
typedef struct EngineSettings
{
    float fixed_dt;
    int max_fixed_steps;
} EngineSettings;
```

No full config system (JSON/YAML/ini) required at start.
