/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * dal_a2d.cpp
 *
 * Implements DAL_A2D, the software mixing backend for A3D's device
 * abstraction layer (DAL). It exposes DirectSound and A3D interfaces,
 * creates A2DBuffer voices for individual sounds, and sends their combined
 * audio to a looping DirectSound buffer as 16-bit stereo PCM at 22050 Hz.
 *
 * This file manages mixer initialization, output buffer creation, voice
 * creation and duplication, capability reporting, and shutdown. Its worker
 * thread monitors queued output, mixes playing voices, removes released
 * voices, and writes more audio as the output buffer is consumed.
 *
 * Per-voice playback state and controls are implemented in a2dbuffer.cpp;
 * sample mixing and output conversion use the routines declared in
 * softmix.h. This file coordinates those operations and owns the shared
 * work buffers and HRTF manager. Optional reflection and reverb processing
 * uses separate reverb and reflection emulation options. Both are project
 * additions.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "dal_a2d.h"
#include "a2dbuffer.h"
#include "d2dbuffer.h"
#include "dalinfo.h"
#include "hrtfmgr.h"
#include "Plex.h"
#include "softmix.h"
#include "A3dReverbEmu.h"	/* NOT PART OF THE ORIGINAL; see the header */
#include "A3dReflectionEmu.h"	/* NOT PART OF THE ORIGINAL; see the header */

/* Reported DAL capability version  */
#define A3D_A2D_CAPS_VERSION 0x0120

/* (RE) Private startup interface IID: dbg:0x101409A8. */

static const GUID IID_IA3dPrvA2D =
	{ 0x0a991a7d, 0xc7a7, 0x11d2,
	  { 0xb9, 0x68, 0x00, 0x10, 0x5a, 0x20, 0x24, 0x9d } };

/* =============================================================
// ReadA3dRegistryDword()
// (RE) rtl:0x10027770; dbg:0x10067b50
//
// Read a four-byte value from HKLM\Software\Aureal\A3D.
//
// Returns:
//   S_OK    when read
//   E_FAIL  otherwise
// =============================================================*/

static HRESULT
ReadA3dRegistryDword(LPCSTR lpValueName, LPBYTE lpData)
{
HKEY	hKey;
DWORD	cbData;
HRESULT	hr;

	hr = E_FAIL;

	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Aureal\\A3D", 0,
			  KEY_READ, &hKey) == ERROR_SUCCESS)
	{
		cbData = 4;

		if (RegQueryValueExA(hKey, lpValueName, 0, 0, lpData, &cbData) ==
		    ERROR_SUCCESS)
			hr = S_OK;

		if (hKey)
			RegCloseKey(hKey);
	}

	return (hr);
}

static HRESULT	CreatePrimaryBuffer(const DSBUFFERDESC1 *pDesc,
				    LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
				    LPUNKNOWN pUnkOuter);

/* =============================================================
// DAL_A2D::DAL_A2D()
// (RE) dbg:0x10048D00
//
// Construct an empty software mixer.
// =============================================================*/

DAL_A2D::DAL_A2D(void)
{
	m_cRef = 1;

	m_dwThreadId             = 0;
	m_hThread                = (HANDLE) -1;
	m_hWake                  = (HANDLE) -1;
	m_hEventCallbackInactive = (HANDLE) -1;

	m_dwWakeTimeoutMs       = 0;
	m_cbFillThreshold       = 0;
	m_Unknown_0x2C          = 0;
	m_cbBuffer              = 0;
	m_dwSampleRate          = 0;
	m_dwChannels            = 0;
	m_dwBitsPerSample       = 0;
	m_cVoices               = 0;
	m_dwWritePos            = 0;
	m_cbWork                = 0;
	m_pStaging              = NULL;
	m_pMix                  = NULL;

	m_pDirectSound  = NULL;
	m_pDSBuffer     = NULL;
	m_pHrtfMgr      = NULL;
	m_fThreadRun    = 0;
	m_fInitialized  = 0;
}

/* =============================================================
// DAL_A2D::~DAL_A2D()
// (RE) rtl:0x1001D990; dbg:0x10048F20
//
// Stop the mixer and release its voices and resources.
// The original leaks the thread handle.
// =============================================================*/

DAL_A2D::~DAL_A2D(void)
{
A2DBuffer	*pVoice;

	if (m_hThread != (HANDLE) -1)
	{
		m_fThreadRun = 0;

		VERIFY(WaitForSingleObject(m_hEventCallbackInactive, 5000) != WAIT_TIMEOUT);
	}

	if (m_hEventCallbackInactive != (HANDLE) -1)
	{
		CloseHandle(m_hEventCallbackInactive);
		m_hEventCallbackInactive = (HANDLE) -1;
	}

	if (m_hWake != (HANDLE) -1)
	{
		CloseHandle(m_hWake);
		m_hWake = (HANDLE) -1;
	}

	if (m_pMix != NULL)
	{
		delete m_pMix;
		m_pMix = NULL;
	}

	if (m_pStaging != NULL)
	{
		delete m_pStaging;
		m_pStaging = NULL;
	}

	if (m_pDSBuffer != NULL)
	{
		m_pDSBuffer->Stop();

		if (m_pDSBuffer != NULL)
		{
			m_pDSBuffer->Release();
			m_pDSBuffer = NULL;
		}
	}

	if (m_pHrtfMgr != NULL)
	{
		delete m_pHrtfMgr;
		m_pHrtfMgr = NULL;
	}

	while ((pVoice = (A2DBuffer *) m_VoiceList.RemoveHead()) != NULL)
		delete pVoice;

	if (m_pDirectSound != NULL)
	{
		m_pDirectSound->Release();
		m_pDirectSound = NULL;
	}
}

/* =============================================================
// DAL_A2D::QueryInterface()
// (RE) rtl:0x1001db80; dbg:0x10049260
//
// Return an AddRef'd interface.
//
// Returns:
//   S_OK
//   E_POINTER      a null output
//   E_NOINTERFACE  an unknown IID
// =============================================================*/

