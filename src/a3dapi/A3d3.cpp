/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3d3.cpp
 *
 * Implements the main A3D API operations on CA3dRoot. It initializes the
 * resource-manager backend, creates sources and reverb objects, manages
 * global rendering settings, and reports device capabilities to
 * applications.
 *
 * Flush coordinates each audio update: it advances frame timing, invokes
 * source and geometry tracing, and submits reverb changes through the
 * supported property vocabulary. Source registration, primary-buffer
 * creation and backend shutdown are also handled here.
 *
 * CA3dRoot is declared in A3d3.h. Its COM lifetime is implemented in
 * A3dRoot.cpp; geometry, listener and matrix operations are in
 * A3dGeom.cpp, Listener.cpp and A3dMatrix.cpp.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "A3d3.h"
#include "a3dclsfc.h"
#include "A3dSource.h"
#include "a3dsourcecom.h"
#include "A3dReverb.h"
#include "apimapper.h"
#include "ChunkPage.h"
#include "A3dList.h"
#include "LinkList.h"
#include "resman.h"
#include "A3dReverbEmu.h"	/* NOT PART OF THE ORIGINAL; see the header */

#include <math.h>

LSTATUS	ReadA3dRegistryDword(LPCSTR lpValueName, LPBYTE lpData);

C_ASSERT(sizeof(CA3dRoot) == 0x1694);

/* Detected property vocabularies */
#define A3D_REVERB_VOCABULARY_EAX       1
#define A3D_REVERB_VOCABULARY_I3DL2     2

/* Flush timing */
#define A3D_FLUSH_MAX_FRAME_SECONDS     5.0f
#define A3D_REF_ORDERS_POLL_INTERVAL_MS 2000.0

/* Streaming duration defaults and limit */
#define A3D_STREAM_BUFFER_MS_PER_REFLECTION_SECOND      3000.0f
#define A3D_STREAM_MAX_BUFFER_LENGTH_MS                 10000

/* Registry override values */
#define A3D_GEOM_REVERB_FORCE_ENABLE    1
#define A3D_GEOM_REVERB_FORCE_DISABLE   2
/* NewSource numeric upper bound */
#define A3D_NEW_SOURCE_MAX_TYPE_VALUE 0x2D

/* Listener reset values  */

/* =============================================================
// SetBackends()
// (RE) rtl:0x10001000; dbg:0x10007810
//
// Adopt the backend interface references and initialize engine defaults.
// =============================================================*/

void
CA3dRoot::SetBackends(IDirectSound *pDS, IA3d2 *pA3d2, IA3d *pA3d)
{
	m_pDirectSound = pDS;
	m_pA3d2        = pA3d2;
	m_pA3d         = pA3d;

	m_pDal                  = NULL;
	m_pPrimary              = NULL;
	m_fBackendsAttached     = 1;
	m_hrReported            = 0;
	m_pDS3dListener         = NULL;

	m_dwCoopLevel = A3D_CL_NORMAL;

	m_dwFeaturesRequested = 0;
	m_dwFeaturesAvailable = 0;
	m_fCompatAu8830       = 0;
	m_dwCompatWalkNear    = 0;
	m_dwCompatRefOrders   = 1;

	LoadIdentity();

	m_fUnitsPerMeter        = 1.0f;
	m_fDopplerScale         = 1.0f;
	m_fDistanceModelScale   = 1.0f;
	m_fEq                   = 1.0f;
	m_fEqCurve              = 1.0f;

	m_dwCoordSystem         = 0;
	m_pReverbPropSet        = NULL;
	m_pBoundedReverb        = NULL;
	m_dwReverbSearched      = 0;
	m_dwReverbVocabulary    = 0;

	m_dwStreamBufferLength          = (DWORD) (m_fMaxReflectionDelayTime * A3D_STREAM_BUFFER_MS_PER_REFLECTION_SECOND);
	m_dwStreamThreadPriority        = A3D_STREAMING_PRIORITY_NORMAL;
	m_dwStreamingPropertiesSet      = 0;

	m_dwSoftAC3Unlocked = 0;
	m_dwLastReverbType  = 0;
}

/* =============================================================
// Detach()
// (RE) rtl:0x100010D0; dbg:0x10007A10
//
// Delete registered sources, release backend interfaces and close the mapper
// log.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
CA3dRoot::Detach(void)
{
CA3dStdList<CA3dSource *>::iterator	it;
CA3dSource				*pSource;

	for (it = m_SourceArray.begin(); it != m_SourceArray.end(); ++it)
	{
		pSource = *it;

		/* Prevent the source destructor from unlinking during traversal. */
		pSource->m_dwOnSourceList = 0;

		delete pSource;
	}

	m_SourceArray.clear();

	if (m_pDal)
	{
		m_pDal->Release();

		m_pDal = NULL;
	}

	if (m_pDS3dListener)
	{
		m_pDS3dListener->Release();

		m_pDS3dListener = NULL;
	}

	if (m_pA3d)
	{
		m_pA3d->Release();

		m_pA3d = NULL;
	}

	if (m_pA3d2)
	{
		m_pA3d2->Release();

		m_pA3d2 = NULL;
	}

	if (m_pPrimary)
	{
		m_pPrimary->Release();

		m_pPrimary = NULL;
	}

	if (m_pDirectSound)
	{
		m_pDirectSound->Release();

		m_pDirectSound = NULL;
	}

	if (m_pReverbPropSet)
	{
		m_pReverbPropSet->Release();

		m_pReverbPropSet = NULL;
	}

	if (m_pBoundedReverb)
	{
		m_pBoundedReverb->Release();

		m_pBoundedReverb = NULL;
	}

	m_fBackendsAttached = 0;

	A3dMapperCloseDebugLog();

	return (S_OK);
}

/* =============================================================
// SetOutputMode()
// (RE) rtl:0x10001200; dbg:0x10007D20; IA3d5 slot 3
//
// Set the backend output mode.
//
// Returns:
//   Backend result
//   A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT
//                                       without a backend
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetOutputMode(DWORD dwRelation, DWORD dwMode, DWORD dwChannels)
{
	if (m_pA3d2)
		return (m_pA3d2->SetOutputMode(dwRelation, dwMode, dwChannels));

	if (m_pA3d)
		return (m_pA3d->SetOutputMode(dwRelation, dwMode, dwChannels));

	DBGSTR("CA3dRoot::SetOutputMode() - Function call not valid before Init.\n");

	return (A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT);
}

/* =============================================================
// GetOutputMode()
// (RE) rtl:0x10001240; dbg:0x10007DD0; IA3d5 slot 4
//
// Read the backend output mode.
//
// Returns:
//   Backend result
//   E_POINTER                           a null output
//   A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT
//                                       without a backend
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetOutputMode(LPDWORD pdwRelation, LPDWORD pdwMode, LPDWORD pdwChannels)
{
	if (!pdwRelation || !pdwMode || !pdwChannels)
	{
		DBGSTR("CA3dRoot::GetOutputMode() - Invalid Pointer passed as parameter.\n");

		return (E_POINTER);
	}

	if (m_pA3d2)
		return (m_pA3d2->GetOutputMode(pdwRelation, pdwMode, pdwChannels));

	if (m_pA3d)
		return (m_pA3d->GetOutputMode(pdwRelation, pdwMode, pdwChannels));

	DBGSTR("CA3dRoot::GetOutputMode() - Function call not valid before Init.\n");

	return (A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT);
}

/* =============================================================
// SetResourceManagerMode()
// (RE) rtl:0x100012B0; dbg:0x10007EB0; IA3d5 slot 5
//
// Set the resource-manager mode for IA3d3 and earlier.
//
// Returns:
//   SetResManMode result
//   A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT
//                                       before initialization
//   A3DERROR_UNIMPLEMENTED_FUNCTION     IA3d4+
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetResourceManagerMode(DWORD dwMode)
{
	if (m_fInitialized && m_pDirectSound)
	{
		if (m_dwInterfaceVersion > 3)
		{
			DBGSTR("CA3dRoot::SetResourceManagerMode() - Function no longer required.\n");

			return (A3DERROR_UNIMPLEMENTED_FUNCTION);
		}

		return (SetResManMode(dwMode));
	}

	DBGSTR("CA3dRoot::SetResourceManagerMode() - Function not valid before Init.\n");

	return (A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT);
}

/* =============================================================
// GetResourceManagerMode()
// (RE) rtl:0x10001360; dbg:0x10007FD0; IA3d5 slot 6
//
// Read the resource-manager mode for IA3d3 and earlier.
//
// Returns:
//   GetResManMode result
//   E_POINTER                           a null output
//   A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT
//                                       before initialization
//   A3DERROR_UNIMPLEMENTED_FUNCTION     IA3d4+
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetResourceManagerMode(LPDWORD pdwMode)
{
	if (!pdwMode)
	{
		DBGSTR("CA3dRoot::GetResourceManagerMode() - lpdwResourceManagerMode is NULL.\n");

		return (E_POINTER);
	}

	if (m_fInitialized && m_pDirectSound)
	{
		if (m_dwInterfaceVersion > 3)
		{
			DBGSTR("CA3dRoot::GetResourceManagerMode() - Function no longer implemented.\n");

			return (A3DERROR_UNIMPLEMENTED_FUNCTION);
		}

		return (GetResManMode(pdwMode));
	}

	DBGSTR("CA3dRoot::GetResourceManagerMode() - Function not valid before Init.\n");

	return (A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT);
}

/* =============================================================
// GetResManMode()
// (RE) dbg:0x10008070; thunk dbg:0x100017DA
//
// Read the backend resource-manager mode.
//
// Returns:
//   Backend result
//   E_FAIL          without a backend
// =============================================================*/

HRESULT
CA3dRoot::GetResManMode(LPDWORD pdwMode)
{
	if (m_pA3d2)
		return (m_pA3d2->GetResourceManagerMode(pdwMode));

	if (m_pA3d)
		return (m_pA3d->GetResourceManagerMode(pdwMode));

	DBGSTR("CA3dRoot::GetResManMode() - Function not valid before Init.\n");

	return (E_FAIL);
}

/* =============================================================
// SetResManMode()
// (RE) dbg:0x10007F30; thunk dbg:0x10003346
//
// Set the backend resource-manager mode.
//
// Returns:
//   Backend result
//   E_FAIL          without a backend
// =============================================================*/

HRESULT
CA3dRoot::SetResManMode(DWORD dwMode)
{
	if (m_pA3d2)
		return (m_pA3d2->SetResourceManagerMode(dwMode));

	if (m_pA3d)
		return (m_pA3d->SetResourceManagerMode(dwMode));

	DBGSTR("CA3dRoot::SetResManMode() - Function not valid before Init.\n");

	return (E_FAIL);
}

