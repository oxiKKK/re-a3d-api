/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dSource.cpp
 *
 * Implements CA3dSource, the engine's audio source and per-source
 * acoustic calculations. It loads or allocates waveform data, manages
 * static and streaming playback, tracks playback events, and stores
 * position, orientation, gain, pitch and distance controls.
 *
 * The tracing helpers derive direct-path and reflection parameters,
 * including ear direction, delay, attenuation and Doppler, then submit
 * control blocks to resource-manager buffers. WAV loading and the MP3 and
 * AC-3 decoder integration also live here.
 *
 * Applications normally reach this implementation through CA3dSourceCom.
 * The root coordinates tracing, the resource manager assigns playback
 * buffers, and the selected DAL renders their audio.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "A3dSource.h"
#include "a3dsourcecom.h"
#include "A3d3.h"
#include "A3dMatrix.h"
#include "A3dReflection.h"
#include "d2dbuffer.h"
#include "Corners.h"

#include <stdlib.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <string.h>
#include <math.h>
#include <crtdbg.h>

/* Mirror the primary table from rmbuffer.h: its IResManBuffer name conflicts
 * with d2dbuffer.h. Keep this declaration synchronized with rmbuffer.h. */

struct IResManBufferPrimary : public IDirectSoundBuffer
{
	virtual ~IResManBufferPrimary(void) {}			/* 21 */
	virtual HRESULT	 Init(void *lpDesc) = 0;				/* 22 */
	virtual HRESULT	 AttachDalBufferInfo(DalBufferInfo *lpInfo) = 0;	/* 23 */
	virtual HRESULT	 Duplicate(void *lpArg) = 0;			/* 24 */
	virtual HRESULT	 GetDalBufferInfo(DalBufferInfo **lplpInfo) = 0;	/* 25 */
};

/* =============================================================
// Rewind()
// (RE) rtl:0x1000C860; dbg:0x1001EE90
//
// Move the wave cursor to the start.
//
// Returns: The SetWavePosition result.
// =============================================================*/

STDMETHODIMP
CA3dSource::Rewind(void)
{
	return (SetWavePosition(0));
}

/* =============================================================
// SetWaveTime()
// (RE) rtl:0x1000D200; dbg:0x10020080
//
// Set the wave cursor in seconds.
//
// Returns:
//   The SetWavePosition result
//   E_INVALIDARG                        negative time
//   A3DERROR_NEEDS_FORMAT_INFORMATION   without a format
//   A3DERROR_ENCODED_SOURCE_TYPE_CANNOT_BE_TIME_SEEKED
//                                       MP3 or AC-3
// =============================================================*/

STDMETHODIMP
CA3dSource::SetWaveTime(A3DVAL fTime)
{
	if (!m_pwfxFormat)
		return (A3DERROR_NEEDS_FORMAT_INFORMATION);

	if ((m_dwFormat & (A3DSOURCE_FORMAT_MP3 | A3DSOURCE_FORMAT_AC3)) != 0)
		return (A3DERROR_ENCODED_SOURCE_TYPE_CANNOT_BE_TIME_SEEKED);

	if (fTime < 0.0f)
		return (E_INVALIDARG);

	return (SetWavePosition(
		(DWORD) ((double) m_pwfxFormat->nAvgBytesPerSec * fTime)));
}

/* =============================================================
// GetWaveTime()
// (RE) rtl:0x1000D280; dbg:0x10020130
//
// Read the wave cursor in seconds.
//
// Returns:
//   S_OK                                or the GetWavePosition failure
//   E_POINTER                           a null output
//   A3DERROR_NEEDS_FORMAT_INFORMATION   without a format
//   A3DERROR_ENCODED_SOURCE_TYPE_CANNOT_BE_TIME_SEEKED
//                                       MP3 or AC-3
// =============================================================*/

STDMETHODIMP
CA3dSource::GetWaveTime(LPA3DVAL pfTime)
{
DWORD   dwPosition;
HRESULT hr;

	if (!pfTime)
		return (E_POINTER);

	if (!m_pwfxFormat)
		return (A3DERROR_NEEDS_FORMAT_INFORMATION);

	if ((m_dwFormat & (A3DSOURCE_FORMAT_MP3 | A3DSOURCE_FORMAT_AC3)) != 0)
		return (A3DERROR_ENCODED_SOURCE_TYPE_CANNOT_BE_TIME_SEEKED);

	hr = GetWavePosition(&dwPosition);
	if (FAILED(hr))
		return (hr);

	*pfTime = (A3DVAL) ((double) dwPosition /
			     (double) m_pwfxFormat->nAvgBytesPerSec);

	return (S_OK);
}

/* Initial trace state, alternated by each constructor */
static DWORD g_dwNextSourceTraceState = 0;

/* Constructor mix skipped by FlushReverb */
#define A3D_SOURCE_REVERB_MIX_UNSET     (-2.0f)
/* Seed value before StepEnd replaces the mode */
#define A3D_SOURCE_INITIAL_CTRL_MODE    12

/* Source control limits */
#define A3D_SOURCE_DISTANCE_FLOOR               0.000001f
#define A3D_SOURCE_MAX_DISTANCE_FALLBACK        0.000002f
#define A3D_SOURCE_CONE_MAX_DEGREES             180.0f
#define A3D_SOURCE_MIN_PITCH                    0.25f
#define A3D_SOURCE_MAX_PITCH                    4.0f
/* Interior non-mono position offset */
#define A3D_VOLUME_INTERIOR_OFFSET              0.1f
/* Damping test-count limit */
#define A3D_VOLUME_MAX_TEST_PAIRS               6

/* Float encoding in the sqrt approximation */
#define A3D_SQRT_FLOAT_ONE_BITS         0x3F800000
#define A3D_SQRT_FLOAT_TWO_BITS         0x40000000
#define A3D_SQRT_FRACTION_MASK          0x7FFFFF
#define A3D_SQRT_ODD_EXPONENT_BIT       0x800000
#define A3D_SQRT_EXPONENT_BIAS          127

/* Direct-control limits, weights and cadence */
#define A3D_SOURCE_CTRL_ALPHA_MAX               0.99f
#define A3D_SOURCE_AUDIBILITY_EAR_WEIGHT        0.25f
#define A3D_SOURCE_AUDIBILITY_EQ_WEIGHT         0.5f
#define A3D_SOURCE_DP_OFF_POLL_CALLS            100
/* Clears reflection and location status */
#define A3D_SOURCE_STATUS_PRESERVE_MASK         0xFFF8FFF7

/* MP3 allocation and output format */
#define A3D_SOURCE_MP3_INPUT_BYTES      0x2000
#define A3D_SOURCE_MP3_DECODE_BYTES     4608
#define A3D_SOURCE_MP3_PCM_BITS         16

/* AC-3 buffer capacities */
#define A3D_SOURCE_AC3_INPUT_BYTES      3840
#define A3D_SOURCE_AC3_DECODE_BYTES     36864
/* AC-3 parser values  */
#define A3D_AC3_SYNC_BIG_ENDIAN         0x0B77
#define A3D_AC3_SYNC_LITTLE_ENDIAN      0x770B
#define A3D_AC3_FRAME_SIZE_CODE_MASK    0x3F
#define A3D_AC3_SAMPLE_RATE_CODE_MASK   3

/* Streaming wait layout and loop tail */
#define A3D_STREAM_EVENTS_PER_SOURCE            4
#define A3D_STREAM_SERVICE_EVENTS               3
#define A3D_WAVE_LOOP_SILENCE_THRESHOLD_S       0.05

/* (RE) Compiler HRESULT helpers: FAILED dbg:0x1001F470;
 * SUCCEEDED dbg:0x10020050. */

/* =============================================================
// CA3dSource()
// (RE) rtl:0x1000C360; dbg:0x1001E330
//
// Initialize source state and event lists, retaining DirectSound. Writes S_OK
// to phr even when event-list allocation fails.
// =============================================================*/

CA3dSource::CA3dSource(IDirectSound *pDS, CA3dRoot *pApi, CA3dSourceCom *pSourceCom,
		       DWORD dwFlags, HRESULT *phr)
{
	*phr = S_OK;

	m_cRef = 1;

	m_pDS = pDS;
	m_pDS->AddRef();

	m_pApi       = pApi;
	m_pSourceCom = pSourceCom;

	m_pBuffer       = NULL;
	m_pBuffer3D     = NULL;
	m_pAurealBuffer = NULL;
	m_pA3dVoiceCtl  = NULL;

	m_pwfxFormat = NULL;
	m_cbFormat   = 0;
	m_dwWaveSize = 0;
	m_dwFormat   = 0;

	m_fStreaming               = 0;
	m_dwStreamEnded            = 0;
	m_adwStreamSegmentValid[0] = 0;
	m_adwStreamSegmentValid[1] = 0;
	m_adwStreamSegmentValid[2] = 0;
	m_pMp3Decoder              = 0;
	m_pMp3Decode               = NULL;
	m_pAc3Format               = 0;
	m_pAc3Decode               = NULL;
	m_pNotifyPositions         = NULL;
	m_dwVelocitySet            = 0;
	m_adwPlayMark[0]           = 0;
	m_adwPlayMark[1]           = 0;
	m_pOccludingList           = NULL;
	m_dwOccludingElementIndex  = 0;
	m_dwReverbMixDirty         = 0;
	m_fAudibilityWindowMin     = 0.0f;
	m_fAudibilityWindowMax     = 1.0f;
	m_dwVolumetricEnable       = 0;
	m_fInsideVolumetricBounds  = 0;

	m_ahStreamEvents[0] = INVALID_HANDLE_VALUE;
	m_ahStreamEvents[1] = INVALID_HANDLE_VALUE;
	m_ahStreamEvents[2] = INVALID_HANDLE_VALUE;
	m_ahStreamEvents[3] = INVALID_HANDLE_VALUE;
	m_hEventMutex       = INVALID_HANDLE_VALUE;

	m_fRateRatio = 1.0f;

	m_afPanValues[0]        = 1.0f;
	m_afPanValues[1]        = 1.0f;
	m_fGain                 = 1.0f;
	m_fEq                   = 1.0f;
	m_fEqCoefficient        = 1.0f;
	m_fPitch                = 1.0f;
	m_fDopplerScale         = 1.0f;
	m_fDistanceModelScale   = 1.0f;
	m_fReflectionDelayScale = 1.0f;
	m_fReflectionGainScale  = 1.0f;

	m_fMinDistance   = 1.0f * pApi->m_fUnitsPerMeter;
	m_fMaxDistance   = 5000.0f * pApi->m_fUnitsPerMeter;
	m_dwDistanceMode = 0;
	m_fDelay         = 0.0f;
	m_fDoppler       = 1.0f;

	m_afEarGain[0] = 1.0f;
	m_afEarGain[1] = 1.0f;
	m_fEqAmount    = 1.0f;

	m_afOcclusionTarget[0] = 1.0f;
	m_afOcclusionTarget[1] = 1.0f;
	m_afOcclusionValue[0]  = 1.0f;
	m_afOcclusionValue[1]  = 1.0f;
	m_afOcclusionRate[0]   = 0.0f;
	m_afOcclusionRate[1]   = 0.0f;
	m_fOcclusion           = 0.0f;

	m_fConeInnerAngle  = 0.0f;
	m_fConeOuterAngle  = 0.0f;
	m_fConeOutsideGain = 1.0f;
	m_fConeGain        = 1.0f;
	m_fReservedCC      = 1.0f;

	m_fPriority = 0.5f;

	m_dwFlags           = dwFlags;
	m_dwNativeModeDirty = 1;

	ZeroMemory(&m_SourceCaps, sizeof(m_SourceCaps));
	m_SourceCaps.dwSize = sizeof(m_SourceCaps);

	if (dwFlags & A3DSOURCE_INITIAL_RENDERMODE_NATIVE)
		m_dwRenderMode = A3DSOURCE_RENDERMODE_NATIVE;
	else
		m_dwRenderMode = A3DSOURCE_RENDERMODE_DEFAULT;

	m_dwPlayState     = A3D_PLAYSTATE_IDLE;
	m_fLooping        = 0;
	m_dwPlayFlags     = 0;
	m_dwTransformMode = 0;

	m_avReflectionPoint[0] = 0.0f;
	m_avReflectionPoint[1] = 0.0f;
	m_avReflectionPoint[2] = 0.0f;
	m_fReserved38          = 1.0f;

	m_vPosition[0] = 0.0f;
	m_vPosition[1] = 0.0f;
	m_vPosition[2] = 0.0f;
	m_fReserved128 = 1.0f;

	m_vOrientAngles[0] = 0.0f;
	m_vOrientAngles[1] = 0.0f;
	m_vOrientAngles[2] = 0.0f;
	m_dwReserved138    = 0;

	m_vVelocity[0]  = 0.0f;
	m_vVelocity[1]  = 0.0f;
	m_vVelocity[2]  = 0.0f;
	m_dwReserved148 = 0;

	m_vUp[0]        = 0.0f;
	m_vUp[1]        = 1.0f;
	m_vUp[2]        = 0.0f;
	m_dwReserved168 = 0;

	m_vFront[0]     = 0.0f;
	m_vFront[1]     = 0.0f;
	m_vFront[2]     = 1.0f;
	m_dwReserved158 = 0;

	if (!pApi->m_dwCoordSystem)
		m_vFront[2] = -1.0f;

	if (pApi->HasCurrentMatrix())
	{
		CopyMemory(m_matSource, pApi->GetCurrentMatrix(), sizeof(m_matSource));
	}
	else
	{
		ZeroMemory(m_matSource, sizeof(m_matSource));

		m_matSource[0]  = 1.0f;
		m_matSource[5]  = 1.0f;
		m_matSource[10] = 1.0f;
		m_matSource[15] = 1.0f;
	}

	m_apChain[0] = NULL;
	m_apChain[1] = NULL;
	m_acChain[0] = 0;
	m_acChain[1] = 0;
	m_nChain     = 0;

	m_afOcclusionCache[0] = 0.0f;
	m_afOcclusionCache[1] = 1.0f;
	m_afOcclusionCache[2] = 1.0f;

	m_apBuffer[0] = NULL;
	m_apBuffer[1] = NULL;

	m_fTraced                = g_dwNextSourceTraceState;
	m_fManualReflections     = 0;
	g_dwNextSourceTraceState = (g_dwNextSourceTraceState == 0);

	m_dwTraceCount   = 0;
	m_dwOnSourceList = 1;
	m_fOrientAngles  = 1;

	m_avVolumetricOrigin[0] = 0.0f;
	m_avVolumetricOrigin[1] = 0.0f;
	m_avVolumetricOrigin[2] = 0.0f;
	m_avVolumetricBounds[0] = 0.0f;
	m_avVolumetricBounds[1] = 0.0f;
	m_avVolumetricBounds[2] = 0.0f;
	m_adwVolumetricHit[0]   = 0;
	m_adwVolumetricHit[1]   = 0;

	m_VolDampInfo.dwSize         = sizeof(m_VolDampInfo);
	m_VolDampInfo.fAzimuthPan    = 0.5f;
	m_VolDampInfo.fSizeDampMin   = 0.5f;
	m_VolDampInfo.fDampWeighting = 0.7f;
	m_VolDampInfo.nTestPointsMax = A3D_VOLUME_MAX_TEST_PAIRS;
	m_VolDampInfo.bMonoInside    = FALSE;

	m_pReverbPropSet  = NULL;
	m_fReverbMix      = A3D_SOURCE_REVERB_MIX_UNSET;
	m_fReverbDirectHF = 1.0f;

	ZeroMemory(m_adwOcclusionMaterialCache, sizeof(m_adwOcclusionMaterialCache));

	m_pUserWaveEvents    = new CA3dPtrList<A3DPLAYEVENT>();
	m_pStreamWaveEvents  = new CA3dPtrList<A3DPLAYEVENT>();
	m_pManualReflections = NULL;
}

/* =============================================================
// GetWaveSize()
// (RE) rtl:0x10014400; dbg:0x1002DB80
//
// Read the resident or logical streaming audio size.
//
// Returns: The audio byte count.
// =============================================================*/

STDMETHODIMP_(DWORD)
CA3dSource::GetWaveSize(void)
{
	if (m_fStreaming)
		return (m_dwStreamSize);

	return (m_dwWaveSize);
}

/* =============================================================
// GetType()
// (RE) rtl:0x1000C830; dbg:0x1001EE50
//
// Read the source creation flags.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dSource::GetType(LPDWORD pdwType)
{
	if (!pdwType)
		return (E_POINTER);

	*pdwType = m_dwFlags;

	return (S_OK);
}

/* =============================================================
// CA3dSource()
// (RE) rtl:0x1000C8B0; dbg:0x1001EF70
//
// Duplicate the source and its wave buffer, reporting failures through phr.
// Failure leaves a partial object that the original caller leaks.
// =============================================================*/

CA3dSource::CA3dSource(CA3dSource *pSource, CA3dSourceCom *pSourceCom, HRESULT *phr)
{
	*phr = S_OK;

	CopyMemory(this, pSource, sizeof(CA3dSource));

	m_apChain[0] = NULL;
	m_apChain[1] = NULL;
	m_acChain[0] = 0;
	m_acChain[1] = 0;
	m_nChain     = 0;

	m_pDS->AddRef();

	if (FAILED(m_pDS->DuplicateSoundBuffer(pSource->m_pBuffer, &m_pBuffer)))
	{
		*phr = A3DERROR_FAILED_DUPLICATION;

		return;
	}

	m_pBuffer->QueryInterface(IID_IDirectSound3DBuffer, (void **) &m_pBuffer3D);
	m_pBuffer->QueryInterface(IID_A3dVoiceCtl, (void **) &m_pA3dVoiceCtl);

	if (FAILED(m_pBuffer->QueryInterface(IID_IA3dBuffer, (void **) &m_pAurealBuffer)))
		m_pAurealBuffer = NULL;

	m_apBuffer[0] = (LPA3DCTRL_SRC_SUPER) calloc(1, A3D_SOURCE_BUFFER_SIZE);
	m_apBuffer[1] = (LPA3DCTRL_SRC_SUPER) calloc(1, A3D_SOURCE_BUFFER_SIZE);
	m_nBuffer     = 0;
	m_pBufferCurr = m_apBuffer[0];

	A3dSourceSeedDirect(this);

	m_cRef        = 1;
	m_dwPlayState = A3D_PLAYSTATE_IDLE;

	m_fTraced                = g_dwNextSourceTraceState;
	g_dwNextSourceTraceState = (g_dwNextSourceTraceState == 0);

	ZeroMemory(m_adwOcclusionMaterialCache, sizeof(m_adwOcclusionMaterialCache));
	m_pOccludingList          = NULL;
	m_dwOccludingElementIndex = 0;
	m_pNotifyPositions        = NULL;

	if (m_pwfxFormat)
	{
		LPWAVEFORMATEX pwfx;

		pwfx = (LPWAVEFORMATEX) new BYTE[m_cbFormat];
		if (!pwfx)
		{
			DBGSTR("CA3dSource::CA3dSource - Unable to allocate memory for duplicate waveformat.\n");
			*phr = A3DERROR_MEMORY_ALLOCATION;

			return;
		}

		CopyMemory(pwfx, m_pwfxFormat, m_cbFormat);
		m_pwfxFormat = pwfx;
	}

	if (m_SourceCaps.szFilename)
	{
		char *szName;

		szName = new char[strlen(m_SourceCaps.szFilename) + 1];
		if (!szName)
		{
			DBGSTR("CA3dSource::CA3dSource - Unable to allocate memory for duplicate caps filename.\n");
			*phr = A3DERROR_MEMORY_ALLOCATION;

			return;
		}

		CopyMemory(szName, m_SourceCaps.szFilename, strlen(m_SourceCaps.szFilename) + 1);
		m_SourceCaps.szFilename = szName;
	}

	m_pSourceCom     = pSourceCom;
	m_pReverbPropSet = NULL;

	m_pUserWaveEvents    = new CA3dPtrList<A3DPLAYEVENT>();
	m_pStreamWaveEvents  = new CA3dPtrList<A3DPLAYEVENT>();
	m_pManualReflections = NULL;
}

/* =============================================================
// ~CA3dSource()
// (RE) rtl:0x1000CBE0; dbg:0x1001F490
//
// Stop and unlink the source, then release its interfaces, buffers and lists.
// =============================================================*/

CA3dSource::~CA3dSource(void)
{
	Stop();

	if (m_dwOnSourceList)
		m_pApi->RemoveSource(this);

	m_pSourceCom->SourceDestroyed(this);

	FreeAudioData();

	if (m_pDS)
	{
		m_pDS->Release();
		m_pDS = NULL;
	}

	A3dSourceRecalc(this);
	A3dSourceRecalc(this);

	if (m_pwfxFormat)
	{
		delete m_pwfxFormat;
		m_pwfxFormat = NULL;
	}

	if (m_pApi->m_dwUseDalInterface)
	{
		if (m_apBuffer[0])
			free(m_apBuffer[0]);

		if (m_apBuffer[1])
			free(m_apBuffer[1]);
	}

	FreeManualReflections();

	if (m_pUserWaveEvents)
	{
		delete m_pUserWaveEvents;
		m_pUserWaveEvents = NULL;
	}

	if (m_pStreamWaveEvents)
	{
		delete m_pStreamWaveEvents;
		m_pStreamWaveEvents = NULL;
	}

	if (m_pNotifyPositions)
	{
		operator delete(m_pNotifyPositions);
		m_pNotifyPositions = NULL;
	}
}

/* =============================================================
// QueryInterface()
// (RE) rtl:0x1000CDF0; dbg:0x1001F6F0
//
// Return a referenced source interface or delegate IA3dPropertySet to the
// buffer.
//
// Returns:
//   S_OK                   or the buffer query result
//   E_POINTER              a null output
//   E_NOINTERFACE          other IIDs
//   A3DERROR_NO_WAVE_DATA  when the property-set query has no buffer
// =============================================================*/

STDMETHODIMP
CA3dSource::QueryInterface(REFIID riid, void **ppv)
{
	if (!ppv)
		return (E_POINTER);

	if (IsEqualIID(riid, IID_IUnknown))
	{
		*ppv = (IA3dSource2 *) this;
	}
	else if (IsEqualIID(riid, IID_IA3dSource) ||
		 IsEqualIID(riid, IID_IA3dSource2))
	{
		*ppv = (IA3dSource2 *) this;
	}
	else if (IsEqualIID(riid, IID_IA3dSrcPrv))
	{
		*ppv = (IA3dSrcPrv *) this;
	}
	else if (IsEqualIID(riid, IID_IA3dPropertySet))
	{
		if (!m_pBuffer)
		{
			*ppv = NULL;

			return (A3DERROR_NO_WAVE_DATA);
		}

		return (m_pBuffer->QueryInterface(IID_IA3dPropertySet, ppv));
	}
	else
	{
		*ppv = NULL;

		return (E_NOINTERFACE);
	}

	((IUnknown *) *ppv)->AddRef();

	return (S_OK);
}

/* =============================================================
// AddRef()
// (RE) rtl:0x1000CF00; dbg:0x1001F860
//
// Increment the reference count.
//
// Returns: The new reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dSource::AddRef(void)
{
	return (++m_cRef);
}

/* =============================================================
// Release()
// (RE) rtl:0x1000CF20; dbg:0x1001F890
//
// Drop a reference and delete the source at zero.
//
// Returns: The remaining reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dSource::Release(void)
{
	if (--m_cRef)
		return (m_cRef);

	delete this;

	return (0);
}

/* =============================================================
// A3dSourceSeedDirect()
// (RE) rtl:0x1000CF50; dbg:0x1001FB10
//
// Reset and submit the direct-path control values without clearing the
// 0x40-byte reflection slot-ID table.
// =============================================================*/

void
A3dSourceSeedDirect(CA3dSource *pSource)
{
LPA3DCTRL_SRC_SUPER pCtrl;

	pCtrl = pSource->m_pBufferCurr;

	pCtrl->dwReserved00 = 0;
	pCtrl->dwMode       = A3D_SOURCE_INITIAL_CTRL_MODE;
	pCtrl->bExecuted    = TRUE;
	pCtrl->fPitch       = 1.0f;
	pCtrl->fAlpha       = 0.0f;
	pCtrl->fFreqFactor  = 1.0f;
	pCtrl->fNative      = 0.0f;

	pCtrl->LeftEar.fAzim  = 0.0f;
	pCtrl->LeftEar.fElev  = 0.0f;
	pCtrl->LeftEar.fGain  = 1.0f;
	pCtrl->LeftEar.fDelay = 0.0f;

	pCtrl->RightEar.fAzim  = 0.0f;
	pCtrl->RightEar.fElev  = 0.0f;
	pCtrl->RightEar.fGain  = 1.0f;
	pCtrl->RightEar.fDelay = 0.0f;

	pCtrl->fPriority    = 1.0f;
	pCtrl->fAudibility  = 1.0f;
	pCtrl->fDistance    = 0.0f;
	pCtrl->fReserved48  = 0.0f;

	A3dSourceStepEnd(pSource, 1);
}

/* =============================================================
// SetWavePosition()
// (RE) rtl:0x1000D030; dbg:0x1001FCD0
//
// Set the audio cursor in bytes.
//
// Returns: The SetPlayPosition result.
// =============================================================*/

STDMETHODIMP
CA3dSource::SetWavePosition(DWORD dwPosition)
{
	return (SetPlayPosition(dwPosition));
}

/* =============================================================
// GetWavePosition()
// (RE) rtl:0x1000D0B0; dbg:0x1001FD90
//
// Read the audio cursor in bytes.
//
// Returns: The GetPlayPosition result.
// =============================================================*/

STDMETHODIMP
CA3dSource::GetWavePosition(LPDWORD pdwPosition)
{
	return (GetPlayPosition(pdwPosition));
}

/* =============================================================
// SetPosition3f()
// (RE) rtl:0x1000D310; dbg:0x10020200
//
// Set the source position.
//
// Returns: The SetPosition3fv result.
// =============================================================*/

STDMETHODIMP
CA3dSource::SetPosition3f(A3DVAL x, A3DVAL y, A3DVAL z)
{
A3DVAL v[3];

	v[0] = x;
	v[1] = y;
	v[2] = z;

	return (SetPosition3fv(v));
}

/* =============================================================
// SetPosition3fv()
// (RE) rtl:0x1000D3B0; dbg:0x100202E0
//
// Set the source position.
//
// Returns:
//   S_OK
//   E_POINTER                       a null vector
//   A3DERROR_SOURCE_IN_NATIVE_MODE  after storing the value in native mode
// =============================================================*/

STDMETHODIMP
CA3dSource::SetPosition3fv(LPA3DVAL pv)
{
	if (!pv)
		return (E_POINTER);

	m_vPosition[0] = pv[0];
	m_vPosition[1] = pv[1];
	m_vPosition[2] = pv[2];

	if (m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE)
		return (A3DERROR_SOURCE_IN_NATIVE_MODE);

	return (S_OK);
}

/* =============================================================
// GetPosition3f()
// (RE) rtl:0x1000D340; dbg:0x10020250
//
// Read the source position.
//
// Returns:
//   S_OK
//   E_POINTER                       a null output
//   A3DERROR_SOURCE_IN_NATIVE_MODE  after writing the output in native mode
// =============================================================*/

STDMETHODIMP
CA3dSource::GetPosition3f(LPA3DVAL px, LPA3DVAL py, LPA3DVAL pz)
{
A3DVAL v[3];

	if (!px || !py || !pz)
		return (E_POINTER);

	GetPosition3fv(v);

	*px = v[0];
	*py = v[1];
	*pz = v[2];

	if (m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE)
		return (A3DERROR_SOURCE_IN_NATIVE_MODE);

	return (S_OK);
}

/* =============================================================
// GetPosition3fv()
// (RE) rtl:0x1000D400; dbg:0x10020360
//
// Read the source position.
//
// Returns:
//   S_OK
//   E_POINTER                       a null output
//   A3DERROR_SOURCE_IN_NATIVE_MODE  after writing the output in native mode
// =============================================================*/

STDMETHODIMP
CA3dSource::GetPosition3fv(LPA3DVAL pv)
{
	if (!pv)
		return (E_POINTER);

	pv[0] = m_vPosition[0];
	pv[1] = m_vPosition[1];
	pv[2] = m_vPosition[2];

	if (m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE)
		return (A3DERROR_SOURCE_IN_NATIVE_MODE);

	return (S_OK);
}

/* =============================================================
// SetOrientationAngles3f()
// (RE) rtl:0x1000D450; dbg:0x100203E0
//
// Set source heading, pitch and roll in degrees.
//
// Returns: The SetOrientationAngles3fv result.
// =============================================================*/

STDMETHODIMP
CA3dSource::SetOrientationAngles3f(A3DVAL h, A3DVAL p, A3DVAL r)
{
A3DVAL v[3];

	v[0] = h;
	v[1] = p;
	v[2] = r;

	return (SetOrientationAngles3fv(v));
}

/* =============================================================
// SetOrientationAngles3fv()
// (RE) rtl:0x1000D4F0; dbg:0x100204C0
//
// Store heading, pitch and roll in degrees as the authoritative orientation.
//
// Returns:
//   S_OK
//   E_POINTER                       a null vector
//   A3DERROR_SOURCE_IN_NATIVE_MODE  after storing the value in native mode
// =============================================================*/

STDMETHODIMP
CA3dSource::SetOrientationAngles3fv(LPA3DVAL pv)
{
	if (!pv)
		return (E_POINTER);

	m_vOrientAngles[0] = pv[0];
	m_vOrientAngles[1] = pv[1];
	m_vOrientAngles[2] = pv[2];

	m_fOrientAngles = 1;

	if (m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE)
		return (A3DERROR_SOURCE_IN_NATIVE_MODE);

	return (S_OK);
}

/* =============================================================
// GetOrientationAngles3f()
// (RE) rtl:0x1000D480; dbg:0x10020430
//
// Read the stored source orientation angles.
//
// Returns:
//   S_OK
//   E_POINTER                       a null output
//   A3DERROR_SOURCE_IN_NATIVE_MODE  after writing the output in native mode
// =============================================================*/

STDMETHODIMP
CA3dSource::GetOrientationAngles3f(LPA3DVAL ph, LPA3DVAL pp, LPA3DVAL pr)
{
A3DVAL v[3];

	if (!ph || !pp || !pr)
		return (E_POINTER);

	GetOrientationAngles3fv(v);

	*ph = v[0];
	*pp = v[1];
	*pr = v[2];

	if (m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE)
		return (A3DERROR_SOURCE_IN_NATIVE_MODE);

	return (S_OK);
}

/* =============================================================
// GetOrientationAngles3fv()
// (RE) rtl:0x1000D540; dbg:0x10020550
//
// Read the stored source orientation angles.
//
// Returns:
//   S_OK
//   E_POINTER                       a null output
//   A3DERROR_SOURCE_IN_NATIVE_MODE  after writing the output in native mode
// =============================================================*/

STDMETHODIMP
CA3dSource::GetOrientationAngles3fv(LPA3DVAL pv)
{
	if (!pv)
		return (E_POINTER);

	pv[0] = m_vOrientAngles[0];
	pv[1] = m_vOrientAngles[1];
	pv[2] = m_vOrientAngles[2];

	if (m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE)
		return (A3DERROR_SOURCE_IN_NATIVE_MODE);

	return (S_OK);
}

/* =============================================================
// SetOrientation6f()
// (RE) rtl:0x1000D590; dbg:0x100205D0
//
// Set the source front and up vectors.
//
// Returns: The SetOrientation6fv result.
// =============================================================*/

STDMETHODIMP
CA3dSource::SetOrientation6f(A3DVAL fx, A3DVAL fy, A3DVAL fz,
			     A3DVAL ux, A3DVAL uy, A3DVAL uz)
{
A3DVAL v[6];

	v[0] = fx;
	v[1] = fy;
	v[2] = fz;
	v[3] = ux;
	v[4] = uy;
	v[5] = uz;

	return (SetOrientation6fv(v));
}

/* =============================================================
// SetOrientation6fv()
// (RE) rtl:0x1000D660; dbg:0x100206F0
//
// Store front and up vectors and derive angles, forcing roll to zero.
//
// Returns:
//   S_OK
//   E_POINTER                       a null vector
//   A3DERROR_SOURCE_IN_NATIVE_MODE  after storing the value in native mode
// =============================================================*/

STDMETHODIMP
CA3dSource::SetOrientation6fv(LPA3DVAL pv)
{
A3DVAL aAngles[3];

	if (!pv)
		return (E_POINTER);

	m_vFront[0] = pv[0];
	m_vFront[1] = pv[1];
	m_vFront[2] = pv[2];

	m_vUp[0] = pv[3];
	m_vUp[1] = pv[4];
	m_vUp[2] = pv[5];

	A3dVectorsToAngles(m_vFront, m_vUp, aAngles, m_pApi->m_dwCoordSystem);

	m_vOrientAngles[0] = aAngles[0];
	m_vOrientAngles[1] = aAngles[1];
	m_vOrientAngles[2] = 0.0f;

	m_fOrientAngles = 0;

	if (m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE)
		return (A3DERROR_SOURCE_IN_NATIVE_MODE);

	return (S_OK);
}

/* =============================================================
// GetOrientation6f()
// (RE) rtl:0x1000D5D0; dbg:0x10020630
//
// Read the source front and up vectors.
//
// Returns:
//   S_OK
//   E_POINTER                       a null output
//   A3DERROR_SOURCE_IN_NATIVE_MODE  after writing the output in native mode
// =============================================================*/

STDMETHODIMP
CA3dSource::GetOrientation6f(LPA3DVAL pfx, LPA3DVAL pfy, LPA3DVAL pfz,
			     LPA3DVAL pux, LPA3DVAL puy, LPA3DVAL puz)
{
A3DVAL v[6];

	if (!pfx || !pfy || !pfz || !pux || !puy || !puz)
		return (E_POINTER);

	GetOrientation6fv(v);

	*pfx = v[0];
	*pfy = v[1];
	*pfz = v[2];
	*pux = v[3];
	*puy = v[4];
	*puz = v[5];

	if (m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE)
		return (A3DERROR_SOURCE_IN_NATIVE_MODE);

	return (S_OK);
}

/* =============================================================
// GetOrientation6fv()
// (RE) rtl:0x1000D710; dbg:0x10020820
//
// Read front and up vectors, rebuilding them from angles when needed.
//
// Returns:
//   S_OK
//   E_POINTER                       a null output
//   A3DERROR_SOURCE_IN_NATIVE_MODE  after writing the output in native mode
// =============================================================*/

STDMETHODIMP
CA3dSource::GetOrientation6fv(LPA3DVAL pv)
{
	if (!pv)
		return (E_POINTER);

	if (m_fOrientAngles)
		A3dAnglesToVectors(m_vOrientAngles, m_vFront, m_vUp,
				   m_pApi->m_dwCoordSystem);

	pv[0] = m_vFront[0];
	pv[1] = m_vFront[1];
	pv[2] = m_vFront[2];
	pv[3] = m_vUp[0];
	pv[4] = m_vUp[1];
	pv[5] = m_vUp[2];

	if (m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE)
		return (A3DERROR_SOURCE_IN_NATIVE_MODE);

	return (S_OK);
}

/* =============================================================
// SetVelocity3f()
// (RE) rtl:0x1000D7B0; dbg:0x10020930
//
// Set the source velocity.
//
// Returns: The SetVelocity3fv result.
// =============================================================*/

STDMETHODIMP
CA3dSource::SetVelocity3f(A3DVAL x, A3DVAL y, A3DVAL z)
{
A3DVAL v[3];

	v[0] = x;
	v[1] = y;
	v[2] = z;

	return (SetVelocity3fv(v));
}

/* =============================================================
// GetVelocity3f()
// (RE) rtl:0x1000D7E0; dbg:0x10020980
//
// Read the source velocity.
//
// Returns:
//   S_OK
//   E_POINTER                       a null output
//   A3DERROR_SOURCE_IN_NATIVE_MODE  after writing the output in native mode
// =============================================================*/