STDMETHODIMP
DAL_A2D::QueryInterface(REFIID riid, void **ppv)
{
	if (ppv == NULL)
	{
		DBGSTR("DAL_A2D::QueryInterface() - ppv is NULL.\n");

		return (E_POINTER);
	}

	if (IsEqualIID(riid, IID_IUnknown) ||
	    IsEqualIID(riid, IID_IDirectSound))
	{
		*ppv = (IDirectSound *) this;
	}
	else if (IsEqualIID(riid, IID_IA3dDal))
	{
		*ppv = (IA3dDal *) this;
	}
	else if (IsEqualIID(riid, IID_IA3d) ||
		 IsEqualIID(riid, IID_IA3d2))
	{
		*ppv = (IA3d2 *) this;
	}
	else if (IsEqualIID(riid, IID_IA3dPrvA2D))
	{
		*ppv = (IA3dPrvA2D *) this;
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
// DAL_A2D::AddRef()
// (RE) rtl:0x1001dca0; dbg:0x10049400
//
// Take a reference.
//
// Returns: Reference count read after the interlocked increment.
// =============================================================*/

STDMETHODIMP_(ULONG)
DAL_A2D::AddRef(void)
{
	InterlockedIncrement(&m_cRef);

	return ((ULONG) m_cRef);
}

/* =============================================================
// DAL_A2D::Release()
// (RE) rtl:0x1001dcc0; dbg:0x10049430
//
// Release a reference and delete the mixer at zero.
//
// Returns: Reference count read after the interlocked decrement; zero after
//          deletion.
// =============================================================*/

STDMETHODIMP_(ULONG)
DAL_A2D::Release(void)
{
	InterlockedDecrement(&m_cRef);

	if (m_cRef != 0)
		return ((ULONG) m_cRef);

	delete this;

	return (0);
}

/* =============================================================
// DAL_A2D::CreateSoundBuffer()
// (RE) rtl:0x1001dd00; dbg:0x100494f0
//
// Create a software secondary buffer after startup; reject primary buffers.
//
// Returns: DSERR_UNINITIALIZED before startup; E_INVALIDARG for aggregation;
//          otherwise the selected buffer creation result.
// =============================================================*/

STDMETHODIMP
DAL_A2D::CreateSoundBuffer(LPCDSBUFFERDESC lpcDSBufferDesc,
			   LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
			   LPUNKNOWN pUnkOuter)
{
	ASSERT((lpcDSBufferDesc != 0 &&
	       !IsBadReadPtr(lpcDSBufferDesc, sizeof(DSBUFFERDESC))));
	ASSERT((lplpDirectSoundBuffer != 0 &&
	       !IsBadReadPtr(lplpDirectSoundBuffer, sizeof(LPDIRECTSOUNDBUFFER))));

	if (!m_fInitialized)
	{
		DBGSTR("DAL_A2D::CreateSoundBuffer() - Called before initialized\n");

		return (DSERR_UNINITIALIZED);
	}

	if (pUnkOuter != NULL)
	{
		DBGSTR("DAL_A2D::CreateSoundBuffer() - Aggregation not supported.\n");

		return (E_INVALIDARG);
	}

	if (lpcDSBufferDesc->dwFlags & DSBCAPS_PRIMARYBUFFER)
		return (CreatePrimaryBuffer((const DSBUFFERDESC1 *) lpcDSBufferDesc,
					    lplpDirectSoundBuffer, pUnkOuter));

	return (CreateSecondaryBuffer((const DSBUFFERDESC1 *) lpcDSBufferDesc,
				      NULL, lplpDirectSoundBuffer, pUnkOuter));
}

/* =============================================================
// DAL_A2D::GetCaps()
// (RE) rtl:0x1001dd50; dbg:0x10049650
//
// Report software formats and voice capacity, with free counts based on
// currently playing voices.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
DAL_A2D::GetCaps(LPDSCAPS lpDirectSoundCaps)
{
int	cActive;

	if (lpDirectSoundCaps == NULL)
	{
		DBGSTR("DAL_A2D::GetCaps() - lpDirectSoundCaps is NULL.\n");

		return (E_POINTER);
	}

	ASSERT((lpDirectSoundCaps != 0 &&
	       !IsBadReadPtr(lpDirectSoundCaps, sizeof(DSCAPS))));

	cActive = CountActive();

	lpDirectSoundCaps->dwSize  = sizeof(DSCAPS);
	lpDirectSoundCaps->dwFlags = DSCAPS_PRIMARYMONO | DSCAPS_PRIMARYSTEREO |
				     DSCAPS_PRIMARY8BIT | DSCAPS_PRIMARY16BIT |
				     DSCAPS_CONTINUOUSRATE |
				     DSCAPS_SECONDARYMONO | DSCAPS_SECONDARYSTEREO |
				     DSCAPS_SECONDARY8BIT | DSCAPS_SECONDARY16BIT;

	lpDirectSoundCaps->dwMinSecondarySampleRate = A3D_MIN_SECONDARY_RATE;
	lpDirectSoundCaps->dwMaxSecondarySampleRate = A3D_MAX_SECONDARY_RATE;
	lpDirectSoundCaps->dwPrimaryBuffers         = 0;

	lpDirectSoundCaps->dwMaxHwMixingAllBuffers       = m_cVoices;
	lpDirectSoundCaps->dwMaxHwMixingStaticBuffers    = m_cVoices;
	lpDirectSoundCaps->dwMaxHwMixingStreamingBuffers = m_cVoices;

	lpDirectSoundCaps->dwFreeHwMixingAllBuffers       = m_cVoices - cActive;
	lpDirectSoundCaps->dwFreeHwMixingStaticBuffers    = m_cVoices - cActive;
	lpDirectSoundCaps->dwFreeHwMixingStreamingBuffers = m_cVoices - cActive;

	lpDirectSoundCaps->dwMaxHw3DAllBuffers       = m_cVoices;
	lpDirectSoundCaps->dwMaxHw3DStaticBuffers    = m_cVoices;
	lpDirectSoundCaps->dwMaxHw3DStreamingBuffers = m_cVoices;

	lpDirectSoundCaps->dwFreeHw3DAllBuffers       = m_cVoices - cActive;
	lpDirectSoundCaps->dwFreeHw3DStaticBuffers    = m_cVoices - cActive;
	lpDirectSoundCaps->dwFreeHw3DStreamingBuffers = m_cVoices - cActive;

	lpDirectSoundCaps->dwTotalHwMemBytes             = 0;
	lpDirectSoundCaps->dwFreeHwMemBytes              = 0;
	lpDirectSoundCaps->dwMaxContigFreeHwMemBytes     = 0;
	lpDirectSoundCaps->dwUnlockTransferRateHwBuffers = 0;
	lpDirectSoundCaps->dwPlayCpuOverheadSwBuffers    = 0;

	return (S_OK);
}

/* =============================================================
// DAL_A2D::DuplicateSoundBuffer()
// (RE) rtl:0x1001de20; dbg:0x10049840
//
// Create and list a voice sharing the original waveform. The original
// omits the list's AddRef and the voice-cap check on this path.
//
// Returns: S_OK; E_OUTOFMEMORY on allocation failure; the InitDuplicate failure
//          otherwise.
// =============================================================*/

STDMETHODIMP
DAL_A2D::DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER lpDirectSoundBuffer,
			      LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer)
{
A2DBuffer      *pVoice;
HRESULT         hr;

	ASSERT((lpDirectSoundBuffer != 0 &&
	       !IsBadReadPtr(lpDirectSoundBuffer, sizeof(IDirectSoundBuffer))));
	ASSERT((lplpDirectSoundBuffer != 0 &&
	       !IsBadReadPtr(lplpDirectSoundBuffer, sizeof(LPDIRECTSOUNDBUFFER))));

	pVoice = new A2DBuffer;

	if (!pVoice)
	{
		DBGSTR("DAL_A2D::DuplicateSoundBuffer() - Failed to allocated memory for new buffer.\n");

		return (E_OUTOFMEMORY);
	}

	hr = pVoice->InitDuplicate((A2DBuffer *) lpDirectSoundBuffer);

	if (FAILED(hr))
	{
		DBGSTR("DAL_A2D::DuplicateSoundBuffer() - Creation of buffer failed.\n");

		delete pVoice;

		return (hr);
	}

	m_VoiceList.AddTail(pVoice);

	*lplpDirectSoundBuffer = (LPDIRECTSOUNDBUFFER) pVoice;

	return (S_OK);
}

/* =============================================================
// DAL_A2D::SetCooperativeLevel()
// (RE) rtl:0x1001df70; dbg:0x10049a60
//
// Forward the cooperative level to the wrapped DirectSound.
//
// Returns:
//   DirectSound SetCooperativeLevel result
//   E_FAIL                              without DirectSound
// =============================================================*/

STDMETHODIMP
DAL_A2D::SetCooperativeLevel(HWND hWnd, DWORD dwLevel)
{
	if (m_pDirectSound != NULL)
	{
		ASSERT((m_pDirectSound != 0 &&
		       !IsBadReadPtr(m_pDirectSound, sizeof(IDirectSound))));

		return (m_pDirectSound->SetCooperativeLevel(hWnd, dwLevel));
	}

	DBGSTR("DAL_A2D::SetCooperativeLevel() - You must call Init first.\n");

	return (E_FAIL);
}

/* =============================================================
// DAL_A2D::Compact()
// (RE) rtl:0x1001eec0; dbg:0x10049b30
//
// Reject compaction.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_A2D::Compact(void)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_A2D::GetSpeakerConfig()
// (RE) rtl:0x10020b90; dbg:0x10049b50
//
// Reject speaker-configuration queries.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_A2D::GetSpeakerConfig(LPDWORD pdwConfig)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_A2D::SetSpeakerConfig()
// (RE) rtl:0x10020b90; dbg:0x10049b70
//
// Reject speaker-configuration changes.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_A2D::SetSpeakerConfig(DWORD dwConfig)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_A2D::Initialize()
// (RE) rtl:0x10020b90; dbg:0x10049b90
//
// Reject DirectSound initialization.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_A2D::Initialize(LPCGUID pGuid)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_A2D::InitializeEx()
// (RE) rtl:0x1001dfa0; dbg:0x10049bb0
//
// Clear the available-feature output and initialize the software mixer.
//
// Returns: Init result.
// =============================================================*/

STDMETHODIMP
DAL_A2D::InitializeEx(LPGUID pGuidDevice, DWORD dwFlags, DWORD dwReserved,
		      LPDWORD lpdwFeaturesEnabled)
{
	ASSERT((lpdwFeaturesEnabled != 0 &&
	       !IsBadReadPtr(lpdwFeaturesEnabled, sizeof(DWORD))));

	*lpdwFeaturesEnabled = 0;

	return (Init());
}

/* =============================================================
// DAL_A2D::CreateSoundBufferEx()
// (RE) rtl:0x1001eef0; dbg:0x10049c40
//
// Reject extended buffer creation.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_A2D::CreateSoundBufferEx(const DSBUFFERDESC1 *lpcDSBufferDesc,
			     LPBYTE lpbWave,
			     LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
			     LPUNKNOWN pUnkOuter)
{
	ASSERT((lpcDSBufferDesc != 0 &&
	       !IsBadReadPtr(lpcDSBufferDesc, sizeof(DSBUFFERDESC))));
	ASSERT((lpbWave != 0 && !IsBadReadPtr(lpbWave, sizeof(BYTE))));
	ASSERT((lplpDirectSoundBuffer != 0 &&
	       !IsBadReadPtr(lplpDirectSoundBuffer, sizeof(LPDIRECTSOUNDBUFFER))));

	return (E_NOTIMPL);
}

/* =============================================================
// DAL_A2D::GetA3dCaps()
// (RE) rtl:0x1001dfc0; dbg:0x10049d60
//
// Clear a 564-byte capability block and fill software mixer capabilities;
// ignore lpdwSize. Writes at 0x4C..0x68 require undeclared A3DDALCAPS564 fields.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
DAL_A2D::GetA3dCaps(LPA3DDALCAPS lpA3dCaps, LPDWORD lpdwSize)
{
A3DDALCAPS564	*pCaps;

	pCaps = (A3DDALCAPS564 *) lpA3dCaps;

	ZeroMemory(pCaps, sizeof(A3DDALCAPS564));

	pCaps->caps.wDeviceType         = A3DCAPS_DEVICE_TYPE_A2D;
	pCaps->caps.wVersion            = A3D_A2D_CAPS_VERSION;
	pCaps->caps.dwCalcFactor        = 32767;
	pCaps->caps.wChannelCount       = 4;
	pCaps->caps.wChannels           = 2;
	pCaps->caps.wRefMask            = 7;
	pCaps->caps.dwMaxSampleRate     = A3D_MIX_SAMPLE_RATE;
	pCaps->caps.wMaxBuffers         = (WORD) m_dwBitsPerSample;
	pCaps->caps.dwSamplesMask       = 1;
	pCaps->caps.dwReserved1C        = 12;
	pCaps->caps.dwFeatureFlags      = 0x00040007;
	pCaps->caps.dwUnknown_0x44      = 256;

#if defined(A3D_FIXES)
	if (A3dGetConfig().bSoftwareReflections)
	{
		/* NOT PART OF THE ORIGINAL: capacity of the software reflection taps. */
		pCaps->caps.dwMaxReflections    = A3D_MIX_VOICES * A3D_MAX_SOURCE_REFLECTIONS;
		pCaps->caps.dwMaxSrcReflections = A3D_MAX_SOURCE_REFLECTIONS;
	}
