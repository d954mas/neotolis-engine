/* clang-format off */
#include "nt_builder_internal.h"
#include "hash/nt_hash.h"
#include "nt_mesh_format.h"
#include "cgltf.h"
/* clang-format on */

/* Uses nt_builder_convert_component() and nt_builder_clampf() from nt_builder_internal.h */

/* --- Helper: get texture index from cgltf_texture_view -> images array --- */

static uint32_t nt_scene_texture_image_index(const cgltf_texture_view *view, const cgltf_data *data) {
    if (view == NULL || view->texture == NULL || view->texture->image == NULL) {
        return UINT32_MAX;
    }
    return (uint32_t)(view->texture->image - data->images);
}

/* --- Parse glb scene --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_build_result_t nt_builder_parse_glb_scene(nt_glb_scene_t *scene, const char *path) {
    if (!scene || !path) {
        return NT_BUILD_ERR_VALIDATION;
    }
    memset(scene, 0, sizeof(nt_glb_scene_t));

    cgltf_options options;
    memset(&options, 0, sizeof(options));

    cgltf_data *data = NULL;
    cgltf_result result = cgltf_parse_file(&options, path, &data);
    if (result != cgltf_result_success) {
        NT_LOG_ERROR("%s: failed to parse glTF (cgltf error %d)", path, (int)result);
        return NT_BUILD_ERR_FORMAT;
    }

    result = cgltf_load_buffers(&options, data, path);
    if (result != cgltf_result_success) {
        cgltf_free(data);
        NT_BUILD_ASSERT(0 && "failed to load glTF buffers");
    }

    result = cgltf_validate(data);
    if (result != cgltf_result_success) {
        NT_LOG_WARN("%s: glTF validation issues (cgltf error %d)", path, (int)result);
    }

    /* Meshes */
    uint32_t mesh_count = (uint32_t)data->meshes_count;
    scene->meshes = (nt_glb_mesh_t *)calloc(mesh_count > 0 ? mesh_count : 1, sizeof(nt_glb_mesh_t));
    scene->mesh_count = mesh_count;
    for (uint32_t m = 0; m < mesh_count; m++) {
        const cgltf_mesh *cm = &data->meshes[m];
        scene->meshes[m].name = cm->name;
        scene->meshes[m].primitive_count = (uint32_t)cm->primitives_count;
        if (cm->primitives_count > 0 && cm->primitives[0].material != NULL) {
            scene->meshes[m].material_index = (uint32_t)(cm->primitives[0].material - data->materials);
        } else {
            scene->meshes[m].material_index = UINT32_MAX;
        }
    }

    /* Materials */
    uint32_t mat_count = (uint32_t)data->materials_count;
    scene->materials = (nt_glb_material_t *)calloc(mat_count > 0 ? mat_count : 1, sizeof(nt_glb_material_t));
    scene->material_count = mat_count;
    for (uint32_t i = 0; i < mat_count; i++) {
        const cgltf_material *cm = &data->materials[i];
        nt_glb_material_t *mat = &scene->materials[i];
        mat->name = cm->name;
        mat->double_sided = cm->double_sided != 0;
        mat->alpha_cutoff = cm->alpha_cutoff;

        /* PBR metallic-roughness base color */
        if (cm->has_pbr_metallic_roughness) {
            memcpy(mat->base_color, cm->pbr_metallic_roughness.base_color_factor, sizeof(float) * 4);
            mat->diffuse_index = nt_scene_texture_image_index(&cm->pbr_metallic_roughness.base_color_texture, data);
            mat->specular_index = nt_scene_texture_image_index(&cm->pbr_metallic_roughness.metallic_roughness_texture, data);
        } else {
            mat->base_color[0] = 1.0F;
            mat->base_color[1] = 1.0F;
            mat->base_color[2] = 1.0F;
            mat->base_color[3] = 1.0F;
            mat->diffuse_index = UINT32_MAX;
            mat->specular_index = UINT32_MAX;
        }
        mat->normal_index = nt_scene_texture_image_index(&cm->normal_texture, data);
    }

    /* Textures (mapped to images) */
    uint32_t img_count = (uint32_t)data->images_count;
    scene->textures = (nt_glb_texture_t *)calloc(img_count > 0 ? img_count : 1, sizeof(nt_glb_texture_t));
    scene->texture_count = img_count;
    for (uint32_t i = 0; i < img_count; i++) {
        const cgltf_image *img = &data->images[i];
        nt_glb_texture_t *tex = &scene->textures[i];
        tex->name = img->name != NULL ? img->name : img->uri;
        tex->mime_type = img->mime_type;
        if (img->buffer_view != NULL) {
            const uint8_t *bv_data = cgltf_buffer_view_data(img->buffer_view);
            tex->data = bv_data;
            tex->size = (uint32_t)img->buffer_view->size;
        } else {
            tex->data = NULL;
            tex->size = 0;
        }
    }

    /* Nodes */
    uint32_t node_count = (uint32_t)data->nodes_count;
    scene->nodes = (nt_glb_node_t *)calloc(node_count > 0 ? node_count : 1, sizeof(nt_glb_node_t));
    scene->node_count = node_count;
    for (uint32_t i = 0; i < node_count; i++) {
        const cgltf_node *cn = &data->nodes[i];
        nt_glb_node_t *node = &scene->nodes[i];
        node->name = cn->name;
        if (cn->mesh != NULL) {
            node->mesh_index = (uint32_t)(cn->mesh - data->meshes);
        } else {
            node->mesh_index = UINT32_MAX;
        }
        cgltf_node_transform_world(cn, node->transform);
    }

    scene->_internal = data;

    NT_LOG_INFO("Parsed glTF scene: %s", path);
    NT_LOG_INFO("  Meshes: %u, Materials: %u, Textures: %u, Nodes: %u", scene->mesh_count, scene->material_count, scene->texture_count, scene->node_count);

    return NT_BUILD_OK;
}