STDMETHODIMP
CA3dSource::GetVelocity3f(LPA3DVAL px, LPA3DVAL py, LPA3DVAL pz)
{
A3DVAL v[3];

	if (!px || !py || !pz)
		return (E_POINTER);

	GetVelocity3fv(v);

	*px = v[0];
	*py = v[1];
	*pz = v[2];

	if (m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE)
		return (A3DERROR_SOURCE_IN_NATIVE_MODE);

	return (S_OK);
}

/* =============================================================
// SetVelocity3fv()
// (RE) rtl:0x1000D850; dbg:0x10020A10
//
// Store the source velocity and mark it as explicitly set.
//
// Returns:
//   S_OK
//   E_POINTER                       a null vector
//   A3DERROR_SOURCE_IN_NATIVE_MODE  after storing the value in native mode
// =============================================================*/

STDMETHODIMP
CA3dSource::SetVelocity3fv(LPA3DVAL pv)
{
	if (!pv)
		return (E_POINTER);

	m_vVelocity[0] = pv[0];
	m_vVelocity[1] = pv[1];
	m_vVelocity[2] = pv[2];

	m_dwVelocitySet = 1;

	if (m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE)
		return (A3DERROR_SOURCE_IN_NATIVE_MODE);

	return (S_OK);
}

/* =============================================================
// GetVelocity3fv()
// (RE) rtl:0x1000D8A0; dbg:0x10020AA0
//
// Read the source velocity.
//
// Returns:
//   S_OK
//   E_POINTER                       a null output
//   A3DERROR_SOURCE_IN_NATIVE_MODE  after writing the output in native mode
// =============================================================*/

STDMETHODIMP
CA3dSource::GetVelocity3fv(LPA3DVAL pv)
{
	if (!pv)
		return (E_POINTER);

	pv[0] = m_vVelocity[0];
	pv[1] = m_vVelocity[1];
	pv[2] = m_vVelocity[2];

	if (m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE)
		return (A3DERROR_SOURCE_IN_NATIVE_MODE);

	return (S_OK);
}

/* =============================================================
// SetMinMaxDistance()
// (RE) rtl:0x1000D8F0; dbg:0x10020B20
//
// Set the distance bounds and mode.
//
// Returns:
//   S_OK
//   E_INVALIDARG                    min >= max on IA3d5 or later
//   A3DERROR_SOURCE_IN_NATIVE_MODE  after storing the values in native mode
// =============================================================*/

STDMETHODIMP
CA3dSource::SetMinMaxDistance(A3DVAL fMin, A3DVAL fMax, DWORD dwMode)
{
	if (m_pApi->m_dwInterfaceVersion > 4 && fMin >= fMax)
		return (E_INVALIDARG);

	if (fMin <= A3D_SOURCE_DISTANCE_FLOOR)
		m_fMinDistance = A3D_SOURCE_DISTANCE_FLOOR;
	else
		m_fMinDistance = fMin;

	if (fMax <= A3D_SOURCE_DISTANCE_FLOOR)
		m_fMaxDistance = A3D_SOURCE_MAX_DISTANCE_FALLBACK;
	else
		m_fMaxDistance = fMax;

	m_dwDistanceMode = dwMode;

	if (m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE)
		return (A3DERROR_SOURCE_IN_NATIVE_MODE);

	return (S_OK);
}

/* =============================================================
// GetMinMaxDistance()
// (RE) rtl:0x1000D980; dbg:0x10020C00
//
// Read the distance bounds and mode.
//
// Returns:
//   S_OK
//   E_POINTER                       a null output
//   A3DERROR_SOURCE_IN_NATIVE_MODE  after writing the output in native mode
// =============================================================*/

STDMETHODIMP
CA3dSource::GetMinMaxDistance(LPA3DVAL pfMin, LPA3DVAL pfMax, LPDWORD pdwMode)
{
	if (!pfMin || !pfMax || !pdwMode)
		return (E_POINTER);

	*pfMin   = m_fMinDistance;
	*pfMax   = m_fMaxDistance;
	*pdwMode = m_dwDistanceMode;

	if (m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE)
		return (A3DERROR_SOURCE_IN_NATIVE_MODE);

	return (S_OK);
}

/* =============================================================
// SetCone()
// (RE) rtl:0x1000D9E0; dbg:0x10020C90
//
// Set cone angles in degrees and the outside gain.
//
// Returns:
//   S_OK
//   E_INVALIDARG                    gain outside 0..1, angles outside 0..180,
//                                   or inner > outer
//   A3DERROR_SOURCE_IN_NATIVE_MODE  after storing in native mode
// =============================================================*/

STDMETHODIMP
CA3dSource::SetCone(A3DVAL fInnerAngle, A3DVAL fOuterAngle, A3DVAL fOutsideGain)
{
	if (fOutsideGain < 0.0f || fOutsideGain > 1.0f)
		return (E_INVALIDARG);

	if (fInnerAngle > fOuterAngle)
		return (E_INVALIDARG);

	if (fInnerAngle < 0.0f || fInnerAngle > A3D_SOURCE_CONE_MAX_DEGREES)
		return (E_INVALIDARG);

	if (fOuterAngle < 0.0f || fOuterAngle > A3D_SOURCE_CONE_MAX_DEGREES)
		return (E_INVALIDARG);

	m_fConeInnerAngle  = fInnerAngle;
	m_fConeOuterAngle  = fOuterAngle;
	m_fConeOutsideGain = fOutsideGain;

	if (m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE)
		return (A3DERROR_SOURCE_IN_NATIVE_MODE);

	return (S_OK);
}

/* =============================================================
// GetCone()
// (RE) rtl:0x1000DA90; dbg:0x10020DB0
//
// Read the cone angles and outside gain.
//
// Returns:
//   S_OK
//   E_POINTER                       a null output
//   A3DERROR_SOURCE_IN_NATIVE_MODE  after writing the output in native mode
// =============================================================*/

STDMETHODIMP
CA3dSource::GetCone(LPA3DVAL pfInnerAngle, LPA3DVAL pfOuterAngle, LPA3DVAL pfOutsideGain)
{
	if (!pfInnerAngle || !pfOuterAngle || !pfOutsideGain)
		return (E_POINTER);

	*pfInnerAngle   = m_fConeInnerAngle;
	*pfOuterAngle   = m_fConeOuterAngle;
	*pfOutsideGain  = m_fConeOutsideGain;

	if (m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE)
		return (A3DERROR_SOURCE_IN_NATIVE_MODE);

	return (S_OK);
}

/* =============================================================
// SetGain()
// (RE) rtl:0x1000DAF0; dbg:0x10020E40
//
// Set the source gain.
//
// Returns:
//   S_OK
//   E_INVALIDARG  outside 0..1
// =============================================================*/

STDMETHODIMP
CA3dSource::SetGain(A3DVAL fGain)
{
	if (fGain < 0.0f || fGain > 1.0f)
		return (E_INVALIDARG);

	m_fGain = fGain;

	return (S_OK);
}

/* =============================================================
// GetGain()
// (RE) rtl:0x1000DB30; dbg:0x10020EA0
//
// Read the source gain.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dSource::GetGain(LPA3DVAL pfGain)
{
	if (!pfGain)
		return (E_POINTER);

	*pfGain = m_fGain;

	return (S_OK);
}

/* =============================================================
// SetPitch()
// (RE) rtl:0x1000DC50; dbg:0x10021080
//
// Set the source pitch factor.
//
// Returns:
//   S_OK
//   E_INVALIDARG  outside 0.25..4
// =============================================================*/

STDMETHODIMP
CA3dSource::SetPitch(A3DVAL fPitch)
{
	if (fPitch < A3D_SOURCE_MIN_PITCH || fPitch > A3D_SOURCE_MAX_PITCH)
		return (E_INVALIDARG);

	m_fPitch = fPitch;

	return (S_OK);
}

/* =============================================================
// GetPitch()
// (RE) rtl:0x1000DC90; dbg:0x100210E0
//
// Read the source pitch factor.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dSource::GetPitch(LPA3DVAL pfPitch)
{
	if (!pfPitch)
		return (E_POINTER);

	*pfPitch = m_fPitch;

	return (S_OK);
}

/* =============================================================
// SetEq()
// (RE) rtl:0x1000DCC0; dbg:0x10021120
//
// Store equalisation and compute the renderer coefficient.
//
// Returns:
//   S_OK
//   E_INVALIDARG                    outside 0..1
//   A3DERROR_SOURCE_IN_NATIVE_MODE  after storing in native mode
// =============================================================*/

STDMETHODIMP
CA3dSource::SetEq(A3DVAL fEq)
{
A3DVAL x2, x3, x4, x5, x6, x7;
A3DVAL fCurve;

	if (fEq < 0.0f || fEq > 1.0f)
		return (E_INVALIDARG);

	x2 = fEq * fEq;
	x3 = fEq * x2;
	x4 = fEq * x3;
	x5 = fEq * x4;
	x6 = fEq * x5;
	x7 = fEq * x6;

	m_fEq = fEq;

	fCurve = fEq * 3.7264376f
	       - x2  * 17.449827f
	       + x3  * 43.63063f
	       - x4  * 52.999165f
	       + x5  * 27.087683f
	       + x6  * 0.77741671f
	       - x7  * 3.7731748f;

	m_fEqCoefficient = fCurve;

	if (fCurve < 0.0f)
		m_fEqCoefficient = 0.0f;
	else if (fCurve > 1.0f)
		m_fEqCoefficient = 1.0f;

	if (m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE)
		return (A3DERROR_SOURCE_IN_NATIVE_MODE);

	return (S_OK);
}

/* =============================================================
// GetEq()
// (RE) rtl:0x1000DDC0; dbg:0x100212B0
//
// Read the requested equalisation.
//
// Returns:
//   S_OK
//   E_POINTER                       a null output
//   A3DERROR_SOURCE_IN_NATIVE_MODE  after writing the output in native mode
// =============================================================*/

STDMETHODIMP
CA3dSource::GetEq(LPA3DVAL pfEq)
{
	if (!pfEq)
		return (E_POINTER);

	*pfEq = m_fEq;

	if (m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE)
		return (A3DERROR_SOURCE_IN_NATIVE_MODE);

	return (S_OK);
}

/* =============================================================
// SetDopplerScale()
// (RE) rtl:0x1000DDF0; dbg:0x10021310
//
// Set the Doppler scale factor.
//
// Returns:
//   S_OK
//   E_INVALIDARG                    a negative scale
//   A3DERROR_SOURCE_IN_NATIVE_MODE  after storing in native mode
// =============================================================*/

STDMETHODIMP
CA3dSource::SetDopplerScale(A3DVAL fScale)
{
	if (fScale < 0.0f)
		return (E_INVALIDARG);

	m_fDopplerScale = fScale;

	if (m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE)
		return (A3DERROR_SOURCE_IN_NATIVE_MODE);

	return (S_OK);
}

/* =============================================================
// GetDopplerScale()
// (RE) rtl:0x1000DE30; dbg:0x10021370
//
// Read the Doppler scale factor.
//
// Returns:
//   S_OK
//   E_POINTER                       a null output
//   A3DERROR_SOURCE_IN_NATIVE_MODE  after writing the output in native mode
// =============================================================*/

STDMETHODIMP
CA3dSource::GetDopplerScale(LPA3DVAL pfScale)
{
	if (!pfScale)
		return (E_POINTER);

	*pfScale = m_fDopplerScale;

	if (m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE)
		return (A3DERROR_SOURCE_IN_NATIVE_MODE);

	return (S_OK);
}

/* =============================================================
// SetDistanceModelScale()
// (RE) rtl:0x1000DE60; dbg:0x100213D0
//
// Set the distance-model scale factor.
//
// Returns:
//   S_OK
//   E_INVALIDARG                    a negative scale
//   A3DERROR_SOURCE_IN_NATIVE_MODE  after storing in native mode
// =============================================================*/

STDMETHODIMP
CA3dSource::SetDistanceModelScale(A3DVAL fScale)
{
	if (fScale < 0.0f)
		return (E_INVALIDARG);

	m_fDistanceModelScale = fScale;

	if (m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE)
		return (A3DERROR_SOURCE_IN_NATIVE_MODE);

	return (S_OK);
}

/* =============================================================
// GetDistanceModelScale()
// (RE) rtl:0x1000DEA0; dbg:0x10021430
//
// Read the distance-model scale factor.
//
// Returns:
//   S_OK
//   E_POINTER                       a null output
//   A3DERROR_SOURCE_IN_NATIVE_MODE  after writing the output in native mode
// =============================================================*/

STDMETHODIMP
CA3dSource::GetDistanceModelScale(LPA3DVAL pfScale)
{
	if (!pfScale)
		return (E_POINTER);

	*pfScale = m_fDistanceModelScale;

	if (m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE)
		return (A3DERROR_SOURCE_IN_NATIVE_MODE);

	return (S_OK);
}

/* =============================================================
// SetRenderMode()
// (RE) rtl:0x1000DED0; dbg:0x10021490
//
// Set rendering flags and mark native-mode changes for the renderer.
//
// Returns:
//   S_OK
//   A3DERROR_UNMANAGED_BUFFER  when an unmanaged source changes its native bit
//   E_INVALIDARG               unsupported flags on IA3d5 or later
// =============================================================*/

STDMETHODIMP
CA3dSource::SetRenderMode(DWORD dwMode)
{
DWORD dwOld;

	if ((m_dwFlags & A3DSOURCE_TYPEUNMANAGED) &&
	    ((dwMode ^ m_dwRenderMode) & A3DSOURCE_RENDERMODE_NATIVE))
		return (A3DERROR_UNMANAGED_BUFFER);

	if (m_pApi->m_dwInterfaceVersion > 4 &&
	    (dwMode & ~(A3DSOURCE_RENDERMODE_MONO |
			A3DSOURCE_RENDERMODE_1ST_REFLECTIONS |
			A3DSOURCE_RENDERMODE_OCCLUSIONS |
			A3DSOURCE_RENDERMODE_NATIVE)))
	{
		DBGSTR("CA3dSource::SetRenderMode() - Invalid argument.\n");
		return (E_INVALIDARG);
	}

	dwOld           = m_dwRenderMode;
	m_dwRenderMode  = dwMode;

	if ((dwOld ^ m_dwRenderMode) & A3DSOURCE_RENDERMODE_NATIVE)
		m_dwNativeModeDirty = 1;

	return (S_OK);
}

/* =============================================================
// GetRenderMode()
// (RE) rtl:0x1000DF40; dbg:0x10021570
//
// Read the rendering flags.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dSource::GetRenderMode(LPDWORD pdwMode)
{
	if (!pdwMode)
		return (E_POINTER);

	*pdwMode = m_dwRenderMode;

	return (S_OK);
}

/* =============================================================
// GetAudibility()
// (RE) rtl:0x1000DF70; dbg:0x100215B0
//
// Read audibility from the previous control block.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dSource::GetAudibility(LPA3DVAL pfAudibility)
{
LPA3DCTRL_SRC_SUPER pCtrl;

	if (!pfAudibility)
		return (E_POINTER);

	pCtrl = m_nBuffer ? m_apBuffer[0] : m_apBuffer[1];

	*pfAudibility = pCtrl->fAudibility;

	return (S_OK);
}

/* =============================================================
// GetOcclusionFactor()
// (RE) rtl:0x1000DFB0; dbg:0x10021610
//
// Read the third occlusion gain.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dSource::GetOcclusionFactor(LPA3DVAL pfFactor)
{
	if (!pfFactor)
		return (E_POINTER);

	*pfFactor = m_fOcclusion;

	return (S_OK);
}

void A3dTraceTransformDir(const A3DVAL *pvDir, const A3DVAL *pm,
			  A3DVAL *pvOut);
void A3dSourceVolumetric(CA3dSource *pSource, A3DVAL *pm);

BOOL A3dSegmentHitsBox(const A3DVAL *pvMin, const A3DVAL *pvMax,
		       const A3DVAL *pvFrom, const A3DVAL *pvDir);

/* =============================================================
// A3dSourceGetMatrix()
// (RE) rtl:0x1000DFE0; dbg:0x10021650; thunk dbg:0x10004287
//
// Build the source frame and apply volumetric positioning when enabled.
// =============================================================*/

void
A3dSourceGetMatrix(CA3dSource *pSource, A3DVAL *pm)
{
A3DVAL vAxis[3];
A3DVAL vPos[4];

	if (pSource->m_dwTransformMode == A3DSOURCE_TRANSFORMMODE_HEADRELATIVE)
	{
		A3dMatrixMultiply(pSource->m_pApi->m_matListenerXform,
				  pSource->m_matSource, pm);

		if (pSource->m_pApi->m_dwCoordSystem)
		{
			vPos[0] = pSource->m_vPosition[0];
			vPos[1] = pSource->m_vPosition[1];
			vPos[2] = -1.0f * pSource->m_vPosition[2];
			vPos[3] = 0.0f;

			A3dMatrixTranslateBy(vPos, pm);
		}
		else
		{
			A3dMatrixTranslateBy(pSource->m_vPosition, pm);
		}
	}
	else
	{
		CopyMemory(pm, pSource->m_matSource, sizeof(pSource->m_matSource));

		A3dMatrixTranslateBy(pSource->m_vPosition, pm);
	}

	vAxis[0] = 0.0f;
	vAxis[1] = 1.0f;
	vAxis[2] = 0.0f;

	A3dMatrixRotateBy(pSource->m_vOrientAngles[0], vAxis, pm);

	vAxis[0] = 1.0f;
	vAxis[1] = 0.0f;

	A3dMatrixRotateBy(pSource->m_vOrientAngles[1], vAxis, pm);

	vAxis[0] = 0.0f;
	vAxis[2] = 1.0f;

	A3dMatrixRotateBy(pSource->m_vOrientAngles[2], vAxis, pm);

	if (pSource->m_dwVolumetricEnable)
	{
		A3dSourceVolumetric(pSource, pm);
	}
	else
	{
		pSource->m_fInsideVolumetricBounds = 0.0f;
		pSource->m_adwVolumetricHit[0]     = 0;
	}
}

/* =============================================================
// A3dVolumetricCoverage()
// (RE) rtl:0x1000E3E0; dbg:0x100218C0; thunk dbg:0x100023AB
//
// Measure face coverage by sampling the volumetric box. Initialization of
// m_aavVolumeAxis is not established; A3dSourceVolumetric does not fill it.
//
// The reference is __thiscall; this helper takes the source explicitly.
//
// Returns: The hit fraction; 1 when no test points are requested.
// =============================================================*/

A3DVAL
A3dVolumetricCoverage(CA3dSource *pSource,
		      int (*pfnHit)(const A3DVAL *, const A3DVAL *,
				    A3DPRIMITIVE *, int, int),
		      const A3DVAL *pvListener, A3DPRIMITIVE *pPrim)
{
A3DVAL avX[3], avY[3], avZ[3];
A3DVAL avXY[3], avXZ[3], avYZ[3];
A3DVAL avSample[3];
A3DVAL fHitCount;
int    nRequestedSamplePairs;

	nRequestedSamplePairs = pSource->m_VolDampInfo.nTestPointsMax;

	if (!nRequestedSamplePairs)
		return (1.0f);

	avX[0] = pSource->m_aavVolumeAxis[0][0] * 0.5f * pSource->m_avVolumetricBounds[0];
	avX[1] = pSource->m_aavVolumeAxis[0][1] * 0.5f * pSource->m_avVolumetricBounds[0];
	avX[2] = pSource->m_aavVolumeAxis[0][2] * 0.5f * pSource->m_avVolumetricBounds[0];

	avY[0] = pSource->m_aavVolumeAxis[1][0] * 0.5f * pSource->m_avVolumetricBounds[1];
	avY[1] = pSource->m_aavVolumeAxis[1][1] * 0.5f * pSource->m_avVolumetricBounds[1];
	avY[2] = pSource->m_aavVolumeAxis[1][2] * 0.5f * pSource->m_avVolumetricBounds[1];

	avZ[0] = pSource->m_aavVolumeAxis[2][0] * 0.5f * pSource->m_avVolumetricBounds[2];
	avZ[1] = pSource->m_aavVolumeAxis[2][1] * 0.5f * pSource->m_avVolumetricBounds[2];
	avZ[2] = pSource->m_aavVolumeAxis[2][2] * 0.5f * pSource->m_avVolumetricBounds[2];

	avXY[0] = avX[0] + avY[0];
	avXY[1] = avX[1] + avY[1];
	avXY[2] = avX[2] + avY[2];

	avXZ[0] = avX[0] + avZ[0];
	avXZ[1] = avX[1] + avZ[1];
	avXZ[2] = avX[2] + avZ[2];

	avYZ[0] = avY[0] + avZ[0];
	avYZ[1] = avY[1] + avZ[1];
	avYZ[2] = avY[2] + avZ[2];

	fHitCount = 0.0f;

	avSample[0] = pSource->m_avVolumetricOrigin[0] - avX[0] - pvListener[0];
	avSample[1] = pSource->m_avVolumetricOrigin[1] - avX[1] - pvListener[1];
	avSample[2] = pSource->m_avVolumetricOrigin[2] - avX[2] - pvListener[2];

	if (pfnHit(pvListener, avSample, pPrim, 1, 0))
		fHitCount += 1.0f;

	avSample[0] = avX[0] + pSource->m_avVolumetricOrigin[0] - pvListener[0];
	avSample[1] = avX[1] + pSource->m_avVolumetricOrigin[1] - pvListener[1];
	avSample[2] = avX[2] + pSource->m_avVolumetricOrigin[2] - pvListener[2];

	if (pfnHit(pvListener, avSample, pPrim, 1, 0))
		fHitCount += 1.0f;

	if (nRequestedSamplePairs > 1)
	{
		avSample[0] = pSource->m_avVolumetricOrigin[0] - avY[0] - pvListener[0];
		avSample[1] = pSource->m_avVolumetricOrigin[1] - avY[1] - pvListener[1];
		avSample[2] = pSource->m_avVolumetricOrigin[2] - avY[2] - pvListener[2];

		if (pfnHit(pvListener, avSample, pPrim, 1, 0))
			fHitCount += 1.0f;

		avSample[0] = avY[0] + pSource->m_avVolumetricOrigin[0] - pvListener[0];
		avSample[1] = avY[1] + pSource->m_avVolumetricOrigin[1] - pvListener[1];
		avSample[2] = avY[2] + pSource->m_avVolumetricOrigin[2] - pvListener[2];

		if (pfnHit(pvListener, avSample, pPrim, 1, 0))
			fHitCount += 1.0f;
	}

	if (nRequestedSamplePairs > 2)
	{
		avSample[0] = pSource->m_avVolumetricOrigin[0] - avZ[0] - pvListener[0];
		avSample[1] = pSource->m_avVolumetricOrigin[1] - avZ[1] - pvListener[1];
		avSample[2] = pSource->m_avVolumetricOrigin[2] - avZ[2] - pvListener[2];

		if (pfnHit(pvListener, avSample, pPrim, 1, 0))
			fHitCount += 1.0f;

		avSample[0] = avZ[0] + pSource->m_avVolumetricOrigin[0] - pvListener[0];
		avSample[1] = avZ[1] + pSource->m_avVolumetricOrigin[1] - pvListener[1];
		avSample[2] = avZ[2] + pSource->m_avVolumetricOrigin[2] - pvListener[2];

		if (pfnHit(pvListener, avSample, pPrim, 1, 0))
			fHitCount += 1.0f;
	}

	if (nRequestedSamplePairs > 3)
	{
		avSample[0] = pSource->m_avVolumetricOrigin[0] - avXY[0] - pvListener[0];
		avSample[1] = pSource->m_avVolumetricOrigin[1] - avXY[1] - pvListener[1];
		avSample[2] = pSource->m_avVolumetricOrigin[2] - avXY[2] - pvListener[2];

		if (pfnHit(pvListener, avSample, pPrim, 1, 0))
			fHitCount += 1.0f;

		avSample[0] = avXY[0] + pSource->m_avVolumetricOrigin[0] - pvListener[0];
		avSample[1] = avXY[1] + pSource->m_avVolumetricOrigin[1] - pvListener[1];
		avSample[2] = avXY[2] + pSource->m_avVolumetricOrigin[2] - pvListener[2];

		if (pfnHit(pvListener, avSample, pPrim, 1, 0))
			fHitCount += 1.0f;
	}

	if (nRequestedSamplePairs > 4)
	{
		avSample[0] = pSource->m_avVolumetricOrigin[0] - avXZ[0] - pvListener[0];
		avSample[1] = pSource->m_avVolumetricOrigin[1] - avXZ[1] - pvListener[1];
		avSample[2] = pSource->m_avVolumetricOrigin[2] - avXZ[2] - pvListener[2];

		if (pfnHit(pvListener, avSample, pPrim, 1, 0))
			fHitCount += 1.0f;

		avSample[0] = avXZ[0] + pSource->m_avVolumetricOrigin[0] - pvListener[0];
		avSample[1] = avXZ[1] + pSource->m_avVolumetricOrigin[1] - pvListener[1];
		avSample[2] = avXZ[2] + pSource->m_avVolumetricOrigin[2] - pvListener[2];

		if (pfnHit(pvListener, avSample, pPrim, 1, 0))
			fHitCount += 1.0f;
	}

	if (nRequestedSamplePairs > 5)
	{
		avSample[0] = pSource->m_avVolumetricOrigin[0] - avYZ[0] - pvListener[0];
		avSample[1] = pSource->m_avVolumetricOrigin[1] - avYZ[1] - pvListener[1];
		avSample[2] = pSource->m_avVolumetricOrigin[2] - avYZ[2] - pvListener[2];

		if (pfnHit(pvListener, avSample, pPrim, 1, 0))
			fHitCount += 1.0f;

		avSample[0] = avYZ[0] + pSource->m_avVolumetricOrigin[0] - pvListener[0];
		avSample[1] = avYZ[1] + pSource->m_avVolumetricOrigin[1] - pvListener[1];
		avSample[2] = avYZ[2] + pSource->m_avVolumetricOrigin[2] - pvListener[2];

		if (pfnHit(pvListener, avSample, pPrim, 1, 0))
			fHitCount += 1.0f;
	}

	return (fHitCount / ((A3DVAL) nRequestedSamplePairs + (A3DVAL) nRequestedSamplePairs));
}

/* =============================================================
// A3dVolumetricSizeDamp()
// (RE) rtl:0x1000E9A0; dbg:0x100222C0; thunk dbg:0x10002FC7
//
// Measure damping from face area relative to volumetric source size.
// The reference is __thiscall; this helper takes the source explicitly.
//
// Returns: The damping ratio, floored at fSizeDampMin.
// =============================================================*/

A3DVAL
A3dVolumetricSizeDamp(CA3dSource *pSource, A3DPRIMITIVE *pPrim)
{
A3DVAL avEdge1[3], avEdge2[3], avCross[3];
A3DVAL fCrossSq, fArea, fDamp;

	avEdge1[0] = pPrim->av[0][0] - pPrim->av[1][0];
	avEdge1[1] = pPrim->av[0][1] - pPrim->av[1][1];
	avEdge1[2] = pPrim->av[0][2] - pPrim->av[1][2];

	avEdge2[0] = pPrim->av[0][0] - pPrim->av[2][0];
	avEdge2[1] = pPrim->av[0][1] - pPrim->av[2][1];
	avEdge2[2] = pPrim->av[0][2] - pPrim->av[2][2];

	avCross[0] = avEdge1[1] * avEdge2[2] - avEdge1[2] * avEdge2[1];
	avCross[1] = avEdge1[2] * avEdge2[0] - avEdge1[0] * avEdge2[2];
	avCross[2] = avEdge1[0] * avEdge2[1] - avEdge1[1] * avEdge2[0];

	fCrossSq = avCross[0] * avCross[0] + avCross[1] * avCross[1] +
		   avCross[2] * avCross[2];

	fArea = A3dFastSqrt(fCrossSq) * 0.5f;

	/* Original defect: zero area and zero source size divide zero by zero. */
	fDamp = fArea / (fArea + ((A3DVAL *) pSource->m_adwVolumetricHit)[1]);

	if (fDamp < pSource->m_VolDampInfo.fSizeDampMin)
		return (pSource->m_VolDampInfo.fSizeDampMin);

	return (fDamp);
}

/* =============================================================
// A3dSourceVolumetric()
// (RE) rtl:0x1000EA80; dbg:0x10022410; thunk dbg:0x1000365C
//
// Move the source frame origin to a point clamped to its volumetric bounds
// and update containment and intersection state. The reference leaves a
// branch-dependent value in eax; this void form discards it.
// =============================================================*/

void
A3dSourceVolumetric(CA3dSource *pSource, A3DVAL *pm)
{
A3DVAL avPoint[4];
A3DVAL avDir[4];
A3DVAL avClamped[3];
A3DVAL avNew[4];
A3DVAL avBoxMin[3], avBoxMax[3];
A3DVAL fHalf;
int    nInside;
int    i;

	A3dMatrixInvert(pm, pSource->m_matVolumeInverse);
	A3dTransformPoint(pSource->m_pApi->m_vListenerPos,
			  pSource->m_matVolumeInverse, avPoint);
	A3dTraceTransformDir(&pSource->m_pApi->m_matListenerXform[8], pm, avDir);

	nInside = 0;

	for (i = 0; i < 3; i++)
	{
		fHalf = pSource->m_avVolumetricBounds[i] * 0.5f;

		if (avPoint[i] > fHalf)
		{
			avClamped[i] = fHalf;
		}
		else if (avPoint[i] < -fHalf)
		{
			avClamped[i] = -fHalf;
		}
		else
		{
			avClamped[i] = avPoint[i];
			nInside++;
		}
	}

	if (nInside == 3)
	{
		pSource->m_fInsideVolumetricBounds = 1;

		if (!pSource->m_VolDampInfo.bMonoInside)
		{
			avClamped[0] = avClamped[0] + A3D_VOLUME_INTERIOR_OFFSET;
			avClamped[1] = avClamped[1] + A3D_VOLUME_INTERIOR_OFFSET;
			avClamped[2] = avClamped[2] + A3D_VOLUME_INTERIOR_OFFSET;
		}
	}
	else
	{
		pSource->m_fInsideVolumetricBounds = 0;
	}

	pSource->m_avVolumetricOrigin[0] = pm[12];
	pSource->m_avVolumetricOrigin[1] = pm[13];
	pSource->m_avVolumetricOrigin[2] = pm[14];

	A3dTransformPoint(avClamped, pm, avNew);

	pm[12] = avNew[0];
	pm[13] = avNew[1];
	pm[14] = avNew[2];

	if (!pSource->m_fInsideVolumetricBounds)
	{
		for (i = 0; i < 3; i++)
		{
			avBoxMin[i] = pSource->m_avVolumetricBounds[i] * -0.5f;
			avBoxMax[i] = pSource->m_avVolumetricBounds[i] * 0.5f;

			if (avBoxMin[i] == 0.0f)
				avBoxMin[i] = -0.5f * pSource->m_fMinDistance;

			if (avBoxMax[i] == 0.0f)
				avBoxMax[i] = 0.5f * pSource->m_fMinDistance;

			avDir[i] = pSource->m_fMaxDistance * -1.0f * avDir[i];
		}

		pSource->m_adwVolumetricHit[0] =
			A3dSegmentHitsBox(avBoxMin, avBoxMax, avPoint, avDir);
	}
}

/* =============================================================
// A3dTraceTransformDir()
// (RE) dbg:0x100227A0; thunk dbg:0x10001CE4
//
// Rotate a direction without translation, reading and writing four floats.
// The reference returns the matrix pointer; this void form discards it.
// =============================================================*/

void
A3dTraceTransformDir(const A3DVAL *pvDir, const A3DVAL *pm, A3DVAL *pvOut)
{
A3DVAL av[4];

	CopyMemory(av, pvDir, sizeof(av));

	pvOut[0] = av[0] * pm[0] + av[1] * pm[1] + av[2] * pm[2];
	pvOut[1] = av[0] * pm[4] + av[1] * pm[5] + av[2] * pm[6];
	pvOut[2] = av[0] * pm[8] + av[1] * pm[9] + av[2] * pm[10];
	pvOut[3] = 0.0f;
}

/* =============================================================
// A3dTraceSetEars()
// (RE) rtl:0x1000EF40; dbg:0x10022860; thunk dbg:0x10002103
//
// Transform source velocity by the inverse frame; write zero velocity
// when none has been set.
// =============================================================*/

void
A3dTraceSetEars(CA3dSource *pSource, A3DVAL *pvOut)
{
A3DVAL matConcat[16];

	if (!pSource->m_dwVelocitySet)
	{

		pvOut[0] = 0.0f;
		pvOut[1] = 0.0f;
		pvOut[2] = 0.0f;

		return;
	}

	if (pSource->m_dwTransformMode == A3DSOURCE_TRANSFORMMODE_HEADRELATIVE)
	{

		A3dMatrixMultiply(pSource->m_pApi->m_matListenerXform,
				  pSource->m_matSource, matConcat);

		A3dTraceTransformDirInverse(pSource->m_vVelocity, matConcat, pvOut);
	}
	else
	{
		A3dTraceTransformDirInverse(pSource->m_vVelocity,
					    pSource->m_matSource, pvOut);
	}
}

/* =============================================================
// A3dTraceTransformDirInverse()
// (RE) dbg:0x10022940; thunk dbg:0x10003FD0
//
// Rotate a direction by the inverse matrix without translation, reading
// and writing four floats.
// =============================================================*/

void
A3dTraceTransformDirInverse(const A3DVAL *pvDir, const A3DVAL *pm, A3DVAL *pvOut)
{
A3DVAL av[4];
A3DVAL matInverse[16];

	CopyMemory(av, pvDir, sizeof(av));

	A3dMatrixInvert(pm, matInverse);

	pvOut[0] = matInverse[0] * av[0] + matInverse[1] * av[1] +
		   matInverse[2] * av[2];
	pvOut[1] = matInverse[4] * av[0] + matInverse[5] * av[1] +
		   matInverse[6] * av[2];
	pvOut[2] = matInverse[8] * av[0] + matInverse[9] * av[1] +
		   matInverse[10] * av[2];
	pvOut[3] = 0.0f;
}

/* =============================================================
// A3dTraceApply()
// (RE) rtl:0x1000F180; dbg:0x10022A00; thunk dbg:0x10003EEF
//
// Update the occlusion gain ramps and store the reported occlusion factor.
// =============================================================*/

void
A3dTraceApply(CA3dSource *pSource, A3DVAL fOcclusion, const A3DVAL *pav)
{
A3DVAL fStep;
int    i;

	if (!(pSource->m_pApi->m_dwFeaturesEnabled & A3D_OCCLUSIONS) ||
	    !(pSource->m_dwRenderMode & A3DSOURCE_RENDERMODE_OCCLUSIONS))
	{

		pSource->m_afOcclusionRate[0] = 0.0f;
		pSource->m_afOcclusionRate[1] = 0.0f;

		pSource->m_afOcclusionTarget[0] = 1.0f;
		pSource->m_afOcclusionTarget[1] = 1.0f;
		pSource->m_afOcclusionValue[0]  = 1.0f;
		pSource->m_afOcclusionValue[1]  = 1.0f;

		pSource->m_fOcclusion = fOcclusion;

		return;
	}

	if (pSource->m_dwPlayState == A3D_PLAYSTATE_PENDING)
	{

		pSource->m_afOcclusionRate[0] = 0.0f;
		pSource->m_afOcclusionRate[1] = 0.0f;

		pSource->m_afOcclusionTarget[0] = pav[0];
		pSource->m_afOcclusionValue[0]  = pav[0];
		pSource->m_afOcclusionTarget[1] = pav[1];
		pSource->m_afOcclusionValue[1]  = pav[1];

		pSource->m_fOcclusion = fOcclusion;

		return;
	}

	for (i = 0; i < 2; i++)
	{
		if (pSource->m_afOcclusionTarget[i] != pav[i])
		{
			pSource->m_afOcclusionTarget[i] = pav[i];

			fStep = (pSource->m_afOcclusionTarget[i] -
				 pSource->m_afOcclusionValue[i]) *
				pSource->m_pApi->m_fFrameTime;

			pSource->m_afOcclusionRate[i] = fStep + fStep;
		}

		if (pSource->m_afOcclusionRate[i] != 0.0f)
		{
			pSource->m_afOcclusionValue[i] +=
				pSource->m_afOcclusionRate[i];

			if (pSource->m_afOcclusionRate[i] <= 0.0f)
			{
				if (pSource->m_afOcclusionValue[i] <
				    pSource->m_afOcclusionTarget[i])
				{
					pSource->m_afOcclusionValue[i] =
						pSource->m_afOcclusionTarget[i];
					pSource->m_afOcclusionRate[i] = 0.0f;
				}
			}
			else if (pSource->m_afOcclusionValue[i] >
				 pSource->m_afOcclusionTarget[i])
			{
				pSource->m_afOcclusionValue[i] =
					pSource->m_afOcclusionTarget[i];
				pSource->m_afOcclusionRate[i] = 0.0f;
			}
		}
	}

	pSource->m_fOcclusion = fOcclusion;
}

