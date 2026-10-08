/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dOpening.h
 *
 * Declares CA3dOpening, a placed opening built from polygon geometry and
 * associated with a wall. It combines the geometry interface base,
 * primitive storage, placement and material link needed by the private
 * scene model.
 *
 * Opening-factor state can be shared by copies on adjoining walls.
 * A3dOpening.cpp implements construction and primitive emission, while
 * A3dWall.cpp manages the containing walls and their openings.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _A3DOPENING_H
#define _A3DOPENING_H

#include "A3dPrivate.h"
#include "A3dGeom.h"
#include "A3dFrame.h"
#include "Polygon.h"
#include "MaterialLink.h"
#include "LinkList.h"

class CA3dOpeningBuilder;
class CA3dRoot;
class CA3dWall;

/* =============================================================
// Class: CA3dOpening
//
// Description: Opening or portal with framed geometry and a shared factor.
//
// Size: 0x88
//
// (RE) Constructor: dbg:0x10077b90
// =============================================================*/

/* CA3dGeomIface at 0x00; CA3dPolygon (0x70 bytes) at 0x04.
 * Private interface: dbg:0x10139220; rtl:0x10054004.
 * Slots: 0 QueryInterface, 1 AddRef, 2 Release, 3 GetWall,
 * 4 SetOpeningFactor, 5 GetOpeningFactor, 6 SetPosition, 7 GetPosition.
 * Polygon table at 0x04: dbg:0x1013921c; rtl:0x10054000;
 * slot 0 deleting destructor. */

class CA3dOpening : public CA3dGeomIface, public CA3dPolygon
{
public:
	CA3dOpening(CA3dOpeningBuilder *pBuilder, LPA3DVAL pvPosition,
		    LPA3DVAL pvFront, LPA3DVAL pvUp);
	CA3dOpening(const CA3dOpening *pFrom, CA3dWall *pWall);

	STDMETHODIMP		QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG)	AddRef(void);
	STDMETHODIMP_(ULONG)	Release(void);

	HRESULT	GetWall(void **ppWall);
	HRESULT	SetPosition(const CA3dFrame *pcRelativeTo, const A3DVAL *pcv);

	HRESULT	SetOpeningFactor(A3DVAL fFactor);
	HRESULT	GetOpeningFactor(A3DVAL *pfFactor);

	void	GetPlane(A3DVAL *pvPlane);

	DWORD	GetOriginal(void) const	{ return (m_fOriginal); }

	CA3dOpening(const CA3dOpening *pFrom, CA3dWall *pWall,
		    CA3dWall *pThrough);

	virtual ~CA3dOpening(void);

protected:

	LONG		m_cRef;			/* 0x74 */
	CA3dWall	*m_pWall;		/* 0x78, borrowed containing wall */

	/* Nonzero permits portal creation; zero prevents recursion. */
	DWORD		m_fOriginal;		/* 0x7c */
	CA3dOpening	*m_pSource;		/* 0x80, borrowed portal source */

	friend void A3dEmitMarked(CA3dOpening *pOwner, class CA3dRoot *pApi);

	A3DVAL		m_fOpeningFactor;	/* 0x84; portals use m_pSource's factor. */
};

void	A3dEmitMarked(class CA3dOpening *pOwner,
		      class CA3dRoot *pApi);

#endif /* _A3DOPENING_H */
