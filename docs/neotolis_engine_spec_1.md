# Neotolis Engine — Technical Specification

**Version:** v0.3-consolidated  
**Status:** Architectural baseline + implementation-oriented spec  
**Language target:** C17 (vendored C++ allowed behind extern "C" boundary)
**Primary runtime target:** Web / WASM + WebGL 2
**Secondary future target:** WebGPU

This document consolidates all architectural decisions from v0.1 overview, v0.2 technical spec, and subsequent design sessions into a single authoritative reference.

Language baseline is C17 for broader compiler and Emscripten toolchain support. Vendored C++ dependencies (e.g. Basis Universal transcoder/encoder) are permitted when no C alternative exists, provided they are isolated behind `extern "C"` wrappers and `enable_language(CXX)` is scoped to their subdirectory CMakeLists — not the root.

---

# 1. Core Principles

## 1.1 Design Philosophy

The engine follows these principles:

1. **Code-first architecture**
   - render loop is game code
   - gameplay system order is game code
   - builder rules are code
   - no heavy declarative system

2. **Explicit over implicit**
   - no hidden system scheduler
   - no hidden render graph
   - no automatic ECS magic
   - no hidden asset conversion at runtime

3. **Runtime simplicity**
   - builder does heavy work
   - runtime only loads, validates, resolves, and renders
   - no source-format import in runtime

4. **Data-oriented where useful**
   - sparse component storages
   - dense iteration
   - typed handles/refs
   - sorted draw items

5. **Minimal abstraction**
   - enough abstraction to survive WebGL 2 → WebGPU later
   - not so much abstraction that the engine becomes hard to understand

## 1.2 Code-First Approach

Throughout the entire architecture:

- render pipeline is defined by **game code**
- system order is defined by **game code**
- builder rules are defined by **code**
- engine provides primitives and infrastructure, does not impose a ready-made pipeline

Engine gives low-level and mid-level infrastructure. Game defines concrete logic, passes, sorting, batching policy, and content build pipeline.

## 1.3 Simplicity Over Universality

Key constraints:

- no premature universal abstractions
- no complex runtime reflection system when layouts and indices suffice
- no complex plugin system
- no material graph, editor framework, or scripting at start
- no multi-platform/multi-backend core design upfront

If a decision can be deferred without loss of base architecture — it is deferred.

---

# 2. Scope

## 2.1 Included in baseline

- Web platform
- WASM runtime
- WebGL 2 renderer backend (sole baseline, no WebGL 1 fallback)
- component-based entity architecture
- hierarchy
- transform system
- resource system with async loading
- custom binary pack format (NTPACK)
- custom runtime formats
- shader + material system
- mesh rendering
- sprite rendering
- text rendering (later-ready)
- fixed update + frame update
- builder in C
- input polling + pointer capture
- render items + sorting + batching policy
- mesh instancing planned
- sprite CPU batching planned
- audio system (platform-agnostic, handle-based)

## 2.2 Explicitly not in scope

