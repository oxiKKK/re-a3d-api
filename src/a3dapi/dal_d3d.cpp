/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * dal_d3d.cpp
 *
 * Implements DAL_D3D, the hardware DirectSound3D backend. It discovers a
 * suitable device, probes A3D property support, initializes the primary
 * listener and creates D3DBuffer voices for source playback.
 *
 * The private startup operation starts output and a worker that advances
 * duplicated reflection buffers within a processing budget. Device-level
 * capability and feature handling live here; d3dbuffer.cpp applies source
 * controls and synchronizes the individual reflected voices.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "dal_d3d.h"
#include "d3dbuffer.h"
#include "a3dclsfc.h"

/* Reflection-worker limits */
#define A3D_D3D_REFLECTION_WAIT_MS      9
#define A3D_D3D_REFLECTION_AGE_BUDGET   5

/* Property-read roles */
#define A3D_D3D_GET_CAPABILITIES 0
#define A3D_D3D_ACQUIRE_FEATURES 1
#define A3D_D3D_RELEASE_FEATURES 2
/* Reflection-buffer capability  */
#define A3D_D3D_CAP_REFLECTION_VOICES 0x80000
/* Name retained in CreateReflection's diagnostic. */
#define DSPROPERTY_A3DBUFFER_SET_REFLECTION 1

/* Reported limits and property buffer size */
#define A3D_D3D_MAX_REFLECTIONS         60
#define A3D_D3D_CAPS_VERSION            288
#define A3D_D3D_PROPERTY_BUFFER_BYTES   1000

/* Device-selection branch masks */
#define A3D_D3D_SKIP_CHECKED_DS3D_DEVICE 0x40
#define A3D_D3D_SKIP_A3D_DEVICE_FALLBACK 0x10

#ifdef _DEBUG
#include "resman.h"
extern ResMan *g_lpResMan;
#endif

/* (RE) Private startup interface IID: dbg:0x101409A8. */

static const GUID IID_IA3dPrvD3D =
	{ 0x0a991a7d, 0xc7a7, 0x11d2,
	  { 0xb9, 0x68, 0x00, 0x10, 0x5a, 0x20, 0x24, 0x9d } };

/* (RE) A3D device CLSID: dbg:0x10140798. */

static const CLSID CLSID_A3dDevice =
	{ 0x47d4d946, 0x62e8, 0x11cf,
	  { 0x93, 0xbc, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };

/* (RE) DS3D device CLSID: dbg:0x10140838. */

static const CLSID CLSID_Ds3dDevice =
	{ 0xd8f1eee0, 0xf634, 0x11cf,
	  { 0x87, 0x00, 0x00, 0xa0, 0x24, 0x5d, 0x91, 0x8b } };

/* =============================================================
// Failed()
// (RE) dbg:0x10050910
//
// Test an HRESULT for failure.
//
// Returns:
//   TRUE   a negative HRESULT
//   FALSE  otherwise
// =============================================================*/

static BOOL Failed(HRESULT hr)		{ return (hr < 0); }

/* =============================================================
// Succeeded()
// (RE) dbg:0x10050930
//
// Test an HRESULT for success.
//
// Returns:
//   TRUE   a nonnegative HRESULT
//   FALSE  otherwise
// =============================================================*/

static BOOL Succeeded(HRESULT hr)	{ return (!Failed(hr)); }

static DWORD WINAPI	ReflectionThread(LPVOID pv);

static HRESULT		InitListener(IDirectSoundBuffer *pPrimary,
				     IDirectSound3DListener **ppListener);

/* =============================================================
// DAL_D3D::DAL_D3D()
// (RE) dbg:0x1004e370; thunk dbg:0x10003972
//
// Construct an empty DirectSound3D device with the requested A3D requirement.
// =============================================================*/

DAL_D3D::DAL_D3D(void *fRequireA3d)
{
	m_cRef                          = 1;
	m_dwSplashWindow                = 0;
	m_pDirectSound                  = NULL;
	m_lpDirectSound3DPrimaryBuffer  = NULL;
	m_lpDirectSound3DListener       = NULL;
	m_dwFreeHw3DBuffers             = 0;
	m_fStarted                      = 0;
	m_fHardware                     = 0;
	m_dwEnabledFeatures             = 0;
	m_dwPropertyReleaseData         = 0;
	m_fThreadRun                    = 0;
	m_hThread                       = (HANDLE) -1;
	m_hBufferListMutex              = 0;
	m_hThreadExitEvent              = 0;
	m_Unknown_0x60                  = 0;
	m_fRequireA3d                   = fRequireA3d;
	m_fUseReflections               = 0;
}

/* =============================================================
// DAL_D3D::~DAL_D3D()
// (RE) dbg:0x1004E4D0; rtl:0x1001F260
//
// Stop reflection processing and release voices, device resources and handles.
// =============================================================*/

DAL_D3D::~DAL_D3D(void)
{
D3DBuffer      *pBuffer;
DWORD           adwFree[3];

	if (m_fUseReflections)
	{
		m_fThreadRun = 0;

		if (m_hThreadExitEvent)
			VERIFY(WaitForSingleObject(m_hThreadExitEvent,
						   INFINITE) == WAIT_OBJECT_0);
	}

	if (m_fUseReflections && m_hBufferListMutex)
		VERIFY(WaitForSingleObject(m_hBufferListMutex,
					   INFINITE) == WAIT_OBJECT_0);

	while ((pBuffer = (D3DBuffer *) m_BufferList.RemoveHead()) != NULL)
		delete pBuffer;

	if (m_fUseReflections && m_hBufferListMutex)
		VERIFY(ReleaseMutex(m_hBufferListMutex));

	if (m_lpDirectSound3DPrimaryBuffer)
	{
		m_lpDirectSound3DPrimaryBuffer->Stop();

		if (m_lpDirectSound3DPrimaryBuffer)
		{
			m_lpDirectSound3DPrimaryBuffer->Release();
			m_lpDirectSound3DPrimaryBuffer = NULL;
		}
	}

	if (m_dwEnabledFeatures)
	{
		adwFree[0] = 0;
		adwFree[1] = m_dwEnabledFeatures;
		adwFree[2] = m_dwPropertyReleaseData;
		AccessA3dProperty(A3D_D3D_RELEASE_FEATURES, TRUE, adwFree, sizeof(adwFree));
	}

	if (m_lpDirectSound3DListener)
	{
		m_lpDirectSound3DListener->Release();
		m_lpDirectSound3DListener = NULL;
	}

	if (m_pDirectSound)
	{
		m_pDirectSound->Release();
		m_pDirectSound = NULL;
	}

	if (m_hThreadExitEvent != (HANDLE) -1)
	{
		CloseHandle(m_hThreadExitEvent);
		m_hThreadExitEvent = (HANDLE) -1;
	}

	if (m_hBufferListMutex != (HANDLE) -1)
	{
		CloseHandle(m_hBufferListMutex);
		m_hBufferListMutex = (HANDLE) -1;
	}

	if (m_hThread != (HANDLE) -1)
	{
		CloseHandle(m_hThread);
		m_hThread = (HANDLE) -1;
	}
}

/* =============================================================
// DAL_D3D::QueryInterface()
// (RE) rtl:0x1001f430; dbg:0x1004e860
//
// Return an AddRef'd interface.
//
// Returns:
//   S_OK
//   E_INVALIDARG   a null output
//   E_NOINTERFACE  an unknown IID
// =============================================================*/

STDMETHODIMP
DAL_D3D::QueryInterface(REFIID riid, void **ppv)
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
	else if (IsEqualIID(riid, IID_IA3dPrvD3D))
	{
		*ppv = (IA3dPrvD3D *) this;
	}
	else
	{
		*ppv = NULL;

		return (E_NOINTERFACE);
	}

	((IDirectSound *) this)->AddRef();

	return (S_OK);
}

