/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3d.h
 *
 * Declares CA3d, the shared A3D 1.x compatibility device behind the
 * coclasses served by a3d.dll. It exposes IDirectSound and IA3d2 and
 * retains the underlying API interfaces, device capabilities and lists of
 * wrapped buffers.
 *
 * The declarations also provide buffer factories and the optional
 * server-count release hook. A3d.cpp implements activation and device
 * behavior, while the listener, secondary buffer and source headers
 * declare the playback objects created through it.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _A3D_A3D_H
#define _A3D_A3D_H

#include "a3dprv.h"
#include "Plex.h"

/* Optional server-count release hook. */

typedef void (*A3DATEXITPROC)(void);

/* =============================================================
// Class: CA3d
//
// Description: DirectSound and A3D object shared by both coclasses.
//
// Size: 0x304
//
// (RE) Constructor: a3d.dll rtl:0x10001230
// =============================================================*/

class CA3d : public IDirectSound,
	     public IA3d2
{
public:
	CA3d(A3DATEXITPROC pfnAtExit);
	~CA3d(void);

	void Zero(void);

	STDMETHODIMP         QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG) AddRef(void);
	STDMETHODIMP_(ULONG) Release(void);

	/* IDirectSound - slots 3..10 of the interface at +0x00 */
	STDMETHODIMP CreateSoundBuffer(LPCDSBUFFERDESC pcDesc,
	                               LPDIRECTSOUNDBUFFER *ppBuffer,
	                               LPUNKNOWN pUnkOuter);
	STDMETHODIMP GetCaps(LPDSCAPS pCaps);
	STDMETHODIMP DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER pOriginal,
	                                  LPDIRECTSOUNDBUFFER *ppDuplicate);
	STDMETHODIMP SetCooperativeLevel(HWND hWnd, DWORD dwLevel);
	STDMETHODIMP Compact(void);
	STDMETHODIMP GetSpeakerConfig(LPDWORD pdwConfig);
	STDMETHODIMP SetSpeakerConfig(DWORD dwConfig);
	STDMETHODIMP Initialize(LPCGUID pcGuidDevice);

	/* IA3d - slots 3..8 of the interface at +0x04 */
	STDMETHODIMP SetOutputMode(DWORD dwFrontXtalkMode, DWORD dwBackXtalkMode,
	                           DWORD dwQuadMode);
	STDMETHODIMP GetOutputMode(LPDWORD lpdwFrontXtalkMode, LPDWORD lpdwBackXtalkMode,
	                           LPDWORD lpdwQuadMode);
	STDMETHODIMP SetResourceManagerMode(DWORD dwResourceManagerMode);
	STDMETHODIMP GetResourceManagerMode(LPDWORD lpdwResourceManagerMode);
	STDMETHODIMP SetHFAbsorbFactor(FLOAT fFactor);
	STDMETHODIMP GetHFAbsorbFactor(FLOAT *pfFactor);

	/* IA3d2 - slots 9..11 */
	STDMETHODIMP RegisterVersion(DWORD dwVersion);
	STDMETHODIMP GetSoftwareCaps(LPA3DCAPS_SOFTWARE pCaps);
	STDMETHODIMP GetHardwareCaps(LPA3DCAPS_HARDWARE pCaps);

	HRESULT Init(LPCGUID pcGuidDevice, DWORD dwFeatures,
	             DWORD dwInitFlags, LPDWORD pdwFeaturesEnabled);

public:

	DWORD         m_Unknown_0x08;
	DWORD         m_Unknown_0x0C;
	DWORD         m_Unknown_0x10;
	BOOL          m_bInited;
	DWORD         m_Unknown_0x18;
	DWORD         m_Unknown_0x1C;
	LONG          m_cRef; /* not interlocked */
	DWORD         m_Unknown_0x24;
	LPDIRECTSOUND m_pDS;  /* a3dapi.dll's IDirectSound */

	A3DPLEX m_PrimaryBuffers;   /* 0x20-byte elements */
	A3DPLEX m_SecondaryBuffers; /* 0xAC-byte elements */

	BYTE m_Unknown_0x54[0x28C - 0x54]; /* Unverified storage. */

	A3DATEXITPROC m_pfnAtExit;     /* 0x28C */
	DWORD         m_fAtExit;       /* 0x290; enables the at-exit hook. */
	BOOL          m_bReady;
	BOOL          m_fSplash;
	DWORD         m_Unknown_0x29C; /* Unverified storage. */
	DWORD         m_dwAppVersion;  /* Application A3D version. */

	A3DCAPS_SOFTWARE m_SoftwareCaps; /* dwSize 40, dwVersion 20 */
	A3DCAPS_HARDWARE m_HardwareCaps; /* dwSize 36 */

	IA3d2        *m_pA3d2;
	IUnknown     *m_pPrv4;
	IA3dDal      *m_pDal;
	LPDIRECTSOUND m_pDSOut; /* DirectSoundCreate */

	DWORD m_dwResourceManagerMode; /* Restored after non-3D buffer creation. */
};

HRESULT A3dMakePrimaryBuffer(CA3d *pOwner, LPCDSBUFFERDESC pcDesc,
                             LPDIRECTSOUNDBUFFER *ppBuffer,
                             LPUNKNOWN pUnkOuter);
HRESULT A3dMakeSecondaryBuffer(CA3d *pOwner, LPCDSBUFFERDESC pcDesc,
                               LPDIRECTSOUNDBUFFER *ppBuffer,
                               LPUNKNOWN pUnkOuter);
HRESULT A3dDuplicateBuffer(CA3d *pOwner, LPDIRECTSOUNDBUFFER pOriginal,
                           LPDIRECTSOUNDBUFFER *ppDuplicate);

void A3dFreeGlobalTables(void);

void A3dSplash(void);

extern HWND	g_hwndSplash;
extern CA3d	*g_pA3d;

#endif /* _A3D_A3D_H */
