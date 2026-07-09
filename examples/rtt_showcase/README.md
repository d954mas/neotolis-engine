# rtt_showcase

Dedicated Phase 74 render-to-texture proof for the general `nt_gfx` RTT and
`nt_postfx_blur` workflow. The visual tuning controls use the engine `nt_ui`
module and a small local UI asset pack for the white atlas region, sprite/text
shaders, and ASCII font.

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
| Mouse drag, Sample zoom slider | Adjust sampled texture zoom from 1.0x to 2.5x. |
| Mouse drag, Blur radius slider | Adjust gaussian blur radius from 2 to 16. |
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
   Drag the `Blur radius` UI slider and confirm the blur strength changes
   continuously while its text value updates.
3. Press `R`. The target size changes, the scene redraws, and the display
   recovers without rebinding sampled target/color/depth handles. The top status
   bar stays green when handle stability is intact.
4. Drag the `Sample zoom` UI slider and confirm the sampled raw/blur panels zoom
   into the same offscreen texture without changing the depth debug strip. Its
   text value should update in-place.
5. The bottom strip samples the target depth texture directly. It should change
   with the scene depth and read as a low-level depth-texture debug view, not a
   shadow map.

## Scope

This is a canonical RTT/postfx workflow demo. It is not a text-specific demo, not
Phase 75 soft shadow or glow routing, and not a shadow-map subsystem. It does not
define light cameras, PCF, cascades, shadow atlases, or material shadow
integration.
