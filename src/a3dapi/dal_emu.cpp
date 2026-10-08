/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * dal_emu.cpp
 *
 * Implements DAL_EMU, a device that tracks emulated voices without
 * producing audio. It creates EMUBuffer objects, retains their allocation
 * list and reports fixed capabilities with available counts based on
 * playing voices.
 *
 * Most device operations do not change state or are unsupported.
 * EMUBuffer supplies elapsed-time playback cursors and stored controls,
 * allowing the resource manager to represent a voice without a
 * DirectSound output buffer.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "dal_emu.h"
#include "emubuffer.h"

/* Reported limits  */
#define A3D_EMU_MAX_BUFFERS 0xFFFF
#define A3D_EMU_MIN_SAMPLE_RATE 6000
#define A3D_EMU_MAX_SAMPLE_RATE 100000

/* =============================================================
// DAL_EMU::DAL_EMU()
// (RE) dbg:0x10052240; thunk dbg:0x100040C0
//
// Initialize an empty emulation device.
// =============================================================*/

DAL_EMU::DAL_EMU(void)
{
	m_cRef        = 1;
	m_dwMaxBuffers = A3D_EMU_MAX_BUFFERS;
}

/* =============================================================
// DAL_EMU::~DAL_EMU()
// (RE) dbg:0x100522d0
//
// Delete all allocated emulation buffers.
// =============================================================*/

DAL_EMU::~DAL_EMU(void)
{
	EMUBuffer	*pBuffer;

	while ((pBuffer = (EMUBuffer *) m_list.RemoveHead()) != NULL)
		delete pBuffer;
}

/* =============================================================
// DAL_EMU::QueryInterface()
// (RE) rtl:0x10020920; dbg:0x100523f0
//
// Obtain a referenced device interface.
//
// Returns:
//   S_OK
//   E_INVALIDARG   a null output
//   E_NOINTERFACE  an unknown IID
// =============================================================*/

STDMETHODIMP
DAL_EMU::QueryInterface(REFIID riid, void **ppv)
{
	if (ppv == NULL)
		return (E_INVALIDARG);

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
	else
	{
		*ppv = NULL;

		return (E_NOINTERFACE);
	}

	((IUnknown *) *ppv)->AddRef();

	return (S_OK);
}

/* =============================================================
// DAL_EMU::AddRef()
// (RE) rtl:0x10017f50; dbg:0x10052530
//
// Increment the reference count.
//
// Returns: Reference count read after the increment.
// =============================================================*/

STDMETHODIMP_(ULONG)
DAL_EMU::AddRef(void)
{
	InterlockedIncrement(&m_cRef);

	return ((ULONG) m_cRef);
}

/* =============================================================
// DAL_EMU::Release()
// (RE) rtl:0x10020a10; dbg:0x10052560
//
// Release a reference and delete the device at zero.
// The original rereads the count after the interlocked decrement.
//
// Returns: Remaining reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
DAL_EMU::Release(void)
{
	InterlockedDecrement(&m_cRef);

	if (m_cRef != 0)
		return ((ULONG) m_cRef);

	delete this;

	return (0);
}

/* =============================================================
// DAL_EMU::CreateSoundBuffer()
// (RE) rtl:0x10020a50; dbg:0x10052620
//
// Create an emulation secondary buffer.
//
// Returns:
//   CreateSecondaryBuffer result
//   E_INVALIDARG                  aggregation
// =============================================================*/

STDMETHODIMP
DAL_EMU::CreateSoundBuffer(LPCDSBUFFERDESC lpcDSBufferDesc,
			   LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
			   LPUNKNOWN pUnkOuter)
{
	ASSERT(lpcDSBufferDesc != 0 &&
	       !IsBadReadPtr(lpcDSBufferDesc, sizeof(DSBUFFERDESC)));
	ASSERT(lplpDirectSoundBuffer != 0 &&
	       !IsBadReadPtr(lplpDirectSoundBuffer, sizeof(LPDIRECTSOUNDBUFFER)));

	if (pUnkOuter != NULL)
	{
		DBGSTR("DAL_EMU::CreateSoundBuffer() - Aggregation not supported.\n");

		return (E_INVALIDARG);
	}

	return (CreateSecondaryBuffer((const DSBUFFERDESC1 *) lpcDSBufferDesc,
				      NULL, lplpDirectSoundBuffer, pUnkOuter));
}

