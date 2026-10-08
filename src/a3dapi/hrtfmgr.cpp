/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * hrtfmgr.cpp
 *
 * Implements selection and interpolation of head-related transfer
 * function (HRTF) data for software voices. CHrtfMgr selects a
 * coefficient bank by sample rate and filter requirements, then maps a
 * direction to nearby rows and interpolation weights.
 *
 * The blending routines produce filter coefficients and interaural delays
 * for the two ears. Coefficient storage is static; mixer and voice
 * objects use the manager to prepare their per-voice processing state.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "hrtfmgr.h"
#include "A3dMatrix.h"

/* Rows beyond the regular grid per ear
 * Two are polar caps; the third's purpose is unknown. */
#define A3D_HRTF_EXTRA_ROWS_PER_EAR 3

/* =============================================================
// CHrtfMgr()
// (RE) dbg:0x10059C80
//
// Initialize the bank flags and reference count.
// =============================================================*/

CHrtfMgr::CHrtfMgr(void)
{

	m_fHaveEntry = 0;
	m_fHaveRate  = 0;

	m_pEntry = NULL;

	m_cRef = 1;
}

/* =============================================================
// ~CHrtfMgr()
// (RE) dbg:0x10059D30
//
// Destroy the manager; coefficient storage is static.
// =============================================================*/

CHrtfMgr::~CHrtfMgr(void)
{
}

/* =============================================================
// AddRef()
// (RE) dbg:0x10059D60
//
// Increment the reference count.
//
// Returns: The incremented count.
// =============================================================*/

ULONG
CHrtfMgr::AddRef(void)
{
	m_cRef = m_cRef + 1;

	return ((ULONG) m_cRef);
}

/* =============================================================
// Release()
// (RE) dbg:0x10059DA0
//
// Release a reference and delete at zero, ignoring an already zero count.
//
// Returns: The remaining count.
// =============================================================*/

ULONG
CHrtfMgr::Release(void)
{
	if (m_cRef == 0)
		return (0);

	m_cRef = m_cRef - 1;

	if (m_cRef != 0)
		return ((ULONG) m_cRef);

	delete this;

	return (0);
}

/* =============================================================
// Select()
// (RE) dbg:0x10059E30
//
// Select an exact-length bank or the longest shorter bank matching
// both keys.
//
// Returns:
//   S_OK
//   E_FAIL  if no bank matches without exceeding the length
// =============================================================*/

HRESULT
CHrtfMgr::Select(DWORD dwSampleRate, DWORD cSamples, DWORD dwKey2)
{
A3DCOEFFENTRY  *pEntry;
A3DCOEFFENTRY  *pShorter;
int             fShorter;
int             fExact;
int             i;

	pEntry   = NULL;
	pShorter = NULL;

	fExact   = 0;
	fShorter = 0;

	for (i = 0; i < A3D_COEFF_TABLE_COUNT; i++)
	{
		pEntry = g_apCoeffTable[i];

		if (dwSampleRate == pEntry->dwSampleRate && dwKey2 == pEntry->dwKey2)
		{
			if (cSamples == pEntry->cSamples)
			{
				fExact = 1;

				break;
			}

			if (cSamples > pEntry->cSamples)
			{
				fShorter = 1;

				if (!pShorter || pEntry->cSamples > pShorter->cSamples)
					pShorter = g_apCoeffTable[i];
			}
		}
	}

	if (!fExact && !fShorter)
		return (E_FAIL);

	if (fExact)
		m_pEntry = pEntry;
	else
		m_pEntry = pShorter;

	m_cSamples   = cSamples;
	m_fHaveRate  = 1;
	m_fHaveEntry = 1;

	return (S_OK);
}

