# ui_showcase

A Druid-style tabbed vitrine that supersedes the older per-phase UI demos
(the buttons, stateful-widget, and theme demos). It is the single source
of truth for every `nt_ui` widget, the modal helper, and the theme hot-swap
pattern. Builds and runs identically on native (GLFW) and WASM (WebGL2).

## Layout

A static-const registry (`g_tabs[]`) of `{name, info, code_url, render fn,
nullable props_fn}` entries drives a **left tab list -> right content stage**.
The stage is wrapped in a scroll container (exercises the scissor stack). A tab
that sets a `props_fn` also renders a focused live properties panel beside its
content; all other tabs render no panel.

## Tabs (8 logical categories)

1. **Labels** - h1 / body / caption variants, themed via the palette.
2. **Buttons** - Primary / Secondary / Disabled as three sibling entries
   (idle / hover / pressed / disabled).
3. **Images & Slice9** - panels at 300x100 / 600x100 / 600x400 (corners stay
   crisp) + a live insets/size properties panel.
4. **Toggles & Radios** - checkbox + exclusive radio group + sliding toggle.
5. **Sliders & Progress** - float + int sliders + a progress bar with a live
   value / auto-animate properties panel.
6. **Scroll** - a tall list with a nested inner scroll (capture-steal).
7. **Modals** - confirm modal + nested depth-2 modal + a live transition panel.
8. **Stress** - N labels @14pt + the frame `gpu_ms` / draw-call readout.

## Controls

| Input | Action |
|-------|--------|
| Left tab list | select the active tab (state per tab is retained) |
| **T** key / header **Theme** button | dark <-> light hot-swap |
| **D** key | toggle the inspector overlay |
| **Esc** (native) | quit |
| **Esc** (modal up) | close the TOP modal only |
| backdrop click (modal up) | close-on-backdrop (the backdrop blocks click-through) |

## Theme hot-swap

The whole hot-swap is a single `g_current` pointer flip over a game-owned
`ui_palette_t` of per-widget style **pointers** (label / button / checkbox /
slider / progress / scroll / **modal**), with `dark` and `light` variants. There
is **no engine API in the swap** (Model D). Pressing **T** or the header button
flips the pointer; every widget — including the modal — restyles on the next
frame.

## Focused properties panels (D-60-13)

- **Modal** (the headline panel): a segmented transition selector
  (scale-pop / fade / slide), plus sliders for ease speed, scale-start
  (~0.85..1.0), and backdrop alpha (0..1). The panel writes into a **runtime**
  `nt_ui_modal_style_t` that the Modals tab passes to `nt_ui_modal` each frame,
  so the open/close animation visibly tracks the panel as you drag.
- **Slice9**: per-side insets L/R/T/B + target W/H drive a live
  `NT_UI_IMAGE_SLICE9_OVERRIDE` emit.
- **Progress**: a value slider + an auto-animate toggle.
- **Stress**: a segmented label count (50 / 100 / 200 / 400) driving the loop.

## Slug measurement protocol (Stress tab)

Open the **Stress** tab and read the **frame `gpu_ms`** overlay. Pick a label
count with the panel (50 / 100 / 200 / 400) and watch the frame `gpu_ms` and the
live `ui_draw_calls` readout respond.

**GPU-timing limitation (no-nest):** `GL_TIME_ELAPSED` query segments **cannot
nest** (`engine/graphics/gl/nt_gfx_gl.c:483` asserts no-nest). The host frame
loop already wraps the whole frame in one `nt_gfx_begin_segment("frame")`
segment, so an *isolated* `ui_text` segment is impossible. The Stress tab is
therefore a **text-only** tab and surfaces the **frame** `gpu_ms` as the honest
Slug-cost proxy — not a nested per-widget timing.

**WebGL2 note:** `nt_stats_get_gpu_ms()` returns `-1.0` when the
`EXT_disjoint_timer_query` extension is absent (common on WebGL2). The overlay
guards `< 0` and shows `gpu: n/a` instead of garbage.

**Threshold is INFORMATIONAL only.** The historical target was `< 4 ms` of text
GPU cost on a mid-tier mobile device. Exceeding it does **not** block this phase
— it merely files a v1.9 bitmap-font-fallback issue (D-07). The frame `gpu_ms`
proxy is reported for that decision, not as a gate.

## Batching evidence

The header shows a live `ui_draw_calls` readout (Phase 52). Batching is driven by
**explicit `nt_ui_layer_t`** (0..255, lower draws first) — there is **no
sort-by-material toggle** (`NT_UI_WALK_SORT_BY_MATERIAL` does not exist; DEMO-09
was removed per D-60-16). The draw-call count is the batching evidence.

## Build & run

```sh
# native-debug
cmake --build build/_cmake/native-debug --target ui_showcase
build/examples/ui_showcase/native-debug/ui_showcase.exe

# wasm-debug (paired) then serve and open in a browser
```
