/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dWall.cpp
 *
 * Implements placed walls in the private room and scene model. A wall
 * owns polygon primitives, openings and boundary edges, with a frame and
 * material binding that determine how it contributes to acoustic
 * geometry.
 *
 * Intersection and coverage tests identify contacts with other walls and
 * support room-shell validation. The file also copies openings,
 * coordinates portal creation and emits wall geometry into the root.
 * Placement changes mark the owning scene stale so its compiled geometry
 * can be rebuilt.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "A3dWall.h"
#include "A3d3.h"
#include "A3dFrame.h"
#include "A3dGeom.h"
#include "A3dRoom.h"
#include "A3dWallBuilder.h"
#include "Corners.h"
#include "LinkList.h"
#include "A3dOpening.h"
#include "WallEdge.h"

#include <math.h>


#define A3D_WALL_NORMAL_MATCH_TOLERANCE                 0.000001
#define A3D_WALL_ORIGIN_DISTANCE_SQUARED_TOLERANCE      1.0e-12
#define A3D_WALL_PLANE_DOT_TOLERANCE                    0.000001
#define A3D_EDGE_PARAMETER_ENDPOINT_TOLERANCE           0.000001
#define A3D_EDGE_AXIS_TOLERANCE_F                       0.000001f
#define A3D_EDGE_AXIS_TOLERANCE                         0.000001
#define A3D_EDGE_ENDPOINT_DISTANCE_SQUARED_TOLERANCE    1.0e-12f
#define A3D_EDGE_PROJECTED_PRODUCT_TOLERANCE            0.000001f

/* =============================================================
// QueryInterface()
// (RE) rtl:0x100322e0; dbg:0x1007ff80
//
// Reject every IID without writing the output.
//
// Returns:
//   E_INVALIDARG   if ppv is null
//   E_NOINTERFACE  otherwise
// =============================================================*/

STDMETHODIMP
CA3dWall::QueryInterface(REFIID riid, void **ppv)
{
	if (!ppv)
		return (E_INVALIDARG);

	return (E_NOINTERFACE);
}

/* =============================================================
// AddRef()
// (RE) rtl:0x10032300; dbg:0x1007ffb0
//
// Add a reference to the wall.
//
// Returns: The new reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dWall::AddRef(void)
{
	return (++m_cRef);
}

/* =============================================================
// Release()
// (RE) rtl:0x10032320; dbg:0x1007ffe0
//
// Release a reference and delete the wall at zero.
//
// Returns: The remaining reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dWall::Release(void)
{
	if (--m_cRef)
		return (m_cRef);

	delete this;

	return (0);
}

/* =============================================================
// CA3dWall()
// (RE) rtl:0x10031c40; dbg:0x1007f9e0
//
// Build an unattached wall from builder geometry, openings and material.
// =============================================================*/

CA3dWall::CA3dWall(CA3dWallBuilder *pBuilder, DWORD dwFlags,
				   LPA3DVAL pvPosition, LPA3DVAL pvFront, LPA3DVAL pvUp)
{
const A3DVAL *pvPlane;

	m_dwFlags       = dwFlags;
	m_cRef          = 0;

	SetFrame(this, pvPosition, pvFront, pvUp);

	CopyPrimitives(pBuilder->GetPrimitiveList());

	CopyEdges(pBuilder->GetEdgeList());

	/* Original defect: a successful 2D builder still returns a null plane. */

	pvPlane = pBuilder->GetPlane();

	m_vPlane[0] = pvPlane[0];
	m_vPlane[1] = pvPlane[1];
	m_vPlane[2] = pvPlane[2];
	m_vPlane[3] = pvPlane[3];

	CopyOpenings((const CA3dPtrList<CA3dOpening> *) pBuilder->GetOpeningList());

	m_link.CopyMaterialFrom(pBuilder->GetMaterialSlot());

	m_pRoom = NULL;
}

/* =============================================================
// CA3dWall()
// (RE) rtl:0x10031ed0; dbg:0x1007fc60
//
// Copy wall geometry and attach its placement to the room frame.
// =============================================================*/

CA3dWall::CA3dWall(const CA3dWall *pFrom, CA3dRoom *pRoom)
{
CA3dWall       *pSource;
CA3dWallEdge   *pEdge;
POSITION        pos;
int             i;

	m_cRef = 0;

	m_dwFlags = pFrom->m_dwFlags;

	pSource = (CA3dWall *) pFrom;

	CopyFrameFrom(this, pFrom);

	m_xform.m_pParent = pRoom->Xform();

	ClearEdges();

	/* (RE) Edge-copy helper, inlined here: dbg:0x100804c0. */
	pos = pSource->m_EdgeList.GetHeadPosition();

	for (i = pSource->m_EdgeList.GetCount(); i > 0; i--)
	{
		pEdge = pSource->m_EdgeList.GetNext(pos);

		m_EdgeList.AddTail(new CA3dWallEdge(pEdge->m_avPoint[0], this));
	}

	CopyPrimitives(&pSource->m_PrimList);
	CopyOpenings(&pSource->m_OpeningList);

	m_vPlane[0] = pFrom->m_vPlane[0];
	m_vPlane[1] = pFrom->m_vPlane[1];
	m_vPlane[2] = pFrom->m_vPlane[2];
	m_vPlane[3] = pFrom->m_vPlane[3];

	m_link.CopyMaterialFrom(&pFrom->m_link);

	m_pRoom = pRoom;
}