/* =============================================================
// SetHFAbsorbFactor()
// (RE) rtl:0x10001420; dbg:0x10008110; IA3d5 slot 7
//
// Set the backend high-frequency absorption factor.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION for IA3d4+; otherwise the backend
//          result, or A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT without a
//          backend.
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetHFAbsorbFactor(FLOAT fFactor)
{
	if (m_dwInterfaceVersion > 3)
	{
		DBGSTR("CA3dRoot::SetHFAbsorbFactor() - Function is obsolete for IA3d4 and up.\n");

		return (A3DERROR_UNIMPLEMENTED_FUNCTION);
	}

	if (m_pA3d2)
		return (m_pA3d2->SetHFAbsorbFactor(fFactor));

	if (m_pA3d)
		return (m_pA3d->SetHFAbsorbFactor(fFactor));

	DBGSTR("CA3dRoot::SetHFAbsorbFactor() - Function not valid before Init.\n");

	return (A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT);
}

/* =============================================================
// GetHFAbsorbFactor()
// (RE) rtl:0x10001470; dbg:0x100081D0; IA3d5 slot 8
//
// Read the backend high-frequency absorption factor.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION for IA3d4+; otherwise E_POINTER for
//          a null output, the backend result, or
//          A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT without a backend.
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetHFAbsorbFactor(FLOAT *pfFactor)
{
	if (m_dwInterfaceVersion > 3)
	{
		DBGSTR("CA3dRoot::GetHFAbsorbFactor() - Function is obsolete for IA3d4 and up.\n");

		return (A3DERROR_UNIMPLEMENTED_FUNCTION);
	}

	if (!pfFactor)
	{
		DBGSTR("CA3dRoot::GetHFAbsorbFactor() - lpfabsorb_factor is NULL.\n");

		return (E_POINTER);
	}

	if (m_pA3d2)
		return (m_pA3d2->GetHFAbsorbFactor(pfFactor));

	if (m_pA3d)
		return (m_pA3d->GetHFAbsorbFactor(pfFactor));

	DBGSTR("CA3dRoot::GetHFAbsorbFactor() - Function not valid before Init.\n");

	return (A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT);
}

/* =============================================================
// RegisterVersion()
// (RE) rtl:0x10001570; dbg:0x100083D0; IA3d5 slot 9
//
// Register the client version with the IA3d2 backend.
// The original diagnostic spells the method RegsiterVersion.
//
// Returns:
//   Backend result
//   A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT
//                                       without IA3d2
// =============================================================*/

STDMETHODIMP
CA3dRoot::RegisterVersion(DWORD dwVersion)
{
	if (m_pA3d2)
		return (m_pA3d2->RegisterVersion(dwVersion));

	DBGSTR("CA3dRoot::RegsiterVersion() - Function not valid before Init.\n");

	return (A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT);
}

/* =============================================================
// GetSoftwareCaps()
// (RE) rtl:0x100014D0; dbg:0x100082B0; IA3d5 slot 10
//
// Read backend software capabilities. As in the original, the initialized
// path assumes the class factory supplied a nonnull IA3d2 interface.
//
// Returns:
//   Backend result
//   E_POINTER                           a null output
//   A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT
//                                       before initialization
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetSoftwareCaps(LPA3DCAPS_SOFTWARE pCaps)
{
	if (!pCaps)
	{
		DBGSTR("CA3dRoot::GetSoftwareCaps() - lpA3dCaps is NULL.\n");

		return (E_POINTER);
	}

	if (m_fInitialized && m_pDirectSound)
		return (m_pA3d2->GetSoftwareCaps(pCaps));

	DBGSTR("CA3dRoot::GetSoftwareCaps() - Function not valid before Init.\n");

	return (A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT);
}

/* =============================================================
// GetHardwareCaps()
// (RE) rtl:0x10001520; dbg:0x10008340; IA3d5 slot 11
//
// Read backend hardware capabilities. As in the original, the initialized
// path assumes the class factory supplied a nonnull IA3d2 interface.
//
// Returns:
//   Backend result
//   E_POINTER                           a null output
//   A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT
//                                       before initialization
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetHardwareCaps(LPA3DCAPS_HARDWARE pCaps)
{
	if (!pCaps)
	{
		DBGSTR("CA3dRoot::GetHardwareCaps() - lpA3dCaps is NULL.\n");

		return (E_POINTER);
	}

	if (m_fInitialized && m_pDirectSound)
		return (m_pA3d2->GetHardwareCaps(pCaps));

	DBGSTR("CA3dRoot::GetHardwareCaps() - Function not valid before Init.\n");

	return (A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT);
}

/* =============================================================
// InitEx()
// (RE) rtl:0x100015A0; dbg:0x10008430; IA3d5 slot 42
//
// Initialize the engine and cooperative level, then enable available reverb
// features. Failed reverb setup leaves its feature bit clear.
//
// Returns: InitResmanLayer result.
// =============================================================*/

STDMETHODIMP
CA3dRoot::InitEx(LPGUID pGuid, DWORD dwFlags, DWORD dwPrefs, HWND hWnd,
		 DWORD dwLevel)
{
HRESULT	hr;

	hr = InitResmanLayer(pGuid, dwFlags, dwPrefs, hWnd, dwLevel);

	if (FAILED(hr))
		return (hr);

	if (m_dwFeaturesRequested & (A3D_REVERB | A3D_GEOMETRIC_REVERB))
	{
		if (SUCCEEDED(InitReverb()))
			m_dwFeaturesAvailable |= A3D_REVERB;
	}

	if (m_dwFeaturesRequested & A3D_GEOMETRIC_REVERB)
	{
		if (SUCCEEDED(EnableGeometricReverb()))
			m_dwFeaturesAvailable |= A3D_GEOMETRIC_REVERB;
	}

	return (hr);
}

/* =============================================================
// Init()
// (RE) rtl:0x100016E0; dbg:0x10008560
//
// Initialize the engine without setting a cooperative level.
//
// Returns: InitResmanLayer result.
// =============================================================*/

STDMETHODIMP
CA3dRoot::Init(LPGUID pGuid, DWORD dwFlags, DWORD dwReserved)
{
	return (InitResmanLayer(pGuid, dwFlags, dwReserved, NULL, 0));
}

/* =============================================================
// InitResmanLayer()
// (RE) rtl:0x10001700; dbg:0x100085A0; thunk dbg:0x100034AE
//
// Initialize the backend and record its features and DAL capabilities.
//
// Returns:
//   S_OK
//   A3DERROR_FAILED_INIT_A3D3  if backend initialization fails
// =============================================================*/

HRESULT
CA3dRoot::InitResmanLayer(LPGUID pGuid, DWORD dwFlags, DWORD dwReserved,
			  HWND hWnd, DWORD dwLevel)
{
DWORD	dwFeatures;
DWORD	dwDalInitFlags;
DWORD	dwSize;

	dwFeatures = 0;

	if (m_dwInterfaceVersion < 5 && (dwFlags & A3D_1ST_REFLECTIONS))
		dwFlags |= A3D_GEOMETRIC_REVERB;

	if (m_dwGeomReverbCtrl == A3D_GEOM_REVERB_FORCE_ENABLE)
	{
		dwFlags |= A3D_GEOMETRIC_REVERB;
		m_dwFeaturesEnabled |= A3D_GEOMETRIC_REVERB;
	}
	else if (m_dwGeomReverbCtrl == A3D_GEOM_REVERB_FORCE_DISABLE)
	{
		dwFlags &= ~A3D_GEOMETRIC_REVERB;
	}

	InterlockedIncrement(&m_cRef);

	m_dwFeaturesRequested = dwFlags;

	if (FAILED(m_pDirectSound->QueryInterface(IID_IA3dDal, (void **) &m_pDal)))
	{
		if (FAILED(m_pDirectSound->Initialize(pGuid)))
		{
			return (A3DERROR_FAILED_INIT_A3D3);
		}
	}
	else
	{
		dwDalInitFlags = A3DINIT_DEFAULT1;

		if (dwFlags & A3D_DISABLE_SPLASHSCREEN)
		{
			dwDalInitFlags |= A3DINIT_DISABLE_SPLASHSCREEN;
			dwFlags &= ~A3D_DISABLE_SPLASHSCREEN;
		}

		if (dwReserved & A3DRENDERPREFS_DISABLE_DS3D)
			dwDalInitFlags |= A3DINIT_DISABLE_DS3D;

		if (dwReserved & A3DRENDERPREFS_DISABLE_DS)
			dwDalInitFlags |= A3DINIT_DISABLE_DS;

		if (FAILED(((ResMan *) m_pDirectSound)->InitResMan(pGuid, dwFlags,
				dwDalInitFlags, &dwFeatures, hWnd, dwLevel)))
		{
			DBGSTR("CA3dRoot::InitResmanLayer() - Failed to initialize the Resource Manager.\n");

			return (A3DERROR_FAILED_INIT_A3D3);
		}

		dwSize = sizeof(A3DDALCAPS564);

		ZeroMemory(&m_SourceCaps, sizeof(m_SourceCaps));

		if (SUCCEEDED(m_pDal->GetA3dCaps(&m_SourceCaps.caps, &dwSize)) &&
		    m_SourceCaps.caps.wDeviceType == A3DCAPS_DEVICE_TYPE_AU8830 &&
		    !m_SourceCaps.caps.dwUnknown_0x44)
		{
			Compat(A3DCOMPAT_AU8830, A3D_ROOT_AU8830_ENABLE_VALUE);

			dwFeatures &= ~A3D_1ST_REFLECTIONS;
		}

		ASSERT(dwSize == sizeof(A3DDALCAPS564));
	}

	m_dwFeaturesAvailable = dwFeatures | (A3D_DIRECT_PATH_A3D | A3D_OCCLUSIONS);

	SetResManMode(A3D_RESOURCE_MODE_DYNAMIC);

	m_fInitialized = 1;

	return (S_OK);
}

/* =============================================================
// NewReverb()
// (RE) rtl:0x10001890; dbg:0x100088C0; IA3d5 slot 43
//
// Create an unbound reverb object with one client reference.
//
// Returns:
//   S_OK
//   E_POINTER                       a null output
//   A3DERROR_FEATURE_NOT_REQUESTED  if reverb was not requested
//   A3DERROR_FEATURE_NOT_SUPPORTED  if unavailable
//   A3DERROR_MEMORY_ALLOCATION      if allocation fails
// =============================================================*/

