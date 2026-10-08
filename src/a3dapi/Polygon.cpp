/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * Polygon.cpp
 *
 * Implements owned polygon primitives and their placement frame.
 * CA3dPolygon allocates or copies primitive records, releases them with
 * the object, and transforms vertices and normals when geometry is
 * requested in another coordinate system.
 *
 * Walls and openings use this common storage rather than maintaining
 * separate primitive containers. A3dFrame supplies the transform
 * operations, and Corners.h defines the primitive format consumed by the
 * builders and tracing code.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "Polygon.h"
#include "A3dFrame.h"
#include "LinkList.h"

static A3DPRIMITIVE *CopyPrimitiveBlock(const A3DPRIMITIVE *pcPrim);

/* =============================================================
// CA3dPolygon()
// (RE) rtl:0x10034a10; dbg:0x10085130
//
// Initialize an empty primitive list and the origin/+Z front/+Y up frame.
// =============================================================*/

CA3dPolygon::CA3dPolygon(void)
{
	static const A3DVAL vAt[4] = {0.0f, 0.0f, 0.0f, 1.0f};
	static const A3DVAL vFront[4] = {0.0f, 0.0f, 1.0f, 0.0f};
	static const A3DVAL vUp[4] = {0.0f, 1.0f, 0.0f, 0.0f};

	ZeroMemory(m_xform.m_mat, sizeof(m_xform.m_mat));

	m_xform.m_pOwner        = NULL;
	m_xform.m_pParent       = NULL;

	SetFrame(NULL, (LPA3DVAL) vAt, (LPA3DVAL) vFront, (LPA3DVAL) vUp);
}

/* =============================================================
// ~CA3dPolygon()
//
// Free owned primitives and their list nodes.
// =============================================================*/

CA3dPolygon::~CA3dPolygon(void)
{
	ClearPrims();
}

/* =============================================================
// SetFrame()
//
// Set the owner and position, front and up vectors on the frame.
// =============================================================*/

void
CA3dPolygon::SetFrame(void *pOwner, LPA3DVAL pvPosition, LPA3DVAL pvFront,
					LPA3DVAL pvUp)
{
	A3dFrameSet(&m_xform, pOwner, pvPosition, pvFront, pvUp);
}

/* =============================================================
// ClearPrims()
//
// Free owned primitive blocks and nodes, retaining an empty list.
// =============================================================*/

void
CA3dPolygon::ClearPrims(void)
{
	m_PrimList.DeleteAll();
}

/* =============================================================
// CopyPrimitivesThrough()
//
// Copy primitives through a matrix. Preserve the discarded source-frame
// fetch, unchecked allocation and read of uninitialized normal w.
// =============================================================*/

