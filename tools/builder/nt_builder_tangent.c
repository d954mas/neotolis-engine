/* clang-format off */
#include "nt_builder_internal.h"
#include "mikktspace.h"
#include "log/nt_log.h"
/* clang-format on */

/* --- MikkTSpace callback context --- */

typedef struct {
    const float *positions;  /* float[vertex_count * 3] */
    const float *normals;    /* float[vertex_count * 3] */
    const float *uvs;        /* float[vertex_count * 2] */
    const uint32_t *indices; /* uint32_t[index_count] */
    uint32_t vertex_count;
    uint32_t index_count;
    float *out_tangents; /* float[vertex_count * 4] -- output */
} NtMikkUserData;

/* --- MikkTSpace callbacks --- */

static int nt_mikk_get_num_faces(const SMikkTSpaceContext *pContext) {
    const NtMikkUserData *ud = (const NtMikkUserData *)pContext->m_pUserData;
    return (int)(ud->index_count / 3);
}

static int nt_mikk_get_num_vertices_of_face(const SMikkTSpaceContext *pContext, const int iFace) {
    (void)pContext;
    (void)iFace;
    return 3; /* triangles only */
}

static void nt_mikk_get_position(const SMikkTSpaceContext *pContext, float fvPosOut[], const int iFace, const int iVert) {
    const NtMikkUserData *ud = (const NtMikkUserData *)pContext->m_pUserData;
    uint32_t idx = ud->indices[((uint32_t)iFace * 3) + (uint32_t)iVert];
    fvPosOut[0] = ud->positions[(idx * 3) + 0];
    fvPosOut[1] = ud->positions[(idx * 3) + 1];
    fvPosOut[2] = ud->positions[(idx * 3) + 2];
}

static void nt_mikk_get_normal(const SMikkTSpaceContext *pContext, float fvNormOut[], const int iFace, const int iVert) {
    const NtMikkUserData *ud = (const NtMikkUserData *)pContext->m_pUserData;
    uint32_t idx = ud->indices[((uint32_t)iFace * 3) + (uint32_t)iVert];
    fvNormOut[0] = ud->normals[(idx * 3) + 0];
    fvNormOut[1] = ud->normals[(idx * 3) + 1];
    fvNormOut[2] = ud->normals[(idx * 3) + 2];
}

static void nt_mikk_get_texcoord(const SMikkTSpaceContext *pContext, float fvTexcOut[], const int iFace, const int iVert) {
    const NtMikkUserData *ud = (const NtMikkUserData *)pContext->m_pUserData;
    uint32_t idx = ud->indices[((uint32_t)iFace * 3) + (uint32_t)iVert];
    fvTexcOut[0] = ud->uvs[(idx * 2) + 0];
    fvTexcOut[1] = ud->uvs[(idx * 2) + 1];
}

static void nt_mikk_set_tspace_basic(const SMikkTSpaceContext *pContext, const float fvTangent[], const float fSign, const int iFace, const int iVert) {
    NtMikkUserData *ud = (NtMikkUserData *)pContext->m_pUserData;
    uint32_t idx = ud->indices[((uint32_t)iFace * 3) + (uint32_t)iVert];
    ud->out_tangents[(idx * 4) + 0] = fvTangent[0];
    ud->out_tangents[(idx * 4) + 1] = fvTangent[1];
    ud->out_tangents[(idx * 4) + 2] = fvTangent[2];
    ud->out_tangents[(idx * 4) + 3] = fSign;
}

/* --- Public tangent computation function --- */

nt_build_result_t nt_builder_compute_tangents(const float *positions, const float *normals, const float *uvs, const uint32_t *indices, uint32_t vertex_count, uint32_t index_count,
                                              float *out_tangents) {
    if (!positions || !normals || !uvs || !indices || !out_tangents) {
        return NT_BUILD_ERR_VALIDATION;
    }
    if (vertex_count == 0 || index_count == 0 || (index_count % 3) != 0) {
        return NT_BUILD_ERR_VALIDATION;
    }

    /* Initialize output tangents to zero */
    memset(out_tangents, 0, ((size_t)vertex_count * 4) * sizeof(float));

    NtMikkUserData user_data;
    user_data.positions = positions;
    user_data.normals = normals;
    user_data.uvs = uvs;
    user_data.indices = indices;
    user_data.vertex_count = vertex_count;
    user_data.index_count = index_count;
    user_data.out_tangents = out_tangents;

    SMikkTSpaceInterface iface;
    memset(&iface, 0, sizeof(iface));
    iface.m_getNumFaces = nt_mikk_get_num_faces;
    iface.m_getNumVerticesOfFace = nt_mikk_get_num_vertices_of_face;
    iface.m_getPosition = nt_mikk_get_position;
    iface.m_getNormal = nt_mikk_get_normal;
    iface.m_getTexCoord = nt_mikk_get_texcoord;
    iface.m_setTSpaceBasic = nt_mikk_set_tspace_basic;
    iface.m_setTSpace = NULL;

    SMikkTSpaceContext context;
    context.m_pInterface = &iface;
    context.m_pUserData = &user_data;

    tbool result = genTangSpaceDefault(&context);
    if (!result) {
        NT_LOG_ERROR("MikkTSpace tangent computation failed");
        return NT_BUILD_ERR_VALIDATION;
    }

    return NT_BUILD_OK;
}