/* =============================================================
// A3dSetDelay()
// (RE) rtl:0x1000F2C0; dbg:0x10022D40; thunk dbg:0x10002D2E
//
// Compute interaural delay in seconds from polar direction.
// =============================================================*/

void
A3dSetDelay(CA3dSource *pSource, const A3DVAL *pvPolar)
{
A3DVAL fScale;
A3DVAL fSeries;

	fScale = (A3DVAL) (cos(pvPolar[1]) * sin(pvPolar[0]));

	fSeries = (A3DVAL) (cos(2.0f * pvPolar[0]) * -0.0429 + 0.5729);
	fSeries = (A3DVAL) (cos(3.0f * pvPolar[0]) *  0.0195 + fSeries);
	fSeries = (A3DVAL) (cos(4.0f * pvPolar[0]) *  0.0371 + fSeries);

	pSource->m_fDelay = fSeries * fScale * 0.001f;
}

/* =============================================================
// A3dDoppler()
// (RE) rtl:0x1000F320; dbg:0x10022E40; thunk dbg:0x10002D1F
//
// Compute the Doppler pitch factor from relative velocities and API/source
// scales.
// =============================================================*/

void
A3dDoppler(CA3dSource *pSource, const A3DVAL *pvVelSource,
	   const A3DVAL *pvVelListener, const A3DVAL *pvTo, A3DVAL fDistance)
{
IA3d5 *pApi;
A3DVAL fSpeed;
A3DVAL fApiDopplerScale;
A3DVAL fUnitsPerMeter;
A3DVAL fToward, fAway;
A3DVAL fLimit;

	if ((pvVelSource[0] == pvVelListener[0] &&
	     pvVelSource[1] == pvVelListener[1] &&
	     pvVelSource[2] == pvVelListener[2]) ||
	    pSource->m_fDopplerScale == 0.0f || fDistance == 0.0f)
	{
		pSource->m_fDoppler = 1.0f;

		return;
	}

	pApi = pSource->m_pApi;

	pApi->GetDopplerScale(&fApiDopplerScale);
	pApi->GetUnitsPerMeter(&fUnitsPerMeter);

	fSpeed = 340.5f * fUnitsPerMeter /
		 (fApiDopplerScale * pSource->m_fDopplerScale);

	fAway = -((pvVelSource[0] * pvTo[0] + pvVelSource[1] * pvTo[1] +
		   pvVelSource[2] * pvTo[2]) / fDistance);

	fToward = -((pvVelListener[0] * pvTo[0] + pvVelListener[1] * pvTo[1] +
		     pvVelListener[2] * pvTo[2]) / fDistance);

	fLimit = fSpeed - 1.0f;

	if (fLimit >= fAway)
	{
		if (1.0f - fSpeed > fAway)
			fAway = 1.0f - fSpeed;
	}
	else
	{
		fAway = fLimit;
	}

	if (fLimit >= fToward)
	{
		if (1.0f - fSpeed > fToward)
			fToward = 1.0f - fSpeed;
	}
	else
	{
		fToward = fLimit;
	}

	if (fSpeed == fAway)
	{
		pSource->m_fDoppler = 1.0f;

		return;
	}

	pSource->m_fDoppler = (fSpeed - fToward) / (fSpeed - fAway);
}

/* Mantissa lookup, 128 WORDs per exponent parity.
 * (RE) rtl:0x10067AC0; dbg:0x10152A80; odd half dbg:0x10152B80. */
static WORD	g_awSqrtTable[256];

/* =============================================================
// A3dInitSqrtTable()
// (RE) dbg:0x1003C740; thunk dbg:0x10002CF2
//
// Initialize both exponent-parity halves of the square-root table.
// =============================================================*/

void
A3dInitSqrtTable(void)
{
union { A3DVAL f; DWORD dw; } u;
WORD	i;

	for (i = 0; i <= 0x7F; i++)
	{
		u.dw                    = ((DWORD) i << 16) | A3D_SQRT_FLOAT_ONE_BITS;
		u.f                     = (A3DVAL) sqrt(u.f);
		g_awSqrtTable[i]        = (WORD) ((u.dw & A3D_SQRT_FRACTION_MASK) >> 16);

		u.dw = ((DWORD) i << 16) | A3D_SQRT_FLOAT_TWO_BITS;
		u.f  = (A3DVAL) sqrt(u.f);
		g_awSqrtTable[i + 128] = (WORD) ((u.dw & A3D_SQRT_FRACTION_MASK) >> 16);
	}
}

/* =============================================================
// A3dFastSqrt()
// (RE) dbg:0x1000FE10; thunk dbg:0x100034CC
//
// Approximate a square root using the initialized mantissa table.
//
// Returns: The square-root approximation.
// =============================================================*/

A3DVAL
A3dFastSqrt(A3DVAL fValue)
{
union { A3DVAL f; DWORD dw; } u;
short	nExp;

	u.f  = fValue;
	nExp = (short) ((u.dw >> 23) - A3D_SQRT_EXPONENT_BIAS);
	u.dw &= A3D_SQRT_FRACTION_MASK;

	if (nExp & 1)
		u.dw |= A3D_SQRT_ODD_EXPONENT_BIT;

	nExp >>= 1;

	u.dw = ((DWORD) (nExp + A3D_SQRT_EXPONENT_BIAS) << 23) |
	       ((DWORD) g_awSqrtTable[u.dw >> 16] << 16);

	return (u.f);
}

/* =============================================================
// A3dConeAngle()
// (RE) rtl:0x1000F700; dbg:0x10023330; thunk dbg:0x10001F32
//
// Measure the angle between the source axis and listener direction.
// The original divides by zero when fDistance is zero.
//
// Returns: The cone angle in degrees.
// =============================================================*/

A3DVAL
A3dConeAngle(CA3dSource *pSource, const A3DVAL *pvAxis,
	     const A3DVAL *pvToListener, A3DVAL fDistance)
{
A3DVAL vAxis[3];
A3DVAL vDir[3];
A3DVAL fCos;

	if (pSource->m_pApi->m_dwCoordSystem)
	{
		vAxis[0] = pvAxis[8];
		vAxis[1] = pvAxis[9];
		vAxis[2] = pvAxis[10];
	}
	else
	{
		vAxis[0] = -1.0f * pvAxis[8];
		vAxis[1] = -1.0f * pvAxis[9];
		vAxis[2] = -1.0f * pvAxis[10];
	}

	vDir[0] = -1.0f * pvToListener[0] / fDistance;
	vDir[1] = -1.0f * pvToListener[1] / fDistance;
	vDir[2] = -1.0f * pvToListener[2] / fDistance;

	fCos = vDir[0] * vAxis[0] + vDir[1] * vAxis[1] + vDir[2] * vAxis[2];

	if (fCos > 1.0f)
		fCos = 1.0f;
	else if (fCos < -1.0f)
		fCos = -1.0f;

	return ((A3DVAL) (acos(fCos) * A3D_RADIANS_TO_DEGREES));
}

/* =============================================================
// A3dConeAngleXform()
// (RE) dbg:0x10023090; thunk dbg:0x100039F4
//
// Measure the listener direction against the rebuilt source frame's z axis.
//
// Returns: The angle in degrees; 0 for zero length.
// =============================================================*/

A3DVAL
A3dConeAngleXform(CA3dSource *pSource)
{
A3DVAL matFrame[16];
A3DVAL matInverse[16];
A3DVAL vAxis[3];
A3DVAL vListener[3];
A3DVAL fDot;
A3DVAL fLenSq;
A3DVAL fLen;
A3DVAL fCos;

	CopyMemory(matFrame, pSource->m_matSource, sizeof(matFrame));

	A3dMatrixTranslateBy(pSource->m_vPosition, matFrame);

	vAxis[0] = 0.0f;
	vAxis[1] = 1.0f;
	vAxis[2] = 0.0f;

	A3dMatrixRotateBy(pSource->m_vOrientAngles[0], vAxis, matFrame);

	vAxis[0] = 1.0f;
	vAxis[1] = 0.0f;

	A3dMatrixRotateBy(pSource->m_vOrientAngles[1], vAxis, matFrame);

	vAxis[0] = 0.0f;
	vAxis[2] = 1.0f;

	A3dMatrixRotateBy(pSource->m_vOrientAngles[2], vAxis, matFrame);

	A3dMatrixInvert(matFrame, matInverse);
	A3dMatrixGetTranslation(matInverse, vListener);

	fDot   = 0.0f * vListener[0] + 0.0f * vListener[1] + 1.0f * vListener[2];
	fLenSq = vListener[0] * vListener[0] +
		 vListener[1] * vListener[1] +
		 vListener[2] * vListener[2];

	fLen = A3dFastSqrt(fLenSq);

	if (fLen > 0.0f)
	{
		fCos = fDot / fLen;

		if (fCos > 1.0f)
			fCos = 1.0f;
		else if (fCos < -1.0f)
			fCos = -1.0f;

		return ((A3DVAL) (acos(fCos) * A3D_RADIANS_TO_DEGREES));
	}

	return (0.0f);
}

/* =============================================================
// A3dTraceSetVector()
// (RE) rtl:0x1000F7D0; dbg:0x10023480; thunk dbg:0x1000115E
//
// Compute cone gain from the listener direction and cone limits.
// =============================================================*/

void
A3dTraceSetVector(CA3dSource *pSource, const A3DVAL *pvAxis,
		  const A3DVAL *pvToListener, A3DVAL fDistance)
{
A3DVAL fAngle;

	if (pSource->m_fConeOutsideGain >= 1.0f ||
	    pSource->m_fConeOuterAngle <= 0.0f)
	{
		pSource->m_fConeGain = 1.0f;

		return;
	}

	if (pSource->m_pApi->m_dwCoordSystem == A3D_LEFT_HANDED_CS &&
	    pSource->m_dwTransformMode == A3DSOURCE_TRANSFORMMODE_HEADRELATIVE)
	{
		fAngle = A3dConeAngleXform(pSource);
	}
	else
	{
		fAngle = A3dConeAngle(pSource, pvAxis, pvToListener, fDistance);
	}

	if (fAngle > pSource->m_fConeOuterAngle)
	{
		pSource->m_fConeGain = pSource->m_fConeOutsideGain;
	}
	else if (fAngle <= pSource->m_fConeInnerAngle)
	{
		pSource->m_fConeGain = 1.0f;
	}
	else
	{
		pSource->m_fConeGain =
			(pSource->m_fConeOuterAngle - fAngle) /
			(pSource->m_fConeOuterAngle - pSource->m_fConeInnerAngle) *
			(1.0f - pSource->m_fConeOutsideGain) +
			pSource->m_fConeOutsideGain;
	}
}

/* =============================================================
// A3dTraceSetSpread()
// (RE) rtl:0x1000F8B0; dbg:0x100235F0; thunk dbg:0x10003EBD
//
// Store ear distances and compute distance attenuation and spread gain.
// The reference is __thiscall; this helper takes the source explicitly.
// =============================================================*/

void
A3dTraceSetSpread(CA3dSource *pSource, A3DVAL fLeft, A3DVAL fRight)
{
	pSource->m_fEarLeft  = fLeft;
	pSource->m_fEarRight = fRight;

	A3dDistanceGain(pSource, fLeft,  &pSource->m_afEarGain[0]);
	A3dDistanceGain(pSource, fRight, &pSource->m_afEarGain[1]);

	A3dSpreadGain(pSource, (fLeft + fRight) * 0.5f, &pSource->m_fEqAmount);
}

/* =============================================================
// A3dDistanceGain()
// (RE) dbg:0x10023690; thunk dbg:0x100011A4
//
// Write attenuation from distance, bounds and the API/source scale product.
// =============================================================*/

void
A3dDistanceGain(CA3dSource *pSource, A3DVAL fDistance, A3DVAL *pfGain)
{
A3DVAL fApiDistanceScale;
A3DVAL fScale;

	((IA3d5 *) pSource->m_pApi)->GetDistanceModelScale(&fApiDistanceScale);

	fScale = fApiDistanceScale * pSource->m_fDistanceModelScale;

	if (fDistance <= pSource->m_fMinDistance || fScale == 0.0f)
	{
		*pfGain = 1.0f;

		return;
	}

	if (fDistance > pSource->m_fMaxDistance)
	{
		if (pSource->m_dwDistanceMode)
		{
			*pfGain = 0.0f;

			return;
		}

		fDistance = pSource->m_fMaxDistance;
	}

	*pfGain = pSource->m_fMinDistance /
		  ((fDistance - pSource->m_fMinDistance) * fScale +
		   pSource->m_fMinDistance);
}

/* =============================================================
// A3dSpreadGain()
// (RE) dbg:0x10023780; thunk dbg:0x1000222F
//
// Write spread gain from distance and the API/source scale product.
// The original leaves the output unchanged for a zero denominator.
// =============================================================*/

void
A3dSpreadGain(CA3dSource *pSource, A3DVAL fDistance, A3DVAL *pfGain)
{
A3DVAL fApiDistanceScale;
A3DVAL fScale;
A3DVAL fKnee;

	((IA3d5 *) pSource->m_pApi)->GetDistanceModelScale(&fApiDistanceScale);

	fScale = fApiDistanceScale * pSource->m_fDistanceModelScale;

	if (fDistance <= pSource->m_fMinDistance || fScale == 0.0f)
	{
		*pfGain = 1.0f;

		return;
	}

	if (fDistance > pSource->m_fMaxDistance)
	{
		if (pSource->m_dwDistanceMode)
		{
			*pfGain = 0.0f;

			return;
		}

		fDistance = pSource->m_fMaxDistance;
	}

	fKnee = A3D_SPREAD_KNEE * pSource->m_fMinDistance + fDistance;

	if (fKnee != 0.0f)
		*pfGain = 1.0f - (fDistance - pSource->m_fMinDistance) *
				 fScale / fKnee;
}

/* =============================================================
// A3dStepAngles()
// (RE) dbg:0x10023890
//
// Write ear directions, mean distance and the native-placement flag.
// =============================================================*/

void
A3dStepAngles(CA3dSource *pSource, const A3DVAL *pvLeftEar,
	      const A3DVAL *pvRightEar)
{
LPA3DCTRL_SRC_SUPER pCtrl;

	pCtrl = pSource->m_pBufferCurr;

	if ((pSource->m_dwRenderMode & A3DSOURCE_RENDERMODE_MONO) ||
	    (pSource->m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE))
	{
		pCtrl->fNative = 1.0f;

		return;
	}

	pCtrl->fNative = 0.0f;

	pCtrl->fDistance = (pvRightEar[2] + pvLeftEar[2]) * 0.5f;

	pCtrl->LeftEar.fAzim  = pvLeftEar[0];
	pCtrl->LeftEar.fElev  = pvLeftEar[1];
	pCtrl->RightEar.fAzim = pvRightEar[0];
	pCtrl->RightEar.fElev = pvRightEar[1];

	if (pSource->m_adwVolumetricHit[0])
	{
		pCtrl->LeftEar.fAzim  *= pSource->m_VolDampInfo.fAzimuthPan;
		pCtrl->RightEar.fAzim *= pSource->m_VolDampInfo.fAzimuthPan;
	}
}

/* =============================================================
// A3dStepDelayPair()
// (RE) dbg:0x100239E0
//
// Split interaural delay across the two ear records; clear it for mono/native
// mode.
// =============================================================*/

void
A3dStepDelayPair(CA3dSource *pSource)
{
LPA3DCTRL_SRC_SUPER pCtrl;

	pCtrl = pSource->m_pBufferCurr;

	if ((pSource->m_dwRenderMode & A3DSOURCE_RENDERMODE_MONO) ||
	    (pSource->m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE))
	{
		pCtrl->LeftEar.fDelay  = 0.0f;
		pCtrl->RightEar.fDelay = 0.0f;

		return;
	}

	pCtrl->LeftEar.fDelay  = -(pSource->m_fDelay * 0.5f);
	pCtrl->RightEar.fDelay =   pSource->m_fDelay * 0.5f;
}

/* =============================================================
// A3dStepEarGains()
// (RE) dbg:0x10023A90
//
// Write per-ear gains and distance-model parameters to the control block.
// =============================================================*/

void
A3dStepEarGains(CA3dSource *pSource)
{
LPA3DCTRL_SRC_SUPER pCtrl;
A3DVAL              fListenerGain;
A3DVAL              fApiDistanceScale;

	fListenerGain = 0.0f;

	((IA3d3 *) pSource->m_pApi)->GetOutputGain(&fListenerGain);

	pCtrl = pSource->m_pBufferCurr;

	if (pSource->m_dwRenderMode & A3DSOURCE_RENDERMODE_MONO)
	{
		pCtrl->LeftEar.fGain  = fListenerGain * pSource->m_fGain;
		pCtrl->RightEar.fGain = fListenerGain * pSource->m_fGain;
	}
	else if (pSource->m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE)
	{
		pCtrl->LeftEar.fGain  = pSource->m_fGain *
					pSource->m_afPanValues[0] * fListenerGain;
		pCtrl->RightEar.fGain = pSource->m_fGain *
					pSource->m_afPanValues[1] * fListenerGain;
	}
	else
	{
		pCtrl->LeftEar.fGain  = pSource->m_afEarGain[0] *
					pSource->m_fGain *
					pSource->m_fConeGain *
					pSource->m_afOcclusionValue[0] * fListenerGain;
		pCtrl->RightEar.fGain = pSource->m_afEarGain[1] *
					pSource->m_fGain *
					pSource->m_fConeGain *
					pSource->m_afOcclusionValue[0] * fListenerGain;
	}

	pCtrl->fMinDist = pSource->m_fMinDistance;
	pCtrl->fMaxDist = pSource->m_fMaxDistance;

	((IA3d5 *) pSource->m_pApi)->GetDistanceModelScale(&fApiDistanceScale);

	pCtrl->fDistScale = pSource->m_fDistanceModelScale * fApiDistanceScale;
}

/* =============================================================
// A3dStepEq()
// (RE) dbg:0x10023C70
//
// Write the combined equalisation coefficient, clamped to 0..0.99.
// =============================================================*/

void
A3dStepEq(CA3dSource *pSource)
{
LPA3DCTRL_SRC_SUPER pCtrl;

	pCtrl = pSource->m_pBufferCurr;

	if ((pSource->m_dwRenderMode & A3DSOURCE_RENDERMODE_MONO) ||
	    (pSource->m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE))
	{
		pCtrl->fAlpha = 1.0f -
				pSource->m_fEqCoefficient *
				pSource->m_pApi->m_fEqCurve;
	}
	else
	{
		pCtrl->fAlpha = 1.0f -
				pSource->m_fEqCoefficient *
				pSource->m_pApi->m_fEqCurve *
				pSource->m_afOcclusionValue[1] *
				pSource->m_fEqAmount;
	}

	if (pCtrl->fAlpha > A3D_SOURCE_CTRL_ALPHA_MAX)
		pCtrl->fAlpha = A3D_SOURCE_CTRL_ALPHA_MAX;
	else if (pCtrl->fAlpha < 0.0f)
		pCtrl->fAlpha = 0.0f;
}

/* =============================================================
// A3dStepMidGain()
// (RE) dbg:0x10023DA0
//
// Write audibility from both ear gains and the equalisation coefficient.
// =============================================================*/

void
A3dStepMidGain(CA3dSource *pSource)
{
LPA3DCTRL_SRC_SUPER pCtrl;

	pCtrl = pSource->m_pBufferCurr;

	pCtrl->fAudibility = (pCtrl->LeftEar.fGain + pCtrl->RightEar.fGain) *
			     A3D_SOURCE_AUDIBILITY_EAR_WEIGHT +
			     (1.0f - pCtrl->fAlpha) *
			     A3D_SOURCE_AUDIBILITY_EQ_WEIGHT;
}

/* =============================================================
// A3dStepPitchOut()
// (RE) dbg:0x10023E10
//
// Write pitch with resampling and, for positioned sources, Doppler scaling.
// =============================================================*/

void
A3dStepPitchOut(CA3dSource *pSource)
{
LPA3DCTRL_SRC_SUPER pCtrl;

	pCtrl = pSource->m_pBufferCurr;

	if ((pSource->m_dwRenderMode & A3DSOURCE_RENDERMODE_MONO) ||
	    (pSource->m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE))
	{
		pCtrl->fPitch = pSource->m_fPitch * pSource->m_fRateRatio;

		return;
	}

	pCtrl->fPitch = pSource->m_fDoppler * pSource->m_fPitch *
			pSource->m_fRateRatio;
}

/* =============================================================
// A3dSourceStep()
// (RE) dbg:0x10023EA0; thunk dbg:0x10001D48
//
// Populate direct-path control values and source priority.
// =============================================================*/

void
A3dSourceStep(CA3dSource *pSource, const A3DVAL *pvLeftEar,
	      const A3DVAL *pvRightEar)
{
	pSource->m_pBufferCurr->fPriority = pSource->m_fPriority;

	A3dStepAngles(pSource, pvLeftEar, pvRightEar);

	A3dStepDelayPair(pSource);
	A3dStepEarGains(pSource);
	A3dStepEq(pSource);
	A3dStepMidGain(pSource);

	A3dStepPitchOut(pSource);
}

#ifdef _DEBUG
/* Debug dp_off poll counter; (RE) dbg:0x10152A4C. */
static DWORD	g_dwStepEndPoll = 0;
static DWORD	g_dwDpOff       = 0;

LSTATUS	ReadA3dRegistryDword(LPCSTR lpValueName, LPBYTE lpData);
#endif

/* =============================================================
// A3dSourceStepEnd()
// (RE) rtl:0x1000FDB0; dbg:0x10023F20; thunk dbg:0x100027D4
//
// Finalize and submit changed or forced control data, then swap buffers.
// Play, Stop and A3dSourceEmit force sends; their 677 conditions are unknown.
// =============================================================*/

void
A3dSourceStepEnd(CA3dSource *pSource, int fForce)
{
LPA3DCTRL_SRC_SUPER pCtrl;
LPA3DCTRL_SRC_SUPER pOther;
A3DVAL              fListenerGain;
int                 nBuffer;
int                 fChanged;

	PushRenderMode(pSource);

	pCtrl = pSource->m_pBufferCurr;

	if (pSource->m_dwRenderMode & A3DSOURCE_RENDERMODE_MONO)
		pCtrl->dwMode = 2;
	else
		pCtrl->dwMode = 1;

	if (pCtrl->fDistance < A3D_NEAR_FIELD_DISTANCE &&
	    !pSource->m_pApi->m_dwCompatWalkNear &&
	    !(pSource->m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE))
	{

		/* The original discards this output gain. */
		((IA3d3 *) pSource->m_pApi)->GetOutputGain(&fListenerGain);

		pCtrl->fAlpha  = 1.0f - pSource->m_fEqCoefficient;
		pCtrl->fNative = 1.0f;

		pCtrl->LeftEar.fAzim  =  A3D_HALF_PI;
		pCtrl->LeftEar.fElev  =  0.0f;
		pCtrl->LeftEar.fDelay =  0.0f;

		pCtrl->RightEar.fAzim  = -A3D_HALF_PI;
		pCtrl->RightEar.fElev  =  0.0f;
		pCtrl->RightEar.fDelay =  0.0f;
	}

#ifdef _DEBUG
	if ((g_dwStepEndPoll++ % A3D_SOURCE_DP_OFF_POLL_CALLS) == 0)
		ReadA3dRegistryDword("dp_off", (LPBYTE) &g_dwDpOff);

	if (g_dwDpOff)
	{
		pCtrl->LeftEar.fGain  = 0.0f;
		pCtrl->RightEar.fGain = 0.0f;
	}
#endif

	if (pSource->m_pApi->m_fMuteEarGains)
	{
		pCtrl->LeftEar.fGain  = 0.0f;
		pCtrl->RightEar.fGain = 0.0f;
	}

	fChanged = 1;

	if (!fForce)
	{
		pOther = pSource->m_apBuffer[pSource->m_nBuffer == 0];

		fChanged = memcmp(pCtrl, pOther, sizeof(A3DCTRL_SRC_SUPER));
	}

	if (fChanged || fForce)
	{
		if (pSource->m_pAurealBuffer)
		{
			pSource->m_pAurealBuffer->SetA3dSuperCtrl(
				pSource->m_pBufferCurr,
				sizeof(A3DCTRL_SRC_SUPER));
		}
		else if (pSource->m_pBuffer3D)
		{
			A3dSourcePushLocal(pSource);
		}

		nBuffer = (pSource->m_nBuffer == 0);

		pSource->m_nBuffer     = nBuffer;
		pSource->m_pBufferCurr = pSource->m_apBuffer[nBuffer];

		pSource->m_pBufferCurr->bExecuted = FALSE;
	}
}

/* =============================================================
// PushRenderMode()
// (RE) rtl:0x1000FF20; dbg:0x10024260; thunk dbg:0x10001924
//
// Apply a pending native-mode change to the Aureal or DirectSound3D buffer.
// =============================================================*/

void
PushRenderMode(CA3dSource *pSource)
{
IResManBuffer *pVoiceCtl;
DWORD          dwMode;

	if (!pSource->m_dwNativeModeDirty)
		return;

	if (pSource->m_pAurealBuffer)
	{
		/* Preserve the original unused native-mode local. */
		dwMode    = pSource->m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE;
		pVoiceCtl = NULL;

		if (SUCCEEDED(pSource->m_pAurealBuffer->QueryInterface(
					IID_A3dVoiceCtl, (void **) &pVoiceCtl)))
		{
			if (pSource->m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE)
				pVoiceCtl->SetNativeModeDisabled(0);
			else
				pVoiceCtl->SetNativeModeDisabled(1);

			pVoiceCtl->Release();
		}
	}
	else if (pSource->m_pBuffer3D)
	{
		if (pSource->m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE)
			dwMode = DS3DMODE_DISABLE;
		else
			dwMode = DS3DMODE_NORMAL;

		pSource->m_pBuffer3D->SetMode(dwMode, DS3D_IMMEDIATE);
	}

	pSource->m_dwNativeModeDirty = 0;
}

/* =============================================================
// A3dSourcePushLocal()
// (RE) rtl:0x1000FFD0; dbg:0x10024390
//
// Convert control data to DirectSound3D position, volume and frequency.
// The legacy sample-rate alias still has an unverified 677 offset.
// =============================================================*/

void
A3dSourcePushLocal(CA3dSource *pSource)
{
DS3DBUFFER          ds3d;
LPA3DCTRL_SRC_SUPER pCtrl;
A3DVAL              fSin, fCos, fSinUp;
A3DVAL              fGain;
LONG                lVolume;

	pCtrl = pSource->m_pBufferCurr;

	ZeroMemory(&ds3d, sizeof(ds3d));

	fSin   = (A3DVAL) sin(pCtrl->LeftEar.fAzim);
	fCos   = (A3DVAL) cos(pCtrl->LeftEar.fAzim);
	fSinUp = (A3DVAL) sin(pCtrl->LeftEar.fElev);

	/* The original computes and discards the right-ear direction. */
	sin(pCtrl->RightEar.fAzim);
	cos(pCtrl->RightEar.fAzim);
	sin(pCtrl->RightEar.fElev);

	fGain = (pCtrl->LeftEar.fGain + pCtrl->RightEar.fGain) * 0.5f;

	ds3d.dwSize = sizeof(ds3d);
	ds3d.dwMode = DS3DMODE_HEADRELATIVE;

	ds3d.vPosition.x = (-fSin - fSin) * 0.5f;
	ds3d.vPosition.y = (fSinUp + fSinUp) * 0.5f;
	ds3d.vPosition.z = (fCos + fCos) * 0.5f;

	ds3d.flMinDistance = 0.5f;
	ds3d.flMaxDistance = 2.0f;

	pSource->m_pBuffer3D->SetAllParameters(&ds3d, DS3D_IMMEDIATE);

	if (fGain == 0.0f)
		lVolume = -10000;
	else
		lVolume = (LONG) (log10((double) fGain) * 0.2 * 10000.0);

	pSource->m_pBuffer->SetVolume(lVolume);

	/* (RE) dbg:0x10024579 reads the rate through m_pwfxFormat, not the
	 * carried 2.02 alias at 0x308, and does not guard the pointer. */
	pSource->m_pBuffer->SetFrequency(
		(DWORD) ((double) pSource->m_pwfxFormat->nSamplesPerSec *
			 pCtrl->fFreqFactor));
}

/* =============================================================
// LoadWaveFile()
// (RE) rtl:0x10010E20; dbg:0x100265B0
//
// Load a RIFF WAVE file and retain its name if none is stored.
//
// Returns: OpenMmioStream/ReadWaveData result; E_POINTER for a null name;
//          A3DERROR_SOURCE_IN_USE for an existing buffer with IA3d5 or later;
//          A3DERROR_MEMORY_ALLOCATION if the filename copy fails.
// =============================================================*/

STDMETHODIMP
CA3dSource::LoadWaveFile(LPSTR pszFileName)
{
	HMMIO    hmmio;
	MMIOINFO mmioinfo;
	MMCKINFO mmckiParent;
	HRESULT  hr;

	if (!pszFileName)
		return (E_POINTER);

	if (m_pBuffer && m_pApi->m_dwInterfaceVersion > 4)
		return (A3DERROR_SOURCE_IN_USE);

	ZeroMemory(&mmioinfo, sizeof(mmioinfo));

	hr = OpenMmioStream(pszFileName, &hmmio, &mmioinfo, MMIO_ALLOCBUF,
			    &mmckiParent);

	if (SUCCEEDED(hr))
	{
		hr = ReadWaveData(&hmmio, &mmckiParent);

		if (SUCCEEDED(hr) && !m_SourceCaps.szFilename)
		{
			m_SourceCaps.szFilename =
				(char *) operator new(strlen(pszFileName) + 1);

			if (!m_SourceCaps.szFilename)
			{
				DBGSTR("CA3dSource::LoadWaveFile - Unable to allocate memory for filename.\n");

				return (A3DERROR_MEMORY_ALLOCATION);
			}

			strcpy(m_SourceCaps.szFilename, pszFileName);
		}
	}

	return (hr);
}

/* =============================================================
// LoadWaveData()
// (RE) rtl:0x10010F10; dbg:0x10026730
//
// Load a RIFF WAVE image from memory.
//
// Returns: OpenMmioStream/ReadWaveData result; E_POINTER for null data;
//          E_INVALIDARG for zero size; A3DERROR_SOURCE_IN_USE for an existing
//          buffer with IA3d5 or later.
// =============================================================*/

STDMETHODIMP
CA3dSource::LoadWaveData(LPVOID pvData, DWORD dwSize)
{
	HMMIO    hmmio;
	MMIOINFO mmioinfo;
	MMCKINFO mmckiParent;
	HRESULT  hr;

	if (!pvData)
		return (E_POINTER);

	if (!dwSize)
		return (E_INVALIDARG);

	if (m_pBuffer && m_pApi->m_dwInterfaceVersion > 4)
		return (A3DERROR_SOURCE_IN_USE);

	ZeroMemory(&mmioinfo, sizeof(mmioinfo));
	mmioinfo.fccIOProc = FOURCC_MEM;
	mmioinfo.pchBuffer = (HPSTR) pvData;
	mmioinfo.cchBuffer = dwSize;

	/* The memory I/O procedure requires read/write mode. */
	hr = OpenMmioStream(NULL, &hmmio, &mmioinfo, MMIO_READWRITE, &mmckiParent);

	if (SUCCEEDED(hr))
		return (ReadWaveData(&hmmio, &mmckiParent));

	return (hr);
}

/* =============================================================
// ReadWaveData()
// (RE) rtl:0x10011150; dbg:0x10026AD0; thunk dbg:0x100039FE
//
// Allocate and fill a buffer from the WAVE data chunk, then close the stream.
//
// Returns:
//   S_OK
//   A3DERROR_UNRECOGNIZED_FORMAT       a missing chunk or failed read
//   A3DERROR_FAILED_ALLOCATE_WAVEDATA  if allocation fails
// =============================================================*/

HRESULT
CA3dSource::ReadWaveData(HMMIO *phmmio, LPMMCKINFO pmmckiParent)
{
	MMCKINFO mmcki;
	MMIOINFO mmioinfo;
	HPSTR    pvLock1;
	void    *pvLock2;
	DWORD    dwLocked1;
	DWORD    dwLocked2;

	mmioSeek(*phmmio, pmmckiParent->dwDataOffset + 4, SEEK_SET);

	mmcki.ckid = mmioFOURCC('d', 'a', 't', 'a');

	if (mmioDescend(*phmmio, &mmcki, pmmckiParent, MMIO_FINDCHUNK))
	{
		mmioClose(*phmmio, 0);

		return (A3DERROR_UNRECOGNIZED_FORMAT);
	}

	m_dwWaveSize = mmcki.cksize;

	if (FAILED(AllocateWaveData(m_dwWaveSize)))
	{
		mmioClose(*phmmio, 0);

		return (A3DERROR_FAILED_ALLOCATE_WAVEDATA);
	}

	pvLock1 = NULL;

	/* The original ignores Lock failure. */
	m_pBuffer->Lock(0, m_dwWaveSize, (LPVOID *) &pvLock1, &dwLocked1,
			&pvLock2, &dwLocked2, 0);

	mmioGetInfo(*phmmio, &mmioinfo, 0);

	if (mmioRead(*phmmio, pvLock1, m_dwWaveSize) == -1)
	{
		mmioClose(*phmmio, 0);

		return (A3DERROR_UNRECOGNIZED_FORMAT);
	}

	mmioSetInfo(*phmmio, &mmioinfo, 0);

	m_pBuffer->Unlock(pvLock1, dwLocked1, pvLock2, dwLocked2);
	m_pBuffer->SetCurrentPosition(0);
	mmioClose(*phmmio, 0);

	return (S_OK);
}

/* =============================================================
// Play()
// (RE) rtl:0x10012D30; dbg:0x10029DA0
//
// Start playback, or mark it pending while API rendering is disabled.
//
// Returns:
//   S_OK
//   A3DERROR_NO_WAVE_DATA  without a buffer
//   E_INVALIDARG           a mode other than 0 or 1
//   A3DERROR_FAILED_PLAY   if buffer playback fails
// =============================================================*/