/* =============================================================
// DAL_EMU::GetCaps()
// (RE) rtl:0x10020a80; dbg:0x10052730
//
// Write fixed capabilities and free counts based on playing voices.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
DAL_EMU::GetCaps(LPDSCAPS lpDirectSoundCaps)
{
	DWORD	dwPlaying;

	ASSERT(lpDirectSoundCaps != 0 &&
	       !IsBadReadPtr(lpDirectSoundCaps, sizeof(DSCAPS)));

	dwPlaying = CountPlayingVoices();

	lpDirectSoundCaps->dwSize  = sizeof(DSCAPS);
	lpDirectSoundCaps->dwFlags = DSCAPS_PRIMARYMONO | DSCAPS_PRIMARYSTEREO |
				     DSCAPS_PRIMARY8BIT | DSCAPS_PRIMARY16BIT |
				     DSCAPS_CONTINUOUSRATE |
				     DSCAPS_SECONDARYMONO | DSCAPS_SECONDARYSTEREO |
				     DSCAPS_SECONDARY8BIT | DSCAPS_SECONDARY16BIT;

	lpDirectSoundCaps->dwMinSecondarySampleRate = A3D_EMU_MIN_SAMPLE_RATE;
	lpDirectSoundCaps->dwMaxSecondarySampleRate = A3D_EMU_MAX_SAMPLE_RATE;
	lpDirectSoundCaps->dwPrimaryBuffers	    = 0;

	lpDirectSoundCaps->dwMaxHwMixingAllBuffers              = m_dwMaxBuffers;
	lpDirectSoundCaps->dwMaxHwMixingStaticBuffers           = m_dwMaxBuffers;
	lpDirectSoundCaps->dwMaxHwMixingStreamingBuffers        = m_dwMaxBuffers;
	lpDirectSoundCaps->dwFreeHwMixingAllBuffers             = m_dwMaxBuffers - dwPlaying;
	lpDirectSoundCaps->dwFreeHwMixingStaticBuffers          = m_dwMaxBuffers - dwPlaying;
	lpDirectSoundCaps->dwFreeHwMixingStreamingBuffers       = m_dwMaxBuffers - dwPlaying;

	lpDirectSoundCaps->dwMaxHw3DAllBuffers		 = m_dwMaxBuffers;
	lpDirectSoundCaps->dwMaxHw3DStaticBuffers	 = m_dwMaxBuffers;
	lpDirectSoundCaps->dwMaxHw3DStreamingBuffers	 = m_dwMaxBuffers;
	lpDirectSoundCaps->dwFreeHw3DAllBuffers		 = m_dwMaxBuffers - dwPlaying;
	lpDirectSoundCaps->dwFreeHw3DStaticBuffers	 = m_dwMaxBuffers - dwPlaying;
	lpDirectSoundCaps->dwFreeHw3DStreamingBuffers	 = m_dwMaxBuffers - dwPlaying;

	lpDirectSoundCaps->dwTotalHwMemBytes		 = 0;
	lpDirectSoundCaps->dwFreeHwMemBytes		 = 0;
	lpDirectSoundCaps->dwMaxContigFreeHwMemBytes	 = 0;
	lpDirectSoundCaps->dwUnlockTransferRateHwBuffers = 0;
	lpDirectSoundCaps->dwPlayCpuOverheadSwBuffers	 = 0;

	return (S_OK);
}

/* =============================================================
// DAL_EMU::DuplicateSoundBuffer()
// (RE) rtl:0x10003500; dbg:0x100528f0
//
// Leave the buffer copy output unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_EMU::DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER lpDirectSoundBuffer,
			      LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer)
{
	ASSERT(lpDirectSoundBuffer != 0 &&
	       !IsBadReadPtr(lpDirectSoundBuffer, sizeof(IDirectSoundBuffer)));
	ASSERT(lplpDirectSoundBuffer != 0 &&
	       !IsBadReadPtr(lplpDirectSoundBuffer, sizeof(LPDIRECTSOUNDBUFFER)));

	return (E_NOTIMPL);
}

/* =============================================================
// DAL_EMU::SetCooperativeLevel()
// (RE) rtl:0x10003500; dbg:0x100529c0
//
// Leave the cooperative level unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_EMU::SetCooperativeLevel(HWND hWnd, DWORD dwLevel)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_EMU::Compact()
// (RE) rtl:0x1001eec0; dbg:0x100529e0
//
// Leave device memory unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_EMU::Compact(void)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_EMU::GetSpeakerConfig()
// (RE) rtl:0x10020b90; dbg:0x10052a00
//
// Leave the speaker configuration output unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_EMU::GetSpeakerConfig(LPDWORD pdwConfig)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_EMU::SetSpeakerConfig()
// (RE) rtl:0x10020b90; dbg:0x10052a20
//
// Leave the speaker configuration unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_EMU::SetSpeakerConfig(DWORD dwConfig)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_EMU::Initialize()
// (RE) rtl:0x10020b90; dbg:0x10052a40
//
// Leave the device unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_EMU::Initialize(LPCGUID pGuid)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_EMU::InitializeEx()
// (RE) rtl:0x10020b40; dbg:0x10052a60
//
// Report no enabled features and initialize the emulation device.
//
// Returns: Init result.
// =============================================================*/

