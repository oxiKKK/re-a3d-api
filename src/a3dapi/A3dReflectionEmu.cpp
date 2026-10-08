/////////////////////////////////////////////////////////////////////////////
// A3dReflectionEmu.cpp
// =====================

// NOT PART OF THE ORIGINAL.  See A3dReflectionEmu.h.

// Renders optional software reflections for A2DBuffer voices. Each enabled
// reflection uses source controls to prepare resampling, HRTF filtering and
// a delay line, then adds its samples to the DAL_A2D floating-point mix.
//
// Per-voice effect state is stored separately from the reconstructed
// A2DBuffer layout. dal_a2d.cpp invokes the processing during voice mixing
// when A3D_FIXES is enabled. A3dReflectionEmu.h declares the
// tap state and the lookup used to obtain it.
/////////////////////////////////////////////////////////////////////////////

#include "A3dReflectionEmu.h"

#if defined(A3D_FIXES)

#include "d2dbuffer.h"
#include "a2dbuffer.h"
#include "hrtfmgr.h"

#define A3D_REFLECTIONEMU_SCRATCH_FRAMES 4096

/* Software-emulation gain conversion. */
#define A3D_REFLECTIONEMU_GAIN_MULTIPLIER 1.5
#define A3D_REFLECTIONEMU_GAIN_SCALE 32767.0

/* =============================================================
// CA3dReflectionEmuVoice()
//
// Initialize empty reflection taps.
// =============================================================*/

CA3dReflectionEmuVoice::CA3dReflectionEmuVoice(void)
{
	ZeroMemory(m_aTap, sizeof(m_aTap));
}

/* =============================================================
// ~CA3dReflectionEmuVoice()
//
// Free the reflection delay lines.
// =============================================================*/

CA3dReflectionEmuVoice::~CA3dReflectionEmuVoice(void)
{
	int i;

	for (i = 0; i < A3D_MAX_SOURCE_REFLECTIONS; i++)
	{
		delete[] m_aTap[i].pfDelayLine;
	}
}

/* =============================================================
// Process()
//
// Mix delayed reflection taps for up to the scratch-frame limit. Gains
// snap to their targets instead of using A3dMixRun ramp semantics. Ear
// delays represent propagation time and bypass interaural lookahead.
// =============================================================*/