STDMETHODIMP
CA3dSource::Play(INT nMode)
{
A3DVAL av[3];
DWORD  fLoop;
DWORD  dwStatus;
DWORD  dwWhere;

	if (!m_pBuffer)
		return (A3DERROR_NO_WAVE_DATA);

	fLoop = 0;

	if (nMode)
	{
		if (nMode != A3D_LOOPED)
			return (E_INVALIDARG);

		fLoop |= DSBPLAY_LOOPING;
	}

	if (m_fStreaming)
	{
		GetStatus(&dwStatus);

		if ((dwStatus & (A3DSTATUS_PLAYING_DIRECTPATH |
				A3DSTATUS_PLAYING_REFLECTION |
				A3DSTATUS_WAITING_FOR_FLUSH)) == 0 &&
		    !m_adwStreamSegmentValid[0])
			Rewind();
	}

	if (m_pApi->m_fRenderingEnabled)
	{
		m_dwPlayState = A3D_PLAYSTATE_IDLE;

		av[0] = 0.0f;
		av[1] = 0.0f;
		av[2] = 0.0f;

		A3dSourceStep(this, av, av);
		A3dSourceStepEnd(this, 1);

		if (m_pApi->m_fCompatAu8830)
		{

			/* The original aliases both cursor outputs and discards the value. */
			dwWhere = 0;
			m_pBuffer->GetCurrentPosition(&dwWhere, &dwWhere);

			if (FAILED(m_pBuffer->Play(0, 0, fLoop)))
				return (A3DERROR_FAILED_PLAY);

			A3dSourceStepEnd(this, 1);
		}
		else
		{
			if (FAILED(m_pBuffer->Play(0, 0, fLoop)))
				return (A3DERROR_FAILED_PLAY);
		}

		m_dwPlayState = A3D_PLAYSTATE_PLAYING;
	}
	else
	{
		m_dwPlayState = A3D_PLAYSTATE_PENDING;
	}

	if (m_fStreaming)
	{
		m_dwPlayFlags   = fLoop;
		m_fLooping      = 1;
		m_dwStreamEnded = 0;
	}
	else
	{
		m_fLooping = fLoop;
	}

	m_dwTraceCount = 0;

	return (S_OK);
}

/* =============================================================
// A3dSourceTraceSkip()
// (RE) dbg:0x10029D00; thunk dbg:0x10004282
// Retail mapping from funcmap.tsv, medium confidence: rtl:0x10012CE0
//
// Test whether a completed one-shot source should be omitted from tracing.
//
// Returns: Nonzero to skip; 0 to retain the source.
// =============================================================*/

int
A3dSourceTraceSkip(CA3dSource *pSource)
{
	DWORD	dwState;

	if (pSource->m_dwPlayState != A3D_PLAYSTATE_PLAYING)
		return (0);

	if (pSource->m_fStreaming)
	{
		if ((pSource->m_dwPlayFlags & DSBPLAY_LOOPING) != 0)
			return (0);
	}
	else
	{
		if ((pSource->m_fLooping & DSBPLAY_LOOPING) != 0)
			return (0);
	}

	if (!pSource->m_pA3dVoiceCtl)
		return (0);

	((IA3dVoiceCtl *) pSource->m_pA3dVoiceCtl)->Ctl_GetState(&dwState);

	return (dwState != 1);
}

/* =============================================================
// A3dSourceEmit()
// (RE) rtl:0x10012F50; dbg:0x10029FF0; thunk dbg:0x10002C3E
//
// Start a source marked pending by Play.
//
// Returns:
//   S_OK
//   A3DERROR_NO_WAVE_DATA  without a buffer
//   A3DERROR_FAILED_PLAY   if buffer playback fails
// =============================================================*/

HRESULT
A3dSourceEmit(CA3dSource *pSource)
{
	if (!pSource->m_pBuffer)
		return (A3DERROR_NO_WAVE_DATA);

	if (pSource->m_dwPlayState != A3D_PLAYSTATE_PENDING)
		return (S_OK);

	if (!pSource->m_pApi->IsCompat1011Set())
	{
		if (FAILED(pSource->m_pBuffer->Play(0, 0,
						    pSource->m_fLooping)))
		{
			return (A3DERROR_FAILED_PLAY);
		}

		pSource->m_dwPlayState = A3D_PLAYSTATE_PLAYING;

		return (S_OK);
	}

	{
		DWORD dwWhere;

		/* The original aliases both cursor outputs and discards the value. */
		pSource->m_pBuffer->GetCurrentPosition(&dwWhere, &dwWhere);
	}

	if (FAILED(pSource->m_pBuffer->Play(0, 0, pSource->m_fLooping)))
		return (A3DERROR_FAILED_PLAY);

	A3dSourceStepEnd(pSource, 1);

	pSource->m_dwPlayState = A3D_PLAYSTATE_PLAYING;

	return (S_OK);
}

/* =============================================================
// Stop()
// (RE) rtl:0x10012FE0; dbg:0x1002A100
//
// Stop playback, clear AU8830 renderer state and rewind streaming sources.
//
// Returns:
//   S_OK
//   A3DERROR_NO_WAVE_DATA  without a buffer
//   A3DERROR_FAILED_STOP   if the buffer cannot stop
// =============================================================*/

STDMETHODIMP
CA3dSource::Stop(void)
{
	if (!m_pBuffer)
		return (A3DERROR_NO_WAVE_DATA);

	m_dwPlayState = A3D_PLAYSTATE_IDLE;

	if (m_pApi->m_fCompatAu8830)
	{
		A3dSourceResetReflections(this);
		A3dSourceStepEnd(this, 1);

		A3dSourceQuiet(this);
		A3dSourceStepEnd(this, 1);
	}

	if (FAILED(m_pBuffer->Stop()))
		return (A3DERROR_FAILED_STOP);

	if (m_fStreaming)
		Rewind();

	return (S_OK);
}

/* =============================================================
// AllocateWaveData()
// (RE) rtl:0x100130C0; dbg:0x1002A1D0
//
// Create a wave buffer and initialize its control data.
// This and AllocateAudioData cite one body;
// the original slot/name mapping is unresolved.
//
// Returns: S_OK; E_INVALIDARG for fewer than 1 byte;
//          A3DERROR_NEEDS_FORMAT_INFORMATION for a missing format or zero
//          sample rate; A3DERROR_SOURCE_IN_USE for an existing buffer with
//          IA3d5 or later; resource-manager mode failure;
//          A3DERROR_FAILED_CREATE_SOUNDBUFFER.
// =============================================================*/

STDMETHODIMP
CA3dSource::AllocateWaveData(INT nBytes)
{
DSBUFFERDESC dsbd;

	if (nBytes < 1)
		return (E_INVALIDARG);

	if (!m_pwfxFormat || !m_pwfxFormat->nSamplesPerSec)
		return (A3DERROR_NEEDS_FORMAT_INFORMATION);

	if (m_pBuffer && m_pApi->m_dwInterfaceVersion > 4)
		return (A3DERROR_SOURCE_IN_USE);

	ZeroMemory(&dsbd, sizeof(dsbd));

	dsbd.dwSize  = sizeof(dsbd);
	dsbd.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_CTRLVOLUME |
		       DSBCAPS_CTRLPAN | DSBCAPS_CTRLFREQUENCY;

	if ((m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE) == 0)
		dsbd.dwFlags |= DSBCAPS_CTRL3D;

	if ((m_dwFlags & A3DSOURCE_TYPESTREAMED) == 0)
		dsbd.dwFlags |= DSBCAPS_STATIC;

	if (m_pApi->m_fCompatAu8830)
		dsbd.dwFlags |= DSBCAPS_GLOBALFOCUS;

	dsbd.dwBufferBytes = nBytes;
	m_dwWaveSize       = nBytes;
	dsbd.lpwfxFormat   = m_pwfxFormat;

	if (m_pApi->m_fCompatAu8830 &&
	    m_pwfxFormat->nSamplesPerSec < A3D_RESAMPLE_BELOW)
	{
		DWORD dwRate;

		dwRate                          = m_pwfxFormat->nSamplesPerSec;
		m_pwfxFormat->nSamplesPerSec    = A3D_RESAMPLE_RATE;
		m_fRateRatio                    = (A3DVAL) ((double) dwRate / (double) A3D_RESAMPLE_RATE);
	}

	if ((m_dwFlags & A3DSOURCE_TYPEUNMANAGED) || m_fStreaming)
	{
		DWORD   dwSavedMode;
		HRESULT hr;

		dwSavedMode = 0;

		hr = m_pApi->GetResManMode(&dwSavedMode);
		if (FAILED(hr))
		{
			DBGSTR("CA3dSource::AllocateAudioData() - Failed call to GetResourceManagerMode().\n");
			return (hr);
		}

		hr = m_pApi->SetResManMode(A3D_RESOURCE_MODE_OFF);
		if (FAILED(hr))
		{
			DBGSTR("CA3dSource::AllocateAudioData() - Failed call to SetResourceManagerMode().\n");
			return (hr);
		}

		if (FAILED(m_pDS->CreateSoundBuffer(&dsbd, &m_pBuffer, NULL)))
		{
			DBGSTR("CA3dSource::AllocateAudioData() - Failed call to CreateSoundBuffer().\n");
			m_pBuffer = NULL;
			return (A3DERROR_FAILED_CREATE_SOUNDBUFFER);
		}

		m_pApi->SetResManMode(dwSavedMode);
	}
	else
	{
		if (FAILED(m_pDS->CreateSoundBuffer(&dsbd, &m_pBuffer, NULL)))
		{
			DBGSTR("CA3dSource::AllocateAudioData() - Failed call to CreateSoundBuffer().\n");
			m_pBuffer = NULL;
			return (A3DERROR_FAILED_CREATE_SOUNDBUFFER);
		}
	}

	m_pBuffer->QueryInterface(IID_IDirectSound3DBuffer, (void **) &m_pBuffer3D);
	m_pBuffer->QueryInterface(IID_A3dVoiceCtl, (void **) &m_pA3dVoiceCtl);

	if (FAILED(m_pBuffer->QueryInterface(IID_IA3dBuffer, (void **) &m_pAurealBuffer)))
		m_pAurealBuffer = NULL;

	AllocBuffers();
	A3dSourceSeedDirect(this);

	return (S_OK);
}

/* =============================================================
// FreeWaveData()
//
// Release the wave buffer and its 3D and Aureal interfaces.
//
// Returns:
//   S_OK
//   A3DERROR_NO_WAVE_DATA  without a buffer
// =============================================================*/

STDMETHODIMP
CA3dSource::FreeWaveData(void)
{
	if (!m_pBuffer)
		return (A3DERROR_NO_WAVE_DATA);

	m_pBuffer->Release();
	m_pBuffer = NULL;

	if (m_pBuffer3D)
	{
		m_pBuffer3D->Release();
		m_pBuffer3D = NULL;
	}

	if (m_pAurealBuffer)
	{
		m_pAurealBuffer->Release();
		m_pAurealBuffer = NULL;
	}

	return (S_OK);
}

/* =============================================================
// SetWaveFormat()
//
// Store the base wave-format fields in the legacy aliases.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null format
// =============================================================*/

STDMETHODIMP
CA3dSource::SetWaveFormat(LPVOID pvFormat)
{
LPWAVEFORMATEX pwfx;

	if (!pvFormat)
		return (E_INVALIDARG);

	pwfx = (LPWAVEFORMATEX) pvFormat;

	m_wFormatTag       = pwfx->wFormatTag;
	m_wChannels        = pwfx->nChannels;
	m_dwSamplesPerSec  = pwfx->nSamplesPerSec;
	m_dwAvgBytesPerSec = pwfx->nAvgBytesPerSec;
	m_wBlockAlign      = pwfx->nBlockAlign;
	m_wBitsPerSample   = pwfx->wBitsPerSample;

	return (S_OK);
}

/* =============================================================
// GetWaveFormat()
//
// Copy the legacy wave-format fields to the caller; leave cbSize unchanged.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null format
// =============================================================*/

STDMETHODIMP
CA3dSource::GetWaveFormat(LPVOID pvFormat)
{
LPWAVEFORMATEX pwfx;

	if (!pvFormat)
		return (E_INVALIDARG);

	pwfx = (LPWAVEFORMATEX) pvFormat;

	pwfx->wFormatTag      = m_wFormatTag;
	pwfx->nChannels       = m_wChannels;
	pwfx->nSamplesPerSec  = m_dwSamplesPerSec;
	pwfx->nAvgBytesPerSec = m_dwAvgBytesPerSec;
	pwfx->nBlockAlign     = m_wBlockAlign;
	pwfx->wBitsPerSample  = m_wBitsPerSample;

	return (S_OK);
}

/* =============================================================
// Lock()
// (RE) rtl:0x10013620; dbg:0x1002A9A0
//
// Lock a resident wave-buffer region, expanding DSBLOCK_ENTIREBUFFER locally.
//
// Returns:
//   Buffer Lock result
//   A3DERROR_FAILED_LOCK_BUFFER  streaming sources
//   E_POINTER                    a null first pointer/length output
//   A3DERROR_NO_WAVE_DATA        without a buffer
//   E_INVALIDARG                 if dwBytes exceeds the wave size
// =============================================================*/

STDMETHODIMP
CA3dSource::Lock(DWORD dwOffset, DWORD dwBytes, LPVOID *ppvAudioPtr1,
		 LPDWORD pdwAudioBytes1, LPVOID *ppvAudioPtr2,
		 LPDWORD pdwAudioBytes2, DWORD dwFlags)
{
	if (m_fStreaming)
	{
		DBGSTR("CA3dSource::Lock() - Cannot lock streamed sources.\n");
		return (A3DERROR_FAILED_LOCK_BUFFER);
	}

	if (!ppvAudioPtr1 || !pdwAudioBytes1)
	{
		DBGSTR("CA3dSource::Lock() - lplpvAudioPtr1 and/or lpdwAudioBytes1 are invalid pointer(s).\n");
		return (E_POINTER);
	}

	if (!m_pBuffer)
	{
		DBGSTR("CA3dSource::Lock() - Can't lock; No audio data yet.\n");
		return (A3DERROR_NO_WAVE_DATA);
	}

	if (dwBytes > m_dwWaveSize)
	{
		DBGSTR("CA3dSource::Lock() - dwWriteBytes is larger than the actual buffer.\n");
		return (E_INVALIDARG);
	}

	if (dwFlags & DSBLOCK_ENTIREBUFFER)
		return (m_pBuffer->Lock(0, m_dwWaveSize, ppvAudioPtr1,
					pdwAudioBytes1, ppvAudioPtr2,
					pdwAudioBytes2,
					dwFlags & ~DSBLOCK_ENTIREBUFFER));

	return (m_pBuffer->Lock(dwOffset, dwBytes, ppvAudioPtr1,
				pdwAudioBytes1, ppvAudioPtr2,
				pdwAudioBytes2, dwFlags));
}

/* =============================================================
// Unlock()
// (RE) rtl:0x100136D0; dbg:0x1002AB00
//
// Unlock previously acquired wave-buffer regions.
//
// Returns:
//   Buffer Unlock result
//   A3DERROR_NO_WAVE_DATA  without a buffer
// =============================================================*/

STDMETHODIMP
CA3dSource::Unlock(LPVOID pvAudioPtr1, DWORD dwAudioBytes1, LPVOID pvAudioPtr2,
		   DWORD dwAudioBytes2)
{
	if (!m_pBuffer)
		return (A3DERROR_NO_WAVE_DATA);

	return (m_pBuffer->Unlock(pvAudioPtr1, dwAudioBytes1,
				  pvAudioPtr2, dwAudioBytes2));
}

/* =============================================================
// SetPriority()
// (RE) rtl:0x10013700; dbg:0x1002AB60
//
// Store source priority in the range 0..1.
//
// Returns:
//   S_OK
//   E_INVALIDARG  outside the range
// =============================================================*/

STDMETHODIMP
CA3dSource::SetPriority(A3DVAL fPriority)
{
	if (fPriority < 0.0f || fPriority > 1.0f)
		return (E_INVALIDARG);

	m_fPriority = fPriority;

	return (S_OK);
}

/* =============================================================
// GetPriority()
// (RE) rtl:0x10013740; dbg:0x1002ABC0
//
// Read source priority.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dSource::GetPriority(LPA3DVAL pfPriority)
{
	if (!pfPriority)
		return (E_POINTER);

	*pfPriority = m_fPriority;

	return (S_OK);
}

/* =============================================================
// GetStatus()
// (RE) rtl:0x10013770; dbg:0x1002AC00
//
// Read playback status and add the enabled A3D placement and occlusion bits.
// A null voice interface crashes. This body uses a -8 adjustment where the
// 677 conversion preserves null.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dSource::GetStatus(LPDWORD pdwStatus)
{
	BOOL                  fA3dMode;
	DalBufferInfo        *pDalBufferInfo;
	IResManBufferPrimary *pPrimary;

	if (!pdwStatus)
		return (E_POINTER);

	if (m_dwPlayState == A3D_PLAYSTATE_IDLE)
	{
		*pdwStatus = 0;
		return (S_OK);
	}

	if (m_dwPlayState == A3D_PLAYSTATE_PENDING)
	{
		*pdwStatus = A3DSTATUS_WAITING_FOR_FLUSH;
		return (S_OK);
	}

	if (m_pBuffer)
		m_pBuffer->GetStatus(pdwStatus);
	else
		*pdwStatus = 0;

	fA3dMode = (m_pApi->m_dwInterfaceVersion > 4);
	if (!fA3dMode && !m_pApi->m_fForceStatusBits)
		return (S_OK);

	*pdwStatus &= A3D_SOURCE_STATUS_PRESERVE_MASK;

	pDalBufferInfo  = NULL;
	pPrimary        = (IResManBufferPrimary *) ((BYTE *) m_pA3dVoiceCtl - 8);
	pPrimary->GetDalBufferInfo(&pDalBufferInfo);

	if (pDalBufferInfo)
	{
		if (pDalBufferInfo->ReportsHardwareStatus())
		{
			IA3dDalBuffer2	*pDalBuffer2;

			if (fA3dMode)
				*pdwStatus |= A3DSTATUS_HARDWARE;

			pDalBuffer2 = NULL;
			pDalBufferInfo->GetIDalBuffer()->QueryInterface(
				IID_IA3dDalBuffer2, (void **) &pDalBuffer2);
			if (pDalBuffer2)
			{
				DWORD dwPlay, dwWrite;	/* Uninitialized outputs, as in the original. */
				DWORD dwOccluded;

				pDalBuffer2->GetCurrentPositionEx(&dwPlay, &dwWrite,
								  &dwOccluded);
				if (dwOccluded)
					*pdwStatus |= A3DSTATUS_PLAYING_REFLECTION;

				pDalBuffer2->Release();
			}
		}
		else if (fA3dMode && pDalBufferInfo->ReportsSoftwareStatus())
		{
			*pdwStatus |= A3DSTATUS_SOFTWARE;
		}
		else if (fA3dMode && !pDalBufferInfo->UsesLockedBufferFill())
		{
			*pdwStatus |= A3DSTATUS_VIRTUAL;
		}
	}

	return (S_OK);
}

/* =============================================================
// Unknown_0x0C()
// (RE) dbg:0x1002CE90; thunk dbg:0x10003B7A
//
// Validate the argument pointer without reading it; its purpose is unknown.
//
// Returns:
//   S_OK
//   E_POINTER  a null argument
// =============================================================*/

STDMETHODIMP
CA3dSource::Unknown_0x0C(LPVOID pv)
{
	if (!pv)
		return (E_POINTER);

	return (S_OK);
}

/* =============================================================
// A3dReflectionNew()
// (RE) dbg:0x1002B010; thunk dbg:0x100037BF
//
// Allocate and prepend a zeroed record to the current reflection chain.
//
// Returns: The new record; NULL if allocation fails.
// =============================================================*/

LPA3DREFLECTIONREC
A3dReflectionNew(CA3dSource *pSource)
{
LPA3DREFLECTIONREC pLink;

	pLink = (LPA3DREFLECTIONREC) calloc(1, sizeof(A3DREFLECTIONREC));

	if (!pLink)
		return (NULL);

	pLink->pNext = pSource->m_apChain[pSource->m_nChain];

	pSource->m_apChain[pSource->m_nChain] = pLink;

	pSource->m_acChain[pSource->m_nChain]++;

	return (pLink);
}

/* =============================================================
// A3dChainNext()
// (RE) dbg:0x1002B0A0
//
// Advance through the current reflection chain.
//
// Returns: The next record, or the chain head when pLink is null.
// =============================================================*/

LPA3DREFLECTIONREC
A3dChainNext(CA3dSource *pSource, LPA3DREFLECTIONREC pLink)
{
	if (pLink)
		return (pLink->pNext);

	return (pSource->m_apChain[pSource->m_nChain]);
}

/* =============================================================
// A3dSourceRecalc()
// (RE) rtl:0x10013970; dbg:0x1002B0E0; thunk dbg:0x100026D5
//
// Switch reflection chains and free the newly selected chain for reuse.
// =============================================================*/

void
A3dSourceRecalc(CA3dSource *pSource)
{
void **ppLink;
void **ppNext;
int    nChain;

	nChain = (pSource->m_nChain == 0);

	pSource->m_nChain = nChain;

	ppLink = (void **) pSource->m_apChain[nChain];

	pSource->m_acChain[nChain] = 0;

	if (!ppLink)
		return;

	while (*ppLink)
	{
		ppNext = (void **) *ppLink;

		if (ppLink == (void **) pSource->m_apChain[pSource->m_nChain])
			pSource->m_apChain[pSource->m_nChain] = NULL;

		free(ppLink);

		ppLink = ppNext;
	}

	if (ppLink == (void **) pSource->m_apChain[pSource->m_nChain])
		pSource->m_apChain[pSource->m_nChain] = NULL;

	free(ppLink);
}

/* =============================================================
// CA3dSource::ReflectionGains()
// (RE) dbg:0x1002B1E0; thunk dbg:0x100043F9
//
// Write broadband and high-frequency reflection gains and their mean.
//
// Returns: The broadband gain, also written through pfGain.
// =============================================================*/

A3DVAL
CA3dSource::ReflectionGains(A3DVAL fHit, A3DVAL fDistance,
			    const A3DVAL *pavMaterial, A3DVAL *pfMean,
			    A3DVAL *pfEq, A3DVAL *pfGain)
{
A3DVAL fOutputGain;
A3DVAL fDistGain;
A3DVAL fSpreadGain;
A3DVAL fGain;
A3DVAL fEq;

	((IA3d3 *) m_pApi)->GetOutputGain(&fOutputGain);

	A3dDistanceGain(this, fDistance, &fDistGain);
	A3dSpreadGain(this, fDistance, &fSpreadGain);

	fGain = m_fReflectionGainScale * m_pApi->m_fGlobalReflectionGainScale *
		fOutputGain * fHit * pavMaterial[0] * fDistGain * m_fGain;

	fEq = fHit * pavMaterial[1] * fSpreadGain * m_fEqCoefficient;

	if (m_pApi->m_fTintReflections)
	{
		fGain = (m_afOcclusionValue[0] * 0.65f + 0.35f) * fGain;
		fEq   = (m_afOcclusionValue[1] * 0.65f + 0.35f) * fEq;
	}

	if (fGain < 0.0f)
		fGain = 0.0f;
	else if (fGain > 1.0f)
		fGain = 1.0f;

	if (fEq < 0.0f)
		fEq = 0.0f;
	else if (fEq > 1.0f)
		fEq = 1.0f;

	*pfMean = fGain * 0.5f + fEq * 0.5f;
	*pfEq   = fEq;
	*pfGain = fGain;

	return (fGain);
}

/* =============================================================
// A3dReflection()
// (RE) dbg:0x1002B8C0
//
// Create a reflection record with direction, material gains and ear delays.
// The reference is __thiscall; this helper takes the source explicitly.
// Selection between this and the four-argument builder at dbg:0x1002B3B0
// (thunk dbg:0x1000185C) is unresolved.
// =============================================================*/

void
A3dReflection(CA3dSource *pSource, DWORD dwSurface, DWORD dwSurface2,
	      DWORD dwKind, const A3DVAL *pvHit, A3DVAL fHit,
	      const A3DVAL *pavMaterial, const A3DVAL *pavTruePoint,
	      const A3DVAL *pavSoftPoint)
{
LPA3DREFLECTIONREC pRec;
A3DVAL             fOutputGain;
A3DVAL             fDistGain;
A3DVAL             fSpreadGain;
A3DVAL             avXform[4];
A3DVAL             fSpeed;

	((IA3d3 *) pSource->m_pApi)->GetOutputGain(&fOutputGain);

	pRec = A3dReflectionNew(pSource);

	pRec->dwSurface = dwSurface;
	pRec->dwKind    = dwKind;

	if (dwKind == 1)
		pRec->dwSurface2 = dwSurface;
	else
		pRec->dwSurface2 = dwSurface2;

	pRec->nSlot = 0;

	pRec->vHit[0] = pvHit[0];
	pRec->vHit[1] = pvHit[1];
	pRec->vHit[2] = pvHit[2];

	avXform[0] = pvHit[0];
	avXform[1] = pvHit[1];
	avXform[2] = pvHit[2];
	avXform[3] = 0.0f;

	A3dTraceTransformDir(avXform, pSource->m_pApi->m_matListenerXform, avXform);

	A3dToPolar(avXform, &pRec->fAzim);

	pRec->fDistance = pRec->fRange;

	pRec->fHit = fHit;

	pRec->afMaterial[0] = pavMaterial[0];
	pRec->afMaterial[1] = pavMaterial[1];

	pRec->avTruePoint[0] = pavTruePoint[0];
	pRec->avTruePoint[1] = pavTruePoint[1];
	pRec->avTruePoint[2] = pavTruePoint[2];

	pRec->avSoftPoint[0] = pavSoftPoint[0];
	pRec->avSoftPoint[1] = pavSoftPoint[1];
	pRec->avSoftPoint[2] = pavSoftPoint[2];

	A3dDistanceGain(pSource, pRec->fDistance, &fDistGain);
	A3dSpreadGain(pSource, pRec->fDistance, &fSpreadGain);

	pRec->fEq = pRec->fHit * pRec->afMaterial[1] * fSpreadGain *
		    pSource->m_fEqCoefficient;

	if (pRec->fEq < 0.0f)
		pRec->fEq = 0.0f;
	else if (pRec->fEq > 1.0f)
		pRec->fEq = 1.0f;

	pRec->fGainLeft = pSource->m_fReflectionGainScale *
			  pSource->m_pApi->m_fGlobalReflectionGainScale * fOutputGain *
			  pRec->fHit * pRec->afMaterial[0] * fDistGain *
			  pSource->m_fGain;

	pRec->fGainRight = pSource->m_fReflectionGainScale *
			   pSource->m_pApi->m_fGlobalReflectionGainScale * fOutputGain *
			   pRec->fHit * pRec->afMaterial[0] * fDistGain *
			   pSource->m_fGain;

	if (pSource->m_pApi->m_fTintReflections)
	{
		pRec->fGainLeft  = (pSource->m_afOcclusionValue[0] * 0.65f + 0.35f) *
				   pRec->fGainLeft;
		pRec->fGainRight = (pSource->m_afOcclusionValue[0] * 0.65f + 0.35f) *
				   pRec->fGainRight;
		pRec->fEq        = (pSource->m_afOcclusionValue[1] * 0.65f + 0.35f) *
				   pRec->fEq;
	}

	if (pRec->fGainLeft < 0.0f)
		pRec->fGainLeft = 0.0f;
	else if (pRec->fGainLeft > 1.0f)
		pRec->fGainLeft = 1.0f;

	if (pRec->fGainRight < 0.0f)
		pRec->fGainRight = 0.0f;
	else if (pRec->fGainRight > 1.0f)
		pRec->fGainRight = 1.0f;

	fSpeed = 340.5f * pSource->m_pApi->m_fUnitsPerMeter;

	pRec->fDelayLeft = pSource->m_fReflectionDelayScale *
			   pSource->m_pApi->m_fGlobalReflectionDelayScale *
			   (pRec->fDistance - pSource->m_fEarLeft) / fSpeed;

	pRec->fDelayRight = pSource->m_fReflectionDelayScale *
			    pSource->m_pApi->m_fGlobalReflectionDelayScale *
			    (pRec->fDistance - pSource->m_fEarRight) / fSpeed;

	pRec->fAudibility = (pRec->fGainLeft + pRec->fGainRight) * 0.25f +
			    pRec->fEq * 0.5f;

	if (pRec->fDelayLeft < 0.0f)
		pRec->fDelayLeft = 0.0f;

	if (pRec->fDelayRight < 0.0f)
		pRec->fDelayRight = 0.0f;

	if (pRec->fDelayLeft > pSource->m_pApi->m_fMaxReflectionDelayTime)
		pRec->fDelayLeft = pSource->m_pApi->m_fMaxReflectionDelayTime;

	if (pRec->fDelayRight > pSource->m_pApi->m_fMaxReflectionDelayTime)
		pRec->fDelayRight = pSource->m_pApi->m_fMaxReflectionDelayTime;
}


#define A3D_REFLECTION_SHARED_BUDGET            32
#define A3D_REFLECTION_MIN_SOURCE_BUDGET        4
#define A3D_REFLECTION_FORCE_UNMUTE             1
#define A3D_REFLECTION_FORCE_MUTE               2
#define A3D_REFLECTION_NEW_DELAY_THRESHOLD_S    0.02f

/* Reflection mute override: 1 clears, 2 sets, otherwise use bNew.
 * (RE) dbg:0x10152A60 is zero and has no writers in the reference image. */
static DWORD	g_dwReflectionMuteMode = 0;

/* =============================================================
// A3dSourcePushB()
// (RE) dbg:0x1002BE60
//
// Write audible, assigned reflections to the current control block within
// budget.
// =============================================================*/

void
A3dSourcePushB(CA3dSource *pSource, int cActive)
{
LPA3DCTRL_SRC_SUPER  pCtrl;
LPA3DCTRL_REFLECTION pSlot;
LPA3DREFLECTIONREC   pLink;
int                  cBudget;
int                  cDone;
int                  i;

	if (pSource->m_pApi->m_dwUseDalInterface)
	{
		cBudget = A3D_REFLECTION_SHARED_BUDGET / cActive;

		if (cBudget < A3D_REFLECTION_MIN_SOURCE_BUDGET)
			cBudget = A3D_REFLECTION_MIN_SOURCE_BUDGET;
		else if (cBudget > A3D_MAX_SOURCE_REFLECTIONS)
			cBudget = A3D_MAX_SOURCE_REFLECTIONS;
	}
	else
	{
		cBudget = A3D_MAX_SOURCE_REFLECTIONS;
	}

	pCtrl = pSource->m_pBufferCurr;

	for (i = 0; i < A3D_MAX_SOURCE_REFLECTIONS; i++)
	{
		pCtrl->Reflections[i].bEnable = FALSE;

		if (pSource->m_pApi->m_dwUseDalInterface)
			pCtrl->Reflections[i].bAvailable = FALSE;
	}

	cDone = 0;

	for (pLink = A3dChainNext(pSource, NULL);
	     pLink && cDone < cBudget;
	     pLink = A3dChainNext(pSource, pLink))
	{
		if (pLink->nSlot > -1)
		{
			pSlot = &pSource->m_pBufferCurr->Reflections[pLink->nSlot];

			if (pLink->fAudibility > 0.0f)
			{
				pSlot->bEnable = TRUE;

				if (pSource->m_pApi->m_dwUseDalInterface)
					pSlot->bAvailable = TRUE;

				if (g_dwReflectionMuteMode == A3D_REFLECTION_FORCE_UNMUTE)
					pSlot->bMute = FALSE;
				else if (g_dwReflectionMuteMode == A3D_REFLECTION_FORCE_MUTE)
					pSlot->bMute = TRUE;
				else
					pSlot->bMute = pLink->bNew;

				pSlot->dwReserved0C = 0;
				pSlot->fAudibility  = pLink->fAudibility;

				pSlot->fAlpha = 1.0f - pLink->fEq;

				if (pSlot->fAlpha > A3D_SOURCE_CTRL_ALPHA_MAX)
					pSlot->fAlpha = A3D_SOURCE_CTRL_ALPHA_MAX;

				pSlot->LeftEar.fAzim   = pLink->fAzim;
				pSlot->LeftEar.fElev   = pLink->fElev;
				pSlot->LeftEar.fGain   = pLink->fGainLeft;
				pSlot->LeftEar.fDelay  = pLink->fDelayLeft;
				pSlot->RightEar.fAzim  = pLink->fAzim;
				pSlot->RightEar.fElev  = pLink->fElev;
				pSlot->RightEar.fGain  = pLink->fGainRight;
				pSlot->RightEar.fDelay = pLink->fDelayRight;

				cDone++;
			}
		}
	}
}

/* =============================================================
// A3dSourceCarry()
// (RE) dbg:0x1002C130; thunk dbg:0x1000132F
//
// Copy the previous buffer's reflection slots to the current buffer.
// =============================================================*/

void
A3dSourceCarry(CA3dSource *pSource)
{
int nOther;

	nOther = (pSource->m_nBuffer == 0);

	CopyMemory(pSource->m_pBufferCurr->Reflections,
		   pSource->m_apBuffer[nOther]->Reflections,
		   sizeof(pSource->m_pBufferCurr->Reflections));
}

/* =============================================================
// A3dSourceResetReflections()
// (RE) dbg:0x1002C190
//
// Mute all reflection slots and zero their ear, audibility and filter values.
// =============================================================*/

void
A3dSourceResetReflections(CA3dSource *pSource)
{
LPA3DCTRL_SRC_SUPER pCtrl;
int                 i;

	pCtrl = pSource->m_pBufferCurr;

	for (i = 0; i < A3D_MAX_SOURCE_REFLECTIONS; i++)
	{
	LPA3DCTRL_REFLECTION	pSlot;

		pSlot = &pCtrl->Reflections[i];

		pSlot->bMute  = TRUE;
		pSlot->fAlpha = 0.0f;

		pSlot->LeftEar.fAzim   = 0.0f;
		pSlot->LeftEar.fElev   = 0.0f;
		pSlot->LeftEar.fGain   = 0.0f;
		pSlot->LeftEar.fDelay  = 0.0f;
		pSlot->RightEar.fAzim  = 0.0f;
		pSlot->RightEar.fElev  = 0.0f;
		pSlot->RightEar.fGain  = 0.0f;
		pSlot->RightEar.fDelay = 0.0f;

		pSlot->fAudibility = 0.0f;
	}
}

/* =============================================================
// A3dSourceQuiet()
// (RE) dbg:0x1002C280; thunk dbg:0x10002158
//
// Disable all reflection slots; clear availability on source-owned control
// blocks.
// =============================================================*/

void
A3dSourceQuiet(CA3dSource *pSource)
{
LPA3DCTRL_SRC_SUPER pCtrl;
int                 i;

	pCtrl = pSource->m_pBufferCurr;

	for (i = 0; i < A3D_MAX_SOURCE_REFLECTIONS; i++)
	{
		pCtrl->Reflections[i].bEnable = FALSE;

		if (pSource->m_pApi->m_dwUseDalInterface)
			pCtrl->Reflections[i].bAvailable = FALSE;
	}
}

/* =============================================================
// A3dSourcePushA()
// (RE) dbg:0x1002C710; thunk dbg:0x10001F14
//
// Assign reflection slots within budget, retaining matches from the previous
// chain.
// =============================================================*/

void
A3dSourcePushA(CA3dSource *pSource, int cActive)
{
DWORD dwUsed;
int   cWanted;
int   cUnplaced;

	if (!pSource->m_apChain[pSource->m_nChain])
		return;

	cWanted = A3dSourcePushBudget(pSource, cActive);

	cUnplaced = A3dSourcePushMatch(pSource, cWanted, &dwUsed);

	if (cUnplaced > 0)
		A3dSourcePushAssign(pSource, cUnplaced, dwUsed);
}

/* =============================================================
// A3dSourcePushBudget()
// (RE) dbg:0x1002C790
//
// Limit the current reflection count by the control-block allocation mode.
//
// Returns: The reflection count capped by the source budget.
// =============================================================*/

