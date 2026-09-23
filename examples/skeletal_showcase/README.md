# Skeletal showcase

`Skeleton & Pose` edits joint offsets on the code-defined humanoid and imported
Khronos rigs. `Skinned Meshes` plays their imported clips on textured meshes with
explicit palette upload. It includes the former Playback controls and optional
bones; there is no separate Playback scene, IK, mixer or animation library.

Build and run the native example with:

```bash
cmake --preset native-debug
cmake --build --preset native-debug --target skeletal_showcase
cd build/examples/skeletal_showcase/native-debug && ./skeletal_showcase.exe
```

The packs are loaded from `assets/skeletal_showcase.ntpack` and
`assets/skeletal_showcase_clips.ntpack` relative to the working directory, so
run the executable from its own build directory.

The WASM shell is built after the native packs exist:

```bash
cmake --preset wasm-debug
cmake --build --preset wasm-debug --target skeletal_showcase
```

The scene list contains `Skeleton & Pose`, `Skinned Meshes` and `Order & Instancing`. The top
selector selects a scene; the right panel holds the scene's controls. Stage
orbit is owned by the shell: drag with the left mouse button inside the stage,
drag with the right mouse button to pan, and use the wheel to zoom. UI controls
do not move the camera.

## Packs

`build_packs.c` builds two packs. The rig pack, `skeletal_showcase.ntpack`,
holds the sprite and text shaders, the UI atlas, the font, and for each of the
two Khronos rigs its NSKL skeleton, NSKN skin binding and skinned MESH. The
clips pack, `skeletal_showcase_clips.ntpack`, holds the four clips (Fox
`Survey`, `Walk`, `Run` and the CesiumMan walk). Both packs mount at init, and
the Skinned Meshes scene plays the clips of the second pack on the skeletons of the
first, which is how the showcase exercises "a clip from another pack on an
already-loaded skeleton". `raw/README.md` lists the raw inputs and their
attribution.

Each glb is parsed once; its rig is imported with the default selection (skin 0,
no cut) and fed to both builder contexts. The skinned mesh is primitive 0 of the
mesh of the node that instantiates skin 0, exported with `POSITION`, `JOINTS`
and `WEIGHTS`, float32 `TEXCOORD_0` and, for CesiumMan only, `NORMAL` (the Fox primitive has none).
Base-color textures come from each primitive material and ship as RAW with mipmaps.
Both CPU and GPU use the same textured unlit shading; normals are not used. Every clip is sampled at 24 fps, and the builder prints
one report line per clip:

```
clip skeletal_showcase/fox/run.nanm: sample_count=29 duration=1.166667 cpu_error_lin=0.116841 (t=0.991667 joint=12) cpu_error_t=1.776836 (t=0.866667 joint=10)
```

`cpu_error_lin` is the largest Frobenius distance between the 3x3 parts of a
joint's model matrix sampled at runtime and the source curve, `cpu_error_t` the
largest translation distance in scene units, each with the time and joint it
was found at. Fox Survey, Walk and CesiumMan are whole frames at 24 fps and
report small errors; Fox Run is authored at 27.8 frames, so the builder logs a
`not a whole number` warning, ships 28 frames, and its report shows the larger
interpolation error the skeletal spec documents. Both are expected. The
generated headers in `generated/` (one per pack, merged into
`skeletal_showcase_assets.h`) are committed and deterministic.

## Skeleton & Pose

The right panel selects joints and edits their local offsets. The stage shows
the selected subtree in amber.

The UI font is the existing `examples/ui_showcase/raw/font.ttf` Roboto Regular
file, distributed under the Apache License 2.0 by Google. Its original
metadata is retained in the source file; see the Roboto project license at
https://github.com/googlefonts/roboto/blob/main/LICENSE. The slider and
checkbox sprites (`bar_track.png`, `bar_fill_smooth.png`, `bar_thumb.png`,
`box_off.png`, `checkmark.png`) come from the same directory. The rig is
original code-defined data in `main.c`, with 21 joints and a symmetric rest
pose. `Test` is a reproducible asymmetric pose for checking forward-kinematic
propagation.

