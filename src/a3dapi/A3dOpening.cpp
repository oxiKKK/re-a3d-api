/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dOpening.cpp
 *
 * Implements openings placed in walls for the private room-based scene
 * model. Opening objects copy builder primitives, retain their placement
 * and expose an opening factor used when tracing sound through a surface.
 *
 * The file supports copies associated with adjoining walls and emits
 * marked opening primitives into the root geometry stream. Wall
 * construction and portal handling are coordinated by A3dWall.cpp; the
 * editable input geometry comes from A3dOpeningBuilder.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "A3dOpening.h"
#include "A3d3.h"
#include "A3dFrame.h"
#include "A3dMatrix.h"
#include "A3dWall.h"
#include "A3dOpeningBuilder.h"

/* =============================================================
// CA3dOpening()
// (RE) dbg:0x10077b90
//
// Build an unattached opening from builder geometry and placement.
// =============================================================*/

CA3dOpening::CA3dOpening(CA3dOpeningBuilder *pBuilder, LPA3DVAL pvPosition,
						 LPA3DVAL pvFront, LPA3DVAL pvUp)
{
const A3DVAL *pvPlane;

	m_cRef = 0;

	SetFrame(this, pvPosition, pvFront, pvUp);

	m_pWall = NULL;

	CopyPrimitives(pBuilder->GetPrimitiveList());

	/* Original defect: a successful 2D builder still returns a null plane. */

	pvPlane = pBuilder->GetPlane();

	m_vPlane[0] = pvPlane[0];
	m_vPlane[1] = pvPlane[1];
	m_vPlane[2] = pvPlane[2];
	m_vPlane[3] = pvPlane[3];

	m_fOriginal             = 1;
	m_pSource               = NULL;
	m_fOpeningFactor        = 0.0f;
}

/* =============================================================
// CA3dOpening()
// (RE) rtl:0x1002e4a0; dbg:0x10077d40
//
// Copy an opening and its geometry into a wall.
// =============================================================*/

CA3dOpening::CA3dOpening(const CA3dOpening *pFrom, CA3dWall *pWall)
{
	m_cRef = 0;

	CopyFrameFrom(this, pFrom);

	m_pWall = pWall;

	m_xform.m_pParent = ((CA3dPolygon *) pWall)->Xform();

	CopyPrimitives(&pFrom->m_PrimList);

	m_vPlane[0] = pFrom->m_vPlane[0];
	m_vPlane[1] = pFrom->m_vPlane[1];
	m_vPlane[2] = pFrom->m_vPlane[2];
	m_vPlane[3] = pFrom->m_vPlane[3];

	m_fOpeningFactor        = 0.0f;
	m_pSource               = NULL;
	m_fOriginal             = 1;
}

/* =============================================================
// GetPlane()
//
// Transform the stored plane into world space. Preserve the builder's
// uninitialized w component, which may apply translation. The standalone
// 677 entry is unresolved; the portal constructor calls an equivalent
// operation through thunk dbg:0x10002ecd.
// =============================================================*/

void
CA3dOpening::GetPlane(A3DVAL *pvPlane)
{
	pvPlane[0] = m_vPlane[0];
	pvPlane[1] = m_vPlane[1];
	pvPlane[2] = m_vPlane[2];
	pvPlane[3] = m_vPlane[3];

	Xform()->TransformPoint(pvPlane);
}

/* =============================================================
// CA3dOpening()
// (RE) dbg:0x10077eb0
//
// Build a portal into a destination wall. Preserve omission of the source
// transform when copying geometry.
// =============================================================*/