/* =============================================================
// ~CA3dWall()
// (RE) rtl:0x10032130; dbg:0x1007fe50
//
// Release edges, openings and borrowed-touch nodes.
// =============================================================*/

CA3dWall::~CA3dWall(void)
{
	ClearEdges();

	ReleaseOpenings();

	m_TouchList.RemoveAll();
}

/* =============================================================
// CopyOpenings()
// (RE) dbg:0x10080060
//
// Copy and retain openings for this wall. Original allocation failure
// leaves a null list entry.
// =============================================================*/

void
CA3dWall::CopyOpenings(const CA3dPtrList<CA3dOpening> *pFrom)
{
CA3dOpening    *pOpening;
CA3dOpening    *pCopy;
POSITION        pos;
int             i;

	pos = pFrom->GetHeadPosition();

	for (i = pFrom->GetCount(); i > 0; i--)
	{
		pOpening = pFrom->GetNext(pos);

		pCopy = new CA3dOpening(pOpening, this);

		m_OpeningList.AddTail(pCopy);

		if (pCopy)
			((IUnknown *) pCopy)->AddRef();
	}
}

/* =============================================================
// ReleaseOpenings()
// (RE) dbg:0x100801f0
//
// Release openings and delete their list nodes. Original defect: an
// excessive count calls Release through the null sentinel element.
// =============================================================*/

void
CA3dWall::ReleaseOpenings(void)
{
IUnknown       *pOpening;
POSITION        pos;
int             i;

	pos = m_OpeningList.GetHeadPosition();

	for (i = m_OpeningList.GetCount(); i > 0; i--)
	{
		pOpening = (IUnknown *) m_OpeningList.GetNext(pos);

		pOpening->Release();
	}

	m_OpeningList.RemoveAll();
}

/* =============================================================
// ClearEdges()
// (RE) dbg:0x100802a0
//
// Delete owned edges and their list nodes.
// =============================================================*/

void
CA3dWall::ClearEdges(void)
{
CA3dWallEdge   *pEdge;
POSITION        pos;
int             i;

	pos = m_EdgeList.GetHeadPosition();

	for (i = m_EdgeList.GetCount(); i > 0; i--)
	{
		pEdge = m_EdgeList.GetNext(pos);

		if (pEdge)
			delete pEdge;
	}

	m_EdgeList.RemoveAll();
}

/* =============================================================
// CopyEdges()
// (RE) dbg:0x10080370
//
// Replace the wall edges with copies of builder edges.
// =============================================================*/

void
CA3dWall::CopyEdges(CA3dPtrList<A3DEDGE> *pFrom)
{
const A3DVAL   *pcvSource;
CA3dWallEdge   *pCopy;
POSITION        pos;
int             i;

	ClearEdges();

	pos = pFrom->GetHeadPosition();

	for (i = pFrom->GetCount(); i > 0; i--)
	{
		pcvSource = (const A3DVAL *) pFrom->GetNext(pos);

		pCopy = new CA3dWallEdge(pcvSource, this);

		m_EdgeList.AddTail(pCopy);
	}
}

/* =============================================================
// A3dSegmentCrosses()
// (RE) dbg:0x10080610
//
// Test a segment against transformed wall primitives from both ends.
// Preserve the original extra distance check on the first direction only.
//
// Returns:
//   TRUE   if either trace hits
//   FALSE  otherwise
// =============================================================*/

BOOL
A3dSegmentCrosses(CA3dWall *pOwner, const A3DVAL *pcvSeg)
{
const A3DVAL   *pcvFar;
POSITION        pos;
int             i;

	pcvFar = &pcvSeg[4];

	pos = pOwner->m_PrimList.GetHeadPosition();

	for (i = pOwner->m_PrimList.GetCount(); i > 0; i--)
	{
		const A3DPRIMITIVE     *pPrim;
		A3DPRIMITIVE            prim;
		A3DVAL                  vDir[4];
		int                     cPoints;
		BOOL                    fHit;

		pPrim = pOwner->m_PrimList.GetNext(pos);

		pOwner->FetchPrimitive(&prim, pPrim);

		/* (RE) Two-direction trace helper, inlined here: dbg:0x100806e0. */
		cPoints = prim.cVertices;

		vDir[0] = pcvSeg[0] - pcvFar[0];
		vDir[1] = pcvSeg[1] - pcvFar[1];
		vDir[2] = pcvSeg[2] - pcvFar[2];

		prim.fDistance = 0.0f;

		fHit = FALSE;

		if (cPoints == 3)
			fHit = A3dTraceHitTri(pcvSeg, vDir, &prim, 0, 0);
		else if (cPoints == 4)
			fHit = A3dTraceHitQuad(pcvSeg, vDir, &prim, 0, 0);

		if (fHit && prim.fDistance <= 1.0f)
			return (TRUE);

		vDir[0] = -vDir[0];
		vDir[1] = -vDir[1];
		vDir[2] = -vDir[2];

		prim.fDistance = 0.0f;

		if (cPoints == 3)
		{
			if (A3dTraceHitTri(pcvFar, vDir, &prim, 0, 0))
				return (TRUE);
		}
		else if (cPoints == 4)
		{
			if (A3dTraceHitQuad(pcvFar, vDir, &prim, 0, 0))
				return (TRUE);
		}
	}

	return (FALSE);
}

