precision highp float;

in vec2 v_uv;
in vec3 v_world_normal;
in vec3 v_world_pos;
in vec4 v_color;

out vec4 frag_color;

uniform sampler2D u_diffuse;

#include "common/globals.glsl"
#include "common/lighting.glsl"

/* Alpha cutoff passed as material param (.x = threshold, .yzw unused) */
uniform vec4 u_alpha_cutoff;

void main() {
    vec4 albedo = texture(u_diffuse, v_uv) * v_color;
    if (albedo.a < u_alpha_cutoff.x) discard;
    vec3 N = normalize(v_world_normal);
    vec3 L = normalize(light_dir.xyz);
    float NdotL = max(dot(N, L), 0.0);
    vec3 diffuse = albedo.rgb * light_color.rgb * light_color.a * NdotL;
    vec3 ambient_term = albedo.rgb * ambient.rgb * ambient.a;
    frag_color = vec4(ambient_term + diffuse, 1.0);
}
