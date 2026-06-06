# 77. Pass 13 road/buffer V3 review

Status: GDD review complete.

Decision:

```text
V3 SOURCE DIRECTION PARTIALLY ACCEPTED
V3 RUNTIME ROAD/BUFFER SLICES REJECTED
NO RAW OVERWRITE
```

Reviewed files:

```text
gamedesign/assets/concept/pass_13_generated_world_foundation/pass_13_road_buffer_source_v3.png
gamedesign/assets/concept/pass_13_generated_world_foundation/pass_13_road_buffer_contact_v3.png
gamedesign/assets/concept/pass_13_generated_world_foundation/pass_13_road_buffer_reuse_map_v3.md
gamedesign/assets/concept/pass_13_generated_world_foundation/proposed_runtime_v3/road/
tmp/pass13_v3_road_grid_preview.png
```

## What Works

- V3 is generated bitmap / painted-source bitmap, not SVG-looking art.
- It uses one road/buffer source sheet instead of per-file one-off generation.
- Road material is more natural than the old technical road.
- Buffer candidates are lower-profile than V2 and less wall-like.

## Why Runtime Slices Are Rejected

The isolated candidates look acceptable, but the 128x128 grid preview fails.

Observed in `tmp/pass13_v3_road_grid_preview.png`:

```text
corner exits do not align with straight road centers
corner road width does not match straight road width
some road endpoints stop before the tile edge or meet at different vertical/horizontal positions
magenta/red cleanup remnants remain around several edges
stone-heavy road edges may become noisy/repetitive in a full loop
```

This means the runtime loop would read as disconnected painted fragments, not one continuous path.

## Required V4

Create V4 as proposal-only. Do not overwrite `games/turkic-jam-2026/raw/*`.

Required deliverables:

```text
gamedesign/assets/concept/pass_13_generated_world_foundation/pass_13_road_buffer_contact_v4.png
gamedesign/assets/concept/pass_13_generated_world_foundation/pass_13_road_buffer_grid_preview_v4.png
gamedesign/assets/concept/pass_13_generated_world_foundation/pass_13_road_buffer_reuse_map_v4.md
gamedesign/assets/concept/pass_13_generated_world_foundation/proposed_runtime_v4/road/
```

Acceptance criteria:

```text
road exits meet tile edges exactly
all road exits share one centerline and one body width
straight/corner pieces connect cleanly in a 3x3 grid preview
road material remains generated bitmap, not flat procedural fill
technical masks are allowed only for shape/alignment
no magenta/red cleanup remnants at runtime size
buffer remains no-build edge, not road and not wall
contact includes current raw vs proposed v4
grid preview includes at least one closed loop segment
```

## V4 Construction Guidance

Use a generated road material as visible fill, but enforce geometry with a shared technical mask:

```text
road body target: one fixed width, e.g. 50-56 px
centerline: exactly tile center for all exits
straight_ns exits: top center and bottom center
straight_ew exits: left center and right center
corner_ne exits: top center and right center
corner_es exits: right center and bottom center
corner_sw exits: bottom center and left center
corner_wn exits: left center and top center
```

The mask may be technical. The visible road surface inside it must come from generated bitmap road material.

