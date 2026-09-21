precision highp float;

in vec3 v_position_probe;
in vec3 v_normal_probe;
in vec3 v_tangent_probe;
uniform vec4 u_probe_mode;
out vec4 frag_color;

void main() {
    vec3 value = v_position_probe;
    if (u_probe_mode.x > 1.5) {
        value = v_tangent_probe;
    } else if (u_probe_mode.x > 0.5) {
        value = v_normal_probe;
    }
    frag_color = vec4(value, 1.0);
}
