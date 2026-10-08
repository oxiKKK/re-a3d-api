/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * WallEdge.cpp
 *
 * Implements the edge records used to compare placed walls. CA3dWallEdge
 * stores local endpoints and a borrowed containing wall, converts
 * endpoints to world coordinates and checks whether recorded crossings
 * cover the edge.
 *
 * The crossing list owns its nodes while referring to edges owned
 * elsewhere. A3dWall.cpp builds and clears these relationships during
 * wall intersection and room coverage checks.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "WallEdge.h"
#include "A3dFrame.h"
#include "A3dWall.h"
#include "LinkList.h"

#include <math.h>

/* Absolute covered-length tolerance */
#define A3D_EDGE_COVERAGE_TOLERANCE	0.000001

/* =============================================================
// CA3dWallEdge()
// (RE) dbg:0x10088b60
//
// Copy local endpoints and borrow the wall. The crossing-list layout
// is unresolved: the declaration occupies 0x38 bytes, but 677
// CopyEdges allocates 0x34.
// =============================================================*/

CA3dWallEdge::CA3dWallEdge(const A3DVAL *pcvPoints, CA3dWall *pWall)
{
	m_pCrossHead    = NULL;
	m_pCrossTail    = NULL;
	m_pCrossCurr    = NULL;
	m_cCross        = 0;

	CopyMemory(m_avPoint, pcvPoints, sizeof(m_avPoint));

	m_pWall = pWall;
}

/* =============================================================
// ~CA3dWallEdge()
//
// Free crossing-list nodes while retaining the borrowed edges. Preserve
// the null dereference when the count exceeds the chain length.
// =============================================================*/

CA3dWallEdge::~CA3dWallEdge(void)
{
A3DLISTNODE    *pNode;
A3DLISTNODE    *pNext;
int             i;

	pNode = m_pCrossHead;

	for (i = 0; i < m_cCross; i++)
	{
		pNext = pNode->pNext;

		if (pNode)
			operator delete(pNode);

		pNode = pNext;
	}

	m_cCross        = 0;
	m_pCrossCurr    = NULL;
	m_pCrossHead    = NULL;
	m_pCrossTail    = NULL;
}

/* =============================================================
// A3dEdgeWorld()
// (RE) dbg:0x10088C50; thunk dbg:0x10002365
//
// Transform both endpoints through the owning wall's ancestor frames.
//
// Returns: The borrowed wall pointer; NULL when unowned. Null outputs are
//          ignored.
// =============================================================*/

void *
A3dEdgeWorld(CA3dWallEdge *pEdge, A3DVAL *pvOut)
{
CA3dWall *pOwner;

	pOwner = pEdge->m_pWall;

	if (!pOwner)
		return (pOwner);

	if (!pvOut)
		return (pOwner);

	pOwner->Xform()->SegmentInto(pvOut, pEdge->m_avPoint[0]);

	return (pOwner);
}

/* =============================================================
// IsCrossed()
//
// Test whether compatible crossings cover this edge. Preserve comparison
// of world crossing lengths against the local reference length.
//
// Returns: 0 for complete coverage; 1 otherwise.
// =============================================================*/

int
CA3dWallEdge::IsCrossed(void)
{
A3DLIST listWork;
A3DVAL  avEdge[8];
A3DVAL  avOther[8];
A3DVAL  fTotal;
A3DVAL dx, dy, dz;
CA3dWallEdge   *pEdge;
CA3dWallEdge   *pOther;
int             fCovered;
int i, n;

	fTotal = 0.0f;

	listWork.pHead  = NULL;
	listWork.pTail  = NULL;
	listWork.pCurr  = NULL;
	listWork.cNodes = 0;

	m_pCrossCurr = m_pCrossHead;

	A3dListClear(&listWork);

	for (i = m_cCross; i > 0; i--)
		A3dListAdd(&listWork, A3dListNext(GetCrossList()));

	m_pCrossCurr = m_pCrossHead;

	for (i = m_cCross; i > 0; i--)
	{
		pEdge = (CA3dWallEdge *) A3dListNext(GetCrossList());

		A3dEdgeWorld(pEdge, avEdge);

		listWork.pCurr = listWork.pHead;

		for (n = listWork.cNodes; n > 0; n--)
		{
		int fOtherFirst;
		int fOtherSecond;
		int fMineFirst;
		int fMineSecond;

			pOther = (CA3dWallEdge *) A3dListNext(&listWork);

			if (pOther == pEdge)
				continue;

			A3dEdgeWorld(pOther, avOther);

			if (A3dEdgesSame(avEdge, avOther))
				continue;

			fOtherFirst  = A3dPointWithinEdge(&avOther[0], avEdge);
			fOtherSecond = A3dPointWithinEdge(&avOther[4], avEdge);

			if (fOtherFirst && fOtherSecond)
				continue;

			fMineFirst  = A3dPointWithinEdge(&avEdge[0], avOther);
			fMineSecond = A3dPointWithinEdge(&avEdge[4], avOther);

			if ((fMineFirst && fMineSecond) ||
			    (fOtherFirst && fMineFirst) ||
			    (fOtherSecond && fMineSecond))
				continue;

			A3dListClear(&listWork);

			return (1);
		}

		dx = avEdge[0] - avEdge[4];
		dy = avEdge[1] - avEdge[5];
		dz = avEdge[2] - avEdge[6];

		fTotal += (A3DVAL) sqrt(dx * dx + dy * dy + dz * dz);
	}

	dx = m_avPoint[0][0] - m_avPoint[1][0];
	dy = m_avPoint[0][1] - m_avPoint[1][1];
	dz = m_avPoint[0][2] - m_avPoint[1][2];

	fCovered = (fabs(fTotal - sqrt(dx * dx + dy * dy + dz * dz)) <=
		    A3D_EDGE_COVERAGE_TOLERANCE);

	A3dListClear(&listWork);

	return (fCovered ? 0 : 1);
}
