/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * resman.cpp
 *
 * Implements ResMan, the layer that assigns source buffers to available
 * DAL playback devices. It selects backends, tracks voice limits and
 * priorities, creates static or streaming resource-manager buffers, and
 * forwards device-level controls.
 *
 * Its service thread updates playback, refills streams, assigns or
 * releases DAL buffers and dispatches queued property operations. Focus
 * handling and muting are coordinated with the device cooperative level.
 *
 * DalInfo records device capabilities and reusable buffers.
 * ResManStatBuffer and ResManStreamBuffer represent source playback,
 * while ResManBuffer provides their shared control and property state.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "resman.h"
#include "a2dbuffer.h"
#include "dal_a2d.h"
#include "dal_d2d.h"
#include "dal_d3d.h"
#include "dal_emu.h"
#include "dalinfo.h"
#include "Plex.h"
#include "PropertySetItem.h"
#include "rmstatbuffer.h"
#include "rmstreambuffer.h"

#include <new>
#include <stdio.h>
#include <stdlib.h>

/* NOT PART OF THE ORIGINAL.
 * The original can deadlock on subsequent property calls after hardware
 * property-set acquisition disables flushing. Skip the wait while disabled.
 */
#if defined(A3D_FIXES)
#define A3D_WAIT_PROPSET_FLUSH(h)	((!A3dGetConfig().bFixPropertyDeadlocks || \
					  m_dwPropSetCacheValid) \
					 ? WaitForSingleObject((h), INFINITE) \
					 : WAIT_OBJECT_0)
#else
#define A3D_WAIT_PROPSET_FLUSH(h)	WaitForSingleObject((h), INFINITE)
#endif

/* (RE) dbg:0x1015545C. Window-hook owner and class-factory singleton guard. */
ResMan	*g_lpResMan;

static LRESULT CALLBACK ActivateAppHookProc(int nCode, WPARAM wParam,
					   LPARAM lParam);

/* Initial source cap */
#define A3D_RESMAN_INITIAL_HW_SOURCE_LIMIT 12
/* Initial priority weight and DAL cap */
#define A3D_RESMAN_DEFAULT_PRIORITY_WEIGHT      0.5f
#define A3D_RESMAN_DEFAULT_DAL_BUFFER_CAP       0xFFFF
/* Reported software-capability version */
#define A3D_RESMAN_SOFTWARE_CAPS_VERSION 20

/* Service timing */
#define A3D_RESMAN_SERVICE_WAIT_MS              10
#define A3D_RESMAN_HEAVY_PASS_INTERVAL_MS       100
#define A3D_RESMAN_EXIT_WAIT_MS                 5000
#define A3D_RESMAN_STATIC_BUFFER_WAKE_DELAY_MS  10
/* Count-dump cadence in calls */
#define A3D_RESMAN_COUNT_DUMP_INTERVAL 20

/* Streaming settings */
#define A3D_RESMAN_MAX_STREAM_SETTING_SECONDS   5.0
#define A3D_RESMAN_MAX_STREAM_SETTING_MS        5000

/* Initial DAL byte counts */
#define A3D_RESMAN_DEFAULT_STREAM_BUFFER_BYTES          88200
#define A3D_RESMAN_DEFAULT_REFILL_THRESHOLD_BYTES       33074
#define A3D_RESMAN_DEFAULT_REFILL_TARGET_BYTES          44100

/* Hardware property-interface probe size */
#define A3D_RESMAN_PROPERTY_PROBE_BYTES 1024
/* Property-interface query gate */
#define A3D_RESMAN_ERROR_NO_SERVICE_THREAD A3DERROR_CODE(527)

/* Array growth amounts */
#define A3D_RESMAN_PRIORITY_GROW_ENTRIES        10
#define A3D_RESMAN_REFLECTION_GROW_ENTRIES      10

/* =============================================================
// IsHrFailed()
// (RE) dbg:0x1005da50
//
// Test whether an HRESULT denotes failure.
// Reference helper call sites outside DrainBufferList are not established.
//
// Returns:
//   TRUE   a negative HRESULT
//   FALSE  otherwise
// =============================================================*/

static BOOL
IsHrFailed(HRESULT hr)
{
	return (hr < 0);
}

/* =============================================================
// IsHrSucceeded()
// (RE) dbg:0x1005dc20
//
// Test whether an HRESULT denotes success.
//
// Returns:
//   TRUE   a nonnegative HRESULT
//   FALSE  otherwise
// =============================================================*/

static BOOL
IsHrSucceeded(HRESULT hr)
{
	return (!IsHrFailed(hr));
}

/* =============================================================
// QueryInterface()
// (RE) rtl:0x10023930; dbg:0x1005C360
//
// Return a supported interface, using IA3dPrv4 for IUnknown identity.
//
// Returns:
//   S_OK
//   E_NOINTERFACE  an unsupported IID
//   E_INVALIDARG   null ppv
//   A3DERROR_CODE  (527) for a property-set query before the service thread
//                  exists
// =============================================================*/

STDMETHODIMP
ResMan::QueryInterface(REFIID riid, void **ppv)
{
	if (!ppv)
		return (E_INVALIDARG);

	if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IA3dPrv4))
	{
		*ppv = (IA3dPrv4 *) this;
	}
	else if (IsEqualIID(riid, IID_IDirectSound))
	{
		*ppv = (IDirectSound *) this;
	}
	else if (IsEqualIID(riid, IID_IA3dDal))
	{
		*ppv = (IA3dDal *) this;
	}
	else if (IsEqualIID(riid, IID_IA3d) || IsEqualIID(riid, IID_IA3d2))
	{
		*ppv = (IA3d2 *) this;
	}
	else if (IsEqualIID(riid, IID_IA3dPropertySet))
	{
		if (!m_hThread || m_hThread == INVALID_HANDLE_VALUE)
		{
			CHAR szMessage[128];

			DBGSTR("ResMan::QueryInterface() - Cannot process call until a sound buffer is created.\n");

			*ppv = NULL;

			return (A3D_RESMAN_ERROR_NO_SERVICE_THREAD);
		}

		*ppv = (IA3dPropertySet *) this;
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
// DumpCounts()
// (RE) dbg:0x1005b6b0; thunk dbg:0x100021b2
//
// Trace playing and total buffer counts every twentieth call or when forced.
// Retail ServiceThread omits this call (rtl:0x10028651).
// =============================================================*/

void
ResMan::DumpCounts(int fForceDump)
{
#ifdef _DEBUG
static DWORD	s_cCall;	/* (RE) dbg:0x10155458. */
POSITION        pos;
DalInfo        *pDalInfo;
DWORD           dwName;
char            szLine[256];

	s_cCall++;

	if (fForceDump)
		s_cCall = 0;

	if (s_cCall % A3D_RESMAN_COUNT_DUMP_INTERVAL)
		return;

	wsprintfA(szLine, "BC: STRM=%d/%d STAT=%d/%d REF=%d/%d",
		  CountPlayingStreamBuffers(), m_ResManStreamBufferList.GetCount(),
		  CountPlayingStaticBuffers(), m_ResManStatBufferList.GetCount(),
		  m_nReflectionSortWanted, m_nReflectionsSupported);

	OutputDebugStringA(szLine);

	pos = m_listDalInfo.GetHeadPosition();

	while (pos)
	{
		pDalInfo = (DalInfo *) m_listDalInfo.GetNext(pos);

		ASSERT((pDalInfo != 0 &&
		       !IsBadReadPtr(pDalInfo, sizeof(DalInfo))));

		dwName = pDalInfo->GetName();

		wsprintfA(szLine, " %s=%d/%d,%d/%d",
			  (char *) &dwName,
			  pDalInfo->GetNumActiveDalBuffers(),
			  pDalInfo->GetNumDalBuffers(),
			  pDalInfo->GetNumActiveStaticBuffers(),
			  pDalInfo->GetNumStaticBuffers());

		OutputDebugStringA(szLine);
	}

	OutputDebugStringA("\n");
#else
	(void) fForceDump;
#endif
}

/* =============================================================
// ResMan()
// (RE) rtl:0x10023300; dbg:0x1005B970
//
// Initialize resource-manager state and owned handles.
// =============================================================*/

ResMan::ResMan(void)
{
	m_pPriorityBufferArray          = NULL;
	m_pReflectionSort               = NULL;
	m_pDirectSound                  = NULL;
	m_pMainDALInfo                  = NULL;
	m_dwResourceManagerMode         = A3D_RESOURCE_MODE_OFF;
	m_dwThreadId                    = 0;
	m_hThread                       = INVALID_HANDLE_VALUE;
	m_hEvent                        = INVALID_HANDLE_VALUE;
	m_hEventCallbackExit            = INVALID_HANDLE_VALUE;
	m_hEventCallbackInactive        = INVALID_HANDLE_VALUE;
	m_dwWait                        = 0;
	m_dwInterval                    = 0;
	m_dwLastRun                     = 0;
	m_cRef                          = 1;
	m_bInitialized                  = 0;
	m_dwHwVoiceCap                  = 0;
	m_dwHeavyPassPending            = 0;
	m_dwReflectionsEnabled          = 0;
	m_nReflectionsSupported         = 0;
	m_nReflectionSortAllocated      = 0;
	m_nReflectionSortWanted         = 0;
	m_fPriorityWeight               = A3D_RESMAN_DEFAULT_PRIORITY_WEIGHT;
	m_nPriorityBufferElements       = 0;
	m_nPriorityBufferArrayLength    = 0;
	m_dwServiceThreadActive         = 0;
	m_dwTimerCallbackStarted        = 0;
	m_dwDalStartedUp                = 0;
	m_dwCoopLevelSet                = 0;
	m_dwDalTypeMask                 = 0;
	m_hHook                         = NULL;
	m_hWnd                          = NULL;
	m_dwAppInactive                 = 0;
	m_dwFocusChangePending          = 0;
	m_hPropSetCacheFlushed          = INVALID_HANDLE_VALUE;
	m_hPropSetMutex                 = INVALID_HANDLE_VALUE;
	m_lpPropertySet                 = NULL;
	m_dwInitialStateSet             = 0;
	m_dwPropSetCacheValid           = 1;
	m_hSuperCtrlMutex               = INVALID_HANDLE_VALUE;
	m_dwMaxHardwareSources          = 0;
	m_dwFocusMuteEnabled            = 1;
}

/* =============================================================
// ~ResMan()
// (RE) dbg:0x1005BDE0; rtl:0x10023520; scalar deleting destructor dbg:0x1005C650
//
// Stop the service thread and release buffers, interfaces, handles and queued
// calls.
// =============================================================*/

ResMan::~ResMan(void)
{
POSITION                pos;
CPropertySetItem       *pItem;

	if (m_hHook != NULL)
	{
		UnhookWindowsHookEx(m_hHook);
		m_hHook = NULL;
	}

	if (m_hWnd != NULL)
		m_hWnd = NULL;

	if (g_lpResMan != NULL)
		g_lpResMan = NULL;

	if (m_hThread != INVALID_HANDLE_VALUE)
	{
		m_dwServiceThreadActive = 0;

		VERIFY(WaitForSingleObject(m_hEventCallbackInactive, 5000)
		       != WAIT_TIMEOUT);
	}

	if (m_pPriorityBufferArray != NULL)
	{
		operator delete((void *) m_pPriorityBufferArray);
		m_pPriorityBufferArray = NULL;
	}

	if (m_pReflectionSort != NULL)
	{
		operator delete((void *) m_pReflectionSort);
		m_pReflectionSort = NULL;
	}

	EmptyBufferLists();

	ReleaseInterfaces();

	if (m_hThread != INVALID_HANDLE_VALUE)
	{
		VERIFY(SetEvent(m_hEventCallbackExit));
		VERIFY(WaitForSingleObject(m_hThread, 5000) != WAIT_TIMEOUT);

		if (m_hThread != INVALID_HANDLE_VALUE)
		{
			CloseHandle(m_hThread);
			m_hThread = INVALID_HANDLE_VALUE;
		}
	}

	if (m_hEvent != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_hEvent);
		m_hEvent = INVALID_HANDLE_VALUE;
	}

	if (m_hEventCallbackInactive != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_hEventCallbackInactive);
		m_hEventCallbackInactive = INVALID_HANDLE_VALUE;
	}

	if (m_hEventCallbackExit != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_hEventCallbackExit);
		m_hEventCallbackExit = INVALID_HANDLE_VALUE;
	}

	if (m_hPropSetCacheFlushed != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_hPropSetCacheFlushed);
		m_hPropSetCacheFlushed = INVALID_HANDLE_VALUE;
	}

	if (m_hPropSetMutex != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_hPropSetMutex);
		m_hPropSetMutex = INVALID_HANDLE_VALUE;
	}

	if (m_hSuperCtrlMutex != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_hSuperCtrlMutex);
		m_hSuperCtrlMutex = INVALID_HANDLE_VALUE;
	}

	pos = m_listPropSetQueue.GetHeadPosition();

	while (pos != NULL)
	{
		pItem = (CPropertySetItem *) m_listPropSetQueue.GetNext(pos);

		if (pItem != NULL)
			delete pItem;
	}

	pos = m_listResManPropSetItems.GetHeadPosition();

	while (pos != NULL)
	{
		pItem = (CPropertySetItem *) m_listResManPropSetItems.GetNext(pos);

		if (pItem != NULL)
			delete pItem;
	}
}

/* =============================================================
// ResMan::AddRef()
// (RE) rtl:0x10023AA0; dbg:0x1005C590
//
// Increment the COM reference count.
//
// Returns: The count read after the interlocked increment.
// =============================================================*/

STDMETHODIMP_(ULONG)
ResMan::AddRef(void)
{
	InterlockedIncrement(&m_cRef);

	return ((ULONG) m_cRef);
}

/* =============================================================
// ResMan::Release()
// (RE) rtl:0x10023AC0; dbg:0x1005C5D0
//
// Decrement the COM reference count and delete the object at zero.
//
// Returns: The count read after the decrement, or 0 after deletion.
// =============================================================*/

STDMETHODIMP_(ULONG)
ResMan::Release(void)
{
	InterlockedDecrement(&m_cRef);

	if (m_cRef)
		return ((ULONG) m_cRef);

	delete this;

	return (0);
}

/* =============================================================
// CreateSoundBuffer()
// (RE) dbg:0x1005c6a0; rtl:0x10023b00
//
// Start DAL processing and create a primary or secondary buffer.
//
// Returns: The selected maker's result or startup failure; DSERR_UNINITIALIZED
//          before initialization or cooperative-level setup; E_INVALIDARG for
//          aggregation.
// =============================================================*/

STDMETHODIMP
ResMan::CreateSoundBuffer(LPCDSBUFFERDESC lpcDSBufferDesc,
			  LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
			  LPUNKNOWN pUnkOuter)
{
DSBUFFERDESC1	desc;
HRESULT		hr;

	ASSERT((lpcDSBufferDesc != 0 &&
	       !IsBadReadPtr(lpcDSBufferDesc, sizeof(DSBUFFERDESC1))));
	ASSERT((lplpDirectSoundBuffer != 0 &&
	       !IsBadReadPtr(lplpDirectSoundBuffer, sizeof(LPDIRECTSOUNDBUFFER))));
	ASSERT(m_bInitialized == 1);

	*lplpDirectSoundBuffer = NULL;

	if (!m_bInitialized)
	{
		DBGSTR("ResMan::CreateSoundBuffer() - Called before InitializeEx().\n");

		return (DSERR_UNINITIALIZED);
	}

	if (!m_dwCoopLevelSet)
	{
		DBGSTR("ResMan::CreateSoundBuffer() - Called before SetCooperativeLevel().\n");

		return (DSERR_UNINITIALIZED);
	}

	if (pUnkOuter)
	{
		DBGSTR("*** ResMan::CreateSoundBuffer() - Aggregation not supported.\n");

		return (E_INVALIDARG);
	}

	hr = InitDalInterfaces();

	if (FAILED(hr))
		return (hr);

	hr = StartTimerCallback();

	if (FAILED(hr))
		return (hr);

	CopyMemory(&desc, lpcDSBufferDesc, sizeof(desc));

	if (desc.dwFlags & DSBCAPS_PRIMARYBUFFER)
		return (MakePrimaryBuffer(&desc, lplpDirectSoundBuffer, NULL));

	return (CreateSoundBuffer(&desc, lplpDirectSoundBuffer, NULL));
}

/* =============================================================
// GetCaps()
// (RE) dbg:0x1005c940; rtl:0x10023c00
//
// Read DirectSound capabilities from the main DAL.
//
// Returns:
//   The delegated GetCaps result
//   E_FAIL                        if the DAL has no DirectSound interface
// =============================================================*/

STDMETHODIMP
ResMan::GetCaps(LPDSCAPS lpDirectSoundCaps)
{
IDirectSound	*lpDS;

	ASSERT((lpDirectSoundCaps != 0 &&
	       !IsBadReadPtr(lpDirectSoundCaps, sizeof(DSCAPS))));

	lpDS = m_pMainDALInfo->GetIDirectSound();

	if (lpDS == NULL)
	{
		DBGSTR("*** ResMan::GetCaps() - DirectSound object is NULL.\n");

		return (E_FAIL);
	}

	ASSERT((lpDS != 0 && !IsBadReadPtr(lpDS, sizeof(IDirectSound))));

	return (lpDS->GetCaps(lpDirectSoundCaps));
}

/* =============================================================
// SetCooperativeLevel()
// (RE) dbg:0x1005d600; rtl:0x10023fa0
//
// Set DSSCL_PRIORITY, install the activation hook and start DAL processing
// once.
//
// Returns: S_OK, including repeated calls; E_INVALIDARG for a null window; a
//          DAL-startup or timer-startup failure.
// =============================================================*/

STDMETHODIMP
ResMan::SetCooperativeLevel(HWND hWnd, DWORD dwLevel)
{
POSITION        pos;
DalInfo        *pDalInfo;
HRESULT         hr;
DWORD           dwName;

	if (m_dwCoopLevelSet == 1)
		return (S_OK);

	if (!hWnd)
		return (E_INVALIDARG);

	pos = m_listDalInfo.GetHeadPosition();

	while (pos)
	{
		pDalInfo = (DalInfo *) m_listDalInfo.GetNext(pos);

		ASSERT((pDalInfo != 0 && !IsBadReadPtr(pDalInfo, sizeof(DalInfo))));

		pDalInfo->SetCooperativeLevel(hWnd, DSSCL_PRIORITY);
	}

	m_pDirectSound->SetCooperativeLevel(hWnd, DSSCL_PRIORITY);

	if (!m_hHook)
	{
		ASSERT(m_hWnd == 0 && m_hHook == 0);

		m_hWnd = hWnd;

		m_hHook = SetWindowsHookExA(WH_CALLWNDPROC,
					    ActivateAppHookProc, NULL,
					    GetCurrentThreadId());

		g_lpResMan = this;
	}

	m_dwCoopLevelSet = 1;

	hr = InitDalInterfaces();

	if (FAILED(hr))
		return (hr);

	hr = StartTimerCallback();

	if (FAILED(hr))
		return (hr);

	pos = m_listDalInfo.GetHeadPosition();

	while (pos)
	{
		pDalInfo = (DalInfo *) m_listDalInfo.GetNext(pos);

		ASSERT((pDalInfo != 0 && !IsBadReadPtr(pDalInfo, sizeof(DalInfo))));

		dwName = pDalInfo->GetName();

		if (!lstrcmpiA((LPCSTR) &dwName, "D3D"))
			static_cast<DAL_D3D *>(pDalInfo->GetIDal())->m_dwSplashWindow =
				(DWORD) m_hWnd;
	}

	return (S_OK);
}

/* =============================================================
// Compact()
// (RE) dbg:0x1005d940; rtl:0x100240d0
//
// Compact the main DAL's DirectSound buffers.
//
// Returns: The delegated Compact result or GetDS failure; E_FAIL for a null
//          interface.
// =============================================================*/

STDMETHODIMP
ResMan::Compact(void)
{
IDirectSound   *lpDS;
HRESULT         hr;

	lpDS = NULL;

	hr = ((IA3dDal *) this)->GetDS(&lpDS);

	if (FAILED(hr))
	{
		DBGSTR("*** ResMan::Compact() - Failed to get IDirectSound interface.\n");

		return (hr);
	}

	if (lpDS == NULL)
	{
		DBGSTR("*** ResMan::Compact() - DirectSound object is NULL.\n");

		return (E_FAIL);
	}

	ASSERT((lpDS != 0 && !IsBadReadPtr(lpDS, sizeof(IDirectSound))));

	return (lpDS->Compact());
}

/* =============================================================
// GetSpeakerConfig()
// (RE) dbg:0x1005da70; rtl:0x10024110
//
// Read the main DAL's DirectSound speaker configuration.
//
// Returns: The delegated result or GetDS failure; E_POINTER for a null output;
//          E_FAIL for a null interface.
// =============================================================*/

STDMETHODIMP
ResMan::GetSpeakerConfig(LPDWORD lpdwSpeakerConfig)
{
IDirectSound   *lpDS;
HRESULT         hr;

	ASSERT((lpdwSpeakerConfig != 0 &&
	       !IsBadReadPtr(lpdwSpeakerConfig, sizeof(DWORD))));

	if (lpdwSpeakerConfig == NULL)
	{
		DBGSTR("ResMan::GetSpeakerConfig() - lpdwSpeakerConfig was passed a NULL pointer.\n");

		return (E_POINTER);
	}

	lpDS = NULL;

	hr = ((IA3dDal *) this)->GetDS(&lpDS);

	if (FAILED(hr))
	{
		DBGSTR("*** ResMan::GetSpeakerConfig() - Failed to get IDirectSound interface.\n");

		return (hr);
	}

	if (lpDS == NULL)
	{
		DBGSTR("*** ResMan::GetSpeakerConfig() - DirectSound object is NULL.\n");

		return (E_FAIL);
	}

	ASSERT((lpDS != 0 && !IsBadReadPtr(lpDS, sizeof(IDirectSound))));

	return (lpDS->GetSpeakerConfig(lpdwSpeakerConfig));
}

/* =============================================================
// SetSpeakerConfig()
// (RE) dbg:0x1005dc50; rtl:0x10024160
//
// Set the main DAL's DirectSound speaker configuration.
//
// Returns: The delegated result or GetDS failure; E_FAIL for a null interface.
// =============================================================*/

STDMETHODIMP
ResMan::SetSpeakerConfig(DWORD dwConfig)
{
IDirectSound   *lpDS;
HRESULT         hr;

	lpDS = NULL;

	hr = ((IA3dDal *) this)->GetDS(&lpDS);

	if (FAILED(hr))
	{
		DBGSTR("*** ResMan::SetSpeakerConfig() - Failed to get IDirectSound interface.\n");

		return (hr);
	}

	if (lpDS == NULL)
	{
		DBGSTR("*** ResMan::SetSpeakerConfig() - DirectSound object is NULL.\n");

		return (E_FAIL);
	}

	ASSERT((lpDS != 0 && !IsBadReadPtr(lpDS, sizeof(IDirectSound))));

	return (lpDS->SetSpeakerConfig(dwConfig));
}

/* =============================================================
// Initialize()
// (RE) dbg:0x1005dd70; rtl:0x100241b0
//
// Initialize the main DAL's DirectSound interface.
//
// Returns: The delegated result or GetDS failure; E_FAIL for a null interface.
// =============================================================*/

STDMETHODIMP
ResMan::Initialize(LPCGUID pGuid)
{
IDirectSound   *lpDS;
HRESULT         hr;

	lpDS = NULL;

	hr = ((IA3dDal *) this)->GetDS(&lpDS);

	if (FAILED(hr))
	{
		DBGSTR("*** ResMan::Initialize() - Failed to get IDirectSound interface.\n");

		return (hr);
	}

	if (lpDS == NULL)
	{
		DBGSTR("*** ResMan::Initialize() - DirectSound object is NULL.\n");

		return (E_FAIL);
	}

	ASSERT((lpDS != 0 && !IsBadReadPtr(lpDS, sizeof(IDirectSound))));

	return (lpDS->Initialize(pGuid));
}

/* =============================================================
// DuplicateSoundBuffer()
// (RE) dbg:0x1005ca80; rtl:0x10023c30
//
// Duplicate the source through its static and streaming buffer interfaces.
// Original defects: a source supporting both can overwrite the first output;
// static-path failures leave the streaming source reference unreleased.
// The creation-mode getters at dbg:0x1005d580 and dbg:0x1005d5b0 (thunks
// dbg:0x10004219 and dbg:0x1000246e) are unidentified; the stored constructor modes are read directly.
//
// Returns: S_OK; E_OUTOFMEMORY on allocation failure; a duplication,
//          interface-query or DAL-buffer failure. An unrecognized source leaves
//          the output untouched.
// =============================================================*/

