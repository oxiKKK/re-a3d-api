/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * a2dbuffer.cpp
 *
 * Implements A2DBuffer, an individual software voice mixed by DAL_A2D. It
 * provides DirectSound buffer controls over shared waveform storage and
 * keeps the playback cursor, looping state, frequency, gains and spatial
 * filter state for each sound.
 *
 * The voice helpers translate A3D source controls into resampling steps,
 * ear delays and head-related transfer function (HRTF) coefficients. They
 * use the sample-processing routines in softmix.cpp. Released voices are
 * marked for collection by the DAL_A2D mixer thread; several DirectSound
 * forwarding and control methods are defined in d2dbuffer.cpp.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "a2dbuffer.h"
#include "d2dbuffer.h"
#include "dalinfo.h"
#include "hrtfmgr.h"
#include "softmix.h"
#include "Waveform.h"

#include <stdio.h>

/* Source-control conversion factors */
#define A3D_A2D_GAIN_MULTIPLIER         1.5
#define A3D_A2D_GAIN_FULL_SCALE         32767.0
#define A3D_A2D_DELAY_SAMPLE_RATE       22050.0
#define A3D_A2D_DELAY_FRACTION_SCALE    65536.0


#define A3D_A2D_MAX_MIX_SAMPLES 0x1000
#define A3D_A2D_FORMAT_EIGHT    0x20
#define A3D_A2D_FORMAT_MONO     1
#define A3D_A2D_FORMAT_STEREO   6

/* =============================================================
// A3dVoiceReset()
// (RE) dbg:0x1003f800; rtl:0x1001a930
//
// Initialize resampler format and cursor position at 11025, 22050 or 44100 Hz.
// =============================================================*/

void
A3dVoiceReset(A3DMIXSTATE *pState, A3DMIXPLAY *pPlay, int nRateKHz)
{
DWORD cb;

	cb = pPlay->cSamples;

	if (pPlay->dwFormatBits & A3DMIX_FORMAT_STEREO)
		cb <<= 1;

	if (pPlay->dwFormatBits & A3DMIX_FORMAT_SIXTEEN)
		cb <<= 1;

	nRateKHz &= 0xFFFF;

	A3dMixSetFormat(pState, pPlay->dwFormatBits, nRateKHz, pPlay->pStart,
			(int) cb);

	A3dMixSeek(pState, pPlay, pPlay->pCur - pPlay->pStart);

	if (nRateKHz == 11)
		pState->nSampleRate = 11025;
	else if (nRateKHz == 22)
		pState->nSampleRate = 22050;
	else
		pState->nSampleRate = 44100;
}

/* =============================================================
// A3dMixSetRate()
// (RE) dbg:0x1003f910; thunk dbg:0x10002847
//
// Set the requested resampler step and cap its target at 10 times the current
// step. The reference return value is unresolved; this declaration returns
// void.
// =============================================================*/

void
A3dMixSetRate(A3DMIXSTATE *pState, DWORD dwRate)
{
DWORD dwStep;

	dwStep = (dwRate << 15) / (pState->nSampleRate >> 1);

	pState->nStepWanted = dwStep;

	if (pState->nStep)
	{
		if (dwStep <= A3D_MIX_MAX_STEP_MULTIPLIER * pState->nStep)
			pState->nStepTarget = dwStep;
		else
			pState->nStepTarget = A3D_MIX_MAX_STEP_MULTIPLIER * pState->nStep;
	}
	else
	{
		pState->nStep       = dwStep;
		pState->nStepTarget = dwStep;
	}
}

/* =============================================================
// A3dMixSnap()
// (RE) dbg:0x1003f9b0; rtl:0x1001a9d0; thunk dbg:0x10001203
//
// Apply requested step and gains immediately; reset gain history and delay
// residue.
// =============================================================*/

static void
A3dMixSnap(A3DMIXSTATE *pState)
{
	pState->nStep       = pState->nStepWanted;
	pState->nStepTarget = pState->nStepWanted;

	pState->earLeft.nGain  = (int) (((DWORD) (pState->earLeft.nGainScale *
						  pState->earLeft.nGainWanted)) >> 15);
	pState->earRight.nGain = (int) (((DWORD) (pState->earRight.nGainScale *
						  pState->earRight.nGainWanted)) >> 15);

	pState->earLeft.nGainPrev  = pState->earLeft.nGain;
	pState->earRight.nGainPrev = pState->earRight.nGain;

	pState->earLeft.nResidue  = pState->earLeft.nLookahead;
	pState->earRight.nResidue = pState->earRight.nLookahead;
}

/* =============================================================
// A3dMixSeek()
// (RE) dbg:0x1003fa80; rtl:0x1001aa30
//
// Set the resampler byte position and clear both ears' fractional positions.
// =============================================================*/

