/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * dal_d2d.h
 *
 * Declares DAL_D2D, the DirectSound device adapter used by the resource
 * manager. It exposes the common DirectSound and A3D device interfaces
 * and stores the wrapped device and allocated D2DBuffer list.
 *
 * dal_d2d.cpp handles device initialization, capabilities and voice
 * creation. Per-buffer forwarding and the conversion of spatial controls
 * to volume, pan and frequency are implemented by D2DBuffer.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _DALD2D_H
#define _DALD2D_H

#include "A3dPrivate.h"
#include "LinkList.h"
#include "Plex.h"

class D2DBuffer;

/* =============================================================
// Class: DAL_D2D
//
// Description: DirectSound device adapter with tracked buffer allocation.
//
// Size: 0x2C
//
// (RE) Constructor: dbg:0x1004c6f0
// =============================================================*/

/* (RE) Vtables:
   0x00 IDirectSound: dbg:0x10131138; rtl:0x10053960; slots 0-10.
   0x04 IA3dDal: dbg:0x10131104; rtl:0x10053934; slots 0-10.
   0x08 IA3d2: dbg:0x101310c8; rtl:0x10053904; slots 0-11. */

class DAL_D2D : public IDirectSound,
		public IA3dDal,
		public IA3d2
{
public:
	DAL_D2D(void);
	~DAL_D2D(void);

	/* IUnknown, one set shared by the three vtables. */

	STDMETHODIMP         QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG) AddRef(void);
	STDMETHODIMP_(ULONG) Release(void);

	/* IDirectSound. */

	STDMETHODIMP CreateSoundBuffer(LPCDSBUFFERDESC lpcDSBufferDesc,
	                               LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
	                               LPUNKNOWN pUnkOuter);
	STDMETHODIMP GetCaps(LPDSCAPS lpDirectSoundCaps);
	STDMETHODIMP DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER pOriginal,
	                                  LPDIRECTSOUNDBUFFER *ppCopy);
	STDMETHODIMP SetCooperativeLevel(HWND hWnd, DWORD dwLevel);
	STDMETHODIMP Compact(void);
	STDMETHODIMP GetSpeakerConfig(LPDWORD pdwConfig);
	STDMETHODIMP SetSpeakerConfig(DWORD dwConfig);
	STDMETHODIMP Initialize(LPCGUID pGuid);

	/* IA3dDal, in ia3ddal.h slot order. */

	STDMETHODIMP InitializeEx(LPGUID pGuidDevice, DWORD dwFlags,
	                          DWORD dwReserved, LPDWORD lpdwAvailable);
	STDMETHODIMP CreateSoundBufferEx(const DSBUFFERDESC1 *lpcDSBufferDesc,
	                                 LPBYTE lpBuffer,
	                                 LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
	                                 LPUNKNOWN pUnkOuter);
	STDMETHODIMP GetA3dCaps(LPA3DDALCAPS lpA3dCaps, LPDWORD lpdwSize);
	STDMETHODIMP GetDriverInfo(LPHANDLE lphA3dVxd, void **lplpIA3dDriver,
	                           void **lplpIDsDriver, LPDWORD lpdwSize);
	STDMETHODIMP GetDS(LPDIRECTSOUND *lplpDirectSound);
	STDMETHODIMP GetDSDriverDesc(void *lpsDSDriverDesc, LPDWORD lpdwSize);
	STDMETHODIMP QueryFunctionality(DWORD dwFunction, LPDWORD lpdwResult);
	STDMETHODIMP Verify(LPSTR lpcString, LPSTR *lplpcStringCrypted,
	                    LPSTR *lplpcCopyright);

	/* IA3d2. */

	STDMETHODIMP SetOutputMode(DWORD dwFrontXtalkMode,
	                           DWORD dwBackXtalkMode, DWORD dwQuadMode);
	STDMETHODIMP GetOutputMode(LPDWORD lpdwFrontXtalkMode,
	                           LPDWORD lpdwBackXtalkMode,
	                           LPDWORD lpdwQuadMode);
	STDMETHODIMP SetResourceManagerMode(DWORD dwResourceManagerMode);
	STDMETHODIMP GetResourceManagerMode(LPDWORD lpdwResourceManagerMode);
	STDMETHODIMP SetHFAbsorbFactor(FLOAT fFactor);
	STDMETHODIMP GetHFAbsorbFactor(FLOAT *pfFactor);
	STDMETHODIMP RegisterVersion(DWORD dwVersion);
	STDMETHODIMP GetSoftwareCaps(LPA3DCAPS_SOFTWARE pCaps);
	STDMETHODIMP GetHardwareCaps(LPA3DCAPS_HARDWARE pCaps);

	HRESULT Init(void);
	HRESULT CreateSecondaryBuffer(const DSBUFFERDESC1 *pDesc,
	                              LPBYTE lpWave,
	                              LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
	                              LPUNKNOWN pUnkOuter);

public:
	/* 0x0C */ LONG         m_cRef;       /* starts at 1 */
	/* 0x10 */ IDirectSound *m_pDS;        /* Owned DirectSound reference. */
	/* 0x14 */ CList        m_BufferList; /* Allocated buffers  */
};

typedef int DalD2dSizeCheck[(sizeof(DAL_D2D) == 0x2C) ? 1 : -1];

#endif /* _DALD2D_H */