STDMETHODIMP
ResMan::DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER lpDirectSoundBuffer,
			     LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer)
{
ResManStatBuffer	*pOrigStat    = NULL;
ResManStreamBuffer	*pOrigStream  = NULL;
ResManStatBuffer       *lpResManStatBuffer;
ResManStreamBuffer     *lpResManStreamBuffer;
IResManBufferPrimary   *pNew;
IResManBufferPrimary   *pOrig;
DalBufferInfo          *pSrcInfo;
DalBufferInfo          *pNewInfo;
LPDIRECTSOUNDBUFFER     pCopy;
HRESULT                 hr1;
HRESULT                 hr2;
HRESULT                 hr;

	ASSERT((lpDirectSoundBuffer != 0 &&
	       !IsBadReadPtr(lpDirectSoundBuffer, sizeof(IDirectSoundBuffer))));
	ASSERT((lplpDirectSoundBuffer != 0 &&
	       !IsBadReadPtr(lplpDirectSoundBuffer, sizeof(LPDIRECTSOUNDBUFFER))));

	hr1 = lpDirectSoundBuffer->QueryInterface(IID_ResManStatBuffer,  (void **) &pOrigStat);
	hr2 = lpDirectSoundBuffer->QueryInterface(IID_ResManStreamBuffer, (void **) &pOrigStream);

	ASSERT(SUCCEEDED(hr1) || SUCCEEDED(hr2));

	if (pOrigStat)
	{
		pOrig = static_cast<IResManBufferPrimary *>(pOrigStat);

		m_dwLastRun = 0;

		VERIFY(SetEvent(m_hEvent));

		Sleep(A3D_RESMAN_STATIC_BUFFER_WAKE_DELAY_MS);

		lpResManStatBuffer = new ResManStatBuffer(this, pOrigStat->m_nResourceManagerMode);

		if (!lpResManStatBuffer)
		{
			DBGSTR("*** ResMan::DuplicateSoundBuffer() - Failed to allocate memory for ResManBuffer object.\n");

			pOrig->Release();

			return (E_OUTOFMEMORY);
		}

		pNew = static_cast<IResManBufferPrimary *>(lpResManStatBuffer);

		hr = pNew->Duplicate(lpDirectSoundBuffer);

		if (FAILED(hr))
		{
			DBGSTR("*** ResMan::DuplicateSoundBuffer() - Duplication of 3D buffer failed.\n");

			delete pNew;
			pOrig->Release();

			return (hr);
		}

		pCopy = NULL;

		hr = pNew->QueryInterface(IID_IDirectSoundBuffer, (void **) &pCopy);

		if (FAILED(hr))
		{
			DBGSTR("*** ResMan::DuplicateSoundBuffer() - Failed querying ResManBuffer object for IDirectSoundBuffer interface.\n");

			delete pNew;
			pOrig->Release();

			return (hr);
		}

		ASSERT((lpResManStatBuffer != 0 &&
		       !IsBadReadPtr(lpResManStatBuffer, sizeof(ResManStatBuffer))));

		pSrcInfo = NULL;

		hr = pOrig->GetDalBufferInfo(&pSrcInfo);

		if (FAILED(hr))
		{
			DBGSTR("ResMan::DuplicateSoundBuffer() - Could not get DalBufferInfo object\n");

			if (pCopy)
				pCopy->Release();

			delete pNew;
			pOrig->Release();

			return (hr);
		}

		pNewInfo = NULL;

		hr = pSrcInfo->m_pDalInfo->CreateDalBuffer(pSrcInfo, &pNewInfo);

		if (FAILED(hr))
		{
			DBGSTR("ResMan::DuplicateSoundBuffer() - Could not create DalBufferInfo object\n");

			if (pCopy)
				pCopy->Release();

			delete pNew;
			pOrig->Release();

			return (hr);
		}

		pNew->AttachDalBufferInfo(pNewInfo);

		m_ResManStatBufferList.AddTail(lpResManStatBuffer);

		*lplpDirectSoundBuffer = pCopy;

		pNew->Release();
		pOrig->Release();
	}

	if (!pOrigStream)
		return (S_OK);

	pOrig = static_cast<IResManBufferPrimary *>(pOrigStream);

	lpResManStreamBuffer = new ResManStreamBuffer(this, pOrigStream->m_nResourceManagerMode);

	if (!lpResManStreamBuffer)
	{
		DBGSTR("*** ResMan::DuplicateSoundBuffer() - Failed to allocate memory for ResManBuffer object.\n");

		pOrig->Release();

		return (E_OUTOFMEMORY);
	}

	pNew = static_cast<IResManBufferPrimary *>(lpResManStreamBuffer);

	hr = pNew->Duplicate(lpDirectSoundBuffer);

	if (FAILED(hr))
	{
		DBGSTR("*** ResMan::DuplicateSoundBuffer() - Duplication of 3D buffer failed.\n");

		delete pNew;
		pOrig->Release();

		return (hr);
	}

	hr = pNew->QueryInterface(IID_IDirectSoundBuffer, (void **) lplpDirectSoundBuffer);

	if (SUCCEEDED(hr))
	{
		ASSERT((lpResManStreamBuffer != 0 &&
		       !IsBadReadPtr(lpResManStreamBuffer, sizeof(ResManStreamBuffer))));

		m_ResManStreamBufferList.AddTail(lpResManStreamBuffer);

		pNew->Release();
		pOrig->Release();

		return (S_OK);
	}

	DBGSTR("*** ResMan::DuplicateSoundBuffer() - Failed querying ResManBuffer object for IDirectSoundBuffer interface.\n");

	delete pNew;
	pOrig->Release();

	return (hr);
}

/* =============================================================
// InitializeEx()
// (RE) dbg:0x1005de90; rtl:0x10024200
//
// Initialize the resource manager without a window.
// Preserve the reference's transposed dwFlags and dwReserved arguments.
//
// Returns:
//   InitResMan's result
//   E_POINTER            a null feature output
// =============================================================*/

STDMETHODIMP
ResMan::InitializeEx(LPGUID pGuidDevice, DWORD dwFlags, DWORD dwReserved,
		     LPDWORD lpdwFeaturesEnabled)
{
	ASSERT((lpdwFeaturesEnabled != 0 &&
	       !IsBadReadPtr(lpdwFeaturesEnabled, sizeof(DWORD))));

	if (lpdwFeaturesEnabled == NULL)
	{
		DBGSTR("ResMan::InitializeEx() - lpdwFeaturesEnabled was passed as a NULL.\n");

		return (E_POINTER);
	}

	return (InitResMan(pGuidDevice, dwFlags, dwReserved, lpdwFeaturesEnabled,
			   NULL, 0));
}

/* =============================================================
// CreateSoundBufferEx()
// (RE) rtl:0x10024240; dbg:0x1005DF60
//
// Reject externally supplied wave buffers after validating the arguments.
//
// Returns: E_NOTIMPL for valid arguments; E_POINTER for a null description,
//          wave buffer; E_INVALIDARG for aggregation. Original defect: clears
//          the output before checking it, faulting if it is null.
// =============================================================*/

STDMETHODIMP
ResMan::CreateSoundBufferEx(const DSBUFFERDESC1 *lpcDSBufferDesc,
			    LPBYTE lpbWave,
			    LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
			    LPUNKNOWN pUnkOuter)
{
	ASSERT((lpcDSBufferDesc != 0 &&
	       !IsBadReadPtr(lpcDSBufferDesc, sizeof(DSBUFFERDESC1))));
	ASSERT((lpbWave != 0 && !IsBadReadPtr(lpbWave, sizeof(BYTE))));
	ASSERT((lplpDirectSoundBuffer != 0 &&
	       !IsBadReadPtr(lplpDirectSoundBuffer, sizeof(LPDIRECTSOUNDBUFFER))));
	ASSERT(pUnkOuter == 0);

	*lplpDirectSoundBuffer = NULL;

	if (!lpcDSBufferDesc)
	{
		DBGSTR("ResMan::CreateSoundBufferEx() - NULL pointer passed for the buffer description.\n");

		return (E_POINTER);
	}

	if (!lpbWave)
	{
		DBGSTR("ResMan::CreateSoundBufferEx() - NULL pointer passed for the wave data buffer.\n");

		return (E_POINTER);
	}

	if (!lplpDirectSoundBuffer)
	{
		DBGSTR("ResMan::CreateSoundBufferEx() - NULL pointer passed for the ds buffer pointer.\n");

		return (E_POINTER);
	}

	if (pUnkOuter)
	{
		DBGSTR("ResMan::CreateSoundBufferEx() - Aggregation not supported, pUnkOuter must be NULL.\n");

		return (E_INVALIDARG);
	}

	return (E_NOTIMPL);
}

/* =============================================================
// GetA3dCaps()
// (RE) dbg:0x1005e1b0; rtl:0x10024290
//
// Read the main DAL's A3D capabilities.
//
// Returns:
//   GetDalCaps' result
//   E_POINTER           if either output is null
// =============================================================*/

STDMETHODIMP
ResMan::GetA3dCaps(LPA3DDALCAPS lpA3dCaps, LPDWORD lpdwSize)
{
	ASSERT((lpA3dCaps != 0 && !IsBadReadPtr(lpA3dCaps, sizeof(A3DDALCAPS564))));
	ASSERT((lpdwSize != 0 && !IsBadReadPtr(lpdwSize, sizeof(DWORD))));

	if (lpA3dCaps == NULL)
	{
		DBGSTR("ResMan::GetA3dCaps() - NULL pointer passed in for lpA3dCaps.\n");

		return (E_POINTER);
	}

	if (lpdwSize == NULL)
	{
		DBGSTR("ResMan::GetA3dCaps() - NULL pointer passed in for lpdwSize.\n");

		return (E_POINTER);
	}

	ASSERT((m_pMainDALInfo != 0 &&
	       !IsBadReadPtr(m_pMainDALInfo, sizeof(DalInfo))));

	return (m_pMainDALInfo->GetDalCaps((A3DDALCAPS564 *) lpA3dCaps, lpdwSize));
}

/* =============================================================
// GetDriverInfo()
// (RE) rtl:0x100242D0; dbg:0x1005E360
//
// Read driver interfaces and card information from the main DAL.
// The reference diagnostic names for the two driver outputs are reversed.
//
// Returns:
//   The delegated result
//   E_POINTER             if any output is null
// =============================================================*/

STDMETHODIMP
ResMan::GetDriverInfo(LPHANDLE lphA3dVxd, void **lplpIDsDriver,
		      void **lplpIA3dDriver, LPDWORD lpdwCardIndex)
{
	ASSERT((lphA3dVxd != 0 && !IsBadReadPtr(lphA3dVxd, sizeof(HANDLE))));
	ASSERT((lplpIDsDriver != 0 && !IsBadReadPtr(lplpIDsDriver, sizeof(LPVOID))));
	ASSERT((lplpIA3dDriver != 0 && !IsBadReadPtr(lplpIA3dDriver, sizeof(LPVOID))));
	ASSERT((lpdwCardIndex != 0 && !IsBadReadPtr(lpdwCardIndex, sizeof(DWORD))));

	if (!lphA3dVxd)
	{
		DBGSTR("ResMan::GetDriverInfo() - NULL pointer passed in for lphA3dVxd.\n");

		return (E_POINTER);
	}

	if (!lplpIDsDriver)
	{
		DBGSTR("ResMan::GetDriverInfo() - NULL pointer passed in for lplpIDsDriver.\n");

		return (E_POINTER);
	}

	if (!lplpIA3dDriver)
	{
		DBGSTR("ResMan::GetDriverInfo() - NULL pointer passed in for lplpIA3dDriver.\n");

		return (E_POINTER);
	}

	if (!lpdwCardIndex)
	{
		DBGSTR("ResMan::GetDriverInfo() - NULL pointer passed in for lpdwCardIndex.\n");

		return (E_POINTER);
	}

	ASSERT((m_pMainDALInfo != 0 && !IsBadReadPtr(m_pMainDALInfo, sizeof(DalInfo))));
	ASSERT((m_pMainDALInfo->GetIDal() != 0 &&
	       !IsBadReadPtr(m_pMainDALInfo->GetIDal(), sizeof(IA3dDal))));

	return (m_pMainDALInfo->m_pIA3dDal->GetDriverInfo(lphA3dVxd,
				lplpIDsDriver, lplpIA3dDriver, lpdwCardIndex));
}

/* =============================================================
// GetDS()
// (RE) rtl:0x10024340; dbg:0x1005E6D0
//
// Return the resource manager's borrowed DirectSound interface.
//
// Returns:
//   S_OK
//   DSERR_UNINITIALIZED  before initialization
//   E_POINTER            a null output
// =============================================================*/

STDMETHODIMP
ResMan::GetDS(LPDIRECTSOUND *lplpDirectSound)
{
	ASSERT((lplpDirectSound != 0 &&
	       !IsBadReadPtr(lplpDirectSound, sizeof(LPDIRECTSOUND))));

	if (!m_bInitialized)
	{
		DBGSTR("ResMan::GetDS() - IA3dDal::InitializeEx() has not been called yet.\n");

		return (DSERR_UNINITIALIZED);
	}

	if (!lplpDirectSound)
	{
		DBGSTR("ResMan::GetDS() - NULL pointer passed in for lplpDirectSound.\n");

		return (E_POINTER);
	}

	ASSERT((m_pDirectSound != 0 &&
	       !IsBadReadPtr(m_pDirectSound, sizeof(LPDIRECTSOUND))));

	*lplpDirectSound = m_pDirectSound;

	return (S_OK);
}

/* =============================================================
// GetDSDriverDesc()
// (RE) rtl:0x10024380; dbg:0x1005E830
//
// Read the main DAL's DirectSound driver description.
//
// Returns:
//   The delegated result
//   E_POINTER             if either output is null
// =============================================================*/

STDMETHODIMP
ResMan::GetDSDriverDesc(void *lpsDSDriverDesc, LPDWORD lpdwSize)
{
	ASSERT((lpsDSDriverDesc != 0 &&
	       !IsBadReadPtr(lpsDSDriverDesc, sizeof(DSDRIVERDESC))));
	ASSERT((lpdwSize != 0 && !IsBadReadPtr(lpdwSize, sizeof(DWORD))));

	if (!lpsDSDriverDesc)
	{
		DBGSTR("ResMan::GetDSDriverDesc() - NULL pointer passed in for lpsDSDriverDesc.\n");

		return (E_POINTER);
	}

	if (!lpdwSize)
	{
		DBGSTR("ResMan::GetDSDriverDesc() - NULL pointer passed in for lpdwSize.\n");

		return (E_POINTER);
	}

	ASSERT((m_pMainDALInfo != 0 && !IsBadReadPtr(m_pMainDALInfo, sizeof(DalInfo))));
	ASSERT((m_pMainDALInfo->GetIDal() != 0 &&
	       !IsBadReadPtr(m_pMainDALInfo->GetIDal(), sizeof(IA3dDal))));

	return (m_pMainDALInfo->m_pIA3dDal->GetDSDriverDesc(lpsDSDriverDesc, lpdwSize));
}

/* =============================================================
// QueryFunctionality()
// (RE) rtl:0x100243C0; dbg:0x1005EA80
//
// Query a functionality selector on the main DAL.
//
// Returns:
//   The delegated result
//   E_POINTER             a null status output
// =============================================================*/

STDMETHODIMP
ResMan::QueryFunctionality(DWORD dwFunction, LPDWORD lpdwStatus)
{
	ASSERT((lpdwStatus != 0 && !IsBadReadPtr(lpdwStatus, sizeof(DWORD))));

	if (!lpdwStatus)
	{
		DBGSTR("ResMan::QueryFunctionality() - NULL pointer passed in to lpdwStatus.\n");

		return (E_POINTER);
	}

	ASSERT((m_pMainDALInfo != 0 && !IsBadReadPtr(m_pMainDALInfo, sizeof(DalInfo))));
	ASSERT((m_pMainDALInfo->GetIDal() != 0 &&
	       !IsBadReadPtr(m_pMainDALInfo->GetIDal(), sizeof(IA3dDal))));

	return (m_pMainDALInfo->m_pIA3dDal->QueryFunctionality(dwFunction, lpdwStatus));
}

/* =============================================================
// Verify()
// (RE) rtl:0x100243F0; dbg:0x1005EC30
//
// Verify a string through the main DAL.
// Preserve the original QueryFunctionality labels in null-pointer diagnostics.
//
// Returns:
//   The delegated result
//   E_POINTER             if any argument is null
// =============================================================*/

STDMETHODIMP
ResMan::Verify(LPSTR lpcString, LPSTR *lplpcStringCrypted,
	       LPSTR *lplpcCopyright)
{
	ASSERT((lpcString != 0 && !IsBadReadPtr(lpcString, sizeof(char))));
	ASSERT((lplpcStringCrypted != 0 &&
	       !IsBadReadPtr(lplpcStringCrypted, sizeof(char*))));
	ASSERT((lplpcCopyright != 0 && !IsBadReadPtr(lplpcCopyright, sizeof(char*))));

	if (!lpcString)
	{
		DBGSTR("ResMan::QueryFunctionality() - NULL pointer passed in to lpcString.\n");

		return (E_POINTER);
	}

	if (!lplpcStringCrypted)
	{
		DBGSTR("ResMan::QueryFunctionality() - NULL pointer passed in to lplpcStringCrypted.\n");

		return (E_POINTER);
	}

	if (!lplpcCopyright)
	{
		DBGSTR("ResMan::QueryFunctionality() - NULL pointer passed in to lplpcCopyright.\n");

		return (E_POINTER);
	}

	ASSERT((m_pMainDALInfo != 0 && !IsBadReadPtr(m_pMainDALInfo, sizeof(DalInfo))));
	ASSERT((m_pMainDALInfo->GetIDal() != 0 &&
	       !IsBadReadPtr(m_pMainDALInfo->GetIDal(), sizeof(IA3dDal))));

	return (m_pMainDALInfo->m_pIA3dDal->Verify(lpcString, lplpcStringCrypted,
						   lplpcCopyright));
}

/* =============================================================
// SetOutputMode()
// (RE) rtl:0x10024450; dbg:0x1005EF10
//
// Set crosstalk and quad modes on every attached DAL.
//
// Returns: S_OK, ignoring DAL failures; E_INVALIDARG for an invalid mode.
// =============================================================*/

STDMETHODIMP
ResMan::SetOutputMode(DWORD dwFrontXtalkMode, DWORD dwBackXtalkMode,
		      DWORD dwQuadMode)
{
POSITION        pos;
DalInfo        *pDalInfo;

	ASSERT(dwFrontXtalkMode >= OUTPUT_HEADPHONES &&
	       dwFrontXtalkMode <= OUTPUT_SPEAKERS_NARROW);
	ASSERT(dwBackXtalkMode >= OUTPUT_HEADPHONES &&
	       dwBackXtalkMode <= OUTPUT_SPEAKERS_NARROW);
	ASSERT(dwQuadMode == OUTPUT_MODE_STEREO || dwQuadMode == OUTPUT_MODE_QUAD);

	if (dwFrontXtalkMode < OUTPUT_HEADPHONES ||
	    dwFrontXtalkMode > OUTPUT_SPEAKERS_NARROW)
	{
		DBGSTR("ResMan::SetOutputMode() - Invalid mode passed to dwFrontXtalkMode.\n");

		return (E_INVALIDARG);
	}

	if (dwBackXtalkMode < OUTPUT_HEADPHONES ||
	    dwBackXtalkMode > OUTPUT_SPEAKERS_NARROW)
	{
		DBGSTR("ResMan::SetOutputMode() - Invalid mode passed to dwBackXtalkMode.\n");

		return (E_INVALIDARG);
	}

	if (dwQuadMode != OUTPUT_MODE_STEREO && dwQuadMode != OUTPUT_MODE_QUAD)
	{
		DBGSTR("ResMan::SetOutputMode() - Invalid mode passed to dwQuadMode.\n");

		return (E_INVALIDARG);
	}

	pos = m_listDalInfo.GetHeadPosition();

	while (pos)
	{
		pDalInfo = (DalInfo *) m_listDalInfo.GetNext(pos);

		ASSERT((pDalInfo != 0 && !IsBadReadPtr(pDalInfo, sizeof(DalInfo))));

		pDalInfo->GetIA3d2()->SetOutputMode(dwFrontXtalkMode,
						    dwBackXtalkMode, dwQuadMode);
	}

	return (S_OK);
}

/* =============================================================
// GetOutputMode()
// (RE) rtl:0x100244C0; dbg:0x1005F1E0
//
// Read crosstalk and quad modes from the main DAL.
// Preserve the original QueryFunctionality labels in null-pointer diagnostics.
//
// Returns:
//   The delegated result
//   E_POINTER             if any output is null
// =============================================================*/

STDMETHODIMP
ResMan::GetOutputMode(LPDWORD lpdwFrontXtalkMode, LPDWORD lpdwBackXtalkMode,
		      LPDWORD lpdwQuadMode)
{
	ASSERT((lpdwFrontXtalkMode != 0 &&
	       !IsBadReadPtr(lpdwFrontXtalkMode, sizeof(DWORD))));
	ASSERT((lpdwBackXtalkMode != 0 &&
	       !IsBadReadPtr(lpdwBackXtalkMode, sizeof(DWORD))));
	ASSERT((lpdwQuadMode != 0 && !IsBadReadPtr(lpdwQuadMode, sizeof(DWORD))));

	if (!lpdwFrontXtalkMode)
	{
		DBGSTR("ResMan::QueryFunctionality() - NULL pointer passed in to lpdwFrontXtalkMode.\n");

		return (E_POINTER);
	}

	if (!lpdwBackXtalkMode)
	{
		DBGSTR("ResMan::QueryFunctionality() - NULL pointer passed in to lpdwBackXtalkMode.\n");

		return (E_POINTER);
	}

	if (!lpdwQuadMode)
	{
		DBGSTR("ResMan::QueryFunctionality() - NULL pointer passed in to lpdwQuadMode.\n");

		return (E_POINTER);
	}

	ASSERT((m_pMainDALInfo != 0 && !IsBadReadPtr(m_pMainDALInfo, sizeof(DalInfo))));

	return (m_pMainDALInfo->GetIA3d2()->GetOutputMode(lpdwFrontXtalkMode,
							  lpdwBackXtalkMode,
							  lpdwQuadMode));
}

/* =============================================================
// SetResourceManagerMode()
// (RE) rtl:0x10024520; dbg:0x1005F450
//
// Store the resource-manager mode, mapping NOTIFY and DYNAMIC_LOOPERS
// to DYNAMIC.
//
// Returns:
//   S_OK
//   E_INVALIDARG  above A3D_RESOURCE_MODE_LAST
// =============================================================*/

STDMETHODIMP
ResMan::SetResourceManagerMode(DWORD dwResourceManagerMode)
{
	ASSERT(dwResourceManagerMode <= A3D_RESOURCE_MODE_LAST);

	if (dwResourceManagerMode > A3D_RESOURCE_MODE_LAST)
		return (E_INVALIDARG);

	m_dwResourceManagerMode = dwResourceManagerMode;

	if (m_dwResourceManagerMode == A3D_RESOURCE_MODE_NOTIFY)
		m_dwResourceManagerMode = A3D_RESOURCE_MODE_DYNAMIC;
	else if (m_dwResourceManagerMode == A3D_RESOURCE_MODE_DYNAMIC_LOOPERS)
		m_dwResourceManagerMode = A3D_RESOURCE_MODE_DYNAMIC;

	return (S_OK);
}

/* =============================================================
// GetResourceManagerMode()
// (RE) rtl:0x10024560; dbg:0x1005F510
//
// Read the stored resource-manager mode.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
ResMan::GetResourceManagerMode(LPDWORD lpdwResourceManagerMode)
{
	ASSERT((lpdwResourceManagerMode != 0 &&
	       !IsBadReadPtr(lpdwResourceManagerMode, sizeof(DWORD))));

	if (!lpdwResourceManagerMode)
		return (E_POINTER);

	*lpdwResourceManagerMode = m_dwResourceManagerMode;

	return (S_OK);
}

/* =============================================================
// SetHFAbsorbFactor()
// (RE) rtl:0x10024590; dbg:0x1005F5A0
//
// Set the high-frequency absorption factor on every attached DAL.
//
// Returns: S_OK, ignoring DAL failures.
// =============================================================*/

STDMETHODIMP
ResMan::SetHFAbsorbFactor(FLOAT fFactor)
{
POSITION        pos;
DalInfo        *pDalInfo;

	pos = m_listDalInfo.GetHeadPosition();

	while (pos)
	{
		pDalInfo = (DalInfo *) m_listDalInfo.GetNext(pos);

		ASSERT((pDalInfo != 0 && !IsBadReadPtr(pDalInfo, sizeof(DalInfo))));

		pDalInfo->GetIA3d2()->SetHFAbsorbFactor(fFactor);
	}

	return (S_OK);
}

