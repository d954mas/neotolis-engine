# Locked Decisions

Registry of locked architectural decisions. Convention: one decision per file in this
directory, named `NNNN-slug.md` with monotonically increasing numbers; the registry is
append-only — a decision that changes course gets a NEW number and supersedes the old
entry rather than editing it. Legacy locked decisions (`LD-*`) predate the per-file
convention and live only in this table; they graduate to numbered files if revisited.

Related: [0001-render-item-size.md](0001-render-item-size.md), [Core Principles](../core/principles.md), [Open Questions](../meta/open-questions.md)

These decisions are **locked** unless a strong reason appears:

| ID | Date | Decision | Source |
|---|---|---|---|
| 0001 | 2026-03-20 | `nt_render_item_t` stays 16 bytes; instance data read from component arrays at draw time (supersedes LD-28) | [0001-render-item-size.md](0001-render-item-size.md) |
| LD-01 | — | Web-first startup target | — |
| LD-02 | — | WebGL 2 as sole baseline (no WebGL 1) | — |
| LD-03 | — | Game-defined render loop | — |
| LD-04 | — | No system registry | — |
| LD-05 | — | Sparse unique component storages | — |
| LD-06 | — | Hierarchy lives in entity system | — |
| LD-07 | — | Builder is standalone native binary (C17 + vendored C++ behind extern "C") | — |
| LD-08 | — | Builder rules are code | — |
| LD-09 | — | Runtime formats are custom binary | — |
| LD-10 | — | Custom pack format (NTPACK) — flat binary, no ZIP | — |
| LD-11 | — | Manifest embedded in pack header | — |
| LD-12 | — | Material numeric params are `vec4[]` | — |
| LD-13 | — | No full duplicated MaterialRuntime object initially | — |
| LD-14 | — | Asset metadata is stable and resident | — |
| LD-15 | — | Render tags are game-defined | — |
| LD-16 | — | sort_key and batch_key are separate concepts | — |
| LD-17 | — | Depth only computed when sort policy needs it | — |
| LD-18 | — | Sprite batching belongs inside SpriteRenderer | — |
| LD-19 | — | Input is polling-based with pointer capture | — |
| LD-20 | — | Compile-time capacities, minimal runtime settings | — |
| LD-21 | — | Audio is engine module, not game-side | — |
| LD-22 | — | Audio public API is handle-based, zero platform types | — |
| LD-23 | — | audio_clip_create contract is always potentially async | — |
| LD-24 | — | audio_try_resume() exists on all platforms | — |
| LD-25 | — | Audio format in packs is OGG Vorbis | — |
| LD-26 | — | Audio internal structures are per-platform, hidden from game code | — |
| LD-27 | — | Voice pool with oldest-non-looping eviction | — |
| LD-28 | — | RenderItem stores world_matrix by value, not pointer — **superseded by 0001** (thin 16-byte item, no inline matrix) | — |
| LD-29 | — | Async loading is non-blocking with PackState/AssetState machines | — |
| LD-30 | — | Eager asset activation with per-frame rate-limit | — |
