#pragma once

// Jets carry value and physical screen derivatives without higher-order
// differentiation.
vec3 nt_ui_shape_projective_mul(vec3 a, vec3 b) { return vec3(a.x * b.x, a.yz * b.x + b.yz * a.x); }
vec3 nt_ui_shape_projective_div(vec3 a, vec3 b) {
    float v = a.x / b.x;
    return vec3(v, (a.yz - v * b.yz) / b.x);
}
vec3 nt_ui_shape_projective_max(vec3 a, vec3 b) { return a.x >= b.x ? a : b; }
vec3 nt_ui_shape_projective_min(vec3 a, vec3 b) { return a.x <= b.x ? a : b; }
vec3 nt_ui_shape_projective_abs(vec3 a) { return a.x >= 0.0 ? a : -a; }
vec3 nt_ui_shape_projective_length(vec3 x, vec3 y) {
    float value = length(vec2(x.x, y.x));
    return value > 0.0 ? vec3(value, (x.x * x.yz + y.x * y.yz) / value) : vec3(0.0);
}
float nt_ui_shape_projective_aa(vec3 field) { return max(abs(field.y) + abs(field.z), 1e-10); }
float nt_ui_shape_projective_coverage(vec3 field) { return clamp(0.5 - field.x / nt_ui_shape_projective_aa(field), 0.0, 1.0); }
float nt_ui_shape_projective_interval(vec3 position, vec3 q, float first, float last) {
    return max(nt_ui_shape_projective_coverage(first * q - position) + nt_ui_shape_projective_coverage(position - last * q) - 1.0, 0.0);
}
vec3 nt_ui_shape_projective_rect(vec3 x, vec3 y, vec3 q, vec2 size) {
    vec3 dx = nt_ui_shape_projective_abs(x - 0.5 * size.x * q) - 0.5 * size.x * q;
    vec3 dy = nt_ui_shape_projective_abs(y - 0.5 * size.y * q) - 0.5 * size.y * q;
    return nt_ui_shape_projective_length(nt_ui_shape_projective_max(dx, vec3(0.0)), nt_ui_shape_projective_max(dy, vec3(0.0))) +
           nt_ui_shape_projective_min(nt_ui_shape_projective_max(dx, dy), vec3(0.0));
}
vec3 nt_ui_shape_projective_circle_corner(vec3 field, vec3 x, vec3 y, vec3 q, float radius) {
    if (q.x > 0.0 && radius > 0.0 && x.x < radius * q.x && y.x < radius * q.x) {
        field = nt_ui_shape_projective_max(field, nt_ui_shape_projective_length(x - radius * q, y - radius * q) - radius * q);
    }
    return field;
}
vec3 nt_ui_shape_projective_box(vec3 x, vec3 y, vec3 q, vec2 size, vec4 radii) {
    vec3 field = nt_ui_shape_projective_rect(x, y, q, size);
    field = nt_ui_shape_projective_circle_corner(field, x, y, q, radii.x);
    field = nt_ui_shape_projective_circle_corner(field, size.x * q - x, y, q, radii.y);
    field = nt_ui_shape_projective_circle_corner(field, size.x * q - x, size.y * q - y, q, radii.z);
    return nt_ui_shape_projective_circle_corner(field, x, size.y * q - y, q, radii.w);
}
vec3 nt_ui_shape_projective_ellipse_corner(vec3 field, vec3 x, vec3 y, vec3 q, vec2 radius) {
    if (q.x > 0.0 && radius.x > 0.0 && radius.y > 0.0 && x.x < radius.x * q.x && y.x < radius.y * q.x) {
        x -= radius.x * q;
        y -= radius.y * q;
        vec3 distance;
        if (radius.x == radius.y)
            distance = nt_ui_shape_projective_length(x, y) - radius.x * q;
        else {
            vec3 nx = x / radius.x, ny = y / radius.y;
            vec3 k = nt_ui_shape_projective_length(nx, ny), normal = nt_ui_shape_projective_length(nx / radius.x, ny / radius.y);
            distance = normal.x > 0.0 ? nt_ui_shape_projective_div(nt_ui_shape_projective_mul(k, k - q), normal) : vec3(0.0);
        }
        field = nt_ui_shape_projective_max(field, distance);
    }
    return field;
}
vec3 nt_ui_shape_projective_inner(vec3 x, vec3 y, vec3 q, vec2 size, vec4 radii, vec4 widths, vec3 outer) {
    vec2 inner_size = size - widths.xy - widths.zw;
    x -= widths.x * q;
    y -= widths.y * q;
    vec3 field = nt_ui_shape_projective_max(outer, nt_ui_shape_projective_rect(x, y, q, inner_size));
    field = nt_ui_shape_projective_ellipse_corner(field, x, y, q, max(vec2(radii.x) - widths.xy, vec2(0.0)));
    field = nt_ui_shape_projective_ellipse_corner(field, inner_size.x * q - x, y, q, max(vec2(radii.y) - widths.zy, vec2(0.0)));
    field = nt_ui_shape_projective_ellipse_corner(field, inner_size.x * q - x, inner_size.y * q - y, q, max(vec2(radii.z) - widths.zw, vec2(0.0)));
    return nt_ui_shape_projective_ellipse_corner(field, x, inner_size.y * q - y, q, max(vec2(radii.w) - widths.xw, vec2(0.0)));
}
float nt_ui_shape_projective_gradient_position(float position, float q, float size) {
    if (position <= 0.0)
        return 0.0;
    if (q <= 0.0 || position >= size * q)
        return 1.0;
    return position / (size * q);
}
vec4 nt_ui_shape_projective_gradient(vec3 h, vec2 size, int direction, vec4 first, vec4 last) {
    if (direction == 0)
        return first;
    float t = direction == 1 ? nt_ui_shape_projective_gradient_position(h.x, h.z, size.x) : nt_ui_shape_projective_gradient_position(h.y, h.z, size.y);
    return mix(first, last, t);
}
vec4 nt_ui_shape_projective_shape(vec3 h, vec3 hx, vec3 hy, vec2 size, vec4 radii, vec4 widths, vec4 strip_data, int mode, int gradient, bool inner_empty, vec4 fill, vec4 endpoint, vec4 border,
                                  vec3 shadow, vec3 radial) {
    vec3 x = vec3(h.x, hx.x, hy.x), y = vec3(h.y, hx.y, hy.y), q = vec3(h.z, hx.z, hy.z);
    float rect_coverage = min(nt_ui_shape_projective_interval(x, q, 0.0, size.x), nt_ui_shape_projective_interval(y, q, 0.0, size.y));
    if (mode == 2) {
        vec3 px = 2.0 * x / size.x - q;
        vec3 py = 2.0 * y / size.y - q;
        vec3 angle_x = x - 0.5 * size.x * q;
        vec3 angle_y = y - 0.5 * size.y * q;
        vec3 radius = nt_ui_shape_projective_length(px, py);
        float outer = nt_ui_shape_projective_coverage(radius - q);
        float ring = radial.z > 0.0 ? nt_ui_shape_projective_coverage(radial.z * q - radius) : 1.0;
        const float tau = 6.28318530717958647692;
        float sweep = mod(radial.y - radial.x, tau);
        float wedge = 1.0;
        if (abs(radial.y - radial.x) < tau - 1e-4) {
            if (sweep == 0.0)
                wedge = 0.0;
            else {
                vec2 first = vec2(cos(radial.x), sin(radial.x));
                vec2 last = vec2(cos(radial.y), sin(radial.y));
                float lead = nt_ui_shape_projective_coverage(first.y * angle_x - first.x * angle_y);
                float trail = nt_ui_shape_projective_coverage(last.x * angle_y - last.y * angle_x);
                wedge = abs(sweep - 0.5 * tau) < 1e-4 ? lead : sweep < 0.5 * tau ? lead * trail : lead + trail - lead * trail;
            }
        }
        return nt_ui_shape_projective_gradient(h, size, gradient, fill, endpoint) * min(outer * ring * wedge, rect_coverage);
    }
    vec3 distance = nt_ui_shape_projective_box(x, y, q, size, radii);
    if (mode == 3) {
        vec3 spread_distance = distance - shadow.x * q;
        float coverage;
        if (shadow.y > 0.0) {
            float support = max(shadow.y * max(q.x, 0.0), 0.5 * nt_ui_shape_projective_aa(spread_distance));
            float t = clamp(0.5 - spread_distance.x / (2.0 * support), 0.0, 1.0);
            coverage = t * t * (3.0 - 2.0 * t);
        } else
            coverage = nt_ui_shape_projective_coverage(spread_distance);
        float support_clip = min(nt_ui_shape_projective_interval(x, q, -shadow.z, size.x + shadow.z), nt_ui_shape_projective_interval(y, q, -shadow.z, size.y + shadow.z));
        return fill * min(coverage, support_clip);
    }
    float outer = min(nt_ui_shape_projective_coverage(distance), rect_coverage);
    float inner = 0.0;
    if (!inner_empty) {
        if (all(equal(widths, vec4(0.0))))
            inner = outer;
        else {
            inner = min(outer, nt_ui_shape_projective_coverage(nt_ui_shape_projective_inner(x, y, q, size, radii, widths, distance)));
            inner = min(inner, min(nt_ui_shape_projective_interval(x, q, widths.x, size.x - widths.z), nt_ui_shape_projective_interval(y, q, widths.y, size.y - widths.w)));
            if (dot(strip_data.xy, strip_data.xy) > 0.0)
                inner = min(inner, nt_ui_shape_projective_interval(strip_data.x * x + strip_data.y * y, q, strip_data.z, strip_data.w));
        }
    }
    return nt_ui_shape_projective_gradient(h, size, gradient, fill, endpoint) * inner + border * (outer - inner);
}

vec4 nt_ui_shape_projective_color(vec2 local, vec3 clip_value, vec3 expansion, vec4 layout_data, vec4 geometry, vec4 widths, vec2 paint, vec4 inner_strip, vec4 fill, vec4 endpoint, vec4 border_color,
                                  int mode, out float fragment_depth) {
    vec3 h = vec3(local, 1.0) * gl_FragCoord.w;
    vec3 hx = dFdx(h);
    vec3 hy = dFdy(h);
    vec3 projected = clip_value * gl_FragCoord.w;
    vec2 delta = (expansion.z - 1.0) * (projected.xy - expansion.xy) / vec2(dFdx(projected.x), dFdy(projected.y));
    h += hx * delta.x + hy * delta.y;
    float depth = projected.z + dFdx(projected.z) * delta.x + dFdy(projected.z) * delta.y;
    if (depth < -1.0 || depth > 1.0) {
        discard;
    }
    fragment_depth = depth * 0.5 + 0.5;
    return nt_ui_shape_projective_shape(h, expansion.z * hx, expansion.z * hy, layout_data.xy, geometry, widths, inner_strip, mode, int(layout_data.w), paint.x > 0.5, fill, endpoint, border_color,
                                        widths.xyz, geometry.xyz);
}
