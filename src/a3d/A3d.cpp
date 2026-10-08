/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3d.cpp
 *
 * Implements CA3d, the A3D 1.x compatibility object returned by this
 * DLL's two coclasses. It activates the main A3D API through COM, exposes
 * the older A3D and DirectSound interfaces, and initializes the
 * compatibility DSP engine.
 *
 * The file creates and duplicates primary and secondary buffer wrappers
 * and forwards device settings to the underlying implementation. The
 * exported A3dCreate helper can fall back to plain DirectSound. Splash
 * launch and process-wide DSP cleanup are also handled here.
 *
 * Listener.cpp, dsbuffer.cpp and A3dSource.cpp implement the wrapped
 * playback interfaces; a3ddsp.cpp calculates their spatial controls.
 *
 *---------------------------------------------------------------------------
 */

#include "a3dprv.h"
#include "A3d.h"
#include "Listener.h"
#include "dsbuffer.h"
#include "A3dSource.h"
#include "a3ddsp.h"
#include "Plex.h"

#include <shellapi.h>
#include <stdio.h>
#include <string.h>

/* Absorption mapping constants.*/
#define A3D_HF_ABSORB_FACTOR_SCALE 17.0
#define A3D_HF_ABSORB_FACTOR_THRESHOLD 0.05f
#define A3D_HF_ABSORB_FALLBACK 1700.0

CA3d	*g_pA3d		= NULL;
/* (RE) Stored as 17.0/factor; the initial 17.0 encodes a factor of 1.0.
*/

double	 g_dHFAbsorb	= A3D_HF_ABSORB_FACTOR_SCALE;

/* =============================================================
// CA3d()
// (RE) a3d.dll rtl:0x10001230
//
// Construct the object with an optional server-count release hook.
// =============================================================*/

/* Nodes per primary/secondary list allocation */
#define A3D_BUFFER_LIST_BLOCK_NODES	32

/* Default engine mask; bit 4 selects hardware geometry, bit 1 is unresolved. */
#define A3D_DEFAULT_ENGINE_FLAGS 5

CA3d::CA3d(A3DATEXITPROC pfnAtExit)
{
	A3dPlexInit(&m_PrimaryBuffers, A3D_BUFFER_LIST_BLOCK_NODES);
	A3dPlexInit(&m_SecondaryBuffers, A3D_BUFFER_LIST_BLOCK_NODES);

	Zero();

	m_pfnAtExit	= pfnAtExit;
	m_fAtExit	= (pfnAtExit != NULL);
}

/* =============================================================
// Zero()
//
// Initialize the fields shared by both construction paths.
// =============================================================*/

void
CA3d::Zero(void)
{
	g_pA3d = this;

	m_cRef		= 0;
	m_pDS		= NULL;
	m_pDSOut	= NULL;
	m_pA3d2		= NULL;
	m_pPrv4		= NULL;
	m_pDal		= NULL;
	m_bReady	= 0;
	m_dwResourceManagerMode = 0;
	m_pfnAtExit	= NULL;
	m_fAtExit	= 0;
	m_fSplash	= TRUE;

	memset(&m_SoftwareCaps, 0, sizeof(m_SoftwareCaps));
	m_SoftwareCaps.dwSize		= sizeof(m_SoftwareCaps);
	m_SoftwareCaps.dwVersion	= IA3DVERSION_RELEASE20;

	memset(&m_HardwareCaps, 0, sizeof(m_HardwareCaps));

	m_dwAppVersion	= 0;

	m_Unknown_0x0C	= 0;
	m_Unknown_0x10	= 0;
	m_bInited		= 0;
	m_Unknown_0x18	= 0;

	m_HardwareCaps.dwSize	= sizeof(m_HardwareCaps);

	m_Unknown_0x1C	= 1;
}

/* Array extent is unresolved; references bound it to at most 2051 pointers. */

LONG	 g_cGlobalTables; /* (RE) a3d.dll rtl:0x1001A594 */
void	*g_apGlobalTables[2048];

/* =============================================================
// A3dFreeGlobalTables()
//
// Free the process-global DSP tables.
// =============================================================*/

void
A3dFreeGlobalTables(void)
{
LONG	i;

	for (i = 0; i < g_cGlobalTables; i++)
		free(g_apGlobalTables[i]);
}

