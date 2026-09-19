# Skeletal showcase raw inputs

The scene geometry is procedural. The rig pack (`skeletal_showcase.ntpack`)
holds the sprite and text shaders, a UI atlas made of a generated white pixel,
the slider sprites `bar_track.png`, `bar_fill_smooth.png` and `bar_thumb.png`
and the checkbox art `box_off.png` and `checkmark.png` from
`examples/ui_showcase/raw`, the permitted Roboto Regular font from the same
directory under Apache License 2.0, the skeleton (NSKL), skin binding (NSKN)
and skinned mesh (MESH) of both rigs, and the CesiumMan clip. The clips pack
(`skeletal_showcase_clips.ntpack`) holds only the three Fox clips (Survey, Walk,
Run), so the showcase plays clips from one pack on a skeleton of another. Every
clip is sampled at 24 fps: Fox Survey, Walk and CesiumMan are whole frames at
that rate; Fox Run is authored at 27.8 frames, so the builder warns about the
fractional frame and the report shows the larger interpolation error the
skeletal spec documents.

`Fox.glb` is the "Fox" model of the Khronos glTF-Sample-Assets repository: a
24-joint quadruped rig with three animation cycles and a single skinned
primitive without normals. Its mesh is CC0 (PixelMannen) while its rigging,
animation and glTF conversion are CC-BY 4.0 (tomkranis, @AsoboStudio and
@scurest), so the file is redistributed under CC-BY 4.0; `Fox-LICENSE.txt`
holds the full attribution. `tests/unit/test_builder_rig.c` imports its rig,
skin binding and skinned mesh and `tests/unit/test_builder_clip.c` its
animations, which is what keeps the importers honest against an asset nobody
here authored.

`CesiumMan.glb` is the "Cesium Man" model of the same repository: a 19-joint
humanoid under two `matrix` wrapper nodes, one animation and one skinned
primitive with normals. It is CC-BY 4.0 (Cesium), and the Cesium logo on its
texture is a trademark used by permission; `CesiumMan-LICENSE.txt` holds the
full attribution and the mark's terms. The same builder test imports it, so the
matrix-decomposition rule of the rig importer is exercised on real content.

Both files are the upstream binaries, unmodified.
