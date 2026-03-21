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

#include "mikktspace.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

#define INTERNAL_RND_SORT_SEED 39871946

/* internal structure */
typedef struct {
    int iNrFaces;
    int *pTriMembers;
} SSubGroup;

typedef struct {
    int iNrFaces;
    int *pFaceIndices;
    int iVertexRepresentitive;
    tbool bOrientPreservering;
} SGroup;

/* internal helpers */
#define MARK_DEGENERATE 1
#define QUAD_ONE_DEGEN_TRI 2
#define GROUP_WITH_ANY 4

typedef struct {
    int iFace;
    int iVert;
    int index;
} STmpVert;

/* TriInfo structure */
typedef struct {
    int FaceNeighbors[3];
    SGroup *AssignedGroup[3];

    /* projected area significance -- also used as a flag for degenerates */
    float fMagS, fMagT;

    /* which vertices are degenerate */
    int iOrgFaceNumber;
    int iFlag, iTSpacesOffs;
    unsigned char vert_num[4];
} STriInfo;

/* function prototypes */
static int MakeIndex(const int iFace, const int iVert);
static void IndexToData(int *piFace, int *piVert, const int iIndexIn);
static STmpVert IsNotZero(const float fX, const float fY, const float fZ);
static tbool VNotZero(const float fX, const float fY, const float fZ);
static float LengthSquared(const float fX, const float fY, const float fZ);
static float Length(const float fX, const float fY, const float fZ);
static int FindGridCell(const float fMin, const float fMax, const float fVal);
static void MergeVertsFast(int piTriList_in_and_out[], STmpVert pTmpVert[], const SMikkTSpaceContext *pContext, const int iL_in, const int iR_in);
static void MergeVertsSlow(int piTriList_in_and_out[], const SMikkTSpaceContext *pContext, const int pTable[], const int iEntries);
static void GenerateSharedVerticesIndexListSlow(int piTriList_in_and_out[], const SMikkTSpaceContext *pContext, const int iNrTrianglesIn);
static void GenerateSharedVerticesIndexList(int piTriList_in_and_out[], const SMikkTSpaceContext *pContext, const int iNrTrianglesIn);
static int GenerateInitialVerticesIndexList(STriInfo pTriInfos[], int piTriList_out[], const SMikkTSpaceContext *pContext, const int iNrTrianglesIn);
static void InitTriInfo(STriInfo pTriInfos[], const int piTriListIn[], const SMikkTSpaceContext *pContext, const int iNrTrianglesIn);
static int Build4RuleGroups(STriInfo pTriInfos[], SGroup pGroups[], int piGroupTrianglesBuffer[], const int piTriListIn[], const int iNrTrianglesIn, const int iNrMaxGroups);
static tbool AssignRecur(const int piTriListIn[], STriInfo psTriInfos[], const int iMyTriIndex, SGroup *pGroup);
static void AddTriToGroup(SGroup *pGroup, const int iTriIndex);
static int InitGroupsAndAvgTS(STriInfo pTriInfos[], SGroup pGroups[], int piGroupTrianglesBuffer[], const int piTriListIn[], const SMikkTSpaceContext *pContext, const int iNrTrianglesIn,
                              const int iNrMaxGroups);
static tbool EvalTspace(float fTs[], int iFaces[], const int iFacesSize, const int piTriListIn[], const STriInfo pTriInfos[], const SMikkTSpaceContext *pContext, const int iVertexRepresentitive);

static int MakeIndex(const int iFace, const int iVert) { return (iFace << 2) | (iVert & 0x3); }

static void IndexToData(int *piFace, int *piVert, const int iIndexIn) {
    *piVert = iIndexIn & 0x3;
    *piFace = iIndexIn >> 2;
}

static STmpVert IsNotZero(const float fX, const float fY, const float fZ) {
    STmpVert r;
    r.iFace = 0;
    r.iVert = 0;
    r.index = ((fX != 0) || (fY != 0) || (fZ != 0)) ? 1 : 0;
    return r;
}

static tbool VNotZero(const float fX, const float fY, const float fZ) { return ((fX != 0) || (fY != 0) || (fZ != 0)) ? 1 : 0; }

static float LengthSquared(const float fX, const float fY, const float fZ) { return fX * fX + fY * fY + fZ * fZ; }

static float Length(const float fX, const float fY, const float fZ) { return sqrtf(fX * fX + fY * fY + fZ * fZ); }