/* =============================================================
// ~CA3d()
// (RE) a3d.dll rtl:0x100012C0
//
// Release buffers and interfaces. Preserve the original unconditional
// CoUninitialize call, including when Init was never called.
// =============================================================*/

CA3d::~CA3d(void)
{
CA3dSecondaryBuffer	*pSecondary;
CA3dPrimaryBuffer	*pPrimary;

	while ((pSecondary = (CA3dSecondaryBuffer *)
			A3dPlexPopFront(&m_SecondaryBuffers)) != NULL)
	{
		delete pSecondary;
	}

	while ((pPrimary = (CA3dPrimaryBuffer *)
			A3dPlexPopFront(&m_PrimaryBuffers)) != NULL)
	{
		delete pPrimary;
	}

	A3dFreeGlobalTables();

	if (m_pDSOut)
	{
		m_pDSOut->Release();
		m_pDSOut = NULL;
	}

	if (m_pDS)
	{
		m_pDS->Release();
		m_pDS = NULL;
	}

	if (m_pA3d2)
	{
		m_pA3d2->Release();
		m_pA3d2 = NULL;
	}

	if (m_pPrv4)
	{
		m_pPrv4->Release();
		m_pPrv4 = NULL;
	}

	if (m_pDal)
	{
		m_pDal->Release();
		m_pDal = NULL;
	}

	if (m_fAtExit)
	{
		m_pfnAtExit();
	}

	CoUninitialize();

	g_pA3d = NULL;

	A3dPlexFree(&m_SecondaryBuffers);
	A3dPlexFree(&m_PrimaryBuffers);
}

/* =============================================================
// QueryInterface()
//
// Acquire an IUnknown, IDirectSound, IA3d or IA3d2 interface.
//
// Returns:
//   S_OK
//   E_INVALIDARG   a null output
//   E_NOINTERFACE  otherwise
// =============================================================*/

STDMETHODIMP
CA3d::QueryInterface(REFIID riid, void **ppv)
{
	if (!ppv)
	{
		return (E_INVALIDARG);
	}

	*ppv = NULL;

	if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDirectSound))
	{
		*ppv = (IDirectSound *) this;
	}
	else if (IsEqualIID(riid, IID_IA3d) || IsEqualIID(riid, IID_IA3d2))
	{
		*ppv = (IA3d2 *) this;
	}

	if (!*ppv)
	{
		return (E_NOINTERFACE);
	}

	((IUnknown *) *ppv)->AddRef();

	return (S_OK);
}

/* =============================================================
// AddRef()
//
// Increment the reference count.
//
// Returns: The updated reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3d::AddRef(void)
{
	return (++m_cRef);
}

/* =============================================================
// Release()
//
// Release a reference and delete the object when the count reaches zero.
//
// Returns: The remaining reference count, including zero on an already zero
//          count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3d::Release(void)
{
	if (!m_cRef)
	{
		return (m_cRef);
	}

	if (--m_cRef)
	{
		return (m_cRef);
	}

	delete this;

	return (0);
}

/* =============================================================
// Init()
//
// Initialize the wrapped A3D object, DirectSound output and DSP engine.
//
// Returns: S_OK; E_FAIL for COM or interface acquisition failures;
//          DirectSoundCreate failure otherwise.
// =============================================================*/

HRESULT
CA3d::Init(LPCGUID pcGuidDevice, DWORD dwFeatures, DWORD dwInitFlags, LPDWORD pdwFeaturesEnabled)
{
HRESULT	hr;

	if (FAILED(CoInitialize(NULL)))
	{
		return (E_FAIL);
	}

	if (FAILED(CoCreateInstance(CLSID_A3dApi, NULL, CLSCTX_INPROC_SERVER,
				   IID_IA3dPrv4, (void **) &m_pPrv4)))
	{
		return (E_FAIL);
	}

	if (FAILED(m_pPrv4->QueryInterface(IID_IA3d2, (void **) &m_pA3d2)))
	{
		return (E_FAIL);
	}

	if (FAILED(m_pPrv4->QueryInterface(IID_IDirectSound, (void **) &m_pDS)))
	{
		return (E_FAIL);
	}

	if (FAILED(m_pDS->QueryInterface(IID_IA3dDal, (void **) &m_pDal)))
	{
		return (E_FAIL);
	}

	m_pDal->InitializeEx((LPGUID) pcGuidDevice, dwFeatures, dwInitFlags, pdwFeaturesEnabled);

	m_pA3d2->SetResourceManagerMode(A3D_RESOURCE_MODE_DYNAMIC);

	m_bInited = 1;

	if (dwInitFlags & A3DINIT_DISABLE_SPLASHSCREEN)
	{
		m_fSplash = FALSE;
	}

	m_Unknown_0x1C	= 0;

	hr = DirectSoundCreate(pcGuidDevice, &m_pDSOut, NULL);

	if (FAILED(hr))
	{
		return (hr);
	}

	/* Initialize listener defaults in A3D axes before latching DirectSound axes. */
	A3dEngineInit(g_cSources, 1.0f,
		      A3D_KEEP_SAMPLE_RATE_MODE, A3D_KEEP_SAMPLE_RATE_MODE,
		      A3D_Q15_FULL_GAIN, A3D_DEFAULT_ENGINE_FLAGS,
		      (m_dwAppVersion == 0) ? A3D_COORD_LEGACY : 0);

	A3dLatchD3DAxes();

	m_bReady = 1;

	return (S_OK);
}

