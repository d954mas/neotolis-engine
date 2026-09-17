# Skeleton & Pose raw inputs

The scene geometry is procedural. The pack uses a generated white pixel plus
the slider sprites `bar_track.png`, `bar_fill_smooth.png`, and `bar_thumb.png`
from `examples/ui_showcase/raw`, and its permitted Roboto Regular font from the
same directory under Apache License 2.0.

`Fox.glb` is the "Fox" model of the Khronos glTF-Sample-Assets repository: a
24-joint quadruped rig with three animation cycles and a single skinned
primitive without normals. Its mesh is CC0 (PixelMannen) while its rigging,
animation and glTF conversion are CC-BY 4.0 (tomkranis, @AsoboStudio and
@scurest), so the file is redistributed under CC-BY 4.0; `Fox-LICENSE.txt`
holds the full attribution. `tests/unit/test_builder_rig.c` imports its rig,
skin binding and skinned mesh, which is what keeps the importer honest against
an asset nobody here authored.

`CesiumMan.glb` is the "Cesium Man" model of the same repository: a 19-joint
humanoid under two `matrix` wrapper nodes, one animation and one skinned
primitive with normals. It is CC-BY 4.0 (Cesium), and the Cesium logo on its
texture is a trademark used by permission; `CesiumMan-LICENSE.txt` holds the
full attribution and the mark's terms. The same builder test imports it, so the
matrix-decomposition rule of the rig importer is exercised on real content.

Both files are the upstream binaries, unmodified.