/* =============================================================
// ClearEdgeCrossings()
// (RE) dbg:0x100808d0
//
// Delete borrowed crossing nodes from every edge.
// =============================================================*/

void
CA3dWall::ClearEdgeCrossings(void)
{
A3DLISTNODE    *pNode;
A3DLISTNODE    *pNext;
CA3dWallEdge   *pEdge;
A3DLIST        *pCross;
POSITION        pos;
	int i, n;

	pos = m_EdgeList.GetHeadPosition();

	for (i = m_EdgeList.GetCount(); i > 0; i--)
	{
		pEdge = m_EdgeList.GetNext(pos);

		pCross = pEdge->GetCrossList();

		pNode = pCross->pHead;

		for (n = 0; n < pCross->cNodes; n++)
		{
			pNext = pNode->pNext;

			if (pNode)
				operator delete(pNode);

			pNode = pNext;
		}

		pCross->cNodes  = 0;
		pCross->pCurr   = NULL;
		pCross->pHead   = NULL;
		pCross->pTail   = NULL;
	}
}

/* =============================================================
// HasCrossing()
// (RE) dbg:0x100809a0
//
// Check for incompatible or incomplete edge coverage.
//
// Returns: 1 if any edge reports a crossing; 0 otherwise.
// =============================================================*/

int
CA3dWall::HasCrossing(void)
{
CA3dWallEdge   *pEdge;
POSITION        pos;
int             i;

	pos = m_EdgeList.GetHeadPosition();

	for (i = m_EdgeList.GetCount(); i > 0; i--)
	{
		pEdge = m_EdgeList.GetNext(pos);

		if (pEdge->IsCrossed())
			return (1);
	}

	return (0);
}

/* =============================================================
// GetPlane()
// (RE) dbg:0x100801b0
//
// Transform the builder plane through the wall frame's ancestors. Original
// defect: omit the wall's placement and translate the normal as a point.
// =============================================================*/

void
CA3dWall::GetPlane(A3DVAL *pvPlane)
{
	Xform()->PlaneInto(pvPlane, m_vPlane);
}

/* =============================================================
// Meets()
// (RE) dbg:0x10080b50
//
// Test plane alignment and primitive coverage in either direction.
//
// Returns: 1 if the walls meet; 0 otherwise.
// =============================================================*/

int
CA3dWall::Meets(CA3dWall *pOther)
{
A3DVAL vMine[4];
A3DVAL vTheirs[4];

	GetPlane(vMine);
	pOther->GetPlane(vTheirs);

	if (1.0 - fabs(vTheirs[0] * vMine[0] + vTheirs[1] * vMine[1] +
				   vTheirs[2] * vMine[2]) >
		A3D_WALL_NORMAL_MATCH_TOLERANCE)
		return (0);

	if (CoveredBy(pOther))
		return (1);

	return (pOther->CoveredBy(this) != 0);
}

/* =============================================================
// NoteTouching()
// (RE) dbg:0x10080a50
//
// Record fully covering aligned wall contacts.
//
// Returns: Zero, including noncontacting walls, as in the original.
// =============================================================*/

HRESULT
CA3dWall::NoteTouching(CA3dWall *pOther)
{
A3DVAL vMine[4];
A3DVAL vTheirs[4];

	GetPlane(vMine);
	pOther->GetPlane(vTheirs);

	if (1.0 - fabs(vTheirs[0] * vMine[0] + vTheirs[1] * vMine[1] +
				   vTheirs[2] * vMine[2]) <=
		A3D_WALL_NORMAL_MATCH_TOLERANCE)
	{
		if (CoveredEntirelyBy(pOther) || pOther->CoveredEntirelyBy(this))
			m_TouchList.AddTail(pOther);
	}

	return ((HRESULT) 0);
}

/* (RE) Coverage-walk candidates: dbg:0x10080c30, dbg:0x10080cf0,
 * dbg:0x10081440. Inlined helpers: quad retry dbg:0x10080e70;
 * coplanarity test dbg:0x10081360.
 */

/* =============================================================
// CoveredBy()
//
// Test whether any primitive passes the other wall coverage checks.
// The exact entry among the coverage walks is unresolved.
//
// Returns: 1 if any primitive passes, including a zero-vertex primitive; 0
//          otherwise.
// =============================================================*/

