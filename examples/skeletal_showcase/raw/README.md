# Skeletal showcase raw inputs

The scene geometry is procedural. The rig pack (`skeletal_showcase.ntpack`)
holds the sprite and text shaders, a UI atlas made of a generated white pixel,
the slider sprites `bar_track.png`, `bar_fill_smooth.png` and `bar_thumb.png`
and the checkbox art `box_off.png` and `checkmark.png` from
`examples/ui_showcase/raw`, the permitted Roboto Regular font from the same
directory under Apache License 2.0, the skeleton (NSKL), skin binding (NSKN)
and skinned mesh (MESH) of all three rigs. The clips pack
(`skeletal_showcase_clips.ntpack`) holds the three Fox clips (Survey, Walk, Run)
and the CesiumMan and KayKit clips, so the showcase plays clips from a pack
mounted separately from their skeletons. The Khronos clips are sampled at
24 fps: Fox Survey, Walk and CesiumMan are whole frames at
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

Both Khronos files are the upstream binaries, unmodified.

`KayKit_Knight_Mixing.glb` is a derived CC0 copy of `Knight.glb` from KayKit
Adventurers 1.0. Source:
https://github.com/KayKit-Game-Assets/KayKit-Character-Pack-Adventures-1.0 .
The upstream SHA-256 is
`60428e3abc09ba83e595d256e3af8c5c976b46cdae599f0802fc82b4a3445168`;
the derived SHA-256 is
`dcffe6ea63ba68ad17e9b3a82838fac49b3e4159219c5ed0ff629c740f979ec9`.
`KayKit-LICENSE.txt` records its CC0 terms.

The committed derivative is made by `prepare_kaykit_mixing.mjs`: it detaches
the authored `root` from the identity `Rig` wrapper and keeps root translation,
joint rotations and the original six skinned meshes. It removes authored
non-root translations and all scale channels. This explicit content edit makes
the composed-pose bound `any_pose_radius + max(r_root)` valid; the original
asset animates hips, IK controls and small scale noise and cannot use that
formula. The packed clips are Idle, Walking_A, Running_A, Jump_Full_Short,
Unarmed_Melee_Attack_Punch_A, Death_A and Lie_StandUp, sampled at their authored
30 fps. No runtime importer or retargeting is involved.