/* =============================================================
// GetHFAbsorbFactor()
// (RE) rtl:0x100245D0; dbg:0x1005F670
//
// Read the main DAL's high-frequency absorption factor.
//
// Returns: The delegated GetHFAbsorbFactor result.
// =============================================================*/

STDMETHODIMP
ResMan::GetHFAbsorbFactor(FLOAT *pfFactor)
{
	ASSERT((m_pMainDALInfo != 0 && !IsBadReadPtr(m_pMainDALInfo, sizeof(DalInfo))));

	return (m_pMainDALInfo->GetIA3d2()->GetHFAbsorbFactor(pfFactor));
}

/* =============================================================
// RegisterVersion()
// (RE) rtl:0x100034F0; dbg:0x1005F720
//
// Accept the version without changing state.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
ResMan::RegisterVersion(DWORD dwVersion)
{
	return (S_OK);
}

/* =============================================================
// GetSoftwareCaps()
// (RE) dbg:0x1005F740; rtl:0x100245F0; thunk dbg:0x10003ADA
//
// Read the A2D capabilities, or leave the output cleared if A2D is absent.
// Preserve the original full-field writes for short requested sizes and
// uninitialized DSCAPS fields left by GetCaps.
//
// Returns: S_OK, ignoring capability-query failures; E_POINTER for a null
//          output; E_INVALIDARG for a zero size or one larger than
//          A3DCAPS_SOFTWARE.
// =============================================================*/

STDMETHODIMP
ResMan::GetSoftwareCaps(LPA3DCAPS_SOFTWARE lpCaps)
{
POSITION        pos;
DalInfo        *pDalInfo;
A3DDALCAPS564   dalCaps;
DSCAPS          dsCaps;
DWORD           dwCapsSize;
DWORD           dwStructSize;
DWORD           dwName;

	ASSERT((lpCaps != 0 &&
	       !IsBadReadPtr(lpCaps, sizeof(A3DCAPS_SOFTWARE))));

	if (!lpCaps)
		return (E_POINTER);

	dwStructSize = lpCaps->dwSize;

	ASSERT(dwStructSize > 0 &&
	       dwStructSize <= sizeof(A3DCAPS_SOFTWARE));

	if (dwStructSize < 1 || dwStructSize > sizeof(A3DCAPS_SOFTWARE))
		return (E_INVALIDARG);

	ZeroMemory(lpCaps, dwStructSize);

	lpCaps->dwSize = dwStructSize;

	pos = m_listDalInfo.GetHeadPosition();

	while (pos)
	{
		pDalInfo = (DalInfo *) m_listDalInfo.GetNext(pos);

		ASSERT((pDalInfo != 0 &&
		       !IsBadReadPtr(pDalInfo, sizeof(DalInfo))));

		dwName = pDalInfo->GetName();

		if (!lstrcmpiA((char *) &dwName, "A2D"))
		{
			dwCapsSize = sizeof(A3DDALCAPS564);

			pDalInfo->GetDalCaps(&dalCaps, &dwCapsSize);

			dsCaps.dwSize = sizeof(DSCAPS);

			pDalInfo->GetIDirectSound()->GetCaps(&dsCaps);

			lpCaps->dwMax2DBuffers   = dsCaps.dwMaxHwMixingAllBuffers;
			lpCaps->dwMax3DBuffers   = dsCaps.dwMaxHw3DAllBuffers;
			lpCaps->dwMaxSampleRate  = dsCaps.dwMaxSecondarySampleRate;
			lpCaps->dwMinSampleRate  = dsCaps.dwMinSecondarySampleRate;
			lpCaps->dwOutputChannels = dalCaps.caps.wChannels;

			lpCaps->dwVersion = A3D_RESMAN_SOFTWARE_CAPS_VERSION;
			lpCaps->dwFlags   = A3D_OCCLUSIONS+A3D_DIRECT_PATH_A3D;

			return (S_OK);
		}
	}

	return (S_OK);
}

/* =============================================================
// GetHardwareCaps()
// (RE) dbg:0x1005F9F0; rtl:0x100246F0; thunk dbg:0x1000316B
//
// Read the first hardware DAL's capabilities, or leave the output cleared
// if none is present. Preserve the original full-field writes for short
// requested sizes and uninitialized DSCAPS fields left by GetCaps.
// Emulation builds expose the software device when no hardware DAL is present.
//
// Returns: S_OK, ignoring capability-query failures; E_POINTER for a null
//          output; E_INVALIDARG for a zero size or one larger than
//          A3DCAPS_HARDWARE.
// =============================================================*/

STDMETHODIMP
ResMan::GetHardwareCaps(LPA3DCAPS_HARDWARE lpCaps)
{
POSITION        pos;
DalInfo        *pDalInfo;
A3DDALCAPS564   dalCaps;
DSCAPS          dsCaps;
DWORD           dwCapsSize;
DWORD           dwStructSize;
DWORD           dwName;

	ASSERT((lpCaps != 0 &&
	       !IsBadReadPtr(lpCaps, sizeof(A3DCAPS_HARDWARE))));

	if (!lpCaps)
	{
		DBGSTR("ResMan::GetHardwareCaps() - lpCaps is NULL.\n");

		return (E_POINTER);
	}

	dwStructSize = lpCaps->dwSize;

	if (dwStructSize < 1 || dwStructSize > sizeof(A3DCAPS_HARDWARE))
	{
		DBGSTR("ResMan::GetHardwareCaps() - Invalid value for lpCaps->dwSize.\n");

		return (E_INVALIDARG);
	}

	ZeroMemory(lpCaps, dwStructSize);

	lpCaps->dwSize = dwStructSize;

	pos = m_listDalInfo.GetHeadPosition();

	while (pos)
	{
		pDalInfo = (DalInfo *) m_listDalInfo.GetNext(pos);

		ASSERT((pDalInfo != 0 &&
		       !IsBadReadPtr(pDalInfo, sizeof(DalInfo))));

		if (pDalInfo->ReportsHardwareStatus() == 1)
		{
			dwCapsSize = sizeof(A3DDALCAPS564);

			pDalInfo->GetDalCaps(&dalCaps, &dwCapsSize);

			dsCaps.dwSize = sizeof(DSCAPS);

			pDalInfo->GetIDirectSound()->GetCaps(&dsCaps);

			lpCaps->dwMax2DBuffers   = dsCaps.dwMaxHwMixingAllBuffers;
			lpCaps->dwMax3DBuffers   = dsCaps.dwMaxHw3DAllBuffers;
			lpCaps->dwMaxSampleRate  = dsCaps.dwMaxSecondarySampleRate;
			lpCaps->dwMinSampleRate  = dsCaps.dwMinSecondarySampleRate;
			lpCaps->dwOutputChannels = dalCaps.caps.wChannels;

			lpCaps->dwFlags = A3D_OCCLUSIONS;

			dwName = pDalInfo->GetName();

			if (pDalInfo->IsD3dHardware())
			{
				lpCaps->dwFlags |= A3D_DIRECT_PATH_A3D;
			}
			else if (!lstrcmpiA((char *) &dwName, "D3D"))
			{
				lpCaps->dwFlags |= A3D_DIRECT_PATH_GENERIC;
			}
			else
			{
				lpCaps->dwFlags |= A3D_DIRECT_PATH_A3D;
			}

			if (dalCaps.caps.dwMaxReflections > 0)
				lpCaps->dwFlags |= A3D_1ST_REFLECTIONS;

			return (S_OK);
		}
	}

#if defined(A3D_FIXES)
	if (A3dGetConfig().bEmulateHardware)
	{
		/* NOT PART OF THE ORIGINAL: the A2D mixer serves legacy hardware queries. */
		A3DCAPS_SOFTWARE software;
		A3DCAPS_HARDWARE hardware;
		ZeroMemory(&software, sizeof(software));
		ZeroMemory(&hardware, sizeof(hardware));
		software.dwSize = sizeof(software);
		GetSoftwareCaps(&software);
		hardware.dwSize                 = dwStructSize;
		hardware.dwFlags                = software.dwFlags;
		hardware.dwOutputChannels       = software.dwOutputChannels;
		hardware.dwMinSampleRate        = software.dwMinSampleRate;
		hardware.dwMaxSampleRate        = software.dwMaxSampleRate;
		hardware.dwMax2DBuffers         = software.dwMax2DBuffers;
		hardware.dwMax3DBuffers         = software.dwMax3DBuffers;
		memcpy(lpCaps, &hardware, dwStructSize);
	}
#endif

	return (S_OK);
}

/* =============================================================
// InitResMan()
// (RE) rtl:0x10024820; dbg:0x1005FD10
//
// Acquire DALs and initialize buffer limits, sorting arrays and
// synchronization. The original clears a null reflection-sort array after
// allocation failure.
//
// Returns: S_OK; AcquireInterfaces or SetCooperativeLevel failure; E_FAIL if no
//          main DAL is available or a synchronization handle cannot be created.
// =============================================================*/

HRESULT
ResMan::InitResMan(LPGUID pGuidDevice, DWORD dwReserved, DWORD dwFlags,
		   LPDWORD lpdwFeatures, HWND hWnd, DWORD dwLevel)
{
POSITION        pos;
DalInfo        *pDalInfo;
DSCAPS          dsCaps;
A3DDALCAPS564   a3dCaps;
DWORD           dwSize;
HRESULT         hr;

	hr = AcquireInterfaces(pGuidDevice, dwReserved, dwFlags, lpdwFeatures);

	if (FAILED(hr))
	{
		DBGSTR("ResMan::InitResMan() - Could not aquire interfaces.\n");

		return (hr);
	}

	BuildDalTypeMask();

	pos = m_listDalInfo.GetHeadPosition();

	if (!pos)
	{
		DBGSTR("ResMan::InitResMan() - There are no DAL's present.\n");

		return (E_FAIL);
	}

	pDalInfo = (DalInfo *) m_listDalInfo.GetNext(pos);

	/* (RE) Inlined main-DAL predicate: dbg:0x10060490. */
	if (!pDalInfo || (pDalInfo->m_sDalModeDesc.dwFlags & A3D_DALMODE_MAIN_ELIGIBLE) == 0)
	{
		DBGSTR("ResMan::InitResMan() - One of the main DALs did not get created.\n");

		return (E_FAIL);
	}

	m_pMainDALInfo = pDalInfo;

	((IA3dPrv4 *) this)->SetBufferLatency(1.0f);
	((IA3dPrv4 *) this)->SetBufferRefreshThreshold(0.75f);

	m_dwFocusMuteEnabled = (dwReserved & A3D_DISABLE_FOCUS_MUTE) == 0;

	((IA3dPrv4 *) this)->GetHardwareSourceCapacity(&m_dwHwVoiceCap);

	if (m_dwHwVoiceCap >= A3D_RESMAN_INITIAL_HW_SOURCE_LIMIT)
		m_dwHwVoiceCap = A3D_RESMAN_INITIAL_HW_SOURCE_LIMIT;

	ASSERT((m_pDirectSound != 0 &&
	       !IsBadReadPtr(m_pDirectSound, sizeof(IDirectSound))));

	memset(&dsCaps, 0, sizeof(dsCaps));
	dsCaps.dwSize = sizeof(DSCAPS);

	VERIFY(SUCCEEDED(m_pDirectSound->GetCaps(&dsCaps)));

#if defined(A3D_FIXES)
	if (A3dGetConfig().bEmulateHardware)
	{
		A3DCAPS_HARDWARE hardware;
		hardware.dwSize = sizeof(hardware);
		GetHardwareCaps(&hardware);
		m_dwMaxHardwareSources = hardware.dwMax3DBuffers;
	}
	else
#endif
	m_dwMaxHardwareSources = dsCaps.dwMaxHwMixingAllBuffers;

	m_bInitialized          = 1;
	m_dwHeavyPassPending    = 0;

	dwSize = sizeof(A3DDALCAPS564);

	if (m_pMainDALInfo != NULL &&
	    SUCCEEDED(m_pMainDALInfo->GetDalCaps(&a3dCaps, &dwSize)))
	{
		m_nPriorityBufferArrayLength = 4 * a3dCaps.caps.wMaxBuffers;

		m_pPriorityBufferArray = (ResManBuffer **)
			operator new(4 * m_nPriorityBufferArrayLength);

		if (m_pPriorityBufferArray == NULL)
		{
			DBGSTR("ResMan::InitResMan() - Failed to allocate memory for m_pPriorityBufferArray.\n");

			m_nPriorityBufferArrayLength = 0;
		}

		memset(m_pPriorityBufferArray, 0, 4 * m_nPriorityBufferArrayLength);

		if (*lpdwFeatures & A3D_1ST_REFLECTIONS)
		{
			m_dwReflectionsEnabled  = 1;
			m_nReflectionsSupported = a3dCaps.caps.dwMaxReflections;

			ASSERT(m_nReflectionsSupported > 0);

			m_nReflectionSortAllocated = a3dCaps.caps.wMaxBuffers / 2
					 * m_nReflectionsSupported;

			m_pReflectionSort = (REFLECTIONSORT *)
				operator new(8 * m_nReflectionSortAllocated);

			if (m_pReflectionSort == NULL)
			{
				DBGSTR("ResMan::InitResMan() - Failed to allocate memory for m_pReflectionSort, disabling reflections.\n");

				m_dwReflectionsEnabled = 0;
				*lpdwFeatures &= ~A3D_1ST_REFLECTIONS;
			}

			memset(m_pReflectionSort, 0, 8 * m_nReflectionSortAllocated);
		}
		else
		{
			m_dwReflectionsEnabled  = 0;
			m_pReflectionSort       = NULL;
			m_nReflectionsSupported = 0;
		}
	}
	else
	{
		DBGSTR("ResMan::InitResMan() - Could not acquire A3D Caps.\n");

		m_nReflectionsSupported = 0;
		m_pReflectionSort       = NULL;
	}

	m_hPropSetCacheFlushed = CreateEventA(NULL, FALSE, FALSE, NULL);

	if (m_hPropSetCacheFlushed == NULL)
	{
		DBGSTR("ResMan::InitResMan() - Could not create Property Set event.\n");

		return (E_FAIL);
	}

	m_hPropSetMutex = CreateMutexA(NULL, FALSE, NULL);

	if (m_hPropSetMutex == NULL)
	{
		DBGSTR("ResMan::InitResMan() - Could not create Property Set mutex.\n");

		return (E_FAIL);
	}

	m_hSuperCtrlMutex = CreateMutexA(NULL, FALSE, NULL);

	if (m_hSuperCtrlMutex == NULL)
	{
		DBGSTR("ResMan::InitResMan() - Could not create Super Control mutex.\n");

		return (E_FAIL);
	}

	if (hWnd == NULL)
		return (S_OK);

	hr = ((IDirectSound *) this)->SetCooperativeLevel(hWnd, dwLevel);

	if (FAILED(hr))
		return (hr);

	return (S_OK);
}

/* =============================================================
// StartTimerCallback()
// (RE) rtl:0x10024a90; dbg:0x100604c0
//
// Start the buffer-service thread and its events once.
// Original failure cleanup passes null event handles to CloseHandle.
//
// Returns:
//   S_OK    if started or already running
//   E_FAIL  if event creation, thread creation or thread-priority setup fails
// =============================================================*/

HRESULT
ResMan::StartTimerCallback(void)
{
CHAR	szError[256];

	if (m_dwTimerCallbackStarted == 1)
		return (S_OK);

	m_dwWait        = A3D_RESMAN_SERVICE_WAIT_MS;
	m_dwInterval    = A3D_RESMAN_HEAVY_PASS_INTERVAL_MS;

	m_hEvent = CreateEventA(NULL, FALSE, FALSE, NULL);

	if (m_hEvent == NULL)
	{
		DBGSTR("ResMan::StartTimerCallback() - Could not create event (1)\n");

		return (E_FAIL);
	}

	m_hEventCallbackInactive = CreateEventA(NULL, FALSE, FALSE, NULL);

	if (m_hEventCallbackInactive != NULL)
	{
		m_hEventCallbackExit = CreateEventA(NULL, FALSE, FALSE, NULL);

		if (m_hEventCallbackExit != NULL)
		{
			m_dwServiceThreadActive = 1;

			m_hThread = CreateThread(NULL, 0, ServiceThread, this, 0,
						 &m_dwThreadId);

			if (m_hThread != NULL)
			{
				if (SetThreadPriority(m_hThread, THREAD_PRIORITY_TIME_CRITICAL))
				{
					m_dwTimerCallbackStarted = 1;

					return (S_OK);
				}

				wsprintfA(szError, "ResMan::StartTimerCallback - setting thread priority failed!  Error = %d\n",
					  GetLastError());
				OutputDebugStringA(szError);
			}
			else
			{
				DBGSTR("ResMan::StartTimerCallback() - Could not create thread\n");
			}
		}
		else
		{
			DBGSTR("ResMan::StartTimerCallback() - Could not create event (3)\n");
		}
	}
	else
	{
		DBGSTR("ResMan::StartTimerCallback() - Could not create event (2)\n");
	}

	if (m_hEvent != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_hEvent);
		m_hEvent = INVALID_HANDLE_VALUE;
	}

	if (m_hEventCallbackInactive != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_hEventCallbackInactive);
		m_hEventCallbackInactive = INVALID_HANDLE_VALUE;
	}

	if (m_hEventCallbackExit != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_hEventCallbackExit);
		m_hEventCallbackExit = INVALID_HANDLE_VALUE;
	}

	return (E_FAIL);
}

/* (RE) Private DAL interface IID: dbg:0x101409A8. */

static const GUID IID_IA3dPrvA2D =
	{ 0x0a991a7d, 0xc7a7, 0x11d2,
	  { 0xb9, 0x68, 0x00, 0x10, 0x5a, 0x20, 0x24, 0x9d } };

/* =============================================================
// InitDalInterfaces()
// (RE) dbg:0x100607d0
//
// Start each attached DAL through its private interface once.
//
// Returns: S_OK, ignoring interface-query and StartupDal failures.
// =============================================================*/

HRESULT
ResMan::InitDalInterfaces(void)
{
DalInfo        *pDalInfo;
IA3dPrvA2D     *pPrvA2D;
POSITION        pos;
HRESULT         hr;

	pos = m_listDalInfo.GetHeadPosition();

	if (m_dwDalStartedUp == 1)
		return (S_OK);

	while (pos)
	{
		pDalInfo = (DalInfo *) m_listDalInfo.GetNext(pos);

		ASSERT((pDalInfo != 0 &&
		       !IsBadReadPtr(pDalInfo, sizeof(DalInfo))));

		pPrvA2D = NULL;

		hr = pDalInfo->GetIDal()->QueryInterface(IID_IA3dPrvA2D,
							 (void **) &pPrvA2D);
		if (SUCCEEDED(hr))
		{
			pPrvA2D->StartupDal();

			if (pPrvA2D)
			{
				pPrvA2D->Release();
				pPrvA2D = NULL;
			}
		}
	}

	m_dwDalStartedUp = 1;

	return (S_OK);
}

/* =============================================================
// BuildDalTypeMask()
// (RE) dbg:0x10060920
//
// Accumulate the attached DAL types in the stored mask.
// =============================================================*/

void
ResMan::BuildDalTypeMask(void)
{
POSITION        pos;
DalInfo        *pDalInfo;

	pos = m_listDalInfo.GetHeadPosition();

	while (pos != NULL)
	{
		pDalInfo = (DalInfo *) m_listDalInfo.GetNext(pos);

		ASSERT((pDalInfo != 0 && !IsBadReadPtr(pDalInfo, sizeof(DalInfo))));

		m_dwDalTypeMask |= pDalInfo->m_sDalModeDesc.dwVoiceCountMode;
	}
}

/* =============================================================
// GetDalTypeMask()
// (RE) dbg:0x1006fa40; thunk dbg:0x100022de
//
// Read the accumulated DAL type mask.
//
// Returns: The stored DAL type bits.
// =============================================================*/

DWORD
ResMan::GetDalTypeMask(void)
{
	return (m_dwDalTypeMask);
}

/* -------------------------------------------------------------------------- */

/* =============================================================
// ActivateAppHookProc()
// (RE) dbg:0x10061760; rtl:0x10024FF0
//
// Handle application activation messages for the owning window and chain
// the WH_CALLWNDPROC hook.
//
// Returns: CallNextHookEx's result.
// =============================================================*/

static LRESULT CALLBACK
ActivateAppHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
CWPSTRUCT	*pcwp;

	ASSERT((g_lpResMan != 0 &&
	       !IsBadReadPtr(g_lpResMan, sizeof(ResMan))));

	pcwp = (CWPSTRUCT *) lParam;

	if (nCode >= 0 &&
	    IsWindow(g_lpResMan->m_hWnd) &&
	    nCode == 0 &&
	    g_lpResMan->m_dwFocusMuteEnabled &&
	    pcwp->hwnd == g_lpResMan->m_hWnd &&
	    pcwp->message == WM_ACTIVATEAPP)
	{
		if (pcwp->wParam)
			g_lpResMan->OnAppActivated();
		else
			g_lpResMan->OnAppDeactivated();
	}

	return (CallNextHookEx(g_lpResMan->m_hHook, nCode, wParam, lParam));
}

/* =============================================================
// OnAppDeactivated()
// (RE) dbg:0x10061880
//
// Mark the application inactive and queue a focus update.
// =============================================================*/

void
ResMan::OnAppDeactivated(void)
{
	m_dwAppInactive         = 1;
	m_dwFocusChangePending  = 1;
}

/* =============================================================
// OnAppActivated()
// (RE) dbg:0x100618C0
//
// Mark the application active and queue a focus update.
// =============================================================*/

void
ResMan::OnAppActivated(void)
{
	m_dwAppInactive         = 0;
	m_dwFocusChangePending  = 1;
}

/* =============================================================
// SetMaxHardwareSources()
// (RE) rtl:0x10025c60; dbg:0x100632b0
//
// Set the hardware source limit within the device capacity.
//
// Returns:
//   S_OK
//   E_FAIL        without DirectSound
//   E_INVALIDARG  above device capacity
// =============================================================*/

STDMETHODIMP
ResMan::SetMaxHardwareSources(DWORD dwCount)
{
DSCAPS	dsCaps;

	if (m_pDirectSound == NULL)
	{
		DBGSTR("ResMan::SetMaxHardwareSources() - ResMan hasn't been initialized yet.\n");

		return (E_FAIL);
	}

	ASSERT((m_pDirectSound != 0 &&
	       !IsBadReadPtr(m_pDirectSound, sizeof(IDirectSound))));

	memset(&dsCaps, 0, sizeof(dsCaps));
	dsCaps.dwSize = sizeof(dsCaps);

#if defined(A3D_FIXES)
	if (A3dGetConfig().bEmulateHardware)
	{
		A3DCAPS_HARDWARE hardware;
		hardware.dwSize = sizeof(hardware);
		GetHardwareCaps(&hardware);
		dsCaps.dwMaxHwMixingAllBuffers = hardware.dwMax3DBuffers;
	}
	else
#endif
	VERIFY(SUCCEEDED(m_pDirectSound->GetCaps(&dsCaps)));

	if (dwCount > dsCaps.dwMaxHwMixingAllBuffers)
	{
		TRACE("ResMan::SetMaxHardwareSource() - User requested %d, Device only supports %u hardware sources.\n",
		      dwCount, dsCaps.dwMaxHwMixingAllBuffers);

		return (E_INVALIDARG);
	}

	m_dwMaxHardwareSources = dwCount;

	return (S_OK);
}

/* =============================================================
// GetMaxHardwareSources()
// (RE) rtl:0x10025cd0; dbg:0x10063450
//
// Read the configured hardware source limit.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
ResMan::GetMaxHardwareSources(LPDWORD lpdwMaxSources)
{
	if (lpdwMaxSources == NULL)
	{
		DBGSTR("ResMan::GetMaxHardwareSources() - lpdwMaxSources is NULL.\n");

		return (E_POINTER);
	}

	ASSERT((lpdwMaxSources != 0 &&
	       !IsBadReadPtr(lpdwMaxSources, sizeof(DWORD))));

	*lpdwMaxSources = m_dwMaxHardwareSources;

	return (S_OK);
}

/* =============================================================
// ForceHeavyPass()
// (RE) rtl:0x10025d00; dbg:0x10063590
//
// Request a heavy service pass on the next wake.
// =============================================================*/

void
ResMan::ForceHeavyPass(void)
{
	m_dwHeavyPassPending = 1;
}