STDMETHODIMP
CA3dRoot::NewReverb(LPA3DREVERB *ppReverb)
{
CA3dReverb	*pReverb;

	if (!ppReverb)
	{
		DBGSTR("CA3dRoot::NewReverb() - ppReverb is NULL.\n");

		return (E_POINTER);
	}

	if (!(m_dwFeaturesRequested & A3D_REVERB))
	{
		DBGSTR("CA3dRoot::NewReverb() - A3D_REVERB was not requested at initialization.\n");

		return (A3DERROR_FEATURE_NOT_REQUESTED);
	}

	if (!(m_dwFeaturesAvailable & A3D_REVERB))
	{
		DBGSTR("CA3dRoot::NewReverb() - Reverb is not available on the current device.\n");

		return (A3DERROR_FEATURE_NOT_SUPPORTED);
	}

	pReverb = new CA3dReverb;

	if (!pReverb)
	{
		DBGSTR("CA3dRoot::NewReverb() - Failed to allocate memory for Reverb object.\n");

		return (A3DERROR_MEMORY_ALLOCATION);
	}

	pReverb->AddRef();

	*ppReverb = pReverb;

	return (S_OK);
}

/* =============================================================
// SendReverbProperty()
// (RE) DWORD dbg:0x1000D160; FLOAT dbg:0x1000D1C0; LONG dbg:0x1000D220
//
// Send a changed property and clear its bit, including when the write fails.
// Preserve changed-bit IDs: EAX decay/damping use 4/8 instead of standard 3/4.
// =============================================================*/

template <class T>
static void __cdecl
SendReverbProperty(DWORD dwProperty, T Value, GUID guidPropSet,
		   DWORD *pdwChanged, IA3dPropertySet *pPropSet)
{
	if (dwProperty & *pdwChanged)
	{
		pPropSet->Set(guidPropSet, dwProperty, NULL, 0,
			      &Value, sizeof(Value), A3DPROPSET_APPENDTOCACHE);

		*pdwChanged &= ~dwProperty;
	}
}

/* =============================================================
// SendReverbEaxCustom()
// (RE) rtl:0x10001970; dbg:0x10008A50
//
// Fit changed custom reverb settings to a preset and send its EAX properties.
//
// Returns: S_OK; property-write failures are ignored.
// =============================================================*/

HRESULT
CA3dRoot::SendReverbEaxCustom(void)
{
A3DREVERB_PROPERTIES	PresetProp;
DWORD			dwChanged;

	ASSERT(m_pBoundedReverb != 0 &&
	       !IsBadReadPtr(m_pBoundedReverb, sizeof(CA3dReverb)));
	ASSERT(m_pReverbPropSet != 0 &&
	       !IsBadReadPtr(m_pReverbPropSet, sizeof(IA3dPropertySet)));

	if (m_pBoundedReverb->m_dwChangedPending & A3DREVERB_CHANGED_CUSTOM_TO_EAX)
	{
		MapCustomToPreset(&m_pBoundedReverb->m_Properties, &PresetProp);

		dwChanged = A3DREVERB_CHANGED_PRESET_ALL;

		SendReverbProperty(A3DREVERB_CHANGED_ENVIRONMENT, PresetProp.uval.preset.dwEnvPreset,
				   DSPROPSETID_EAX_ReverbProperties,
				   &dwChanged, m_pReverbPropSet);
		SendReverbProperty(A3DREVERB_CHANGED_VOLUME, PresetProp.uval.preset.fVolume,
				   DSPROPSETID_EAX_ReverbProperties,
				   &dwChanged, m_pReverbPropSet);
		SendReverbProperty(A3DREVERB_CHANGED_DECAYTIME, PresetProp.uval.preset.fDecayTime,
				   DSPROPSETID_EAX_ReverbProperties,
				   &dwChanged, m_pReverbPropSet);
		SendReverbProperty(A3DREVERB_CHANGED_DAMPING, PresetProp.uval.preset.fDamping,
				   DSPROPSETID_EAX_ReverbProperties,
				   &dwChanged, m_pReverbPropSet);
	}

	return (S_OK);
}

/* =============================================================
// SendReverbI3dl2()
// (RE) rtl:0x10001AE0; dbg:0x10008D00
//
// Send changed custom listener properties through I3DL2.
//
// Returns: S_OK; property-write failures are ignored.
// =============================================================*/

HRESULT
CA3dRoot::SendReverbI3dl2(void)
{
A3DREVERB_CUSTOM       *p;
DWORD                   dwChanged;

	ASSERT(m_pBoundedReverb != 0 &&
	       !IsBadReadPtr(m_pBoundedReverb, sizeof(CA3dReverb)));
	ASSERT(m_pReverbPropSet != 0 &&
	       !IsBadReadPtr(m_pReverbPropSet, sizeof(IA3dPropertySet)));

	dwChanged = m_pBoundedReverb->m_dwChangedPending;

	if (dwChanged & A3DREVERB_CHANGED_CUSTOM_ALL)
	{
		p = &m_pBoundedReverb->m_Properties.uval.custom;

		SendReverbProperty(A3DREVERB_CHANGED_CUSTOM_ROOM, p->lRoom,
				   DSPROPSETID_I3DL2_ListenerProperties,
				   &dwChanged, m_pReverbPropSet);
		SendReverbProperty(A3DREVERB_CHANGED_CUSTOM_ROOMHF, p->lRoomHF,
				   DSPROPSETID_I3DL2_ListenerProperties,
				   &dwChanged, m_pReverbPropSet);
		SendReverbProperty(A3DREVERB_CHANGED_CUSTOM_ROOMROLLOFFFACTOR, p->flRoomRolloffFactor,
				   DSPROPSETID_I3DL2_ListenerProperties,
				   &dwChanged, m_pReverbPropSet);
		SendReverbProperty(A3DREVERB_CHANGED_CUSTOM_DECAYTIME, p->flDecayTime,
				   DSPROPSETID_I3DL2_ListenerProperties,
				   &dwChanged, m_pReverbPropSet);
		SendReverbProperty(A3DREVERB_CHANGED_CUSTOM_DECAYHFRATIO, p->flDecayHFRatio,
				   DSPROPSETID_I3DL2_ListenerProperties,
				   &dwChanged, m_pReverbPropSet);
		SendReverbProperty(A3DREVERB_CHANGED_CUSTOM_REFLECTIONS, p->lReflections,
				   DSPROPSETID_I3DL2_ListenerProperties,
				   &dwChanged, m_pReverbPropSet);
		SendReverbProperty(A3DREVERB_CHANGED_CUSTOM_REFLECTIONSDELAY, p->flReflectionsDelay,
				   DSPROPSETID_I3DL2_ListenerProperties,
				   &dwChanged, m_pReverbPropSet);
		SendReverbProperty(A3DREVERB_CHANGED_CUSTOM_REVERB, p->lReverb,
				   DSPROPSETID_I3DL2_ListenerProperties,
				   &dwChanged, m_pReverbPropSet);
		SendReverbProperty(A3DREVERB_CHANGED_CUSTOM_REVERBDELAY, p->flReverbDelay,
				   DSPROPSETID_I3DL2_ListenerProperties,
				   &dwChanged, m_pReverbPropSet);
		SendReverbProperty(A3DREVERB_CHANGED_CUSTOM_DIFFUSION, p->flDiffusion,
				   DSPROPSETID_I3DL2_ListenerProperties,
				   &dwChanged, m_pReverbPropSet);
		SendReverbProperty(A3DREVERB_CHANGED_CUSTOM_DENSITY, p->flDensity,
				   DSPROPSETID_I3DL2_ListenerProperties,
				   &dwChanged, m_pReverbPropSet);
		SendReverbProperty(A3DREVERB_CHANGED_CUSTOM_HFREFERENCE, p->flHFReference,
				   DSPROPSETID_I3DL2_ListenerProperties,
				   &dwChanged, m_pReverbPropSet);
	}

	return (S_OK);
}

/* =============================================================
// SendReverbEaxPreset()
// (RE) rtl:0x10001F30; dbg:0x100092A0
//
// Send changed preset listener properties through EAX.
//
// Returns: S_OK; property-write failures are ignored.
// =============================================================*/

HRESULT
CA3dRoot::SendReverbEaxPreset(void)
{
A3DREVERB_PRESET       *p;
DWORD                   dwChanged;

	ASSERT(m_pBoundedReverb != 0 &&
	       !IsBadReadPtr(m_pBoundedReverb, sizeof(CA3dReverb)));
	ASSERT(m_pReverbPropSet != 0 &&
	       !IsBadReadPtr(m_pReverbPropSet, sizeof(IA3dPropertySet)));

	dwChanged = m_pBoundedReverb->m_dwChangedPending;

	if (dwChanged & A3DREVERB_CHANGED_PRESET_ALL)
	{
		p = &m_pBoundedReverb->m_Properties.uval.preset;

		SendReverbProperty(A3DREVERB_CHANGED_ENVIRONMENT, p->dwEnvPreset,
				   DSPROPSETID_EAX_ReverbProperties,
				   &dwChanged, m_pReverbPropSet);
		SendReverbProperty(A3DREVERB_CHANGED_VOLUME, p->fVolume,
				   DSPROPSETID_EAX_ReverbProperties,
				   &dwChanged, m_pReverbPropSet);
		SendReverbProperty(A3DREVERB_CHANGED_DECAYTIME, p->fDecayTime,
				   DSPROPSETID_EAX_ReverbProperties,
				   &dwChanged, m_pReverbPropSet);
		SendReverbProperty(A3DREVERB_CHANGED_DAMPING, p->fDamping,
				   DSPROPSETID_EAX_ReverbProperties,
				   &dwChanged, m_pReverbPropSet);
	}

	return (S_OK);
}

/* =============================================================
// SendSourceI3dl2DirectHF()
// (RE) dbg:0x10009550; inlined in rtl:0x100020B0
//
// Send source direct-path high-frequency gain using the original
// integer-truncated linear-gain conversion.
// =============================================================*/

void
CA3dRoot::SendSourceI3dl2DirectHF(CA3dSource *pSource)
{
LPA3DPROPERTYSET	pPropSet;
LONG			lDirectHF;

	ASSERT(pSource != 0 && !IsBadReadPtr(pSource, sizeof(CA3dSource)));

	pPropSet = pSource->m_pReverbPropSet;

	ASSERT(pPropSet != 0 &&
	       !IsBadReadPtr(pPropSet, sizeof(IA3dPropertySet)));

	lDirectHF = (LONG) pow(10.0, pSource->m_fReverbDirectHF / 2000.0);

	VERIFY(SUCCEEDED(pPropSet->Set(DSPROPSETID_I3DL2_BufferProperties,
				       DSPROPERTY_I3DL2BUFFER_DIRECTHF, 0, 0,
				       &lDirectHF, sizeof(LONG), 0x00000001)));
}

