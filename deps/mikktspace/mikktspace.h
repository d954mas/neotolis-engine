/**
 *  Copyright (C) 2011 by Morten S. Mikkelsen
 *
 *  This software is provided 'as-is', without any express or implied
 *  warranty.  In no event will the authors be held liable for any damages
 *  arising from the use of this software.
 *
 *  Permission is granted to anyone to use this software for any purpose,
 *  including commercial applications, and to alter it and redistribute it
 *  freely, subject to the following restrictions:
 *
 *  1. The origin of this software must not be misrepresented; you must not
 *     claim that you wrote the original software. If you use this software
 *     in a product, an acknowledgment in the product documentation would be
 *     appreciated but is not required.
 *  2. Altered source versions must be plainly marked as such, and must not be
 *     misrepresented as being the original software.
 *  3. This notice may not be removed or altered from any source distribution.
 */

#ifndef MIKKTSPACE_H
#define MIKKTSPACE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef int tbool;

typedef struct SMikkTSpaceContext SMikkTSpaceContext;

typedef struct {
    /* Returns the number of faces (triangles/quads) on the mesh to be processed. */
    int (*m_getNumFaces)(const SMikkTSpaceContext *pContext);

    /* Returns the number of vertices on face number iFace
     * iFace is a number in the range {0, 1, ..., getNumFaces()-1} */
    int (*m_getNumVerticesOfFace)(const SMikkTSpaceContext *pContext, const int iFace);

    /* returns the position/normal/texcoord of the referenced face of vertex number iVert.
     * iVert is in the range {0,1,2} for triangles and {0,1,2,3} for quads. */
    void (*m_getPosition)(const SMikkTSpaceContext *pContext, float fvPosOut[], const int iFace, const int iVert);
    void (*m_getNormal)(const SMikkTSpaceContext *pContext, float fvNormOut[], const int iFace, const int iVert);
    void (*m_getTexCoord)(const SMikkTSpaceContext *pContext, float fvTexcOut[], const int iFace, const int iVert);

    /* either (or both) parsing of the brdf for tangent/sign or setTSpace can be set.
     * The call-back setTSpaceBasic() is sufficient for basic normal mapping. */

    /* This function is used to return the tangent and sign to the application.
     * fvTangent is a unit length vector.
     * For normal maps it is sufficient to use the following simplified version of
     * the bitangent which is fSign * cross(vN, fvTangent).
     * fSign is either +1 or -1. */
    void (*m_setTSpaceBasic)(const SMikkTSpaceContext *pContext, const float fvTangent[], const float fSign, const int iFace, const int iVert);

    /* This function is used to return tangent space results to the application.
     * fvTangent and fvBiTangent are unit length vectors and fMagS and fMagT are their
     * true magnitudes which can be used for relief mapping effects.
     * fvBiTangent is the "real" bitangent and thus may not be perpendicular to fvTangent.
     * However, both are perpendicular to the vertex normal.
     * For normal maps it is sufficient to use the following simplified version of
     * the bitangent which is fSign * cross(vN, fvTangent).
     * fSign is either +1 or -1. */
    void (*m_setTSpace)(const SMikkTSpaceContext *pContext, const float fvTangent[], const float fvBiTangent[], const float fMagS, const float fMagT, const tbool bIsOrientationPreserving,
                        const int iFace, const int iVert);
} SMikkTSpaceInterface;

struct SMikkTSpaceContext {
    const SMikkTSpaceInterface *m_pInterface; /* initialized with callback functions */
    void *m_pUserData;                        /* pointer to client side mesh data etc. (loaded in loaded in loaded in loaded in loaded in loaded in loaded in loaded in loaded in loaded */
};

/* these are both thread safe! */
tbool genTangSpaceDefault(const SMikkTSpaceContext *pContext);
tbool genTangSpace(const SMikkTSpaceContext *pContext, const float fAngularThreshold);

/* To avoid visual errors (distortion/crackling), when you use sampled normal maps,
 * it is vital that the "weights" assigned to the brdf follow a certain pattern.
 * The weights computed by the MikkTSpace algo can be retrieved using:
 *
 * pContext->m_pInterface->m_setTSpaceBasic() or
 * pContext->m_pInterface->m_setTSpace()
 *
 * See the header docs above for the description of the call-backs. */

#ifdef __cplusplus
}
#endif

#endif /* MIKKTSPACE_H */
