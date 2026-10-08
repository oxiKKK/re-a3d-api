/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * refaudbin.cpp
 *
 * Selects reflected sound paths for the available source reflection
 * slots. CRefAudBin groups candidate images by audibility, gathers the
 * strongest candidates and preserves or assigns slots before writing
 * source controls.
 *
 * This file also implements the global CA3dChained object list used for
 * cleanup and the driver vendor/version checks used during compatibility
 * activation. Geometry tracing supplies reflection candidates; source
 * processing consumes the selected controls.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "a3dclsfc.h"
#include "refaudbin.h"
#include "A3d3.h"
#include "A3dSource.h"
#include "A3dMatrix.h"

#include <stdio.h>
#include <string.h>


#define A3D_REF_AUD_BIN_SCALE   10.0f
#define A3D_REF_SOUND_SPEED     340.5f
#define A3D_REF_MAX_ALPHA       0.99f

/* =============================================================
// CRefAudBin()
// (RE) rtl:0x1001a0e0; dbg:0x1003e550; thunk dbg:0x1000136B
//
// Initialize empty reflection bins.
// =============================================================*/

CRefAudBin::CRefAudBin(void)
{
	Reset();
}

/* =============================================================
// ~CRefAudBin()
// (RE) dbg:0x1003e5d0; thunk dbg:0x100022C5
//
// Destroy the reflection bins.
// =============================================================*/

CRefAudBin::~CRefAudBin(void)
{
}

/* =============================================================
// Reset()
// (RE) dbg:0x1003e600
//
// Clear the bin and image counts.
// =============================================================*/

void
CRefAudBin::Reset(void)
{
	m_cImages       = 0;
	m_cCollected    = 0;
	memset(m_RefImageFilled, 0, sizeof(m_RefImageFilled));
}

/* =============================================================
// SetWindow()
// (RE) rtl:0x1001a150; dbg:0x1003e660; thunk dbg:0x10001C9E
//
// Load the source audibility window and reset the observed range.
// =============================================================*/

void
CRefAudBin::SetWindow(CA3dSource *pSource)
{
A3DVAL fDelta;

	m_fWindowMin = pSource->m_fAudibilityWindowMin;
	m_fWindowMax = pSource->m_fAudibilityWindowMax;

	fDelta = m_fWindowMax - m_fWindowMin;

	ASSERT(fDelta >= 0);

	if (fDelta <= 0.f)
		m_fBinScale = A3D_REF_AUD_BIN_SCALE;
	else
		m_fBinScale = A3D_REF_AUD_BIN_SCALE / fDelta;

	m_fSeenMin = 1.0f;
	m_fSeenMax = 0.0f;
}

/* =============================================================
// SaveWindow()
// (RE) dbg:0x1003e770; thunk dbg:0x10001B9A
//
// Save the observed audibility range when its maximum exceeds its minimum.
// Retail candidate rtl:0x1001a1c0 is based on address order only.
// =============================================================*/

void
CRefAudBin::SaveWindow(CA3dSource *pSource)
{
	if (m_fSeenMax > m_fSeenMin)
	{
		pSource->m_fAudibilityWindowMin = m_fSeenMin;
		pSource->m_fAudibilityWindowMax = m_fSeenMax;
	}
}

/* =============================================================
// Add()
// (RE) rtl:0x1001a200; dbg:0x1003e7e0; thunk dbg:0x10002A9A
//
// Add a reflection image to its audibility bin.
//
// Returns: New bin count, or 0 if the bin is full.
// =============================================================*/