void
A3dMixSeek(A3DMIXSTATE *pState, A3DMIXPLAY *pPlay, int cb)
{
	pState->dwPos = cb;

	pState->earLeft.nPos     = cb;
	pState->earLeft.nResidue = 0;

	pState->earRight.nPos     = cb;
	pState->earRight.nResidue = 0;

	pPlay->pCur = pPlay->pStart + cb;

	pState->earLeft.dwFrac  = 0;
	pState->earRight.dwFrac = 0;
}

/* =============================================================
// A3dMixRun()
// (RE) dbg:0x1003fb10; rtl:0x1001aa70
//
// Mix one voice into the accumulator, applying HRTF filters for nonzero mode.
//
// Returns: 0 after mixing; 2 for zero samples; -1 above 4096 samples.
// =============================================================*/

short
A3dMixRun(A3DMIXPLAY *pPlay, float *pMix, A3DMIXSTATE *pState, DWORD cSamples,
	  int nMode)
{
	if (cSamples > A3D_A2D_MAX_MIX_SAMPLES)
	{
		printf("numsamples of %d is too high!\n", cSamples);

		return (-1);
	}

	if (!cSamples)
		return (2);

	pState->dwLooping = pPlay->dwFormatBits & A3DMIX_FORMAT_LOOPING;

	if (nMode)
	{
		pState->earLeft.nGain  = pState->earLeft.nGainWanted;
		pState->earRight.nGain = pState->earRight.nGainWanted;

		A3dMixFilter(pPlay, pState, &pState->earLeft, cSamples, pMix);
		A3dMixFilter(pPlay, pState, &pState->earRight, cSamples,
			     pMix + 1);

		A3dMixFinish(pState, cSamples);
	}
	else
	{
		pState->earLeft.nGain = (int) (((DWORD) (pState->earLeft.nGainScale *
							 pState->earLeft.nGainWanted)) >> 14);
		pState->earRight.nGain = (int) (((DWORD) (pState->earRight.nGainScale *
							  pState->earRight.nGainWanted)) >> 14);

		A3dMixPlain(pState, pMix, cSamples);
	}

	pPlay->pCur = pPlay->pStart + pState->dwPos;

	return (0);
}

/* =============================================================
// A3dMixFilter()
// (RE) dbg:0x1003fc90; rtl:0x1002cb11
//
// Generate and convolve one ear with saved filter history.
// Calls must be serialized because g_afMixScratch is shared.
// =============================================================*/

void
A3dMixFilter(A3DMIXPLAY *pPlay, A3DMIXSTATE *pState, A3DMIXEAR *pEar,
	     DWORD cSamples, float *pOut)
{
DWORD cTaps;

	cTaps = pEar->cTaps;

	CopyMemory(g_afMixScratch, pEar->afHistory, sizeof(float) * cTaps);

	A3dMixGenerate(pState, pEar, &g_afMixScratch[cTaps], cSamples);

	CopyMemory(pEar->afHistory, &g_afMixScratch[cSamples],
		   sizeof(float) * cTaps);

	A3dMixConvolve(pEar, g_afMixScratch, pOut, cSamples);
}

/* =============================================================
// A2DBuffer::A2DBuffer()
// (RE) dbg:0x1003fd50; rtl:0x1001ac20
//
// Initialize an empty software voice with an unset playback cursor.
// =============================================================*/

A2DBuffer::A2DBuffer(void)
{
	m_cRef     = 1;
	m_dwFlags  = 0;
	m_lVolume  = 0;
	m_lPan     = 0;

	m_dwSampleRate = 0;
	m_dwFrequency  = 0;
	m_dwWrite      = 0;

	m_nPendingSeek        = A3D_A2D_NO_PENDING_SEEK;

	m_cbBuffer     = 0;
	m_dwState      = 0;

	m_pWaveForm    = NULL;
	m_pHrtfMgr     = NULL;
}

/* =============================================================
// A2DBuffer::~A2DBuffer()
// (RE) dbg:0x1003ff20
//
// Release shared wave storage.
// =============================================================*/

A2DBuffer::~A2DBuffer(void)
{
	if (m_pWaveForm != NULL)
	{
		m_pWaveForm->Release();
		m_pWaveForm = NULL;
	}
}

/* =============================================================
// A2DBuffer::AddRef()
// (RE) dbg:0x10040150; rtl:0x1001ae10
//
// Increment the reference count.
//
// Returns: Reference count read after the increment.
// =============================================================*/

STDMETHODIMP_(ULONG)
A2DBuffer::AddRef(void)
{
	InterlockedIncrement(&m_cRef);

	return ((ULONG) m_cRef);
}