/* IDirectSound - the interface at +0x00                                      */

/* =============================================================
// CreateSoundBuffer()
//
// Create a wrapped primary or secondary buffer.
//
// Returns:
//   The buffer factory result
//   DSERR_UNINITIALIZED        before Init
//   E_INVALIDARG               null arguments
//   CLASS_E_NOAGGREGATION      aggregation
// =============================================================*/

STDMETHODIMP
CA3d::CreateSoundBuffer(LPCDSBUFFERDESC pcDesc, LPDIRECTSOUNDBUFFER *ppBuffer,
			LPUNKNOWN pUnkOuter)
{
	if (!m_bReady)
	{
		return (DSERR_UNINITIALIZED);
	}

	if (!ppBuffer || !pcDesc)
	{
		return (E_INVALIDARG);
	}

	*ppBuffer = NULL;

	if (pUnkOuter)
	{
		return (CLASS_E_NOAGGREGATION);
	}

	if (pcDesc->dwFlags & DSBCAPS_PRIMARYBUFFER)
	{
		return (A3dMakePrimaryBuffer(this, pcDesc, ppBuffer, NULL));
	}

	return (A3dMakeSecondaryBuffer(this, pcDesc, ppBuffer, NULL));
}

/* =============================================================
// GetCaps()
//
// Set the capability structure size and forward the query.
//
// Returns:
//   The wrapped GetCaps result
//   DSERR_UNINITIALIZED         before Init
// =============================================================*/

STDMETHODIMP
CA3d::GetCaps(LPDSCAPS pCaps)
{
	if (!m_bReady)
	{
		return (DSERR_UNINITIALIZED);
	}

	if (pCaps->dwSize != sizeof(DSCAPS))
	{
		pCaps->dwSize = sizeof(DSCAPS);
	}

	return (m_pDS->GetCaps(pCaps));
}

/* =============================================================
// DuplicateSoundBuffer()
//
// Duplicate a wrapped secondary buffer.
//
// Returns:
//   The GetCaps failure or duplication result
//   E_INVALIDARG                        null arguments
//   DSERR_UNINITIALIZED                 before Init
//   E_FAIL                              a primary buffer
// =============================================================*/

STDMETHODIMP
CA3d::DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER pOriginal,
			   LPDIRECTSOUNDBUFFER *ppDuplicate)
{
DSBCAPS	caps;
HRESULT	hr;

	if (!ppDuplicate || !pOriginal)
	{
		return (E_INVALIDARG);
	}

	if (!m_bReady)
	{
		return (DSERR_UNINITIALIZED);
	}

	*ppDuplicate = NULL;

	memset(&caps, 0, sizeof(caps));
	caps.dwSize = sizeof(caps);

	hr = pOriginal->GetCaps(&caps);

	if (FAILED(hr))
	{
		return (hr);
	}

	if (caps.dwFlags & DSBCAPS_PRIMARYBUFFER)
	{
		return (E_FAIL);
	}

	return (A3dDuplicateBuffer(this, pOriginal, ppDuplicate));
}

/* =============================================================
// SetCooperativeLevel()
// (RE) a3d.dll rtl:0x10001F20
//
// Forward the cooperative level. Preserve the original unchecked
// interface access before initialization.
//
// Returns: The wrapped SetCooperativeLevel result.
// =============================================================*/