INT
CRefAudBin::Add(DWORD dwId, A3DVAL fDistance, A3DVAL fAudibility,
		A3DVAL fAlpha, A3DVAL fGain, const A3DVAL *avPosition,
		A3DVAL fHit, const DWORD *adwMaterialPrefix, A3DVAL fDelay)
{
A3DREFIMAGE    *pImage;
int             nFilled;
int             iBin;

	ASSERT((fAudibility >= 0.f)&&(fAudibility<= 1.f));

	if (fAudibility < m_fSeenMin)
		m_fSeenMin = fAudibility;
	else if (fAudibility > m_fSeenMax)
		m_fSeenMax = fAudibility;

	iBin = (int) ((fAudibility - m_fWindowMin) * m_fBinScale);

	if (iBin < 0)
		iBin = 0;
	else if (iBin > A3D_REF_AUD_BIN_COUNT - 1)
		iBin = A3D_REF_AUD_BIN_COUNT - 1;

	ASSERT(iBin < sizeof(m_RefImageFilled)/sizeof(int));
	ASSERT(iBin >= 0);

	nFilled = m_RefImageFilled[iBin];
	ASSERT(nFilled >= 0);

	if (nFilled < A3D_MAX_SOURCE_REFLECTIONS)
	{
		pImage = &m_RefImage[iBin][nFilled];

		pImage->dwId                    = dwId;
		pImage->fDistance               = fDistance;
		pImage->fAudibility             = fAudibility;
		pImage->fAlpha                  = fAlpha;
		pImage->fGain                   = fGain;
		pImage->avPosition[0]           = avPosition[0];
		pImage->avPosition[1]           = avPosition[1];
		pImage->avPosition[2]           = avPosition[2];
		pImage->fHit                    = fHit;
		pImage->adwMaterialPrefix[0]    = adwMaterialPrefix[0];
		pImage->adwMaterialPrefix[1]    = adwMaterialPrefix[1];
		pImage->fDelay                  = fDelay;

		m_cImages++;
		return (++m_RefImageFilled[iBin]);
	}

	return (0);
}

/* =============================================================
// Update()
// (RE) dbg:0x1003ead0; thunk dbg:0x10001839
//
// Assign audible images to reflection slots, disabling all slots if none exist.
// Retail candidate rtl:0x1001a2f0 is based on address order only.
// =============================================================*/

void
CRefAudBin::Update(CA3dRoot *pRoot, A3DMATRIX pmListener, CA3dSource *pSource)
{
A3DREFIMAGE    *apCollected[A3D_MAX_SOURCE_REFLECTIONS];
A3DREFIMAGE    *apSlot[A3D_MAX_SOURCE_REFLECTIONS];
int             i;

	if (m_cImages > 0)
	{
		memset(apSlot, 0, sizeof(apSlot));

		Collect(pSource, apCollected, apSlot);
		AssignSlots(apCollected, apSlot);
		Output(pRoot, pmListener, pSource, apSlot);
	}
	else
	{
		for (i = 0; i < A3D_MAX_SOURCE_REFLECTIONS; i++)
			pSource->m_pBufferCurr->Reflections[i].bEnable = FALSE;
	}
}

/* =============================================================
// Collect()
// (RE) dbg:0x1003ebc0
//
// Collect up to 16 images from the most audible bins, preserving available
// slots.
// Retail candidate rtl:0x1001a3b0 is based on address order only.
// =============================================================*/

void
CRefAudBin::Collect(CA3dSource *pSource, A3DREFIMAGE **apCollected,
		    A3DREFIMAGE **apSlot)
{
A3DREFIMAGE    *pImage;
int             iBin;
int             i;
int             j;

	m_cCollected = 0;

	iBin = A3D_REF_AUD_BIN_COUNT;

	while (m_cCollected < A3D_MAX_SOURCE_REFLECTIONS)
	{
		if (--iBin < 0)
			break;

		for (i = 0; i < m_RefImageFilled[iBin]; i++)
		{
		BOOL bClaimed;

			bClaimed = FALSE;

			pImage = &m_RefImage[iBin][i];

			pImage->nSlot           = A3D_REF_SLOT_UNASSIGNED;
			pImage->bDontTrack      = TRUE;

			for (j = 0; j < A3D_MAX_SOURCE_REFLECTIONS; j++)
			{
				if (pImage->dwId == pSource->m_adwReflectionSlotIds[j])
				{
					if (apSlot[j] != NULL)
					{
						bClaimed = TRUE;
					}
					else
					{
						apSlot[j]               = pImage;
						pImage->nSlot           = j;
						pImage->bDontTrack      = FALSE;
					}

					break;
				}
			}

			if (!bClaimed)
			{
				apCollected[m_cCollected++] = pImage;

				if (m_cCollected >= A3D_MAX_SOURCE_REFLECTIONS)
					break;
			}
		}
	}
}

/* =============================================================
// AssignSlots()
// (RE) dbg:0x1003ed70; inlined at rtl:0x1001A34E
//
// Assign free hardware slots to collected images without slots.
// =============================================================*/

