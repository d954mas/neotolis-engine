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

    /* cgltf_validate proves what every reader below relies on: accessors
     * inside their buffer views, one vertex count per primitive, indices and
     * sparse indices in range, an acyclic node hierarchy. Reading past it
     * would be reading past a buffer. */
    result = cgltf_validate(data);
    if (result != cgltf_result_success) {
        NT_LOG_ERROR("%s: glTF fails cgltf_validate (cgltf error %d)", path, (int)result);
        cgltf_free(data);
        NT_BUILD_ASSERT(0 && "glTF fails cgltf_validate");
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
        if (cn->skin != NULL) {
            node->skin_index = (uint32_t)(cn->skin - data->skins);
        } else {
            node->skin_index = UINT32_MAX;
        }
        if (cn->parent != NULL) {
            node->parent = (uint32_t)(cn->parent - data->nodes);
        } else {
            node->parent = UINT32_MAX;
        }
        cgltf_node_transform_world(cn, node->transform);
    }

    scene->_internal = data;

    NT_LOG_INFO("Parsed glTF scene: %s", path);
    NT_LOG_INFO("  Meshes: %u, Materials: %u, Textures: %u, Nodes: %u, Skins: %u, Animations: %u", scene->mesh_count, scene->material_count, scene->texture_count, scene->node_count,
                (uint32_t)data->skins_count, (uint32_t)data->animations_count);

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