/* =============================================================
// SendSourceEaxReverbMix()
// (RE) dbg:0x100096D0; inlined in rtl:0x100020B0
//
// Send the source reverb mix through its EAX property interface.
// =============================================================*/

void
CA3dRoot::SendSourceEaxReverbMix(CA3dSource *pSource)
{
LPA3DPROPERTYSET	pPropSet;
FLOAT			fReverbMix;

	ASSERT(pSource != 0 && !IsBadReadPtr(pSource, sizeof(CA3dSource)));

	pPropSet = pSource->m_pReverbPropSet;

	ASSERT(pPropSet != 0 &&
	       !IsBadReadPtr(pPropSet, sizeof(IA3dPropertySet)));

	fReverbMix = pSource->m_fReverbMix;

	VERIFY(SUCCEEDED(pPropSet->Set(DSPROPSETID_EAXBUFFER_ReverbProperties,
				       DSPROPERTY_EAXBUFFER_REVERBMIX, 0, 0,
				       &fReverbMix, sizeof(float), 0x00000001)));
}

/* =============================================================
// FlushReverb()
// (RE) rtl:0x100020B0; dbg:0x10009830
//
// Apply pending listener properties and traced sources' reverb levels.
// The original clears source dirty flags without testing them.
// =============================================================*/

void
CA3dRoot::FlushReverb(void)
{
CA3dStdList<CA3dSource *>::iterator	it;
CA3dSource				*pSource;

	if (!m_pBoundedReverb)
		return;

	if (m_pBoundedReverb->m_Properties.dwType != m_dwLastReverbType)
	{
		ResetReverbVocabularyDefaults();

		m_dwLastReverbType = m_pBoundedReverb->m_Properties.dwType;
	}

	if (m_pBoundedReverb->m_dwChangedPending)
	{
		if (m_pBoundedReverb->m_Properties.dwType == A3DREVERB_TYPE_PRESET)
		{
			if (m_dwReverbVocabulary & A3D_REVERB_VOCABULARY_EAX)
				SendReverbEaxPreset();
		}
		else if (m_pBoundedReverb->m_Properties.dwType == A3DREVERB_TYPE_CUSTOM)
		{
			if (m_dwReverbVocabulary & A3D_REVERB_VOCABULARY_I3DL2)
				SendReverbI3dl2();
			else if (m_dwReverbVocabulary & A3D_REVERB_VOCABULARY_EAX)
				SendReverbEaxCustom();
		}

		m_pBoundedReverb->ClearChangedPending();
	}

	for (it = m_listReverbSources.begin(); it != m_listReverbSources.end(); ++it)
	{
		pSource = *it;

		if (pSource->m_fReverbMix >= 0.0f ||
		    pSource->m_fReverbMix == A3D_REVERB_MIX_DEFAULT)
		{
			if (m_pBoundedReverb->m_Properties.dwType == A3DREVERB_TYPE_PRESET)
			{
				if (m_dwReverbVocabulary & A3D_REVERB_VOCABULARY_EAX)
					SendSourceEaxReverbMix(pSource);
			}
			else if (m_pBoundedReverb->m_Properties.dwType == A3DREVERB_TYPE_CUSTOM)
			{
				if (m_dwReverbVocabulary & A3D_REVERB_VOCABULARY_I3DL2)
					SendSourceI3dl2DirectHF(pSource);
				else if (m_dwReverbVocabulary & A3D_REVERB_VOCABULARY_EAX)
					SendSourceEaxReverbMix(pSource);
			}
		}

		pSource->m_dwReverbMixDirty = 0;
	}

	m_listReverbSources.clear();
}

/* =============================================================
// ResetReverbVocabularyDefaults()
// (RE) rtl:0x10002470; dbg:0x10009CF0
//
// Restore listener defaults for each detected reverb vocabulary.
// =============================================================*/

void
CA3dRoot::ResetReverbVocabularyDefaults(void)
{
	if (m_dwReverbVocabulary & A3D_REVERB_VOCABULARY_I3DL2)
		SendI3dl2ListenerDefaults();

	if (m_dwReverbVocabulary & A3D_REVERB_VOCABULARY_EAX)
		SendEaxReverbDefaults();
}

/* =============================================================
// SendI3dl2ListenerDefaults()
// (RE) rtl:0x10002540; dbg:0x10009D50
//
// Send the I3DL2 listener default block when a property interface exists.
// =============================================================*/

void
CA3dRoot::SendI3dl2ListenerDefaults(void)
{
I3DL2_LISTENERPROPERTIES	I3dl2LisProp;

	if (!m_pReverbPropSet)
		return;

	I3dl2LisProp.lRoom               = (-10000);
	I3dl2LisProp.lRoomHF             = 0;
	I3dl2LisProp.flRoomRolloffFactor = 0.0f;
	I3dl2LisProp.flDecayTime         = 1.0f;
	I3dl2LisProp.flDecayHFRatio      = 0.5f;
	I3dl2LisProp.lReflections        = (-10000);
	I3dl2LisProp.flReflectionsDelay  = 0.02f;
	I3dl2LisProp.lReverb             = (-10000);
	I3dl2LisProp.flReverbDelay       = 0.04f;
	I3dl2LisProp.flDiffusion         = 100.0f;
	I3dl2LisProp.flDensity           = 100.0f;
	I3dl2LisProp.flHFReference       = 5000.0f;

	VERIFY(SUCCEEDED(m_pReverbPropSet->Set(
			DSPROPSETID_I3DL2_ListenerProperties,
			DSPROPERTY_I3DL2LISTENER_ALL, 0, 0, &I3dl2LisProp,
			sizeof(I3DL2_LISTENERPROPERTIES), 0x00000001)));
}

/* =============================================================
// SendEaxReverbDefaults()
// (RE) dbg:0x10009E70; inlined in rtl:0x100020B0
//
// Send a zeroed EAX listener block when a property interface exists.
// The redundant volume store reproduces Debug.
// =============================================================*/

void
CA3dRoot::SendEaxReverbDefaults(void)
{
EAX_REVERBPROPERTIES	props;

	if (!m_pReverbPropSet)
		return;

	memset(&props, 0, sizeof(props));

	props.fVolume = 0.0f;

	VERIFY(SUCCEEDED(m_pReverbPropSet->Set(
			DSPROPSETID_EAX_ReverbProperties,
			DSPROPERTY_EAX_ALL, 0, 0, &props, sizeof(props),
			0x00000001)));
}

/* =============================================================
// InitReverb()
// (RE) dbg:0x10009F40
//
// Acquire the reverb property interface and detect its supported vocabularies.
//
// Returns:
//   S_OK    if a vocabulary is available or was already found
//   E_FAIL  if the interface or both vocabularies are unavailable
// =============================================================*/

HRESULT
CA3dRoot::InitReverb(void)
{
	if (m_dwReverbSearched)
		return (S_OK);

#if defined(A3D_FIXES)
	if (A3dGetConfig().bSoftwareReverb)
	{
		/* NOT PART OF THE ORIGINAL.  Supply an emulated property set for use
		 * without an Aureal or I3DL2/EAX device. The 677 vocabulary detection,
		 * m_dwReverbVocabulary flags and Set() dispatch still apply.
		 * See A3dReverbEmu.h.
		*/
		m_pReverbPropSet = new CA3dReverbEmuPropertySet;
		m_pReverbPropSet->AddRef();
	}
	else
#endif
	if (FAILED(m_pDirectSound->QueryInterface(IID_IA3dPropertySet,
						  (void **) &m_pReverbPropSet)))
	{
		DBGSTR("CA3dRoot::InitReverb() - Device does not support Property Sets.\n");

		return (E_FAIL);
	}

	DetectEaxVocabulary();
	DetectI3dl2Vocabulary();

	if (!m_dwReverbVocabulary)
	{
		DBGSTR("CA3dRoot::InitReverb() - Couldn't find EAX or I3DL2 support from device.\n");

		return (E_FAIL);
	}

	m_dwReverbSearched = 1;

	return (S_OK);
}

/* =============================================================
// DetectI3dl2Vocabulary()
// (RE) rtl:0x100025C0; dbg:0x1000A020
//
// Record I3DL2 support after checking listener and buffer access and
// registering the initial buffer state.
// =============================================================*/

void
CA3dRoot::DetectI3dl2Vocabulary(void)
{
I3DL2_BUFFERPROPERTIES	I3dl2BufProp;
ULONG			ulSupport;

	ASSERT(m_pReverbPropSet != 0 &&
	       !IsBadReadPtr(m_pReverbPropSet, sizeof(IA3dPropertySet)));

	ulSupport = 0;

	if (FAILED(m_pReverbPropSet->QuerySupport(
			DSPROPSETID_I3DL2_ListenerProperties,
			DSPROPERTY_I3DL2LISTENER_ALL, &ulSupport)))
		return;

	if ((ulSupport & (KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET)) !=
	    (KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET))
		return;

	ulSupport = 0;

	if (FAILED(m_pReverbPropSet->QuerySupport(
			DSPROPSETID_I3DL2_BufferProperties,
			0, &ulSupport)))
		return;

	if ((ulSupport & (KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET)) !=
	    (KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET))
		return;

	memset(&I3dl2BufProp, 0, sizeof(I3dl2BufProp) - sizeof(FLOAT));

	I3dl2BufProp.flOcclusionLFRatio = 0.25f;

	if (FAILED(m_pReverbPropSet->AddInitialStateParameters(
			DSPROPSETID_I3DL2_BufferProperties, 0, NULL, 0,
			&I3dl2BufProp, sizeof(I3dl2BufProp))))
		return;

	m_dwReverbVocabulary |= A3D_REVERB_VOCABULARY_I3DL2;
}

/* =============================================================
// DetectEaxVocabulary()
// (RE) dbg:0x1000A210
//
// Record EAX support after checking listener and buffer access and registering
// the initial reverb mix sentinel; its external effect is unverified.
// =============================================================*/

void
CA3dRoot::DetectEaxVocabulary(void)
{
FLOAT	fInitialState;
ULONG	ulSupport;

	ASSERT(m_pReverbPropSet != 0 &&
	       !IsBadReadPtr(m_pReverbPropSet, sizeof(IA3dPropertySet)));

	ulSupport = 0;

	if (FAILED(m_pReverbPropSet->QuerySupport(
			DSPROPSETID_EAX_ReverbProperties,
			DSPROPERTY_EAX_ALL, &ulSupport)))
		return;

	if ((ulSupport & (KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET)) !=
	    (KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET))
		return;

	ulSupport = 0;

	if (FAILED(m_pReverbPropSet->QuerySupport(
			DSPROPSETID_EAXBUFFER_ReverbProperties,
			DSPROPERTY_EAXBUFFER_REVERBMIX, &ulSupport)))
		return;

	if ((ulSupport & (KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET)) !=
	    (KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET))
		return;

	fInitialState = A3D_REVERB_MIX_DEFAULT;

	if (FAILED(m_pReverbPropSet->AddInitialStateParameters(
			DSPROPSETID_EAXBUFFER_ReverbProperties,
			DSPROPERTY_EAXBUFFER_REVERBMIX, NULL, 0,
			&fInitialState, sizeof(fInitialState))))
		return;

	m_dwReverbVocabulary |= A3D_REVERB_VOCABULARY_EAX;
}