int
CA3dWall::CoveredBy(CA3dWall *pOther)
{
A3DPRIMITIVE            primMine;
A3DPRIMITIVE            primTheirs;
A3DVAL                  avQuad[12];
A3DVAL                  vPlane[4];
A3DVAL                  d[3];
const A3DVAL           *pv;
const A3DVAL           *pcvTheirs;
const A3DPRIMITIVE     *pBlock;
POSITION                pos;
POSITION                posOther;
int                     cVertices;
int                     nResult;
int                     fMissed;
	int i, n, v;

	pos = m_PrimList.GetHeadPosition();

	if (m_PrimList.GetCount() <= 0)
		return (0);

	for (i = m_PrimList.GetCount(); i > 0; i--)
	{
		pBlock = m_PrimList.GetNext(pos);

		FetchPrimitive(&primMine, pBlock);

		pOther->GetPlane(vPlane);

		cVertices = primMine.cVertices;

		if (!cVertices)
			return (1);

		fMissed = 0;

		for (v = cVertices - 1; v >= 0; v--)
		{
			pv = primMine.av[v];

			posOther = pOther->m_PrimList.GetHeadPosition();

			for (n = pOther->m_PrimList.GetCount(); n > 0; n--)
			{
				pBlock = pOther->m_PrimList.GetNext(posOther);

				pOther->FetchPrimitive(&primTheirs, pBlock);

				pcvTheirs = primTheirs.av[0];

				d[0] = pcvTheirs[0] - pv[0];
				d[1] = pcvTheirs[1] - pv[1];
				d[2] = pcvTheirs[2] - pv[2];

				if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] >=
					A3D_WALL_ORIGIN_DISTANCE_SQUARED_TOLERANCE &&
					fabs(d[0] * vPlane[0] + d[1] * vPlane[1] +
						 d[2] * vPlane[2]) >= A3D_WALL_PLANE_DOT_TOLERANCE)
				{
					fMissed = 1;

					break;
				}

				nResult = A3dEdgeCompare(pv, vPlane, pcvTheirs);

				if (nResult == A3D_EDGE_MISSES &&
					primTheirs.cVertices == 4)
				{
					avQuad[0]       = pcvTheirs[0];
					avQuad[1]       = pcvTheirs[1];
					avQuad[2]       = pcvTheirs[2];
					avQuad[3]       = pcvTheirs[3];
					avQuad[4]       = pcvTheirs[8];
					avQuad[5]       = pcvTheirs[9];
					avQuad[6]       = pcvTheirs[10];
					avQuad[7]       = pcvTheirs[11];
					avQuad[8]       = pcvTheirs[12];
					avQuad[9]       = pcvTheirs[13];
					avQuad[10]      = pcvTheirs[14];
					avQuad[11]      = pcvTheirs[15];

					nResult = A3dEdgeCompare(pv, vPlane, avQuad);
				}

				if (nResult > A3D_EDGE_CROSSES)
				{
					fMissed = 1;

					break;
				}
			}

			if (fMissed)
				break;
		}

		if (!fMissed)
			return (1);
	}

	return (0);
}

/* =============================================================
// A3dEdgeCompare()
// (RE) dbg:0x10080f30
//
// Classify a projected point against three homogeneous corners.
//
// Returns:
//   A3D_EDGE_ENDPOINT  endpoint adjacency
//   A3D_EDGE_CROSSES   overlap
//   A3D_EDGE_MISSES    if no solution is found
// =============================================================*/

int
A3dEdgeCompare(const A3DVAL *pvPoint,
			   const A3DVAL *pvNormal,
			   const A3DVAL *pvCorners)
{
	A3DVAL fNx, fNy, fNz;
	A3DVAL du, dv;
	A3DVAL e1u, e1v;
	A3DVAL e2u, e2v;
	A3DVAL fDet;
	A3DVAL s, t;
	int fFound;

	fFound = 0;

	fNx = (A3DVAL) fabs(pvNormal[0]);
	fNy = (A3DVAL) fabs(pvNormal[1]);
	fNz = (A3DVAL) fabs(pvNormal[2]);

	if (fNx > fNy && fNx > fNz)
	{
		du      = pvPoint[1] - pvCorners[1];
		dv      = pvPoint[2] - pvCorners[2];
		e1u     = pvCorners[5] - pvCorners[1];
		e1v     = pvCorners[6] - pvCorners[2];
		e2u     = pvCorners[9] - pvCorners[1];
		e2v     = pvCorners[10] - pvCorners[2];
	}
	else if (fNy > fNx && fNy > fNz)
	{
		du      = pvPoint[0] - pvCorners[0];
		dv      = pvPoint[2] - pvCorners[2];
		e1u     = pvCorners[4] - pvCorners[0];
		e1v     = pvCorners[6] - pvCorners[2];
		e2u     = pvCorners[8] - pvCorners[0];
		e2v     = pvCorners[10] - pvCorners[2];
	}
	else
	{
		du      = pvPoint[0] - pvCorners[0];
		dv      = pvPoint[1] - pvCorners[1];
		e1u     = pvCorners[4] - pvCorners[0];
		e1v     = pvCorners[5] - pvCorners[1];
		e2u     = pvCorners[8] - pvCorners[0];
		e2v     = pvCorners[9] - pvCorners[1];
	}

	s = 0.0f;

	if (e1u == 0.0f)
	{
		if (e2u != 0.0f)
		{
			t = du / e2u;

			if (t >= 0.0f && t <= 1.0f && e1v != 0.0f)
			{
				s = (dv - t * e2v) / e1v + t;

				if (s >= 0.0f && s <= 1.0f)
					fFound = 1;
			}
			else
			{
				s = t;
			}
		}
	}
	else
	{
		fDet = e2v * e1u - e1v * e2u;

		if (fDet != 0.0f)
		{
			t = (dv * e1u - e1v * du) / fDet;

			if (t >= 0.0f && t <= 1.0f)
			{
				s = (du - t * e2u) / e1u + t;

				if (s >= 0.0f && s <= 1.0f)
					fFound = 1;
			}
			else
			{
				s = t;
			}
		}
	}

	if (!fFound)
		return (A3D_EDGE_MISSES);

	if (fabs(s - 1.0f) >= A3D_EDGE_PARAMETER_ENDPOINT_TOLERANCE &&
		s >= A3D_EDGE_PARAMETER_ENDPOINT_TOLERANCE)
		return (A3D_EDGE_CROSSES);

	return (A3D_EDGE_ENDPOINT);
}

