/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * dal_d3d.h
 *
 * Declares DAL_D3D and its private startup interface for hardware
 * DirectSound3D playback. The device holds DirectSound and
 * primary-listener resources, detected feature state, allocated voices
 * and reflection-thread synchronization.
 *
 * dal_d3d.cpp implements device probing, initialization and scheduling.
 * D3DBuffer and CReflection, declared in d3dbuffer.h, hold the playback
 * and timing state that the device's worker services.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _DAL_D3D_H
#define _DAL_D3D_H

#include "A3dPrivate.h"
#include "LinkList.h"
#include "Plex.h"

class D3DBuffer;

/* (RE) Private startup interface;
 * Vtable at DAL_D3D+0x0C: dbg:0x10131594; rtl:0x1005398c, 4 slots.
 */

#undef  INTERFACE
#define INTERFACE IA3dPrvD3D

DECLARE_INTERFACE_(IA3dPrvD3D, IUnknown)
{
	STDMETHOD(QueryInterface)	(THIS_ REFIID, void **) PURE;	/* Slot 0. */
	STDMETHOD_(ULONG, AddRef)	(THIS) PURE;	/* Slot 1. */
	STDMETHOD_(ULONG, Release)	(THIS) PURE;	/* Slot 2. */

	/* Slot 3: create the primary buffer and start reflection processing. */
	STDMETHOD(StartupDal)		(THIS) PURE;
};

/* =============================================================
// Class: DAL_D3D
//
// Description: DirectSound3D device with A3D properties and reflection
// processing.
//
// Size: 0x70
//
// (RE) Constructor: dbg:0x1004e370
// =============================================================*/

/* (RE) Vtables:
 * 0x00 IDirectSound dbg:0x10131618 rtl:0x100539f8, 11 slots.
 * 0x04 IA3dDal      dbg:0x101315e4 rtl:0x100539cc, 11 slots.
 * 0x08 IA3d2        dbg:0x101315a8 rtl:0x1005399c, 12 slots.
 * 0x0C IA3dPrvD3D   dbg:0x10131594 rtl:0x1005398c, 4 slots.
 */

class DAL_D3D : public IDirectSound,
		public IA3dDal,
		public IA3d2,
		public IA3dPrvD3D
{
public:
	DAL_D3D(void *fRequireA3d);
	~DAL_D3D(void);

	/* IUnknown, one set shared by the four vtables. */

	STDMETHODIMP		QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG)	AddRef(void);
	STDMETHODIMP_(ULONG)	Release(void);

	/* IDirectSound. */

	STDMETHODIMP	CreateSoundBuffer(LPCDSBUFFERDESC lpcDSBufferDesc,
					  LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
					  LPUNKNOWN pUnkOuter);
	STDMETHODIMP	GetCaps(LPDSCAPS lpDirectSoundCaps);
	STDMETHODIMP	DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER pOriginal,
					     LPDIRECTSOUNDBUFFER *ppCopy);
	STDMETHODIMP	SetCooperativeLevel(HWND hWnd, DWORD dwLevel);
	STDMETHODIMP	Compact(void);
	STDMETHODIMP	GetSpeakerConfig(LPDWORD pdwConfig);
	STDMETHODIMP	SetSpeakerConfig(DWORD dwConfig);
	STDMETHODIMP	Initialize(LPCGUID pGuid);

	/* IA3dDal, in ia3ddal.h slot order. */

	STDMETHODIMP	InitializeEx(LPGUID pGuidDevice, DWORD dwFlags,
				     DWORD dwReserved, LPDWORD lpdwFeaturesEnabled);
	STDMETHODIMP	CreateSoundBufferEx(const DSBUFFERDESC1 *lpcDSBufferDesc,
					    LPBYTE lpbWave,
					    LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
					    LPUNKNOWN pUnkOuter);
	STDMETHODIMP	GetA3dCaps(LPA3DDALCAPS lpA3dCaps, LPDWORD lpdwSize);
	STDMETHODIMP	GetDriverInfo(LPHANDLE lphA3dVxd, void **lplpIDsDriver,
				      void **lplpIA3dDriver, LPDWORD lpdwCardIndex);
	STDMETHODIMP	GetDS(LPDIRECTSOUND *lplpDirectSound);
	STDMETHODIMP	GetDSDriverDesc(void *lpsDSDriverDesc, LPDWORD lpdwSize);
	STDMETHODIMP	QueryFunctionality(DWORD dwFunction, LPDWORD lpdwStatus);
	STDMETHODIMP	Verify(LPSTR lpcString, LPSTR *lplpcStringCrypted,
			       LPSTR *lplpcCopyright);

	/* IA3d2. */

	STDMETHODIMP	SetOutputMode(DWORD dwFrontXtalkMode,
				      DWORD dwBackXtalkMode, DWORD dwQuadMode);
	STDMETHODIMP	GetOutputMode(LPDWORD lpdwFrontXtalkMode,
				      LPDWORD lpdwBackXtalkMode,
				      LPDWORD lpdwQuadMode);
	STDMETHODIMP	SetResourceManagerMode(DWORD dwResourceManagerMode);
	STDMETHODIMP	GetResourceManagerMode(LPDWORD lpdwResourceManagerMode);
	STDMETHODIMP	SetHFAbsorbFactor(FLOAT fFactor);
	STDMETHODIMP	GetHFAbsorbFactor(FLOAT *pfFactor);
	STDMETHODIMP	RegisterVersion(DWORD dwVersion);
	STDMETHODIMP	GetSoftwareCaps(LPA3DCAPS_SOFTWARE pCaps);
	STDMETHODIMP	GetHardwareCaps(LPA3DCAPS_HARDWARE pCaps);

	/* IA3dPrvD3D slot 3. */

	STDMETHODIMP	StartupDal(void);

	HRESULT		Init(DWORD dwFeatures, DWORD dwReserved, LPDWORD lpdwAvailable);
	HRESULT		InitDS(IDirectSound **lplpDS, REFCLSID rclsid);
	BOOL		AllocProbe(void);
	HRESULT		AccessA3dProperty(ULONG ulId, BOOL bGet, LPVOID pPropertyData,
					  ULONG ulDataLength);
	HRESULT		CreateSecondaryBuffer(const DSBUFFERDESC1 *pDesc, LPBYTE lpWave,
					      LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
					      LPUNKNOWN pUnkOuter);
	HRESULT		GetPrimaryBuffer(const DSBUFFERDESC1 *pDesc,
					 LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
					 LPUNKNOWN pUnkOuter);

	HRESULT		CreateReflection(LPDIRECTSOUNDBUFFER lpDirectSoundBuffer,
					 D3DBuffer **ppReflectionDsb,
					 D3DBuffer **ppReflectionDal);

