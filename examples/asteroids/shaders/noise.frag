/* 4-octave 2D simplex fBm for one asteroid noise texture. The sum is written
 * with 16 bits across R and G; the CPU normalizes each texture by its own range. */
precision highp float;
precision highp int;
uniform vec4 u_noise_params; /* frequency, lacunarity, gain, 1 / amplitude sum */
uniform vec4 u_noise_offset; /* noise-space offset xy, tile origin in pixels zw */
out vec4 frag_color;
uint lattice_hash(ivec2 cell) {
    uvec2 q = uvec2(cell);
    uint h = (q.x * 0x8DA6B343u) ^ (q.y * 0xD8163841u);
    h ^= h >> 13;
    h *= 0x5BD1E995u;
    return h ^ (h >> 15);
}
float corner(uint hash, vec2 d) {
    float t = 0.5 - dot(d, d);
    if (t <= 0.0) {
        return 0.0;
    }
    uint h = hash & 7u;
    float u = h < 4u ? d.x : d.y;
    float v = h < 4u ? d.y : d.x;
    return t * t * t * t * (((h & 1u) != 0u ? -u : u) + ((h & 2u) != 0u ? -2.0 * v : 2.0 * v));
}
float simplex2(vec2 p) {
    const float F2 = 0.36602540378;
    const float G2 = 0.21132486540;
    vec2 cell = floor(p + (p.x + p.y) * F2);
    vec2 d0 = p - (cell - (cell.x + cell.y) * G2);
    vec2 step1 = d0.x > d0.y ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    ivec2 i = ivec2(cell);
    float sum = corner(lattice_hash(i), d0);
    sum += corner(lattice_hash(i + ivec2(step1)), d0 - step1 + G2);
    sum += corner(lattice_hash(i + 1), d0 - 1.0 + 2.0 * G2);
    return 40.0 * sum;
}
void main() {
    vec2 p = (floor(gl_FragCoord.xy) - u_noise_offset.zw) * u_noise_params.x + u_noise_offset.xy;
    float sum = 0.0;
    float amplitude = 1.0;
    for (int octave = 0; octave < 4; octave++) {
        sum += amplitude * simplex2(p);
        p *= u_noise_params.y;
        amplitude *= u_noise_params.z;
    }
    float q = floor(clamp(sum * u_noise_params.w * 0.5 + 0.5, 0.0, 1.0) * 65535.0 + 0.5);
    frag_color = vec4(floor(q / 256.0) / 255.0, mod(q, 256.0) / 255.0, 0.0, 1.0);
}
