# Skeleton & Pose

`skeletal_showcase` currently ships one manual pose scene. It demonstrates the existing
`nt_skeletal_fk` kernel with an original code-defined humanoid in a symmetric
T-pose. Bones and joints use the existing shape renderer; there is no imported
mesh, skinning, clip loader, IK, mixer, or animation library.

Build and run the native example with:

```bash
cmake --preset native-debug
cmake --build --preset native-debug --target skeletal_showcase
build/examples/skeletal_showcase/native-debug/skeletal_showcase.exe
```

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

Source: [examples/skeletal_showcase/main.c](main.c).

The properties panel shows the selected joint's local offset in degrees and its
resulting model-space 3x4 matrix. Offsets are composed in the fixed order
`q_offset = qz * qy * qx`, then `q_local = q_offset * q_rest`; the displayed
matrix comes from the full `nt_skeletal_fk` pass. `Axes on` draws local X/Y/Z
axes in RGB. `Reset` and `R` reset the active scene and shared camera; entering
a scene for the first time initializes it, switching scenes resets the camera,
and the Controls visibility setting is preserved.

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
stage orbit/pan/zoom.
