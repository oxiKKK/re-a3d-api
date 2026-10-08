/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * PolygonBuilder.cpp
 *
 * Implements primitive input and boundary construction shared by wall and
 * opening builders. It collects vertices into supported primitive types,
 * computes their plane and normal, and exposes indexed vertex editing.
 *
 * Validation checks the retained geometry and constructs its unmatched
 * boundary edges for later coverage tests. The specialized builders add
 * materials, names and opening relationships; this file handles the
 * common polygon construction state.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "PolygonBuilder.h"
#include "Corners.h"
#include "LinkList.h"

#include <math.h>


#define A3D_POLYGON_ORIGIN_DISTANCE_SQUARED_TOLERANCE 1.0e-12
#define A3D_POLYGON_PLANE_DOT_TOLERANCE 0.000001

/* =============================================================
// CA3dPolygonBuilder()
// (RE) dbg:0x10085f00
//
// Initialize vertex storage, the normal and the lists. Original defect:
// primitive metadata, normal w, vertex index and first-block pointer
// remain uninitialized.
// =============================================================*/

CA3dPolygonBuilder::CA3dPolygonBuilder(int nType)
{
int i;

	for (i = 0; i < 4; i++)
	{
		m_prim.av[i][0] = 0.0f;
		m_prim.av[i][1] = 0.0f;
		m_prim.av[i][2] = 0.0f;
		m_prim.av[i][3] = 1.0f;
	}

	m_prim.vNormal[0] = 0.0f;
	m_prim.vNormal[1] = 0.0f;
	m_prim.vNormal[2] = 0.0f;

	m_fChecked      = 0;
	m_nType         = nType;
	m_pvPlaneNormal = NULL;
}

/* =============================================================
// ~CA3dPolygonBuilder()
//
// Delete the owned primitives and edges.
// =============================================================*/

CA3dPolygonBuilder::~CA3dPolygonBuilder(void)
{
	Clear();
}

/* =============================================================
// CompleteVertex()
//
// Finish a primitive when its last vertex arrives and compute its normal.
//
// Returns: The next vertex index cast to void * while incomplete; otherwise the
//          allocated primitive, or null on allocation failure.
// =============================================================*/

void *
CA3dPolygonBuilder::CompleteVertex(void)
{
A3DPRIMITIVE *pBlock;
	A3DVAL ax, ay, az;
	A3DVAL bx, by, bz;
	A3DVAL fLength;

	m_nVertexIndex++;

	if (m_nVertexIndex < m_prim.cVertices)
		return ((void *) m_nVertexIndex);

	pBlock = new A3DPRIMITIVE;

	if (pBlock)
	{
		ax = m_prim.av[0][0] - m_prim.av[1][0];
		ay = m_prim.av[0][1] - m_prim.av[1][1];
		az = m_prim.av[0][2] - m_prim.av[1][2];

		bx = m_prim.av[0][0] - m_prim.av[2][0];
		by = m_prim.av[0][1] - m_prim.av[2][1];
		bz = m_prim.av[0][2] - m_prim.av[2][2];

		m_prim.vNormal[0] = bz * ay - by * az;
		m_prim.vNormal[1] = bx * az - bz * ax;
		m_prim.vNormal[2] = by * ax - bx * ay;

		fLength = (A3DVAL) sqrt(m_prim.vNormal[0] * m_prim.vNormal[0] +
								m_prim.vNormal[1] * m_prim.vNormal[1] +
								m_prim.vNormal[2] * m_prim.vNormal[2]);

		if (fLength != 0.0f)
		{
			m_prim.vNormal[0] = (1.0f / fLength) * m_prim.vNormal[0];
			m_prim.vNormal[1] = (1.0f / fLength) * m_prim.vNormal[1];
			m_prim.vNormal[2] = (1.0f / fLength) * m_prim.vNormal[2];
		}

		*pBlock = m_prim;

		m_PrimList.AddTail(pBlock);
	}

	m_nVertexIndex = 0;

	return (pBlock);
}

/* =============================================================
// CheckPlanar()
//
// Compute the reference plane and test all primitives against it.
//
// Returns:
//   S_OK
//   E_FAIL  if empty or nonplanar
// =============================================================*/