/* =============================================================
// MarkBankReady()
// (RE) dbg:0x10059F70
//
// Set the entry-ready flag, ignoring the argument.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
CHrtfMgr::MarkBankReady(DWORD)
{
	m_fHaveEntry = 1;

	return (S_OK);
}

/* =============================================================
// GetCoeffs()
// (RE) rtl:0x10022F50; dbg:0x10059FA0
//
// Blend four coefficient rows into 32-bit output.
//
// Returns: E_ACCESSDENIED if either ready flag is clear, otherwise the
//          BlendCoeffs32 result.
// =============================================================*/

HRESULT
CHrtfMgr::GetCoeffs(DWORD *pcIndex, A3DVAL *pfWeight, A3DVAL fUnused,
		    DWORD *pdwOut)
{
	if (!m_fHaveEntry || !m_fHaveRate)
		return (E_ACCESSDENIED);

	return (BlendCoeffs32(m_pEntry, pcIndex, pfWeight, fUnused, pdwOut,
				   m_cSamples));
}

/* =============================================================
// GetCoeffs16()
// (RE) dbg:0x1005A010
//
// Blend four coefficient rows into 16-bit output.
//
// Returns: E_ACCESSDENIED if either ready flag is clear, otherwise the
//          BlendCoeffs16 result.
// =============================================================*/

HRESULT
CHrtfMgr::GetCoeffs16(DWORD *pcIndex, A3DVAL *pfWeight, A3DVAL fUnused,
			     short *pnOut)
{
	if (!m_fHaveEntry || !m_fHaveRate)
		return (E_ACCESSDENIED);

	return (BlendCoeffs16(m_pEntry, pcIndex, pfWeight, fUnused, pnOut,
				   m_cSamples));
}

/* =============================================================
// GetDelay()
// (RE) dbg:0x1005A080
//
// Blend four interaural delays.
//
// Returns:
//   S_OK
//   E_ACCESSDENIED  if either ready flag is clear or the delay array is absent
// =============================================================*/

HRESULT
CHrtfMgr::GetDelay(DWORD *pcIndex, A3DVAL *pfWeight, short *pnOut)
{
const short	*pDelay;

	if (!m_fHaveEntry || !m_fHaveRate || !m_pEntry->pDelay)
		return (E_ACCESSDENIED);

	pDelay = (const short *) m_pEntry->pDelay;

	*pnOut = (short) (pDelay[pcIndex[0]] * pfWeight[0] +
			  pDelay[pcIndex[1]] * pfWeight[1] +
			  pDelay[pcIndex[2]] * pfWeight[2] +
			  pDelay[pcIndex[3]] * pfWeight[3]);

	return (S_OK);
}

/* =============================================================
// GetSecondaryCoeffs()
// (RE) dbg:0x1005A250
//
// Blend four unsigned secondary rows without tail padding.
//
// Returns:
//   S_OK
//   E_ACCESSDENIED  if either ready flag is clear or the secondary array is
//                   absent
// =============================================================*/

HRESULT
CHrtfMgr::GetSecondaryCoeffs(DWORD *pcIndex, A3DVAL *pfWeight, short *pnOut)
{
const unsigned short	*pRow0;
const unsigned short	*pRow1;
const unsigned short	*pRow2;
const unsigned short	*pRow3;
A3DVAL			w0, w1, w2, w3;
DWORD			cStride;
DWORD			i;

	if (!m_fHaveEntry || !m_fHaveRate || !m_pEntry->pSecondaryData)
		return (E_ACCESSDENIED);

	cStride = m_pEntry->cSecondaryStride;

	pRow0 = (const unsigned short *) (m_pEntry->pSecondaryData + 2 * cStride * pcIndex[0]);
	pRow1 = (const unsigned short *) (m_pEntry->pSecondaryData + 2 * cStride * pcIndex[1]);
	pRow2 = (const unsigned short *) (m_pEntry->pSecondaryData + 2 * cStride * pcIndex[2]);
	pRow3 = (const unsigned short *) (m_pEntry->pSecondaryData + 2 * cStride * pcIndex[3]);

	w0 = pfWeight[0];
	w1 = pfWeight[1];
	w2 = pfWeight[2];
	w3 = pfWeight[3];

	for (i = 0; i < cStride; i++)
	{
		pnOut[i] = (short) (pRow0[i] * w0 +
				    pRow1[i] * w1 +
				    pRow2[i] * w2 +
				    pRow3[i] * w3);
	}

	return (S_OK);
}

