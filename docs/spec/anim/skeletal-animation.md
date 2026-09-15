# Skeletal Animation

**Status:** architecture specification v0.2 (2026-09-15), implementation in progress under epic #472; the core pose ABI, FK, binding math and rig identity are implemented (#473). Function names of unimplemented parts are provisional; responsibilities, coordinate spaces, ownership, memory and behavior are normative. Changes to this chapter land together with the code that implements them.

Related: [Principles](../core/principles.md), [API contracts](../core/api-contracts.md), [Render architecture](../render/architecture.md), [Items, sorting, batching](../render/items-sorting-batching.md), [Material](../render/material.md), [Resource](../assets/resource.md), [Builder](../builder/builder.md).

## Requirements

Requirements the system satisfies (from the developer's previous shipped game; the design is new, the needs are not):

- R1 live crossfade: both clips keep advancing during the blend (run→jump, idle→run).
- R2 parametric blend of N cycles (walk↔run by speed).
- R3 partial-body composition through per-joint weights that may exceed 1 (legs 0.25 / arms 3).
- R4 a per-clip object transform signal blended with the pose and applied by the game.
- R5 sockets (bone world transforms) read after the final pose.
- R6 adding clips at runtime without rebuilding the character.
- R7 baked GPU playback for crowds, produced by one runtime call, and an explicit switch to CPU playback.
- R8 interrupting a transition with fixed memory and no visible pop.

Rejected in the 2026-09-15 design review, do not re-propose: CPU/baked material pairs or program kinds with kind↔program validation; `skeleton_comp`/`animation_player_comp`/`skin_comp.source_entity`; ten-channel SoA pose as the initial layout; ordered layer stack with `evaluate_layer` and frozen-snapshot transitions as the default; per-joint weights restricted to `[0,1]`; automatic weakest-track eviction; builder-baked page assets; a separate skinned-mesh metadata asset or skin fields inside MESH; mesh lists in NSKN; runtime mesh↔binding index checks (the builder guarantees them); per-bone bound envelopes; duplicated frames at texture-row breaks; an optional "retain CPU matrices" bank knob; a three-tier normal fallback ladder; blending model-space matrices per bone; animation catalogs (resources are found by name hash); `nt_pose_get/set_trs` accessors (the array is the accessor).

## 1. Decision

Own C17 implementation. The system is independent data plus explicit operations on that data. The game calls operations in the order it chooses, selects CPU or baked playback, and owns time, loading, physics, pass order and sorting. Numerical operations work without entities, renderer or resource manager.

Three asset types: **NSKL** Skeleton, **NANM** Clip, **NSKN** SkinBinding; MESH gains `joints`/`weights` streams. Clips are addressed by resource name hash through the existing resource API, from any pack mounted at any time; no catalog. Baked playback is produced **at runtime** from the same three assets by one explicit bake call into a game-owned bank; no baked asset ships in packs.

Both paths end in GPU skinning of the same mesh through **one vertex program** that always reads two frames and interpolates:

- **CPU:** clips → sampled track poses → mix/override/additive → procedural edits/IK → FK → skinning palette → `anim_gpu` staging → GPU (frame1 = frame0, alpha 0).
- **Baked:** bank (matrices per frame, baked once) → clip time → two frame origins + alpha → GPU.

`skinned_mesh_renderer` draws meshes with joint indices and weights from prepared deformation data. It never discovers the animation source.

## 2. Principles and boundaries

1. **Data apart from behavior.** Skeleton, clip, pose, weights and binding are data. Sample, mix, override, additive, FK, palette build, bake and upload are separate functions. No operation also advances time, loads, allocates or draws.
2. **One authority for the final pose.** The game orders writes to local/model buffers; nothing resamples after IK or physics.
3. **Predictable memory.** Heap only at init, character/bank creation, asset activation and explicit reconfiguration. Sampling, advance, composition, FK, IK, retargeting and frame preparation allocate nothing. Per-frame working memory (sampled track poses, capture scratch) comes from `nt_mem_scratch` or a buffer the game sized at init; kernels receive it as pointer + capacity and never allocate. Bank bake is a heavy explicit call at load time, not a frame operation.
4. **Capacity is configured, overflow asserts.** Track capacity, workspace size, bank size, palette staging, texture budgets are set at creation. Exceeding them is `NT_ASSERT`, like component storage: no growth, no eviction policy, no fallback. Recoverable outcomes exist at the resource boundary (readiness, pack validation, activator rejection) and at the existing gfx boundary (texture creation and context restoration may fail per [API contracts](../core/api-contracts.md); the game uses the existing readiness queries and retries restoration explicitly; no new readiness mirror).
5. **Validate at boundaries.** A loader validates its own payload before publishing views; an NSKL/NSKN/NANM activator that rejects its payload returns 0 and the asset is FAILED like a mesh or texture. Cross-resource compatibility (`rig_compat_id`) is asserted at explicit binding; what the builder already guarantees (vertex indices within the palette, normalized weights) is not re-checked at runtime. Numerical functions are `void` with `NT_ASSERT` preconditions — per-call checks (counts, rig id, capacity) in every build; per-element checks (unit quaternions, finite values, mask range) are compiled under `NT_ANIM_CHECKS` (auto: on in Debug, off in Release; overridable from CMake, so a release build can keep them). They are plain `NT_ASSERT`s, so `NT_ASSERT_MODE=OFF` removes them too. Genuine alternative outcomes (degenerate IK chain) return a defined result. A game that must survive mismatched content compares the ids it already holds before binding.
6. **No extension framework.** No graph, layer stack, controller, solver registry, scheduler, palette cache or per-joint callback.
7. **Modularity at link time.** No-animation builds link no animation code. Do not rely on LTO.
8. **Contracts are invariant, layouts are revisable.** Ownership, spaces, compatibility, lifetimes and draw compatibility are invariant. Pose ABI, shader specialization, bank texture dimensions and attribute locations are implementation details owned by one header each and changed by measurement.

## 3. Core data

### 3.1 Skeleton (`NSKL`)

Immutable resource: joint count, parent indices, local rest pose, stable joint identifiers, `rig_compat_id` (`nt_hash64_t`). **Rig identity schema:** `rig_compat_id = hash64` (existing `nt_hash`, seed 0) over one padding-free little-endian byte sequence. The stable joint id is `uint32_t` = `nt_hash32_str(node name)`, so renaming a bone is a different rig; the unit/axis convention id is `uint8_t`, `NT_ANIM_RIG_CONVENTION_GLTF = 1` (glTF: metres, Y-up, right-handed); the schema version is `NT_ANIM_RIG_SCHEMA_VERSION = 1` and a new value is a new rig identity. The sequence is `8 + 46·J` bytes:

```
"NRIG"  u8 schema  u8 convention  u16 joint_count                        8 B
per joint, index order:  u32 joint_id  u16 parent  f32 t[3] q[4] s[3]   46 B
```

Floats are exact canonical binary32, canonicalized in this order: quaternion sign first (largest |component| positive, the first maximum in x, y, z, w order winning ties), then `-0 → +0` on every float. Inverse binds, mesh data, display names and clip codecs are excluded.

Published test vector — joint 0 id `0x11111111`, no parent, t (1, 2, 3), q (0, 0, 0, 1), s (1, 1, 1); joint 1 id `0x22222222`, parent 0, t (0, −0, 0.5), q (0, 0, −0.70710678, −0.70710678), s (1, 1, 1):

```
4E 52 49 47 01 01 02 00
11 11 11 11 FF FF  00 00 80 3F 00 00 00 40 00 00 40 40  00 00 00 00 00 00 00 00 00 00 00 00 00 00 80 3F  00 00 80 3F 00 00 80 3F 00 00 80 3F
22 22 22 22 00 00  00 00 00 00 00 00 00 00 00 00 00 3F  00 00 00 00 00 00 00 00 F3 04 35 3F F3 04 35 3F  00 00 80 3F 00 00 80 3F 00 00 80 3F
rig_compat_id = 0x03E59E1475239034
```

`nt_anim_rig_compat_id(skel, scratch, size)` in `engine/anim` is the single implementation of this schema: the builder (#475) hashes the exported rig with it, and procedural rigs fill their own field with it. Clips and bindings are exported against the authoritative rig export; near-equal rigs from independent exports are different rigs by design. Joint indices `uint16_t`, `UINT16_MAX` = no parent. Builder orders joints in preorder; each subtree is a contiguous range; multiple roots allowed; helper nodes affecting transforms are retained. `subtree_end[j]` (`uint16_t`) closes that range — the subtree of `j` is `[j, subtree_end[j])` — and is part of the skeleton: the builder writes it into NSKL and the activator validates preorder and range nesting. With multiple roots `subtree_end[root_k]` is the next root, or `joint_count` for the last one. A joint is not an entity. Skeleton contains no meshes, clips, time, GPU handles, inverse binds or mutable buffers. Rest pose ≠ bind pose. Shared by every character and clip of the rig.

### 3.2 Clip (`NANM`)

Immutable, independently loadable resource: duration, `rig_compat_id`, absolute/additive kind with the additive reference identity, codec/version, joint tracks, interpolation rules, an optional **object curve** (one TRS signal for the whole character, §7.5), and builder-computed numbers: bounds `r_joints` (max over frames and joints of `|t_G|`), `r_root` (max root translation), `s_max` (max scale factor), and bake certificate `bake_fps_min`, `bake_reach` (§10). Skeleton owns no clips and declares no closed clip list. New clips are added by mounting new packs and resolving them by name (R6).

### 3.3 Pose ABI

`nt_anim_trs_t { float t[3]; float q[4]; float s[3]; }` — 40 bytes, alignment 4, AoS in joint order. Quaternions unit xyzw. This is the documented kernel ABI: a pose view is `nt_anim_trs_t *` plus a joint count and `v[j]` is the accessor, for whole-pose kernels and for sparse edits (IK apply, procedural) alike. Changing the layout (SoA, SIMD padding) is an ABI migration through one header and its consumers, decided by a sample→mix→FK measurement (#492), never a runtime dispatch. Disk formats are separate.

cglm rule: kernels that call cglm link `nt_math`, which sets `CGLM_ALL_UNALIGNED` (`engine/math/CMakeLists.txt`), so `vec4`/`versor` are unaligned and `q[4]` may be passed to `glm_quat_*`. `mat4` stays aligned (16, 32 with AVX) regardless; pose and model buffers are never cast to `mat4*`/`mat3*` — FK and palette build use the engine's own 3×4 row kernels or copy through aligned locals. The #473 kernels call no cglm: they are hand-written 3×4 math and link only `nt_core` + `nt_hash`.

`ModelPose` is a separate array of `nt_anim_mat34_t` — float32 affine 3×4 (three vec4 matrix rows `[m_r0 m_r1 m_r2 m_r3]`, 48 bytes) — in skeleton space; it preserves shear from hierarchical TRS. `SkinPalette` uses the same element type. Neither is local TRS.

`PoseInstance` = borrowed Skeleton view + caller-owned local and model buffers. The Skeleton view is `nt_anim_skeleton_t` (`nt_anim.h`) and the SkinBinding view of §3.4 is `nt_skin_binding_t` (`nt_skin.h`). Independently mutable instances need separate buffers; shared instances are read-only for all sharers. Views are pointer+count; no resource lookup, generation pool or content hash in hot arguments.

### 3.4 SkinBinding (`NSKN`)

Immutable resource describing how a mesh's vertices attach to the skeleton: `rig_compat_id`, palette→skeleton joint remap, inverse bind matrices (mesh space → joint space in the bind pose), input mesh-space convention, and two builder numbers: `reach` (max over bound vertices of `|inverse_bind[p]·v|`, the farthest a vertex sits from its joint) and `any_pose_radius` (§14). Vertex `joints` address the palette, not the skeleton. Every mesh exported from the same glTF skin shares one binding; a binding is the sharing key for baked banks (§10). Contains no mesh list, no per-mesh summaries and no geometry; the MESH asset is unchanged apart from its two streams, and the builder — which writes MESH and NSKN from one skin — guarantees that vertex indices lie inside the palette and weights are normalized. When the builder exports a further mesh against an existing NSKN it asserts that the mesh does not exceed the binding's `reach`.

### 3.5 Joint factors

One view type `nt_joint_factors_t { const float *v; uint16_t count; }`. `mix` takes it as **weights** — per-joint ratios ≥ 0 without upper bound (R3: 3 = three times the influence of a gain-1 input); `override`/`additive` take it as a **mask** — per-joint fractions in [0,1] (Debug-asserted). Absent = 1 everywhere. The parameter name and the kernel's documented range are the contract; there is no second type.

`DeformationBinding` (§12): prepared GPU matrix source for one frame.

## 4. Coordinate spaces

Column vectors. `L = T·R·S`; `G[j] = G[parent[j]]·L[j]`, roots `G = L`. Palette entry `p`: `j = remap[p]`, `B[p] = G[j]·inverse_bind[p]`, `vertex_world = E·Σ w[p]·B[p]·vertex_mesh`. `E` maps skeleton space to world. Entity/model transforms are neither baked into B nor applied twice. glTF import normalizes mesh-node/skin spaces explicitly; absent inverse binds mean identity, not inverse(rest). cglm mat4 → three vec4 rows is the explicit conversion `nt_anim_mat34_from_mat4`. Sockets (R5): `socket_world = E·G[j]·socket_local` after final FK, computed by `nt_anim_socket(world[16], G[j], socket_local, out)`, which writes an exact affine 3×4 and never decomposes to TRS (shear and nonuniform scale survive); a decomposition helper appears with its first consumer (#481), as does the two-handed composition.

**E for multi-mesh characters.** `transform_comp` currently computes standalone `T·R·S` without parent inheritance (`nt_transform_comp.c`), diverging from [Transform](../data/transform.md). v1 rule: the game writes the same world TRS to every mesh entity of a character before list construction; mesh entities are roots. General transform inheritance is a separate engine issue, not an animation prerequisite. With an object curve (§7.5), `E = E_game · O_blended` must stay TRS-representable for this component.

Skeleton math handles nonuniform scale; operations needing rigid/uniform transforms state that precondition. Imported shear is normalized under an explicit builder option with error checks or rejected.

## 5. Components and ownership

Only one new component: **`skin_comp`**, whose sole SoA field is a by-value `DeformationBinding`. Mesh/material/transform/color stay in `mesh_comp`, `material_comp`, `transform_comp`, `drawable_comp`.

Rig↔mesh association, pose memory, track state, banks, clip and binding views live in **game-owned character records** (a shared character definition: MESH + NSKN pairs, NSKL, clips, optional bank; per character: PoseInstance or bank lookup, tracks, world TRS, mesh entities). The renderer reads only `skin_comp` and ordinary render components. No `skeleton_comp`, `animation_player_comp` or `source_entity`: an entity→pointer map that the game already holds is ceremony. If a concrete game system later needs an entity association, add it then.

Lifetimes: PoseInstance, track arrays and banks live at stable game-owned addresses. `skin_comp` swap-and-pop moves only the by-value binding. Component publication and borrowed-view changes happen outside render-list construction/use; from list build through the last draw, entities, mesh/material/binding and referenced resources stay alive and unchanged. Changing mesh or binding invalidates the prepared binding.

## 6. Tracks: caller-owned playback state

Playback state is a fixed-capacity array of tracks the game allocates at character creation. There is no Player object in the engine.

```c
typedef struct {
    double   time, duration;
    uint32_t clip_key;
    float    speed, gain;   /* gain = g_t of §7.3 */
    uint32_t flags;         /* occupied, looping */
} nt_anim_track_t;          /* 32 B */
```

- `clip_key` is an opaque game/content `uint32_t`; assigning a clip supplies key and duration (fixed for that assignment). Tracks store no clip, weights, resource or GPU pointer; the game supplies current clip and factor views per evaluation call after publication.
- **Occupancy ≠ gain.** A gain-0 track keeps advancing (blend spaces need synchronized cycles); `speed = 0` pauses; the game releases a slot explicitly.
- `nt_anim_tracks_advance(tracks, count, dt)` updates clocks only (wrap, clamp). It is the only engine function over tracks; assign/release/crossfade ramps are three-field writes that live in game code (the showcase's assign helper asserts when no slot is free — the engine has no assign function and no eviction policy). #491 extends advance with time spans and signed cycle crossings for root-motion/event consumers. Baked characters use the same tracks with a bank lookup instead of kernels.

## 7. Time, sampling and composition

### 7.1 Time

`dt ≥ 0`, speed may be negative. Looping time normalizes to `[0,duration)` by floor/modulo including reverse; non-looping clamps to `[0,duration]`; duration 0 = static pose. No ever-growing clock. Seek emits nothing. Sampling calls no callbacks.

### 7.2 Sampling

`nt_anim_sample(clip, time, defaults, out)`: absent channels take the supplied defaults; T/S lerp; rotations shortest-path normalized lerp; STEP exact at its timestamp; CUBICSPLINE is resampled by the builder with error checks; random seek/reverse need no cursor. Direct-access codec (§16). Output must not overlap input.

### 7.3 Composition kernels

Three stateless kernels over local poses; the game chains them in any order. Preconditions per §2 item 5.

**`nt_anim_mix(inputs[T], defaults, out)`** — normalized N-way mixing. Input `t` has a pose view, optional joint weights `bw_t` (≥ 0) and a gain `g_t ≥ 0`. Per joint: `w_t = g_t·bw_t[j]`; `W = Σ w_t`; if `W == 0` copy `defaults[j]`; else T/S = `Σ (w_t/W)·x_t`. Quaternion: running accumulator in supplied input order — seed `A = w_first·canonicalize(q_first)` where `canonicalize` makes the largest-magnitude component positive with a fixed tie order; for each next `q`, flip its sign so `dot(A,q) ≥ 0`, and if `dot == 0` exactly use `canonicalize(q)`; `A += w·q`; `Q = normalize(A)` once (`|A + w·q|² ≥ |A|²`, so A never cancels). Seed canonicalization makes `q` and `−q` equivalent for every input including the first. Never align to rest (`+170°/−170°` would average to 0° instead of 180°); never reselect a dominant reference (sign flips across gain changes). **No epsilon threshold**: a sole contributor at gain 0.0001 contributes fully; to fade toward rest, add rest as an explicit input or use `override`. Cost `O(J·T)`, one quaternion normalization and one `1/W` per joint. The result is an approximate rotation average, order-sensitive for widely separated rotations and not associative: **supplied input order is part of the semantics** — no implementation (SIMD, #492) may reassociate or tree-reduce across inputs; parallelism is across joints or characters only.

This one kernel covers R1 (two full-body inputs with gains `1−a`, `a`, both advancing), R2 (N cycles with gains from a gameplay parameter; phase set by the game as `time_t = phase·duration_t`), and R3 (run at gain 1, attack at gain 1 with joint weights legs 0.25 / arms 3 → per-joint coefficients 0.2/0.75). A partial input with weights `bw` has coefficient `bw/(1+bw)`, constant through any crossfade whose full-body inputs sum to gain 1.

**`nt_anim_override(base, top, mask, alpha, out)`** — `a = alpha·mask[j]`, both in [0,1]; T/S lerp, Q shortest-path nlerp. Provides independent strength: "80 % upper-body aim regardless of locomotion's internal gains" — not expressible through `mix` (needs per-joint compensation, and `normalize((1−a)·normalize(A) + a·q) ≠ normalize(A + w·q)`). Permits `out == base`.

**`nt_anim_additive(base, delta, mask, alpha, out)`** — prepared deltas against an explicit reference: `T += a·ΔT`, `Q = normalize(Q·nlerp(identity, ΔQ, a))`, `S *= lerp(1, ΔS, a)`; missing prepared channels are neutral; `−identity` deltas take shortest-path nlerp. The builder reconstructs source channels, then computes deltas into the NANM kind/reference fields defined by #475 (#480 scope; consumers of additive data depend on #480).

### 7.4 Transitions and interruption

A normal transition is a **live crossfade** (R1): both clips advance, the game ramps gains over its chosen duration and releases the outgoing track at 0. Continuity follows from continuous gains.

**Interruption** (R8: new target while a crossfade is in progress, fixed capacity): reference recipe, entirely game code with kernels. In the update that detects the interruption: evaluate the current signal (before its external gain/mask, before later composition and IK, including its object sample) into capture scratch; copy into **one preallocated snapshot per interruptible signal** (`40·(J+1)` bytes: joints plus the object sample; scratch and snapshot never alias); release the replaced live sources; display `override(snapshot, target, 0)` in that same update, then ramp `a`. Another interruption overwrites the same snapshot. Promise: C0 pose continuity at the handoff within float tolerance; from the next update the snapshot is frozen (C1 break accepted); not across seeks, mask jumps or hemisphere ambiguities. Zero-duration replacement is immediate. A game that allocates no snapshot explicitly accepts a pop. The snapshot is an ordinary game buffer: the engine adds no transition object, history chain or eviction.

### 7.5 Object curve

R4 is a **separate one-element TRS signal**, not joint −1: `nt_anim_sample_object(curve, time, defaults, out)`; blended with the same kernels through one-element views and an explicit per-input object gain (never inferred from a pelvis weight). Missing curve = supplied defaults (identity). The game applies `E = E_game·O` or feeds it to its controller — in baked mode too: banks hold joint matrices only, and the crowd loop samples the object curve at the same track time. Extracted, loop-accumulating root motion and events over time spans are extension #491; differencing a blended absolute curve is not equivalent to blending deltas.

## 8. FK, procedural edits, IK, sockets, physics

Composition ends at a local pose. The game applies local edits and runs FK (whole rig or the edited subtree; same contract). A local write makes the model pose stale until FK; no dirty graph. Sockets per §4; no bone entities.

`nt_anim_fk(skel, local, model, first, count)` evaluates `[first, first + count)` — one contract for the whole rig and for a subtree, which the game enters as `first = j`, `count = subtree_end[j] − j`. A range starting at a root may span several roots; a range starting inside a subtree must stay inside it, asserted as `parent[first] == NT_ANIM_NO_PARENT || first + count ≤ subtree_end[first]`, so every in-range parent is either in the range or is `parent[first]`. Precondition, documented and not guarded because it is unverifiable: when `parent[first]` exists, `model[parent[first]]` is already current.

IK (extension #481): aim and analytic two-bone are functions over gathered transforms, target point, pole direction, local bend axis and parameters; they return rotation deltas **expressed in the joint's local frame** (applied by postmultiplying the local rotation) plus solved/clamped/degenerate status and target error before external weighting; the game applies deltas with a mask and re-runs FK. Replacing a solver = calling another C function. Two hands: main hand → weapon socket → off-hand target → off-hand IK → FK, no dependency cycle.

Ragdoll (extension #482): physics stays external; adapter maps body world transforms to a consistent target hierarchy and target local pose (`G_bone = inverse(E)·W_body·C_body_from_bone`), then the game blends with `override` and runs FK. Shear that TRS cannot hold is normalized under an explicit profile or rejected.

## 9. Retargeting

Extension #483. Builder path preferred: source clip + skeletons + RetargetMap → ordinary target clip. Runtime transfer is an independent kernel over pose views: source sample → source FK → retarget → target locals; unmapped joints take supplied target defaults; additive transfer reconstructs absolute poses against both references. Names help prepare maps, never hot lookup.

## 10. Baked playback: runtime banks

A **bank** is a game-owned object holding the skinning matrices `B = G·inverse_bind` of a set of **absolute** clips for one SkinBinding, sampled at a fixed rate, encoded as RGBA32F or RGBA16F texels. The bank **owns its texels on the CPU** (part of its single init allocation) and one GPU texture created from them; after bake it is independent of the clip/skeleton/binding views it was built from, so clip packs may be unmounted. The developer's contract (R7):

```c
nt_anim_bank_init(&bank, &desc);   /* CPU only: skeleton, binding, clips[], fps, format, width → one allocation */
nt_anim_bank_bake(&bank);          /* sample → FK → palette per frame into the CPU texels; nt_gfx_make_texture(data) */
s = nt_anim_bank_lookup(&bank, clip_index, normalized_time);   /* frame0, frame1 origins, alpha — pure */
```

The lookup result `nt_anim_bank_lookup_t { uint16_t x0, y0, x1, y1; float alpha; }` (12 B) lives in `engine/anim_bank/nt_anim_bank.h`, which is a contract header only until #478 implements init/bake/lookup.

**Deliberate divergence** from [Principles](../core/principles.md) §3 (the builder does heavy work, the runtime loads): the bake is a one-time explicit load-time operation on prebuilt data, chosen because the developer's rigs are small and the load-time cost is measured (#487); serialized banks in packs are the fallback if that measurement fails.

**`init`** (CPU only, no gfx call) takes `desc.width` (0 = `min(2048, gpu_caps.max_texture_size)`) and computes the layout: `fpr = floor(width / 3P)` frames per texture row; for each clip `F = ceil(duration·fps)` frames at `k/fps`, plus one frame at exactly `duration` for non-looping clips, plus two frames per distinct STEP timestamp `t_s` (sampled at `t_s⁻` and `t_s`) with a small per-clip segment table; `height = ceil(frames_total / fpr)`. Asserts: `3P ≤ width` (P ≤ 682 at the WebGL2 minimum 2048), `height ≤ max_texture_size`, origins addressable in `uint16`, every clip absolute with equal `rig_compat_id`, and the certificate check below. GPU storage is `width·height·bytes_per_texel`; the CPU copy is the same size; descriptors and bake workspace are counted separately. Baked-only characters allocate no PoseInstance. Adding clips = a replacement bank (init + bake with the expanded list), swapped in after outstanding draws finish; no append API, no implicit growth.

**`bake`** samples absent channels from the skeleton rest pose, runs the ordinary kernels frame by frame (`sample → fk → skin_palette_build`), converts to the texel format (FP16 through the shared `nt_half.h` round-to-nearest-even), and creates the texture with `nt_gfx_make_texture(data = texels, NEAREST, no mips)` in one call; precondition: a live context. During FP16 conversion the bank asserts representability (`|x| ≤ 65504`) and a translation quantization error ≤ `desc.fp16_tolerance` in scene units — exact for the actual binding, so no builder flag is needed for FP16. **Context loss:** destroy the husk and recreate the texture from the CPU texels; nothing rebakes, no source views are needed.

**Texel layout** (shared with dynamic palette textures, §12): a **frame** is `3·P` contiguous texels in one texture row starting at its origin `(x, y)`; palette entry `p` occupies texels `(x + 3p + r, y)`, `r = 0..2`, texel `r` = matrix row `r` of the 3×4 affine `[m_r0 m_r1 m_r2 m_r3]`. Frames are packed row-major; no frame spans a texture row; the two frames of an interpolated pair may lie in different texture rows (the binding carries two independent origins), so nothing is duplicated at row breaks. Looping seam pair `(F−1, 0)` interpolates over the actual interval `duration − (F−1)/fps`; non-looping lookups clamp to the end frame; STEP: at `t_s` the lookup selects the new segment and never interpolates across the discontinuity.

**Temporal certificate in the builder.** Interpolating two frames is not sampling the clip; the error between the lerp of grid frames and the exact decoded pose depends on the rate. For each clip the builder evaluates the decoded pose densely (source keyframe times, STEP boundaries and ≥ 4 sub-samples per grid interval) at the profile fps and bounds the model-space error `|ΔG|` of every joint over the interval, then writes `bake_fps_min` — the smallest admitted rate for which `max|ΔG|·bake_reach ≤ tolerance` — and `bake_reach`, the reach the certificate assumes (profile parameter, default: the largest `reach` among the skins in the export). This is computed in joint space, so it needs no skin and holds for every binding whose `reach ≤ bake_reach`. `bank_init` asserts `fps ≥ clip.bake_fps_min` and `binding.reach ≤ clip.bake_reach`; `bake_fps_min = 0` means uncertified and is rejected. The dominant interpolation term is the rotation-lerp scale shrink `cos(Δθ/2)` between frames. Clips with travelling root translation will need higher FP16 tolerances until #491 extracts root motion.

**Memory arithmetic for `desc`:** a 100-joint frame is 300 texels = 2,400 B FP16 / 4,800 B FP32; at width 2048 six frames fit per row, so 20 clips × 60 frames ≈ 1,200 frames → 200 rows → 3.3 MB FP16 / 6.6 MB FP32 of texture plus the same again for the CPU texels; bake time ≈ frames × (sample + FK + palette + conversion) — tens of milliseconds for simple rigs, measured in #487. Banks are shared by every character on the same binding; different bindings need different banks (B depends on inverse binds). Baking G plus a per-binding inverse-bind texture is a measured alternative if binding duplication dominates, changing bank layout and shader together.

## 11. CPU ↔ baked switching

The game stores mode and pending intent; the engine never switches by distance, IK weight or count. **v1: explicit hard switch** (R7) — matching clip/time preserves phase, not displayed continuity (bank frames are matrix-lerped at bank fps; CPU poses are exact); the object curve is sampled identically in both modes, so `E` does not jump. A continuous palette bridge (blend of the currently displayed bank pair with the CPU palette in the RGBA32F dynamic path) is extension #485; it reconstructs the displayed pair from the bank's CPU texels, without GPU readback.

## 12. GPU preparation

`skin_palette_build(binding, model, out, capacity)` computes B without uploads. `anim_gpu` owns preallocated shared RGBA32F dynamic textures (NEAREST) and staging: `begin_frame → write_palette* → flush → all passes`. `write_palette` copies a frame into staging and returns a `DeformationBinding` without GL calls; `flush` uploads coalesced dirty texture rows as full rows plus `h = 1` edge fragments (`nt_gfx_update_texture` takes tightly packed rectangles). Bank entries receive a binding through `anim_gpu_bank_binding(bank_texture, lookup_result)`, which stamps the current epoch; baked characters re-derive their binding every frame after `lookup`.

```c
typedef struct {
    nt_texture_t texture;        /* borrowed bank or dynamic texture */
    uint16_t x0, y0, x1, y1;     /* origins of the two frames (CPU: x1==x0, y1==y0) */
    float alpha;                 /* CPU: 0 */
    uint32_t frame_epoch;        /* 0 reserved */
} nt_deformation_binding_t;      /* _Static_assert(sizeof == 20) */
```

The type lives in `engine/anim_gpu/nt_anim_gpu.h`, a contract header only until #476 implements staging and upload.

`frame_epoch` mirrors `anim_gpu`'s counter for one reachable bug — a character whose binding was not prepared this frame; an item whose epoch ≠ the current epoch is `NT_ASSERT` (per-item, every build). A binding is valid until the context's next `begin_frame` or graphics invalidation. Textures and assets stay alive through all consuming passes. Capacities are init parameters; overflow asserts. Identical pose+binding may share one prepared binding explicitly; no global dedup cache. Coordinates are separate x/y integers (a linear offset may exceed 2²⁴ in float).

## 13. Renderer

`skinned_mesh_renderer_draw_list(items, count, context)` consumes existing 16-byte render items in the given order; through entity it reads mesh/material/world/color and `skin_comp`. No sampling, FK, mode selection, culling or sorting.

**One skinning vertex program per pass, always two frames.** The shader fetches both frame origins and interpolates by alpha; CPU palettes are the case `frame1 == frame0`, `alpha == 0` (second fetch hits the same texels). There is no program kind, no per-run mode uniform, no material pair and no kind↔program validator: every material submitted here implements the documented shader ABI. `u_skin_matrices` (declared `highp sampler2D`; GLSL ES defaults samplers to `lowp`) is a renderer-reserved name; materials own surface textures/params/state (this specializes [Material](../render/material.md) for this renderer, like other specialized bindings). `joints`/`weights` reach the program through the material `attr_map` like every other stream; unmapped streams are skipped, so a skinned MESH still draws through `mesh_renderer`. The program's non-sampler uniforms must fit the backend's 16-entry cache. #487 measures always-lerp against a single-fetch variant; a second program appears only if that number justifies it.

**Sampler set.** Surface textures plus bones are bound in **one complete** `nt_gfx_apply_texture_bindings` call (five entries; the shared helper array holds `NT_MATERIAL_MAX_TEXTURES` = 4 — this renderer has its own). Reapply when material **or** deformation texture changes; reset tracking at each `draw_list`.

**Batching.** `batch_key(material, mesh)` stays the exact two-slot packing; equal key is a candidate run, and the run also requires equal deformation texture and pass state. Frame origins, alpha, world and color are per-instance. Only adjacent compatible items merge; the game's order wins.

**Instance layout** is owned by the renderer header (world rows 3, color 1, frame origins 1 as four UINT16, alpha 1 = 6 of 8 instance attributes; stride 64–76 B, independent of the mesh renderer's 64-byte cap). `joints` are UINT16×4 through float attributes with shader integer conversion; `weights` are normalized UINT8×4 or FLOAT16×4 per builder profile. Locations are not part of this specification.

**Normals/tangents.** LBS approximation with the linear part of the blended matrix and the world normal transform. One guard: `len2 = dot(n,n); n = (len2 > EPS && len2 < BIG) ? n·inversesqrt(len2) : FIXED_UNIT` with `EPS = 1e-12`, `BIG = 1e30` in `highp` — a two-sided comparison rather than `isnan`/`isinf`, because NaN generation is optional in GLSL ES; tangent orthogonalized against the final normal with the same guard. Finite output is promised for finite matrices and weights (bank matrices come from finite CPU math; CPU palettes are finite by construction), not correct lighting on collapsed geometry. The fast profile is named `POSITIVE_UNIFORM_SCALE_FAST`: positive-uniform joint and world scale, validated by the builder over the full mesh→joint→skeleton chain; CPU skeleton math still supports nonuniform scale. It is a named restriction, not a runtime enum.

**Passes.** All passes use the same frame binding and epoch. Baseline multipass: assign pass material → build/sort list → draw → next pass. WebGL2 needs only 2D float textures with NEAREST filters, `texelFetch`, instanced attributes; no SSBO/compute/texture arrays/float render targets/float-linear filtering.

## 14. Bounds and culling

Numbers, no per-bone data. Single-clip playback (baked crowd, single-track CPU) culls with a sphere at `E·origin` of radius `r_joints + reach·s_max` (clip numbers × binding reach — binding-independent composition). **Composed or edited poses** (mix, override, additive, procedural, IK) use the binding's `any_pose_radius = Σ_chain |S_rest·t_rest| + reach`, which is an upper bound **only for poses whose non-root translations and scales equal the rest pose**; the game adds the largest `r_root` among the inputs. The max of two clip radii is *not* a bound for their mix (two 80° bends mixed at 0.5 straighten the arm past both inputs). Poses that edit translations or scales, and ragdolls, need a game-supplied radius or no culling. Rest AABB is not an animated bound. Gameplay pose/time and visual pose/time are declared separately. Per-bone envelopes are deferred.

## 15. Resources and lifetimes

`rig_compat_id` (`nt_hash64_t`) is shared by NSKL, NANM and NSKN (order, parents, rest semantics, units); binding a clip, skeleton and skin together asserts equality; nothing else is cross-checked at runtime (§2 item 5). Handles/generations still protect slots. Adapters validate their own payload, publish immutable views and own runtime memory. Order per frame: draws finished → `resource_step` → refresh views → advance/compose/prepare → build list → draw. A borrowed view lasts until its owner is deactivated/republished; a bank owns its texels and survives the unload of the clips it was baked from.

**Pack grouping.** Activation and unmount are whole-pack (default `NT_RESOURCE_MAX_PACKS` = 16), and mounting a pack that contains a non-BLOB type whose activator is not registered asserts (`nt_resource.c`, parse). Builder manifests therefore group by **co-residency**: a rig/mesh pack (MESH, NSKL, NSKN); clip-group packs (NANM, e.g. base locomotion vs. dances loaded mid-game). Applications that link animation register the three activators; the manifest keeps peak mounted packs (old + new + prefetch) within the limit or overrides it deliberately.

Not ready → the game continues old playback, holds a pose or omits the item. Planned unload: detach consumers, finish draws, release. Context loss invalidates prepared bindings and lists; CPU poses, tracks, bank texels and immutable CPU assets survive; owners recreate dynamic textures, bank textures (from CPU texels), programs and vertex inputs; a fresh epoch follows.

## 16. Builder, codec, wire formats

Order: import → normalize spaces/units → canonical hierarchy/remap → validate clips/skin → prepare absolute/additive and object curves → retarget if requested → CPU codec → bounds and bake certificate (§10, §14) → pack. Clips stay independent. Rates and error budgets come from content profiles. Content errors (invalid rigs, channels, ranges, weights, failed certificates) are `NT_BUILD_ASSERT` after a logged diagnostic, per the existing builder policy; the ATLAS graceful-error channel is not extended.

**v1 payloads:** `NSKL`, `NSKN`, `NANM` (F32 direct-access: constant/default tracks, uniform samples, exact STEP, kind/reference, object curve, bounds numbers, certificate; Q16 is #486). `NANM` uses an explicit little-endian header plus bounded `tag/offset/count/stride` section descriptors for its v1 sections (fixed parsing, no extensible registry); `NSKL`/`NSKN` keep simple fixed layouts. `NSKL` carries `subtree_end` next to the parent indices (§3.1), and the builder computes `rig_compat_id` by calling `nt_anim_rig_compat_id` rather than reimplementing the schema. Major version = incompatible layout or semantics; minor = explicitly skippable optional sections only, never flags that change decode semantics; unsupported required features/codecs are rejected; the outer pack still requires an exact version and a rebuild. The pack CRC32 detects accidental corruption, does not authenticate content and does not replace payload-local validation; no per-asset CRC. Asset types 7–9 extend the enum in `shared/include/nt_pack_format.h`; `NT_RESOURCE_MAX_ASSET_TYPES` (`engine/resource/nt_resource_internal.h`) goes 8 → 12; the parser's `> NT_ASSET_ATLAS` bound becomes `> NT_ASSET_LAST` (still recoverable pack validation; unregistered activators keep asserting); every enumeration site is updated (#475 lists them); activators are registered explicitly by applications that link them. A shared `shared/include/nt_half.h` provides FP32↔FP16 conversion for the builder (FLOAT16 weights) and the bank. Little-endian fields, magic/version, explicit counts/offsets, overflow and range checks before views; no struct casting; adapters copy into aligned memory when needed. glTF is the normative source reference; import selects the canonical rig (skin/node), helper joints and identity explicitly so independently imported clips reproduce the same identity; the current scene API (flattened nodes) gains parent/skin access. The importer reads every paired `JOINTS_n/WEIGHTS_n` set, keeps the four largest influences per vertex with deterministic tie-breaking, renormalizes (UINT8 weights sum to 255), and gates the reduction on decoded vertex error against the full source influences; it also gates runtime nlerp against the source quaternion interpolation at keys and interior samples (quarter points), refining resampling within the profile before failing.

## 17. Modules and composition checks

- `anim` (`engine/anim`, one module `nt_anim`): `nt_anim.h` — pose ABI, skeleton view, 3×4 kernels, FK, sockets, rig identity, plus sample and mix/override/additive, with `tracks_advance` in a separate object file; `nt_skin.h` — binding view, palette build. The radius bounds helper (§14) lands with its first consumer (#479).
- `anim_bank`: bank init/bake/lookup over `anim` + gfx interface.
- `anim_gpu`: staging/upload, DeformationBinding; depends on the gfx interface. `anim_bank` and `anim_gpu` exist today as contract headers only (§10, §12).
- `skinned_mesh_renderer` + `skin_comp`.
- Optional asset adapters (NSKL/NSKN/NANM); `anim_ik`, `anim_retarget` as extensions. Track assign/release/crossfade helpers live in the showcase.
- Kernels retain no inputs and keep no mutable global evaluation state; concurrent calls (if a game ever schedules them) need immutable shared inputs, disjoint outputs/workspaces and caller synchronization — no engine job system, staging reservation or atomics exist or are planned.

v1 composition checks without LTO (#488): no animation (no animation symbols at all); headless CPU without gfx (`anim` only); full v1. Extensions add CPU + IK (#481) and CPU + retarget (#483). A bank-only character allocates no PoseInstance.

## 18. Verification

Math (helpers, roots, non-identity binds, `bind·inverse_bind = I` at the bind pose, two bindings one pose; CPU vs GPU agreement; ABI alignment under `-fsanitize=alignment`); clips (absent/constant channels, STEP, cubic resampling, reverse, seek, duration 0, multi-loop, object curve, certificate fields); composition (`q/−q` for every input incl. the first, exact zero-dot pair, `±170°`, zero totals, all-zero joint weights, asymmetric parent/child weights, non-unit scale, override strength under changing mix gains, interruption at fixed capacity with constant memory, object-curve blend); bank (FP16/32, row-crossing and seam pairs, STEP segments, non-integral `duration·fps`, `3·P ≤ width`, certificate asserts, FP16 tolerance assert, matrices equal to the CPU path at frame times, restore from CPU texels after clip unload); render (mixed static/skinned order, A/B/A deformation textures with full sampler reapply, per-instance frames in one batch, degenerate normals finite, one epoch across passes, stale epoch asserts); lifetimes (swap-and-pop, slot reuse, unload/reload, loss/restore); memory/linking (no hot heap, asserted overflow, composition symbol checks); performance (separate sample/mix/FK/palette/upload/bake timings, draws, bytes, resident memory, `.wasm.gz`; comparisons only on identical content and quality).

## 19. Outside v1

Continuous bridge, IK, ragdoll, retargeting (builder and runtime), Q16, block codec, SIMD, root-motion/event traversal, evaluation-rate LOD — extensions with issues. Without issues: catalogs/AnimationSet, serialized banks in packs, morph targets (a future authored morph path applies mesh-local deltas before skinning and the world transform; representation deferred), mirroring, unskinned node animation, N-way mixing inside baked playback, dual quaternions, compute skinning, per-bone envelopes, a normal-angle bake gate (not transferable across bindings), a caller-owned sampling cache (only if #487 shows sampling dominates; #492), FBX/DAE builder adapters into the same canonical rig/clip/skin data, runtime threading, `transform_comp` inheritance, joint-set/influence-count LOD (only after #494 and profiling); no LOD enum, seam or automatic distance policy.