STDMETHODIMP
CA3d::SetCooperativeLevel(HWND hWnd, DWORD dwLevel)
{
	return (m_pDS->SetCooperativeLevel(hWnd, dwLevel));
}

/* =============================================================
// Compact()
// (RE) a3d.dll rtl:0x10001A80
//
// Forward compaction. Preserve the original unchecked interface access
// before initialization.
//
// Returns: The wrapped Compact result.
// =============================================================*/

STDMETHODIMP
CA3d::Compact(void)
{
	return (m_pDS->Compact());
}

/* =============================================================
// GetSpeakerConfig()
//
// Read the wrapped speaker configuration.
//
// Returns:
//   The wrapped GetSpeakerConfig result
//   DSERR_UNINITIALIZED                 before Init
// =============================================================*/

STDMETHODIMP
CA3d::GetSpeakerConfig(LPDWORD pdwConfig)
{
	if (!m_bReady)
	{
		return (DSERR_UNINITIALIZED);
	}

	return (m_pDS->GetSpeakerConfig(pdwConfig));
}

/* =============================================================
// SetSpeakerConfig()
//
// Set the wrapped speaker configuration.
//
// Returns:
//   The wrapped SetSpeakerConfig result
//   DSERR_UNINITIALIZED                 before Init
// =============================================================*/

STDMETHODIMP
CA3d::SetSpeakerConfig(DWORD dwConfig)
{
	if (!m_bReady)
	{
		return (DSERR_UNINITIALIZED);
	}

	return (m_pDS->SetSpeakerConfig(dwConfig));
}

/* =============================================================
// Initialize()
//
// Initialize with the default flags and A3D disabled.
//
// Returns: The Init result.
// =============================================================*/

STDMETHODIMP
CA3d::Initialize(LPCGUID pcGuidDevice)
{
DWORD	dwOut;

	dwOut = 0;

	return (Init(pcGuidDevice, A3DINIT_DEFAULT1, A3DINIT_DISABLE_A3D, &dwOut));
}

/* IA3d / IA3d2 - the interface at +0x04                                      */

/* =============================================================
// SetOutputMode()
//
// Set the wrapped output mode.
//
// Returns: The wrapped SetOutputMode result.
// =============================================================*/

STDMETHODIMP
CA3d::SetOutputMode(DWORD dwFrontXtalkMode, DWORD dwBackXtalkMode,
		    DWORD dwQuadMode)
{
	return (m_pA3d2->SetOutputMode(dwFrontXtalkMode, dwBackXtalkMode,
				       dwQuadMode));
}

/* =============================================================
// GetOutputMode()
//
// Read the wrapped output mode.
//
// Returns: The wrapped GetOutputMode result.
// =============================================================*/

STDMETHODIMP
CA3d::GetOutputMode(LPDWORD lpdwFrontXtalkMode, LPDWORD lpdwBackXtalkMode,
		    LPDWORD lpdwQuadMode)
{
	return (m_pA3d2->GetOutputMode(lpdwFrontXtalkMode, lpdwBackXtalkMode,
				       lpdwQuadMode));
}

/* =============================================================
// SetResourceManagerMode()
//
// Cache and forward the resource-manager mode.
//
// Returns: The wrapped SetResourceManagerMode result.
// =============================================================*/

STDMETHODIMP
CA3d::SetResourceManagerMode(DWORD dwResourceManagerMode)
{
	m_dwResourceManagerMode = dwResourceManagerMode;

	return (m_pA3d2->SetResourceManagerMode(dwResourceManagerMode));
}

/* =============================================================
// GetResourceManagerMode()
//
// Read the wrapped resource-manager mode.
//
// Returns: The wrapped GetResourceManagerMode result.
// =============================================================*/

STDMETHODIMP
CA3d::GetResourceManagerMode(LPDWORD lpdwResourceManagerMode)
{
	return (m_pA3d2->GetResourceManagerMode(lpdwResourceManagerMode));
}