/* =============================================================
// IsFeatureAvailable()
// (RE) rtl:0x10002670; dbg:0x1000A3B0; IA3d5 slot 16
//
// Check whether any requested feature bit is available.
//
// Returns: For IA3d4+, TRUE or FALSE. For earlier interfaces, S_OK if
//          available; A3DERROR_FEATURE_NOT_SUPPORTED if requested but
//          unavailable; A3DERROR_FEATURE_NOT_REQUESTED otherwise.
// =============================================================*/

STDMETHODIMP
CA3dRoot::IsFeatureAvailable(DWORD dwFeature)
{
	if (m_dwInterfaceVersion > 3)
		return ((dwFeature & m_dwFeaturesAvailable) != 0);

	if (dwFeature & m_dwFeaturesAvailable)
		return (S_OK);

	if (dwFeature & m_dwFeaturesRequested)
		return (A3DERROR_FEATURE_NOT_SUPPORTED);

	return (A3DERROR_FEATURE_NOT_REQUESTED);
}

/* =============================================================
// Clear()
// (RE) rtl:0x100026C0; dbg:0x1000A430; IA3d5 slot 12
//
// Release additional geometry pools and reset the first pool.
//
// Returns:
//   S_OK
//   E_FAIL  while a display list is recording
// =============================================================*/

STDMETHODIMP
CA3dRoot::Clear(void)
{
CA3dPool       *pPool;
int             nSize;

	InterlockedIncrement(&m_cRef);

	if (m_cListsRecording > 0)
		return (E_FAIL);

	if (m_fGeomReady)
	{
		nSize = m_GeomPools.size();

		ASSERT(nSize > 0);

		while (nSize > 1)
		{
			pPool = m_GeomPools.back();

			if (pPool->m_dwListOwner)
			{
				((CA3dList *) pPool->m_dwListOwner)->ClearCalled();
				((CA3dList *) pPool->m_dwListOwner)->Release();
			}
			else if (pPool)
			{
				delete pPool;
			}

			m_GeomPools.pop_back();

			nSize--;
		}

		m_pPool = m_GeomPools.front();

		m_pPool->Clear();
	}

	return (S_OK);
}

/* =============================================================
// SetCooperativeLevel()
// (RE) rtl:0x10002780; dbg:0x1000A620; IA3d5 slot 19
//
// Set the backend cooperative level and enable available reverb features.
// The original stores the level and succeeds even if the backend refuses it.
//
// Returns:
//   S_OK
//   A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT
//                                       before initialization
//   E_INVALIDARG                        a null window or unsupported
//                                       cooperative level
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetCooperativeLevel(HWND hWnd, DWORD dwLevel)
{
	if (!m_fInitialized || !m_pDirectSound)
	{
		DBGSTR(
			"CA3dRoot::SetCooperativeLevel() - Function not valid before Init.\n");

		return (A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT);
	}

	if (!hWnd)
	{
		DBGSTR(
			"CA3dRoot::SetCooperativeLevel() - hWnd cannot be NULL.\n");

		return (E_INVALIDARG);
	}

	if (dwLevel != A3D_CL_NORMAL && dwLevel != A3D_CL_EXCLUSIVE)
	{
		DBGSTR(
			"CA3dRoot::SetCooperativeLevel() - dwLevel is not a valid value.\n");

		return (E_INVALIDARG);
	}

	m_pDirectSound->SetCooperativeLevel(hWnd, dwLevel);

	m_dwCoopLevel = dwLevel;

	if (m_dwFeaturesRequested & (A3D_REVERB | A3D_GEOMETRIC_REVERB))
	{
		if (SUCCEEDED(InitReverb()))
			m_dwFeaturesAvailable |= A3D_REVERB;
	}

	if (m_dwFeaturesRequested & A3D_GEOMETRIC_REVERB)
	{
		if (SUCCEEDED(EnableGeometricReverb()))
			m_dwFeaturesAvailable |= A3D_GEOMETRIC_REVERB;
	}

	return (S_OK);
}

/* =============================================================
// GetCooperativeLevel()
// (RE) rtl:0x100028F0; dbg:0x1000A770
//
// Read the stored cooperative level.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetCooperativeLevel(LPDWORD pdwLevel)
{
	if (!pdwLevel)
	{
		return (E_POINTER);
	}

	*pdwLevel = m_dwCoopLevel;

	return (S_OK);
}

/* =============================================================
// SetMaxReflectionDelayTime()
// (RE) rtl:0x10004500; dbg:0x1000A7C0
//
// Set the maximum reflection delay in seconds.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a negative delay
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetMaxReflectionDelayTime(A3DVAL fSeconds)
{
	if (fSeconds < 0.0f)
	{
		DBGSTR("CA3dRoot::SetMaxReflectionDelayTime() - fSeconds cannot be less than 0.0\n");

		return (E_INVALIDARG);
	}

	m_fMaxReflectionDelayTime = fSeconds;

	return (S_OK);
}

/* =============================================================
// GetMaxReflectionDelayTime()
// (RE) rtl:0x10002920; dbg:0x1000A820
//
// Read the maximum reflection delay in seconds.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetMaxReflectionDelayTime(LPA3DVAL pfSeconds)
{
	if (!pfSeconds)
	{
		DBGSTR("CA3dRoot::GetMaxReflectionDelayTime() - pfSeconds is NULL.\n");

		return (E_POINTER);
	}

	*pfSeconds = m_fMaxReflectionDelayTime;

	return (S_OK);
}

/* =============================================================
// SetOutputGain()
// (RE) rtl:0x10002950; dbg:0x1000A870
//
// Set the listener output gain. The original rejects NaN; this comparison
// accepts it under MSVC 19, including with /arch:IA32.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a gain below 0.0 or above 1.0
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetOutputGain(A3DVAL fGain)
{
	if (fGain < 0.0f || fGain > 1.0f)
	{
		TRACE("CA3dRoot::SetOutputGain() - fGain must be a value "
		      "between 0.0->1.0.\n\tValue Passed: %f.\n", fGain);
		return (E_INVALIDARG);
	}

	m_fOutputGain = fGain;

	return (S_OK);
}

/* =============================================================
// GetOutputGain()
// (RE) rtl:0x10002990; dbg:0x1000A900
//
// Read the listener output gain.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetOutputGain(LPA3DVAL pfGain)
{
	if (!pfGain)
	{
		return (E_POINTER);
	}

	*pfGain = m_fOutputGain;

	return (S_OK);
}

/* =============================================================
// SetDopplerScale()
// (RE) rtl:0x100029C0; dbg:0x1000A950
//
// Set the global Doppler scale.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a negative scale
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetDopplerScale(A3DVAL fDoppler)
{
	if (fDoppler < 0.0f)
	{
		DBGSTR("CA3dRoot::SetDopplerScale() - fDoppler cannot be less than 0.0\n");

		return (E_INVALIDARG);
	}

	m_fDopplerScale = fDoppler;

	return (S_OK);
}

/* =============================================================
// GetDopplerScale()
// (RE) rtl:0x100029F0; dbg:0x1000A9A0
//
// Read the global Doppler scale.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetDopplerScale(LPA3DVAL pfDoppler)
{
	if (!pfDoppler)
	{
		DBGSTR("CA3dRoot::GetDopplerScale() - lpfDoppler is NULL.\n");

		return (E_POINTER);
	}

	*pfDoppler = m_fDopplerScale;

	return (S_OK);
}

/* =============================================================
// SetDistanceModelScale()
// (RE) rtl:0x10002A20; dbg:0x1000A9F0
//
// Set the global distance-model scale.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a negative scale
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetDistanceModelScale(A3DVAL fDistanceModelScale)
{
	if (fDistanceModelScale < 0.0f)
	{
		DBGSTR("CA3dRoot::SetDistanceModelScale() - fDistanceModelScale cannot be less than 0.0\n");

		return (E_INVALIDARG);
	}

	m_fDistanceModelScale = fDistanceModelScale;

	return (S_OK);
}

/* =============================================================
// GetDistanceModelScale()
// (RE) rtl:0x10002A50; dbg:0x1000AA40
//
// Read the global distance-model scale.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetDistanceModelScale(LPA3DVAL pfDistanceModelScale)
{
	if (!pfDistanceModelScale)
	{
		DBGSTR("CA3dRoot::GetDistanceModelScale() - lpfDistanceModelScale is NULL.\n");

		return (E_POINTER);
	}

	*pfDistanceModelScale = m_fDistanceModelScale;

	return (S_OK);
}

/* =============================================================
// SetEq()
// (RE) rtl:0x10002A80; dbg:0x1000AA90
//
// Set the global equalization factor and its clamped polynomial curve.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a factor below 0.0 or above 1.0
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetEq(A3DVAL fEq)
{
	if (fEq < 0.0f || fEq > 1.0f)
	{
		return (E_INVALIDARG);
	}

	m_fEq = fEq;

	m_fEqCurve =   3.7264376f  * fEq
			  - 17.449827f  * fEq * fEq
			  + 43.63063f   * fEq * fEq * fEq
			  - 52.999165f  * fEq * fEq * fEq * fEq
			  + 27.087683f  * fEq * fEq * fEq * fEq * fEq
			  +  0.77741671f * fEq * fEq * fEq * fEq * fEq * fEq
			  -  3.7731748f * fEq * fEq * fEq * fEq * fEq * fEq * fEq;

	if (m_fEqCurve >= 0.0f)
	{
		if (m_fEqCurve > 1.0f)
			m_fEqCurve = 1.0f;
	}
	else
	{
		m_fEqCurve = 0.0f;
	}

	return (S_OK);
}

/* =============================================================
// GetEq()
// (RE) rtl:0x10002B70; dbg:0x1000AC10
//
// Read the global equalization factor.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetEq(LPA3DVAL pfEq)
{
	if (!pfEq)
	{
		return (E_POINTER);
	}

	*pfEq = m_fEq;

	return (S_OK);
}

/* =============================================================
// Flush()
// (RE) rtl:0x10002BA0; dbg:0x1000AC60; IA3d5 slot 13
//
// Update frame timing, trace sources and submit reverb changes.
//
// Returns:
//   S_OK
//   E_FAIL  while a display list is recording
// =============================================================*/

