#pragma once

/* Instance attributes (divisor=1, mat4x3 row layout, set by nt_mesh_renderer) */
#include "nt/locations.glsl"
layout(location = NT_INST_LOC_WORLD_ROW0) in vec4 a_world_row0;
layout(location = NT_INST_LOC_WORLD_ROW1) in vec4 a_world_row1;
layout(location = NT_INST_LOC_WORLD_ROW2) in vec4 a_world_row2;
layout(location = NT_INST_LOC_COLOR) in vec4 a_color;
