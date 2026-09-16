# Skeleton & Pose

`skeletal_showcase` is a single manual pose scene. It demonstrates the existing
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

The scene list contains one entry, `Skeleton & Pose`. The left list selects a
joint; the stage shows the selected subtree in amber. The right panel reports
the selected joint and its model-space position. Stage orbit is owned by the
application: drag with the left mouse button inside the stage, drag with the
right mouse button to pan, use the wheel to zoom, and press `R` to reset the
camera and pose. UI controls do not move the camera.

The UI font is the existing `examples/ui_showcase/raw/font.ttf` Roboto Regular
file, distributed under the Apache License 2.0 by Google. Its original
metadata is retained in the source file; see the Roboto project license at
https://github.com/googlefonts/roboto/blob/main/LICENSE. The rig is original
code-defined data in `main.c`, with 21 joints and a symmetric rest pose.
`Test pose` is a reproducible asymmetric pose for checking forward-kinematic
propagation.

Visual QA: start the native executable, confirm the T-pose and ground grid,
select `left_forearm`, rotate it in the properties panel, then compare the
opposite arm and legs. Try `Test pose`, `Rest pose`, and `Reset scene`; reset
must return the initial selected joint, camera, pose, paused clock, and zero
time. The clock is app time only: this scene has no animation clips.