STDMETHODIMP
CA3dRoot::Flush(void)
{
double	dNow;
DWORD	dwRefOrders;

	InterlockedIncrement(&m_cRef);

	if (m_cListsRecording > 0)
		return (E_FAIL);

	dNow = (double) timeGetTime();

	m_fFrameTime = (A3DVAL) ((dNow - g_fLastFlush) * 0.001f);

	if (m_fFrameTime < 0.0f || m_fFrameTime > A3D_FLUSH_MAX_FRAME_SECONDS)
		m_fFrameTime = g_fLastFrameTime;

	g_fLastFrameTime = m_fFrameTime;
	g_fLastFlush     = (A3DVAL) dNow;

	if (dNow - g_fLastRefOrders > A3D_REF_ORDERS_POLL_INTERVAL_MS)
	{
		g_fLastRefOrders = (A3DVAL) dNow;

		dwRefOrders = 0;

		if (SUCCEEDED(ReadA3dRegistryDword("ref_orders",
						 (LPBYTE) &dwRefOrders)))
		{
			if (dwRefOrders > 2)
				Compat(A3DCOMPAT_REF_ORDERS, 2);
			else if (dwRefOrders == 1)
				Compat(A3DCOMPAT_REF_ORDERS, 1);
		}
	}

	if (m_dwInterfaceVersion > 3)
		Trace();
	else
		TraceLegacy();

	FlushReverb();

	return (S_OK);
}

/* =============================================================
// NewSource()
// (RE) rtl:0x10002D00; dbg:0x1000AE00; IA3d5 slot 17
//
// Create a source COM wrapper with one client reference.
//
// Returns: S_OK; A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT before initialization;
//          E_INVALIDARG for a type above 0x2D; E_POINTER for a null output;
//          primary-buffer creation failure; E_OUTOFMEMORY if wrapper allocation
//          fails.
// =============================================================*/

STDMETHODIMP
CA3dRoot::NewSource(DWORD dwType, LPA3DSOURCE2 *ppSource)
{
CA3dSourceCom  *pSource;
HRESULT         hr;

	InterlockedIncrement(&m_cRef);

	if (!m_fInitialized || !m_pDirectSound)
	{
		DBGSTR("CA3dRoot::NewSource() - Function not valid before Init.\n");

		return (A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT);
	}

	if (dwType > A3D_NEW_SOURCE_MAX_TYPE_VALUE)
	{
		DBGSTR("CA3dRoot::NewSource() - dwType is an invalid value.\n");

		return (E_INVALIDARG);
	}

	if (!ppSource)
	{
		DBGSTR("CA3dRoot::NewSource() - ppA3dSource is NULL.\n");

		return (E_POINTER);
	}

	*ppSource = NULL;

	if (!m_dwPrimaryBufferBuilt)
	{
		hr = NewPrimaryBuffer();

		if (FAILED(hr))
			return (hr);
	}

	pSource = new CA3dSourceCom(m_pDirectSound, this, dwType);

	if (!pSource)
	{
		DBGSTR("CA3dRoot::NewSource() - Failed to allocate memory for new Source object.\n");

		return (E_OUTOFMEMORY);
	}

	*ppSource = (LPA3DSOURCE2) (IA3dSource2 *) pSource;

	return (S_OK);
}

/* =============================================================
// NewPrimaryBuffer()
// (RE) dbg:0x1000AFC0
//
// Create the primary sound buffer and, for a nonzero backend, acquire its
// 3D listener with distance attenuation disabled.
//
// Returns:
//   S_OK
//   A3DERROR_FAILED_CREATE_PRIMARY_BUFFER
//                                       if buffer creation fails
// =============================================================*/

HRESULT
CA3dRoot::NewPrimaryBuffer(void)
{
DSBUFFERDESC	dsbd;
HRESULT		hr;

	ZeroMemory(&dsbd, sizeof(dsbd));

	dsbd.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D;
	dsbd.dwSize  = sizeof(dsbd);

	if (FAILED(m_pDirectSound->CreateSoundBuffer(&dsbd, &m_pPrimary, NULL)))
	{
		DBGSTR("CA3dRoot::NewPrimaryBuffer() - Failed CreateSoundBuffer() call.\n");

		return (A3DERROR_FAILED_CREATE_PRIMARY_BUFFER);
	}

	m_dwPrimaryBufferBuilt = 1;

	if (m_nBackend && m_pPrimary)
	{
		hr = m_pPrimary->QueryInterface(IID_IDirectSound3DListener,
						(void **) &m_pDS3dListener);

		if (SUCCEEDED(hr))
			m_pDS3dListener->SetRolloffFactor(0.0f, 0);
	}

	return (S_OK);
}

/* =============================================================
// ReportError()
// (RE) dbg:0x1000B100; thunk dbg:0x100043EF
//
// Print a diagnostic and retain successful HRESULTs. The original drops
// failures.
//
// Returns: The supplied HRESULT on success; S_OK on failure.
// =============================================================*/

HRESULT
CA3dRoot::ReportError(HRESULT hr, LPCSTR pszMessage)
{
	DBGSTR(pszMessage);

	if (!SUCCEEDED(hr))
		return (S_OK);

	m_hrReported = hr;

	return (hr);
}

/* =============================================================
// RemoveSource()
// (RE) dbg:0x1000B150
//
// Remove a registered source from the engine list.
// =============================================================*/

void
CA3dRoot::RemoveSource(CA3dSource *pA3dSource)
{
	InterlockedIncrement(&m_cRef);

	ASSERT(pA3dSource != 0 &&
	       !IsBadReadPtr(pA3dSource, sizeof(CA3dSource)));
	ASSERT(LIST_HAS(m_SourceArray, (CA3dSource *) pA3dSource));

	m_SourceArray.remove(pA3dSource);
}

/* =============================================================
// DuplicateSource()
// (RE) rtl:0x10002EF0; dbg:0x1000B270; IA3d5 slot 18
//
// Duplicate a registered nonstreaming wave source. A null allocation can
// leave S_OK and the output unchanged.
//
// Returns:
//   S_OK                                or the copy-constructor failure
//   E_POINTER                           a null argument
//   A3DERROR_CANT_DUPLICATE_STREAM_SRC  AC3 or streaming sources
//   A3DERROR_NOT_VALID_SOURCE           an unregistered source
//   A3DERROR_NO_WAVE_DATA               without an audio buffer
// =============================================================*/

STDMETHODIMP
CA3dRoot::DuplicateSource(LPA3DSOURCE2 pSource, LPA3DSOURCE2 *ppSource)
{
CA3dSourceCom  *pOriginal;
CA3dSourceCom  *pDuplicate;
CA3dSource     *pA3dSource;
HRESULT         hr;

	InterlockedIncrement(&m_cRef);

	if (!pSource || !ppSource)
	{
		DBGSTR(
			"CA3dRoot::DuplicateSource() - pOriginalCom and/or ppDuplicateCom are NULL.\n");

		return (E_POINTER);
	}

	pOriginal = (CA3dSourceCom *) pSource;

	if (pOriginal->m_dwPointerType != A3D_SOURCE_POINTER_WAVE)
		return (A3DERROR_CANT_DUPLICATE_STREAM_SRC);

	pA3dSource = (CA3dSource *) pOriginal->m_pSource;

	if (!m_SourceArray.contains(pA3dSource))
		return (A3DERROR_NOT_VALID_SOURCE);

	if (pA3dSource->m_fStreaming)
	{
		DBGSTR(
			"CA3dRoot::DuplicateSource() - Cannot duplicate a streaming source.\n");

		return (A3DERROR_CANT_DUPLICATE_STREAM_SRC);
	}

	if (!pA3dSource->m_pBuffer)
	{
		DBGSTR(
			"CA3dRoot::DuplicateSource() - Cannot duplicate a source that contains no audio data.\n");

		return (A3DERROR_NO_WAVE_DATA);
	}

	hr = S_OK;

	pDuplicate = new CA3dSourceCom(pOriginal, &hr);

	if (FAILED(hr) || !pDuplicate)
	{
		DBGSTR(
			"CA3dRoot::DuplicateSource() - Failed memory allocation of CA3dSourceCom object.\n");

		return (hr);
	}

	*ppSource = (LPA3DSOURCE2) pDuplicate;

	m_SourceArray.push_back((CA3dSource *) pDuplicate->m_pSource);

	return (S_OK);
}

/* =============================================================
// GetStreamingProperties()
// (RE) rtl:0x100030A0; dbg:0x1000B480
//
// Read default streaming buffer length and thread priority.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetStreamingProperties(DWORD *pdwBufferLength, DWORD *pdwThreadPriority)
{
	if (!pdwBufferLength || !pdwThreadPriority)
	{
		DBGSTR("CA3dRoot::GetStreamingProperties() - dwBufferLength and/or dwThreadPriority are NULL.\n");

		return (E_POINTER);
	}

	*pdwBufferLength   = m_dwStreamBufferLength;
	*pdwThreadPriority = m_dwStreamThreadPriority;

	return (S_OK);
}

/* =============================================================
// SetStreamingProperties()
// (RE) rtl:0x100030E0; dbg:0x1000B4F0
//
// Set streaming defaults. The original changes buffer length before
// rejecting an invalid priority.
//
// Returns:
//   S_OK
//   A3DERROR_STREAMING_BUFFER_LENGTH  outside 1..10000 ms
//   A3DERROR_STREAMING_PRIORITY       above priority 2
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetStreamingProperties(DWORD dwBufferLength, DWORD dwThreadPriority)
{
	if (!dwBufferLength || dwBufferLength > A3D_STREAM_MAX_BUFFER_LENGTH_MS)
	{
		DBGSTR("CA3dRoot::SetStreamingProperties() - dwBufferLength must be a value between 1 and 10,000.\n");

		return (A3DERROR_STREAMING_BUFFER_LENGTH);
	}

	m_dwStreamBufferLength = dwBufferLength;

	if (dwThreadPriority > A3D_STREAMING_PRIORITY_HIGHEST)
		return (A3DERROR_STREAMING_PRIORITY);

	m_dwStreamThreadPriority        = dwThreadPriority;
	m_dwStreamingPropertiesSet      = 1;

	return (S_OK);
}

/* =============================================================
// A3dEnumerate()
// (RE) rtl:0x10003130; dbg:0x1000B590; IA3d5 slot 47
//
// Enumerate DirectSound devices. The original passes ANSI strings despite
// the SDK callback declaring LPCWSTR; its internal prototype is unknown.
//
// Returns:
//   S_OK
//   E_POINTER  a null callback
//   E_FAIL     if enumeration fails
// =============================================================*/

