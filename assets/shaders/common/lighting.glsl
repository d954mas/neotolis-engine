#pragma once

layout(std140) uniform Lighting {
    vec4 light_dir;
    vec4 light_color;
    vec4 ambient;
};