CA3dOpening::CA3dOpening(const CA3dOpening *pFrom, CA3dWall *pWall,
						 CA3dWall *pThrough)
{
	A3DVAL avRow[4][4];
	A3DVAL matFrame[16];
	A3DVAL matInverse[16];
	A3DVAL vPlane[4];
	int i, n;

	m_cRef = 0;

	for (i = 0; i < 4; i++)
	{
		for (n = 0; n < 4; n++)
			avRow[i][n] = (i == n) ? 1.0f : 0.0f;

		((CA3dPolygon *) pWall)->Xform()->TransformPoint(avRow[i]);
	}

	for (i = 0; i < 4; i++)
	{
		matFrame[4 * i + 0] = avRow[i][0];
		matFrame[4 * i + 1] = avRow[i][1];
		matFrame[4 * i + 2] = avRow[i][2];
		matFrame[4 * i + 3] = avRow[i][3];
	}

	A3dMatrixInvert(matFrame, matInverse);

	CopyPrimitivesThrough(&pFrom->m_PrimList, pFrom, matInverse);

	SetOwner(this);

	m_pWall                 = pWall;
	m_xform.m_pParent       = ((CA3dPolygon *) pWall)->Xform();

	((CA3dOpening *) pFrom)->GetPlane(vPlane);

	m_vPlane[0] = vPlane[0] * matInverse[0] + vPlane[1] * matInverse[4] +
				  vPlane[2] * matInverse[8] + vPlane[3] * matInverse[12];
	m_vPlane[1] = vPlane[0] * matInverse[1] + vPlane[1] * matInverse[5] +
				  vPlane[2] * matInverse[9] + vPlane[3] * matInverse[13];
	m_vPlane[2] = vPlane[0] * matInverse[2] + vPlane[1] * matInverse[6] +
				  vPlane[2] * matInverse[10] + vPlane[3] * matInverse[14];
	m_vPlane[3] = vPlane[0] * matInverse[3] + vPlane[1] * matInverse[7] +
				  vPlane[2] * matInverse[11] + vPlane[3] * matInverse[15];

	m_fOriginal = 0;

	m_fOpeningFactor        = 0.0f;
	m_pSource               = (CA3dOpening *) pFrom;
}

/* =============================================================
// CA3dOpening::~CA3dOpening() scalar deleting destructor
// (RE) rtl:0x1002e470; dbg:0x10077cc0
// =============================================================*/

/* =============================================================
// ~CA3dOpening()
//
// Destroy the opening and its polygon base.
// =============================================================*/

CA3dOpening::~CA3dOpening(void)
{
}

/* =============================================================
// QueryInterface()
// (RE) rtl:0x1002e800; dbg:0x10078370
//
// Query IUnknown or the private opening interface. Preserve use of the
// caller's prior output for unsupported IIDs, including its possible AddRef.
//
// Returns:
//   S_OK
//   E_INVALIDARG   a null output
//   E_NOINTERFACE  if *ppv is null
// =============================================================*/

STDMETHODIMP
CA3dOpening::QueryInterface(REFIID riid, void **ppv)
{
	if (!ppv)
		return (E_INVALIDARG);

	if (!memcmp(&riid, &IID_IUnknown, sizeof(IID)) ||
		!memcmp(&riid, &IID_IA3dOpening, sizeof(IID)))
		*ppv = this;

	if (!*ppv)
		return (E_NOINTERFACE);

	((IUnknown *) *ppv)->AddRef();

	return (S_OK);
}

/* =============================================================
// AddRef()
// (RE) rtl:0x1002e860; dbg:0x10078410
//
// Increment the reference count.
//
// Returns: The updated reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dOpening::AddRef(void)
{
	return (++m_cRef);
}

/* =============================================================
// Release()
// (RE) rtl:0x1002e880; dbg:0x10078440
//
// Release a reference and delete the opening at zero.
//
// Returns: The remaining reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dOpening::Release(void)
{
	if (--m_cRef)
		return (m_cRef);

	delete this;

	return (0);
}

/* =============================================================
// SetPosition()
// (RE) rtl:0x1002e8d0; dbg:0x100784f0
//
// Set the opening position without marking its wall or scene stale.
//
// Returns: The frame SetPosition result.
// =============================================================*/