STDMETHODIMP
CA3dRoot::A3dEnumerate(LPA3DENUMCALLBACK lpA3dEnumCallback, LPVOID lpContext)
{
	if (!lpA3dEnumCallback)
	{
		DBGSTR("CA3dRoot::A3dEnumerate() - lpA3dEnumCallback is NULL.\n");

		return (E_POINTER);
	}

	if (FAILED(DirectSoundEnumerateA((LPDSENUMCALLBACKA) lpA3dEnumCallback,
					 lpContext)))
		return (E_FAIL);

	return (S_OK);
}

/* =============================================================
// DsGetCaps()
// (RE) rtl:0x10003170; dbg:0x1000B5F0; delegates IDirectSound slot 4
//
// Read backend DirectSound capabilities.
//
// Returns: IDirectSound::GetCaps result.
// =============================================================*/

HRESULT
CA3dRoot::DsGetCaps(LPDSCAPS pCaps)
{
	ASSERT(m_pDirectSound != 0 &&
	       !IsBadReadPtr(m_pDirectSound, sizeof(IDirectSound)));

	return (m_pDirectSound->GetCaps(pCaps));
}

/* =============================================================
// DsCompact()
// (RE) rtl:0x10003190; dbg:0x1000B690; delegates IDirectSound slot 7
//
// Compact backend DirectSound buffer memory.
//
// Returns: IDirectSound::Compact result.
// =============================================================*/

HRESULT
CA3dRoot::DsCompact(void)
{
	ASSERT(m_pDirectSound != 0 &&
	       !IsBadReadPtr(m_pDirectSound, sizeof(IDirectSound)));

	return (m_pDirectSound->Compact());
}

/* =============================================================
// DsSetSpeakerConfig()
// (RE) rtl:0x100031A0; dbg:0x1000B730; delegates IDirectSound slot 9
//
// Set the backend DirectSound speaker configuration.
//
// Returns: IDirectSound::SetSpeakerConfig result.
// =============================================================*/

HRESULT
CA3dRoot::DsSetSpeakerConfig(DWORD dwConfig)
{
	ASSERT(m_pDirectSound != 0 &&
	       !IsBadReadPtr(m_pDirectSound, sizeof(IDirectSound)));

	return (m_pDirectSound->SetSpeakerConfig(dwConfig));
}

/* =============================================================
// DsGetSpeakerConfig()
// (RE) rtl:0x100031C0; dbg:0x1000B7D0; delegates IDirectSound slot 8
//
// Read the backend DirectSound speaker configuration.
//
// Returns: IDirectSound::GetSpeakerConfig result.
// =============================================================*/

HRESULT
CA3dRoot::DsGetSpeakerConfig(DWORD *pdwConfig)
{
	ASSERT(m_pDirectSound != 0 &&
	       !IsBadReadPtr(m_pDirectSound, sizeof(IDirectSound)));

	return (m_pDirectSound->GetSpeakerConfig(pdwConfig));
}

/* =============================================================
// SetCoordinateSystem()
// (RE) rtl:0x100031E0; dbg:0x1000B870; IA3d5 slot 23
//
// Select the coordinate system once.
//
// Returns: S_OK; A3DERROR_COORD_SYSTEM_CAN_ONLY_BE_SET_ONCE after selection;
//          otherwise E_INVALIDARG for an unsupported coordinate system.
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetCoordinateSystem(DWORD dwSystem)
{
	if (m_dwCoordSystemSet)
	{
		DBGSTR("CA3dRoot::SetCoordinateSystem() - This function may only be called once.\n");

		return (A3DERROR_COORD_SYSTEM_CAN_ONLY_BE_SET_ONCE);
	}

	if (dwSystem > A3D_LEFT_HANDED_CS)
		return (E_INVALIDARG);

	m_dwCoordSystem         = dwSystem;
	m_dwCoordSystemSet      = 1;

	return (S_OK);
}

/* =============================================================
// GetCoordinateSystem()
// (RE) rtl:0x10003220; dbg:0x1000B900
//
// Read the selected coordinate system.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetCoordinateSystem(LPDWORD pdwCoordSystem)
{
	if (!pdwCoordSystem)
	{
		DBGSTR("CA3dRoot::GetCoordinateSystem() - pdwCoordSystem is NULL.\n");

		return (E_POINTER);
	}

	*pdwCoordSystem = m_dwCoordSystem;

	return (S_OK);
}

/* =============================================================
// SetNumFallbackSources()
// (RE) rtl:0x10003250; dbg:0x1000B950; delegates IA3dPrv4 slot 10
//
// Set the backend hardware-source limit through the fallback-source API.
//
// Returns:
//   IA3dPrv4::SetHardwareSourceLimit result
//   A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT
//                                       before initialization
//   A3DERROR_FEATURE_NOT_SUPPORTED      without IA3dPrv4
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetNumFallbackSources(DWORD dwSources)
{
IA3dPrv4       *pPrv4;
HRESULT         hr;

	if (!m_fInitialized || !m_pDirectSound)
	{
		DBGSTR("CA3dRoot::SetNumFallbackSources() - Function not valid before Init.\n");

		return (A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT);
	}

	pPrv4 = NULL;

	if (FAILED(m_pDirectSound->QueryInterface(IID_IA3dPrv4, (void **) &pPrv4)))
	{
		DBGSTR("CA3dRoot::SetNumFallbackSources() - Feature not supported with the A3D 2.x+ Resource Manager.\n");

		return (A3DERROR_FEATURE_NOT_SUPPORTED);
	}

	hr = pPrv4->SetHardwareSourceLimit(dwSources);

	if (pPrv4)
	{
		pPrv4->Release();

		pPrv4 = NULL;
	}

	return (hr);
}

/* =============================================================
// GetNumFallbackSources()
// (RE) rtl:0x100032C0; dbg:0x1000BA40; delegates IA3dPrv4 slot 9
//
// Read the backend hardware-source limit through the fallback-source API.
//
// Returns:
//   IA3dPrv4::GetHardwareSourceLimit result
//   E_POINTER                           a null output
//   A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT
//                                       before initialization
//   A3DERROR_FEATURE_NOT_SUPPORTED      without IA3dPrv4
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetNumFallbackSources(LPDWORD pdwSources)
{
IA3dPrv4       *pPrv4;
HRESULT         hr;

	if (!pdwSources)
	{
		DBGSTR("CA3dRoot::GetNumFallbackSources() - pdwSources is NULL.\n");

		return (E_POINTER);
	}

	if (!m_fInitialized || !m_pDirectSound)
	{
		DBGSTR("CA3dRoot::GetNumFallbackSources() - Function not valid before Init.\n");

		return (A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT);
	}

	pPrv4 = NULL;

	if (FAILED(m_pDirectSound->QueryInterface(IID_IA3dPrv4, (void **) &pPrv4)))
	{
		DBGSTR("CA3dRoot::GetNumFallbackSources() - Feature not supported without the A3D 2.x+ Resource Manager.\n");

		return (A3DERROR_FEATURE_NOT_SUPPORTED);
	}

	hr = pPrv4->GetHardwareSourceLimit(pdwSources);

	if (pPrv4)
	{
		pPrv4->Release();

		pPrv4 = NULL;
	}

	return (hr);
}

/* =============================================================
// SetRMPriorityBias()
// (RE) rtl:0x10003340; dbg:0x1000BB50; delegates IA3dPrv4 slot 4
//
// Set the backend resource-manager priority weight.
//
// Returns:
//   IA3dPrv4::SetPriorityWeight result
//   E_INVALIDARG                        below 0.0 or above 1.0
//   A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT
//                                       before initialization
//   A3DERROR_FEATURE_NOT_SUPPORTED      without IA3dPrv4
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetRMPriorityBias(A3DVAL fBias)
{
IA3dPrv4       *pPrv4;
HRESULT         hr;

	if (fBias < 0.0f || fBias > 1.0f)
	{
		TRACE("CA3dRoot::SetRMPriorityBias() - fBias must be between "
		      "0.0 -> 1.0.\n\tValue Passed: %f.\n", fBias);
		return (E_INVALIDARG);
	}

	if (!m_fInitialized || !m_pDirectSound)
	{
		DBGSTR("CA3dRoot::SetRMPriorityBias() - Function not valid before Init.\n");

		return (A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT);
	}

	pPrv4 = NULL;

	if (FAILED(m_pDirectSound->QueryInterface(IID_IA3dPrv4, (void **) &pPrv4)))
	{
		DBGSTR("CA3dRoot::SetRMPriorityBias() - Feature not supported without the A3D 2.x+ Resource Manager.\n");

		return (A3DERROR_FEATURE_NOT_SUPPORTED);
	}

	hr = pPrv4->SetPriorityWeight(fBias);

	if (pPrv4)
	{
		pPrv4->Release();

		pPrv4 = NULL;
	}

	return (hr);
}

/* =============================================================
// GetRMPriorityBias()
// (RE) rtl:0x100033D0; dbg:0x1000BCA0; delegates IA3dPrv4 slot 3
//
// Read the backend resource-manager priority weight.
//
// Returns:
//   IA3dPrv4::GetPriorityWeight result
//   E_POINTER                           a null output
//   A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT
//                                       before initialization
//   A3DERROR_FEATURE_NOT_SUPPORTED      without IA3dPrv4
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetRMPriorityBias(LPA3DVAL pfBias)
{
IA3dPrv4       *pPrv4;
HRESULT         hr;

	if (!pfBias)
	{
		return (E_POINTER);
	}

	if (!m_fInitialized || !m_pDirectSound)
	{
		return (A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT);
	}

	pPrv4 = NULL;

	if (FAILED(m_pDirectSound->QueryInterface(IID_IA3dPrv4, (void **) &pPrv4)))
	{
		return (A3DERROR_FEATURE_NOT_SUPPORTED);
	}

	hr = pPrv4->GetPriorityWeight(pfBias);

	if (pPrv4)
	{
		pPrv4->Release();

		pPrv4 = NULL;
	}

	return (hr);
}

/* =============================================================
// SetUnitsPerMeter()
// (RE) rtl:0x10003450; dbg:0x1000BDB0
//
// Set the world-unit conversion scale.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a nonpositive scale
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetUnitsPerMeter(A3DVAL fUnitsPerMeter)
{
	if (fUnitsPerMeter <= 0.0f)
	{
		DBGSTR("CA3dRoot::SetUnitsPerMeter() - fUnitsPerMeter must be greater than 0.\n");

		return (E_INVALIDARG);
	}

	m_fUnitsPerMeter = fUnitsPerMeter;

	return (S_OK);
}

/* =============================================================
// GetUnitsPerMeter()
// (RE) rtl:0x10003480; dbg:0x1000BE10
//
// Read the world-unit conversion scale.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetUnitsPerMeter(LPA3DVAL pfUnitsPerMeter)
{
	if (!pfUnitsPerMeter)
	{
		DBGSTR("CA3dRoot::GetUnitsPerMeter() - pfUnitsPerMeter is NULL.\n");

		return (E_POINTER);
	}

	*pfUnitsPerMeter = m_fUnitsPerMeter;

	return (S_OK);
}

/* =============================================================
// Shutdown()
// (RE) rtl:0x100034B0; dbg:0x1000BE60; IA3d5 slot 40
//
// Delete the engine immediately, invalidating outstanding references.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dRoot::Shutdown(void)
{
	InterlockedIncrement(&m_cRef);

	m_fShutdownRequested = 1;

	delete this;

	return (S_OK);
}

/* =============================================================
// RegisterApp()
// (RE) rtl:0x100034F0; dbg:0x1000BEE0; IA3d5 slot 41
//
// Accept application registration without recording it.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP CA3dRoot::RegisterApp(REFIID) { return (S_OK); }

/* =============================================================
// UnlockFallbackAC3Decoder()
// (RE) rtl:0x10003500; dbg:0x1000BF00; IA3d5 slot 48
//
// Reject fallback AC3 decoder unlocking in this build.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
CA3dRoot::UnlockFallbackAC3Decoder(LPSTR, DWORD)
{
	DBGSTR("CA3dRoot::UnlockFallbackAC3Decoder() - Disabled until Dolby changes agreement.\n");

	return (E_NOTIMPL);
}

/* =============================================================
// SetMaxHardwareSources()
// (RE) rtl:0x10003510; dbg:0x1000C0E0; IA3d5 slot 49
//
// Set the resource-manager hardware-source limit.
//
// Returns: S_OK; the resource-manager result is discarded.
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetMaxHardwareSources(DWORD dwCount)
{
	ASSERT(m_pDirectSound);

	((ResMan *) m_pDirectSound)->SetMaxHardwareSources(dwCount);

	return (S_OK);
}

/* =============================================================
// GetMaxHardwareSources()
// (RE) rtl:0x10003530; dbg:0x1000C180; IA3d5 slot 50
//
// Read the resource-manager hardware-source limit.
//
// Returns: S_OK; E_POINTER for a null output. The resource-manager result is
//          discarded.
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetMaxHardwareSources(LPDWORD pdwMaxSources)
{
	ASSERT(m_pDirectSound);

	if (!pdwMaxSources)
	{
		DBGSTR("CA3dRoot::GetMaxHardwareSources - Invalid pointer.\n");

		return (E_POINTER);
	}

	((ResMan *) m_pDirectSound)->GetMaxHardwareSources(pdwMaxSources);

	return (S_OK);
}

/* =============================================================
// BindReverb()
// (RE) rtl:0x100022E0; dbg:0x10009AE0; IA3d5 slot 44
//
// Bind a reverb and reapply source mixes, or restore device defaults on unbind.
//
// Returns:
//   S_OK
//   A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT
//                                       until reverb detection succeeds
//   A3DERROR_FEATURE_NOT_REQUESTED      if reverb was not requested
//   A3DERROR_FEATURE_NOT_SUPPORTED      if unavailable
// =============================================================*/