HRESULT
CA3dPolygonBuilder::CheckPlanar(void)
{
A3DPRIMITIVE   *pBlock;
A3DPRIMITIVE   *p;
A3DVAL         *pv;
const A3DVAL   *pvFirst;
POSITION        pos;
	A3DVAL a[3], b[3];
	A3DVAL  d[3];
	double  dLength;
	int     cBlocks;
	int i, v;

	cBlocks = m_PrimList.GetCount();

	pos = m_PrimList.GetHeadPosition();

	if (!m_PrimList.IsEnd(pos))
		pBlock = m_PrimList.GetNext(pos);
	else
		pBlock = NULL;

	m_pFirstBlock = pBlock;

	if (!pBlock)
		return (E_FAIL);

	pvFirst = pBlock->av[0];

	a[0] = pvFirst[0] - pBlock->av[2][0];
	a[1] = pvFirst[1] - pBlock->av[2][1];
	a[2] = pvFirst[2] - pBlock->av[2][2];

	b[0] = pvFirst[0] - pBlock->av[1][0];
	b[1] = pvFirst[1] - pBlock->av[1][1];
	b[2] = pvFirst[2] - pBlock->av[1][2];

	pv = pBlock->vNormal;

	pv[0] = a[2] * b[1] - a[1] * b[2];
	pv[1] = a[0] * b[2] - a[2] * b[0];
	pv[2] = a[1] * b[0] - a[0] * b[1];

	dLength = sqrt(pv[0] * pv[0] + pv[1] * pv[1] + pv[2] * pv[2]);

	if (dLength != 0.0)
	{
		pv[0] = (A3DVAL) (1.0 / dLength * pv[0]);
		pv[1] = (A3DVAL) (1.0 / dLength * pv[1]);
		pv[2] = (A3DVAL) (1.0 / dLength * pv[2]);
	}

	m_pvPlaneNormal = pv;

	if (pBlock->cVertices == 4)
	{
		d[0] = pvFirst[0] - pBlock->av[3][0];
		d[1] = pvFirst[1] - pBlock->av[3][1];
		d[2] = pvFirst[2] - pBlock->av[3][2];

		if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] >=
			A3D_POLYGON_ORIGIN_DISTANCE_SQUARED_TOLERANCE &&
			fabs(d[0] * pv[0] + d[1] * pv[1] + d[2] * pv[2]) >=
					A3D_POLYGON_PLANE_DOT_TOLERANCE)
			return (E_FAIL);
	}

	for (i = cBlocks - 1; i > 0; i--)
	{
		p = m_PrimList.GetNext(pos);

		if (!p->cVertices)
			continue;

		for (v = p->cVertices - 1; v >= 0; v--)
		{
			pv = p->av[v];

			d[0] = pvFirst[0] - pv[0];
			d[1] = pvFirst[1] - pv[1];
			d[2] = pvFirst[2] - pv[2];

			if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] >=
				A3D_POLYGON_ORIGIN_DISTANCE_SQUARED_TOLERANCE &&
				fabs(d[0] * m_pvPlaneNormal[0] + d[1] * m_pvPlaneNormal[1] +
					 d[2] * m_pvPlaneNormal[2]) >= A3D_POLYGON_PLANE_DOT_TOLERANCE)
				return (E_FAIL);
		}
	}

	return (S_OK);
}

/* =============================================================
// MarkEdge()
//
// Update the unmatched-edge mask. Original defect: vertex 3 uses mask 3.
// =============================================================*/

void
CA3dPolygonBuilder::MarkEdge(A3DPRIMITIVE *pPrim, int nVertex, int fUnmatched)
{
DWORD dwBit;
DWORD dwMask;

	switch (nVertex)
	{
	case 0:
		dwBit = 1;
		break;

	case 1:
		dwBit = 2;
		break;

	case 2:
		dwBit = 4;
		break;

	case 4:
		dwBit = 8;
		break;

	default:
		dwBit = (DWORD) nVertex;
		break;
	}

	dwMask = pPrim->dwFlags;

	if (fUnmatched)
	{
		if (!(dwMask & dwBit))
			pPrim->dwFlags = dwBit ^ dwMask;
	}
	else
	{
		if (dwMask & dwBit)
			pPrim->dwFlags = dwBit ^ dwMask;
	}
}

/* =============================================================
// EmitUnmatchedEdges()
//
// Emit boundary edges from the low flag nibble. Preserve the original
// mapping: mask 2 emits both edges 0-1 and 1-2.
// =============================================================*/