#endif

	return (S_OK);
}

/* =============================================================
// DAL_A2D::GetDriverInfo()
// (RE) rtl:0x1001eef0; dbg:0x10049ef0
//
// Reject driver-information queries.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_A2D::GetDriverInfo(LPHANDLE lphA3dVxd, void **lplpIDsDriver,
		       void **lplpIA3dDriver, LPDWORD lpdwCardIndex)
{
	ASSERT((lphA3dVxd != 0 && !IsBadReadPtr(lphA3dVxd, sizeof(HANDLE))));
	ASSERT((lplpIDsDriver != 0 &&
	       !IsBadReadPtr(lplpIDsDriver, sizeof(LPVOID))));
	ASSERT((lplpIA3dDriver != 0 &&
	       !IsBadReadPtr(lplpIA3dDriver, sizeof(LPVOID))));
	ASSERT((lpdwCardIndex != 0 &&
	       !IsBadReadPtr(lpdwCardIndex, sizeof(DWORD))));

	return (E_NOTIMPL);
}

/* =============================================================
// DAL_A2D::GetDS()
// (RE) rtl:0x10020b90; dbg:0x1004a060
//
// Reject DirectSound-interface queries.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_A2D::GetDS(LPDIRECTSOUND *lplpDirectSound)
{
	ASSERT((lplpDirectSound != 0 &&
	       !IsBadReadPtr(lplpDirectSound, sizeof(LPDIRECTSOUND))));

	return (E_NOTIMPL);
}

/* =============================================================
// DAL_A2D::GetDSDriverDesc()
// (RE) rtl:0x10003500; dbg:0x1004a0d0
//
// Reject driver-descriptor queries.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_A2D::GetDSDriverDesc(void *lpsDSDriverDesc, LPDWORD lpdwSize)
{
	ASSERT((lpsDSDriverDesc != 0 &&
	       !IsBadReadPtr(lpsDSDriverDesc, sizeof(DSDRIVERDESC))));
	ASSERT((lpdwSize != 0 && !IsBadReadPtr(lpdwSize, sizeof(DWORD))));

	return (E_NOTIMPL);
}

/* =============================================================
// DAL_A2D::QueryFunctionality()
// (RE) rtl:0x10003500; dbg:0x1004a1a0
//
// Reject functionality queries.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_A2D::QueryFunctionality(DWORD dwFunction, LPDWORD lpdwStatus)
{
	ASSERT((lpdwStatus != 0 && !IsBadReadPtr(lpdwStatus, sizeof(DWORD))));

	return (E_NOTIMPL);
}

/* =============================================================
// DAL_A2D::Verify()
// (RE) rtl:0x10020b60; dbg:0x1004a210
//
// Reject string verification.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_A2D::Verify(LPSTR lpcString, LPSTR *lplpcStringCrypted,
		LPSTR *lplpcCopyright)
{
	ASSERT((lpcString != 0 && !IsBadReadPtr(lpcString, sizeof(char))));
	ASSERT((lplpcStringCrypted != 0 &&
	       !IsBadReadPtr(lplpcStringCrypted, sizeof(char*))));
	ASSERT((lplpcCopyright != 0 &&
	       !IsBadReadPtr(lplpcCopyright, sizeof(char*))));

	return (E_NOTIMPL);
}

/* =============================================================
// DAL_A2D::SetOutputMode()
// (RE) rtl:0x10020b60; dbg:0x1004a330
//
// Reject output-mode changes.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_A2D::SetOutputMode(DWORD dwFrontXtalkMode, DWORD dwBackXtalkMode,
		       DWORD dwQuadMode)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_A2D::GetOutputMode()
// (RE) rtl:0x10020b60; dbg:0x1004a350
//
// Reject output-mode queries.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_A2D::GetOutputMode(LPDWORD lpdwFrontXtalkMode, LPDWORD lpdwBackXtalkMode,
		       LPDWORD lpdwQuadMode)
{
	ASSERT((lpdwFrontXtalkMode != 0 &&
	       !IsBadReadPtr(lpdwFrontXtalkMode, sizeof(DWORD))));
	ASSERT((lpdwBackXtalkMode != 0 &&
	       !IsBadReadPtr(lpdwBackXtalkMode, sizeof(DWORD))));
	ASSERT((lpdwQuadMode != 0 &&
	       !IsBadReadPtr(lpdwQuadMode, sizeof(DWORD))));

	return (E_NOTIMPL);
}

/* =============================================================
// DAL_A2D::SetResourceManagerMode()
// (RE) rtl:0x10020b70; dbg:0x1004a470
//
// Reject resource-manager mode changes.
//
// Returns:
//   E_NOTIMPL     modes 0..A3D_RESOURCE_MODE_LAST
//   E_INVALIDARG  otherwise
// =============================================================*/