/* =============================================================
// DAL_D3D::AddRef()
// (RE) rtl:0x1001dca0; dbg:0x1004e9f0
//
// Take a reference.
//
// Returns: Reference count read after the interlocked increment.
// =============================================================*/

STDMETHODIMP_(ULONG)
DAL_D3D::AddRef(void)
{
	InterlockedIncrement(&m_cRef);

	return ((ULONG) m_cRef);
}

/* =============================================================
// DAL_D3D::Release()
// (RE) rtl:0x1001f550; dbg:0x1004ea20
//
// Release a reference and delete the device at zero.
//
// Returns: Reference count read after the interlocked decrement; zero after
//          deletion.
// =============================================================*/

STDMETHODIMP_(ULONG)
DAL_D3D::Release(void)
{
	InterlockedDecrement(&m_cRef);

	if (m_cRef != 0)
		return ((ULONG) m_cRef);

	delete this;

	return (0);
}

/* =============================================================
// DAL_D3D::CreateSoundBuffer()
// (RE) rtl:0x1001f590; dbg:0x1004eae0
//
// Return the shared primary buffer or create a secondary voice.
//
// Returns: E_INVALIDARG for aggregation; otherwise the selected buffer method's
//          result.
// =============================================================*/

STDMETHODIMP
DAL_D3D::CreateSoundBuffer(LPCDSBUFFERDESC lpcDSBufferDesc,
			   LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
			   LPUNKNOWN pUnkOuter)
{
	ASSERT((lpcDSBufferDesc != 0 &&
	       !IsBadReadPtr(lpcDSBufferDesc, sizeof(DSBUFFERDESC))));
	ASSERT((lplpDirectSoundBuffer != 0 &&
	       !IsBadReadPtr(lplpDirectSoundBuffer, sizeof(LPDIRECTSOUNDBUFFER))));

	if (pUnkOuter != NULL)
	{
		DBGSTR("DAL_D3D::CreateSoundBuffer() - Aggregation not supported.\n");

		return (E_INVALIDARG);
	}

	if (lpcDSBufferDesc->dwFlags & DSBCAPS_PRIMARYBUFFER)
		return (GetPrimaryBuffer((const DSBUFFERDESC1 *) lpcDSBufferDesc,
					 lplpDirectSoundBuffer, NULL));

	return (CreateSecondaryBuffer((const DSBUFFERDESC1 *) lpcDSBufferDesc,
				      NULL, lplpDirectSoundBuffer, NULL));
}

/* =============================================================
// DAL_D3D::GetCaps()
// (RE) rtl:0x1001f600; dbg:0x1004ec30
//
// Forward to the wrapped device's GetCaps.
//
// Returns:
//   DirectSound GetCaps result
//   E_FAIL                      without a device
// =============================================================*/

STDMETHODIMP
DAL_D3D::GetCaps(LPDSCAPS lpDirectSoundCaps)
{
	ASSERT((lpDirectSoundCaps != 0 &&
	       !IsBadReadPtr(lpDirectSoundCaps, sizeof(DSCAPS))));

	if (m_pDirectSound != NULL)
		return (m_pDirectSound->GetCaps(lpDirectSoundCaps));

	DBGSTR("DAL_D3D::GetCaps() - No DirectSound interface.\n");

	return (E_FAIL);
}

/* =============================================================
// DAL_D3D::DuplicateSoundBuffer()
// (RE) rtl:0x1001f630; dbg:0x1004ece0
//
// Duplicate the source's DirectSound buffer and list its new D3DBuffer wrapper.
//
// Returns: S_OK; E_OUTOFMEMORY on wrapper allocation failure; the DirectSound
//          DuplicateSoundBuffer or InitDuplicate failure otherwise.
// =============================================================*/

