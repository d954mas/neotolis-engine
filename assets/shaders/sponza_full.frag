precision highp float;

in vec2 v_uv;
in vec3 v_world_pos;
in mat3 v_tbn;

out vec4 frag_color;

uniform sampler2D u_diffuse;
uniform sampler2D u_normal;
uniform sampler2D u_specular;

#include "common/globals.glsl"
#include "common/lighting.glsl"
#include "common/color.glsl"

/* glTF baseColorFactor, linear like the sRGB-stored texture's samples */
layout(std140) uniform Material { vec4 u_base_color; };

void main() {
    vec4 albedo = texture(u_diffuse, v_uv) * u_base_color;
    vec2 normal_xy = texture(u_normal, v_uv).rg * 2.0 - 1.0;
    vec3 normal_map = vec3(normal_xy, sqrt(max(1.0 - dot(normal_xy, normal_xy), 0.0)));
    float roughness = texture(u_specular, v_uv).g; /* glTF: G=roughness, B=metallic */
    float spec_map = 1.0 - roughness;
    vec3 N = normalize(v_tbn * normal_map);
    vec3 L = normalize(light_dir.xyz);
    vec3 V = normalize(camera_pos.xyz - v_world_pos);
    vec3 H = normalize(L + V);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float spec = pow(NdotH, 32.0) * spec_map;
    vec3 diffuse = albedo.rgb * light_color.rgb * light_color.a * NdotL;
    vec3 specular = light_color.rgb * light_color.a * spec;
    vec3 ambient_term = albedo.rgb * ambient.rgb * ambient.a;
    /* The one linear-to-sRGB conversion; the canvas is not an sRGB framebuffer. */
    frag_color = vec4(nt_linear_to_srgb(ambient_term + diffuse + specular), albedo.a);
}
