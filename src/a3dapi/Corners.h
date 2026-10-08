/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * Corners.h
 *
 * Defines primitive and edge records shared by polygons, geometry pools
 * and the room, wall and opening builders. These records carry vertices,
 * normals and boundary flags used during construction and intersection
 * tests.
 *
 * The file also declares the common builder interface base and shared
 * construction data. PolygonBuilder.cpp assembles the records, while
 * Corners.cpp provides edge intersection and shared builder support.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _CORNERS_H
#define _CORNERS_H

#include "A3dPrivate.h"
#include "ChunkPage.h"
#include "LinkList.h"

/* A3DPRIMITIVE: 0x60 bytes. */

typedef struct _A3DPRIMITIVE
{
	DWORD     dwTag;
	BYTE      cVertices;
	BYTE      abPad05[3];
	A3DVERTEX av[4];
	A3DVECTOR vNormal;
	A3DVAL    fDistance;
	DWORD     dwFlags;
} A3DPRIMITIVE;

/* A3DEDGE: 0x20 bytes, two homogeneous endpoints. */

typedef struct _A3DEDGE
{
	A3DVERTEX avEnd[2];
} A3DEDGE;

/* Low-nibble edge masks. */

#define A3D_EDGEMASK_TRIANGLE 0x07
#define A3D_EDGEMASK_QUAD     0x0F
#define A3D_EDGEMASK_ALL      0x0F

void A3dEdgeAgainstPrim(const A3DPRIMITIVE *pcPrim, int iA, int iB,
                        const A3DEDGE *pcEdge);

/* =============================================================
// Class: CA3dBuilderIface
//
// Description: Virtual destructor interface for builders.
// =============================================================*/

class CA3dBuilderIface
{
public:
	virtual ~CA3dBuilderIface(void);
};

/* A3dEdgeCompare() result values. */

#define A3D_EDGE_ENDPOINT 0 /* adjacent at an endpoint */
#define A3D_EDGE_CROSSES  1 /* overlaps through an edge */
#define A3D_EDGE_MISSES   2

int A3dEdgeCompare(const A3DVAL *pvPoint, const A3DVAL *pvNormal,
                   const A3DVAL *pvCorners);

#endif /* _CORNERS_H */
