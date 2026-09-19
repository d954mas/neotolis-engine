# Skeletal showcase

`skeletal_showcase` ships two scenes over the existing `nt_skeletal` kernels:
`Skeleton & Pose` poses an original code-defined humanoid and two imported
Khronos rigs through `nt_skeletal_fk`, and `Playback` plays the imported glTF
clips of those rigs on one caller-owned track through `nt_skeletal_sample`.
The packs carry the skeleton, skin binding, skinned mesh and clips of both
rigs, but the stage draws bone primitives only, with the existing shape
renderer: the GPU skinning renderer is a later issue, and there is no IK,
mixer or animation library.

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

The scene list contains two entries, `Skeleton & Pose` and `Playback`. The top
selector selects a scene; the right panel holds the scene's controls. Stage
orbit is owned by the shell: drag with the left mouse button inside the stage,
drag with the right mouse button to pan, and use the wheel to zoom. UI controls
do not move the camera.

## Packs

`build_packs.c` builds two packs. The rig pack, `skeletal_showcase.ntpack`,
holds the sprite and text shaders, the UI atlas, the font, and for each of the
two Khronos rigs its NSKL skeleton, NSKN skin binding and skinned MESH, plus the
CesiumMan clip. The clips pack, `skeletal_showcase_clips.ntpack`, holds only the
three Fox clips (`Survey`, `Walk`, `Run`). Both packs mount at init, and the
Playback scene plays the Fox clips of the second pack on the Fox skeleton of
the first, which is how the showcase exercises "a clip from another pack on an
already-loaded skeleton". `raw/README.md` lists the raw inputs and their
attribution.

Each glb is parsed once; its rig is imported with the default selection (skin 0,
no cut) and fed to both builder contexts. The skinned mesh is primitive 0 of the
mesh of the node that instantiates skin 0, exported with `POSITION`, `JOINTS`
and `WEIGHTS` and, for CesiumMan only, `NORMAL` (the Fox primitive has none);
nothing draws it yet. Every clip is sampled at 24 fps, and the builder prints
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
rest pose; the clips play in `Playback`. Imported joints carry
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

## Playback

The scene owns one `nt_skeletal_track_t` and nothing else moves time: every
frame it refetches the selected skeleton and clip views after `resource_step`,
writes `track.speed` and `track.flags` from the controls, calls
`nt_skeletal_tracks_advance` with the frame `dt`, samples the clip at
`track.time` with `nt_skeletal_sample` (or copies the rest pose when no clip
is selected) and runs `nt_skeletal_fk`. The stage draws the pose with the same
bone primitives and framing as `Skeleton & Pose`, without a selected subtree.

Controls, top to bottom:

- `Character`: `Fox` or `CesiumMan` (the humanoid has no clips and is not
  offered). Switching deselects the clip and refits the camera. While the
  skeleton is not ready the panel shows `loading...`.
- `Clip`: every clip whose resource is loaded, from both packs. A clip made for
  another skeleton is listed with the suffix ` (other rig)` and selecting it
  does nothing, so for CesiumMan the three Fox entries are visible but inert
  and the Fox skeleton takes `Fox Survey`, `Fox Walk` and `Fox Run` in any
  order without being reloaded. Selecting a clip resets the track to time 0 and
  logs its name, duration and sample count once. A clip whose resource goes
  away, or whose reloaded view no longer matches the skeleton, is deselected.
- `Play`/`Pause`: pause is `speed = 0` on the track, no special case.
- `Step`: pauses and advances the track by one 1/24 s frame (backwards with
  `Reverse`) through the track's own wrap or clamp, so a step at the end of a
  looping clip wraps and a step at the end of a clamped clip stays.
- `Time`: a slider over `[0, duration]` on the 1/24 s grid; a grid time
  reproduces the stored sample. The clock is written only when the slider
  moves. Disabled without a clip.
- `Speed x0.00..2.00`: the speed magnitude, step 0.05.
- `Loop`: sets `NT_SKELETAL_TRACK_LOOPING`; off, the track clamps to
  `[0, duration]` and holds.
- `Reverse`: negates the speed. Pause, step, loop and clamp all follow the
  sign.
- The status line `<clip>  time / duration s` is the read-out for checks; `no
  clip` while none is selected.

`Reset` and `R` deselect the clip and restore speed x1, looping, playing and
forward. Every clip is sampled at 24 fps, so the `Step` and `Time` grid is the
clip's own.

## Shell

The Controls panel header contains the common `Reset` button; `Reset` and `R`
reset the active scene and shared camera, re-applying the fit of the active
rig. Entering a scene for the first time initializes it, switching scenes
resets the camera, and the Controls visibility setting is preserved.

To add a scene, define one typed state block and its callbacks in the scene
region of `main.c`, then append one descriptor to `s_scene_registry` with its
title, description, source, and enter/leave/reset/update/cancel_input/
declare_controls/draw callbacks. The
shell calls callbacks only for the active scene, preserves state while a scene
is inactive, calls `leave` before `enter`, closes scene input on transitions,
and owns the stage camera and panel visibility. Keep scene widget IDs under a
scene-specific prefix and keep all state fixed-size and example-local.

Source: [examples/skeletal_showcase/main.c](main.c).

## Visual QA

Skeleton & Pose: start the native executable, confirm the T-pose and ground
grid, select `left_forearm`, rotate it in the properties panel, then compare
the opposite arm and legs. Try `Test`, `Rest`, the `Controls` toggle, `Reset`,
and stage orbit/pan/zoom. Then switch `Rig` to `CesiumMan` and `Fox`, orbit
each one, and check that the framing, joint size and grid match the
humanoid's.

Playback: switch the scene to `Playback`; Fox stands at rest and the status
line reads `no clip`. Open `Clip`: all four entries are listed and `CesiumMan`
carries ` (other rig)`. Select `Fox Walk` and confirm the legs cycle and the
status time wraps at the duration; select `Fox Run` and confirm the skeleton
is not reloaded (the framing does not jump) and the log shows the new clip
line. Uncheck `Loop` and confirm the time clamps at the duration and the pose
holds; check `Reverse` and confirm it runs back to 0 and holds. Pause, press
`Step` a few times and confirm the time moves by 1/24 s per press; drag `Time`
and confirm the pose follows. Switch `Character` to `CesiumMan`: the three Fox
entries read ` (other rig)` and selecting one does nothing; select `CesiumMan`
and confirm the walk plays upright.
