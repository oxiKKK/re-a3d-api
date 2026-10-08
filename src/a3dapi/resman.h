/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * resman.h
 *
 * Declares ResMan, the resource manager connecting source playback to DAL
 * devices. The class exposes DirectSound, A3D, private management and
 * property interfaces and owns backend records, playback-buffer lists and
 * voice-allocation state.
 *
 * Its shared state includes service-thread events, focus handling,
 * priority settings and deferred property calls. resman.cpp coordinates
 * allocation and servicing; dalinfo.h and the resource-manager buffer
 * headers define the device and source records it manages.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _RESMAN_H
#define _RESMAN_H

#include "A3dPrivate.h"
#include "LinkList.h"
#include "Plex.h"
#include "softmix.h"
#include "a2dbuffer.h"

class CHrtfMgr;

class   DalInfo;
class   ResManBuffer;
class   ResManStreamBuffer;
class   DalBufferInfo;
class   CPropertySetItem;
struct  IKsPropertySet;

/* =============================================================
// Class: REFLECTIONSORT
//
// Description: Reflection availability flag and descending sort key.
//
// Size: 0x08
//
// (RE) Comparator: dbg:0x10067040
// =============================================================*/

struct REFLECTIONSORT
{
	/* 0x00 */ DWORD	*pbAvailable;	/* Availability flag set to 1 when selected. */
	/* 0x04 */ FLOAT	fKey;	/* Descending priority. */
};

typedef int REFLECTIONSORTSizeCheck[(sizeof(REFLECTIONSORT) == 8) ? 1 : -1];

/* =============================================================
// Class: ResMan
//
// Description: DAL selection, voice allocation and property-set dispatch.
//
// Size: 0x178
//
// (RE) Constructor: dbg:0x1005B970
// =============================================================*/

/* (RE) Interface subobjects and vtables:
 * 0x00 IDirectSound:   11 slots; rtl:0x10053c4c; dbg:0x101335b4.
 * 0x04 IA3dDal:        11 slots; rtl:0x10053c20; dbg:0x10133580.
 * 0x08 IA3d2:          12 slots; rtl:0x10053bf0; dbg:0x10133544.
 * 0x0C IA3dPrv4:       12 slots; rtl:0x10053bc0; dbg:0x10133508.
 * 0x10 IA3dPropertySet:  7 slots; rtl:0x10053ba4; dbg:0x101334e4.
 */