STDMETHODIMP
DAL_D3D::DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER lpDirectSoundBuffer,
			      LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer)
{
D3DBuffer              *pBuffer;
LPDIRECTSOUNDBUFFER     lpDuplicateSoundBuffer;
HRESULT                 hr;

	ASSERT((lpDirectSoundBuffer != 0 &&
	       !IsBadReadPtr(lpDirectSoundBuffer, sizeof(IDirectSoundBuffer))));
	ASSERT((lplpDirectSoundBuffer != 0 &&
	       !IsBadReadPtr(lplpDirectSoundBuffer, sizeof(LPDIRECTSOUNDBUFFER))));

	*lplpDirectSoundBuffer = NULL;

	lpDuplicateSoundBuffer = NULL;
	hr = m_pDirectSound->DuplicateSoundBuffer(
			((D3DBuffer *) lpDirectSoundBuffer)->m_lpDirectSoundBuffer,
			&lpDuplicateSoundBuffer);
	if (Failed(hr))
	{
		DBGSTR("DAL_D3D::DuplicateSoundBuffer() - Failed to duplicate buffer from DirectSound.\n");

		return (hr);
	}

	ASSERT((lpDuplicateSoundBuffer != 0 &&
	       !IsBadReadPtr(lpDuplicateSoundBuffer, sizeof(IDirectSoundBuffer))));

	pBuffer = new D3DBuffer;
	if (pBuffer == NULL)
	{
		DBGSTR("DAL_D3D::DuplicateSoundBuffer() - Failed to allocated memory for new buffer.\n");

		return (E_OUTOFMEMORY);
	}

	hr = pBuffer->InitDuplicate(lpDirectSoundBuffer, lpDuplicateSoundBuffer);
	if (Failed(hr))
	{
		DBGSTR("DAL_D3D::DuplicateSoundBuffer() - Creation of buffer failed.\n");

		delete pBuffer;

		return (hr);
	}

	if (m_fUseReflections)
	{
		VERIFY(WaitForSingleObject(m_hBufferListMutex, INFINITE) == WAIT_OBJECT_0);
	}

	m_BufferList.AddTail(pBuffer);

	if (m_fUseReflections)
		VERIFY(ReleaseMutex(m_hBufferListMutex));

	*lplpDirectSoundBuffer = (LPDIRECTSOUNDBUFFER) pBuffer;

	return (S_OK);
}

/* =============================================================
// DAL_D3D::SetCooperativeLevel()
// (RE) rtl:0x1001f9c0; dbg:0x1004f6e0
//
// Forward the cooperative level to the wrapped device.
//
// Returns:
//   DirectSound SetCooperativeLevel result
//   E_FAIL                              without a device
// =============================================================*/

STDMETHODIMP
DAL_D3D::SetCooperativeLevel(HWND hWnd, DWORD dwLevel)
{
	if (m_pDirectSound != NULL)
		return (m_pDirectSound->SetCooperativeLevel(hWnd, dwLevel));

	return (E_FAIL);
}

/* =============================================================
// DAL_D3D::Compact()
// (RE) rtl:0x1001eec0; dbg:0x1004f730
//
// Reject compaction.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D3D::Compact(void)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_D3D::GetSpeakerConfig()
// (RE) rtl:0x10020b90; dbg:0x1004f750
//
// Reject speaker-configuration queries.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D3D::GetSpeakerConfig(LPDWORD pdwConfig)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_D3D::SetSpeakerConfig()
// (RE) rtl:0x10020b90; dbg:0x1004f770
//
// Reject speaker-configuration changes.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D3D::SetSpeakerConfig(DWORD dwConfig)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_D3D::Initialize()
// (RE) rtl:0x10020b90; dbg:0x1004f790
//
// Reject DirectSound initialization.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D3D::Initialize(LPCGUID pGuid)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_D3D::InitializeEx()
// (RE) rtl:0x1001f9f0; dbg:0x1004f7b0
//
// Initialize the device with requested features and report enabled features.
//
// Returns: Init result.
// =============================================================*/

STDMETHODIMP
DAL_D3D::InitializeEx(LPGUID pGuidDevice, DWORD dwFlags, DWORD dwReserved,
		      LPDWORD lpdwFeaturesEnabled)
{
	ASSERT((lpdwFeaturesEnabled != 0 &&
	       !IsBadReadPtr(lpdwFeaturesEnabled, sizeof(DWORD))));

	return (Init(dwFlags, dwReserved, lpdwFeaturesEnabled));
}

/* =============================================================
// DAL_D3D::CreateSoundBufferEx()
// (RE) rtl:0x1001eef0; dbg:0x1004f840
//
// Reject extended buffer creation.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D3D::CreateSoundBufferEx(const DSBUFFERDESC1 *lpcDSBufferDesc,
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
// DAL_D3D::GetA3dCaps()
// (RE) rtl:0x1001fa10; dbg:0x1004f960
//
// Read A3D property capabilities with reflection overrides, or clear a 564-byte
// block and fill fallback capabilities. Writes through 0x68 require undeclared
// A3DDALCAPS fields.
//
// Returns:
//   A3D   property result for hardware
//   S_OK  fallback capabilities
// =============================================================*/

STDMETHODIMP
DAL_D3D::GetA3dCaps(LPA3DDALCAPS lpA3dCaps, LPDWORD lpdwSize)
{
HRESULT	hr;
DSCAPS	dsCaps;

	if (m_fHardware)
	{
		hr = AccessA3dProperty(A3D_D3D_GET_CAPABILITIES, TRUE, lpA3dCaps, *lpdwSize);

		if (lpA3dCaps->dwFeatureFlags & A3D_D3D_CAP_REFLECTION_VOICES)
		{
			memset(&dsCaps, 0, sizeof(dsCaps));
			dsCaps.dwSize = sizeof(dsCaps);
			VERIFY(SUCCEEDED(m_pDirectSound->GetCaps(&dsCaps)));

			lpA3dCaps->dwMaxReflections	= A3D_D3D_MAX_REFLECTIONS;
			lpA3dCaps->dwMaxSrcReflections	= A3D_MAX_SOURCE_REFLECTIONS;
			lpA3dCaps->dwReserved30		= 0;
		}

		return (hr);
	}

	memset(lpA3dCaps, 0, 0x234);

	lpA3dCaps->wDeviceType          = A3DCAPS_DEVICE_TYPE_D3D;
	lpA3dCaps->wVersion             = A3D_D3D_CAPS_VERSION;
	lpA3dCaps->dwCalcFactor         = 0x7FFF;
	lpA3dCaps->wChannelCount        = 4;
	lpA3dCaps->wChannels            = 2;
	lpA3dCaps->wRefMask             = 7;
	lpA3dCaps->dwMaxSampleRate      = 22050;
	lpA3dCaps->wMaxBuffers          = (WORD) m_dwFreeHw3DBuffers;
	lpA3dCaps->dwSamplesMask        = 1;
	lpA3dCaps->dwReserved1C         = 12;
	lpA3dCaps->dwFeatureFlags       = 0x40007;
	lpA3dCaps->dwUnknown_0x44       = 256;

	return (S_OK);
}

