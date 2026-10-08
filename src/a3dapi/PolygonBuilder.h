/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * PolygonBuilder.h
 *
 * Declares CA3dPolygonBuilder, the shared geometry-building component for
 * walls and openings. It keeps pending vertices, retained primitives, a
 * reference plane and the boundary-edge list produced by validation.
 *
 * The methods accept Begin/Vertex/End input, expose indexed edits and
 * clear or validate the result. A3dWallBuilder and A3dOpeningBuilder add
 * their private interfaces and object-specific state around this
 * component.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _POLYGONBUILDER_H
#define _POLYGONBUILDER_H

#include "A3dPrivate.h"
#include "Corners.h"
#include "LinkList.h"

/* -------------------------------------------------------------------------- */

/* =============================================================
// Class: CA3dPolygonBuilder
//
// Description: Primitive input and boundary-edge storage.
//
// Size: 0x90
//
// (RE) Constructor: dbg:0x10085f00
// =============================================================*/

/* (RE) Vtable: dbg:0x10139920; slot 0: destructor. */

class CA3dPolygonBuilder
{
public:
	CA3dPolygonBuilder(int nType);
	virtual ~CA3dPolygonBuilder(void);

	HRESULT Begin(DWORD dwMode);
	HRESULT End(void);
	HRESULT Vertex3f(A3DVAL x, A3DVAL y, A3DVAL z);
	HRESULT Vertex3fv(LPA3DVAL pv);
	HRESULT CheckPrimitives(void);

	HRESULT RemovePrimitive(int nPrim);
	int     GetPrimitiveCount(void);
	HRESULT SetVertex(int nPrim, int nVertex,
	                  A3DVAL x, A3DVAL y, A3DVAL z);
	HRESULT GetVertex(int nPrim, int nVertex, LPA3DVAL pv);

	void    ClearPrimitives(void);
	HRESULT Clear(void);

	CA3dPtrList<A3DPRIMITIVE> *GetPrimitiveList(void) { return (&m_PrimList); }

	CA3dPtrList<A3DEDGE> *GetEdgeList(void) { return (&m_EdgeList); }

	/* Original defect: 2D validation leaves the plane normal null although
	 * wall construction dereferences it. */
	const A3DVAL *GetPlane(void) { return (m_pvPlaneNormal); }

protected:
	void *CompleteVertex(void);

	HRESULT CheckPlanar(void);
	void    ClearEdges(void);
	void    MarkEdge(A3DPRIMITIVE *pPrim, int nVertex, int fUnmatched);
	void    AddEdge(const A3DPRIMITIVE *pcPrim, int nFrom, int nTo);
	void    EmitUnmatchedEdges(const A3DPRIMITIVE *pcPrim);
	void    MergeEdges(void);

protected:
	/* 0x04 */ CA3dPtrList<A3DPRIMITIVE> m_PrimList; /* Owned primitives. */

	/* 0x10 */ CA3dPtrList<A3DEDGE> m_EdgeList; /* Owned boundary edges. */

	/* 0x1C */ A3DPRIMITIVE m_prim; /* Primitive under construction. */

	/* 0x7C */ int   m_nVertexIndex; /* Next input corner. */
	/* 0x80 */ DWORD m_fChecked; /* set once the walk passes */
	/* 0x84 */ int   m_nType;    /* A3D_SCENE_2D or A3D_SCENE_3D */

	/* 0x88 */ A3DVAL      *m_pvPlaneNormal; /* Borrows first primitive normal. */
	/* 0x8C */ A3DPRIMITIVE *m_pFirstBlock; /* First primitive checked. */
};

#endif /* _POLYGONBUILDER_H */