class ResMan : public IDirectSound,
	       public IA3dDal,
	       public IA3d2,
	       public IA3dPrv4,
	       public IA3dPropertySet
{
public:
	ResMan(void);
	~ResMan(void);

	/* 0 */ STDMETHODIMP		QueryInterface(REFIID riid, void **ppv);
	/* 1 */ STDMETHODIMP_(ULONG)	AddRef(void);
	/* 2 */ STDMETHODIMP_(ULONG)	Release(void);

	/* 3 */ STDMETHODIMP	CreateSoundBuffer(LPCDSBUFFERDESC lpcDSBufferDesc,
					  LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
					  LPUNKNOWN pUnkOuter);
	/* 4 */ STDMETHODIMP	GetCaps(LPDSCAPS lpDirectSoundCaps);
	/* 5 */ STDMETHODIMP	DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER pOriginal,
					     LPDIRECTSOUNDBUFFER *ppCopy);
	/* 6 */ STDMETHODIMP	SetCooperativeLevel(HWND hWnd, DWORD dwLevel);
	/* 7 */ STDMETHODIMP	Compact(void);
	/* 8 */ STDMETHODIMP	GetSpeakerConfig(LPDWORD pdwConfig);
	/* 9 */ STDMETHODIMP	SetSpeakerConfig(DWORD dwConfig);
	/* 10 */ STDMETHODIMP	Initialize(LPCGUID pGuid);

	/* IA3dDal; slots 0-2 use the shared IUnknown methods. */
	/* 3 */ STDMETHODIMP	InitializeEx(LPGUID pGuidDevice, DWORD dwFlags,
				     DWORD dwReserved,
				     LPDWORD lpdwAvailable);
	/* 4 */ STDMETHODIMP	CreateSoundBufferEx(const DSBUFFERDESC1 *lpcDSBufferDesc,
					    LPBYTE lpbWave,
					    LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
					    LPUNKNOWN pUnkOuter);
	/* 5 */ STDMETHODIMP	GetA3dCaps(LPA3DDALCAPS lpA3dCaps,
				   LPDWORD lpdwSize);
	/* 6 */ STDMETHODIMP	GetDriverInfo(LPHANDLE lphA3dVxd, void **lplpIDsDriver,
				      void **lplpIA3dDriver,
				      LPDWORD lpdwCardIndex);
	/* 7 */ STDMETHODIMP	GetDS(LPDIRECTSOUND *lplpDirectSound);
	/* 8 */ STDMETHODIMP	GetDSDriverDesc(void *lpsDSDriverDesc,
					LPDWORD lpdwSize);
	/* 9 */ STDMETHODIMP	QueryFunctionality(DWORD dwFunction,
					   LPDWORD lpdwResult);
	/* 10 */ STDMETHODIMP	Verify(LPSTR lpcString, LPSTR *lplpcStringCrypted,
			       LPSTR *lplpcCopyright);

	/* IA3d2; slots 0-2 use the shared IUnknown methods. */
	/* 3 */ STDMETHODIMP	SetOutputMode(DWORD dwFrontXtalkMode,
				      DWORD dwBackXtalkMode,
				      DWORD dwQuadMode);
	/* 4 */ STDMETHODIMP	GetOutputMode(LPDWORD lpdwFrontXtalkMode,
				      LPDWORD lpdwBackXtalkMode,
				      LPDWORD lpdwQuadMode);
	/* 5 */ STDMETHODIMP	SetResourceManagerMode(DWORD dwResourceManagerMode);
	/* 6 */ STDMETHODIMP	GetResourceManagerMode(LPDWORD lpdwResourceManagerMode);
	/* 7 */ STDMETHODIMP	SetHFAbsorbFactor(FLOAT fFactor);
	/* 8 */ STDMETHODIMP	GetHFAbsorbFactor(FLOAT *pfFactor);
	/* 9 */ STDMETHODIMP	RegisterVersion(DWORD dwVersion);
	/* 10 */ STDMETHODIMP	GetSoftwareCaps(LPA3DCAPS_SOFTWARE pCaps);
	/* 11 */ STDMETHODIMP	GetHardwareCaps(LPA3DCAPS_HARDWARE pCaps);

	/* IA3dPrv4; slots 0-2 use the shared IUnknown methods. */
	/* 3 */ STDMETHODIMP	GetPriorityWeight(LPA3DVAL pfPriorityWeight);
	/* 4 */ STDMETHODIMP	SetPriorityWeight(A3DVAL fPriorityWeight);
	/* 5 */ STDMETHODIMP	GetBufferLatency(LPA3DVAL pfBufferLatency);
	/* 6 */ STDMETHODIMP	SetBufferLatency(A3DVAL fBufferLatency);
	/* 7 */ STDMETHODIMP	GetBufferRefreshThreshold(LPA3DVAL pfBufferRefreshThreshold);
	/* 8 */ STDMETHODIMP	SetBufferRefreshThreshold(A3DVAL fBufferRefreshThreshold);
	/* 9 */ STDMETHODIMP	GetHardwareSourceLimit(LPDWORD lpdwCount);
	/* 10 */ STDMETHODIMP	SetHardwareSourceLimit(DWORD dwCount);
	/* 11 */ STDMETHODIMP	GetHardwareSourceCapacity(LPDWORD lpdwCount);

	HRESULT		SetDalBufferLatency(DalInfo *pDalInfo, A3DVAL fBufferLatency);
	HRESULT		GetDalBufferLatency(DalInfo *pDalInfo, LPA3DVAL pfBufferLatency);
	HRESULT		SetDalBufferRefreshThreshold(DalInfo *pDalInfo,
						     A3DVAL fBufferRefreshThreshold);
	HRESULT		GetDalBufferRefreshThreshold(DalInfo *pDalInfo,
						     LPA3DVAL pfBufferRefreshThreshold);

	/* IA3dPropertySet; slots 0-2 use the shared IUnknown methods. */
	/* 3 */ STDMETHODIMP	QuerySupport(REFGUID rguidPropSet, ULONG ulId,
				     PULONG pulTypeSupport);
	/* 4 */ STDMETHODIMP	Get(REFGUID rguidPropSet, ULONG ulId,
			    LPVOID pInstanceData, ULONG cbInstanceData,
			    LPVOID pPropertyData, ULONG cbPropertyData,
			    PULONG pulBytesReturned);
	/* 5 */ STDMETHODIMP	Set(REFGUID rguidPropSet, ULONG ulId,
			    LPVOID pInstanceData, ULONG cbInstanceData,
			    LPVOID pPropertyData, ULONG cbPropertyData,
			    DWORD dwFlags);
	/* 6 */ STDMETHODIMP	AddInitialStateParameters(REFGUID rguidPropSet, ULONG ulId,
						  LPVOID pInstanceData,
						  ULONG cbInstanceData,
						  LPVOID pPropertyData,
						  ULONG cbPropertyData);

	static DWORD WINAPI	ServiceThread(void *pv);

	void		LightPass(void);
	HRESULT		FreeReleasedStreams(int nWhen);
	HRESULT		HaltStoppedStreams(int nWhen);
	HRESULT		UpdateStreamRenderModes(int nWhen);
	HRESULT		CommitDalPass1(int nWhen);
	HRESULT		SortAndPickReflections(int nWhen);
	HRESULT		CommitDalPass2(int nWhen);
	HRESULT		PriorityPass(int nWhen);
	HRESULT		ApplyFocusChange(void);
	HRESULT		TickPass(int nWhen);

	HRESULT		RebuildTwoChannelCommitList(int nWhen, CList *pSrcList,
					   CList *pDstList);
	int		ClearReflectionAvailability(int nHardwareSources);
	HRESULT		SearchForReflectionsToEnable(ResManBuffer *pResManBuffer,
						     int *pReflectionCount);

	void		AddBufferToPriorityArray(ResManStreamBuffer *pBuffer);

	int		CountPlayingStreamBuffers(void);
	int		CountPlayingTwoChannel(void);

	int		CountPlayingStaticBuffers(void);

	HRESULT		TopUpStreamingBuffers(int nTarget, int *pnRunning,
					   DalInfo *pDalInfo);

	HRESULT		CommitStreamsOnDal(int nWhen, DalInfo *pDalInfo,
					   int *pnBufferIndex);
	HRESULT		CommitStreams(int nWhen, CList *pList,
					   DalInfo *pDalInfo, int *pnCount,
					   POSITION *pPosition);

	int		CountHardwareCommitted(void);

	HRESULT		ReturnStoppedBuffersToDal(int nWhen, CList *pList);
	HRESULT		ProcessRenderModes(int nWhen, CList *pList, DWORD dwRenderMode);

	HRESULT		FreeListedBuffers(int nWhen, CList *pList);
	HRESULT		ReturnBufferToDal(int nWhen, ResManBuffer *pResManBuffer,
					   DalBufferInfo *pDalBufferInfo);

	HRESULT		ReplayPropertySetItems(ResManBuffer *pResManBuffer,
					   int bReplayResManList);
	HRESULT		FlushPropertySetCache(void);

	static void	MakePropertySetCall(CPropertySetItem *pItem,
					    IKsPropertySet *pPropertySet);

	HRESULT		StartTimerCallback(void);

	HRESULT		InitDalInterfaces(void);

	HRESULT		MakeStaticDalBuffer(int nMode, DSBUFFERDESC1 *pDesc,
					   DalBufferInfo **ppDalBufferInfo);

	STDMETHODIMP	CreateSoundBuffer(const DSBUFFERDESC1 *lpcDSBufferDesc,
					  LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
					  LPUNKNOWN pUnkOuter);

	DWORD		CountEntriesW95(void);
	DWORD		CountPlayingOnW95(void);
	DWORD		FreeVoices(void);
	void		DumpCounts(int fForceDump);

	HRESULT		MakePrimaryBuffer(DSBUFFERDESC1 *lpDSBufferDesc,
					  LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
					  LPUNKNOWN pUnkOuter);

	HRESULT		InitResMan(LPGUID pGuidDevice, DWORD dwReserved,
				   DWORD dwFlags, LPDWORD lpdwFeatures,
				   HWND hWnd, DWORD dwLevel);

	HRESULT		AcquireInterfaces(LPGUID pGuidDevice, DWORD dwReserved,
					  DWORD dwFlags, LPDWORD lpdwFeatures);

	void		DrainBufferList(CList *pList);

	void		EmptyBufferLists(void);

	HRESULT		ReleaseInterfaces(void);

	HRESULT		AcquireW95Interface(LPGUID pGuidDevice, DWORD dwFlags,
					    DWORD dwReserved,
					    LPDWORD lpdwFeaturesEnabled);
	HRESULT		AcquireD3DInterface(LPGUID pGuidDevice, DWORD dwFlags,
					    DWORD dwReserved,
					    LPDWORD lpdwFeaturesEnabled, int fRequireA3d);
	HRESULT		AcquireA2DInterface(LPGUID pGuidDevice, DWORD dwFlags,
					    DWORD dwReserved,
					    LPDWORD lpdwFeaturesEnabled);
	HRESULT		AcquireD2DInterface(LPGUID pGuidDevice, DWORD dwFlags,
					    DWORD dwReserved,
					    LPDWORD lpdwFeaturesEnabled);
	HRESULT		AcquireEMUInterface(LPGUID pGuidDevice, DWORD dwFlags,
					    DWORD dwReserved,
					    LPDWORD lpdwFeaturesEnabled);

	void		BuildDalTypeMask(void);

	DWORD		GetDalTypeMask(void);

	STDMETHODIMP	SetMaxHardwareSources(DWORD dwCount);
	STDMETHODIMP	GetMaxHardwareSources(LPDWORD lpdwMaxSources);

	void		ForceHeavyPass(void);

	void		OnAppDeactivated(void);
	void		OnAppActivated(void);