/* =============================================================
// GetIDXsAndWts()
// (RE) rtl:0x10023080; dbg:0x1005A480
//
// Compute four HRTF row indices and interpolation weights for a direction
// and ear.
//
// Returns:
//   S_OK
//   E_ACCESSDENIED  if the entry-ready flag is clear
// =============================================================*/

HRESULT
CHrtfMgr::GetIDXsAndWts(A3DVAL fAzimuth, A3DVAL fElevation, int nEar,
			DWORD *pcIndex, A3DVAL *pfWeight)
{
A3DVAL	fAzStep;
A3DVAL	fElStep;
A3DVAL	fAz;
A3DVAL	fEl;
A3DVAL	fElFrac;
A3DVAL	fAzFrac;
int	cAz;
int	cEl;
int	cSpan;
int	cRows;
int	cTotal;
int	iAz;
int	iEl;

	if (!m_fHaveEntry)
		return (E_ACCESSDENIED);

	cAz    = m_pEntry->cAzimuth;
	cEl    = m_pEntry->cElevation;
	cSpan  = m_pEntry->cLastRow - m_pEntry->cFirstRow;
	cRows  = cSpan + 1;
	cTotal = cAz * cRows;

	fAzStep = A3D_TWO_PI / (A3DVAL) cAz;
	fElStep = A3D_PI / (A3DVAL) cEl;

	fAz = fAzimuth + A3D_PI;
	fAz = fAz - (A3DVAL) (int) (__int64) (fAz * A3D_INV_TWO_PI) * A3D_TWO_PI;

	fEl = A3D_HALF_PI - fElevation;

	iAz = (int) (__int64) (fAz / fAzStep) % cAz;
	iEl = (int) (__int64) (fEl / fElStep) % cEl;

	if (iEl < m_pEntry->cFirstRow)
	{
		pcIndex[0] = cTotal;
		pcIndex[1] = cRows * iAz;
		pcIndex[2] = cTotal;
		pcIndex[3] = cRows * ((iAz + 1) % m_pEntry->cAzimuth);

		fElFrac = 1.0f - fEl / (fElStep * (A3DVAL) m_pEntry->cFirstRow);
	}
	else if (iEl < m_pEntry->cLastRow)
	{
	int	iRow;
	int	iBase;

		iRow  = iEl - m_pEntry->cFirstRow;
		iBase = iRow + cRows * iAz;

		pcIndex[0] = iBase;
		pcIndex[1] = iBase + 1;
		pcIndex[2] = (cSpan + iBase + 1) % cTotal;
		pcIndex[3] = (cSpan + iBase + 2) % cTotal;

		fElFrac = 1.0f - (fEl - (A3DVAL) (iRow + m_pEntry->cFirstRow) *
				  fElStep) / fElStep;
	}
	else
	{
		pcIndex[0] = cSpan + cRows * iAz;
		pcIndex[1] = cTotal + 1;
		pcIndex[2] = cSpan + cRows * ((iAz + 1) % m_pEntry->cAzimuth);
		pcIndex[3] = cTotal + 1;

		fElFrac = 1.0f -
			(fEl - (A3DVAL) m_pEntry->cLastRow * fElStep) /
			((A3DVAL) (m_pEntry->cElevation - m_pEntry->cLastRow) *
			 fElStep);
	}

	fAzFrac = 1.0f - (fAz - (A3DVAL) iAz * fAzStep) / fAzStep;

	pfWeight[0] = fElFrac * fAzFrac;
	pfWeight[1] = fAzFrac * (1.0f - fElFrac);
	pfWeight[2] = fElFrac * (1.0f - fAzFrac);
	pfWeight[3] = (1.0f - fAzFrac) * (1.0f - fElFrac);

	if (nEar == 1)
	{
		pcIndex[0] += cTotal + A3D_HRTF_EXTRA_ROWS_PER_EAR;
		pcIndex[1] += cTotal + A3D_HRTF_EXTRA_ROWS_PER_EAR;
		pcIndex[2] += cTotal + A3D_HRTF_EXTRA_ROWS_PER_EAR;
		pcIndex[3] += cTotal + A3D_HRTF_EXTRA_ROWS_PER_EAR;
	}

	return (S_OK);
}