/* =============================================================
// AcquireW95Interface()
// (RE) rtl:0x10025D10; dbg:0x10063700
//
// Initialize and register the hardware A3D DAL from the COM server.
// Initialization bit 8 is forwarded; its external DAL meaning is unknown.
//
// Returns: S_OK; a COM, InitializeEx or DalInfo::Init failure; E_OUTOFMEMORY if
//          the DalInfo cannot be allocated.
// =============================================================*/

HRESULT
ResMan::AcquireW95Interface(LPGUID pGuidDevice, DWORD dwFlags, DWORD dwReserved,
			   LPDWORD lpdwFeaturesEnabled)
{
IA3d2          *pIA3d;
IA3dDal        *pIA3dDal;
DalInfo        *pDalInfo;
DALMODEDESC     sModeDesc;
HRESULT         hr;

	ASSERT((lpdwFeaturesEnabled != 0 &&
	       !IsBadReadPtr(lpdwFeaturesEnabled, sizeof(DWORD))));

	*lpdwFeaturesEnabled = 0;
	pIA3d = NULL;

	hr = CoCreateInstance(CLSID_A3d, NULL, CLSCTX_INPROC_SERVER, IID_IA3d,
			      (void **) &pIA3d);

	if (FAILED(hr))
	{
		DBGSTR("ResMan::AcquireW95Interface - Failed to get the IA3d Interface.\n");

		return (hr);
	}

	ASSERT((pIA3d != 0 && !IsBadReadPtr(pIA3d, sizeof(IA3d2))));

	pIA3dDal = NULL;

	hr = pIA3d->QueryInterface(IID_IA3dDal, (void **) &pIA3dDal);

	if (FAILED(hr))
	{
		DBGSTR("ResMan::AcquireW95Interface - Failed to get the IA3dDal Interface.\n");

		pIA3d->Release();

		return (hr);
	}

	ASSERT((pIA3dDal != 0 && !IsBadReadPtr(pIA3dDal, sizeof(IA3dDal))));

	hr = pIA3dDal->InitializeEx(pGuidDevice, dwFlags, dwReserved | 8,
				    lpdwFeaturesEnabled);

	if (FAILED(hr))
	{
		DBGSTR("ResMan::AcquireW95Interface - Failed to initialize W95 object\n");

		pIA3dDal->Release();

		return (hr);
	}

	VERIFY(SUCCEEDED(pIA3d->SetResourceManagerMode(0)));

	pIA3d->Release();
	pIA3d = NULL;

	sModeDesc.dwFlags               = A3D_DALMODE_LOCKED_BUFFER_FILL |
	                                  A3D_DALMODE_HARDWARE_STATUS |
	                                  A3D_DALMODE_MAIN_ELIGIBLE |
	                                  A3D_DALMODE_DISABLE_SCALE_HACK;
	sModeDesc.dwVoiceCountMode      = A3D_DAL_VOICE_COUNT_3D;
	sModeDesc.dwStreamingBufferSize = A3D_RESMAN_DEFAULT_STREAM_BUFFER_BYTES;
	sModeDesc.dwSampleRate          = 22050;
	sModeDesc.dwBitsPerSample       = 16;
	sModeDesc.dwRefillThreshold     = A3D_RESMAN_DEFAULT_REFILL_THRESHOLD_BYTES;
	sModeDesc.dwRefillTarget        = A3D_RESMAN_DEFAULT_REFILL_TARGET_BYTES;
	sModeDesc.dwBufferFlags         = DSBCAPS_LOCHARDWARE | DSBCAPS_CTRL3D |
	                                  DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLPAN |
	                                  DSBCAPS_CTRLVOLUME;
	sModeDesc.dwChannels            = 1;
	sModeDesc.dwTotalBufferCap      = A3D_RESMAN_DEFAULT_DAL_BUFFER_CAP;
	sModeDesc.dwName                = mmioFOURCC('W', '9', '5', '\0');

	pDalInfo = new DalInfo(pIA3dDal, &sModeDesc);

	if (!pDalInfo)
	{
		DBGSTR("ResMan::AcquireW95Interface - Not enough memory to create new A3D DalInfo object.\n");

		pIA3dDal->Release();

		return (E_OUTOFMEMORY);
	}

	pIA3dDal->Release();
	pIA3dDal = NULL;

	hr = pDalInfo->Init();

	if (FAILED(hr))
	{
		delete pDalInfo;

		return (hr);
	}

	m_listDalInfo.AddTail(pDalInfo);

	return (S_OK);
}

/* =============================================================
// AcquireD3DInterface()
// (RE) dbg:0x10063C30; rtl:0x10025F60
//
// Initialize and register the DirectSound3D DAL.
// fRequireA3d follows the DAL parameter role;
//
// Returns: S_OK; an InitializeEx or DalInfo::Init failure; E_OUTOFMEMORY if the
//          DAL or DalInfo cannot be allocated.
// =============================================================*/

HRESULT
ResMan::AcquireD3DInterface(LPGUID pGuidDevice, DWORD dwFlags, DWORD dwReserved,
			   LPDWORD lpdwFeaturesEnabled, int fRequireA3d)
{
IA3dDal        *pIA3dDal;
DalInfo        *pDalInfo;
DALMODEDESC     sModeDesc;
HRESULT         hr;

	pIA3dDal = new DAL_D3D((void *) fRequireA3d);

	if (!pIA3dDal)
	{
		DBGSTR("ResMan::AcquireD3DInterface - Not enough memory to create new D3D object.\n");

		return (E_OUTOFMEMORY);
	}

	ASSERT((pIA3dDal != 0 && !IsBadReadPtr(pIA3dDal, sizeof(IA3dDal))));

	hr = pIA3dDal->InitializeEx(pGuidDevice, dwFlags, dwReserved,
				    lpdwFeaturesEnabled);

	if (FAILED(hr))
	{
		DBGSTR("ResMan::AcquireD3DInterface - Failed to initialize D3D object\n");

		pIA3dDal->Release();

		return (hr);
	}

	sModeDesc.dwFlags               = A3D_DALMODE_LOCKED_BUFFER_FILL | A3D_DALMODE_HARDWARE_STATUS | A3D_DALMODE_MAIN_ELIGIBLE;
	sModeDesc.dwVoiceCountMode      = A3D_DAL_VOICE_COUNT_3D;
	sModeDesc.dwStreamingBufferSize = A3D_RESMAN_DEFAULT_STREAM_BUFFER_BYTES;
	sModeDesc.dwSampleRate          = 22050;
	sModeDesc.dwBitsPerSample       = 16;
	sModeDesc.dwRefillThreshold     = A3D_RESMAN_DEFAULT_REFILL_THRESHOLD_BYTES;
	sModeDesc.dwRefillTarget        = A3D_RESMAN_DEFAULT_REFILL_TARGET_BYTES;
	sModeDesc.dwBufferFlags         = DSBCAPS_LOCHARDWARE | DSBCAPS_CTRL3D | DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLPAN | DSBCAPS_CTRLVOLUME;
	sModeDesc.dwChannels            = 1;
	sModeDesc.dwTotalBufferCap      = A3D_RESMAN_DEFAULT_DAL_BUFFER_CAP;
	sModeDesc.dwName                = mmioFOURCC('D', '3', 'D', '\0');

	pDalInfo = new DalInfo(pIA3dDal, &sModeDesc);

	if (!pDalInfo)
	{
		DBGSTR("ResMan::AcquireD3DInterface - Not enough memory to create new D3D DalInfo object.\n");

		pIA3dDal->Release();

		return (E_OUTOFMEMORY);
	}

	pIA3dDal->Release();

	hr = pDalInfo->Init();

	if (FAILED(hr))
	{
		delete pDalInfo;

		return (hr);
	}

	m_listDalInfo.AddTail(pDalInfo);

	return (S_OK);
}

/* =============================================================
// AcquireA2DInterface()
// (RE) dbg:0x10064050; rtl:0x10026160
//
// Initialize and register the software-mixer DAL.
//
// Returns: S_OK; an InitializeEx or DalInfo::Init failure; E_OUTOFMEMORY if the
//          DAL or DalInfo cannot be allocated.
// =============================================================*/

HRESULT
ResMan::AcquireA2DInterface(LPGUID pGuidDevice, DWORD dwFlags, DWORD dwReserved,
			   LPDWORD lpdwFeaturesEnabled)
{
IA3dDal        *pIA3dDal;
DalInfo        *pDalInfo;
DALMODEDESC     sModeDesc;
HRESULT         hr;

	pIA3dDal = new DAL_A2D;

	if (pIA3dDal)
	{
		ASSERT((pIA3dDal != 0 && !IsBadReadPtr(pIA3dDal, sizeof(IA3dDal))));

		hr = pIA3dDal->InitializeEx(pGuidDevice, dwFlags, dwReserved,
					    lpdwFeaturesEnabled);

		if (FAILED(hr))
		{
			DBGSTR("ResMan::AcquireA2DInterface - Failed to initialize A2D object\n");

			pIA3dDal->Release();

			return (hr);
		}

		sModeDesc.dwFlags               = A3D_DALMODE_LOCKED_BUFFER_FILL | A3D_DALMODE_SOFTWARE_STATUS | A3D_DALMODE_MAIN_ELIGIBLE;
		sModeDesc.dwVoiceCountMode      = A3D_DAL_VOICE_COUNT_3D;
		sModeDesc.dwStreamingBufferSize = A3D_RESMAN_DEFAULT_STREAM_BUFFER_BYTES;
		sModeDesc.dwSampleRate          = 22050;
		sModeDesc.dwBitsPerSample       = 16;
		sModeDesc.dwRefillThreshold     = A3D_RESMAN_DEFAULT_REFILL_THRESHOLD_BYTES;
		sModeDesc.dwRefillTarget        = A3D_RESMAN_DEFAULT_REFILL_TARGET_BYTES;
		sModeDesc.dwBufferFlags         = DSBCAPS_CTRL3D | DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLPAN | DSBCAPS_CTRLVOLUME;
		sModeDesc.dwChannels            = 1;
		sModeDesc.dwTotalBufferCap      = A3D_RESMAN_DEFAULT_DAL_BUFFER_CAP;
		sModeDesc.dwName                = mmioFOURCC('A', '2', 'D', '\0');

		pDalInfo = new DalInfo(pIA3dDal, &sModeDesc);

		if (pDalInfo)
		{
			pIA3dDal->Release();

			hr = pDalInfo->Init();

			if (FAILED(hr))
			{
				delete pDalInfo;

				return (hr);
			}

			m_listDalInfo.AddTail(pDalInfo);

			return (S_OK);
		}
		else
		{
			DBGSTR("ResMan::AcquireA2DInterface - Not enough memory to create new A2D DalInfo object.\n");

			pIA3dDal->Release();

			return (E_OUTOFMEMORY);
		}
	}
	else
	{
		DBGSTR("ResMan::AcquireA2DInterface - Not enough memory to create new A2D object.\n");

		return (E_OUTOFMEMORY);
	}
}

/* =============================================================
// AcquireD2DInterface()
// (RE) dbg:0x10064470; rtl:0x10026360
//
// Initialize and register the DirectSound DAL.
//
// Returns: S_OK; an InitializeEx or DalInfo::Init failure; E_OUTOFMEMORY if the
//          DAL or DalInfo cannot be allocated.
// =============================================================*/

HRESULT
ResMan::AcquireD2DInterface(LPGUID pGuidDevice, DWORD dwFlags, DWORD dwReserved,
			   LPDWORD lpdwFeaturesEnabled)
{
IA3dDal        *pIA3dDal;
DalInfo        *pDalInfo;
DALMODEDESC     sModeDesc;
HRESULT         hr;

	pIA3dDal = new DAL_D2D;

	if (pIA3dDal)
	{
		ASSERT((pIA3dDal != 0 && !IsBadReadPtr(pIA3dDal, sizeof(IA3dDal))));

		hr = pIA3dDal->InitializeEx(pGuidDevice, dwFlags, dwReserved,
					    lpdwFeaturesEnabled);

		if (FAILED(hr))
		{
			DBGSTR("ResMan::AcquireD2DInterface - Failed to initialize D2D object\n");

			pIA3dDal->Release();

			return (hr);
		}

		sModeDesc.dwFlags               = A3D_DALMODE_LOCKED_BUFFER_FILL;
		sModeDesc.dwVoiceCountMode      = A3D_DAL_VOICE_COUNT_MIXING;
		sModeDesc.dwStreamingBufferSize = A3D_RESMAN_DEFAULT_STREAM_BUFFER_BYTES;
		sModeDesc.dwSampleRate          = 22050;
		sModeDesc.dwBitsPerSample       = 16;
		sModeDesc.dwRefillThreshold     = A3D_RESMAN_DEFAULT_REFILL_THRESHOLD_BYTES;
		sModeDesc.dwRefillTarget        = A3D_RESMAN_DEFAULT_REFILL_TARGET_BYTES;
		sModeDesc.dwBufferFlags         = DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLPAN | DSBCAPS_CTRLVOLUME;
		sModeDesc.dwChannels            = 2;
		sModeDesc.dwTotalBufferCap      = A3D_RESMAN_DEFAULT_DAL_BUFFER_CAP;
		sModeDesc.dwName                = mmioFOURCC('D', '2', 'D', '\0');

		pDalInfo = new DalInfo(pIA3dDal, &sModeDesc);

		if (pDalInfo)
		{
			pIA3dDal->Release();

			hr = pDalInfo->Init();

			if (FAILED(hr))
			{
				delete pDalInfo;

				return (hr);
			}

			m_listDalInfo.AddTail(pDalInfo);

			return (S_OK);
		}
		else
		{
			DBGSTR("ResMan::AcquireD2DInterface - Not enough memory to create new D2D DalInfo object.\n");

			pIA3dDal->Release();

			return (E_OUTOFMEMORY);
		}
	}
	else
	{
		DBGSTR("ResMan::AcquireD2DInterface - Not enough memory to create new D2D object.\n");

		return (E_OUTOFMEMORY);
	}
}

/* =============================================================
// AcquireEMUInterface()
// (RE) dbg:0x10064890; rtl:0x10026560
//
// Initialize and register the emulation DAL.
//
// Returns: S_OK; an InitializeEx or DalInfo::Init failure; E_OUTOFMEMORY if the
//          DAL or DalInfo cannot be allocated.
// =============================================================*/

HRESULT
ResMan::AcquireEMUInterface(LPGUID pGuidDevice, DWORD dwFlags, DWORD dwReserved,
			   LPDWORD lpdwFeaturesEnabled)
{
IA3dDal        *pIA3dDal;
DalInfo        *pDalInfo;
DALMODEDESC     sModeDesc;
HRESULT         hr;

	pIA3dDal = new DAL_EMU;

	if (pIA3dDal)
	{
		ASSERT((pIA3dDal != 0 && !IsBadReadPtr(pIA3dDal, sizeof(IA3dDal))));

		hr = pIA3dDal->InitializeEx(pGuidDevice, dwFlags, dwReserved,
					    lpdwFeaturesEnabled);

		if (FAILED(hr))
		{
			DBGSTR("ResMan::AcquireEMUInterface - Failed to initialize EMU object\n");

			pIA3dDal->Release();

			return (hr);
		}

		sModeDesc.dwFlags               = 0;
		sModeDesc.dwVoiceCountMode      = A3D_DAL_VOICE_COUNT_3D;
		sModeDesc.dwStreamingBufferSize = A3D_RESMAN_DEFAULT_STREAM_BUFFER_BYTES;
		sModeDesc.dwSampleRate          = 22050;
		sModeDesc.dwBitsPerSample       = 16;
		sModeDesc.dwRefillThreshold     = A3D_RESMAN_DEFAULT_REFILL_THRESHOLD_BYTES;
		sModeDesc.dwRefillTarget        = A3D_RESMAN_DEFAULT_REFILL_TARGET_BYTES;
		sModeDesc.dwBufferFlags         = DSBCAPS_CTRL3D | DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLPAN | DSBCAPS_CTRLVOLUME;
		sModeDesc.dwChannels            = 1;
		sModeDesc.dwTotalBufferCap      = A3D_RESMAN_DEFAULT_DAL_BUFFER_CAP;
		sModeDesc.dwName                = mmioFOURCC('E', 'M', 'U', '\0');

		pDalInfo = new DalInfo(pIA3dDal, &sModeDesc);

		if (pDalInfo)
		{
			pIA3dDal->Release();

			hr = pDalInfo->Init();

			if (FAILED(hr))
			{
				delete pDalInfo;

				return (hr);
			}

			m_listDalInfo.AddTail(pDalInfo);

			return (S_OK);
		}
		else
		{
			DBGSTR("ResMan::AcquireEMUInterface - Not enough memory to create new EMU DalInfo object.\n");

			pIA3dDal->Release();

			return (E_OUTOFMEMORY);
		}
	}
	else
	{
		DBGSTR("ResMan::AcquireEMUInterface - Not enough memory to create new EMU object.\n");

		return (E_OUTOFMEMORY);
	}
}

/* =============================================================
// AcquireInterfaces()
// (RE) rtl:0x10026760; dbg:0x10064CB0
//
// Create DirectSound and register DALs, allowing hardware failures to fall
// back to software. Preserve the original transposed flags/reserved arguments.
//
// Returns: S_OK; COM, DirectSound or required software-DAL initialization
//          failure; E_FAIL for a null DirectSound interface or disabled
//          DirectSound without a DAL.
// =============================================================*/

HRESULT
ResMan::AcquireInterfaces(LPGUID pGuidDevice, DWORD dwReserved, DWORD dwFlags,
			  LPDWORD lpdwFeatures)
{
DWORD	dwAvailable;
HRESULT	hr;

	hr = CoInitialize(NULL);

	if (FAILED(hr))
	{
		DBGSTR("ResMan::AquireInterfaces - Failed to initialize COM\n");

		return (hr);
	}

	hr = CoCreateInstance(CLSID_DirectSound, NULL, CLSCTX_INPROC_SERVER,
			      IID_IDirectSound, (void **) &m_pDirectSound);

	if (FAILED(hr))
	{
		DBGSTR("ResMan::AquireInterfaces - Failed to get DS\n");

		return (hr);
	}

	if (!m_pDirectSound)
	{
		DBGSTR("ResMan::AquireInterfaces - Failed to get DS\n");

		return (E_FAIL);
	}

	hr = m_pDirectSound->Initialize(NULL);

	if (FAILED(hr))
	{
		DBGSTR("ResMan::AquireInterfaces() - Could not init DS\n");

		return (hr);
	}

#if defined(A3D_FIXES)
	A3dGetConfig();
#endif

	*lpdwFeatures = 0;

#if defined(A3D_FIXES)
	if (!A3dGetConfig().bSoftwareReverb &&
	    !A3dGetConfig().bSoftwareReflections)
#endif
	{
		// NOT PART OF THE ORIGINAL: either software effect requires A2D.
		hr = AcquireD3DInterface(pGuidDevice, dwReserved, dwFlags, &dwAvailable, 1);

		if (FAILED(hr))
		{
			hr = AcquireW95Interface(pGuidDevice, dwReserved, dwFlags,
						 &dwAvailable);

			if (FAILED(hr))
				AcquireD3DInterface(pGuidDevice, dwReserved, dwFlags,
						    &dwAvailable, 0);
		}

		*lpdwFeatures |= dwAvailable;
	}
#if defined(A3D_FIXES)
	if (A3dGetConfig().bSoftwareReflections)
	{
		// NOT PART OF THE ORIGINAL: grant the software reflection taps.
		*lpdwFeatures |= A3D_1ST_REFLECTIONS;
	}
#endif

	if (m_listDalInfo.IsEmpty() && (dwFlags & A3DINIT_DISABLE_DS))
	{
		DBGSTR("ResMan::AcquireInterfaces - DirectSound disabled by user.\n");

		return (E_FAIL);
	}

	hr = AcquireA2DInterface(pGuidDevice, dwReserved, dwFlags, &dwAvailable);

	if (FAILED(hr))
	{
		DBGSTR("ResMan::AquireInterfaces - Failed to get required DAL_A2D\n");

		return (hr);
	}

	*lpdwFeatures |= dwAvailable;

	hr = AcquireD2DInterface(pGuidDevice, dwReserved, dwFlags, &dwAvailable);

	if (FAILED(hr))
	{
		DBGSTR("ResMan::AquireInterfaces - Failed to get required DAL_D2D\n");

		return (hr);
	}

	*lpdwFeatures |= dwAvailable;

	hr = AcquireEMUInterface(pGuidDevice, dwReserved, dwFlags, &dwAvailable);

	if (FAILED(hr))
	{
		DBGSTR("ResMan::AquireInterfaces - Failed to get required DAL_EMU\n");

		return (hr);
	}

	*lpdwFeatures |= dwAvailable;

	return (S_OK);
}

/* =============================================================
// DrainBufferList()
// (RE) dbg:0x10065210; rtl:0x100268B0; thunk dbg:0x10001b68
//
// Empty a buffer list, returning DAL buffers and deleting resource buffers.
// =============================================================*/

void
ResMan::DrainBufferList(CList *pList)
{
void                   *pElement;
IResManBufferPrimary   *pStatBuffer;
IResManBufferPrimary   *pStreamBuffer;
DalBufferInfo          *pDalBufferInfo;
DWORD                   dwTick;
HRESULT                 hr1;
HRESULT                 hr2;

	dwTick = GetTickCount();

	while ((pElement = pList->RemoveHead()) != NULL)
	{
		pDalBufferInfo = NULL;

		(static_cast<IResManBufferPrimary *>((IUnknown *) pElement))->
			GetDalBufferInfo(&pDalBufferInfo);

		if (pDalBufferInfo)
			ReturnBufferToDal(dwTick, (ResManBuffer *) pElement,
					   pDalBufferInfo);

		pStatBuffer   = NULL;
		pStreamBuffer = NULL;

		hr1   = ((IUnknown *) pElement)->
			QueryInterface(IID_ResManStatBuffer, (void **) &pStatBuffer);
		hr2 = ((IUnknown *) pElement)->
			QueryInterface(IID_ResManStreamBuffer, (void **) &pStreamBuffer);

		ASSERT(SUCCEEDED(hr1) || SUCCEEDED(hr2));

		if (pStreamBuffer)
			delete pStreamBuffer;

		if (pStatBuffer)
			delete pStatBuffer;
	}
}

/* =============================================================
// EmptyBufferLists()
// (RE) dbg:0x100653e0; thunk dbg:0x10001b6d
//
// Drain the streaming, static and two-channel buffer lists.
// =============================================================*/

void
ResMan::EmptyBufferLists(void)
{
	DrainBufferList(&m_ResManStreamBufferList);
	DrainBufferList(&m_ResManStatBufferList);
	DrainBufferList(&m_ResMan2ChBufferList);
}

/* =============================================================
// ReleaseInterfaces()
// (RE) dbg:0x100650B0; inlined in destructor rtl:0x10023520
//
// Delete the DAL list, release property-set and DirectSound interfaces,
// and check the Easter egg.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
ResMan::ReleaseInterfaces(void)
{
DalInfo		*pDalInfo;

	while ((pDalInfo = (DalInfo *) m_listDalInfo.RemoveHead()) != NULL)
	{
		ASSERT((pDalInfo != 0 && !IsBadReadPtr(pDalInfo, sizeof(DalInfo))));

		delete pDalInfo;
	}

	if (m_lpPropertySet != NULL)
	{
		m_lpPropertySet->Release();
		m_lpPropertySet = NULL;
	}

	if (m_pDirectSound != NULL)
	{
		m_pDirectSound->Release();
		m_pDirectSound = NULL;
	}

	CheckEasterEgg("ResMan::ReleaseInterfaces() - completed");

	return (S_OK);
}

/* =============================================================
// GetPriorityWeight()
// (RE) rtl:0x10027700; dbg:0x100679b0
//
// Read the commit pass's priority weight.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
ResMan::GetPriorityWeight(LPA3DVAL pfPriorityWeight)
{
	ASSERT((pfPriorityWeight != 0 &&
	       !IsBadReadPtr(pfPriorityWeight, sizeof(float))));

	if (pfPriorityWeight != NULL)
	{
		*pfPriorityWeight = m_fPriorityWeight;

		return (S_OK);
	}

	DBGSTR("ResMan::GetPriorityWeight() - NULL pointer passed in.\n");

	return (E_POINTER);
}

/* =============================================================
// SetPriorityWeight()
// (RE) rtl:0x10027730; dbg:0x10067a70
//
// Set the commit pass's priority weight.
//
// Returns:
//   S_OK
//   E_INVALIDARG  outside 0.0 to 1.0
// =============================================================*/

STDMETHODIMP
ResMan::SetPriorityWeight(A3DVAL fPriorityWeight)
{
	ASSERT(fPriorityWeight >= 0.0 && fPriorityWeight <= 1.0);

	if (fPriorityWeight >= 0.0 && fPriorityWeight <= 1.0)
	{
		m_fPriorityWeight = fPriorityWeight;

		return (S_OK);
	}

	DBGSTR("ResMan::SetPriorityWeight() - Invalid value sent.\n");

	return (E_INVALIDARG);
}