STDMETHODIMP
DAL_A2D::SetResourceManagerMode(DWORD dwResourceManagerMode)
{
	if (dwResourceManagerMode <= A3D_RESOURCE_MODE_LAST)
		return (E_NOTIMPL);

	TRACE("DAL_A2D::SetResourceManagerMode() - dwResourceManagerMode is an invalid value.\n"
	      "\tValue Passed: %u.\n", dwResourceManagerMode);

	return (E_INVALIDARG);
}

/* =============================================================
// DAL_A2D::GetResourceManagerMode()
// (RE) rtl:0x10020b90; dbg:0x1004a4d0
//
// Reject resource-manager mode queries.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_A2D::GetResourceManagerMode(LPDWORD lpdwResourceManagerMode)
{
	ASSERT((lpdwResourceManagerMode != 0 &&
	       !IsBadReadPtr(lpdwResourceManagerMode, sizeof(DWORD))));

	return (E_NOTIMPL);
}

/* =============================================================
// DAL_A2D::SetHFAbsorbFactor()
// (RE) rtl:0x10020b90; dbg:0x1004a540
//
// Reject absorption-factor changes.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_A2D::SetHFAbsorbFactor(FLOAT fFactor)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_A2D::GetHFAbsorbFactor()
// (RE) rtl:0x10020b90; dbg:0x1004a560
//
// Reject absorption-factor queries.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_A2D::GetHFAbsorbFactor(FLOAT *pfFactor)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_A2D::RegisterVersion()
// (RE) rtl:0x10020b90; dbg:0x1004a580
//
// Reject version registration.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_A2D::RegisterVersion(DWORD dwVersion)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_A2D::GetSoftwareCaps()
// (RE) rtl:0x10020b90; dbg:0x1004a5a0
//
// Reject software-capability queries.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_A2D::GetSoftwareCaps(LPA3DCAPS_SOFTWARE pCaps)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_A2D::GetHardwareCaps()
// (RE) rtl:0x10020b90; dbg:0x1004a5c0
//
// Reject hardware-capability queries.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_A2D::GetHardwareCaps(LPA3DCAPS_HARDWARE pCaps)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_A2D::StartupDal()
// (RE) rtl:0x1001e070; dbg:0x1004a5e0
//
// Create the output buffer and start the mixer thread once.
//
// Returns: S_OK, including when already started; E_FAIL on startup failure.
// =============================================================*/