static int FindGridCell(const float fMin, const float fMax, const float fVal) {
    const float fIndex = 127 * ((fVal - fMin) / (fMax - fMin));
    const int iIndex = fIndex < 0 ? 0 : ((int)fIndex);
    return iIndex < 128 ? iIndex : 127;
}

/* tangent space generation entry points */

tbool genTangSpaceDefault(const SMikkTSpaceContext *pContext) { return genTangSpace(pContext, (float)(180.0 * M_PI / 180.0)); }

tbool genTangSpace(const SMikkTSpaceContext *pContext, const float fAngularThreshold) {
    /* count nr_tri */
    int *piTriListIn = NULL;
    STriInfo *pTriInfos = NULL;
    SGroup *pGroups = NULL;
    int *piGroupTrianglesBuffer = NULL;
    int iNrTrianglesIn = 0, f = 0, t = 0, i = 0;
    int iNrTSPaces = 0, iTotTris = 0, iDegenTriangles = 0, iNrMaxGroups = 0;
    int iNrActiveGroups = 0, index = 0;
    const int iNrFaces = pContext->m_pInterface->m_getNumFaces(pContext);
    tbool bRes = 0;
    const float fThresCos = cosf(fAngularThreshold);

    /* count triangles */
    for (f = 0; f < iNrFaces; f++) {
        const int verts = pContext->m_pInterface->m_getNumVerticesOfFace(pContext, f);
        if (verts == 3) {
            ++iNrTrianglesIn;
        } else if (verts == 4) {
            iNrTrianglesIn += 2;
        }
    }
    if (iNrTrianglesIn <= 0) {
        return 0;
    }

    piTriListIn = (int *)malloc(sizeof(int) * 3 * (size_t)iNrTrianglesIn);
    pTriInfos = (STriInfo *)malloc(sizeof(STriInfo) * (size_t)iNrTrianglesIn);
    if (piTriListIn == NULL || pTriInfos == NULL) {
        free(piTriListIn);
        free(pTriInfos);
        return 0;
    }

    /* make initial trilists + set up data */
    iNrTSPaces = GenerateInitialVerticesIndexList(pTriInfos, piTriListIn, pContext, iNrTrianglesIn);

    /* make trilists */
    GenerateSharedVerticesIndexList(piTriListIn, pContext, iNrTrianglesIn);

    /* init triinfo */
    InitTriInfo(pTriInfos, piTriListIn, pContext, iNrTrianglesIn);

    /* count active triangles */
    iTotTris = iNrTrianglesIn;
    iDegenTriangles = 0;
    for (t = 0; t < iTotTris; t++) {
        if ((pTriInfos[t].iFlag & MARK_DEGENERATE) != 0) {
            ++iDegenTriangles;
        }
    }
    iNrMaxGroups = iNrTrianglesIn * 3;

    /* build groups */
    pGroups = (SGroup *)malloc(sizeof(SGroup) * (size_t)iNrMaxGroups);
    piGroupTrianglesBuffer = (int *)malloc(sizeof(int) * (size_t)(iNrTrianglesIn * 3));
    if (pGroups == NULL || piGroupTrianglesBuffer == NULL) {
        free(piTriListIn);
        free(pTriInfos);
        free(pGroups);
        free(piGroupTrianglesBuffer);
        return 0;
    }

    iNrActiveGroups = Build4RuleGroups(pTriInfos, pGroups, piGroupTrianglesBuffer, piTriListIn, iNrTrianglesIn, iNrMaxGroups);

    /* compute tangent spaces for each group */
    {
        float *psTSpace = (float *)malloc(sizeof(float) * 8 * (size_t)iNrTSPaces);
        if (psTSpace == NULL) {
            free(piTriListIn);
            free(pTriInfos);
            free(pGroups);
            free(piGroupTrianglesBuffer);
            return 0;
        }
        memset(psTSpace, 0, sizeof(float) * 8 * (size_t)iNrTSPaces);

        for (i = 0; i < iNrTSPaces; i++) {
            psTSpace[i * 8 + 0] = 1.0f;
            psTSpace[i * 8 + 4] = 1.0f;
        }

        for (i = 0; i < iNrActiveGroups; i++) {
            const SGroup *pGrp = &pGroups[i];
            int iFaces[16];
            int iMembers = pGrp->iNrFaces;
            int j;

            /* if more than 16, evaluate in chunks */
            if (iMembers > 16) {
                float *pFaceTsRes = (float *)malloc(sizeof(float) * 8);
                if (pFaceTsRes != NULL) {
                    int iRemaining = iMembers;
                    int iOffs = 0;
                    while (iRemaining > 0) {
                        int iFacesChunk[16];
                        int iChunkSize = iRemaining > 16 ? 16 : iRemaining;
                        int k;
                        for (k = 0; k < iChunkSize; k++) {
                            iFacesChunk[k] = pGrp->pFaceIndices[iOffs + k];
                        }
                        if (EvalTspace(pFaceTsRes, iFacesChunk, iChunkSize, piTriListIn, pTriInfos, pContext, pGrp->iVertexRepresentitive)) {
                            /* accumulate */
                            int idx = pTriInfos[iFacesChunk[0]].iTSpacesOffs;
                            int iVert = pTriInfos[iFacesChunk[0]].vert_num[0];
                            /* find the right tspace slot for this group */
                            int g;
                            for (g = 0; g < 3; g++) {
                                int iF, iV;
                                IndexToData(&iF, &iV, piTriListIn[iFacesChunk[0] * 3 + g]);
                                if (pTriInfos[iFacesChunk[0]].AssignedGroup[g] == pGrp) {
                                    idx = pTriInfos[iF].iTSpacesOffs + iV;
                                    break;
                                }
                            }
                            for (k = 0; k < 8; k++) {
                                psTSpace[idx * 8 + k] += pFaceTsRes[k];
                            }
                        }
                        iOffs += iChunkSize;
                        iRemaining -= iChunkSize;
                    }
                    free(pFaceTsRes);
                }
            } else {
                float fTs[8];
                for (j = 0; j < iMembers; j++) {
                    iFaces[j] = pGrp->pFaceIndices[j];
                }
                if (EvalTspace(fTs, iFaces, iMembers, piTriListIn, pTriInfos, pContext, pGrp->iVertexRepresentitive)) {
                    /* find which tspace slot this group corresponds to */
                    int idx = 0;
                    int g;
                    for (g = 0; g < 3; g++) {
                        int iF, iV;
                        IndexToData(&iF, &iV, piTriListIn[iFaces[0] * 3 + g]);
                        if (pTriInfos[iFaces[0]].AssignedGroup[g] == pGrp) {
                            idx = pTriInfos[iF].iTSpacesOffs + iV;
                            break;
                        }
                    }
                    psTSpace[idx * 8 + 0] = fTs[0];
                    psTSpace[idx * 8 + 1] = fTs[1];
                    psTSpace[idx * 8 + 2] = fTs[2];
                    psTSpace[idx * 8 + 3] = fTs[3];
                    psTSpace[idx * 8 + 4] = fTs[4];
                    psTSpace[idx * 8 + 5] = fTs[5];
                    psTSpace[idx * 8 + 6] = fTs[6];
                    psTSpace[idx * 8 + 7] = fTs[7];
                }
            }
        }

        /* set tspace */
        index = 0;
        for (f = 0; f < iNrFaces; f++) {
            const int verts = pContext->m_pInterface->m_getNumVerticesOfFace(pContext, f);
            if (verts != 3 && verts != 4) {
                continue;
            }
            for (i = 0; i < verts; i++) {
                float tang[3], bitang[3];
                float fLenT, fLenB;
                float fTs0 = psTSpace[index * 8 + 0];
                float fTs1 = psTSpace[index * 8 + 1];
                float fTs2 = psTSpace[index * 8 + 2];
                float fSign = psTSpace[index * 8 + 3];
                float fBs0 = psTSpace[index * 8 + 4];
                float fBs1 = psTSpace[index * 8 + 5];
                float fBs2 = psTSpace[index * 8 + 6];

                fLenT = Length(fTs0, fTs1, fTs2);
                fLenB = Length(fBs0, fBs1, fBs2);

                if (fLenT > 0) {
                    tang[0] = fTs0 / fLenT;
                    tang[1] = fTs1 / fLenT;
                    tang[2] = fTs2 / fLenT;
                } else {
                    tang[0] = 1;
                    tang[1] = 0;
                    tang[2] = 0;
                }
                if (fLenB > 0) {
                    bitang[0] = fBs0 / fLenB;
                    bitang[1] = fBs1 / fLenB;
                    bitang[2] = fBs2 / fLenB;
                } else {
                    bitang[0] = 0;
                    bitang[1] = 1;
                    bitang[2] = 0;
                }

                if (pContext->m_pInterface->m_setTSpaceBasic != NULL) {
                    pContext->m_pInterface->m_setTSpaceBasic(pContext, tang, fSign > 0 ? 1.0f : (-1.0f), f, i);
                }
                if (pContext->m_pInterface->m_setTSpace != NULL) {
                    pContext->m_pInterface->m_setTSpace(pContext, tang, bitang, fLenT, fLenB, fSign > 0 ? 1 : 0, f, i);
                }

                ++index;
            }
        }

        free(psTSpace);
    }

    free(piTriListIn);
    free(pTriInfos);
    free(pGroups);
    free(piGroupTrianglesBuffer);

    return 1;
}

