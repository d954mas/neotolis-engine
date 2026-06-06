# 70. Gameplay Map Zoom Visual Review

Status: accepted as scale direction, not final art acceptance.

Evidence:

```text
tmp/map_zoom_game_check_done.png
```

Code result reviewed:

- map scale was increased from `360` to `720`;
- map sprites and overlay zones are culled to the map viewport;
- top HUD and bottom card hand are no longer covered by overflowing map tiles;
- screenshot was captured from `SCENE_GAME` using desktop/native path.

## GDD Decision

Accept the larger map scale direction.

Reason:

- hero is readable at gameplay distance;
- road loop is now legible;
- active card hand/right hero panel remain visible;
- this scale is a better baseline for judging world art than the old tiny map.

This is not final visual acceptance.

## Visual Findings

| Area | Finding | Decision |
| --- | --- | --- |
| Map scale | 2x direction makes hero/road readable | Keep as current gameplay review baseline |
| Ground/decor | Empty cells still read as technical tile blocks, with green placeholder-like cells | Pass 13 remains high priority |
| Road | Route is readable but visually rigid/repeated | Pass 13 should make road material more organic while preserving reuse |
| Aul center | Start/home reads, but still needs generated world foundation pass to blend with road/ground | Review again after Pass 13 slicing |
| Hero | Wayfarer reads clearly at new scale | Keep Pass 10 candidate pending normal gameplay review |
| Card hand | Cards are readable; card backs show visible mojibake-looking marks/text artifacts | Needs targeted UI/card-back review after current art pass |
| Right panel | Hero panel readable; title/marks at top show mojibake-like artifacts | Needs text/asset audit; do not blame generated art until source identified |
| Log | Useful and readable; large enough at this resolution | Keep chat/log as important gameplay feedback |

## Implications For Pass 13

Pass 13 source/slicing must be judged at this larger map scale.

Review focus:

```text
ground must not look like flat square placeholders
decor must be quiet but alive
road must be readable and reusable without rigid stamped repetition
road buffer must separate build/no-build without becoming a wall
aul core must stay small camp, not city
```

## Next Code QA Request

No immediate Code task from this review.

After Pass 13 source and slicing are accepted, Code/GDD should capture a new desktop `SCENE_GAME` screenshot at the current map scale and compare:

```text
before: tmp/map_zoom_game_check_done.png
after: Pass 13 runtime world foundation screenshot
```

## Not Accepted Yet

```text
final world art
final card back/readability
final right-panel text/art
final L5 readability for all art
```