void
CA3dPolygonBuilder::EmitUnmatchedEdges(const A3DPRIMITIVE *pcPrim)
{
int fQuad;

	fQuad = (pcPrim->cVertices == 4);

	switch (pcPrim->dwFlags & A3D_EDGEMASK_ALL)
	{
	case 1:
		AddEdge(pcPrim, 0, 1);

		if (!fQuad)
			AddEdge(pcPrim, 2, 0);
		else
			AddEdge(pcPrim, 3, 0);
		break;

	case 2:
		AddEdge(pcPrim, 0, 1);
		AddEdge(pcPrim, 1, 2);
		break;

	case 3:
		AddEdge(pcPrim, 0, 1);
		AddEdge(pcPrim, 1, 2);

		if (!fQuad)
			AddEdge(pcPrim, 2, 0);
		else
			AddEdge(pcPrim, 3, 0);
		break;

	case 4:
		AddEdge(pcPrim, 1, 2);

		if (fQuad)
			AddEdge(pcPrim, 2, 3);
		else
			AddEdge(pcPrim, 2, 0);
		break;

	case 6:
		AddEdge(pcPrim, 0, 1);
		AddEdge(pcPrim, 1, 2);

		if (fQuad)
			AddEdge(pcPrim, 2, 3);
		else
			AddEdge(pcPrim, 2, 0);
		break;

	case 8:
		AddEdge(pcPrim, 2, 3);
		AddEdge(pcPrim, 3, 0);
		break;

	case 9:
		AddEdge(pcPrim, 0, 1);
		AddEdge(pcPrim, 2, 3);
		AddEdge(pcPrim, 3, 0);
		break;

	case 12:
		AddEdge(pcPrim, 1, 2);
		AddEdge(pcPrim, 2, 3);
		AddEdge(pcPrim, 3, 0);
		break;

	case 5:
	case 7:
	case 10:
	case 11:
	case 13:
	case 14:
	case 15:
		AddEdge(pcPrim, 0, 1);
		AddEdge(pcPrim, 1, 2);

		if (!fQuad)
		{
			AddEdge(pcPrim, 2, 0);
			break;
		}

		AddEdge(pcPrim, 2, 3);
		AddEdge(pcPrim, 3, 0);
		break;

	default:
		break;
	}
}

/* =============================================================
// AddEdge()
//
// Copy two homogeneous vertices into an owned edge. Original defect:
// allocation failure is dereferenced.
// =============================================================*/

void
CA3dPolygonBuilder::AddEdge(const A3DPRIMITIVE *pcPrim, int nFrom, int nTo)
{
A3DEDGE        *pEdge;
const A3DVAL   *pv;

	pEdge = new A3DEDGE;

	pv = pcPrim->av[nFrom];

	pEdge->avEnd[0][0] = pv[0];
	pEdge->avEnd[0][1] = pv[1];
	pEdge->avEnd[0][2] = pv[2];
	pEdge->avEnd[0][3] = pv[3];

	pv = pcPrim->av[nTo];

	pEdge->avEnd[1][0] = pv[0];
	pEdge->avEnd[1][1] = pv[1];
	pEdge->avEnd[1][2] = pv[2];
	pEdge->avEnd[1][3] = pv[3];

	m_EdgeList.AddTail(pEdge);
}

/* =============================================================
// ClearEdges()
//
// Delete the owned edges and their list nodes.
// =============================================================*/

void
CA3dPolygonBuilder::ClearEdges(void)
{
	m_EdgeList.DeleteAll();
}

/* =============================================================
// ClearPrimitives()
//
// Delete the owned primitives and their list nodes.
// =============================================================*/

void
CA3dPolygonBuilder::ClearPrimitives(void)
{
	m_PrimList.DeleteAll();
}

/* =============================================================
// Clear()
//
// Delete primitives and edges, and clear the plane pointer.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
CA3dPolygonBuilder::Clear(void)
{
	ClearPrimitives();
	ClearEdges();

	m_pvPlaneNormal = NULL;

	return (S_OK);
}

/* =============================================================
// MergeEdges()
//
// Test retained edges against primitive edges.
// =============================================================*/

