/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dRoot.cpp
 *
 * Implements CA3dRoot construction, COM identity, attachment and
 * destruction. The root is the shared object behind the main A3D,
 * geometry, listener and private scene interfaces.
 *
 * Initialization establishes engine defaults, registry overrides and math
 * tables, then connects the object to its backend interfaces.
 * QueryInterface selects or initializes the requested API surface and
 * records the requested version for compatibility behavior.
 *
 * The class is declared in A3d3.h. Core playback and rendering API
 * operations are in A3d3.cpp, with geometry, listener and private scene
 * operations split into their respective files.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "A3d3.h"
#include "A3dSource.h"
#include "a3dsourcecom.h"
#include "fmath.h"

#include <string.h>

LSTATUS	ReadA3dRegistryDword(LPCSTR lpValueName, LPBYTE lpData);
void	FreeChain(void);

/* =============================================================
// CA3dRoot()
// (RE) rtl:0x1000B720; dbg:0x1001CE40
//
// Initialize root state, registry overrides and the math tables.
// =============================================================*/

CA3dRoot::CA3dRoot(int nBackend)
{
	m_nMatrixDepth = 0;

	m_dwFeaturesEnabled     = 0;
	m_dwWalkEnable          = 1;
	m_Unknown_0x14A4        = 1;
	m_fOutputGain           = 1.0f;

	m_fMaxReflectionDelayTime       = 0.3f;
	m_fGlobalReflectionGainScale    = 1.0f;
	m_fGlobalReflectionDelayScale   = 1.0f;

	m_fRenderingEnabled     = 0;
	m_fTintReflections      = 0;

	m_dwInfinitePlanes      = 0;
	m_fWalkNear             = 1.0f;

	m_nBackend = nBackend;

	m_fInheritMatrix = 0;

	memset(m_matCurrent, 0, sizeof(m_matCurrent));

	m_matCurrent[0]  = 1.0f;
	m_matCurrent[5]  = 1.0f;
	m_matCurrent[10] = 1.0f;
	m_matCurrent[15] = 1.0f;

	m_dwFeaturesEnabled = A3D_DIRECT_PATH_A3D;

	m_dwLastTrace                   = 0;
	m_dwTraceInterval               = 33;
	m_dwReflectionUpdateInterval    = 2;
	m_dwOcclusionUpdateInterval     = 1;

	m_cListsRecording       = 0;
	m_fBinauralWalk         = 0;
	m_fShutdownRequested    = 0;

	m_dwOrientAsAngles      = 0;
	m_dwCoordSystemSet      = 0;
	m_dwInterfaceVersion    = 0;
	m_fInitialized          = 0;
	m_dwPrimaryBufferBuilt  = 0;

	ReadA3dRegistryDword("DisableReflections", (LPBYTE) &m_dwDisableReflections);
	ReadA3dRegistryDword("DisableOcclusions",  (LPBYTE) &m_dwDisableOcclusions);
	ReadA3dRegistryDword("UseDalInterface",    (LPBYTE) &m_dwUseDalInterface);
	ReadA3dRegistryDword("do_refs_every",      (LPBYTE) &m_dwDoRefsEvery);
	ReadA3dRegistryDword("do_occs_every",      (LPBYTE) &m_dwDoOccsEvery);
	ReadA3dRegistryDword("geom_reverb_ctrl",   (LPBYTE) &m_dwGeomReverbCtrl);

	if (m_dwGeomReverbCtrl)
		TRACE("Geom Reverb control= %d\n", m_dwGeomReverbCtrl);

	InterlockedExchange(&m_cRef, 0);

	VERIFY(SUCCEEDED(fmath::Init()));

	m_pReflectBin           = 0;
	m_fMuteEarGains         = 0;
	m_fForceStatusBits      = 0;
}

/* =============================================================
// SetOcclusionMode()
// (RE) rtl:0x10009810; dbg:0x1001D2F0
//
// Reject the unsupported occlusion-mode request.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetOcclusionMode(DWORD dwMode)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// GetOcclusionMode()
// (RE) rtl:0x10009810; dbg:0x1001D310
//
// Reject the unsupported occlusion-mode query without writing the output.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetOcclusionMode(LPDWORD pdwMode)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// SetReflectionMode()
// (RE) rtl:0x10009810; dbg:0x1001D330
//
// Reject the unsupported reflection-mode request.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetReflectionMode(DWORD dwMode)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// GetReflectionMode()
// (RE) rtl:0x10009810; dbg:0x1001D350
//
// Reject the unsupported reflection-mode query without writing the output.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetReflectionMode(LPDWORD pdwMode)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// NewEnvironment()
// (RE) rtl:0x10009810; dbg:0x1001D370
//
// Reject environment creation without writing the output.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dRoot::NewEnvironment(LPA3DENVIRONMENT *ppEnvironment)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// BindEnvironment()
// (RE) rtl:0x10009810; dbg:0x1001D390
//
// Reject the unsupported environment binding request.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dRoot::BindEnvironment(LPA3DENVIRONMENT pEnvironment)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// ~CA3dRoot()
// (RE) rtl:0x1000B9D0; dbg:0x1001D4C0
//
// Detach backends, destroy initialized interfaces and free the global chain.
// =============================================================*/