/* =============================================================
// DAL_D3D::GetDriverInfo()
// (RE) rtl:0x1001eef0; dbg:0x1004fbe0
//
// Reject driver-information queries.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D3D::GetDriverInfo(LPHANDLE lphA3dVxd, void **lplpIDsDriver,
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
// DAL_D3D::GetDS()
// (RE) rtl:0x10020b90; dbg:0x1004fd50
//
// Reject DirectSound-interface queries.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D3D::GetDS(LPDIRECTSOUND *lplpDirectSound)
{
	ASSERT((lplpDirectSound != 0 &&
	       !IsBadReadPtr(lplpDirectSound, sizeof(LPDIRECTSOUND))));

	return (E_NOTIMPL);
}

/* =============================================================
// DAL_D3D::GetDSDriverDesc()
// (RE) rtl:0x10003500; dbg:0x1004fdc0
//
// Reject driver-descriptor queries.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D3D::GetDSDriverDesc(void *lpsDSDriverDesc, LPDWORD lpdwSize)
{
	ASSERT((lpsDSDriverDesc != 0 &&
	       !IsBadReadPtr(lpsDSDriverDesc, sizeof(DSDRIVERDESC))));
	ASSERT((lpdwSize != 0 && !IsBadReadPtr(lpdwSize, sizeof(DWORD))));

	return (E_NOTIMPL);
}

/* =============================================================
// DAL_D3D::QueryFunctionality()
// (RE) rtl:0x10003500; dbg:0x1004fe90
//
// Reject functionality queries.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D3D::QueryFunctionality(DWORD dwFunction, LPDWORD lpdwStatus)
{
	ASSERT((lpdwStatus != 0 && !IsBadReadPtr(lpdwStatus, sizeof(DWORD))));

	return (E_NOTIMPL);
}

/* =============================================================
// DAL_D3D::Verify()
// (RE) rtl:0x10020b60; dbg:0x1004ff00
//
// Reject string verification.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D3D::Verify(LPSTR lpcString, LPSTR *lplpcStringCrypted,
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
// DAL_D3D::SetOutputMode()
// (RE) rtl:0x10020b60; dbg:0x10050020
//
// Reject output-mode changes.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D3D::SetOutputMode(DWORD dwFrontXtalkMode, DWORD dwBackXtalkMode,
		       DWORD dwQuadMode)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_D3D::GetOutputMode()
// (RE) rtl:0x10020b60; dbg:0x10050040
//
// Reject output-mode queries.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D3D::GetOutputMode(LPDWORD lpdwFrontXtalkMode, LPDWORD lpdwBackXtalkMode,
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
// DAL_D3D::SetResourceManagerMode()
// (RE) rtl:0x10020b70; dbg:0x10050160
//
// Reject resource-manager mode changes.
//
// Returns:
//   E_NOTIMPL     modes 0..3
//   E_INVALIDARG  otherwise
// =============================================================*/

STDMETHODIMP
DAL_D3D::SetResourceManagerMode(DWORD dwResourceManagerMode)
{
	ASSERT(dwResourceManagerMode <= 0x00000003);

	if (dwResourceManagerMode <= A3D_RESOURCE_MODE_LAST)
		return (E_NOTIMPL);

	return (E_INVALIDARG);
}

/* =============================================================
// DAL_D3D::GetResourceManagerMode()
// (RE) rtl:0x10020b90; dbg:0x100501d0
//
// Reject resource-manager mode queries.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D3D::GetResourceManagerMode(LPDWORD lpdwResourceManagerMode)
{
	ASSERT((lpdwResourceManagerMode != 0 &&
	       !IsBadReadPtr(lpdwResourceManagerMode, sizeof(DWORD))));

	return (E_NOTIMPL);
}

/* =============================================================
// DAL_D3D::SetHFAbsorbFactor()
// (RE) rtl:0x10020b90; dbg:0x10050240
//
// Reject absorption-factor changes.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D3D::SetHFAbsorbFactor(FLOAT fFactor)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_D3D::GetHFAbsorbFactor()
// (RE) rtl:0x10020b90; dbg:0x10050260
//
// Reject absorption-factor queries.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D3D::GetHFAbsorbFactor(FLOAT *pfFactor)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_D3D::RegisterVersion()
// (RE) rtl:0x10020b90; dbg:0x10050280
//
// Reject version registration.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D3D::RegisterVersion(DWORD dwVersion)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_D3D::GetSoftwareCaps()
// (RE) rtl:0x10020b90; dbg:0x100502a0
//
// Reject software-capability queries.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D3D::GetSoftwareCaps(LPA3DCAPS_SOFTWARE pCaps)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_D3D::GetHardwareCaps()
// (RE) rtl:0x10020b90; dbg:0x100502c0
//
// Reject hardware-capability queries.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D3D::GetHardwareCaps(LPA3DCAPS_HARDWARE pCaps)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_D3D::StartupDal()
// (RE) rtl:0x1001fb30; dbg:0x100502e0
//
// Create and play the primary 3D buffer, initialize its listener and start
// the reflection thread when enabled.
//
// Returns: S_OK, including when already started; E_FAIL on startup failure.
// =============================================================*/

STDMETHODIMP
DAL_D3D::StartupDal(void)
{
DSBUFFERDESC1	dsbd;
WAVEFORMATEX	wfx;
HRESULT		hr;

	if (m_fStarted)
		return (S_OK);

	try
	{
		memset(&dsbd, 0, sizeof(dsbd));
		dsbd.dwSize  = sizeof(dsbd);
		dsbd.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D;

		hr = m_pDirectSound->CreateSoundBuffer((LPCDSBUFFERDESC) &dsbd,
					      &m_lpDirectSound3DPrimaryBuffer, NULL);
		if (Failed(hr))
			throw ("Failed to create a new buffer from DirectSound");

		if (m_lpDirectSound3DPrimaryBuffer == NULL)
			throw ("m_lpDirectSound3DPrimaryBuffer is NULL");

		/* (RE) Original unused format; never applied to the primary buffer. */
		memset(&wfx, 0, sizeof(wfx));
		wfx.wFormatTag		= WAVE_FORMAT_PCM;
		wfx.nChannels		= 2;
		wfx.nSamplesPerSec	= 22050;
		wfx.nAvgBytesPerSec	= 88200;
		wfx.nBlockAlign		= 4;
		wfx.wBitsPerSample	= 16;

		hr = InitListener(m_lpDirectSound3DPrimaryBuffer, &m_lpDirectSound3DListener);
		if (Failed(hr))
			throw ("Init Listener failed");

		hr = m_lpDirectSound3DPrimaryBuffer->Play(0, 0, DSBPLAY_LOOPING);
		if (Failed(hr))
			throw ("Play failed on Primary 3D Buffer");

		if (m_fUseReflections)
		{
			m_hBufferListMutex = CreateMutexA(NULL, FALSE, NULL);
			if (m_hBufferListMutex == NULL)
				throw ("Failed to create Buffer List Mutex.\n");

			m_hThreadExitEvent = CreateEventA(NULL, FALSE, FALSE,
							  NULL);
			if (m_hThreadExitEvent == NULL)
				throw ("Failed to create Reflection Event");

			m_fThreadRun = 1;

			m_hThread = CreateThread(NULL, 0, ReflectionThread,
						 this, 0, &m_dwThreadId);
			if (m_hThread == NULL)
				throw ("Failed to create Reflection Timeout Thread");

			if (!SetThreadPriority(m_hThread, THREAD_PRIORITY_TIME_CRITICAL))
				throw ("Failed to set Reflection Thread Priority");
		}
	}
	/* (RE) Catch handler: dbg:0x1005058B; FuncInfo: dbg:0x10143AF8. */
	catch (const char *pszWhy)
	{
		TRACE("DAL_D3D::StartupDal() - %s.\n", pszWhy);

		if (m_lpDirectSound3DPrimaryBuffer)
		{
			m_lpDirectSound3DPrimaryBuffer->Release();
			m_lpDirectSound3DPrimaryBuffer = NULL;
		}

		ASSERT(0);

		return (E_FAIL);
	}

	m_fStarted = 1;

	return (S_OK);
}