// #region skin influences
/* Every lane of one vertex across every set: finite non-negative weights, no
 * palette entry weighted twice, some weight at all. Both readers of the
 * influences depend on these, so the check lives with the read. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_BUILD_ASSERT expansions dominate the count
static void nt_scene_validate_vertex_influences(const nt_builder_influences_t *inf, const char *label, uint32_t v) {
    const size_t set_floats = (size_t)inf->vertex_count * 4U;
    const uint32_t lane_count = inf->set_count * 4U;
    double total = 0.0;
    for (uint32_t lane = 0; lane < lane_count; lane++) {
        const uint32_t s = lane / 4U;
        const uint32_t c = lane % 4U;
        const size_t at = ((size_t)s * set_floats) + ((size_t)v * 4U) + c;
        const float w = inf->weights[at];
        if (!nt_builder_finite(w)) {
            NT_LOG_ERROR("%s: vertex %u weight %u of set %u is not a finite number", label, v, c, s);
            NT_BUILD_ASSERT(0 && "influence weight is not finite");
        }
        if (w < 0.0F) {
            NT_LOG_ERROR("%s: vertex %u weight %u of set %u is %g; a weight is a non-negative fraction", label, v, c, s, (double)w);
            NT_BUILD_ASSERT(0 && "influence weight is negative");
        }
        if (w == 0.0F) {
            continue;
        }
        total += (double)w;
        const float joint = inf->joints[at];
        for (uint32_t earlier = 0; earlier < lane; earlier++) {
            const size_t e_at = ((size_t)(earlier / 4U) * set_floats) + ((size_t)v * 4U) + (earlier % 4U);
            if (inf->weights[e_at] != 0.0F && inf->joints[e_at] == joint) {
                NT_LOG_ERROR("%s: vertex %u weights palette entry %u twice", label, v, (uint32_t)joint);
                NT_BUILD_ASSERT(0 && "vertex weights one joint twice");
            }
        }
    }
    if (!(total > 0.0)) {
        NT_LOG_ERROR("%s: vertex %u has no influence with a non-zero weight", label, v);
        NT_BUILD_ASSERT(0 && "vertex influence weights sum to zero");
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_BUILD_ASSERT expansions dominate the count
void nt_builder_read_influences(const struct cgltf_primitive *prim, const char *label, uint32_t vertex_count, nt_builder_influences_t *out) {
    NT_BUILD_ASSERT(prim && label && out && "invalid read_influences args");

    /* A morph target moves the bound vertices, so the binding's reach would no
     * longer hold; morphs are deferred rather than silently unbounded. */
    if (prim->targets_count != 0) {
        NT_LOG_ERROR("%s: skinned primitive carries %u morph targets, which the skeletal importer does not support", label, (uint32_t)prim->targets_count);
        NT_BUILD_ASSERT(0 && "skinned primitive has morph targets");
    }

    uint32_t joint_sets = 0;
    uint32_t weight_sets = 0;
    for (cgltf_size a = 0; a < prim->attributes_count; a++) {
        joint_sets += (prim->attributes[a].type == cgltf_attribute_type_joints) ? 1U : 0U;
        weight_sets += (prim->attributes[a].type == cgltf_attribute_type_weights) ? 1U : 0U;
    }
    if (joint_sets == 0 || joint_sets != weight_sets) {
        NT_LOG_ERROR("%s: %u JOINTS_n and %u WEIGHTS_n attributes; a skinned primitive pairs every set", label, joint_sets, weight_sets);
        NT_BUILD_ASSERT(0 && "unpaired JOINTS_n/WEIGHTS_n set");
    }

    const size_t set_floats = (size_t)vertex_count * 4U;
    float *joints = (float *)calloc(set_floats * joint_sets, sizeof(float));
    float *weights = (float *)calloc(set_floats * joint_sets, sizeof(float));
    NT_BUILD_ASSERT(joints && weights && "read_influences: alloc failed (OOM)");
    out->joints = joints;
    out->weights = weights;
    out->set_count = joint_sets;
    out->vertex_count = vertex_count;

    for (uint32_t n = 0; n < joint_sets; n++) {
        const cgltf_accessor *ja = cgltf_find_accessor(prim, cgltf_attribute_type_joints, (cgltf_int)n);
        const cgltf_accessor *wa = cgltf_find_accessor(prim, cgltf_attribute_type_weights, (cgltf_int)n);
        if (ja == NULL || wa == NULL) {
            NT_LOG_ERROR("%s: influence set %u is absent although the primitive carries %u sets; sets run from 0 without gaps", label, n, joint_sets);
            NT_BUILD_ASSERT(0 && "JOINTS_n/WEIGHTS_n sets are not consecutive");
        }
        /* cgltf_validate already holds every attribute to the primitive's count. */
        NT_BUILD_ASSERT(ja->count == (cgltf_size)vertex_count && wa->count == (cgltf_size)vertex_count && "influence accessor covers a different vertex count");
        /* cgltf_accessor_read_uint returns 0 on a FLOAT accessor, so the joint
         * component type is checked before anything reads it. */
        if (ja->type != cgltf_type_vec4 || (ja->component_type != cgltf_component_type_r_8u && ja->component_type != cgltf_component_type_r_16u) || ja->normalized) {
            NT_LOG_ERROR("%s: JOINTS_%u must be VEC4 UNSIGNED_BYTE or UNSIGNED_SHORT and not normalized", label, n);
            NT_BUILD_ASSERT(0 && "JOINTS accessor has an invalid type");
        }
        const bool w_float = wa->component_type == cgltf_component_type_r_32f;
        const bool w_norm_int = (wa->component_type == cgltf_component_type_r_8u || wa->component_type == cgltf_component_type_r_16u) && wa->normalized != 0;
        if (wa->type != cgltf_type_vec4 || (!w_float && !w_norm_int)) {
            NT_LOG_ERROR("%s: WEIGHTS_%u must be VEC4 FLOAT or normalized UNSIGNED_BYTE/UNSIGNED_SHORT", label, n);
            NT_BUILD_ASSERT(0 && "WEIGHTS accessor has an invalid type");
        }
        /* unpack_floats is sparse-capable and exact for u8/u16 integers. */
        const cgltf_size want = (cgltf_size)set_floats;
        const cgltf_size got_joints = cgltf_accessor_unpack_floats(ja, joints + ((size_t)n * set_floats), want);
        if (got_joints != want) {
            NT_LOG_ERROR("%s: JOINTS_%u unpacked %u of %u floats", label, n, (uint32_t)got_joints, (uint32_t)want);
            NT_BUILD_ASSERT(0 && "JOINTS accessor could not be unpacked");
        }
        const cgltf_size got_weights = cgltf_accessor_unpack_floats(wa, weights + ((size_t)n * set_floats), want);
        if (got_weights != want) {
            NT_LOG_ERROR("%s: WEIGHTS_%u unpacked %u of %u floats", label, n, (uint32_t)got_weights, (uint32_t)want);
            NT_BUILD_ASSERT(0 && "WEIGHTS accessor could not be unpacked");
        }
    }

    for (uint32_t v = 0; v < vertex_count; v++) {
        nt_scene_validate_vertex_influences(out, label, v);
    }
}