/* =============================================================
// ReadA3dRegistryDword()
// (RE) rtl:0x10027770; dbg:0x10067b50
//
// Read a named value of up to four bytes from HKLM\Software\Aureal\A3D.
//
// Returns:
//   S_OK    if queried successfully
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

		if (RegQueryValueExA(hKey, lpValueName, 0, 0, lpData, &cbData) == ERROR_SUCCESS)
			hr = S_OK;

		if (hKey)
			RegCloseKey(hKey);
	}

	return (hr);
}

/* =============================================================
// SetBufferLatency()
// (RE) rtl:0x100277e0; dbg:0x10067bf0
//
// Set every DAL's latency in seconds, using StreamingBufferLatency
// from the registry when present and within 0 to 5000 milliseconds.
//
// Returns:
//   S_OK
//   E_INVALIDARG  if the argument is outside 0.0 to 5.0 seconds
// =============================================================*/

STDMETHODIMP
ResMan::SetBufferLatency(A3DVAL fBufferLatency)
{
POSITION        pos;
DalInfo        *pDalInfo;
DWORD           dwRegValue;

	ASSERT(fBufferLatency >= 0.0 && fBufferLatency <= 5.0);

	if (fBufferLatency >= 0.0 && fBufferLatency <= A3D_RESMAN_MAX_STREAM_SETTING_SECONDS)
	{
		if (SUCCEEDED(ReadA3dRegistryDword("StreamingBufferLatency",
						   (LPBYTE) &dwRegValue)) &&
		    dwRegValue <= A3D_RESMAN_MAX_STREAM_SETTING_MS)
			fBufferLatency = (A3DVAL) ((double) dwRegValue / 1000.0);

		pos = m_listDalInfo.GetHeadPosition();

		while (pos != NULL)
		{
			pDalInfo = (DalInfo *) m_listDalInfo.GetNext(pos);

			ASSERT((pDalInfo != 0 &&
			       !IsBadReadPtr(pDalInfo, sizeof(DalInfo))));

			SetDalBufferLatency(pDalInfo, fBufferLatency);
		}

		return (S_OK);
	}

	DBGSTR("ResMan::SetBufferLatency() - Invalid value sent.\n");

	return (E_INVALIDARG);
}

/* =============================================================
// GetBufferLatency()
// (RE) rtl:0x100278f0; dbg:0x10067dc0
//
// Read the main DAL's streaming latency in seconds.
//
// Returns:
//   GetDalBufferLatency's result
//   E_POINTER                     a null output
// =============================================================*/

STDMETHODIMP
ResMan::GetBufferLatency(LPA3DVAL pfBufferLatency)
{
	ASSERT((pfBufferLatency != 0 &&
	       !IsBadReadPtr(pfBufferLatency, sizeof(float))));

	if (pfBufferLatency != NULL)
	{
		ASSERT((m_pMainDALInfo != 0 &&
		       !IsBadReadPtr(m_pMainDALInfo, sizeof(DalInfo))));

		return (GetDalBufferLatency(m_pMainDALInfo, pfBufferLatency));
	}

	DBGSTR("ResMan::GetBufferLatency() - NULL pointer passed in.\n");

	return (E_POINTER);
}

/* =============================================================
// SetDalBufferLatency()
// (RE) dbg:0x10067ee0
//
// Convert a latency in seconds to an even byte count in the DAL descriptor.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
ResMan::SetDalBufferLatency(DalInfo *pDalInfo, A3DVAL fBufferLatency)
{
DALMODEDESC	sModeDesc;
FLOAT		fByteRate;
DWORD		dwBytes;

	pDalInfo->GetModeDesc(&sModeDesc);

	fByteRate = (FLOAT) ((sModeDesc.dwBitsPerSample >> 3) * sModeDesc.dwSampleRate);

	dwBytes = (DWORD) (LONGLONG) (fBufferLatency * fByteRate);
	dwBytes &= ~1u;

	sModeDesc.dwRefillTarget = dwBytes;

	pDalInfo->SetModeDesc(&sModeDesc);

	return (S_OK);
}

/* =============================================================
// GetDalBufferLatency()
// (RE) dbg:0x10067f60
//
// Convert the DAL's target byte count to a latency in seconds.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
ResMan::GetDalBufferLatency(DalInfo *pDalInfo, LPA3DVAL pfBufferLatency)
{
DALMODEDESC	sModeDesc;
FLOAT		fByteRate;

	pDalInfo->GetModeDesc(&sModeDesc);

	fByteRate = (FLOAT) ((sModeDesc.dwBitsPerSample >> 3) * sModeDesc.dwSampleRate);

	*pfBufferLatency = (A3DVAL) ((double) sModeDesc.dwRefillTarget / fByteRate);

	return (S_OK);
}

/* =============================================================
// SetBufferRefreshThreshold()
// (RE) rtl:0x10027950; dbg:0x10067fd0
//
// Set every DAL's refresh threshold in seconds, using the registry value
// StreamingBufferRefreshThreshold when within 0 to 5000 milliseconds.
// Preserve the original SetBufferLatency label in the error diagnostic.
//
// Returns:
//   S_OK
//   E_INVALIDARG  if the argument is outside 0.0 to 5.0 seconds
// =============================================================*/

STDMETHODIMP
ResMan::SetBufferRefreshThreshold(A3DVAL fBufferRefreshThreshold)
{
POSITION        pos;
DalInfo        *pDalInfo;
DWORD           dwRegValue;

	ASSERT(fBufferRefreshThreshold >= 0.0 && fBufferRefreshThreshold <= 5.0);

	if (fBufferRefreshThreshold >= 0.0 && fBufferRefreshThreshold <= A3D_RESMAN_MAX_STREAM_SETTING_SECONDS)
	{
		if (SUCCEEDED(ReadA3dRegistryDword("StreamingBufferRefreshThreshold",
						   (LPBYTE) &dwRegValue)) &&
		    dwRegValue <= A3D_RESMAN_MAX_STREAM_SETTING_MS)
			fBufferRefreshThreshold = (A3DVAL) ((double) dwRegValue / 1000.0);

		pos = m_listDalInfo.GetHeadPosition();

		while (pos != NULL)
		{
			pDalInfo = (DalInfo *) m_listDalInfo.GetNext(pos);

			ASSERT((pDalInfo != 0 &&
			       !IsBadReadPtr(pDalInfo, sizeof(DalInfo))));

			SetDalBufferRefreshThreshold(pDalInfo, fBufferRefreshThreshold);
		}

		return (S_OK);
	}

	DBGSTR("ResMan::SetBufferLatency() - Invalid value sent.\n");

	return (E_INVALIDARG);
}

/* =============================================================
// SetDalBufferRefreshThreshold()
// (RE) dbg:0x100681a0
//
// Convert a threshold in seconds to an even byte count in the DAL descriptor.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
ResMan::SetDalBufferRefreshThreshold(DalInfo *pDalInfo, A3DVAL fBufferRefreshThreshold)
{
DALMODEDESC	sModeDesc;
FLOAT		fByteRate;
DWORD		dwBytes;

	pDalInfo->GetModeDesc(&sModeDesc);

	fByteRate = (FLOAT) ((sModeDesc.dwBitsPerSample >> 3) * sModeDesc.dwSampleRate);

	dwBytes = (DWORD) (LONGLONG) (fBufferRefreshThreshold * fByteRate);
	dwBytes &= ~1u;

	sModeDesc.dwRefillThreshold = dwBytes;

	pDalInfo->SetModeDesc(&sModeDesc);

	return (S_OK);
}

/* =============================================================
// GetDalBufferRefreshThreshold()
// (RE) dbg:0x10068220
//
// Convert the DAL's refresh byte count to a threshold in seconds.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
ResMan::GetDalBufferRefreshThreshold(DalInfo *pDalInfo, LPA3DVAL pfBufferRefreshThreshold)
{
DALMODEDESC	sModeDesc;
FLOAT		fByteRate;

	pDalInfo->GetModeDesc(&sModeDesc);

	fByteRate = (FLOAT) ((sModeDesc.dwBitsPerSample >> 3) * sModeDesc.dwSampleRate);

	*pfBufferRefreshThreshold = (A3DVAL) ((double) sModeDesc.dwRefillThreshold / fByteRate);

	return (S_OK);
}

/* =============================================================
// GetBufferRefreshThreshold()
// (RE) rtl:0x10027a60; dbg:0x10068290
//
// Read the main DAL's refresh threshold in seconds.
//
// Returns:
//   GetDalBufferRefreshThreshold's result
//   E_POINTER                           a null output
// =============================================================*/

STDMETHODIMP
ResMan::GetBufferRefreshThreshold(LPA3DVAL pfBufferRefreshThreshold)
{
	ASSERT((pfBufferRefreshThreshold != 0 &&
	       !IsBadReadPtr(pfBufferRefreshThreshold, sizeof(float))));

	if (pfBufferRefreshThreshold != NULL)
	{
		ASSERT((m_pMainDALInfo != 0 &&
		       !IsBadReadPtr(m_pMainDALInfo, sizeof(DalInfo))));

		return (GetDalBufferRefreshThreshold(m_pMainDALInfo,
						     pfBufferRefreshThreshold));
	}

	DBGSTR("ResMan::GetBufferRefreshThreshold() - NULL pointer passed in.\n");

	return (E_POINTER);
}

/* =============================================================
// GetHardwareSourceCapacity()
// (RE) rtl:0x100251e0; dbg:0x10062110
//
// Sum hardware mixing capacity across DALs with mode flag 0x04.
//
// Returns: S_OK, ignoring unavailable interfaces and GetCaps failures;
//          E_POINTER for a null output.
// =============================================================*/

STDMETHODIMP
ResMan::GetHardwareSourceCapacity(LPDWORD lpdwCount)
{
POSITION        pos;
DalInfo        *pDalInfo;
IDirectSound   *lpDS;
DSCAPS          dsCaps;
HRESULT         hr;

	if (lpdwCount == NULL)
		return (E_POINTER);

	*lpdwCount = 0;

	pos = m_listDalInfo.GetHeadPosition();

	while (pos != NULL)
	{
		pDalInfo = (DalInfo *) m_listDalInfo.GetNext(pos);

		ASSERT((pDalInfo != 0 &&
		       !IsBadReadPtr(pDalInfo, sizeof(DalInfo))));

		if (pDalInfo->m_sDalModeDesc.dwFlags & A3D_DALMODE_SOFTWARE_STATUS)
		{
			lpDS = pDalInfo->GetIDirectSound();

			if (lpDS != NULL)
			{
				ASSERT((lpDS != 0 &&
				       !IsBadReadPtr(lpDS, sizeof(IDirectSound))));

				memset(&dsCaps, 0, sizeof(dsCaps));
				dsCaps.dwSize = sizeof(dsCaps);

				hr = lpDS->GetCaps(&dsCaps);

				if (SUCCEEDED(hr))
					*lpdwCount += dsCaps.dwMaxHwMixingAllBuffers;
			}
		}
	}

	return (S_OK);
}

/* =============================================================
// SetHardwareSourceLimit()
// (RE) rtl:0x10025150; dbg:0x10062050
//
// Set the hardware source limit within the current DAL capacity.
//
// Returns:
//   S_OK
//   E_INVALIDARG  above GetHardwareSourceCapacity's reported capacity
// =============================================================*/

STDMETHODIMP
ResMan::SetHardwareSourceLimit(DWORD dwCount)
{
DWORD	dwMax;

	GetHardwareSourceCapacity(&dwMax);

	if (dwCount > dwMax)
		return (E_INVALIDARG);

	m_dwHwVoiceCap = dwCount;

	return (S_OK);
}

/* =============================================================
// GetHardwareSourceLimit()
// (RE) rtl:0x10025190; dbg:0x100620a0
//
// Clamp the stored hardware source limit to current capacity and return it.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
ResMan::GetHardwareSourceLimit(LPDWORD lpdwCount)
{
DWORD	dwMax;

	if (lpdwCount == NULL)
		return (E_POINTER);

	GetHardwareSourceCapacity(&dwMax);

	if (m_dwHwVoiceCap > dwMax)
		m_dwHwVoiceCap = dwMax;

	*lpdwCount = m_dwHwVoiceCap;

	return (S_OK);
}

/* =============================================================
// QuerySupport()
// (RE) rtl:0x10025260; dbg:0x100622c0
//
// Queue a property-support query and wait for the service thread's result.
// The original can read back a null list position after allocation or
// initial mutex-wait failure.
//
// Returns:
//   The queued call's result
//   E_FAIL                    if the flush or read-back mutex wait fails
// =============================================================*/

STDMETHODIMP
ResMan::QuerySupport(REFGUID rguidPropSet, ULONG ulId, PULONG pulTypeSupport)
{
CPropertySetItem       *pItem;
POSITION                pos;
HRESULT                 hr;

	hr      = E_FAIL;
	pos     = NULL;

	pItem = new CPropertySetItem;

	if (pItem != NULL)
	{
		if (WaitForSingleObject(m_hPropSetMutex, INFINITE) == WAIT_OBJECT_0)
		{
			pItem->Create(rguidPropSet, ulId, pulTypeSupport);
			pos = m_listPropSetQueue.AddTail(pItem);
		}

		VERIFY(ReleaseMutex(m_hPropSetMutex));
	}

	if (A3D_WAIT_PROPSET_FLUSH(m_hPropSetCacheFlushed) != WAIT_OBJECT_0)
	{
		DBGSTR("ResMan::QuerySupport() - TimerCallback failed to signal Property Set event.\n");
		ASSERT(0);
		return (E_FAIL);
	}

	if (WaitForSingleObject(m_hPropSetMutex, INFINITE) == WAIT_OBJECT_0)
	{
		pItem = (CPropertySetItem *) m_listPropSetQueue.GetAt(pos);

		ASSERT((pItem != 0 &&
		       !IsBadReadPtr(pItem, sizeof(CPropertySetItem))));

		*pulTypeSupport = pItem->m_ulTypeSupport;
		hr = pItem->m_hrCall;

		m_listPropSetQueue.RemoveAt(pos);
		delete pItem;
	}

	VERIFY(ReleaseMutex(m_hPropSetMutex));
	VERIFY(ResetEvent(m_hPropSetCacheFlushed));

	return (hr);
}

/* =============================================================
// Get()
// (RE) rtl:0x100254b0; dbg:0x10062660
//
// Queue a property read and wait for the service thread's result.
// Original read-back uses the recorded byte count without bounding it to
// the caller's property buffer.
//
// Returns:
//   The queued call's result
//   E_FAIL                    if the flush or read-back mutex wait fails
// =============================================================*/

STDMETHODIMP
ResMan::Get(REFGUID rguidPropSet, ULONG ulId,
	    LPVOID pInstanceData, ULONG cbInstanceData,
	    LPVOID pPropertyData, ULONG cbPropertyData,
	    PULONG pulBytesReturned)
{
CPropertySetItem       *pProcessedItem;
CPropertySetItem       *pItem;
POSITION                pos;
HRESULT                 hr;

	hr      = E_FAIL;
	pos     = NULL;

	pItem = new CPropertySetItem;

	if (pItem != NULL)
	{
		if (WaitForSingleObject(m_hPropSetMutex, INFINITE) == WAIT_OBJECT_0)
		{
			pItem->Get(rguidPropSet, ulId, pInstanceData, cbInstanceData,
				   pPropertyData, cbPropertyData, pulBytesReturned);
			pos = m_listPropSetQueue.AddTail(pItem);
		}

		VERIFY(ReleaseMutex(m_hPropSetMutex));
	}

	if (A3D_WAIT_PROPSET_FLUSH(m_hPropSetCacheFlushed) != WAIT_OBJECT_0)
	{
		DBGSTR("ResMan::Get() - TimerCallback failed to signal Property Set event.\n");
		ASSERT(0);
		return (E_FAIL);
	}

	if (WaitForSingleObject(m_hPropSetMutex, INFINITE) == WAIT_OBJECT_0)
	{
		pProcessedItem = (CPropertySetItem *) m_listPropSetQueue.GetAt(pos);

		ASSERT((pProcessedItem != 0 &&
		       !IsBadReadPtr(pProcessedItem, sizeof(CPropertySetItem))));

		memcpy(pInstanceData, pProcessedItem->m_pInstanceData, cbInstanceData);
		memcpy(pPropertyData, pProcessedItem->m_pPropertyData,
		       pProcessedItem->m_cbBytesReturned);
		*pulBytesReturned = pProcessedItem->m_cbBytesReturned;
		hr = pProcessedItem->m_hrCall;

		m_listPropSetQueue.RemoveAt(pos);
		delete pProcessedItem;
	}

	VERIFY(ReleaseMutex(m_hPropSetMutex));
	VERIFY(ResetEvent(m_hPropSetCacheFlushed));

	return (hr);
}

/* =============================================================
// Set()
// (RE) rtl:0x10025750; dbg:0x10062a40
//
// Queue a property write, optionally waiting for results and copying data back.
// Preserve the original SetPropertySet and ResManBuffer diagnostic labels.
//
// Returns: E_INVALIDARG for GUID_NULL; E_POINTER for null data with nonzero
//          size; A3DERROR_INITIAL_PARAMETERS_NOT_SET before initial state is
//          set. S_OK without WAITFORRESULTS, even if queuing fails; otherwise
//          the queued call's result, or E_FAIL if the flush or read-back mutex
//          wait fails.
// =============================================================*/

STDMETHODIMP
ResMan::Set(REFGUID rguidPropSet, ULONG ulId,
	    LPVOID pInstanceData, ULONG cbInstanceData,
	    LPVOID pPropertyData, ULONG cbPropertyData,
	    DWORD dwFlags)
{
CPropertySetItem       *pProcessedItem;
CPropertySetItem       *pItem;
POSITION                pos;
DWORD                   bWaitForResults;
HRESULT                 hr;

	if (IsEqualGUID(rguidPropSet, GUID_NULL))
	{
		DBGSTR("ResMan::SetPropertySet() - NULL GUID passed in.\n");
		return (E_INVALIDARG);
	}

	if (cbInstanceData != 0 && pInstanceData == NULL)
	{
		DBGSTR("ResMan::SetPropertySet() - Instance Data is NULL and size is not zero.\n");
		return (E_POINTER);
	}

	if (cbPropertyData != 0 && pPropertyData == NULL)
	{
		DBGSTR("ResManBuffer::SetPropertySet() - Property Data is NULL and size is not zero.\n");
		return (E_POINTER);
	}

	if (m_dwInitialStateSet == 0)
	{
		DBGSTR("ResMan::SetPropertySet() - You need to set the default state of the buffer first.\n");
		return (A3DERROR_INITIAL_PARAMETERS_NOT_SET);
	}

	hr              = E_FAIL;
	pos             = NULL;
	bWaitForResults = dwFlags & A3DPROPSET_WAITFORRESULTS;

	pItem = new CPropertySetItem;

	if (pItem != NULL)
	{
		if (WaitForSingleObject(m_hPropSetMutex, INFINITE) == WAIT_OBJECT_0)
		{
			pItem->AddSet(rguidPropSet, ulId, pInstanceData, cbInstanceData,
				      pPropertyData, cbPropertyData, bWaitForResults, 0);
			pos = m_listPropSetQueue.AddTail(pItem);
		}

		VERIFY(ReleaseMutex(m_hPropSetMutex));
	}

	if (!bWaitForResults)
		return (S_OK);

	if (A3D_WAIT_PROPSET_FLUSH(m_hPropSetCacheFlushed) != WAIT_OBJECT_0)
	{
		DBGSTR("ResMan::Set() - TimerCallback failed to signal Property Set event.\n");
		ASSERT(0);
		return (E_FAIL);
	}

	if (WaitForSingleObject(m_hPropSetMutex, INFINITE) == WAIT_OBJECT_0)
	{
		pProcessedItem = (CPropertySetItem *) m_listPropSetQueue.GetAt(pos);

		ASSERT((pProcessedItem != 0 &&
		       !IsBadReadPtr(pProcessedItem, sizeof(CPropertySetItem))));

		memcpy(pInstanceData, pProcessedItem->m_pInstanceData, cbInstanceData);
		memcpy(pPropertyData, pProcessedItem->m_pPropertyData, cbPropertyData);
		hr = pProcessedItem->m_hrCall;

		m_listPropSetQueue.RemoveAt(pos);
		delete pProcessedItem;
	}

	VERIFY(ReleaseMutex(m_hPropSetMutex));
	VERIFY(ResetEvent(m_hPropSetCacheFlushed));

	return (hr);
}

/* =============================================================
// AddInitialStateParameters()
// (RE) rtl:0x10025a90; dbg:0x10062fe0
//
// Queue default property state and enable subsequent Set calls.
//
// Returns: S_OK, even if the mutex wait fails; E_INVALIDARG for GUID_NULL;
//          E_POINTER for null data with nonzero size; E_OUTOFMEMORY on
//          allocation failure.
// =============================================================*/

STDMETHODIMP
ResMan::AddInitialStateParameters(REFGUID rguidPropSet, ULONG ulId,
				  LPVOID pInstanceData, ULONG cbInstanceData,
				  LPVOID pPropertyData, ULONG cbPropertyData)
{
CPropertySetItem	*pItem;

	if (IsEqualGUID(rguidPropSet, GUID_NULL))
	{
		DBGSTR("ResMan::AddInitialStateParameters() - NULL GUID passed in.\n");
		return (E_INVALIDARG);
	}

	if (cbInstanceData != 0 && pInstanceData == NULL)
	{
		DBGSTR("ResMan::AddInitialStateParameters() - Instance Data is NULL and size is not zero.\n");
		return (E_POINTER);
	}

	if (cbPropertyData != 0 && pPropertyData == NULL)
	{
		DBGSTR("ResMan::AddInitialStateParameters() - Property Data is NULL and size is not zero.\n");
		return (E_POINTER);
	}

	pItem = new CPropertySetItem;

	if (pItem == NULL)
	{
		DBGSTR("ResMan::AddInitialStateParameters() - Failed to allocate memory for item.\n");
		return (E_OUTOFMEMORY);
	}

	if (WaitForSingleObject(m_hPropSetMutex, INFINITE) == WAIT_OBJECT_0)
	{
		pItem->SaveBufferState(rguidPropSet, ulId, pInstanceData, cbInstanceData,
				       pPropertyData, cbPropertyData);
		m_listResManPropSetItems.AddTail(pItem);
	}

	VERIFY(ReleaseMutex(m_hPropSetMutex));

	m_dwInitialStateSet = 1;

	return (S_OK);
}

/* =============================================================
// MakePrimaryBuffer()
// (RE) rtl:0x10024BD0; dbg:0x10060C40; thunk dbg:0x100013A7
//
// Create a primary buffer through the main DAL or fallback DirectSound
// interface, then request stereo 22050 Hz, 16-bit PCM.
//
// Returns: S_OK, ignoring SetFormat failure; the fallback CreateSoundBuffer
//          failure if neither device creates the buffer.
// =============================================================*/

HRESULT
ResMan::MakePrimaryBuffer(DSBUFFERDESC1 *lpDSBdesc,
			  LPDIRECTSOUNDBUFFER *lplpDSB,
			  LPUNKNOWN pUnkOuter)
{
IDirectSound   *pInner;
WAVEFORMATEX    wfx;
HRESULT         hr;

	ASSERT((lpDSBdesc != 0 &&
	       !IsBadReadPtr(lpDSBdesc, sizeof(DSBUFFERDESC1))));
	ASSERT((lplpDSB != 0 &&
	       !IsBadReadPtr(lplpDSB, sizeof(LPDIRECTSOUNDBUFFER))));

	*lplpDSB = NULL;

	ASSERT((m_pMainDALInfo != 0 &&
	       !IsBadReadPtr(m_pMainDALInfo, sizeof(DalInfo))));

	pInner = m_pMainDALInfo->GetIDirectSound();

	hr = pInner->CreateSoundBuffer((LPCDSBUFFERDESC) lpDSBdesc,
				       lplpDSB, pUnkOuter);

	if (FAILED(hr))
	{
		hr = m_pDirectSound->CreateSoundBuffer((LPCDSBUFFERDESC) lpDSBdesc,
						       lplpDSB, pUnkOuter);

		if (FAILED(hr))
		{
			DBGSTR("ResMan::CreatePrimaryBuffer - Failed to create primary buffer.\n");

			return (hr);
		}
	}

	ZeroMemory(&wfx, sizeof(wfx));

	wfx.wFormatTag      = WAVE_FORMAT_PCM;
	wfx.nChannels       = 2;
	wfx.nSamplesPerSec  = 22050;
	wfx.nAvgBytesPerSec = 88200;
	wfx.nBlockAlign     = 4;
	wfx.wBitsPerSample  = 16;

	hr = (*lplpDSB)->SetFormat(&wfx);

	if (FAILED(hr))
		DBGSTR("ResMan::CreatePrimaryBuffer - Failed to SetFormat of the primary buffer. Not failing call.\n");

	return (S_OK);
}