/* =============================================================
// InitListener()
// (RE) rtl:0x1001fd40; dbg:0x10050720
//
// Query the primary buffer's listener and set default parameters.
//
// Returns:
//   SetAllParameters success result
//   E_FAIL                           on failure, releasing the listener
// =============================================================*/

static HRESULT
InitListener(IDirectSoundBuffer *pPrimary, IDirectSound3DListener **ppListener)
{
DS3DLISTENER	dsListener;
HRESULT		hr;

	try
	{
		hr = pPrimary->QueryInterface(IID_IDirectSound3DListener,
					      (void **) ppListener);
		if (Failed(hr))
			throw ("QueryInterface() for listener failed");

		if (*ppListener == NULL)
			throw ("m_lpDirectSound3DListener is NULL");

		memset(&dsListener, 0, sizeof(dsListener));
		dsListener.dwSize		= sizeof(dsListener);
		dsListener.vOrientFront.z	= 1.0f;
		dsListener.vOrientTop.y		= 1.0f;
		dsListener.flDistanceFactor	= 1.0f;
		dsListener.flDopplerFactor	= 1.0f;

		hr = (*ppListener)->SetAllParameters(&dsListener, DS3D_IMMEDIATE);
		if (Failed(hr))
			throw ("SetAllParameters() failed");
	}
	/* (RE) Catch handler: dbg:0x10050835; FuncInfo: dbg:0x10143B58. */
	catch (const char *pszWhy)
	{
		TRACE("DAL_D3D-InitListener() - %s.\n", pszWhy);

		if (*ppListener)
		{
			(*ppListener)->Release();
			*ppListener = NULL;
		}

		return (E_FAIL);
	}

	return (hr);
}

/* =============================================================
// DAL_D3D::AccessA3dProperty()
// (RE) rtl:0x1001fe40; dbg:0x10050960; thunk dbg:0x10001492
//
// Get or set an A3D super-control property through temporary DirectSound
// buffers.
//
// Returns: Last delegated HRESULT; a null interface can leave a success code
//          unchanged.
// =============================================================*/

HRESULT
DAL_D3D::AccessA3dProperty(ULONG ulId, BOOL bGet, LPVOID pPropertyData,
			   ULONG ulDataLength)
{
DSBUFFERDESC1           dsbd;
WAVEFORMATEX            wfx;
LPDIRECTSOUNDBUFFER     pPrimary;
LPDIRECTSOUNDBUFFER     pSecondary;
LPDIRECTSOUND3DBUFFER   p3dBuffer;
IKsPropertySet         *pPropSet;
ULONG                   ulSupport;
ULONG                   ulReturned;
HRESULT                 hr;

	pPrimary   = NULL;
	pSecondary = NULL;
	p3dBuffer  = NULL;
	pPropSet   = NULL;
	hr	   = S_OK;

	try
	{
		memset(&dsbd, 0, sizeof(dsbd));
		dsbd.dwSize  = sizeof(dsbd);
		dsbd.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D;

		hr = m_pDirectSound->CreateSoundBuffer((LPCDSBUFFERDESC) &dsbd, &pPrimary,
					      NULL);
		if (Failed(hr) || pPrimary == NULL)
			throw ("Unable to create primary buffer");

		memset(&wfx, 0, sizeof(wfx));
		wfx.wFormatTag		= WAVE_FORMAT_PCM;
		wfx.nChannels		= 1;
		wfx.nSamplesPerSec	= 22050;
		wfx.nAvgBytesPerSec	= 44100;
		wfx.nBlockAlign		= 2;
		wfx.wBitsPerSample	= 16;

		dsbd.lpwfxFormat = &wfx;
		dsbd.dwFlags = (dsbd.dwFlags & ~DSBCAPS_PRIMARYBUFFER) |
			       (DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLPAN |
				DSBCAPS_CTRLVOLUME | DSBCAPS_GLOBALFOCUS |
				DSBCAPS_GETCURRENTPOSITION2);
		dsbd.dwBufferBytes = A3D_D3D_PROPERTY_BUFFER_BYTES;

		hr = m_pDirectSound->CreateSoundBuffer((LPCDSBUFFERDESC) &dsbd, &pSecondary,
					      NULL);
		if (Failed(hr) || pSecondary == NULL)
			throw ("Unable to create secondary buffer");

		hr = pSecondary->QueryInterface(IID_IDirectSound3DBuffer,
						(void **) &p3dBuffer);
		if (Failed(hr) || p3dBuffer == NULL)
			throw ("Unable to retrieve 3D buffer interface");

		hr = p3dBuffer->QueryInterface(IID_IKsPropertySet,
					       (void **) &pPropSet);
		if (Failed(hr) || pPropSet == NULL)
			throw ("Couldn't get the Property Set interface");

		ulSupport = 3;
		hr = pPropSet->QuerySupport(DSPROPSETID_A3dSourceSuper, 0,
					    &ulSupport);
		if (Failed(hr))
			throw ("Device does not have support for the A3D Property Sets");

		if (bGet)
		{
			ulReturned = 0;
			hr = pPropSet->Get(DSPROPSETID_A3dSourceSuper, ulId,
					   NULL, 0, pPropertyData, ulDataLength,
					   &ulReturned);
			if (Failed(hr))
				throw ("Device does not support A3D extensions");
		}
		else
		{
			hr = pPropSet->Set(DSPROPSETID_A3dSourceSuper, ulId,
					   NULL, 0, pPropertyData, ulDataLength);
			if (Failed(hr))
				throw ("Device does not support A3D extensions");
		}
	}
	/* (RE) Catch handler: dbg:0x10050CEE; FuncInfo: dbg:0x10143BB8. */
	catch (const char *pszWhy)
	{
		TRACE("DAL_D3D::IsA3dPS() - %s.\n", pszWhy);
	}

	if (pPropSet)
	{
		pPropSet->Release();
		pPropSet = NULL;
	}

	if (p3dBuffer)
	{
		p3dBuffer->Release();
		p3dBuffer = NULL;
	}

	if (pSecondary)
	{
		pSecondary->Release();
		pSecondary = NULL;
	}

	if (pPrimary)
	{
		pPrimary->Release();
		pPrimary = NULL;
	}

	return (hr);
}