/* =============================================================
// A2DBuffer::Release()
// (RE) dbg:0x10040180; rtl:0x1001ae30
//
// Release a reference and mark the voice for collection at zero.
//
// Returns: Remaining reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
A2DBuffer::Release(void)
{
	InterlockedDecrement(&m_cRef);

	if (m_cRef)
		return ((ULONG) m_cRef);

	m_dwState = A3DVOICE_STATE_RELEASED;

	return (0);
}

/* =============================================================
// A2DBuffer::GetCurrentPosition()
// (RE) dbg:0x100402b0; rtl:0x1001ae90
//
// Write identical play and write cursors, using a pending seek when present.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
A2DBuffer::GetCurrentPosition(DWORD *pdwPlay, DWORD *pdwWrite)
{
	if (m_nPendingSeek == A3D_A2D_NO_PENDING_SEEK)
	{
		*pdwPlay  = m_dwWrite;
		*pdwWrite = m_dwWrite;
	}
	else
	{
		*pdwPlay  = (DWORD) m_nPendingSeek;
		*pdwWrite = (DWORD) m_nPendingSeek;
	}

	return (S_OK);
}

/* =============================================================
// A2DBuffer::GetVolume()
// (RE) dbg:0x10040410; rtl:0x1001af00
//
// Write stored playback volume.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null output
// =============================================================*/

STDMETHODIMP
A2DBuffer::GetVolume(LONG *lplVolume)
{
	ASSERT(lplVolume != 0 && !IsBadReadPtr(lplVolume, sizeof(LONG)));

	if (!lplVolume)
		return (E_INVALIDARG);

	*lplVolume = m_lVolume;

	return (S_OK);
}

/* =============================================================
// A2DBuffer::GetPan()
// (RE) dbg:0x100404a0; rtl:0x1001af30
//
// Write stored playback pan.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null output
// =============================================================*/

STDMETHODIMP
A2DBuffer::GetPan(LONG *lplPan)
{
	ASSERT(lplPan != 0 && !IsBadReadPtr(lplPan, sizeof(LONG)));

	if (!lplPan)
		return (E_INVALIDARG);

	*lplPan = m_lPan;

	return (S_OK);
}

/* =============================================================
// A2DBuffer::GetFrequency()
// (RE) dbg:0x10040530; rtl:0x1001af60
//
// Write playback frequency.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
A2DBuffer::GetFrequency(DWORD *lpdwFrequency)
{
	ASSERT(lpdwFrequency != 0 && !IsBadReadPtr(lpdwFrequency, sizeof(DWORD)));

	*lpdwFrequency = m_dwFrequency;

	return (S_OK);
}

/* =============================================================
// A2DBuffer::GetStatus()
// (RE) dbg:0x100405b0; rtl:0x10022bb0
//
// Write playback status. The original suppresses the looping status when
// any other voice flag is set.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
A2DBuffer::GetStatus(DWORD *lpdwStatus)
{
	ASSERT(lpdwStatus != 0 && !IsBadReadPtr(lpdwStatus, sizeof(DWORD)));

	*lpdwStatus = 0;

	if (m_dwState == A3DVOICE_STATE_PLAYING)
	{
		*lpdwStatus |= DSBSTATUS_PLAYING;

		if (m_dwFlags == A3DVOICE_FLAG_LOOPING)
			*lpdwStatus |= DSBSTATUS_LOOPING;
	}

	return (S_OK);
}

/* =============================================================
// A2DBuffer::Lock()
// (RE) dbg:0x10040720; rtl:0x1001af80
//
// Return wave storage in up to 2 spans. The original ignores
// DSBLOCK_ENTIREBUFFER.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null first pointer/count output or a requested length
//                 exceeding the buffer size
// =============================================================*/

STDMETHODIMP
A2DBuffer::Lock(DWORD dwOffset, DWORD dwWriteBytes, LPVOID *lplpvAudioPtr1,
		LPDWORD lpdwAudioBytes1, LPVOID *ppvAudioPtr2,
		LPDWORD pdwAudioBytes2, DWORD dwFlags)
{
DWORD	cbLock;

	ASSERT(lplpvAudioPtr1 != 0);
	ASSERT(lpdwAudioBytes1 != 0 && !IsBadReadPtr(lpdwAudioBytes1, sizeof(DWORD)));
	ASSERT(dwWriteBytes <= m_DSBufferDesc.dwBufferBytes);
	ASSERT(m_pWaveForm != 0 && !IsBadReadPtr(m_pWaveForm, sizeof(CWaveForm)));

	if (!lplpvAudioPtr1)
	{
		DBGSTR("A2DBuffer::Lock() - lplpvAudioPtr1 is NULL.\n");

		return (E_INVALIDARG);
	}

	if (!lpdwAudioBytes1)
	{
		DBGSTR("A2DBuffer::Lock() - lpdwAudioBytes1 is NULL.\n");

		return (E_INVALIDARG);
	}

	if (dwWriteBytes > m_DSBufferDesc.dwBufferBytes)
	{
		DBGSTR("A2DBuffer::Lock() - Requested write bytes greater than the size of the actual buffer.\n");

		return (E_INVALIDARG);
	}

	cbLock = dwWriteBytes;

	if ((dwFlags & DSBLOCK_ENTIREBUFFER) == 1)
		cbLock = m_DSBufferDesc.dwBufferBytes;

	if ((dwFlags & DSBLOCK_FROMWRITECURSOR) == 1)
		dwOffset = m_dwWrite;

	if (dwOffset + cbLock > m_DSBufferDesc.dwBufferBytes)
	{
		*lplpvAudioPtr1   = m_pWaveForm->GetBuffer() + dwOffset;
		*lpdwAudioBytes1 = m_DSBufferDesc.dwBufferBytes - dwOffset;

		if (ppvAudioPtr2 && pdwAudioBytes2)
		{
			*ppvAudioPtr2   = m_pWaveForm->GetBuffer();
			*pdwAudioBytes2 = cbLock - *lpdwAudioBytes1;
		}
	}
	else
	{
		*lplpvAudioPtr1   = m_pWaveForm->GetBuffer() + dwOffset;
		*lpdwAudioBytes1 = cbLock;

		if (ppvAudioPtr2)
		{
			if (pdwAudioBytes2)
			{
				*ppvAudioPtr2   = NULL;
				*pdwAudioBytes2 = 0;
			}
		}
	}

	return (S_OK);
}