/* =============================================================
// CoveredEntirelyBy()
//
// Test whether all primitive corners pass the other wall coverage checks.
// The exact entry among the coverage walks is unresolved.
//
// Returns: 1 if all pass, including an empty wall; 0 otherwise.
// =============================================================*/

int
CA3dWall::CoveredEntirelyBy(CA3dWall *pOther)
{
A3DPRIMITIVE            primMine;
A3DPRIMITIVE            primTheirs;
A3DVAL                  avQuad[12];
A3DVAL                  vPlane[4];
A3DVAL                  d[3];
const A3DVAL           *pv;
const A3DVAL           *pcvTheirs;
const A3DPRIMITIVE     *pBlock;
POSITION                pos;
POSITION                posOther;
int                     cVertices;
int                     nResult;
	int i, n, v;

	pos = m_PrimList.GetHeadPosition();

	if (m_PrimList.GetCount() <= 0)
		return (1);

	for (i = m_PrimList.GetCount(); i > 0; i--)
	{
		pBlock = m_PrimList.GetNext(pos);

		FetchPrimitive(&primMine, pBlock);

		pOther->GetPlane(vPlane);

		cVertices = primMine.cVertices;

		if (!cVertices)
			continue;

		for (v = cVertices - 1; v >= 0; v--)
		{
			pv = primMine.av[v];

			posOther = pOther->m_PrimList.GetHeadPosition();

			for (n = pOther->m_PrimList.GetCount(); n > 0; n--)
			{
				pBlock = pOther->m_PrimList.GetNext(posOther);

				pOther->FetchPrimitive(&primTheirs, pBlock);

				pcvTheirs = primTheirs.av[0];

				d[0] = pcvTheirs[0] - pv[0];
				d[1] = pcvTheirs[1] - pv[1];
				d[2] = pcvTheirs[2] - pv[2];

				if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] >=
					A3D_WALL_ORIGIN_DISTANCE_SQUARED_TOLERANCE &&
					fabs(d[0] * vPlane[0] + d[1] * vPlane[1] +
						 d[2] * vPlane[2]) >= A3D_WALL_PLANE_DOT_TOLERANCE)
					return (0);

				nResult = A3dEdgeCompare(pv, vPlane, pcvTheirs);

				if (nResult == A3D_EDGE_MISSES &&
					primTheirs.cVertices == 4)
				{
					avQuad[0]       = pcvTheirs[0];
					avQuad[1]       = pcvTheirs[1];
					avQuad[2]       = pcvTheirs[2];
					avQuad[3]       = pcvTheirs[3];
					avQuad[4]       = pcvTheirs[8];
					avQuad[5]       = pcvTheirs[9];
					avQuad[6]       = pcvTheirs[10];
					avQuad[7]       = pcvTheirs[11];
					avQuad[8]       = pcvTheirs[12];
					avQuad[9]       = pcvTheirs[13];
					avQuad[10]      = pcvTheirs[14];
					avQuad[11]      = pcvTheirs[15];

					nResult = A3dEdgeCompare(pv, vPlane, avQuad);
				}

				if (nResult > A3D_EDGE_CROSSES)
					return (0);
			}
		}
	}

	return (1);
}

/* =============================================================
// A3dWallsMeet()
// (RE) dbg:0x10081500
//
// Record compatible edge contacts and detect wall crossings. Original
// partial-overlap tests miss reversed winding.
//
// Returns: 1 for a crossing; 0 if an edge has contacts; 2 otherwise.
// =============================================================*/

int
A3dWallsMeet(CA3dWall *pWallA, CA3dWall *pWallB)
{
A3DVAL          avEdgeA[8];
A3DVAL          avEdgeB[8];
POSITION        posA;
int             cMet;
int             i;

	cMet = 0;

	posA = pWallA->m_EdgeList.GetHeadPosition();

	for (i = pWallA->m_EdgeList.GetCount(); i > 0; i--)
	{
		CA3dWallEdge   *pEdgeA;
		POSITION        posB;
		int             j;

		pEdgeA = pWallA->m_EdgeList.GetNext(posA);

		A3dEdgeWorld(pEdgeA, avEdgeA);

		if (A3dSegmentCrosses(pWallB, avEdgeA))
			return (A3D_WALLS_CROSS);

		/* (RE) Inner edge walk, inlined here: dbg:0x10081600. */
		posB = pWallB->m_EdgeList.GetHeadPosition();

		for (j = pWallB->m_EdgeList.GetCount(); j > 0; j--)
		{
			CA3dWallEdge *pEdgeB;
			BOOL fB0InA, fB1InA;
			BOOL fA0InB, fA1InB;

			pEdgeB = pWallB->m_EdgeList.GetNext(posB);

			A3dEdgeWorld(pEdgeB, avEdgeB);

			/* (RE) Edge-relation classifier, inlined here: dbg:0x10081760. */
			if (!A3dEdgesSame(avEdgeA, avEdgeB))
			{
				fB0InA = A3dPointWithinEdge(&avEdgeB[0], avEdgeA);
				fB1InA = A3dPointWithinEdge(&avEdgeB[4], avEdgeA);

				if (!fB0InA || !fB1InA)
				{
					fA0InB = A3dPointWithinEdge(&avEdgeA[0], avEdgeB);
					fA1InB = A3dPointWithinEdge(&avEdgeA[4], avEdgeB);

					if ((!fA0InB || !fA1InB) && (!fB0InA || !fA0InB) &&
						(!fB1InA || !fA1InB))
						continue;
				}
			}

			A3dListAdd(pEdgeA->GetCrossList(), pEdgeB);
		}

		if (pEdgeA->GetCrossList()->cNodes > 0)
			cMet++;
	}

	return ((cMet > 0) ? A3D_WALLS_HAVE_EDGE_CONTACT : A3D_WALLS_HAVE_NO_CONTACT);
}

