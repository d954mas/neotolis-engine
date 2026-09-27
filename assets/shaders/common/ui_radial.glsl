#pragma once

float nt_ui_radial_coverage(vec2 p, vec3 radial) {
    const float tau = 6.28318530717958647692;
    float radius = length(p);
    float ppu = 1.0 / max(fwidth(radius), 1e-6);
    float outer = clamp((1.0 - radius) * ppu + 0.5, 0.0, 1.0);
    float ring = radial.z > 0.0 ? clamp((radius - radial.z) * ppu + 0.5, 0.0, 1.0) : 1.0;
    float angle = atan(p.y, p.x);
    float sweep = mod(angle - radial.x, tau);
    float total = mod(radial.y - radial.x, tau);
    bool full_turn = abs(radial.y - radial.x) >= tau - 1e-4;
    float lead = clamp(radius * sweep * ppu + 0.5, 0.0, 1.0);
    float trail = clamp(radius * (total - sweep) * ppu + 0.5, 0.0, 1.0);
    float wedge = full_turn ? 1.0 : lead * trail;
    return (outer * ring) * wedge;
}
