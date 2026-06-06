# 71. Pass 13 World Foundation Source Review

Status: source accepted with restrictions; slicing not approved yet.

Reviewed source:

```text
gamedesign/assets/concept/pass_13_generated_world_foundation/pass_13_world_foundation_source.png
```

Parent docs:

```text
68_pass_13_world_foundation_contract.md
69_pass_13_world_foundation_source_review.md
70_gameplay_map_zoom_visual_review.md
```

## Decision

```text
ACCEPT SOURCE WITH CHANGES BEFORE SLICING
```

The image is a valid generated bitmap / painted-source bitmap sheet. It is not SVG-looking, not procedural, and it is not a set of repeated one-off UI elements.

However, runtime slicing is not approved yet. The source is good enough as a material family base, but several parts must be treated carefully or avoided.

## What Works

| Area | Review |
| --- | --- |
| Production rule | Passes: source is generated bitmap/painterly, not script art. |
| World family coherence | Passes: sand, decor, road, buffer and aul components share one visual language. |
| Road material | Strong: packed road strips/corners are readable and much better than current technical tiles. |
| Buffer candidates | Strong in principle: rocks, packed edges and small stakes can define no-build edge. |
| Decor library | Useful: dunes, rocks, dry grass, tracks, cracks and bones are present. |
| Aul components | Strong mood: yurts, fire and practical camp objects read as steppe camp assets. |

## Risks

| Risk | Severity | Required handling |
| --- | --- | --- |
| Sand swatches are too noisy for base cells | Medium | Use the quietest swatches only; reduce contrast during slicing if needed. |
| Yurts are large and detailed | Medium | Downscale/crop carefully; starting aul must remain small camp, not hero-object centerpiece. |
| Road buffer stakes can read as fence/wall | Medium | Use stakes sparsely; prefer stones/packed edge for MVP buffer. |
| Decor clusters can read as active tiles | Medium | Keep decor low contrast and small; do not use the biggest rock/grass clusters as empty-cell decor. |
| Magenta cleanup around thin grass/stakes/fire | Medium | Validate alpha edges before runtime export. |
| Road pieces have painterly edge dust | Low/Medium | Make straight/corner widths consistent during slicing. |

## Approved Source Areas

Use these first:

```text
quiet sand swatches for ground
packed road straight and corner pieces
low stone/sand strips for road buffer
small stones, soft tracks, small cracks for decor
small campfire and practical camp props for aul detail
```

Use carefully:

```text
large yurts
big grass/shrub clusters
bone cluster
stake lines with rope
large circular aul ground patch
blue-highlight circular patch
```

Do not use for MVP runtime unless specifically approved:

```text
large blue circular marker as ground/current highlight
large decorative yurt ornaments as gameplay signs
long rope fence as continuous road buffer
oversized rock clusters as empty decor
```

## Slicing Guidance

Before any runtime overwrite, create a contact sheet that compares:

```text
current technical raw asset
generated Pass 13 candidate
target runtime size
intended map role
```

For the first slicing proposal, prioritize these files:

```text
raw/ground/ground_sand_base_01.png
raw/decor/decor_dune_01.png
raw/decor/decor_stones_01.png
raw/decor/decor_dry_grass_01.png
raw/decor/decor_tracks_01.png
raw/decor/decor_cracks_01.png
raw/road/road_straight_ns.png
raw/road/road_straight_ew.png
raw/road/road_corner_ne.png
raw/road/road_corner_es.png
raw/road/road_corner_sw.png
raw/road/road_corner_wn.png
raw/road/buffer_edge_stones_01.png
raw/road/buffer_packed_sand_01.png
raw/aul/aul_ground_2x2.png
raw/aul/aul_fire_01.png
```

Defer or handle as second pass:

```text
raw/road/buffer_stakes_01.png
raw/road/buffer_cart_marks_01.png
raw/aul/aul_yurt_small_01.png
raw/aul/aul_yurt_small_02.png
raw/road/road_current_highlight.png
raw/decor/decor_bones_01.png
```

Reason: these are more likely to become too active, too large, or too symbolic at the current 2x map scale.

## Runtime Gate

Runtime overwrite remains blocked until:

1. a reuse map exists;
2. contact sheet exists;
3. exact dimensions are validated;
4. GDD accepts the slicing proposal;
5. pack/L5 desktop screenshot can compare against `tmp/map_zoom_game_check_done.png`.

## Message To Art

Proceed to slicing proposal, not runtime overwrite.

Deliver:

```text
pass_13_world_foundation_contact_sheet.png
pass_13_world_foundation_reuse_map.md
optional crop map / slicing tool
```

Do not write to `games/turkic-jam-2026/raw/*` yet.