/* =============================================================
// A3dPointWithinEdge()
// (RE) dbg:0x10081860
//
// Test the interior of an axis-aligned edge. Original limitation:
// diagonal edges are rejected.
//
// Returns:
//   TRUE   if the point lies strictly inside
//   FALSE  otherwise
// =============================================================*/

BOOL
A3dPointWithinEdge(const A3DVAL *pcvPoint, const A3DVAL *pcvEdge)
{
	A3DVAL dx, dy, dz;
	A3DVAL ex, ey, ez;
	BOOL fFirst;
	BOOL fSecond;

	dx = (A3DVAL) fabs(pcvEdge[0] - pcvEdge[4]);
	dy = pcvEdge[1] - pcvEdge[5];
	dz = pcvEdge[2] - pcvEdge[6];

	fFirst  = FALSE;
	fSecond = FALSE;

	if (dx < A3D_EDGE_AXIS_TOLERANCE_F && fabs(dy) < A3D_EDGE_AXIS_TOLERANCE)
	{
		fFirst  = A3dPointOnEdge2D(pcvPoint, 0, 2, pcvEdge);
		fSecond = A3dPointOnEdge2D(pcvPoint, 1, 2, pcvEdge);
	}
	else if (dx < A3D_EDGE_AXIS_TOLERANCE_F &&
			 fabs(dz) < A3D_EDGE_AXIS_TOLERANCE)
	{
		fFirst  = A3dPointOnEdge2D(pcvPoint, 0, 1, pcvEdge);
		fSecond = A3dPointOnEdge2D(pcvPoint, 2, 1, pcvEdge);
	}
	else if (fabs(dy) < A3D_EDGE_AXIS_TOLERANCE &&
			 fabs(dz) < A3D_EDGE_AXIS_TOLERANCE)
	{
		fFirst  = A3dPointOnEdge2D(pcvPoint, 1, 0, pcvEdge);
		fSecond = A3dPointOnEdge2D(pcvPoint, 2, 0, pcvEdge);
	}

	if (!fFirst || !fSecond)
		return (FALSE);

	ex = pcvPoint[0] - pcvEdge[0];
	ey = pcvPoint[1] - pcvEdge[1];
	ez = pcvPoint[2] - pcvEdge[2];

	if (ex * ex + ey * ey + ez * ez < A3D_EDGE_ENDPOINT_DISTANCE_SQUARED_TOLERANCE)
		return (FALSE);

	ex = pcvPoint[0] - pcvEdge[4];
	ey = pcvPoint[1] - pcvEdge[5];
	ez = pcvPoint[2] - pcvEdge[6];

	return (ex * ex + ey * ey + ez * ez >= A3D_EDGE_ENDPOINT_DISTANCE_SQUARED_TOLERANCE);
}

/* =============================================================
// A3dPointOnEdge2D()
// (RE) dbg:0x10081b40
//
// Test collinearity and extent in two selected axes.
//
// Returns:
//   TRUE   if the projected point is on the edge
//   FALSE  otherwise
// =============================================================*/

BOOL
A3dPointOnEdge2D(const A3DVAL *pcvPoint, int iU, int iV, const A3DVAL *pcvEdge)
{
A3DVAL fPointV;
A3DVAL fPointU;
A3DVAL fEdgeU;
A3DVAL fEdgeV;
A3DVAL fCross;

	fPointV = pcvPoint[iV] - pcvEdge[iV];
	fEdgeU  = pcvEdge[iU + 4] - pcvEdge[iU];
	fPointU = pcvPoint[iU] - pcvEdge[iU];
	fEdgeV  = pcvEdge[iV + 4] - pcvEdge[iV];

	fCross = fPointV * fEdgeU - fPointU * fEdgeV;

	if (fCross > A3D_EDGE_PROJECTED_PRODUCT_TOLERANCE)
		return (FALSE);

	return (fCross >= -A3D_EDGE_PROJECTED_PRODUCT_TOLERANCE &&
			fPointU * fEdgeU >= -A3D_EDGE_PROJECTED_PRODUCT_TOLERANCE &&
			fPointV * fEdgeV >= -A3D_EDGE_PROJECTED_PRODUCT_TOLERANCE &&
			fPointU * fPointU + fPointV * fPointV <=
					fEdgeU * fEdgeU + fEdgeV * fEdgeV);
}

/* =============================================================
// A3dEdgesSame()
// (RE) dbg:0x10081c90
//
// Compare two edges' xyz endpoints within tolerance, in either order.
//
// Returns:
//   TRUE   if the endpoints match in order or reversed
//   FALSE  otherwise
// =============================================================*/