STDMETHODIMP
DAL_A2D::StartupDal(void)
{
	if (m_fInitialized)
		return (S_OK);

	try
	{
		A3dMakeOutputBuffer(this, m_pDirectSound, &m_pDSBuffer);

		if (!m_pDSBuffer)
			throw ("Could not output buffer");

		m_fThreadRun = 1;

		m_hThread = CreateThread(NULL, 0, MixThread, this, 0,
					 &m_dwThreadId);

		if (!m_hThread)
			throw ("Could not create thread");

		if (!SetThreadPriority(m_hThread, THREAD_PRIORITY_TIME_CRITICAL))
			throw ("Could not set thread priority");

		m_hEventCallbackInactive = CreateEvent(NULL, FALSE, FALSE, NULL);

		if (!m_hEventCallbackInactive)
			throw ("Could not create event");

		m_fInitialized = 1;
	}
	/* (RE) Catch handler: dbg:0x1004A71F; FuncInfo: dbg:0x10143838. */
	catch (const char *pszWhy)
	{
		TRACE("DAL_A2D::StartupDal() - %s.\n", pszWhy);

		if (m_pDSBuffer != NULL)
		{
			m_pDSBuffer->Stop();

			if (m_pDSBuffer != NULL)
			{
				m_pDSBuffer->Release();
				m_pDSBuffer = NULL;
			}
		}

		m_fInitialized = 0;

		return (E_FAIL);
	}

	return (S_OK);
}

/* =============================================================
// DAL_A2D::Init()
// (RE) rtl:0x1001e1d0; dbg:0x1004a850
//
// Create DirectSound, work buffers, the wake event and the HRTF manager,
// and initialize output filter state.
//
// Returns:
//   S_OK
//   E_FAIL  after cleaning up an initialization failure
// =============================================================*/