/* =============================================================
// DAL_D3D::AllocProbe()
// (RE) dbg:0x10050eb0
//
// Set the hardware flag from an A3D capabilities probe.
//
// Returns:
//   TRUE   if the property read succeeded
//   FALSE  otherwise
// =============================================================*/

BOOL
DAL_D3D::AllocProbe(void)
{
BYTE	probe[564];

	m_fHardware = 0;

	if (Succeeded(AccessA3dProperty(A3D_D3D_GET_CAPABILITIES, TRUE, probe, sizeof(probe))))
		m_fHardware = 1;

	return (m_fHardware);
}

/* =============================================================
// DAL_D3D::InitDS()
// (RE) rtl:0x100200f0; dbg:0x10050f20
//
// Create and initialize DirectSound, requiring free hardware 3D static buffers.
// The caller must pass &m_pDirectSound; original failure cleanup releases that
// member.
//
// Returns:
//   S_OK
//   E_FAIL  on creation, initialization or capability failure
// =============================================================*/

HRESULT
DAL_D3D::InitDS(IDirectSound **lplpDS, REFCLSID rclsid)
{
DSCAPS	dsCaps;
HRESULT	hr;

	try
	{
		hr = CoCreateInstance(rclsid, NULL, CLSCTX_INPROC_SERVER,
				      IID_IDirectSound, (void **) lplpDS);
		if (Failed(hr))
			throw ("CoCreateInstance() for DirectSound failed");

		if (*lplpDS == NULL)
			throw ("*lplpDS is NULL");

		hr = (*lplpDS)->Initialize(NULL);
		if (Failed(hr))
			throw ("Could not Initialize() DirectSound");

		memset(&dsCaps, 0, sizeof(dsCaps));
		dsCaps.dwSize = sizeof(dsCaps);

		hr = (*lplpDS)->GetCaps(&dsCaps);
		if (Failed(hr))
			throw ("GetCaps() failed");

		if (!dsCaps.dwFreeHw3DStaticBuffers)
			throw ("Device does not have support for hardware DS3D buffers");
	}
	/* (RE) Catch handler: dbg:0x100510AD; FuncInfo: dbg:0x10143C18. */
	catch (const char *pszWhy)
	{
		TRACE("DAL_D3D::InitDS() - %s.\n", pszWhy);

		if (m_pDirectSound)
		{
			m_pDirectSound->Release();
			m_pDirectSound = NULL;
		}

		return (E_FAIL);
	}

	return (S_OK);
}

/* =============================================================
// DAL_D3D::Init()
// (RE) rtl:0x10020240; dbg:0x100511b0
//
// Find an A3D or DS3D device, acquire features and report reflection support.
// The try block still extends beyond the original device-check/GetCaps section
// (RE) dbg:0x10051323..0x1005143C; rtl:0x10020352..0x100203D0.
//
// Returns:
//   S_OK
//   E_FAIL  when device selection or initialization fails
// =============================================================*/

HRESULT
DAL_D3D::Init(DWORD dwFeatures, DWORD dwReserved, LPDWORD lpdwAvailable)
{
DSCAPS		dsCaps;
BYTE		a3dCaps[564];
DWORD		cbA3dCaps;
DWORD		adwResource[3];
BOOL		fFoundA3d;
BOOL		fFoundDs3d;
HRESULT		hr;

	try
	{
		m_dwFeatures = dwFeatures;

		fFoundA3d  = FALSE;
		fFoundDs3d = FALSE;
		*lpdwAvailable = 0;

		hr = InitDS(&m_pDirectSound, CLSID_A3dDevice);
		if (Succeeded(hr))
		{
			fFoundA3d = AllocProbe();

			if (fFoundA3d)
			{
				adwResource[0] = dwFeatures;
				adwResource[1] = 0;
				adwResource[2] = 0;
				AccessA3dProperty(A3D_D3D_ACQUIRE_FEATURES, TRUE, adwResource,
						  sizeof(adwResource));
				m_dwEnabledFeatures     = adwResource[1];
				m_dwPropertyReleaseData = adwResource[2];
				*lpdwAvailable = m_dwEnabledFeatures;
			}
			else
			{
				if (m_pDirectSound)
				{
					m_pDirectSound->Release();
					m_pDirectSound = NULL;
				}

				if (m_fRequireA3d)
					return (E_FAIL);
			}
		}

		if (!fFoundA3d && !(dwReserved & A3D_D3D_SKIP_CHECKED_DS3D_DEVICE))
		{
			if (A3dCheckDriverVersion())
			{
				if (Succeeded(InitDS(&m_pDirectSound, CLSID_Ds3dDevice)))
					fFoundA3d = TRUE;
			}
		}

		if (!fFoundA3d && !(dwReserved & A3D_D3D_SKIP_A3D_DEVICE_FALLBACK))
			fFoundDs3d = Succeeded(InitDS(&m_pDirectSound, CLSID_A3dDevice));

		if (!fFoundDs3d && !fFoundA3d)
			throw ("Couldn't find an A3D or DS3D device");

		memset(&dsCaps, 0, sizeof(dsCaps));
		dsCaps.dwSize = sizeof(dsCaps);

		hr = m_pDirectSound->GetCaps(&dsCaps);
		if (Failed(hr))
			throw ("GetCaps() failed");

		m_dwFreeHw3DBuffers = dsCaps.dwFreeHw3DStaticBuffers;

		cbA3dCaps = sizeof(a3dCaps);
		GetA3dCaps((LPA3DDALCAPS) a3dCaps, &cbA3dCaps);

		if ((dwFeatures & A3D_1ST_REFLECTIONS) &&
		    (((A3DDALCAPS *) a3dCaps)->dwFeatureFlags & A3D_D3D_CAP_REFLECTION_VOICES))
		{
			m_fUseReflections = 1;
			*lpdwAvailable |= A3D_1ST_REFLECTIONS;
		}
	}
	/* (RE) Catch handler: dbg:0x100513D7; FuncInfo: dbg:0x10143C78. */
	catch (const char *pszWhy)
	{
		TRACE("DAL_D3D::Init - %s.\n", pszWhy);

		if (m_pDirectSound)
		{
			m_pDirectSound->Release();
			m_pDirectSound = NULL;
		}

		return (E_FAIL);
	}

	return (S_OK);
}