HRESULT
CA3dOpening::SetPosition(const CA3dFrame *pcRelativeTo, const A3DVAL *pcv)
{
	return (Xform()->SetPosition(pcRelativeTo, pcv));
}

/* =============================================================
// GetWall()
// (RE) rtl:0x1002e8f0; dbg:0x10078530
//
// Return the containing wall with a new reference, or NULL if unattached.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null output
// =============================================================*/

HRESULT
CA3dOpening::GetWall(void **ppWall)
{
	if (!ppWall)
		return (E_INVALIDARG);

	*ppWall = NULL;

	if (m_pWall)
	{
		*ppWall = m_pWall;

		((IUnknown *) m_pWall)->AddRef();
	}

	return (S_OK);
}

/* =============================================================
// SetOpeningFactor()
// (RE) rtl:0x1002e930; dbg:0x100785a0
//
// Set the original opening's factor, following the portal source if present.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
CA3dOpening::SetOpeningFactor(A3DVAL fFactor)
{
	if (m_pSource)
		m_pSource->SetOpeningFactor(fFactor);
	else
		m_fOpeningFactor = fFactor;

	return (S_OK);
}

/* =============================================================
// GetOpeningFactor()
// (RE) rtl:0x1002e960; dbg:0x10078600
//
// Read the original opening's factor, following the portal source if present.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null output
// =============================================================*/

HRESULT
CA3dOpening::GetOpeningFactor(A3DVAL *pfFactor)
{
	if (!pfFactor)
		return (E_INVALIDARG);

	if (m_pSource)
		m_pSource->GetOpeningFactor(pfFactor);
	else
		*pfFactor = m_fOpeningFactor;

	return (S_OK);
}

/* =============================================================
// A3dEmitMarked()
//
// Emit transformed opening primitives and their opening factor. Preserve the
// null-list dereference and unmatched End for unsupported vertex counts.
// =============================================================*/

void
A3dEmitMarked(CA3dOpening *pOwner, CA3dRoot *pApi)
{
IA3dGeom2 *pGeo = pApi;
LPA3DVAL        pvOpeningFactor;
POSITION        pos;
int             i;

	if (pOwner->m_pSource)
		pvOpeningFactor = &pOwner->m_pSource->m_fOpeningFactor;
	else
		pvOpeningFactor = &pOwner->m_fOpeningFactor;

	pos = pOwner->m_PrimList.GetHeadPosition();

	for (i = pOwner->m_PrimList.GetCount(); i > 0; i--)
	{
		const A3DPRIMITIVE     *pcPrim;
		A3DPRIMITIVE            prim;

		pcPrim = pOwner->m_PrimList.GetNext(pos);

		pOwner->FetchPrimitive(&prim, pcPrim);

		if (pcPrim->cVertices == 3)
		{
			pGeo->Begin(A3D_SUB_TRIANGLES);
			pGeo->Normal3fv(prim.vNormal);
			pGeo->Vertex3fv(prim.av[0]);
			pGeo->Vertex3fv(prim.av[1]);
			pGeo->Vertex3fv(prim.av[2]);
			pGeo->SetOpeningFactorfv(pvOpeningFactor);
		}
		else if (pcPrim->cVertices == 4)
		{
			pGeo->Begin(A3D_SUB_QUADS);
			pGeo->Normal3fv(prim.vNormal);
			pGeo->Vertex3fv(prim.av[0]);
			pGeo->Vertex3fv(prim.av[1]);
			pGeo->Vertex3fv(prim.av[2]);
			pGeo->Vertex3fv(prim.av[3]);
			pGeo->SetOpeningFactorfv(pvOpeningFactor);
		}

		pGeo->End();
	}
}

typedef int A3dOpeningSizeCheck[(sizeof(CA3dOpening) == 0x88) ? 1 : -1];