void
CA3dPolygonBuilder::MergeEdges(void)
{
const A3DEDGE          *pcEdge;
const A3DPRIMITIVE     *pcPrim;
POSITION                posEdge;
POSITION                posPrim;
int                     i;
int                     j;

	posEdge = m_EdgeList.GetHeadPosition();

	for (i = m_EdgeList.GetCount(); i > 0; i--)
	{
		pcEdge = m_EdgeList.GetNext(posEdge);

		posPrim = m_PrimList.GetHeadPosition();

		for (j = m_PrimList.GetCount(); j > 0; j--)
		{
			pcPrim = m_PrimList.GetNext(posPrim);

			A3dEdgeAgainstPrim(pcPrim, 0, 1, pcEdge);
			A3dEdgeAgainstPrim(pcPrim, 1, 2, pcEdge);

			if (pcPrim->cVertices == 3)
			{
				A3dEdgeAgainstPrim(pcPrim, 2, 0, pcEdge);
			}
			else
			{
				A3dEdgeAgainstPrim(pcPrim, 2, 3, pcEdge);
				A3dEdgeAgainstPrim(pcPrim, 3, 0, pcEdge);
			}
		}
	}
}

/* =============================================================
// Begin()
//
// Begin a primitive compatible with the builder dimensionality.
//
// Returns:
//   S_OK
//   A3DERROR_MIXING_2D_AND_3D_MODES  incompatible modes
//   A3DERROR_INVALID_BEGIN_MODE      unsupported modes
// =============================================================*/

HRESULT
CA3dPolygonBuilder::Begin(DWORD dwMode)
{
	m_prim.cVertices = 0;

	switch (dwMode)
	{
	case A3D_LINES:
	case A3D_SUB_LINES:
		if (m_nType == A3D_SCENE_3D)
			return (A3DERROR_MIXING_2D_AND_3D_MODES);

		m_prim.cVertices = 2;
		break;

	case A3D_TRIANGLES:
	case A3D_SUB_TRIANGLES:
		if (m_nType == A3D_SCENE_2D)
			return (A3DERROR_MIXING_2D_AND_3D_MODES);

		m_prim.cVertices = 3;
		break;

	case A3D_QUADS:
	case A3D_SUB_QUADS:
		if (m_nType == A3D_SCENE_2D)
			return (A3DERROR_MIXING_2D_AND_3D_MODES);

		m_prim.cVertices = 4;
		break;

	default:
		return (A3DERROR_INVALID_BEGIN_MODE);
	}

	m_nVertexIndex = 0;

	return (S_OK);
}

/* =============================================================
// End()
//
// Close primitive input.
//
// Returns:
//   S_OK
//   A3DERROR_END_BEFORE_VALID_BEGIN_BLOCK
//                                       if none is open
// =============================================================*/

HRESULT
CA3dPolygonBuilder::End(void)
{
	if (!m_prim.cVertices)
		return (A3DERROR_END_BEFORE_VALID_BEGIN_BLOCK);

	m_prim.cVertices = 0;

	return (S_OK);
}

/* =============================================================
// Vertex3f()
//
// Add a vertex. The original writes coordinates before rejecting a second
// 2D line.
//
// Returns:
//   S_OK
//   A3DERROR_INVALID_BEGIN_MODE         if no primitive is open
//   A3DERROR_2DWALL_REQUIRES_EXACTLY_ONE_LINE
//                                       if a 2D line already exists
// =============================================================*/

HRESULT
CA3dPolygonBuilder::Vertex3f(A3DVAL x, A3DVAL y, A3DVAL z)
{
	if (!m_prim.cVertices)
		return (A3DERROR_INVALID_BEGIN_MODE);

	m_prim.av[m_nVertexIndex][0] = x;
	m_prim.av[m_nVertexIndex][1] = y;
	m_prim.av[m_nVertexIndex][2] = z;

	if (m_nType == A3D_SCENE_2D && m_PrimList.GetCount() == 1)
		return (A3DERROR_2DWALL_REQUIRES_EXACTLY_ONE_LINE);

	CompleteVertex();

	return (S_OK);
}

/* =============================================================
// Vertex3fv()
//
// Add a vertex from an xyz vector.
//
// Returns:
//   S_OK
//   A3DERROR_INVALID_ARGUMENT           if pv is null
//   A3DERROR_INVALID_BEGIN_MODE         if no primitive is open
//   A3DERROR_2DWALL_REQUIRES_EXACTLY_ONE_LINE
//                                       if a 2D line already exists
// =============================================================*/