public:

	/* 0x014 */ HANDLE	m_hSuperCtrlMutex;	/* Serializes A3DCTRL_SRC_SUPER updates. */

	/* 0x018 */ HHOOK	m_hHook;	/* WH_CALLWNDPROC hook for activation messages. */
	/* 0x01C */ HWND	m_hWnd;	/* Cooperative-level window. */

	/* 0x020 */ CList	m_listDalInfo;	/* DalInfo *; attached DALs. */

	/* 0x038 */ CList	m_ResManStreamBufferList;	/* ResManStreamBuffer *. */

	/* 0x050 */ BYTE	m_Unknown_0x50[0x18];	/* Original list; element type is unresolved. */

	/* 0x068 */ CList	m_ResManStatBufferList;	/* ResManStatBuffer *. */

	/* 0x080 */ ResManBuffer	**m_pPriorityBufferArray;	/* Owned array of buffers awaiting priority assignment. */
	/* 0x084 */ REFLECTIONSORT	*m_pReflectionSort;	/* Owned reflection-sort array. */

	/* 0x088 */ IDirectSound	*m_pDirectSound;	/* Owned DirectSound reference. */
	/* 0x08C */ IKsPropertySet	*m_lpPropertySet;	/* Owned property set queried from a hardware buffer. */

	/* 0x090 */ DalInfo	*m_pMainDALInfo;	/* First attached DAL. */

	/* 0x094 */ DWORD	m_dwResourceManagerMode;	/* Current resource-manager mode. */

	/* 0x098 */ DWORD	m_dwThreadId;	/* Service-thread ID. */
	/* 0x09C */ HANDLE	m_hThread;	/* Owned service-thread handle. */
	/* 0x0A0 */ HANDLE	m_hEvent;	/* Service-thread wake event. */
	/* 0x0A4 */ HANDLE	m_hEventCallbackExit;	/* Allows the inactive service thread to exit. */
	/* 0x0A8 */ HANDLE	m_hEventCallbackInactive;	/* Signals that the service thread is inactive. */
	/* 0x0AC */ DWORD	m_dwWait;	/* Wake timeout in ms. */
	/* 0x0B0 */ DWORD	m_dwInterval;	/* Heavy-pass interval in ms. */
	/* 0x0B4 */ DWORD	m_dwLastRun;	/* Last heavy-pass tick, in ms. */

	/* 0x0B8 */ LONG	m_cRef;	/* Interlocked COM reference count. */
	/* 0x0BC */ DWORD	m_bInitialized;	/* InitResMan completed. */

	/* 0x0C0 */ DWORD	m_dwHwVoiceCap;	/* IA3dPrv4 hardware-source limit. */

	/* 0x0C4 */ DWORD	m_dwHeavyPassPending;	/* Forces the heavy passes on the next wake. */

	/* 0x0C8 */ DWORD	m_dwReflectionsEnabled;	/* Reflection allocation succeeded and is enabled. */
	/* 0x0CC */ int		m_nReflectionsSupported;	/* Maximum enabled reflections. */
	/* 0x0D0 */ int		m_nReflectionSortAllocated;	/* Allocated REFLECTIONSORT entries. */
	/* 0x0D4 */ int		m_nReflectionSortWanted;	/* Reflections selected by the current pass. */

	/* 0x0D8 */ CList	m_ResMan2ChBufferList;	/* ResManBuffer *; two-channel sources. */

	/* 0x0F0 */ CList	m_ResMan2ChPlayingList;	/* ResManBuffer *; playing two-channel sources to commit. */

	/* 0x108 */ FLOAT	m_fPriorityWeight;	/* Priority blend weight, 0.0 to 1.0. */

	/* 0x10C */ int		m_nPriorityBufferElements;	/* Used priority-array entries. */
	/* 0x110 */ int		m_nPriorityBufferArrayLength;	/* Allocated priority-array entries. */

	/* 0x114 */ DWORD	m_dwServiceThreadActive;	/* Service thread should continue running. */
	/* 0x118 */ DWORD	m_dwTimerCallbackStarted;	/* Timer callback has been started. */
	/* 0x11C */ DWORD	m_dwDalStartedUp;	/* DAL startup walk completed. */

	/* 0x120 */ DWORD	m_dwCoopLevelSet;	/* SetCooperativeLevel has completed. */

	/* 0x124 */ DWORD	m_dwDalTypeMask;	/* OR of attached DAL types. */

	/* 0x128 */ DWORD	m_dwAppInactive;	/* Application is inactive. */
	/* 0x12C */ DWORD	m_dwFocusChangePending;	/* Activation change awaiting buffer mute/unmute. */

	/* 0x130 */ CList	m_listPropSetQueue;	/* CPropertySetItem *; queued calls owned by this manager. */
	/* 0x148 */ CList	m_listResManPropSetItems;	/* CPropertySetItem *; defaults queued by AddInitialStateParameters. */

	/* 0x160 */ HANDLE	m_hPropSetCacheFlushed;	/* Signals property-set dispatch completion. */
	/* 0x164 */ HANDLE	m_hPropSetMutex;	/* Serializes property-set list access. */

	/* 0x168 */ DWORD	m_dwInitialStateSet;	/* AddInitialStateParameters has run. */
	/* 0x16C */ DWORD	m_dwPropSetCacheValid;	/* Property-set queue flushing remains enabled. */

	/* 0x170 */ DWORD	m_dwMaxHardwareSources;	/* DirectSound hardware mixing limit. */

	/* 0x174 */ DWORD	m_dwFocusMuteEnabled;	/* A3D_DISABLE_FOCUS_MUTE was not requested. */

};

typedef int A3dResManSizeCheck[(sizeof(ResMan) == 0x178) ? 1 : -1];

#endif /* _RESMAN_H */