/* =============================================================
// DAL_D3D::GetPrimaryBuffer()
// (RE) dbg:0x10051580
//
// Return an AddRef'd primary buffer. The unreachable splash guard's source
// expression is unknown.
//
// Returns:
//   S_OK
//   E_FAIL  without a primary buffer
// =============================================================*/

HRESULT
DAL_D3D::GetPrimaryBuffer(const DSBUFFERDESC1 *pDesc,
			  LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
			  LPUNKNOWN pUnkOuter)
{
	if (m_lpDirectSound3DPrimaryBuffer == NULL)
		return (E_FAIL);

	m_lpDirectSound3DPrimaryBuffer->AddRef();
	*lplpDirectSoundBuffer = m_lpDirectSound3DPrimaryBuffer;

	return (S_OK);
}

/* =============================================================
// DAL_D3D::CreateSecondaryBuffer()
// (RE) rtl:0x10020450; dbg:0x10051600
//
// Create a hardware DS3D buffer and list its D3DBuffer wrapper.
//
// Returns: S_OK; E_FAIL without a device; E_OUTOFMEMORY on wrapper allocation
//          failure; the DirectSound CreateSoundBuffer or voice Init failure
//          otherwise. A null DirectSound output preserves its returned HRESULT.
// =============================================================*/

HRESULT
DAL_D3D::CreateSecondaryBuffer(const DSBUFFERDESC1 *pDesc, LPBYTE lpWave,
			       LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
			       LPUNKNOWN pUnkOuter)
{
D3DBuffer              *pBuffer;
LPDIRECTSOUNDBUFFER     lpDirectSoundBuffer;
DSBUFFERDESC1           dsbd;
HRESULT                 hr;

	*lplpDirectSoundBuffer = NULL;
	lpDirectSoundBuffer = NULL;

	if (m_pDirectSound == NULL)
	{
		DBGSTR("DAL_D3D::CreateSecondaryBuffer() - No DirectSound interface.\n");

		return (E_FAIL);
	}

	dsbd = *pDesc;
	dsbd.dwFlags = DSBCAPS_STATIC | DSBCAPS_LOCHARDWARE | DSBCAPS_CTRL3D |
		       DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLPAN |
		       DSBCAPS_CTRLVOLUME | DSBCAPS_GLOBALFOCUS |
		       DSBCAPS_GETCURRENTPOSITION2;

	hr = m_pDirectSound->CreateSoundBuffer((LPCDSBUFFERDESC) &dsbd,
				      &lpDirectSoundBuffer, NULL);
	if (Failed(hr))
	{
		DBGSTR("DAL_D3D::CreateSecondaryBuffer() - Failed to create a new buffer from DirectSound.\n");

		return (hr);
	}

	if (lpDirectSoundBuffer == NULL)
	{
		DBGSTR("DAL_D3D::CreateSecondaryBuffer() - lpDirectSoundBuffer is NULL.\n");

		return (hr);
	}

	pBuffer = new D3DBuffer;
	if (pBuffer == NULL)
	{
		DBGSTR("DAL_D3D::CreateSecondaryBuffer() - Failed to allocated memory for new buffer.\n");

		lpDirectSoundBuffer->Release();

		return (E_OUTOFMEMORY);
	}

	hr = pBuffer->Init(lpDirectSoundBuffer, pDesc, &m_BufferList,
			   m_hBufferListMutex, this);
	if (Failed(hr))
	{
		DBGSTR("DAL_D3D::CreateSecondaryBuffer() - Creation of 3D buffer failed.\n");

		delete pBuffer;
		lpDirectSoundBuffer->Release();

		return (hr);
	}

	lpDirectSoundBuffer->Release();

	if (m_fUseReflections)
		VERIFY(WaitForSingleObject(m_hBufferListMutex,
					   INFINITE) == WAIT_OBJECT_0);

	m_BufferList.AddTail(pBuffer);

	if (m_fUseReflections)
		VERIFY(ReleaseMutex(m_hBufferListMutex));

	*lplpDirectSoundBuffer = (LPDIRECTSOUNDBUFFER) pBuffer;

	return (S_OK);
}

/* =============================================================
// ReflectionThread()
// (RE) rtl:0x10020640; dbg:0x100519c0; thunk dbg:0x10001a32
//
// Age reflections within a budget of 5 per 9 ms tick until stopped,
// then signal the exit event.
//
// Returns: 0.
// =============================================================*/