/* =============================================================
// SetHFAbsorbFactor()
//
// Store the process-wide high-frequency absorption factor.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3d::SetHFAbsorbFactor(FLOAT fFactor)
{
	if (fFactor <= A3D_HF_ABSORB_FACTOR_THRESHOLD)
	{
		g_dHFAbsorb = A3D_HF_ABSORB_FALLBACK;
	}
	else
	{
		g_dHFAbsorb = A3D_HF_ABSORB_FACTOR_SCALE / fFactor;
	}

	return (S_OK);
}

/* =============================================================
// GetHFAbsorbFactor()
// (RE) a3d.dll rtl:0x10001970
//
// Read the process-wide absorption factor. Preserve the original
// unchecked output pointer.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3d::GetHFAbsorbFactor(FLOAT *pfFactor)
{
	*pfFactor = (FLOAT) (A3D_HF_ABSORB_FACTOR_SCALE / g_dHFAbsorb);

	return (S_OK);
}

/* =============================================================
// RegisterVersion()
//
// Record the application A3D version.
//
// Returns:
//   S_OK          A3D 1.0 or 1.2
//   E_INVALIDARG  otherwise
// =============================================================*/

STDMETHODIMP
CA3d::RegisterVersion(DWORD dwVersion)
{
	if (dwVersion != A3D_APP_VERSION_10 && dwVersion != A3D_APP_VERSION_12)
	{
		return (E_INVALIDARG);
	}

	m_dwAppVersion = dwVersion;

	return (S_OK);
}

/* =============================================================
// GetSoftwareCaps()
//
// Copy software capabilities, clamping the caller-supplied byte count.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null output or zero size
// =============================================================*/

STDMETHODIMP
CA3d::GetSoftwareCaps(LPA3DCAPS_SOFTWARE pCaps)
{
DWORD	cb;

	if (!pCaps)
	{
		return (E_INVALIDARG);
	}

	cb = pCaps->dwSize;

	if (!cb)
	{
		return (E_INVALIDARG);
	}

	if (cb >= sizeof(A3DCAPS_SOFTWARE))
	{
		cb = sizeof(A3DCAPS_SOFTWARE);
	}

	memcpy(pCaps, &m_SoftwareCaps, cb);

	pCaps->dwSize = cb;

	return (S_OK);
}

/* =============================================================
// GetHardwareCaps()
//
// Copy hardware capabilities, clamping the caller-supplied byte count.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null output or zero size
// =============================================================*/

STDMETHODIMP
CA3d::GetHardwareCaps(LPA3DCAPS_HARDWARE pCaps)
{
DWORD	cb;

	if (!pCaps)
	{
		return (E_INVALIDARG);
	}

	cb = pCaps->dwSize;

	if (!cb)
	{
		return (E_INVALIDARG);
	}

	if (cb >= sizeof(A3DCAPS_HARDWARE))
	{
		cb = sizeof(A3DCAPS_HARDWARE);
	}

	memcpy(pCaps, &m_HardwareCaps, cb);

	pCaps->dwSize = cb;

	return (S_OK);
}

typedef int A3dSizeCheck[(sizeof(CA3d) == 0x304) ? 1 : -1];
typedef int A3dAtExitCheck[(offsetof(CA3d, m_pfnAtExit) == 0x28C) ? 1 : -1];

/* =============================================================
// A3dCreate()
// (RE) a3d.dll rtl:0x10001000
//
// Create an A3D object, using plain DirectSound for ForceDS or Init failure.
//
// Returns: S_FALSE for initialized A3D or successful ForceDS > 1; the
//          DirectSound result for fallback or ForceDS = 1; E_INVALIDARG for a
//          null output; CLASS_E_NOAGGREGATION for aggregation; E_OUTOFMEMORY
//          for allocation failure; QueryInterface failure otherwise.
// =============================================================*/