void nt_builder_free_influences(nt_builder_influences_t *inf) {
    if (inf == NULL) {
        return;
    }
    free(inf->joints);
    free(inf->weights);
    memset(inf, 0, sizeof(*inf));
}

/* One source influence of one vertex, before the reduction. */
typedef struct {
    uint32_t joint;
    float weight;
} nt_scene_influence_t;

/* Four integer lanes summing to exactly 255: floor first, then hand the leftover
 * to the largest fractional parts (lower lane first), so the stored bytes
 * reproduce the renormalized weights as closely as the format allows and a
 * vertex never drifts off the unit sum. */
static void nt_scene_quantize_255(float w[4]) {
    uint32_t k[4];
    double frac[4];
    uint32_t sum = 0;
    for (uint32_t i = 0; i < 4; i++) {
        const double x = (double)w[i] * 255.0;
        k[i] = (uint32_t)x;
        frac[i] = x - (double)k[i];
        sum += k[i];
    }
    while (sum < 255U) {
        uint32_t best = 0;
        for (uint32_t i = 1; i < 4; i++) {
            if (frac[i] > frac[best]) {
                best = i;
            }
        }
        k[best]++;
        frac[best] = -1.0;
        sum++;
    }
    for (uint32_t i = 0; i < 4; i++) {
        w[i] = (float)k[i] / 255.0F;
    }
}