/* ====================================================================== */
/* internal helper implementations */

static int GenerateInitialVerticesIndexList(STriInfo pTriInfos[], int piTriList_out[], const SMikkTSpaceContext *pContext, const int iNrTrianglesIn) {
    int iTSpacesOffs = 0, f = 0, t = 0;
    const int iNrFaces = pContext->m_pInterface->m_getNumFaces(pContext);

    for (f = 0; f < iNrFaces; f++) {
        const int verts = pContext->m_pInterface->m_getNumVerticesOfFace(pContext, f);
        if (verts != 3 && verts != 4) {
            continue;
        }

        pTriInfos[t].iOrgFaceNumber = f;
        pTriInfos[t].iTSpacesOffs = iTSpacesOffs;

        if (verts == 3) {
            /* 0, 1, 2 */
            unsigned char *pVerts = pTriInfos[t].vert_num;
            pVerts[0] = 0;
            pVerts[1] = 1;
            pVerts[2] = 2;
            piTriList_out[t * 3 + 0] = MakeIndex(f, 0);
            piTriList_out[t * 3 + 1] = MakeIndex(f, 1);
            piTriList_out[t * 3 + 2] = MakeIndex(f, 2);
            ++t;
        } else {
            /* 0, 1, 2 and 0, 2, 3 */
            {
                unsigned char *pVerts = pTriInfos[t].vert_num;
                pVerts[0] = 0;
                pVerts[1] = 1;
                pVerts[2] = 2;
                piTriList_out[t * 3 + 0] = MakeIndex(f, 0);
                piTriList_out[t * 3 + 1] = MakeIndex(f, 1);
                piTriList_out[t * 3 + 2] = MakeIndex(f, 2);
                ++t;
            }
            {
                unsigned char *pVerts = pTriInfos[t].vert_num;
                pVerts[0] = 0;
                pVerts[1] = 2;
                pVerts[2] = 3;
                piTriList_out[t * 3 + 0] = MakeIndex(f, 0);
                piTriList_out[t * 3 + 1] = MakeIndex(f, 2);
                piTriList_out[t * 3 + 2] = MakeIndex(f, 3);
                ++t;
            }
        }

        iTSpacesOffs += verts;
    }

    assert(t == iNrTrianglesIn);
    return iTSpacesOffs;
}

