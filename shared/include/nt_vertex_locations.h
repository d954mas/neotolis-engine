#ifndef NT_VERTEX_LOCATIONS_H
#define NT_VERTEX_LOCATIONS_H

/* The engine's vertex attribute location convention: builder stream layouts set NtStreamLayout.location
 * from these and shaders declare the same slots, so a mesh stream feeds the attribute that reads it. */
#define NT_LOC_POSITION 0
#define NT_LOC_NORMAL 1
#define NT_LOC_COLOR 2
#define NT_LOC_UV0 3
#define NT_LOC_TANGENT 4
#define NT_LOC_UV1 5
#define NT_LOC_JOINTS 6
#define NT_LOC_WEIGHTS 7
#define NT_LOC_VERTEX_END 8 /* mesh streams use locations below this */

/* Instance head shared by the mesh and skinned instance layouts; the skinned one adds two. */
#define NT_INST_LOC_WORLD_ROW0 8
#define NT_INST_LOC_WORLD_ROW1 9
#define NT_INST_LOC_WORLD_ROW2 10
#define NT_INST_LOC_COLOR 11
#define NT_INST_LOC_SKIN_FRAMES 12
#define NT_INST_LOC_SKIN_ALPHA 13

#define NT_LOC_STR_(x) #x
#define NT_LOC_STR(x) NT_LOC_STR_(x)
#define NT_LOC_GLSL_DEFINE(name) "#define " #name " " NT_LOC_STR(name) "\n"

/* Source of the builder's virtual include "nt/locations.glsl": the macros above as GLSL defines.
 * The GLSL guard, not the include resolver, dedupes it, so an inactive #if branch cannot hide it. */
#define NT_LOCATIONS_GLSL_INCLUDE "nt/locations.glsl"
#define NT_LOCATIONS_GLSL                                                                                                                                                                              \
    "#ifndef NT_LOC_POSITION\n" NT_LOC_GLSL_DEFINE(NT_LOC_POSITION) NT_LOC_GLSL_DEFINE(NT_LOC_NORMAL) NT_LOC_GLSL_DEFINE(NT_LOC_COLOR) NT_LOC_GLSL_DEFINE(NT_LOC_UV0)                                  \
        NT_LOC_GLSL_DEFINE(NT_LOC_TANGENT) NT_LOC_GLSL_DEFINE(NT_LOC_UV1) NT_LOC_GLSL_DEFINE(NT_LOC_JOINTS) NT_LOC_GLSL_DEFINE(NT_LOC_WEIGHTS) NT_LOC_GLSL_DEFINE(NT_INST_LOC_WORLD_ROW0)              \
            NT_LOC_GLSL_DEFINE(NT_INST_LOC_WORLD_ROW1) NT_LOC_GLSL_DEFINE(NT_INST_LOC_WORLD_ROW2) NT_LOC_GLSL_DEFINE(NT_INST_LOC_COLOR) NT_LOC_GLSL_DEFINE(NT_INST_LOC_SKIN_FRAMES)                    \
                NT_LOC_GLSL_DEFINE(NT_INST_LOC_SKIN_ALPHA) "#endif\n"

#endif /* NT_VERTEX_LOCATIONS_H */
