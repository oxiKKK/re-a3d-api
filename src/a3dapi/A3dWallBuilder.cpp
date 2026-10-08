/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dWallBuilder.cpp
 *
 * Implements editable wall geometry for the private scene API. It accepts
 * polygon primitives, binds a material and retains openings placed in the
 * wall's plane.
 *
 * Validation checks the polygon input and opening placement before the
 * wall is copied into a room or scene. Shared primitive construction
 * comes from CA3dPolygonBuilder, and the placed result is CA3dWall.
 * Persistence and duplication entry points are present but unsupported.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "A3dWallBuilder.h"
#include "A3dFrame.h"
#include "Corners.h"
#include "LinkList.h"
#include "A3dOpening.h"
#include "A3dOpeningBuilder.h"

#include <math.h>


#define A3D_OPENING_ORIGIN_DISTANCE_SQUARED_TOLERANCE 1.0e-12f
#define A3D_OPENING_PLANE_DOT_TOLERANCE 1.0e-12

/* =============================================================
// CA3dWallBuilder()
// (RE) rtl:0x10034070; dbg:0x10084070
//
// Initialize a wall builder and its default name.
// =============================================================*/

CA3dWallBuilder::CA3dWallBuilder(int nType) : CA3dPolygonBuilder(nType)
{

	m_cRef = 0;

	m_named.SetName("WallBuilder", ++g_cWallBuilders);
}

/* =============================================================
// SetName()
// (RE) rtl:0x10034150; dbg:0x10084250
//
// Copy the supplied fixed-size name buffer.
//
// Returns: The SetNameBuffer result.
// =============================================================*/

STDMETHODIMP
CA3dWallBuilder::SetName(LPCVOID pvName)
{
	return (m_named.SetNameBuffer(pvName));
}

/* =============================================================
// GetName()
// (RE) rtl:0x10034170; dbg:0x10084280
//
// Copy the stored fixed-size name buffer.
//
// Returns: The GetNameBuffer result.
// =============================================================*/

STDMETHODIMP
CA3dWallBuilder::GetName(LPVOID pvName, int nSize)
{
	return (m_named.GetNameBuffer(pvName, nSize));
}

/* =============================================================
// Begin()
// (RE) rtl:0x1002ecf0; dbg:0x10084590
//
// Begin a polygon primitive.
//
// Returns: The polygon builder Begin result.
// =============================================================*/

STDMETHODIMP
CA3dWallBuilder::Begin(DWORD dwMode)
{
	return (CA3dPolygonBuilder::Begin(dwMode));
}

/* =============================================================
// Vertex3f()
// (RE) rtl:0x1002ed50; dbg:0x100846d0
//
// Append a vertex from three scalar components.
//
// Returns: The polygon builder Vertex3f result.
// =============================================================*/

STDMETHODIMP
CA3dWallBuilder::Vertex3f(A3DVAL x, A3DVAL y, A3DVAL z)
{
	return (CA3dPolygonBuilder::Vertex3f(x, y, z));
}

/* =============================================================
// Vertex3fv()
// (RE) rtl:0x1002ed70; dbg:0x10084710
//
// Append a vertex from a vector.
//
// Returns: The polygon builder Vertex3fv result.
// =============================================================*/

STDMETHODIMP
CA3dWallBuilder::Vertex3fv(LPA3DVAL pv)
{
	return (CA3dPolygonBuilder::Vertex3fv(pv));
}

/* =============================================================
// End()
// (RE) rtl:0x1002ed10; dbg:0x100845c0
//
// Finish the current polygon primitive.
//
// Returns: The polygon builder End result.
// =============================================================*/

STDMETHODIMP
CA3dWallBuilder::End(void)
{
	return (CA3dPolygonBuilder::End());
}

/* =============================================================
// SetMaterial()
// (RE) rtl:0x10034830; dbg:0x10084d70
//
// Bind the supplied material through the material link.
//
// Returns: The link SetMaterial result.
// =============================================================*/

STDMETHODIMP
CA3dWallBuilder::SetMaterial(void *pMaterial)
{
	return (m_link.SetMaterial(pMaterial));
}

/* =============================================================
// GetMaterial()
// (RE) rtl:0x10034850; dbg:0x10084da0
//
// Copy the bound material through the material link.
//
// Returns: The link GetMaterial result.
// =============================================================*/

STDMETHODIMP
CA3dWallBuilder::GetMaterial(void **ppMaterial)
{
	return (m_link.GetMaterial(ppMaterial));
}

/* =============================================================
// RemovePrimitive()
// (RE) rtl:0x1002ed90; dbg:0x10084740
//
// Remove the selected polygon primitive.
//
// Returns: The polygon builder RemovePrimitive result.
// =============================================================*/

STDMETHODIMP
CA3dWallBuilder::RemovePrimitive(int nPrim)
{
	return (CA3dPolygonBuilder::RemovePrimitive(nPrim));
}

