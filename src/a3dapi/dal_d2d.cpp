/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * dal_d2d.cpp
 *
 * Implements DAL_D2D, the device adapter that renders through ordinary
 * DirectSound buffers. Initialization acquires the default DirectSound
 * device, and buffer creation or duplication wraps its secondary buffers
 * in D2DBuffer objects.
 *
 * The device reports capabilities and forwards cooperative-level changes
 * while retaining the list of allocated voices. D2DBuffer translates A3D
 * source controls into frequency, volume and pan; the resource manager
 * selects and assigns voices through the DAL interfaces.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "dal_d2d.h"
#include "d2dbuffer.h"

/* Reported mixing capacity  */
#define A3D_D2D_MAX_MIXING_BUFFERS 128

static HRESULT	CreatePrimaryBuffer(const DSBUFFERDESC1 *pDesc,
				    LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
				    LPUNKNOWN pUnkOuter);

/* =============================================================
// DAL_D2D::DAL_D2D()
// (RE) dbg:0x1004c6f0
//
// Initialize an empty DirectSound adapter.
// =============================================================*/

DAL_D2D::DAL_D2D(void)
{
	m_cRef = 1;
	m_pDS  = NULL;
}

/* =============================================================
// DAL_D2D::~DAL_D2D()
// (RE) dbg:0x1004c780
//
// Delete allocated voices and release the DirectSound device.
// =============================================================*/

DAL_D2D::~DAL_D2D(void)
{
D2DBuffer	*pBuffer;

	while ((pBuffer = (D2DBuffer *) m_BufferList.RemoveHead()) != NULL)
		delete pBuffer;

	if (m_pDS != NULL)
	{
		m_pDS->Release();
		m_pDS = NULL;
	}
}

/* =============================================================
// DAL_D2D::QueryInterface()
// (RE) rtl:0x10020920; dbg:0x1004c890
//
// Obtain a referenced device interface.
//
// Returns:
//   S_OK
//   E_INVALIDARG   a null output
//   E_NOINTERFACE  an unknown IID
// =============================================================*/

STDMETHODIMP
DAL_D2D::QueryInterface(REFIID riid, void **ppv)
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
// DAL_D2D::AddRef()
// (RE) rtl:0x10017f50; dbg:0x1004c9d0
//
// Increment the reference count.
//
// Returns: Reference count read after the increment.
// =============================================================*/

STDMETHODIMP_(ULONG)
DAL_D2D::AddRef(void)
{
	InterlockedIncrement(&m_cRef);

	return ((ULONG) m_cRef);
}

/* =============================================================
// DAL_D2D::Release()
// (RE) rtl:0x1001ec80; dbg:0x1004ca00
//
// Release a reference and delete the device at zero.
// The original rereads the count after the interlocked decrement.
//
// Returns: Remaining reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
DAL_D2D::Release(void)
{
	InterlockedDecrement(&m_cRef);

	if (m_cRef != 0)
		return ((ULONG) m_cRef);

	delete this;

	return (0);
}

/* =============================================================
// DAL_D2D::CreateSoundBuffer()
// (RE) rtl:0x1001ecc0; dbg:0x1004cac0
//
// Dispatch primary and secondary buffer creation.
//
// Returns:
//   Creation result
//   E_INVALIDARG     aggregation
// =============================================================*/

STDMETHODIMP
DAL_D2D::CreateSoundBuffer(LPCDSBUFFERDESC lpcDSBufferDesc,
			   LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
			   LPUNKNOWN pUnkOuter)
{
	ASSERT((lpcDSBufferDesc != 0 &&
	       !IsBadReadPtr(lpcDSBufferDesc, sizeof(DSBUFFERDESC))));
	ASSERT((lplpDirectSoundBuffer != 0 &&
	       !IsBadReadPtr(lplpDirectSoundBuffer, sizeof(LPDIRECTSOUNDBUFFER))));

	if (pUnkOuter != NULL)
	{
		DBGSTR("DAL_D2D::CreateSoundBuffer() - Aggregation not supported.\n");

		return (E_INVALIDARG);
	}

	if (lpcDSBufferDesc->dwFlags & DSBCAPS_PRIMARYBUFFER)
		return (CreatePrimaryBuffer((const DSBUFFERDESC1 *) lpcDSBufferDesc,
					    lplpDirectSoundBuffer, pUnkOuter));

	return (CreateSecondaryBuffer((const DSBUFFERDESC1 *) lpcDSBufferDesc,
				      NULL, lplpDirectSoundBuffer, pUnkOuter));
}

