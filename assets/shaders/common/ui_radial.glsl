#pragma once

// Coordinates use local UI Y down; angular rays use layout pixels.
float nt_ui_radial_coverage(vec2 ellipse_p, vec2 angle_p, vec3 radial) {
    const float tau = 6.28318530717958647692;
    float radius = length(ellipse_p);
    float ppu = 1.0 / max(fwidth(radius), 1e-6);
    float outer = clamp((1.0 - radius) * ppu + 0.5, 0.0, 1.0);
    float ring = radial.z > 0.0 ? clamp((radius - radial.z) * ppu + 0.5, 0.0, 1.0) : 1.0;
    float total = mod(radial.y - radial.x, tau);
    float wedge = 1.0;
    if (abs(radial.y - radial.x) < tau - 1e-4) {
        wedge = 0.0;
        if (total > 0.0) {
            vec2 first = vec2(cos(radial.x), sin(radial.x));
            vec2 last = vec2(cos(radial.y), sin(radial.y));
            float lead_field = first.y * angle_p.x - first.x * angle_p.y;
            float trail_field = last.x * angle_p.y - last.y * angle_p.x;
            float lead = clamp(0.5 - lead_field / max(fwidth(lead_field), 1e-6), 0.0, 1.0);
            float trail = clamp(0.5 - trail_field / max(fwidth(trail_field), 1e-6), 0.0, 1.0);
            wedge = abs(total - 0.5 * tau) < 1e-4 ? lead : total < 0.5 * tau ? lead * trail : lead + trail - lead * trail;
        }
    }
    return (outer * ring) * wedge;
}