void
CRefAudBin::AssignSlots(A3DREFIMAGE **apCollected, A3DREFIMAGE **apSlot)
{
A3DREFIMAGE    *pImage;
int             nSlot;
int             i;

	nSlot = 0;

	for (i = 0; i < m_cCollected; i++)
	{
		ASSERT((i>=0)&&(i<16));

		pImage = apCollected[i];

		if (pImage->nSlot == A3D_REF_SLOT_UNASSIGNED)
		{
			while (nSlot < A3D_MAX_SOURCE_REFLECTIONS)
			{
				if (apSlot[nSlot] == NULL)
				{
					apSlot[nSlot] = pImage;
					pImage->nSlot = nSlot++;
					break;
				}
				nSlot++;
			}
		}
	}
}

/* =============================================================
// Output()
// (RE) rtl:0x1001a490; dbg:0x1003ee80
//
// Write reflection slots and source IDs using the Retail behavior.
// Write no Debug ref_track_mode tracking overrides. Debug-only globals: counter
// dbg:0x10152ce8, mode dbg:0x10152cec, last mode dbg:0x10152cf0.
// =============================================================*/

void
CRefAudBin::Output(CA3dRoot *pRoot, A3DMATRIX pmListener, CA3dSource *pSource,
		   A3DREFIMAGE **apSlot)
{
A3DCTRL_REFLECTION     *pRefl;
A3DREFIMAGE            *pImg;
A3DVAL                  av[4];
A3DVAL                  avPolar[3];
A3DVAL                  fDelay;
int                     i;

	for (i = 0; i < A3D_MAX_SOURCE_REFLECTIONS; i++)
	{
		pRefl = &pSource->m_pBufferCurr->Reflections[i];

		pImg = apSlot[i];

		if (pImg != NULL)
		{
			pRefl->bEnable          = TRUE;
			pRefl->fAudibility      = pImg->fAudibility;

			av[0] = pImg->avPosition[0];
			av[1] = pImg->avPosition[1];
			av[2] = pImg->avPosition[2];
			av[3] = 0.0f;

			A3dTraceTransformDir(av, pmListener, av);
			A3dToPolar(av, avPolar);

			if (pImg->fDelay == A3D_REF_DELAY_FROM_DISTANCE)
				fDelay = pSource->m_fReflectionDelayScale
					* pRoot->m_fGlobalReflectionDelayScale
					* (pImg->fDistance - pSource->m_fEarLeft)
					/ (A3D_REF_SOUND_SPEED * pRoot->m_fUnitsPerMeter);
			else
				fDelay = pImg->fDelay;

			if (fDelay < 0.0f)
				fDelay = 0.0f;
			if (fDelay > pRoot->m_fMaxReflectionDelayTime)
				fDelay = pRoot->m_fMaxReflectionDelayTime;

			pRefl->bMute = pImg->bDontTrack;

			pRefl->fAlpha = 1.0f - pImg->fAlpha;
			if (pRefl->fAlpha > A3D_REF_MAX_ALPHA)
				pRefl->fAlpha = A3D_REF_MAX_ALPHA;

			pRefl->LeftEar.fAzim    = pRefl->RightEar.fAzim = avPolar[0];
			pRefl->LeftEar.fElev    = pRefl->RightEar.fElev = avPolar[1];
			pRefl->LeftEar.fGain    = pRefl->RightEar.fGain = pImg->fGain;
			pRefl->LeftEar.fDelay   = pRefl->RightEar.fDelay = fDelay;

			pSource->m_adwReflectionSlotIds[i] = pImg->dwId;
		}
		else
		{
			pSource->m_adwReflectionSlotIds[i]      = 0;
			pRefl->bEnable                          = FALSE;
		}
	}
}

/* (RE) Global object chain: rtl:0x10067cc0; dbg:0x10152cf8. */

static CA3dChained	*g_pChainHead;

/* =============================================================
// FreeChain()
// (RE) rtl:0x1001a660; dbg:0x1003f1f0
//
// Delete every object on the global chain.
// =============================================================*/

void
FreeChain(void)
{
CA3dChained *pNext;

	while (g_pChainHead != NULL)
	{
		pNext = g_pChainHead->m_pNext;
		delete g_pChainHead;
		g_pChainHead = pNext;
	}
}

/* =============================================================
// Link()
// (RE) rtl:0x1001a690; dbg:0x1003f270
//
// Insert the object at the global chain head.
// =============================================================*/

void
CA3dChained::Link(void)
{
	if (g_pChainHead != NULL)
		g_pChainHead->m_pPrev = this;

	m_pNext         = g_pChainHead;
	g_pChainHead    = this;
	m_pPrev         = NULL;
}

