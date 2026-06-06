# 75. Pass 13 slicing proposal V2 review

Status: GDD review of Pass 13 V2 proposal-only world foundation candidates.

Decision:

```text
V2 PARTIALLY ACCEPTED FOR RUNTIME CANDIDATE
GROUND / DECOR / AUL CORE ACCEPTED
ROAD / BUFFER REJECTED
```

## Reviewed Files

```text
gamedesign/assets/concept/pass_13_generated_world_foundation/pass_13_world_foundation_slicing_proposal_contact_v2.png
gamedesign/assets/concept/pass_13_generated_world_foundation/pass_13_world_foundation_reuse_map_v2.md
gamedesign/assets/concept/pass_13_generated_world_foundation/proposed_runtime_v2/
```

## Accepted For Runtime Candidate

These V2 files may be copied into `games/turkic-jam-2026/raw/*` as candidate generated bitmap art:

```text
ground/ground_sand_base_01.png
decor/decor_dune_01.png
decor/decor_stones_01.png
decor/decor_dry_grass_01.png
decor/decor_tracks_01.png
decor/decor_cracks_01.png
aul/aul_ground_2x2.png
aul/aul_fire_01.png
```

Reason:

- source is accepted generated bitmap;
- reuse map is present;
- candidates are quieter than current technical art;
- decor no longer contains obvious side fragments from V1;
- fire is now a small campfire, not a large rack structure;
- no new unrelated generation was used.

Risks to watch after runtime screenshot:

```text
sand may still be slightly noisy at 2x map scale
cracks may draw attention if repeated too often
decor alpha/halo must be checked on live sand background
aul ground may need lower contrast if it fights hero/readability
```

## Rejected For Runtime

Do not copy these V2 files into raw:

```text
road/road_straight_ns.png
road/road_straight_ew.png
road/road_corner_ne.png
road/road_corner_es.png
road/road_corner_sw.png
road/road_corner_wn.png
road/buffer_edge_stones_01.png
road/buffer_packed_sand_01.png
```

Reason:

- road candidates still show technical strip/crop artifacts;
- some corners expose cut edges and warm/red source remnants;
- road material does not read as one natural packed path family;
- buffer candidates still read like thin repeated lines and may become wall/road noise.

## Required V3 Art Task

Create V3 only for road and buffer.

Do not regenerate full world foundation. Do not touch accepted ground/decor/aul candidates unless runtime screenshot fails them.

V3 deliverables:

```text
gamedesign/assets/concept/pass_13_generated_world_foundation/pass_13_road_buffer_contact_v3.png
gamedesign/assets/concept/pass_13_generated_world_foundation/pass_13_road_buffer_reuse_map_v3.md
gamedesign/assets/concept/pass_13_generated_world_foundation/proposed_runtime_v3/road/
```

V3 must solve:

```text
road straight/corners contain only road material
road straight/corners connect cleanly on a 128x128 grid
road family has one consistent width
road edges are painterly but not noisy
buffer reads no-build edge, not wall and not extra road
no red/magenta/crop remnants
```

## Runtime Verification Required After Partial Integration

After accepted V2 subset is copied into raw:

```text
build_turkic_jam_packs
cmake --build build/_cmake/native-debug --target turkic_jam
desktop runtime dump from SCENE_GAME
visual review of live map at current 2x scale
```

Only after screenshot review can these accepted subset files move from `candidate generated bitmap art` to stronger runtime acceptance.
