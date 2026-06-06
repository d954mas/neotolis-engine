# 73. Pass 13 slicing proposal review

Status: GDD review of Pass 13 proposal-only slicing deliverables.

Decision:

```text
SOURCE ACCEPTED
REUSE PROTOCOL ACCEPTED
SLICING PROPOSAL REJECTED FOR RUNTIME OVERWRITE
```

No `games/turkic-jam-2026/raw/*` overwrite is approved yet.

## Reviewed Files

```text
gamedesign/assets/concept/pass_13_generated_world_foundation/pass_13_world_foundation_source.png
gamedesign/assets/concept/pass_13_generated_world_foundation/pass_13_world_foundation_reuse_map.md
gamedesign/assets/concept/pass_13_generated_world_foundation/pass_13_world_foundation_slicing_proposal_contact.png
gamedesign/assets/concept/pass_13_generated_world_foundation/proposed_runtime/
```

## What Works

- The source sheet is generated bitmap / painted-source bitmap, not SVG or procedural art.
- The pass now follows the mandatory reuse protocol.
- `reuse_map.md` exists and lists runtime path, source crop, reuse operation and risk.
- The proposal correctly avoids runtime/raw overwrite.
- The contact sheet compares current raw against generated candidates at target size.

## Why Runtime Slicing Is Rejected

The current candidate crops are not clean enough for gameplay.

| Area | Issue | Required correction |
| --- | --- | --- |
| `decor_dune_01` | crop includes loose fragments at the right edge; reads like multiple objects, not one quiet dune | crop one quiet low dune only, no side fragments |
| `decor_stones_01` | includes many warm clustered objects, can read as active pickup/landmark | use fewer, smaller, quieter stones |
| `decor_dry_grass_01` | reads like multiple active bushes/flames at runtime scale | reduce to low dry grass silhouette, less contrast |
| `decor_tracks_01` | crop includes object clusters and curved marks; too close to event/track tile | isolate subtle old tracks only |
| `decor_cracks_01` | high-contrast cracks plus extra light fragments; can pull attention | use quieter cracks, no separate bright fragments |
| `road_straight_ns` | crop includes non-road objects beside the road | road crop must contain road material only |
| `road_straight_ew` | crop includes fire/object residue on the left and inconsistent strip width | road crop must contain road material only, same width as vertical |
| road corners | curve material is painterly, but widths/alignment are not proven consistent with straight road | create consistent road family with matching centerline/edge width |
| `buffer_edge_stones_01` | good direction but too line-like; can become a wall if repeated | break rhythm, lower contrast, keep sparse edge stones |
| `buffer_packed_sand_01` | currently reads like a separate thin road strip | make it no-build packed edge, lower contrast than road |
| `aul_ground_2x2` | usable direction, but may be too noisy and object-like as base ground | quiet the center; keep packed earth as base, not active object |
| `aul_fire_01` | too large/complex: tripod and structure dominate, not small first campfire | crop a smaller simple campfire; no large support structure for MVP |

## Required Next Art Step

Create a second slicing proposal from the same accepted source sheet.

Do not generate new unrelated images. Do not overwrite raw.

Required deliverables:

```text
gamedesign/assets/concept/pass_13_generated_world_foundation/pass_13_world_foundation_slicing_proposal_contact_v2.png
gamedesign/assets/concept/pass_13_generated_world_foundation/pass_13_world_foundation_reuse_map_v2.md
gamedesign/assets/concept/pass_13_generated_world_foundation/proposed_runtime_v2/
```

V2 must prioritize only these files:

```text
ground/ground_sand_base_01.png
decor/decor_dune_01.png
decor/decor_stones_01.png
decor/decor_dry_grass_01.png
decor/decor_tracks_01.png
decor/decor_cracks_01.png
road/road_straight_ns.png
road/road_straight_ew.png
road/road_corner_ne.png
road/road_corner_es.png
road/road_corner_sw.png
road/road_corner_wn.png
road/buffer_edge_stones_01.png
road/buffer_packed_sand_01.png
aul/aul_ground_2x2.png
aul/aul_fire_01.png
```

Still deferred:

```text
decor_bones_01
buffer_stakes_01
buffer_cart_marks_01
aul_yurt_small_01
aul_yurt_small_02
road_current_highlight
```

## Acceptance Criteria For V2

```text
1. road straight/corners contain only road material;
2. road family has consistent width and alignment;
3. buffer is visible no-build edge, not road and not wall;
4. decor overlays are quiet and do not read as active tile objects;
5. aul fire is a small campfire, not a large structure;
6. contact sheet includes current raw vs proposed v2 at target size;
7. reuse map explains source crop/reuse/risk per file;
8. no raw overwrite before GDD approval.
```

## Code Instruction

No Code work is needed from this proposal yet.

Code should wait until GDD accepts V2 slicing and only then integrate exact runtime files.
