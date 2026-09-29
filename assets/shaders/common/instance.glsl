#pragma once

/* Per-instance data from the engine's instancing renderers: a gfx transient
 * RGBA32F texture, NT_GFX_TRANSIENT_TEXTURE_WIDTH (1024) texels per row, one
 * payload after another. The renderer sets nt_instance_base per draw to its
 * run's first instance in the texture. */
uniform highp sampler2D nt_instances;
uniform highp int nt_instance_base;

highp int nt_instance_index() {
    return nt_instance_base + gl_InstanceID;
}

/* Texel k of the current instance's payload of stride texels; a payload may
 * straddle two rows, so each texel is addressed on its own. */
highp vec4 nt_instance_texel(highp int stride, highp int k) {
    highp int i = nt_instance_index() * stride + k;
    return texelFetch(nt_instances, ivec2(i & 1023, i >> 10), 0);
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
    return nt_instance_t(nt_instance_world(nt_instance_texel(4, 0), nt_instance_texel(4, 1), nt_instance_texel(4, 2)), nt_instance_texel(4, 3));
}