/* =============================================================
// DAL_D2D::GetCaps()
// (RE) rtl:0x1001ed00; dbg:0x1004cc10
//
// Read DirectSound capabilities and report a 128-buffer mixing limit.
//
// Returns: S_OK; E_FAIL without a device; otherwise the DirectSound GetCaps
//          failure.
// =============================================================*/

STDMETHODIMP
DAL_D2D::GetCaps(LPDSCAPS lpDirectSoundCaps)
{
	HRESULT	hr;

	ASSERT((lpDirectSoundCaps != 0 &&
	       !IsBadReadPtr(lpDirectSoundCaps, sizeof(DSCAPS))));

	if (m_pDS != NULL)
	{
		hr = m_pDS->GetCaps(lpDirectSoundCaps);

		if (FAILED(hr))
		{
			DBGSTR("DAL_D2D::GetCaps() - GetCaps() failed.\n");

			return (hr);
		}

		lpDirectSoundCaps->dwMaxHwMixingAllBuffers = A3D_D2D_MAX_MIXING_BUFFERS;

		return (S_OK);
	}

	DBGSTR("DAL_D2D::GetCaps() - No direct sound interface\n");

	return (E_FAIL);
}

/* =============================================================
// DAL_D2D::DuplicateSoundBuffer()
// (RE) rtl:0x1001ed40; dbg:0x1004cd30
//
// Duplicate a wrapped buffer and list its new wrapper.
// The original leaks the duplicate if wrapper allocation fails.
//
// Returns: S_OK; E_OUTOFMEMORY for wrapper allocation failure; otherwise the
//          DirectSound DuplicateSoundBuffer or InitDuplicate failure.
// =============================================================*/

STDMETHODIMP
DAL_D2D::DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER lpDirectSoundBuffer,
			      LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer)
{
D2DBuffer              *pBuffer;
LPDIRECTSOUNDBUFFER     lpDuplicateSoundBuffer;
HRESULT                 hr;

	ASSERT((lpDirectSoundBuffer != 0 &&
	       !IsBadReadPtr(lpDirectSoundBuffer, sizeof(IDirectSoundBuffer))));
	ASSERT((lplpDirectSoundBuffer != 0 &&
	       !IsBadReadPtr(lplpDirectSoundBuffer, sizeof(LPDIRECTSOUNDBUFFER))));

	*lplpDirectSoundBuffer = NULL;

	lpDuplicateSoundBuffer = NULL;
	hr = m_pDS->DuplicateSoundBuffer(
			((D2DBuffer *) lpDirectSoundBuffer)->m_lpDirectSoundBuffer,
			&lpDuplicateSoundBuffer);
	if (FAILED(hr))
	{
		DBGSTR("DAL_D2D::DuplicateSoundBuffer() - Failed to duplicate buffer from DS.\n");

		return (hr);
	}

	ASSERT((lpDuplicateSoundBuffer != 0 &&
	       !IsBadReadPtr(lpDuplicateSoundBuffer, sizeof(IDirectSoundBuffer))));

	pBuffer = new D2DBuffer;
	if (pBuffer == NULL)
	{
		DBGSTR("DAL_D2D::DuplicateSoundBuffer() - Failed to allocated memory for new buffer.\n");

		return (E_OUTOFMEMORY);
	}

	hr = pBuffer->InitDuplicate(lpDirectSoundBuffer, lpDuplicateSoundBuffer);
	if (FAILED(hr))
	{
		DBGSTR("DAL_D2D::DuplicateSoundBuffer() - Creation of buffer failed.\n");

		delete pBuffer;
		return (hr);
	}

	m_BufferList.AddTail(pBuffer);
	*lplpDirectSoundBuffer = (LPDIRECTSOUNDBUFFER) pBuffer;

	return (S_OK);
}

