# nt_ui Module

The minimal immediate-mode UI module built on vendored Clay: what it deliberately
is not, the Clay dependency contract, the render-time transform pipeline, the
interaction and arbitration model, the anim cache and state pool, stateful
widgets, scroll physics, virtualization, text input, popups and menus, and the
two known rendering limitations.

Related: [Scope](../core/scope.md), [Core Principles](../core/principles.md), [Rich Text](rich-text.md), [Radial Widgets](radial-widgets.md), [Render Items, Sorting, Batching](../render/items-sorting-batching.md)

## Not a full UI framework

`nt_ui` is a minimal immediate-mode layout + render bridge module. It is **not**
a full UI framework — no widget authoring tools, no styling pipeline beyond
inline calls, no asset hot-reload of UI definitions, no scene-graph
integration. Game owns the loop and render order; `nt_ui` provides
building blocks per the engine's "set of modules" principle.

## Clay as a public dependency

`NT_UI_TIMING_ENABLED` independently selects layout/build/walk measurement.
OFF keeps the getters but returns zero without clock reads or timing fields;
see [optional measurements](../debug/logging-errors-debugging.md#optional-measurements).

`nt_ui_rich` is an optional library in the UI family: link it alongside `nt_ui`
for rich text and markup. It depends on `nt_ui_interface`, leaving the executable
to select the UI implementation. It shares the UI private ABI and must be built with the
same engine sources. The base `nt_ui` library does not link it. Ordinary labels
and widgets need only `nt_ui`.

### CUSTOM command callbacks

`nt_ui_custom_data_t` carries a type, borrowed `data`, and `emit` callback.
`NONE` is an inert layout anchor. `GAME` preserves the context handler installed
by `nt_ui_set_custom_handler`, receiving that handler's user data; `emit` is unused.
`CALLBACK` requires a non-NULL `emit` and invokes it with the frame snapshot and
the command's `data`. Rich text creates one CALLBACK command per solved block;
the walker has no direct reference to the rich renderer.

The command and payload must outlive every consuming walk. Frame-scratch data
is invalidated by the scratch reset (`nt_arena_reset(&g_nt_mem_scratch)`); the engine never frees user data.
Repeated walks may invoke the callback again. A callback must not re-enter walk,
change the layout tree, or reset scratch. It preserves active clipping and owns
the GPU state it touches. Sprites and text record at the call, so call order is
draw order and the walker drains nothing around the callback. Every later sprite
and text command selects its own material, so a callback may leave another
sprite or text material selected. A callback that emits sprites or text calls
`nt_sprite_renderer_set_material` or `nt_text_renderer_set_material` first: the
selection it finds is whatever an earlier command left. The walk's draw count (`nt_ui_get_last_walk_draw_calls`)
counts the draws the walk added, so a walk whose first sprite or text draw merges
into the caller's preceding draw in the same pass adds nothing for it.
The supplied frame contains the composed world matrix, opacity, context and
layout-space Clay command.

Consumers must recompile after this public-struct change: the wrapper is 12 bytes
on wasm32 and 24 bytes on native 64-bit. Existing GAME/NONE aggregate initializers
zero the new trailing field. Replace direct `NT_UI_CUSTOM_TYPE_RICH_TEXT`
construction with CALLBACK plus the desired function; rich API callers need no
manual callback setup.

### Public Clay surface

`NT_UI_CLAY_DEBUG_VIEW` defaults to **OFF** in every preset, independently of
`NT_UI_DEBUG_TOOLS` (the Neotolis inspector). OFF removes Clay's built-in debug
renderer, helpers, strings, per-element debug data and arena allocations. It
preserves layout and duplicate-ID reporting. The vendor guard is private to the
Clay implementation TU; applications use the same public headers.

With OFF, `Clay_IsDebugModeEnabled()` returns false and
`Clay_SetDebugModeEnabled(false)` is valid. Passing true reports the disabled
feature through the existing error handler; Neotolis treats this as an assert.
Enable `-DNT_UI_CLAY_DEBUG_VIEW=ON` explicitly to use the view. `nt_ui_begin`
resets it to false as before; call `Clay_SetDebugModeEnabled(true)` after begin
and before `nt_ui_end` to inspect that frame. Layout finalization reserves the
sidebar width, keeping the panel inside the viewport and shrinking the content
area. The next frame uses the full width unless the game enables the view again.
Both view settings work with the engine inspector ON or OFF. Internal debug
child floats use relative z offsets, like other engine floating elements.

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

## Widget IDs

Never assign sibling widget IDs as `base_id + index`: Clay's anonymous child IDs
are additive (`seed + offset`), so consecutive seeds can collide with children of
adjacent widgets. Derive sub-IDs with `nt_ui_child_id` / `nt_ui_fmix_id` instead.
Clay's element ID hashmap retains IDs for one frame: `Clay_EndLayout` prunes
every ID not declared in that frame, and the next frame still reads the boxes of
the IDs it kept. The table holds `2 × max_elements` IDs, one per element for the
previous and current frame. An element with an explicit ID whose declaration
calls `Clay_Hovered`, `Clay_OnHover` or `Clay_GetScrollOffset` also keeps an
anonymous alias ID; nt_ui widgets never do. A frame past Clay's per-frame
capacity (`max_elements - 1` layout elements, or its configs and render commands
sized by `max_elements`) asserts. Per-ID widget state in `nt_ui_state` is not
pruned, so stateful widgets with ever-new IDs still need recycling.

A floating `parentId` must name an element declared earlier in the same frame
(NT patch 8). A kept ID or a later declaration does not qualify; the declaration
asserts through Clay's error callback.

## Floating zIndex is relative

Upstream Clay sorts every floating element globally by its own
`zIndex`. The vendored copy diverges (NT patch 4 in `deps/clay/clay.h`):
a floating declared inside another floating gets `parent_z + own_z`, so
`zIndex` reads as a stacking context, like CSS. A widget's floating part
therefore composes anywhere without knowing its global band, which is
what lets the slider thumb, the input caret and the scrollbar declare a
bare delta instead of reading the modal subsystem's depth counter.

A delta of 0 ties with the parent and, since the root sort is stable,
paints after it with a same-frame bbox. A NEGATIVE delta sorts ahead of
its parent and reads the parent's PREVIOUS-frame bbox; two parts use
one — the tooltip drop-shadow, which guards for the first frame, and the
inspector highlight, which attaches to a base-band element and so is
unaffected.

Bands accumulate with declaration nesting, NOT with attachment:
`attachTo = ROOT` still stacks where it was declared. Overlay bands are
declared as one `modal_zband_stride` each, so nested overlays accumulate
one stride per level, themselves included, when every enclosing floating
is an engine overlay. A widget's own floating parts declare delta 0 and
paint above their container's content because each is a later tree root
of the same band, and `layer` never reorders across tree roots. A floating
declared AFTER one of them in the same band paints over it. There is no way out
of an enclosing stacking context: a game floating that must stay under
all UI belongs at the root level.

The value Clay stores and reports for a floating is the accumulated
band, not the declared delta. Clay sorts its tree roots (the document and
every floating element) by that band, stably, so equal bands keep
declaration order. `nt_ui` bakes each element's stacking order — its tree
root's place in that sort, then its declaration index — and the walker and
the pointer arbiter both order on it; the `zIndex` field of individual
render commands is never read by the engine (Clay fills it only for
RECTANGLE, TEXT and the floating root's clip SCISSOR). Paint order is tree
root, then `layer` asc within each barrier-delimited run of one root, then
Clay's command order (a parent's border after its children); SCISSOR/CUSTOM
are hard barriers. An accumulated band that would saturate
`int16` raises a Clay error, which `nt_ui` asserts on rather than
silently merging two bands. That caps what a game floating may declare: its own
`zIndex` plus one stride per overlay level nested inside it must still
fit `int16`.

## Clay private symbols

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

One of those wrappers reads state that exists only because of NT patch 4:
`nt_ui_clay_priv_open_floating_z` returns the top of Clay's
`openFloatingZStack`, and `nt_ui_popup` uses it to rank overlays. Re-applying
patch 4 is therefore a precondition for overlay arbitration, not only for
painting.

With `NT_UI_DEBUG_TOOLS=ON` this expands by ~30 more Clay private
symbols for the verbatim Clay debug-view port (the inspector body
lives in the same TU). Either way, bumping Clay can require
coordinated nt_ui-side changes.

## Render-time pipeline

`nt_ui` composes a per-element column-major
`mat4` + opacity in a post-`Clay_EndLayout` build pass
(`nt_ui_internal_build_tree`). The transform is `nt_ui_transform_t`
with 9 floats (offset_xyz, rotation_xyz Euler, scale_xyz). Composition
follows standard scene-graph order: `world = accum_parent · L_child`,
where `L = T(O+C) · R(rot_xyz) · S · T(-C)` is built around the element's
layout-space bbox center via cglm. The walker reads the composed mat4
from `tree_baked[layout_idx]` (hot path, no hashmap lookup) and ships
`world_mat4[16]` to every emit_*.

`emit_region` and `emit_slice9` share one local space: Y-up, pivot-relative,
`flip_bits` negate positions (which reverses winding). The walker's mat4
carries the Y inversion and the bbox center; a nine-patch gets a centered
pivot so it fills its bbox unless `NT_UI_IMAGE_ORIGIN_OVERRIDE` moves it, and
a `NT_UI_IMAGE_SLICE9_OVERRIDE` of all zeros turns a baked nine-patch back
into a plain quad. `emit_geometry` takes caller-space corners, no pivot or
flip. Slice9 corner bands are the one place `pixels_per_unit` reaches the
layout: borders are source px ÷ ppu, so a denser HD atlas renders the same
corner; a stretched region divides ppu straight back out.

Two coord-space modes are gated by `nt_ui_create_desc_t.use_raycast_input`:

- **2D ctx** (default): screen Y-flip (Clay layout Y-down → GL Y-up) is
  folded into `world_mat4` once per dispatch (negate m[1]/m[5]/m[9],
  add `vy+vh` to m[13]). Hit-test reverses through the top-left 2×2 +
  col3 of the same mat4 (closed-form inverse-affine).
- **3D ctx** (`use_raycast_input=true`): walker leaves `world_mat4`
  unflipped; the game owns the screen mapping via `nt_ui_frame_desc_t.view_proj`.
  Hit-test raycasts pointer → NDC (Clay Y-down to NDC Y-up) → world ray
  via `inv_view_proj` → ray-plane at widget Z=0 → `mat4_inv_trs(baked.m)`
  back to widget-local → bbox check. Optional `element_depth_bias_ndc`
  is render-only: positive values move deeper hierarchy levels slightly
  closer in projected depth to avoid z-fighting; panels whose backgrounds
  write depth need it above 0, or their children are coplanar with the
  background. Text drawn without depth writes occludes nothing: a farther
  panel drawn later covers a label wherever the label's own background does
  not. Hit-test stays on the unbiased widget plane. With depth-writing
  panels the depth test, not the stacking order, decides what is drawn on
  top: the bias grows with `hierarchy_depth`, so document content nested
  deeper than an overlay can cover it while the pointer goes to the
  overlay. This is a known divergence from the stacking order, tracked in
  #875. Debug inspector render commands are excluded
  from the regular 3D walk and drawn by `nt_ui_debug_inspector_walk` as
  a separate final screen-space pass. The game binds a 2D UI projection
  first; `nt_ui_make_screen_view_proj(w, h, ...)` provides the standard
  Y-up orthographic matrix used by 2D demos. The post-walk highlight
  overlay (`nt_ui_inspector_overlay_draw`) differs: in 3D ctx it emits the
  hovered element's bbox under that element's `hit_baked` world matrix, so
  the game binds the perspective VP for it while the sidebar tree stays
  orthographic.

Both modes use the same `tree_baked[layout_idx]` + per-id mirror
`hit_baked[slot]` (Clay keeps the previous frame's ids;
`hit_generation[slot]` rejects a slot reused by a new id). Opacity is a separate
`float` accumulator on the same struct.

### Custom-attr base material

The base sprite material may declare an `attr_map` if it also declares attr
defaults ([Attr defaults](../render/material.md#attr-defaults)). Every emit
without its own block bakes them: RECTANGLE, BORDER, IMAGE, rich-text inline
images and the debug overlays. An `nt_ui_image_custom` block replaces them for
its own emit. Custom widgets on the base handle then batch with plain panels and
icons instead of flushing at every boundary.

A custom-attr base moves all base UI to the extended vertex stride
([Sprite custom-attr block](../render/items-sorting-batching.md#sprite-custom-attr-block)),
so its geometry takes more vertex frame storage; size the host's budget from the
measured peak.

## Interaction model

A frame starts with `nt_ui_begin(ctx, &frame)`: `nt_ui_frame_desc_t` carries everything
the frame's input decisions read — layout size, dt, the raw device pointers, the device
viewport (zero = identity), and for a 3D ctx the `view_proj` and per-pointer occlusion.
Begin converts the pointers to layout space, resolves the front-most record per pointer
and decides the scroll gestures; no setter may change that input later in the frame.
Game ids interact via `nt_ui_query_interaction` (a pure read, multiple calls per frame OK) and
`nt_ui_step(ctx, id, desc)` (mutating, exactly one call per id per frame; `desc` carries
the hit padding and the drag axes the widget consumes itself, NULL for a plain tap
target). The step commits capture, so a later
query that frame can read different edges: read one-shot edges from the step's
result, and `double_clicked` only from there. Capture is per-pointer:
a press records `active_id`; release clears. Other widgets are
`exclusive_gated` while one holds capture. A frame carries a pointer's release or a
whole tap, never a release with a newer press: `nt_input` publishes that press one frame
later ([Input](../io/input.md)), and `nt_ui_begin` asserts the one shape it can see in a
pointer built by hand (pressed, released and still down). A press always starts a new
gesture: whatever the pointer still held is dropped. **Orphan cleanup** at
`nt_ui_begin` drops `active_id` if the widget didn't call step last
frame — covers scene switches, conditional disable, and widget hide. It also drops
`active_id` when a pointer has no release edge and its button is up or newly pressed:
frames without `nt_ui_begin` swallow edges, and the pointer must not stay bound.
Result: 1-frame IM-lag is intrinsic (current frame reads previous
frame's bbox); no stuck input on widget disappearance.

## Front-most arbitration

When interactive widgets overlap a pointer, only
the front-most may react; the rest are gated off. The hot widget per pointer
is resolved once per frame, in `nt_ui_begin`, from the PREVIOUS frame's
interactive registry; only widgets that called `nt_ui_step` (or registered as a
blocker or scroll surface) last frame are candidates. 3D ctx (`use_raycast_input`):
nearest world ray-distance within a game-fed occlusion cutoff
(`nt_ui_frame_desc_t.occlusion`) so UI can't be clicked through
world geometry — the game owns the raycast, the engine only takes the cutoff distance;
`nt_ui_interaction_t.distance` reports the hit distance. Hits within 1e-5 of the nearest
distance (relative, floored at one unit) are coplanar and compete like 2D. 2D ctx: the
ranking is the stacking order (see "Floating zIndex is relative"): the later tree root wins
(band, then declaration), and inside one root the later-declared element wins, so a child is
over its parent and a later sibling over an earlier one. The order `nt_ui_step` is called in
does not matter. The draw `layer` is not part of it: it reorders painting inside one root for
batching and never changes which element gets the pointer. A child overlaps its parent: a
parent on a higher layer than its interactive child, or the parent's border (Clay emits it
after the children), paints over the child while the child gets the pointer, so keep an
interactive child's layer at or above its parent's. Siblings of one root overlap only
through a transform, overflow without a clip, or hit padding; put such interactive parts on
one layer or make the upper one floating. `nt_ui_wants_pointer` is true
while a pointer of the current frame holds a capture or a pending scroll gesture, or its front-most record is declared again this frame (a
record that vanished this frame is not UI under the pointer). A free pointer drives a
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
same record `nt_ui_step` makes, minus any interaction). It blocks input to widgets
behind it but never captures or clicks; a query on a blocked id reports hover while
it is front-most. The stock disabled widgets (button/checkbox/slider) call it so a
disabled overlay or modal can't leak clicks through.

## Anim cache

`nt_ui_anim_*` provides per-id eased state for widget
visuals. Open-addressing direct-mapped table (`NT_UI_ANIM_SLOTS`,
default 512, power-of-2); 4-probe chain; full-chain collision evicts the STALEST
slot in the probe window — the one with the oldest `last_touch`
generation tick (snap-reseed, easing lost for one id). Evicting by
staleness rather than blindly the tail avoids bleeding a slot that
was already touched this frame between widgets sharing the window.
The `anim_collision_count` monotonic counter surfaces this
degradation; game polls the delta to size `NT_UI_ANIM_SLOTS`.

## State pool

`nt_ui_state(ctx, id, size, tag)` is a generic per-widget-id
retained-state pool — the durable counterpart of the anim cache. It returns a
get-or-create cell (zeroed on create; zero is a valid initial state). The 4-char
`tag` (built with `NT_UI_STATE_TAG`) identifies the owning widget so two widgets
that hash to the same id+size trap on re-acquire instead of aliasing silently,
`nt_ui_state_find` returns NULL if absent, and `nt_ui_state_clear` /
`nt_ui_state_clear_all` drop one or all cells (e.g. a screen transition). The
pool is carved from the caller-provided context arena; the engine allocates no
heap. Its size comes from `nt_ui_create_desc_t.state_slots` (default
`NT_UI_STATE_SLOTS` = 256, about 19 KB) and uses the configured probe window.
It is direct-mapped + linear-probe like the anim cache, but with **no LRU
eviction**: a cell dies only via clear or context destroy. This no-eviction property is the contract that
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

## Stateful widgets

`nt_ui_checkbox` / `nt_ui_radio` / `nt_ui_toggle`
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
hit-test and arbitration. `nt_ui_checkbox_tri` adds a tristate (OFF/ON/MIXED) over the
same core, additive to the bool checkbox: MIXED renders a dedicated `mixed` dash row but is a
DISPLAY-only state the game sets (e.g. aggregated from children) — a click NEVER produces MIXED
(it resolves any non-ON value to ON, else toggles ON↔OFF), so Model D holds.
`nt_ui_slider` / `nt_ui_progress` extend the same
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
overhang past the track even at zero style pad. An `orientation` style field selects the drag
AXIS (horizontal default / vertical): the SAME `nt_ui_slider_float` / `_int` branch on it, with
`fill_direction` the anchor WITHIN the axis (vertical defaults to BOTTOM_UP — value 0 at the
bottom; horizontal to LTR). A `fill_direction` whose axis disagrees with `orientation` is a
developer assert; the caller's style is never mutated.
Slider and progress share one fill-emit
helper (STRETCH slice9 stretch vs CROP scissor-reveal × four directions).

## Custom scroll physics

`nt_ui` scroll containers bypass Clay's built-in scroll
integration for physics, but `Clay_UpdateScrollContainers` is STILL called each frame
in neutral mode (drag disabled, zero delta) purely for its slot-GC side effect — every
CLIP element reclaims its scroll-pool slot through it, so dropping the call leaks slots
until the pool overflows (type=7 crash). The engine integrates the scroll offset itself
and feeds Clay a ready `clip.childOffset` each frame — it never reads
`Clay_GetScrollOffset`, only `Clay_GetScrollContainerData` for the content/container
clamp dims. Everything is in
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
Per-container scroll state (pos/vel/target/flags)
rides the state pool keyed by the scroll id; no heap. With `rubber_band_c == 0` the
edge is hard for drag and fling (no reversal dead zone); a rubber band needs
`bounce_speed > 0` to return (asserted).

**Gesture arbitration.** One arbiter per frame decides every pointer's scroll gesture,
in `nt_ui_begin` right after the hot resolve, so every step and query of the frame only
reads its result. It reads only last frame's layout: the
interactive registry and each scroll's eligible axes (clipped AND overflowing, from the
clip data Clay keeps for a scroll declared last frame). A scroll container registers its own bbox as a
*surface* record, so an empty-area press resolves through the normal front-most
arbitration (z-order, blockers, modals, transforms, 3D occlusion); a scroll whose content
fits (within half a unit) is see-through. The scroll chain of an element is the scrolls
that clip it, innermost first: it is read from last frame's clip tree, the one the hit
test walks, so no declaration order or bookkeeping is involved. A floating element takes
no clip unless it asks for one (`clipTo`), so everything inside a popup (dropdown, menu,
modal, tooltip) and the menu occluder has an empty chain, and a drag inside a popup never
reaches a scroll behind it (a scroll declared inside the popup, a dropdown list, is on the
chain of its own content); the same holds for a game's own unclipped floating element
declared inside a scroll: neither a drag nor the wheel over it reaches that scroll.
Scrollbars clip to their scroll, so they belong to its chain (the wheel over a bar
scrolls it). A thumb is not driven on an axis the style stopped scrolling (its owner is dropped the next
frame). A bar has a hit record only while it shows a thumb that can move: thumb art,
a non-zero thickness, an overflowing axis, room for the thumb to travel, and not faded out; otherwise its strip belongs to the content under
it. `nt_ui_scroll_begin` / `_end` must balance: `nt_ui_end` asserts that no element is left
open. A scroll's own id is its surface and must not be stepped as a widget (checked under
`NT_UI_CHECKS`).

A press starts a gesture record on the chain of the front-most record under it; a press on a bar that can move makes the pointer its scroll's owner by the thumb
at once (a thumb has no slop; one owner per scroll, by content or by a thumb), and a chain with no eligible axis decides at once (widgets
in fitting panels are never pending). A chain link counts
only if that scroll was declared and its layout solved last frame, so a vanished scroll
takes nothing. While pending, the pressed widget sees `gesture_pending`. Each frame the
pointer's displacement from the press is compared per axis with the drag slop
(`NT_UI_DRAG_SLOP_DP`, 8 dp, Android touchSlop): candidates are the
pressed widget's own `drag_axes` and the chain's eligible axes, inner first. The
strongest crossing (|d| / slop) wins, ties going to the widget, then the inner scroll;
nothing crossed keeps the gesture pending. A scroll that wins takes the capture — the
widget sees a foreign owner and goes silent (no release, no click), keeping any value it wrote (Model D:
draggables hold value changes while pending). The excess past
the slop is routed (no jump, no lost distance).
A crossing on the release frame still claims (fling, no click), and the owner keeps the
capture through its release frame. A release with nothing crossed stays pending through
its release frame (`nt_ui_end` ends it, so skipped frames never continue it): the widget gets its release (click when over it) with `gesture_pending`,
which marks a tap; a pending draggable applies a tap only when `clicked`. The newest pointer to claim a scroll or press its bar takes
it from older pointers (Android). A press on a flinging, undragged scroll of the chain
stops the fling and is swallowed (tap-to-stop). Wheel goes per pointer and axis to the
first scroll of the hovered chain that can move on that axis, consumed the same frame:
a vertical wheel over a horizontal track reaches its vertical parent. Engine-internal
inspector panes are chain roots; the sidebar's pointer only reaches panes, and a game
drag that enters the sidebar ends. Wheel input is normalized to a NOTCH unit at the
platform edge — `1.0 == one physical detent` (GLFW pass-through; web `deltaMode` divided
to notches), so wheel strength is platform-independent and the consumer scales by
`wheel_step_px`. Scroll sizes (`wheel_step_px`, bar/hit dimensions) are in the coordinate
space passed to `nt_ui_begin`; gesture tolerances (slop, double-click radius, tap-to-stop
speed) are in dp, converted per axis through `nt_ui_viewport_t.px_per_dp` (the window
DPR; the identity viewport uses 1). The
scrollbar (2-piece slice9 track + thumb, both axes) emits as floating children of
the container with
ALWAYS / AUTO / AUTO_HIDE visibility (AUTO_HIDE shows on scroll activity and fades
out `bar_hide_delay` seconds after the last motion, via one `nt_ui_anim` value_t).
A thumb press grabs at the press offset inside the thumb (no jump-to-center); a press
on the track off the thumb centers the thumb under the pointer: a held press at once, a
tap within one frame smoothly (`nt_ui_scroll_to`). A
thin bar gets a wider touch target from the style's `bar_hit_pad` (default 12, a 24-wide target): extra hit width from the visual bar toward the content only, since the container clips the outer side; the bar floats over the content, so that zone takes presses from the content under it; 0 leaves the visual bar alone. The
"Scissor limitation" note below still holds (AABB clip of a rotated scroll
container is unchanged).

## Virtual list

`nt_ui_vlist_begin` / `nt_ui_vlist_end` is a code-first virtualized-list
clipper (the immediate-mode analogue of Dear ImGui's `ImGuiListClipper`) over ONE engine-owned
scroll/clip. From the scroll position + viewport + a fixed `item_extent` it derives an inclusive
`{first,last}` visible window (returned to the game), emits a LEADING spacer, lets the game loop
`for (i = first; i <= last; ++i)` emitting only the visible rows, then emits a TRAILING spacer so
the content still measures `count × extent` and the existing scrollbar geometry stays correct — a
10k-row list costs ~the visible count, not the row count. Per-row ids RECYCLE over a frame-stable
ring (`id_ring`, slot = `index % id_ring`), so the distinct ids per list are bounded by the ring,
never the row count, and so are the non-evicting `nt_ui_state` cells of stateful row widgets; the
visible window must stay below the ring; exceeding it asserts before two visible rows can alias a slot.
Because ids follow the screen SLOT, the game dispatches per-row ACTIONS by the ABSOLUTE index and
keeps per-row PERSISTENT state game-owned (keyed by absolute index), never hung off the recycled
id — transient UI state (hover/press) follows the slot, standard IM virtualization. Both axes (Y
default, X). The general `nt_ui_child_id(parent_id, "label")` helper derives a per-widget id from a
parent scope + a string label (fmix-folded, never 0), so game code derives child ids without
inventing numeric salts.

`style.gap` must be finite and in `[0, UINT16_MAX]`; invalid values assert
before the scroll opens. Its fractional part is discarded once, and the same
integer pixel gap is used for Clay child spacing, row stride and spacers.

## Text input

`nt_ui_input_text` is a single-line field over a game-owned
`char*` buffer (Model D — the engine never reallocs or owns the string; it stores
only caret/selection/scroll/blink/focus in the state pool). Editing is UTF-8
codepoint-aware: insert/backspace/delete/arrows never split a multi-byte sequence
and clamp to `buffer_size-1` / `max_length-1` bytes. Typed text arrives via the
`nt_input_pop_char` UTF-32 characters (frame-local, read each frame while focused);
physical keys drive navigation/edit. T2 adds selection (Shift+arrows, mouse drag,
double-click word-select, Ctrl+A; Cmd works as Ctrl on macOS), copy/cut/paste via the swappable `nt_clipboard`
façade (paste decodes untrusted bytes per-codepoint, runs the allow-predicate,
clamps, and drops a non-fitting multibyte codepoint whole), a render-only password
mask, stock allow-predicates (numeric/email/url), a per-field `placeholder` string
shown dimmed while empty+unfocused, and a minimal focus arbiter (Esc unfocus, Tab
advance/wrap). A press focuses the field, except an unfocused field whose press waits
for a scroll (`gesture_pending`): it focuses on the tap's release, so a list drag never
opens the keyboard. A focused field drags its selection along x (`drag_axes`). Caret, selection highlight, and the text all float as clipped
children carrying a `HAS_TRANSFORM` offset of `pad_x - scroll_x` (set as the Clay
element `userData`, which the walker reads for layer+transform), so the line
scrolls to keep the caret visible while clipping to the field box. Cut is a no-op
when `nt_clipboard_available()` is false (no silent delete-without-copy).

The allow-predicate must not call `nt_clipboard_*`. Paste replaces the selection,
then inserts the clipboard text codepoint by codepoint: each passes the
allow-predicate, and the paste stops at the first codepoint that does not fit
the buffer or `max_length` (both count the NUL byte). A codepoint is never split.

## Gestures

`nt_ui_step` also reports the gestures of a press; `nt_ui_button_begin`
returns the same result. Gesture state follows the press, not the widget, and uses no
state-pool cell:

- **Hold** lives in the capturing pointer's capture and counts `dt` since the press:
  `hold_secs` and `hold_prev_secs`. Motion inside the hitbox keeps the hold. When that
  pointer leaves the hitbox, the hold reads 0 until a new press. A widget that skips its
  step loses the capture at the next `nt_ui_begin`, so its hold cannot resume without a
  new press.
- **Long press** is a threshold over the hold. `nt_ui_hold_crossed` fires once as the
  hold crosses `secs`, which must be positive and stay constant for the whole hold.
  `nt_ui_hold_progress` is the linear 0..1 ramp for hold-to-confirm feedback.
- **Double-click** pairs a stepped press with the previous stepped press through one
  record per context: same widget, within the window and the move radius (dp)
  (`nt_ui_set_gesture_constants`). A stepped press on another widget replaces the
  record, even from another finger; a completed pair empties it, so a third press starts a new pair.
  The move radius gates double-click only.

Gesture time is UI time. Every `nt_ui_begin` advances it by `dt`, whether or not the
widget stepped, so frames with a disabled widget count toward the window and
`dt==0` pauses both gestures. A frame without `nt_ui_begin` adds no gesture time: the
game passes the elapsed time in the next `dt` if it must count. Edges lost in such a
frame end the capture, see "Interaction model".

## Popup-core

A single floating-overlay primitive (`nt_ui_popup_begin/end` +
a one-bool `nt_ui_popup_visible` wrapper) underpins every transient overlay —
modal, dropdown, tooltip, and context-menu. It owns: a Clay floating panel
attached to the ROOT (anchor-derived offset, no trigger-id dependency, so a
missing trigger element can't trip Clay's parent-not-found); trigger-anchored
placement with per-side edge-flip (BELOW/ABOVE/RIGHT/LEFT, CENTER for modal)
read from the panel's previous-frame bbox; a `value_t` open/close tween; a shared
modal-depth z-band declared as ONE `modal_zband_stride` above the enclosing
floating (Clay accumulates the nesting, see "Floating zIndex is relative" above;
NT_ASSERT before the push so a runaway nesting fails early); and a present-only, transparent light-dismiss
catcher at `panel_z-1` (outside-click raises a close signal). Esc and the
outside-click scan run on ONE CATCHER-BEARING popup per frame: the highest
effective band, ties to the last declared. That is the panels' tree-root
order, so the popup that paints on top is the one whose catcher wins the
click and performs the dismissal. The agreement is a 2D-context property: a 3D
ctx arbitrates the pointer by ray distance and does not rank overlays by
band at all. A catcher-less
overlay (menu, tooltip) never claims that slot and runs its own dismiss; the
text field's Esc-unfocus is independent of both. A fully-closed
popup declares NO catcher, so the base UI stays clickable; a hover-driven
overlay (tooltip) can clear the catcher flag entirely. Dismiss is always a
SIGNAL the game acts on (Model D) — popup-core never owns the open bool. The
context-menu stacks one popup-core fly-out per submenu level; the only novel
algorithm is the mouse-aim triangle that keeps a submenu open while the cursor
travels diagonally toward it (never collapse on raw hover-loss).

## Immediate menu + combo

The context-menu and dropdown are immediate-mode:
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
long combo list). There is no data-array form — immediate-only.

A custom-content row (`item_begin..item_end`, `selectable_begin..selectable_end`,
`preview_begin..preview_end`, and a tab's `nt_ui_tab_begin..nt_ui_tab_end`) steps
in its `_end`, after its children, but the stacking order is declaration, so an
interactive child declared inside the row wins the press over its own area; the
rest of the row activates it. Over the child the child is the front-most record,
so a query on the row reports no hover there: a combo or tab row drops its hover
look. A menu row instead counts the pointer inside its box while the front-most
record is in the row's tree root and the pointer is not captured by another
record, so it keeps highlight, hover focus and the leaf collapse over its own
control. A rich-text block
with links is one record over its whole box, so inside a row it takes the press
over all of its text, not only over the links. A menu row with `non_activatable`
has no action of its own (for a row that only hosts a control): it never steps,
only blocks the pointer, so it hovers but never captures or activates.

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
duplicate sibling key aliases the same row state. `NT_UI_CHECKS` enables the
asserting duplicate-key scans: complete within the menu's bounded per-level cap;
each combo row is compared with the first `NT_UI_COMBO_DUP_KEY_WINDOW` row IDs,
so duplicates exclusively outside that window are not detected. OFF removes
these scans and the combo's duplicate-check storage. Ordinary pointer, index and
capacity assertions remain independent of this flag. Configuration is explicit
and independent of the inspector and assert mode; see [build options](../../build.md#build-options).
Keyboard nav runs in `menu_end` against THIS frame's per-level record (built as
rows declare, complete by `menu_end`); a focus or open-chain change re-declares
the tree and becomes navigable NEXT frame — a 1-frame latency in the EFFECT,
not in the record lookup.

## Atlas region identity

`nt_atlas_region_ref_t { uint64_t name_hash; nt_resource_t atlas;
uint32_t region; }` is the canonical "sprite-in-atlas" handle (atlas.id==0
is the unset handle; consumers assign their own meaning). The widget APIs
that take atlas art — `nt_ui_image`, `nt_ui_panel_begin`, and the
button/checkbox style structs — take this single ref rather than separate
`(atlas, region_index)` arguments.

## Scissor limitation

GL scissor is axis-aligned in framebuffer
space. A rotated scroll container is clipped by the AABB of its
rotated corners, so content can poke past the visual corners.
Stencil-mask fix is out of scope for v1.

A clip with zero extent along an axis it clips (for example a collapsed
scroll container) yields a zero-area scissor, so its subtree and any float
clipped to it with `clipTo = ATTACHED_PARENT` draw nothing. Under rotation the
AABB rule above still applies. A float finds its clip by comparing boxes, so
under a transformed clip, or nested clips with the same box, it can get another
clip's scissor and stay visible. A non-finite clip box is a layout bug and
asserts.

## Text under non-uniform scale

Font atlas em-size is picked from
the X-column magnitude of the composed affine (`sqrt(a²+c²)`). Under
uniform scale or pure rotation, glyph rasterisation stays crisp;
under `sx ≠ sy` the quad stretches but the rasteriser samples one
axis, blurring the other.
