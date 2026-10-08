/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * Polygon.h
 *
 * Declares CA3dPolygon, the common primitive container used by walls and
 * openings. It owns a list of primitive records and embeds the coordinate
 * frame that places them relative to their owner.
 *
 * The methods copy geometry, transform it through a supplied matrix and
 * fetch placed primitives for emission or tests. Polygon.cpp implements
 * storage and conversion; Corners.h and A3dFrame.h define the shared
 * records and placement state.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _POLYGON_H
#define _POLYGON_H

#include "A3dPrivate.h"
#include "A3dFrame.h"
#include "Corners.h"
#include "LinkList.h"

class CA3dOpening;
class CA3dRoot;
class CA3dWall;

/* =============================================================
// Class: CA3dPolygon
//
// Description: Owned primitive geometry and its transform.
//
// Size: 0x70
//
// (RE) Constructor: rtl:0x10034a10; dbg:0x10085130
// =============================================================*/

/* Vtable: dbg:0x101398ec; slot 0 destructor. */

class CA3dPolygon
{
public:
	CA3dPolygon(void);
	virtual ~CA3dPolygon(void);

	void ClearPrims(void);

	void SetFrame(void *pOwner, LPA3DVAL pvPosition,
	              LPA3DVAL pvFront, LPA3DVAL pvUp);

	void CopyPrimitives(const CA3dPtrList<A3DPRIMITIVE> *pFrom);
	void CopyPrimitivesThrough(const CA3dPtrList<A3DPRIMITIVE> *pFrom,
	                           const CA3dPolygon *pFromFrame,
	                           const A3DVAL *pMatrix);
	void SetOwner(void *pOwner);
	void CopyFrameFrom(void *pOwner, const CA3dPolygon *pFrom);

	CA3dFrame *Xform(void) { return (&m_xform); }

	const CA3dFrame *Xform(void) const { return (&m_xform); }

	friend void A3dEmitMarked(class CA3dOpening *pOwner, class CA3dRoot *pApi);
	friend BOOL A3dSegmentCrosses(class CA3dWall *pOwner,
	                              const A3DVAL *pcvSeg);

	void FetchPrimitive(A3DPRIMITIVE *pOut,
	                    const A3DPRIMITIVE *pcBlock) const;

	friend BOOL A3dPointInside(class CA3dRoom *pRoom, const A3DVAL *pcvPoint);
	friend class CA3dWallBuilder;

protected:

	CA3dFrame m_xform; /* 0x04; right, up, front, at */

	/* Owned primitive copies in a 12-byte circular list. */
	CA3dPtrList<A3DPRIMITIVE> m_PrimList; /* 0x54 */

	/* Stored homogeneous plane shared by walls and openings. */
	A3DVAL m_vPlane[4]; /* 0x60 */
};

#endif /* _POLYGON_H */
