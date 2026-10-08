/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * WallEdge.h
 *
 * Declares CA3dWallEdge, a placed wall's boundary edge and its
 * crossing-list state. Each edge stores two local endpoints, a borrowed
 * wall pointer and references to crossing edges.
 *
 * WallEdge.cpp converts the endpoints and evaluates coverage. A3dWall.cpp
 * manages the relationships during intersection passes; the edge owns
 * list nodes but does not own the other edges they reference.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _WALLEDGE_H
#define _WALLEDGE_H

#include "A3dPrivate.h"
#include "LinkList.h"

class CA3dWall;

void   *A3dEdgeWorld(class CA3dWallEdge *pEdge, A3DVAL *pvOut);

class CA3dWall;

/* =============================================================
// Class: CA3dWallEdge
//
// Description: Local wall endpoints and borrowed crossing edges.
//
// Size: 0x38 in the reconstruction; see the constructor's layout constraint.
//
// (RE) Reference allocation in CopyEdges: dbg:0x10080370
// =============================================================*/

/* Vtable slot 0: destructor. */

class CA3dWallEdge
{
public:

	CA3dWallEdge(const A3DVAL *pcvPoints, CA3dWall *pWall);
	virtual ~CA3dWallEdge(void);

	A3DLIST	*GetCrossList(void)	{ return ((A3DLIST *) &m_pCrossHead); }

	int	IsCrossed(void);

protected:
	/* 0x04 */ A3DVAL		m_avPoint[2][4]; /* Local homogeneous endpoints. */
	/* 0x24 */ CA3dWall	*m_pWall; /* Borrowed containing wall. */

	friend void *A3dEdgeWorld(CA3dWallEdge *pEdge, A3DVAL *pvOut);

	friend class CA3dWall;

	/* Crossing nodes owned here; edges borrowed. Cleared each intersection pass. */
	A3DLISTNODE    *m_pCrossHead;
	A3DLISTNODE    *m_pCrossTail;
	A3DLISTNODE    *m_pCrossCurr;
	int             m_cCross;
};

#endif /* _WALLEDGE_H */