/* Fills the two skin lanes of one primitive: every set is read, each vertex is
 * validated, reduced to its four heaviest influences and renormalized.
 * joints_out and weights_out hold vertex_count * 4 floats each. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_BUILD_ASSERT expansions dominate the count
static void nt_scene_extract_skin(const cgltf_primitive *prim, uint32_t mesh_index, uint32_t primitive_index, uint32_t vertex_count, const nt_builder_skin_ctx_t *skin, bool weights_uint8,
                                  float *joints_out, float *weights_out) {
    char label[64];
    (void)snprintf(label, sizeof(label), "mesh[%u] prim[%u]", mesh_index, primitive_index);

    nt_builder_influences_t inf;
    nt_builder_read_influences(prim, label, vertex_count, &inf);

    const size_t set_floats = (size_t)vertex_count * 4U;
    const uint32_t lane_count = inf.set_count * 4U;
    nt_scene_influence_t *cand = (nt_scene_influence_t *)calloc(lane_count, sizeof(nt_scene_influence_t));
    NT_BUILD_ASSERT(cand && "skin reduction: alloc failed (OOM)");

    uint32_t reduced_count = 0;
    double max_dropped = 0.0;
    for (uint32_t v = 0; v < vertex_count; v++) {
        // #region collect
        /* read_influences already rejected non-finite, negative, duplicated and
         * all-zero weights; the palette bound is the one rule it cannot know. */
        uint32_t n_cand = 0;
        double total = 0.0;
        for (uint32_t s = 0; s < inf.set_count; s++) {
            for (uint32_t c = 0; c < 4U; c++) {
                const size_t at = ((size_t)s * set_floats) + ((size_t)v * 4U) + c;
                const float w = inf.weights[at];
                if (w == 0.0F) {
                    continue;
                }
                const uint32_t joint = (uint32_t)inf.joints[at];
                if (joint >= (uint32_t)skin->palette_count) {
                    NT_LOG_ERROR("%s: vertex %u addresses palette entry %u, the skin has %u", label, v, joint, (uint32_t)skin->palette_count);
                    NT_BUILD_ASSERT(0 && "joint index lies outside the palette");
                }
                cand[n_cand].joint = joint;
                cand[n_cand].weight = w;
                n_cand++;
                total += (double)w;
            }
        }
        // #endregion

        // #region reduce to four
        /* Selection sort of the leading four: heaviest first, ties to the lower
         * joint index, so the export does not depend on attribute order. */
        const uint32_t n_keep = (n_cand < 4U) ? n_cand : 4U;
        for (uint32_t i = 0; i < n_keep; i++) {
            uint32_t best = i;
            for (uint32_t k = i + 1U; k < n_cand; k++) {
                if (cand[k].weight > cand[best].weight || (cand[k].weight == cand[best].weight && cand[k].joint < cand[best].joint)) {
                    best = k;
                }
            }
            const nt_scene_influence_t tmp = cand[i];
            cand[i] = cand[best];
            cand[best] = tmp;
        }

        double kept = 0.0;
        for (uint32_t i = 0; i < n_keep; i++) {
            kept += (double)cand[i].weight;
        }
        /* A vertex that keeps everything drops nothing: total - kept is then
         * float noise, not mass. */
        const double dropped = (n_cand <= 4U) ? 0.0 : (total - kept) / total;
        if (dropped > (double)skin->drop_tolerance) {
            NT_LOG_ERROR("%s: vertex %u has %u influences and loses %.4f of its weight to the four heaviest, tolerance %.4f", label, v, n_cand, dropped, (double)skin->drop_tolerance);
            NT_BUILD_ASSERT(0 && "skinned vertex drops more weight than the tolerance allows");
        }
        if (n_cand > 4U) {
            reduced_count++;
            max_dropped = (dropped > max_dropped) ? dropped : max_dropped;
        }

        float lane_w[4] = {0.0F, 0.0F, 0.0F, 0.0F};
        uint32_t lane_j[4] = {0U, 0U, 0U, 0U};
        for (uint32_t i = 0; i < n_keep; i++) {
            lane_j[i] = cand[i].joint;
            lane_w[i] = (float)((double)cand[i].weight / kept);
        }
        if (weights_uint8) {
            nt_scene_quantize_255(lane_w);
        }
        for (uint32_t c = 0; c < 4U; c++) {
            joints_out[((size_t)v * 4U) + c] = (float)lane_j[c];
            weights_out[((size_t)v * 4U) + c] = lane_w[c];
        }
        // #endregion
    }
    if (reduced_count != 0) {
        NT_LOG_WARN("%s: %u of %u vertices reduced to four influences, dropping at most %.4f of a vertex's weight (tolerance %.4f)", label, reduced_count, vertex_count, max_dropped,
                    (double)skin->drop_tolerance);
    }

    free(cand);
    nt_builder_free_influences(&inf);
}
// #endregion

/* --- Decode: scene mesh -> binary mesh buffer (eager, called from add_scene_mesh) --- */

