# rtt_showcase

Dedicated Phase 74 render-to-texture proof for the general `nt_gfx` RTT and
`nt_postfx_blur` workflow. This example is independent of `ui_showcase` and does
not use asset packs.

## Build and Run

```sh
# native-debug
cmake --build build/_cmake/native-debug --target rtt_showcase
build/examples/rtt_showcase/native-debug/rtt_showcase.exe

# wasm-debug
emcmake cmake --preset wasm-debug
cmake --build build/_cmake/wasm-debug --target rtt_showcase
# serve build/examples/rtt_showcase/wasm-debug/ and open index.html
```

## Controls

| Input | Action |
|---|---|
| R | Toggle the offscreen targets between 512x288 and 768x432. |
| Esc | Quit on native builds. |

## Visual QA

The GL surface is not reliably headless-capturable here. Automated tests cover
render-target mechanics and gaussian-kernel math; visual blur quality is
human-validated in this example.

Build and run the native example, then confirm:

1. The left panel shows the raw offscreen color texture on the default
   framebuffer. The scene is intentionally asymmetric so UV or axis flips are
   visible.
2. The right panel shows the same offscreen scene blurred through
   `nt_postfx_blur` using caller-supplied source, temp, and destination targets.
3. Press `R`. The target size changes, the scene redraws, and the display
   recovers without rebinding sampled target/color/depth handles. The top status
   bar stays green when handle stability is intact.
4. The bottom strip samples the target depth texture directly. It should change
   with the scene depth and read as a low-level depth-texture debug view, not a
   shadow map.

## Scope

This is a canonical RTT/postfx workflow demo. It is not a text-specific demo, not
Phase 75 soft shadow or glow routing, and not a shadow-map subsystem. It does not
define light cameras, PCF, cascades, shadow atlases, or material shadow
integration.
