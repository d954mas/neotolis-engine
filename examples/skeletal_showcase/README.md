# Skeleton & Pose

`skeletal_showcase` currently ships one manual pose scene. It demonstrates the existing
`nt_skeletal_fk` kernel with an original code-defined humanoid in a symmetric
T-pose and with two imported Khronos rigs at rest. Bones and joints use the
existing shape renderer; there is no imported mesh, skinning, clip loader, IK,
mixer, or animation library.

Build and run the native example with:

```bash
cmake --preset native-debug
cmake --build --preset native-debug --target skeletal_showcase
cd build/examples/skeletal_showcase/native-debug && ./skeletal_showcase.exe
```

The pack is loaded from `assets/skeletal_showcase.ntpack` relative to the
working directory, so run the executable from its own build directory.

The WASM shell is built after the native pack exists:

```bash
cmake --preset wasm-debug
cmake --build --preset wasm-debug --target skeletal_showcase
```

The scene list contains one entry, `Skeleton & Pose`. The top selector selects a
scene; the right panel selects joints and edits their local offsets. The stage
shows the selected subtree in amber. Stage orbit is owned by the shell: drag
with the left mouse button inside the stage, drag with the right mouse button
to pan, and use the wheel to zoom. UI controls do not move the camera.

The UI font is the existing `examples/ui_showcase/raw/font.ttf` Roboto Regular
file, distributed under the Apache License 2.0 by Google. Its original
metadata is retained in the source file; see the Roboto project license at
https://github.com/googlefonts/roboto/blob/main/LICENSE. The rig is original
code-defined data in `main.c`, with 21 joints and a symmetric rest pose.
`Test` is a reproducible asymmetric pose for checking forward-kinematic
propagation.

The `Rig` dropdown switches the scene between `Humanoid` (the code-defined rig
above), `Fox` and `CesiumMan`. The last two are the Khronos glTF sample
assets in `raw/` (see `raw/README.md` and the `*-LICENSE.txt` files for their
CC-BY 4.0 attribution); `build_packs.c` imports each one with the default rig
selection (skin 0, no cut) into an NSKL skeleton, and the scene shows only its
rest pose: clips and skinned meshes are #512 and #513. Imported joints carry
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

Source: [examples/skeletal_showcase/main.c](main.c).

The properties panel shows the selected joint's local offset in degrees and its
resulting model-space 3x4 matrix. Offsets are composed in the fixed order
`q_offset = qz * qy * qx`, then `q_local = q_offset * q_rest`; the displayed
matrix comes from the full `nt_skeletal_fk` pass. `Axes on` draws local X/Y/Z
axes in RGB. The Controls panel header contains the common `Reset` button;
`Reset` and `R` reset the active scene and shared camera, re-applying the
fit of the active rig. Entering a scene for
the first time initializes it, switching scenes resets the camera, and the
Controls visibility setting is preserved.

To add a scene, define one typed state block and its callbacks in the scene
region of `main.c`, then append one descriptor to `s_scene_registry` with its
title, description, source, and enter/leave/reset/update/cancel_input/
declare_controls/draw callbacks. The
shell calls callbacks only for the active scene, preserves state while a scene
is inactive, calls `leave` before `enter`, closes scene input on transitions,
and owns the stage camera and panel visibility. Keep scene widget IDs under a
scene-specific prefix and keep all state fixed-size and example-local.

Visual QA: start the native executable, confirm the T-pose and ground grid,
select `left_forearm`, rotate it in the properties panel, then compare the
opposite arm and legs. Try `Test`, `Rest`, the `Controls` toggle, `Reset`, and
stage orbit/pan/zoom. Then switch `Rig` to `CesiumMan` and `Fox`, orbit each
one, and check that the framing, joint size and grid match the humanoid's.