nt_build_result_t nt_builder_decode_scene_mesh(const nt_glb_scene_t *scene, uint32_t mesh_index, uint32_t primitive_index, const NtStreamLayout *layout, uint32_t stream_count,
                                               nt_tangent_mode_t tangent_mode, uint8_t **out_data, uint32_t *out_size) {
    return nt_builder_decode_scene_mesh_skinned(scene, mesh_index, primitive_index, layout, stream_count, tangent_mode, NULL, out_data, out_size);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_build_result_t nt_builder_decode_scene_mesh_skinned(const nt_glb_scene_t *scene, uint32_t mesh_index, uint32_t primitive_index, const NtStreamLayout *layout, uint32_t stream_count,
                                                       nt_tangent_mode_t tangent_mode, const nt_builder_skin_ctx_t *skin, uint8_t **out_data, uint32_t *out_size) {
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

    NT_BUILD_ASSERT((unsigned)tangent_mode <= (unsigned)NT_TANGENT_REQUIRE && "invalid tangent_mode");

    char label[64];
    (void)snprintf(label, sizeof(label), "scene mesh[%u] prim[%u]", mesh_index, primitive_index);
    nt_build_result_t layout_ret = nt_builder_validate_stream_layout(label, layout, stream_count);
    if (layout_ret != NT_BUILD_OK) {
        return layout_ret;
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

    /* A provenance request that cannot apply is a config contradiction (same rule as add_mesh) */
    NT_BUILD_ASSERT((tangent_stream_idx >= 0 || tangent_mode == NT_TANGENT_AUTO) && "tangent_mode set but the layout has no TANGENT stream");

    /* The skin lanes are produced, not extracted: one JOINTS and one WEIGHTS
     * stream whatever number of JOINTS_n/WEIGHTS_n sets the primitive carries. */
    int32_t joints_stream_idx = -1;
    int32_t weights_stream_idx = -1;
    for (uint32_t s = 0; s < stream_count; s++) {
        if (layout[s].gltf_name == NULL) {
            continue;
        }
        if (strcmp(layout[s].gltf_name, "JOINTS") == 0) {
            joints_stream_idx = (int32_t)s;
        } else if (strcmp(layout[s].gltf_name, "WEIGHTS") == 0) {
            weights_stream_idx = (int32_t)s;
        }
    }
    const bool has_skin_streams = joints_stream_idx >= 0 && weights_stream_idx >= 0;
    NT_BUILD_ASSERT(((joints_stream_idx >= 0) == (weights_stream_idx >= 0)) && "a skinned layout declares both a JOINTS and a WEIGHTS stream");
    NT_BUILD_ASSERT(((skin != NULL) == has_skin_streams) && "skin export and the JOINTS/WEIGHTS streams must agree");

    bool weights_uint8 = false;
    if (has_skin_streams) {
        /* D3: the NtStreamLayout of the two streams is the only authority on how
         * the lanes are stored, so the whole rule set is checked here. */
        const NtStreamLayout *jl = &layout[joints_stream_idx];
        const NtStreamLayout *wl = &layout[weights_stream_idx];
        if (jl->count != 4 || (jl->source_components != 0 && jl->source_components != 4) || wl->count != 4 || (wl->source_components != 0 && wl->source_components != 4)) {
            NT_LOG_ERROR("mesh[%u] prim[%u]: the reduction writes four lanes, the layout declares JOINTS %u and WEIGHTS %u", mesh_index, primitive_index, (uint32_t)jl->count, (uint32_t)wl->count);
            NT_BUILD_ASSERT(0 && "JOINTS and WEIGHTS streams must declare 4 components");
        }
        if ((jl->type != NT_STREAM_UINT8 && jl->type != NT_STREAM_UINT16) || jl->normalized) {
            NT_LOG_ERROR("mesh[%u] prim[%u]: the JOINTS stream is an index, so it must be UINT8 or UINT16 and not normalized", mesh_index, primitive_index);
            NT_BUILD_ASSERT(0 && "JOINTS stream has an invalid type");
        }
        if (jl->type == NT_STREAM_UINT8 && (uint32_t)skin->palette_count > 256U) {
            NT_LOG_ERROR("mesh[%u] prim[%u]: UINT8 joint lanes address 256 palette entries, the skin has %u", mesh_index, primitive_index, (uint32_t)skin->palette_count);
            NT_BUILD_ASSERT(0 && "UINT8 JOINTS stream cannot address this palette");
        }
        weights_uint8 = wl->type == NT_STREAM_UINT8;
        if (!((weights_uint8 && wl->normalized) || wl->type == NT_STREAM_FLOAT16 || wl->type == NT_STREAM_FLOAT32)) {
            NT_LOG_ERROR("mesh[%u] prim[%u]: the WEIGHTS stream must be normalized UINT8, FLOAT16 or FLOAT32", mesh_index, primitive_index);
            NT_BUILD_ASSERT(0 && "WEIGHTS stream has an invalid type");
        }
    }

    /* Determine tangent handling */
    if (tangent_stream_idx >= 0) {
        switch (tangent_mode) {
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

    /* MikkTSpace emits 4 floats per vertex; interleave indexes by layout count -- any other count scrambles data.
     * Narrowing a computed tangent must be declared as source_components=4, like any other 4-component source. */
    if (need_compute_tangent) {
        uint8_t tan_src = layout[tangent_stream_idx].source_components;
        if (tan_src != 0 && tan_src != 4) {
            NT_LOG_ERROR("mesh[%u] prim[%u]: computed TANGENT has 4 source components, source_components must be 0 or 4 (got %u)", mesh_index, primitive_index, (uint32_t)tan_src);
            return NT_BUILD_ERR_VALIDATION;
        }
        if (tan_src == 0 && layout[tangent_stream_idx].count != 4) {
            NT_LOG_ERROR("mesh[%u] prim[%u]: computed TANGENT requires count 4, layout declares %u", mesh_index, primitive_index, (uint32_t)layout[tangent_stream_idx].count);
            return NT_BUILD_ERR_VALIDATION;
        }
    }

    /* Extract vertex streams at SOURCE width -- narrowing is a pack-layout property,
     * so builder computations (MikkTSpace) always see full-width data; streams are
     * compacted to their layout counts only after the tangent block. */
    float *stream_floats[NT_MESH_MAX_STREAMS];
    memset((void *)stream_floats, 0, sizeof(stream_floats));
    uint32_t src_widths[NT_MESH_MAX_STREAMS] = {0};
    uint32_t vertex_count = 0;
    bool vertex_count_set = false;
    nt_build_result_t ret = NT_BUILD_OK;

    for (uint32_t s = 0; s < stream_count; s++) {
        /* Skip TANGENT stream if we need to compute it */
        if ((int32_t)s == tangent_stream_idx && need_compute_tangent) {
            continue;
        }
        /* The skin lanes have no source attribute of their own */
        if ((int32_t)s == joints_stream_idx || (int32_t)s == weights_stream_idx) {
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
        uint32_t src_components = (layout[s].source_components != 0) ? layout[s].source_components : layout[s].count;
        if (acc_components != src_components) {
            NT_LOG_ERROR("mesh[%u] prim[%u]: attribute %s has %u components, layout expects %u", mesh_index, primitive_index, layout[s].gltf_name ? layout[s].gltf_name : "(null)", acc_components,
                         src_components);
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

        /* cgltf can only unpack whole source-width elements; narrow by compacting after */
        cgltf_size float_count = (cgltf_size)count * (cgltf_size)src_components;
        stream_floats[s] = (float *)calloc(float_count, sizeof(float));
        NT_BUILD_ASSERT(stream_floats[s] && "scene mesh: float buffer alloc failed");

        cgltf_size unpacked = cgltf_accessor_unpack_floats(acc, stream_floats[s], float_count);
        if (unpacked == 0) {
            ret = NT_BUILD_ERR_FORMAT;
            goto cleanup_streams;
        }
        src_widths[s] = src_components;
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
            /* limit-check on cgltf_size BEFORE the u32 cast (truncation would bypass it) */
            if (prim->indices->count > (cgltf_size)NT_BUILD_MAX_INDICES) {
                NT_LOG_ERROR("mesh[%u] prim[%u]: index count %zu exceeds max %d", mesh_index, primitive_index, (size_t)prim->indices->count, NT_BUILD_MAX_INDICES);
                ret = NT_BUILD_ERR_LIMIT;
                goto cleanup_streams;
            }
            /* empty index accessor is malformed content, not a non-indexed mesh */
            if (prim->indices->count == 0) {
                NT_LOG_ERROR("mesh[%u] prim[%u]: index accessor is empty", mesh_index, primitive_index);
                ret = NT_BUILD_ERR_VALIDATION;
                goto cleanup_streams;
            }
            index_count = (uint32_t)prim->indices->count;

            if (vertex_count <= 65535) {
                index_type = 1;
                index_data_size = index_count * (uint32_t)sizeof(uint16_t);
            } else {
                index_type = 2;
                index_data_size = index_count * (uint32_t)sizeof(uint32_t);
            }

            index_buf = (uint8_t *)calloc(index_data_size, 1);
            NT_BUILD_ASSERT(index_buf && "scene mesh: index buffer alloc failed");

            ret = nt_builder_unpack_indices(prim->indices, "scene mesh", index_buf, index_count, index_type, vertex_count);
            if (ret != NT_BUILD_OK) {
                free(index_buf);
                goto cleanup_streams;
            }
        }

        /* Compute tangents if needed */
        if (tangent_stream_idx >= 0 && need_compute_tangent) {
            /* Need POSITION, NORMAL, TEXCOORD_0 float data + indices as uint32 */
            float *pos_data = NULL;
            float *norm_data = NULL;
            float *uv_data = NULL;
            uint32_t pos_src = 0;
            uint32_t norm_src = 0;
            uint32_t uv_src = 0;

            for (uint32_t s = 0; s < stream_count; s++) {
                if (layout[s].gltf_name != NULL) {
                    if (strcmp(layout[s].gltf_name, "POSITION") == 0) {
                        pos_data = stream_floats[s];
                        pos_src = src_widths[s];
                    }
                    if (strcmp(layout[s].gltf_name, "NORMAL") == 0) {
                        norm_data = stream_floats[s];
                        norm_src = src_widths[s];
                    }
                    if (strcmp(layout[s].gltf_name, "TEXCOORD_0") == 0) {
                        uv_data = stream_floats[s];
                        uv_src = src_widths[s];
                    }
                }
            }

            if (!pos_data || !norm_data || !uv_data) {
                NT_LOG_ERROR("mesh[%u] prim[%u]: tangent computation requires POSITION, NORMAL, TEXCOORD_0", mesh_index, primitive_index);
                free(index_buf);
                ret = NT_BUILD_ERR_VALIDATION;
                goto cleanup_streams;
            }
            /* MikkTSpace reads fixed float[3]/float[3]/float[2] strides. Streams are still at
             * SOURCE width here, so this holds for any conformant glTF regardless of narrowing. */
            if (pos_src != 3 || norm_src != 3 || uv_src != 2) {
                NT_LOG_ERROR("mesh[%u] prim[%u]: tangent computation requires VEC3 POSITION/NORMAL and VEC2 TEXCOORD_0 sources", mesh_index, primitive_index);
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
            src_widths[tangent_stream_idx] = 4; /* MikkTSpace output */
        }

        /* Skin lanes last: the reduction needs the final vertex count, and the
         * lanes go in at source width so the narrow loop treats them like any
         * other stream. */
        if (has_skin_streams) {
            const size_t lane_floats = (size_t)vertex_count * 4U;
            stream_floats[joints_stream_idx] = (float *)calloc(lane_floats, sizeof(float));
            stream_floats[weights_stream_idx] = (float *)calloc(lane_floats, sizeof(float));
            NT_BUILD_ASSERT(stream_floats[joints_stream_idx] && stream_floats[weights_stream_idx] && "scene mesh: skin lane alloc failed");
            nt_scene_extract_skin(prim, mesh_index, primitive_index, vertex_count, skin, weights_uint8, stream_floats[joints_stream_idx], stream_floats[weights_stream_idx]);
            src_widths[joints_stream_idx] = 4;
            src_widths[weights_stream_idx] = 4;
        }

        /* All computations done -- compact every stream to its declared pack width */
        for (uint32_t s = 0; s < stream_count; s++) {
            nt_builder_narrow_stream_floats(stream_floats[s], vertex_count, src_widths[s], layout[s].count);
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