static void GenerateSharedVerticesIndexListSlow(int piTriList_in_and_out[], const SMikkTSpaceContext *pContext, const int iNrTrianglesIn) {
    int t, i;
    for (t = 0; t < iNrTrianglesIn; t++) {
        for (i = 0; i < 3; i++) {
            int iF_A, iV_A;
            float vP_A[3], vN_A[3], vT_A[2];
            int t2, i2, index;
            IndexToData(&iF_A, &iV_A, piTriList_in_and_out[t * 3 + i]);
            pContext->m_pInterface->m_getPosition(pContext, vP_A, iF_A, iV_A);
            pContext->m_pInterface->m_getNormal(pContext, vN_A, iF_A, iV_A);
            pContext->m_pInterface->m_getTexCoord(pContext, vT_A, iF_A, iV_A);

            /* find first match (including self) */
            index = piTriList_in_and_out[t * 3 + i];
            for (t2 = 0; t2 <= t; t2++) {
                int iLim = (t2 == t) ? i : 3;
                int i2_start;
                for (i2_start = 0; i2_start < iLim; i2_start++) {
                    int iF_B, iV_B;
                    float vP_B[3], vN_B[3], vT_B[2];
                    IndexToData(&iF_B, &iV_B, piTriList_in_and_out[t2 * 3 + i2_start]);
                    pContext->m_pInterface->m_getPosition(pContext, vP_B, iF_B, iV_B);
                    pContext->m_pInterface->m_getNormal(pContext, vN_B, iF_B, iV_B);
                    pContext->m_pInterface->m_getTexCoord(pContext, vT_B, iF_B, iV_B);

                    if (vP_A[0] == vP_B[0] && vP_A[1] == vP_B[1] && vP_A[2] == vP_B[2] && vN_A[0] == vN_B[0] && vN_A[1] == vN_B[1] && vN_A[2] == vN_B[2] && vT_A[0] == vT_B[0] &&
                        vT_A[1] == vT_B[1]) {
                        index = piTriList_in_and_out[t2 * 3 + i2_start];
                        break;
                    }
                }
                if (i2_start < iLim) {
                    break;
                }
            }
            piTriList_in_and_out[t * 3 + i] = index;
        }
    }
}