/* =============================================================
// MakeStaticDalBuffer()
// (RE) dbg:0x10060B40; thunk dbg:0x10001893; inline rtl:0x10024DBA
//
// Create a static buffer on a DAL matching the requested mode.
//
// Returns:
//   S_OK    on the first successful creation
//   E_FAIL  if no matching DAL succeeds
// =============================================================*/

HRESULT
ResMan::MakeStaticDalBuffer(int nMode, DSBUFFERDESC1 *pDesc,
			   DalBufferInfo **ppDalBufferInfo)
{
POSITION        pos;
DalInfo        *pDalInfo;
HRESULT         hr;

	pos = m_listDalInfo.GetHeadPosition();

	while (pos)
	{
		pDalInfo = (DalInfo *) m_listDalInfo.GetNext(pos);

		ASSERT((pDalInfo != 0 &&
		       !IsBadReadPtr(pDalInfo, sizeof(DalInfo))));

		if (nMode == (int) pDalInfo->GetMode())
		{
			hr = pDalInfo->CreateStaticDalBuffer(pDesc,
							     ppDalBufferInfo);

			if (SUCCEEDED(hr))
				return (S_OK);
		}
	}

	return (E_FAIL);
}

/* =============================================================
// CreateSoundBuffer()
// (RE) rtl:0x10024c70; dbg:0x10060ef0; thunk dbg:0x10002ad6
//
// Create a static or streaming secondary buffer for the resource-manager mode.
// The original returns success with no buffer when static creation receives
// both description flags 0x10 and 0x08.
//
// Returns: S_OK; E_OUTOFMEMORY on resource-buffer allocation failure; the Init,
//          QueryInterface or DAL-buffer creation failure.
// =============================================================*/

STDMETHODIMP
ResMan::CreateSoundBuffer(const DSBUFFERDESC1 *lpDSBdesc,
			  LPDIRECTSOUNDBUFFER *lplpDSB,
			  LPUNKNOWN pUnkOuter)
{
ResManStatBuffer       *pStatBuffer;
ResManStreamBuffer     *pStreamBuffer;
IDirectSoundBuffer     *pDSBuffer;
DalBufferInfo          *pDalBufferInfo;
DWORD                   dwRenderMode;
HRESULT                 hr;

	ASSERT((lpDSBdesc != 0 &&
	       !IsBadReadPtr(lpDSBdesc, sizeof(DSBUFFERDESC1))));
	ASSERT((lplpDSB != 0 &&
	       !IsBadReadPtr(lplpDSB, sizeof(LPDIRECTSOUNDBUFFER))));

	*lplpDSB = NULL;

	/* Original write through the caller's const description. */
	((DSBUFFERDESC1 *) lpDSBdesc)->dwFlags |= DSBCAPS_GLOBALFOCUS;

	if (m_dwResourceManagerMode == A3D_RESOURCE_MODE_OFF)
	{
		pDalBufferInfo = NULL;
		pDSBuffer      = NULL;

		m_dwLastRun = 0;
		VERIFY(SetEvent(m_hEvent));
		Sleep(A3D_RESMAN_STATIC_BUFFER_WAKE_DELAY_MS);

		pStatBuffer = new ResManStatBuffer(this, m_dwResourceManagerMode);

		if (!pStatBuffer)
		{
			DBGSTR("ResMan::CreateSecondaryBuffer() - Could not create 3D buffer\n");

			return (E_OUTOFMEMORY);
		}

		hr = (static_cast<IResManBufferPrimary *>(pStatBuffer))->
			Init(lpDSBdesc);

		if (FAILED(hr))
		{
			DBGSTR("ResMan::CreateSecondaryBuffer() - Could not init 3D buffer\n");

			delete pStatBuffer;

			return (hr);
		}

		hr = (static_cast<IResManBufferPrimary *>(pStatBuffer))->
			QueryInterface(IID_IDirectSoundBuffer,
				       (void **) &pDSBuffer);

		if (FAILED(hr))
		{
			DBGSTR("ResMan::CreateSecondaryBuffer() - Failed when querying for IDirectSoundBuffer interface.\n");

			delete pStatBuffer;

			return (hr);
		}

		if (lpDSBdesc->dwFlags & DSBCAPS_CTRL3D)
		{
			if (pDalBufferInfo == NULL &&
			    (lpDSBdesc->dwFlags & DSBCAPS_LOCSOFTWARE) == 0)
				hr = m_pMainDALInfo->CreateStaticDalBuffer(
						(DSBUFFERDESC1 *) lpDSBdesc,
						&pDalBufferInfo);
		}
		else
		{
			(static_cast<IResManBuffer *>(pStatBuffer))->
				GetRenderMode(&dwRenderMode);

			hr = MakeStaticDalBuffer(dwRenderMode,
						(DSBUFFERDESC1 *) lpDSBdesc,
						&pDalBufferInfo);
		}

		if (pDalBufferInfo == NULL)
		{
			DBGSTR("ResMan::CreateSecondaryBuffer() - Could not create DalBufferInfo object\n");

			if (pDSBuffer != NULL)
			{
				pDSBuffer->Release();
				pDSBuffer = NULL;
			}

			delete pStatBuffer;

			return (hr);
		}

		pDalBufferInfo->SetOwner((DWORD) pStatBuffer);

		(static_cast<IResManBufferPrimary *>(pStatBuffer))->
			AttachDalBufferInfo(pDalBufferInfo);

		m_ResManStatBufferList.AddTail(pStatBuffer);

		(static_cast<IResManBufferPrimary *>(pStatBuffer))->Release();

		*lplpDSB = pDSBuffer;

		return (S_OK);
	}

	pStreamBuffer = new ResManStreamBuffer(this, m_dwResourceManagerMode);

	if (!pStreamBuffer)
	{
		DBGSTR("ResMan::CreateSecondaryBuffer() - Could not create 3D buffer\n");

		return (E_OUTOFMEMORY);
	}

	hr = (static_cast<IResManBufferPrimary *>(pStreamBuffer))->
		Init(lpDSBdesc);

	if (FAILED(hr))
	{
		DBGSTR("ResMan::CreateSecondaryBuffer() - Could not init 3D buffer\n");

		delete pStreamBuffer;

		return (hr);
	}

	hr = (static_cast<IResManBufferPrimary *>(pStreamBuffer))->
		QueryInterface(IID_IDirectSoundBuffer,
			       (void **) lplpDSB);

	if (SUCCEEDED(hr))
	{
		m_ResManStreamBufferList.AddTail(pStreamBuffer);

		(static_cast<IResManBufferPrimary *>(pStreamBuffer))->Release();

		return (S_OK);
	}

	DBGSTR("ResMan::CreateSecondaryBuffer() - Failed when querying for IDirectSoundBuffer interface.\n");

	delete pStreamBuffer;

	return (hr);
}

/* =============================================================
// CountEntriesW95()
// (RE) rtl:0x10025080; dbg:0x10061900
//
// Count DAL buffers with voice-count-mode bit 0 and mode flag bit 1 set.
//
// Returns: The total number of matching DAL buffers.
// =============================================================*/

DWORD
ResMan::CountEntriesW95(void)
{
POSITION        pos;
DalInfo        *pDalInfo;
DWORD           cEntries;

	cEntries = 0;

	pos = m_listDalInfo.GetHeadPosition();

	while (pos)
	{
		pDalInfo = (DalInfo *) m_listDalInfo.GetNext(pos);

		ASSERT((pDalInfo != 0 && !IsBadReadPtr(pDalInfo, sizeof(DalInfo))));

		if (pDalInfo->Has3DVoiceCountMode() &&
		    pDalInfo->ReportsHardwareStatus())
		{
			cEntries += pDalInfo->GetNumDalBuffers();
		}
	}

	return (cEntries);
}

/* =============================================================
// CountPlayingOnW95()
// (RE) rtl:0x100250b0; dbg:0x10061a20
//
// Count playing stream buffers whose DAL binding has voice-count-mode
// bit 0 and mode flag bit 1 set.
//
// Returns: The number of matching playing buffers.
// =============================================================*/

DWORD
ResMan::CountPlayingOnW95(void)
{
POSITION        pos;
ResManBuffer   *pResManBuffer;
DalBufferInfo  *pDalBufferInfo;
DWORD           dwState;
DWORD           cPlaying;

	cPlaying       = 0;
	pDalBufferInfo = NULL;

	pos = m_ResManStreamBufferList.GetHeadPosition();

	while (pos)
	{
		pResManBuffer = (ResManBuffer *) m_ResManStreamBufferList.GetNext(pos);

		ASSERT((pResManBuffer != 0 &&
		       !IsBadReadPtr(pResManBuffer, sizeof(ResManBuffer))));

		(static_cast<IResManBuffer *>(pResManBuffer))
			->GetBufferState(&dwState);

		(static_cast<IResManBufferPrimary *>(pResManBuffer))
			->GetDalBufferInfo(&pDalBufferInfo);

		if (pDalBufferInfo &&
		    pDalBufferInfo->Has3DVoiceCountMode() &&
		    pDalBufferInfo->ReportsHardwareStatus() &&
		    dwState == A3DVOICE_STATE_PLAYING)
		{
			cPlaying++;
		}
	}

	return (cPlaying);
}

/* =============================================================
// FreeVoices()
// (RE) rtl:0x10025120; dbg:0x10061b80
//
// Sum available hardware 3D voices on DALs with voice-count-mode bit 0
// and mode flag bit 1 set.
//
// Returns: The available voice count.
// =============================================================*/

DWORD
ResMan::FreeVoices(void)
{
POSITION        pos;
DalInfo        *pDalInfo;
DWORD           cFree;

	cFree = 0;

	pos = m_listDalInfo.GetHeadPosition();

	while (pos)
	{
		pDalInfo = (DalInfo *) m_listDalInfo.GetNext(pos);

		ASSERT((pDalInfo != 0 && !IsBadReadPtr(pDalInfo, sizeof(DalInfo))));

		if (pDalInfo->Has3DVoiceCountMode() &&
		    pDalInfo->ReportsHardwareStatus())
		{
			cFree += pDalInfo->GetNumAvailable();
		}
	}

	return (cFree);
}

/* =============================================================
// LightPass()
// (RE) dbg:0x1005b950; thunk dbg:0x10001db1
//
// Perform no work; the reference light-pass body is empty.
// =============================================================*/

void
ResMan::LightPass(void)
{
}

/* =============================================================
// FreeReleasedStreams()
// (RE) rtl:0x10026FE0; dbg:0x100662F0
//
// Delete released static, streaming and two-channel resource buffers,
// returning their DAL bindings.
//
// Returns: S_OK; the first FreeListedBuffers failure.
// =============================================================*/

HRESULT
ResMan::FreeReleasedStreams(int nWhen)
{
POSITION                pos;
POSITION                posThis;
ResManStatBuffer       *pResManStatBuffer;
DalBufferInfo          *pDalBufferInfo;
DWORD                   dwBufferState;
HRESULT                 hr;

	pos = m_ResManStatBufferList.GetHeadPosition();

	while (pos)
	{
		posThis                 = pos;
		pResManStatBuffer       = (ResManStatBuffer *) m_ResManStatBufferList.GetNext(pos);

		ASSERT((pResManStatBuffer != 0 &&
		       !IsBadReadPtr(pResManStatBuffer, sizeof(ResManStatBuffer))));

		(static_cast<IResManBuffer *>(pResManStatBuffer))->
			GetBufferState(&dwBufferState);

		if (dwBufferState == A3DVOICE_STATE_RELEASED)
		{
			pDalBufferInfo = NULL;

			hr = (static_cast<IResManBufferPrimary *>(pResManStatBuffer))->
				GetDalBufferInfo(&pDalBufferInfo);

			if (SUCCEEDED(hr) && pDalBufferInfo)
			{
				pDalBufferInfo->m_pDalInfo->RemoveBuffer(pDalBufferInfo);

				(static_cast<IResManBufferPrimary *>(pResManStatBuffer))->
					SetDalBufferInfoBare(NULL);
			}

			m_ResManStatBufferList.RemoveAt(posThis);

			if (pResManStatBuffer)
				delete pResManStatBuffer;
		}
	}

	hr = FreeListedBuffers(nWhen, &m_ResManStreamBufferList);

	if (FAILED(hr))
	{
		DBGSTR("ResMan::FreeReleasedStreams() - had an error "
				   "for m_ResManStreamBufferList\n");

		return (hr);
	}

	hr = FreeListedBuffers(nWhen, &m_ResMan2ChBufferList);

	if (FAILED(hr))
	{
		DBGSTR("ResMan::FreeReleasedStreams() - had an error "
				   "for m_ResMan2ChBufferList\n");

		return (hr);
	}

	return (S_OK);
}

/* =============================================================
// FreeListedBuffers()
// (RE) rtl:0x10026EB0; dbg:0x10066180
//
// Delete released buffers from a list after returning their DAL bindings.
//
// Returns: S_OK, ignoring DAL-return failures.
// =============================================================*/

HRESULT
ResMan::FreeListedBuffers(int nWhen, CList *pList)
{
POSITION                pos;
POSITION                posThis;
ResManStreamBuffer     *pResManStreamBuffer;
DalBufferInfo          *pDalBufferInfo;
DWORD                   dwBufferState;

	pDalBufferInfo = NULL;

	pos = pList->GetHeadPosition();

	while (pos)
	{
		posThis                 = pos;
		pResManStreamBuffer     = (ResManStreamBuffer *) pList->GetNext(pos);

		ASSERT((pResManStreamBuffer != 0 &&
		       !IsBadReadPtr(pResManStreamBuffer, sizeof(ResManStreamBuffer))));

		(static_cast<IResManBuffer *>(pResManStreamBuffer))->
			GetBufferState(&dwBufferState);

		if (dwBufferState == A3DVOICE_STATE_RELEASED)
		{
			(static_cast<IResManBufferPrimary *>(pResManStreamBuffer))->
				GetDalBufferInfo(&pDalBufferInfo);

			if (pDalBufferInfo)
				ReturnBufferToDal(nWhen, pResManStreamBuffer, pDalBufferInfo);

			pList->RemoveAt(posThis);

			if (pResManStreamBuffer)
				delete pResManStreamBuffer;
		}
	}

	return (S_OK);
}

/* =============================================================
// ReturnBufferToDal()
// (RE) rtl:0x100269E0; dbg:0x10065440
//
// Preserve the resource buffer's cursor, stop and rewind its DAL buffer,
// and clear the binding with an idle timestamp.
//
// Returns: S_OK, ignoring buffer-operation failures.
// =============================================================*/

HRESULT
ResMan::ReturnBufferToDal(int nWhen, ResManBuffer *pResManBuffer,
			   DalBufferInfo *pDalBufferInfo)
{
IResManBufferPrimary   *pPrimary;
DWORD                   dwPlayCursor;
DWORD                   dwWriteCursor;

	ASSERT((pResManBuffer != 0 &&
	       !IsBadReadPtr(pResManBuffer, sizeof(ResManBuffer))));
	ASSERT((pDalBufferInfo != 0 &&
	       !IsBadReadPtr(pDalBufferInfo, sizeof(DalBufferInfo))));
	ASSERT((pDalBufferInfo->GetDSBuffer() != 0 &&
	       !IsBadReadPtr(pDalBufferInfo->GetDSBuffer(),
			     sizeof(IDirectSoundBuffer))));

	pPrimary = static_cast<IResManBufferPrimary *>(pResManBuffer);

	pPrimary->GetCurrentPosition(&dwPlayCursor, &dwWriteCursor);
	pPrimary->SetCurrentPosition(dwPlayCursor);

	pDalBufferInfo->GetDSBuffer()->Stop();
	pDalBufferInfo->GetDSBuffer()->SetCurrentPosition(0);

	pDalBufferInfo->SetTick(nWhen);
	pDalBufferInfo->SetOwner(0);

	pPrimary->SetDalBufferInfoBare(NULL);

	return (S_OK);
}

/* =============================================================
// ReturnStoppedBuffersToDal()
// (RE) dbg:0x10066560; inline rtl:0x10028560, rtl:0x100285AB
//
// Return stopped buffers' DAL bindings from a resource-buffer list.
// Original leaves the binding local unset before the getter writes it.
//
// Returns: S_OK, ignoring DAL-return failures.
// =============================================================*/

HRESULT
ResMan::ReturnStoppedBuffersToDal(int nWhen, CList *pList)
{
POSITION        pos;
ResManBuffer   *pResManBuffer;
DalBufferInfo  *pDalBufferInfo;
DWORD           dwBufferState;

	pos = pList->GetHeadPosition();

	while (pos)
	{
		pResManBuffer = (ResManBuffer *) pList->GetNext(pos);

		ASSERT((pResManBuffer != 0 &&
		       !IsBadReadPtr(pResManBuffer, sizeof(ResManBuffer))));

		(static_cast<IResManBuffer *>(pResManBuffer))->
			GetBufferState(&dwBufferState);

		if (dwBufferState == A3DVOICE_STATE_STOPPED)
		{
			(static_cast<IResManBufferPrimary *>(pResManBuffer))->
				GetDalBufferInfo(&pDalBufferInfo);

			if (pDalBufferInfo)
				ReturnBufferToDal(nWhen, pResManBuffer, pDalBufferInfo);
		}
	}

	return (S_OK);
}

/* =============================================================
// HaltStoppedStreams()
// (RE) dbg:0x10066670; inline in ServiceThread rtl:0x100284F0
//
// Return stopped streaming and two-channel buffers' DAL bindings.
//
// Returns: S_OK; the first ReturnStoppedBuffersToDal failure.
// =============================================================*/

HRESULT
ResMan::HaltStoppedStreams(int nWhen)
{
HRESULT	hr;

	hr = ReturnStoppedBuffersToDal(nWhen, &m_ResManStreamBufferList);

	if (FAILED(hr))
	{
		DBGSTR("ResMan::HaltStoppedStreams() - had an error "
				   "for m_ResManStreamBufferList\n");

		return (hr);
	}

	hr = ReturnStoppedBuffersToDal(nWhen, &m_ResMan2ChBufferList);

	if (FAILED(hr))
	{
		DBGSTR("ResMan::HaltStoppedStreams() - had an error "
				   "for m_ResMan2ChBufferList\n");

		return (hr);
	}

	return (S_OK);
}

static int __cdecl ComparePriority(const void *pElement1, const void *pElement2);
static int __cdecl CompareReflectionSortKey(const void *pRef1, const void *pRef2);

/* =============================================================
// SortAndPickReflections()
// (RE) rtl:0x10027100; dbg:0x10066750
//
// Prioritize playing stream buffers, select audible reflections within
// the hardware limit, and rebuild the two-channel commit list.
//
// Returns: S_OK, ignoring buffer-operation failures.
// =============================================================*/

HRESULT
ResMan::SortAndPickReflections(int nWhen)
{
POSITION                pos;
ResManStreamBuffer     *pResManStreamBuffer;
ResManStatBuffer       *pResManStatBuffer;
DSBCAPS                 dsbCaps;
DWORD                   dwState;
int                     nHardwareSources;
int                     nReflectionsToEnable;
int                     nToSearch;
int                     nIndex;
int                     j;

	m_nPriorityBufferElements = 0;

	pos = m_ResManStreamBufferList.GetHeadPosition();

	while (pos)
	{
		pResManStreamBuffer = (ResManStreamBuffer *) m_ResManStreamBufferList.GetNext(pos);

		ASSERT((pResManStreamBuffer != 0 &&
		       !IsBadReadPtr(pResManStreamBuffer, sizeof(ResManStreamBuffer))));

		VERIFY(SUCCEEDED(pResManStreamBuffer->GetBufferState(&dwState)));

		if (dwState == A3DVOICE_STATE_PLAYING)
			AddBufferToPriorityArray(pResManStreamBuffer);
	}

	nHardwareSources = m_pMainDALInfo->GetNumDalBuffers();

	if (m_nPriorityBufferElements > nHardwareSources)
	{
		ASSERT(m_pPriorityBufferArray != 0);

		qsort(m_pPriorityBufferArray, m_nPriorityBufferElements,
		      sizeof(ResManBuffer *), ComparePriority);
	}

	if (m_dwReflectionsEnabled)
	{
		nReflectionsToEnable = 0;

		ClearReflectionAvailability(nHardwareSources);

		dsbCaps.dwSize = sizeof(DSBCAPS);

		pos = m_ResManStatBufferList.GetHeadPosition();

		while (pos)
		{
			pResManStatBuffer = (ResManStatBuffer *) m_ResManStatBufferList.GetNext(pos);

			ASSERT((pResManStatBuffer != 0 &&
			       !IsBadReadPtr(pResManStatBuffer, sizeof(ResManStatBuffer))));

			VERIFY(SUCCEEDED(pResManStatBuffer->GetCaps(&dsbCaps)));

			if (dsbCaps.dwFlags & DSBCAPS_LOCHARDWARE)
			{
				VERIFY(SUCCEEDED(pResManStatBuffer->GetBufferState(&dwState)));

				if (dwState == A3DVOICE_STATE_PLAYING)
					SearchForReflectionsToEnable(pResManStatBuffer,
								     &nReflectionsToEnable);
			}
		}

		if (nHardwareSources >= m_nPriorityBufferElements)
			nToSearch = m_nPriorityBufferElements;
		else
			nToSearch = nHardwareSources;

		for (nIndex = 0; nIndex < nToSearch; nIndex++)
		{
ResManBuffer	*pResManStreamBuffer;

			ASSERT(nIndex < m_nPriorityBufferElements);

			pResManStreamBuffer = m_pPriorityBufferArray[nIndex];

			ASSERT((pResManStreamBuffer != 0 &&
			       !IsBadReadPtr(pResManStreamBuffer, sizeof(ResManBuffer))));

			SearchForReflectionsToEnable(pResManStreamBuffer, &nReflectionsToEnable);
		}

		if (nReflectionsToEnable != 0)
			ASSERT(m_pReflectionSort != 0);

		m_nReflectionSortWanted = nReflectionsToEnable;

		if (nReflectionsToEnable > m_nReflectionsSupported)
		{
			qsort(m_pReflectionSort, nReflectionsToEnable,
			      sizeof(REFLECTIONSORT), CompareReflectionSortKey);

			nReflectionsToEnable = m_nReflectionsSupported;
		}

		ASSERT(nReflectionsToEnable <= m_nReflectionsSupported);

		for (j = 0; j < nReflectionsToEnable; j++)
			*m_pReflectionSort[j].pbAvailable = 1;
	}

	RebuildTwoChannelCommitList(nWhen, &m_ResMan2ChBufferList, &m_ResMan2ChPlayingList);

	return (S_OK);
}

/* =============================================================
// RebuildTwoChannelCommitList()
// (RE) dbg:0x10066D80; thunk dbg:0x100026E9; inline rtl:0x1002724A
//
// Replace the destination list with playing buffers from the source list.
//
// Returns: S_OK, ignoring state-query failures.
// =============================================================*/

HRESULT
ResMan::RebuildTwoChannelCommitList(int nWhen, CList *pSrcList, CList *pDstList)
{
POSITION        pos;
ResManBuffer   *pResManBuffer;
DWORD           dwState;

	pDstList->RemoveAll();

	pos = pSrcList->GetHeadPosition();

	while (pos)
	{
		pResManBuffer = (ResManBuffer *) pSrcList->GetNext(pos);

		ASSERT((pResManBuffer != 0 &&
		       !IsBadReadPtr(pResManBuffer, sizeof(ResManBuffer))));

		(static_cast<IResManBuffer *>(pResManBuffer))
			->GetBufferState(&dwState);

		if (dwState == A3DVOICE_STATE_PLAYING)
			pDstList->AddTail(pResManBuffer);
	}

	return (S_OK);
}

/* =============================================================
// ComparePriority()
// (RE) rtl:0x10027360; dbg:0x10066E70; thunk dbg:0x10001DD9
//
// Compare stream buffers by descending sort key, then descending repeat count.
//
// Returns: 1 if the first sorts later, -1 if earlier, or 0 for equal keys and
//          counts.
// =============================================================*/