STDMETHODIMP
DAL_EMU::InitializeEx(LPGUID pGuidDevice, DWORD dwFlags, DWORD dwReserved,
		      LPDWORD lpdwFeaturesEnabled)
{
	ASSERT(lpdwFeaturesEnabled != 0 &&
	       !IsBadReadPtr(lpdwFeaturesEnabled, sizeof(DWORD)));

	*lpdwFeaturesEnabled = 0;

	return (Init());
}

/* =============================================================
// DAL_EMU::CreateSoundBufferEx()
// (RE) rtl:0x1001eef0; dbg:0x10052af0
//
// Leave the buffer output unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_EMU::CreateSoundBufferEx(const DSBUFFERDESC1 *lpcDSBufferDesc,
			     LPBYTE lpbWave,
			     LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
			     LPUNKNOWN pUnkOuter)
{
	ASSERT(lpcDSBufferDesc != 0 &&
	       !IsBadReadPtr(lpcDSBufferDesc, sizeof(DSBUFFERDESC)));
	ASSERT(lpbWave != 0 &&
	       !IsBadReadPtr(lpbWave, sizeof(BYTE)));
	ASSERT(lplpDirectSoundBuffer != 0 &&
	       !IsBadReadPtr(lplpDirectSoundBuffer, sizeof(LPDIRECTSOUNDBUFFER)));

	return (E_NOTIMPL);
}

/* =============================================================
// DAL_EMU::GetA3dCaps()
// (RE) rtl:0x10003500; dbg:0x10052c10
//
// Leave capability outputs unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_EMU::GetA3dCaps(LPA3DDALCAPS lpA3dCaps, LPDWORD lpdwSize)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_EMU::GetDriverInfo()
// (RE) rtl:0x1001eef0; dbg:0x10052c30
//
// Leave driver information outputs unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_EMU::GetDriverInfo(LPHANDLE lphA3dVxd, void **lplpIDsDriver,
		       void **lplpIA3dDriver, LPDWORD lpdwCardIndex)
{
	ASSERT(lphA3dVxd != 0 && !IsBadReadPtr(lphA3dVxd, sizeof(HANDLE)));
	ASSERT(lplpIDsDriver != 0 &&
	       !IsBadReadPtr(lplpIDsDriver, sizeof(LPVOID)));
	ASSERT(lplpIA3dDriver != 0 &&
	       !IsBadReadPtr(lplpIA3dDriver, sizeof(LPVOID)));
	ASSERT(lpdwCardIndex != 0 &&
	       !IsBadReadPtr(lpdwCardIndex, sizeof(DWORD)));

	return (E_NOTIMPL);
}

/* =============================================================
// DAL_EMU::GetDS()
// (RE) rtl:0x10020b90; dbg:0x10052da0
//
// Leave the DirectSound output unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_EMU::GetDS(LPDIRECTSOUND *lplpDirectSound)
{
	ASSERT(lplpDirectSound != 0 &&
	       !IsBadReadPtr(lplpDirectSound, sizeof(LPDIRECTSOUND)));

	return (E_NOTIMPL);
}

/* =============================================================
// DAL_EMU::GetDSDriverDesc()
// (RE) rtl:0x10003500; dbg:0x10052e10
//
// Leave driver descriptor outputs unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_EMU::GetDSDriverDesc(void *lpsDSDriverDesc, LPDWORD lpdwSize)
{
	ASSERT(lpdwSize != 0 && !IsBadReadPtr(lpdwSize, sizeof(DWORD)));

	return (E_NOTIMPL);
}

/* =============================================================
// DAL_EMU::QueryFunctionality()
// (RE) rtl:0x10003500; dbg:0x10052e80
//
// Leave the functionality output unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_EMU::QueryFunctionality(DWORD dwFunction, LPDWORD lpdwStatus)
{
	ASSERT(lpdwStatus != 0 && !IsBadReadPtr(lpdwStatus, sizeof(DWORD)));

	return (E_NOTIMPL);
}

/* =============================================================
// DAL_EMU::Verify()
// (RE) rtl:0x10020b60; dbg:0x10052ef0
//
// Leave verification outputs unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_EMU::Verify(LPSTR lpcString, LPSTR *lplpcStringCrypted,
		LPSTR *lplpcCopyright)
{
	ASSERT(lpcString != 0 && !IsBadReadPtr(lpcString, sizeof(char)));
	ASSERT(lplpcStringCrypted != 0 &&
	       !IsBadReadPtr(lplpcStringCrypted, sizeof(char *)));
	ASSERT(lplpcCopyright != 0 &&
	       !IsBadReadPtr(lplpcCopyright, sizeof(char *)));

	return (E_NOTIMPL);
}

/* =============================================================
// DAL_EMU::SetOutputMode()
// (RE) rtl:0x10020b60; dbg:0x10053010
//
// Leave the output mode unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_EMU::SetOutputMode(DWORD dwFrontXtalkMode, DWORD dwBackXtalkMode,
		       DWORD dwQuadMode)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_EMU::GetOutputMode()
// (RE) rtl:0x10020b60; dbg:0x10053030
//
// Leave output mode outputs unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_EMU::GetOutputMode(LPDWORD lpdwFrontXtalkMode, LPDWORD lpdwBackXtalkMode,
		       LPDWORD lpdwQuadMode)
{
	ASSERT(lpdwFrontXtalkMode != 0 &&
	       !IsBadReadPtr(lpdwFrontXtalkMode, sizeof(DWORD)));
	ASSERT(lpdwBackXtalkMode != 0 &&
	       !IsBadReadPtr(lpdwBackXtalkMode, sizeof(DWORD)));
	ASSERT(lpdwQuadMode != 0 &&
	       !IsBadReadPtr(lpdwQuadMode, sizeof(DWORD)));

	return (E_NOTIMPL);
}

/* =============================================================
// DAL_EMU::SetResourceManagerMode()
// (RE) rtl:0x10020b70; dbg:0x10053150
//
// Reject the requested resource manager mode.
//
// Returns:
//   E_NOTIMPL     modes 0 through 3
//   E_INVALIDARG  otherwise
// =============================================================*/