int
A3dSourcePushBudget(CA3dSource *pSource, int cActive)
{
int cChain;
int cBudget;

	cChain = pSource->m_acChain[pSource->m_nChain];

	if (pSource->m_pApi->m_dwUseDalInterface)
	{
		cBudget = A3D_REFLECTION_SHARED_BUDGET / cActive;

		if (cBudget < A3D_REFLECTION_MIN_SOURCE_BUDGET)
			cBudget = A3D_REFLECTION_MIN_SOURCE_BUDGET;
		else if (cBudget > A3D_MAX_SOURCE_REFLECTIONS)
			cBudget = A3D_MAX_SOURCE_REFLECTIONS;

		if (cChain > cBudget)
			return (cBudget);
	}
	else if (cChain > A3D_MAX_SOURCE_REFLECTIONS)
	{
		return (A3D_MAX_SOURCE_REFLECTIONS);
	}

	return (cChain);
}

/* =============================================================
// A3dSourcePushMatch()
// (RE) dbg:0x1002C830
//
// Reuse prior slots for matching reflections and write the occupied-slot mask.
//
// Returns: The number of requested reflections without a match.
// =============================================================*/

int
A3dSourcePushMatch(CA3dSource *pSource, int cWanted, DWORD *pdwUsed)
{
LPA3DREFLECTIONREC pLink;
LPA3DREFLECTIONREC pOther;
int                cUnplaced;
int                i;

	pLink  = pSource->m_apChain[pSource->m_nChain];
	pOther = pSource->m_apChain[pSource->m_nChain == 0];

	cUnplaced = 0;
	*pdwUsed  = 0;

	for (i = 0; i < cWanted; i++)
	{
		LPA3DREFLECTIONREC p;
		int                fFree;

		fFree = 1;

		p = pOther;

		while (p && fFree)
		{
			if (p->dwSurface == pLink->dwSurface &&
			    p->dwKind == pLink->dwKind)
			{
				*pdwUsed |= 1u << p->nSlot;

				pLink->nSlot = p->nSlot;

				if (pSource->m_pApi->m_dwUseDalInterface)
					pLink->bNew = FALSE;

				fFree = 0;
			}
			else
			{
				p = A3dChainNext(pSource, p);
			}
		}

		if (fFree)
		{
			cUnplaced++;

			pLink->nSlot = -1;
		}

		pLink = A3dChainNext(pSource, pLink);
	}

	return (cUnplaced);
}

/* =============================================================
// A3dSourcePushAssign()
// (RE) dbg:0x1002C9C0
//
// Assign slots to unmatched reflections by delay order.
// The original leaks either allocation when the other fails.
// =============================================================*/

void
A3dSourcePushAssign(CA3dSource *pSource, int cUnplaced, DWORD dwUsed)
{
LPA3DREFLECTIONREC *apMatched;
LPA3DREFLECTIONREC *apUnplaced;
LPA3DREFLECTIONREC  pLink;
LPA3DREFLECTIONREC  pOther;
LPA3DREFLECTIONREC  p;
int                 cMatched;
int                 cGathered;
int                 nFree;
int                 i;

	pLink  = pSource->m_apChain[pSource->m_nChain];
	pOther = pSource->m_apChain[pSource->m_nChain == 0];

	apMatched  = (LPA3DREFLECTIONREC *) calloc(cUnplaced, sizeof(LPA3DREFLECTIONREC));
	apUnplaced = (LPA3DREFLECTIONREC *) calloc(cUnplaced, sizeof(LPA3DREFLECTIONREC));

	if (apMatched && apUnplaced)
	{
		nFree    = 0;
		cMatched = 0;

		for (p = pOther; p && cMatched < cUnplaced; p = A3dChainNext(pSource, p))
		{
			if (!(dwUsed & (1u << p->nSlot)))
				apMatched[cMatched++] = p;
		}

		if (cMatched)
			qsort(apMatched, cMatched, sizeof(void *), A3dReflectionCompareDelay);

		cGathered       = 0;
		p               = pLink;

		while (cGathered < cUnplaced)
		{
			if (p->nSlot == -1)
				apUnplaced[cGathered++] = p;

			p = A3dChainNext(pSource, p);
		}

		qsort(apUnplaced, cGathered, sizeof(void *), A3dReflectionCompareDelay);

		for (i = 0; i < cGathered; i++)
		{
			if (i >= cMatched)
			{
				while ((dwUsed & (1u << nFree)) && nFree < A3D_MAX_SOURCE_REFLECTIONS)
					nFree++;

				apUnplaced[i]->nSlot = nFree;
				apUnplaced[i]->bNew  = TRUE;

				dwUsed |= 1u << nFree;
			}
			else
			{
				A3DVAL fDelta;

				apUnplaced[i]->nSlot = apMatched[i]->nSlot;

				dwUsed |= 1u << apUnplaced[i]->nSlot;

				fDelta = apUnplaced[i]->fDelayLeft - apMatched[i]->fDelayLeft;

				if (fDelta > A3D_REFLECTION_NEW_DELAY_THRESHOLD_S ||
				    fDelta < -A3D_REFLECTION_NEW_DELAY_THRESHOLD_S)
					apUnplaced[i]->bNew = TRUE;
			}
		}

		free(apMatched);
		free(apUnplaced);
	}
}

/* =============================================================
// A3dReflectionCompare()
// (RE) dbg:0x1002CCB0
//
// Compare reflection audibility in descending order.
// The original comparator silences the quieter duplicate as a side effect.
//
// Returns: -1 when the first is louder; 1 when quieter; 0 for equal audibility.
// =============================================================*/

int
A3dReflectionCompare(const void *pv1, const void *pv2)
{
LPA3DREFLECTIONREC p1;
LPA3DREFLECTIONREC p2;

	p1 = *(LPA3DREFLECTIONREC *) pv1;
	p2 = *(LPA3DREFLECTIONREC *) pv2;

	if (p1->dwSurface == p2->dwSurface &&
	    p1->dwKind == p2->dwKind &&
	    p1->vHit[0] == p2->vHit[0] &&
	    p1->vHit[1] == p2->vHit[1] &&
	    p1->vHit[2] == p2->vHit[2] &&
	    p1->dwSurface2 == p2->dwSurface2)
	{

		if (p1->fAudibility <= p2->fAudibility)
		{
			p1->fAudibility = 0.0f;
			p1->fGainLeft   = 0.0f;
			p1->fGainRight  = 0.0f;
		}
		else
		{
			p2->fAudibility = 0.0f;
			p2->fGainLeft   = 0.0f;
			p2->fGainRight  = 0.0f;
		}
	}

	if (p1->fAudibility <= p2->fAudibility)
		return (p1->fAudibility < p2->fAudibility);

	return (-1);
}

/* =============================================================
// A3dReflectionCompareDelay()
// (RE) dbg:0x1002CE10
//
// Compare reflections by ascending sum of ear delays.
//
// Returns: 1 for a greater first sum; 0 for equal sums; -1 otherwise.
// =============================================================*/

int
A3dReflectionCompareDelay(const void *pv1, const void *pv2)
{
LPA3DREFLECTIONREC p1;
LPA3DREFLECTIONREC p2;
A3DVAL             fSum1;
A3DVAL             fSum2;

	p1 = *(LPA3DREFLECTIONREC *) pv1;
	p2 = *(LPA3DREFLECTIONREC *) pv2;

	fSum1 = p1->fDelayLeft + p1->fDelayRight;
	fSum2 = p2->fDelayLeft + p2->fDelayRight;

	if (fSum1 > fSum2)
		return (1);

	if (fSum1 >= fSum2)
		return (0);

	return (-1);
}

/* =============================================================
// RenumberReflections()
// (RE) rtl:0x10013CA0; dbg:0x1002CEC0; thunk dbg:0x100036B1
//
// Clear surface tags and assign array indices to 16 reflection records.
//
// Returns:
//   S_OK
//   E_POINTER  a null array
// =============================================================*/

STDMETHODIMP
CA3dSource::RenumberReflections(LPVOID *paReflections)
{
LPA3DREFLECTIONREC pRec;
int                i;

	if (!paReflections)
		return (E_POINTER);

	for (i = 0; i < A3D_MAX_SOURCE_REFLECTIONS; i++)
	{
		pRec = (LPA3DREFLECTIONREC) paReflections[i];

		pRec->dwSurface2 = i;
		pRec->dwSurface  = 0;
	}

	return (S_OK);
}

typedef int A3dSourceSizeCheck[(sizeof(CA3dSource) == A3D_SOURCE_SIZE) ? 1 : -1];

/* =============================================================
// LoadFile()
// (RE) rtl:0x10014A80; dbg:0x1002E610
// Catch funclet dbg:0x1002E7C6
//
// Dispatch to the requested audio loader and retain the filename on success.
//
// Returns:
//   Loader result
//   A3DERROR_UNRECOGNIZED_FORMAT  an unknown format
//   A3DERROR_MEMORY_ALLOCATION    if the filename copy fails
// =============================================================*/

STDMETHODIMP
CA3dSource::LoadFile(char *szFile, DWORD dwFormat)
{
HRESULT hr;

	try
	{
		m_dwFormat = dwFormat;

		switch (dwFormat)
		{
		case A3DSOURCE_FORMAT_WAVE:
			hr = LoadWaveFile(szFile);
			break;

		case A3DSOURCE_FORMAT_WAVE | A3DSOURCE_FORMAT_STREAMING:
			hr = LoadStreamingWaveFile(szFile);
			break;

		case A3DSOURCE_FORMAT_MP3:
			hr = LoadMp3File(szFile);
			break;

		case A3DSOURCE_FORMAT_MP3 | A3DSOURCE_FORMAT_STREAMING:
			hr = LoadStreamingMp3File(szFile);
			break;

		case A3DSOURCE_FORMAT_AC3:
		case A3DSOURCE_FORMAT_AC3 | A3DSOURCE_FORMAT_STREAMING:
			hr = LoadAc3File(szFile);
			break;

		default:
			hr = A3DERROR_UNRECOGNIZED_FORMAT;
			throw "Unknown file format";
		}

		if (FAILED(hr))
			throw "Loading file failed. Check error code.\n";

		if (!m_SourceCaps.szFilename)
		{
			m_SourceCaps.szFilename = new char[strlen(szFile) + 1];
			if (!m_SourceCaps.szFilename)
			{
				DBGSTR(
					"CA3dSource::LoadFile - Unable to allocate memory for filename.\n");

				return (A3DERROR_MEMORY_ALLOCATION);
			}

			strcpy(m_SourceCaps.szFilename, szFile);
		}
	}
	catch (const char *pszWhy)
	{
		TRACE("CA3dSource::LoadFile - %s\n", pszWhy);

		m_dwFormat = 0;
	}

	return (hr);
}

/* =============================================================
// LoadStreamingWaveFile()
// (RE) dbg:0x10026CD0; thunk dbg:0x100015B9
//
// Open a WAVE stream, start its worker and prime the segmented buffer.
//
// Returns: OpenMmioStream/SizeStreamBuffer failure or StartStreaming result;
//          E_POINTER for a null filename.
// =============================================================*/

HRESULT
CA3dSource::LoadStreamingWaveFile(LPSTR pszFileName)
{
	MMIOINFO mmioinfo;
	MMCKINFO mmckiParent;
	HRESULT  hr;

	if (!pszFileName)
		return (E_POINTER);

	m_fStreaming = 1;

	ZeroMemory(&mmioinfo, sizeof(mmioinfo));

	hr = OpenMmioStream(pszFileName, &m_hmmio, &mmioinfo, 0, &mmckiParent);

	if (FAILED(hr))
	{
		m_fStreaming = 0;

		return (hr);
	}

	hr = SizeStreamBuffer(&mmckiParent);

	if (FAILED(hr))
	{
		m_fStreaming = 0;

		return (hr);
	}

	hr = StartStreaming();

	UpdateStreamBuffer(0);
	UpdateStreamBuffer(1);

	FillStreamTail(2 * m_dwStreamSegmentSize, m_dwWaveSize);

	return (hr);
}

#ifndef A3D_FIXES

/* =============================================================
// Mp3SscCreateDecoder()
// (RE) dbg:0x100A51B0 (reference wrapper for dbg:0x100A51D0)
//
// Stub used without A3D_FIXES; clear the decoder output when supplied.
//
// Returns: 0xC0000000, a decoder failure status.
// =============================================================*/

int
Mp3SscCreateDecoder(LPA3DMP3DECODER *ppDecoder)
{
	if (ppDecoder)
		*ppDecoder = NULL;

	return ((int) 0xC0000000);
}

/* =============================================================
// Mp3SscErrorString()
// (RE) dbg:0x100A5180 (reference entry)
//
// Describe the unavailable MP3 decoder in builds without A3D_FIXES.
//
// Returns: A static diagnostic string.
// =============================================================*/

const char *
Mp3SscErrorString(int nStatus)
{
	return ("(Mp3Ssc) decoder not present in this build");
}

#endif

#ifndef A3D_FIXES

/* =============================================================
// Ac3OpenAudio()
// (RE) dbg:0x100ACE90 (reference entry)
//
// Stub used without A3D_FIXES; create no decoder.
//
// Returns: 0, indicating no decoder handle.
// =============================================================*/

DWORD
Ac3OpenAudio(DWORD dwMode)
{
	return (0);
}

/* =============================================================
// Ac3CloseAudio()
// (RE) dbg:0x100ACF60 (reference entry)
//
// Do nothing in builds without A3D_FIXES.
// =============================================================*/

void
Ac3CloseAudio(DWORD hDecoder)
{
}

/* =============================================================
// Ac3DecodeAudio()
// (RE) rtl:0x10048130; dbg:0x100ACF90 (reference entries)
//
// Stub used without A3D_FIXES; decode no audio.
//
// Returns: 0.
// =============================================================*/

int
Ac3DecodeAudio(DWORD hDecoder, LPA3DAC3CONFIG pConfig)
{
	return (0);
}

/* =============================================================
// Ac3ResetAudio()
// (RE) rtl:0x10048110; dbg:0x100ACF70 (reference entries)
//
// Stub used without A3D_FIXES; change no decoder state.
//
// Returns: 0.
// =============================================================*/

int
Ac3ResetAudio(DWORD hDecoder, DWORD dwMode)
{
	return (0);
}

#endif

/* =============================================================
// LoadMp3File()
// (RE) rtl:0x100111A0; dbg:0x10024600; thunk dbg:0x10003E68
//
// Decode an entire MP3 file into a resident buffer and close the decoder.
//
// Returns: OpenMp3Stream, Lock or FillMp3StreamBuffer result.
// =============================================================*/

HRESULT
CA3dSource::LoadMp3File(LPSTR pszFileName)
{
	void   *pvLock;
	DWORD   dwLocked;
	HRESULT hr;

	hr = OpenMp3Stream(pszFileName);

	if (SUCCEEDED(hr))
	{
		hr = Lock(0, 0, &pvLock, &dwLocked, NULL, NULL, DSBLOCK_ENTIREBUFFER);

		if (SUCCEEDED(hr))
		{
			hr = FillMp3StreamBuffer(pvLock, 0, dwLocked, -1);

			if (FAILED(hr))
				DBGSTR(
					"CA3dSource::LoadMp3File() - Unable to decode whole mp3 file.\n");
		}

		Unlock(pvLock, dwLocked, NULL, 0);
	}

	CloseMp3Stream();

	return (hr);
}

/* =============================================================
// OpenMp3Stream()
// (RE) rtl:0x100100E0; dbg:0x100247E0; thunk dbg:0x10003053
// Catch funclet dbg:0x10024F17
//
// Open and prime the MP3 decoder, set the format and allocate playback storage.
//
// Returns: S_OK, including the current file-open failure path; E_POINTER for a
//          null filename; file-seek or allocation error; FillMp3Decoder or
//          SetAudioFormat failure.
// =============================================================*/

HRESULT
CA3dSource::OpenMp3Stream(LPSTR pszFileName)
{
	LPA3DMP3FORMAT pFormat;
	WAVEFORMATEX   wfx;
	int            nStatus;
	HRESULT        hr;
	const char    *pszError;

	if (!pszFileName)
		return (E_POINTER);

	hr = S_OK;

	try
	{
		m_hmmio = mmioOpenA(pszFileName, NULL, 0);

		if (!m_hmmio)
			throw "Unable to open mp3 file.";

		m_dwStreamSize = mmioSeek(m_hmmio, 0, SEEK_END);

		if (m_dwStreamSize == (DWORD) -1)
		{
			hr = A3DERROR_FAILED_FILE_OPEN;
			throw "Unable to get mp3 file size.";
		}

		if (mmioSeek(m_hmmio, 0, SEEK_SET) == -1)
		{
			hr = A3DERROR_FAILED_FILE_OPEN;
			throw "Unable to reset mp3 file.";
		}

		m_dwStreamRemaining = m_dwStreamSize;

		if (m_pMp3Decoder)
		{
			m_pMp3Decoder->Reset();
		}
		else
		{
			nStatus = Mp3SscCreateDecoder(&m_pMp3Decoder);

			if ((nStatus & A3D_MP3_STATUS_SEVERITY_MASK) != 0 &&
			    (nStatus & A3D_MP3_STATUS_SEVERITY_MASK) != 0x40000000)
			{
				hr              = A3DERROR_MEMORY_ALLOCATION;
				pszError        = Mp3SscErrorString(nStatus);
				throw pszError;
			}
		}

		m_pMp3Decode = new A3DMP3STATE;

		if (!m_pMp3Decode)
		{
			hr = A3DERROR_MEMORY_ALLOCATION;
			throw "Failed to allocate memory for Mp3 decoding.";
		}

		ZeroMemory(m_pMp3Decode, sizeof(A3DMP3STATE));
		m_pMp3Decode->dwInputSize = A3D_SOURCE_MP3_INPUT_BYTES;
		m_pMp3Decode->pInput      = operator new(m_pMp3Decode->dwInputSize);

		hr = FillMp3Decoder();

		if (FAILED(hr))
			throw "Failed on call to FillMp3Decoder()";

		nStatus = m_pMp3Decoder->DecodeBlock(NULL, 0, &m_pMp3Decode->dwHeaderBytes);

		if ((nStatus & A3D_MP3_STATUS_SEVERITY_MASK) != 0 &&
		    (nStatus & A3D_MP3_STATUS_SEVERITY_MASK) != 0x40000000)
		{
			hr = A3DERROR_MEMORY_ALLOCATION;
			throw "Unable to decode mp3 header.";
		}

		pFormat = m_pMp3Decoder->GetFormat();

		m_SourceCaps.data.mp3Info.dwSize         = 32;
		m_SourceCaps.data.mp3Info.nMpegLayer     = pFormat->nMpegLayer;
		m_SourceCaps.data.mp3Info.nMpegVersion   = pFormat->nMpegVersion;
		m_SourceCaps.data.mp3Info.nBitsPerSample = pFormat->nBitsPerSample;
		m_SourceCaps.data.mp3Info.nBitrate       = pFormat->nBitrate;
		m_SourceCaps.data.mp3Info.nChannels      = pFormat->nChannels;
		m_SourceCaps.data.mp3Info.nSamplerate    = pFormat->nSamplerate;

		m_pMp3Decode->dwDecodedSize = (DWORD) (__int64)
			((double) m_SourceCaps.data.mp3Info.nChannels *
			 ((double) m_SourceCaps.data.mp3Info.nSamplerate *
			  ((double) m_dwStreamSize / (double) m_SourceCaps.data.mp3Info.nBitrate) *
			  16.0));

		ZeroMemory(&wfx, sizeof(wfx));
		wfx.wFormatTag      = WAVE_FORMAT_PCM;
		wfx.nChannels       = (WORD) m_SourceCaps.data.mp3Info.nChannels;
		wfx.nSamplesPerSec  = m_SourceCaps.data.mp3Info.nSamplerate;
		wfx.wBitsPerSample  = A3D_SOURCE_MP3_PCM_BITS;
		wfx.nBlockAlign     = A3D_SOURCE_MP3_PCM_BITS *
				     (WORD) m_SourceCaps.data.mp3Info.nChannels / 8;
		wfx.nAvgBytesPerSec = wfx.nBlockAlign *
				     m_SourceCaps.data.mp3Info.nSamplerate;

		m_SourceCaps.data.mp3Info.fTotalPlayLength =
			(A3DVAL) ((double) m_pMp3Decode->dwDecodedSize /
				  (double) wfx.nAvgBytesPerSec);

		hr = SetAudioFormat(&wfx);

		if (FAILED(hr))
			throw "Unable to SetAudioFormat.";

		if (m_fStreaming)
		{
			m_pMp3Decode->dwDecodeSize = A3D_SOURCE_MP3_DECODE_BYTES;

			m_pMp3Decode->pDecode = operator new(m_pMp3Decode->dwDecodeSize);

			if (!m_pMp3Decode->pDecode)
			{
				hr = A3DERROR_MEMORY_ALLOCATION;
				throw "Unable to allocate memory for Mp3 decoding buffer.";
			}

			hr = AllocStreamBuffer(
				(DWORD) ((double) m_pwfxFormat->nAvgBytesPerSec *
					 m_pApi->m_fMaxReflectionDelayTime),
				(DWORD) ceil((double) m_pMp3Decode->dwDecodedSize / 3.0));

			if (FAILED(hr))
			{
				hr = A3DERROR_FAILED_ALLOCATE_WAVEDATA;
				throw "Failed on AllocateAudioData.";
			}
		}
		else
		{
			hr = AllocateAudioData(m_pMp3Decode->dwDecodedSize);

			if (FAILED(hr))
			{
				hr = A3DERROR_FAILED_ALLOCATE_WAVEDATA;
				throw "A3DERROR_FAILED_ALLOCATE_WAVEDATA (probably not enough memory)";
			}
		}
	}
	catch (const char *pszWhy)
	{
		TRACE("CA3dSource::OpenMp3File() - %s\n", pszWhy);

		CloseMp3Stream();
	}

	return (hr);
}

/* =============================================================
// FillMp3Decoder()
// (RE) rtl:0x100105C0; dbg:0x100254B0; thunk dbg:0x10002A5E
//
// Read compressed data as needed and supply it to the MP3 decoder.
//
// Returns: Bytes consumed, returned as an HRESULT; E_FAIL on an mmio read
//          error.
// =============================================================*/

HRESULT
CA3dSource::FillMp3Decoder(void)
{
	if (!m_pMp3Decode->dwInputAvail)
	{
		m_pMp3Decode->dwInputAvail = mmioRead(m_hmmio,
						      (HPSTR) m_pMp3Decode->pInput,
						      m_pMp3Decode->dwInputSize);

		if (m_pMp3Decode->dwInputAvail == (DWORD) -1)
			return (E_FAIL);

		if (m_pMp3Decode->dwInputAvail < m_pMp3Decode->dwInputSize)
			m_pMp3Decoder->EndOfInput();

		m_pMp3Decode->dwInputPos = 0;
	}

	m_pMp3Decode->dwConsumed = m_pMp3Decoder->SupplyInput(
		(BYTE *) m_pMp3Decode->pInput + m_pMp3Decode->dwInputPos,
		m_pMp3Decode->dwInputAvail);

	m_pMp3Decode->dwInputAvail -= m_pMp3Decode->dwConsumed;
	m_pMp3Decode->dwInputPos   += m_pMp3Decode->dwConsumed;

	return (m_pMp3Decode->dwConsumed);
}

/* =============================================================
// CloseMp3Stream()
// (RE) rtl:0x10010530; dbg:0x100252D0; thunk dbg:0x10002392
//
// Close the input handle and release the MP3 decoder, state and buffers.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
CA3dSource::CloseMp3Stream(void)
{
	if (m_hmmio)
		mmioClose(m_hmmio, 0);

	m_dwStreamSize = 0;

	if (m_pMp3Decoder)
	{
		m_pMp3Decoder->Destroy();
		m_pMp3Decoder = NULL;
	}

	if (m_pMp3Decode)
	{
		if (m_pMp3Decode->pDecode)
		{
			operator delete(m_pMp3Decode->pDecode);
			m_pMp3Decode->pDecode = NULL;
		}

		if (m_pMp3Decode->pInput)
			operator delete(m_pMp3Decode->pInput);

		delete m_pMp3Decode;
		m_pMp3Decode = NULL;
	}

	return (S_OK);
}

/* =============================================================
// LoadStreamingMp3File()
// (RE) rtl:0x10011290; dbg:0x10024710; thunk dbg:0x100018F7
//
// Open an MP3 stream, start its worker and prime the segmented buffer.
//
// Returns: OpenMp3Stream failure or StartStreaming result.
// =============================================================*/

HRESULT
CA3dSource::LoadStreamingMp3File(LPSTR pszFileName)
{
	HRESULT	hr;

	m_fStreaming = 1;

	hr = OpenMp3Stream(pszFileName);

	if (SUCCEEDED(hr))
	{
		hr = StartStreaming();

		UpdateStreamBuffer(0);
		UpdateStreamBuffer(1);

		FillStreamTail(2 * m_dwStreamSegmentSize, m_dwWaveSize);
	}

	if (FAILED(hr))
		m_fStreaming = 0;

	return (hr);
}

/* =============================================================
// LoadAc3File()
// (RE) rtl:0x10014570; dbg:0x1002DFF0; thunk dbg:0x100015E6
//
// Load an AC-3 stream through the bundled-decoder interface in native mode.
//
// Returns: OpenAc3File or segment-update result; A3DERROR_SOURCE_IN_USE if a
//          buffer already exists.
// =============================================================*/

HRESULT
CA3dSource::LoadAc3File(LPSTR pszFileName)
{
	HRESULT	hr;

	if (m_pBuffer)
		return (A3DERROR_SOURCE_IN_USE);

	m_fStreaming = 1;

	hr = OpenAc3File(pszFileName);

	if (SUCCEEDED(hr))
	{
		StartStreaming();

		hr = UpdateStreamBuffer(0);

		if (SUCCEEDED(hr))
		{
			hr = UpdateStreamBuffer(1);

			if (SUCCEEDED(hr))
				FillStreamTail(2 * m_dwStreamSegmentSize, m_dwWaveSize);
		}
	}

	if (FAILED(hr))
	{
		CloseAc3File();
		m_fStreaming = 0;
	}
	else
	{
		m_dwRenderMode = A3DSOURCE_RENDERMODE_NATIVE;
	}

	return (hr);
}

/* =============================================================
// OpenAc3File()
// (RE) rtl:0x10010B50; dbg:0x10025F00; thunk dbg:0x1000235B
// Catch funclet dbg:0x10026360
//
// Open the AC-3 decoder and allocate a stream using 48 kHz stereo 16-bit PCM.
//
// Returns: S_OK; file-open/seek or allocation error; E_FAIL if decoder creation
//          fails; SetAudioFormat failure.
// =============================================================*/

HRESULT
CA3dSource::OpenAc3File(LPSTR pszFileName)
{
	LPA3DAC3CONFIG pConfig;
	WAVEFORMATEX   wfx;
	HRESULT        hr;

	try
	{
		m_hmmio = mmioOpenA(pszFileName, NULL, MMIO_ALLOCBUF);

		if (!m_hmmio)
			return (A3DERROR_FAILED_FILE_OPEN);

		hr = S_OK;

		m_dwStreamSize = mmioSeek(m_hmmio, 0, SEEK_END);

		if (m_dwStreamSize == (DWORD) -1)
		{
			hr = A3DERROR_FAILED_FILE_OPEN;
			throw "Unable to get mp3 file size.";
		}

		if (mmioSeek(m_hmmio, 0, SEEK_SET) == -1)
		{
			hr = A3DERROR_FAILED_FILE_OPEN;
			throw "Unable to reset mp3 file.";
		}

		m_dwStreamRemaining = m_dwStreamSize;

		m_pAc3Format = (LPA3DAC3CONFIG) operator new(sizeof(A3DAC3CONFIG));

		if (!m_pAc3Format)
		{
			hr = A3DERROR_MEMORY_ALLOCATION;
			throw "Unable to allocate memory for Ac3 decoder.";
		}

		m_pAc3Decode = new A3DAC3STATE;

		if (!m_pAc3Decode)
		{
			hr = A3DERROR_MEMORY_ALLOCATION;
			throw "Unable to allocate memory for Ac3 decoding.";
		}

		ZeroMemory(m_pAc3Decode, sizeof(A3DAC3STATE));
		m_pAc3Decode->dwInputSize  = A3D_SOURCE_AC3_INPUT_BYTES;
		m_pAc3Decode->dwDecodeSize = A3D_SOURCE_AC3_DECODE_BYTES;
		m_pAc3Decode->dwCrcPending = 1;
		m_pAc3Decode->pInput       = operator new(m_pAc3Decode->dwInputSize);
		m_pAc3Decode->pDecode      = operator new(m_pAc3Decode->dwDecodeSize);

		pConfig = m_pAc3Format;

		ZeroMemory(pConfig, sizeof(A3DAC3CONFIG));
		pConfig->dwMode       = 2;
		pConfig->dwFrameCount = 1;
		pConfig->dwProduced   = 0;
		pConfig->dwDecodeSize = m_pAc3Decode->dwDecodeSize;
		pConfig->pDecode      = m_pAc3Decode->pDecode;
		pConfig->pInput0      = m_pAc3Decode->pInput;
		pConfig->pFrameStart  = m_pAc3Decode->pInput;
		pConfig->pFrameEnd    = m_pAc3Decode->pInput;
		pConfig->dwField20    = 0;
		pConfig->dwField24    = 2;

		m_pAc3Decode->hDecoder = Ac3OpenAudio(pConfig->dwMode);

		if (!m_pAc3Decode->hDecoder)
		{
			hr = E_FAIL;
			throw "OpenAudio Failed.";
		}

		ZeroMemory(&wfx, sizeof(wfx));
		wfx.wFormatTag      = WAVE_FORMAT_PCM;
		wfx.nChannels       = 2;
		wfx.nSamplesPerSec  = 48000;
		wfx.nAvgBytesPerSec = 192000;
		wfx.nBlockAlign     = 4;
		wfx.wBitsPerSample  = 16;

		m_pAc3Decode->fDefaultPositionScale = (A3DVAL) (5232310.0 /
					((double) 2 * ((double) 16 / 8.0 * 48000.0)));

		hr = SetAudioFormat(&wfx);

		if (FAILED(hr))
			throw "Unable to set wave format.";

		m_SourceCaps.data.ac3Info.dwSize             = 8;
		m_SourceCaps.data.ac3Info.bPlayingInHardware = 0;

		hr = AllocStreamBuffer(
			(DWORD) ((double) m_pwfxFormat->nAvgBytesPerSec *
				 m_pApi->m_fMaxReflectionDelayTime),
			0);

		if (FAILED(hr))
		{
			hr = A3DERROR_FAILED_ALLOCATE_WAVEDATA;
			throw "Failed on AllocateAudioData.";
		}
	}
	catch (const char *pszWhy)
	{
		TRACE("CA3dSource::OpenAc3File - %s\n", pszWhy);

		CloseAc3File();
	}

	return (hr);
}

/* =============================================================
// CloseAc3File()
// (RE) rtl:0x100143F0; dbg:0x1002DBC0; thunk dbg:0x10002F36
//
// Close the input handle and release the AC-3 decoder, state and configuration.
// =============================================================*/

void
CA3dSource::CloseAc3File(void)
{
	if (m_hmmio)
		mmioClose(m_hmmio, 0);

	m_dwStreamSize = 0;

	if (m_pAc3Decode)
	{
		Ac3CloseAudio(m_pAc3Decode->hDecoder);

		delete m_pAc3Decode;
		m_pAc3Decode = NULL;
	}

	if (m_pAc3Format)
	{
		operator delete(m_pAc3Format);
		m_pAc3Format = NULL;
	}
}

/* (RE) g_StreamSources compiler helpers:
 * initializer dbg:0x1001E260, CRT entry dbg:0x10146104;
 * construction wrapper dbg:0x1001E280;
 * atexit registration rtl:0x1000C2B0, dbg:0x1001E2B0;
 * destruction rtl:0x1000C2F0, dbg:0x1001E2E0, guard dbg:0x10152A2C. */
static CA3dStdList<CA3dSource *>	g_StreamSources; /* Shared streaming sources. (RE) dbg:0x10152A30. */
static HANDLE		g_hStreamThread = INVALID_HANDLE_VALUE; /* INVALID_HANDLE_VALUE until created. (RE) dbg:0x1014782C. */
static DWORD		g_dwStreamThreadId; /* (RE) dbg:0x10147830. */
static HANDLE		g_hStreamNewSource; /* Add request. (RE) dbg:0x10147834. */
static HANDLE		g_hStreamDelSource; /* Remove request. (RE) dbg:0x10147838. */
static HANDLE		g_hStreamKill; /* Shutdown request. (RE) dbg:0x1014783C. */
static HANDLE		g_hStreamAck; /* Add/remove acknowledgement. (RE) dbg:0x10147840. */
static HANDLE		g_hStreamThreadDead; /* Shutdown completion. (RE) dbg:0x10147844. */
static DWORD		g_bStreamThreadRun; /* Worker loop enabled. (RE) dbg:0x10152A40. */
static HANDLE		*g_pStreamHandles; /* Worker wait array. (RE) dbg:0x10152A44. */
static CA3dSource	*g_pStreamNewSource; /* Published add/remove request source. (RE) dbg:0x10152A48. */
static DWORD		g_cStreamFillActive; /* Active WAVE fills. (RE) dbg:0x10152A58. */

static DWORD		g_cMp3FillActive; /* Active MP3 fills. (RE) dbg:0x10152A50. */
static DWORD		g_cAc3FillActive; /* Active AC-3 fills. (RE) dbg:0x10152A54. */

/* =============================================================
// BuildStreamHandleArray()
// (RE) dbg:0x100283B0; thunk dbg:0x10002626
//
// Rebuild the wait array with four events per source and three service events.
//
// Returns:
//   S_OK
//   E_OUTOFMEMORY  if the new array cannot be allocated
// =============================================================*/

HRESULT
CA3dSource::BuildStreamHandleArray(void)
{
	CA3dStdList<CA3dSource *>::iterator it;

	CA3dSource *pSource;
	DWORD       cSources;
	DWORD       i;

	operator delete(g_pStreamHandles);

	cSources = g_StreamSources.size();

	g_pStreamHandles = (HANDLE *) operator new(
		(A3D_STREAM_EVENTS_PER_SOURCE * cSources +
		 A3D_STREAM_SERVICE_EVENTS) * sizeof(HANDLE));

	if (!g_pStreamHandles)
		return (E_OUTOFMEMORY);

	i = 0;

	for (it = g_StreamSources.begin(); it != g_StreamSources.end(); ++it)
	{
		pSource = *it;

		g_pStreamHandles[A3D_STREAM_EVENTS_PER_SOURCE * i + 0] =
			pSource->m_ahStreamEvents[0];
		g_pStreamHandles[A3D_STREAM_EVENTS_PER_SOURCE * i + 1] =
			pSource->m_ahStreamEvents[1];
		g_pStreamHandles[A3D_STREAM_EVENTS_PER_SOURCE * i + 2] =
			pSource->m_ahStreamEvents[2];
		g_pStreamHandles[A3D_STREAM_EVENTS_PER_SOURCE * i + 3] =
			pSource->m_ahStreamEvents[3];

		i++;
	}

	g_pStreamHandles[A3D_STREAM_EVENTS_PER_SOURCE * cSources + 0] = g_hStreamKill;
	g_pStreamHandles[A3D_STREAM_EVENTS_PER_SOURCE * cSources + 1] = g_hStreamNewSource;
	g_pStreamHandles[A3D_STREAM_EVENTS_PER_SOURCE * cSources + 2] = g_hStreamDelSource;

	return (S_OK);
}

/* =============================================================
// AddStreamSource()
// (RE) dbg:0x100282D0; thunk dbg:0x1000433B
//
// Append the published source to the shared stream list and rebuild its wait
// array.
//
// Returns: BuildStreamHandleArray result.
// =============================================================*/