static void GenerateSharedVerticesIndexList(int piTriList_in_and_out[], const SMikkTSpaceContext *pContext, const int iNrTrianglesIn) {
    /* slow path for all mesh sizes in this simplified version */
    GenerateSharedVerticesIndexListSlow(piTriList_in_and_out, pContext, iNrTrianglesIn);
}

static void InitTriInfo(STriInfo pTriInfos[], const int piTriListIn[], const SMikkTSpaceContext *pContext, const int iNrTrianglesIn) {
    int f, t, i;
    for (t = 0; t < iNrTrianglesIn; t++) {
        float p0[3], p1[3], p2[3], d1[3], d2[3];
        float fSignedAreaSTx2;
        float t0[2], t1[2], t2[2], dt1[2], dt2[2];
        int iF_0, iV_0, iF_1, iV_1, iF_2, iV_2;

        pTriInfos[t].iFlag = 0;
        pTriInfos[t].AssignedGroup[0] = NULL;
        pTriInfos[t].AssignedGroup[1] = NULL;
        pTriInfos[t].AssignedGroup[2] = NULL;

        pTriInfos[t].FaceNeighbors[0] = -1;
        pTriInfos[t].FaceNeighbors[1] = -1;
        pTriInfos[t].FaceNeighbors[2] = -1;

        IndexToData(&iF_0, &iV_0, piTriListIn[t * 3 + 0]);
        IndexToData(&iF_1, &iV_1, piTriListIn[t * 3 + 1]);
        IndexToData(&iF_2, &iV_2, piTriListIn[t * 3 + 2]);

        pContext->m_pInterface->m_getPosition(pContext, p0, iF_0, iV_0);
        pContext->m_pInterface->m_getPosition(pContext, p1, iF_1, iV_1);
        pContext->m_pInterface->m_getPosition(pContext, p2, iF_2, iV_2);
        pContext->m_pInterface->m_getTexCoord(pContext, t0, iF_0, iV_0);
        pContext->m_pInterface->m_getTexCoord(pContext, t1, iF_1, iV_1);
        pContext->m_pInterface->m_getTexCoord(pContext, t2, iF_2, iV_2);

        d1[0] = p1[0] - p0[0];
        d1[1] = p1[1] - p0[1];
        d1[2] = p1[2] - p0[2];
        d2[0] = p2[0] - p0[0];
        d2[1] = p2[1] - p0[1];
        d2[2] = p2[2] - p0[2];

        dt1[0] = t1[0] - t0[0];
        dt1[1] = t1[1] - t0[1];
        dt2[0] = t2[0] - t0[0];
        dt2[1] = t2[1] - t0[1];

        fSignedAreaSTx2 = dt1[0] * dt2[1] - dt1[1] * dt2[0];

        {
            float vOs[3], vOt[3];
            float fLenOs, fLenOt;

            if (fSignedAreaSTx2 > 0) {
                pTriInfos[t].iFlag |= GROUP_WITH_ANY;
            }

            if (fabsf(fSignedAreaSTx2) > 1e-10f) {
                float fInvArea = 1.0f / fSignedAreaSTx2;
                vOs[0] = fInvArea * (dt2[1] * d1[0] - dt1[1] * d2[0]);
                vOs[1] = fInvArea * (dt2[1] * d1[1] - dt1[1] * d2[1]);
                vOs[2] = fInvArea * (dt2[1] * d1[2] - dt1[1] * d2[2]);
                vOt[0] = fInvArea * (-dt2[0] * d1[0] + dt1[0] * d2[0]);
                vOt[1] = fInvArea * (-dt2[0] * d1[1] + dt1[0] * d2[1]);
                vOt[2] = fInvArea * (-dt2[0] * d1[2] + dt1[0] * d2[2]);
            } else {
                vOs[0] = 0;
                vOs[1] = 0;
                vOs[2] = 0;
                vOt[0] = 0;
                vOt[1] = 0;
                vOt[2] = 0;
            }

            fLenOs = Length(vOs[0], vOs[1], vOs[2]);
            fLenOt = Length(vOt[0], vOt[1], vOt[2]);

            pTriInfos[t].fMagS = fLenOs;
            pTriInfos[t].fMagT = fLenOt;

            /* check for degenerate */
            if (fLenOs < 1e-10f || fLenOt < 1e-10f) {
                pTriInfos[t].iFlag |= MARK_DEGENERATE;
            }
        }
    }

    /* build neighbor info (edges) - simplified */
    for (t = 0; t < iNrTrianglesIn; t++) {
        if ((pTriInfos[t].iFlag & MARK_DEGENERATE) != 0) {
            continue;
        }
        for (i = 0; i < 3; i++) {
            int i0 = piTriListIn[t * 3 + i];
            int i1 = piTriListIn[t * 3 + ((i + 1) % 3)];
            int t2;
            for (t2 = t + 1; t2 < iNrTrianglesIn; t2++) {
                int j;
                if ((pTriInfos[t2].iFlag & MARK_DEGENERATE) != 0) {
                    continue;
                }
                for (j = 0; j < 3; j++) {
                    int j0 = piTriListIn[t2 * 3 + j];
                    int j1 = piTriListIn[t2 * 3 + ((j + 1) % 3)];
                    if ((i0 == j1 && i1 == j0)) {
                        pTriInfos[t].FaceNeighbors[i] = t2;
                        pTriInfos[t2].FaceNeighbors[j] = t;
                    }
                }
            }
        }
    }
}