STDMETHODIMP
CA3dRoot::BindReverb(LPA3DREVERB pReverb)
{
CA3dStdList<CA3dSource *>::iterator	it;
CA3dSource				*pSource;

	if (!m_dwReverbSearched)
	{
		DBGSTR("CA3dRoot::BindReverb() - Function not valid before Init.\n");

		return (A3DERROR_FUNCTION_NOT_VALID_BEFORE_INIT);
	}

	if (!(m_dwFeaturesRequested & A3D_REVERB))
	{
		DBGSTR("CA3dRoot::BindReverb() - A3D_REVERB was not requested at Init.\n");

		return (A3DERROR_FEATURE_NOT_REQUESTED);
	}

	if (!(m_dwFeaturesAvailable & A3D_REVERB))
	{
		DBGSTR("A3dRoot::BindReverb() - Reverb is not supported for the current device.\n");

		return (A3DERROR_FEATURE_NOT_SUPPORTED);
	}

	if (m_pBoundedReverb == (CA3dReverb *) pReverb)
		return (S_OK);

	if (m_pBoundedReverb)
	{
		m_pBoundedReverb->Release();

		m_pBoundedReverb = NULL;
	}

	m_pBoundedReverb = (CA3dReverb *) pReverb;

#if defined(A3D_FIXES)
	if (A3dGetConfig().bSoftwareReverb)
	{
		/* NOT PART OF THE ORIGINAL. Notify the emulated engine when reverb is
		 * bound or unbound so runtime toggling follows BindReverb.
		*/
		A3dReverbEmuGetEngine()->SetEnabled(m_pBoundedReverb != NULL);
	}
#endif

	if (m_pBoundedReverb)
	{
		m_pBoundedReverb->AddRef();

		m_pBoundedReverb->m_dwChangedPending = m_pBoundedReverb->m_dwChangedEver;

		for (it = m_SourceArray.begin(); it != m_SourceArray.end(); ++it)
		{
			pSource = *it;

			pSource->SetReverbMix(pSource->m_fReverbMix,
					      pSource->m_fReverbDirectHF);

			pSource->m_dwReverbMixDirty = 1;
		}
	}
	else
	{
		ResetReverbVocabularyDefaults();
	}

	return (S_OK);
}

/* =============================================================
// SetGeomReverbParam()
// (RE) rtl:0x10007E50; dbg:0x10014EC0; IA3dGeom2 slot 50
//
// Set geometric-reverb scales and vertical axis. As in the original, valid
// fields can be stored even when another field is rejected.
//
// Returns:
//   S_OK
//   E_POINTER     a null input
//   E_INVALIDARG  a size mismatch, negative scale or invalid axis
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetGeomReverbParam(LPA3DGEOMREVERBPARAM pParam)
{
	HRESULT	hr = S_OK;

	if (!pParam)
	{
		DBGSTR("IA3dGeom2::SetGeomReverbParam() NULL is an invalid input parameter\n");

		return (E_POINTER);
	}

	if (pParam->dwSize != sizeof(A3DGEOMREVERBPARAM))
	{
		TRACE("IA3dGeom2::SetGeomReverbParam() input structure size(%d) invalid. expecting (%d)\n",
		      pParam->dwSize, sizeof(A3DGEOMREVERBPARAM));
		return (E_INVALIDARG);
	}

	if (pParam->fEffectScaling < 0.0f)
	{
		TRACE("fEffectScaling=%f cannot be less than zero\n",
		      pParam->fEffectScaling);
		hr = E_INVALIDARG;
	}
	else
	{
		m_fEffectScaling = pParam->fEffectScaling;
	}

	if (pParam->fGeomScaling < 0.0f)
	{
		TRACE("fGeomScaling=%f cannot be less than zero\n",
		      pParam->fGeomScaling);
		hr = E_INVALIDARG;
	}
	else
	{
		m_fGeomScaling = pParam->fGeomScaling;
	}

	if (pParam->nVerticalAxis > A3DAXIS_Z)
	{
		TRACE(" %d is an invalid Axis index\n",
		      pParam->nVerticalAxis);
		return (E_INVALIDARG);
	}

	m_nMeasureAxis = pParam->nVerticalAxis;

	return (hr);
}

/* =============================================================
// GetGeomReverbParam()
// (RE) rtl:0x10007EE0; dbg:0x100150A0; IA3dGeom2 slot 51
//
// Read geometric-reverb scales, vertical axis and cached structure size.
//
// Returns:
//   S_OK
//   E_POINTER     a null output
//   E_INVALIDARG  a size mismatch
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetGeomReverbParam(LPA3DGEOMREVERBPARAM pParam)
{
	if (!pParam)
	{
		DBGSTR("IA3dGeom2::GetGeomReverbParam() NULL is an invalid input parameter\n");

		return (E_POINTER);
	}

	if (pParam->dwSize != sizeof(A3DGEOMREVERBPARAM))
	{
		TRACE("IA3dGeom2::GetGeomReverbParam() input structure size(%d) invalid. expecting (%d)\n",
		      pParam->dwSize, sizeof(A3DGEOMREVERBPARAM));
		return (E_INVALIDARG);
	}

	pParam->dwSize          = m_dwGeomReverbParamSize;
	pParam->fGeomScaling    = m_fGeomScaling;
	pParam->fEffectScaling  = m_fEffectScaling;
	pParam->nVerticalAxis   = (A3DAXIS) m_nMeasureAxis;

	return (S_OK);
}

/* Unresolved bodies: dbg:0x1000BF30 and dbg:0x1000C060; behavior unknown. */

/* Single-precision frame clocks preserve the original inter-frame rounding. */
A3DVAL g_fLastFlush     = 0.0f;	/* (RE) dbg:0x10152944; last flush time in milliseconds. */
A3DVAL g_fLastFrameTime = 0.0f;	/* (RE) dbg:0x10146D40; previous frame duration in seconds. */
A3DVAL g_fLastRefOrders = 0.0f;	/* (RE) dbg:0x10152940; last ref_orders poll in milliseconds. */

/* =============================================================
// A3dListAppend()
//
// Append an object after the legacy chain tail, starting from any node.
//
// Returns: The new node; NULL on allocation failure, with the tail link
//          cleared.
// =============================================================*/

A3DLISTNODE *
A3dListAppend(
	A3DLISTNODE *pNode,
	void *pObject)
{
A3DLISTNODE *pTail;
A3DLISTNODE *p;
A3DLISTNODE *pNew;

	pTail = pNode;

	if (pNode->pNext)
	{
		do
		{
			p = pTail->pNext;

			while (p)
			{
				pTail   = p;
				p       = p->pNext;
			}
		}
		while (pTail->pNext);
	}

	pNew = (A3DLISTNODE *) new BYTE[sizeof(A3DLISTNODE)];

	if (!pNew)
	{
		pTail->pNext = NULL;

		return (NULL);
	}

	pNew->pPrev   = pTail;
	pNew->pNext   = NULL;
	pNew->pObject = pObject;

	pTail->pNext = pNew;

	return (pNew);
}

/* =============================================================
// DestroyListener()
//
// Clear the listener initialization flag. The 677 teardown is unresolved.
// =============================================================*/

void
CA3dRoot::DestroyListener(void)
{
	m_fListenerReady = 0;
}

/* =============================================================
// DestroyGeom()
//
// Clear the geometry initialization flag. The 677 teardown is unresolved.
// =============================================================*/

void
CA3dRoot::DestroyGeom(void)
{
	m_fGeomReady = 0;
}