/* =============================================================
// BlendCoeffs32()
// (RE) dbg:0x1005A940
//
// Blend four signed coefficient rows into 32-bit output and zero-pad
// to the requested length.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
BlendCoeffs32(const A3DCOEFFENTRY *pcEntry, const DWORD *pcIndex,
		   const A3DVAL *pcWeight, A3DVAL fUnused, DWORD *pdwOut,
		   DWORD cWanted)
{
const short	*pRow0;
const short	*pRow1;
const short	*pRow2;
const short	*pRow3;
A3DVAL		w0, w1, w2, w3;
DWORD		cStride;
DWORD		cRow;
DWORD		i;

	(void) fUnused;

	cStride = pcEntry->cStride;

	pRow0 = (const short *) (pcEntry->pData + 2 * cStride * pcIndex[0]);
	pRow1 = (const short *) (pcEntry->pData + 2 * cStride * pcIndex[1]);
	pRow2 = (const short *) (pcEntry->pData + 2 * cStride * pcIndex[2]);
	pRow3 = (const short *) (pcEntry->pData + 2 * cStride * pcIndex[3]);

	w0 = pcWeight[0];
	w1 = pcWeight[1];
	w2 = pcWeight[2];
	w3 = pcWeight[3];

	cRow = pcEntry->cSamples;

	for (i = 0; i < cRow; i++)
	{
		pdwOut[i] = (DWORD) (pRow0[i] * w0 +
				     pRow1[i] * w1 +
				     pRow2[i] * w2 +
				     pRow3[i] * w3);
	}

	for (i = cRow; i < cWanted; i++)
		pdwOut[i] = 0;

	return (S_OK);
}

/* =============================================================
// BlendCoeffs16()
// (RE) dbg:0x1005AB70
//
// Blend four signed coefficient rows into 16-bit output and zero-pad
// to the requested length.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
BlendCoeffs16(const A3DCOEFFENTRY *pcEntry, const DWORD *pcIndex,
		   const A3DVAL *pcWeight, A3DVAL fUnused, short *pnOut,
		   DWORD cWanted)
{
const short	*pRow0;
const short	*pRow1;
const short	*pRow2;
const short	*pRow3;
A3DVAL		w0, w1, w2, w3;
DWORD		cStride;
DWORD		cRow;
DWORD		i;

	(void) fUnused;

	cStride = pcEntry->cStride;

	pRow0 = (const short *) (pcEntry->pData + 2 * cStride * pcIndex[0]);
	pRow1 = (const short *) (pcEntry->pData + 2 * cStride * pcIndex[1]);
	pRow2 = (const short *) (pcEntry->pData + 2 * cStride * pcIndex[2]);
	pRow3 = (const short *) (pcEntry->pData + 2 * cStride * pcIndex[3]);

	w0 = pcWeight[0];
	w1 = pcWeight[1];
	w2 = pcWeight[2];
	w3 = pcWeight[3];

	cRow = pcEntry->cSamples;

	for (i = 0; i < cRow; i++)
	{
		pnOut[i] = (short) (pRow0[i] * w0 +
				    pRow1[i] * w1 +
				    pRow2[i] * w2 +
				    pRow3[i] * w3);
	}

	for (i = cRow; i < cWanted; i++)
		pnOut[i] = 0;

	return (S_OK);
}