extern "C" HRESULT __stdcall
A3dCreate(GUID *pGuidDevice, IDirectSound **ppDS, IUnknown *pUnkOuter)
{
CA3d	*pObj;
HKEY	 hKey;
DWORD	 dwForceDS;
DWORD	 cbData;
HRESULT	 hr;

	dwForceDS = 0;

	if (!RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Aureal\\A3D", 0,
			   KEY_READ, &hKey))
	{
		cbData = sizeof(dwForceDS);

		if (!RegQueryValueExA(hKey, "ForceDS", 0, 0,
				      (LPBYTE) &dwForceDS, &cbData))
		{
		}

		if (hKey)
		{
			RegCloseKey(hKey);
		}

		if (dwForceDS)
		{
			MessageBeep((UINT) -1);

			hr = DirectSoundCreate(pGuidDevice, ppDS, NULL);

			if (hr == S_OK)
			{
				return (dwForceDS > 1 ? S_FALSE : S_OK);
			}

			return (hr);
		}
	}

	if (!ppDS)
	{
		return (E_INVALIDARG);
	}

	*ppDS = NULL;

	if (pUnkOuter)
	{
		return (CLASS_E_NOAGGREGATION);
	}

	pObj = new CA3d(NULL);

	if (!pObj)
	{
		return (E_OUTOFMEMORY);
	}

	hr = pObj->QueryInterface(IID_IDirectSound, (void **) ppDS);

	if (FAILED(hr))
	{
		delete pObj;

		return (hr);
	}

	hr = (*ppDS)->Initialize(pGuidDevice);

	if (FAILED(hr))
	{
		delete pObj;

		return (DirectSoundCreate(pGuidDevice, ppDS, NULL));
	}

	return ((HRESULT) (hr + 1));
}

/* =============================================================
// A3dMakePrimaryBuffer()
// (RE) a3d.dll rtl:0x10001660
//
// Create a primary wrapper, falling back to a raw buffer if Init fails.
// Preserve the wrapper leak on the first CreateSoundBuffer failure.
//
// Returns: S_OK; E_OUTOFMEMORY for wrapper allocation failure; the wrapped
//          creation or interface failure otherwise.
// =============================================================*/

HRESULT
A3dMakePrimaryBuffer(CA3d *pOwner, LPCDSBUFFERDESC pcDesc,
		     LPDIRECTSOUNDBUFFER *ppBuffer, LPUNKNOWN pUnkOuter)
{
CA3dPrimaryBuffer	*pWrapper;
LPDIRECTSOUNDBUFFER	 pRaw;
HRESULT				 hr;

	pWrapper = new CA3dPrimaryBuffer(&pOwner->m_PrimaryBuffers);

	if (!pWrapper)
		return (E_OUTOFMEMORY);

	pRaw = NULL;

	hr = pOwner->m_pDS->CreateSoundBuffer(pcDesc, &pRaw, pUnkOuter);

	if (FAILED(hr))
		return (hr);

	if (FAILED(pWrapper->Init(pRaw)))
	{
		delete pWrapper;

		hr = pOwner->m_pDS->CreateSoundBuffer(pcDesc, &pRaw, pUnkOuter);

		if (FAILED(hr))
			return (hr);

		*ppBuffer = pRaw;

		return (S_OK);
	}

	hr = pWrapper->QueryInterface(IID_IDirectSoundBuffer, (void **) ppBuffer);

	if (FAILED(hr))
	{
		delete pWrapper;

		return (hr);
	}

	A3dPlexPushFront(&pOwner->m_PrimaryBuffers, pWrapper);

	if ((pcDesc->dwFlags & DSBCAPS_CTRL3D) && pOwner->m_fSplash == TRUE)
	{
		A3dSplash();

		pOwner->m_fSplash = FALSE;
	}

	return (S_OK);
}

/* =============================================================
// A3dMakeSecondaryBuffer()
// (RE) a3d.dll rtl:0x10001510
//
// Create a secondary wrapper. Preserve success with a null raw buffer
// and the raw-buffer leak when wrapper allocation fails.
//
// Returns: The wrapped creation, initialization or interface result;
//          E_OUTOFMEMORY for wrapper allocation failure.
// =============================================================*/

HRESULT
A3dMakeSecondaryBuffer(CA3d *pOwner, LPCDSBUFFERDESC pcDesc,
		       LPDIRECTSOUNDBUFFER *ppBuffer, LPUNKNOWN pUnkOuter)
{
CA3dSecondaryBuffer	*pWrapper;
LPDIRECTSOUNDBUFFER	 pRaw;
HRESULT				 hr;

	if (!(pcDesc->dwFlags & DSBCAPS_CTRL3D))
		pOwner->m_pA3d2->SetResourceManagerMode(A3D_RESOURCE_MODE_OFF);

	pRaw = NULL;

	hr = pOwner->m_pDS->CreateSoundBuffer(pcDesc, &pRaw, pUnkOuter);

	pOwner->m_pA3d2->SetResourceManagerMode(pOwner->m_dwResourceManagerMode);

	if (FAILED(hr) || !pRaw)
	{
		*ppBuffer = NULL;

		return (hr);
	}

	pWrapper = new CA3dSecondaryBuffer(&pOwner->m_SecondaryBuffers);

	if (!pWrapper)
		return (E_OUTOFMEMORY);

	hr = pWrapper->Init(pOwner, pcDesc, pRaw);

	if (SUCCEEDED(hr))
		hr = pWrapper->QueryInterface(IID_IDirectSoundBuffer,
					      (void **) ppBuffer);

	if (FAILED(hr))
	{
		delete pWrapper;

		return (hr);
	}

	A3dPlexPushFront(&pOwner->m_SecondaryBuffers, pWrapper);

	return (S_OK);
}