/* =============================================================
// GetPrimitiveCount()
// (RE) rtl:0x1002edb0; dbg:0x10084770
//
// Read the number of stored polygon primitives.
//
// Returns: The polygon builder GetPrimitiveCount result.
// =============================================================*/

STDMETHODIMP_(int)
CA3dWallBuilder::GetPrimitiveCount(void)
{
	return (CA3dPolygonBuilder::GetPrimitiveCount());
}

/* =============================================================
// SetVertex()
// (RE) rtl:0x100343d0; dbg:0x100847a0
//
// Replace a vertex in the selected primitive.
//
// Returns: The polygon builder SetVertex result.
// =============================================================*/

STDMETHODIMP
CA3dWallBuilder::SetVertex(int nPrim, int nVertex, A3DVAL x, A3DVAL y, A3DVAL z)
{
	return (CA3dPolygonBuilder::SetVertex(nPrim, nVertex, x, y, z));
}

/* =============================================================
// GetVertex()
// (RE) rtl:0x1002edd0; dbg:0x100847e0
//
// Read a vertex from the selected primitive.
//
// Returns: The polygon builder GetVertex result.
// =============================================================*/

STDMETHODIMP
CA3dWallBuilder::GetVertex(int nPrim, int nVertex, LPA3DVAL pv)
{
	return (CA3dPolygonBuilder::GetVertex(nPrim, nVertex, pv));
}

/* =============================================================
// CA3dWallBuilder::~CA3dWallBuilder() scalar deleting destructor
// (RE) rtl:0x10034190; dbg:0x100842c0
// =============================================================*/

/* =============================================================
// ~CA3dWallBuilder()
// (RE) rtl:0x100341c0; dbg:0x10084340
//
// Release stored openings and builder state.
// =============================================================*/

CA3dWallBuilder::~CA3dWallBuilder(void)
{
	ClearOpenings();

}

/* =============================================================
// ClearOpenings()
// (RE) dbg:0x10084620
//
// Release stored openings and their nodes. Preserve the null Release call
// when the count exceeds the list length.
// =============================================================*/

void
CA3dWallBuilder::ClearOpenings(void)
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
// Clear()
// (RE) rtl:0x10034350; dbg:0x100845f0
//
// Release openings and clear polygon primitive and edge state.
//
// Returns: The polygon builder Clear result.
// =============================================================*/

STDMETHODIMP
CA3dWallBuilder::Clear(void)
{
	ClearOpenings();

	return (CA3dPolygonBuilder::Clear());
}

/* =============================================================
// Load()
// (RE) rtl:0x10009810; dbg:0x100841b0
//
// Reject wall-builder loading.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dWallBuilder::Load(void *pv)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// Save()
// (RE) rtl:0x10009810; dbg:0x100841d0
//
// Reject wall-builder saving.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dWallBuilder::Save(void *pv)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// UnSerialize()
// (RE) rtl:0x10009820; dbg:0x100841f0
//
// Reject wall-builder deserialization.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dWallBuilder::UnSerialize(void *pv, UINT cb)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// Serialize()
// (RE) rtl:0x10009820; dbg:0x10084210
//
// Reject wall-builder serialization.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dWallBuilder::Serialize(void *pv, UINT cb)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// Duplicate()
// (RE) rtl:0x10009810; dbg:0x10084230
//
// Reject wall-builder duplication.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dWallBuilder::Duplicate(void *pv)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// Validate()
// (RE) rtl:0x10034400; dbg:0x10084820
//
// Validate polygon primitives and require every opening to be coplanar.
//
// Returns: The CheckPrimitives failure; A3DERROR_INVALID_ARGUMENT for an
//          opening outside the plane; S_OK otherwise.
// =============================================================*/

STDMETHODIMP
CA3dWallBuilder::Validate(void)
{
CA3dOpening    *pOpening;
POSITION        pos;
HRESULT         hr;
int             i;

	hr = CheckPrimitives();

	if (FAILED(hr))
		return (hr);

	pos = m_OpeningList.GetHeadPosition();

	for (i = m_OpeningList.GetCount(); i > 0; i--)
	{
		pOpening = m_OpeningList.GetNext(pos);

		if (!IsOpeningInPlane(pOpening))
			return (A3DERROR_INVALID_ARGUMENT);
	}

	return (S_OK);
}

/* =============================================================
// AddOpening()
// (RE) rtl:0x10034450; dbg:0x10084920
//
// Build and retain an opening at the supplied or default placement.
// Preserve unchecked builder and allocation pointers.
//
// Returns: The new opening count; A3DERROR_OPENING_NOT_VALID or
//          A3DERROR_DIR_AND_UP_VECTORS_NOT_PERPENDICULAR on validation failure.
// =============================================================*/

