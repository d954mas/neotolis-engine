# ADR: Render Item Size and Instance Packing Strategy

**Status:** Accepted
**Date:** 2026-03-20
**Context:** Phase 27 — Mesh Rendering Pipeline

## Decision

`nt_render_item_t` stays at 16 bytes. Instance data (world_matrix, color) is read from component arrays via entity lookup at draw time (scattered access). No fat render items, no pre-cached dense indices, no GPU data textures.

## Render Item Layout (16 bytes)

```c
typedef struct {
    uint64_t sort_key;    // 8 — ordering (material+mesh for opaque, depth for transparent)
    uint32_t entity;      // 4 — raw entity id
    uint32_t batch_key;   // 4 — instancing compatibility (independent of sort order)
} nt_render_item_t;
```

## Why Not Fat Items (~96 bytes with inline mat4+color)

Different renderers need different per-instance data:

| Renderer | Instance data | Size |
|----------|--------------|------|
| Mesh | mat4 world + vec4 color | 80B |
| Sprite | vec2 pos + vec2 scale + float rot + vec4 color + vec4 uv | 44B |
| Spine | mat4 bones[N] | 256B-4KB |

A universal fat item would require a union of all variants — hundreds of bytes, wasted memory, killed sort performance. Per-renderer typed items break universal sorting (can't depth-sort mixed mesh+sprite in one list for transparency).

16-byte thin item is universal: one list, one sort, any renderer.

## Why Not Per-Renderer Fat Data Arrays (Indirect Sort)

Collect phase fills a dense per-renderer array, sort references it by index. Draw reads sequentially.

The scattered access doesn't disappear — it moves from draw to collect. At single-pass rendering, total CPU time is the same (~400us at 20K entities). Benefit only appears with multi-pass rendering (shadow + main + post), which the engine doesn't have yet.

## Why Not Cached Dense Indices in Render Item

```c
// hypothetical 24-byte item
uint16_t transform_idx;  // skip sparse lookup
uint16_t drawable_idx;
```

Saves ~100-150us at 20K entities (removes sparse→dense indirection). But scatter across data arrays remains. Marginal improvement, not worth the complexity and item size increase (16→24 bytes, 50% more memory, slower sort).

## Why Not GPU Data Textures (texelFetch)

Upload all transforms as RGBA32F texture, pass entity_id as instance attribute, shader does `texelFetch`.

Eliminates CPU scatter entirely. But:
- Mobile WebGL 2: RGBA32F textures unreliable on cheap Mali/Adreno (precision loss, driver bugs)
- Vertex texture fetch on mobile TBDR: not free, hits texture cache instead of vertex fetch unit
- Adds shader complexity, consumes texture unit

Current VBO instancing (`glDrawElementsInstanced` + `glBufferSubData`) works 100% reliably on all WebGL 2 targets including mobile.

## Performance Analysis at Scale

At 20K entities per frame:

| Stage | Current (16B item) | Fat item (96B) |
|-------|-------------------|----------------|
| Sort (qsort) | ~1-2ms (320KB moved) | ~6-8ms (1.9MB moved) |
| Instance packing | ~400us (scattered) | ~50us (sequential) |
| Buffer upload | ~0.5-1ms | ~0.5-1ms |
| **Total CPU** | **~3-4ms** | **~7-10ms** |

Sort dominates. Thin items win overall.

## Real Bottleneck

WebGL 2 chokes at 2000-3000 draw calls. The mesh renderer's batch_key + instancing groups 20K objects into 50-100 instanced calls. This is the critical optimization — not scattered memory access.

## Scaling Path (When Needed)

If CPU profiling shows instance packing as bottleneck (50K+ entities):

1. **First:** Cached dense indices (Variant 1) — cheapest change, removes one indirection level
2. **Then:** Indirect sort with per-renderer fat arrays (Variant 2) — if multi-pass rendering is added
3. **Endgame:** GPU data textures (Variant 3) — only if targeting high-end WebGL 2 / future WebGPU

Current architecture (thin item + batch_key) is ready for all three upgrades without breaking changes.
