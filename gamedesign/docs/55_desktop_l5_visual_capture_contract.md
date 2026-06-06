# 55. Desktop L5 visual capture contract

Status: active Code request.

Owner: GDD / Art lead.

Audience: Code.

## Why

The visual pipeline has candidate-final runtime PNGs and desktop devapi bind proof for Batch A/B/C, Pass 6 and Pass 7. Final art acceptance is still blocked because L5 pixel/readability evidence is missing.

Devapi counts prove reachability and atlas binding. They do not prove that the actual rendered pixels are readable in the game window.

## Decision

Next Code priority is a reliable desktop/native L5 capture or readback path.

Do not use stale WASM output for L5. The user explicitly requested desktop tests.

## Required output

Produce a fresh desktop/native visual artifact from:

```text
turkic_jam.exe --visual-qa
```

or an equivalent QA-only command that boots the same `SCENE_VISUAL_QA`.

Accepted artifact options, in priority order:

1. `--visual-qa --dump-frame <png>` or similar command that renders at least one frame, reads real framebuffer pixels and writes a PNG.
2. Devapi command such as `visual_qa.dump_png <path>` that writes a PNG from the current rendered frame.
3. A visible desktop/manual verification path with user-facing instructions and a matching `visual_qa.status` evidence record, if automated framebuffer capture is impossible.

## Acceptance gates

An artifact only counts as L5 evidence if all are true:

- Source is fresh native desktop runtime, not stale WASM.
- The image contains real rendered QA harness pixels, not a blank/white/black client area.
- Resolution is at least `1280x720`, or the actual runtime logical size is recorded.
- The artifact includes enough detail to judge:
  - 24px HUD icons;
  - card hand and card art;
  - right hero panel and equipment;
  - map ground/decor/road/buffer/aul/hero;
  - active tile objects;
  - FX strips;
  - QA-only aul progression;
  - QA-only future library and hero archetype panels.
- A matching `visual_qa.status` record is saved in the report.
- If the capture is from a QA-only scene, it must remain labeled QA/debug only.

## Rejected evidence

Do not count these as L5:

- `visual_qa.status` alone.
- `ui.tree` alone.
- `PrintWindow` result that is only a white client area.
- `CopyFromScreen` failure logs.
- stale `wasm-debug/index.*`.
- contact sheets or fake-shots.
- generated source images before runtime slicing/packing.

## Implementation guidance

Preferred low-risk implementation:

```text
turkic_jam.exe --visual-qa --dump-frame tmp/visual_qa_l5.png --exit-after-frame
```

Expected behavior:

1. normal resource load;
2. boot into `SCENE_VISUAL_QA`;
3. advance enough frames for layout/resources to render;
4. read actual framebuffer pixels;
5. write PNG;
6. exit cleanly.

If the engine needs a graphics API for readback, keep it explicit and QA-only. Do not add readback to the hot path. If implementation touches the engine, keep it behind a narrow debug/test API and explain any spec implications.

## Code report format

Update `games/turkic-jam-2026/coordination/FROM_CODE.md` with:

```text
L5 capture path:
Command:
Output PNG:
Resolution:
Blank/white/black check:
visual_qa.status:
What is readable:
What is not readable:
Blocked:
```

## GDD review after delivery

GDD will inspect the PNG and write a targeted art fix list. Do not mark final art accepted from capture existence alone; the image must be reviewed for readability.
