#pragma once

float nt_ui_radial_edge_coverage(vec3 field) {
    vec2 normal = abs(field.yz);
    float major = max(max(normal.x, normal.y), 1e-10), minor = min(normal.x, normal.y);
    float tail = max(0.5 * (major + minor) - abs(field.x), 0.0);
    float area = minor > 0.0 && tail < minor ? tail * tail / (2.0 * major * minor) : (tail - 0.5 * minor) / major;
    return field.x > 0.0 ? area : 1.0 - area;
}

// Correlated angular edges clip the same physical pixel square.
// Jets contain the field value and its two screen derivatives.
float nt_ui_radial_intersection(vec3 first, vec3 last) {
    float first_coverage = nt_ui_radial_edge_coverage(first), last_coverage = nt_ui_radial_edge_coverage(last);
    if (first_coverage == 0.0 || last_coverage == 0.0)
        return 0.0;
    if (first_coverage == 1.0 || last_coverage == 1.0)
        return min(first_coverage, last_coverage);
    vec2 polygon[6], clipped[6];
    polygon[0] = vec2(-0.5, -0.5);
    polygon[1] = vec2(0.5, -0.5);
    polygon[2] = vec2(0.5, 0.5);
    polygon[3] = vec2(-0.5, 0.5);
    int count = 4;
    for (int edge = 0; edge < 2; ++edge) {
        vec3 field = edge == 0 ? first : last;
        vec2 previous = polygon[count - 1];
        float previous_distance = field.x + dot(field.yz, previous);
        int next_count = 0;
        for (int i = 0; i < count; ++i) {
            vec2 current = polygon[i];
            float distance = field.x + dot(field.yz, current);
            if ((previous_distance > 0.0 && distance < 0.0) || (previous_distance < 0.0 && distance > 0.0))
                clipped[next_count++] = mix(previous, current, previous_distance / (previous_distance - distance));
            if (distance <= 0.0)
                clipped[next_count++] = current;
            previous = current;
            previous_distance = distance;
        }
        count = next_count;
        if (count < 3)
            return 0.0;
        for (int i = 0; i < count; ++i)
            polygon[i] = clipped[i];
    }
    float area = 0.0;
    vec2 previous = polygon[count - 1];
    for (int i = 0; i < count; ++i) {
        area += previous.x * polygon[i].y - previous.y * polygon[i].x;
        previous = polygon[i];
    }
    return clamp(0.5 * area, 0.0, 1.0);
}

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
            vec3 lead = vec3(lead_field, dFdx(lead_field), dFdy(lead_field));
            vec3 trail = vec3(trail_field, dFdx(trail_field), dFdy(trail_field));
            wedge = total <= 0.5 * tau ? nt_ui_radial_intersection(lead, trail) : 1.0 - nt_ui_radial_intersection(-lead, -trail);
        }
    }
    return max(outer + ring - 1.0, 0.0) * wedge;
}
