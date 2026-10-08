/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * hrtfmgr.h
 *
 * Declares CHrtfMgr and the coefficient-bank records used by software
 * spatialization. A bank describes its sample rate, filter length,
 * angular grid and coefficient and delay storage.
 *
 * The manager selects a bank and blends nearby directions into the filter
 * and delay values needed by a voice. hrtfmgr.cpp implements selection
 * and interpolation; A2DBuffer and the software mixer consume the
 * results.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _HRTFMGR_H
#define _HRTFMGR_H

#include "A3dPrivate.h"

/* =============================================================
// Class: A3DCOEFFENTRY
//
// Description: HRTF coefficient grid and row strides.
//
// Size: 0x30
// =============================================================*/

typedef struct A3DCOEFFENTRY
{
	/* 0x00 */ DWORD dwSampleRate; /* Hz Aureal's  */
	/* 0x04 */ DWORD cSamples;     /* Coefficients per row. */
	/* 0x08 */ DWORD cStride;      /* Shorts between rows. */
	/* 0x0C */ DWORD dwKey2;       /* Secondary selection key. */
	/* 0x10 */ BYTE *pData;        /* Signed 16-bit coefficients. */
	/* 0x14 */ BYTE *pDelay;       /* Interaural delay; one short per row. */

	/* Unsigned secondary coefficients and stride in shorts; purpose unresolved.
	 *  */
	/* 0x18 */ BYTE *pSecondaryData;
	/* 0x1C */ DWORD cSecondaryStride;

	/* Azimuth wraps. Elevations outside the first/last rows use polar caps. */
	/* 0x20 */ int cAzimuth;
	/* 0x24 */ int cElevation;
	/* 0x28 */ int cFirstRow;
	/* 0x2C */ int cLastRow;
} A3DCOEFFENTRY;

/* (RE) Table count used by Select: dbg:0x10059E6A. */

#define A3D_COEFF_TABLE_COUNT 1

extern A3DCOEFFENTRY *g_apCoeffTable[A3D_COEFF_TABLE_COUNT];

/* Single-precision angular constants. */

#define A3D_TWO_PI     6.2831855f
#define A3D_PI         3.1415927f
#define A3D_INV_TWO_PI 0.15915494f

/* =============================================================
// Class: CHrtfMgr
//
// Description: HRTF bank selection and weighted row lookup.
//
// Size: 0x18
//
// (RE) Constructor: dbg:0x10059C80
// =============================================================*/

class CHrtfMgr
{
public:
	CHrtfMgr(void);
	virtual ~CHrtfMgr(void); /* Slot 0: scalar deleting destructor. */

	ULONG AddRef(void);
	ULONG Release(void);

	HRESULT Select(DWORD dwSampleRate, DWORD cSamples, DWORD dwKey2);

	HRESULT GetCoeffs(DWORD *pcIndex, A3DVAL *pfWeight, A3DVAL fUnused,
	                  DWORD *pdwOut);
	HRESULT GetCoeffs16(DWORD *pcIndex, A3DVAL *pfWeight,
	                    A3DVAL fUnused, short *pnOut);

	HRESULT GetDelay(DWORD *pcIndex, A3DVAL *pfWeight,
	                 short *pnOut);
	HRESULT GetSecondaryCoeffs(DWORD *pcIndex, A3DVAL *pfWeight,
	                           short *pnOut);

	HRESULT GetIDXsAndWts(A3DVAL fAzimuth, A3DVAL fElevation, int nEar,
	                      DWORD *pcIndex, A3DVAL *pfWeight);

	HRESULT MarkBankReady(DWORD);

public:
	/* 0x00 */ /* (RE) Vtable: dbg:0x101333BC; 1 slot. */
	/* 0x04 */ DWORD         m_fHaveEntry; /* Entry-ready flag. */
	/* 0x08 */ DWORD         m_fHaveRate;  /* Rate-ready flag set by Select. */
	/* 0x0C */ LONG          m_cRef;       /* Non-interlocked reference count. */
	/* 0x10 */ DWORD         m_cSamples;   /* Requested output sample count. */
	/* 0x14 */ A3DCOEFFENTRY *m_pEntry;     /* Borrowed static coefficient entry. */
};

HRESULT BlendCoeffs32(const A3DCOEFFENTRY *pcEntry, const DWORD *pcIndex,
                      const A3DVAL *pcWeight, A3DVAL fUnused, DWORD *pdwOut,
                      DWORD cWanted);
HRESULT BlendCoeffs16(const A3DCOEFFENTRY *pcEntry, const DWORD *pcIndex,
                      const A3DVAL *pcWeight, A3DVAL fUnused, short *pnOut,
                      DWORD cWanted);

#endif /* _HRTFMGR_H */