/* =============================================================
// DAL_D2D::SetCooperativeLevel()
// (RE) rtl:0x1001ee90; dbg:0x1004d0b0
//
// Set the wrapped device cooperative level.
//
// Returns:
//   DirectSound SetCooperativeLevel result
//   E_FAIL                              without a device
// =============================================================*/

STDMETHODIMP
DAL_D2D::SetCooperativeLevel(HWND hWnd, DWORD dwLevel)
{
	if (m_pDS != NULL)
		return (m_pDS->SetCooperativeLevel(hWnd, dwLevel));

	return (E_FAIL);
}

/* =============================================================
// DAL_D2D::Compact()
// (RE) rtl:0x1001eec0; dbg:0x1004d100
//
// Leave device memory unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D2D::Compact(void)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_D2D::GetSpeakerConfig()
// (RE) rtl:0x10020b90; dbg:0x1004d120
//
// Leave the speaker configuration output unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D2D::GetSpeakerConfig(LPDWORD pdwConfig)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_D2D::SetSpeakerConfig()
// (RE) rtl:0x10020b90; dbg:0x1004d140
//
// Leave the speaker configuration unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D2D::SetSpeakerConfig(DWORD dwConfig)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_D2D::Initialize()
// (RE) rtl:0x10020b90; dbg:0x1004d160
//
// Leave the device unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D2D::Initialize(LPCGUID pGuid)
{
	return (E_NOTIMPL);
}

/* =============================================================
// CreatePrimaryBuffer()
// (RE) dbg:0x1004dbe0
//
// Leave the primary buffer output unchanged.
//
// Returns: S_OK.
// =============================================================*/

static HRESULT
CreatePrimaryBuffer(const DSBUFFERDESC1 *pDesc,
		    LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
		    LPUNKNOWN pUnkOuter)
{
	return (S_OK);
}

/* =============================================================
// DAL_D2D::InitializeEx()
// (RE) rtl:0x1001eed0; dbg:0x1004d180
//
// Report no enabled features and initialize the default DirectSound device.
//
// Returns: Init result.
// =============================================================*/

STDMETHODIMP
DAL_D2D::InitializeEx(LPGUID pGuidDevice, DWORD dwFlags, DWORD dwReserved,
		      LPDWORD lpdwFeaturesEnabled)
{
	ASSERT((lpdwFeaturesEnabled != 0 &&
	       !IsBadReadPtr(lpdwFeaturesEnabled, sizeof(DWORD))));

	*lpdwFeaturesEnabled = 0;

	return (Init());
}