static int __cdecl
ComparePriority(const void *pElement1, const void *pElement2)
{
ResManStreamBuffer     *pRMB1;
ResManStreamBuffer     *pRMB2;
FLOAT                   fKey1;
FLOAT                   fKey2;
DWORD                   dwTie1;
DWORD                   dwTie2;

	pRMB1 = *(ResManStreamBuffer **) pElement1;
	pRMB2 = *(ResManStreamBuffer **) pElement2;

	ASSERT((pRMB1 != 0 && !IsBadReadPtr(pRMB1, sizeof(ResManStreamBuffer))));
	ASSERT((pRMB2 != 0 && !IsBadReadPtr(pRMB2, sizeof(ResManStreamBuffer))));

	fKey1 = pRMB1->GetSortKey();
	fKey2 = pRMB2->GetSortKey();

	if (fKey1 < fKey2)
		return (1);

	if (fKey1 > fKey2)
		return (-1);

	dwTie1 = pRMB1->GetRepeatCount();
	dwTie2 = pRMB2->GetRepeatCount();

	if (dwTie1 < dwTie2)
		return (1);

	if (dwTie1 <= dwTie2)
		return (0);

	return (-1);
}

/* =============================================================
// CompareReflectionSortKey()
// (RE) rtl:0x100273C0; dbg:0x10067040; thunk dbg:0x1000135C
//
// Compare reflection entries by descending audibility.
//
// Returns: 1 if the first key is lower, 0 if equal, or -1 otherwise.
// =============================================================*/

static int __cdecl
CompareReflectionSortKey(const void *pRef1, const void *pRef2)
{
const REFLECTIONSORT	*p1;
const REFLECTIONSORT	*p2;

	p1 = (const REFLECTIONSORT *) pRef1;
	p2 = (const REFLECTIONSORT *) pRef2;

	ASSERT((pRef1 != 0 && !IsBadReadPtr(pRef1, sizeof(REFLECTIONSORT))));
	ASSERT((pRef2 != 0 && !IsBadReadPtr(pRef2, sizeof(REFLECTIONSORT))));

	if (p1->fKey < p2->fKey)
		return (1);

	if (p1->fKey <= p2->fKey)
		return (0);

	return (-1);
}

/* =============================================================
// AddBufferToPriorityArray()
// (RE) rtl:0x10027400; dbg:0x10067150; thunk dbg:0x10002CA7
//
// Recompute a stream buffer's sort key and append it to the priority array.
// Preserve the original inverted allocation check and copy from the reflection
// array: growth can leak memory, overrun the old array or write through null.
// The original returns an unused pointer; this implementation returns void.
// =============================================================*/

void
ResMan::AddBufferToPriorityArray(ResManStreamBuffer *pBuffer)
{
ResManBuffer  **pNew;
CHAR            szRealloc[60];
CHAR            szFailed[64];

	++m_nPriorityBufferElements;

	if (m_nPriorityBufferElements > m_nPriorityBufferArrayLength)
	{
		DBGSTR("ResMan::AddBufferToPriorityArray() - Had to realloc array.\n");

		m_nPriorityBufferArrayLength += A3D_RESMAN_PRIORITY_GROW_ENTRIES;

		pNew = (ResManBuffer **) operator new(4 * m_nPriorityBufferArrayLength);

		if (pNew == NULL)
		{
			if (m_pPriorityBufferArray != NULL)
			{
				memset(pNew, 0, 4 * m_nPriorityBufferArrayLength);

				memcpy(pNew, m_pReflectionSort, sizeof(*pNew) * (m_nPriorityBufferArrayLength - A3D_RESMAN_PRIORITY_GROW_ENTRIES));

				operator delete((void *) m_pPriorityBufferArray);

				m_pPriorityBufferArray = pNew;
			}
			else
			{
				DBGSTR("ResMan::AddBufferToPriorityArray() - Failed to realloc memory.\n");

				m_nPriorityBufferArrayLength    = 0;
				m_nPriorityBufferElements       = 0;
			}
		}
	}

	if (m_pPriorityBufferArray != NULL)
	{
		pBuffer->RecomputeSortKey();

		m_pPriorityBufferArray[m_nPriorityBufferElements - 1] =
			(ResManBuffer *) pBuffer;
	}
}

/* =============================================================
// ClearReflectionAvailability()
// (RE) rtl:0x100274E0; dbg:0x10067350; thunk dbg:0x10002F95
//
// Clear reflection availability for static buffers on the main DAL and
// stream buffers within the hardware source limit.
//
// Returns: The number of priority-array entries visited.
// =============================================================*/

int
ResMan::ClearReflectionAvailability(int nHardwareSources)
{
POSITION                pos;
ResManStreamBuffer     *pResManBuffer;
IResManBufferPrimary   *pPrimary;
DalBufferInfo          *pDalBufferInfo;
A3DCTRL_SRC_SUPER      *pA3dSuperCtrl;
HRESULT                 hr;
int                     i;
int                     nCount;
int                     j;

	pos = m_ResManStatBufferList.GetHeadPosition();

	while (pos)
	{
	ResManStatBuffer	*pResManBuffer;

		pResManBuffer =
			(ResManStatBuffer *) m_ResManStatBufferList.GetNext(pos);

		ASSERT((pResManBuffer != 0 &&
		       !IsBadReadPtr(pResManBuffer, sizeof(ResManStatBuffer))));

		pPrimary = static_cast<IResManBufferPrimary *>(pResManBuffer);

		hr = pPrimary->GetDalBufferInfo(&pDalBufferInfo);

		if (SUCCEEDED(hr) && pDalBufferInfo &&
		    pDalBufferInfo->GetDalInfo() == m_pMainDALInfo)
		{
			pA3dSuperCtrl = NULL;

			pPrimary->GetA3dCtrlSuper((LPVOID *) &pA3dSuperCtrl);

			if (pA3dSuperCtrl)
			{
				ASSERT((pA3dSuperCtrl != 0 &&
				       !IsBadReadPtr(pA3dSuperCtrl, sizeof(A3DCTRL_SRC_SUPER))));

				for (i = 0; i < A3D_MAX_SOURCE_REFLECTIONS; i++)
					pA3dSuperCtrl->Reflections[i].bAvailable = 0;

				(static_cast<IA3dDalBuffer *>(pResManBuffer))
					->SetA3dSuperCtrl(pA3dSuperCtrl,
							  sizeof(A3DCTRL_SRC_SUPER));
			}
		}
	}

	for (j = 0; ; j++)
	{
		nCount = m_nPriorityBufferElements;

		if (nCount >= nHardwareSources)
			nCount = nHardwareSources;

		if (j >= nCount)
			break;

		pResManBuffer = (ResManStreamBuffer *) m_pPriorityBufferArray[j];

		ASSERT((pResManBuffer != 0 &&
		       !IsBadReadPtr(pResManBuffer, sizeof(ResManStreamBuffer))));

		pPrimary = static_cast<IResManBufferPrimary *>(pResManBuffer);

		pA3dSuperCtrl = NULL;

		pPrimary->GetA3dCtrlSuper((LPVOID *) &pA3dSuperCtrl);

		if (pA3dSuperCtrl)
		{
			ASSERT((pA3dSuperCtrl != 0 &&
			       !IsBadReadPtr(pA3dSuperCtrl, sizeof(A3DCTRL_SRC_SUPER))));

			for (i = 0; i < A3D_MAX_SOURCE_REFLECTIONS; i++)
				pA3dSuperCtrl->Reflections[i].bAvailable = 0;

			(static_cast<IA3dDalBuffer *>(pResManBuffer))
				->SetA3dSuperCtrl(pA3dSuperCtrl,
						  sizeof(A3DCTRL_SRC_SUPER));
		}
	}

	return (nCount);
}

/* =============================================================
// SearchForReflectionsToEnable()
// (RE) rtl:0x100275D0; dbg:0x100676D0; thunk dbg:0x10001807
//
// Append a buffer's enabled reflections to the audibility sort array.
// Original growth failure can leave the following write beyond the old array.
// The reference returns an unused buffer pointer when control data is present;
// this implementation returns S_OK on that path.
//
// Returns: GetA3dCtrlSuper's result if control data is absent; S_OK otherwise.
// =============================================================*/

HRESULT
ResMan::SearchForReflectionsToEnable(ResManBuffer *pResManBuffer,
				     int *pReflectionCount)
{
IResManBufferPrimary   *pPrimary;
A3DCTRL_SRC_SUPER      *pSuper;
REFLECTIONSORT         *pNew;
HRESULT                 hr;
int                     i;

	ASSERT((pResManBuffer != 0 &&
	       !IsBadReadPtr(pResManBuffer, sizeof(ResManBuffer))));
	ASSERT((pReflectionCount != 0 &&
	       !IsBadReadPtr(pReflectionCount, sizeof(int))));

	pPrimary = static_cast<IResManBufferPrimary *>(pResManBuffer);

	pSuper = NULL;

	hr = pPrimary->GetA3dCtrlSuper((LPVOID *) &pSuper);

	if (!pSuper)
		return (hr);

	for (i = 0; i < A3D_MAX_SOURCE_REFLECTIONS; i++)
	{
		if (pSuper->Reflections[i].bEnable == 1)
		{
			if (++*pReflectionCount > m_nReflectionSortAllocated)
			{
				DBGSTR("ResMan::SearchForReflectionsToEnable() - Had to realloc array.\n");

				m_nReflectionSortAllocated += A3D_RESMAN_REFLECTION_GROW_ENTRIES;

				pNew = (REFLECTIONSORT *) operator new(
						sizeof(REFLECTIONSORT) * m_nReflectionSortAllocated);

				if (pNew)
				{
					memset(pNew, 0,
					       sizeof(REFLECTIONSORT) * m_nReflectionSortAllocated);
					memcpy(pNew, m_pReflectionSort,
					       sizeof(REFLECTIONSORT) * (m_nReflectionSortAllocated - A3D_RESMAN_REFLECTION_GROW_ENTRIES));
					operator delete(m_pReflectionSort);
					m_pReflectionSort = pNew;
				}
				else
				{
					DBGSTR("ResMan::SearchForReflectionsToEnable() - Failed to reallocate new reflection array.\n");
					m_nReflectionSortAllocated -= A3D_RESMAN_REFLECTION_GROW_ENTRIES;
				}
			}

			if (m_pReflectionSort)
			{
				m_pReflectionSort[*pReflectionCount - 1].pbAvailable =
					(DWORD *) &pSuper->Reflections[i].bAvailable;
				m_pReflectionSort[*pReflectionCount - 1].fKey =
					pSuper->Reflections[i].fAudibility;
			}
		}
	}

	pResManBuffer->m_lpReflectionSuperCtrl = pSuper;

	return (S_OK);
}

/* =============================================================
// CountPlayingStreamBuffers()
// (RE) dbg:0x10061C70; thunk dbg:0x100025EF; inline rtl:0x10027AD2
//
// Count playing buffers in the streaming list.
//
// Returns: The number of playing streaming buffers.
// =============================================================*/

int
ResMan::CountPlayingStreamBuffers(void)
{
POSITION        pos;
ResManBuffer   *pResManBuffer;
DWORD           dwState;
int             cPlaying;

	cPlaying = 0;

	pos = m_ResManStreamBufferList.GetHeadPosition();

	while (pos)
	{
		pResManBuffer = (ResManBuffer *) m_ResManStreamBufferList.GetNext(pos);

		ASSERT((pResManBuffer != 0 &&
		       !IsBadReadPtr(pResManBuffer, sizeof(ResManBuffer))));

		(static_cast<IResManBuffer *>(pResManBuffer))
			->GetBufferState(&dwState);

		if (dwState == A3DVOICE_STATE_PLAYING)
			cPlaying++;
	}

	return (cPlaying);
}

/* =============================================================
// CountPlayingStaticBuffers()
// (RE) dbg:0x10061d60; thunk dbg:0x10002888
//
// Count playing buffers in the static list.
// The original validity assertion checks only the pointer size.
//
// Returns: The number of playing static buffers.
// =============================================================*/

int
ResMan::CountPlayingStaticBuffers(void)
{
POSITION        pos;
ResManBuffer   *pResManBuffer;
DWORD           dwState;
int             cPlaying;

	cPlaying = 0;

	pos = m_ResManStatBufferList.GetHeadPosition();

	while (pos)
	{
		pResManBuffer = (ResManBuffer *) m_ResManStatBufferList.GetNext(pos);

		ASSERT((pResManBuffer != 0 &&
		       !IsBadReadPtr(pResManBuffer, sizeof(pResManBuffer))));

		(static_cast<IResManBuffer *>(pResManBuffer))
			->GetBufferState(&dwState);

		if (dwState == A3DVOICE_STATE_PLAYING)
			cPlaying++;
	}

	return (cPlaying);
}

/* =============================================================
// CountPlayingTwoChannel()
// (RE) dbg:0x10061E50; thunk dbg:0x10001B90; inline rtl:0x10027B26
//
// Count playing buffers in the two-channel list.
//
// Returns: The number of playing two-channel buffers.
// =============================================================*/

int
ResMan::CountPlayingTwoChannel(void)
{
POSITION        pos;
ResManBuffer   *pResManBuffer;
DWORD           dwState;
int             cPlaying;

	cPlaying = 0;

	pos = m_ResMan2ChBufferList.GetHeadPosition();

	while (pos)
	{
		pResManBuffer = (ResManBuffer *) m_ResMan2ChBufferList.GetNext(pos);

		ASSERT((pResManBuffer != 0 &&
		       !IsBadReadPtr(pResManBuffer, sizeof(pResManBuffer))));

		(static_cast<IResManBuffer *>(pResManBuffer))
			->GetBufferState(&dwState);

		if (dwState == A3DVOICE_STATE_PLAYING)
			cPlaying++;
	}

	return (cPlaying);
}

/* =============================================================
// CountHardwareCommitted()
// (RE) dbg:0x10061F50; thunk dbg:0x10002E87; inline rtl:0x10026A97
//
// Count static and streaming DAL buffers with mode flag bit 2 set.
//
// Returns: The total committed buffer count.
// =============================================================*/

int
ResMan::CountHardwareCommitted(void)
{
POSITION        pos;
DalInfo        *pDalInfo;
int             cCommitted;

	cCommitted = 0;

	pos = m_listDalInfo.GetHeadPosition();

	while (pos)
	{
		pDalInfo = (DalInfo *) m_listDalInfo.GetNext(pos);

		ASSERT((pDalInfo != 0 &&
		       !IsBadReadPtr(pDalInfo, sizeof(DalInfo))));

		if (pDalInfo->ReportsSoftwareStatus())
		{
			cCommitted += pDalInfo->GetNumStaticBuffers();
			cCommitted += pDalInfo->GetNumDalBuffers();
		}
	}

	return (cCommitted);
}

/* =============================================================
// TopUpStreamingBuffers()
// (RE) rtl:0x10026A40; dbg:0x10065630; thunk dbg:0x10003689
//
// Allocate streaming DAL buffers toward a target within device and global
// limits, then add the DAL's buffer count to the running total.
//
// Returns: S_OK if no allocation fails; the first CreateDalBuffer failure.
// =============================================================*/

HRESULT
ResMan::TopUpStreamingBuffers(int nTarget, int *pnRunning, DalInfo *pDalInfo)
{
int	nToMake;
int	i;
int	nOnDal;
HRESULT	hr;

	hr = S_OK;

	ASSERT((pDalInfo != 0 &&
	       !IsBadReadPtr(pDalInfo, sizeof(DalInfo))));

	if ((DWORD) nTarget > (DWORD) (*pnRunning + pDalInfo->GetNumDalBuffers()))
	{
		nToMake = nTarget - (*pnRunning + pDalInfo->GetNumDalBuffers());

		for (i = 0; i < nToMake; i++)
		{
			nOnDal = pDalInfo->GetNumStaticBuffers() +
				 pDalInfo->GetNumDalBuffers();

			if (pDalInfo->ReportsHardwareStatus())
			{
				if ((DWORD) nOnDal >= m_dwMaxHardwareSources)
					break;
			}

			if ((DWORD) nOnDal >= pDalInfo->GetTotalBufferCap() ||
			    (pDalInfo->ReportsSoftwareStatus() &&
			     (DWORD) CountHardwareCommitted() >= m_dwHwVoiceCap))
				break;

			hr = pDalInfo->CreateDalBuffer(NULL);

			if (FAILED(hr))
				break;
		}
	}

	*pnRunning += pDalInfo->GetNumDalBuffers();

	return (hr);
}

/* =============================================================
// CommitDalPass1()
// (RE) rtl:0x10027ac0; dbg:0x100683c0
//
// Allocate DAL buffers for the playing streaming and two-channel totals.
//
// Returns: S_OK, ignoring TopUpStreamingBuffers failures.
// =============================================================*/

HRESULT
ResMan::CommitDalPass1(int nWhen)
{
POSITION        pos;
DalInfo        *pDalInfo;
int             nRunning;
int             nTarget;

	nRunning = 0;
	nTarget  = CountPlayingStreamBuffers();

	pos = m_listDalInfo.GetHeadPosition();

	while (pos)
	{
		pDalInfo = (DalInfo *) m_listDalInfo.GetNext(pos);

		ASSERT((pDalInfo != 0 &&
		       !IsBadReadPtr(pDalInfo, sizeof(DalInfo))));

		if (pDalInfo->GetMode() == A3D_DAL_VOICE_COUNT_3D)
			TopUpStreamingBuffers(nTarget, &nRunning, pDalInfo);
	}

	nRunning = 0;
	nTarget  = CountPlayingTwoChannel();

	pos = m_listDalInfo.GetHeadPosition();

	while (pos)
	{
		pDalInfo = (DalInfo *) m_listDalInfo.GetNext(pos);

		ASSERT((pDalInfo != 0 &&
		       !IsBadReadPtr(pDalInfo, sizeof(DalInfo))));

		if (pDalInfo->GetMode() == A3D_DAL_VOICE_COUNT_MIXING)
			TopUpStreamingBuffers(nTarget, &nRunning, pDalInfo);
	}

	return (S_OK);
}

/* =============================================================
// CommitStreamsOnDal()
// (RE) rtl:0x10026b00; dbg:0x100657f0; thunk dbg:0x10001e06
//
// Bind priority-ordered stream buffers to one DAL, migrating bindings
// and evicting lower-priority buffers when needed.
//
// Returns: S_OK, including when no reusable DAL buffer can be found.
// =============================================================*/

HRESULT
ResMan::CommitStreamsOnDal(int nWhen, DalInfo *pDalInfo, int *pnBufferIndex)
{
ResManStreamBuffer     *pResManBuffer;
ResManStreamBuffer     *pVictim;
ResManStreamBuffer     *pResManBufferCurrent;
DalBufferInfo          *pDalBufferInfo;
DalBufferInfo          *pVictimInfo;
DalBufferInfo          *pTemp;
int                     nArrayIndex;
int                     nCurrentIndex;
unsigned int            i;

	ASSERT((pDalInfo != 0 &&
	       !IsBadReadPtr(pDalInfo, sizeof(DalInfo))));
	ASSERT((pnBufferIndex != 0 &&
	       !IsBadReadPtr(pnBufferIndex, sizeof(int))));

	if (m_nPriorityBufferElements < 1)
		return (S_OK);

	for (i = 0; ; i++)
	{
		if (i >= (unsigned int) pDalInfo->GetNumDalBuffers())
			break;

		if (--*pnBufferIndex < 0)
			break;

		nArrayIndex = m_nPriorityBufferElements - 1 - *pnBufferIndex;

		ASSERT(nArrayIndex >= 0 &&
		       nArrayIndex < m_nPriorityBufferElements);

		pResManBuffer = (ResManStreamBuffer *) m_pPriorityBufferArray[nArrayIndex];

		ASSERT((pResManBuffer != 0 &&
		       !IsBadReadPtr(pResManBuffer, sizeof(ResManBuffer))));

		(static_cast<IResManBufferPrimary *>(pResManBuffer))->
			GetDalBufferInfo(&pDalBufferInfo);

		if (pDalBufferInfo != NULL &&
		    pDalBufferInfo->GetDalInfo() == pDalInfo)
			continue;

		if (pDalBufferInfo != NULL &&
		    pDalBufferInfo->GetDalInfo() != pDalInfo)
		{
			if (WaitForSingleObject(pResManBuffer->m_hPropSetMutex,
						INFINITE) == WAIT_OBJECT_0)
				ReplayPropertySetItems(pResManBuffer,
						   pDalBufferInfo->ReportsHardwareStatus());

			VERIFY(ReleaseMutex(pResManBuffer->m_hPropSetMutex));

			ReturnBufferToDal(nWhen, pResManBuffer, pDalBufferInfo);
		}

		pDalInfo->FindIdle(&pDalBufferInfo);

		if (pDalBufferInfo == NULL)
		{
			pVictim     = NULL;
			pVictimInfo = NULL;

			for (nCurrentIndex = m_nPriorityBufferElements - 1;
			     nCurrentIndex >= 0 && pVictim == NULL;
			     nCurrentIndex--)
			{
				ASSERT(nCurrentIndex >= 0 &&
				       nCurrentIndex < m_nPriorityBufferElements);

				pResManBufferCurrent = (ResManStreamBuffer *)
						m_pPriorityBufferArray[nCurrentIndex];

				ASSERT((pResManBufferCurrent != 0 &&
				       !IsBadReadPtr(pResManBufferCurrent, sizeof(ResManBuffer))));

				(static_cast<IResManBufferPrimary *>(pResManBufferCurrent))->
					GetDalBufferInfo(&pTemp);

				if (pTemp != NULL &&
				    pTemp->GetDalInfo() == pDalInfo)
				{
					pVictim     = pResManBufferCurrent;
					pVictimInfo = pTemp;
				}
			}

			if (pVictim == NULL || pVictimInfo == NULL)
			{
				DBGSTR("ResMan::CommitStreamsOnDal() - Could not find a hardware buffer to kill.\n");

				return (S_OK);
			}

			ReturnBufferToDal(nWhen, pVictim, pVictimInfo);

			pDalBufferInfo = pVictimInfo;
		}

		ASSERT((pResManBuffer != 0 &&
		       !IsBadReadPtr(pResManBuffer, sizeof(ResManBuffer))));

		pDalBufferInfo->SetOwner((DWORD) pResManBuffer);
		pDalBufferInfo->SetChannels(1);
		pDalBufferInfo->SetTick(nWhen);

		(static_cast<IResManBufferPrimary *>(pResManBuffer))->
			AttachDalBufferInfo(pDalBufferInfo);

		pResManBuffer->SetRepeatGate(pDalInfo->ReportsHardwareStatus());
	}

	return (S_OK);
}

/* =============================================================
// CommitStreams()
// (RE) rtl:0x10026D20; dbg:0x10065DF0; thunk dbg:0x1000263F
//
// Bind playing two-channel buffers to one DAL, migrating bindings and
// evicting existing buffers when needed.
//
// Returns: S_OK, including when no reusable DAL buffer can be found.
// =============================================================*/

HRESULT
ResMan::CommitStreams(int nWhen, CList *pList, DalInfo *pDalInfo,
			   int *pnCount, POSITION *pPosition)
{
ResManBuffer   *pResManBuffer;
ResManBuffer   *pVictim;
ResManBuffer   *pResManBufferCurrent;
DalBufferInfo  *pDalBufferInfo;
DalBufferInfo  *pVictimInfo;
DalBufferInfo  *pTemp;
POSITION        pos;
unsigned int    i;

	ASSERT((pDalInfo != 0 &&
	       !IsBadReadPtr(pDalInfo, sizeof(DalInfo))));

	for (i = 0; ; i++)
	{
		if (i >= (unsigned int) pDalInfo->GetNumDalBuffers())
			break;

		if (--*pnCount < 0)
			break;

		pResManBuffer = (ResManBuffer *) pList->GetNext(*pPosition);

		ASSERT((pResManBuffer != 0 &&
		       !IsBadReadPtr(pResManBuffer, sizeof(ResManBuffer))));

		(static_cast<IResManBufferPrimary *>(pResManBuffer))->
			GetDalBufferInfo(&pDalBufferInfo);

		if (pDalBufferInfo != NULL &&
		    pDalBufferInfo->GetDalInfo() == pDalInfo)
			continue;

		if (pDalBufferInfo != NULL &&
		    pDalBufferInfo->GetDalInfo() != pDalInfo)
			ReturnBufferToDal(nWhen, pResManBuffer, pDalBufferInfo);

		pDalInfo->FindIdle(&pDalBufferInfo);

		if (pDalBufferInfo == NULL)
		{
			pVictim     = NULL;
			pVictimInfo = NULL;

			pos = pList->GetTailPosition();

			while (pos)
			{
				pResManBufferCurrent = (ResManBuffer *) pList->GetPrev(pos);

				ASSERT((pResManBufferCurrent != 0 &&
				       !IsBadReadPtr(pResManBufferCurrent, sizeof(ResManBuffer))));

				(static_cast<IResManBufferPrimary *>(pResManBufferCurrent))->
					GetDalBufferInfo(&pTemp);

				if (pTemp != NULL &&
				    pTemp->GetDalInfo() == pDalInfo)
				{
					pVictim     = pResManBufferCurrent;
					pVictimInfo = pTemp;
				}
			}

			if (pVictim == NULL || pVictimInfo == NULL)
			{
				DBGSTR("ResMan::CommitStreams - Could not find a hardware buffer to kill\n");

				return (S_OK);
			}

			ReturnBufferToDal(nWhen, pVictim, pVictimInfo);

			pDalBufferInfo = pVictimInfo;
		}

		if (pDalBufferInfo != NULL)
		{
			ASSERT((pResManBuffer != 0 &&
			       !IsBadReadPtr(pResManBuffer, sizeof(ResManBuffer))));

			pDalBufferInfo->SetOwner((DWORD) pResManBuffer);
			pDalBufferInfo->SetChannels(2);
			pDalBufferInfo->SetTick(nWhen);

			(static_cast<IResManBufferPrimary *>(pResManBuffer))->
				AttachDalBufferInfo(pDalBufferInfo);
		}
	}

	return (S_OK);
}

