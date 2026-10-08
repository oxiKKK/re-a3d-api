/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * dal_emu.h
 *
 * Declares DAL_EMU, the non-rendering device implementation used with
 * EMUBuffer voices. It presents the common DirectSound and A3D device
 * interfaces and keeps the allocated voice list and device reference
 * count.
 *
 * dal_emu.cpp creates voices and reports their capacity. The playback
 * state and elapsed-time cursor behavior are implemented in
 * emubuffer.cpp; this device owns no audio output or mixer thread.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _DAL_EMU_H
#define _DAL_EMU_H

#include "A3dPrivate.h"
#include "LinkList.h"
#include "Plex.h"

class EMUBuffer;

/* =============================================================
// Class: DAL_EMU
//
// Description: Silent emulation device with fixed capabilities.
//
// Size: 0x2C
//
// (RE) Constructor: dbg:0x10052240
// =============================================================*/

/* (RE) Vtables:
   0x00 IDirectSound: dbg:0x10132274; rtl:0x10053a80; slots 0-10.
   0x04 IA3dDal: dbg:0x10132240; rtl:0x10053a54; slots 0-10.
   0x08 IA3d2: dbg:0x10132204; rtl:0x10053a24; slots 0-11. */

class DAL_EMU : public IDirectSound,
		public IA3dDal,
		public IA3d2
{
public:
	DAL_EMU(void);
	~DAL_EMU(void);

	/* IUnknown, one set shared by the three vtables. */

	STDMETHODIMP		QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG)	AddRef(void);
	STDMETHODIMP_(ULONG)	Release(void);

	/* IDirectSound. */

	STDMETHODIMP	CreateSoundBuffer(LPCDSBUFFERDESC lpcDSBufferDesc,
					  LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
					  LPUNKNOWN pUnkOuter);
	STDMETHODIMP	GetCaps(LPDSCAPS lpDirectSoundCaps);
	STDMETHODIMP	DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER lpDirectSoundBuffer,
					     LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer);
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

	HRESULT		Init(void);
	HRESULT		CreateSecondaryBuffer(const DSBUFFERDESC1 *pDesc,
					      LPBYTE lpWave,
					      LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
					      LPUNKNOWN pUnkOuter);
	DWORD		CountPlayingVoices(void);

public:
	/* 0x0C */ LONG		m_cRef;		/* starts at 1 */
	/* 0x10 */ DWORD	m_dwMaxBuffers;	/* 0xFFFF; the buffer cap GetCaps reports */
	/* 0x14 */ CList	m_list;		/* Allocated buffers. */
};

typedef int DalEmuSizeCheck[(sizeof(DAL_EMU) == 0x2C) ? 1 : -1];

#endif /* _DAL_EMU_H */