/* =============================================================
// DAL_D2D::CreateSoundBufferEx()
// (RE) rtl:0x1001eef0; dbg:0x1004d210
//
// Leave the buffer output unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D2D::CreateSoundBufferEx(const DSBUFFERDESC1 *lpcDSBufferDesc,
			     LPBYTE lpbWave,
			     LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
			     LPUNKNOWN pUnkOuter)
{
	ASSERT((lpcDSBufferDesc != 0 &&
	       !IsBadReadPtr(lpcDSBufferDesc, sizeof(DSBUFFERDESC))));
	ASSERT((lpbWave != 0 &&
	       !IsBadReadPtr(lpbWave, sizeof(BYTE))));
	ASSERT((lplpDirectSoundBuffer != 0 &&
	       !IsBadReadPtr(lplpDirectSoundBuffer, sizeof(LPDIRECTSOUNDBUFFER))));

	return (E_NOTIMPL);
}

/* =============================================================
// DAL_D2D::GetA3dCaps()
// (RE) rtl:0x10003500; dbg:0x1004d330
//
// Leave capability outputs unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D2D::GetA3dCaps(LPA3DDALCAPS lpA3dCaps, LPDWORD lpdwSize)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_D2D::GetDriverInfo()
// (RE) rtl:0x1001eef0; dbg:0x1004d350
//
// Leave driver information outputs unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D2D::GetDriverInfo(LPHANDLE lphA3dVxd, void **lplpIDsDriver,
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
// DAL_D2D::GetDS()
// (RE) rtl:0x10020b90; dbg:0x1004d4c0
//
// Leave the DirectSound output unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D2D::GetDS(LPDIRECTSOUND *lplpDirectSound)
{
	ASSERT((lplpDirectSound != 0 &&
	       !IsBadReadPtr(lplpDirectSound, sizeof(LPDIRECTSOUND))));

	return (E_NOTIMPL);
}

/* =============================================================
// DAL_D2D::GetDSDriverDesc()
// (RE) rtl:0x10003500; dbg:0x1004d530
//
// Leave driver descriptor outputs unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D2D::GetDSDriverDesc(void *lpsDSDriverDesc, LPDWORD lpdwSize)
{
	ASSERT((lpsDSDriverDesc != 0 &&
	       !IsBadReadPtr(lpsDSDriverDesc, sizeof(DSDRIVERDESC))));
	ASSERT((lpdwSize != 0 && !IsBadReadPtr(lpdwSize, sizeof(DWORD))));

	return (E_NOTIMPL);
}

/* =============================================================
// DAL_D2D::QueryFunctionality()
// (RE) rtl:0x10003500; dbg:0x1004d600
//
// Leave the functionality output unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D2D::QueryFunctionality(DWORD dwFunction, LPDWORD lpdwStatus)
{
	ASSERT((lpdwStatus != 0 && !IsBadReadPtr(lpdwStatus, sizeof(DWORD))));

	return (E_NOTIMPL);
}

/* =============================================================
// DAL_D2D::Verify()
// (RE) rtl:0x10020b60; dbg:0x1004d670
//
// Leave verification outputs unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D2D::Verify(LPSTR lpcString, LPSTR *lplpcStringCrypted,
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
// DAL_D2D::SetOutputMode()
// (RE) rtl:0x10020b60; dbg:0x1004d790
//
// Leave the output mode unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D2D::SetOutputMode(DWORD dwFrontXtalkMode, DWORD dwBackXtalkMode,
		       DWORD dwQuadMode)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_D2D::GetOutputMode()
// (RE) rtl:0x10020b60; dbg:0x1004d7b0
//
// Leave output mode outputs unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D2D::GetOutputMode(LPDWORD lpdwFrontXtalkMode, LPDWORD lpdwBackXtalkMode,
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
// DAL_D2D::SetResourceManagerMode()
// (RE) rtl:0x10020b70; dbg:0x1004d8d0
//
// Reject the requested resource manager mode.
//
// Returns:
//   E_NOTIMPL     modes 0 through 3
//   E_INVALIDARG  otherwise
// =============================================================*/

STDMETHODIMP
DAL_D2D::SetResourceManagerMode(DWORD dwResourceManagerMode)
{
	ASSERT(dwResourceManagerMode <= 0x00000003);

	if (dwResourceManagerMode <= A3D_RESOURCE_MODE_LAST)
		return (E_NOTIMPL);

	return (E_INVALIDARG);
}

/* =============================================================
// DAL_D2D::GetResourceManagerMode()
// (RE) rtl:0x10020b90; dbg:0x1004d940
//
// Leave the resource manager mode output unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D2D::GetResourceManagerMode(LPDWORD lpdwResourceManagerMode)
{
	ASSERT((lpdwResourceManagerMode != 0 &&
	       !IsBadReadPtr(lpdwResourceManagerMode, sizeof(DWORD))));

	return (E_NOTIMPL);
}

/* =============================================================
// DAL_D2D::SetHFAbsorbFactor()
// (RE) rtl:0x10020b90; dbg:0x1004d9b0
//
// Leave the absorption factor unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D2D::SetHFAbsorbFactor(FLOAT fFactor)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_D2D::GetHFAbsorbFactor()
// (RE) rtl:0x10020b90; dbg:0x1004d9d0
//
// Leave the absorption factor output unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D2D::GetHFAbsorbFactor(FLOAT *pfFactor)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_D2D::RegisterVersion()
// (RE) rtl:0x10020b90; dbg:0x1004d9f0
//
// Leave the registered version unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D2D::RegisterVersion(DWORD dwVersion)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_D2D::GetSoftwareCaps()
// (RE) rtl:0x10020b90; dbg:0x1004da10
//
// Leave software capability outputs unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D2D::GetSoftwareCaps(LPA3DCAPS_SOFTWARE pCaps)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_D2D::GetHardwareCaps()
// (RE) rtl:0x10020b90; dbg:0x1004da30
//
// Leave hardware capability outputs unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
DAL_D2D::GetHardwareCaps(LPA3DCAPS_HARDWARE pCaps)
{
	return (E_NOTIMPL);
}

/* =============================================================
// DAL_D2D::Init()
// (RE) rtl:0x1001ef00; dbg:0x1004da50
//
// Create and initialize the default DirectSound device.
//
// Returns:
//   S_OK
//   E_FAIL  if creation or initialization fails
// =============================================================*/