/* =============================================================
// A2DBuffer::Play()
// (RE) dbg:0x10040a60; rtl:0x1001b070
//
// Start playback with the requested looping mode; rewind a stopped voice at its
// end.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
A2DBuffer::Play(DWORD dwReserved1, DWORD dwPriority, DWORD dwFlags)
{
	if (m_dwState == A3DVOICE_STATE_STOPPED &&
	    m_dwWrite == m_pWaveForm->GetBufferSize())
	{
		m_dwWrite       = 0;
		m_nPendingSeek  = 0;
	}

	m_dwState = A3DVOICE_STATE_PLAYING;

	if (dwFlags & DSBPLAY_LOOPING)
	{
		m_dwFlags           |= A3DVOICE_FLAG_LOOPING;
		m_play.dwFormatBits |= A3D_MIXFMT_LOOPING;
	}
	else
	{
		m_dwFlags           &= ~A3DVOICE_FLAG_LOOPING;
		m_play.dwFormatBits &= ~A3D_MIXFMT_LOOPING;
	}

	return (S_OK);
}

/* =============================================================
// A2DBuffer::SetVolume()
// (RE) dbg:0x10040b80; rtl:0x1001b100
//
// Store playback volume.
//
// Returns:
//   S_OK
//   E_INVALIDARG  outside DSBVOLUME_MIN through DSBVOLUME_MAX
// =============================================================*/

STDMETHODIMP
A2DBuffer::SetVolume(LONG lVolume)
{
	if (lVolume < DSBVOLUME_MIN || lVolume > DSBVOLUME_MAX)
	{
		DBGSTR("A2DBuffer::SetVolume() - Invalid value.  Must be between DSBVOLUME_MIN & DSBVOLUME_MAX.\n");

		return (E_INVALIDARG);
	}

	m_lVolume = lVolume;

	return (S_OK);
}

/* =============================================================
// A2DBuffer::Unknown_0x54()
// (RE) dbg:0x10041640; rtl:0x10003500; thunk dbg:0x10002027
//
// Leave buffer state unchanged; the operation is unresolved.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
A2DBuffer::Unknown_0x54(DWORD, DWORD)
{
	return (E_NOTIMPL);
}

/* (RE) IUnknown adjustor thunks: dbg:0x10041e50 through dbg:0x10041f00. */

/* =============================================================
// A2DBuffer::SetA3dSuperCtrl()
// (RE) dbg:0x10040d10; rtl:0x1001b1d0
//
// Apply source gains, relative delay, resampling rate and HRTF coefficients.
// The original selects the delayed ear from the left delay sign.
// The HRTF bank must provide one gain coefficient per ear.
//
// Returns:
//   S_OK
//   HRTF  lookup failures are ignored
// =============================================================*/