void
CA3dPolygon::CopyPrimitivesThrough(const CA3dPtrList<A3DPRIMITIVE> *pFrom,
								 const CA3dPolygon *pFromFrame,
								 const A3DVAL *pMatrix)
{
A3DPRIMITIVE            primFetched;
const A3DVAL           *pvSrc;
A3DVAL                 *pvDst;
const A3DPRIMITIVE     *pcPrim;
A3DPRIMITIVE           *pCopy;
POSITION                pos;
	int i, v;

	pos = pFrom->GetHeadPosition();

	for (i = pFrom->GetCount(); i > 0; i--)
	{
		pcPrim = pFrom->GetNext(pos);

		((CA3dPolygon *) pFromFrame)->FetchPrimitive(&primFetched, pcPrim);

		pCopy = new A3DPRIMITIVE;

		*pCopy = *pcPrim;

		for (v = 0; v < pCopy->cVertices; v++)
		{
			pvSrc = pcPrim->av[v];
			pvDst = pCopy->av[v];

			pvDst[0] = pvSrc[0] * pMatrix[0] + pvSrc[1] * pMatrix[4] +
					   pvSrc[2] * pMatrix[8] + pvSrc[3] * pMatrix[12];
			pvDst[1] = pvSrc[0] * pMatrix[1] + pvSrc[1] * pMatrix[5] +
					   pvSrc[2] * pMatrix[9] + pvSrc[3] * pMatrix[13];
			pvDst[2] = pvSrc[0] * pMatrix[2] + pvSrc[1] * pMatrix[6] +
					   pvSrc[2] * pMatrix[10] + pvSrc[3] * pMatrix[14];
			pvDst[3] = pvSrc[0] * pMatrix[3] + pvSrc[1] * pMatrix[7] +
					   pvSrc[2] * pMatrix[11] + pvSrc[3] * pMatrix[15];
		}

		pvSrc = pcPrim->vNormal;
		pvDst = pCopy->vNormal;

		pvDst[0] = pvSrc[0] * pMatrix[0] + pvSrc[1] * pMatrix[4] +
				   pvSrc[2] * pMatrix[8] + pvSrc[3] * pMatrix[12];
		pvDst[1] = pvSrc[0] * pMatrix[1] + pvSrc[1] * pMatrix[5] +
				   pvSrc[2] * pMatrix[9] + pvSrc[3] * pMatrix[13];
		pvDst[2] = pvSrc[0] * pMatrix[2] + pvSrc[1] * pMatrix[6] +
				   pvSrc[2] * pMatrix[10] + pvSrc[3] * pMatrix[14];
		pvDst[3] = pvSrc[0] * pMatrix[3] + pvSrc[1] * pMatrix[7] +
				   pvSrc[2] * pMatrix[11] + pvSrc[3] * pMatrix[15];

		m_PrimList.AddTail(pCopy);
	}
}

/* =============================================================
// CopyPrimitives()
//
// Copy primitives into owned storage, appending NULL on allocation failure.
// =============================================================*/

void
CA3dPolygon::CopyPrimitives(const CA3dPtrList<A3DPRIMITIVE> *pFrom)
{
const A3DPRIMITIVE     *pcPrim;
POSITION                pos;
int                     i;

	pos = pFrom->GetHeadPosition();

	for (i = pFrom->GetCount(); i > 0; i--)
	{
		pcPrim = pFrom->GetNext(pos);

		m_PrimList.AddTail(CopyPrimitiveBlock(pcPrim));
	}
}

/* =============================================================
// SetOwner()
//
// Store the enclosing owner on the transform.
// =============================================================*/

void
CA3dPolygon::SetOwner(void *pOwner)
{
	m_xform.m_pOwner = pOwner;
}

/* =============================================================
// CopyFrameFrom()
//
// Copy the matrix and parent, replacing the owner only when supplied.
// =============================================================*/

void
CA3dPolygon::CopyFrameFrom(void *pOwner, const CA3dPolygon *pFrom)
{
	CopyMemory(m_xform.m_mat, pFrom->m_xform.m_mat, sizeof(m_xform.m_mat));

	m_xform.m_pParent = pFrom->m_xform.m_pParent;

	if (pOwner)
		m_xform.m_pOwner = pOwner;
}

/* =============================================================
// FetchPrimitive()
//
// Copy a primitive and transform it through the frame's ancestors.
// =============================================================*/

void
CA3dPolygon::FetchPrimitive(A3DPRIMITIVE *pOut, const A3DPRIMITIVE *pcBlock) const
{
	Xform()->PrimitiveInto(pOut, pcBlock);
}

typedef int A3dPolygonSizeCheck[(sizeof(CA3dPolygon) == 0x70) ? 1 : -1];

/* =============================================================
// CopyPrimitiveBlock()
//
// Allocate an independent copy of a primitive.
//
// Returns: The owned copy; NULL on allocation failure.
// =============================================================*/

static A3DPRIMITIVE *
CopyPrimitiveBlock(const A3DPRIMITIVE *pcPrim)
{
A3DPRIMITIVE *pNew;

	pNew = new A3DPRIMITIVE;

	if (pNew)
		*pNew = *pcPrim;

	return (pNew);
}