STDMETHODIMP
DAL_EMU::SetResourceManagerMode(DWORD dwResourceManagerMode)
{
	ASSERT(dwResourceManagerMode <= 0x00000003);

	if (dwResourceManagerMode <= A3D_RESOURCE_MODE_LAST)
		return (E_NOTIMPL);

	return (E_INVALIDARG);
}

/* =============================================================
// DAL_EMU::GetResourceManagerMode()
// (RE) rtl:0x10020b90; dbg:0x100531c0
//
// Leave the resource manager mode output unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_EMU::GetResourceManagerMode(LPDWORD lpdwResourceManagerMode)
{
	ASSERT(lpdwResourceManagerMode != 0 &&
	       !IsBadReadPtr(lpdwResourceManagerMode, sizeof(DWORD)));

	return (E_NOTIMPL);
}

/* =============================================================
// DAL_EMU::SetHFAbsorbFactor()
// (RE) rtl:0x10020b90; dbg:0x10053230
//
// Leave the absorption factor unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_EMU::SetHFAbsorbFactor(FLOAT fFactor)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_EMU::GetHFAbsorbFactor()
// (RE) rtl:0x10020b90; dbg:0x10053250
//
// Leave the absorption factor output unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_EMU::GetHFAbsorbFactor(FLOAT *pfFactor)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_EMU::RegisterVersion()
// (RE) rtl:0x10020b90; dbg:0x10053270
//
// Leave the registered version unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_EMU::RegisterVersion(DWORD dwVersion)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_EMU::GetSoftwareCaps()
// (RE) rtl:0x10020b90; dbg:0x10053290
//
// Leave software capability outputs unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_EMU::GetSoftwareCaps(LPA3DCAPS_SOFTWARE pCaps)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_EMU::GetHardwareCaps()
// (RE) rtl:0x10020b90; dbg:0x100532b0
//
// Leave hardware capability outputs unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_EMU::GetHardwareCaps(LPA3DCAPS_HARDWARE pCaps)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_EMU::Init()
// (RE) dbg:0x100532d0
//
// Leave the emulation device unchanged.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
DAL_EMU::Init(void)
{
	return (S_OK);
}

/* =============================================================
// DAL_EMU::CreateSecondaryBuffer()
// (RE) rtl:0x10020ba0; dbg:0x10053310
//
// Allocate, initialize and list an emulation buffer.
//
// Returns: S_OK; E_FAIL at the playing voice limit; E_OUTOFMEMORY for
//          allocation failure; otherwise the buffer Init failure.
// =============================================================*/

