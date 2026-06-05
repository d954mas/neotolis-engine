# Tile Saxaul 01 Review

Source image:

```text
gamedesign/assets/concept/tile_saxaul_01_variants.png
```

## Summary

This sheet is useful for silhouette exploration, but not final style.

Best candidates from the first sheet:

```text
bottom_left
bottom_right
```

They communicate "small roadside help" because they include loose brushwood sticks. They still need to be lower, flatter and less tree-like.

GDD owner confirms final direction:

```text
low/wide shrub
45-60% tile width
25-40% tile height
2-4 dry twigs / brushwood sticks
small shadow
humble first help, much weaker visual value than oasis
transparent active sprite, not baked into sand tile
```

## Accepted Traits

| Trait | Keep |
| --- | --- |
| Dark dry twigs | Reads immediately as saxaul/brushwood. |
| Small shadow | Helps separate tile from sand. |
| Brushwood sticks | Supports the gameplay fantasy: shade and fuel. |
| Dry grass base | Makes it part of desert decor, not a magical object. |

## Problems

| Problem | Fix |
| --- | --- |
| Too tall | Height should fit the lower `35-45%` of the tile. |
| Too tree-like | Remove vertical trunk logic. Use sprawling bush/twigs. |
| Too painterly | Flatten values and reduce microtexture. |
| Too close to active landmark | It must feel common, not rare. |
| Tile background too decorated | Keep background quieter than active silhouette. |

## Final Tile Direction

`tile_saxaul_01` should be:

```text
low dry shrub
dark twisted twigs
2-3 loose brushwood sticks
small compact shadow
tiny dry grass tufts
no water
no palm
no magic glow
```

Target silhouette:

```text
wide > tall
sprawling > vertical
common helper > rare landmark
```

## Sprite Requirements

| Property | Value |
| --- | --- |
| Source tile | `64x64` |
| 2x source | `128x128` |
| Safe area | `48x48` |
| Occupied visual area | bottom `40x36` px of a `64x64` tile |
| Target width | `29-38` px in a `64x64` tile |
| Target height | `16-26` px in a `64x64` tile |
| Anchor | tile center |
| Shadow | compact oval, no long cast shadow |
| Palette | `wood_dark`, `sand_shadow`, `sand_base`, small `dry_grass` accent |
| Background | transparent alpha |

## Prompt For Next Saxaul Pass

```text
Use case: stylized-concept
Asset type: isolated transparent active tile sprite concept
Primary request: one 64x64-style top-down tile_saxaul_01 sprite on transparent background, no text
Scene/backdrop: transparent background only
Subject: very low sprawling saxaul shrub, dark twisted dry twigs, two small loose brushwood sticks at the base, tiny dry grass tufts, compact shadow
Style/medium: flat readable jam game art, simple bold silhouette, limited texture, atlas-friendly, clean 2D sprite, not painterly
Composition/framing: shrub occupies the lower 40 percent of the tile, wide silhouette, generous padding, readable at 32x32
Lighting/mood: ordinary roadside help, humble and practical
Color palette: dark brown twigs, muted dry grass, soft transparent shadow
Constraints: must look like a low bush and brushwood, not a tree, not a landmark, not magical, not lush
Avoid: tall tree, trunk, oasis, water, palm, green lush bush, glow, magic particles, arabian palace, genie lamp, flying carpet, ornate bazaar, photorealism, 3D render, excessive texture, text, label, watermark
```

## Atlas Candidate

Use bottom-right as the nearest visual reference, but redraw flatter:

```text
tile_saxaul_01 = bottom_right silhouette, 60% height, 75% texture detail, flatter shadow
```