HRESULT
CA3dSource::AddStreamSource(void)
{
	g_StreamSources.push_back(g_pStreamNewSource);
	g_pStreamNewSource = NULL;

	return (BuildStreamHandleArray());
}

/* =============================================================
// RemoveStreamSource()
// (RE) dbg:0x10028320; thunk dbg:0x10002DB5
//
// Remove the published source, update the current source and rebuild the wait
// array.
//
// Returns: BuildStreamHandleArray result.
// =============================================================*/

HRESULT
CA3dSource::RemoveStreamSource(CA3dSource **ppCurrent)
{
	g_StreamSources.remove(g_pStreamNewSource);

	if (g_pStreamNewSource == *ppCurrent)
	{
		*ppCurrent = NULL;

		if (g_StreamSources.size())
			*ppCurrent = g_StreamSources._Head->_Prev->_Value;
	}

	g_pStreamNewSource = NULL;

	return (BuildStreamHandleArray());
}

/* =============================================================
// SeekStreamBuffer()
// (RE) dbg:0x100285D0; thunk dbg:0x1000331E
//
// Seek the compressed or PCM stream, refill its buffers and restore playback.
//
// Returns: Buffer Play result if previously playing; otherwise the final
//          FillStreamTail result.
// =============================================================*/

HRESULT
CA3dSource::SeekStreamBuffer(DWORD dwPosition)
{
	DWORD   dwStatus;
	HRESULT hr;

	if (m_dwFormat & A3DSOURCE_FORMAT_WAVE)
		dwPosition += dwPosition % m_pwfxFormat->nBlockAlign;

	mmioSeek(m_hmmio, dwPosition + m_dwStreamDataOffset, SEEK_SET);

	if (m_dwFormat & A3DSOURCE_FORMAT_MP3)
		m_pMp3Decoder->Reset();

	m_dwStreamRemaining = m_dwStreamSize - dwPosition;

	RebuildNotifyPositions(TRUE);

	m_pBuffer->GetStatus(&dwStatus);
	m_pBuffer->Stop();
	m_pBuffer->SetCurrentPosition(0);

	FillStreamTail(0, m_dwStreamSegmentSize);

	RebuildNotifyPositions(FALSE);

	if (m_dwFormat & A3DSOURCE_FORMAT_AC3)
		Ac3ResetAudio(m_pAc3Decode->hDecoder, m_pAc3Format->dwMode);
	else if (m_dwFormat & A3DSOURCE_FORMAT_MP3)
		m_pMp3Decoder->Reset();

	UpdateStreamBuffer(0);
	UpdateStreamBuffer(1);

	hr = FillStreamTail(2 * m_dwStreamSegmentSize, m_dwWaveSize);

	if (dwStatus & DSBSTATUS_PLAYING)
		return (m_pBuffer->Play(0, 0, m_fLooping));

	return (hr);
}

/* =============================================================
// ClearStreamPlayEvents()
// (RE) dbg:0x100288A0; thunk dbg:0x100012C6; inlined at rtl:0x100133A7
//
// Delete streaming play events under the event mutex. Original defect:
// release the mutex even when the wait fails.
// =============================================================*/

void
CA3dSource::ClearStreamPlayEvents(void)
{
A3DPLAYEVENT       *pEvent;
POSITION            pos;

	if (!m_fStreaming)
		return;

	if (WaitForSingleObject(m_hEventMutex, INFINITE) == WAIT_OBJECT_0)
	{
		pos = m_pStreamWaveEvents->GetHeadPosition();

		while (!m_pStreamWaveEvents->IsEnd(pos))
		{
			pEvent = m_pStreamWaveEvents->GetNext(pos);
			operator delete(pEvent);
		}

		m_pStreamWaveEvents->RemoveAll();
	}

	ReleaseMutex(m_hEventMutex);
}

/* =============================================================
// StreamingCallback()
// (RE) dbg:0x100294E0; thunk dbg:0x1000295F; list-index helper dbg:0x10030130
//
// Service shared source events for seeks, segment refills, registration and
// shutdown.
//
// Returns: 0 when the service stops.
// =============================================================*/

DWORD WINAPI
CA3dSource::StreamingCallback(LPVOID pParam)
{
	CA3dStdList<CA3dSource *>::iterator it;

	CA3dSource *pCurrentSource;
	CA3dSource *pSource;
	HRESULT     hr;
	DWORD       nCount;
	DWORD       dwWait;
	DWORD       nSource;
	DWORD       n;
	DWORD       i;
	DWORD       j;
	char        szMessage[128];

	pCurrentSource = (CA3dSource *) pParam;

	pCurrentSource->BuildStreamHandleArray();

	nCount = A3D_STREAM_EVENTS_PER_SOURCE * g_StreamSources.size() +
		 A3D_STREAM_SERVICE_EVENTS;

	while (g_bStreamThreadRun)
	{
		if (pCurrentSource)
			pCurrentSource->SetStreamingPriority();

		dwWait = WaitForMultipleObjects(nCount, g_pStreamHandles, FALSE, INFINITE);

		if (dwWait == WAIT_FAILED)
		{
			for (i = 0; i < nCount; i++)
			{
#ifdef _DEBUG
				sprintf(szMessage,
					"StreamingCallback() - Handle: 0x%x\n",
					g_pStreamHandles[i]);
				OutputDebugStringA(szMessage);
#endif
			}

			g_bStreamThreadRun = 0;

			break;
		}

		if (WaitForSingleObject(g_hStreamKill, 0))
		{
			if (WaitForSingleObject(g_hStreamDelSource, 0))
			{
				if (WaitForSingleObject(g_hStreamNewSource, 0))
				{

					for (j = dwWait; j < nCount - A3D_STREAM_SERVICE_EVENTS; j++)
					{
						if (WaitForSingleObject(
							g_pStreamHandles[j], 0) ==
						    WAIT_OBJECT_0)
						{
							nSource = j / A3D_STREAM_EVENTS_PER_SOURCE;

							pSource = NULL;
							n       = 0;

							for (it = g_StreamSources.begin();
							     it != g_StreamSources.end();
							     ++it)
							{
								if (n == nSource)
								{
									pSource = *it;

									break;
								}

								n++;
							}

							if (pSource->m_ahStreamEvents[0] ==
							    g_pStreamHandles[j])
								pSource->SeekStreamBuffer(
									pSource->m_dwSeekPosition);

							if (!pSource->m_dwStreamEnded)
							{
								if (pSource->m_ahStreamEvents[1] ==
								    g_pStreamHandles[j])
								{
									pSource->UpdateStreamBuffer(2);
									pSource->m_adwStreamSegmentValid[0] = 0;
								}
								else if (pSource->m_ahStreamEvents[2] ==
									 g_pStreamHandles[j])
								{
									pSource->UpdateStreamBuffer(0);
									pSource->m_adwStreamSegmentValid[1] = 0;
								}
								else if (pSource->m_ahStreamEvents[3] ==
									 g_pStreamHandles[j])
								{
									pSource->UpdateStreamBuffer(1);
									pSource->m_adwStreamSegmentValid[2] = 0;
								}
							}

							ResetEvent(g_pStreamHandles[j]);
						}
					}
				}
				else
				{
					hr = pCurrentSource->AddStreamSource();

					if (FAILED(hr))
						DBGSTR("CA3dSource::StreamingCallback() - Unable to generate event list (out of memory).\n");
					else
						nCount += A3D_STREAM_EVENTS_PER_SOURCE;

					ResetEvent(g_hStreamNewSource);
					SetEvent(g_hStreamAck);
				}
			}
			else
			{
				hr = pCurrentSource->RemoveStreamSource(&pCurrentSource);

				if (FAILED(hr))
					DBGSTR("CA3dSource::StreamingCallback() - Unable to generate event list (out of memory).\n");
				else
					nCount -= A3D_STREAM_EVENTS_PER_SOURCE;

				ResetEvent(g_hStreamDelSource);
				SetEvent(g_hStreamAck);
			}
		}
		else
		{
			g_bStreamThreadRun = 0;

			ResetEvent(g_hStreamKill);
		}
	}

	SetEvent(g_hStreamThreadDead);

	return (0);
}

/* =============================================================
// StopStreaming()
// (RE) rtl:0x10012AD0; dbg:0x10029930; thunk dbg:0x10002815
//
// Unregister the source and release its streaming resources; the last source
// also stops the worker. Original defect: release the mutex if its wait fails.
// =============================================================*/

void
CA3dSource::StopStreaming(void)
{
A3DPLAYEVENT       *pEvent;
POSITION            pos;

	g_pStreamNewSource = this;

	SetEvent(g_hStreamDelSource);
	WaitForSingleObject(g_hStreamAck, INFINITE);

	if (!g_StreamSources.size())
	{
		SetEvent(g_hStreamKill);
		WaitForSingleObject(g_hStreamThreadDead, INFINITE);

		if (g_hStreamThread != INVALID_HANDLE_VALUE)
		{
			CloseHandle(g_hStreamThread);
			g_hStreamThread = INVALID_HANDLE_VALUE;
		}

		if (g_hStreamNewSource != INVALID_HANDLE_VALUE)
		{
			CloseHandle(g_hStreamNewSource);
			g_hStreamNewSource = INVALID_HANDLE_VALUE;
		}

		if (g_hStreamDelSource != INVALID_HANDLE_VALUE)
		{
			CloseHandle(g_hStreamDelSource);
			g_hStreamDelSource = INVALID_HANDLE_VALUE;
		}

		if (g_hStreamAck != INVALID_HANDLE_VALUE)
		{
			CloseHandle(g_hStreamAck);
			g_hStreamAck = INVALID_HANDLE_VALUE;
		}

		if (g_hStreamKill != INVALID_HANDLE_VALUE)
		{
			CloseHandle(g_hStreamKill);
			g_hStreamKill = INVALID_HANDLE_VALUE;
		}

		if (g_hStreamThreadDead != INVALID_HANDLE_VALUE)
		{
			CloseHandle(g_hStreamThreadDead);
			g_hStreamThreadDead = INVALID_HANDLE_VALUE;
		}

		operator delete(g_pStreamHandles);
		g_pStreamHandles   = NULL;
		g_pStreamNewSource = NULL;
	}

	mmioClose(m_hmmio, 0);

	if (WaitForSingleObject(m_hEventMutex, INFINITE) == WAIT_OBJECT_0)
	{
		pos = m_pStreamWaveEvents->GetHeadPosition();

		while (!m_pStreamWaveEvents->IsEnd(pos))
		{
			pEvent = m_pStreamWaveEvents->GetNext(pos);
			operator delete(pEvent);
		}

		m_pStreamWaveEvents->RemoveAll();
	}

	ReleaseMutex(m_hEventMutex);

	m_adwStreamSegmentValid[0] = 0;
	m_adwStreamSegmentValid[1] = 0;
	m_adwStreamSegmentValid[2] = 0;

	if (m_ahStreamEvents[1] != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_ahStreamEvents[1]);
		m_ahStreamEvents[1] = INVALID_HANDLE_VALUE;
	}

	if (m_ahStreamEvents[2] != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_ahStreamEvents[2]);
		m_ahStreamEvents[2] = INVALID_HANDLE_VALUE;
	}

	if (m_ahStreamEvents[3] != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_ahStreamEvents[3]);
		m_ahStreamEvents[3] = INVALID_HANDLE_VALUE;
	}

	if (m_ahStreamEvents[0] != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_ahStreamEvents[0]);
		m_ahStreamEvents[0] = INVALID_HANDLE_VALUE;
	}

	if (m_hEventMutex != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_hEventMutex);
		m_hEventMutex = INVALID_HANDLE_VALUE;
	}

	m_fStreaming = 0;
}

/* =============================================================
// OpenMmioStream()
// (RE) dbg:0x10026820; thunk dbg:0x10001B63
//
// Open a RIFF WAVE stream and allocate its format descriptor.
//
// Returns:
//   S_OK
//   A3DERROR_FAILED_FILE_OPEN     if mmioOpenA fails
//   A3DERROR_UNRECOGNIZED_FORMAT  a non-WAVE RIFF
//   E_OUTOFMEMORY                 if format allocation fails
// =============================================================*/

HRESULT
CA3dSource::OpenMmioStream(LPSTR pszFileName, HMMIO *phmmio, LPMMIOINFO pmmioinfo,
			   DWORD fdwOpen, LPMMCKINFO pmmcki)
{
	MMCKINFO     mmckiFormat;
	WAVEFORMATEX wfx;
	WORD         wcbSize;
	char         szMessage[128];

	*phmmio = mmioOpenA(pszFileName, pmmioinfo, fdwOpen);

	if (!*phmmio)
		return (A3DERROR_FAILED_FILE_OPEN);

	mmioDescend(*phmmio, pmmcki, NULL, 0);

	if (pmmcki->ckid != FOURCC_RIFF ||
	    pmmcki->fccType != mmioFOURCC('W', 'A', 'V', 'E'))
	{
		DBGSTR("CA3dSource::LoadWaveFile() - Unrecognized file format.\n");
		mmioClose(*phmmio, 0);

		return (A3DERROR_UNRECOGNIZED_FORMAT);
	}

	mmckiFormat.ckid = mmioFOURCC('f', 'm', 't', ' ');
	mmioDescend(*phmmio, &mmckiFormat, pmmcki, MMIO_FINDCHUNK);

	ZeroMemory(&wfx, sizeof(wfx));
	mmioRead(*phmmio, (HPSTR) &wfx, 18);

	if (wfx.wFormatTag == WAVE_FORMAT_PCM)
		wcbSize = 0;
	else
		mmioRead(*phmmio, (HPSTR) &wcbSize, 2);

	if (m_pwfxFormat)
	{
		operator delete(m_pwfxFormat);
		m_pwfxFormat = NULL;
	}

	m_cbFormat   = wcbSize + 18;
	m_pwfxFormat = (LPWAVEFORMATEX) operator new(m_cbFormat);

	if (!m_pwfxFormat)
	{
		mmioClose(*phmmio, 0);

		return (E_OUTOFMEMORY);
	}

	CopyMemory(m_pwfxFormat, &wfx, 18);
	m_pwfxFormat->cbSize = wcbSize;

	if (wcbSize)
		mmioRead(*phmmio, (HPSTR) ((BYTE *) m_pwfxFormat + 18), wcbSize);

	mmioAscend(*phmmio, &mmckiFormat, 0);

	return (S_OK);
}

/* =============================================================
// SizeStreamBuffer()
// (RE) dbg:0x10026E10; thunk dbg:0x10003445
//
// Locate the WAVE data chunk and allocate a segmented streaming buffer.
//
// Returns:
//   S_OK
//   A3DERROR_UNRECOGNIZED_FORMAT       if the data chunk is missing
//   A3DERROR_FAILED_ALLOCATE_WAVEDATA  if buffer allocation fails
// =============================================================*/

HRESULT
CA3dSource::SizeStreamBuffer(LPMMCKINFO pmmckiParent)
{
	MMCKINFO mmcki;
	DWORD    dwMaxSegment;

	mmioSeek(m_hmmio, pmmckiParent->dwDataOffset + 4, SEEK_SET);

	mmcki.ckid = mmioFOURCC('d', 'a', 't', 'a');

	if (mmioDescend(m_hmmio, &mmcki, pmmckiParent, MMIO_FINDCHUNK))
	{
		mmioClose(m_hmmio, 0);

		return (A3DERROR_UNRECOGNIZED_FORMAT);
	}

	m_dwStreamDataOffset = mmcki.dwDataOffset;
	m_dwStreamSize       = mmcki.cksize;
	m_dwStreamRemaining  = m_dwStreamSize;

	dwMaxSegment = (DWORD) ceil((double) m_dwStreamSize / 3.0);

	if (FAILED(AllocStreamBuffer(
			(DWORD) ((double) m_pwfxFormat->nAvgBytesPerSec *
				 m_pApi->m_fMaxReflectionDelayTime),
			dwMaxSegment)))
	{
		mmioClose(m_hmmio, 0);

		return (A3DERROR_FAILED_ALLOCATE_WAVEDATA);
	}

	return (S_OK);
}

/* =============================================================
// AllocStreamBuffer()
// (RE) dbg:0x1002E8A0; thunk dbg:0x100012B2
//
// Choose the segment size from latency and duration bounds, then initialize its
// marks.
//
// Returns: AllocateWaveData result.
// =============================================================*/

HRESULT
CA3dSource::AllocStreamBuffer(DWORD dwLatencyBytes, DWORD dwMaxSegment)
{
	DWORD dwSegment;
	int   i;

	dwSegment = m_pApi->m_dwStreamBufferLength *
		    m_pwfxFormat->nAvgBytesPerSec / 3000;

	if (dwSegment < dwLatencyBytes)
		dwSegment = dwLatencyBytes;

	if (dwMaxSegment && dwSegment > dwMaxSegment)
		dwSegment = dwMaxSegment;

	m_dwStreamSegmentSize = dwSegment + dwSegment % m_pwfxFormat->nBlockAlign;
	m_dwWaveSize          = 3 * m_dwStreamSegmentSize;

	for (i = 0; i < 3; i++)
	{
		m_aPlayEventMark[i][0] = m_dwStreamSegmentSize * i;
		m_aPlayEventMark[i][1] = (DWORD) A3DSOURCE_WAVEEVENT_NULL;
	}

	m_aPlayEventMark[i][0] = (DWORD) A3DSOURCE_WAVEEVENT_NULL;

	return (AllocateWaveData(m_dwWaveSize));
}

/* =============================================================
// UpdateStreamBuffer()
// (RE) dbg:0x10026F90; thunk dbg:0x10001BD1
//
// Fill a locked segment with its format decoder and record its validity.
//
// Returns: Lock failure or decoder result; successful Lock result for an
//          unsupported format.
// =============================================================*/

HRESULT
CA3dSource::UpdateStreamBuffer(int nSegment)
{
	void   *pvLock1;
	void   *pvLock2;
	DWORD   dwLocked1;
	DWORD   dwLocked2;
	DWORD   dwOffset;
	HRESULT hr;
	char    szMessage[64];

	dwOffset = m_dwStreamSegmentSize * nSegment;

	hr = m_pBuffer->Lock(dwOffset, m_dwStreamSegmentSize,
			     &pvLock1, &dwLocked1, &pvLock2, &dwLocked2, 0);

	if (FAILED(hr))
	{
		DBGSTR("CA3dSource::UpdateStreamBuffer() - Unable to lock buffer.\n");

		return (hr);
	}

	if (nSegment == 0)
		memset(pvLock1, 10, dwLocked1);
	else if (nSegment == 1)
		memset(pvLock1, 11, dwLocked1);
	else if (nSegment == 2)
		memset(pvLock1, 12, dwLocked1);

	switch (m_dwFormat & ~A3DSOURCE_FORMAT_STREAMING)
	{
	case A3DSOURCE_FORMAT_WAVE:
		hr = FillWaveStreamBuffer(pvLock1, 0, dwLocked1, nSegment);
		break;

	case A3DSOURCE_FORMAT_MP3:
		hr = FillMp3StreamBuffer(pvLock1, 0, dwLocked1, nSegment);
		break;

	case A3DSOURCE_FORMAT_AC3:
		hr = FillAc3StreamBuffer(pvLock1, 0, dwLocked1, nSegment);
		break;

	default:
		DBGSTR("Unsupported streaming formats.\n");
		break;
	}

	m_adwStreamSegmentValid[nSegment] = SUCCEEDED(hr);

	m_pBuffer->Unlock(pvLock1, dwLocked1, pvLock2, dwLocked2);

	return (hr);
}

/* =============================================================
// FillWaveStreamBuffer()
// (RE) dbg:0x10027780; thunk dbg:0x10001FFF
//
// Read a WAVE segment, update event positions and handle looping or trailing
// silence.
//
// Returns: S_OK; A3DERROR_UNRECOGNIZED_FORMAT on read failure; E_FAIL on loop
//          seek failure; recursive fill or buffer Play result when used.
// =============================================================*/

HRESULT
CA3dSource::FillWaveStreamBuffer(void *pvBuffer, DWORD dwOffset, DWORD dwLength,
				 int nSegment)
{
	LONG    cRead;
	DWORD   cch;
	int     nMark;
	int     nNext;
	HRESULT hr;
	char    szMessage[96];

	hr = S_OK;

	g_cStreamFillActive++;

	cch = dwLength;

	if (cch > m_dwStreamRemaining)
		cch = m_dwStreamRemaining;

	cRead = mmioRead(m_hmmio, (HPSTR) ((BYTE *) pvBuffer + dwOffset), cch);

	if (cRead == -1)
	{
		mmioClose(m_hmmio, 0);
		m_hmmio = NULL;

		return (A3DERROR_UNRECOGNIZED_FORMAT);
	}

	m_dwStreamRemaining -= cRead;

	if (dwOffset)
	{
		nMark = 3;

		m_aPlayEventMark[3][0] = dwOffset + m_dwStreamSegmentSize * nSegment;
		m_aPlayEventMark[3][1] = m_dwStreamSize - m_dwStreamRemaining - cRead;
	}
	else
	{
		nMark = nSegment;

		m_aPlayEventMark[nSegment][1] = m_dwStreamSize - m_dwStreamRemaining - cRead;

		nNext = nSegment + 1;

		if (nNext > 2)
			nNext = 0;

		m_aPlayEventMark[nNext][1] = (DWORD) A3DSOURCE_WAVEEVENT_NULL;

		if (m_aPlayEventMark[3][0] != (DWORD) A3DSOURCE_WAVEEVENT_NULL &&
		    m_aPlayEventMark[3][0] >= (DWORD) (nSegment * m_dwStreamSegmentSize) &&
		    m_aPlayEventMark[3][0] < m_dwStreamSegmentSize + nSegment * m_dwStreamSegmentSize)
		{
			m_aPlayEventMark[3][0] = (DWORD) A3DSOURCE_WAVEEVENT_NULL;
			m_aPlayEventMark[3][1] = (DWORD) A3DSOURCE_WAVEEVENT_NULL;
		}
	}

	MapWaveEvents(nMark, cRead, TRUE);

	if ((DWORD) cRead < dwLength)
	{
		if (m_dwPlayFlags & DSBPLAY_LOOPING)
		{
			if (mmioSeek(m_hmmio, m_dwStreamDataOffset, SEEK_SET) ==
			    (LONG) m_dwStreamDataOffset)
			{
				m_dwStreamRemaining = m_dwStreamSize;

				if (nSegment == 2 &&
				    (double) m_pwfxFormat->nAvgBytesPerSec *
				    A3D_WAVE_LOOP_SILENCE_THRESHOLD_S >=
				    (double) (dwLength - cRead))
				{
					FillStreamSilence((BYTE *) pvBuffer + cRead,
							  dwLength - cRead);
				}
				else
				{
					hr = FillWaveStreamBuffer(pvBuffer, cRead + dwOffset,
								  dwLength - cRead, nSegment);
				}
			}
			else
			{
				DBGSTR("CA3dSource::FillWaveStreamBuffer() - Failed to seek to beginning of data section.\n");
				hr = E_FAIL;
			}
		}
		else
		{
			FillStreamSilence((BYTE *) pvBuffer + cRead, dwLength - cRead);

			if (nSegment == 2)
			{
				m_dwStreamEnded = 1;
				m_fLooping      = 0;

				hr = m_pBuffer->Play(0, 0, m_fLooping);

				if (FAILED(hr))
				{
					DBGSTR("CA3dSource::FillWaveStreamBuffer() - Unable to change play mode.\n");
				}
			}
		}
	}

	g_cStreamFillActive--;

	return (hr);
}

/* =============================================================
// FillMp3StreamBuffer()
// (RE) rtl:0x10010680; dbg:0x10025660; thunk dbg:0x10003125
//
// Decode resident or streaming MP3 data and update streaming event positions.
//
// Returns: S_OK, including decoder, loop-seek and playback failures.
// =============================================================*/

HRESULT
CA3dSource::FillMp3StreamBuffer(void *pvBuffer, DWORD dwOffset, DWORD dwLength,
				int nSegment)
{
	DWORD dwFilled;
	DWORD dwProduced;
	DWORD dwCopy;
	int   nRead;
	int   nStatus;
	int   nNext;

	dwFilled   = 0;
	dwProduced = 0;
	nRead      = 0;

	g_cMp3FillActive++;

	if (m_pMp3Decode->dwDecodeLeft)
	{
		memcpy((BYTE *) pvBuffer + dwOffset,
		       (BYTE *) m_pMp3Decode->pDecode +
		       m_pMp3Decode->dwDecodeFill - m_pMp3Decode->dwDecodeLeft,
		       m_pMp3Decode->dwDecodeLeft);

		dwFilled += m_pMp3Decode->dwDecodeLeft;
		m_pMp3Decode->dwDecodeLeft = 0;
	}

	while (dwFilled < dwLength)
	{
		nRead += FillMp3Decoder();

		if (m_fStreaming)
			nStatus = m_pMp3Decoder->DecodeBlock(m_pMp3Decode->pDecode,
							     m_pMp3Decode->dwDecodeSize,
							     &dwProduced);
		else
			nStatus = m_pMp3Decoder->DecodeBlock(
					(BYTE *) pvBuffer + dwFilled,
					dwLength - dwFilled, &dwProduced);

		if ((nStatus & A3D_MP3_STATUS_SEVERITY_MASK) != 0 &&
		    (nStatus & A3D_MP3_STATUS_SEVERITY_MASK) != 0x40000000)
			break;

		if (nStatus)
			TRACE("CA3dSource::FillMp3Buffer() - Decoder: %s\n",
			      Mp3SscErrorString(nStatus));

		dwCopy = dwProduced;

		if (m_fStreaming)
		{
			m_pMp3Decode->dwDecodeFill = dwProduced;

			if (dwCopy > dwLength - dwFilled)
				dwCopy = dwLength - dwFilled;

			memcpy((BYTE *) pvBuffer + dwOffset + dwFilled,
			       m_pMp3Decode->pDecode, dwCopy);

			m_pMp3Decode->dwDecodeLeft = dwProduced - dwCopy;
		}

		dwFilled += dwCopy;
	}

	if (m_fStreaming)
	{
		m_dwStreamRemaining -= nRead;

		if (dwOffset)
		{
			m_aPlayEventMark[3][0] = dwOffset + m_dwStreamSegmentSize * nSegment;
			m_aPlayEventMark[3][1] = m_dwStreamSize - m_dwStreamRemaining;
		}
		else
		{
			m_aPlayEventMark[nSegment][1] = m_dwStreamSize - m_dwStreamRemaining - nRead;

			nNext = nSegment + 1;

			if (nNext > 2)
				nNext = 0;

			m_aPlayEventMark[nNext][1] = (DWORD) A3DSOURCE_WAVEEVENT_NULL;

			if (m_aPlayEventMark[3][0] != (DWORD) A3DSOURCE_WAVEEVENT_NULL &&
			    m_aPlayEventMark[3][0] >= (DWORD) (nSegment * m_dwStreamSegmentSize) &&
			    m_aPlayEventMark[3][0] < m_dwStreamSegmentSize + nSegment * m_dwStreamSegmentSize)
			{
				m_aPlayEventMark[3][0] = (DWORD) A3DSOURCE_WAVEEVENT_NULL;
				m_aPlayEventMark[3][1] = (DWORD) A3DSOURCE_WAVEEVENT_NULL;
			}
		}

		MapMp3Events(nSegment, nRead, TRUE);
	}

	if (dwFilled < dwLength)
	{
		if (m_fStreaming && (m_dwPlayFlags & DSBPLAY_LOOPING))
		{
			if (mmioSeek(m_hmmio, 0, SEEK_SET))
			{
				DBGSTR(
					"CA3dSource::FillMp3Buffer() - Failed to seek to beginning of data section.\n");
			}
			else
			{
				m_pMp3Decoder->Reset();
				m_dwStreamRemaining = m_dwStreamSize;

				FillMp3StreamBuffer(pvBuffer, dwOffset + dwFilled,
						    dwLength - dwFilled, nSegment);
			}
		}
		else
		{
			FillStreamSilence((BYTE *) pvBuffer + dwFilled, dwLength - dwFilled);

			if (m_fStreaming && nSegment == 2)
			{
				m_dwStreamEnded = 1;
				m_fLooping      = 0;

				if (FAILED(m_pBuffer->Play(0, 0, m_fLooping)))
					DBGSTR(
						"CA3dSource::FillMp3Buffer() - Unable to change play mode.\n");
			}
		}
	}

	g_cMp3FillActive--;

	return (S_OK);
}

/* =============================================================
// FillAc3StreamBuffer()
// (RE) rtl:0x10011590; dbg:0x100271E0; thunk dbg:0x1000175D
//
// Decode an AC-3 segment, retain excess PCM and update streaming event
// positions. The original copies ignore dwOffset, so loop recursion overwrites
// the buffer head.
//
// Returns: The initial FillAc3Decoder failure; S_OK on the remaining paths.
// =============================================================*/

HRESULT
CA3dSource::FillAc3StreamBuffer(void *pvBuffer, DWORD dwOffset, DWORD dwLength,
				int nSegment)
{
	DWORD   dwRemaining;
	DWORD   dwCopy;
	int     nRead;
	int     nPass;
	int     nDecode;
	int     nNext;
	HRESULT hrStatus;

	hrStatus    = S_OK;
	dwRemaining = dwLength;
	nRead       = 0;

	g_cAc3FillActive++;

	if (m_pAc3Decode->dwDecodeLeft)
	{
		memcpy((BYTE *) pvBuffer + (dwLength - dwRemaining),
		       m_pAc3Decode->pDecodeLeft,
		       m_pAc3Decode->dwDecodeLeft);

		dwRemaining -= m_pAc3Decode->dwDecodeLeft;
		m_pAc3Decode->dwDecodeLeft = 0;
	}

	nPass = 0;

	while (dwRemaining)
	{
		nPass  = FillAc3Decoder(&hrStatus);
		nRead += nPass;

		if (FAILED(hrStatus))
		{
			if (hrStatus == A3DERROR_UNRECOGNIZED_FORMAT)
				DBGSTR(
					"CA3dSource::FillAc3StreamBuffer - Invalid AC3 file format.\n");

			/* The original returns without decrementing g_cAc3FillActive. */
			return (hrStatus);
		}

		nDecode = Ac3DecodeAudio(m_pAc3Decode->hDecoder, m_pAc3Format);

		/* Bundled-decoder status 139 requests another input frame. */
		if (nDecode == A3D_AC3_MORE_INPUT)
		{
			nPass = FillAc3Decoder(&hrStatus);

			if (FAILED(hrStatus))
			{
				DBGSTR(
					"CA3dSource::FillAc3StreamBuffer - Not enough data. Corrupt Ac3 file?\n");
				break;
			}

			nRead += nPass;
		}

		dwCopy = m_pAc3Format->dwProduced;

		if (dwCopy > dwRemaining)
			dwCopy = dwRemaining;

		memcpy((BYTE *) pvBuffer + (dwLength - dwRemaining),
		       m_pAc3Decode->pDecode, dwCopy);

		if (m_pAc3Format->dwProduced > dwCopy)
		{
			m_pAc3Decode->dwDecodeLeft = m_pAc3Format->dwProduced - dwCopy;
			m_pAc3Decode->pDecodeLeft  = (BYTE *) m_pAc3Decode->pDecode + dwCopy;
		}

		dwRemaining -= dwCopy;

		if (!nPass)
			break;
	}

	m_dwStreamRemaining -= nRead;

	if (dwOffset)
	{
		m_aPlayEventMark[3][0] = dwOffset + m_dwStreamSegmentSize * nSegment;
		m_aPlayEventMark[3][1] = m_dwStreamSize - m_dwStreamRemaining;
	}
	else
	{
		m_aPlayEventMark[nSegment][1] = m_dwStreamSize - m_dwStreamRemaining - nRead;

		nNext = nSegment + 1;

		if (nNext > 2)
			nNext = 0;

		m_aPlayEventMark[nNext][1] = (DWORD) A3DSOURCE_WAVEEVENT_NULL;

		if (m_aPlayEventMark[3][0] != (DWORD) A3DSOURCE_WAVEEVENT_NULL &&
		    m_aPlayEventMark[3][0] >= (DWORD) (nSegment * m_dwStreamSegmentSize) &&
		    m_aPlayEventMark[3][0] < m_dwStreamSegmentSize + nSegment * m_dwStreamSegmentSize)
		{
			m_aPlayEventMark[3][0] = (DWORD) A3DSOURCE_WAVEEVENT_NULL;
			m_aPlayEventMark[3][1] = (DWORD) A3DSOURCE_WAVEEVENT_NULL;
		}
	}

	MapAc3Events(nSegment, nRead, TRUE);

	if (!m_dwStreamRemaining && !m_pAc3Decode->dwDecodeLeft)
	{
		if (m_dwPlayFlags & DSBPLAY_LOOPING)
		{
			if (mmioSeek(m_hmmio, 0, SEEK_SET))
			{
				DBGSTR(
					"CA3dSource::FillAc3Buffer() - Failed to seek to beginning of data section.\n");
			}
			else
			{
				Ac3ResetAudio(m_pAc3Decode->hDecoder, m_pAc3Format->dwMode);
				m_dwStreamRemaining = m_dwStreamSize;

				hrStatus = FillAc3StreamBuffer(pvBuffer,
						dwOffset + (dwLength - dwRemaining),
						dwRemaining, nSegment);
			}
		}
		else
		{
			DBGSTR(
				"CA3dSource::FillAc3StreamBuffer() - Silencing memory\n");

			FillStreamSilence((BYTE *) pvBuffer + (dwLength - dwRemaining),
					  dwRemaining);

			if (nSegment == 2)
			{
				m_dwStreamEnded = 1;
				m_fLooping      = 0;

				if (FAILED(m_pBuffer->Play(0, 0, m_fLooping)))
					DBGSTR(
						"CA3dSource::FillAc3StreamBuffer() - Unable to change play mode.\n");
			}
		}
	}

	g_cAc3FillActive--;

	return (S_OK);
}

/* Frame sizes in 16-bit words: 38 codes each for 48, 44.1 and 32 kHz.
 * (RE) dbg:0x10128C98. */
static const DWORD	g_adwAc3FrameSize[114] =
{
	  64,   64,   80,   80,   96,   96,  112,  112,  128,  128,
	 160,  160,  192,  192,  224,  224,  256,  256,  320,  320,
	 384,  384,  448,  448,  512,  512,  640,  640,  768,  768,
	 896,  896, 1024, 1024, 1152, 1152, 1280, 1280,
	  69,   70,   87,   88,  104,  105,  121,  122,  139,  140,
	 174,  175,  208,  209,  243,  244,  278,  279,  348,  349,
	 417,  418,  487,  488,  557,  558,  696,  697,  835,  836,
	 975,  976, 1114, 1115, 1253, 1254, 1393, 1394,
	  96,   96,  120,  120,  144,  144,  168,  168,  192,  192,
	 240,  240,  288,  288,  336,  336,  384,  384,  480,  480,
	 576,  576,  672,  672,  768,  768,  960,  960, 1152, 1152,
	1344, 1344, 1536, 1536, 1728, 1728, 1920, 1920,
};

/* Nominal kbps by frame-size code; (RE) dbg:0x10128E60. */
static const DWORD	g_adwAc3Bitrate[38] =
{
	  32,   32,   40,   40,   48,   48,   56,   56,   64,   64,
	  80,   80,   96,   96,  112,  112,  128,  128,  160,  160,
	 192,  192,  224,  224,  256,  256,  320,  320,  384,  384,
	 448,  448,  512,  512,  576,  576,  640,  640,
};

