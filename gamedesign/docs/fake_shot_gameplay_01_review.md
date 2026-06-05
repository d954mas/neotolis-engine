# Fake Shot Gameplay 01 Review

Source image:

```text
gamedesign/assets/concept/fake_shot_gameplay_01.png
```

## Summary

`fake_shot_gameplay_01` is accepted by GDD owner as a composition reference, not as final asset style.

It proves the main readability stack:

```text
aul_core -> road_path -> road_buffer -> buildable desert field
```

The image is too detailed/painterly for the first `flat_readable_jam_style` pass, but it gives a useful layout target for the first playable.

GDD owner confirms:

```text
central aul = good
rectangular loop = good
visible road_buffer = good
living decor cells = good
```

## What Works

| Area | Keep |
| --- | --- |
| `aul_core` | Central camp is immediately readable. Yurts, fire and clan props establish the theme without palace imagery. |
| `road_path` | Rectangular loop reads clearly as the hero route. Packed-earth value is distinct from sand. |
| `road_buffer` | Stones and stakes make the no-build border understandable. |
| `field_build` | Free cells are not empty. Dunes, stones, grass, tracks, bones and cracks all read as base decor. |
| `hero` | Small wayfarer scale works: hero is visible but does not dominate the map. |
| Tone | Turkic nomadic camp direction is stronger than generic fantasy desert. |

## What To Fix

| Issue | Direction |
| --- | --- |
| Too detailed | Reduce texture by 40-60%. Each tile should have one main mark and one secondary mark. |
| Too painterly | Push toward flat shapes and controlled shadow blocks. |
| Aul too rich | First playable aul should be humbler: 2-3 yurts, fire, supplies, one tamga post. Remove extra racks if they clutter. |
| Saxaul too tall/tree-like | `tile_saxaul_01` must be a low bush: dark twigs, small shadow, no tree silhouette. |
| Road too wide | Keep road width inside one grid cell. The buffer should be around it, not blend into it. |
| Grid too visible | Tile seams can remain for debug/fake-shot, but final art should make seams subtle except during placement. |
| Buffer too ornamental | Stones/stakes should mark no-build, but not look like a fence/wall. |
| Road/buffer contrast too soft | Strengthen `road_path` vs `road_buffer`: road is smoother/darker/continuous, buffer is broken stones/stakes/cart marks. |
| Too little FX space | Leave more air around hero and likely battle/trigger bubble positions. |
| Aul anchor could be warmer | Add controlled warm contrast around fire/yurts so aul is the first visual anchor. |

## Asset Notes From This Shot

### `aul_core`

For first playable:

```text
aul_ground_2x2
aul_yurt_small_01
aul_yurt_small_02
aul_fire_01
aul_supplies_rack_01
aul_tamga_post_01
```

Do not use the large central yurt as the only aul symbol. The game starts as a small clan camp, not a capital.

### `road_buffer`

Accepted ingredients:

```text
low stones
short stakes
packed sand
cart marks
```

Avoid turning it into a defensive wall. It should say "edge of path / no build", not "fortification".

### `base_decor`

The shot confirms six useful decor families:

```text
decor_dune_01
decor_stones_01
decor_dry_grass_01
decor_tracks_01
decor_bones_01
decor_cracks_01
```

Each decor cell must stay quieter than any active `tile_*`.

## Prompt For Next Fake Shot

```text
Use case: stylized-concept
Asset type: first gameplay fake-shot for a 2D roguelite loop-builder game
Primary request: 16:9 top down 2D game screenshot mockup, no text, no UI labels
Scene/backdrop: warm desert steppe field with a small central Turkic nomadic clan camp and a square-tile road loop around it
Subject: humble clan camp with two small felt yurts, a campfire, supplies, one tamga post; compact one-cell-wide packed dirt road loop around the camp; visible no-build road buffer made from sparse low stones, a few short stakes, packed sand and cart marks; buildable desert cells beyond the buffer; one small low saxaul bush tile placed roadside; tiny wayfarer hero walking on the road
Style/medium: flat readable jam game art, clean silhouettes, simple bold shapes, limited texture, atlas-friendly 64x64 tile style, not painterly
Composition/framing: wide 16:9 gameplay camera, camp centered and warmer than the field, road loop clearly readable, road buffer strongly distinct from road and buildable field, several base decor cells visible as simple dunes stones dry grass tracks bones cracks, extra clear space around the hero for future FX/battle bubbles
Lighting/mood: dry sunlit desert, calm but lonely, clan memory tone
Color palette: warm ochre sand, darker packed-earth road, felt off-white yurts, muted clan red accents, small teal tamga accent only
Constraints: every free buildable cell must have visible base decor; saxaul is a low dry bush with twigs and modest shadow, not a tree and not an oasis; no oasis, no palm trees, no text, no watermark
Avoid: arabian palace, genie lamp, flying carpet, ornate bazaar, golden dome, aladdin, magic carpet, huge palm oasis, lush tropical greenery, ornate magic UI, empty blank beige grid cells, photorealism, 3D render, excessive texture, detailed painting, UI text, labels, watermark
```

## Next Asset Generation Order

1. `tile_saxaul_01` as isolated sprite concept.
2. `road_buffer_set_01` as four tile concepts.
3. `decor_base_set_01` as six tile concepts.
4. `hero_wayfarer_walk_rough` as small 4-frame strip.
5. `fake_shot_gameplay_02` with stricter flat style.