HRESULT
A2DBuffer::SetA3dSuperCtrl(LPA3DCTRL_SRC_SUPER lpA3dCtrlSuper, DWORD dwSize)
{
CHrtfMgr       *pCoeff;
DWORD           anIndex[4];
A3DVAL          afWeight[4];
DWORD           dwStatus;

	ASSERT(lpA3dCtrlSuper != 0);

	CopyMemory(&m_A3dCtrlSuper, lpA3dCtrlSuper, sizeof(A3DCTRL_SRC_SUPER));

	SetRate((DWORD) ((double) m_dwSampleRate * lpA3dCtrlSuper->fFreqFactor));

	m_mix.earLeft.nGainWanted  = (int) (A3D_A2D_GAIN_MULTIPLIER * lpA3dCtrlSuper->LeftEar.fGain * A3D_A2D_GAIN_FULL_SCALE);
	m_mix.earRight.nGainWanted = (int) (A3D_A2D_GAIN_MULTIPLIER * lpA3dCtrlSuper->RightEar.fGain * A3D_A2D_GAIN_FULL_SCALE);

	if (lpA3dCtrlSuper->LeftEar.fDelay >= 0.0f)
	{
		m_mix.earRight.nLookahead = 0;
		m_mix.earLeft.nLookahead  = (int)
			(lpA3dCtrlSuper->LeftEar.fDelay  * A3D_A2D_DELAY_SAMPLE_RATE * A3D_A2D_DELAY_FRACTION_SCALE -
			 lpA3dCtrlSuper->RightEar.fDelay * A3D_A2D_DELAY_SAMPLE_RATE * A3D_A2D_DELAY_FRACTION_SCALE);
	}
	else
	{
		m_mix.earLeft.nLookahead  = 0;
		m_mix.earRight.nLookahead = (int)
			(lpA3dCtrlSuper->RightEar.fDelay * A3D_A2D_DELAY_SAMPLE_RATE * A3D_A2D_DELAY_FRACTION_SCALE -
			 lpA3dCtrlSuper->LeftEar.fDelay  * A3D_A2D_DELAY_SAMPLE_RATE * A3D_A2D_DELAY_FRACTION_SCALE);
	}

	pCoeff = m_pHrtfMgr;

	pCoeff->GetIDXsAndWts(lpA3dCtrlSuper->LeftEar.fAzim,
			      lpA3dCtrlSuper->LeftEar.fElev, 0,
			      anIndex, afWeight);
	pCoeff->GetCoeffs(anIndex, afWeight, 0,
			  (DWORD *) &m_mix.earLeft.nGainScale);

	pCoeff->GetIDXsAndWts(lpA3dCtrlSuper->RightEar.fAzim,
			      lpA3dCtrlSuper->RightEar.fElev, 1,
			      anIndex, afWeight);
	pCoeff->GetCoeffs(anIndex, afWeight, 0,
			  (DWORD *) &m_mix.earRight.nGainScale);

	GetStatus(&dwStatus);

	if (!(dwStatus & DSBSTATUS_PLAYING))
		A3dMixSnap(&m_mix);

	return (S_OK);
}

/* =============================================================
// A2DBuffer::GetAllocationStatus()
// (RE) dbg:0x10040fc0; rtl:0x100034f0
//
// Leave the allocation status output unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
A2DBuffer::GetAllocationStatus(LPDWORD lpdwStatus)
{
	ASSERT(lpdwStatus != 0 && !IsBadReadPtr(lpdwStatus, sizeof(DWORD)));

	return (S_OK);
}

/* =============================================================
// A2DBuffer::GetWave()
// (RE) dbg:0x10041030; rtl:0x100034f0
//
// Leave the audio pointer output unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
A2DBuffer::GetWave(LPBYTE *lplpbWave)
{
	ASSERT(lplpbWave != 0);

	return (S_OK);
}

/* =============================================================
// A2DBuffer::GetDriverInfo()
// (RE) dbg:0x10041090; rtl:0x1001b380
//
// Leave driver information outputs unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
A2DBuffer::GetDriverInfo(void **lplpIDsDriverBuffer, void **lplpIA3dDriverBuffer)
{
	ASSERT(lplpIDsDriverBuffer != 0);
	ASSERT(lplpIA3dDriverBuffer != 0);

	return (S_OK);
}

/* =============================================================
// A2DBuffer::SetNewBuffer()
// (RE) dbg:0x10041130; rtl:0x1001b380
//
// Leave audio storage unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
A2DBuffer::SetNewBuffer(LPBYTE lpbWave, DWORD dwBufferBytes)
{
	ASSERT(lpbWave != 0);

	return (S_OK);
}

/* =============================================================
// A2DBuffer::SetA3dDirectCtrl()
// (RE) dbg:0x10041190; rtl:0x1001b380
//
// Leave direct controls unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
A2DBuffer::SetA3dDirectCtrl(LPA3DCTRL_SRC_SUPER lpA3dCtrlDirect, DWORD dwSize)
{
	ASSERT(lpA3dCtrlDirect != 0);

	return (S_OK);
}

/* =============================================================
// A2DBuffer::GetStatusEx()
// (RE) dbg:0x100411f0; rtl:0x100034f0
//
// Leave the extended status output unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
A2DBuffer::GetStatusEx(LPDWORD lpdwStatusEx)
{
	ASSERT(lpdwStatusEx != 0 && !IsBadReadPtr(lpdwStatusEx, sizeof(DWORD)));

	return (S_OK);
}

/* =============================================================
// A2DBuffer::GetPriority()
// (RE) dbg:0x10041260; rtl:0x1000b6f0
//
// Write resource manager priority.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
A2DBuffer::GetPriority(FLOAT *pfPriority)
{
	ASSERT(pfPriority != 0 && !IsBadReadPtr(pfPriority, sizeof(FLOAT)));

	if (!pfPriority)
	{
		DBGSTR("A2DBuffer::GetPriority() - Invalid pointer passed in.\n");

		return (E_POINTER);
	}

	ASSERT(m_fPriority >= 0.0 && m_fPriority <= 1.0);

	*pfPriority = m_fPriority;

	return (S_OK);
}

/* =============================================================
// A2DBuffer::GetBufferState()
// (RE) dbg:0x10041380; rtl:0x1001b390
//
// Write the voice state.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
A2DBuffer::GetBufferState(LPDWORD lpdwBufferState)
{
	ASSERT(lpdwBufferState != 0 && !IsBadReadPtr(lpdwBufferState, sizeof(DWORD)));

	*lpdwBufferState = m_dwState;

	return (S_OK);
}

/* =============================================================
// A2DBuffer::GetWaveSize()
// (RE) dbg:0x10041400; rtl:0x1001b3b0; thunk dbg:0x1000358f
//
// Read the shared wave allocation size.
//
// Returns: Size in bytes; 0 without wave storage.
// =============================================================*/