- editor
- Lua scripting
- full physics engine
- material graph editor
- plugin architecture
- WebGPU backend implementation
- scene editor / authoring editor
- full UI framework (NOTE: Phase 51 introduces `nt_ui` — a minimal
  immediate-mode layout + render bridge module. It is **not** a full UI
  framework — no widget authoring tools, no styling pipeline beyond
  inline calls, no asset hot-reload of UI definitions, no scene-graph
  integration. Game owns the loop and render order; `nt_ui` provides
  building blocks per the engine's "set of modules" principle.

  Clay v0.14 is vendored as a **public** dependency of `nt_ui`: game
  code declares layout and widgets via `CLAY_*` macros directly,
  while `nt_ui` owns lifecycle (contexts, the walker that turns Clay's
  render commands into sprite/text renderer calls). Styling is
  game-owned: `nt_ui_label_style_t` is a plain data struct passed by
  const-pointer; engine ships no theme manager or style registry. This
  is a deliberate compromise — wrapping Clay's full surface in an
  engine-owned adapter API would be ~100+ shim functions for no
  benefit beyond hiding the dependency. The cost we accept is that
  Clay's pinned API (`CLAY_PINNED_MAJOR/MINOR` enforced via
  `_Static_assert` in `nt_ui.c`) becomes part of the engine's
  publicly-promised surface; bumping Clay can require coordinated
  game-side changes.

  Even with `NT_UI_DEBUG_TOOLS=OFF`, nt_ui touches ~10 Clay **private**
  symbols (`Clay__OpenElement` / `Clay__ConfigureOpenElement` /
  `Clay__CloseElement`, the `Clay__MeasureText` callback hookup, a few
  `Clay__default*` size constants) — all routed through thin wrappers in
  `engine/ui/nt_ui_clay_impl.c`, the exclusive `CLAY_IMPLEMENTATION`
  TU. The widget composition pattern (button / panel / group `_begin`)
  splits Clay's `CLAY({...}) { body }` into separate `_begin`/`_end`
  calls so widget code can run between them — Clay's public macro
  bundles `Open` + `Configure` + scoped body into a single statement
  and can't be split across function boundaries. The wrappers are the
  smallest possible escape hatch.

  With `NT_UI_DEBUG_TOOLS=ON` this expands by ~30 more Clay private
  symbols for the verbatim Clay debug-view port (the inspector body
  lives in the same TU). Either way, bumping Clay can require
  coordinated nt_ui-side changes.

  **Render-time pipeline.** `nt_ui` composes a per-element column-major
  `mat4` + opacity in a post-`Clay_EndLayout` build pass
  (`nt_ui_internal_build_tree`). The transform is `nt_ui_transform_t`
  with 9 floats (offset_xyz, rotation_xyz Euler, scale_xyz). Composition
  follows standard scene-graph order: `world = accum_parent · L_child`,
  where `L = T(O+C) · R(rot_xyz) · S · T(-C)` is built around the element's
  layout-space bbox center via cglm. The walker reads the composed mat4
  from `tree_baked[layout_idx]` (hot path, no hashmap lookup) and ships
  `world_mat4[16]` to every emit_*.

  Two coord-space modes are gated by `nt_ui_create_desc_t.use_raycast_input`:

  - **2D ctx** (default): screen Y-flip (Clay layout Y-down → GL Y-up) is
    folded into `world_mat4` once per dispatch (negate m[1]/m[5]/m[9],
    add `vy+vh` to m[13]). Hit-test reverses through the top-left 2×2 +
    col3 of the same mat4 (closed-form inverse-affine).
  - **3D ctx** (`use_raycast_input=true`): walker leaves `world_mat4`
    unflipped; the game owns the screen mapping via `nt_ui_set_view_proj`.
    Hit-test raycasts pointer → NDC (Clay Y-down to NDC Y-up) → world ray
    via `inv_view_proj` → ray-plane at widget Z=0 → `mat4_inv_trs(baked.m)`
    back to widget-local → bbox check. Optional `element_depth_bias_ndc`
    is render-only: positive values move deeper hierarchy levels slightly
    closer in projected depth to avoid z-fighting. Hit-test stays on the
    unbiased widget plane. Debug inspector render commands are excluded
    from the regular 3D walk and drawn by `nt_ui_debug_inspector_walk` as
    a separate final screen-space pass. The game binds a 2D UI projection
    first; `nt_ui_make_screen_view_proj(w, h, ...)` provides the standard
    Y-up orthographic matrix used by 2D demos. The post-walk highlight
    overlay (`nt_ui_inspector_overlay_draw`) differs: in 3D ctx it emits the
    hovered element's bbox under that element's `hit_baked` world matrix, so
    the game binds the perspective VP for it while the sidebar tree stays
    orthographic.

  Both modes use the same `tree_baked[layout_idx]` + per-id mirror
  `hit_baked[slot]` (Clay's hashmap is persistent across frames;
  `hit_generation[slot]` rejects stale ids). Opacity is a separate
  `float` accumulator on the same struct.

  **Interaction model.** Game ids interact via `nt_ui_query_interaction`
  (pure, multiple calls per frame OK) and `nt_ui_step_interaction`
  (mutating, exactly one call per id per frame). Capture is per-pointer:
  a press records `active_id`; release clears. Other widgets are
  `exclusive_gated` while one holds capture. **Orphan cleanup** at
  `nt_ui_begin` drops `active_id` if the widget didn't call step last
  frame — covers scene switches, conditional disable, and widget hide.
  Result: 1-frame IM-lag is intrinsic (current frame reads previous
  frame's bbox); no stuck input on widget disappearance.

  **Front-most arbitration.** When interactive widgets overlap a pointer, only
  the front-most may react; the rest are gated off. The hot widget per pointer
  is resolved once per frame — lazily, on the first `step_interaction` /
  `query_interaction` / `nt_ui_pointer_hot` — from the PREVIOUS frame's
  interactive registry; only widgets that called `nt_ui_step_interaction` last
  frame are candidates. 3D ctx (`use_raycast_input`): nearest world ray-distance
  within a game-fed occlusion cutoff (`nt_ui_set_pointer_occlusion`, reset each
  begin) so UI can't be clicked through world geometry — the game owns the
  raycast, the engine only takes the cutoff distance; `nt_ui_interaction_t.distance`
  reports the hit distance. 2D ctx: highest effective Clay zIndex (tie →
  last-registered, i.e. the later `step_interaction` call; widget code should step
  in declaration order so this matches paint order). A free pointer drives a
  widget only if it is the resolved hot
  widget or already holds capture; `nt_ui_pointer_hot` exposes the resolved id.
  Consequence: a freshly-shown widget registers on its first step and only
  becomes eligible the NEXT frame; on the first frame (empty registry) nothing
  reacts — reliability over instant first-frame response, no raw-hit fallback
  (matches Dear ImGui). This trades immediacy for unambiguous overlap/occlusion.
  Symmetrically, a widget that interacted last frame but is gone or disabled this
  frame stays a hot candidate for that ONE transition frame (it can gate widgets
  beneath it but itself reacts to nothing), then orphan cleanup drops it next frame.
  A statically-disabled widget never registers, so never gates — unless it opts in via
  `nt_ui_block_pointer(ctx, id, pad_lrtb)`, which writes an INERT hit-registry entry (the
  same record `step_interaction` makes, minus any interaction). It blocks input to widgets
  behind it but never captures, clicks, or reports hover — the stock disabled widgets
  (button/checkbox/slider) call it so a disabled overlay or modal can't leak clicks through.

  **Anim cache.** `nt_ui_anim_*` provides per-id eased state for widget
  visuals. Open-addressing direct-mapped table (`NT_UI_ANIM_SLOTS`,
  default 64); 4-probe chain; full-chain collision evicts the STALEST
  slot in the probe window — the one with the oldest `last_touch`
  generation tick (snap-reseed, easing lost for one id). Evicting by
  staleness rather than blindly the tail avoids bleeding a slot that
  was already touched this frame between widgets sharing the window.
  The `anim_collision_count` monotonic counter surfaces this
  degradation; game polls the delta to size `NT_UI_ANIM_SLOTS`.

  **State pool.** `nt_ui_state(ctx, id, size, tag)` is a generic per-widget-id
  retained-state pool — the durable counterpart of the anim cache. It returns a
  get-or-create cell (zeroed on create; zero is a valid initial state). The 4-char
  `tag` (built with `NT_UI_STATE_TAG`) identifies the owning widget so two widgets
  that hash to the same id+size trap on re-acquire instead of aliasing silently,
  `nt_ui_state_find` returns NULL if absent, and `nt_ui_state_clear` /
  `nt_ui_state_clear_all` drop one or all cells (e.g. a screen transition). It is
  BSS in the context (`NT_UI_STATE_SLOTS` × `NT_UI_STATE_PAYLOAD_MAX`, defaults
  256 × 64 B ≈ 19 KB/ctx, `NT_UI_STATE_PROBE_MAX` probe window), no heap — direct-mapped +
  linear-probe like the anim cache, but with **no LRU eviction**: a cell dies only
  via clear or context destroy. This no-eviction property is the contract that
  makes the game-owned-pointer escape hatch leak-safe — for an oversize payload the
  game allocates, stores the pointer in the cell, and frees it before clear, knowing
  the engine will never silently reclaim it. Overflow, a size mismatch, or a tag
  mismatch on re-acquire is `NT_ASSERT` (fail early, not a soft fallback). The returned pointer
  is a frame-scoped cache, not held across frames — widgets re-acquire by id each
  frame (the same const-cast read pattern as `nt_ui_get_bbox`). Occupancy
  (slots/bytes) is surfaced on the inspector "UI memory" line. Distinct from the
  anim cache: the anim cache evicts (transient easing), the state pool never evicts
  (durable retained state such as a slider's drag offset or a scroll container's
  momentum). Slider drag state and scroll physics state both ride it, keyed by
  (optionally salted) widget id.

  **Stateful widgets.** `nt_ui_checkbox` / `nt_ui_radio` / `nt_ui_toggle`
  are leaf widgets over one shared core. Per code-first /
  explicit-over-implicit, the engine stores NO logical value — the game owns
  it: `bool*` for checkbox/toggle, a shared `int*` for radio (exclusivity is
  free — every radio compares `*selected == my_value`, no engine-side group
  state). Each returns `changed` only on the release-over-widget frame. One
  shared `nt_ui_checkbox_style_t` (strict superset for all three) holds, per
  interaction state (idle/hover/pressed/disabled) × value row
  (unchecked/checked), the art refs + tint + transform + opacity; only
  transient visual easing lives in the engine (the anim cache above). The
  indicator (box + overlay) draws on `data->layer`; the label on a separate
  `label_layer` arg, so games may batch sprite-then-text. `scale_label`
  chooses whether press/hover scales the whole row or only the indicator
  (opacity/dim is always whole-widget). Toggle adds a render-only sliding
  thumb with a symmetric `thumb_pad` end-margin. On the click-release frame the
  widget renders the PRE-flip value and returns `changed` after drawing, so the
  pop/slide animation begins the next frame — the same intrinsic 1-frame IM lag as
  hit-test and arbitration. `nt_ui_slider` / `nt_ui_progress` extend the same
  game-owns-the-value model: the float/int value lives in the game, the engine eases
  only the visual fraction (`value_t`); the slider exposes its thumb screen position
  (`nt_ui_slider_thumb_pos`) so the game can draw drag-bubbles via a Clay floating
  element. A press ON the thumb grabs at its press offset (relative drag, value unchanged
  that frame); a press on the track jumps the value to the click point. `step` quantizes
  the visual snap with the value, the fill edge meets the thumb CENTER (not `fraction ×
  track_w`, which would under/overshoot the thumb's travel), and an out-of-range incoming
  `*value` is clamped AND written back so game memory matches what is drawn. A live drag
  returns `changed` every frame; act-once callers poll the release edge (commit-on-release).
  Hit padding inflates the touch target and auto-grows vertically to cover the thumb's
  overhang past the track even at zero style pad. Slider and progress share one fill-emit
  helper (STRETCH slice9 stretch vs CROP scissor-reveal × four directions).

  **Custom scroll physics.** `nt_ui` scroll containers bypass Clay's built-in
  `Clay_UpdateScrollContainers` (the unconditional call was REMOVED from
  `nt_ui_begin`): the engine integrates the offset itself and feeds Clay a ready
  `clip.childOffset` each frame — it never reads `Clay_GetScrollOffset`, only
  `Clay_GetScrollContainerData` for the content/container clamp dims. Everything is in
  Clay's NEGATIVE-down sign convention (childOffset negative going down/right, clamped
  to `[-(content-container), 0]`); only the input edge (wheel) and the scrollbar-thumb
  mapping flip sign. The four-behavior feel model lives in `nt_ui_scroll_style_t`
  tunables: momentum/fling (exponential velocity decay by `friction`), overscroll
  rubber-band + critically-damped bounce-back (`rubber_band_c` / `bounce_speed`),
  smooth wheel (ease toward target by `wheel_ease_speed`, 0 = instant, no teleport),
  and animated `nt_ui_scroll_to`. Release-fling velocity is tracked over a TIME WINDOW
  (`TAU_GAIN`/`TAU_DECAY`), not a per-frame delta: a moving frame blends toward the
  instantaneous sample, a zero-delta frame decays toward 0, so a fling survives pointer
  events arriving slower than the render loop instead of depending on release-frame parity.
  A press landing on a flinging container stops the fling and is swallowed (tap-to-stop).
  Per-container scroll state (pos/vel/target/flags)
  rides the state pool keyed by the scroll id; no heap. Wheel input is normalized to a
  NOTCH unit at the platform edge — `1.0 == one physical detent` (GLFW pass-through; web
  `deltaMode` divided to notches), so wheel strength is platform-independent and the
  consumer scales by `wheel_step_px`. Wheel routing is exclusive
  (innermost-wins): each frame every scroll container registers as a candidate, and
  ownership is resolved at frame END — for each pointer the engine picks the candidate
  whose just-solved bbox holds it (ranked by max scroll-nesting depth, then smallest
  area, then latest declaration) and writes a single owner per pointer. That owner is
  consumed the NEXT frame, so wheel routing carries a 1-frame lag: a newly shown
  container sees its first wheel notch one frame late (the same intrinsic IM lag as
  hit-test, which reads the previous frame's bbox). All scroll sizes and pixel
  thresholds (`NT_UI_SCROLL_STEAL_THRESHOLD_PX`, `wheel_step_px`, bar/hit dimensions)
  are in the coordinate space passed to `nt_ui_begin` — NOT framebuffer pixels; games
  typically feed `nt_ui_scale`'s logical space, so these are resolution-independent.
  Capture-steal-by-threshold
  (`NT_UI_SCROLL_STEAL_THRESHOLD_PX`, ~8 px) arbitrates an inner widget's click vs a
  scroll drag: a drag past the threshold cancels the inner capture and scrolls; a tap
  below it leaves the inner widget to click. On the latch frame the steal routes zero
  positional delta (the inner widget already applied it), tracking 1:1 from the next
  frame; the cancelled inner widget keeps any value change it made before the steal
  (Model D — the value lives in the game, so the engine cannot revert it). The
  scrollbar (2-piece slice9 track + thumb, both axes) emits as floating children of
  the container with
  ALWAYS / AUTO / AUTO_HIDE visibility (AUTO_HIDE shows on scroll activity and fades
  out `bar_hide_delay` seconds after the last motion, via one `nt_ui_anim` value_t).
  A thumb press grabs at the press offset inside the thumb (no jump-to-center); a click
  on the track off the thumb smooth-jumps to the clicked spot (`nt_ui_scroll_to`). A
  thin bar still gets a ≥ 24 px touch target via a cross-axis hit pad. This replaces the
  v1.7-era assumption that Clay drives scroll; the
  "Scissor limitation" note below still holds (AABB clip of a rotated scroll
  container is unchanged).

  **Text input.** `nt_ui_input_text` is a single-line field over a game-owned
  `char*` buffer (Model D — the engine never reallocs or owns the string; it stores
  only caret/selection/scroll/blink/focus in the state pool). Editing is UTF-8
  codepoint-aware: insert/backspace/delete/arrows never split a multi-byte sequence
  and clamp to `buffer_size-1` / `max_length`. Typed text arrives via the
  `nt_input_pop_char` UTF-32 ring (frame-local, drained each frame while focused);
  physical keys drive navigation/edit. T2 adds selection (Shift+arrows, mouse drag,
  double-click word-select, Ctrl+A), copy/cut/paste via the swappable `nt_clipboard`
  façade (paste decodes untrusted bytes per-codepoint, runs the allow-predicate,
  clamps, and drops a non-fitting multibyte codepoint whole), a render-only password
  mask, stock allow-predicates (numeric/email/url), a per-field `placeholder` string
  shown dimmed while empty+unfocused, and a minimal focus arbiter (Esc unfocus, Tab
  advance/wrap). Caret, selection highlight, and the text all float as clipped
  children carrying a `HAS_TRANSFORM` offset of `pad_x - scroll_x` (set as the Clay
  element `userData`, which the walker reads for layer+transform), so the line
  scrolls to keep the caret visible while clipping to the field box. Cut is a no-op
  when `nt_clipboard_available()` is false (no silent delete-without-copy).

  **Consolidated interaction events.** `nt_ui_events(ctx, id, cfg)` is the single
  canonical MUTATING interaction step (one per widget per frame), superseding the
  raw `nt_ui_step_interaction` as the widget-facing call: it returns the base
  capture edges (`hovered/pressed/released/held/clicked`) PLUS cfg-gated gestures
  (`double_clicked`, `long_pressed`, `hold_progress` — a linear 0..1 ramp toward
  `long_press_secs` while `held && hovered`). The base path delegates to
  `step_interaction` verbatim (byte-for-byte capture parity); the gesture cell is
  allocated ONLY when `cfg` opts in (`cfg==NULL` grows the state pool by zero), so
  zero-overhead is the default — explicit-over-implicit. The dbl-click window and
  drag move-radius are engine-global (`nt_ui_set_gesture_constants`); dragging off
  the bbox or past the radius resets `hold_progress` and suppresses `long_pressed`.
  `nt_ui_query_events` is the idempotent read-side mirror (advances nothing).
  `hold_progress` is the hold-to-confirm feedback signal and feeds the radial phase.

  **Popup-core.** A single floating-overlay primitive (`nt_ui_popup_begin/end` +
  a one-bool `nt_ui_popup_visible` wrapper) underpins every transient overlay —
  modal, dropdown, tooltip, and context-menu. It owns: a Clay floating panel
  attached to the ROOT (anchor-derived offset, no trigger-id dependency, so a
  missing trigger element can't trip Clay's parent-not-found); trigger-anchored
  placement with per-side edge-flip (BELOW/ABOVE/RIGHT/LEFT, CENTER for modal)
  read from the panel's previous-frame bbox; a `value_t` open/close tween; a shared
  modal-depth z-band (`modal_zband_stride*(depth+1)`, NT_ASSERT before the push so
  a runaway nesting fails early); and a present-only, transparent light-dismiss
  catcher at `panel_z-1` (outside-click raises a close signal). A fully-closed
  popup declares NO catcher, so the base UI stays clickable; a hover-driven
  overlay (tooltip) can clear the catcher flag entirely. Dismiss is always a
  SIGNAL the game acts on (Model D) — popup-core never owns the open bool. The
  context-menu stacks one popup-core fly-out per submenu level; the only novel
  algorithm is the mouse-aim triangle that keeps a submenu open while the cursor
  travels diagonally toward it (never collapse on raw hover-loss).

  **Immediate menu + combo.** The context-menu and dropdown are immediate-mode:
  the game declares the tree (or list) in code EVERY frame; there is no items[]
  data array and no `chosen_id` sink. Menu rows are declared with
  `nt_ui_menu_begin` / `item` / `item_ex` / `item_begin..item_end` /
  `submenu_begin` / `submenu_begin_ex..submenu_end` / `separator` /
  `separator_text` / `menu_end`; the combo with `nt_ui_combo_begin` /
  `selectable` / `selectable_icon` / `selectable_begin..selectable_end` /
  `preview_begin..preview_end` / `combo_end`. Activation is reported INLINE by
  the row call (`if (nt_ui_menu_item(...)) act();` / a combo selectable returns
  clicked) — there is no per-frame result struct to scan. The game owns the open
  state (`nt_ui_menu_state_t` / `bool *open`) and, for the combo, the `int`
  selection; the widget only signals and clears `open` on a pick (Model D, same
  as popup-core). Both build on the kept popup-core (+ the scroll wrapper for a
  long combo list). The earlier data-array forms (`nt_ui_menu(items[])`,
  `nt_ui_dropdown_trigger`/`_list`) were REMOVED — immediate-only.

  Per-frame menu scratch is GAME-owned (`nt_ui_menu_ctx_t`, allocated by the
  game — static / app field / arena slice — and init via `nt_ui_menu_init`), so
  the core `nt_ui_context` pays zero bytes for a game that uses no menu and the
  menu TU dead-strips. The SAME instance MUST be reused every frame: it holds the
  per-level keyboard-nav record, so a fresh `{0}` each frame would silently break
  nav. The combo carries no such scratch (its state lives in the core ctx's pool,
  bounded by caps). Row identity is KEY-STABLE via a scope stack:
  `mix(scope_id, key)` (fmix, never additive — additive sibling ids collide in
  Clay's anonymous child-id space). A submenu pushes its own row id as the child
  scope (ImGui `PushID` model), so keys need only be unique among SIBLINGS; a
  duplicate sibling key aliases the same row state and is a DEBUG fail-early
  (complete for the menu's bounded per-level cap, best-effort window-scanned for
  the unbounded combo list — a complete scan there would need heap or O(N²)).
  Keyboard nav runs in `menu_end` against THIS frame's per-level record (built as
  rows declare, complete by `menu_end`); a focus or open-chain change re-declares
  the tree and becomes navigable NEXT frame — a 1-frame latency in the EFFECT,
  not in the record lookup.

  **Atlas region identity.** `nt_atlas_region_ref_t { nt_resource_t atlas;
  uint32_t region; }` is the canonical "sprite-in-atlas" handle (atlas.id==0
  is the unset handle; consumers assign their own meaning). The widget APIs
  that take atlas art — `nt_ui_image`, `nt_ui_panel_begin`, and the
  button/checkbox style structs — take this single ref rather than separate
  `(atlas, region_index)` arguments. This is a breaking change versus the
  earlier surface; callers pass one `nt_atlas_region_ref_t`.

  **Scissor limitation.** GL scissor is axis-aligned in framebuffer
  space. A rotated scroll container is clipped by the AABB of its
  rotated corners, so content can poke past the visual corners.
  Stencil-mask fix is out of scope for v1.

  **Text-under-non-uniform-scale.** Font atlas em-size is picked from
  the X-column magnitude of the composed affine (`sqrt(a²+c²)`). Under
  uniform scale or pure rotation, glyph rasterisation stays crisp;
  under `sx ≠ sy` the quad stretches but the rasteriser samples one
  axis, blurring the other.)
- hot reload of compiled native/WASM code
- generic reflection-heavy system architecture
- WebGL 1 support
- a command/RPC API in the shipped runtime (NOTE: the `nt_devapi` milestone adds a
  dev-only, self-describing JSON command surface for engine introspection and
  automation. It is gated by `NT_DEVAPI_ENABLED` (OFF by default) and the
  `engine/devapi` subdir is compile-excluded from release, so it is **not** part of
  the shipped runtime or the asset/format pipeline. See §24.5.)

---

# 3. Platform Architecture

## 3.1 Initial platform target

```
WEB / WASM + WebGL 2
```

Meaning:

- application runs in browser
- engine compiled to WASM
- rendering through WebGL 2 backend
- debugging via browser console and overlays

WebGL 2 is the sole baseline. WebGL 1 is not supported. Rationale: WebGL 2 coverage is 95%+ of devices; the remaining 5% cannot run the target content (large 3D worlds) regardless; WebGL 2 gives native instancing, UBO, NPOT textures, gl_VertexID without extension management overhead; cleaner migration path to future WebGPU.

Windows / desktop platform is not required in v0.1 but architecture must allow adding `platform_win32` or other platform backends later. All platform-specific code (window creation, input, audio, etc.) works through engine abstractions.

Platform backends use the module-composition model (interface / impl / stub); see §26.

## 3.2 Platform layer

Platform is a **subsystem/module**, not an ECS component.

```text
platform/
    platform.h
    platform_web.c
    platform_web.h
```

Future optional additions:

```text
platform_win32.c
platform_linux.c
```

## 3.3 Platform responsibilities

Platform module handles:

- application startup/shutdown hooks
- canvas/window integration
- timing and frame delta
- input event forwarding
- browser-specific bridges (JS interop)
- file/network helpers (async fetch)
- frame scheduling hook
- canvas resize / device pixel ratio handling
- orientation change handling

Platform does **not** handle:

- gameplay
- scene logic
- render passes
- material logic
- resource manifests

## 3.4 Canvas, DPR, and Viewport

Platform layer must handle:

- **Device pixel ratio (DPR):** mobile devices may have DPR 2-3x. Rendering at native resolution on high-DPR is often prohibitive. Platform should expose current DPR and allow render resolution scaling.
- **Canvas resize:** window/orientation changes must update framebuffer size. Platform detects resize events and notifies engine.
- **Input coordinate mapping:** pointer positions arrive in CSS pixels, must be mapped to canvas/framebuffer coordinates.

```c
typedef struct PlatformDisplayInfo {
    uint32_t canvas_width;      // CSS pixels
    uint32_t canvas_height;     // CSS pixels
    uint32_t framebuffer_width; // actual render pixels
    uint32_t framebuffer_height;
    float dpr;          // device pixel ratio
    float render_scale; // game-controlled quality scaling
} PlatformDisplayInfo;
```

---

# 4. Frame Lifecycle

The engine owns the top-level frame execution. The game provides callbacks.

## 4.1 Engine callbacks exposed to game

```c
void game_init(void);
void game_fixed_update(float dt);
void game_update(float dt);
void game_render(void);
void game_shutdown(void);
```

## 4.2 Engine frame order

```text
platform_step
input_begin_frame
    → if pointer pressed && audio suspended → audio_try_resume()
input_event_apply
resource_step         ← async loading processing
game-defined resource sync helpers
    → e.g. sprite_comp_sync_resources() after resource publication changes
audio_update          ← voice state management
nt_mem_scratch_reset  ← frame scratch arena cleared (see §5.2)
fixed_update loop
game_update           ← CLAY layout, NT_UI_DATA_* allocations
transform_update
game_render           ← nt_ui_walk reads scratch pointers
```

`nt_mem_scratch_reset()` MUST run before any scratch allocation in the
current frame — typically right after `audio_update`. Allocating then
resetting in the same frame invalidates pointers already handed to
systems (e.g. `nt_ui` retains them through `nt_ui_walk`).

## 4.3 Fixed update loop

```c
accumulator += frame_dt;
int fixed_steps = 0;

while (accumulator >= fixed_dt && fixed_steps < max_fixed_steps)
{
    game_fixed_update(fixed_dt);
    accumulator -= fixed_dt;
    fixed_steps++;
}
```

Recommended defaults:

```c
fixed_dt = 1.0f / 60.0f
max_fixed_steps = 4
```

## 4.4 Update responsibilities

### `game_fixed_update(dt)`

Stable simulation: movement, AI, combat, timers, deterministic logic, future physics.

### `game_update(dt)`

Frame-based logic: camera, UI logic, effect fades, interpolation inputs, render-state preparation.

### `transform_update()`

Happens after gameplay movement, before render.

## 4.5 Optional interpolation factor

```c
float alpha = accumulator / fixed_dt;
```

Can be used for render interpolation between previous/current transform state.

## 4.6 Systems registry not used

The engine does not have a system registry. The game calls its systems explicitly in code. System order is defined explicitly. No phases, dependency graph, or scheduler.

```c
void game_fixed_update(float dt)
{
    movement_system_fixed(dt);
    ai_system_fixed(dt);
    combat_system_fixed(dt);
}

void game_update(float dt) {
    camera_system_update(dt);
    ui_system_update(dt);
}
```

---

# 5. Memory Policy

## 5.1 High-level memory rules

- no heap allocation in hot path if avoidable
- component storages preallocated
- asset metadata always resident
- pack blobs transient
- frame temporary memory reset every frame
- renderer staging/batch buffers explicitly sized
- resource pools managed centrally

## 5.2 Memory categories

### Permanent memory

Lifetime ≈ engine/application lifetime.

Examples: entity tables, component storages, asset metadata, shader metadata, persistent runtime pools.

### Pack/blob transient memory

Lifetime ≈ load operation or recent-use cache window.

Examples: loaded pack blob, manifest read buffer, temporary decompression buffer if ever needed.

### Frame scratch memory

Lifetime: from allocation until the next `nt_mem_scratch_reset()` (typically the start of the next frame, see §4.2).

Examples: render item arrays, temporary sort arrays, transient CPU batch buffers, build temp lists in render pass, `nt_ui_element_data_t` attached to CLAY elements via `NT_UI_DATA_*` macros.

The engine provides a global bump arena in `engine/memory/nt_mem_scratch`:

```c
nt_mem_scratch_init(NT_MEM_SCRATCH_DEFAULT_SIZE_BYTES); // boot, default 512 KB
// per frame (see §4.2):
nt_mem_scratch_reset();            // start of frame
nt_mem_scratch_alloc(size, align); // anywhere during the frame
// pointers stay valid until the next nt_mem_scratch_reset()
nt_mem_scratch_shutdown(); // exit
```

Type-safe macros: `NT_MEM_SCRATCH_ALLOC(T)`, `NT_MEM_SCRATCH_ALLOC_ARRAY(T, count)`.

## 5.3 Capacity policy

Capacities split into two tiers.

**Compile-time hard caps.** Sized at build time. Used where a single global
sparse table or fixed pool needs a known upper bound. Cannot be overridden
without recompiling the engine.

```c
#define NT_MAX_ENTITIES 65536 /* sparse side, generation tables */
#define NT_MAX_PACKS 16
#define NT_MAX_SLOTS 2048
#define NT_MAX_ASSETS 2048
#define NT_MAX_RENDER_ITEMS 16384
#define NT_MAX_AUDIO_CLIPS 256
#define NT_MAX_AUDIO_VOICES 32
#define NT_MAX_POINTERS 8
```

**Init-time component capacities.** Each component module exposes a
descriptor and a defaults helper. The game picks the size at init based
on its scene density. Allocation happens once in init (`calloc`), never
grows after — the "preallocated, no heap in hot path" rule from §5.1
still holds.

```c
nt_sprite_comp_init(&(nt_sprite_comp_desc_t){ .capacity = 4096 });
/* or use defaults: */
nt_sprite_comp_init(&nt_sprite_comp_desc_defaults());
```

Suggested baseline defaults (override per game):

| Component       | Default | Notes                          |
|-----------------|---------|--------------------------------|
| transform_comp  | 256     | Adjust for scene density       |
| drawable_comp   | 256     |                                |
| mesh_comp       | 256     |                                |
| material_comp   | 256     |                                |
| sprite_comp     | 256     |                                |
| text_comp       | 64      |                                |
| shadow_comp     | 256     |                                |

Component capacity must not exceed `NT_MAX_ENTITIES` — the sparse side is
sized off the entity table, not the component capacity.

## 5.4 Runtime settings

```c
typedef struct EngineSettings
{
    float fixed_dt;
    int max_fixed_steps;
} EngineSettings;
```

No full config system (JSON/YAML/ini) required at start.

---

# 6. Entity System

## 6.1 Entity identity

Entities are lightweight IDs with generation validation.

```c
typedef uint16_t EntityIndex;
typedef uint16_t EntityGeneration;

typedef struct EntityHandle {
    EntityIndex index;
    EntityGeneration generation;
} EntityHandle;
```

If larger scale is needed later, compile-time switch can allow 32-bit indices.

**Generation overflow note:** `uint16_t` generation overflows after 65535 create/destroy cycles on a single slot. For long-running web sessions with hot slots, monitor usage. If needed, upgrade to `uint32_t` generation (minimal cost increase).

## 6.2 Why generation exists

Without generation, stale entity handles can silently become valid again after slot reuse. Generation solves stale references and destroyed-then-reused slot ambiguity.

## 6.3 Entity table

Engine-level entity data lives in entity system, not in Transform.

Per entity slot:

```c
generation[index]
alive[index]
enabled[index]

parent[index]
first_child[index]
next_sibling[index]
prev_sibling[index]
```

This gives: valid/alive checks, hierarchy, enable/disable tree support, transform inheritance source tree, general logical tree use.

## 6.4 Hierarchy policy

Hierarchy belongs to **entity system**, not Transform.

Reasons: hierarchy useful beyond transform, enable/disable subtree, ownership/tree traversal, tree-based logic, render grouping if desired later.

## 6.5 Root detection

No separate root component needed. An entity is a root if:

```c
parent == INVALID_ENTITY_INDEX
```

## 6.6 Entity destruction and cleanup

When an entity is destroyed, systems that hold component data for that entity must be cleaned up. Two strategies are available:

1. **Deferred destruction queue:** mark entities for destruction, process queue at a defined point in frame (e.g., after game_update, before transform_update).
2. **Immediate destruction with per-storage cleanup:** entity_destroy iterates registered storages and removes components.

For v0.1, deferred destruction is recommended to avoid mid-frame structural changes to storages.

---

# 7. Component Storage Design

## 7.1 Storage model

Each component type has: unique component per entity, sparse lookup, dense storage, preallocated capacity.

## 7.2 Canonical storage layout

```c
typedef struct
{
    ComponentType data[CAPACITY];
    ComponentIndex entity_to_index[MAX_ENTITIES];
    EntityIndex index_to_entity[CAPACITY];
    uint32_t count;
} ComponentStorage;
```

Where:

- `entity_to_index` maps entity → dense index
- `index_to_entity` maps dense index → entity
- `data` stores actual dense components

Yes, sparse side is sized by max entities even if component capacity is smaller. This is correct and intentional.

## 7.3 Component index type

Default component indices = `uint16_t`. Only use `uint32_t` if truly needed later. Do not use odd-width runtime types like 12-bit indices.

## 7.4 Component API style

Typed APIs, not one generic mega-API. Each component module exposes its own
init descriptor, lifecycle (`init`, `shutdown`), per-entity ops (`add`, `has`,
`remove`), and per-field accessors. Components do not return a monolithic
struct — fields live in parallel SoA arrays and are read/written one at a time.

```c
/* Lifecycle */
nt_result_t nt_transform_comp_init(const nt_transform_comp_desc_t *desc);
void nt_transform_comp_shutdown(void);

/* Per-entity ops */
bool nt_transform_comp_add(nt_entity_t e);
bool nt_transform_comp_has(nt_entity_t e);
void nt_transform_comp_remove(nt_entity_t e);

/* Per-field accessors return pointers into the dense SoA — the caller
 * mutates fields directly through the returned pointer where appropriate. */
float *nt_transform_comp_position(nt_entity_t e); /* vec3 */
float *nt_transform_comp_rotation(nt_entity_t e); /* vec4 quaternion */
/* ... */
```

Modules with cross-field invariants (e.g. sprite_comp, where `atlas` and
`region_hash` drive a cached `region_index`) return `const` accessors and
provide dedicated setters instead. See per-component sections in §8 and §9
for the actual API surface.

---

# 8. Transform System

## 8.1 Transform component data

The transform component is SoA — there is no monolithic `TransformComponent`
struct in the code. Each entity contributes one row across these dense fields:

| Field          | Type     | Notes                                |
|----------------|----------|--------------------------------------|
| local position | `vec3`   | mutable via `nt_transform_comp_position(e)`  |
| local rotation | `vec4`   | quaternion, via `nt_transform_comp_rotation(e)`  |
| local scale    | `vec3`   | via `nt_transform_comp_scale(e)`     |
| world matrix   | `mat4`   | read-only via `nt_transform_comp_world_matrix(e)`; recomputed by `nt_transform_comp_update()` |
| dirty          | `bool`   | mutable; set when locals change      |

API surface lives in `engine/transform_comp/nt_transform_comp.h`.

Optional future additions: previous_world_matrix, decomposed world data, bounds dirty flag.

## 8.2 Hierarchy source

Transform inheritance reads from entity hierarchy. Transform does not own parent/child links.

## 8.3 Update model

Update top-down. Roots are entities with no parent. Traversal uses entity hierarchy.

## 8.4 Dirty propagation

When local transform changes: mark this transform dirty, mark descendant transforms dirty. When parent changes: mark subtree dirty. Dirty only means world transform must be recomputed.

## 8.5 Non-transform nodes in hierarchy

Entities without Transform may still exist in hierarchy.

Traversal rule: walk entity tree; if node has transform, update world basis; if node has no transform, continue traversal with last valid inherited transform basis.

---

# 9. Render-Related Components

The architecture supports different renderable kinds via separate components, not one universal component.

## 9.1 Common render state

The drawable component is SoA. Fields:

| Field   | Type          | Default      | Notes                                |
|---------|---------------|--------------|--------------------------------------|
| tag     | `nt_hash32_t` | `{0}`        | pass/group filter; set via `nt_hash32_str("world")` etc. |
| visible | `bool`        | `true`       | render visibility only               |
| color   | `vec4`        | `(1,1,1,1)`  | object tint / alpha multiplier       |

Accessors for `tag` and `visible` return mutable pointers. Color is mutated
through `nt_drawable_comp_set_color()` / `nt_drawable_comp_set_alpha()` so the
module can keep its float SoA and packed RGBA8 mirror in sync for renderers.
API lives in `engine/drawable_comp/nt_drawable_comp.h`.

Per-entity shader params (`params0`) deferred to ShaderParamsComponent — add when per-entity shader effects are needed (#98).

## 9.2 Mesh component

Per-entity mesh handle. The component stores a single `nt_mesh_t` per entity
(typed handle from the gfx module); accessor returns a mutable pointer:

```c
nt_mesh_t *nt_mesh_comp_handle(nt_entity_t e);
```

API in `engine/mesh_comp/nt_mesh_comp.h`. There is no `MeshComponent` struct
or `MeshAssetRef` wrapper — just one handle per entity.

## 9.3 Material component

Per-entity material handle. The component stores a single `nt_material_t`
per entity (handle from the `nt_material` module); accessor returns a
mutable pointer:

```c
nt_material_t *nt_material_comp_handle(nt_entity_t e);
```

API in `engine/material_comp/nt_material_comp.h`. There is no
`MaterialComponent` struct — just one handle per entity. The `nt_material`
module behind the handle is described in §16.

## 9.4 Sprite component

The sprite component is a SoA module — there is no monolithic `SpriteComponent`
struct. Each sprite-bearing entity contributes one row across parallel dense
arrays (atlas handle, region hash, cached region index, cached atlas revision,
effective origin, flag bits). The module owns those arrays directly and
exposes them via per-entity accessors and a bulk view (see header
`engine/sprite_comp/nt_sprite_comp.h` for the full API).

### Identity

Sprite identity is the pair `(nt_resource_t atlas, uint64_t region_hash)`.
That pair survives atlas republish, hot reload, and region renumbering.

The runtime additionally caches:

- **Resolved region index** — `uint16_t` index into the atlas region table.
- **Atlas revision snapshot** — `uint32_t`, used to detect republish.
- **Effective origin** — `float[2]`, either authored from the region or
  overridden by the game.
- **Flag byte** — `FLIP_X`, `FLIP_Y`, `ORIGIN_OV`, `RESOLVED`.

These are implementation details. Game code reads them through the public
accessors; layout (SoA, packed flags, sentinel choices) is not part of the
contract.

### Binding modes

Two ways to bind a sprite to a region, picked by what the game knows:

- **By hash (slow path, async-friendly).** Game knows `region_hash` only.
  The atlas may not be ready yet. Sync resolves the hash to a region index
  on the next `nt_sprite_comp_sync_resources()` once the atlas is published.
  Cost: one hash-table lookup per sprite per resolve.
- **By index (fast path, requires ready atlas).** Game already knows the
  numeric region index — typically because it cycled an animation frame,
  or read it back from a previously resolved sprite. Skips the hash lookup
  on bind and on stable frames (the atlas-revision gate inside sync resolves
  to a no-op when nothing changed). After atlas republish, sync re-resolves
  via hash to pick up a removed region going dead (`vertex_count==0`). The
  atlas merge contract guarantees every region ever present keeps the same
  index for the atlas lifetime — removed regions are marked dead in place but
  keep their index and revive there if re-added — so the cached index never
  moves; a dead region simply zero-draws.

Game code is free to read back the cached region index for animation logic
(e.g. cycle to the next frame). It is stable across atlas republish for
surviving regions;
the only failure mode — tombstoning — is observable via
`is_resolved()`
    .

### Origin override and flip

Each sprite carries an effective origin (`float[2]`). By default it tracks
the region's authored origin from the atlas. The game may override it with
`set_origin(x, y)`, which sets the `ORIGIN_OV` flag and pins the value across
subsequent atlas republishes. `reset_origin()` clears the override and
restores the authored value on the next sync.

Flip is a pair of flag bits (`FLIP_X`, `FLIP_Y`) toggled via `set_flip`.
They are pure state — no resolve, no atlas interaction.

### Lifecycle

```c
nt_sprite_comp_init(&(nt_sprite_comp_desc_t){.capacity = 4096});
/* per-frame: */
nt_resource_step();
nt_sprite_comp_sync_resources(); /* explicit, after publication */
/* on shutdown: */
nt_sprite_comp_shutdown();
```

Capacity is set once at init (see §5.3). All SoA arrays are allocated up
front and never grow.

### Resolution flow

Resolution is explicit, not renderer-driven magic:

- game code requests / mounts resources
- `resource_step()` publishes winners
- game code calls `nt_sprite_comp_sync_resources()` (or equivalent system)
- sync iterates dense sprite rows when the resource publication epoch
  advanced or any sprite was bound by hash since the last call; per-row
  early-out via cached atlas revision keeps stable frames cheap
- sprite render-item build skips unresolved sprites

### Bulk iteration

`nt_sprite_comp_view()` returns base pointers into the dense SoA arrays plus
the live count and an `entity_indices` array (dense → entity index) for
joining sprites with other components. Pointers are stable for the lifetime
of the module; values shift on add/remove (swap-and-pop) so views must not
be cached across mutations.

> **Status:** sprite_comp, atlas runtime data, explicit resource sync, and the
> dedicated SpriteRenderer are implemented. Sprite render-item construction is
> still game-side code: the engine consumes caller-provided render-item arrays
> and does not introduce a hidden sprite scheduler.

Sprite is a separate render kind, not a special mode of mesh.

## 9.5 Text component

> **Status:** the text component module does not exist yet — only the
> `nt_font` module backing it is implemented (`engine/font/nt_font.h`).
> The shape below describes the planned component.

```text
text_comp fields (planned):
  font   nt_font_t   /* handle from nt_font_create / nt_font_add */
  text   StringId    /* intern-table reference, design TBD */
```

`nt_font_t` is a pool-backed handle to a font instance. A font instance owns GPU textures (curve + band) and a glyph cache. Font data comes from one or more `nt_resource_t` assets attached via `nt_font_add()`, allowing fallback chains (base font + CJK extension pack, etc.). Glyphs are decoded and uploaded to GPU on first lookup, not on asset load.

StringId references a string in a string pool/intern table (detail deferred to implementation phase).

## 9.6 Shadow component

> **Status:** the shadow component module does not exist yet. The shape
> below describes the planned component once a shadow pass lands.

```text
shadow_comp fields (planned):
  enabled            bool
  mesh_override      nt_mesh_t       /* optional override; INVALID = use primary mesh */
  material_override  nt_material_t   /* optional override; INVALID = use primary material */
```

If missing: object does not participate in shadow pass. If present and enabled: use override mesh/material if valid, otherwise use primary mesh/material or default shadow path.

---

# 10. Render Tags

## 10.1 RenderTag philosophy

Render tags are **game-defined**, not engine-enum-defined. Tags are `nt_hash32_t` values created via `nt_hash32_str()`.

```c
nt_hash32_t TAG_WORLD = nt_hash32_str("world");
nt_hash32_t TAG_UI = nt_hash32_str("ui");
nt_hash32_t TAG_DEBUG = nt_hash32_str("debug");
```

## 10.2 What tags mean

RenderTag means: pass category, render grouping chosen by game, filter for pass building.

RenderTag does **not** mean: component type, mesh vs sprite vs text, material type.

The renderer backend and low-level render API do not know about tags. Tags are used by game code for filtering, grouping by pass, choosing sort/batch policy.

---

# 11. Rendering Architecture

## 11.1 Engine/game boundary

Renderer backend and render primitives belong to engine. Render pipeline belongs to game.

### Engine provides

- renderer begin/end frame
- begin/end pass
- draw mesh primitive
- draw sprite primitive
- GPU resource creation and binding
- material/shader binding helpers

### Game decides

- pass order
- which tags are used
- sort policy for a pass
- whether a pass sorts by depth or material
- whether a given list uses batching or not

## 11.2 Renderer backend API shape

Engine-oriented, not WebGL-mirror and not full WebGPU abstraction:

```c
renderer_begin_frame();
renderer_end_frame();

renderer_begin_pass(&desc);
renderer_end_pass();

renderer_set_camera(&camera);

renderer_draw_mesh(...);
renderer_draw_sprite(...);
```

## 11.3 Renderer complexity classes

Not all renderers carry the same weight. The engine ships three classes; copying patterns across classes is a common mistake.

**Building blocks** — direct GPU primitives (`nt_gfx_draw_indexed`, `nt_mesh_renderer`). Single pipeline, fixed pattern, one draw call per item. Use for 3D meshes, custom geometry, anything where the game owns batching strategy. Stay minimal.

**Batched dynamic** — high-throughput accumulation renderers (`nt_sprite_renderer`; future particles). Cmd queue, state-delta tracking, overflow recovery via snapshot/replay, multi-page atlas resolution, SIMD path. Optimized for many small draws per frame (1k–60k items). Complex by necessity — the 580 LOC of `nt_sprite_renderer.c` are paid for by measured throughput on bunnymark. Don't simplify away the cmd queue or snapshot recovery without a measured replacement plan.

**Specialized** — domain-specific layout (`nt_text_renderer` glyph atlas + line layout; future debug-line/IM-GUI). Sit between the two — more state than primitives, less throughput pressure than batched dynamic.

When adding a new renderer, classify first:

- One pipeline, fixed pattern → **building block** (model after `nt_mesh_renderer`)
- 1k+ items/frame with dynamic state → **batched dynamic** (study `nt_sprite_renderer`, but only copy what your throughput demands)
- Domain-specific layout/data → **specialized**

`nt_sprite_renderer.c` is not a renderer template. Its complexity earns its keep at 60k items/frame; a 100-item UI overlay doesn't need any of it.

---

# 12. Render Items, Sort Keys, Batch Keys

## 12.1 RenderItem concept

A RenderItem is a **CPU-side prepared draw record**, not a GPU object.

```text
Entity / components
    → RenderItem build
    → sort
    → renderer consumes
    → GPU draw calls
```

## 12.2 RenderItem model

Minimal render item — sorted draw record, not a fat data carrier. Renderer reads per-entity data (world matrix, color) from components at draw time.

```c
typedef struct nt_render_item_t {
    uint64_t sort_key;  // 8 bytes — encodes material+mesh for opaque, depth for transparent
    uint32_t entity;    // 4 bytes — raw entity id
    uint32_t batch_key; // 4 bytes — state compatibility (same material+mesh = same key)
} nt_render_item_t;     // 16 bytes, naturally aligned
```

**batch_key vs sort_key:** sort_key controls draw order (can be anything: material, depth, layer). batch_key controls instancing compatibility (same material+mesh). These are independent — depth-sorted items still batch by material+mesh.

**Why no inline world_matrix:** Instance packing reads world_matrix + color from component arrays via entity lookup (scattered access). Inlining them in the render item (96B) would make packing sequential, but qsort on 96B elements is ~6× slower than on 16B. At typical scales (<5K entities), sort dominates over packing. If CPU-bound at 10K+: switch to radix sort or indirect sort, then fat items become free.


## 12.3 Sort key meaning

Sort key determines item order in the final draw sequence for a pass. It is pass-dependent. There is not one universal sort key layout for all passes.

**Material-sorted pass:** sort by material/state/pipeline/texture.

**Depth-sorted pass:** sort by depth first, then other fields as tie-break.

## 12.4 Batch key / run detection

`batch_key` encodes state compatibility (same material+mesh = same key). `sort_key` controls draw order. These are independent concerns — sort order can be anything (material, depth, layer) without affecting batch detection.

Game fills `batch_key` via `nt_batch_key(material_id, mesh_id)`. Renderer compares consecutive batch_keys to detect instancing runs:

```c
while (run_end < count && items[run_end].batch_key == items[run_start].batch_key) run_end++;
```

---

# 13. Sorting Policy

## 13.1 Sort policy is pass-controlled

The game decides sort mode for each pass. Typical modes: sort by material/state, sort by depth, no sort, custom order + tie-break.

## 13.2 Depth sorting

Depth is computed on CPU only when needed. Transparent/depth-sensitive passes compute depth. Opaque/material passes may skip depth entirely.

**Do not compute depth for all items by default.**

---

# 14. Batching and Instancing

## 14.1 Renderer-specific batching

### SpriteRenderer

CPU batch: SpriteRenderer consumes consecutive `batch_key` runs, emits dynamic
sprite vertices into a shared staging VBO, records draw commands on state/page
changes, and flushes when staging capacity, uint16 index range, or command
capacity requires it. Rect and polygon sprites share the same generic dynamic
IBO path — see §14.3. Atlas regions still carry `NT_ATLAS_REGION_FLAG_QUAD_*`
metadata for a future GPU-instanced rect renderer (Issue #176); the current
SpriteRenderer ignores those flags.

### MeshRenderer

No true merging for arbitrary meshes. Same batch key means no state changes. Later add instancing for same mesh/material runs.

## 14.2 Mesh instancing

Mesh instancing is desired early. Works best when: same mesh, same material, same shader layout, different world/object params only.

WebGL 2 provides native `drawArraysInstanced` / `drawElementsInstanced` — no extension management needed.

## 14.3 Sprite batching strategy

Sprite renderer: gather sorted sprite render items, resolve component SoA views
once, pack sprite vertices into one dynamic vertex buffer per flush chunk, and
draw recorded commands. The renderer owns atlas page correctness: `batch_key`
is a compatibility hint from the game, while SpriteRenderer verifies actual
atlas page textures and splits commands when a run crosses pages.

Rect and polygon sprites use the same generic dynamic IBO path. The renderer
does not keep a separate static-quad fast path unless measurements show a clear
win on the target workload; this keeps the sprite batching code small and makes
draw splitting depend only on capacity and state changes.

---

# 15. Shader System

## 15.1 ShaderAsset purpose

ShaderAsset defines interface, not values.

## 15.2 ShaderAsset fields

```c
typedef struct ShaderAsset {
    ShaderCodeRef vs;
    ShaderCodeRef fs;

    uint32_t vertex_input_mask;

    uint16_t material_vec4_count;
    uint16_t texture_slot_count;

    uint16_t object_usage_mask;
    uint16_t global_usage_mask;

    BlendMode default_blend_mode;
    bool default_depth_test;
    bool default_depth_write;
    CullMode default_cull_mode;
} ShaderAsset;
```

## 15.3 Four levels of shader data

1. **vertex inputs** — from geometry/mesh
2. **material params** — from MaterialAsset
3. **object params** — from RenderState, Transform
4. **globals** — from renderer/pass

## 15.4 Vertex input mask

Possible semantics: POSITION, NORMAL, UV0, COLOR0. Mesh/shader compatibility validated in builder and sanity-checked at runtime.

## 15.5 Object params

Fixed object-level params for v0.1: world_matrix, object_color, object_params0.

## 15.6 Global params

Possible globals: view, proj, view_proj, camera_pos, time, light_dir. Start minimal, expand later.

WebGL 2 Uniform Buffer Objects can be used to share globals efficiently across shaders.

---

# 16. Material System

## 16.1 MaterialAsset purpose

Material = shader + render state + values.

## 16.2 Numeric params policy

All numeric material params stored as `vec4[]`. This is intentional.

Rule: float uses `.x`, vec2 uses `.xy`, vec3 uses `.xyz`, vec4 uses `.xyzw`.

Benefits: simple layout, simple alignment, easy future GPU block packing, no per-type runtime complexity.

## 16.3 MaterialAsset binary layout

> **Status:** materials today are created at runtime from
> `nt_material_create_desc_t` (see `engine/material/nt_material.h`). There is
> no `NT_ASSET_MATERIAL` activator and no pack-loadable material format yet.
> The layout below describes the planned on-disk shape once material assets
> become pack-loadable. `ShaderAssetRef` and `TextureAssetRef` are also
> planned types; current shaders are loaded as `NT_ASSET_SHADER_CODE` blobs
> referenced by `nt_resource_t` directly.

```c
// In-memory header (NOT a C struct with FAM) — PLANNED, not yet implemented
typedef struct MaterialAssetHeader {
    ShaderAssetRef vertex_shader;
    ShaderAssetRef fragment_shader;

    BlendMode blend_mode;
    bool depth_test;
    bool depth_write;
    CullMode cull_mode;

    uint16_t param_count;
    uint16_t texture_count;
} MaterialAssetHeader;
```

    Binary layout in pack :

```text
┌─────────────────────────┐
│ MaterialAssetHeader      │
├─────────────────────────┤
│ vec4 params[param_count] │
├─────────────────────────┤
│ TextureAssetRef          │
│   textures[texture_count]│
└─────────────────────────┘
```

At runtime, params and textures are accessed via computed offset from header pointer:

```c
const vec4 *material_get_params(const MaterialAssetHeader *h) {
    return (const vec4 *)((const uint8_t *)h + sizeof(MaterialAssetHeader));
}

const TextureAssetRef *material_get_textures(const MaterialAssetHeader *h) { return (const TextureAssetRef *)(material_get_params(h) + h->param_count); }
```

**Note:** C does not allow two flexible array members in one struct. The layout above uses computed offsets instead.

## 16.4 One material, one copy

No duplicated material data. Material is created once (either from code via descriptor or loaded from pack asset in the future) and lives in a single pool slot. Multiple entities reference the same material handle.

Per-entity variation (e.g. per-character color, dissolve progress) goes through entity param components, not material mutation — each entity carries its own values, the material stays shared.

Material-wide params (e.g. global alpha cutoff, roughness) can be mutated at runtime via `nt_material_set_param` / `nt_material_set_param_component`. This changes the value for all entities sharing that material. The renderer re-reads params every frame; no version bump is needed. Hash-based overloads (`_h` suffix) accept a pre-computed `nt_hash32_t` to avoid per-frame string hashing.

## 16.5 Render state and material

Material stores render state (blend mode, depth test/write, cull mode) because it is a property of the surface, not the pass. Pipeline (GPU state object) is derived from material render state + mesh vertex layout at render time.

Sort order is **not** a material property. Sorting is game-controlled: game code gets entities by tag, sorts them (by material for opaques, by depth for transparents, or any custom order), and submits draw items to the renderer in that order. The renderer draws in submission order and batches consecutive compatible items. See section 13.1.

---

# 17. Resource System

## 17.1 Core concepts

- `publication_epoch`: monotonic counter that changes when the published view of any slot changes

- `resource_id`: 64-bit xxHash of asset path (`nt_hash64_t`) — stable resource identity
- `NtAssetMeta`: per-asset metadata entry (one per asset per pack)
- `NtResourceSlot`: per unique resource requested by game code — holds resolved handle and optional user_data
- `nt_resource_t`: generational handle to a slot — what game code holds and passes around

Two-level system:
- **Assets** (MAX_ASSETS): metadata from all packs. Same resource_id can appear in multiple packs.
- **Slots** (MAX_SLOTS): unique resources requested by game. One slot per resource_id, holds the published handle plus any per-slot auxiliary state.

Each slot tracks two winner notions:
- **target winner**: highest-priority READY asset for this resource_id
- **published winner**: highest-priority asset that is usable right now

For simple runtime-handle asset types (texture, mesh, blob), target and published winners usually match. Asset types that derive persistent auxiliary state from pack bytes (atlas, future similar types) may defer publication until `user_data` has been synchronized to the target winner.

## 17.2 ResourceId

`resource_id` is a `uint64_t` xxHash (XXH64) of the asset path, wrapped in `nt_hash64_t` for type safety. Game code obtains it via `nt_hash64_str("path")`. The `nt_hash` module provides centralized hashing for both builder and runtime. The registry uses resource_id to match assets across packs and resolve priority.

## 17.3 Generational handles

Game code receives `nt_resource_t` — a 32-bit handle encoding slot index (lower 16 bits) and generation (upper 16 bits). Generation detects stale handles within a single init/shutdown lifecycle. After shutdown, all handles are invalid — game code must re-request resources after reinit. Access functions (`nt_resource_get`, `nt_resource_is_ready`) validate generation before returning data. `nt_resource_get()` returns the currently published winner handle. `nt_resource_is_ready()` means "published winner is fully usable", not merely "some runtime handle exists somewhere in the stack."

Typed wrappers (MeshHandle, TextureHandle) live outside nt_resource — game code or future phases.

`nt_resource_publication_epoch()` exposes a monotonic change counter for systems that want to skip work when published slot data has not changed.

## 17.4 AssetMeta stability

**Unmount** removes asset entries (resource_id = 0) — slots are recycled for new packs. **Unload** (Phase 25) clears runtime handle/state but preserves metadata — enables fast reload without re-parsing.

`NT_BLOB_AUTO` eviction clears only the pack blob bytes. Already-activated assets keep `state == READY` and their `runtime_handle`. Whether a slot can stay published after eviction depends on asset type:
- simple assets stay usable from the runtime handle alone
- aux-backed assets stay published only if their existing `user_data` already belongs to the published winner

## 17.5 NtAssetMeta

```c
typedef struct {
    uint64_t resource_id;    /* nt_hash64 value */
    uint32_t offset;         /* byte offset in pack blob */
    uint32_t size;           /* asset data size */
    uint32_t runtime_handle; /* resolved handle, 0 = none */
    uint16_t format_version; /* per-type binary format version */
    uint16_t pack_index;     /* index into packs[] */
    uint8_t asset_type;      /* nt_asset_type_t */
    uint8_t state;           /* nt_asset_state_t */
    uint8_t is_dedup;        /* 1 = shares data with another asset in same pack */
    uint8_t _pad;
    uint32_t meta_offset; /* byte offset into pack's resident meta_data buffer (NT_NO_METADATA = no meta) */
} NtAssetMeta;
```

## 17.6 NtResourceSlot

Persistent per-slot state — survives across frames:

```c
typedef struct {
    uint64_t resource_id;            /* nt_hash64 value */
    uint32_t runtime_handle;         /* published winner's runtime handle (what game sees) */
    uint16_t generation;             /* stale-handle detection; incremented on slot reuse */
    int16_t resolve_prio;            /* priority of currently published winner */
    uint16_t resolve_seq;            /* mount_seq of published winner (tiebreak) */
    uint16_t resolve_asset_idx;      /* index into assets[] of published winner */
    uint16_t prev_resolve_asset_idx; /* previous published winner (change detection) */
    uint16_t user_data_asset_idx;    /* asset idx last used to build user_data (aux sync check) */
    uint32_t prev_runtime_handle;    /* previous published handle (detect re-activation) */
    uint8_t asset_type;              /* nt_asset_type_t */
    uint8_t state;                   /* nt_asset_state_t visible to game code */
    void *user_data;                 /* per-slot auxiliary data (on_resolve/on_cleanup) */
} NtResourceSlot;
```

Transient per-pass state — allocated at the start of each resolve pass, freed at the end. Lives in a separate array to keep NtResourceSlot small for the common case (resolve runs only when `needs_resolve` is true):

```c
typedef struct {
    uint32_t target_runtime_handle;    /* best READY asset handle, even if blob is evicted */
    uint32_t candidate_runtime_handle; /* best READY asset handle that is publishable now */
    int16_t target_prio;               /* priority of target winner */
    int16_t candidate_prio;            /* priority of publishable candidate */
    uint16_t target_seq;               /* mount_seq of target winner */
    uint16_t candidate_seq;            /* mount_seq of publishable candidate */
    uint16_t target_asset_idx;         /* assets[] index of target winner */
    uint16_t candidate_asset_idx;      /* assets[] index of publishable candidate */
    uint8_t scan_state;                /* best nt_asset_state_t seen among all matching assets */
    uint8_t resolve_pending;           /* on_resolve fired for the published winner */
    uint8_t post_resolve_pending;      /* on_post_resolve should fire after the pass */
} NtResolveTemp;
```

The resolve pass computes both the target winner and the published winner for each slot:
- the target winner is purely priority/sequence-based over READY assets
- the published winner is the best asset that is usable now

For aux-backed asset types, "usable now" means one of two things:
- `user_data` was already built from this exact asset (`user_data_asset_idx == asset_idx`)
- or the winner's blob is currently resident, so `on_resolve` can rebuild `user_data` immediately

If a higher-priority target winner is not yet publishable, the slot keeps the best lower-priority usable fallback published. If no usable fallback exists, the slot reports `LOADING` until publication can complete. `nt_resource_get_state()` and `nt_resource_is_ready()` always report the published state, not the raw target winner state.

### 17.6.1 Resolve callbacks (on_resolve / on_cleanup / on_post_resolve)

Per-asset-type callbacks for auxiliary data that persists across pack stacking. Registered separately from activate/deactivate — asset types that don't use them pay nothing.

```c
typedef void (*nt_resolve_fn)(const uint8_t *data, uint32_t size, uint32_t runtime_handle, void **user_data);
typedef void (*nt_cleanup_fn)(void *user_data);
typedef void (*nt_post_resolve_fn)(const uint8_t *data, uint32_t size, nt_resource_t handle, uint32_t runtime_handle, void *user_data);

nt_resource_set_resolve_callbacks(asset_type, on_resolve, on_cleanup);
nt_resource_set_post_resolve_callback(asset_type, on_post_resolve);
nt_resource_set_behavior_flags(asset_type, flags);
void *nt_resource_get_user_data(handle);
```

Behavior flags:
- `NT_RESOURCE_BEHAVIOR_AUX_BACKED`: published winner is deferred until `user_data` is synchronized to the winning asset. If the target winner requires aux data but its file-pack blob is currently missing, `resource_step()` schedules that pack for immediate re-download.

**on_resolve** fires in Phase D for the published winner when:
- published asset identity changes
- published `runtime_handle` changes (re-activation / invalidate / context loss)
- or an aux-backed asset is being published but its `user_data` has not yet been synchronized to that asset

`on_resolve` only runs when the published winner is usable now. For aux-backed assets this means the callback either already owns matching `user_data`, or the winner's blob is resident and can be parsed immediately. For simple runtime-handle asset types, `data` may still be NULL (virtual pack, placeholder-style handle substitution, or evicted blob) because publication can proceed from the runtime handle alone. The data pointer is valid only for the duration of the call — callbacks must copy if needed.

**on_cleanup** fires when a slot loses its published real winner (no publishable real candidate remains) and during shutdown for remaining non-NULL user_data. `on_resolve` requires `on_cleanup` — registering resolve without cleanup is an assert.

**on_post_resolve** fires after the resolve iteration finishes. It may call `request` / `find` / `get` style resource accessors, but must not recurse into `mount` / `unmount` / `step` / `load` / `parse`. Typical use: materialize dependent resource slots from ids that were copied in `on_resolve`.

Publication change detection uses three pieces of state: published asset identity (`resolve_asset_idx`), published `runtime_handle`, and aux synchronization (`user_data_asset_idx`). Placeholder substitution does not trigger `on_resolve` — placeholders are visual fallbacks, not real winners.

`resource_step()` may run more than one resolve pass in the same frame when `on_post_resolve` work creates new slots that need resolution. The pass count is bounded.

## 17.7 Virtual packs

Game code can create virtual packs to register runtime-created resources (procedural textures, generated meshes) into the registry. Virtual pack assets participate in priority stacking identically to file pack assets. Unmount clears registry entries but does not destroy resources — game owns them.

```c
nt_resource_create_pack(pack_id, priority);
nt_resource_register(pack_id, resource_id, asset_type, runtime_handle);
nt_resource_unregister(pack_id, resource_id);
```

## 17.8 Asset types

```c
typedef enum {
        NT_ASSET_MESH = 1,
        NT_ASSET_TEXTURE = 2,
        NT_ASSET_SHADER_CODE = 3,
        NT_ASSET_BLOB = 4,  /* generic binary data (game-defined) */
        NT_ASSET_FONT = 5,  /* font glyph data (Slug format) */
        NT_ASSET_ATLAS = 6, /* atlas region metadata (vertices + UVs + origin) */
    } nt_asset_type_t;
```

Additional types (material, audio) will be added as needed.

### NT_ASSET_FONT binary format

Builder produces font assets from TTF/OTF sources. Binary layout:

```
NtFontAssetHeader (16 bytes)
  magic:        u32   (0x544E4F46 "FONT")
  version:      u16   (2)
  glyph_count:  u16
  units_per_em: u16
  ascent:       i16
  descent:      i16   (negative)
  line_gap:     i16

NtFontGlyphEntry[glyph_count] (24 bytes each, sorted by codepoint for bsearch)
  codepoint:    u32
  data_offset:  u32   (byte offset from header start)
  advance:      i16
  bbox:         i16 x4 (x0, y0, x1, y1)
  curve_count:  u16
  kern_count:   u16
  _reserved:    u8 x2

Per-glyph data (at data_offset):
  NtFontKernEntry[kern_count] (4 bytes each, sorted by right_glyph_index)
    right_glyph_index: u16
    value:             i16
  Contour data (delta-encoded int16 coordinates, line/quadratic bitmask)
```

Runtime does not parse TTF. Glyph contours are delta-encoded quadratic Bezier curves (lines promoted to degenerate quadratics). At lookup time, contours are decoded into float control points, decomposed into horizontal bands, and uploaded to GPU textures for Slug-style vector rendering. Glyphs are cached with LRU eviction — not immutable once loaded.

### NT_ASSET_ATLAS binary format

Builder produces atlas assets from a set of sprite PNGs (or raw RGBA buffers). One atlas yields **two kinds of pack entries**: a single `NT_ASSET_ATLAS` blob with region metadata, plus N `NT_ASSET_TEXTURE` page entries (named `<atlas>/tex0`, `<atlas>/tex1`, …). Runtime keeps a 1:N relationship — one metadata blob references N textures.

Binary layout (`shared/include/nt_atlas_format.h`, packed, **v6**):

```
NtAtlasHeader (28 bytes)
  magic:               u32  (0x534C5441 "ATLS")
  version:             u16  (6)
  region_count:        u16  (one entry per source sprite)
  page_count:          u16  (number of texture pages)
  _pad:                u16
  vertex_offset:       u32  (byte offset from header start)
  total_vertex_count:  u32
  index_offset:        u32  (byte offset from header start)
  total_index_count:   u32

texture_resource_ids[page_count]: u64
  Each entry is nt_hash64_str("<atlas_name>/tex<N>") matching the
  page texture's resource_id in the same pack.

NtAtlasRegion[region_count] (48 bytes each, v6)
  name_hash:      u64   (xxh64 of region name)
  source_w:       u16   (original image width in pixels, pre-trim)
  source_h:       u16   (original image height in pixels, pre-trim)
  trim_offset_x:  i16   (pixels stripped from the left edge during alpha trim)
  trim_offset_y:  i16   (pixels stripped from the BOTTOM edge in y-up source space.
                         v5 — was top edge in v4. Builder converts at write time:
                         trim_offset_y = source_h - trim_y_png - trim_h.)
  origin_x:       f32   (pivot X, normalized over source_w — 0.5 = centre, 1.0 = right edge.
                         Values outside [0, 1] are allowed for off-frame pivots. Source-space
                         NOT trim-space — stable across animation frames with varying trim bounds.)
  origin_y:       f32   (pivot Y, normalized over source_h, in y-up source space.
                         v5 — 0.0 = bottom edge, 1.0 = top edge. Builder converts at write
                         time: origin_y = 1 - origin_y_png.)
  vertex_start:   u32   (index into vertex array — u32 in v3, was u16 in v2)
  index_start:    u32   (index into the index array — u32 in v3, was u16 in v2)
  vertex_count:   u8    (vertices for this region; ≤ max_vertices)
  page_index:     u8    (which texture page)
  transform:      u8    (3-bit D4 mask: bit0=flipH, bit1=flipV, bit2=diagonal)
  index_count:    u8    (triangle indices for this region; ≤ 255)
  flags:          u8    (builder-authored render hints, e.g. NT_ATLAS_REGION_FLAG_QUAD_*;
                         bit 3 reserved)
  _pad0:          u8    (alignment padding for uint16 slice9_lrtb)
  slice9_lrtb[4]: u16   (slice9 borders [left, right, top, bottom] in pixels;
                         all zero = no slice9. Non-zero values signal 9-cell
                         stretching at runtime — no separate flag bit needed.
                         Slice9 sprites are never alpha-trimmed and always use
                         RECT shape.)
  _reserved2[2]:  u8    (must be zero)

NtAtlasVertex[total_vertex_count] (8 bytes each, at vertex_offset)
  local_x:   i16  (corner X in trim-rect local space, 0..trim_w.
                   Polygon vertices use corner coordinates, not pixel centres.
                   Source-image pos: local_x + trim_offset_x
                   Pivot-relative:   (local_x + trim_offset_x) - origin_x * source_w)
  local_y:   i16  (corner Y in trim-rect local space, y-up — 0 = bottom of trim,
                   trim_h = top. v5 — was y-down in v4. Symmetric to local_x:
                   pivot_relative_y = (local_y + trim_offset_y) - origin_y * source_h.
                   Runtime cached_pos applies this directly with no Y-flip.)
  atlas_u:   u16  (normalized 0..65535 over atlas page width)
  atlas_v:   u16  (normalized 0..65535 over atlas page height. UV.v stays y-down because
                   atlas page texture pixel data is uploaded top-row-first; UV.v=0 maps to
                   PNG top, which is what the y-up sprite sees at its high-y vertex.)

uint16[total_index_count] (at index_offset)
  Triangle list, indices local per region (0 .. vertex_count-1).
  Builder pre-swaps each triangle's last two indices at pack time (a,b,c)→(a,c,b)
  so the in-blob winding is world-CCW after y-up vertices are read directly. This
  lets sprite materials use cull_mode = BACK without per-game opt-outs.
  Runtime offsets indices by vertex_start when building GPU buffers.
```

Runtime keeps an owned atlas snapshot in slot `user_data`, not a raw mmap view. On first publication the atlas module validates the blob, copies region metadata, vertex data, index data, and page resource ids into owned buffers, then builds an open-addressing hash table for O(1) region lookup. UVs are pre-normalized and triangles are pre-built by the builder (Clipper2 CDT for all polygons, fan triangulation as fallback on CDT failure).

Subsequent publications merge by `name_hash` to preserve stable region indices across pack stacking:
- common regions update metadata in place and rewrite their copied vertex/index payload
- new regions append to the end
- removed regions are marked dead in place (`vertex_count = index_count = 0`) but KEEP their `name_hash` and stay in the hash table, so a later merge that re-adds the name revives the SAME index — a resolved region index is therefore stable for the atlas lifetime
- the hash table is rebuilt from all named regions (live + dead) after each merge

Page texture resource ids are copied during `on_resolve`. The actual `nt_resource_t` page handles are materialized in `on_post_resolve` and cached in the atlas snapshot, so `nt_atlas_get_page_resource()` remains O(1).

Atlas registers `NT_RESOURCE_BEHAVIOR_AUX_BACKED`. A higher-priority atlas whose blob is currently missing becomes the target winner, but it is not published until its metadata snapshot has been rebuilt. If a lower-priority usable atlas is already published, it stays active until the target blob is reloaded and resolved.

## 17.9 Placeholder policy

Texture-only placeholder: if a texture slot has no publishable READY asset, `nt_resource_step()` may publish the placeholder resource's handle for rendering. Non-texture resources never publish placeholder handles and return handle 0 when not ready.

```c
// Placeholder is a regular resource (e.g. from a virtual pack or base pack)
nt_resource_set_placeholder_texture(nt_hash64_str("textures/placeholder.png"));
```

The function automatically requests a slot for the placeholder resource_id if one does not exist. Placeholder participates in the same resolve system — if the placeholder resource itself has no publishable READY winner, no substitution occurs. Publishing a placeholder handle does not make the slot READY: `nt_resource_is_ready()` remains false and `nt_resource_get_state()` continues to report the non-ready state.

## 17.10 nt_hash -- Identity Hashing

`nt_hash` provides xxHash (XXH32/XXH64) hashing in 32-bit and 64-bit widths. Used for resource identity (64-bit) and attribute/pack naming (32-bit). Both builder and runtime link this module -- single source of truth for hash computation. xxHash chosen over FNV-1a for superior avalanche properties (critical for open-addressing hash maps) and higher throughput on WASM.

Type-safe wrappers prevent accidental mixing of raw integers with hash values:

```c
typedef struct {
    uint32_t value;
} nt_hash32_t;
typedef struct {
    uint64_t value;
} nt_hash64_t;
```

API:

```c
nt_hash32_t nt_hash32(const void *data, uint32_t size);
nt_hash64_t nt_hash64(const void *data, uint32_t size);

static inline nt_hash32_t nt_hash32_str(const char *s);
static inline nt_hash64_t nt_hash64_str(const char *s);
```

Debug label system for hash-to-string reverse lookup (compile-time toggle `NT_HASH_LABELS`):

```c
void nt_hash_register_label64(nt_hash64_t hash, const char *label);
const char *nt_hash64_label(nt_hash64_t hash);
```

CRC32 remains in `shared/` for pack data checksum -- different purpose (error detection vs identity hashing).

---

# 18. Async Loading System

## 18.1 Overview

On the web, all data loading is asynchronous. `fetch()` returns a Promise. The main thread cannot be blocked. Loading must be non-blocking and integrated into the frame loop.

The same async contract applies to all platforms for consistency — desktop implementations may complete instantly but the API contract remains "potentially async."

## 18.2 Pack state machine

```c
typedef enum {
    NT_PACK_STATE_NONE = 0,    /* not loaded */
    NT_PACK_STATE_REQUESTED,   /* I/O request issued */
    NT_PACK_STATE_DOWNLOADING, /* receiving data (progress available) */
    NT_PACK_STATE_LOADED,      /* data received, not yet parsed */
    NT_PACK_STATE_READY,       /* parsed, assets registered */
    NT_PACK_STATE_FAILED,      /* load failed (may retry) */
} nt_pack_state_t;
```

## 18.3 Asset state machine

```c
typedef enum {
        NT_ASSET_STATE_REGISTERED = 0, /* meta exists, data not loaded */
        NT_ASSET_STATE_FAILED,         /* error, permanent, no retry */
        NT_ASSET_STATE_LOADING,        /* being activated; slot state may also wait for publication */
        NT_ASSET_STATE_READY,          /* runtime handle valid; for slots this means published winner fully usable */
    } nt_asset_state_t;
```

## 18.4 Pack loading flow

```text
game code: pack_request_load("world.pak")
  → PackMeta.state = REQUESTED
  → platform_web calls fetch() via JS bridge

... N frames pass ...

JS callback → WASM: platform_on_fetch_complete(request_id, blob_ptr, blob_size, success)
  → PackMeta.state = LOADED
  → PackMeta.blob = blob_ptr

Next resource_step():
  → sees LOADED pack
  → parses header/manifest (NTPACK format, direct struct read)
  → registers AssetMeta entries (state = REGISTERED)
  → PackMeta.state = READY

Asset activation (eager with rate-limit):
  → resource_step() processes up to N assets per frame
  → reads data from blob by offset/size
  → parses runtime format
  → creates GPU resources / decodes audio
  → AssetState = READY

Resolve/publication:
  → dirty slots run a resolve pass after activation / mount / unmount / priority change / invalidation
  → simple asset types publish immediately once the target winner is READY
  → aux-backed asset types run on_resolve to build per-slot user_data before publication
  → if the highest-priority target winner needs aux data but its blob is missing, the slot keeps the best usable fallback published or reports LOADING and schedules a reload
```

## 18.5 Loading progress

Current `NtPackMeta`:

```c
typedef struct {
    uint32_t pack_id;    /* nt_hash32 value */
    int16_t priority;    /* higher = wins on conflict */
    uint8_t pack_type;   /* NT_PACK_FILE or NT_PACK_VIRTUAL */
    uint8_t mounted;     /* 1 if slot occupied */
    uint16_t mount_seq;  /* monotonic mount order tiebreak */
    uint8_t pack_state;  /* nt_pack_state_t */
    uint8_t blob_policy; /* NT_BLOB_KEEP or NT_BLOB_AUTO */
    const uint8_t *blob; /* loaded pack bytes, may be NULL after eviction */
    uint32_t blob_size;  /* original blob size */
    uint8_t *meta_data;  /* resident metadata copy (survives blob eviction) */
    uint32_t meta_size;
    uint32_t meta_count;
    uint32_t bytes_received; /* async progress */
    uint32_t bytes_total;
    uint32_t io_request_id;
    uint8_t io_type;        /* NT_IO_NONE / NT_IO_FS / NT_IO_HTTP */
    uint16_t attempt_count; /* retry state */
    uint32_t retry_delay_ms;
    uint32_t retry_time_ms;
    uint32_t blob_last_access_ms;
    uint32_t blob_ttl_ms;
    char load_path[256];
} NtPackMeta;
```

`meta_data` is copied out of the pack blob at parse time so metadata queries survive blob eviction. `retry_*`, `io_type`, and `load_path` drive both normal retry/backoff and immediate aux-miss reloads. `blob_last_access_ms` + `blob_ttl_ms` implement `NT_BLOB_AUTO` eviction.

## 18.6 JS bridge — fetch contract

C exports:

```c
// Called from C → JS
void platform_request_fetch(uint32_t request_id, const char *url);

// Called from JS → C
EMSCRIPTEN_KEEPALIVE
void platform_on_fetch_progress(uint32_t request_id, uint32_t received, uint32_t total);

EMSCRIPTEN_KEEPALIVE
void platform_on_fetch_complete(uint32_t request_id, uint8_t *data, uint32_t size, uint32_t success);
```

## 18.7 Asset activation strategy

**Eager with rate-limit**: when a pack becomes READY, `resource_step()` processes up to N assets per frame from the ready queue. This prevents frame spikes while ensuring assets become available quickly.

Any change that can affect publication (`mount`, `unmount`, `set_priority`, asset activation, virtual register/unregister, invalidation, placeholder change, or aux-miss reload scheduling) marks the registry dirty. Dirty frames run a resolve scan over assets to compute each slot's target winner and published winner. Clean frames stay on the O(1) fast path.

If `on_post_resolve` work creates new dependent slots (for example atlas page textures), `resource_step()` may execute additional resolve passes in the same frame. The total pass count is bounded to avoid infinite loops.

## 18.8 Retry policy

Normal load failures use 1-2 retries with exponential backoff. After retries fail: PackState = FAILED, log error, game code decides response (show error, retry later).

Aux-miss reloads (target winner requires aux data but its blob was evicted) reuse the same I/O path, but schedule an immediate retry on the next `resource_step()` instead of waiting for backoff.

## 18.9 Memory note

Peak memory during loading = 2x pack size (JS fetch buffer + WASM heap copy). For packs in the low megabytes range this is acceptable.

---

# 19. Pack Format (NTPACK)

## 19.1 Design rationale

Custom flat binary format instead of ZIP. Rationale:

- no external library dependency (no miniz in WASM, saves ~15-25KB binary size)
- trivial parsing: direct struct reads, no variable-length header parsing
- zero-copy asset access: pointer + offset into loaded blob
- manifest is embedded in header, not a separate file
- HTTP transport compression (gzip/brotli) handles delivery size
- partial loading via HTTP Range requests is straightforward (header first, then assets by offset)

## 19.2 Binary layout

```text
┌──────────────────────────────────────┐
│ NtPackHeader (32 bytes, packed)       │
│   magic: uint32     "NPAK"           │
│   meta_count: uint32                 │
│   version: uint16   NT_PACK_VERSION  │
│   asset_count: uint16                │
│   header_size: uint32  ← data start  │
│   total_size: uint32                  │
│   checksum: uint32     ← CRC32       │
│   meta_offset: uint32  ← meta start  │
│   _pad: uint32      (8-byte align)   │
├──────────────────────────────────────┤
│ NtAssetEntry[0] (24 bytes, packed)    │
│   resource_id: uint64                 │
│   offset: uint32  ← from file start  │
│   size: uint32                        │
│   format_version: uint16              │
│   asset_type: uint8                   │
│   _pad: uint8                         │
│   meta_offset: uint32  ← per-asset   │
├──────────────────────────────────────┤
│ NtAssetEntry[1..N-1]                  │
│   ...                                 │
╞══════════════════════════════════════╡
│ [padding to 8-byte alignment]         │
│ [asset 0 binary data]                 │
│ [asset 1 binary data]                 │
│ ...                                   │
│ [asset N-1 binary data]               │
╞══════════════════════════════════════╡
│ [meta section] (optional)             │
│   NtMetaEntryHeader + payload ...     │
│   grouped by resource_id              │
└──────────────────────────────────────┘
```

Assets aligned to 4 bytes (NT_PACK_ASSET_ALIGN). Header/entries region aligned to 8 bytes (NT_PACK_DATA_ALIGN) before data start. Meta section appended after asset data, covered by CRC32. Resident copy made at parse time (survives blob eviction).

## 19.2.1 Version policy

No backwards compatibility. Runtime asserts `version == NT_PACK_VERSION`. Old packs must be rebuilt when format changes. This is intentional: the engine is in active development, and maintaining backwards compat for a format that changes frequently adds complexity without benefit. Builder and runtime always agree on version.

## 19.2.2 Metadata section

Optional section after asset data. Contains variable-length entries (NtMetaEntryHeader + payload) grouped by resource_id. Header-level `meta_offset` points to section start; per-asset `meta_offset` points to first entry for that asset. Used for game-defined metadata (tags, material bindings, custom properties). AABB is not metadata — it lives in NtMeshAssetHeader as inherent mesh data.

```c
NtMetaEntryHeader (20 bytes, packed):
    uint64_t resource_id;  /* which asset */
    uint64_t kind;         /* hash64 of metadata type name */
    uint32_t size;         /* payload bytes (max 256) */
    /* uint8_t data[size] follows immediately */
```

Query: `nt_resource_get_meta(handle, nt_hash64_str("tag").value, &size)` — returns pointer to resident memory, NULL if absent.

## 19.3 Runtime parsing

```c
// Pseudocode — see nt_resource.c for actual implementation
void parse_pack(const uint8_t *blob, uint32_t blob_size) {
    const NtPackHeader *h = (const NtPackHeader *)blob;

    NT_ASSERT(h->magic == NT_PACK_MAGIC);
    NT_ASSERT(h->version == NT_PACK_VERSION); /* no backwards compat */

    const NtAssetEntry *entries = (const NtAssetEntry *)(blob + sizeof(NtPackHeader));

    for (uint16_t i = 0; i < h->asset_count; i++) {
        NtAssetMeta *meta = asset_alloc();
        meta->resource_id = entries[i].resource_id;
        meta->offset = entries[i].offset;
        meta->size = entries[i].size;
        /* Convert per-asset meta_offset from absolute to meta_data-relative */
        meta->meta_offset = (entries[i].meta_offset != 0) ? entries[i].meta_offset - h->meta_offset : NT_NO_METADATA;
    }

    /* Copy meta section to resident memory (survives blob eviction) */
    if (h->meta_count > 0 && h->meta_offset != 0) {
        uint32_t meta_size = blob_size - h->meta_offset;
        pack->meta_data = malloc(meta_size);
        memcpy(pack->meta_data, blob + h->meta_offset, meta_size);
    }
}
```

## 19.4 Asset data access

```c
const uint8_t *pack_get_asset_data(const PackMeta *pack, uint32_t offset, uint32_t size) {
    return pack->blob_data + offset;
}
```

Zero copy. Data is already in WASM heap.

## 19.5 Debugging

Builder includes `pack_dump(filename)` utility command that prints pack contents to console. No external tool needed.

## 19.6 Future: partial loading

Flat layout allows HTTP Range requests: load first `header_size` bytes to get manifest, then load individual assets by offset/size on demand.

---

# 20. Runtime Formats

## 20.1 General rule

Runtime reads only runtime formats. Builder converts from source formats to runtime formats.

Examples:

- source `.glb` → runtime mesh binary
- source `.png` → runtime texture binary
- source material description → runtime material binary
- source `.wav`/`.ogg` → runtime audio binary (OGG Vorbis)

## 20.2 Runtime format validation

Runtime must validate: magic, version, type, sizes/offsets, required vertex/material compatibility. Builder validation is primary. Runtime validation is safety net.

## 20.3 Mesh format strategy

Runtime mesh format should be: compact, near GPU-ready, not authoring-friendly.

Attributes: POSITION (required), NORMAL (optional), UV0 (optional), COLOR0 (optional).

Preferred data types: position float16 or float32, normals snorm8 packed, uv unorm16, colors uint8 normalized. Avoid runtime unpacking.

---

# 21. Input System

## 21.1 Model

Input system is polling-based. Game queries state each frame, does not subscribe to callbacks.

## 21.2 Pointer state

```c
typedef struct InputPointer {
    bool active;
    bool down;
    bool pressed;
    bool released;

    float x;
    float y;
    float prev_x;
    float prev_y;
    float dx;
    float dy;

    uint32_t capture_owner;
} InputPointer;
```

    Mouse and touch unify under pointer model
        .

## 21.3 Input capture

Capture is stored centrally in input system.

```c
bool input_try_capture(int pointer, uint32_t owner);
void input_release_capture(int pointer, uint32_t owner);
bool input_is_owner(int pointer, uint32_t owner);
bool input_pointer_captured(int pointer);
```

Raw input always exists; capture only affects processing ownership.

Capture owner: not necessarily entity id, generic `uint32_t owner_id` chosen by game/systems. Auto-release on pointer release.

---

# 22. Audio System

## 22.1 Architecture overview

Audio is an **engine module**, analogous to input and platform. Not an ECS component, not game-side code.

```text
engine/
    audio/
        audio.h           // public API — single for all platforms
        audio_types.h     // handles, enums, defines
        audio_web.c       // Web Audio API via JS bridge
        audio_desktop.c   // miniaudio or custom mixer (future)
```

Build system compiles only one implementation file per platform.

## 22.2 Platform-agnostic design

**Public API contains zero platform-specific types.** Only handles, floats, and bools. Game code is identical across web and desktop.

Key contracts:

- `audio_clip_create` is always potentially async (desktop may complete instantly, but game code does not rely on this)
- `audio_try_resume()` exists on all platforms (no-op on desktop)
- Audio format in packs is OGG Vorbis — both platforms can decode it
- Internal structures are different per-platform, hidden from game code

## 22.3 Audio state

```c
typedef enum AudioState {
        AUDIO_SUSPENDED, // before first user gesture (web) or init failure
        AUDIO_RUNNING,   // ready to play
        AUDIO_FAILED     // AudioContext/backend creation failed
    } AudioState;
```

All `audio_play` calls in SUSPENDED state return `AUDIO_VOICE_INVALID` without error. Game code continues normally.

## 22.4 Audio clips

```c
typedef struct AudioClipHandle {
    uint16_t index;
} AudioClipHandle;
#define AUDIO_CLIP_INVALID ((AudioClipHandle){0xFFFF})

typedef enum AudioClipState {
    AUDIO_CLIP_NONE,
    AUDIO_CLIP_DECODING, // decodeAudioData in progress (web) or decoding (desktop)
    AUDIO_CLIP_READY,
    AUDIO_CLIP_FAILED
} AudioClipState;
```

Internal storage (web):

```c
typedef struct AudioClipInternal {
    AudioClipState state;
    uint32_t js_buffer_id; // index into JS-side AudioBuffer array
    float duration;
    uint16_t generation;
} AudioClipInternal;
```

Internal storage (desktop):

```c
typedef struct AudioClipInternal {
    AudioClipState state;
    int16_t *pcm_data; // decoded samples in C heap
    uint32_t sample_count;
    uint32_t sample_rate;
    uint8_t channels;
    float duration;
    uint16_t generation;
} AudioClipInternal;
```

## 22.5 Audio voices

```c
typedef struct AudioVoiceHandle {
    uint16_t index;
} AudioVoiceHandle;
#define AUDIO_VOICE_INVALID ((AudioVoiceHandle){0xFFFF})

typedef enum AudioVoiceState { VOICE_FREE, VOICE_PLAYING, VOICE_STOPPING } AudioVoiceState;
```

Voice pool with eviction: when all 32 voices are occupied, evict the oldest non-looping voice. If all voices are looping, do not play the new sound.

## 22.6 Public API

```c
// === Lifecycle ===
void audio_init(void);
void audio_shutdown(void);
void audio_update(void);
AudioState audio_get_state(void);

// === Resume (call on user gesture) ===
void audio_try_resume(void);

// === Clips ===
AudioClipHandle audio_clip_create(const uint8_t *encoded_data, uint32_t size);
void audio_clip_destroy(AudioClipHandle clip);
AudioClipState audio_clip_get_state(AudioClipHandle clip);
float audio_clip_get_duration(AudioClipHandle clip);

// === Playback ===
AudioVoiceHandle audio_play(AudioClipHandle clip, float volume, float pitch, bool loop);
void audio_stop(AudioVoiceHandle voice);
void audio_stop_all(void);

// === Voice control ===
void audio_set_volume(AudioVoiceHandle voice, float volume);
void audio_set_pitch(AudioVoiceHandle voice, float pitch);
bool audio_is_playing(AudioVoiceHandle voice);

// === Global ===
void audio_set_master_volume(float volume);
float audio_get_master_volume(void);
```

## 22.7 JS bridge contract (web implementation)

C calls to JS:

```c
extern void js_audio_init(void);
extern void js_audio_shutdown(void);
extern void js_audio_resume(void);
extern uint32_t js_audio_decode(uint16_t clip_index, const uint8_t *data, uint32_t size);
extern uint32_t js_audio_play(uint32_t js_buffer_id, float volume, float pitch, bool loop, uint16_t voice_index);
extern void js_audio_stop(uint32_t js_source_id);
extern void js_audio_set_volume(uint32_t js_source_id, float volume);
extern void js_audio_set_pitch(uint32_t js_source_id, float pitch);
extern void js_audio_set_master_volume(float volume);
```

JS calls to C:

```c
EMSCRIPTEN_KEEPALIVE
void audio_on_clip_decoded(uint16_t clip_index, uint32_t js_buffer_id, float duration, uint32_t success);

EMSCRIPTEN_KEEPALIVE
void audio_on_voice_ended(uint16_t voice_index);

EMSCRIPTEN_KEEPALIVE
void audio_on_state_changed(uint32_t running);
```

## 22.8 Integration with frame loop

In `input_begin_frame` or `platform_step`:

```c
if (audio_get_state() == AUDIO_SUSPENDED && any_pointer_pressed) {
    audio_try_resume();
}
```

`audio_update()` is called each frame for voice state management (safety timeout, future fade management).

## 22.9 Audio in resource pipeline

```c
// Builder
add_audio("assets/sfx/hit.wav");      // WAV → OGG conversion
add_audio("assets/music/theme.ogg");  // already OGG, pack as-is
```

Loading flow:

```text
Pack loaded → AssetMeta registered (REGISTERED)
  → asset_ensure_loaded() for audio:
      → read blob from pack by offset/size
      → call audio_clip_create(data, size)
      → AudioClipState = DECODING, AssetState = LOADING

  ... JS/native decoding ...

  → audio_on_clip_decoded callback:
      → AudioClipState = READY
      → AssetState = READY
```

## 22.10 What is intentionally absent

- 3D audio / positional panning (future: add PannerNode on web, positional mixing on desktop)
- Sound groups / buses (game code manages category volumes through own wrappers)
- Effects (reverb, delay)
- Fade-in / fade-out (game code does via audio_set_volume + tween)
- Streaming long tracks (OGG 128kbps ≈ 1MB/min, decodeAudioData handles 5-minute tracks in milliseconds)

---

# 23. Builder Architecture

## 23.1 Builder model

Builder is a standalone native binary (C17, with vendored C++ for Basis Universal encoder behind extern "C"). Rules are written in code.

```c
start_pack("base");
add_shaders("assets/shaders/*.shader");
add_textures("assets/textures/ui/*.png");
add_materials("assets/materials/ui/*.mat");
add_meshes("assets/meshes/common/*.glb");
add_audio("assets/sfx/*.wav");
add_audio("assets/music/*.ogg");
finish_pack();
```

## 23.2 Why code-based builder

Explicit control, no DSL needed, powerful grouping logic, easy custom per-project rules, aligns with engine philosophy.

## 23.3 Builder module layers

```text
builder/
    main_builder.c
    builder_pack.c
    builder_manifest.c
    builder_import_mesh.c
    builder_import_texture.c
    builder_import_shader.c
    builder_import_material.c
    builder_import_audio.c
    builder_project.c
```

## 23.4 Core builder API

```c
start_pack(const char *name);
finish_pack(void);

add_mesh(const char *path);
add_texture(const char *path);
add_shader(const char *path);
add_material(const char *path);
add_audio(const char *path);
add_font(const char *path);

add_meshes(const char *pattern);
add_textures(const char *pattern);
add_shaders(const char *pattern);
add_materials(const char *pattern);
add_audios(const char *pattern);
add_fonts(const char *pattern);

/* Atlas: groups N source sprites into 1 metadata blob + M texture pages.
 * Per-sprite opts carry the name override and the pivot point (NULL = defaults). */
begin_atlas(const char *name, const nt_atlas_opts_t *opts);
atlas_add(const char *path, const nt_atlas_sprite_opts_t *opts);
atlas_add_raw(const uint8_t *rgba, uint32_t w, uint32_t h, const nt_atlas_sprite_opts_t *opts);
atlas_add_glob(const char *pattern, const nt_atlas_sprite_opts_t *opts);
end_atlas(void);
```

Prefer typed wildcard functions over one untyped `add_files()`. Atlas uses a `begin/add*/end` pattern because one atlas requires its full sprite set before packing can start — this is the only place in the API where multi-call grouping is required.

## 23.5 Builder stages

1. source assets
2. import
3. validation
4. conversion to runtime format
5. pack placement with alignment
6. manifest generation (embedded in pack header)
7. write NTPACK binary

## 23.6 Builder validation

Builder must check: references between assets, resource types, mesh/material/shader compatibility, required attributes, runtime format generation correctness, audio format validity.

## 23.7 Asset ID codegen

`finish_pack` generates a `.h` header alongside each `.ntpack` with typed compile-time constants for every asset:

```c
/* Auto-generated by nt_builder -- do not edit */
#define ASSET_MESH_MESHES_CUBE_GLB ((nt_hash64_t){0x...ULL})     /* meshes/cube.glb */
#define ASSET_SHADER_SHADERS_MESH_VERT ((nt_hash64_t){0x...ULL}) /* shaders/mesh.vert */
```

Rules:
- Constants are typed compound literals `((nt_hash64_t){...})` -- work with `nt_resource_request` without casts.
- Hash values match `nt_hash64_str(normalized_path)` at runtime. The header is the single source of truth.
- Identifier format: `ASSET_{TYPE}_{
    PATH
}` where path includes file extension (`.vert`, `.frag`, `.glb` etc.) to avoid collisions between same-stem files. Slashes, dots, dashes become underscores, uppercased.
- Entries sorted alphabetically within each type group (MESH, TEXTURE, SHADER, BLOB) for deterministic, diffable output.
- `register_labels()` function under `#if NT_HASH_LABELS` registers all paths for debug hash lookup.
- Identifier collisions (two assets producing the same `#define` name) are a fatal builder error.

## 23.8 Combined headers (multi-pack projects)

Projects with multiple packs merge per-pack headers into one combined header after all packs are built:

```c
/* Each finish_pack generates a per-pack .h (e.g. core.h, textures.h) */
nt_builder_set_header_dir(ctx, "examples/myproject/generated");
nt_builder_finish_pack(ctx);

/* After all packs: merge into one combined header */
const char *headers[] = {"generated/core.h", "generated/textures.h"};
nt_builder_merge_headers(headers, 2, "generated/assets.h");
```

Rules:
- `merge_headers` reads per-pack `.h` files, deduplicates by hash, sorts, writes one combined header.
- No runtime state needed during pack building — merge operates on already-generated files.
- Game code includes the single combined header, not per-pack headers.
- Per-pack headers are still generated for diagnostics and per-pack diffing.
- `set_header_dir` controls where headers are written. Convention: `examples/{project}/generated/` in source tree so headers are visible in IDE and version control.

## 23.9 Generated headers in version control

Generated asset headers are committed to git. This is intentional:

- Output is deterministic (sorted by name, stable hashes). File only changes when assets are added, removed, or renamed.
- Git diff on the header shows exactly which assets changed between commits -- acts as an asset changelog.
- New contributors can build and run without first running the builder.
- Header files are small (one line per asset) and compress well in git.

## 23.10 Builder cache

Content-addressed encode cache. Opt-in via `nt_builder_set_cache_dir(ctx, path)`. Skips re-encoding unchanged assets on repeat builds.

**Cache key:** `decoded_hash` (xxHash64 of decoded source bytes) × `opts_hash` (hash of encode-affecting options + `NT_BUILDER_VERSION`).

**Storage:** flat directory of `.bin` files named `{decoded_hash}_{opts_hash}.bin`. No index file, no subdirectories. Cached data is raw encoded asset bytes (post-encode, pre-pack-header).

**Pipeline order:** early dedup → cache lookup → encode → cache store. Dedup runs first so duplicates never hit cache. Cache stores only unique encoded results.

**Invalidation:**
- Source data changes → different `decoded_hash` → automatic miss.
- Encode options change (format, compression, quality) → different `opts_hash` → automatic miss.
- Encoder logic changes → bump `NT_BUILDER_VERSION` → all `opts_hash` values change → full cache miss.
- Manual: delete cache directory contents.

**Safety:** write-to-temp + atomic rename. Cache failures (read/write) fall through to normal encode — never break the build.

**Build summary** reports per-asset cache status (cached / miss-new / miss-opts) and aggregate hit/miss counts.

## 23.11 Atlas builder

The atlas builder packs a set of sprite images into one or more atlas pages and emits compact runtime metadata (`NT_ASSET_ATLAS`, see §17.8). It is the only "grouping" producer in the builder — every other importer is single-asset.

**API shape (begin/add*/end):**

```c
nt_atlas_opts_t opts = nt_atlas_opts_defaults();  /* atlas-level: packer, format, etc. */
opts.max_size = 2048;
opts.max_vertices = 8;
opts.shape = NT_ATLAS_SHAPE_CONCAVE_CONTOUR;

nt_builder_begin_atlas(ctx, "hero", &opts);

/* Idle frames use the default centre pivot (0.5, 0.5). */
nt_builder_atlas_add_glob(ctx, "assets/sprites/hero/idle_*.png", NULL);

/* Walk cycle: override the pivot to bottom-centre for every matched frame. */
nt_atlas_sprite_opts_t walk = nt_atlas_sprite_opts_defaults();
walk.origin_y = 1.0F; /* feet at bottom edge */
nt_builder_atlas_add_glob(ctx, "assets/sprites/hero/walk_*.png", &walk);

/* Single sprite with a custom name override. */
nt_builder_atlas_add(ctx, "assets/sprites/hero/portrait.png",
                     &(nt_atlas_sprite_opts_t){
                         .name = "hero_portrait",
                         .origin_x = 0.5F,
                         .origin_y = 0.5F,
                     });

nt_builder_end_atlas(ctx);
```

`begin_atlas` opens an atlas state on the context. Subsequent `atlas_add*` calls feed sprites in — each takes an optional `nt_atlas_sprite_opts_t*` carrying the per-sprite name override and pivot point (pass `NULL` for the centred-pivot default). `end_atlas` runs the full pipeline and registers entries. Nested atlases are not allowed (asserts).

### 23.11.1 Pipeline

`end_atlas` runs ten stages in order:

1. **alpha_trim** — extract alpha plane, find tight bbox per sprite (rejects fully transparent inputs).
2. **cache_check** — compute atlas-level cache key (per-sprite hashes + origins in add-order + pack-affecting opts + version), try loading cached placement+pages. Key is order-sensitive because cached placements reference sprites by add-order index. Post-pack fields (format, premultiplied, compress, debug_png) are excluded — they only affect the texture encode stage, which has its own cache.
3. **dedup** — hash + byte-level compare to find identical sprites; duplicates share `vertex_start`/`index_start` in the final blob.
4. **geometry** — for each unique sprite: build binary mask, optional morphological closing for disjoint components, contour trace, multi-strategy simplification (RDP / perpendicular distance / bbox / convex hull — pick lowest estimated final area), Clipper2 inflate by `extrude + padding/2`, post-verify pixel coverage with fallback to bbox.
5. **tile_pack** — call `vector_pack` (NFP packer, see below) to assign each unique sprite to a page and (x, y) position.
6. **compose** — blit trimmed pixels onto page buffers, run AABB edge-extrude only when packing uses rectangles; in polygon mode, require `extrude=0` and rely on `padding`.
7. **debug_png** — optional outline visualization (when `opts.debug_png`).
8. **cache_write** — persist placement+pages for next build.
9. **serialize** — pack `NtAtlasHeader + texture_resource_ids + regions + vertices + indices` into one blob, register as `NT_ASSET_ATLAS`.
10. **register** — add `NT_ASSET_TEXTURE` entries for each page texture, populate region codegen entries.

Stages 5–8 are skipped on cache hit; serialize/register always run.

### 23.11.2 Vector packer

The packer is **NFP/Minkowski-based** (`nt_builder_atlas_vpack.c`). For each candidate position the incoming polygon is tested against the union of No-Fit Polygons of all already-placed sprites. Properties:

- **Sub-pixel exact** — no quantization to a tile grid.
- **Concave-aware** — Clipper2 `MinkowskiSum + Union(NonZero)` produces multi-ring NFPs for concave inputs; rings are forbidden zones.
- **8 D4 orientations** — flipH, flipV, diagonal flip and combinations. Identity-equivalent orientations are deduplicated.
- **NFP cache** — 8-way set-associative seqlock cache keyed by `(placed_shape_hash, incoming_shape_hash)`. Lock-free reads via version counter, CAS writes. Same shape pair across different sprites reuses the cached NFP.
- **Parallel build** — when `nt_builder_set_threads(ctx, N)` is called, NFP construction and candidate scanning run on a thread pool. Per-thread stat accumulators merge into global stats deterministically.
- **Page growth** — sprites that don't fit allocate a new page (up to `ATLAS_MAX_PAGES = 64`); new pages start with the same dimensions as the first.

### 23.11.3 Atlas options

```c
/* Silhouette mode for atlas packing. Ordered by cost and density. */
typedef enum {
    NT_ATLAS_SHAPE_RECT = 0,            /* AABB trim rect — fastest, worst pack density */
    NT_ATLAS_SHAPE_CONVEX_HULL = 1,     /* convex hull of opaque pixels — no contour trace */
    NT_ATLAS_SHAPE_CONCAVE_CONTOUR = 2, /* concave contour + multi-strategy — densest, slowest */
} nt_atlas_shape_t;

typedef struct {
    const nt_tex_compress_opts_t *compress; /* NULL = raw RGBA */
    nt_texture_pixel_format_t format;       /* RGBA8 default */
    uint32_t max_size;                      /* max atlas page dimension (default 2048) */
    uint32_t padding;                       /* extra spacing between sprites after extrude (default 2) */
    uint32_t margin;                        /* atlas edge margin (default 0) */
    uint32_t extrude;                       /* AABB edge pixel duplication count (default 0; must be 0 unless shape == NT_ATLAS_SHAPE_RECT) */
    uint8_t alpha_threshold;                /* alpha >= threshold = opaque (default 1) */
    uint8_t max_vertices;                   /* max polygon vertices per region (default 8, hard cap 16) */
    nt_atlas_shape_t shape;                 /* silhouette mode (default NT_ATLAS_SHAPE_CONCAVE_CONTOUR) */
    bool allow_transform;                   /* try 8 D4 orientations (4 rotations × 2 flips; default true) */
    bool power_of_two;                      /* round atlas dims to POT (default true) */
    bool debug_png;                         /* write debug atlas page PNGs (default false) */
    bool premultiplied;                     /* premultiply RGB by alpha during texture encode (default true) */
} nt_atlas_opts_t;
```

**Silhouette modes (`nt_atlas_shape_t`):**

- `NT_ATLAS_SHAPE_RECT` — 4-vertex AABB of the trim rect. No contour tracing, no hull, no RDP. Fastest geometry stage; lowest pack density because the packer cannot slot concave notches between sprites. The only mode where `extrude > 0` is legal.
- `NT_ATLAS_SHAPE_CONVEX_HULL` — convex hull of opaque pixels via `binary_build_convex_polygon`, simplified to `max_vertices`. Skips morphological closing, contour tracing, RDP, and the 4-strategy pipeline entirely. Good compromise when sprites are roughly convex: noticeably denser than `RECT` without paying the full concave cost.
- `NT_ATLAS_SHAPE_CONCAVE_CONTOUR` (default) — traces the concave alpha boundary, runs RDP plus a multi-strategy simplification (RDP / perpendicular distance / bbox / convex hull), Clipper2-inflates the chosen polygon, and post-verifies pixel coverage. Internally falls back to `binary_build_convex_polygon` for degenerate inputs (disjoint components that morphological closing cannot merge, degenerate contours, Clipper2 inflate failure). Densest packing, highest cost.

**Premultiplied alpha (default):** atlas pages are encoded through the regular texture pipeline with `premultiplied = true`, which writes `RGB' = (RGB * A + 127) / 255` into the page before `strip_channels` (RAW path) or `nt_basisu_encode` (BASIS path). The resulting texture sets `NT_TEXTURE_FLAG_PREMULTIPLIED` in `NtTextureAssetHeader.flags`, and the runtime must draw with `(ONE, ONE_MINUS_SRC_ALPHA)` blending. This is what keeps NFP-packed sprites free of dark fringes at sub-pixel clearance: `(0,0,0,0)` gap pixels are the identity for premultiplied blending, so bilinear filtering at sprite edges stays correct. Setting `premultiplied = false` logs a warning and is only valid for NEAREST-filtered or fully-opaque atlases; combining it with a non-RGBA8 `format` is a hard assert.

**Hard limits:**
- `max_vertices ≤ 16`. NFP buffers are stack-sized for `nA + nB ≤ 32`.
- Per-region `index_count` is `uint8_t` → ≤ 255 indices per region. With `max_vertices ≤ 16` an ear-clipped/fan triangulation produces at most `(16 - 2) * 3 = 42` indices, so one byte is sufficient.
- Per-atlas `vertex_start` / `index_start` are `uint32_t`.

### 23.11.4 Per-sprite options (`nt_atlas_sprite_opts_t`)

Each `atlas_add` / `atlas_add_raw` / `atlas_add_glob` call accepts an optional `nt_atlas_sprite_opts_t*`. `NULL` picks the defaults (centre pivot, name derived from path).

```c
typedef struct {
    const char *name;     /* NULL = derive from path (atlas_add/glob); required for atlas_add_raw */
    float origin_x;       /* pivot X, normalized over source_w (default 0.5) */
    float origin_y;       /* pivot Y, normalized over source_h (default 0.5) */
    uint16_t slice9_left; /* slice9 borders in source pixels (0 = no slice9) */
    uint16_t slice9_right;
    uint16_t slice9_top;
    uint16_t slice9_bottom;
    uint8_t shape;        /* 0 = atlas default, 1 = RECT, 2 = CONVEX, 3 = CONCAVE */
    uint8_t allow_rotate; /* 0 = atlas default, 1 = NO */
    uint8_t max_vertices; /* 0 = atlas default, max 16 */
    uint8_t margin;       /* 0 = atlas default, per-sprite packing margin */
    uint8_t extrude;      /* 0 = atlas default, requires shape = RECT */
} nt_atlas_sprite_opts_t;
```

**Pivot semantics:**
- Normalized over the **source image** dimensions (not the trimmed rect). Default `(0.5, 0.5)` = image centre.
- **Values outside `[0, 1]` are allowed** — pivots may lie outside the frame for weapons, effects, or motion-stabilised sprites. Must be finite (`isfinite()` asserted; NaN/inf is caller bug).
- Source-space (not trim-space) is chosen so frame-by-frame animations with varying per-frame trim bounds have stable pivots across frames. A walk cycle with `origin_y = 0.9375` sits on the same source-image pixel row regardless of how much whitespace the alpha trim removed from each frame.

**Glob rule:** `atlas_add_glob` asserts `opts->name == NULL` — a single name cannot apply to N matched files without hash collisions. Each matched file derives its own name from its path, and the `origin_x/y` fields propagate to all of them. For per-file name overrides within a glob, fall back to calling `nt_builder_glob_iterate` directly with a custom callback that calls `nt_builder_atlas_add` per match.

**Dedup + different pivots:** adding the same pixel-identical sprite twice with different `origin_x/y` produces **two separate regions** that **share** `vertex_start` and `index_start` in the blob. The dedup pass matches on pixel hash + byte-level pixel compare (origin is not considered), so the geometry/pixel data is stored once; each logical region stores its own pivot. This is the cheap path for "same sprite, different anchor" (e.g. icon referenced with centre pivot in menu vs bottom-centre in HUD).

**Zero-init footgun:** C99 designated-initialiser compound literals (`&(nt_atlas_sprite_opts_t){.origin_y = 1.0F}`) zero-init unset fields — so `origin_x` becomes `0.0`, not the default `0.5`. Always start from `nt_atlas_sprite_opts_defaults()` for partial overrides, or set every field explicitly in the literal.

### 23.11.5 Atlas cache

Separate from the per-asset builder cache (§23.10) because atlas placement is a global decision over the whole sprite set.

**Cache key:** `xxh64(per_sprite(decoded_hash + origin_x + origin_y + slice9_lrtb + shape + allow_rotate + max_vertices + margin + extrude) + pack_opts + ATLAS_CACHE_KEY_VERSION)`. Per-sprite data is hashed in add-order (not sorted) because cached placements reference sprites by index. Per-sprite overrides (slice9 borders, shape, allow_rotate, max_vertices, margin, extrude) are included because they affect packing geometry and tile placement. Only pack/compose-affecting opts are included (max_size, padding, margin, extrude, alpha_threshold, max_vertices, allow_transform, power_of_two, shape); post-pack fields (format, premultiplied, compress, debug_png) are excluded — those affect the texture encode stage which has its own cache.

**Storage:** one `atlas_<key>.bin` file per cache hit, containing the placement table and the composed page pixels. On hit, the pipeline skips pack/compose/debug_png/cache_write entirely.

**Invalidation:** any change to source pixels, opts, or `ATLAS_CACHE_KEY_VERSION` produces a fresh key. The version constant is bumped when the packer's behavior changes in a way that would silently produce different output.

**Failure mode:** atomic temp+rename writes; read/write failures fall through to a fresh build, never break it.

---

# 24. Logging, Errors, Debugging

## 24.1 Logging levels

- INFO
- WARN
- ERROR
- ASSERT
- PANIC

## 24.2 Assert policy

Asserts are contracts, not error handling. A failed assert means the program is broken beyond recovery — continuing would mask bugs.

- **NT_ASSERT** — single macro, three compile-time modes via `NT_ASSERT_MODE`:
  - `0 (OFF)` — `((void)0)`, zero overhead. Available via CMake override (`-DNT_ASSERT_MODE=0`) for final production builds where binary size is critical.
  - `1 (TRAP)` — `__builtin_trap()`, no strings, minimal binary impact. **Release default.**
  - `2 (FULL)` — hookable handler with `expr/file/line` strings. **Debug default.** Tests use the handler to catch and verify assert failures via `setjmp`/`longjmp`.
- Release ships with TRAP (1): contract violations crash immediately instead of continuing with corrupted state. No string bloat, no handler overhead — just a single branch + trap instruction per assert.
- Never use asserts for conditions that can legitimately occur at runtime (missing files, user input, network errors) — those are error handling (see below).

## 24.3 Error policy

### Fatal

- backend init failure
- unsupported critical format version
- impossible startup state

### Recoverable

- missing texture → placeholder
- material mismatch → placeholder material
- resource load fail → asset state failed + log
- audio decode failure → clip state failed + log

## 24.4 Debug overlay

Recommended stats: frame time, fixed step count, draw call count, batch count, loaded resource count, active pack count, temp memory usage, audio voice count.

## 24.5 Developer API (devapi)

`nt_devapi` is an **optional, dev-only** self-describing command surface for engine introspection and automation (probing, testing, external tooling). It is gated by `NT_DEVAPI_ENABLED` (OFF by default); the `engine/devapi` subdirectory is excluded at the CMake level when off, so a release binary contains zero devapi code or symbols. A CI "zero-delta" check asserts a devapi-OFF WASM has no `nt_devapi_*` symbols. devapi is the one sanctioned exception to the runtime's no-parser rule — it is dev-only and compiled out of release.

**Transport-agnostic core.** The dispatch core is `submit(line) -> response line`: one JSON request line in, one JSON response line out, with no platform/socket/transport code. A command may also **defer** its response: `submit` returns `NULL`, the command records a deadline of `g_nt_app.frame + N` game frames at submit time, and the result is delivered later by `nt_devapi_poll_response()` once `g_nt_app.frame` has reached that deadline. The host drains ready responses each tick (via `nt_devapi_update()`); because readiness is a comparison against the game frame counter, it counts real simulation advances, not poll calls — a paused game never advances its frame, so a wait never resolves. Real transports (loopback TCP, web `ccall`) are separate, opt-in, and added by later phases.

**Self-describing registry.** Commands are registered once into a fixed-size table, each with a 7-field descriptor (`method`, `group`, `summary`, `params_shape`, `result_shape`, `frame_behavior`, `side_effects`). The discovery commands (`endpoints`, `command.describe`, `features`) expose the whole surface so a client reads it without source. A game registers `group="game"` commands through the public API with zero engine edits. `features` lists the distinct `group` values across all registered commands — which groups exist depends on which commands are compiled in (engine) or registered at runtime (game).

**Envelope.** Each request returns `{ok:true,result}` or `{ok:false,error:{code,message}}`, echoes `request_id` unchanged, and a JSON-array line runs as an ordered batch with continue-on-error. A **deferred** command emits no synchronous envelope — its `{ok,result,request_id}` envelope arrives later via `nt_devapi_poll_response()`; a deferred command is **not** allowed inside a batch (it is rejected with an error entry, since a batch is one ordered response array). A future phase may define a batch-deferred protocol.

**cJSON dependency.** devapi parses/serializes with vendored cJSON, exposed as a standalone reusable `cjson` static-lib target (not an engine module, `EXCLUDE_FROM_ALL`). cJSON is usable by games independently of devapi; only targets that link it pull it in.

## 24.6 Time & render control (`nt_app`)

`nt_app_run(fn)` is a **single, flag-aware frame loop** that owns the one `g_nt_app.dt` scalar. Its default state — mode `RUN`, `scale` 1, unpaused — is a plain wall-clock advance (`dt` = clamped wall delta, `frame++` every iteration), so a game that never touches the controls runs exactly as a minimal loop would. The game still wires poll/input/update/render inside `fn`; the engine gives building blocks, not a pipeline.

Time control is a set of flags in `g_nt_app`, set via the `nt_app_*` mutators (and exposed to a bot through the devapi `time.*`/`render.*` commands):

- **mode** `RUN` / `MANUAL`. RUN advances on wall time; MANUAL advances one fixed-`step_dt` step per queued `nt_app_step`, with no wall clock and no `max_dt` clamp — the byte-reproducible lockstep contract.
- **paused** (RUN only): `dt` = 0 and `frame` frozen while `fn` keeps running.
- **scale** (RUN only): multiplies `dt` for slow-mo / fast-forward **observation** — explicitly not a determinism primitive (reproducible fast runs use MANUAL lockstep).
- **render_enabled**: a loop-agnostic gate the game reads to skip its draw + present together.

`g_nt_app.frame` is a **simulation-advance counter**: it increments only on a real advance, so pause and manual-idle freeze it. Deferred devapi responses (`frame.wait`, `time.step`) are keyed to it — a wait resolves once `frame` reaches its submit-time deadline (`frame + N`), so it counts game frames, not transport polls, and never resolves while the sim is frozen. `frame.wait` is therefore RUN-only (rejected in manual/paused); in MANUAL a bot uses `time.step{count}`, which advances and blocks until done.

Game-elapsed time `g_nt_app.time` (a `double` — the sum of applied `dt`s, kept double so long deterministic runs don't lose seconds-resolution to float accumulation) is the **second clock**. `time.wait{seconds?}` defers on a game-time deadline (`time + seconds`, default 0 = resolve on the next drain) rather than a frame count — the right tool in RUN, where the variable `dt` (and `scale`) make a frame count meaningless, and where time-authored mechanics (cooldowns, durations) live. Because game time only flows under RUN with `scale > 0`, `time.wait` is rejected in manual/paused/scale-0 (where it could never resolve). In MANUAL the two clocks are **linear** (`time = frames × step_dt`), so `time.step` accepts either `{count}` frames or `{seconds}` (= `ceil(seconds / step_dt)` frames) — convenience for the same time-authored logic in lockstep.

The L1 mutators **assert** their invariants (finite/non-negative scale, positive `step_dt`, valid mode) — callers are trusted game code; untrusted bot input is range-checked at the devapi L2 layer and returns `bad_params`, never an assert.

## 24.7 Input automation (devapi `input.*` + player gate)

A bot drives input through the devapi `input.*` command group, which is a thin L2 veneer over an L1 engine capability (`nt_input_inject_*` + the player gate). The design rule is **bot-indistinguishable-from-human**: injected events flow through the same input-apply cores a real device hits, so the query API (`nt_input_key_is_*`, `nt_input_mouse_is_*`, `nt_input_pop_char`) cannot tell synthetic input from physical. Compiled out with the rest of devapi when `NT_DEVAPI_ENABLED` is OFF.

**Range-checked, never asserts.** Every `input.*` command validates its params and returns `bad_params` on any out-of-domain value (unknown key name, out-of-range pointer id, a button mask outside `[0,7]` or non-integral, malformed/invalid UTF-8, a frame count outside `[0,65535]`). The L1 inject API itself never asserts — it is driven by untrusted L2 — so bad bot input is always a structured error, never a crash.

**The command group** (all `frame_behavior: any`, all fire-and-forget unless noted):

| Command | Params | Result | Kind |
|---|---|---|---|
| `input.key` | `{key, down?, hold?}` | `{ok}` | inject a key edge (`down` default true), or with `hold` a tap = down@0 + up@hold |
| `input.pointer` | `{action, id, x?, y?, type?, buttons?}` | `{queued}` | the pointer primitive: action `down`/`move`/`up` on a given id (default mouse type) |
| `input.move` | `{x, y, id?, type?}` | `{queued}` | sugar: pointer move on the default mouse slot |
| `input.click` | `{x, y, button?, id?, hold?}` | `{queued}` | sugar: pointer down@0 + up@`hold` (2 entries) carrying a button mask; `hold` default 1 frame (a realistic 1-frame-held click), `hold=0` = same-frame instant click |
| `input.wheel` | `{dx?, dy?, x?, y?}` | `{ok}` | scroll the mouse slot; with `x`/`y` a move to (x,y) then the wheel (self-contained, scrolls AT (x,y)), else at the slot's **apply-time** position (no slot → no-op) |
| `input.gesture` | `{id, type?, points:[[x,y]], frame_stride?}` | `{queued}` | sugar: down@0 + a move per subsequent point (`frame_stride` apart) + up; NO C interpolation — the bot supplies the path samples |
| `input.button` | `{buttons, id?}` | `{ok}` | set the mouse-button mask `{1,2,4}` on the given id at the slot's **apply-time** position (respects a pending `input.move` queued ahead of it; no prior slot → created at (0,0)) |
| `input.set_player_enabled` | `{enabled}` | `{enabled}` | toggle the player gate (see below) |
| `input.text` | `{text}` | `{queued}` | decode a UTF-8 string → codepoints and enqueue them into the char ring |
| `input.state` | `{key?, pop_text?}` | `{down?, pressed?, released?, codepoints?}` | **READ** (not an enqueue) of the polled input state — see below |

**The L1 player gate.** `nt_input_set_player_enabled(bool)` gates **real device** events at the apply seam: while disabled, the public real-input wrappers (`nt_input_set_key`, `nt_input_pointer_*`, `nt_input_wheel`, `nt_input_buffer_char`) early-return, so a physical device is suppressed — but injected events still flow, because inject calls the `*_apply` cores directly past the gate. The bot is therefore indistinguishable from a human to the query API while the real player is locked out. The **ON→OFF edge releases held current input state (synthetic and real are indistinguishable by design — bot == human)** so nothing sticks down across a focus-lost-style cutover: held keys raise their release edge, and held pointer buttons raise their release edge with **deferred deactivation** — the slot stays active one more poll so the release edge is readable via the public mouse query, then deactivates on the next poll (the same lifecycle as a normal pointer-up). These release primitives are the **changer's** tool (a graceful bot calls them, or the game does); the engine does **not** invoke them on a devapi client disconnect — applied input is game-owned (see the B-strict disconnect rule below).

**Scheduling lives in the devapi layer; `nt_input` is a pure apply layer.** `nt_input` knows nothing about frames or scheduling: its inject API is **immediate** (`nt_input_inject_key/pointer/wheel/text`), each call staging into a bounded static-BSS **immediate inject buffer** (`NT_INPUT_INJECT_QUEUE_MAX`, `-D` overridable) that `nt_input_poll` drains **whole** every poll, after the platform poll, through the same `*_apply` cores (gate-bypassing). The **frame schedule** is owned by the devapi input group (`NT_DEVAPI_INPUT_SCHED_MAX`, `-D` overridable) — exactly because the devtool is the only side that legitimately knows `g_nt_app.frame`. The `input.*` handlers enqueue into that schedule with their offsets (immediate commands at offset 0; `input.click` = down@0 + up@hold; `input.gesture` = down@0 + moves@(idx·stride) + up@last; a `hold` key = down@0 + up@hold; a UTF-8 string = one entry per codepoint at offset 0). A command reserves its entries **whole-or-nothing** against the schedule, so a near-full schedule can never leave a stuck pointer-down or a key with no release; overflow → `bad_params`.

**The input group is an OPTIONAL devapi module.** A single CMake switch, `NT_DEVAPI_GROUP_INPUT` (default ON), gates the WHOLE group consistently: it compiles `nt_devapi_input.c`, registers the `input.*` commands, links `nt_input` + enables the `nt_input` automation surface (player gate + inject pipeline, `NT_INPUT_AUTOMATION_ENABLED`), and wires the per-tick + client-reset lifecycle hooks. With the group OFF, the devapi core and transport carry **zero** input symbols, and `nt_input` itself is the lean apply layer (no gate, no inject buffer) — "use only what you need". The core never hard-references the group: it exposes a tiny fixed-array **lifecycle-hook registry** (a per-tick hook and a client-reset hook, registered by a group's registrar exactly like its commands). The input group registers its schedule tick as the tick hook and its release-on-disconnect as the reset hook; `nt_devapi_update` and the transport's `close_client` call the generic hook runners, naming no group. Fail-fast is accepted: the group ON without an `nt_input` impl in the executable is an unresolved-symbol **link** error (the developer's responsibility), not a silent no-op.

**Advance-gated release.** `nt_devapi_update` runs every tick: first `net_poll` (handlers enqueue into the schedule), then it runs the per-tick hooks (the input group's schedule tick). The tick detects a real sim-advance by comparing `g_nt_app.frame` to its own last-seen value and, **only on an advance**, releases every entry whose countdown reached 0 into `nt_input`'s immediate buffer (decrementing survivors); the next `nt_input_poll` (same tick) applies that buffer post-edge-clear, so an injected rising edge survives to that frame's update. On a frozen tick (pause / manual-idle) the schedule releases **nothing** — synthetic input holds while the game is paused (real device input still flows through `nt_input_poll`). This replaces the old per-`nt_input` relative-countdown freeze.

**B-strict disconnect ownership.** On a devapi client disconnect the engine resets **only devapi-owned transient state**: the inject schedule, its advance-clock re-seed, and the in-flight deferred-reply queue — the gone bot's own pending bookkeeping, which must not bleed to the next client and is not game state. **Game-owned state is the changer's responsibility, never the engine's**: time mode/pause/scale, the render flag, the player gate, and already-**applied** input (a landed key/pointer DOWN) all stay as they were. The engine deliberately does not clobber them because L1 cannot tell whether the game or the bot set a given state — resetting it on a dev-client drop would violate code-first. A bot restores what it changed before disconnecting (its `finally`), or the host recovers explicitly: the bare `examples/devapi_host` watches `nt_devapi_net_has_client()` for the connected→disconnected edge and applies its OWN policy (return to plain RUN) so an ungraceful drop mid-MANUAL can't leave it frozen — host application code, not the engine library.

**`input.state` is a DEV-ONLY observation read.** It returns the input state `nt_input_poll` last produced — so before a sim-advance an enqueued inject is **not yet visible** (the drain-race, now machine-observable over the socket). With `key`, it returns `{down,pressed,released}` for that key (unknown name → `bad_params`). With `pop_text:true` it is a **CONSUMING read** — it drains the char ring into a `codepoints` raw-codepoint array as a side effect.

**`nt_input_poll(void)` contract.** No frame argument: `nt_input` is a pure apply layer that never includes `app/nt_app.h`. The host order is `nt_window_poll → nt_devapi_update → nt_input_poll → (game update)`. `nt_devapi_update` releases the due scheduled events into the immediate buffer (advance-gated), then `nt_input_poll` drains that whole buffer after the platform poll, in the same post-edge-clear window as the native char drain.

## 24.8 UI automation (devapi `ui.*`)

A bot reads and drives UI through the devapi `ui.*` command group — a thin L2 veneer with no UI logic of its own. The **read** surface (`ui.tree`, `ui.element`, `ui.contexts`) is a projection of the L1 `nt_ui` probe; the **write** surface (`ui.click`, `ui.drag`, `ui.scroll`) resolves a target to a point and hands it to the **same** synthetic inject path `input.*` uses — there is no second injector. The whole group is gated by `NT_DEVAPI_GROUP_UI` (default OFF — opt-in) and compiled out with the rest of devapi when `NT_DEVAPI_ENABLED` is OFF.

**Build deps are hard, not silent.** Two CMake `FATAL_ERROR` guards keep the group from compiling into a vacuous or unbuildable state: `NT_DEVAPI_GROUP_UI` requires `NT_UI_DEBUG_TOOLS` (the probe has a real impl only under DEBUG_TOOLS — else a 0-node stub, a false green), and requires `NT_DEVAPI_GROUP_INPUT` (the writes reuse the input group's single inject scheduler; ui without input would link-fail on the reuse wrappers). The ui group is default **OFF** (opt-in, "use only what you need") so a plain `-DNT_DEVAPI_ENABLED` build stays lean and does not pull in UI debug tools; enable it explicitly alongside `NT_UI_DEBUG_TOOLS` + `NT_DEVAPI_GROUP_INPUT` (which stays default ON), and the two guards enforce both are present. The group registers **no** tick/reset lifecycle hook: scheduling is delegated to the input group's drain, and the only ui-owned state is the host context table, which is not transient (see below).

**Context registration & lifetime.** The host registers each UI context by name (`nt_devapi_ui_register_context(name, ctx)`); the engine keeps **no** global ctx registry. Names and ctx pointers are stored **by reference** in a small fixed table (`NT_DEVAPI_UI_CONTEXT_MAX`), so both must be **pointer-stable** for the devapi lifetime. Registration is a trusted in-process host call (it **asserts** on NULL / duplicate / table-full, like `game.*` command registration) and happens once at startup — the table is cleared only at init, never on a client disconnect, so it **survives client reconnects**. Bot input that misses the table is always `bad_params`, never an assert. A command with no `ctx` resolves to the sole/first context. A ctx that has **not completed a frame** (`nt_ui_context_has_frame` false — its layout dims are 0 and the coord converters would trap) is rejected up front with `bad_params`, so no wire input ever reaches a degenerate converter.

**One coordinate space, read==write.** The group declares a **single** space: **Y-up, origin bottom-left, in ctx LAYOUT pixels** (not raw `g_nt_window.fb_*` — those differ under `nt_ui_scale`). `ui.tree`/`ui.element` bounds and `ui.click({x,y})` speak that **same** space, so a bot reads a widget's bounds and clicks its center with **no transform**. The ctx owns a device↔layout viewport (`nt_ui_set_viewport`, default identity = full screen); `nt_ui_begin` takes the **raw device** pointer and converts device→layout internally, while the writes map layout→device through that viewport — so a click lands correctly in scaled / letterboxed / DPR UIs. The sole Y-up→Y-down flip lives in the write path's target resolve. A **string id** resolves to the **same projected bounds center** the probe reports for that id (read==id-write), including 2D-affine and 3D-raycast contexts; a behind-camera or collapsed target (zero bounds) → `bad_params` rather than a click at a screen corner. Each read result carries a metadata block — `space`, `origin`, `y_axis`, `width`, `height`, `dpr`, `projection` (`2d`/`3d`), and a `viewport` rect (omitted for `3d`, where no device↔layout affine exists).

**Probe snapshot + truncation.** `ui.tree`/`ui.element` are an **immediate** read of the **last completed frame's** tree as flat, Clay-free POD nodes (`id`/`parent`/`role`/`text`/`label`/`bounds`/`visible`/`enabled`/`child_count`); strings are **copied** into node-owned fixed-cap buffers (the borrowed Clay pointer dies at the next `nt_ui_begin`). Nodes are emitted incl. invisible / offscreen / disabled — the bot filters, not the engine. The devapi read path (`nt_ui_probe_collect_owned`) walks into a **per-ctx arena scratch sized to the runtime `max_elements`** — Clay caps live elements at that same knob and each maps to ≤ 1 node, so a node-**count** overflow can never fire; `truncated` reflects **only** the internal DFS-stack depth limit (a tree nested past the walk's `STACK_CAP`). `NT_UI_PROBE_MAX_NODES` (tied to `NT_UI_DEFAULT_MAX_ELEMENT_COUNT`) bounds only the **legacy caller-buffer** `nt_ui_probe_collect` form, where the caller picks the cap and a count overrun is possible. Under truncation a not-found id returns an **honest "truncated"** error rather than "unknown id" — a dropped widget is not called stale.

**Range-checked, never asserts.** Like `input.*`, every `ui.*` command validates its params and returns `bad_params` on any out-of-domain value (non-finite or float-overflowing coords, a fractional/negative/over-`UINT16_MAX` frame count, unknown ctx, unknown / stale id, a never-begun ctx). `ui.drag` additionally floors `frames` at **1**: the inject up releases at the pointer's current position, so a 0-frame drag (no move) would land the up back at `from` and silently drop `to`; `frames` ≥ 1 always emits at least one move that ends at `to`. The writes reuse the input scheduler's **whole-or-nothing** reservation — a multi-event command (`ui.click` = down@0 + up@hold; `ui.drag` = down + `frames` interpolated moves + up; `ui.scroll` = move + wheel) reserves **all** its slots or rejects, so a near-full schedule can never leave a stuck pointer-down or an unmatched edge. The bot supplies the drag path implicitly via `frames` (the handler expands the linear interpolation; `frames` is DoS-capped to fit the input scheduler). All writes are fire-and-forget — advance a frame to apply.

**The command group** (all `frame_behavior: any`; reads have no side effect, writes are fire-and-forget):

| Command | Params | Result | Kind |
|---|---|---|---|
| `ui.tree` | `{ctx?}` | `{…meta, truncated, nodes:[{id,parent,role,id_string,text,label,child_count,visible,enabled,bounds}]}` | **READ** the last completed frame's tree (Y-up bounds, same space the writes take); `truncated` true **only** when the DFS-depth limit cut the walk (scratch is sized to the ctx element budget, so node count never truncates) |
| `ui.element` | `{id, ctx?}` | `{…meta, truncated, node}` | **READ** one node by developer string id; unknown/stale → `bad_params` (a miss under truncation says so) |
| `ui.contexts` | `{}` | `{contexts:[string]}` | **READ** the host-registered context names |
| `ui.click` | `{id\|{x,y}, hold?, ctx?}` | `{queued}` | resolve target → center → synthetic down@0 + up@`hold` (`hold` default 1 frame) |
| `ui.scroll` | `{id\|{x,y}, dx?, dy?, ctx?}` | `{queued}` | resolve target → center → synthetic move there + wheel(`dx`,`dy`) notches |
| `ui.drag` | `{from, to, frames?, ctx?}` | `{queued}` | resolve `from`/`to` → down@from + `frames` interpolated moves + up@to (handler expands the path; `frames` ≥ 1 and DoS-capped — a 0-frame drag emits no move so the inject up lands at `from`, not `to`, and is rejected `bad_params`) |

## 24.9 Observability (devapi `log.*` / `perf.*` / `entity.*` / `resource.*`)

A bot inspects engine state through the devapi **obs** command group — a thin L2 veneer that serializes the dev-only log ring, the `nt_metrics` perf collector, the debug overlay, and the entity/resource enumeration accessors. Every obs command is a **pure immediate read**: it serializes the live L1 state on the same `submit` call and returns synchronously — **nothing in this group ever defers** (no `frame.wait`-style continuation), so all are `frame_behavior: any` with no side effect except `perf.reset`. Untrusted bot input is range/type-checked and returns `bad_params`; only host-call invariants assert.

**The command group:**

| Command | Params | Result | Kind |
|---|---|---|---|
| `log.tail` | `{n?, level?}` | `{entries:[{level,domain,msg}]}` | **READ** newest-first ring entries, up to `n` (integer `[0, NT_LOG_RING_DEPTH]`, default full depth), optionally filtered to `level` ≥ `info\|warn\|error` |
| `perf.snapshot` | `{}` | `{fps,frame_ms,cpu_ms,gpu_ms\|null,draw_calls,user_counters:object}` | **READ** the live current-frame overlay view; `gpu_ms` is JSON `null` when the GPU timer is unsupported |
| `perf.stats` | `{channels?, budget_ms?}` | `{channels:object,user_channels:object,fps_low_1pct,fps_low_01pct,over_budget_pct,budget_ms}` | **READ** windowed `nt_metrics` aggregates (`samples`/`avg`/`min`/`max`/`median`/`p95`/`p99`/`p99_9`; null aggregates when `samples:0`) per requested-or-all fixed channels + user channels; `budget_ms` (finite, > 0, default 16.67) drives `over_budget_pct` and is echoed back |
| `perf.reset` | `{}` | `{reset:true}` | clear the metrics window (counts → 0) without tearing down state |
| `entity.list` | `{offset?, limit?, only_drawable?}` | `{total,entities:[{id,index,generation,alive,enabled,position,drawable}]}` | **READ** live entities with compact fields (no world matrix); fully paginated against the honest `total` (two heap-free passes — the whole live range is pageable); optional `only_drawable` filter |
| `resource.list` | `{offset?, limit?, pack_id?, include_assets?}` | `{total,packs:[{id,state,priority,asset_count,mounted}],assets?:[{resource_id,type,state,pack}],asset_total?,assets_truncated?}` | **READ** mounted packs (paginated with `total`); a flat `assets[]` only when `include_assets`. `pack_id` filters **both** packs and assets. `resource_id` is a `0x`-hex string (a 64-bit hash can't round-trip through a JSON double); the flat `assets[]` is DoS-capped, with `asset_total`/`assets_truncated` reporting the honest scope vs the emitted prefix |

`offset`/`limit` (and `n`, `pack_id`) are parsed **exactly**: a non-finite, fractional, or out-of-range number is `bad_params`, never silently truncated.

**OFF semantics — dev-only, compiled out.** The whole obs group is gated by `NT_DEVAPI_GROUP_OBS` (default **OFF**, opt-in). When off, `nt_devapi_obs.c` is not compiled, the commands are **absent** from the registry (a `log.tail`/`perf.stats`/… request returns `unknown_method`), and the discovery surface does not list them. As with all of devapi it also vanishes entirely when `NT_DEVAPI_ENABLED` is OFF.

**Build deps are hard, not silent.** A CMake `FATAL_ERROR` guard (mirroring the `ui` group's DEBUG_TOOLS guard) requires `NT_DEVAPI_GROUP_OBS` to be built with **both** `NT_LOG_RING_ENABLED` **and** `NT_METRICS_ENABLED` ON — those carry the real log-ring and metrics bodies the group reads. With either dep OFF the group would link no-op stubs (an always-empty `log.tail`, a zero `perf.stats`) — a vacuously-passing false green — so configure fails fast instead. `NT_UI_DEBUG_TOOLS=ON` defaults both deps on.

---

# 25. Engine/Game Boundary

This is one of the most important decisions.

## 25.1 Engine owns

- platform
- memory
- entities
- component storage pattern
- hierarchy storage
- resources
- packs
- runtime format loading
- GPU backend
- render primitives
- input system
- audio system

## 25.2 Game owns

- gameplay systems
- system order
- render passes
- render tags
- sort policy
- batching choice per pass
- level/scene logic
- capture ownership semantics
- high-level content organization

This boundary is strict.

---

# 26. Module Layout

A fixed module is a single directory with its header + TU(s).

A **swappable** module is a directory with a public interface header plus
per-platform impl subdirs (`native/`, `web/`, `stub/`); the executable picks one.

```text
engine/
    core/
    memory/
    log/                    # swappable
        nt_log.h
        default/            # real impl (note: "default", not "native")
        stub/
        CMakeLists.txt
    input/                  # swappable
        nt_input.h
        native/  web/  stub/
        CMakeLists.txt
    clipboard/              # swappable
        nt_clipboard.h
        native/  web/  stub/
        CMakeLists.txt
    fs/                     # swappable: nt_fs.h + native/ web/ stub/
    http/                   # swappable: nt_http.h + native/ web/ stub/
    window/                 # swappable: nt_window.h + native/ web/ stub/
    app/                    # swappable: nt_app.h + native/ web/ stub/
    graphics/               # swappable: nt_gfx.h + gl/ + stub/ (real impl dir is "gl")
    ui/
    font/
    debug_overlay/          # dev HUD (frame time / draw calls / element count)
    resources/
    packs/
    formats/
    audio/
    devapi/    (dev-only, NT_DEVAPI_ENABLED — see §24.5)

deps/
    cjson/     (vendored standalone lib, EXCLUDE_FROM_ALL; linked only by consumers)

builder/   (separate program)

game/      (game-side code)
```

## 26.1 Module composition (interface / impl / stub)

Every swappable module exposes a header-only `nt_X_interface` target, declared
via `nt_declare_interface()` in `cmake/nt_module.cmake`. It carries ONLY the
public header + the engine include root — no TU, no symbols.

Two or more concrete targets implement that interface with identical signatures:
the real `nt_X` and the `nt_X_stub` (the web/native variant of the real impl is
selected inside `nt_X`'s own `CMakeLists.txt`).

Consumer modules link ONLY `nt_X_interface` — never a concrete impl. The
**executable** selects exactly one impl per interface in its
`target_link_libraries`.

Omitting an impl is a LOUD unresolved-symbol link error, not a silent no-op.
Two gates enforce this:

- `scripts/check_no_real_impl_links.sh` — scans every `engine/**/CMakeLists.txt`
  and fails if any engine module links a real swappable impl (it must link the
  `_interface` instead). Re-poisoning the link graph would make stubs unreachable.
- `scripts/check_link_failure_loud.sh` — compiles a throwaway TU that calls
  `nt_log_write` but links NO `nt_log` impl, and asserts the linker reports an
  undefined reference. Proves omission fails loudly (exit 0 only when the
  expected unresolved-symbol error is observed).

Current swappable pairs: `nt_log`, `nt_input`, `nt_http`, `nt_gfx`,
`nt_window`, `nt_app`, `nt_fs`, `nt_clipboard`.

**Why link-time, not compile-time.** Selection happens at LINK time. This
replaced the older `NT_MODULE_X` `#define` + provider-fn-ptr + weak-symbol
approach. Link-time selection keeps each consumer compiled exactly once (one
artifact reused across executables), and makes the choice explicit and
gate-enforced instead of buried in per-TU macros.

## 26.2 Adding a swappable module

1. Create the interface header `nt_X.h` (public API only).
2. Declare the interface target: `nt_declare_interface(nt_X)` in the module's
   `CMakeLists.txt`.
3. Write `native/`, `web/`, `stub/` impls with identical signatures.
4. Add `nt_X` to the `SWAPPABLE` list in
   `scripts/check_no_real_impl_links.sh`.
5. Each executable picks exactly one impl in its `target_link_libraries`
   (the real `nt_X` or `nt_X_stub`).

## 26.3 Stub semantics and capability queries

A stub is a Null Object: the API is always present, the impl is inert.

A no-op / empty stub is safe ONLY when the caller already treats "nothing
happened" as a legitimate runtime state, not an error:

- `nt_log` — a dropped log is functionally invisible.
- `nt_input` — a frame with "no events" is already a normal, handled case.
- `nt_clipboard` get/paste — an empty clipboard is a real state the text field
  already tolerates (paste of "" is a no-op).

A no-op stub is NOT sufficient when either:

1. the capability is user-facing, so a silent no-op looks like a bug; or
2. an operation pairs a destructive LOCAL half with the backend — e.g. cut
   deletes the selection locally AND stores it remotely; a no-op store turns cut
   into silent DATA LOSS.

For those cases expose a runtime capability query `bool nt_X_available(void)`
(real → `true`, stub → `false`) and let the caller branch: skip the destructive
half, or gray out the affordance. `nt_clipboard_available()` is the engine's
example — the text field makes Ctrl+X a no-op when it returns `false`.

Availability is a link-time/runtime fact, so it MUST be a linked symbol
(function), NEVER a `#define`. A macro is compile-time and per-TU; it cannot
differ per-executable for a consumer that is compiled once.

This matches established practice: SDL `SDL_HasClipboardText`, GLFW
`glfwVulkanSupported`, Godot `DisplayServer::has_feature(FEATURE_CLIPBOARD)`.

---

# 27. Suggested Implementation Order

1. platform_web + core loop + fixed/update lifecycle
2. memory arenas / alloc policy
3. entity system + hierarchy
4. sparse component storage template
5. transform + hierarchy update
6. resource ids / asset meta / pack meta
7. NTPACK format: pack parsing + asset access
8. async loading (fetch bridge + resource_step)
9. shader asset + material asset parsing
10. texture asset handling
11. mesh asset/runtime handling
12. render backend basics (WebGL 2)
13. render item build/sort
14. mesh rendering
15. sprite renderer with CPU batching
16. input polling + capture
17. audio system (web backend)
18. builder binary + importers + pack generation
19. builder audio importer

---

# 28. Summary of Critical Locked Decisions

These decisions are **locked** unless a strong reason appears:

1. Web-first startup target
2. WebGL 2 as sole baseline (no WebGL 1)
3. Game-defined render loop
4. No system registry
5. Sparse unique component storages
6. Hierarchy lives in entity system
7. Builder is standalone native binary (C17 + vendored C++ behind extern "C")
8. Builder rules are code
9. Runtime formats are custom binary
10. Custom pack format (NTPACK) — flat binary, no ZIP
11. Manifest embedded in pack header
12. Material numeric params are `vec4[]`
13. No full duplicated MaterialRuntime object initially
14. Asset metadata is stable and resident
15. Render tags are game-defined
16. sort_key and batch_key are separate concepts
17. Depth only computed when sort policy needs it
18. Sprite batching belongs inside SpriteRenderer
19. Input is polling-based with pointer capture
20. Compile-time capacities, minimal runtime settings
21. Audio is engine module, not game-side
22. Audio public API is handle-based, zero platform types
23. audio_clip_create contract is always potentially async
24. audio_try_resume() exists on all platforms
25. Audio format in packs is OGG Vorbis
26. Audio internal structures are per-platform, hidden from game code
27. Voice pool with oldest-non-looping eviction
28. RenderItem stores world_matrix by value, not pointer
29. Async loading is non-blocking with PackState/AssetState machines
30. Eager asset activation with per-frame rate-limit

---

# 29. Open but Non-Critical Future Questions

These do not block implementation:

- exact binary layout of each runtime format header
- precise bit packing of sort keys
- future WebGPU backend details
- ~~exact sprite asset format~~ → resolved: `NT_ASSET_ATLAS` (§17.8) builder-side, runtime atlas consumer, `SpriteComponent`, and SpriteRenderer.
- sprite animation system
- ~~exact text rendering strategy and string pool design~~ → resolved: Slug-based GPU vector rendering (§17.8 NT_ASSET_FONT), font module API (§9.5)
- camera component/structure definition
- whether some renderer-specific caches are worth adding later
- whether pack hot-reload becomes needed
- ~~whether asset build database is needed immediately or later~~ → resolved: content-addressed builder cache (§23.10)
- 3D audio / positional sound
- sound groups / mix buses
- desktop audio backend library choice (miniaudio recommended)
- entity destruction notification mechanism details
- generation overflow monitoring strategy

These can be solved incrementally without breaking the core architecture.

---

# 30. Final Architecture Snapshot

```text
Game code
    ├─ defines gameplay system order
    ├─ defines render passes
    ├─ defines tags
    ├─ calls engine subsystems
    └─ uses engine primitives

Engine
    ├─ runs frame lifecycle
    ├─ stores entities/components/resources
    ├─ updates transforms
    ├─ loads runtime assets from NTPACK packs (async)
    ├─ provides render backend (WebGL 2)
    ├─ provides input + platform services
    └─ provides audio playback

Builder
    ├─ imports source assets
    ├─ validates compatibility
    ├─ converts to runtime formats
    ├─ builds NTPACK packs
    └─ embeds manifest in pack header
```

This gives: explicit control, minimal runtime complexity, strong builder/runtime split, web-first practicality, platform-agnostic game code, future room for WebGPU and desktop platforms without redesigning the core.