HRESULT
DAL_A2D::Init(void)
{
OSVERSIONINFO	osvi;
DWORD		dwRegBuffer;
HRESULT		hr;

	try
	{
		m_dwWakeTimeoutMs       = A3D_MIX_WAIT_MS;
		m_Unknown_0x2C          = 0;
		m_cbBuffer              = A3D_MIX_BUFFER_BYTES;
		m_dwSampleRate          = A3D_MIX_SAMPLE_RATE;
		m_dwChannels            = A3D_MIX_CHANNELS;
		m_dwBitsPerSample       = A3D_MIX_BITS;

		osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);

		if (GetVersionEx(&osvi) &&
		    osvi.dwPlatformId == VER_PLATFORM_WIN32_NT &&
		    osvi.dwMajorVersion == 4)
			m_cbBuffer = A3D_MIX_BUFFER_NT4;

		if (SUCCEEDED(ReadA3dRegistryDword("A2DBufferSize",
						   (LPBYTE) &dwRegBuffer)))
		{
			if (dwRegBuffer > A3D_MIX_BUFFER_MAX)
				m_cbBuffer = A3D_MIX_BUFFER_MAX;
			else
				m_cbBuffer = dwRegBuffer;
		}

		m_cbFillThreshold = m_cbBuffer >> 1;
		m_cVoices         = A3D_MIX_VOICES;

		hr = CoCreateInstance(CLSID_DirectSound, NULL,
				      CLSCTX_INPROC_SERVER, IID_IDirectSound,
				      (void **) &m_pDirectSound);

		if (FAILED(hr))
			throw ("Could not CoCreateInstance DirectSound");

		hr = m_pDirectSound->Initialize(NULL);

		if (FAILED(hr))
			throw ("Failed to Initialize() DirectSound");

		m_cbWork = 4 * (4 * m_cbBuffer / (m_dwBitsPerSample >> 3));

		m_pStaging = (short *) operator new(m_cbWork);

		if (!m_pStaging)
			throw ("Failed to allocate memory for Crosstalk Work Buffer");

		m_pMix = (float *) operator new(m_cbWork);

		if (!m_pMix)
			throw ("Failed to allocate memory for Non-Crosstalk Work Buffer");

		m_hWake = CreateEvent(NULL, FALSE, FALSE, NULL);

		if (!m_hWake)
			throw ("Could not Create Event");

		m_pHrtfMgr = new CHrtfMgr;

		if (!m_pHrtfMgr)
			throw ("Failed to allocate memory for the HRTF Manager");

		if (FAILED(m_pHrtfMgr->Select(A3D_MIX_SAMPLE_RATE, 1, 0)))
			throw ("Could not Initialize the HRTF Manager");

		InitFilterState(&m_FilterMode, &m_Filter, A3D_MIX_FILTER_RATE_KHZ);
	}
	/* (RE) Catch handler: dbg:0x1004AC4B; FuncInfo: dbg:0x10143898. */
	catch (const char *pszWhy)
	{
		TRACE("DAL_A2D::Init() - %s.\n", pszWhy);

		if (m_hWake != (HANDLE) -1)
		{
			CloseHandle(m_hWake);
			m_hWake = (HANDLE) -1;
		}

		if (m_hEventCallbackInactive != (HANDLE) -1)
		{
			CloseHandle(m_hEventCallbackInactive);
			m_hEventCallbackInactive = (HANDLE) -1;
		}

		if (m_pMix != NULL)
		{
			operator delete(m_pMix);
			m_pMix = NULL;
		}

		if (m_pStaging != NULL)
		{
			operator delete(m_pStaging);
			m_pStaging = NULL;
		}

		if (m_pHrtfMgr != NULL)
		{
			delete m_pHrtfMgr;
			m_pHrtfMgr = NULL;
		}

		if (m_pDirectSound != NULL)
		{
			m_pDirectSound->Release();
			m_pDirectSound = NULL;
		}

		return (E_FAIL);
	}

	return (S_OK);
}

/* =============================================================
// A3dMakeOutputBuffer()
// (RE) rtl:0x1001E4A0; dbg:0x1004aef0
//
// Create the looping PCM output buffer, start playback and set full volume.
//
// Returns: S_OK; the CreateSoundBuffer failure, clearing the output pointer.
// =============================================================*/

HRESULT
A3dMakeOutputBuffer(DAL_A2D *pDevice, IDirectSound *pDS,
		    IDirectSoundBuffer **lplpDsb)
{
WAVEFORMATEX	wfx;
DSBUFFERDESC1	desc;
HRESULT		hr;

	ZeroMemory(&wfx, sizeof(wfx));

	wfx.wFormatTag          = WAVE_FORMAT_PCM;
	wfx.nChannels           = (WORD) pDevice->m_dwChannels;
	wfx.nSamplesPerSec      = pDevice->m_dwSampleRate;
	wfx.wBitsPerSample      = (WORD) pDevice->m_dwBitsPerSample;
	wfx.nBlockAlign         = (WORD) (wfx.wBitsPerSample * wfx.nChannels / 8);
	wfx.nAvgBytesPerSec     = wfx.nBlockAlign * wfx.nSamplesPerSec;

	ZeroMemory(&desc, sizeof(desc));

	desc.dwSize        = sizeof(DSBUFFERDESC1);
	desc.dwFlags       = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS |
			     DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN |
			     DSBCAPS_CTRLFREQUENCY;
	desc.dwBufferBytes = pDevice->m_cbBuffer;
	desc.lpwfxFormat   = &wfx;

	hr = pDS->CreateSoundBuffer((LPCDSBUFFERDESC) &desc, lplpDsb, NULL);

	if (FAILED(hr))
	{
		*lplpDsb = NULL;

		return (hr);
	}

	VERIFY(SUCCEEDED((*lplpDsb)->Play(0, 0, DSBPLAY_LOOPING)));

	VERIFY(SUCCEEDED((*lplpDsb)->SetVolume(0)));

	return (S_OK);
}

typedef int A3dDalA2dSizeCheck2[(sizeof(DAL_A2D) == 0x258) ? 1 : -1];

/* =============================================================
// CreatePrimaryBuffer()
// (RE) dbg:0x1004b0e0
//
// Reject primary-buffer creation.
//
// Returns: E_NOTIMPL.
// =============================================================*/

static HRESULT
CreatePrimaryBuffer(const DSBUFFERDESC1 *pDesc,
		    LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
		    LPUNKNOWN pUnkOuter)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_A2D::CreateSecondaryBuffer()
// (RE) rtl:0x1001e580; dbg:0x1004b100
//
// Create and list a software voice with a reference for the caller.
//
// Returns: S_OK; E_FAIL at the active-voice cap; E_OUTOFMEMORY on allocation
//          failure; the voice Init failure otherwise.
// =============================================================*/

HRESULT
DAL_A2D::CreateSecondaryBuffer(const DSBUFFERDESC1 *pDesc, LPBYTE lpWave,
			       LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
			       LPUNKNOWN pUnkOuter)
{
A2DBuffer      *lpA2DBuffer;
HRESULT         hr;

	*lplpDirectSoundBuffer = NULL;

	if ((DWORD) CountActive() >= m_cVoices)
	{
		DBGSTR("DAL_A2D::CreateSecondaryBuffer() - Max Buffers Reached.\n");

		return (E_FAIL);
	}

	lpA2DBuffer = new A2DBuffer;

	if (!lpA2DBuffer)
	{
		DBGSTR("DAL_A2D::CreateSecondaryBuffer() - Failed to allocated memory for new buffer.\n");

		return (E_OUTOFMEMORY);
	}

	ASSERT((lpA2DBuffer != 0 && !IsBadReadPtr(lpA2DBuffer, sizeof(A2DBuffer))));
	ASSERT((m_pHrtfMgr != 0 && !IsBadReadPtr(m_pHrtfMgr, sizeof(CHrtfMgr))));

	hr = lpA2DBuffer->Init(pDesc, m_pHrtfMgr);

	if (FAILED(hr))
	{
		DBGSTR("DAL_A2D::Creation of 3D buffer failed.\n");

		delete lpA2DBuffer;

		return (hr);
	}

	lpA2DBuffer->AddRef();

	m_VoiceList.AddTail(lpA2DBuffer);

	*lplpDirectSoundBuffer = (LPDIRECTSOUNDBUFFER) lpA2DBuffer;

	return (S_OK);
}

