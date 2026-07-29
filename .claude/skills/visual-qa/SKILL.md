---
name: visual-qa
description: Headless pixel-level QA of GL rendering via devapi capture.frame (glReadPixels). Use after visual/rendering changes to verify output without the user's eyes. Aesthetic judgement still belongs to the user.
---

# Headless visual QA via devapi capture

GDI/PrintWindow screen capture of GL windows does NOT work in this environment
(black frames). The working channel is the app capturing its OWN framebuffer via
devapi `capture.frame` (glReadPixels inside the app's GL context) — validated
cross-font during the `@`-outline fix.

## When to use

- Pixel-exact verification: did the glyph/sprite/region render, did a fill leak,
  did an offset move, before/after comparison of a rendering change.
- NOT for aesthetic/layout judgement (spacing "looks right", colors "feel off") —
  that stays with the user: brief them explicitly on what to check.

## Procedure

1. **Build a devapi-enabled config** (dedicated build dir; groups must be ON):

```bash
cmake --preset native-debug -B build/_cmake/native-debug-capture \
  -DNT_DEVAPI_ENABLED=ON -DNT_DEVAPI_GROUP_CORE=ON \
  -DNT_DEVAPI_GROUP_DISCOVERY=ON -DNT_DEVAPI_GROUP_CAPTURE=ON
cmake --build build/_cmake/native-debug-capture --target <example>
```

2. **Build/refresh the example's packs** (delete stale `.ntpack` after editing
   shader/asset sources — packs depend only on the builder exe).

3. **Run the example in the background** with its build dir argument, then drive
   the devapi client:

```bash
./build/examples/<example>/native-debug-capture/<example> build/examples/<example> &
```

   Then drive the Python client (`tools/devapi/client.py` — API, not a CLI):
   `capture_frame()` / `capture.frame` over the devapi transport. Ready-made
   patterns: `tools/devapi/scenarios/` and `tools/devapi/pixel_health.py`.

4. **Assert on pixels** (compare regions/colors with a small script, or diff two
   captures before/after). Small exact assertions beat full-image goldens —
   goldens are brittle across drivers.

5. Kill the app (`taskkill //F //IM <example>.exe` if it lingers).

## Gotchas

- Reference host wiring: `examples/capture_host` shows the devapi capture setup.
- The app renders every frame regardless of focus — black captures mean the GDI
  path was used, not that rendering stalled.
- Non-ASCII test strings must be hardcoded UTF-8 in C — Git Bash→Windows env
  re-encodes env-var text.
- Stdout redirect can break GL init in some hosts; log to a file via fopen when
  the host needs printf-debugging.