/* --- Free glb scene --- */

void nt_builder_free_glb_scene(nt_glb_scene_t *scene) {
    if (!scene) {
        return;
    }
    free(scene->meshes);
    free(scene->materials);
    free(scene->textures);
    free(scene->nodes);
    if (scene->_internal != NULL) {
        cgltf_free((cgltf_data *)scene->_internal);
    }
    memset(scene, 0, sizeof(nt_glb_scene_t));
}

/* --- Add scene mesh (eager decode) --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_builder_add_scene_mesh(NtBuilderContext *ctx, const nt_glb_scene_t *scene, uint32_t mesh_index, uint32_t primitive_index, const char *resource_id, const nt_mesh_opts_t *opts) {
    NT_BUILD_ASSERT(ctx && scene && resource_id && opts && opts->layout && "invalid scene_mesh args");
    NT_BUILD_ASSERT(mesh_index < scene->mesh_count && "mesh_index out of range");
    NT_BUILD_ASSERT(primitive_index < scene->meshes[mesh_index].primitive_count && "primitive_index out of range");

    uint8_t *mesh_data = NULL;
    uint32_t mesh_size = 0;
    nt_build_result_t r = nt_builder_decode_scene_mesh(scene, mesh_index, primitive_index, opts->layout, opts->stream_count, opts->tangent_mode, &mesh_data, &mesh_size);
    NT_BUILD_ASSERT(r == NT_BUILD_OK && "add_scene_mesh: decode failed");

    uint64_t hash = nt_hash64(mesh_data, mesh_size).value;
    nt_builder_add_entry(ctx, resource_id, NT_BUILD_ASSET_MESH, NULL, mesh_data, mesh_size, hash);
}

/* --- Decode: scene mesh -> binary mesh buffer (eager, called from add_scene_mesh) --- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_build_result_t nt_builder_decode_scene_mesh(const nt_glb_scene_t *scene, uint32_t mesh_index, uint32_t primitive_index, const NtStreamLayout *layout, uint32_t stream_count,
                                               nt_tangent_mode_t tangent_mode, uint8_t **out_data, uint32_t *out_size) {
    if (!scene || !layout || !out_data || !out_size) {
        return NT_BUILD_ERR_VALIDATION;
    }

    cgltf_data *data = (cgltf_data *)scene->_internal;
    if (!data || mesh_index >= data->meshes_count) {
        return NT_BUILD_ERR_VALIDATION;
    }

    cgltf_mesh *mesh = &data->meshes[mesh_index];
    if (primitive_index >= mesh->primitives_count) {
        return NT_BUILD_ERR_VALIDATION;
    }

    cgltf_primitive *prim = &mesh->primitives[primitive_index];
    if (prim->type != cgltf_primitive_type_triangles) {
        NT_LOG_ERROR("mesh[%u] prim[%u]: only triangle primitives supported", mesh_index, primitive_index);
        return NT_BUILD_ERR_VALIDATION;
    }

    /* Determine which streams need tangent data from MikkTSpace */
    int32_t tangent_stream_idx = -1;
    bool need_compute_tangent = false;
    bool has_gltf_tangent = false;

    /* Check if glTF primitive has TANGENT attribute */
    for (cgltf_size a = 0; a < prim->attributes_count; a++) {
        if (prim->attributes[a].name != NULL && strcmp(prim->attributes[a].name, "TANGENT") == 0) {
            has_gltf_tangent = true;
            break;
        }
    }

    /* Find tangent stream in layout if present */
    for (uint32_t s = 0; s < stream_count; s++) {
        if (layout[s].gltf_name != NULL && strcmp(layout[s].gltf_name, "TANGENT") == 0) {
            tangent_stream_idx = (int32_t)s;
            break;
        }
    }

    /* Determine tangent handling */
    if (tangent_stream_idx >= 0) {
        switch (tangent_mode) {
        case NT_TANGENT_NONE:
            /* Skip tangent -- leave tangent_stream_idx but fill with zeros */
            break;
        case NT_TANGENT_AUTO:
            if (!has_gltf_tangent) {
                need_compute_tangent = true;
            }
            break;
        case NT_TANGENT_COMPUTE:
            need_compute_tangent = true;
            break;
        case NT_TANGENT_REQUIRE:
            if (!has_gltf_tangent) {
                NT_LOG_ERROR("mesh[%u] prim[%u]: TANGENT required but not found in glTF", mesh_index, primitive_index);
                return NT_BUILD_ERR_VALIDATION;
            }
            break;
        }
    }

    /* Extract vertex streams */
    float *stream_floats[NT_MESH_MAX_STREAMS];
    memset((void *)stream_floats, 0, sizeof(stream_floats));
    uint32_t vertex_count = 0;
    bool vertex_count_set = false;
    nt_build_result_t ret = NT_BUILD_OK;

    for (uint32_t s = 0; s < stream_count; s++) {
        /* Skip TANGENT stream if we need to compute it */
        if ((int32_t)s == tangent_stream_idx && (need_compute_tangent || tangent_mode == NT_TANGENT_NONE)) {
            continue;
        }

        const cgltf_accessor *acc = NULL;
        for (cgltf_size a = 0; a < prim->attributes_count; a++) {
            if (layout[s].gltf_name != NULL && prim->attributes[a].name != NULL && strcmp(prim->attributes[a].name, layout[s].gltf_name) == 0) {
                acc = prim->attributes[a].data;
                break;
            }
        }

        if (!acc) {
            NT_LOG_ERROR("mesh[%u] prim[%u]: attribute %s not found", mesh_index, primitive_index, layout[s].gltf_name ? layout[s].gltf_name : "(null)");
            ret = NT_BUILD_ERR_VALIDATION;
            goto cleanup_streams;
        }

        uint32_t acc_components = (uint32_t)cgltf_num_components(acc->type);
        if (acc_components != layout[s].count) {
            NT_LOG_ERROR("mesh[%u] prim[%u]: attribute %s has %u components, layout expects %u", mesh_index, primitive_index, layout[s].gltf_name ? layout[s].gltf_name : "(null)", acc_components,
                         (uint32_t)layout[s].count);
            ret = NT_BUILD_ERR_VALIDATION;
            goto cleanup_streams;
        }

        uint32_t count = (uint32_t)acc->count;
        if (!vertex_count_set) {
            vertex_count = count;
            vertex_count_set = true;
        } else if (count != vertex_count) {
            ret = NT_BUILD_ERR_VALIDATION;
            goto cleanup_streams;
        }

        cgltf_size float_count = (cgltf_size)count * (cgltf_size)layout[s].count;
        stream_floats[s] = (float *)calloc(float_count, sizeof(float));
        NT_BUILD_ASSERT(stream_floats[s] && "scene mesh: float buffer alloc failed");

        cgltf_size unpacked = cgltf_accessor_unpack_floats(acc, stream_floats[s], float_count);
        if (unpacked == 0) {
            ret = NT_BUILD_ERR_FORMAT;
            goto cleanup_streams;
        }
    }

    if (!vertex_count_set || vertex_count == 0) {
        ret = NT_BUILD_ERR_VALIDATION;
        goto cleanup_streams;
    }

    if (vertex_count > NT_BUILD_MAX_VERTICES) {
        NT_LOG_ERROR("mesh[%u] prim[%u]: vertex count %u exceeds max %d", mesh_index, primitive_index, vertex_count, NT_BUILD_MAX_VERTICES);
        ret = NT_BUILD_ERR_LIMIT;
        goto cleanup_streams;
    }

    /* Extract indices */
    {
        uint32_t index_count = 0;
        uint8_t index_type = 0;
        uint8_t *index_buf = NULL;
        uint32_t index_data_size = 0;

        if (prim->indices != NULL) {
            index_count = (uint32_t)prim->indices->count;
            if (index_count > NT_BUILD_MAX_INDICES) {
                NT_LOG_ERROR("mesh[%u] prim[%u]: index count %u exceeds max %d", mesh_index, primitive_index, index_count, NT_BUILD_MAX_INDICES);
                ret = NT_BUILD_ERR_LIMIT;
                goto cleanup_streams;
            }

            if (vertex_count <= 65535) {
                index_type = 1;
                index_data_size = index_count * (uint32_t)sizeof(uint16_t);
            } else {
                index_type = 2;
                index_data_size = index_count * (uint32_t)sizeof(uint32_t);
            }

            index_buf = (uint8_t *)calloc(index_data_size, 1);
            NT_BUILD_ASSERT(index_buf && "scene mesh: index buffer alloc failed");

            size_t idx_elem_size = (index_type == 1) ? sizeof(uint16_t) : sizeof(uint32_t);
            cgltf_accessor_unpack_indices(prim->indices, index_buf, idx_elem_size, index_count);
        }

        /* Compute tangents if needed */
        if (tangent_stream_idx >= 0 && need_compute_tangent) {
            /* Need POSITION, NORMAL, TEXCOORD_0 float data + indices as uint32 */
            float *pos_data = NULL;
            float *norm_data = NULL;
            float *uv_data = NULL;

            for (uint32_t s = 0; s < stream_count; s++) {
                if (layout[s].gltf_name != NULL) {
                    if (strcmp(layout[s].gltf_name, "POSITION") == 0) {
                        pos_data = stream_floats[s];
                    }
                    if (strcmp(layout[s].gltf_name, "NORMAL") == 0) {
                        norm_data = stream_floats[s];
                    }
                    if (strcmp(layout[s].gltf_name, "TEXCOORD_0") == 0) {
                        uv_data = stream_floats[s];
                    }
                }
            }

            if (!pos_data || !norm_data || !uv_data) {
                NT_LOG_ERROR("mesh[%u] prim[%u]: tangent computation requires POSITION, NORMAL, TEXCOORD_0", mesh_index, primitive_index);
                free(index_buf);
                ret = NT_BUILD_ERR_VALIDATION;
                goto cleanup_streams;
            }

            /* Build uint32 index array for MikkTSpace */
            uint32_t *idx32 = NULL;
            uint32_t mikk_index_count = index_count;
            if (index_count > 0) {
                idx32 = (uint32_t *)calloc(index_count, sizeof(uint32_t));
                NT_BUILD_ASSERT(idx32 && "scene mesh: tangent idx32 alloc failed");
                if (index_type == 1) {
                    const uint16_t *idx16 = (const uint16_t *)index_buf;
                    for (uint32_t i = 0; i < index_count; i++) {
                        idx32[i] = idx16[i];
                    }
                } else {
                    memcpy(idx32, index_buf, index_count * sizeof(uint32_t));
                }
            } else {
                /* No indices -- generate sequential */
                mikk_index_count = vertex_count;
                idx32 = (uint32_t *)calloc(vertex_count, sizeof(uint32_t));
                NT_BUILD_ASSERT(idx32 && "scene mesh: tangent sequential idx32 alloc failed");
                for (uint32_t i = 0; i < vertex_count; i++) {
                    idx32[i] = i;
                }
            }

            stream_floats[tangent_stream_idx] = (float *)calloc((size_t)vertex_count * 4, sizeof(float));
            NT_BUILD_ASSERT(stream_floats[tangent_stream_idx] && "scene mesh: tangent output alloc failed");

            ret = nt_builder_compute_tangents(pos_data, norm_data, uv_data, idx32, vertex_count, mikk_index_count, stream_floats[tangent_stream_idx]);
            free(idx32);
            if (ret != NT_BUILD_OK) {
                free(index_buf);
                goto cleanup_streams;
            }
        }

        /* Handle TANGENT_NONE: fill with zero */
        if (tangent_stream_idx >= 0 && tangent_mode == NT_TANGENT_NONE) {
            stream_floats[tangent_stream_idx] = (float *)calloc((size_t)vertex_count * layout[tangent_stream_idx].count, sizeof(float));
            NT_BUILD_ASSERT(stream_floats[tangent_stream_idx] && "scene mesh: tangent zero-fill alloc failed");
        }

        /* Build final mesh buffer */
        ret = nt_builder_build_mesh_buffer(layout, stream_count, stream_floats, vertex_count, prim, index_buf, index_count, index_type, index_data_size, out_data, out_size);
        free(index_buf);
    }

cleanup_streams:
    for (uint32_t s = 0; s < stream_count; s++) {
        free(stream_floats[s]);
    }
    /* DON'T free scene data -- borrowed reference */
    return ret;
}