BOOL
A3dEdgesSame(const A3DVAL *pcvA, const A3DVAL *pcvB)
{
	A3DVAL dx, dy, dz;

	dx = pcvA[0] - pcvB[0];
	dy = pcvA[1] - pcvB[1];
	dz = pcvA[2] - pcvB[2];

	if (dx * dx + dy * dy + dz * dz < A3D_EDGE_ENDPOINT_DISTANCE_SQUARED_TOLERANCE)
	{
		dx = pcvA[4] - pcvB[4];
		dy = pcvA[5] - pcvB[5];
		dz = pcvA[6] - pcvB[6];

		if (dx * dx + dy * dy + dz * dz < A3D_EDGE_ENDPOINT_DISTANCE_SQUARED_TOLERANCE)
			return (TRUE);
	}

	dx = pcvA[0] - pcvB[4];
	dy = pcvA[1] - pcvB[5];
	dz = pcvA[2] - pcvB[6];

	if (dx * dx + dy * dy + dz * dz >= A3D_EDGE_ENDPOINT_DISTANCE_SQUARED_TOLERANCE)
		return (FALSE);

	dx = pcvA[4] - pcvB[0];
	dy = pcvA[5] - pcvB[1];
	dz = pcvA[6] - pcvB[2];

	return (dx * dx + dy * dy + dz * dz < A3D_EDGE_ENDPOINT_DISTANCE_SQUARED_TOLERANCE);
}

/* =============================================================
// NewTransform()
// (RE) rtl:0x10033970; dbg:0x10081e70
//
// Allocate a snapshot of the wall transform. Original defect: ppXform
// is not checked.
//
// Returns:
//   S_OK
//   A3DERROR_MEMORY_ALLOCATION  if allocation fails
// =============================================================*/

HRESULT
CA3dWall::NewTransform(void **ppXform)
{
CA3dFrame *pXform;

	pXform = new CA3dFrame;
	if (!pXform)
		return (A3DERROR_MEMORY_ALLOCATION);

	pXform->CopyFrameFrom(this, Xform());

	((IUnknown *) pXform)->AddRef();

	*ppXform = pXform;

	return (S_OK);
}

/* =============================================================
// SetMaterial()
// (RE) rtl:0x10033a00; dbg:0x10081f60
//
// Bind the wall material.
//
// Returns: The material-link result.
// =============================================================*/

HRESULT
CA3dWall::SetMaterial(void *pMaterial)
{
	return (m_link.SetMaterial(pMaterial));
}

/* =============================================================
// GetMaterial()
// (RE) rtl:0x10033a20; dbg:0x10081f90
//
// Return a new copy of the bound material.
//
// Returns: The material-link result.
// =============================================================*/

HRESULT
CA3dWall::GetMaterial(void **ppMaterial)
{
	return (m_link.GetMaterial(ppMaterial));
}

/* =============================================================
// GetRoom()
// (RE) rtl:0x10033a40; dbg:0x10081fc0
//
// Return the owning room with a new reference.
//
// Returns:
//   S_OK
//   E_INVALIDARG   if ppRoom is null
//   E_NOINTERFACE  if unattached
// =============================================================*/

HRESULT
CA3dWall::GetRoom(void **ppRoom)
{
	if (!ppRoom)
		return (E_INVALIDARG);

	*ppRoom = NULL;

	if (!m_pRoom)
		return (E_NOINTERFACE);

	*ppRoom = m_pRoom;

	((IUnknown *) m_pRoom)->AddRef();

	return (S_OK);
}

/* =============================================================
// MarkSceneStale()
// (RE) dbg:0x10082210
//
// Invalidate the scene for a shell wall attached to a room.
// =============================================================*/

void
CA3dWall::MarkSceneStale(void)
{
	if (m_dwFlags & A3DWALL_FLAG_SHELL)
	{
		if (m_pRoom)
			m_pRoom->MarkSceneStale();
	}
}

/* =============================================================
// CopyFrom()
// (RE) rtl:0x10033a90; dbg:0x10082040
//
// Copy the matrix from a CA3dFrame and propagate staleness.
//
// Returns:
//   S_OK
//   A3DERROR_INVALID_ARGUMENT  if pcFrom is null
// =============================================================*/

HRESULT
CA3dWall::CopyFrom(const void *pcFrom)
{
	if (!pcFrom)
		return (A3DERROR_INVALID_ARGUMENT);

	A3dXformCopyMatrix(Xform(), pcFrom);

	MarkSceneStale();

	return (S_OK);
}

/* =============================================================
// GetPosition()
// (RE) rtl:0x1002e8b0; dbg:0x10082090
//
// Read the wall position relative to a frame.
//
// Returns: The embedded transform result.
// =============================================================*/

HRESULT
CA3dWall::GetPosition(const CA3dFrame *pcRelativeTo, A3DVAL *pv)
{
	return (Xform()->GetPosition(pcRelativeTo, pv));
}

/* =============================================================
// SUCCEEDED compiler-generated helper
// (RE) dbg:0x10082120
// =============================================================*/

/* =============================================================
// FAILED compiler-generated helper
// (RE) dbg:0x10082150
// =============================================================*/

/* =============================================================
// SetPosition()
// (RE) rtl:0x10033ad0; dbg:0x100820c0
//
// Set the wall position and propagate staleness on success.
//
// Returns: The embedded transform result.
// =============================================================*/