The `Rig` dropdown switches the scene between `Humanoid` (the code-defined rig
above), `Fox` and `CesiumMan`. The last two are the Khronos glTF sample
assets in `raw/` (see `raw/README.md` and the `*-LICENSE.txt` files for their
CC-BY 4.0 attribution); `build_packs.c` imports each one with the default rig
selection (skin 0, no cut) into an NSKL skeleton, and this scene shows only its
rest pose; the clips play in `Skinned Meshes`. Imported joints carry
`joint_id` hashes but no names, so their list reads `j00 C14E6FD1`. Every rig
is framed the same way: after FK at rest the scene computes the joint
centroid and extent, aims the camera at the centroid, and scales the camera
distance, near/far planes, bone width, joint spheres, axis length and the grid
cell by `extent / humanoid extent` (Fox is authored in centimetres, CesiumMan
is about 1.5 units tall). While an imported skeleton is not ready the stage
stays empty and the panel shows `loading...`. Joints whose whole ancestor
chain, themselves included, has zero rest translation draw as thin grey
scaffolding, so the link up to the first translated joint does not read as a
limb. NSKL does not mark exporter wrappers, so besides CesiumMan
`Z_UP`/`Armature` and Fox `root` this also covers Fox's skin joints
`_rootJoint` and `b_Root_00`, which rest at the origin. `Test` on an imported
rig bends every third joint outside that scaffolding about Z. The scene holds
at most 32 joints; the pack builder asserts it. Visual QA: CesiumMan stands
upright, Fox faces along its authored axis.

The properties panel shows the selected joint's local offset in degrees and its
resulting model-space 3x4 matrix. Offsets are composed in the fixed order
`q_offset = qz * qy * qx`, then `q_local = q_offset * q_rest`; the displayed
matrix comes from the full `nt_skeletal_fk` pass. `Axes on` draws local X/Y/Z
axes in RGB.

## Skinned Meshes

The scene owns one character track. Every frame the example refetches the
selected skeleton and clip views right after `resource_step`, before the controls
and the scene read them; the scene then writes `track.speed` and `track.flags` from the controls, calls
`nt_skeletal_tracks_advance` with the frame `dt`, samples the clip at
`track.time` with `nt_skeletal_sample` and runs `nt_skeletal_fk` (over the
rest pose when no clip is selected). The stage draws the mesh with depth test
and depth writes. `Bones` enables the bone overlay; the humanoid also shows a hand marker.

Controls, top to bottom:

- `Character`: `Humanoid`, `Fox` or `CesiumMan`. Switching deselects the clip
  and refits the camera. Clip controls wait for the skeleton to load.
- `Clip`: every loaded clip whose `rig_compat_id` matches the selected
  skeleton, so CesiumMan lists its one clip and the Fox skeleton takes
  `Fox Survey`, `Fox Walk` and `Fox Run` in any order without being reloaded.
  Selecting a clip resets the track to time 0 and logs its name, duration and
  sample count once.
- `Play`/`Pause`: pause is `speed = 0` on the track, no special case.
- `Step`: pauses and seeks to the adjacent sample (backwards with `Reverse`)
  in frame arithmetic: with `Loop` on the index wraps, so a step at the last
  reachable sample lands on 0; with `Loop` off it clamps at the ends. The seek
  is a `time` write, not an `advance`: advancing by one grid interval lands one
  ulp short of the wrap point for many clip lengths.
- `Time`: a slider over the sample indices; a drag pauses and writes the clock
  as `frame * step`, so a seek lands exactly on the stored sample. With `Loop`
  on the last index is the sample before `duration`, the wrap point. Disabled
  without a scrubbable range (no clip, or a clip with a single reachable
  sample).
- `Speed x0.00..2.00`: the speed magnitude, step 0.05.
- `Loop`: sets `NT_SKELETAL_TRACK_LOOPING`; off, the track clamps to
  `[0, duration]` and holds.
- `Reverse`: negates the speed. Pause, step, loop and clamp all follow the
  sign.
- The status line `<clip>  time / duration s` is the read-out for checks; `no
  clip` while none is selected.

`Reset` and `R` deselect the clip and restore speed x1, looping, playing and
forward. The `Step` and `Time` grid comes from the clip itself,
`duration / (sample_count - 1)`, not from a constant.

### CPU comparison

`CPU reference (pauses)` freezes the player and draws the CPU-deformed mesh in
place of GPU skinning, with the same pose, camera, texture, sampler and depth
settings. The visual check is that nothing changes when toggled. `Step`, the
time slider and clip selection move the reference; disable it to enable Play
again. Reset also remains paused while the reference is enabled.

