/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * MaterialLink.h
 *
 * Declares CA3dLink, the common material-binding base used by builders
 * and placed geometry. It stores a retained material reference and
 * exposes binding, copying and material retrieval operations.
 *
 * Public retrieval creates a material copy; the internal raw accessor
 * allows geometry processing to inspect the bound object.
 * MaterialLink.cpp implements the reference handling, and
 * MaterialObject.h declares the material itself.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _MATERIALLINK_H
#define _MATERIALLINK_H

#include "A3dPrivate.h"

/* =============================================================
// Class: CA3dLink
//
// Description: Retained material binding shared by geometry objects.
//
// Size: 0x08
// =============================================================*/

class CA3dLink
{
public:
	CA3dLink(void);
	virtual ~CA3dLink(void);

	HRESULT	SetMaterial(void *pMaterial);
	HRESULT	GetMaterial(void **ppMaterial);

	void	CopyMaterialFrom(const CA3dLink *pOther);

	int		HasMaterial(void) const	{ return (m_pMaterial != NULL); }

	/* Borrowed material pointer; NULL when unbound. */
	LPA3DMATERIAL	GetMaterialRaw(void)
			{ return ((LPA3DMATERIAL) m_pMaterial); }

protected:
	/* 0x04 */ void *m_pMaterial; /* Retained material reference. */
};

#endif /* _MATERIALLINK_H */
