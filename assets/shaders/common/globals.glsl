#pragma once

layout(std140) uniform Globals {
    mat4 view_proj;
    mat4 view;
    mat4 proj;
    vec4 camera_pos;
    vec4 time;
    vec4 resolution;
    vec4 near_far;
};
