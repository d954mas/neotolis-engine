# Shader System

ShaderAsset defines the interface, not values: vertex input mask, material
vec4/texture counts, object/global usage masks, and default render state.
Shader data comes in four levels — vertex inputs, material params, object
params, and globals.

Related: [Material System](material.md), [Runtime Formats](../assets/runtime-formats.md), [Rendering Architecture](architecture.md)

## ShaderAsset purpose

ShaderAsset defines interface, not values.

## ShaderAsset fields

```c
typedef struct ShaderAsset {
    ShaderCodeRef vs;
    ShaderCodeRef fs;

    uint32_t vertex_input_mask;

    uint16_t material_vec4_count;
    uint16_t texture_slot_count;

    uint16_t object_usage_mask;
    uint16_t global_usage_mask;

    BlendMode default_blend_mode;
    bool default_depth_test;
    bool default_depth_write;
    CullMode default_cull_mode;
} ShaderAsset;
```

## Four levels of shader data

1. **vertex inputs** — from geometry/mesh
2. **material params** — from MaterialAsset
3. **object params** — from RenderState, Transform
4. **globals** — from renderer/pass

## Vertex input mask

Possible semantics: POSITION, NORMAL, UV0, COLOR0. Mesh/shader compatibility validated in builder and sanity-checked at runtime.

## Object params

Fixed object-level params for v0.1: world_matrix, object_color, object_params0.

## Global params

Possible globals: view, proj, view_proj, camera_pos, time, light_dir. Start minimal, expand later.

WebGL 2 Uniform Buffer Objects can be used to share globals efficiently across shaders.
