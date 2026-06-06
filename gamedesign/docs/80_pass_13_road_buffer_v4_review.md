# 80. Pass 13 road/buffer V4 review

Status: GDD review complete after revised V4 cleanup.

Decision:

```text
V4 GEOMETRY ACCEPTED
V4 CLEANUP ACCEPTED AFTER REGENERATION
V4 ROAD/BUFFER APPROVED FOR RUNTIME CANDIDATE COPY
```

Reviewed files:

```text
gamedesign/assets/concept/pass_13_generated_world_foundation/pass_13_road_buffer_contact_v4.png
gamedesign/assets/concept/pass_13_generated_world_foundation/pass_13_road_buffer_grid_preview_v4.png
gamedesign/assets/concept/pass_13_generated_world_foundation/pass_13_road_buffer_reuse_map_v4.md
gamedesign/assets/concept/pass_13_generated_world_foundation/proposed_runtime_v4/road/
```

## What Works

- V4 reuses the V3 generated bitmap road source instead of generating unrelated one-off road files.
- Road geometry now follows a shared centerline and shared body width.
- Straight/corner exits connect in the grid preview.
- Technical masks are correctly treated as alignment tools, not final visual style.
- Revised V4 removed the earlier black/magenta/red cleanup artifacts.
- Current grid preview reads as one continuous loop segment at 128x128 tile scale.

## Previous Rejection Superseded

The first V4 check failed because:

```text
black/magenta/red artifacts remain near several road seams
some tile edges show harsh cut/cleanup blocks
grid preview makes cleanup artifacts obvious at loop scale
contact sheet candidates still show dark blocks on several corners/straight edges
```

Art regenerated V4 after cleanup changes. The current files dated `2026-06-06 15:58` supersede the failed V4 artifacts.

## Approved Runtime Candidate Files

Approved for Code copy from:

```text
gamedesign/assets/concept/pass_13_generated_world_foundation/proposed_runtime_v4/road/
```

to:

```text
games/turkic-jam-2026/raw/road/road_straight_ns.png
games/turkic-jam-2026/raw/road/road_straight_ew.png
games/turkic-jam-2026/raw/road/road_corner_ne.png
games/turkic-jam-2026/raw/road/road_corner_es.png
games/turkic-jam-2026/raw/road/road_corner_sw.png
games/turkic-jam-2026/raw/road/road_corner_wn.png
games/turkic-jam-2026/raw/road/buffer_edge_stones_01.png
games/turkic-jam-2026/raw/road/buffer_packed_sand_01.png
```

## Required Code Verification

After copying the approved candidates:

```text
build_turkic_jam_packs
cmake --build build/_cmake/native-debug --target turkic_jam
desktop runtime dump in normal gameplay state
```

GDD must review the runtime screenshot before final L5 acceptance. This approval is for runtime candidate copy, not final visual acceptance.