HRESULT
DAL_D2D::Init(void)
{
HRESULT		hr;

	try
	{
		hr = CoCreateInstance(CLSID_DirectSound, NULL,
				      CLSCTX_INPROC_SERVER, IID_IDirectSound,
				      (void **) &m_pDS);
		if (FAILED(hr))
			throw ("DAL_D2D::Init() - Could not CoCreate for DS\n");

		if (m_pDS == NULL)
			throw ("DAL_D2D::Init() - pDS is NULL\n");

		hr = m_pDS->Initialize(NULL);
		if (FAILED(hr))
			throw ("DAL_D2D::Init() - Could not init DS\n");
	}
	/* (RE) Catch handler: dbg:0x1004DB2B. */
	catch (const char *pszWhy)
	{
		DBGSTR(pszWhy);

		if (m_pDS != NULL)
		{
			m_pDS->Release();
			m_pDS = NULL;
		}

		return (E_FAIL);
	}

	return (S_OK);
}

/* =============================================================
// DAL_D2D::CreateSecondaryBuffer()
// (RE) rtl:0x1001efe0; dbg:0x1004dc00
//
// Create a DirectSound buffer with global focus and frequency, pan and volume
// controls, then list its D2DBuffer wrapper.
//
// Returns: S_OK; E_FAIL without a device; E_OUTOFMEMORY for wrapper allocation
//          failure; otherwise the DirectSound CreateSoundBuffer or buffer Init
//          result.
// =============================================================*/

HRESULT
DAL_D2D::CreateSecondaryBuffer(const DSBUFFERDESC1 *pDesc, LPBYTE lpWave,
			       LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
			       LPUNKNOWN pUnkOuter)
{
D2DBuffer              *pBuffer;
LPDIRECTSOUNDBUFFER     lpDirectSoundBuffer;
DSBUFFERDESC1           dsbd;
HRESULT                 hr;

	*lplpDirectSoundBuffer = NULL;

	lpDirectSoundBuffer = NULL;

	if (m_pDS == NULL)
	{
		DBGSTR("DAL_D2D::CreateSecondaryBuffer() - No direct sound interface\n");

		return (E_FAIL);
	}

	dsbd = *pDesc;
	dsbd.dwFlags = DSBCAPS_GLOBALFOCUS | DSBCAPS_CTRLFREQUENCY |
		       DSBCAPS_CTRLPAN | DSBCAPS_CTRLVOLUME;

	hr = m_pDS->CreateSoundBuffer((LPCDSBUFFERDESC) &dsbd,
				      &lpDirectSoundBuffer, NULL);
	if (FAILED(hr))
	{
		DBGSTR("DAL_D2D::CreateSecondaryBuffer() - Failed to create a new buffer from DS.\n");

		return (hr);
	}

	if (lpDirectSoundBuffer == NULL)
	{
		DBGSTR("DAL_D2D::CreateSecondaryBuffer() - lpDirectSoundBuffer is NULL.\n");

		return (hr);
	}

	pBuffer = new D2DBuffer;
	if (pBuffer == NULL)
	{
		DBGSTR("DAL_D2D::CreateSecondaryBuffer() - Failed to allocated memory for new buffer.\n");

		lpDirectSoundBuffer->Release();

		return (E_OUTOFMEMORY);
	}

	hr = pBuffer->Init(lpDirectSoundBuffer, pDesc, &m_BufferList);
	if (FAILED(hr))
	{
		DBGSTR("DAL_D2D::Creation of 3d buffer failed.\n");

		delete pBuffer;
		lpDirectSoundBuffer->Release();

		return (hr);
	}

	lpDirectSoundBuffer->Release();

	m_BufferList.AddTail(pBuffer);
	*lplpDirectSoundBuffer = (LPDIRECTSOUNDBUFFER) pBuffer;

	return (S_OK);
}