static void AddTriToGroup(SGroup *pGroup, const int iTriIndex) { pGroup->pFaceIndices[pGroup->iNrFaces] = iTriIndex; ++pGroup->iNrFaces; }

static tbool AssignRecur(const int piTriListIn[], STriInfo psTriInfos[], const int iMyTriIndex, SGroup *pGroup) {
    STriInfo *pMyTriInfo = &psTriInfos[iMyTriIndex];
    int i;

    if (pMyTriInfo->AssignedGroup[0] == pGroup || pMyTriInfo->AssignedGroup[1] == pGroup || pMyTriInfo->AssignedGroup[2] == pGroup) {
        return 1;
    }

    /* which slot? */
    for (i = 0; i < 3; i++) {
        int iVertRep = pGroup->iVertexRepresentitive;
        int iIndex = piTriListIn[iMyTriIndex * 3 + i];
        if (iIndex == iVertRep) {
            break;
        }
    }
    if (i == 3) {
        return 0;
    }

    if (pMyTriInfo->AssignedGroup[i] != NULL) {
        return 0;
    }

    /* check orient compat */
    {
        tbool bOrPre = (pMyTriInfo->iFlag & GROUP_WITH_ANY) != 0 ? 1 : 0;
        if (bOrPre != pGroup->bOrientPreservering) {
            return 0;
        }
    }

    AddTriToGroup(pGroup, iMyTriIndex);
    pMyTriInfo->AssignedGroup[i] = pGroup;

    {
        int iNeighbor;
        for (i = 0; i < 3; i++) {
            iNeighbor = pMyTriInfo->FaceNeighbors[i];
            if (iNeighbor >= 0) {
                AssignRecur(piTriListIn, psTriInfos, iNeighbor, pGroup);
            }
        }
    }

    return 1;
}

static int Build4RuleGroups(STriInfo pTriInfos[], SGroup pGroups[], int piGroupTrianglesBuffer[], const int piTriListIn[], const int iNrTrianglesIn, const int iNrMaxGroups) {
    int iNrActiveGroups = 0;
    int iOffset = 0, t, i;

    for (t = 0; t < iNrTrianglesIn; t++) {
        if ((pTriInfos[t].iFlag & MARK_DEGENERATE) != 0) {
            continue;
        }

        for (i = 0; i < 3; i++) {
            if (pTriInfos[t].AssignedGroup[i] == NULL) {
                int iVertRep = piTriListIn[t * 3 + i];
                tbool bOrPre = (pTriInfos[t].iFlag & GROUP_WITH_ANY) != 0 ? 1 : 0;
                SGroup *pGrp;

                assert(iNrActiveGroups < iNrMaxGroups);
                pGrp = &pGroups[iNrActiveGroups];
                pGrp->iVertexRepresentitive = iVertRep;
                pGrp->bOrientPreservering = bOrPre;
                pGrp->iNrFaces = 0;
                pGrp->pFaceIndices = &piGroupTrianglesBuffer[iOffset];
                ++iNrActiveGroups;

                AddTriToGroup(pGrp, t);
                pTriInfos[t].AssignedGroup[i] = pGrp;

                {
                    int iNeighbor = pTriInfos[t].FaceNeighbors[i];
                    if (iNeighbor >= 0) {
                        AssignRecur(piTriListIn, pTriInfos, iNeighbor, pGrp);
                    }
                }

                iOffset += pGrp->iNrFaces;
            }
        }
    }

    return iNrActiveGroups;
}

