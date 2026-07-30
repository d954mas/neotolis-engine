---
name: visual-qa
description: Headless pixel-level QA of GL rendering via devapi capture.frame (glReadPixels). Use after visual/rendering changes to verify output without the user's eyes. Aesthetic judgement still belongs to the user.
---

# Headless visual QA via devapi capture

GDI/PrintWindow screen capture of GL windows does NOT work in this environment
(black frames). The working channel is the app capturing its OWN framebuffer via
devapi `capture.frame` / `capture.region` (glReadPixels at a pre-swap seam —
see docs/spec/debug/logging-errors-debugging.md). Battle-tested in the
game-67-idle AI-studio repo (same engine lineage); the rules below are its
distilled lessons.

## When to use

- Pixel-exact verification: did the glyph/sprite/region render, did a fill leak,
  did an offset move, before/after comparison of a rendering change.
- NOT for aesthetic/layout judgement — that stays with the user: brief them
  explicitly on what to check.

## Procedure

1. **Build a devapi-enabled config** (dedicated build dir; groups ON):

```bash
cmake --preset native-debug -B build/_cmake/native-debug-capture \
  -DNT_DEVAPI_ENABLED=ON -DNT_DEVAPI_GROUP_CORE=ON \
  -DNT_DEVAPI_GROUP_DISCOVERY=ON -DNT_DEVAPI_GROUP_CAPTURE=ON
cmake --build build/_cmake/native-debug-capture --target <example>
```

2. **Refresh packs** (delete stale `.ntpack` after editing shader/asset sources).

3. **Run the example in the background**, drive the Python client
   (`tools/devapi/client.py` — API, not a CLI): `capture_frame()` /
   `capture_region()`. Patterns: `tools/devapi/scenarios/`, liveness gate:
   `tools/devapi/pixel_health.py`.

4. **Interaction rhythm (hard rule):** observe → act → `frame.wait` → observe;
   capture only after the state is stable. Lazy font glyphs / UI resources need
   tens of frames to settle — wait, then capture, or the frame lies.

5. **Assert on pixels**: small exact region/color assertions beat full-image
   goldens (fonts + lazy resources + driver variance make frame-exact goldens
   flake; the studio never shipped them). For before/after — capture both and
   diff regions.

6. Kill the app (`taskkill //F //IM <example>.exe` if it lingers).

## Judging the image (LLM failure modes the studio codified)

- **Never judge text legibility from a full-frame thumbnail** — crop the region
  and upscale ×3 before looking. This is the #1 recurring mistake.
- **Scoped acceptance language**: "PASS for first-screen, reward-active", never
  a blanket "UI pass". Name the states you actually saw.
- **"Not enough" evidence**: code compiles but no screen checked; one screenshot
  that does not show the CHANGED state; a prose claim that the screen is fine
  without a visible artifact.
- Batch N states into ONE contact-sheet PNG + one JSON where possible — best
  token-per-insight ratio.

## Gotchas

- `pixel_health.py` liveness thresholds false-positive on visually quiet valid
  states — use it opt-in, not as a default gate.
- Under MANUAL time mode use `capture_frame_and_step()` — a bare
  `capture_frame()` deadlocks (capture defers to presentation, which never
  comes without a `time.step`).
- The app renders every frame regardless of focus — a black capture means the
  GDI path was used, not that rendering stalled.
- Non-ASCII test strings must be hardcoded UTF-8 in C — Git Bash→Windows env
  re-encodes env-var text.
- Stdout redirect can break GL init in some hosts; printf-debug to a file via
  fopen instead.
- Reference host wiring: `examples/capture_host`.
