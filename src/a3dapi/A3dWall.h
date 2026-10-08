/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dWall.h
 *
 * Declares CA3dWall, the placed wall object shared by rooms and scenes.
 * It combines polygon storage and a coordinate frame with material,
 * opening, edge and contact state.
 *
 * The methods support placement, primitive emission and wall intersection
 * or coverage checks. A3dWallBuilder supplies construction input;
 * A3dWall.cpp implements placed-wall behavior using the opening and
 * wall-edge types declared in their own headers.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _A3DWALL_H
#define _A3DWALL_H

#include "A3dPrivate.h"
#include "A3dGeom.h"
#include "A3dFrame.h"
#include "Polygon.h"
#include "MaterialLink.h"
#include "LinkList.h"

class CA3dOpening;
class CA3dRoom;
class CA3dRoot;
class CA3dWallBuilder;
class CA3dWallEdge;

/* SHELL walls participate in room closure, portals and scene invalidation.
 * MARKED walls are omitted by CA3dWall::Emit() but noted by CA3dRoom::Emit();
 * no code in this DLL sets MARKED.
*/

#define A3DWALL_FLAG_SHELL  0x00000001
#define A3DWALL_FLAG_MARKED 0x00000002

int  A3dWallIsShell(const class CA3dWall *pcWall);

int  A3dWallIsMarked(const class CA3dWall *pcWall);

BOOL A3dPointOnEdge2D(const A3DVAL *pcvPoint, int iU, int iV,
                      const A3DVAL *pcvEdge);
BOOL A3dEdgesSame(const A3DVAL *pcvA, const A3DVAL *pcvB);
BOOL A3dPointWithinEdge(const A3DVAL *pcvPoint,
                        const A3DVAL *pcvEdge);
BOOL A3dSegmentCrosses(class CA3dWall *pOwner, const A3DVAL *pcvSeg);

/* A3dWallsMeet results  */
#define A3D_WALLS_HAVE_EDGE_CONTACT 0
#define A3D_WALLS_CROSS 1
#define A3D_WALLS_HAVE_NO_CONTACT 2

int A3dWallsMeet(class CA3dWall *pWallA,
                 class CA3dWall *pWallB);

class CA3dRoom;

/* =============================================================
// Class: CA3dWall
//
// Description: Placed wall geometry with openings and contact edges.
//
// Size: 0xAC
//
// (RE) Builder constructor: rtl:0x10031c40; dbg:0x1007f9e0
// =============================================================*/

/* (RE) Primary vtable: rtl:0x10054330; dbg:0x101396b8.
 * Slot 13: dbg:0x1007FBC0; thunk dbg:0x10001168.
 * Polygon-base table at 0x04: dbg:0x101396b4.
 * Material-link table at 0x74: rtl:0x10054328; dbg:0x101396b0.
 */

class CA3dWall : public CA3dGeomIface, public CA3dPolygon
{
public:
	CA3dWall(CA3dWallBuilder *pBuilder, DWORD dwFlags, LPA3DVAL pvPosition,
	         LPA3DVAL pvFront, LPA3DVAL pvUp);
	CA3dWall(const CA3dWall *pFrom, class CA3dRoom *pRoom);

	HRESULT SetMaterial(void *pMaterial); /* 4 */
	HRESULT GetMaterial(void **ppMaterial); /* 5 */
	HRESULT GetRoom(void **ppRoom); /* 3 */
	HRESULT GetOpening(int nOpening, void **ppOpening); /* 6 */
	HRESULT NewTransform(void **ppXform); /* 12 */
	HRESULT CopyFrom(const void *pcFrom); /* 11 */
	HRESULT GetPosition(const CA3dFrame *pcRelativeTo, A3DVAL *pv); /* 8 */
	HRESULT SetPosition(const CA3dFrame *pcRelativeTo, const A3DVAL *pcv); /* 7 */
	HRESULT GetOrientation(const CA3dFrame *pcRelativeTo, A3DVAL *pvFront,
	                       A3DVAL *pvUp); /* 10 */
	HRESULT SetOrientation(const CA3dFrame *pcRelativeTo, const A3DVAL *pcvFront,
	                       const A3DVAL *pcvUp); /* 9 */

	void MarkSceneStale(void);

	STDMETHODIMP         QueryInterface(REFIID riid, void **ppv); /* 0 */
	STDMETHODIMP_(ULONG) AddRef(void); /* 1 */
	STDMETHODIMP_(ULONG) Release(void); /* 2 */

	virtual ~CA3dWall(void);

	void CopyOpenings(const CA3dPtrList<CA3dOpening> *pFrom);
	void CopyEdges(CA3dPtrList<A3DEDGE> *pFrom);
	void ClearEdges(void);
	void ReleaseOpenings(void);
	void GetPlane(A3DVAL *pvPlane);
	void Emit(CA3dRoot *pApi);
	void ClearEdgeCrossings(void);

	CA3dPtrList<CA3dWall> *GetTouchList(void) { return (&m_TouchList); }
	int                                       HasCrossing(void);

	int     CoveredBy(CA3dWall *pOther);
	int     CoveredEntirelyBy(CA3dWall *pOther);
	int     Covers(CA3dWall *pOther);
	void    MakePortals(void);
	int     Meets(CA3dWall *pOther);
	HRESULT NoteTouching(CA3dWall *pOther);

	friend int A3dWallIsShell(const CA3dWall *pcWall);
	friend int A3dWallIsMarked(const CA3dWall *pcWall);
	friend int A3dWallsMeet(CA3dWall *pWallA, CA3dWall *pWallB);

protected:
	CA3dLink m_link; /* 0x74, referenced material */
	LONG     m_cRef; /* 0x7c */

	CA3dPtrList<CA3dWallEdge> m_EdgeList;    /* 0x80, owned edges */
	CA3dPtrList<CA3dOpening>  m_OpeningList; /* 0x8c, referenced copies */
	CA3dPtrList<CA3dWall>     m_TouchList;   /* 0x98, borrowed touching walls */

	DWORD    m_dwFlags; /* 0xa4, A3DWALL_FLAG_* */
	CA3dRoom *m_pRoom;   /* 0xa8, borrowed owning room */
};

#endif /* _A3DWALL_H */