/* =============================================================
// DAL_A2D::IsStarved()
// (RE) dbg:0x1004b380
//
// Report bytes needed when queued output falls below the fill threshold.
//
// Returns: 1 when filling is needed; 0 otherwise, for a null output, or on
//          queue-query failure.
// =============================================================*/

HRESULT
DAL_A2D::IsStarved(DWORD dwNow, DWORD *pdwBytesToFill)
{
DWORD	dwQueued;
int	cbFill;
HRESULT	hr;

	if (pdwBytesToFill == NULL)
	{
		DBGSTR("DAL_A2D::IsStarved() - pdwBytesToFill is NULL.\n");

		return (0);
	}

	ASSERT((pdwBytesToFill != 0 &&
	       !IsBadReadPtr(pdwBytesToFill, sizeof(DWORD))));

	dwQueued = 0;

	hr = GetBytesQueued(&dwQueued);

	if (FAILED(hr))
	{
		DBGSTR("DalBufferInfo::Refresh - ERROR . GetBytesQueued failed\n");

		return (0);
	}

	if (dwQueued >= m_cbFillThreshold)
	{
		*pdwBytesToFill = 0;

		return (0);
	}

	cbFill = m_cbBuffer - dwQueued;

	if (cbFill <= 0)
	{
		*pdwBytesToFill = 0;

		return (0);
	}

	*pdwBytesToFill = cbFill;

	return (1);
}

/* =============================================================
// DAL_A2D::GetBytesQueued()
// (RE) dbg:0x1004b490
//
// Read the queued byte count between the play and mixer write cursors.
//
// Returns: S_OK; E_POINTER for a null output; the GetCurrentPosition failure
//          otherwise.
// =============================================================*/

HRESULT
DAL_A2D::GetBytesQueued(DWORD *lpdwBytesQueued)
{
DWORD	dwPlay;
DWORD	dwWrite;
HRESULT	hr;

	if (lpdwBytesQueued == NULL)
	{
		DBGSTR("DAL_A2D::GetBytesQueued() - lpdwBytesQueued is NULL.\n");

		return (E_POINTER);
	}

	ASSERT((lpdwBytesQueued != 0 &&
	       !IsBadReadPtr(lpdwBytesQueued, sizeof(DWORD))));

	ASSERT((m_pDSBuffer != 0 &&
	       !IsBadReadPtr(m_pDSBuffer, sizeof(IDirectSoundBuffer))));

	dwPlay  = 0;
	dwWrite = 0;

	hr = m_pDSBuffer->GetCurrentPosition(&dwPlay, &dwWrite);

	if (FAILED(hr))
	{
		DBGSTR("DAL_A2D::GetBytesQueued() - Failed GetCurrentPosition() call.\n");

		return (hr);
	}

	if (dwPlay <= m_dwWritePos)
		*lpdwBytesQueued = m_dwWritePos - dwPlay;
	else
		*lpdwBytesQueued = m_dwWritePos + m_cbBuffer - dwPlay;

	return (S_OK);
}

/* =============================================================
// A3dMixVoice()
// (RE) rtl:0x1001e720; dbg:0x1004b680
//
// Update one voice's cursor and playback state, then mix its samples.
// The original ignores pStaging and nMode and passes mode 0 to A3dMixRun.
// =============================================================*/

void
A3dMixVoice(short *pStaging, float *pMix, A2DBuffer *pVoice, DWORD cb, int nMode)
{
A3DMIXPLAY	*pIn;
A3DMIXSTATE	*pState;

	pIn    = &pVoice->m_play;
	pState = &pVoice->m_mix;

	if (pVoice->m_nPendingSeek != A3D_A2D_NO_PENDING_SEEK)
	{
		pVoice->m_dwWrite = (DWORD) pVoice->m_nPendingSeek;

		A3dMixSeek(pState, pIn, pVoice->m_nPendingSeek);

		pVoice->m_nPendingSeek = A3D_A2D_NO_PENDING_SEEK;
	}

	if (pIn->pCur < pIn->pEnd ||
	    (pVoice->m_dwFlags & A3DVOICE_FLAG_LOOPING))
	{
		ASSERT(pIn->pCur >= pIn->pStart);

		pVoice->m_dwWrite = (DWORD) (pIn->pCur - pIn->pStart);
	}
	else
	{
		pVoice->m_dwState = A3DVOICE_STATE_STOPPED;
		pVoice->m_dwWrite = pVoice->m_cbBuffer;
	}

	A3dMixRun(pIn, pMix, pState, cb >> 2, 0);
}

/* =============================================================
// DAL_A2D::WriteOut()
// (RE) rtl:0x1001e7a0; dbg:0x1004b830
//
// Copy staged PCM into the output buffer and advance the write cursor,
// even when Unlock fails.
//
// Returns: Output buffer Lock failure or Unlock result.
// =============================================================*/

