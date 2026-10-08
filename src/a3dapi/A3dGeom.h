/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dGeom.h
 *
 * Defines the geometry-stream element format and CA3dGeomIface, the
 * common interface base for room, wall and opening objects. Pool elements
 * carry the primitive and material records consumed during acoustic
 * tracing.
 *
 * The helper declarations connect geometry submission, intersection
 * testing, reflection traversal and source control calculation. CA3dRoot
 * itself is declared in A3d3.h; A3dGeom.cpp implements its geometry
 * operations, using the storage declared in ChunkPage.h and primitive
 * types in Corners.h.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _A3DGEOM_H
#define _A3DGEOM_H

#include "A3dPrivate.h"
#include "ChunkPage.h"
#include "Corners.h"
#include "LinkList.h"

class CA3dRoot;

/* A3DELEMENT: 0x6C bytes; pool primitive or material record with opening factors. */

typedef struct _A3DELEMENT
{
	/* A3D primitive mode, subface mode or A3D_MATERIAL. */
	/* 0x00 */ DWORD        dwMode;

	/* Primitive or six-value material record. */
	/* 0x04 */ A3DPRIMITIVE prim;

	/* Subface opening factors, indirect and scalar. */
	/* 0x64 */ LPA3DVAL     pfOpening;
	/* 0x68 */ A3DVAL       fOpening;
} A3DELEMENT;

typedef int A3dElementSizeCheck[(sizeof(A3DELEMENT) ==
				 A3D_POOL_ELEMENT_SIZE) ? 1 : -1];

/* Treat the primitive as an infinite plane. */

#define A3D_PRIM_INFINITE_PLANE	0x00000010

/* Exclude the primitive from reflection or occlusion walks. */

/* (RE) Vertex3f stores: dbg:0x1000F9BB; dbg:0x1000F9E0. */
#define A3D_PRIM_NO_REFLECT	0x00000020
#define A3D_PRIM_NO_OCCLUDE	0x00000040

/* =============================================================
// Class: CA3dGeomIface
//
// Description: Leading interface base; wall and opening frames start at +4.
//
// Size: 0x04
// =============================================================*/

class CA3dGeomIface : public IUnknown
{
public:
	virtual ~CA3dGeomIface(void);
};

/* Hit tests write the primitive intersection distance. nLoose permits a
 * +/- 0.000001 edge tolerance; nInfinite removes the segment far limit. */

BOOL   A3dTraceHitTri(const A3DVAL *pvFrom, const A3DVAL *pvTo,
                      A3DPRIMITIVE *pPrim, int nLoose, int nInfinite);
BOOL   A3dTraceHitQuad(const A3DVAL *pvFrom, const A3DVAL *pvTo,
                       A3DPRIMITIVE *pPrim, int nLoose, int nInfinite);

void   A3dSourceOcclude(class CA3dRoot *pApi, class CA3dSource *pSource);

A3DVAL A3dMirrorPoint(class CA3dRoot *pApi, const A3DVAL *pv,
                      const A3DVAL *pvNormal, const A3DVAL *pvOn,
                      A3DVAL *pvTrue, A3DVAL *pvSoft);

#endif /* _A3DGEOM_H */
