/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * refaudbin.h
 *
 * Declares the reflection-image records and CRefAudBin selection state
 * used during acoustic tracing. Images carry direction, gain, distance,
 * material and slot information so the most audible paths can be assigned
 * to a source's reflection slots.
 *
 * The file also declares CA3dChained, the shared object-chain base used
 * for cleanup, and the driver-check interface. refaudbin.cpp implements
 * selection, chain lifetime and vendor checks.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _REFAUDBIN_H
#define _REFAUDBIN_H

#include "A3dPrivate.h"

class CA3dRoot;
class CA3dSource;

/* Audibility bin count  */
#define A3D_REF_AUD_BIN_COUNT 10

/* Sentinel  */
#define A3D_REF_DELAY_FROM_DISTANCE     (-1.0f)
#define A3D_REF_SLOT_UNASSIGNED         (-1)

/* Reflection image; size: 0x38. */

typedef struct _A3DREFIMAGE
{
	/* 0x00 */ DWORD	dwId;
	/* 0x04 */ A3DVAL	fAlpha;			/* Output() sends 1-x, capped at 0.99 */
	/* 0x08 */ A3DVAL	fGain;
	/* 0x0C */ A3DVAL	fDistance;
	/* 0x10 */ A3DVAL	fAudibility;		/* 0..1; picks the bin */
	/* 0x14 */ A3DVAL	avPosition[3];		/* listener-relative */
	/* 0x20 */ A3DVAL	fHit;			/* stored, never read */
	/* 0x24 */ DWORD	adwMaterialPrefix[2];	/* First two walk-material words */
	/* 0x2C */ INT		nSlot;			/* hardware slot; -1 until assigned */
	/* 0x30 */ BOOL		bDontTrack;		/* Disable reflection tracking. */
	/* 0x34 */ A3DVAL	fDelay;			/* -1 = compute from distance */
} A3DREFIMAGE;

typedef int A3DREFIMAGESizeCheck[(sizeof(A3DREFIMAGE) == 0x38) ? 1 : -1];

/* =============================================================
// Class: CA3dChained
//
// Description: Base for objects deleted through the global lifetime chain.
//
// Size: 0x0C
// =============================================================*/

/* (RE) Vtable dbg:0x10127db8, slot 0: dbg:0x10003353 (invalid instruction
   boundary); derived vtables replace this unreachable original entry. */

class CA3dChained
{
	friend void	FreeChain(void);

public:
	/* (RE) dbg:0x100174e0; thunk dbg:0x10003814 */
	CA3dChained(void)		{ Link(); }

	/* (RE) dbg:0x10017560 */
	virtual ~CA3dChained(void)	{ Unlink(); }

	void	Link(void);
	void	Unlink(void);

protected:
	/* 0x00 */ /* vptr, from the virtual destructor */
	/* 0x04 */ CA3dChained	*m_pPrev;	/* toward the chain head */
	/* 0x08 */ CA3dChained	*m_pNext;
};

typedef int CA3dChainedSizeCheck[(sizeof(CA3dChained) == 0x0C) ? 1 : -1];

/* =============================================================
// Class: CRefAudBin
//
// Description: Selects audible reflection images for 16 hardware slots.
//
// Size: 0x2348
// =============================================================*/

/* (RE) Vtable rtl:0x100534fc; dbg:0x1012d958.
   Slot 0: scalar deleting destructor rtl:0x1001a110; dbg:0x1003e580. */

class CRefAudBin
{
public:
	CRefAudBin(void);
	virtual ~CRefAudBin(void);

	void	Reset(void);

	void	SetWindow(CA3dSource *pSource);
	void	SaveWindow(CA3dSource *pSource);

	INT	Add(DWORD dwId, A3DVAL fDistance, A3DVAL fAudibility,
		    A3DVAL fAlpha, A3DVAL fGain, const A3DVAL *avPosition,
		    A3DVAL fHit, const DWORD *adwMaterialPrefix, A3DVAL fDelay);

	void	Update(CA3dRoot *pRoot, A3DMATRIX pmListener,
		       CA3dSource *pSource);
	void	Collect(CA3dSource *pSource, A3DREFIMAGE **apCollected,
			A3DREFIMAGE **apSlot);
	void	AssignSlots(A3DREFIMAGE **apCollected, A3DREFIMAGE **apSlot);
	void	Output(CA3dRoot *pRoot, A3DMATRIX pmListener,
		       CA3dSource *pSource, A3DREFIMAGE **apSlot);

protected:
	/* 0x0000 */ /* vptr, from the virtual destructor */
	/* 0x0004 */ A3DREFIMAGE	m_RefImage[A3D_REF_AUD_BIN_COUNT][A3D_MAX_SOURCE_REFLECTIONS];

	/* 0x2304 */ INT	m_RefImageFilled[A3D_REF_AUD_BIN_COUNT]; /* Image count per bin. */

	/* 0x232C */ INT	m_cImages;	/* total added since Reset() */
	/* 0x2330 */ INT	m_cCollected; /* Images selected for output. */

	/* 0x2334 */ A3DVAL	m_fBinScale; /* 10 / window width; 10 for nonpositive width. */
	/* 0x2338 */ A3DVAL	m_fWindowMin;
	/* 0x233C */ A3DVAL	m_fWindowMax;

	/* Audibility range seen since SetWindow(); seeded 1.0 and 0.0. */

	/* 0x2340 */ A3DVAL	m_fSeenMin;
	/* 0x2344 */ A3DVAL	m_fSeenMax;
};

typedef int CRefAudBinSizeCheck[(sizeof(CRefAudBin) == 0x2348) ? 1 : -1];

#endif /* _REFAUDBIN_H */
