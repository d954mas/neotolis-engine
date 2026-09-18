# Skeletal Animation

**Status:** architecture specification v0.3 (2026-09-17), implementation in progress under epic #472; the pose ABI, FK, binding math, rig identity, clip sampling with the object curve, the track clock, the NSKL/NSKN/NANM wire formats including the NSKN bounds, their builder encoders, the runtime adapters and the glTF import of a rig, its skin binding and its skinned meshes are implemented; mix/override/additive, banks, GPU staging, the renderer, clip import with its resampling gates, the bake certificate and the §14 culling helper are not. Function names of unimplemented parts are provisional; responsibilities, coordinate spaces, ownership, memory and behavior are normative. Changes to this chapter land together with the code that implements them.

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

- **CPU:** clips → sampled track poses → mix/override/additive → procedural edits/IK → FK → skinning palette → `skeletal_gpu` staging → GPU (frame1 = frame0, alpha 0).
- **Baked:** bank (matrices per frame, baked once) → clip time → two frame origins + alpha → GPU.

`skinned_mesh_renderer` draws meshes with joint indices and weights from prepared deformation data. It never discovers the animation source.

## 2. Principles and boundaries

1. **Data apart from behavior.** Skeleton, clip, pose, weights and binding are data. Sample, mix, override, additive, FK, palette build, bake and upload are separate functions. No operation also advances time, loads, allocates or draws.
2. **One authority for the final pose.** The game orders writes to local/model buffers; nothing resamples after IK or physics.
3. **Predictable memory.** Heap only at init, character/bank creation, asset activation and explicit reconfiguration. Sampling, advance, composition, FK, IK, retargeting and frame preparation allocate nothing. Per-frame working memory (sampled track poses, capture scratch) comes from `nt_mem_scratch` or a buffer the game sized at init; kernels receive it as pointer + capacity and never allocate. Bank bake is a heavy explicit call at load time, not a frame operation.
4. **Capacity is configured, overflow asserts.** Track capacity, workspace size, bank size, palette staging, texture budgets are set at creation. Exceeding them is `NT_ASSERT`, like component storage: no growth, no eviction policy, no fallback. Recoverable outcomes exist at the resource boundary (readiness, pack validation, activator rejection) and at the existing gfx boundary (texture creation and context restoration may fail per [API contracts](../core/api-contracts.md); the game uses the existing readiness queries and retries restoration explicitly; no new readiness mirror).
5. **Validate at boundaries.** A loader validates its own payload before publishing views; an NSKL/NSKN/NANM activator that rejects its payload returns 0 and the asset is FAILED like a mesh or texture. Cross-resource compatibility (`rig_compat_id`) is asserted at explicit binding; what the builder already guarantees (vertex indices within the palette, normalized weights) is not re-checked at runtime. Numerical functions are `void` with `NT_ASSERT` preconditions — cheap checks (pointers, aliasing, indices, counts, rig id, capacity and scalar ranges) remain independent of `NT_SKELETAL_CHECKS`, including inside joint/palette loops. The flag gates expensive numerical validation (unit quaternions and finite TRS components) and is an explicit CMake option (OFF by default; Debug and release-test presets select ON, production Release presets select OFF). All checks use plain `NT_ASSERT`s, so `NT_ASSERT_MODE=0` removes them without changing the configured flag. Genuine alternative outcomes (degenerate IK chain) return a defined result. A game that must survive mismatched content compares the ids it already holds before binding.
6. **No extension framework.** No graph, layer stack, controller, solver registry, scheduler, palette cache or per-joint callback.
7. **Modularity at link time.** No-animation builds link no animation code. Do not rely on LTO.
8. **Contracts are invariant, layouts are revisable.** Ownership, spaces, compatibility, lifetimes and draw compatibility are invariant. Pose ABI, shader specialization, bank texture dimensions and attribute locations are implementation details owned by one header each and changed by measurement.

## 3. Core data

### 3.1 Skeleton (`NSKL`)

Immutable resource: joint count, parent indices, local rest pose, stable joint identifiers, `rig_compat_id` (`nt_hash64_t`). **Rig identity schema:** `rig_compat_id = hash64` (existing `nt_hash`, seed 0) over one padding-free little-endian byte sequence. The stable joint id is `uint32_t` = `nt_hash32_str(node name)`, so renaming a bone is a different rig; the schema version is 1 and a new value is a new rig identity. The convention byte is a reserved `uint8_t` fixed at 1: every rig the engine hashes carries the glTF convention (metres, Y-up, right-handed), so it is a schema constant, not a per-rig property. The sequence is `8 + 46·J` bytes (`NT_SKELETAL_RIG_ID_BYTES`):

```
"NRIG"  u8 schema  u8 convention  u16 joint_count                        8 B
per joint, index order:  u32 joint_id  u16 parent  f32 t[3] q[4] s[3]   46 B
```

Floats are exact canonical binary32, canonicalized in this order: quaternion sign first (largest |component| positive, the first maximum in x, y, z, w order winning ties), then `-0 → +0` on every float. The schema hashes exact binary32 bits, so the id identifies bit-identical rest data, not numerically near-equal data: a near-tie between opposite-sign quaternion components can flip the canonical sign between toolchains, and the two rigs are then different rigs. Inverse binds, mesh data, display names and clip codecs are excluded.

Published test vector — joint 0 id `0x11111111`, no parent, t (1, 2, 3), q (0, 0, 0, 1), s (1, 1, 1); joint 1 id `0x22222222`, parent 0, t (0, −0, 0.5), q (0, 0, −0.70710678, −0.70710678), s (1, 1, 1):

```
4E 52 49 47 01 01 02 00
11 11 11 11 FF FF  00 00 80 3F 00 00 00 40 00 00 40 40  00 00 00 00 00 00 00 00 00 00 00 00 00 00 80 3F  00 00 80 3F 00 00 80 3F 00 00 80 3F
22 22 22 22 00 00  00 00 00 00 00 00 00 00 00 00 00 3F  00 00 00 00 00 00 00 00 F3 04 35 3F F3 04 35 3F  00 00 80 3F 00 00 80 3F 00 00 80 3F
rig_compat_id = 0x03E59E1475239034
```

`nt_skeletal_rig_compat_id(skel, scratch, size)` in `engine/skeletal` is the single implementation of this schema: `nt_builder_add_skeleton` hashes the exported rig with it, and procedural rigs fill their own field with it. Clips and bindings are exported against the authoritative rig export; near-equal rigs from independent exports are different rigs by design. Joint indices `uint16_t`, `UINT16_MAX` = no parent. Builder orders joints in preorder; each subtree is a contiguous range; multiple roots allowed; helpers are every node on the path from the scene root, or from the cut node of the rule below, to a skin joint — identity or not — and all of them are retained. `subtree_end[j]` (`uint16_t`) closes that range — the subtree of `j` is `[j, subtree_end[j])` — and is part of the skeleton: the builder writes it into NSKL and the activator validates preorder and range nesting. With multiple roots `subtree_end[root_k]` is the next root, or `joint_count` for the last one. A joint is not an entity. Skeleton contains no meshes, clips, time, GPU handles, inverse binds or mutable buffers. Rest pose ≠ bind pose. Shared by every character and clip of the rig.

**Skeleton space is glTF scene space.** The joints of an imported rig are every node on the paths from the *scene root* of the joints' hierarchy to each skin joint, identity wrappers included. `skin.skeleton` is a hint and is not used for selection: a rig that drops an identity ancestor silently moves skeleton space. Skin joints reaching two scene roots are a content error — one rig has one root. The optional skeleton-root override is an explicit **cut**: the cut node becomes joint 0, its parent space becomes skeleton space, the game's `E` must carry the omitted ancestors, and a skin joint outside the cut subtree is a content error rather than a second root.