CA3dRoot::~CA3dRoot(void)
{
	Detach();

	if (m_fListenerReady)
		DestroyListener();

	if (m_fGeomReady)
		DestroyGeom();

	if (m_fPrv1Ready)
		DestroyPrv1();

	FreeChain();
}

/* =============================================================
// Attach()
// (RE) rtl:0x1000BBE0; dbg:0x1001D610
//
// Set the backends and create the listener, ignoring its initialization result.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
CA3dRoot::Attach(IDirectSound *pDS, IA3d2 *pA3d2, IA3d *pA3d)
{
	m_cRefCount = 0;

	SetBackends(pDS, pA3d2, pA3d);

	CreateListener();

	m_fGeomReady = 0;
	m_fPrv1Ready = 0;

	return (S_OK);
}

/* =============================================================
// QueryInterface()
// (RE) rtl:0x1000BC30; dbg:0x1001D680
//
// Return or initialize an interface and record the highest requested API
// version.
//
// Returns: S_OK; E_POINTER for a null output; E_NOINTERFACE for an unknown IID;
//          A3DERROR_FAILED_INIT_QUERIED_INTERFACE if initialization or DAL
//          lookup fails; the DirectSound QueryInterface result for
//          IID_IA3dPropertySet.
// =============================================================*/

STDMETHODIMP
CA3dRoot::QueryInterface(REFIID riid, void **ppv)
{
	if (!ppv)
		return (E_POINTER);

	if (IsEqualIID(riid, IID_IUnknown))
	{
		*ppv = (IA3d5 *) this;
	}
	else if (IsEqualIID(riid, IID_IA3d))
	{
		*ppv = (IA3d5 *) this;

		if (m_dwInterfaceVersion < 1)
			m_dwInterfaceVersion = 1;
	}
	else if (IsEqualIID(riid, IID_IA3d2))
	{
		*ppv = (IA3d5 *) this;

		if (m_dwInterfaceVersion < 2)
			m_dwInterfaceVersion = 2;
	}
	else if (IsEqualIID(riid, IID_IA3d3))
	{
		*ppv = (IA3d5 *) this;

		if (m_dwInterfaceVersion < 3)
			m_dwInterfaceVersion = 3;
	}
	else if (IsEqualIID(riid, IID_IA3d4))
	{
		*ppv = (IA3d5 *) this;

		if (m_dwInterfaceVersion < 4)
			m_dwInterfaceVersion = 4;
	}
	else if (IsEqualIID(riid, IID_IA3d5))
	{
		*ppv = (IA3d5 *) this;

		if (m_dwInterfaceVersion < 5)
			m_dwInterfaceVersion = 5;
	}
	else if (IsEqualIID(riid, IID_IA3dListener))
	{
		if (!m_fListenerReady && FAILED(CreateListener()))
			return (A3DERROR_FAILED_INIT_QUERIED_INTERFACE);

		*ppv = (IA3dListener *) this;
	}
	else if (IsEqualIID(riid, IID_IA3dGeom) ||
		 IsEqualIID(riid, IID_IA3dGeom2))
	{
		if (!m_fGeomReady && FAILED(CreateGeom()))
			return (A3DERROR_FAILED_INIT_QUERIED_INTERFACE);

		*ppv = (IA3dGeom2 *) this;
	}
	else if (IsEqualIID(riid, IID_IA3dPrv1))
	{
		if (!m_fPrv1Ready && FAILED(CreatePrv1()))
			return (A3DERROR_FAILED_INIT_QUERIED_INTERFACE);

		*ppv = (IA3dPrv1 *) this;
	}
	else if (IsEqualIID(riid, IID_IA3dPrv3))
	{
		if (!m_pDal)
			return (A3DERROR_FAILED_INIT_QUERIED_INTERFACE);

		if (FAILED(m_pDal->GetDS((LPDIRECTSOUND *) ppv)))
			return (A3DERROR_FAILED_INIT_QUERIED_INTERFACE);
	}
	else if (IsEqualIID(riid, IID_IA3dPropertySet))
	{
		ASSERT(m_pDirectSound);

		return (m_pDirectSound->QueryInterface(IID_IA3dPropertySet,
						       ppv));
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
// (RE) rtl:0x1000BF10; dbg:0x1001DAF0
//
// Increment the COM reference count.
//
// Returns: The new reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dRoot::AddRef(void)
{
	return (++m_cRefCount);
}

/* =============================================================
// Release()
// (RE) rtl:0x1000BF30; dbg:0x1001DB20
//
// Decrement the COM reference count and delete the root at zero.
//
// Returns: The remaining reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dRoot::Release(void)
{
	if (--m_cRefCount)
		return (m_cRefCount);

	delete this;

	return (0);
}

/* =============================================================
// Compat()
// (RE) rtl:0x1000BF60; dbg:0x1001DBA0
//
// Apply an internal compatibility option.
//
// Returns: S_OK; E_INVALIDARG for an unknown mode; the API call count for
//          A3DCOMPAT_GET_CALL_COUNT or A3DCOMPAT_SET_CALL_COUNT.
// =============================================================*/

STDMETHODIMP
CA3dRoot::Compat(DWORD dwMode, DWORD dwValue)
{
	switch (dwMode)
	{
	case A3DCOMPAT_ENABLE_RENDERING:
		m_fRenderingEnabled = dwValue;
		break;

	case A3DCOMPAT_INHERIT_MATRIX:
		if (dwValue == 1)
		{
			RebindAllSources();

			m_fInheritMatrix = 1;
		}
		else
		{
			m_fInheritMatrix = 0;
		}
		break;

	case A3DCOMPAT_REFLECT_SCALE:
		memcpy(&m_fReflectScale, &dwValue,
		       sizeof(m_fReflectScale));
		break;

	case A3DCOMPAT_WALK_MAX:
		m_cWalkMax = dwValue;
		break;

	case A3DCOMPAT_TRACE_PHASE:
		if (m_dwDoRefsEvery < 1)
			m_dwReflectionUpdateInterval = dwValue;
		break;

	case A3DCOMPAT_TRACE_PHASE2:
		if (m_dwDoOccsEvery < 1)
			m_dwOcclusionUpdateInterval = dwValue;
		break;

	case A3DCOMPAT_TRACE_INTERVAL:
		m_dwTraceInterval = dwValue;
		break;

	case A3DCOMPAT_TINT_REFLECTIONS:
		m_fTintReflections = dwValue;
		break;

	case A3DCOMPAT_1008:
		break;

	case A3DCOMPAT_INFINITE_PLANES:
		m_dwInfinitePlanes = dwValue;
		break;

	case A3DCOMPAT_WALK_NEAR:
		memcpy(&m_fWalkNear, &dwValue,
		       sizeof(m_fWalkNear));

		m_dwCompatWalkNear = 1;
		break;

	case A3DCOMPAT_AU8830:
		if (dwValue == A3D_ROOT_AU8830_ENABLE_VALUE)
			m_fCompatAu8830 = 1;
		break;

	case A3DCOMPAT_REF_ORDERS:
		m_dwCompatRefOrders = dwValue;
		break;

	case A3DCOMPAT_AUDIBLE_MAX:
		break;

	case A3DCOMPAT_BINAURAL_WALK:
		m_fBinauralWalk = dwValue;
		break;

	case A3DCOMPAT_SET_INTERFACE_VERSION:
		m_dwInterfaceVersion = dwValue;
		break;

	case A3DCOMPAT_GET_CALL_COUNT:
		return (m_cRef);

	case A3DCOMPAT_SET_CALL_COUNT:
		InterlockedExchange(&m_cRef, dwValue);

		return (m_cRef);

	case A3DCOMPAT_MUTE_EAR_GAINS:
		m_fMuteEarGains = dwValue;
		break;

	case A3DCOMPAT_FORCE_STATUS_BITS:
		m_fForceStatusBits = dwValue;
		break;

	default:
		return (E_INVALIDARG);
	}

	return (S_OK);
}

/* =============================================================
// RebindAllSources()
// (RE) dbg:0x1001DE90
//
// Bind every registered source and restore the current matrix from the stack.
// =============================================================*/

void
CA3dRoot::RebindAllSources(void)
{
CA3dStdList<CA3dSource *>::iterator	it;
CA3dSource				*pSource;

	for (it = m_SourceArray.begin(); it != m_SourceArray.end(); ++it)
	{
		pSource = *it;

		BindSource(pSource->m_pSourceCom);
	}

	memcpy(m_matCurrent, m_amatStack[m_nMatrixDepth], sizeof(m_matCurrent));
}

/* =============================================================
// DisableViewer()
// (RE) rtl:0x1001B1C0; dbg:0x1001DF80
//
// Accept the request without changing viewer state.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dRoot::DisableViewer(void)
{
	return (S_OK);
}
