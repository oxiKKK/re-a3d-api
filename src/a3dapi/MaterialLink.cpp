/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * MaterialLink.cpp
 *
 * Implements the shared material binding used by geometry objects and
 * builders. CA3dLink retains a bound material, releases it at destruction
 * and supports copying a binding from another object.
 *
 * GetMaterial returns a new material copy rather than the stored pointer.
 * The methods preserve the original reference behavior, including the
 * rebinding leak. MaterialObject.cpp implements the material values
 * carried by these links.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "MaterialLink.h"
#include "MaterialObject.h"

/* =============================================================
// CA3dLink()
//
// Initialize an unbound material link.
// =============================================================*/

CA3dLink::CA3dLink(void)
{
	m_pMaterial = NULL;
}

/* =============================================================
// ~CA3dLink()
//
// Release the bound material reference.
// =============================================================*/

CA3dLink::~CA3dLink(void)
{
	if (m_pMaterial)
		((IUnknown *) m_pMaterial)->Release();
}

/* =============================================================
// SetMaterial()
//
// Bind and retain a material. Preserve the original reference leak on
// rebinding.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null material
// =============================================================*/

HRESULT
CA3dLink::SetMaterial(void *pMaterial)
{
	if (!pMaterial)
		return (E_INVALIDARG);

	m_pMaterial = pMaterial;

	((IUnknown *) pMaterial)->AddRef();

	return (S_OK);
}

/* =============================================================
// GetMaterial()
//
// Return a new copy of the bound material, or null when unbound. Preserve
// the original unchecked allocation before AddRef.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null output
// =============================================================*/

HRESULT
CA3dLink::GetMaterial(void **ppMaterial)
{
CA3dMaterial *pCopy;

	if (!ppMaterial)
		return (E_INVALIDARG);

	if (!m_pMaterial)
	{
		*ppMaterial = NULL;

		return (S_OK);
	}

	pCopy = new CA3dMaterial((const CA3dMaterial *) m_pMaterial);

	((IUnknown *) pCopy)->AddRef();

	*ppMaterial = pCopy;

	return (S_OK);
}

/* =============================================================
// CopyMaterialFrom()
//
// Copy a bound material reference; an unbound source leaves the link unchanged.
// =============================================================*/

void
CA3dLink::CopyMaterialFrom(const CA3dLink *pOther)
{
	if (!pOther->m_pMaterial)
		return;

	SetMaterial(pOther->m_pMaterial);
}