HRESULT
CA3dPolygonBuilder::Vertex3fv(LPA3DVAL pv)
{
	if (!pv)
		return (A3DERROR_INVALID_ARGUMENT);

	if (!m_prim.cVertices)
		return (A3DERROR_INVALID_BEGIN_MODE);

	if (m_nType == A3D_SCENE_2D && m_PrimList.GetCount() == 1)
		return (A3DERROR_2DWALL_REQUIRES_EXACTLY_ONE_LINE);

	m_prim.av[m_nVertexIndex][0] = pv[0];
	m_prim.av[m_nVertexIndex][1] = pv[1];
	m_prim.av[m_nVertexIndex][2] = pv[2];

	CompleteVertex();

	return (S_OK);
}

/* =============================================================
// RemovePrimitive()
//
// Delete the primitive at a zero-based index.
//
// Returns:
//   S_OK
//   A3DERROR_INVALID_INDEX  if the primitive is absent
// =============================================================*/

HRESULT
CA3dPolygonBuilder::RemovePrimitive(int nPrim)
{
POSITION        pos;
A3DPRIMITIVE   *pBlock;

	pos = m_PrimList.FindIndex(nPrim + 1);

	pBlock = NULL;

	if (pos)
		pBlock = m_PrimList.GetAt(pos);

	if (!pBlock)
		return (A3DERROR_INVALID_INDEX);

	delete pBlock;

	pos = m_PrimList.FindIndex(nPrim + 1);

	m_PrimList.RemoveAt(pos);

	return (S_OK);
}

/* =============================================================
// GetPrimitiveCount()
//
// Read the retained primitive count.
//
// Returns: The number of primitives.
// =============================================================*/

int
CA3dPolygonBuilder::GetPrimitiveCount(void)
{
	return (m_PrimList.GetCount());
}

/* =============================================================
// SetVertex()
//
// Set one primitive vertex position.
//
// Returns:
//   S_OK
//   A3DERROR_INVALID_PRIMITIVE_INDEX  or A3DERROR_INVALID_VERTEX_INDEX for
//                                     out-of-range indices
// =============================================================*/

HRESULT
CA3dPolygonBuilder::SetVertex(int nPrim, int nVertex, A3DVAL x, A3DVAL y, A3DVAL z)
{
POSITION        pos;
A3DPRIMITIVE   *pBlock;
A3DVAL         *pv;

	if (nPrim < 0 || nPrim >= m_PrimList.GetCount())
		return (A3DERROR_INVALID_PRIMITIVE_INDEX);

	pos = m_PrimList.FindIndex(nPrim + 1);

	pBlock = NULL;

	if (pos)
		pBlock = m_PrimList.GetAt(pos);

	if (nVertex < 0 || nVertex >= pBlock->cVertices)
		return (A3DERROR_INVALID_VERTEX_INDEX);

	pv = pBlock->av[nVertex];

	pv[0] = x;
	pv[1] = y;
	pv[2] = z;

	return (S_OK);
}

/* =============================================================
// GetVertex()
//
// Read one primitive vertex position.
//
// Returns:
//   S_OK
//   A3DERROR_INVALID_ARGUMENT         if pv is null
//   A3DERROR_INVALID_PRIMITIVE_INDEX  or A3DERROR_INVALID_VERTEX_INDEX for
//                                     out-of-range indices
// =============================================================*/

HRESULT
CA3dPolygonBuilder::GetVertex(int nPrim, int nVertex, LPA3DVAL pv)
{
POSITION                pos;
const A3DPRIMITIVE     *pBlock;
const A3DVAL           *pvFrom;

	if (!pv)
		return (A3DERROR_INVALID_ARGUMENT);

	if (nPrim < 0 || nPrim >= m_PrimList.GetCount())
		return (A3DERROR_INVALID_PRIMITIVE_INDEX);

	pos = m_PrimList.FindIndex(nPrim + 1);

	pBlock = NULL;

	if (pos)
		pBlock = m_PrimList.GetAt(pos);

	if (nVertex < 0 || nVertex >= pBlock->cVertices)
		return (A3DERROR_INVALID_VERTEX_INDEX);

	pvFrom = pBlock->av[nVertex];

	pv[0] = pvFrom[0];
	pv[1] = pvFrom[1];
	pv[2] = pvFrom[2];

	return (S_OK);
}

