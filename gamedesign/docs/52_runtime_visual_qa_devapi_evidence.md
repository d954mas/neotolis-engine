# 52. Runtime visual QA devapi evidence

Status: current evidence snapshot, not final visual acceptance.

Purpose: record the successful desktop/native evidence that the QA harness reaches the runtime renderer and binds optional atlas regions after the generated-constant lookup fix, including Pass 7 hero archetype panels.

## Command

```text
turkic_jam.exe --visual-qa --devapi 9125
visual_qa.status
ui.tree
```

## Evidence

`visual_qa.status` returned:

```json
{
  "ok": true,
  "data": {
    "scene": "visual_qa",
    "visual_qa": true,
    "resources_ready": true,
    "logical": [1280, 720],
    "batch_a": [44, 44],
    "batch_b": [47, 47],
    "batch_c_aul": [6, 6],
    "batch_c_hero_panels": [3, 3],
    "pass6_future": [21, 21],
    "draw_groups": [
      "gameplay_composition",
      "active_tiles",
      "hud_cards_icons",
      "hero_equipment",
      "fx_strips",
      "aul_progression",
      "ui_surfaces",
      "future_library",
      "hero_archetype_panels"
    ]
  }
}
```

`ui.tree` returned the expected QA root label:

```text
VISUAL QA HARNESS - runtime renderer, not production gameplay/progression
```

## Decision

Accepted:

```text
L2 builder: pass for Batch A, Batch B, Batch C aul progression, and Pass 6 future library.
L2 builder: pass for Batch C hero archetype panels 3/0.
L3 bind: pass for Batch A 44/44, Batch B 47/47, Batch C aul progression 6/6, Batch C hero panels 3/3, Pass 6 future library 21/21.
Visual QA scene reachability: pass.
```

Not accepted yet:

```text
L5 screenshot/readability: not proven.
Final art acceptance: not proven.
```

Why: devapi proves runtime state, bind counts, and UI tree construction. It does not prove pixel readability at gameplay scale. Screenshot/capture/readback is still required before marking art final accepted.

## Current pack evidence

Fresh pack builder run after Pass 7 registry:

```text
Batch A found 44 / missing 0
Batch B found 47 / missing 0
Batch C aul progression found 6 / missing 0
Batch C hero archetype panels found 3 / missing 0
Pass 6 future library found 21 / missing 0
Optional production sprites found 121 / missing 0
Atlas packed 125 sprites, 124 unique, 1 page
Generated merged header 132 assets
CRC32 0xEC459C00
```

Native target:

```text
cmake --build build/_cmake/native-debug --target turkic_jam
PASS
```

## Remaining blocker

Desktop screenshot capture still needs a reliable path:

- stale WASM output cannot be used for L5;
- previous `CopyFromScreen`, `PrintWindow`, and `ffmpeg gdigrab` attempts failed or produced invalid images;
- native devapi is accepted for status evidence, but visual readability still needs screenshot/readback/manual visible verification.

## Next owner actions

1. Code/GDD: produce valid desktop screenshot/readback or manual visual verification artifact for `--visual-qa`.
2. GDD: review real screenshot and send targeted fix list to Art.
3. Art: repaint only targeted defects found in L5, not broad new fake-shot passes.