/* =============================================================
// A3dDuplicateBuffer()
// (RE) a3d.dll rtl:0x100017B0
//
// Duplicate a secondary buffer from this DLL; other buffers are invalid.
// Preserve the raw-duplicate leak when wrapper allocation fails.
//
// Returns: The wrapped duplication, initialization or interface result;
//          E_OUTOFMEMORY for wrapper allocation failure.
// =============================================================*/

HRESULT
A3dDuplicateBuffer(CA3d *pOwner, LPDIRECTSOUNDBUFFER pOriginal,
		   LPDIRECTSOUNDBUFFER *ppDuplicate)
{
CA3dSecondaryBuffer	*pSource;
CA3dSecondaryBuffer	*pWrapper;
LPDIRECTSOUNDBUFFER	 pRaw;
HRESULT				 hr;

	pSource = (CA3dSecondaryBuffer *) pOriginal;

	pRaw = NULL;

	hr = pOwner->m_pDS->DuplicateSoundBuffer(pSource->m_pBuffer, &pRaw);

	if (FAILED(hr))
		return (hr);

	pWrapper = new CA3dSecondaryBuffer(&pOwner->m_SecondaryBuffers);

	if (!pWrapper)
		return (E_OUTOFMEMORY);

	hr = pWrapper->InitFromDuplicate(pOwner, pSource, pRaw);

	if (SUCCEEDED(hr))
		hr = pWrapper->QueryInterface(IID_IDirectSoundBuffer,
					      (void **) ppDuplicate);

	if (FAILED(hr))
	{
		delete pWrapper;

		return (hr);
	}

	A3dPlexPushFront(&pOwner->m_SecondaryBuffers, pWrapper);

	return (S_OK);
}

HWND	g_hwndSplash = NULL;

/* Wait after launching enabled splash output */
#define A3D_SPLASH_WAIT_MS	3500

/* =============================================================
// A3dSplash()
// (RE) a3d.dll rtl:0x10001DD0
//
// Launch A3DSplsh.exe and restore the splash window. Preserve the reused
// registry byte count, empty-path root lookup, uninitialized path on key-open
// failure and unchecked path concatenation.
// =============================================================*/

void
A3dSplash(void)
{
HKEY	hKey;
BYTE	szPath[256];
BYTE	szAudio[256];
BYTE	szScreen[256];
DWORD	cbData;
LONG	lPath;

	cbData = sizeof(szPath);

	*(DWORD *) szAudio  = 0;
	*(DWORD *) szScreen = 0;

	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Aureal\\A3D", 0,
			  KEY_READ, &hKey))
	{
		lPath = *(DWORD *) szScreen;
	}
	else
	{
		lPath = RegQueryValueExA(hKey, "SplashPath", 0, 0, szPath, &cbData);

		RegQueryValueExA(hKey, "SplashAudio", 0, 0, szAudio, &cbData);
		RegQueryValueExA(hKey, "SplashScreen", 0, 0, szScreen, &cbData);

		if (hKey)
		{
			RegCloseKey(hKey);
		}
	}

	if (lPath)
	{
		sprintf((char *) szPath, "A3DSplsh.exe");
	}
	else
	{
		strcat((char *) szPath, "\\A3DSplsh.exe");
	}

	ShellExecuteA(g_hwndSplash, NULL, (LPCSTR) szPath, NULL, NULL, SW_SHOWNORMAL);

	if (*(DWORD *) szAudio || *(DWORD *) szScreen)
	{
		Sleep(A3D_SPLASH_WAIT_MS);
	}

	if (g_hwndSplash)
	{
		if (IsIconic(g_hwndSplash))
		{
			ShowWindow(g_hwndSplash, SW_RESTORE);
		}
	}
}
