#pragma once

/* Per-instance data from the engine's instancing renderers. A flat vec4 array
 * reflects as one active uniform; each payload reads a fixed number of vec4s
 * at nt_instance_index(). The renderer binds 16 KB (WebGL2's guaranteed block
 * size) at slot 15 and sets nt_instance_base per draw. */
layout(std140) uniform NtInstances {
    vec4 nt_instance_data[1024];
};
uniform int nt_instance_base;

int nt_instance_index() {
    return nt_instance_base + gl_InstanceID;
}

mat4 nt_instance_world(vec4 row0, vec4 row1, vec4 row2) {
    return mat4(
        vec4(row0.x, row1.x, row2.x, 0.0),
        vec4(row0.y, row1.y, row2.y, 0.0),
        vec4(row0.z, row1.z, row2.z, 0.0),
        vec4(row0.w, row1.w, row2.w, 1.0)
    );
}

/* Mesh renderer payload (nt_mesh_instance_t): affine world rows, drawable colour. */
struct nt_instance_t {
    mat4 world;
    vec4 color;
};

nt_instance_t nt_instance() {
    int i = nt_instance_index() * 4;
    return nt_instance_t(nt_instance_world(nt_instance_data[i], nt_instance_data[i + 1], nt_instance_data[i + 2]), nt_instance_data[i + 3]);
}