The MESH type registration's `on_post_resolve` copies each imported mesh when
its provider publishes, decodes SOA/index compression, and keeps its own source
bytes independent of the pack and the GPU. Four packed weight bytes divided by
255 feed a scalar CPU deformation. Its RAW result is rasterized by the static
mesh renderer and survives context loss in CPU memory. The reference is rebuilt
only when the model pose differs from the one it was built from. Activated
meshes are immutable, so each rebuild re-activates the reference mesh; scrubbing
pays that per frame until #542 adds a vertex update. This is a verification mode
of the example, not a general CPU renderer or a required engine animation path.

### Body and clothes

`Humanoid motion` is an asymmetric, code-authored two-second clip. White body
and blue shirt have mixed-weight vertices. The shirt vertices use a different
mesh space: `C` scales by 1.25 and translates by (0.3, 0.25, -0.2).
Its vertices contain `inverse(C) * authored_position` and its binding contains
`IB_body * C`, so the two transforms cancel before deformation. Both meshes
receive the same nonidentity entity world transform, applied once after skinning.

Shared pose evaluates one track but builds two palettes because the inverse
binds differ. `Independent clothes` samples the clip for the clothes 0.45 s ahead
of the body's clock (wrapped or clamped like the track); it adds no second track. These are skinned garments,
without cloth physics. `Bones & marker` transforms a point (0.2, 0.1, 0.15) on the
left hand through `E * G[hand]`; there is no socket object or attachment API.

## Order & Instancing

The instance slider selects an active prefix of 256 preallocated entities and
tracks. Inactive tracks pause; changing the count preserves their times.
`Shared binding` evaluates one pose and copies its deformation binding to all
active entities. Otherwise each active track produces its own palette. Tracks
keep advancing in either mode. One workspace is reused for sample/FK.

The game submits items in the displayed order without sorting. Alternating
meshes uses body and a shirt mesh in the body's authored space under the same
inverse binds; alternating materials uses two actual material handles.

| Mode | Pass 1 draws | Pass 2 draws |
| --- | --- | --- |
| Grouped | 1 | 1 |
| Alternating meshes | N | N |
| Alternating materials | N | 1 |

`Two passes` draws the same poses/world transforms into two viewports within
one gfx pass. Pass 2 assigns one tint material and rebuilds the batch keys.
It does not test transitions between two `nt_gfx_begin_pass` calls. Both draws
reuse one palette upload. Per-pass counters show measured draw calls and
instances plus the expected count for that completed frame, excluding UI.

The palette texture is 96 by 256 RGBA32F texels. Each 21-joint palette occupies
one row: 1008 useful bytes, 1536 uploaded bytes. Shared mode builds one palette;
independent mode builds N. Both views together draw 2N instances.

## Shell

The Controls panel header contains the common `Reset` button; `Reset` and `R`
reset the active scene and shared camera, re-applying the fit of the active
rig. Switching scenes resets the camera and refits the incoming rig, and the
Controls visibility setting is preserved.

To add a scene, define one typed state block and its callbacks in the scene
region of `main.c`, then append one descriptor to `s_scene_registry` with its
title and reset/update/cancel_input/declare_controls/draw callbacks. The
shell calls callbacks only for the active scene, preserves state while a scene
is inactive, closes scene input on transitions, and owns the stage camera,
the pending camera fit and panel visibility. Keep scene widget IDs under a
scene-specific prefix and keep all state fixed-size and example-local.

Source: [examples/skeletal_showcase/main.c](main.c).

## Visual QA

Skeleton & Pose: start the native executable, confirm the T-pose and ground
grid, select `left_forearm`, rotate it in the properties panel, then compare
the opposite arm and legs. Try `Test`, `Rest`, the `Controls` toggle, `Reset`,
and stage orbit/pan/zoom. Then switch `Rig` to `CesiumMan` and `Fox`, orbit
each one, and check that the framing, joint size and grid match the
humanoid's.

