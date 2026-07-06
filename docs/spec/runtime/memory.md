# Memory Policy

Memory rules: no heap allocation in the hot path, preallocated component
storages, resident asset metadata, transient pack blobs, and a per-frame bump
scratch arena (`nt_mem_scratch`). Capacities split into compile-time hard caps
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

### Frame scratch memory

Lifetime: from allocation until the next `nt_mem_scratch_reset()` (typically the start of the next frame, see [Frame Lifecycle — engine frame order](frame-lifecycle.md)).

Examples: render item arrays, temporary sort arrays, transient CPU batch buffers, build temp lists in render pass, `nt_ui_element_data_t` attached to CLAY elements via `NT_UI_DATA_*` macros.

The engine provides a global bump arena in `engine/memory/nt_mem_scratch`:

```c
nt_mem_scratch_init(NT_MEM_SCRATCH_DEFAULT_SIZE_BYTES); // boot, default 512 KB
// per frame (see frame-lifecycle.md):
nt_mem_scratch_reset();            // start of frame
nt_mem_scratch_alloc(size, align); // anywhere during the frame
// pointers stay valid until the next nt_mem_scratch_reset()
nt_mem_scratch_shutdown(); // exit
```

Type-safe macros: `NT_MEM_SCRATCH_ALLOC(T)`, `NT_MEM_SCRATCH_ALLOC_ARRAY(T, count)`.

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