STDMETHODIMP_(int)
CA3dWallBuilder::AddOpening(void *pOpening, LPA3DVAL pvPosition,
							LPA3DVAL pvFront, LPA3DVAL pvUp)
{
const A3DVAL   *pvAt;
const A3DVAL   *pvDir;
const A3DVAL   *pvTop;
CA3dOpening    *pInstance;

	if (FAILED(((IA3dOpeningBuilder *) pOpening)->Validate()))
		return (A3DERROR_OPENING_NOT_VALID);

	pvAt    = pvPosition ? pvPosition : vDefaultPosition;
	pvDir   = pvFront ? pvFront : vDefaultFront;
	pvTop   = pvUp ? pvUp : vDefaultUp;

	if (!A3dFrameIsSquare(pvDir, pvTop))
		return (A3DERROR_DIR_AND_UP_VECTORS_NOT_PERPENDICULAR);

	pInstance =
			new CA3dOpening((CA3dOpeningBuilder *) pOpening, (LPA3DVAL) pvAt,
							(LPA3DVAL) pvDir, (LPA3DVAL) pvTop);

	((IUnknown *) pInstance)->AddRef();

	m_OpeningList.AddTail(pInstance);

	return (m_OpeningList.GetCount());
}

/* =============================================================
// IsOpeningInPlane()
// (RE) rtl:0x10034660; dbg:0x10084b20
//
// Test transformed opening vertices against the wall plane. Preserve
// invalid plane assumptions after 2D validation and unchecked list elements.
//
// Returns:
//   TRUE   when all vertices lie in the plane
//   FALSE  otherwise
// =============================================================*/

BOOL
CA3dWallBuilder::IsOpeningInPlane(CA3dOpening *pOpening) const
{
const A3DVAL   *pvNormal;
const A3DVAL   *pvOrigin;
POSITION        pos;
int             i;

	pvNormal = m_pvPlaneNormal;
	pvOrigin = m_pFirstBlock->av[0];

	pos = pOpening->m_PrimList.GetHeadPosition();

	for (i = pOpening->m_PrimList.GetCount(); i > 0; i--)
	{
		A3DPRIMITIVE            prim;
		const A3DPRIMITIVE     *pPrim;
		int                     cPoints;
		int                     k;

		pPrim = pOpening->m_PrimList.GetNext(pos);

		cPoints = pPrim->cVertices;

		pOpening->FetchPrimitive(&prim, pPrim);

		for (k = cPoints - 1; k >= 0; k--)
		{
			const A3DVAL *pv;
			A3DVAL dx, dy, dz;

			pv = prim.av[k];

			dx = pvOrigin[0] - pv[0];
			dy = pvOrigin[1] - pv[1];
			dz = pvOrigin[2] - pv[2];

			if (dx * dx + dy * dy + dz * dz <
				A3D_OPENING_ORIGIN_DISTANCE_SQUARED_TOLERANCE)
				continue;

			if (fabs((double) (pvNormal[0] * dx + pvNormal[1] * dy +
							   pvNormal[2] * dz)) < A3D_OPENING_PLANE_DOT_TOLERANCE)
				continue;

			return (FALSE);
		}
	}

	return (TRUE);
}

/* =============================================================
// RemoveOpening()
// (RE) rtl:0x10034740; dbg:0x10084c30
//
// Unlink and release a one-based opening.
//
// Returns:
//   S_OK
//   A3DERROR_INVALID_OPENING_INDEX  a missing opening
// =============================================================*/

STDMETHODIMP
CA3dWallBuilder::RemoveOpening(int nOpening)
{
IUnknown       *pOpening;
POSITION        pos;

	pos = nOpening > 0 ? m_OpeningList.FindIndex(nOpening) : NULL;

	pOpening = pos ? (IUnknown *) m_OpeningList.GetAt(pos) : NULL;

	if (!pOpening)
		return (A3DERROR_INVALID_OPENING_INDEX);

	m_OpeningList.RemoveAt(pos);

	pOpening->Release();

	return (S_OK);
}

/* =============================================================
// GetOpening()
// (RE) rtl:0x100347d0; dbg:0x10084cd0
//
// Return a referenced opening by one-based index.
//
// Returns:
//   S_OK
//   A3DERROR_INVALID_ARGUMENT       a null output
//   A3DERROR_INVALID_OPENING_INDEX  a missing opening
// =============================================================*/

STDMETHODIMP
CA3dWallBuilder::GetOpening(int nOpening, void **ppOpening)
{
IUnknown       *pOpening;
POSITION        pos;

	if (!ppOpening)
		return (A3DERROR_INVALID_ARGUMENT);

	pos = nOpening > 0 ? m_OpeningList.FindIndex(nOpening) : NULL;

	pOpening = pos ? (IUnknown *) m_OpeningList.GetAt(pos) : NULL;

	if (!pOpening)
		return (A3DERROR_INVALID_OPENING_INDEX);

	pOpening->AddRef();

	*ppOpening = pOpening;

	return (S_OK);
}

typedef int A3dWallBuilderSizeCheck[(sizeof(CA3dWallBuilder) == 0x1B0) ? 1 : -1];
