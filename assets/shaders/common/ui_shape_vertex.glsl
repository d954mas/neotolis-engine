#pragma once

vec2 nt_ui_shape_strip_axis(vec2 first_radius, vec2 second_radius, vec2 delta) {
    if (first_radius.x == first_radius.y && second_radius.x == second_radius.y) {
        return normalize(delta);
    }
    float scale = max(max(max(first_radius.x, first_radius.y), max(second_radius.x, second_radius.y)), max(delta.x, delta.y));
    first_radius /= scale;
    second_radius /= scale;
    delta /= scale;
    float low = 0.0;
    float high = 1.0;
    // Support width is convex for the unnormalized axis (u, 1-u).
    for (int step = 0; step < 24; ++step) {
        float u = 0.5 * (low + high);
        vec2 axis = vec2(u, 1.0 - u);
        vec2 derivative_axis = vec2(u, u - 1.0);
        float derivative = delta.y - delta.x;
        float first_support = length(first_radius * axis);
        float second_support = length(second_radius * axis);
        if (first_support > 0.0) {
            derivative += dot(first_radius * first_radius, derivative_axis) / first_support;
        }
        if (second_support > 0.0) {
            derivative += dot(second_radius * second_radius, derivative_axis) / second_support;
        }
        if (derivative > 0.0) {
            high = u;
        } else {
            low = u;
        }
    }
    float u = 0.5 * (low + high);
    return normalize(vec2(u, 1.0 - u));
}

vec2 nt_ui_shape_strip_bounds(vec2 axis, vec4 inset, vec4 corners[4]) {
    vec2 first = vec2(axis.x >= 0.0 ? inset.x : inset.z, axis.y >= 0.0 ? inset.y : inset.w);
    vec2 last = vec2(axis.x >= 0.0 ? inset.z : inset.x, axis.y >= 0.0 ? inset.w : inset.y);
    vec2 bounds = vec2(dot(axis, first), dot(axis, last));
    for (int corner = 0; corner < 4; ++corner) {
        vec2 outward = vec2(corner == 0 || corner == 3 ? -1.0 : 1.0, corner < 2 ? -1.0 : 1.0);
        float center = dot(axis, corners[corner].xy);
        float support = length(axis * corners[corner].zw);
        if (all(lessThanEqual(outward * axis, vec2(0.0)))) {
            bounds.x = max(bounds.x, center - support);
        }
        if (all(greaterThanEqual(outward * axis, vec2(0.0)))) {
            bounds.y = min(bounds.y, center + support);
        }
    }
    return bounds;
}

void nt_ui_shape_strip_candidate(vec4 first, vec4 second, vec2 direction, vec4 inset, vec4 corners[4], inout vec4 best) {
    vec2 delta = direction * (first.xy - second.xy);
    if (any(lessThanEqual(delta, vec2(0.0)))) {
        return;
    }
    vec2 axis = direction * nt_ui_shape_strip_axis(first.zw, second.zw, delta);
    vec2 bounds = nt_ui_shape_strip_bounds(axis, inset, corners);
    if (dot(best.xy, best.xy) == 0.0 || bounds.y - bounds.x < best.w - best.z) {
        best = vec4(axis, bounds);
    }
}

vec4 nt_ui_shape_inner_strip(vec2 size, vec4 radii, vec4 widths) {
    vec4 best = vec4(0.0);
    if (all(equal(radii, vec4(0.0))) || all(equal(widths, vec4(0.0)))) {
        return best;
    }
    vec4 inset = vec4(widths.xy, size - widths.zw);
    vec4 corners[4];
    for (int corner = 0; corner < 4; ++corner) {
        bool left = corner == 0 || corner == 3;
        bool top = corner < 2;
        vec2 outward = vec2(left ? -1.0 : 1.0, top ? -1.0 : 1.0);
        vec2 radius = max(vec2(radii[corner]) - vec2(left ? widths.x : widths.z, top ? widths.y : widths.w), vec2(0.0));
        vec2 vertex = vec2(left ? inset.x : inset.z, top ? inset.y : inset.w);
        // A collapsed inner ellipse leaves the outer contour as the corner bound.
        if (any(equal(radius, vec2(0.0)))) {
            radius = vec2(radii[corner]);
            vertex = vec2(left ? 0.0 : size.x, top ? 0.0 : size.y);
        }
        corners[corner] = vec4(vertex - outward * radius, radius);
    }
    nt_ui_shape_strip_candidate(corners[0], corners[2], vec2(1.0), inset, corners, best);
    nt_ui_shape_strip_candidate(corners[1], corners[3], vec2(-1.0, 1.0), inset, corners, best);
    for (int corner = 0; corner < 4; ++corner) {
        vec2 direction = vec2(corner == 0 || corner == 3 ? 1.0 : -1.0, corner < 2 ? 1.0 : -1.0);
        vec2 vertex = vec2(direction.x > 0.0 ? inset.z : inset.x, direction.y > 0.0 ? inset.w : inset.y);
        nt_ui_shape_strip_candidate(corners[corner], vec4(vertex, 0.0, 0.0), direction, inset, corners, best);
    }
    return best;
}
