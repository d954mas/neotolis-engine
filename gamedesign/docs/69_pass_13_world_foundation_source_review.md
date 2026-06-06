# 69. Pass 13 World Foundation Source Review

Status: waiting for Art delivery.

Purpose: define the GDD review gate for the Pass 13 generated world foundation source sheet before any runtime slicing or overwrite.

Parent contract:

```text
68_pass_13_world_foundation_contract.md
```

## Expected Delivery

Art must deliver these files first:

```text
gamedesign/assets/concept/pass_13_generated_world_foundation/pass_13_world_foundation_source.png
gamedesign/assets/concept/pass_13_generated_world_foundation/pass_13_world_foundation_contact_sheet.png
gamedesign/assets/concept/pass_13_generated_world_foundation/pass_13_world_foundation_reuse_map.md
```

Optional:

```text
gamedesign/assets/concept/pass_13_generated_world_foundation/pass_13_world_foundation_chroma_removed.png
```

Do not overwrite `games/turkic-jam-2026/raw/*` until this review is accepted.

## Review Decision Format

Use one of:

```text
ACCEPT SOURCE -> approve slicing plan, still no runtime overwrite until slice/contact review
CHANGE SOURCE -> request one targeted source correction
REJECT SOURCE -> source breaks art direction or production protocol
```

## Source Review Checklist

| Area | Accept If | Reject If |
| --- | --- | --- |
| Generated bitmap source | Looks like painterly/generated bitmap material sheet | Looks like SVG, vector, procedural rectangles or flat debug art |
| One-family workflow | One coherent source sheet can feed multiple world families | Separate unrelated one-off images or repeated tile generation |
| Ground | Sand is warm, readable, quiet and not blank | Sand is noisy, photoreal, muddy, or hides gameplay cells |
| Decor overlays | Dune/stones/grass/tracks/bones/cracks are low-contrast cell life | Decor reads as active tile, loot, hazard or clickable object |
| Road | Packed road is visibly walkable and reusable | Road is too close to buffer/ground or cannot be sliced into straights/corners |
| Road buffer | No-build edge is visible through packed edge/stones/stakes/cart marks | Buffer reads like wall/fence/fortification or active obstacle |
| Aul core | Small starting camp: packed earth, modest yurts, fire | Town, palace, city, bazaar, golden fantasy settlement |
| Cultural guardrails | Practical nomadic/steppe language, fictional marks only if any | Aladdin/palace/genie/flying carpet/golden dome/real sacred marks |
| Runtime feasibility | Clear padding and separable source clusters | Crops overlap, shadows bleed, or alpha cleanup will be unreliable |

## Slicing Review Checklist

After source is accepted, Art may propose a slicing tool/contact sheet. GDD must check:

```text
all intended runtime filenames preserved
all dimensions preserved
no new asset ids required
ground/decor/road/buffer/aul families visually match each other
active tile objects from Pass 9 still read above the new base
```

## Runtime Overwrite Gate

Runtime overwrite is allowed only after:

1. source review accepted;
2. slicing/contact review accepted;
3. map migration status is clear enough to avoid conflicting changes;
4. generated candidate files pass local dimension/alpha checks;
5. pack builder and desktop L5 screenshot can be run.

## Current GDD Position

As of this review doc, Pass 13 is:

```text
approved to generate source-only
not approved to overwrite runtime raw files
not a Code task yet
```

## Likely Targeted Fixes

If the first generated sheet is close but flawed, prefer one targeted correction:

| Problem | Correction |
| --- | --- |
| Ground too noisy | Ask for quieter sand/value range, preserve road/aul if good |
| Buffer too wall-like | Ask for lower stones/stakes and packed edge language |
| Aul too rich | Ask for smaller camp, fewer objects, no settlement complexity |
| Decor too active | Ask for lower contrast and smaller decor clusters |
| Road not sliceable | Ask for clearer straight/corner strips with consistent width |