DWORD
A2DBuffer::GetWaveSize(void)
{
	ASSERT(m_pWaveForm != 0 && !IsBadReadPtr(m_pWaveForm, sizeof(CWaveForm)));

	if (m_pWaveForm)
		return (m_pWaveForm->GetBufferSize());

	return (0);
}

/* =============================================================
// A2DBuffer::SetPriority()
// (RE) dbg:0x100414a0; rtl:0x1001b3d0
//
// Store resource manager priority.
//
// Returns:
//   S_OK
//   E_INVALIDARG  unless priority is in [0, 1]
// =============================================================*/

STDMETHODIMP
A2DBuffer::SetPriority(FLOAT fPriority)
{
	ASSERT(fPriority >= 0.0 && fPriority <= 1.0);

	if (fPriority >= 0.0 && fPriority <= 1.0)
	{
		m_fPriority = fPriority;

		return (S_OK);
	}

	DBGSTR("A2DBuffer::SetPriority() - Invalid priority value sent.\n");

	return (E_INVALIDARG);
}

/* =============================================================
// A2DBuffer::Enable()
// (RE) dbg:0x10041580; rtl:0x1001b410
//
// Set the voice enable flag.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
A2DBuffer::Enable(void)
{
	m_dwFlags |= A3DVOICE_FLAG_ENABLED;

	return (S_OK);
}

/* =============================================================
// A2DBuffer::Disable()
// (RE) dbg:0x100415b0; rtl:0x10022cf0
//
// Clear the voice enable flag.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
A2DBuffer::Disable(void)
{
	m_dwFlags &= ~A3DVOICE_FLAG_ENABLED;

	return (S_OK);
}

/* =============================================================
// A2DBuffer::SetNativeModeDisabled()
// (RE) dbg:0x100415e0; rtl:0x10020b90
//
// Reject the native-mode-disable request
//
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
A2DBuffer::SetNativeModeDisabled(DWORD)
{
	return (E_NOTIMPL);
}

/* =============================================================
// A2DBuffer::Unknown_0x28()
// (RE) dbg:0x10041600; rtl:0x10020b90
//
// Leave buffer state unchanged; the operation is unresolved.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
A2DBuffer::Unknown_0x28(DWORD)
{
	return (E_NOTIMPL);
}

/* =============================================================
// A2DBuffer::GetControlBufferPair()
// (RE) dbg:0x10041620; rtl:0x10020b90
//
// Reject the control-buffer-pair query
//
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
A2DBuffer::GetControlBufferPair(DWORD)
{
	return (E_NOTIMPL);
}

/* =============================================================
// A2DBuffer::GetAllParameters()
// (RE) dbg:0x10041660; rtl:0x10020b90
//
// Leave listener parameter outputs unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
A2DBuffer::GetAllParameters(LPDS3DLISTENER pListener)
{
	return (E_NOTIMPL);
}

/* =============================================================
// A2DBuffer::GetDistanceFactor()
// (RE) dbg:0x10041680; rtl:0x10020b90
//
// Leave the distance factor output unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
A2DBuffer::GetDistanceFactor(D3DVALUE *pflDistanceFactor)
{
	return (E_NOTIMPL);
}

/* =============================================================
// A2DBuffer::GetDopplerFactor()
// (RE) dbg:0x100416a0; rtl:0x10020b90
//
// Leave the Doppler factor output unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
A2DBuffer::GetDopplerFactor(D3DVALUE *pflDopplerFactor)
{
	return (E_NOTIMPL);
}

/* =============================================================
// A2DBuffer::GetOrientation()
// (RE) dbg:0x100416c0; rtl:0x10003500
//
// Leave orientation outputs unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
A2DBuffer::GetOrientation(D3DVECTOR *pvOrientFront, D3DVECTOR *pvOrientTop)
{
	return (E_NOTIMPL);
}

/* =============================================================
// A2DBuffer::GetPosition()
// (RE) dbg:0x100416e0; rtl:0x10020b90
//
// Leave the position output unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
A2DBuffer::GetPosition(D3DVECTOR *pvPosition)
{
	return (E_NOTIMPL);
}

/* =============================================================
// A2DBuffer::GetRolloffFactor()
// (RE) dbg:0x10041700; rtl:0x10020b90
//
// Leave the rolloff factor output unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
A2DBuffer::GetRolloffFactor(D3DVALUE *pflRolloffFactor)
{
	return (E_NOTIMPL);
}

/* =============================================================
// A2DBuffer::GetVelocity()
// (RE) dbg:0x10041720; rtl:0x10020b90
//
// Leave the velocity output unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
A2DBuffer::GetVelocity(D3DVECTOR *pvVelocity)
{
	return (E_NOTIMPL);
}

/* =============================================================
// A2DBuffer::SetAllParameters()
// (RE) dbg:0x10041740; rtl:0x10003500
//
// Leave listener parameters unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
A2DBuffer::SetAllParameters(LPCDS3DLISTENER pcListener, DWORD dwApply)
{
	return (E_NOTIMPL);
}

/* =============================================================
// A2DBuffer::SetDistanceFactor()
// (RE) dbg:0x10041760; rtl:0x10003500
//
// Leave the distance factor unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
A2DBuffer::SetDistanceFactor(D3DVALUE flDistanceFactor, DWORD dwApply)
{
	return (E_NOTIMPL);
}

/* =============================================================
// A2DBuffer::SetDopplerFactor()
// (RE) dbg:0x10041780; rtl:0x10003500
//
// Leave the Doppler factor unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
A2DBuffer::SetDopplerFactor(D3DVALUE flDopplerFactor, DWORD dwApply)
{
	return (E_NOTIMPL);
}

/* =============================================================
// A2DBuffer::SetOrientation()
// (RE) dbg:0x100417a0; rtl:0x1001b430
//
// Leave listener orientation unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
A2DBuffer::SetOrientation(D3DVALUE xFront, D3DVALUE yFront, D3DVALUE zFront,
			  D3DVALUE xTop, D3DVALUE yTop, D3DVALUE zTop, DWORD dwApply)
{
	return (E_NOTIMPL);
}

/* =============================================================
// A2DBuffer::SetPosition()
// (RE) dbg:0x100417c0; rtl:0x1001eef0
//
// Leave listener position unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
A2DBuffer::SetPosition(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD dwApply)
{
	return (E_NOTIMPL);
}

/* =============================================================
// A2DBuffer::SetRolloffFactor()
// (RE) dbg:0x100417e0; rtl:0x10003500
//
// Leave the rolloff factor unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
A2DBuffer::SetRolloffFactor(D3DVALUE flRolloffFactor, DWORD dwApply)
{
	return (E_NOTIMPL);
}

/* =============================================================
// A2DBuffer::SetVelocity()
// (RE) dbg:0x10041800; rtl:0x1001eef0
//
// Leave listener velocity unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
A2DBuffer::SetVelocity(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD dwApply)
{
	return (E_NOTIMPL);
}

/* =============================================================
// A2DBuffer::CommitDeferredSettings()
// (RE) dbg:0x10041820; rtl:0x1001eec0
//
// Leave deferred listener settings unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
A2DBuffer::CommitDeferredSettings(void)
{
	return (E_NOTIMPL);
}

/* =============================================================
// A2DBuffer::Get()
// (RE) dbg:0x10041e10; rtl:0x1001b6c0
//
// Leave property outputs unchanged.
//
// Returns: A3DERROR_BUFFER_IN_SOFTWARE.
// =============================================================*/