/* =============================================================
// CheckPrimitives()
//
// Validate geometry and build boundary edges. Original defect: 2D lines
// receive the triangle edge mask and emit edges through vertex 2.
//
// Returns: S_OK; A3DERROR_NO_PRIMITIVES_DEFINED,
//          A3DERROR_PRIMITIVES_NON_PLANAR, A3DERROR_PRIMITIVES_OVERLAPPING or
//          A3DERROR_PRIMITIVES_NOT_ADJACENT if validation fails.
// =============================================================*/

HRESULT
CA3dPolygonBuilder::CheckPrimitives(void)
{
A3DLIST                 listWork;
A3DLISTNODE            *pNode;
A3DLISTNODE            *pNext;
A3DPRIMITIVE           *pBlock;
const A3DPRIMITIVE     *pOther;
A3DVERTEX               avQuad[3];
const A3DVAL           *pv;
POSITION                pos;
int                     fAdjacent;
int                     cVertices;
int                     nResult;
	int i, v, n;

	if (m_PrimList.GetCount() <= 0)
		return (A3DERROR_NO_PRIMITIVES_DEFINED);

	if (m_nType == A3D_SCENE_3D && FAILED(CheckPlanar()))
		return (A3DERROR_PRIMITIVES_NON_PLANAR);

	listWork.pHead  = NULL;
	listWork.pTail  = NULL;
	listWork.pCurr  = NULL;
	listWork.cNodes = 0;

	pos = m_PrimList.GetHeadPosition();

	A3dListClear(&listWork);

	for (i = m_PrimList.GetCount(); i > 0; i--)
	{
		pBlock = m_PrimList.GetNext(pos);

		A3dListAdd(&listWork, pBlock);
	}

	ClearEdges();

	pos = m_PrimList.GetHeadPosition();

	for (i = m_PrimList.GetCount(); i > 0; i--)
	{
		pBlock = m_PrimList.GetNext(pos);

		fAdjacent = 0;
		cVertices = pBlock->cVertices;

		pBlock->dwFlags = (cVertices == 4) ? A3D_EDGEMASK_QUAD
						 : A3D_EDGEMASK_TRIANGLE;

		for (v = cVertices - 1; v >= 0; v--)
		{
			pNode = listWork.pHead;

			for (n = listWork.cNodes; n > 0; n--)
			{
				if (pNode)
				{
					pOther  = (const A3DPRIMITIVE *) pNode->pObject;
					pNode   = pNode->pNext;
				}
				else
				{
					pOther = NULL;
				}

				if (pOther == pBlock)
					continue;

				pv = pBlock->av[v];

				nResult = A3dEdgeCompare(pv, m_pvPlaneNormal,
										 pOther->av[0]);

				if (nResult == A3D_EDGE_MISSES && pOther->cVertices == 4)
				{
					int c;

					for (c = 0; c < 4; c++)
					{
						avQuad[0][c] = pOther->av[0][c];
						avQuad[1][c] = pOther->av[2][c];
						avQuad[2][c] = pOther->av[3][c];
					}

					nResult = A3dEdgeCompare(pv, m_pvPlaneNormal,
											 avQuad[0]);
				}

				if (nResult == A3D_EDGE_ENDPOINT)
				{
					fAdjacent = 1;

					MarkEdge(pBlock, v, 0);

					continue;
				}

				if (nResult == A3D_EDGE_CROSSES)
				{
					pNode = listWork.pHead;

					for (n = 0; n < listWork.cNodes; n++)
					{
						pNext = pNode->pNext;

						if (pNode)
							operator delete(pNode);

						pNode = pNext;
					}

					return (A3DERROR_PRIMITIVES_OVERLAPPING);
				}

				MarkEdge(pBlock, v, 1);
			}
		}

		if (!fAdjacent && m_PrimList.GetCount() > 1)
		{
			pNode = listWork.pHead;

			for (n = 0; n < listWork.cNodes; n++)
			{
				pNext = pNode->pNext;

				if (pNode)
					operator delete(pNode);

				pNode = pNext;
			}

			return (A3DERROR_PRIMITIVES_NOT_ADJACENT);
		}

		EmitUnmatchedEdges(pBlock);
	}

	MergeEdges();

	m_fChecked = 1;

	pNode = listWork.pHead;

	for (n = 0; n < listWork.cNodes; n++)
	{
		pNext = pNode->pNext;

		if (pNode)
			operator delete(pNode);

		pNode = pNext;
	}

	return (S_OK);
}