/* =============================================================
// Unlink()
// (RE) rtl:0x1001a6c0; dbg:0x1003f2d0
//
// Remove the object from the global chain.
// =============================================================*/

void
CA3dChained::Unlink(void)
{
	if (m_pPrev != NULL)
		m_pPrev->m_pNext = m_pNext;

	if (m_pNext != NULL)
		m_pNext->m_pPrev = m_pPrev;

	if (this == g_pChainHead)
		g_pChainHead = m_pNext;
}

#define NUM_VERSION_KEYS	(sizeof(aszVersionKeys) / sizeof(aszVersionKeys[0]))
#define VERKEY_PRODUCTNAME	0
#define VERKEY_COMPANYNAME	5

static const char *aszVersionKeys[] =
{
	"ProductName",
	"ProductVersion",
	"OriginalFilename",
	"FileDescription",
	"FileVersion",
	"CompanyName",
	"LegalCopyright",
	"LegalTrademarks",
	"InternalName",
	"PrivateBuild",
	"SpecialBuild",
	"Comments"
};

static char szEmpty[] = "";

/* =============================================================
// A3dCheckFileVersion()
// (RE) rtl:0x1001A6F0; dbg:0x1003F520
//
// Check a file's product and company strings.
//
// Returns:
//   A3DVER_AUREAL   accepted vendor strings
//   A3DVER_NONE     if both strings are absent
//   A3DVER_FOREIGN  other strings or a resource read failure
// =============================================================*/

int
A3dCheckFileVersion(
	CHAR *lptstrFilename,
	DWORD dwLen,
	LPVOID lpData)
{
struct
{
	const char     *pszKey;
	char           *pszValue;
} aValues[NUM_VERSION_KEYS];
char    szSubBlock[MAX_PATH];
char    szLang[12];
UINT    puLen;
LPVOID  lpValue;
UINT    i;

	if (!GetFileVersionInfoA(lptstrFilename, 0, dwLen, lpData))
		return (A3DVER_FOREIGN);

	wsprintfA(szLang, "%04X", GetUserDefaultLangID());
	strcat(szLang, "04B0");

	for (i = 0; i < NUM_VERSION_KEYS; i++)
	{
		lstrcpyA(szSubBlock, "\\StringFileInfo\\");
		lstrcatA(szSubBlock, szLang);
		lstrcatA(szSubBlock, "\\");
		lstrcatA(szSubBlock, aszVersionKeys[i]);

		aValues[i].pszKey = aszVersionKeys[i];

		if (dwLen && VerQueryValueA(lpData, szSubBlock, &lpValue, &puLen))
			aValues[i].pszValue = (char *) lpValue;
		else
			aValues[i].pszValue = szEmpty;
	}

	if (!_strcmpi(aValues[VERKEY_COMPANYNAME].pszValue, "Aureal Semiconductor"))
		return (A3DVER_AUREAL);

	if (!_strcmpi(aValues[VERKEY_COMPANYNAME].pszValue, "Aureal Semiconductor Inc."))
		return (A3DVER_AUREAL);

	if (!_strcmpi(aValues[VERKEY_PRODUCTNAME].pszValue, "SM Emulation"))
		return (A3DVER_AUREAL);

	if (strlen(aValues[VERKEY_PRODUCTNAME].pszValue) ||
	    strlen(aValues[VERKEY_COMPANYNAME].pszValue))
		return (A3DVER_FOREIGN);

	return (A3DVER_NONE);
}

/* =============================================================
// A3dCheckDriverVersion()
// (RE) rtl:0x1001A880; dbg:0x1003F730
//
// Check A3D.DLL in the system directory. The original undersized resource
// buffer causes the reference DLL to report A3DVER_NONE.
//
// Returns: A3dCheckFileVersion result; -1 (A3DVER_NONE) if the system directory
//          is unavailable.
// =============================================================*/

int
A3dCheckDriverVersion(void)
{
BYTE abData[1024];
char szPath[MAX_PATH];

	if (!GetSystemDirectoryA(szPath, MAX_PATH))
		return (A3DVER_NONE);

	strcat(szPath, szPath[strlen(szPath) - 1] != '\\' ? "\\A3D.DLL" : "A3D.DLL");

	return (A3dCheckFileVersion(szPath, sizeof(abData), abData));
}