HRESULT
CA3dWall::SetPosition(const CA3dFrame *pcRelativeTo, const A3DVAL *pcv)
{
HRESULT hr;

	hr = Xform()->SetPosition(pcRelativeTo, pcv);

	if (SUCCEEDED(hr))
		MarkSceneStale();

	return (hr);
}

/* =============================================================
// GetOrientation()
// (RE) rtl:0x10033b10; dbg:0x10082170
//
// Read the wall axes relative to a frame.
//
// Returns: The embedded transform result.
// =============================================================*/

HRESULT
CA3dWall::GetOrientation(const CA3dFrame *pcRelativeTo, A3DVAL *pvFront, A3DVAL *pvUp)
{
	return (Xform()->GetOrientation(pcRelativeTo,
									pvFront, pvUp));
}

/* =============================================================
// Emit()
// (RE) dbg:0x10082260
//
// Submit unmarked wall geometry and openings. Original defect:
// unsupported primitive sizes call End without Begin.
// =============================================================*/

void
CA3dWall::Emit(CA3dRoot *pApi)
{
IA3dGeom2 *pGeom;
A3DPRIMITIVE primBlock;
const A3DPRIMITIVE *pBlock;
POSITION pos;
DWORD cVertices;
	int i;
	DWORD v;

	if (m_dwFlags & A3DWALL_FLAG_MARKED)
		return;

	pGeom = (IA3dGeom2 *) pApi;

	if (m_link.HasMaterial())
		pGeom->BindMaterial(m_link.GetMaterialRaw());

	pos = m_PrimList.GetHeadPosition();

	for (i = m_PrimList.GetCount(); i > 0; i--)
	{
		pBlock = m_PrimList.GetNext(pos);

		FetchPrimitive(&primBlock, pBlock);

		cVertices = pBlock->cVertices;

		if (cVertices != 3 && cVertices != 4)
		{
			pGeom->End();

			continue;
		}

		pGeom->Begin(cVertices);
		pGeom->Tag((DWORD) pBlock);
		pGeom->Normal3fv(primBlock.vNormal);

		for (v = 0; v < cVertices; v++)
			pGeom->Vertex3fv(primBlock.av[v]);

		pGeom->End();
	}

	pos = m_OpeningList.GetHeadPosition();

	for (i = m_OpeningList.GetCount(); i > 0; i--)
	{
	CA3dOpening *pOpening;

		pOpening = m_OpeningList.GetNext(pos);

		A3dEmitMarked(pOpening, pApi);
	}
}

/* =============================================================
// MakePortals()
// (RE) dbg:0x10082560
//
// Copy covered original openings from touching walls as portals. Original
// defect: allocation failure is dereferenced by AddRef.
// =============================================================*/

void
CA3dWall::MakePortals(void)
{
CA3dWall       *pTouching;
CA3dOpening    *pOpening;
CA3dOpening    *pPortal;
POSITION        posTouch;
	int i, n;

	posTouch = m_TouchList.GetHeadPosition();

	for (i = m_TouchList.GetCount(); i > 0; i--)
	{
		POSITION posOpen;

		pTouching = m_TouchList.GetNext(posTouch);

		posOpen = pTouching->m_OpeningList.GetHeadPosition();

		for (n = pTouching->m_OpeningList.GetCount(); n > 0; n--)
		{
			pOpening = pTouching->m_OpeningList.GetNext(posOpen);

			if (!pOpening->GetOriginal())
				continue;

			if (!Covers((CA3dWall *) pOpening))
				continue;

			pPortal = new CA3dOpening(pOpening, this, pTouching);

			((IUnknown *) pPortal)->AddRef();

			m_OpeningList.AddTail(pPortal);
		}
	}
}

/* =============================================================
// GetOpening()
// (RE) rtl:0x10033ed0; dbg:0x10082770
//
// Return an opening by one-based index. Preserve the original room error
// for an empty opening list.
//
// Returns:
//   S_OK
//   E_INVALIDARG                    if ppOpening is null
//   A3DERROR_INVALID_OPENING_INDEX  an invalid index
//   A3DERROR_NO_ROOMS_IN_SCENE      if the list is empty
// =============================================================*/

HRESULT
CA3dWall::GetOpening(int nOpening, void **ppOpening)
{
CA3dOpening    *pOpening;
POSITION        pos;

	if (!ppOpening)
		return (E_INVALIDARG);

	if (nOpening <= 0)
		return (A3DERROR_INVALID_OPENING_INDEX);

	*ppOpening = NULL;

	if (m_OpeningList.GetCount() <= 0)
		return (A3DERROR_NO_ROOMS_IN_SCENE);

	pos = m_OpeningList.FindIndex(nOpening);

	pOpening = pos ? m_OpeningList.GetAt(pos) : NULL;

	if (!pOpening)
		return (A3DERROR_INVALID_OPENING_INDEX);

	*ppOpening = pOpening;

	((IUnknown *) pOpening)->AddRef();

	return (S_OK);
}

/* =============================================================
// Covers()
// (RE) dbg:0x10082850
//
// Test whether this wall covers a primitive of the other wall.
//
// Returns: The other wall CoveredBy result.
// =============================================================*/

int
CA3dWall::Covers(CA3dWall *pOther)
{
	return (pOther->CoveredBy(this));
}

/* -------------------------------------------------------------------------- */

typedef int A3dWallSizeCheck[(sizeof(CA3dWall) == 0xAC) ? 1 : -1];