void
CA3dReflectionEmuVoice::Process(A2DBuffer *pVoice, float *pMix, DWORD cFrames,
                                DWORD dwSampleRate)
{
static float s_afScratch[A3D_REFLECTIONEMU_SCRATCH_FRAMES * 2];
int          i;
DWORD        cRun;

	cRun = cFrames;
	if (cRun > A3D_REFLECTIONEMU_SCRATCH_FRAMES)
		cRun = A3D_REFLECTIONEMU_SCRATCH_FRAMES;

	for (i = 0; i < A3D_MAX_SOURCE_REFLECTIONS; i++)
	{
	A3DREFLECTIONEMU_TAP *pTap = &m_aTap[i];
	A3DCTRL_REFLECTION   *pRefl = &pVoice->m_A3dCtrlSuper.Reflections[i];
	DWORD  anIndex[4];
	A3DVAL afWeight[4];
	int    nDelayFrames;
	DWORD  f;

		pTap->bActive = pRefl->bEnable && !pRefl->bMute;

		if (!pTap->pfDelayLine)
		{
			pTap->cDelayFrames = (int)
				(A3D_REFLECTIONEMU_MAX_DELAY_S * dwSampleRate) + 1;

			pTap->pfDelayLine = new float[pTap->cDelayFrames * 2];
			ZeroMemory(pTap->pfDelayLine,
			           pTap->cDelayFrames * 2 * sizeof(float));

			pTap->nWritePos = 0;
		}

		ZeroMemory(s_afScratch, cRun * 2 * sizeof(float));

		if (pTap->bActive && pVoice->m_pHrtfMgr)
		{
			pTap->play = pVoice->m_play;
			pTap->mix  = pVoice->m_mix;

			pVoice->m_pHrtfMgr->GetIDXsAndWts(pRefl->LeftEar.fAzim,
			                                  pRefl->LeftEar.fElev,
			                                  0, anIndex, afWeight);
			pVoice->m_pHrtfMgr->GetCoeffs(anIndex, afWeight, 0,
			                              (DWORD *) &pTap->mix.earLeft.nGainScale);

			pVoice->m_pHrtfMgr->GetIDXsAndWts(pRefl->RightEar.fAzim,
			                                  pRefl->RightEar.fElev,
			                                  1, anIndex, afWeight);
			pVoice->m_pHrtfMgr->GetCoeffs(anIndex, afWeight, 0,
			                              (DWORD *) &pTap->mix.earRight.nGainScale);

			pTap->mix.earLeft.nGainWanted =
				(int) (A3D_REFLECTIONEMU_GAIN_MULTIPLIER *
				       pRefl->LeftEar.fGain * A3D_REFLECTIONEMU_GAIN_SCALE);
			pTap->mix.earRight.nGainWanted =
				(int) (A3D_REFLECTIONEMU_GAIN_MULTIPLIER *
				       pRefl->RightEar.fGain * A3D_REFLECTIONEMU_GAIN_SCALE);

			pTap->mix.earLeft.nGain = pTap->mix.earLeft.nGainPrev =
				pTap->mix.earLeft.nGainWanted;
			pTap->mix.earRight.nGain = pTap->mix.earRight.nGainPrev =
				pTap->mix.earRight.nGainWanted;

			pTap->mix.earLeft.nLookahead  = 0;
			pTap->mix.earRight.nLookahead = 0;

			A3dMixRun(&pTap->play, s_afScratch, &pTap->mix, cRun, 0);
		}

		nDelayFrames = (int) (pRefl->LeftEar.fDelay * (float) dwSampleRate);
		if (nDelayFrames < 0)
			nDelayFrames = 0;
		if (nDelayFrames >= pTap->cDelayFrames)
			nDelayFrames = pTap->cDelayFrames - 1;

		for (f = 0; f < cRun; f++)
		{
		int nRead;

			pTap->pfDelayLine[pTap->nWritePos * 2 + 0] = s_afScratch[f * 2 + 0];
			pTap->pfDelayLine[pTap->nWritePos * 2 + 1] = s_afScratch[f * 2 + 1];

			nRead = pTap->nWritePos - nDelayFrames;
			if (nRead < 0)
				nRead += pTap->cDelayFrames;

			pMix[f * 2 + 0] += pTap->pfDelayLine[nRead * 2 + 0];
			pMix[f * 2 + 1] += pTap->pfDelayLine[nRead * 2 + 1];

			pTap->nWritePos++;
			if (pTap->nWritePos >= pTap->cDelayFrames)
				pTap->nWritePos = 0;
		}
	}
}

#define A3D_REFLECTIONEMU_MAX_VOICES 128

typedef struct
{
	A2DBuffer             *pKey;
	CA3dReflectionEmuVoice *pVoice;
} A3DREFLECTIONEMU_SLOT;

static A3DREFLECTIONEMU_SLOT g_aSlot[A3D_REFLECTIONEMU_MAX_VOICES];

/* =============================================================
// A3dReflectionEmuGetVoice()
//
// Find or allocate reflection state for a voice.
//
// Returns: The reflection state; NULL when the table is full.
// =============================================================*/

CA3dReflectionEmuVoice *
A3dReflectionEmuGetVoice(A2DBuffer *pVoice)
{
int i;
int iFree;

	iFree = -1;

	for (i = 0; i < A3D_REFLECTIONEMU_MAX_VOICES; i++)
	{
		if (g_aSlot[i].pKey == pVoice)
			return (g_aSlot[i].pVoice);

		if (iFree < 0 && g_aSlot[i].pKey == NULL)
			iFree = i;
	}

	if (iFree < 0)
		return (NULL);

	g_aSlot[iFree].pKey   =     pVoice;
	g_aSlot[iFree].pVoice = new CA3dReflectionEmuVoice;

	return (g_aSlot[iFree].pVoice);
}

#endif /* A3D_FIXES */