/* CRC-16 table, polynomial 0x8005; (RE) dbg:0x10128EF8. */
static const WORD	g_awAc3Crc[256] =
{
	0x0000, 0x8005, 0x800F, 0x000A, 0x801B, 0x001E, 0x0014, 0x8011,
	0x8033, 0x0036, 0x003C, 0x8039, 0x0028, 0x802D, 0x8027, 0x0022,
	0x8063, 0x0066, 0x006C, 0x8069, 0x0078, 0x807D, 0x8077, 0x0072,
	0x0050, 0x8055, 0x805F, 0x005A, 0x804B, 0x004E, 0x0044, 0x8041,
	0x80C3, 0x00C6, 0x00CC, 0x80C9, 0x00D8, 0x80DD, 0x80D7, 0x00D2,
	0x00F0, 0x80F5, 0x80FF, 0x00FA, 0x80EB, 0x00EE, 0x00E4, 0x80E1,
	0x00A0, 0x80A5, 0x80AF, 0x00AA, 0x80BB, 0x00BE, 0x00B4, 0x80B1,
	0x8093, 0x0096, 0x009C, 0x8099, 0x0088, 0x808D, 0x8087, 0x0082,
	0x8183, 0x0186, 0x018C, 0x8189, 0x0198, 0x819D, 0x8197, 0x0192,
	0x01B0, 0x81B5, 0x81BF, 0x01BA, 0x81AB, 0x01AE, 0x01A4, 0x81A1,
	0x01E0, 0x81E5, 0x81EF, 0x01EA, 0x81FB, 0x01FE, 0x01F4, 0x81F1,
	0x81D3, 0x01D6, 0x01DC, 0x81D9, 0x01C8, 0x81CD, 0x81C7, 0x01C2,
	0x0140, 0x8145, 0x814F, 0x014A, 0x815B, 0x015E, 0x0154, 0x8151,
	0x8173, 0x0176, 0x017C, 0x8179, 0x0168, 0x816D, 0x8167, 0x0162,
	0x8123, 0x0126, 0x012C, 0x8129, 0x0138, 0x813D, 0x8137, 0x0132,
	0x0110, 0x8115, 0x811F, 0x011A, 0x810B, 0x010E, 0x0104, 0x8101,
	0x8303, 0x0306, 0x030C, 0x8309, 0x0318, 0x831D, 0x8317, 0x0312,
	0x0330, 0x8335, 0x833F, 0x033A, 0x832B, 0x032E, 0x0324, 0x8321,
	0x0360, 0x8365, 0x836F, 0x036A, 0x837B, 0x037E, 0x0374, 0x8371,
	0x8353, 0x0356, 0x035C, 0x8359, 0x0348, 0x834D, 0x8347, 0x0342,
	0x03C0, 0x83C5, 0x83CF, 0x03CA, 0x83DB, 0x03DE, 0x03D4, 0x83D1,
	0x83F3, 0x03F6, 0x03FC, 0x83F9, 0x03E8, 0x83ED, 0x83E7, 0x03E2,
	0x83A3, 0x03A6, 0x03AC, 0x83A9, 0x03B8, 0x83BD, 0x83B7, 0x03B2,
	0x0390, 0x8395, 0x839F, 0x039A, 0x838B, 0x038E, 0x0384, 0x8381,
	0x0280, 0x8285, 0x828F, 0x028A, 0x829B, 0x029E, 0x0294, 0x8291,
	0x82B3, 0x02B6, 0x02BC, 0x82B9, 0x02A8, 0x82AD, 0x82A7, 0x02A2,
	0x82E3, 0x02E6, 0x02EC, 0x82E9, 0x02F8, 0x82FD, 0x82F7, 0x02F2,
	0x02D0, 0x82D5, 0x82DF, 0x02DA, 0x82CB, 0x02CE, 0x02C4, 0x82C1,
	0x8243, 0x0246, 0x024C, 0x8249, 0x0258, 0x825D, 0x8257, 0x0252,
	0x0270, 0x8275, 0x827F, 0x027A, 0x826B, 0x026E, 0x0264, 0x8261,
	0x0220, 0x8225, 0x822F, 0x022A, 0x823B, 0x023E, 0x0234, 0x8231,
	0x8213, 0x0216, 0x021C, 0x8219, 0x0208, 0x820D, 0x8207, 0x0202,
};

/* =============================================================
// FillAc3Decoder()
// (RE) rtl:0x10014DA0; dbg:0x1002EA00; thunk dbg:0x1000230B
//
// Read sync frames until one passes CRC and report status through phrStatus.
//
// Returns: File bytes consumed, including resynchronization seeks.
// =============================================================*/

int
CA3dSource::FillAc3Decoder(HRESULT *phrStatus)
{
	DWORD   dwFrameWords;
	DWORD   dwBitrate;
	DWORD   cbFrameRead; /* Bytes read */
	int     nRead;
	int     nBack;
	BOOL    fBigEndian;
	BOOL    fDone;
	HRESULT hrCrc;

	nRead       = 0;
	cbFrameRead = 0;
	fBigEndian  = FALSE;
	fDone       = FALSE;

	while (!fDone)
	{
		*phrStatus = Ac3GetSyncFrame(&cbFrameRead, &fBigEndian, &dwBitrate,
					     &dwFrameWords);

		if (FAILED(*phrStatus))
			return (nRead);

		nRead += cbFrameRead;
		fDone  = TRUE;

		m_pAc3Decode->dwCrcPending = 1;

		if (m_pAc3Decode->dwCrcPending)
		{
			hrCrc = Ac3CrcCheck(fBigEndian, 2 * dwFrameWords - 2);

			if (SUCCEEDED(hrCrc))
			{
				m_pAc3Decode->dwCrcPending = 0;
			}
			else
			{
				fDone = FALSE;
				nBack = (int) ((BYTE *) m_pAc3Format->pFrameEnd -
					       (BYTE *) m_pAc3Format->pFrameStart) - 2;

				if (mmioSeek(m_hmmio,
					     2 - (LONG) ((BYTE *) m_pAc3Format->pFrameEnd -
							 (BYTE *) m_pAc3Format->pFrameStart),
					     SEEK_CUR) == -1 && nBack != -1)
				{
					DBGSTR(
						"CA3dSource::ReadAc3Data - Failed to back-seek in Ac3 file.\n");
					*phrStatus = E_FAIL;

					return (nRead);
				}

				nRead -= nBack;
			}
		}
	}

	if (!m_pAc3Decode->wSyncWord)
	{
		if (fBigEndian)
			m_pAc3Decode->wSyncWord = A3D_AC3_SYNC_BIG_ENDIAN;
		else
			m_pAc3Decode->wSyncWord = A3D_AC3_SYNC_LITTLE_ENDIAN;
	}

	*phrStatus = S_OK;

	return (nRead);
}

/* =============================================================
// Ac3GetSyncFrame()
// (RE) rtl:0x10014F20; dbg:0x1002EC00; thunk dbg:0x100027D9
//
// Read a sync frame and report byte count, byte order, bitrate and frame size.
// The original does not guard the rate-code table indices.
//
// Returns: S_OK, including end of input; E_FAIL on a read error.
// =============================================================*/

HRESULT
CA3dSource::Ac3GetSyncFrame(DWORD *pcbRead, BOOL *pfBigEndian, DWORD *pdwBitrate,
			    DWORD *pdwFrameWords)
{
	BYTE *pFrame;
	BYTE  bPrev;
	BYTE  bCur;
	BYTE  bCode;
	BOOL  fFound;
	int   nSkipped;
	int   nRead;

	bPrev    = 0;
	bCur     = 0;
	fFound   = FALSE;
	nSkipped = -1;

	*pdwBitrate    = 0;
	*pdwFrameWords = 0;
	*pcbRead       = 0;

	while (!fFound)
	{
		nSkipped++;
		bPrev = bCur;

		nRead = mmioRead(m_hmmio, (HPSTR) &bCur, 1);

		if (nRead < 0)
			return (E_FAIL);

		*pcbRead += nRead;

		if (!nRead)
			return (S_OK);

		if (m_pAc3Decode->wSyncWord != A3D_AC3_SYNC_LITTLE_ENDIAN &&
		    bPrev == 0x0B && bCur == 0x77)
		{
			*pfBigEndian = 1;
			fFound       = TRUE;
		}

		if (m_pAc3Decode->wSyncWord != A3D_AC3_SYNC_BIG_ENDIAN &&
		    bPrev == 0x77 && bCur == 0x0B)
		{
			*pfBigEndian = 0;
			fFound       = TRUE;
		}
	}

	if (nSkipped > 0)
		m_pAc3Decode->dwCrcPending = 1;

	pFrame    = (BYTE *) m_pAc3Decode->pInput;
	pFrame[0] = bPrev;
	pFrame[1] = bCur;

	nRead = mmioRead(m_hmmio, (HPSTR) (pFrame + 2), 4);

	if (nRead < 0)
		return (E_FAIL);

	*pcbRead += nRead;

	if (!nRead)
		return (S_OK);

	if (*pfBigEndian)
		bCode = pFrame[4];
	else
		bCode = pFrame[5];

	*pdwBitrate    = g_adwAc3Bitrate[bCode & A3D_AC3_FRAME_SIZE_CODE_MASK];
	*pdwFrameWords = g_adwAc3FrameSize[
		38 * ((bCode >> 6) & A3D_AC3_SAMPLE_RATE_CODE_MASK) +
		(bCode & A3D_AC3_FRAME_SIZE_CODE_MASK)];

	nRead = mmioRead(m_hmmio, (HPSTR) (pFrame + 6), 2 * *pdwFrameWords - 6);

	if (nRead >= 0)
	{
		*pcbRead += nRead;

		m_pAc3Format->pFrameStart = m_pAc3Decode->pInput;
		m_pAc3Format->pFrameEnd   = (BYTE *) m_pAc3Format->pFrameStart +
					    2 * *pdwFrameWords;

		return (S_OK);
	}

	DBGSTR(
		"CA3dSource::Ac3GetSyncFrame - Error reading from file after syncframe detection.\n");

	return (E_FAIL);
}

/* =============================================================
// Ac3CrcCheck()
// (RE) dbg:0x1002EF50
//
// Check cbData bytes after the sync word, swapping byte pairs for little-endian
// frames. Byte-count // Returns: S_OK for zero CRC or a negative length; E_FAIL
// otherwise.
// =============================================================*/

HRESULT
CA3dSource::Ac3CrcCheck(BOOL fBigEndian, int cbData)
{
	BYTE *pData;
	BYTE  bData;
	WORD  wCrc;
	int   nToggle;

	if (cbData < 0)
		return (S_OK);

	nToggle = 1;
	wCrc    = 0;
	pData   = (BYTE *) m_pAc3Format->pFrameStart + 2;

	while (cbData--)
	{
		if (fBigEndian)
		{
			bData = *pData++;
		}
		else
		{
			bData = pData[nToggle];

			if (!nToggle)
				pData += 2;

			nToggle ^= 1;
		}

		wCrc = (WORD) (g_awAc3Crc[(BYTE) (bData ^ (wCrc >> 8))] ^
			       (bData | (WORD) (wCrc << 8)));
	}

	if (wCrc)
		return (E_FAIL);

	return (S_OK);
}

/* =============================================================
// FillStreamTail()
// (RE) dbg:0x10027BC0; thunk dbg:0x10004179
//
// Fill a wave-buffer byte range with silence.
// The original ignores Lock failure.
//
// Returns: Buffer Unlock result.
// =============================================================*/

HRESULT
CA3dSource::FillStreamTail(DWORD dwStart, DWORD dwEnd)
{
	void *pvLock1;
	void *pvLock2;
	DWORD dwLocked1;
	DWORD dwLocked2;

	m_pBuffer->Lock(dwStart, dwEnd - dwStart, &pvLock1, &dwLocked1,
			&pvLock2, &dwLocked2, 0);

	FillStreamSilence(pvLock1, dwLocked1);

	return (m_pBuffer->Unlock(pvLock1, dwLocked1, pvLock2, dwLocked2));
}

/* =============================================================
// FillStreamSilence()
// (RE) dbg:0x10027C70; thunk dbg:0x10004345
//
// Fill bytes with zero for 16-bit samples, or 0x80 for other sample widths.
// =============================================================*/

void
CA3dSource::FillStreamSilence(void *pvBuffer, DWORD dwSize)
{
	if (m_pwfxFormat->wBitsPerSample == 16)
		memset(pvBuffer, 0, dwSize);
	else
		memset(pvBuffer, 128, dwSize);
}

/* =============================================================
// StartStreaming()
// (RE) dbg:0x10027CE0; thunk dbg:0x100037A6
// Catch funclet dbg:0x10027DD6
//
// Create streaming events, register the source with the worker and wait for
// acknowledgement. The original reports success when the registration SetEvent
// fails.
//
// Returns: S_OK; CreateStreamThread or InitStreamEvents failure.
// =============================================================*/

HRESULT
CA3dSource::StartStreaming(void)
{
HRESULT	hr;

	hr = S_OK;

	try
	{
		if (g_hStreamThread == INVALID_HANDLE_VALUE)
		{
			hr = CreateStreamThread();

			if (FAILED(hr))
				throw "Unable to start streaming thread callback.";
		}

		hr = InitStreamEvents();

		if (FAILED(hr))
			throw "Unable to initialize stream events.";

		g_pStreamNewSource = this;

		if (!SetEvent(g_hStreamNewSource))
			throw "Unable to add new stream source.";

		WaitForSingleObject(g_hStreamAck, INFINITE);
	}
	catch (const char *pszWhy)
	{
		TRACE("CA3dSource::InitializeStream() - %s\n", pszWhy);

		m_fStreaming = 0;

		return (hr);
	}

	return (S_OK);
}

/* =============================================================
// CreateStreamThread()
// (RE) dbg:0x10029080; thunk dbg:0x1000338C
// Catch funclet dbg:0x100291FC
//
// Create shared service events and start the streaming worker.
//
// Returns:
//   S_OK
//   E_FAIL  if an event or the thread cannot be created
// =============================================================*/

HRESULT
CA3dSource::CreateStreamThread(void)
{
	try
	{
		g_hStreamNewSource = CreateEventA(NULL, TRUE, FALSE, NULL);

		if (!g_hStreamNewSource)
			throw "Could not create event (NewSrc).";

		g_hStreamDelSource = CreateEventA(NULL, TRUE, FALSE, NULL);

		if (!g_hStreamDelSource)
			throw "Could not create event (DelSrc).";

		g_hStreamKill = CreateEventA(NULL, TRUE, FALSE, NULL);

		if (!g_hStreamKill)
			throw "Could not create event (kill stream).";

		g_hStreamAck = CreateEventA(NULL, FALSE, FALSE, NULL);

		if (!g_hStreamAck)
			throw "Could not create event (got add).";

		g_hStreamThreadDead = CreateEventA(NULL, FALSE, FALSE, NULL);

		if (!g_hStreamThreadDead)
			throw "Could not create event (thread dead).";

		g_hStreamThread = CreateThread(NULL, 0, StreamingCallback, this, 0,
					       &g_dwStreamThreadId);

		if (!g_hStreamThread)
			throw "Could not create thread.";

		g_bStreamThreadRun = 1;

		SetStreamingPriority();
	}
	catch (const char *pszWhy)
	{
		TRACE("CA3dSource::StartStreamingCallback() - %s\n", pszWhy);

		if (g_hStreamNewSource != INVALID_HANDLE_VALUE)
		{
			CloseHandle(g_hStreamNewSource);
			g_hStreamNewSource = INVALID_HANDLE_VALUE;
		}

		if (g_hStreamDelSource != INVALID_HANDLE_VALUE)
		{
			CloseHandle(g_hStreamDelSource);
			g_hStreamDelSource = INVALID_HANDLE_VALUE;
		}

		if (g_hStreamAck != INVALID_HANDLE_VALUE)
		{
			CloseHandle(g_hStreamAck);
			g_hStreamAck = INVALID_HANDLE_VALUE;
		}

		if (g_hStreamKill != INVALID_HANDLE_VALUE)
		{
			CloseHandle(g_hStreamKill);
			g_hStreamKill = INVALID_HANDLE_VALUE;
		}

		if (g_hStreamThreadDead != INVALID_HANDLE_VALUE)
		{
			CloseHandle(g_hStreamThreadDead);
			g_hStreamThreadDead = INVALID_HANDLE_VALUE;
		}

		return (E_FAIL);
	}

	return (S_OK);
}

/* =============================================================
// SetStreamingPriority()
// (RE) dbg:0x10029380; thunk dbg:0x10002653
//
// Apply pending API streaming priority to the shared worker.
//
// Returns:
//   S_OK    if applied or no update is needed
//   E_FAIL  if SetThreadPriority fails
// =============================================================*/

HRESULT
CA3dSource::SetStreamingPriority(void)
{
	BOOL  bOk;
	LPSTR pszError;
	char  szMessage[96];

	if (!m_pApi || !m_pApi->m_dwStreamingPropertiesSet ||
	    g_hStreamThread == INVALID_HANDLE_VALUE)
		return (S_OK);

	if (m_pApi->m_dwStreamThreadPriority == A3D_STREAMING_PRIORITY_HIGH)
		bOk = SetThreadPriority(g_hStreamThread, THREAD_PRIORITY_ABOVE_NORMAL);
	else if (m_pApi->m_dwStreamThreadPriority == A3D_STREAMING_PRIORITY_HIGHEST)
		bOk = SetThreadPriority(g_hStreamThread, THREAD_PRIORITY_HIGHEST);
	else
		bOk = SetThreadPriority(g_hStreamThread, THREAD_PRIORITY_NORMAL);

	if (!bOk)
	{
		DBGSTR("CA3dSource::SetStreamingPriority() - Could not set thread priority.\n");

#ifdef _DEBUG
		FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
			       FORMAT_MESSAGE_FROM_SYSTEM | 0x200,
			       NULL, GetLastError(), 0x400, (LPSTR) &pszError, 0, NULL);
		OutputDebugStringA(pszError);
		LocalFree(pszError);
#endif

		return (E_FAIL);
	}

	m_pApi->m_dwStreamingPropertiesSet = 0;

	return (S_OK);
}

/* =============================================================
// InitStreamEvents()
// (RE) dbg:0x10027E90; thunk dbg:0x1000244B
// Catch funclet dbg:0x100280D3
//
// Create the source event mutex and four events, then install three segment
// marks.
//
// Returns:
//   The final SetStreamEvent result
//   E_FAIL                           if initialization fails
// =============================================================*/

HRESULT
CA3dSource::InitStreamEvents(void)
{
	HRESULT hr;
	int     i;

	try
	{
		m_hEventMutex = CreateMutexA(NULL, FALSE, NULL);

		if (!m_hEventMutex)
			throw "Could not create wave event mutex.";

		m_ahStreamEvents[0] = CreateEventA(NULL, TRUE, FALSE, NULL);

		if (!m_ahStreamEvents[0])
			throw "Could not create event (end).";

		m_ahStreamEvents[1] = CreateEventA(NULL, TRUE, FALSE, NULL);

		if (!m_ahStreamEvents[1])
			throw "Could not create event (one third).";

		m_ahStreamEvents[2] = CreateEventA(NULL, TRUE, FALSE, NULL);

		if (!m_ahStreamEvents[2])
			throw "Could not create event (two thirds).";

		m_ahStreamEvents[3] = CreateEventA(NULL, TRUE, FALSE, NULL);

		if (!m_ahStreamEvents[3])
			throw "Could not create event (end).";

		hr = SetStreamEvent(m_dwStreamSegmentSize, m_ahStreamEvents[1]);

		if (FAILED(hr))
			throw "Could not set wave event (one third).";

		hr = SetStreamEvent(2 * m_dwStreamSegmentSize, m_ahStreamEvents[2]);

		if (FAILED(hr))
			throw "Could not set wave event (two third).";

		hr = SetStreamEvent(m_dwWaveSize - 1, m_ahStreamEvents[3]);

		if (FAILED(hr))
			throw "Could not set wave event (end).";
	}
	catch (const char *pszWhy)
	{
		TRACE("CA3dSource::InitializeStreamEvents() - %s\n", pszWhy);

		for (i = 0; i < A3D_STREAM_EVENTS_PER_SOURCE; i++)
		{
			if (m_ahStreamEvents[i] != INVALID_HANDLE_VALUE)
			{
				CloseHandle(m_ahStreamEvents[i]);
				m_ahStreamEvents[i] = INVALID_HANDLE_VALUE;
			}
		}

		if (m_hEventMutex != INVALID_HANDLE_VALUE)
		{
			CloseHandle(m_hEventMutex);
			m_hEventMutex = INVALID_HANDLE_VALUE;
		}

		return (E_FAIL);
	}

	return (hr);
}

/* =============================================================
// SetStreamEvent()
// (RE) dbg:0x10028800; thunk dbg:0x10002D6F
//
// Update an internal stream event and rebuild DirectSound notifications.
// A failed mutex wait returns S_OK here; the original return is uninitialized.
// Both paths release the mutex even if the wait failed.
//
// Returns:
//   UpdatePlayEvent or RebuildNotifyPositions result
//   S_OK                                if the wait fails
// =============================================================*/

HRESULT
CA3dSource::SetStreamEvent(DWORD dwPosition, HANDLE hEvent)
{
	HRESULT	hr;

	hr = S_OK;

	if (WaitForSingleObject(m_hEventMutex, INFINITE) == WAIT_OBJECT_0)
	{
		hr = UpdatePlayEvent(dwPosition, hEvent, m_pStreamWaveEvents);

		if (SUCCEEDED(hr))
			hr = RebuildNotifyPositions(FALSE);
	}

	ReleaseMutex(m_hEventMutex);

	return (hr);
}

/* =============================================================
// AllocateAudioData()
// (RE) rtl:0x100130C0; dbg:0x1002A1D0
//
// Create the wave buffer and initialize its control data. The shared entry
// mapping with AllocateWaveData is unresolved; see its banner.
//
// Returns: S_OK; E_INVALIDARG for fewer than 1 byte;
//          A3DERROR_NEEDS_FORMAT_INFORMATION for a missing format or zero
//          sample rate; A3DERROR_SOURCE_IN_USE for an existing buffer with
//          IA3d5 or later; resource-manager mode failure;
//          A3DERROR_FAILED_CREATE_SOUNDBUFFER.
// =============================================================*/

STDMETHODIMP
CA3dSource::AllocateAudioData(INT nBytes)
{
DSBUFFERDESC dsbd;
DWORD        dwSavedMode;
DWORD        dwOrigRate;
HRESULT      hr;

	if (nBytes < 1)
		return (E_INVALIDARG);

	if (!m_pwfxFormat)
		return (A3DERROR_NEEDS_FORMAT_INFORMATION);

	if (!m_pwfxFormat->nSamplesPerSec)
		return (A3DERROR_NEEDS_FORMAT_INFORMATION);

	if (m_pBuffer && m_pApi->m_dwInterfaceVersion > 4)
		return (A3DERROR_SOURCE_IN_USE);

	ZeroMemory(&dsbd, sizeof(dsbd));

	dsbd.dwSize  = sizeof(dsbd);
	dsbd.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_CTRLVOLUME |
		       DSBCAPS_CTRLPAN | DSBCAPS_CTRLFREQUENCY;

	if (!(m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE))
		dsbd.dwFlags |= DSBCAPS_CTRL3D;

	if (!(m_dwFlags & A3DSOURCE_TYPESTREAMED))
		dsbd.dwFlags |= DSBCAPS_STATIC;

	if (m_pApi->IsCompat1011Set())
		dsbd.dwFlags |= DSBCAPS_GLOBALFOCUS;

	dsbd.dwBufferBytes = nBytes;
	m_dwWaveSize       = nBytes;
	dsbd.lpwfxFormat   = m_pwfxFormat;

	if (m_pApi->IsCompat1011Set())
	{
		dwOrigRate = m_pwfxFormat->nSamplesPerSec;

		if (dwOrigRate < A3D_RESAMPLE_BELOW)
		{
			m_pwfxFormat->nSamplesPerSec = A3D_RESAMPLE_RATE;
			m_fRateRatio = (A3DVAL) ((double) dwOrigRate /
						 (double) A3D_RESAMPLE_RATE);
		}
	}

	if ((m_dwFlags & A3DSOURCE_TYPEUNMANAGED) || m_fStreaming)
	{
		dwSavedMode = 0;

		hr = m_pApi->GetResManMode(&dwSavedMode);
		if (FAILED(hr))
		{
			DBGSTR(
				"CA3dSource::AllocateAudioData() - Failed call to GetResourceManagerMode().\n");

			return (hr);
		}

		hr = m_pApi->SetResManMode(A3D_RESOURCE_MODE_OFF);
		if (FAILED(hr))
		{
			DBGSTR(
				"CA3dSource::AllocateAudioData() - Failed call to SetResourceManagerMode().\n");

			return (hr);
		}

		if (FAILED(m_pDS->CreateSoundBuffer(&dsbd, &m_pBuffer, NULL)))
		{
			DBGSTR(
				"CA3dSource::AllocateAudioData() - Failed call to CreateSoundBuffer().\n");

			m_pBuffer = NULL;

			return (A3DERROR_FAILED_CREATE_SOUNDBUFFER);
		}

		m_pApi->SetResManMode(dwSavedMode);
	}
	else
	{
		if (FAILED(m_pDS->CreateSoundBuffer(&dsbd, &m_pBuffer, NULL)))
		{
			DBGSTR(
				"CA3dSource::AllocateAudioData() - Failed call to CreateSoundBuffer().\n");

			m_pBuffer = NULL;

			return (A3DERROR_FAILED_CREATE_SOUNDBUFFER);
		}
	}

	m_pBuffer->QueryInterface(IID_IDirectSound3DBuffer, (void **) &m_pBuffer3D);
	m_pBuffer->QueryInterface(IID_A3dVoiceCtl, (void **) &m_pA3dVoiceCtl);

	if (FAILED(m_pBuffer->QueryInterface(IID_IA3dBuffer, (void **) &m_pAurealBuffer)))
		m_pAurealBuffer = NULL;

	AllocBuffers();
	A3dSourceSeedDirect(this);

	return (S_OK);
}

/* =============================================================
// AllocBuffers()
// (RE) dbg:0x1001F970; thunk dbg:0x10001AEB (inlined in Retail)
//
// Allocate source-owned control blocks or borrow the driver's contiguous pair.
//
// Returns: S_OK on the allocation path, even if calloc fails; otherwise the
//          voice-interface query or control-buffer retrieval result.
// =============================================================*/

HRESULT
CA3dSource::AllocBuffers(void)
{
IResManBuffer      *pVoiceCtl;
LPA3DCTRL_SRC_SUPER pCtrlBase;
HRESULT             hr;

	if (m_pApi->m_dwUseDalInterface)
	{
		m_apBuffer[0] = (LPA3DCTRL_SRC_SUPER) calloc(1, A3D_SOURCE_BUFFER_SIZE);

		if (m_apBuffer[0])
		{
			m_apBuffer[1] = (LPA3DCTRL_SRC_SUPER) calloc(1, A3D_SOURCE_BUFFER_SIZE);

			if (m_apBuffer[1])
			{
				m_nBuffer     = 0;
				m_pBufferCurr = m_apBuffer[m_nBuffer];
			}
		}

		return (S_OK);
	}

	pVoiceCtl = NULL;

	hr = m_pBuffer->QueryInterface(IID_A3dVoiceCtl, (void **) &pVoiceCtl);
	if (FAILED(hr))
		return (hr);

	pCtrlBase = NULL;
	/* Slot 11 takes a control-pair output pointer through its declared DWORD. */
	hr = pVoiceCtl->GetControlBufferPair((DWORD) (DWORD_PTR) &pCtrlBase);

	if (SUCCEEDED(hr))
	{
		m_apBuffer[0] = pCtrlBase;
		m_apBuffer[1] =
			(LPA3DCTRL_SRC_SUPER) ((BYTE *) pCtrlBase + A3D_SOURCE_BUFFER_SIZE);

		if (pVoiceCtl)
		{
			pVoiceCtl->Release();
			pVoiceCtl = NULL;
		}

		m_nBuffer     = 0;
		m_pBufferCurr = m_apBuffer[m_nBuffer];

		return (S_OK);
	}

	if (pVoiceCtl)
	{
		pVoiceCtl->Release();
		pVoiceCtl = NULL;
	}

	return (hr);
}

/* =============================================================
// FreeAudioData()
// (RE) rtl:0x10013380; dbg:0x1002A610
//
// Clear play events, stop streaming and release decoder state, filename and
// buffer interfaces.
//
// Returns:
//   S_OK
//   A3DERROR_NO_WAVE_DATA  without a buffer
// =============================================================*/

STDMETHODIMP
CA3dSource::FreeAudioData()
{
	if (!m_pBuffer)
		return (A3DERROR_NO_WAVE_DATA);

	ClearPlayEvents();

	if (m_fStreaming)
	{
		ClearStreamPlayEvents();
		StopStreaming();
	}

	if (m_pMp3Decoder)
		CloseMp3Stream();
	else if (m_pAc3Decode)
		CloseAc3File();

	if (m_SourceCaps.szFilename)
	{
		delete m_SourceCaps.szFilename;
		m_SourceCaps.szFilename = NULL;
	}

	m_dwFormat = 0;

	/* Inlined release helper: dbg:0x1002A710; thunk dbg:0x10003341. */
	if (m_pReverbPropSet)
	{
		m_pReverbPropSet->Release();
		m_pReverbPropSet = NULL;
	}

	if (m_pA3dVoiceCtl)
	{
		m_pA3dVoiceCtl->Release();
		m_pA3dVoiceCtl = NULL;
	}

	if (m_pAurealBuffer)
	{
		m_pAurealBuffer->Release();
		m_pAurealBuffer = NULL;
	}

	if (m_pBuffer3D)
	{
		m_pBuffer3D->Release();
		m_pBuffer3D = NULL;
	}

	if (m_pBuffer)
	{
		m_pBuffer->Release();
		m_pBuffer = NULL;
	}

	return (S_OK);
}

/* =============================================================
// SetAudioFormat()
// (RE) dbg:0x1002A820; thunk dbg:0x10003BF7
//
// Copy the base 18-byte wave format before a playback buffer is allocated.
//
// Returns:
//   S_OK
//   E_POINTER                           null format
//   E_OUTOFMEMORY                       on allocation failure
//   A3DERROR_CANNOT_CHANGE_FORMAT_FOR_ALLOCATED_BUFFER
//                                       if a buffer exists
// =============================================================*/

STDMETHODIMP
CA3dSource::SetAudioFormat(LPVOID pvFormat)
{
	if (!pvFormat)
		return (E_POINTER);

	if (m_pBuffer)
		return (A3DERROR_CANNOT_CHANGE_FORMAT_FOR_ALLOCATED_BUFFER);

	if (m_pwfxFormat)
	{
		operator delete(m_pwfxFormat);
		m_pwfxFormat = NULL;
	}

	m_cbFormat   = 18;
	m_pwfxFormat = (LPWAVEFORMATEX) operator new(m_cbFormat);

	if (!m_pwfxFormat)
		return (E_OUTOFMEMORY);

	CopyMemory(m_pwfxFormat, pvFormat, m_cbFormat);

	return (S_OK);
}

/* =============================================================
// GetAudioFormat()
// (RE) dbg:0x1002A940; thunk dbg:0x10001BF4
//
// Copy the base 18-byte wave format to the caller.
//
// Returns:
//   S_OK
//   E_POINTER              a null output
//   A3DERROR_NO_WAVE_DATA  without a format
// =============================================================*/

STDMETHODIMP
CA3dSource::GetAudioFormat(LPVOID pvFormat)
{
	if (!pvFormat)
		return (E_POINTER);

	if (!m_pwfxFormat)
		return (A3DERROR_NO_WAVE_DATA);

	CopyMemory(pvFormat, m_pwfxFormat, 0x12);

	return (S_OK);
}

/* =============================================================
// GetAudioSize()
// (RE) rtl:0x10014400; dbg:0x1002DB80
//
// Read the resident wave size or full streaming input size.
//
// Returns: The size in bytes.
// =============================================================*/

STDMETHODIMP_(DWORD)
CA3dSource::GetAudioSize()
{
	if (m_fStreaming)
		return (m_dwStreamSize);

	return (m_dwWaveSize);
}

/* =============================================================
// SetPlayTime()
// (RE) rtl:0x1000D200; dbg:0x10020080
//
// Convert seconds to a byte offset and move the wave cursor.
//
// Returns:
//   SetWavePosition result
//   A3DERROR_NEEDS_FORMAT_INFORMATION   without a format
//   A3DERROR_ENCODED_SOURCE_TYPE_CANNOT_BE_TIME_SEEKED
//                                       MP3/AC-3
//   E_INVALIDARG                        negative time
// =============================================================*/

STDMETHODIMP
CA3dSource::SetPlayTime(A3DVAL fTime)
{
	if (!m_pwfxFormat)
		return (A3DERROR_NEEDS_FORMAT_INFORMATION);

	if (m_dwFormat & (A3DSOURCE_FORMAT_MP3 | A3DSOURCE_FORMAT_AC3))
		return (A3DERROR_ENCODED_SOURCE_TYPE_CANNOT_BE_TIME_SEEKED);

	if (fTime < 0.0f)
		return (E_INVALIDARG);

	return (SetWavePosition((DWORD) ((double) m_pwfxFormat->nAvgBytesPerSec * fTime)));
}

/* =============================================================
// GetPlayTime()
// (RE) rtl:0x1000D280; dbg:0x10020130
//
// Read the wave cursor in seconds.
//
// Returns:
//   S_OK                                or GetWavePosition failure
//   E_POINTER                           a null output
//   A3DERROR_NEEDS_FORMAT_INFORMATION   without a format
//   A3DERROR_ENCODED_SOURCE_TYPE_CANNOT_BE_TIME_SEEKED
//                                       MP3/AC-3
// =============================================================*/

STDMETHODIMP
CA3dSource::GetPlayTime(LPA3DVAL pfTime)
{
DWORD   dwPosition;
HRESULT hr;

	if (!pfTime)
		return (E_POINTER);

	if (!m_pwfxFormat)
		return (A3DERROR_NEEDS_FORMAT_INFORMATION);

	if (m_dwFormat & (A3DSOURCE_FORMAT_MP3 | A3DSOURCE_FORMAT_AC3))
		return (A3DERROR_ENCODED_SOURCE_TYPE_CANNOT_BE_TIME_SEEKED);

	hr = GetWavePosition(&dwPosition);

	if (FAILED(hr))
		return (hr);

	*pfTime = (A3DVAL) ((double) dwPosition / (double) (int) m_pwfxFormat->nAvgBytesPerSec);

	return (S_OK);
}

/* =============================================================
// SetPlayPosition()
// (RE) rtl:0x1000D030; dbg:0x1001FCD0
//
// Move the resident buffer cursor or post a streaming seek request.
//
// Returns: Buffer SetCurrentPosition result, or S_OK for a posted seek;
//          A3DERROR_NO_WAVE_DATA without a buffer; E_INVALIDARG beyond the
//          input size.
// =============================================================*/

STDMETHODIMP
CA3dSource::SetPlayPosition(DWORD dwPosition)
{
	if (!m_pBuffer)
		return (A3DERROR_NO_WAVE_DATA);

	if (m_fStreaming)
	{
		if (dwPosition > m_dwStreamSize)
			return (E_INVALIDARG);

		m_dwSeekPosition = dwPosition;
		SetEvent(m_ahStreamEvents[0]);

		return (S_OK);
	}

	if (dwPosition > m_dwWaveSize)
		return (E_INVALIDARG);

	return (m_pBuffer->SetCurrentPosition(dwPosition));
}

/* =============================================================
// GetPlayPosition()
// (RE) rtl:0x1000D0B0; dbg:0x1001FD90
//
// Read the buffer cursor and map streaming positions to input byte offsets.
// The original reads m_adwPlayMark when the cursor precedes every segment mark.
//
// Returns:
//   Buffer GetCurrentPosition result
//   E_POINTER                         a null output
//   A3DERROR_NO_WAVE_DATA             without a buffer
// =============================================================*/