**A `matrix` node on a rig path is decomposed** in the builder's rig importer — never in `engine/skeletal`, and never during parse, because a sheared matrix in a static-mesh scene must not abort a mesh-only build. Column lengths and the rotation run in double: a non-finite element or a bottom row other than `(0, 0, 0, 1)` is rejected before any column is measured, a column shorter than `1e-6` cannot recover a rotation and is rejected, a negative determinant becomes a negative X scale, and the quaternion comes from the largest-diagonal branch canonicalized to `w ≥ 0` so the stored bits do not depend on which branch ran. The decomposition is accepted only if `nt_skeletal_mat34_from_trs` recomposes every linear element within `64·FLT_EPSILON·|scale[col]|` of its column and the translation exactly; anything else is shear the runtime cannot represent, and shear is a logged diagnostic followed by `NT_BUILD_ASSERT`. The decomposed bits are the rest bits `rig_compat_id` hashes.

### 3.2 Clip (`NANM`)

Immutable, independently loadable resource: duration, `rig_compat_id`, the additive reference identity (`additive_ref_id`, 0 for an absolute clip), version, joint tracks, interpolation rules and an optional **object curve** (one TRS signal for the whole character, §7.5). The builder-computed bounds `r_joints`, `r_root`, `s_max` (§14) and the bake certificate `bake_fps_min`, `bake_reach` (§10) are added with their first consumer (#499); nothing in v1 reads them, so they are not in the payload. Skeleton owns no clips and declares no closed clip list. New clips are added by mounting new packs and resolving them by name (R6).

### 3.3 Pose ABI

`nt_skeletal_trs_t { float t[3]; float q[4]; float s[3]; }` — 40 bytes, alignment 4, AoS in joint order. Quaternions unit xyzw. This is the documented kernel ABI: a pose view is `nt_skeletal_trs_t *` plus a joint count and `v[j]` is the accessor, for whole-pose kernels and for sparse edits (IK apply, procedural) alike. Changing the layout (SoA, SIMD padding) is an ABI migration through one header and its consumers, decided by a sample→mix→FK measurement (#492), never a runtime dispatch. The migration candidate is AoSoA ×4 for local poses (blocks of four joints, SoA inside the block, as ozz's `SoaTransform`): it matches the 128-bit SIMD width of wasm simd128/SSE/NEON for sample and mix, while the model pose stays `nt_skeletal_mat34_t[]` (FK is per joint, all fields). What `tools/research/skeletal_layout/RESULTS.md` measured is ten-channel SoA, not AoSoA ×4: with the native compiler's per-joint SIMD but no explicit cross-joint SIMD, that layout is slower in sample, mix and summed total; FK timings do not establish a consistent winner. AoSoA ×4 is the unmeasured candidate, and #492 measures it with simd128 on; sparse edits would then go through lane accessors instead of `v[j]`. Disk formats are separate.

The public `nt_skeletal_mat34_from_trs` helper checks finite translation/scale and a unit quaternion under `NT_SKELETAL_CHECKS`; FK and sockets use that check without repeating it. With `NT_SKELETAL_CHECKS=0` the validation is compiled out, including for direct calls to the helper.

cglm rule: kernels that call cglm link `nt_math`, which sets `CGLM_ALL_UNALIGNED` (`engine/math/CMakeLists.txt`), so `vec4`/`versor` are unaligned and `q[4]` may be passed to `glm_quat_*`. `mat4` stays aligned (16, 32 with AVX) regardless; pose and model buffers are never cast to `mat4*`/`mat3*` — FK and palette build use the engine's own 3×4 row kernels or copy through aligned locals. The kernels call no cglm: they are hand-written 3×4 math, and `nt_skeletal` links only `nt_core`, `nt_hash` and libm (`sqrtf` in the sampler's nlerp).

`ModelPose` is a separate array of `nt_skeletal_mat34_t` — float32 affine 3×4 (three vec4 matrix rows `[m_r0 m_r1 m_r2 m_r3]`, 48 bytes) — in skeleton space; it preserves shear from hierarchical TRS. `SkinPalette` uses the same element type. Neither is local TRS.

`PoseInstance` = borrowed Skeleton view + caller-owned local and model buffers. The Skeleton view is `nt_skeletal_skeleton_t` and the SkinBinding view of §3.4 is `nt_skin_binding_t`, both in `nt_skeletal.h`. Independently mutable instances need separate buffers; shared instances are read-only for all sharers. Views are pointer+count; no resource lookup, generation pool or content hash in hot arguments.

### 3.4 SkinBinding (`NSKN`)

Immutable resource describing how a mesh's vertices attach to the skeleton: `rig_compat_id`, palette→skeleton joint remap and inverse bind matrices (mesh space → joint space in the bind pose, where mesh space is the primitive's vertex space; the skinned mesh node's transform is ignored, which is the glTF rule rather than a builder choice). The two builder numbers `reach` (max over bound vertices of `|inverse_bind[p]·v|` — a joint-space distance, the farthest a vertex sits from its joint; it becomes a skeleton-space radius only through the §14 stretch bound) and `any_pose_radius` (§14) are present in NSKN and in the view. Vertex `joints` address the palette, not the skeleton. Every mesh exported from the same glTF skin shares one binding; a binding is the sharing key for baked banks (§10). Contains no mesh list, no per-mesh summaries and no geometry; the MESH asset is unchanged apart from its two streams, and the builder — which writes MESH and NSKN from one skin — guarantees that vertex indices lie inside the palette and weights are normalized.

### 3.5 Joint factors

One view type `nt_joint_factors_t { const float *v; uint16_t count; }`. `mix` takes it as **weights** — per-joint ratios ≥ 0 without upper bound (R3: 3 = three times the influence of a gain-1 input); `override`/`additive` take it as a **mask** — per-joint fractions in [0,1] (an ordinary `NT_ASSERT` contract). Absent = 1 everywhere. The parameter name and the kernel's documented range are the contract; there is no second type.

`DeformationBinding` (§12): prepared GPU matrix source for one frame.

## 4. Coordinate spaces

Column vectors. `L = T·R·S`; `G[j] = G[parent[j]]·L[j]`, roots `G = L`. Palette entry `p`: `j = remap[p]`, `B[p] = G[j]·inverse_bind[p]`, `vertex_world = E·Σ w[p]·B[p]·vertex_mesh`. `E` maps skeleton space to world. Entity/model transforms are neither baked into B nor applied twice. glTF import takes skeleton space to be scene space — the joints are every node from the scene root of the joints' hierarchy down to each skin joint, unless an explicit skeleton root cuts the hierarchy and its parent space becomes skeleton space (§3.1); absent inverse binds mean identity, not inverse(rest). cglm mat4 → three vec4 rows is the explicit conversion `nt_skeletal_mat34_from_mat4`; its input and output must not overlap. Sockets (R5): `socket_world = E·G[j]·socket_local` after final FK, computed by `nt_skeletal_socket(world[16], G[j], socket_local, out)`, which writes an exact affine 3×4 and never decomposes to TRS (shear and nonuniform scale survive); a decomposition helper appears with its first consumer (#481), as does the two-handed composition.

**E for multi-mesh characters.** `transform_comp` currently computes standalone `T·R·S` without parent inheritance (`nt_transform_comp.c`), diverging from [Transform](../data/transform.md). v1 rule: the game writes the same world TRS to every mesh entity of a character before list construction; mesh entities are roots. General transform inheritance is a separate engine issue, not an animation prerequisite. With an object curve (§7.5), `E = E_game · O_blended` must stay TRS-representable for this component.

Skeleton math handles nonuniform scale; operations needing rigid/uniform transforms state that precondition. Imported shear is rejected (§3.1 matrix rule); no normalization option exists.

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
    uint32_t flags;         /* NT_SKELETAL_TRACK_OCCUPIED | NT_SKELETAL_TRACK_LOOPING */
} nt_skeletal_track_t;      /* 32 B, pinned by a _Static_assert in nt_skeletal.h */
```

- `clip_key` is an opaque game/content `uint32_t`; assigning a clip supplies key and duration (fixed for that assignment). Tracks store no clip, weights, resource or GPU pointer; the game supplies current clip and factor views per evaluation call after publication.
- **Occupancy ≠ gain.** A gain-0 track keeps advancing (blend spaces need synchronized cycles); `speed = 0` pauses; the game releases a slot explicitly.
- `nt_skeletal_tracks_advance(tracks, count, dt)` updates clocks only (wrap, clamp). Per track: a slot without `NT_SKELETAL_TRACK_OCCUPIED` is untouched; `duration == 0` resets `time` to 0; otherwise `time += speed·dt`, then `NT_SKELETAL_TRACK_LOOPING` normalizes by `time − floor(time/duration)·duration` (one step for reverse and for several cycles at once, with a rounding residue on either boundary restarting the cycle at 0) and a non-looping track clamps to `[0, duration]`. `speed = 0` is a pause with no special case. A finite `dt ≥ 0` is asserted, and every occupied track is asserted to carry a `duration ≥ 0` and a finite `speed`, and a looping track's cycle count `time/duration` to lie within ±9.2·10¹⁸ (a NaN, an infinite step or a sub-attosecond duration would make the int64 floor undefined); nothing else is written and nothing is called. It is the only engine function over tracks; assign/release/crossfade ramps are three-field writes that live in game code (the showcase's assign helper asserts when no slot is free — the engine has no assign function and no eviction policy). #491 extends advance with time spans and signed cycle crossings for root-motion/event consumers. Baked characters use the same tracks with a bank lookup instead of kernels.

## 7. Time, sampling and composition

### 7.1 Time

`dt ≥ 0`, speed may be negative. Looping time normalizes to `[0,duration)` by floor/modulo including reverse; non-looping clamps to `[0,duration]`; duration 0 = static pose. No ever-growing clock. Seek emits nothing. Sampling calls no callbacks.

### 7.2 Sampling

**Runtime clip layout.** `nt_skeletal_clip_t` in `nt_skeletal.h` is an immutable borrowed view with the ownership contract of `nt_skeletal_skeleton_t`. It is also the wire layout: the NANM payload (§16) holds exactly these tables, so the activator copies the payload once and points the view into the copy. Each channel has exactly one storage mode, so the tables never describe the same joint channel twice (a builder invariant by construction: one channel record per joint component; activation does not check it), and a channel in no table is absent.

- **Sampled** channels share one uniform grid of `sample_count` samples on `[0, duration]` (`inv_step = (sample_count−1)/duration`, 0 when `sample_count == 1` or `duration == 0`) and live in `sample_count` frame blocks of `block_floats` floats, block `i` at `blocks + i·block_floats`, laid out as `t` rows `[n_t][3]`, then `q` rows `[n_q][4]`, then `s` rows `[n_s][3]`. Row `k` belongs to joint `t_joint[k]`/`q_joint[k]`/`s_joint[k]`. One sample therefore reads two adjacent blocks and nothing else instead of striding once per channel. `blocks` is NULL when the clip has no sampled channel.
- **Constant** channels are `ct_joint`/`ct` (3 floats), `cq_joint`/`cq` (4), `cs_joint`/`cs` (3) with their counts.
- **STEP** channels keep their authored timestamps: `steps[]` of `{first, count, joint, channel, pad}` into the shared `keys[]` table of `{time, v[4]}` (t/s use `v[0..2]` and leave `v[3]` at 0, q is unit xyzw). A track's value at `time` is found by binary search over its own range.
- The object curve (§7.5) is a separate one-element signal inside the clip, with its own modes and key ranges over the clip's grid and key table.
- Identity travels with the view: `rig_compat_id`, `additive_ref_id`, `joint_count`.

**Contract.** `nt_skeletal_sample(clip, time, defaults, out)` writes `joint_count` local transforms. `time` is a `double` in `[0, duration]`, asserted. Absent channels take the supplied defaults, constants copy, and STEP channels hold the last key at or before `time` (the first key when `time` precedes it). For sampled channels `f = time·inv_step` in double, `i = floor(f)` clamped so `i+1 ≤ sample_count−1`, `u = (float)(f − i)`; `u == 0` copies block `i` and `u == 1` copies block `i+1`, both bit for bit, so a grid time reproduces its stored sample exactly. `u` within `2⁻²⁰` of 0 or 1 is snapped to it: `time·inv_step` lands a ulp off its integer for most grid times of a non-binary duration, and the snap keeps those exact copies instead of lerps (`2⁻²⁰` of one grid interval is far below any authored key spacing); the clamp at `time == duration` yields `u == 1` the same way. Otherwise T/S lerp as `a·(1−u) + b·u` and Q takes the shortest-path normalized lerp (`d = dot(a,b)`, `b' = d < 0 ? −b : b`, `q = normalize(a·(1−u) + b'·u)`). Random seek and reverse need no cursor because the index is computed, not stepped. CUBICSPLINE will be resampled by the importer (#499) with error checks. Direct-access codec (§16). `defaults` and `out` are caller-owned buffers of `joint_count` entries and must not overlap.

### 7.3 Composition kernels

Three stateless kernels over local poses; the game chains them in any order. Preconditions per §2 item 5.

**`nt_skeletal_mix(inputs[T], defaults, out)`** — normalized N-way mixing. Input `t` has a pose view, optional joint weights `bw_t` (≥ 0) and a gain `g_t ≥ 0`. Per joint: `w_t = g_t·bw_t[j]`; `W = Σ w_t`; if `W == 0` copy `defaults[j]`; else T/S = `Σ (w_t/W)·x_t`. Quaternion: running accumulator in supplied input order — seed `A = w_first·canonicalize(q_first)` where `canonicalize` makes the largest-magnitude component positive with a fixed tie order; for each next `q`, flip its sign so `dot(A,q) ≥ 0`, and if `dot == 0` exactly use `canonicalize(q)`; `A += w·q`; `Q = normalize(A)` once (`|A + w·q|² ≥ |A|²`, so A never cancels). Seed canonicalization makes `q` and `−q` equivalent for every input including the first. Never align to rest (`+170°/−170°` would average to 0° instead of 180°); never reselect a dominant reference (sign flips across gain changes). **No epsilon threshold**: a sole contributor at gain 0.0001 contributes fully; to fade toward rest, add rest as an explicit input or use `override`. Cost `O(J·T)`, one quaternion normalization and one `1/W` per joint. The result is an approximate rotation average, order-sensitive for widely separated rotations and not associative: **supplied input order is part of the semantics** — no implementation (SIMD, #492) may reassociate or tree-reduce across inputs; parallelism is across joints or characters only.

This one kernel covers R1 (two full-body inputs with gains `1−a`, `a`, both advancing), R2 (N cycles with gains from a gameplay parameter; phase set by the game as `time_t = phase·duration_t`), and R3 (run at gain 1, attack at gain 1 with joint weights legs 0.25 / arms 3 → per-joint coefficients 0.2/0.75). A partial input with weights `bw` has coefficient `bw/(1+bw)`, constant through any crossfade whose full-body inputs sum to gain 1.

**`nt_skeletal_override(base, top, mask, alpha, out)`** — `a = alpha·mask[j]`, both in [0,1]; T/S lerp, Q shortest-path nlerp. Provides independent strength: "80 % upper-body aim regardless of locomotion's internal gains" — not expressible through `mix` (needs per-joint compensation, and `normalize((1−a)·normalize(A) + a·q) ≠ normalize(A + w·q)`). Permits `out == base`.

**`nt_skeletal_additive(base, delta, mask, alpha, out)`** — prepared deltas against an explicit reference: `T += a·ΔT`, `Q = normalize(Q·nlerp(identity, ΔQ, a))`, `S *= lerp(1, ΔS, a)`; missing prepared channels are neutral; `−identity` deltas take shortest-path nlerp. The builder reconstructs source channels, then computes deltas and stamps the reference pose identity into the NANM `additive_ref_id` (§16); the delta preparation and its consumers are #480 scope.

### 7.4 Transitions and interruption

A normal transition is a **live crossfade** (R1): both clips advance, the game ramps gains over its chosen duration and releases the outgoing track at 0. Continuity follows from continuous gains.

**Interruption** (R8: new target while a crossfade is in progress, fixed capacity): reference recipe, entirely game code with kernels. In the update that detects the interruption: evaluate the current signal (before its external gain/mask, before later composition and IK, including its object sample) into capture scratch; copy into **one preallocated snapshot per interruptible signal** (`40·(J+1)` bytes: joints plus the object sample; scratch and snapshot never alias); release the replaced live sources; display `override(snapshot, target, 0)` in that same update, then ramp `a`. Another interruption overwrites the same snapshot. Promise: C0 pose continuity at the handoff within float tolerance; from the next update the snapshot is frozen (C1 break accepted); not across seeks, mask jumps or hemisphere ambiguities. Zero-duration replacement is immediate. A game that allocates no snapshot explicitly accepts a pop. The snapshot is an ordinary game buffer: the engine adds no transition object, history chain or eviction.

### 7.5 Object curve

R4 is a **separate one-element TRS signal**, not joint −1: `void nt_skeletal_sample_object(const nt_skeletal_object_curve_t *curve, double time, const nt_skeletal_trs_t *defaults, nt_skeletal_trs_t *out)`; blended with the same kernels through one-element views and an explicit per-input object gain (never inferred from a pelvis weight). `nt_skeletal_object_curve_t` is self-contained — a mode per channel, its own constant value, its own sampled array, the grid (`sample_count`, `inv_step`, `duration`) and STEP ranges into the key table it borrows from the clip — so it is sampled without a clip pointer and follows §7.2's per-channel rules over one element. Missing curve = supplied defaults (identity): a NULL curve and a curve whose three modes are all absent both copy `defaults`, so a clip without an object curve needs no branch at the call site. `out` must not alias `defaults`. The game applies `E = E_game·O` or feeds it to its controller — in baked mode too: banks hold joint matrices only, and the crowd loop samples the object curve at the same track time. Extracted, loop-accumulating root motion and events over time spans are extension #491; differencing a blended absolute curve is not equivalent to blending deltas.

## 8. FK, procedural edits, IK, sockets, physics

Composition ends at a local pose. The game applies local edits and runs FK (whole rig or the edited subtree; same contract). A local write makes the model pose stale until FK; no dirty graph. Sockets per §4; no bone entities.

`nt_skeletal_fk(skel, local, model, first, count)` requires `count ≥ 1` and `first + count ≤ joint_count` and evaluates `[first, first + count)` — one contract for the whole rig and for a subtree, which the game enters as `first = j`, `count = subtree_end[j] − j`. A range starting at a root may span several roots; a range starting inside a subtree must stay inside it, asserted as `parent[first] == NT_SKELETAL_NO_PARENT || first + count ≤ subtree_end[first]`, so every in-range parent is either in the range or is `parent[first]`. `local` and `model` are caller-owned and must not overlap, asserted per call and `restrict`-qualified, as are the buffers of `nt_skin_palette_build`. `nt_skeletal_mat34_mul` also requires non-aliasing output and uses `restrict`; its alias assertion, FK parent-order checks and palette remap bounds remain active when `NT_SKELETAL_CHECKS=0` (unless assertions are disabled by `NT_ASSERT_MODE=0`). Precondition, documented and not guarded because it is unverifiable: when `parent[first]` exists, `model[parent[first]]` is already current.

IK (extension #481): aim and analytic two-bone are functions over gathered transforms, target point, pole direction, local bend axis and parameters; they return rotation deltas **expressed in the joint's local frame** (applied by postmultiplying the local rotation) plus solved/clamped/degenerate status and target error before external weighting; the game applies deltas with a mask and re-runs FK. Replacing a solver = calling another C function. Two hands: main hand → weapon socket → off-hand target → off-hand IK → FK, no dependency cycle.

Ragdoll (extension #482): physics stays external; adapter maps body world transforms to a consistent target hierarchy and target local pose (`G_bone = inverse(E)·W_body·C_body_from_bone`), then the game blends with `override` and runs FK. Shear that TRS cannot hold is normalized under an explicit profile or rejected.

## 9. Retargeting

Extension #483. Builder path preferred: source clip + skeletons + RetargetMap → ordinary target clip. Runtime transfer is an independent kernel over pose views: source sample → source FK → retarget → target locals; unmapped joints take supplied target defaults; additive transfer reconstructs absolute poses against both references. Names help prepare maps, never hot lookup.

## 10. Baked playback: runtime banks

A **bank** is a game-owned object holding the skinning matrices `B = G·inverse_bind` of a set of **absolute** clips for one SkinBinding, sampled at a fixed rate, encoded as RGBA32F or RGBA16F texels. The bank **owns its texels on the CPU** (part of its single init allocation) and one GPU texture created from them; after bake it is independent of the clip/skeleton/binding views it was built from, so clip packs may be unmounted. The developer's contract (R7):

```c
nt_skeletal_bank_init(&bank, &desc);   /* CPU only: skeleton, binding, clips[], fps, format, width → one allocation */
nt_skeletal_bank_bake(&bank);          /* sample → FK → palette per frame into the CPU texels; nt_gfx_make_texture(data) */
s = nt_skeletal_bank_lookup(&bank, clip_index, normalized_time);   /* frame0, frame1 origins, alpha — pure */
```

The lookup result is `nt_skeletal_bank_lookup_t { uint16_t x0, y0, x1, y1; float alpha; }` (12 B).

**Deliberate divergence** from [Principles](../core/principles.md) §3 (the builder does heavy work, the runtime loads): the bake is a one-time explicit load-time operation on prebuilt data, chosen because the developer's rigs are small and the load-time cost is measured (#487); serialized banks in packs are the fallback if that measurement fails.

**`init`** (CPU only, no gfx call) takes `desc.width` (0 = `min(2048, gpu_caps.max_texture_size)`) and computes the layout: `fpr = floor(width / 3P)` frames per texture row; a duration-zero clip stores exactly one frame at time 0, without extra endpoint or STEP frames, in either playback mode. For positive durations: `F = ceil(duration·fps)` frames at `k/fps`, plus one frame at exactly `duration` for non-looping clips, plus two frames per distinct STEP timestamp `t_s` (sampled at `t_s⁻` and `t_s`) with a small per-clip segment table; `height = ceil(frames_total / fpr)`. Asserts: `3P ≤ width` (P ≤ 682 at the WebGL2 minimum 2048), `height ≤ max_texture_size`, origins addressable in `uint16`, every clip absolute (`additive_ref_id == 0`) with equal `rig_compat_id`, and the certificate check below once the certificate fields land (#499). GPU storage is `width·height·bytes_per_texel`; the CPU copy is the same size; descriptors and bake workspace are counted separately. Baked-only characters allocate no PoseInstance. Adding clips = a replacement bank (init + bake with the expanded list), swapped in after outstanding draws finish; no append API, no implicit growth.

**`bake`** samples absent channels from the skeleton rest pose, runs the ordinary kernels frame by frame (`sample → fk → nt_skin_palette_build`), converts to the texel format (FP16 through the shared `nt_half.h` round-to-nearest-even), and creates the texture with `nt_gfx_make_texture(data = texels, NEAREST, no mips)` in one call; precondition: a live context. During FP16 conversion the bank asserts representability (`|x| ≤ 65504`) and a translation quantization error ≤ `desc.fp16_tolerance` in scene units — exact for the actual binding, so no builder flag is needed for FP16. **Context loss:** destroy the husk and recreate the texture from the CPU texels; nothing rebakes, no source views are needed.

**Texel layout** (shared with dynamic palette textures, §12): a **frame** is `3·P` contiguous texels in one texture row starting at its origin `(x, y)`; palette entry `p` occupies texels `(x + 3p + r, y)`, `r = 0..2`, texel `r` = matrix row `r` of the 3×4 affine `[m_r0 m_r1 m_r2 m_r3]`. Frames are packed row-major; no frame spans a texture row; the two frames of an interpolated pair may lie in different texture rows (the binding carries two independent origins), so nothing is duplicated at row breaks. A duration-zero lookup returns the same frame as both origins with `alpha = 0`, without wrapping or seam interpolation. For positive durations, looping seam pair `(F−1, 0)` interpolates over the actual interval `duration − (F−1)/fps`; non-looping lookups clamp to the end frame; STEP: at `t_s` the lookup selects the new segment and never interpolates across the discontinuity.

**Temporal certificate in the builder** (#499, with the bank that consumes it; the NANM payload carries no certificate field until then). Interpolating two frames is not sampling the clip; the error between the lerp of grid frames and the exact decoded pose depends on the rate. For each clip the builder evaluates the decoded pose densely (source keyframe times, STEP boundaries and ≥ 4 sub-samples per grid interval) at the profile fps and bounds the model-space error `|ΔG|` of every joint over the interval, then writes `bake_fps_min` — the smallest admitted rate for which `max|ΔG|·bake_reach ≤ tolerance` — and `bake_reach`, the reach the certificate assumes (profile parameter, default: the largest `reach` among the skins in the export). This is computed in joint space, so it needs no skin and holds for every binding whose `reach ≤ bake_reach`. `bank_init` asserts `fps ≥ clip.bake_fps_min` and `binding.reach ≤ clip.bake_reach`; `bake_fps_min = 0` means uncertified and is rejected. The dominant interpolation term is the rotation-lerp scale shrink `cos(Δθ/2)` between frames. Clips with travelling root translation will need higher FP16 tolerances until #491 extracts root motion.

**Memory arithmetic for `desc`:** a 100-joint frame is 300 texels = 2,400 B FP16 / 4,800 B FP32; at width 2048 six frames fit per row, so 20 clips × 60 frames ≈ 1,200 frames → 200 rows → 3.3 MB FP16 / 6.6 MB FP32 of texture plus the same again for the CPU texels; bake time ≈ frames × (sample + FK + palette + conversion) — tens of milliseconds for simple rigs, measured in #487. Banks are shared by every character on the same binding; different bindings need different banks (B depends on inverse binds). Baking G plus a per-binding inverse-bind texture is a measured alternative if binding duplication dominates, changing bank layout and shader together.

## 11. CPU ↔ baked switching

The game stores mode and pending intent; the engine never switches by distance, IK weight or count. **v1: explicit hard switch** (R7) — matching clip/time preserves phase, not displayed continuity (bank frames are matrix-lerped at bank fps; CPU poses are exact); the object curve is sampled identically in both modes, so `E` does not jump. A continuous palette bridge (blend of the currently displayed bank pair with the CPU palette in the RGBA32F dynamic path) is extension #485; it reconstructs the displayed pair from the bank's CPU texels, without GPU readback.

## 12. GPU preparation

`nt_skin_palette_build(binding, model, model_count, out, capacity)` computes B without uploads. `skeletal_gpu` owns preallocated shared RGBA32F dynamic textures (NEAREST) and staging: `begin_frame → write_palette* → flush → all passes`. `write_palette` copies a frame into staging and returns a `DeformationBinding` without GL calls; `flush` uploads coalesced dirty texture rows as full rows plus `h = 1` edge fragments (`nt_gfx_update_texture` takes tightly packed rectangles). Bank entries receive a binding through `skeletal_gpu_bank_binding(bank_texture, lookup_result)`, which stamps the current epoch; baked characters re-derive their binding every frame after `lookup`.

```c
typedef struct {
    nt_texture_t texture;        /* borrowed bank or dynamic texture */
    uint16_t x0, y0, x1, y1;     /* origins of the two frames (CPU: x1==x0, y1==y0) */
    float alpha;                 /* CPU: 0 */
    uint32_t frame_epoch;        /* 0 reserved */
} nt_deformation_binding_t;      /* _Static_assert(sizeof == 20) */
```

`frame_epoch` mirrors `skeletal_gpu`'s counter for one reachable bug — a character whose binding was not prepared this frame; an item whose epoch ≠ the current epoch is `NT_ASSERT` (per-item, every build). A binding is valid until the context's next `begin_frame` or graphics invalidation. Textures and assets stay alive through all consuming passes. Capacities are init parameters; overflow asserts. Identical pose+binding may share one prepared binding explicitly; no global dedup cache. Coordinates are separate x/y integers (a linear offset may exceed 2²⁴ in float).

## 13. Renderer

`skinned_mesh_renderer_draw_list(items, count, context)` consumes existing 16-byte render items in the given order; through entity it reads mesh/material/world/color and `skin_comp`. No sampling, FK, mode selection, culling or sorting.

**One skinning vertex program per pass, always two frames.** The shader fetches both frame origins and interpolates by alpha; CPU palettes are the case `frame1 == frame0`, `alpha == 0` (second fetch hits the same texels). There is no program kind, no per-run mode uniform, no material pair and no kind↔program validator: every material submitted here implements the documented shader ABI. `u_skin_matrices` (declared `highp sampler2D`; GLSL ES defaults samplers to `lowp`) is a renderer-reserved name; materials own surface textures/params/state (this specializes [Material](../render/material.md) for this renderer, like other specialized bindings). `joints`/`weights` reach the program through the material `attr_map` like every other stream; unmapped streams are skipped, so a skinned MESH still draws through `mesh_renderer`. The program's non-sampler uniforms must fit the backend's 16-entry cache. #487 measures always-lerp against a single-fetch variant; a second program appears only if that number justifies it.

**Sampler set.** Surface textures plus bones are bound in **one complete** `nt_gfx_apply_texture_bindings` call (five entries; the shared helper array holds `NT_MATERIAL_MAX_TEXTURES` = 4 — this renderer has its own). Reapply when material **or** deformation texture changes; reset tracking at each `draw_list`.

**Batching.** `batch_key(material, mesh)` stays the exact two-slot packing; equal key is a candidate run, and the run also requires equal deformation texture and pass state. Frame origins, alpha, world and color are per-instance. Only adjacent compatible items merge; the game's order wins.

**Instance layout** is owned by the renderer header (world rows 3, color 1, frame origins 1 as four UINT16, alpha 1 = 6 of 8 instance attributes; stride 64–76 B, independent of the mesh renderer's 64-byte cap). `joints` are UINT8×4 or UINT16×4, never normalized, through float attributes with shader integer conversion; `weights` are normalized UINT8×4 — whose four lanes sum to exactly 255 by largest-remainder rounding — FLOAT16×4 or FLOAT32×4, per the mesh layout; the builder validates both streams. FLOAT16 lane sums deviate from 1 by at most `4·2⁻¹¹`, and no builder gate exists for that deviation. Stored lane order is heaviest-first, ties broken towards the lower joint index: a contract of the export, not an accident of the reduction. Locations are not part of this specification.

**Normals/tangents.** LBS approximation with the linear part of the blended matrix and the world normal transform. One guard: `len2 = dot(n,n); n = (len2 > EPS && len2 < BIG) ? n·inversesqrt(len2) : FIXED_UNIT` with `EPS = 1e-12`, `BIG = 1e30` in `highp` — a two-sided comparison rather than `isnan`/`isinf`, because NaN generation is optional in GLSL ES; tangent orthogonalized against the final normal with the same guard. Finite output is promised for finite matrices and weights (bank matrices come from finite CPU math; CPU palettes are finite by construction), not correct lighting on collapsed geometry. The fast profile is named `POSITIVE_UNIFORM_SCALE_FAST`: positive-uniform joint and world scale, validated by the builder over the full mesh→joint→skeleton chain; CPU skeleton math still supports nonuniform scale. It is a named restriction, not a runtime enum.

**Passes.** All passes use the same frame binding and epoch. Baseline multipass: assign pass material → build/sort list → draw → next pass. WebGL2 needs only 2D float textures with NEAREST filters, `texelFetch`, instanced attributes; no SSBO/compute/texture arrays/float render targets/float-linear filtering.

## 14. Bounds and culling

Numbers, no stored per-bone data. All radii below are in skeleton space, except `reach`, which is joint space and enters through the stretch bound `a`; the builder validates every stored radius as finite and ≥ 0 and the activator copies them unchecked. A sphere of radius `r` becomes a world-space sphere centred at `E·origin` with radius `s_E·r`, where `s_E = max(abs(scale(E)))` for the TRS world transform required by §4. `reach` and `any_pose_radius` are present in NSKN and written by the skin-binding import; `r_joints`, `r_root` and `s_max` in NANM, and the culling helper that reads all five, are still pending. The rules below define what the builder computes.

Single-clip playback uses `r = r_joints + reach·s_max`. `r_joints` bounds joint-origin distance from the skeleton origin, and `s_max` bounds the maximum stretch (largest singular value) of every model matrix's linear part, over the entire decoded playback including interpolation. Products of local maximum absolute scale components along each ancestor chain give a conservative stretch bound, including hierarchical shear; individual local scales or model-matrix column lengths do not.

For **composed or edited poses**, the builder computes `any_pose_radius` from the rest hierarchy. Let `m[j] = max(abs(s_rest[j]))`. At a root, `a[j] = m[j]` and `d[j] = 0`; at a non-root, `a[j] = a[parent]·m[j]` and `d[j] = d[parent] + a[parent]·length(t_rest[j])`. Store only `any_pose_radius = max_palette_j(d[j] + a[j]·reach)`. Here `a` bounds accumulated stretch and `d` bounds distance from the root. Arbitrary joint rotations are allowed, but **all local scales, including root scales, and all non-root translations must equal rest**.

Use `r = R_root + any_pose_radius`, where `R_root` bounds the final root translation lengths. For convex mix/override, the largest contributing absolute-pose `r_root` suffices, including defaults selected by zero weights; additive or procedural root translation changes need a bound on their result. Poses that change the required rest translations or scales, and ragdolls, need a game-supplied radius or no culling.

The max of two clip radii is *not* a bound for their mix (two 80° bends mixed at 0.5 straighten the arm past both inputs). Rest AABB is not an animated bound. Gameplay pose/time and visual pose/time are declared separately. Per-bone envelopes are deferred.

## 15. Resources and lifetimes

`rig_compat_id` (`nt_hash64_t`) is shared by NSKL, NANM and NSKN (order, parents, rest semantics, units); binding a clip, skeleton and skin together asserts equality; nothing else is cross-checked at runtime (§2 item 5). Handles/generations still protect slots. Adapters validate their own payload, publish immutable views and own runtime memory.

**The v1 adapters** are `nt_skeletal_assets_activate_skeleton/skin_binding/clip` and their deactivators (`engine/skeletal_assets`), registered by the application through `nt_resource_register_type` exactly like textures; resource core references none of them. Each one validates the structure of the *whole* payload before it allocates anything, copies the payload into **one** allocation and points the view of §7.2 into that copy — the wire layout is the runtime layout (§16), so nothing is transposed or re-indexed. A structurally broken payload logs one warning and returns 0, which leaves the asset FAILED with nothing allocated. Nothing reads the blob after activation, so any blob policy may drop it. `nt_skeletal_assets_init(max_assets)` allocates **one** pool for all three types, so the game sizes the peak mounted set once instead of guessing three splits; activating past the capacity is an assert, not a load failure. The capacity counts every *activated* asset, not every published one: when one resource id is present in two mounted packs both copies activate and hold a slot, only the winner is published, and the loser is released when its own pack unmounts. The runtime handle is a generational `nt_pool` id, so a stale handle fails `nt_pool_valid` instead of naming a reused slot. `nt_skeletal_assets_skeleton/skin_binding/clip(nt_resource_t)` return the views and assert the asset type and a live handle; a view stays valid until its asset is deactivated (unmount, reload, shutdown), so the game refetches it after `resource_step`. The adapters cross-check no `rig_compat_id` — no second asset exists at activation.

Order per frame: draws finished → `resource_step` → refresh views → advance/compose/prepare → build list → draw. A borrowed view lasts until its owner is deactivated/republished; a bank owns its texels and survives the unload of the clips it was baked from.

**Pack grouping.** Activation and unmount are whole-pack (default `NT_RESOURCE_MAX_PACKS` = 16), and mounting a pack that contains a non-BLOB type whose activator is not registered asserts (`nt_resource.c`, parse). Builder manifests therefore group by **co-residency**: a rig/mesh pack (MESH, NSKL, NSKN); clip-group packs (NANM, e.g. base locomotion vs. dances loaded mid-game). Applications that link animation register the three activators; the manifest keeps peak mounted packs (old + new + prefetch) within the limit or overrides it deliberately.

Not ready → the game continues old playback, holds a pose or omits the item. Planned unload: detach consumers, finish draws, release. Context loss invalidates prepared bindings and lists; CPU poses, tracks, bank texels and immutable CPU assets survive; owners recreate dynamic textures, bank textures (from CPU texels), programs and vertex inputs; a fresh epoch follows.

## 16. Builder, codec, wire formats

Order: import → canonical hierarchy/remap → validate clips/skin → prepare absolute/additive and object curves → retarget if requested → CPU codec → bounds and bake certificate (§10, §14) → pack. Clips stay independent. Rates and error budgets arrive with the importer that reads them (today: `skin_drop_tolerance`). Content errors (invalid rigs, channels, ranges, weights, failed certificates) are `NT_BUILD_ASSERT` after a logged diagnostic, per the existing builder policy; the ATLAS graceful-error channel is not extended.

**v1 payloads:** `NSKL`, `NSKN` and `NANM`, all little-endian, all defined in
`shared/include/nt_skeletal_format.h` and shared by builder and runtime. The
codec is F32 direct-access (Q16 is #486). **The wire layout is the runtime
layout:** the builder writes exactly the tables the sampler of §7.2 reads, and
an activator validates the structure, copies the whole payload into one
allocation and points the runtime view at it — no transpose, no per-field
decode, no section table. Every target of this engine is little-endian, so
headers travel as packed structs and arrays are copied as bytes; the builder
pins that with a `__BYTE_ORDER__` static assert where the compiler defines it (GCC/Clang; MSVC targets are little-endian by platform). `NT_SKELETAL_FORMAT_VERSION`
is a plain `uint16_t` (v1 of this layout = 3) compared **exactly** in all three
payloads: the layout is the runtime layout, so any change to it is a rebuild.
The builder computes `rig_compat_id` by calling `nt_skeletal_rig_compat_id`
rather than reimplementing the schema, and `nt_builder_add_skeleton` returns
the id it wrote, so clips and bindings are stamped with the identity that
actually shipped. The pack CRC32 detects accidental corruption, does not
authenticate content and does not replace payload-local validation; there is no
per-asset CRC.

**NSKL** — `NT_SKL_SIZE(J) = 16 + 48·J` bytes, exact:

| offset | field | note |
|---|---|---|
| 0 | `u32 magic` | `"NSKL"` |
| 4 | `u16 version` | exact match |
| 6 | `u16 joint_count` (J) | ≥ 1 |
| 8 | `u64 rig_compat_id` | computed by the encoder |
| 16 | `u16 parent[J]` | `0xFFFF` = root, else `< j` (preorder) |
| 16+2J | `u16 subtree_end[J]` | subtree of j = `[j, subtree_end[j])` |
| 16+4J | `u32 joint_id[J]` | `nt_hash32_str(node name)`, unique |
| 16+8J | `f32 rest[J][10]` | AoS `t[3] q[4] s[3]`, q unit xyzw |

**NSKN** — `NT_SKN_SIZE(P) = 24 + 50·P` bytes, exact. The two radii sit in the
header, before the matrices, because a culling consumer reads them alone and a
fixed offset costs it no arithmetic over P; the matrices then come first among
the arrays so the `u16` table ends the payload without padding:

| offset | field | note |
|---|---|---|
| 0 | `u32 magic` | `"NSKN"` |
| 4 | `u16 version` | |
| 6 | `u16 palette_count` (P) | ≥ 1 |
| 8 | `u64 rig_compat_id` | |
| 16 | `f32 reach` | joint space (§3.4), finite and ≥ 0; validated by the builder, copied unchecked by the activator |
| 20 | `f32 any_pose_radius` | skeleton space (§14), finite and ≥ 0; validated by the builder, copied unchecked by the activator |
| 24 | `f32 inverse_bind[P][12]` | `nt_skeletal_mat34_t` row order `r[3][4]` |
| 24+48P | `u16 remap[P]` | palette entry → skeleton joint |

`remap[p]` is not bounded against a skeleton at activation — no skeleton is
available there; `nt_skin_palette_build` asserts `remap[p] < model_count` where
both exist.

**NANM** — a 56-byte header, then the arrays in one fixed order. Every array
before the `u16` tables is a multiple of 4 bytes, so each one starts aligned and
nothing is padded:

| offset | field | note |
|---|---|---|
| 0 | `u32 magic` | `"NANM"` |
| 4 | `u16 version` | |
| 6 | `u16 joint_count` (J) | ≥ 1 |
| 8 | `u32 sample_count` (N) | ≥ 1 |
| 12 | `f32 duration` | finite, ≥ 0 |
| 16 | `u64 rig_compat_id` | |
| 24 | `u64 additive_ref_id` | 0 = absolute |
| 32 | `u16 n_t, n_q, n_s` | sampled joint rows per component kind |
| 38 | `u16 n_ct, n_cq, n_cs` | constant joint channels per component kind |
| 44 | `u32 n_steps` | joint STEP tracks |
| 48 | `u32 n_keys` | keys of every STEP track, joints and object |
| 52 | `u8 object_mode[3]` | 0 ABSENT, 1 CONSTANT, 2 SAMPLED, 3 STEP (t, q, s) |
| 55 | `u8 _pad` | zero |

| order | array | bytes |
|---|---|---|
| 1 | `f32 blocks[N][block_floats]` | `block_floats = 3n_t + 4n_q + 3n_s` |
| 2 | `f32 ct[n_ct][3]`, `cq[n_cq][4]`, `cs[n_cs][3]` | constant values |
| 3 | `steps[n_steps]` | 12 B: `{u32 first; u32 count; u16 joint; u8 channel; u8 pad}` |
| 4 | `keys[n_keys]` | 20 B: `{f32 time; f32 v[4]}` |
| 5 | `object` | 64 B `NtAnmObject`: `{f32 constant[10]; u32 step_first[3]; u32 step_count[3]}`, only when some `object_mode` is not ABSENT |
| 6 | `f32 object_sampled[N][10]` | only when some `object_mode` is SAMPLED |
| 7 | `u16 t_joint[n_t], q_joint[n_q], s_joint[n_s]` | joint of each sampled row |
| 8 | `u16 ct_joint[n_ct], cq_joint[n_cq], cs_joint[n_cs]` | joint of each constant |

The modes stay in the header because they decide whether the record and the
sampled array exist at all; `constant` is `t[3] q[4] s[3]` with only the
CONSTANT channels used, and `step_first`/`step_count` are 0 unless that mode is
STEP.

The payload size is exactly `nt_anm_size(header)`, computed in 64 bits on both
sides. The STEP tracks partition `keys` exactly: the joint tracks in table order,
then the object STEP channels in t, q, s order, each `first` being the running
key total. `sample_count == 1` means no frame block — a clip built only from
constant and step channels may still have a duration.

**Activation checks structure only**, because a structurally sound payload can
still only misbehave numerically, and numbers are the builder's contract
(`NT_BUILD_ASSERT`) plus `NT_SKELETAL_CHECKS` in the kernels that consume a
pose (`nt_skeletal_mat34_from_trs`). The list is:

- all three: at least a header, magic, `version` equal, and a size equal to the
  exact size the counts imply (computed in 64 bits).
- NSKL: `joint_count ≥ 1`, `j < subtree_end[j] ≤ J`, and one exact-preorder rule
  walked in joint order — **each joint's parent is the nearest earlier joint
  whose range is still open** at that joint (`NO_PARENT` when none is) — plus
  `subtree_end[j] ≤ subtree_end[parent[j]]` for a non-root. That single rule
  carries preorder parents, sibling contiguity, root chaining and the last root
  closing at `J`, which is what makes FK's parent-before-child read safe. The
  walk pops each closed subtree once, so it is `O(J)` amortized.
- NSKN: `palette_count ≥ 1`.
- NANM: `joint_count ≥ 1`; `sample_count ≥ 1`; `duration` finite and ≥ 0; every
  `object_mode[c] ≤ 3`; every entry of the six `u16` joint tables and every
  `steps[s].joint` below `joint_count`, and `steps[s].channel ≤ 2` — these are
  the write indices the sampler turns into positions in the caller's pose; the
  key partition above, each count ≥ 1 and the running total ending at `n_keys`;
  and `sample_count ≥ 2` with `duration > 0` whenever a frame block or an object
  sampled array exists, because interpolation reads two adjacent grid entries.

Asset types 7–9 extend the enum in `shared/include/nt_pack_format.h` and
`NT_ASSET_LAST` bounds it; `NT_RESOURCE_MAX_ASSET_TYPES`
(`engine/resource/nt_resource_internal.h`) is 12 and a `_Static_assert` there
keeps it above `NT_ASSET_LAST`; the parser rejects an asset type above
`NT_ASSET_LAST` (still recoverable pack validation; unregistered activators keep
asserting); activators are registered explicitly by applications that link them.
The builder's public API is `nt_builder_add_skeleton/skin_binding/clip`
(`nt_builder.h`); each encodes an in-memory import result and registers it in
one step, and the raw encoders stay builder-internal. The glTF side of the same
header is `nt_builder_import_rig`/`nt_builder_free_rig` over a
`nt_builder_rig_t`, `nt_builder_add_scene_skinned_mesh` (with its
`skin_drop_tolerance`, default `NT_BUILDER_SKIN_DROP_TOLERANCE`) and
`nt_builder_add_scene_skin_binding`. A source violation of any
rule above is `NT_BUILD_ASSERT`, because the importer is the only producer. The
value rules it asserts: every float finite; every quaternion unit (squared norm
within 1e-3 of 1); `v[3] == 0` for t/s constants and keys; STEP times strictly
increasing, the first ≥ 0 and the last ≤ `duration`; a SAMPLED channel needs
`sample_count ≥ 2` and `duration > 0`; joint ids unique. Activation re-checks
none of these. The encoder
zeroes the payload before writing it, so pad bytes and unused object-constant
slots are deterministic and the payload hash is the clip's identity. A shared
`shared/include/nt_half.h` provides FP32↔FP16 conversion for the builder
(FLOAT16 weights) and the bank.

glTF is the normative source reference; import selects the canonical rig (skin/node), helper joints and identity explicitly so independently imported clips reproduce the same identity. The scene API publishes the flattened nodes with their parent and skin index; the rig importer reads rest transforms and skins from the parsed glTF directly.

**Implemented.** `nt_builder_import_rig` builds the rig of §3.1 out of one skin. `nt_builder_add_scene_skinned_mesh` exports one primitive: it reads every `JOINTS_n/WEIGHTS_n` pair through sparse-capable unpacking, checks each source accessor's type (`JOINTS_n` VEC4 unsigned byte or short and not normalized, `WEIGHTS_n` VEC4 float or normalized unsigned byte/short — the integer read returns 0 on a float accessor, so the type is checked before anything reads it), keeps the four largest influences per vertex with ties broken towards the lower joint index, gates each vertex on `Σ dropped / Σ source ≤ skin_drop_tolerance` (default 0.02) after a logged diagnostic, logs one warning per primitive naming how many vertices were reduced and the largest dropped ratio, and renormalizes what it kept. `nt_builder_add_scene_skin_binding` writes the binding from the same rig: inverse binds from the skin's accessor (identity when it has none), the rig's palette remap, `reach` measured over the *source* influences of every primitive of every node that uses the skin — so the order in which meshes and binding are exported does not matter — and `any_pose_radius` from the rest hierarchy per §14. Rejected as content errors, each a diagnostic then `NT_BUILD_ASSERT`: a glTF that fails `cgltf_validate` (accessors outside their buffer views, mismatched attribute counts, out-of-range indices or sparse indices, a parent cycle — checked in parse, before any reader trusts the data); an unpaired or non-consecutive influence set; a vertex that weights one palette entry twice, or carries a negative, non-finite or all-zero weight set, or, on a lane with a non-zero weight, addresses an index at or above the palette count; an `inverseBindMatrices` accessor that is not MAT4 float over at least the palette, or holds a non-finite element or a bottom row other than `(0, 0, 0, 1)`; morph targets on a skinned primitive (they move the bound vertices `reach` is measured over); a rig node with a non-finite rest translation or scale, or a non-unit rest rotation; a `matrix` rig node that is non-finite, non-affine or not T·R·S (§3.1), or that carries translation/rotation/scale next to its matrix; a skin that lists one node twice; a joint chain deeper than 256; an unnamed or empty-named rig node (a joint id is the hash of its name); two rig nodes whose names hash to one joint id; joints spanning several scene roots; a skin joint outside the `skeleton_root` cut; a skin without joints, or a rig above 65535 joints; a skinned primitive without a VEC3 `POSITION`; a UINT8 `JOINTS` layout over a palette above 256 entries; a skinned vertex at a non-finite distance from its joint (`inverse_bind·POSITION`); a skinned-mesh export of a mesh no node instantiates with the rig's skin; and a binding export for a skin no node uses (its reach would be zero).

**Pending** (#512): clip import — resampling CUBICSPLINE and the authored keys onto a profile grid, and the gate of runtime nlerp against the source quaternion interpolation at keys and interior samples (quarter points), refining the resampling within the profile before failing.

## 17. Modules and composition checks

- `skeletal` (`engine/skeletal`, one module `nt_skeletal`): `nt_skeletal.h` — pose ABI, skeleton view, 3×4 kernels, FK, sockets, rig identity, skin binding view and palette build, plus sample, the object curve and `tracks_advance`; mix/override/additive land with their issue. A radius bounds helper (§14) is added with its first consumer.
- `skeletal_bank`: bank init/bake/lookup over `skeletal` + gfx interface.
- `skeletal_gpu`: staging/upload, DeformationBinding; depends on the gfx interface.
- `skinned_mesh_renderer` + `skin_comp`.
- `skeletal_assets` (`engine/skeletal_assets`, one module `nt_skeletal_assets`): the optional NSKL/NSKN/NANM adapters (§15) over `skeletal` + `resource`. `skeletal` itself links neither, so a headless CPU build links no pack code. `skeletal_ik`, `skeletal_retarget` as extensions. Track assign/release/crossfade helpers live in the showcase.
- Kernels retain no inputs and keep no mutable global evaluation state; concurrent calls (if a game ever schedules them) need immutable shared inputs, disjoint outputs/workspaces and caller synchronization — no engine job system, staging reservation or atomics exist or are planned.

v1 composition checks without LTO (#488): no animation (no animation symbols at all, `skeletal_assets` included); headless CPU without gfx (`skeletal` only, no resource symbol through it); full v1. Extensions add CPU + IK (#481) and CPU + retarget (#483). A bank-only character allocates no PoseInstance.

## 18. Verification

Math (helpers, roots, non-identity binds, `bind·inverse_bind = I` at the bind pose, two bindings one pose; CPU vs GPU agreement; ABI alignment under `-fsanitize=alignment`); clips (absent/constant channels, STEP, cubic resampling (with the importer), reverse, seek, duration 0, multi-loop, non-binary grids and durations, object curve, structural payload rejections: exact size, write indices, key partition); composition (`q/−q` for every input incl. the first, exact zero-dot pair, `±170°`, zero totals, all-zero joint weights, asymmetric parent/child weights, non-unit scale, override strength under changing mix gains, interruption at fixed capacity with constant memory, object-curve blend); bank (FP16/32, row-crossing and seam pairs, STEP segments, non-integral `duration·fps`, `3·P ≤ width`, certificate asserts, FP16 tolerance assert, matrices equal to the CPU path at frame times, restore from CPU texels after clip unload); render (mixed static/skinned order, A/B/A deformation textures with full sampler reapply, per-instance frames in one batch, degenerate normals finite, one epoch across passes, stale epoch asserts); lifetimes (swap-and-pop, slot reuse, unload/reload, loss/restore); memory/linking (no hot heap, asserted overflow, composition symbol checks); performance (separate sample/mix/FK/palette/upload/bake timings, draws, bytes, resident memory, `.wasm.gz`; comparisons only on identical content and quality).

**Benchmark workload.** Performance numbers come from one fixed workload: joints J ∈ {30, 60, 100}, characters C ∈ {1, 100, 1000}, tracks T ∈ {1, 2, 4}, a logical frame of `sample → mix → FK`. The layout microbenchmark times each stage in a separate repeated batch after a full-frame warm-up; its total is the sum of the three stage medians, not a separately measured full-frame time. The rig is a synthetic chain with branches (`parent[j] = j − 1` for 80 % of joints, otherwise a random earlier joint from a fixed LCG seed, relabelled to preorder) and the keyframes are deterministic random unit quaternions and translations. Poses for all characters are one contiguous C × J buffer per layout, so cache behaviour across characters is part of the measurement; every buffer is allocated once and reused. The reported metric is ns per skeleton joint per stage (the C × J joints of one frame, so a stage's cost scales visibly with T), median of 5 repetitions after a warm-up. `tools/research/skeletal_layout/` runs it today on synthetic kernels over three pose storages (AoS 40 B, padded AoS 48 B, ten-channel SoA) to justify the initial ABI; #487 measures the real kernels on the same workload and #492 revisits the layout with SIMD.

## 19. Outside v1

Continuous bridge, IK, ragdoll, retargeting (builder and runtime), Q16, block codec, SIMD, root-motion/event traversal, evaluation-rate LOD — extensions with issues. Without issues: catalogs/AnimationSet, serialized banks in packs, morph targets (a future authored morph path applies mesh-local deltas before skinning and the world transform; representation deferred), mirroring, unskinned node animation, N-way mixing inside baked playback, dual quaternions, compute skinning, per-bone envelopes, a normal-angle bake gate (not transferable across bindings), a caller-owned sampling cache (only if #487 shows sampling dominates; #492), FBX/DAE builder adapters into the same canonical rig/clip/skin data, runtime threading, `transform_comp` inheritance, joint-set/influence-count LOD (only after #494 and profiling); no LOD enum, seam or automatic distance policy.