/* =============================================================
// CommitDalPass2()
// (RE) rtl:0x10027ba0; dbg:0x10068570
//
// Distribute the priority array and two-channel playing list across DALs.
//
// Returns: S_OK, ignoring commit-worker failures.
// =============================================================*/

HRESULT
ResMan::CommitDalPass2(int nWhen)
{
POSITION        pos;
DalInfo        *pDalInfo;
int             nBufferIndex;
int             nCount;
POSITION        posCommit;

	nBufferIndex = m_nPriorityBufferElements;

	pos = m_listDalInfo.GetHeadPosition();

	while (pos)
	{
		pDalInfo = (DalInfo *) m_listDalInfo.GetNext(pos);

		ASSERT((pDalInfo != 0 &&
		        !IsBadReadPtr(pDalInfo, sizeof(DalInfo))));

		if (pDalInfo->GetMode() == A3D_DAL_VOICE_COUNT_3D)
			CommitStreamsOnDal(nWhen, pDalInfo, &nBufferIndex);
	}

	nCount    = m_ResMan2ChPlayingList.GetCount();
	posCommit = m_ResMan2ChPlayingList.GetHeadPosition();

	pos = m_listDalInfo.GetHeadPosition();

	while (pos)
	{
		pDalInfo = (DalInfo *) m_listDalInfo.GetNext(pos);

		ASSERT((pDalInfo != 0 &&
		        !IsBadReadPtr(pDalInfo, sizeof(DalInfo))));

		if (pDalInfo->GetMode() == A3D_DAL_VOICE_COUNT_MIXING)
			CommitStreams(nWhen, &m_ResMan2ChPlayingList, pDalInfo,
					   &nCount, &posCommit);
	}

	return (S_OK);
}

/* =============================================================
// PriorityPass()
// (RE) dbg:0x10068750; inlined in Retail ServiceThread at rtl:0x10028637
//
// Release expired idle buffers on every DAL.
//
// Returns: S_OK, ignoring Reap failures.
// =============================================================*/

HRESULT
ResMan::PriorityPass(int nWhen)
{
POSITION        pos;
DalInfo        *pDalInfo;

	pos = m_listDalInfo.GetHeadPosition();

	while (pos)
	{
		pDalInfo = (DalInfo *) m_listDalInfo.GetNext(pos);

		ASSERT((pDalInfo != 0 &&
		        !IsBadReadPtr(pDalInfo, sizeof(DalInfo))));

		pDalInfo->Reap(nWhen);
	}

	return (S_OK);
}

/* =============================================================
// ApplyFocusChange()
// (RE) dbg:0x10068810; rtl:0x10027c30
//
// Apply pending focus mute or unmute changes to streaming and static buffers.
//
// Returns: S_OK, ignoring buffer-operation failures. Unresolved: neither
//          reference consistently sets a return value.
// =============================================================*/

HRESULT
ResMan::ApplyFocusChange(void)
{
POSITION        pos;
ResManBuffer   *pResManBuffer;

	if (!m_dwFocusChangePending)
		return (S_OK);

	pos = m_ResManStreamBufferList.GetHeadPosition();

	while (pos)
	{
		pResManBuffer = (ResManBuffer *) m_ResManStreamBufferList.GetNext(pos);

		ASSERT((pResManBuffer != 0 &&
		        !IsBadReadPtr(pResManBuffer, sizeof(ResManBuffer))));

		if (m_dwAppInactive)
			(static_cast<IResManBuffer *>(pResManBuffer))->
				MuteForFocusLoss();
		else
			(static_cast<IResManBuffer *>(pResManBuffer))->
				UnMuteForFocusGain();
	}

	pos = m_ResManStatBufferList.GetHeadPosition();

	while (pos)
	{
		pResManBuffer = (ResManBuffer *) m_ResManStatBufferList.GetNext(pos);

		ASSERT((pResManBuffer != 0 &&
		        !IsBadReadPtr(pResManBuffer, sizeof(ResManBuffer))));

		if (m_dwAppInactive)
			(static_cast<IResManBuffer *>(pResManBuffer))->
				MuteForFocusLoss();
		else
			(static_cast<IResManBuffer *>(pResManBuffer))->
				UnMuteForFocusGain();
	}

	m_dwFocusChangePending = 0;

	return (S_OK);
}

/* =============================================================
// TickPass()
// (RE) rtl:0x10027cb0; dbg:0x10068a00
//
// Send pending controls and tick playing buffers, replay buffer properties,
// and flush the resource manager's property queue.
//
// Returns: S_OK, ignoring buffer-operation and property-flush failures.
// =============================================================*/

HRESULT
ResMan::TickPass(int nWhen)
{
POSITION        pos;
ResManBuffer   *pResManBuffer;
DWORD           dwBufferState;

	pos = m_ResManStreamBufferList.GetHeadPosition();

	while (pos)
	{
		pResManBuffer = (ResManBuffer *) m_ResManStreamBufferList.GetNext(pos);

		ASSERT((pResManBuffer != 0 &&
		        !IsBadReadPtr(pResManBuffer, sizeof(ResManBuffer))));

		(static_cast<IResManBuffer *>(pResManBuffer))->
			GetBufferState(&dwBufferState);

		if (dwBufferState == A3DVOICE_STATE_PLAYING)
		{
			pResManBuffer->SendPendingCtrl();

			(static_cast<IResManBufferPrimary *>(pResManBuffer))->
				Tick(nWhen);

			if (WaitForSingleObject(pResManBuffer->m_hPropSetMutex,
						INFINITE) == WAIT_OBJECT_0 &&
			    (static_cast<IResManBufferPrimary *>(pResManBuffer))->HasUnappliedPropertySet())
			{
				ReplayPropertySetItems(pResManBuffer, 0);
			}

			VERIFY(ReleaseMutex(pResManBuffer->m_hPropSetMutex));
		}
	}

	pos = m_ResManStatBufferList.GetHeadPosition();

	while (pos)
	{
		pResManBuffer = (ResManBuffer *) m_ResManStatBufferList.GetNext(pos);

		ASSERT((pResManBuffer != 0 &&
		        !IsBadReadPtr(pResManBuffer, sizeof(ResManBuffer))));

		(static_cast<IResManBuffer *>(pResManBuffer))->
			GetBufferState(&dwBufferState);

		if (dwBufferState == A3DVOICE_STATE_PLAYING)
		{
			pResManBuffer->SendPendingCtrl();

			(static_cast<IResManBufferPrimary *>(pResManBuffer))->
				Tick(nWhen);
		}

		if (WaitForSingleObject(pResManBuffer->m_hPropSetMutex,
					INFINITE) == WAIT_OBJECT_0 &&
		    (static_cast<IResManBufferPrimary *>(pResManBuffer))->HasUnappliedPropertySet())
		{
			ReplayPropertySetItems(pResManBuffer, 0);
		}

		VERIFY(ReleaseMutex(pResManBuffer->m_hPropSetMutex));
	}

	pos = m_ResMan2ChBufferList.GetHeadPosition();

	while (pos)
	{
		pResManBuffer = (ResManBuffer *) m_ResMan2ChBufferList.GetNext(pos);

		ASSERT((pResManBuffer != 0 &&
		        !IsBadReadPtr(pResManBuffer, sizeof(ResManBuffer))));

		(static_cast<IResManBuffer *>(pResManBuffer))->
			GetBufferState(&dwBufferState);

		if (dwBufferState == A3DVOICE_STATE_PLAYING)
		{
			pResManBuffer->SendPendingCtrl();

			(static_cast<IResManBufferPrimary *>(pResManBuffer))->
				Tick(nWhen);
		}
	}

	if (m_dwPropSetCacheValid)
	{
		if (WaitForSingleObject(m_hPropSetMutex, INFINITE) == WAIT_OBJECT_0 &&
		    m_listPropSetQueue.GetCount() > 0)
		{
			FlushPropertySetCache();
		}

		VERIFY(ReleaseMutex(m_hPropSetMutex));
	}

	return (0);
}

/* =============================================================
// ReplayPropertySetItems()
// (RE) rtl:0x10027e20; dbg:0x10068e70
//
// Replay cached properties onto a buffer's DAL and signal completion.
// Preserve the original FlushPropertySetCache labels in failure diagnostics.
//
// Returns: S_OK without a DAL or after replay; the property-interface query
//          failure.
// =============================================================*/

HRESULT
ResMan::ReplayPropertySetItems(ResManBuffer *pResManBuffer, int bReplayResManList)
{
DalBufferInfo          *pDalBufferInfo;
IDirectSoundBuffer     *lpDSB;
IKsPropertySet         *pPropertySet;
CPropertySetItem       *pItem;
POSITION                pos;
POSITION                posThis;
HRESULT                 hr;

	ASSERT((pResManBuffer != 0 &&
	        !IsBadReadPtr(pResManBuffer, sizeof(ResManBuffer))));

	pDalBufferInfo = NULL;

	(static_cast<IResManBufferPrimary *>(pResManBuffer))->
		GetDalBufferInfo(&pDalBufferInfo);

	if (pDalBufferInfo == NULL)
	{
		DBGSTR("ResMan::FlushPropertySetCache() - Flush called with no DAL attached.\n");

#if defined(A3D_FIXES)
		/* NOT PART OF THE ORIGINAL.  The original returns here without
		 * signalling, which is the second deadlock of the same class as
		 * the one the macro at the top of this file describes: the three
		 * ResManStreamBuffer property-set methods (rmstreambuffer.cpp)
		 * queue a record and wait on this buffer's own
		 * m_hPropSetCacheFlushed at 0x85C, and a buffer that never gets
		 * a DAL is never signalled at all.  The failed-query path just
		 * below signals before returning; this one does not.  Signal it
		 * here so the waiter reads its record back unanswered, which is
		 * what the failed-query path already gives it.
		 */
		if (A3dGetConfig().bFixPropertyDeadlocks)
		{
			ASSERT(pResManBuffer->m_hPropSetCacheFlushed != INVALID_HANDLE_VALUE &&
			       pResManBuffer->m_hPropSetCacheFlushed != 0);
			VERIFY(SetEvent(pResManBuffer->m_hPropSetCacheFlushed));
		}
#endif

		return (S_OK);
	}

	ASSERT((pDalBufferInfo != 0 &&
	        !IsBadReadPtr(pDalBufferInfo, sizeof(DalBufferInfo))));

	lpDSB = pDalBufferInfo->GetDSBuffer();

	ASSERT((lpDSB != 0 &&
	        !IsBadReadPtr(lpDSB, sizeof(IDirectSoundBuffer))));

	pPropertySet = NULL;

	hr = lpDSB->QueryInterface(IID_IKsPropertySet, (void **) &pPropertySet);

	if (FAILED(hr))
	{
		DBGSTR("ResMan::FlushPropertySetCache() - Could not query for Property Set interface.\n");

		ASSERT(pResManBuffer->m_hPropSetCacheFlushed != INVALID_HANDLE_VALUE &&
		       pResManBuffer->m_hPropSetCacheFlushed != 0);
		VERIFY(SetEvent(pResManBuffer->m_hPropSetCacheFlushed));

		return (hr);
	}

	if (WaitForSingleObject(m_hPropSetMutex, INFINITE) == WAIT_OBJECT_0)
	{
		if (bReplayResManList)
		{
			pos = m_listResManPropSetItems.GetHeadPosition();

			while (pos)
			{
				pItem = (CPropertySetItem *)
					m_listResManPropSetItems.GetNext(pos);

				pItem->Apply(pPropertySet);
			}
		}

		pos = pResManBuffer->m_listPropSetItems.GetHeadPosition();

		while (pos)
		{
			posThis = pos;
			pItem   = (CPropertySetItem *)
				pResManBuffer->m_listPropSetItems.GetNext(pos);

			ASSERT((pItem != 0 &&
			        !IsBadReadPtr(pItem, sizeof(CPropertySetItem))));

			if (!pItem->m_dwApplied)
				MakePropertySetCall(pItem, pPropertySet);

			if (!pItem->m_dwBufferReplayEnabled && !pItem->m_dwRetainAfterCall)
			{
				pResManBuffer->m_listPropSetItems.RemoveAt(posThis);

				if (pItem)
					delete pItem;
			}
		}
	}

	VERIFY(ReleaseMutex(m_hPropSetMutex));

	if (pPropertySet)
	{
		pPropertySet->Release();
		pPropertySet = NULL;
	}

	ASSERT(pResManBuffer->m_hPropSetCacheFlushed != INVALID_HANDLE_VALUE &&
	       pResManBuffer->m_hPropSetCacheFlushed != 0);
	VERIFY(SetEvent(pResManBuffer->m_hPropSetCacheFlushed));

	return (S_OK);
}

/* =============================================================
// FlushPropertySetCache()
// (RE) rtl:0x10027fe0; dbg:0x10069440
//
// Acquire a hardware property interface and replay queued calls, retaining
// flagged items for read-back.
//
// Returns: The property-interface query failure; E_FAIL for a non-hardware
//          buffer; S_OK otherwise, including buffer-creation, GetCaps and
//          mutex-wait failures.
// =============================================================*/

HRESULT
ResMan::FlushPropertySetCache(void)
{
WAVEFORMATEX            wfx;
DSBUFFERDESC1           desc;
DSBCAPS                 caps;
IDirectSoundBuffer     *lpDSB;
CPropertySetItem       *pItem;
POSITION                pos;
POSITION                posThis;
HRESULT                 hr;

	if (m_lpPropertySet == NULL)
	{
		wfx.wFormatTag      = WAVE_FORMAT_PCM;
		wfx.nChannels       = 1;
		wfx.nSamplesPerSec  = 11025;
		wfx.nAvgBytesPerSec = 22050;
		wfx.nBlockAlign     = 2;
		wfx.wBitsPerSample  = 16;
		wfx.cbSize          = 0;

		desc.dwSize        = sizeof(DSBUFFERDESC1);
		desc.dwFlags       = DSBCAPS_LOCHARDWARE | DSBCAPS_CTRL3D;
		desc.dwBufferBytes = A3D_RESMAN_PROPERTY_PROBE_BYTES;
		desc.dwReserved    = 0;
		desc.lpwfxFormat   = &wfx;

		lpDSB = NULL;

		hr = m_pDirectSound->CreateSoundBuffer((LPCDSBUFFERDESC) &desc,
						       (LPDIRECTSOUNDBUFFER *) &lpDSB,
						       NULL);
		if (SUCCEEDED(hr))
		{
			ASSERT((lpDSB != 0 &&
			        !IsBadReadPtr(lpDSB, sizeof(IDirectSoundBuffer))));

			ZeroMemory(&caps, sizeof(caps));
			caps.dwSize = sizeof(DSBCAPS);

			if (SUCCEEDED(lpDSB->GetCaps(&caps)))
			{
				if (caps.dwFlags & DSBCAPS_LOCHARDWARE)
				{
					hr = lpDSB->QueryInterface(IID_IKsPropertySet,
								   (void **) &m_lpPropertySet);
					if (FAILED(hr))
					{
						DBGSTR("ResMan::FlushPropertySetCache() - Could not query for Property Set interface.\n");

						if (lpDSB)
						{
							lpDSB->Release();
							lpDSB = NULL;
						}

						return (hr);
					}
				}
				else
				{
					DBGSTR("ResMan::FlushPropertySetCache() - Failed to get a hardware buffer.\n");

					if (lpDSB)
					{
						lpDSB->Release();
						lpDSB = NULL;
					}

					return (E_FAIL);
				}
			}

			if (lpDSB)
			{
				lpDSB->Release();
				lpDSB = NULL;
			}
		}
	}

	if (m_lpPropertySet != NULL)
	{
		ASSERT((m_lpPropertySet != 0 &&
		        !IsBadReadPtr(m_lpPropertySet, sizeof(IKsPropertySet))));

		if (WaitForSingleObject(m_hPropSetMutex, INFINITE) == WAIT_OBJECT_0)
		{
			pos = m_listPropSetQueue.GetHeadPosition();

			while (pos)
			{
				posThis = pos;
				pItem   = (CPropertySetItem *)
					m_listPropSetQueue.GetNext(pos);

				MakePropertySetCall(pItem, m_lpPropertySet);

				if (pItem->m_dwRetainAfterCall == 0)
				{
					m_listPropSetQueue.RemoveAt(posThis);

					if (pItem)
						delete pItem;
				}
			}
		}

		VERIFY(ReleaseMutex(m_hPropSetMutex));
	}
	else
	{
		m_dwPropSetCacheValid = 0;
	}

	ASSERT(m_hPropSetCacheFlushed != INVALID_HANDLE_VALUE &&
	       m_hPropSetCacheFlushed != 0);
	VERIFY(SetEvent(m_hPropSetCacheFlushed));

	return (S_OK);
}

/* =============================================================
// MakePropertySetCall()
// (RE) rtl:0x10028230; dbg:0x100699b0; thunk dbg:0x10004200
//
// Execute a recorded property call and mark the item applied.
// Debug returns an unused item pointer; Retail leaves a call-dependent value.
// Both references use __stdcall without this;
// this implementation is a static __cdecl void member.
// =============================================================*/

void
ResMan::MakePropertySetCall(CPropertySetItem *pItem, IKsPropertySet *pPropertySet)
{
	ASSERT((pItem != 0 &&
	        !IsBadReadPtr(pItem, sizeof(CPropertySetItem))));
	ASSERT((pPropertySet != 0 &&
	        !IsBadReadPtr(pPropertySet, sizeof(IKsPropertySet))));

	switch (pItem->m_nCallType)
	{
	case A3D_PROPERTY_RECORD_QUERY_SUPPORT:
		pItem->m_hrCall = pPropertySet->QuerySupport(
			pItem->m_guidPropertySet, pItem->m_ulId,
			&pItem->m_ulTypeSupport);
		break;

	case A3D_PROPERTY_RECORD_GET:
		pItem->m_hrCall = pPropertySet->Get(
			pItem->m_guidPropertySet, pItem->m_ulId,
			pItem->m_pInstanceData, pItem->m_cbInstanceData,
			pItem->m_pPropertyData, pItem->m_cbPropertyData,
			&pItem->m_cbBytesReturned);
		break;

	case A3D_PROPERTY_RECORD_SET:
		pItem->m_hrCall = pPropertySet->Set(
			pItem->m_guidPropertySet, pItem->m_ulId,
			pItem->m_pInstanceData, pItem->m_cbInstanceData,
			pItem->m_pPropertyData, pItem->m_cbPropertyData);
		break;

	default:
		TRACE("ResMan::MakePropertySetCall() - Unsupported property set call made (pItem->m_nCallType = %d).\n",
		      pItem->m_nCallType);

		ASSERT(0);
		break;
	}

	pItem->m_dwApplied = 1;
}

/* =============================================================
// ProcessRenderModes()
// (RE) rtl:0x100282d0; dbg:0x10069c00
//
// Move buffers to the list for their render mode, returning old DAL bindings.
// Unsupported modes are reset to the source list's mode.
//
// Returns: S_OK, ignoring buffer-operation failures.
// =============================================================*/

HRESULT
ResMan::ProcessRenderModes(int nWhen, CList *pList, DWORD dwRenderMode)
{
POSITION        pos;
POSITION        posThis;
ResManBuffer   *pResManBuffer;
DalBufferInfo	*pDalBufferInfo; /* Original leaves this unset before the getter. */
DWORD		dwRenderModeHave;

	pos = pList->GetHeadPosition();

	while (pos)
	{
		posThis       = pos;
		pResManBuffer = (ResManBuffer *) pList->GetNext(pos);

		ASSERT((pResManBuffer != 0 &&
		        !IsBadReadPtr(pResManBuffer, sizeof(ResManBuffer))));

		(static_cast<IResManBuffer *>(pResManBuffer))
			->GetRenderMode(&dwRenderModeHave);

		if (dwRenderModeHave != dwRenderMode)
		{
			(static_cast<IResManBufferPrimary *>(pResManBuffer))
				->GetDalBufferInfo(&pDalBufferInfo);

			if (pDalBufferInfo)
				ReturnBufferToDal(nWhen, pResManBuffer, pDalBufferInfo);

			pList->RemoveAt(posThis);
		}

		switch (dwRenderModeHave)
		{
		case RESMANBUFFER_RENDER_3D:
			if (dwRenderMode != RESMANBUFFER_RENDER_3D)
				m_ResManStreamBufferList.AddTail(pResManBuffer);
			break;

		case RESMANBUFFER_RENDER_DEFAULT:
			if (dwRenderMode != RESMANBUFFER_RENDER_DEFAULT)
				m_ResMan2ChBufferList.AddTail(pResManBuffer);
			break;

		case RESMANBUFFER_RENDER_FOUR_CHANNEL:
			if (dwRenderMode != RESMANBUFFER_RENDER_FOUR_CHANNEL)
			{
				DBGSTR("ResMan::ProcessRenderModes 4Ch mode is currently unsupported\n");

				ASSERT(0);

				pList->AddTail(pResManBuffer);

				(static_cast<IResManBuffer *>(pResManBuffer))
					->SetRenderMode(dwRenderMode);
			}
			break;

		default:
			{
				DBGSTR("ResMan::ProcessRenderModes Buffer has unsupported render mode\n");

				ASSERT(0);

				pList->AddTail(pResManBuffer);

				(static_cast<IResManBuffer *>(pResManBuffer))
					->SetRenderMode(dwRenderMode);
			}
			break;
		}
	}

	return (S_OK);
}

/* =============================================================
// UpdateStreamRenderModes()
// (RE) dbg:0x10069ef0; inlined in Retail ServiceThread at rtl:0x10028600
//
// Reconcile the streaming and two-channel lists with buffer render modes.
//
// Returns: S_OK, ignoring ProcessRenderModes failures.
// =============================================================*/

HRESULT
ResMan::UpdateStreamRenderModes(int nWhen)
{
	ProcessRenderModes(nWhen, &m_ResManStreamBufferList, RESMANBUFFER_RENDER_3D);
	ProcessRenderModes(nWhen, &m_ResMan2ChBufferList, RESMANBUFFER_RENDER_DEFAULT);

	return (S_OK);
}

/* =============================================================
// ServiceThread()
// (RE) rtl:0x100284f0; dbg:0x10069f50
//
// Service buffers until shutdown, then signal inactivity and wait for exit.
// The service and mixer threads use independent events and timeouts.
//
// Returns: 0.
// =============================================================*/

DWORD WINAPI
ResMan::ServiceThread(void *lpvRef)
{
ResMan *pResMan;
DWORD   dwNow;

	ASSERT(lpvRef != 0);

	pResMan = (ResMan *) lpvRef;

	ASSERT((pResMan != 0 && !IsBadReadPtr(pResMan, sizeof(ResMan))));

	while (pResMan->m_dwServiceThreadActive)
	{
		WaitForSingleObject(pResMan->m_hEvent, pResMan->m_dwWait);

		dwNow = GetTickCount();

		pResMan->LightPass();

		if (dwNow - pResMan->m_dwLastRun > pResMan->m_dwInterval ||
		    pResMan->m_dwHeavyPassPending)
		{
			pResMan->m_dwLastRun            = dwNow;
			pResMan->m_dwHeavyPassPending   = 0;

			pResMan->FreeReleasedStreams(dwNow);
			pResMan->HaltStoppedStreams(dwNow);
			pResMan->UpdateStreamRenderModes(dwNow);
			pResMan->CommitDalPass1(dwNow);
			pResMan->SortAndPickReflections(dwNow);
			pResMan->CommitDalPass2(dwNow);
			pResMan->PriorityPass(dwNow);
			pResMan->ApplyFocusChange();
			pResMan->DumpCounts(0);
		}

		pResMan->TickPass(dwNow);
	}

	VERIFY(SetEvent(pResMan->m_hEventCallbackInactive));

	WaitForSingleObject(pResMan->m_hEventCallbackExit, A3D_RESMAN_EXIT_WAIT_MS);

	return (0);
}