Playback: switch the scene to `Skinned Meshes`; Fox stands at rest and the status
line reads `no clip`. Open `Clip`: the three Fox entries are listed and
`CesiumMan` is not. Select `Fox Walk` and confirm the legs cycle and the
status time wraps at the duration; select `Fox Run` and confirm the skeleton
is not reloaded (the framing does not jump) and the log shows the new clip
line. Uncheck `Loop` and confirm the time clamps at the duration and the pose
holds; check `Reverse` and confirm it runs back to 0 and holds. Pause, press
`Step` a few times and confirm the time moves by one sample per press; drag `Time`
and confirm the pose follows. Switch `Character` to `CesiumMan`: `Clip` lists
only `CesiumMan`; select it and confirm the walk plays upright.


### Capture and compare

Enable the existing DevAPI groups when configuring a capture build:

```bash
cmake --preset native-debug -DNT_DEVAPI_ENABLED=ON -DNT_DEVAPI_GROUP_CORE=ON -DNT_DEVAPI_GROUP_DISCOVERY=ON -DNT_DEVAPI_GROUP_CAPTURE=ON -DNT_DEVAPI_GROUP_UI=ON
cmake --build --preset native-debug --target skeletal_showcase
```

Run from `build/examples/skeletal_showcase/native-debug`, select Skinned Meshes,
choose the pose, enable CPU reference, disable Bones and keep Controls visible for
the report. Keep the engine loop running; only the character player pauses. From
the repository root, with Pillow and NumPy installed:

```bash
python -m tools.devapi.scenarios.skeletal_compare --output build/skeletal-compare
```

This uses `SocketTransport` and `DevApiClient.capture_frame(scale=1)`: it
captures the stage with the CPU reference, clicks `CPU reference` off, captures
GPU skinning at the same pose and clicks it back on. It saves both stage images
and a JSON report, and exits with an error if the pose moved between the
captures or any-channel delta >2 affects more than 0.5% of the union of visible
model pixels. Empty coverage fails. The background must stay uniform and clear;
UI, ground and bones must be outside the compared area. Matching images are
expected: CPU computes positions while the static renderer still rasterizes them
on the GPU.

After activating the pinned SDK, build the WebGL2 capture variant:

```bash
cmake --preset wasm-debug -DNT_DEVAPI_ENABLED=ON -DNT_DEVAPI_GROUP_CORE=ON -DNT_DEVAPI_GROUP_DISCOVERY=ON -DNT_DEVAPI_GROUP_CAPTURE=ON -DNT_DEVAPI_GROUP_UI=ON
cmake --build --preset wasm-debug --target skeletal_showcase
python -m http.server 8125 --bind 127.0.0.1 --directory build/examples/skeletal_showcase/wasm-debug
```

Open `http://127.0.0.1:8125`, select the same pose, then capture once with and
once without `CPU reference` through the existing browser bridge. Capture replies are deferred; wait for the actual PNG:

```js
const command = {method: "capture.frame", request_id: 1, params: {scale: 1}};
let reply = window.__devapi.submit(JSON.stringify(command));
const deadline = performance.now() + 5000;
while (!reply && performance.now() < deadline) {
    await new Promise(requestAnimationFrame);
    reply = window.__devapi.poll();
}
if (!reply) throw new Error("Capture timed out");
const response = JSON.parse(reply);
if (!response.ok) throw new Error(JSON.stringify(response.error));
const image = new Image();
image.src = "data:image/png;base64," + response.result.data;
document.body.append(image);
```

The Python comparator's `compare(client, output, backend)` also accepts a
`DevApiClient(PlaywrightTransport(page))` for automated browser captures.
Context-loss restoration is reviewed in code; a full loss/retry run remains
outside this showcase's evidence.


Verification on the implementation branch covered all four imported clips at
zero, an internal sample, the endpoint and between samples, plus endpoint-to-loop
normalization. Native and Chromium WebGL2 (ANGLE SwiftShader) also compared the
procedural body/clothes with shared and independent poses under nonidentity E/C.
All comparisons met the 0.5% coverage threshold. Both backends measured the
ordering table for N=1, 2, 17 and 256 with Shared binding off/on. This verifies
unlit position deformation and batching; it does not verify lighting normals,
cloth simulation, performance budgets or the full context-loss/retry sequence.
After the switch from a split view to the `CPU reference` toggle, the native
toggle captures were rerun (Fox Walk, CesiumMan, humanoid shared and independent,
stepped while paused): 0 mismatched pixels each. The WebGL2 captures predate the
toggle and were not rerun.