HRESULT
DAL_EMU::CreateSecondaryBuffer(const DSBUFFERDESC1 *pDesc, LPBYTE lpWave,
			       LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
			       LPUNKNOWN pUnkOuter)
{
	EMUBuffer      *pBuffer;
	HRESULT         hr;

	if (CountPlayingVoices() >= m_dwMaxBuffers)
		return (E_FAIL);

	*lplpDirectSoundBuffer = NULL;

	pBuffer = new EMUBuffer;

	if (pBuffer == NULL)
	{
		DBGSTR("DAL_EMU::CreateSecondaryBuffer() - Failed to allocated memory for new buffer.\n");

		return (E_OUTOFMEMORY);
	}

	hr = pBuffer->Init(pDesc, &m_list);

	if (FAILED(hr))
	{
		DBGSTR("DAL_EMU::Creation of 3d buffer failed.\n");

		delete pBuffer;

		return (hr);
	}

	m_list.AddTail(pBuffer);

	*lplpDirectSoundBuffer = (IDirectSoundBuffer *) pBuffer;

	return (S_OK);
}

/* =============================================================
// DAL_EMU::CountPlayingVoices()
// (RE) dbg:0x10053510; inlined in Retail GetCaps at rtl:0x10020A8A
//
// Count allocated buffers in the playing state.
//
// Returns: Number of playing voices.
// =============================================================*/

DWORD
DAL_EMU::CountPlayingVoices(void)
{
	EMUBuffer      *lpEMUBuffer;
	POSITION        pos;
	DWORD           dwPlaying;

	dwPlaying = 0;

	pos = m_list.GetHeadPosition();

	while (pos != NULL)
	{
		lpEMUBuffer = (EMUBuffer *) m_list.GetNext(pos);

		ASSERT(lpEMUBuffer != 0 &&
		       !IsBadReadPtr(lpEMUBuffer, sizeof(EMUBuffer)));

		if (lpEMUBuffer->m_dwState == A3DVOICE_STATE_PLAYING)
			dwPlaying++;
	}

	return (dwPlaying);
}