STDMETHODIMP
A2DBuffer::Get(REFGUID rguidPropSet, ULONG ulId, LPVOID pInstanceData,
	       ULONG ulInstanceLength, LPVOID pPropertyData, ULONG ulDataLength,
	       PULONG pulBytesReturned)
{
	return (A3DERROR_BUFFER_IN_SOFTWARE);
}

/* =============================================================
// A2DBuffer::Set()
// (RE) dbg:0x10041e30; rtl:0x1001b6d0
//
// Leave property state unchanged.
//
// Returns: A3DERROR_BUFFER_IN_SOFTWARE.
// =============================================================*/

STDMETHODIMP
A2DBuffer::Set(REFGUID rguidPropSet, ULONG ulId, LPVOID pInstanceData,
	       ULONG ulInstanceLength, LPVOID pPropertyData, ULONG ulDataLength)
{
	return (A3DERROR_BUFFER_IN_SOFTWARE);
}

/* =============================================================
// A2DBuffer::QuerySupport()
// (RE) dbg:0x10041df0; rtl:0x10022d80
//
// Leave the property support output unchanged.
//
// Returns: A3DERROR_BUFFER_IN_SOFTWARE.
// =============================================================*/

STDMETHODIMP
A2DBuffer::QuerySupport(REFGUID rguidPropSet, ULONG ulId, PULONG pulTypeSupport)
{
	return (A3DERROR_BUFFER_IN_SOFTWARE);
}