static DWORD WINAPI
ReflectionThread(LPVOID pv)
{
DAL_D3D        *pDal;
D3DBuffer      *pBuffer;
HANDLE          hTimeoutEvent;
POSITION        pos;
DWORD           dwBudget;
DWORD           dwAged;

	pDal = (DAL_D3D *) pv;

	VERIFY((hTimeoutEvent = CreateEventA(NULL, FALSE, FALSE, NULL)) != 0);

	while (pDal->m_fThreadRun)
	{
		WaitForSingleObject(hTimeoutEvent, A3D_D3D_REFLECTION_WAIT_MS);

		dwBudget = A3D_D3D_REFLECTION_AGE_BUDGET;

		VERIFY(WaitForSingleObject(pDal->m_hBufferListMutex, INFINITE) ==
		       WAIT_OBJECT_0);

		pos = pDal->m_BufferList.GetHeadPosition();

		while (pos != NULL)
		{
			pBuffer = (D3DBuffer *) pDal->m_BufferList.GetNext(pos);

			ASSERT((pBuffer != 0 &&
			       !IsBadReadPtr(pBuffer, sizeof(D3DBuffer))));

			if (!pBuffer->m_fReflectionsAged)
			{
				dwAged = 0;
				pBuffer->m_fReflectionsAged =
					pBuffer->AgeReflectionVoices(dwBudget, &dwAged);
				dwBudget -= dwAged;
			}

			if (!dwBudget)
			{
				while (pos != NULL)
				{
					pBuffer = (D3DBuffer *) pDal->m_BufferList.GetNext(pos);

					ASSERT((pBuffer != 0 &&
					       !IsBadReadPtr(pBuffer, sizeof(D3DBuffer))));

					pBuffer->AdvanceReflectionAge();
				}

				break;
			}

			if (pos == NULL)
			{
				pos = pDal->m_BufferList.GetHeadPosition();

				while (pos != NULL)
				{
					pBuffer = (D3DBuffer *) pDal->m_BufferList.GetNext(pos);

					ASSERT((pBuffer != 0 &&
					       !IsBadReadPtr(pBuffer, sizeof(D3DBuffer))));

					pBuffer->m_fReflectionsAged = 0;
				}
			}
		}

		VERIFY(ReleaseMutex(pDal->m_hBufferListMutex));
	}

	VERIFY(SetEvent(pDal->m_hThreadExitEvent));

	return (0);
}

/* =============================================================
// DAL_D3D::CreateReflection()
// (RE) rtl:0x1001f7a0; dbg:0x1004f110
//
// Duplicate a source as a reflection voice and return the same wrapper
// through both outputs.
//
// Returns: S_OK; E_OUTOFMEMORY on wrapper allocation failure; E_FAIL on
//          property setup failure; the DirectSound DuplicateSoundBuffer or
//          InitDuplicate failure otherwise.
// =============================================================*/

HRESULT
DAL_D3D::CreateReflection(LPDIRECTSOUNDBUFFER lpDirectSoundBuffer, D3DBuffer **ppReflectionDsb,
			  D3DBuffer **ppReflectionDal)
{
LPDIRECTSOUNDBUFFER     lpDuplicateSoundBuffer;
LPDIRECTSOUND3DBUFFER   p3dBuffer;
IKsPropertySet         *pPropSet;
D3DBuffer              *pBuffer;
ULONG                   ulSupport;
HRESULT                 hr;

	ASSERT((lpDirectSoundBuffer != 0 &&
	       !IsBadReadPtr(lpDirectSoundBuffer, sizeof(IDirectSoundBuffer))));

	*ppReflectionDsb = NULL;

	lpDuplicateSoundBuffer = NULL;
	hr = m_pDirectSound->DuplicateSoundBuffer(
			((D3DBuffer *) lpDirectSoundBuffer)->m_lpDirectSoundBuffer,
			&lpDuplicateSoundBuffer);
	if (Failed(hr))
	{
		TRACE("DAL_D3D::CreateReflection() - Failed to duplicate buffer from DS. %x\n", hr);
#ifdef _DEBUG
		g_lpResMan->DumpCounts(1);
#endif

		return (hr);
	}

	p3dBuffer = NULL;
	pPropSet  = NULL;

	try
	{
		if (Failed(lpDuplicateSoundBuffer->QueryInterface(
				IID_IDirectSound3DBuffer, (void **) &p3dBuffer)))
			throw ("Failed to query for DS3D buffer\n");

		if (Failed(p3dBuffer->QueryInterface(IID_IKsPropertySet,
						     (void **) &pPropSet)))
			throw ("Failed to get Prop Set interface\n");

		ulSupport = 0;
		if (Failed(pPropSet->QuerySupport(DSPROPSETID_A3dSourceSuper, DSPROPERTY_A3DBUFFER_SET_REFLECTION,
						  &ulSupport)))
			throw ("Failed to support for DSPROPERTY_A3DBUFFER_SET_REFLECTION\n");

		if (ulSupport & KSPROPERTY_SUPPORT_SET)
		{
			if (Failed(pPropSet->Set(DSPROPSETID_A3dSourceSuper, DSPROPERTY_A3DBUFFER_SET_REFLECTION,
						 NULL, 0, NULL, 0)))
				throw ("Failed to set DSPROPERTY_A3DBUFFER_SET_REFLECTION \n");
		}
	}
	/* (RE) Catch handler: dbg:0x1004F3A9; FuncInfo: dbg:0x10143A88. */
	catch (const char *pszWhy)
	{
		TRACE("DAL_D3D::CreateReflection() - %s\n", pszWhy);
#ifdef _DEBUG
		g_lpResMan->DumpCounts(1);
#endif

		if (p3dBuffer != NULL)
		{
			p3dBuffer->Release();
			p3dBuffer = NULL;
		}

		if (pPropSet != NULL)
		{
			pPropSet->Release();
			pPropSet = NULL;
		}

		return (E_FAIL);
	}

	if (p3dBuffer != NULL)
	{
		p3dBuffer->Release();
		p3dBuffer = NULL;
	}

	if (pPropSet != NULL)
	{
		pPropSet->Release();
		pPropSet = NULL;
	}

	ASSERT((lpDuplicateSoundBuffer != 0 &&
	       !IsBadReadPtr(lpDuplicateSoundBuffer, sizeof(IDirectSoundBuffer))));

	pBuffer = new D3DBuffer;
	if (pBuffer == NULL)
	{
		DBGSTR("DAL_D3D::DuplicateSoundBuffer() - Failed to allocated memory for new buffer.\n");

		return (E_OUTOFMEMORY);
	}

	pBuffer->m_bIsReflection = 1;

	hr = pBuffer->InitDuplicate(lpDirectSoundBuffer, lpDuplicateSoundBuffer);
	if (Failed(hr))
	{
		DBGSTR("DAL_D3D::DuplicateSoundBuffer() - Creation of buffer failed.\n");

		delete pBuffer;

		return (hr);
	}

	*ppReflectionDsb = pBuffer;
	*ppReflectionDal = pBuffer;

	return (S_OK);
}