STDMETHODIMP
CA3dSource::GetPlayPosition(LPDWORD pdwPosition)
{
DWORD   dwWrite;
HRESULT hr;
int     i;
LPDWORD pMark;
DWORD   dwEventValue;
DWORD   dwScaled;
DWORD   dwDelta;

	if (!pdwPosition)
		return (E_POINTER);

	if (!m_pBuffer)
		return (A3DERROR_NO_WAVE_DATA);

	hr = m_pBuffer->GetCurrentPosition(pdwPosition, &dwWrite);

	if (m_fStreaming && SUCCEEDED(hr))
	{
		dwEventValue = 0;
		dwScaled     = 0;

		for (i = 2; i >= 0 && *pdwPosition < m_aPlayEventMark[i][0]; i--)
			;

		pMark = (i >= 0) ? m_aPlayEventMark[i] : m_adwPlayMark;

		if (m_aPlayEventMark[3][0] >= pMark[0] && m_aPlayEventMark[3][0] <= *pdwPosition)
			pMark = m_aPlayEventMark[3];

		if (pMark[1] != A3DSOURCE_WAVEEVENT_NULL)
			dwEventValue = pMark[1];

		dwDelta = *pdwPosition - pMark[0];

		if (m_dwFormat & A3DSOURCE_FORMAT_WAVE)
		{
			dwScaled = *pdwPosition - pMark[0];
		}
		else if (m_dwFormat & A3DSOURCE_FORMAT_MP3)
		{
			dwScaled = (DWORD) ((double) m_dwStreamSize
					    / (double) ((LPA3DMP3STATE) m_pMp3Decode)->dwDecodedSize
					    * (double) dwDelta);
		}
		else if (m_dwFormat & A3DSOURCE_FORMAT_AC3)
		{
			if (((LPA3DAC3STATE) m_pAc3Decode)->fSegmentPositionScale <= 0.0f)
				dwScaled = (DWORD) ((double) dwDelta /
					((LPA3DAC3STATE) m_pAc3Decode)->fDefaultPositionScale);
			else
				dwScaled = (DWORD) ((double) dwDelta /
					((LPA3DAC3STATE) m_pAc3Decode)->fSegmentPositionScale);
		}

		*pdwPosition = dwScaled + dwEventValue;

		if (*pdwPosition > m_dwStreamSize)
			*pdwPosition = 0;
	}

	return (hr);
}

/* =============================================================
// SetPanValues()
// (RE) rtl:0x1000DB60; dbg:0x10020EE0
//
// Store native-mode pan gains.
// The original reads and stores two values even when dwNumValues is 1.
//
// Returns:
//   S_OK
//   E_POINTER                    null values
//   E_INVALIDARG                 count outside 1..2 or gain outside 0..1
//   A3DERROR_SOURCE_IN_A3D_MODE  when not native
// =============================================================*/

STDMETHODIMP
CA3dSource::SetPanValues(DWORD dwNumValues, LPA3DVAL pValues)
{
	if (!dwNumValues || dwNumValues > 2)
		return (E_INVALIDARG);

	if (!pValues)
		return (E_POINTER);

	if (!(m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE))
		return (A3DERROR_SOURCE_IN_A3D_MODE);

	if (pValues[0] < 0.0f || pValues[0] > 1.0f)
		return (E_INVALIDARG);

	if (pValues[1] < 0.0f || pValues[1] > 1.0f)
		return (E_INVALIDARG);

	m_afPanValues[0] = pValues[0];
	m_afPanValues[1] = pValues[1];

	return (S_OK);
}

/* =============================================================
// GetPanValues()
// (RE) rtl:0x1000DBF0; dbg:0x10020FE0
//
// Copy the requested pan gains, then report whether native mode is enabled.
//
// Returns:
//   S_OK
//   E_POINTER                    null values
//   E_INVALIDARG                 count outside 1..2
//   A3DERROR_SOURCE_IN_A3D_MODE  after copying when not native
// =============================================================*/

STDMETHODIMP
CA3dSource::GetPanValues(DWORD dwNumValues, LPA3DVAL pValues)
{
DWORD	i;

	if (!pValues)
		return (E_POINTER);

	if (!dwNumValues || dwNumValues > 2)
		return (E_INVALIDARG);

	for (i = 0; i < dwNumValues; i++)
		pValues[i] = m_afPanValues[i];

	if (m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE)
		return (S_OK);

	return (A3DERROR_SOURCE_IN_A3D_MODE);
}

/* =============================================================
// UpdatePlayEvent()
// (RE) dbg:0x10028990; thunk dbg:0x100036A7
//
// Add, update or remove a play-event record keyed by position.
// The original inserts -1 at the head (dbg:0x1002F380), others at the tail
// (dbg:0x1002F330); this implementation always uses AddTail.
//
// Returns:
//   S_OK
//   A3DERROR_NO_WAVE_DATA  without a buffer
//   E_OUTOFMEMORY          on allocation failure
// =============================================================*/

HRESULT
CA3dSource::UpdatePlayEvent(DWORD dwPosition, HANDLE hEvent,
			   CA3dPtrList<A3DPLAYEVENT> *pList)
{
A3DPLAYEVENT *pEvent;
A3DPLAYEVENT *pFound;
POSITION      pos;
POSITION      posCur;
POSITION      posFound;

	if (!m_pBuffer)
		return (A3DERROR_NO_WAVE_DATA);

	pFound   = NULL;
	posFound = NULL;

	pos = pList->GetHeadPosition();

	while (!pList->IsEnd(pos))
	{
		posCur = pos;
		pEvent = pList->GetNext(pos);

		if (pEvent->dwPosition == dwPosition)
		{
			pFound   = pEvent;
			posFound = posCur;
			break;
		}
	}

	if (hEvent != NULL)
	{
		if (pFound == NULL)
		{
			pFound = (A3DPLAYEVENT *) operator new(sizeof(A3DPLAYEVENT));

			if (pFound == NULL)
				return (E_OUTOFMEMORY);

			ZeroMemory(pFound, sizeof(A3DPLAYEVENT));

			pList->AddTail(pFound);
		}

		if (pFound->hEvent != hEvent)
		{
			if (pList != m_pStreamWaveEvents && m_fStreaming)
				pFound->dwMark = (DWORD) A3DSOURCE_WAVEEVENT_NULL;
			else
				pFound->dwMark = dwPosition;

			pFound->dwPosition = dwPosition;
			pFound->hEvent     = hEvent;
		}
	}
	else if (pFound != NULL)
	{
		pList->RemoveAt(posFound);
		operator delete(pFound);
	}

	return (S_OK);
}

/* =============================================================
// MapWaveEvents()
// (RE) dbg:0x10028E90; thunk dbg:0x10001032
//
// Map WAVE input positions to segment notifications and retire wrapped marks.
//
// Returns: ReleaseMutex result.
// =============================================================*/

BOOL
CA3dSource::MapWaveEvents(int nMark, DWORD dwSegLen, BOOL bWrap)
{
A3DPLAYEVENT *pEvent;
POSITION      pos;
DWORD         dwBase;
BOOL          bChanged;
int           nNext;

	dwBase   = m_aPlayEventMark[nMark][1];
	bChanged = FALSE;

	if (WaitForSingleObject(m_hEventMutex, INFINITE) == WAIT_OBJECT_0)
	{
		pos = m_pUserWaveEvents->GetHeadPosition();

		while (!m_pUserWaveEvents->IsEnd(pos))
		{
			pEvent = m_pUserWaveEvents->GetNext(pos);

			if (pEvent->dwPosition == (DWORD) A3DSOURCE_WAVEEVENT_STOP)
			{
				if (pEvent->dwMark != (DWORD) A3DSOURCE_WAVEEVENT_STOP)
				{
					pEvent->dwMark  = (DWORD) A3DSOURCE_WAVEEVENT_STOP;
					bChanged        = TRUE;
				}
			}
			else if (pEvent->dwPosition < dwBase ||
				 pEvent->dwPosition >= dwSegLen + dwBase)
			{
				nNext = nMark + 1;

				if (nNext > 2)
					nNext = 0;

				if (bWrap &&
				    pEvent->dwMark >= m_aPlayEventMark[nNext][0] &&
				    pEvent->dwMark < m_aPlayEventMark[nNext][0] + m_dwStreamSegmentSize)
				{
					pEvent->dwMark  = (DWORD) A3DSOURCE_WAVEEVENT_NULL;
					bChanged        = TRUE;
				}
			}
			else
			{
				pEvent->dwMark = m_aPlayEventMark[nMark][0] +
						 pEvent->dwPosition - dwBase;
				bChanged = TRUE;
			}
		}

		if (bChanged)
			RebuildNotifyPositions(FALSE);
	}

	return (ReleaseMutex(m_hEventMutex));
}

/* =============================================================
// MapMp3Events()
// (RE) dbg:0x10025C90; thunk dbg:0x10003963
//
// Map MP3 input positions to decoded segment notifications and retire wrapped
// marks.
//
// Returns: ReleaseMutex result.
// =============================================================*/

BOOL
CA3dSource::MapMp3Events(int nMark, DWORD dwSegLen, BOOL bWrap)
{
A3DPLAYEVENT *pEvent;
POSITION      pos;
DWORD         dwBase;
BOOL          bChanged;

	dwBase   = m_aPlayEventMark[nMark][1];
	bChanged = FALSE;

	if (WaitForSingleObject(m_hEventMutex, INFINITE) == WAIT_OBJECT_0)
	{
		pos = m_pUserWaveEvents->GetHeadPosition();

		while (!m_pUserWaveEvents->IsEnd(pos))
		{
			pEvent = m_pUserWaveEvents->GetNext(pos);

			if (pEvent->dwPosition == (DWORD) A3DSOURCE_WAVEEVENT_STOP)
			{
				if (pEvent->dwMark != (DWORD) A3DSOURCE_WAVEEVENT_STOP)
				{
					pEvent->dwMark  = (DWORD) A3DSOURCE_WAVEEVENT_STOP;
					bChanged        = TRUE;
				}
			}
			else if (pEvent->dwPosition < dwBase ||
				 pEvent->dwPosition >= dwSegLen + dwBase)
			{
				if (bWrap &&
				    pEvent->dwMark >= (DWORD) (nMark * m_dwStreamSegmentSize) &&
				    pEvent->dwMark < m_dwStreamSegmentSize + nMark * m_dwStreamSegmentSize)
				{
					pEvent->dwMark  = (DWORD) A3DSOURCE_WAVEEVENT_NULL;
					bChanged        = TRUE;
				}
			}
			else
			{
				pEvent->dwMark = (DWORD) ((double) m_aPlayEventMark[nMark][0] +
					(double) (int) ((LPA3DMP3STATE) m_pMp3Decode)->dwDecodedSize /
					(double) (int) m_dwStreamSize *
					(double) (int) (pEvent->dwPosition - dwBase));
				bChanged = TRUE;
			}
		}

		if (bChanged)
			RebuildNotifyPositions(FALSE);
	}

	return (ReleaseMutex(m_hEventMutex));
}

/* =============================================================
// MapAc3Events()
// (RE) dbg:0x1002DD70; thunk dbg:0x10002A31
//
// Map AC-3 input positions to segment notifications and update the cursor
// scale.
//
// Returns: ReleaseMutex result.
// =============================================================*/

BOOL
CA3dSource::MapAc3Events(int nMark, DWORD dwSegLen, BOOL bWrap)
{
A3DPLAYEVENT *pEvent;
POSITION      pos;
DWORD         dwBase;
BOOL          bChanged;

	dwBase   = m_aPlayEventMark[nMark][1];
	bChanged = FALSE;

	if (WaitForSingleObject(m_hEventMutex, INFINITE) == WAIT_OBJECT_0)
	{
		pos = m_pUserWaveEvents->GetHeadPosition();

		while (!m_pUserWaveEvents->IsEnd(pos))
		{
			pEvent = m_pUserWaveEvents->GetNext(pos);

			if (pEvent->dwPosition == (DWORD) A3DSOURCE_WAVEEVENT_STOP)
			{
				if (pEvent->dwMark != (DWORD) A3DSOURCE_WAVEEVENT_STOP)
				{
					pEvent->dwMark  = (DWORD) A3DSOURCE_WAVEEVENT_STOP;
					bChanged        = TRUE;
				}
			}
			else if (pEvent->dwPosition < dwBase ||
				 pEvent->dwPosition >= dwSegLen + dwBase)
			{
				if (bWrap &&
				    pEvent->dwMark >= (DWORD) (nMark * m_dwStreamSegmentSize) &&
				    pEvent->dwMark < m_dwStreamSegmentSize + nMark * m_dwStreamSegmentSize)
				{
					pEvent->dwMark  = (DWORD) A3DSOURCE_WAVEEVENT_NULL;
					bChanged        = TRUE;
				}
			}
			else
			{
				((LPA3DAC3STATE) m_pAc3Decode)->fSegmentPositionScale =
					(A3DVAL) ((double) m_dwStreamSegmentSize / (double) dwSegLen);

				pEvent->dwMark = (DWORD) ((double) m_aPlayEventMark[nMark][0] +
					(double) ((LPA3DAC3STATE) m_pAc3Decode)->fSegmentPositionScale *
					(double) (int) (pEvent->dwPosition - dwBase));
				bChanged = TRUE;
			}
		}

		if (bChanged)
			RebuildNotifyPositions(FALSE);
	}

	return (ReleaseMutex(m_hEventMutex));
}

/* =============================================================
// RebuildNotifyPositions()
// (RE) dbg:0x10028B80; thunk dbg:0x10001F2D
//
// Build and install DirectSound notifications from the stream and user lists.
// The original counts unwritten user entries and leaves a dangling array on
// failure.
//
// Returns:
//   S_OK
//   E_OUTOFMEMORY                       on allocation failure
//   A3DERROR_FAILED_QUERY_DIRECTSOUNDNOTIFY
//                                       or A3DERROR_DIRECTSOUNDNOTIFY_FAILED
// =============================================================*/

HRESULT
CA3dSource::RebuildNotifyPositions(BOOL bExcludeLoopMark)
{
IDirectSoundNotify *pNotify;
A3DPLAYEVENT       *pEvent;
POSITION            pos;
int                 cNotify;
int                 i;
HRESULT             hr;
HRESULT             hrResult;

	cNotify = m_pUserWaveEvents->GetCount() + m_pStreamWaveEvents->GetCount();

	if (m_pNotifyPositions)
	{
		operator delete(m_pNotifyPositions);
		m_pNotifyPositions = NULL;
	}

	if (cNotify)
	{
		m_pNotifyPositions = (LPDSBPOSITIONNOTIFY)
			operator new(sizeof(DSBPOSITIONNOTIFY) * cNotify);

		if (!m_pNotifyPositions)
			return (E_OUTOFMEMORY);
	}

	i = 0;

	pos = m_pStreamWaveEvents->GetHeadPosition();

	while (!m_pStreamWaveEvents->IsEnd(pos))
	{
		pEvent = m_pStreamWaveEvents->GetNext(pos);

		m_pNotifyPositions[i].dwOffset     = pEvent->dwMark;
		m_pNotifyPositions[i].hEventNotify = pEvent->hEvent;
		i++;
	}

	pos = m_pUserWaveEvents->GetHeadPosition();

	while (!m_pUserWaveEvents->IsEnd(pos))
	{
		pEvent = m_pUserWaveEvents->GetNext(pos);

		if (pEvent->dwMark != (DWORD) A3DSOURCE_WAVEEVENT_NULL &&
		    (!bExcludeLoopMark || pEvent->dwMark != (DWORD) A3DSOURCE_WAVEEVENT_STOP))
		{
			m_pNotifyPositions[i].dwOffset     = pEvent->dwMark;
			m_pNotifyPositions[i].hEventNotify = pEvent->hEvent;
		}

		i++;
	}

	hr = m_pBuffer->QueryInterface(IID_IDirectSoundNotify, (void **) &pNotify);

	if (FAILED(hr))
	{
		operator delete(m_pNotifyPositions);
		return (A3DERROR_FAILED_QUERY_DIRECTSOUNDNOTIFY);
	}

	hrResult = S_OK;

	hr = pNotify->SetNotificationPositions(cNotify, m_pNotifyPositions);

	if (FAILED(hr))
	{
		operator delete(m_pNotifyPositions);
		hrResult = A3DERROR_DIRECTSOUNDNOTIFY_FAILED;
	}

	if (pNotify)
	{
		pNotify->Release();
		pNotify = NULL;
	}

	return (hrResult);
}

/* =============================================================
// SetPlayEvent()
// (RE) rtl:0x10013CD0; dbg:0x1002CF50
//
// Update a user play event and map it to the current streaming segment.
// A failed mutex wait returns S_OK here; the original return is uninitialized.
// Both implementations release the streaming mutex even after a failed wait.
//
// Returns: UpdatePlayEvent result; S_OK if the wait fails. Notification errors
//          are ignored.
// =============================================================*/

STDMETHODIMP
CA3dSource::SetPlayEvent(DWORD dwPosition, HANDLE hEvent)
{
HRESULT hr;
int     nMark;
int     j;
DWORD   dwSegOffset;

	hr = S_OK;

	if (!m_fStreaming ||
	    WaitForSingleObject(m_hEventMutex, INFINITE) == WAIT_OBJECT_0)
	{
		hr = UpdatePlayEvent(dwPosition, hEvent, m_pUserWaveEvents);

		if (SUCCEEDED(hr))
		{
			if (!m_fStreaming || dwPosition == (DWORD) A3DSOURCE_WAVEEVENT_STOP)
			{
				RebuildNotifyPositions(FALSE);
			}
			else
			{
				dwSegOffset = 0;

				for (nMark = 2; nMark >= 0; nMark--)
				{
					if (dwPosition >= m_aPlayEventMark[3][1] &&
					    m_aPlayEventMark[3][0] >= m_aPlayEventMark[nMark][0])
					{
						nMark = 3;
						break;
					}

					if (dwPosition >= m_aPlayEventMark[nMark][1])
						break;
				}

				if (nMark >= 0)
				{
					for (j = 1; j <= 3; j++)
					{
						if (m_aPlayEventMark[nMark][0] < m_dwStreamSegmentSize * j)
						{
							dwSegOffset = m_dwStreamSegmentSize * j -
								      m_aPlayEventMark[nMark][0];
							break;
						}
					}

					if (m_dwFormat == (A3DSOURCE_FORMAT_STREAMING |
							  A3DSOURCE_FORMAT_WAVE))
					{
						MapWaveEvents(nMark, dwSegOffset, FALSE);
					}
					else if (m_dwFormat == (A3DSOURCE_FORMAT_STREAMING |
							       A3DSOURCE_FORMAT_MP3))
					{
						MapMp3Events(nMark,
							(DWORD) ((double) dwSegOffset *
								 (double) (int) m_dwStreamSize /
								 (double) (int)
								 ((LPA3DMP3STATE) m_pMp3Decode)->dwDecodedSize),
							FALSE);
					}
					else if (m_dwFormat == (A3DSOURCE_FORMAT_STREAMING |
							       A3DSOURCE_FORMAT_AC3) ||
						 m_dwFormat == A3DSOURCE_FORMAT_AC3)
					{
						if (((LPA3DAC3STATE) m_pAc3Decode)->fSegmentPositionScale <=
						    0.0f)
							MapAc3Events(nMark,
								(DWORD) ((double) dwSegOffset /
									 (double) ((LPA3DAC3STATE)
									 m_pAc3Decode)->fDefaultPositionScale),
								FALSE);
						else
							MapAc3Events(nMark,
								(DWORD) ((double) dwSegOffset /
									 (double) ((LPA3DAC3STATE)
									 m_pAc3Decode)->fSegmentPositionScale),
								FALSE);
					}
					else
					{
						DBGSTR("CA3dSource::SetPlayEvent - Audio events not supported by this source format.\n");
					}
				}
			}
		}
	}

	if (m_fStreaming)
		ReleaseMutex(m_hEventMutex);

	return (hr);
}

/* =============================================================
// ClearPlayEvents()
// (RE) rtl:0x10013EA0; dbg:0x1002D310
//
// Release user play events and clear the DirectSound notification set.
//
// Returns:
//   S_OK
//   A3DERROR_NO_WAVE_DATA               without a buffer
//   A3DERROR_FAILED_QUERY_DIRECTSOUNDNOTIFY
//                                       or A3DERROR_DIRECTSOUNDNOTIFY_FAILED
// =============================================================*/

STDMETHODIMP
CA3dSource::ClearPlayEvents()
{
IDirectSoundNotify *pNotify;
A3DPLAYEVENT       *pEvent;
POSITION            pos;
HRESULT             hr;

	if (!m_fStreaming ||
	    WaitForSingleObject(m_hEventMutex, INFINITE) == WAIT_OBJECT_0)
	{
		pos = m_pUserWaveEvents->GetHeadPosition();

		while (!m_pUserWaveEvents->IsEnd(pos))
		{
			pEvent = m_pUserWaveEvents->GetNext(pos);
			operator delete(pEvent);
		}

		m_pUserWaveEvents->RemoveAll();
	}

	if (m_fStreaming)
		ReleaseMutex(m_hEventMutex);

	if (m_pNotifyPositions)
	{
		operator delete(m_pNotifyPositions);
		m_pNotifyPositions = NULL;
	}

	if (!m_pBuffer)
		return (A3DERROR_NO_WAVE_DATA);

	hr = m_pBuffer->QueryInterface(IID_IDirectSoundNotify, (void **) &pNotify);

	if (FAILED(hr))
		return (A3DERROR_FAILED_QUERY_DIRECTSOUNDNOTIFY);

	hr = pNotify->SetNotificationPositions(0, NULL);

	if (FAILED(hr))
	{
		if (pNotify)
		{
			pNotify->Release();
			pNotify = NULL;
		}

		return (A3DERROR_DIRECTSOUNDNOTIFY_FAILED);
	}

	if (pNotify)
	{
		pNotify->Release();
		pNotify = NULL;
	}

	return (S_OK);
}

/* =============================================================
// SetTransformMode()
// (RE) rtl:0x10013FD0; dbg:0x1002D520
//
// Store the source transform mode.
//
// Returns:
//   S_OK
//   E_INVALIDARG  modes other than 0 or 1
// =============================================================*/

STDMETHODIMP
CA3dSource::SetTransformMode(DWORD dwMode)
{
	if (dwMode >= 2)
		return (E_INVALIDARG);

	m_dwTransformMode = dwMode;

	return (S_OK);
}

/* =============================================================
// GetTransformMode()
// (RE) rtl:0x10014000; dbg:0x1002D560
//
// Read the source transform mode.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dSource::GetTransformMode(LPDWORD pdwMode)
{
	if (!pdwMode)
		return (E_POINTER);

	*pdwMode = m_dwTransformMode;

	return (S_OK);
}

/* =============================================================
// SetReflectionDelayScale()
// (RE) rtl:0x100138B0; dbg:0x1002AEF0
//
// Store the scale applied to reflection delays.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a negative scale
// =============================================================*/

STDMETHODIMP
CA3dSource::SetReflectionDelayScale(A3DVAL fScale)
{
	if (fScale < 0.0f)
		return (E_INVALIDARG);

	m_fReflectionDelayScale = fScale;

	return (S_OK);
}

/* =============================================================
// GetReflectionDelayScale()
// (RE) rtl:0x10013940; dbg:0x1002AFD0
//
// Read the reflection delay scale.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dSource::GetReflectionDelayScale(LPA3DVAL pfScale)
{
	if (!pfScale)
		return (E_POINTER);

	*pfScale = m_fReflectionDelayScale;

	return (S_OK);
}

/* =============================================================
// SetReflectionGainScale()
// (RE) rtl:0x10013910; dbg:0x1002AF80
//
// Store the scale applied to reflection gains.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a negative scale
// =============================================================*/

STDMETHODIMP
CA3dSource::SetReflectionGainScale(A3DVAL fScale)
{
	if (fScale < 0.0f)
		return (E_INVALIDARG);

	m_fReflectionGainScale = fScale;

	return (S_OK);
}

/* =============================================================
// GetReflectionGainScale()
// (RE) rtl:0x100138E0; dbg:0x1002AF40
//
// Read the reflection gain scale.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dSource::GetReflectionGainScale(LPA3DVAL pfScale)
{
	if (!pfScale)
		return (E_POINTER);

	*pfScale = m_fReflectionGainScale;

	return (S_OK);
}

/* =============================================================
// SetVolumetricBounds()
// (RE) rtl:0x10014180; dbg:0x1002D7C0
//
// Store nonnegative extents and derive volumetric enable/area from all
// arguments.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a negative extent, retaining the other updates
// =============================================================*/

STDMETHODIMP
CA3dSource::SetVolumetricBounds(A3DVAL x, A3DVAL y, A3DVAL z)
{
HRESULT hr;

	hr = S_OK;

	if (x < 0.0f)
		hr = E_INVALIDARG;
	else
		m_avVolumetricBounds[0] = x;

	if (y < 0.0f)
		hr = E_INVALIDARG;
	else
		m_avVolumetricBounds[1] = y;

	if (z < 0.0f)
		hr = E_INVALIDARG;
	else
		m_avVolumetricBounds[2] = z;

	if (x == 0.0f && y == 0.0f && z == 0.0f)
	{
		m_dwVolumetricEnable = 0;
	}
	else
	{
		m_dwVolumetricEnable = 1;
		((A3DVAL *) m_adwVolumetricHit)[1] = (x * y + x * z + y * z) / 3.0f;
	}

	return (hr);
}

/* =============================================================
// GetVolumetricBounds()
// (RE) rtl:0x10014260; dbg:0x1002D900
//
// Write each volumetric extent whose output pointer is supplied.
//
// Returns:
//   S_OK
//   E_POINTER  if any output is null
// =============================================================*/

STDMETHODIMP
CA3dSource::GetVolumetricBounds(LPA3DVAL px, LPA3DVAL py, LPA3DVAL pz)
{
	if (px)
		*px = m_avVolumetricBounds[0];

	if (py)
		*py = m_avVolumetricBounds[1];

	if (pz)
		*pz = m_avVolumetricBounds[2];

	if (px && py && pz)
		return (S_OK);

	return (E_POINTER);
}

/* =============================================================
// SetVolumetricDamping()
// (RE) rtl:0x10014030; dbg:0x1002D5A0
//
// Store valid damping fields and the mono-inside flag, retaining rejected
// fields.
//
// Returns:
//   S_OK
//   E_POINTER     a null structure
//   E_INVALIDARG  wrong size, weights outside 0..1 or test-point count outside
//                 0..6
// =============================================================*/

STDMETHODIMP
CA3dSource::SetVolumetricDamping(A3DVOLSRCDAMPINFO *pDampInfo)
{
HRESULT hr;

	if (!pDampInfo)
		return (E_POINTER);

	if (pDampInfo->dwSize != sizeof(A3DVOLSRCDAMPINFO))
		return (E_INVALIDARG);

	hr = S_OK;

	if (pDampInfo->fAzimuthPan < 0.0f || pDampInfo->fAzimuthPan > 1.0f)
		hr = E_INVALIDARG;
	else
		m_VolDampInfo.fAzimuthPan = pDampInfo->fAzimuthPan;

	if (pDampInfo->fSizeDampMin < 0.0f || pDampInfo->fSizeDampMin > 1.0f)
		hr = E_INVALIDARG;
	else
		m_VolDampInfo.fSizeDampMin = pDampInfo->fSizeDampMin;

	if (pDampInfo->fDampWeighting < 0.0f || pDampInfo->fDampWeighting > 1.0f)
		hr = E_INVALIDARG;
	else
		m_VolDampInfo.fDampWeighting = pDampInfo->fDampWeighting;

	if ((DWORD) pDampInfo->nTestPointsMax > A3D_VOLUME_MAX_TEST_PAIRS)
		hr = E_INVALIDARG;
	else
		m_VolDampInfo.nTestPointsMax = pDampInfo->nTestPointsMax;

	m_VolDampInfo.bMonoInside = pDampInfo->bMonoInside;

	return (hr);
}

/* =============================================================
// GetVolumetricDamping()
// (RE) rtl:0x10014120; dbg:0x1002D720
//
// Copy stored volumetric damping, limited by the caller and stored sizes.
//
// Returns:
//   S_OK
//   E_POINTER     a null structure
//   E_INVALIDARG  wrong caller size
// =============================================================*/

STDMETHODIMP
CA3dSource::GetVolumetricDamping(A3DVOLSRCDAMPINFO *pDampInfo)
{
DWORD cbCopy;

	if (!pDampInfo)
		return (E_POINTER);

	if (pDampInfo->dwSize != sizeof(A3DVOLSRCDAMPINFO))
		return (E_INVALIDARG);

	if (pDampInfo->dwSize >= m_VolDampInfo.dwSize)
		cbCopy = m_VolDampInfo.dwSize;
	else
		cbCopy = pDampInfo->dwSize;

	memcpy(pDampInfo, &m_VolDampInfo, cbCopy);

	return (S_OK);
}

/* =============================================================
// SetReverbMix()
// (RE) rtl:0x100142B0; dbg:0x1002D990
//
// Store the reverb mix and direct high-frequency gain and mark changes for
// submission.
//
// Returns:
//   S_OK
//   A3DERROR_FEATURE_NOT_SUPPORTED  when the API reverb flag is clear
//   E_INVALIDARG                    mix outside 0..1 except -1
// =============================================================*/

STDMETHODIMP
CA3dSource::SetReverbMix(A3DVAL fMix, A3DVAL fDirectHF)
{
HRESULT hr;

	hr = S_OK;

	if (!m_pApi->m_dwReverbSearched)
		return (A3DERROR_FEATURE_NOT_SUPPORTED);

	if ((fMix >= 0.0f && fMix <= 1.0f) || fMix == A3D_REVERB_MIX_DEFAULT)
	{
		if (m_fReverbMix != fMix)
		{
			m_fReverbMix       = fMix;
			m_dwReverbMixDirty = 1;
		}
	}
	else
	{
		hr = E_INVALIDARG;
	}

	/* The original impossible test accepts every fDirectHF value. */
	if (fDirectHF < 0.0f && fDirectHF > 1.0f)
	{
		hr = E_INVALIDARG;
	}
	else if (m_fReverbDirectHF != fDirectHF)
	{
		m_fReverbDirectHF  = fDirectHF;
		m_dwReverbMixDirty = 1;
	}

	if (!m_pReverbPropSet)
		QueryInterface(IID_IA3dPropertySet, (void **) &m_pReverbPropSet);

	return (hr);
}

/* =============================================================
// GetReverbMix()
// (RE) rtl:0x100143A0; dbg:0x1002DAF0
//
// Write each supplied reverb mix output when the API reverb flag is set.
//
// Returns:
//   S_OK
//   A3DERROR_FEATURE_NOT_SUPPORTED  when the flag is clear
//   E_POINTER                       if either output is null
// =============================================================*/

STDMETHODIMP
CA3dSource::GetReverbMix(A3DVAL *pfMix, A3DVAL *pfDirectHF)
{
HRESULT hr;

	hr = S_OK;

	if (!m_pApi->m_dwReverbSearched)
		return (A3DERROR_FEATURE_NOT_SUPPORTED);

	if (pfMix)
		*pfMix = m_fReverbMix;
	else
		hr = E_POINTER;

	if (!pfDirectHF)
		return (E_POINTER);

	*pfDirectHF = m_fReverbDirectHF;

	return (hr);
}

/* =============================================================
// NewManualReflection()
// (RE) rtl:0x100146F0; dbg:0x1002E120
//
// Create and retain a manual reflection, returning a caller reference.
//
// Returns:
//   S_OK
//   E_POINTER                         a null output
//   E_OUTOFMEMORY                     on allocation failure
//   A3DERROR_SOURCE_IN_NATIVE_MODE    in native mode
//   A3DERROR_REFLECTIONS_NOT_ENABLED  if first-order reflections are
//                                     unavailable
// =============================================================*/

STDMETHODIMP
CA3dSource::NewManualReflection(LPA3DREFLECTION *ppReflection)
{
CA3dReflection *pReflection;
HRESULT         hr;

	if (!ppReflection)
		return (E_POINTER);

	hr = S_OK;
	*ppReflection = NULL;

	try
	{
		if (m_dwRenderMode == A3DSOURCE_RENDERMODE_NATIVE)
		{
			hr = A3DERROR_SOURCE_IN_NATIVE_MODE;
			throw "Source in native mode. Can't have reflections.";
		}

		if (!m_pApi->IsFeatureAvailable(A3D_1ST_REFLECTIONS))
		{
			hr = A3DERROR_REFLECTIONS_NOT_ENABLED;
			throw "Reflections were not requested or not "
			      "supported.\n";
		}

		if (!m_pManualReflections)
		{
			m_pManualReflections =
				new CA3dStdList<CA3dReflection *>;
			if (!m_pManualReflections)
			{
				hr = E_OUTOFMEMORY;
				throw "Couldn't allocate manual reflection "
				      "array.";
			}
		}

		pReflection = new CA3dReflection(this);
		if (!pReflection)
		{
			hr = E_OUTOFMEMORY;
			throw "Couldn't allocate manual reflection.";
		}

		m_pManualReflections->push_back(pReflection);
		pReflection->AddRef();

		*ppReflection = pReflection;
		m_fManualReflections = 1;
	}
	catch (const char *)
	{

	}

	return (hr);
}

/* =============================================================
// FreeManualReflections()
// (RE) rtl:0x100148C0; dbg:0x1002E400
//
// Delete all manual reflections and their list.
// The original deletes objects despite outstanding caller references.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dSource::FreeManualReflections()
{
CA3dStdList<CA3dReflection *>::iterator it;

CA3dReflection *pReflection;

	if (m_pManualReflections)
	{
		for (it = m_pManualReflections->begin();
		     it != m_pManualReflections->end();
		     ++it)
		{
			pReflection = *it;
			if (pReflection)
				delete pReflection;
		}

		delete m_pManualReflections;
		m_pManualReflections = NULL;
	}

	return (S_OK);
}

/* =============================================================
// RemoveManualReflection()
// (RE) rtl:0x10014A20; dbg:0x1002E5E0; thunk dbg:0x100022E8
//
// Unlink the reflection without releasing it.
// The original dereferences the list without a null check.
// =============================================================*/

void
CA3dSource::RemoveManualReflection(CA3dReflection *pReflection)
{
	m_pManualReflections->remove(pReflection);
}

/* =============================================================
// GetNumManualReflections()
// (RE) rtl:0x100149E0; dbg:0x1002E580
//
// Read the number of manual reflections, using zero if no list exists.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dSource::GetNumManualReflections(int *pnReflections)
{
	if (!pnReflections)
		return (E_POINTER);

	if (m_pManualReflections)
		*pnReflections = m_pManualReflections->size();
	else
		*pnReflections = 0;

	return (S_OK);
}

/* =============================================================
// GetCaps()
// (RE) rtl:0x100150E0; dbg:0x1002F090
//
// Refresh WAVE capability fields and copy the source capability block.
// The original writes the 20-byte wave size into the 44-byte outer block
// and checks neither the output pointer nor the WAVE format pointer.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dSource::GetCaps(LPA3DCAPS_SOURCE lpSourceCaps)
{
	if (m_dwFormat & A3DSOURCE_FORMAT_WAVE)
	{
		m_SourceCaps.dwSize = sizeof(A3DSOURCE_WAVEFORMAT);

		m_SourceCaps.data.waveFormat.nChannels =
			m_pwfxFormat->nChannels;
		m_SourceCaps.data.waveFormat.nSamplesPerSec =
			m_pwfxFormat->nSamplesPerSec;
		m_SourceCaps.data.waveFormat.nAvgBytesPerSec =
			m_pwfxFormat->nAvgBytesPerSec;
		m_SourceCaps.data.waveFormat.nBlockAlign =
			m_pwfxFormat->nBlockAlign;
		m_SourceCaps.data.waveFormat.wBitsPerSample =
			m_pwfxFormat->wBitsPerSample;
	}

	memcpy(lpSourceCaps, &m_SourceCaps, sizeof(A3DCAPS_SOURCE));

	return (S_OK);
}