/* =============================================================
// A2DBuffer::SetRate()
// (RE) dbg:0x10041bb0
//
// Set the requested resampler step and cap its target at 10 times the current
// step.
//
// Returns: Uncapped requested step.
// =============================================================*/

DWORD
A2DBuffer::SetRate(DWORD dwRate)
{
DWORD dwStep;
DWORD dwLimit;

	dwStep = (dwRate << 15) / (m_mix.nSampleRate >> 1);

	m_mix.nStepWanted = dwStep;

	if (m_mix.nStep)
	{
		dwLimit = A3D_MIX_MAX_STEP_MULTIPLIER * m_mix.nStep;

		if (dwStep <= dwLimit)
			m_mix.nStepTarget = dwStep;
		else
			m_mix.nStepTarget = dwLimit;
	}
	else
	{
		m_mix.nStep       = dwStep;
		m_mix.nStepTarget = dwStep;
	}

	return (dwStep);
}

/* =============================================================
// A2DBuffer::Init()
// (RE) dbg:0x10041840; rtl:0x1001b440
//
// Allocate shared wave storage and initialize format, resampling and HRTF
// state.
//
// Returns:
//   S_OK
//   E_OUTOFMEMORY  waveform object allocation failure
//   E_FAIL         if wave storage allocation fails
// =============================================================*/

HRESULT
A2DBuffer::Init(const DSBUFFERDESC1 *pDesc, CHrtfMgr *pHrtfMgr)
{
	CopyMemory(&m_DSBufferDesc, pDesc, sizeof(DSBUFFERDESC1));

	m_DSBufferDesc.lpwfxFormat = &m_wfx;

	m_wfx = *pDesc->lpwfxFormat;

	m_dwSampleRate = pDesc->lpwfxFormat->nSamplesPerSec;
	m_dwFrequency  = m_dwSampleRate;

	m_pWaveForm = new CWaveForm;

	if (!m_pWaveForm)
	{
		DBGSTR("A2DBuffer::Init() - Could not allocate memory for WaveForm object.\n");

		return (E_OUTOFMEMORY);
	}

	if (!m_pWaveForm->Create(m_DSBufferDesc.dwBufferBytes))
	{
		DBGSTR("A2dBuffer::Init() - Failed to initialize CWaveForm object.\n");

		return (E_FAIL);
	}

	m_play.pStart   = m_pWaveForm->GetBuffer();
	m_play.pCur     = m_play.pStart;
	m_play.cSamples = m_pWaveForm->GetBufferSize();
	m_play.pEnd     = m_play.pStart + m_play.cSamples;

	m_play.dwFormatBits = 0;

	if (m_wfx.wBitsPerSample == 8)
	{
		m_play.dwFormatBits |= A3D_A2D_FORMAT_EIGHT;
	}
	else
	{
		m_play.dwFormatBits |= A3DMIX_FORMAT_SIXTEEN;
		m_play.cSamples     >>= 1;
	}

	if (m_wfx.nChannels == 1)
	{
		m_play.dwFormatBits |= A3D_A2D_FORMAT_MONO;
	}
	else
	{
		m_play.dwFormatBits |= A3D_A2D_FORMAT_STEREO;
		m_play.cSamples     >>= 1;
	}

	m_pHrtfMgr = pHrtfMgr;

	A3dVoiceReset(&m_mix, &m_play, 22);

	SetRate(m_wfx.nSamplesPerSec);

	return (S_OK);
}

/* =============================================================
// A2DBuffer::InitDuplicate()
// (RE) dbg:0x10041c80; rtl:0x1001B610
//
// Share the original wave storage and copy its playback, resampler and HRTF
// state.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
A2DBuffer::InitDuplicate(A2DBuffer *lpOriginalBuffer)
{
	ASSERT(lpOriginalBuffer != 0 &&
	       !IsBadReadPtr(lpOriginalBuffer, sizeof(IDirectSoundBuffer)));

	lpOriginalBuffer->m_pWaveForm->AddRef();
	m_pWaveForm = lpOriginalBuffer->m_pWaveForm;

	CopyMemory(&m_DSBufferDesc, &lpOriginalBuffer->m_DSBufferDesc,
		   sizeof(DSBUFFERDESC1) + sizeof(WAVEFORMATEX));

	m_DSBufferDesc.lpwfxFormat = &m_wfx;

	m_dwSampleRate = lpOriginalBuffer->m_dwSampleRate;
	m_dwFrequency  = lpOriginalBuffer->m_dwFrequency;
	m_pHrtfMgr     = lpOriginalBuffer->m_pHrtfMgr;

	CopyMemory(&m_mix, &lpOriginalBuffer->m_mix, sizeof(A3DMIXSTATE));
	CopyMemory(&m_play, &lpOriginalBuffer->m_play, sizeof(A3DMIXPLAY));

	return (S_OK);
}