static tbool EvalTspace(float fTs[], int iFaces[], const int iFacesSize, const int piTriListIn[], const STriInfo pTriInfos[], const SMikkTSpaceContext *pContext, const int iVertexRepresentitive) {
    float n[3], vOs[3], vOt[3];
    float fRes1[3] = {0, 0, 0};
    float fRes2[3] = {0, 0, 0};
    float fAngleSum = 0;
    int face, i;

    /* get normal at representative vertex */
    {
        int iF, iV;
        IndexToData(&iF, &iV, iVertexRepresentitive);
        pContext->m_pInterface->m_getNormal(pContext, n, iF, iV);
    }

    for (face = 0; face < iFacesSize; face++) {
        const int f = iFaces[face];
        float p0[3], p1[3], p2[3], v1[3], v2[3];
        float fCos, fAngle, fMagS, fMagT;
        int iF_0, iV_0, iF_1, iV_1, iF_2, iV_2;
        float t0[2], t1[2], t2[2], dt1[2], dt2[2];
        float fSignedAreaSTx2;

        if ((pTriInfos[f].iFlag & MARK_DEGENERATE) != 0) {
            continue;
        }

        IndexToData(&iF_0, &iV_0, piTriListIn[f * 3 + 0]);
        IndexToData(&iF_1, &iV_1, piTriListIn[f * 3 + 1]);
        IndexToData(&iF_2, &iV_2, piTriListIn[f * 3 + 2]);

        pContext->m_pInterface->m_getPosition(pContext, p0, iF_0, iV_0);
        pContext->m_pInterface->m_getPosition(pContext, p1, iF_1, iV_1);
        pContext->m_pInterface->m_getPosition(pContext, p2, iF_2, iV_2);
        pContext->m_pInterface->m_getTexCoord(pContext, t0, iF_0, iV_0);
        pContext->m_pInterface->m_getTexCoord(pContext, t1, iF_1, iV_1);
        pContext->m_pInterface->m_getTexCoord(pContext, t2, iF_2, iV_2);

        /* find the vertex matching iVertexRepresentitive */
        {
            int iMatchVert = -1;
            for (i = 0; i < 3; i++) {
                if (piTriListIn[f * 3 + i] == iVertexRepresentitive) {
                    iMatchVert = i;
                    break;
                }
            }
            if (iMatchVert < 0) {
                continue;
            }

            /* compute angle at this vertex */
            {
                float pa[3], pb[3], pc[3];
                if (iMatchVert == 0) {
                    memcpy(pa, p0, 12);
                    memcpy(pb, p1, 12);
                    memcpy(pc, p2, 12);
                } else if (iMatchVert == 1) {
                    memcpy(pa, p1, 12);
                    memcpy(pb, p2, 12);
                    memcpy(pc, p0, 12);
                } else {
                    memcpy(pa, p2, 12);
                    memcpy(pb, p0, 12);
                    memcpy(pc, p1, 12);
                }

                v1[0] = pb[0] - pa[0];
                v1[1] = pb[1] - pa[1];
                v1[2] = pb[2] - pa[2];
                v2[0] = pc[0] - pa[0];
                v2[1] = pc[1] - pa[1];
                v2[2] = pc[2] - pa[2];

                {
                    float fL1 = Length(v1[0], v1[1], v1[2]);
                    float fL2 = Length(v2[0], v2[1], v2[2]);
                    if (fL1 < 1e-10f || fL2 < 1e-10f) {
                        continue;
                    }
                    fCos = (v1[0] * v2[0] + v1[1] * v2[1] + v1[2] * v2[2]) / (fL1 * fL2);
                    if (fCos > 1) {
                        fCos = 1;
                    }
                    if (fCos < -1) {
                        fCos = -1;
                    }
                    fAngle = acosf(fCos);
                    fAngleSum += fAngle;
                }
            }
        }

        /* tangent/bitangent from UV deltas */
        dt1[0] = t1[0] - t0[0];
        dt1[1] = t1[1] - t0[1];
        dt2[0] = t2[0] - t0[0];
        dt2[1] = t2[1] - t0[1];
        fSignedAreaSTx2 = dt1[0] * dt2[1] - dt1[1] * dt2[0];

        {
            float d1[3], d2[3];
            d1[0] = p1[0] - p0[0];
            d1[1] = p1[1] - p0[1];
            d1[2] = p1[2] - p0[2];
            d2[0] = p2[0] - p0[0];
            d2[1] = p2[1] - p0[1];
            d2[2] = p2[2] - p0[2];

            if (fabsf(fSignedAreaSTx2) > 1e-10f) {
                float fInvArea = 1.0f / fSignedAreaSTx2;
                vOs[0] = fInvArea * (dt2[1] * d1[0] - dt1[1] * d2[0]);
                vOs[1] = fInvArea * (dt2[1] * d1[1] - dt1[1] * d2[1]);
                vOs[2] = fInvArea * (dt2[1] * d1[2] - dt1[1] * d2[2]);
                vOt[0] = fInvArea * (-dt2[0] * d1[0] + dt1[0] * d2[0]);
                vOt[1] = fInvArea * (-dt2[0] * d1[1] + dt1[0] * d2[1]);
                vOt[2] = fInvArea * (-dt2[0] * d1[2] + dt1[0] * d2[2]);
            } else {
                vOs[0] = 0;
                vOs[1] = 0;
                vOs[2] = 0;
                vOt[0] = 0;
                vOt[1] = 0;
                vOt[2] = 0;
            }
        }

        /* project onto normal plane */
        {
            float fOsNdot = vOs[0] * n[0] + vOs[1] * n[1] + vOs[2] * n[2];
            float fOtNdot = vOt[0] * n[0] + vOt[1] * n[1] + vOt[2] * n[2];
            vOs[0] -= fOsNdot * n[0];
            vOs[1] -= fOsNdot * n[1];
            vOs[2] -= fOsNdot * n[2];
            vOt[0] -= fOtNdot * n[0];
            vOt[1] -= fOtNdot * n[1];
            vOt[2] -= fOtNdot * n[2];
        }

        fMagS = pTriInfos[f].fMagS;
        fMagT = pTriInfos[f].fMagT;

        if (VNotZero(vOs[0], vOs[1], vOs[2]) && VNotZero(vOt[0], vOt[1], vOt[2])) {
            float fS, fT;
            if (fMagS > 1e-10f) {
                fS = fAngle / fMagS;
            } else {
                fS = 0;
            }
            if (fMagT > 1e-10f) {
                fT = fAngle / fMagT;
            } else {
                fT = 0;
            }
            {
                float fLenOs = Length(vOs[0], vOs[1], vOs[2]);
                float fLenOt = Length(vOt[0], vOt[1], vOt[2]);
                if (fLenOs > 0) {
                    fRes1[0] += fS * vOs[0] / fLenOs;
                    fRes1[1] += fS * vOs[1] / fLenOs;
                    fRes1[2] += fS * vOs[2] / fLenOs;
                }
                if (fLenOt > 0) {
                    fRes2[0] += fT * vOt[0] / fLenOt;
                    fRes2[1] += fT * vOt[1] / fLenOt;
                    fRes2[2] += fT * vOt[2] / fLenOt;
                }
            }
        }
    }

    /* normalize */
    {
        float fLen1 = Length(fRes1[0], fRes1[1], fRes1[2]);
        float fLen2 = Length(fRes2[0], fRes2[1], fRes2[2]);
        if (fLen1 > 0) {
            fRes1[0] /= fLen1;
            fRes1[1] /= fLen1;
            fRes1[2] /= fLen1;
        }
        if (fLen2 > 0) {
            fRes2[0] /= fLen2;
            fRes2[1] /= fLen2;
            fRes2[2] /= fLen2;
        }

        /* determine sign */
        {
            float fCross[3];
            fCross[0] = fRes1[1] * fRes2[2] - fRes1[2] * fRes2[1];
            fCross[1] = fRes1[2] * fRes2[0] - fRes1[0] * fRes2[2];
            fCross[2] = fRes1[0] * fRes2[1] - fRes1[1] * fRes2[0];
            float fDot = fCross[0] * n[0] + fCross[1] * n[1] + fCross[2] * n[2];
            float fSign = fDot < 0 ? (-1.0f) : 1.0f;

            fTs[0] = fRes1[0];
            fTs[1] = fRes1[1];
            fTs[2] = fRes1[2];
            fTs[3] = fSign;
            fTs[4] = fRes2[0];
            fTs[5] = fRes2[1];
            fTs[6] = fRes2[2];
            fTs[7] = fSign;
        }
    }

    return 1;
}
