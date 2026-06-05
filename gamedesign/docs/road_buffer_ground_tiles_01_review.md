# Road Buffer Ground Tiles 01 Review

Source image:

```text
gamedesign/assets/concept/road_buffer_ground_tiles_01.png
```

## Summary

This sheet is useful as the first `road_path` vs `road_buffer` visual test.

Accepted direction:

```text
road_path = smoother, darker, continuous packed-earth route
road_buffer = broken no-build edge with sparse stones, stakes and packed sand
```

Not production-ready yet: bottom-row buffer variants are too close to a stone/stake wall.

## What Works

| Area | Keep |
| --- | --- |
| `road_path` value | Road is clearly darker than surrounding sand. |
| `road_path` continuity | Top row reads as actual route surface, not random decor. |
| `road_buffer` language | Stones and stakes communicate no-build. |
| Corner variants | Curved road/buffer language is readable. |

## Problems

| Problem | Fix |
| --- | --- |
| Buffer too wall-like | Reduce stone/stake density by ~35%. Use broken clusters, not continuous line. |
| Too much texture | Flatten dirt/noise for jam readability. |
| Stakes too tall | Shorter stakes, fewer per tile. They mark an edge, not a fence. |
| Road and buffer sometimes merge | Keep road as smooth packed band; keep buffer as rough broken edge outside it. |
| Buildable sand too decorated | Adjacent buildable area must stay quieter than active tile objects. |

## Candidate Mapping

| Concept area | Candidate asset ids |
| --- | --- |
| top-left vertical road | `road_straight_ns` |
| top-second horizontal road | `road_straight_ew` |
| top-third road corner | `road_corner_ne` / `road_corner_es` guide |
| top-fourth tighter curve | post-MVP or alternate corner guide |
| bottom-left buffer edge | `buffer_edge_stones_ns_01` guide |
| bottom-second buffer edge | `buffer_edge_stones_ew_01` guide |
| bottom-third buffer corner | `buffer_edge_stones_corner_01` guide |
| bottom-fourth buffer inner curve | alternate no-build corner guide |

## Production Rule

For first playable, prefer simple composable cells:

```text
road_straight_ns
road_straight_ew
road_corner_ne
road_corner_es
road_corner_sw
road_corner_wn
buffer_packed_sand_01
buffer_edge_stones_01
buffer_stakes_01
buffer_cart_marks_01
```

Do not overfit to all generated variants. The runtime map can compose visual variety from a few quiet buffer decals.

## Prompt For Next Buffer Pass

```text
Use case: stylized-concept
Asset type: flat road and road-buffer ground tile sheet
Primary request: create 8 square 64x64-style opaque ground tile concepts, no text labels
Scene/backdrop: eight separate square warm desert game tiles in a clean 4 by 2 grid
Subject: top row four road_path tiles as smooth darker packed-earth road, low noise, subtle tracks; bottom row four road_buffer no-build tiles as sparse broken edge decals: scattered low stones, very short stakes, cart marks, packed sand berm, no continuous wall
Style/medium: flat readable jam game art, top-down 2D ground tiles, simple bold shapes, low texture, atlas-friendly
Composition/framing: each square tile fills its cell, road_path reads continuous and smoother, road_buffer reads broken and no-build but low profile, no labels
Constraints: buffer stones and stakes must be sparse, not a wall and not a fence; no active objects; no empty blank beige cells
Avoid: castle wall, full fence, high posts, dense stone line, lush greenery, arabian palace, genie lamp, flying carpet, ornate bazaar, photorealism, 3D render, excessive texture, labels, text, watermark
```
