#ifndef NT_VERTEX_LOCATIONS_H
#define NT_VERTEX_LOCATIONS_H

/* Default vertex attribute locations for known mesh stream names. The builder writes the
 * resolved location into NtStreamDesc.location; a stream layout may override any of them. */
#define NT_LOC_POSITION 0
#define NT_LOC_NORMAL 1
#define NT_LOC_COLOR 2
#define NT_LOC_UV0 3
#define NT_LOC_TANGENT 4
#define NT_LOC_UV1 5
#define NT_LOC_JOINTS 6
#define NT_LOC_WEIGHTS 7
#define NT_LOC_VERTEX_END 8 /* mesh streams use locations below this */

#define NT_LOC_STR_(x) #x
#define NT_LOC_STR(x) NT_LOC_STR_(x)
#define NT_LOC_GLSL_DEFINE(name) "#define " #name " " NT_LOC_STR(name) "\n"

/* Source of the builder's virtual include "nt/locations.glsl": the macros above as GLSL defines. */
#define NT_LOCATIONS_GLSL_INCLUDE "nt/locations.glsl"
#define NT_LOCATIONS_GLSL                                                                                                                                                                              \
    NT_LOC_GLSL_DEFINE(NT_LOC_POSITION)                                                                                                                                                                \
    NT_LOC_GLSL_DEFINE(NT_LOC_NORMAL)                                                                                                                                                                  \
    NT_LOC_GLSL_DEFINE(NT_LOC_COLOR)                                                                                                                                                                   \
    NT_LOC_GLSL_DEFINE(NT_LOC_UV0)                                                                                                                                                                     \
    NT_LOC_GLSL_DEFINE(NT_LOC_TANGENT)                                                                                                                                                                 \
    NT_LOC_GLSL_DEFINE(NT_LOC_UV1)                                                                                                                                                                     \
    NT_LOC_GLSL_DEFINE(NT_LOC_JOINTS)                                                                                                                                                                  \
    NT_LOC_GLSL_DEFINE(NT_LOC_WEIGHTS)

#endif /* NT_VERTEX_LOCATIONS_H */