HRESULT
DAL_A2D::WriteOut(DWORD dwBytesToFill)
{
BYTE           *pSrc;
void           *pvAudio1;
void           *pvAudio2;
DWORD           cbAudio1;
DWORD           cbAudio2;
HRESULT         hr;

	ASSERT(dwBytesToFill > 0);

	pSrc = (BYTE *) m_pStaging;

	pvAudio1 = NULL;
	pvAudio2 = NULL;
	cbAudio1 = 0;
	cbAudio2 = 0;

	ASSERT((m_pDSBuffer != 0 &&
	       !IsBadReadPtr(m_pDSBuffer, sizeof(IDirectSoundBuffer))));

	hr = m_pDSBuffer->Lock(m_dwWritePos, dwBytesToFill, &pvAudio1, &cbAudio1,
			     &pvAudio2, &cbAudio2, 0);

	if (FAILED(hr))
	{
		DBGSTR("DAL_A2D::CopyWorkToOutput() - Failed Lock().\n");

		return (hr);
	}

	CopyMemory(pvAudio1, pSrc, cbAudio1);

	pSrc += cbAudio1;

	if (cbAudio2)
		CopyMemory(pvAudio2, pSrc, cbAudio2);

	m_dwWritePos = (m_dwWritePos + dwBytesToFill) % m_cbBuffer;

	hr = m_pDSBuffer->Unlock(pvAudio1, cbAudio1, pvAudio2, cbAudio2);

	if (FAILED(hr))
		DBGSTR("DAL_A2D::CopyToWorkOutput() - Failed Unlock().\n");

	return (hr);
}

/* =============================================================
// DAL_A2D::CountActive()
// (RE) dbg:0x1004ba50
//
// Count playing voices.
//
// Returns: Number of voices in A3DVOICE_STATE_PLAYING.
// =============================================================*/

int
DAL_A2D::CountActive(void)
{
POSITION        pos;
A2DBuffer      *lpA2DBuffer;
int             cActive;

	cActive = 0;

	for (pos = m_VoiceList.GetHeadPosition(); pos != NULL; )
	{
		lpA2DBuffer = (A2DBuffer *) m_VoiceList.GetNext(pos);

		ASSERT((lpA2DBuffer != 0 &&
		       !IsBadReadPtr(lpA2DBuffer, sizeof(A2DBuffer))));

		if (lpA2DBuffer->m_dwState == A3DVOICE_STATE_PLAYING)
			cActive++;
	}

	return (cActive);
}

/* =============================================================
// DAL_A2D::MixThread()
// (RE) rtl:0x1001e860; dbg:0x1004bb20
//
// Fill the output from playing voices and collect released voices until
// stopped, then signal the inactive event.
//
// Returns: 0.
// =============================================================*/

DWORD WINAPI
DAL_A2D::MixThread(void *pv)
{
DAL_A2D        *pDAL_A2D;
POSITION        pos;
POSITION        posCur;
A2DBuffer      *lpA2DBuffer;
DWORD           dwNow;
DWORD           cb;
int             fMixed;
int             fStopped;

	pDAL_A2D = (DAL_A2D *) pv;
	fStopped = 0;

	while (pDAL_A2D->m_fThreadRun)
	{
		WaitForSingleObject(pDAL_A2D->m_hWake, pDAL_A2D->m_dwWakeTimeoutMs);

		if (pDAL_A2D->m_VoiceList.GetCount() != 0)
		{
			if (fStopped)
			{
				VERIFY(SUCCEEDED(pDAL_A2D->m_pDSBuffer->SetCurrentPosition(0)));
				VERIFY(SUCCEEDED(pDAL_A2D->m_pDSBuffer->Play(0, 0, DSBPLAY_LOOPING)));

				fStopped = 0;
			}

			dwNow = GetTickCount();

			if (pDAL_A2D->IsStarved(dwNow, &cb) != 0)
			{
				if (cb > pDAL_A2D->m_cbBuffer >> 1)
					cb = pDAL_A2D->m_cbBuffer >> 1;

				fMixed = 0;

				ZeroMemory(pDAL_A2D->m_pMix, 2 * cb);
				ZeroMemory(pDAL_A2D->m_pStaging, 2 * cb);

				for (pos = pDAL_A2D->m_VoiceList.GetHeadPosition(); pos != NULL; )
				{
					posCur = pos;

					lpA2DBuffer = (A2DBuffer *) pDAL_A2D->m_VoiceList.GetNext(pos);

					ASSERT((lpA2DBuffer != 0 &&
					       !IsBadReadPtr(lpA2DBuffer, sizeof(A2DBuffer))));

					if (lpA2DBuffer->m_dwState == A3DVOICE_STATE_PLAYING)
					{
						A3dMixVoice(pDAL_A2D->m_pStaging,
							    pDAL_A2D->m_pMix,
							    lpA2DBuffer, cb, fMixed);

#if defined(A3D_FIXES)
						if (A3dGetConfig().bSoftwareReflections)
						{
							/* NOT PART OF THE ORIGINAL.
							 * See A3dReflectionEmu.h.
							*/
							CA3dReflectionEmuVoice *pRefVoice =
								A3dReflectionEmuGetVoice(lpA2DBuffer);

							if (pRefVoice)
								pRefVoice->Process(
									lpA2DBuffer, pDAL_A2D->m_pMix,
									cb >> 2,
									pDAL_A2D->m_dwSampleRate);
						}
#endif

						fMixed = 1;
					}

					if (lpA2DBuffer->m_dwState == A3DVOICE_STATE_RELEASED)
					{
						pDAL_A2D->m_VoiceList.RemoveAt(posCur);

						if (lpA2DBuffer)
							delete lpA2DBuffer;
					}
				}

				if (fMixed)
				{
#if defined(A3D_FIXES)
					if (A3dGetConfig().bSoftwareReverb)
					{
						/* NOT PART OF THE ORIGINAL.  The
						 * whole dry mix, wet mix summed in;
						 * see A3dReverbEmu.h.
						 * A3dMixInterleave's cSamples below
						 * is one float per channel per frame,
						 * i.e. cb >> 1 floats total for
						 * interleaved stereo; this call wants
						 * frames, which is half that.
						*/
						A3dReverbEmuGetEngine()->Process(
							pDAL_A2D->m_pMix, cb >> 2,
							pDAL_A2D->m_dwSampleRate);
					}
#endif

					A3dMixInterleave(pDAL_A2D->m_pMix,
							 pDAL_A2D->m_pStaging,
							 cb >> 1);
				}

				pDAL_A2D->WriteOut(cb);
			}
		}
		else if (!fStopped)
		{
			pDAL_A2D->m_pDSBuffer->Stop();

			fStopped = 1;
		}
	}

	VERIFY(SetEvent(pDAL_A2D->m_hEventCallbackInactive));

	return (0);
}

