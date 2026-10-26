precision highp float;

in vec2 v_uv;
in vec3 v_world_normal;

out vec4 frag_color;

uniform sampler2D u_diffuse;

#include "common/globals.glsl"
#include "common/lighting.glsl"
#include "common/color.glsl"

/* glTF baseColorFactor (linear) and alpha cutoff (.x; .yzw unused) */
layout(std140) uniform Material {
    vec4 u_base_color;
    vec4 u_alpha_cutoff;
};

void main() {
    vec4 albedo = texture(u_diffuse, v_uv) * u_base_color;
    if (albedo.a < u_alpha_cutoff.x) discard;
    vec3 N = normalize(v_world_normal);
    vec3 L = normalize(light_dir.xyz);
    float NdotL = max(dot(N, L), 0.0);
    vec3 diffuse = albedo.rgb * light_color.rgb * light_color.a * NdotL;
    vec3 ambient_term = albedo.rgb * ambient.rgb * ambient.a;
    frag_color = vec4(nt_linear_to_srgb(ambient_term + diffuse), 1.0);
}
