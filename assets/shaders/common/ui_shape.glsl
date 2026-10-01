#pragma once

#include "ui_radial.glsl"

float nt_ui_shape_corner_distance(float distance, vec2 from_corner, float radius) {
    if (radius > 0.0 && from_corner.x < radius && from_corner.y < radius) {
        distance = max(distance, length(from_corner - vec2(radius)) - radius);
    }
    return distance;
}

float nt_ui_shape_box_distance(vec2 p, vec2 size, vec4 radii) {
    vec2 q = abs(p - 0.5 * size) - 0.5 * size;
    float distance = length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0);
    // Opposite corner regions can overlap when a radius exceeds half the size.
    distance = nt_ui_shape_corner_distance(distance, p, radii.x);
    distance = nt_ui_shape_corner_distance(distance, vec2(size.x - p.x, p.y), radii.y);
    distance = nt_ui_shape_corner_distance(distance, size - p, radii.z);
    return nt_ui_shape_corner_distance(distance, vec2(p.x, size.y - p.y), radii.w);
}

vec4 nt_ui_shape_gradient(vec2 p, vec4 layout_data, vec4 fill, vec4 endpoint) {
    uint gradient = uint(layout_data.w);
    if (gradient != 0u) {
        float position = gradient == 1u ? p.x / layout_data.x : p.y / layout_data.y;
        fill = mix(fill, endpoint, clamp(position, 0.0, 1.0));
    }
    return fill;
}

float nt_ui_shape_inner_corner(float distance, vec2 from_corner, vec2 radius) {
    if (radius.x > 0.0 && radius.y > 0.0 && from_corner.x < radius.x && from_corner.y < radius.y) {
        vec2 delta = from_corner - radius;
        float corner_distance;
        if (radius.x == radius.y) {
            corner_distance = length(delta) - radius.x;
        } else {
            vec2 q = delta / radius;
            float k = length(q);
            corner_distance = k * (k - 1.0) / length(q / radius);
        }
        distance = max(distance, corner_distance);
    }
    return distance;
}

float nt_ui_shape_inner_distance(vec2 p, vec2 size, vec4 radii, vec4 widths, float outer_distance) {
    vec2 inner_size = size - widths.xy - widths.zw;
    vec2 inner_p = p - widths.xy;
    vec2 q = abs(inner_p - 0.5 * inner_size) - 0.5 * inner_size;
    float distance = max(outer_distance, length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0));
    distance = nt_ui_shape_inner_corner(distance, inner_p, max(vec2(radii.x) - widths.xy, vec2(0.0)));
    distance = nt_ui_shape_inner_corner(distance, vec2(inner_size.x - inner_p.x, inner_p.y), max(vec2(radii.y) - widths.zy, vec2(0.0)));
    distance = nt_ui_shape_inner_corner(distance, inner_size - inner_p, max(vec2(radii.z) - widths.zw, vec2(0.0)));
    return nt_ui_shape_inner_corner(distance, vec2(inner_p.x, inner_size.y - inner_p.y), max(vec2(radii.w) - widths.xw, vec2(0.0)));
}

vec4 nt_ui_shape_box(vec2 p, vec4 layout_data, vec4 radii, vec4 widths, vec4 inner_strip, vec4 fill, vec4 endpoint, vec4 border_color) {
    float distance = nt_ui_shape_box_distance(p, layout_data.xy, radii);
    float aa = max(fwidth(distance), 1e-6);
    float outer = clamp(0.5 - distance / aa, 0.0, 1.0);
    vec2 pixel_span = max(fwidth(p), vec2(1e-6));
    vec2 first_outer = clamp(0.5 + p / pixel_span, 0.0, 1.0);
    vec2 last_outer = clamp(0.5 + (layout_data.xy - p) / pixel_span, 0.0, 1.0);
    vec2 outer_interval = max(first_outer + last_outer - 1.0, vec2(0.0));
    outer = min(outer, min(outer_interval.x, outer_interval.y));
    float inner = outer;
    if (any(notEqual(widths, vec4(0.0)))) {
        float inner_distance = nt_ui_shape_inner_distance(p, layout_data.xy, radii, widths, distance);
        float inner_aa = max(fwidth(inner_distance), 1e-6);
        inner = min(inner, clamp(0.5 - inner_distance / inner_aa, 0.0, 1.0));
        // Opposing edges share one footprint when the inset becomes subpixel.
        vec2 first_edge = clamp(0.5 + (p - widths.xy) / pixel_span, 0.0, 1.0);
        vec2 last_edge = clamp(0.5 + (layout_data.xy - widths.zw - p) / pixel_span, 0.0, 1.0);
        vec2 interval = max(first_edge + last_edge - 1.0, vec2(0.0));
        inner = min(inner, min(interval.x, interval.y));
        if (dot(inner_strip.xy, inner_strip.xy) > 0.0) {
            float position = dot(p, inner_strip.xy);
            float span = max(fwidth(position), 1e-6);
            float first = clamp(0.5 + (position - inner_strip.z) / span, 0.0, 1.0);
            float last = clamp(0.5 + (inner_strip.w - position) / span, 0.0, 1.0);
            inner = min(inner, max(first + last - 1.0, 0.0));
        }
    }
    float border = max(outer - inner, 0.0);

    fill = nt_ui_shape_gradient(p, layout_data, fill, endpoint);
    // Fill and border partition coverage; source-over would darken their AA seam.
    return fill * inner + border_color * border;
}

vec4 nt_ui_shape_shadow(vec2 p, vec4 layout_data, vec4 radii, vec2 paint, vec4 color) {
    float distance = nt_ui_shape_box_distance(p, layout_data.xy, radii) - paint.x;
    float aa = max(fwidth(distance), 1e-6);
    float coverage;
    if (paint.y > 0.0) {
        float support = max(paint.y, 0.5 * aa);
        float t = clamp(0.5 - distance / (2.0 * support), 0.0, 1.0);
        coverage = t * t * (3.0 - 2.0 * t);
    } else {
        coverage = clamp(0.5 - distance / aa, 0.0, 1.0);
    }
    return color * coverage;
}

vec4 nt_ui_shape_radial(vec2 local, vec4 layout_data, vec4 radial, vec4 fill, vec4 endpoint) {
    if (radial.x == radial.y) {
        return vec4(0.0);
    }
    vec2 ellipse_p = 2.0 * local / layout_data.xy - 1.0;
    vec2 angle_p = local - 0.5 * layout_data.xy;
    float radial_coverage = nt_ui_radial_coverage(ellipse_p, angle_p, radial.xyz);

    // AA padding must not enlarge the original bbox.
    float rect_distance = nt_ui_shape_box_distance(local, layout_data.xy, vec4(0.0));
    float rect_aa = max(fwidth(rect_distance), 1e-6);
    float rect_coverage = clamp(0.5 - rect_distance / rect_aa, 0.0, 1.0);
    float coverage = min(radial_coverage, rect_coverage);
    return nt_ui_shape_gradient(local, layout_data, fill, endpoint) * coverage;
}