public:
	/* 0x10 */ LONG		m_cRef;	/* Initially 1. */

	/* 0x14 */ DWORD	m_dwSplashWindow;	/* HWND for the omitted, unreachable splash call. */

	/* 0x18 */ IDirectSound		*m_pDirectSound;	/* Owned DirectSound reference. */
	/* 0x1C */ IDirectSoundBuffer	*m_lpDirectSound3DPrimaryBuffer;	/* Owned primary buffer. */
	/* 0x20 */ IDirectSound3DListener *m_lpDirectSound3DListener;	/* Owned listener interface. */

	/* 0x24 */ CList		m_BufferList;	/* Owned D3DBuffer pointers */

	/* 0x3C */ DWORD	m_dwFreeHw3DBuffers;	/* Free hardware 3D static-buffer count. */

	/* 0x40 */ BYTE		m_fStarted;	/* StartupDal completed. */
	/* 0x41 */ BYTE		m_fHardware;	/* A3D property capabilities available. */
	/* 0x42 */ BYTE		m_abUnknown_0x42[2];

	/* 0x44 */ DWORD	m_dwEnabledFeatures;	/* Property 1 enabled features; returned to property 2 on release. */
	/* 0x48 */ DWORD	m_dwPropertyReleaseData;	/* Opaque property 1 word returned to property 2 on release. */

	/* 0x4C */ DWORD	m_fThreadRun;	/* Continue reflection processing while set. */
	/* 0x50 */ DWORD	m_dwThreadId;	/* Reflection thread ID. */
	/* 0x54 */ HANDLE	m_hThread;	/* Owned reflection thread handle; initially -1. */
	/* 0x58 */ HANDLE	m_hBufferListMutex;	/* Owned voice-list mutex; initially null. */
	/* 0x5C */ HANDLE	m_hThreadExitEvent;	/* Owned thread completion event; initially null. */

	/* 0x60 */ DWORD	m_Unknown_0x60;	/* Zeroed; no other use established. */

	/* 0x64 */ void		*m_fRequireA3d;	/* Boolean use: reject an A3D device whose capability probe fails. */
	/* 0x68 */ DWORD	m_dwFeatures;	/* Requested InitializeEx flags. */
	/* 0x6C */ DWORD	m_fUseReflections;	/* Reflection processing enabled. */
};

typedef int DalD3dSizeCheck[(sizeof(DAL_D3D) == 0x70) ? 1 : -1];

#endif /* _DAL_D3D_H */
