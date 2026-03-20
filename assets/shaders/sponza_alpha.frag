precision mediump float;

in vec2 v_uv;
in vec3 v_world_normal;
in vec3 v_world_pos;

out vec4 frag_color;

uniform sampler2D u_diffuse;

layout(std140) uniform Globals {
    mat4 view_proj; mat4 view; mat4 proj;
    vec4 camera_pos; vec4 time; vec4 resolution; vec4 near_far;
};

layout(std140) uniform Lighting {
    vec4 light_dir;
    vec4 light_color;
    vec4 ambient;
};

/* Alpha cutoff passed as material param */
uniform float u_alpha_cutoff;

void main() {
    vec4 albedo = texture(u_diffuse, v_uv);
    if (albedo.a < 0.5) discard;
    vec3 N = normalize(v_world_normal);
    vec3 L = normalize(light_dir.xyz);
    float NdotL = max(dot(N, L), 0.0);
    vec3 diffuse = albedo.rgb * light_color.rgb * light_color.a * NdotL;
    vec3 ambient_term = albedo.rgb * ambient.rgb * ambient.a;
    frag_color = vec4(ambient_term + diffuse, 1.0);
}
