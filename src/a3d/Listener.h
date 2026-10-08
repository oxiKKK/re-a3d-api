/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * Listener.h
 *
 * Declares the compatibility primary-buffer container, its
 * IDirectSoundBuffer wrapper and its IDirectSound3DListener object. The
 * declarations connect a wrapped primary buffer to stored listener
 * parameters and the owning device's buffer lists.
 *
 * The listener tracks changes for deferred source commits, and both
 * interface objects share the container's references. Listener.cpp
 * implements this coordination using the source state and DSP operations
 * declared in A3dSource.h and a3ddsp.h.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _A3D_LISTENER_H
#define _A3D_LISTENER_H

#include "a3dprv.h"
#include "Plex.h"

class CA3dPrimaryBuffer;
class CA3dSecondaryBuffer;
class CA3dSource;

/* =============================================================
// Class: CA3dPrimaryDSB
//
// Description: Primary-buffer IDirectSoundBuffer interface, slots 0..20.
// =============================================================*/

class CA3dPrimaryDSB : public IDirectSoundBuffer
{
public:
	STDMETHODIMP         QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG) AddRef(void);
	STDMETHODIMP_(ULONG) Release(void);

	STDMETHODIMP GetCaps(LPDSBCAPS pCaps);
	STDMETHODIMP GetCurrentPosition(LPDWORD pdwPlay, LPDWORD pdwWrite);
	STDMETHODIMP GetFormat(LPWAVEFORMATEX pwfx, DWORD cbSize,
	                       LPDWORD pcbWritten);
	STDMETHODIMP GetVolume(LPLONG plVolume);
	STDMETHODIMP GetPan(LPLONG plPan);
	STDMETHODIMP GetFrequency(LPDWORD pdwFrequency);
	STDMETHODIMP GetStatus(LPDWORD pdwStatus);
	STDMETHODIMP Initialize(LPDIRECTSOUND pDS, LPCDSBUFFERDESC pcDesc);
	STDMETHODIMP Lock(DWORD dwOffset, DWORD dwBytes, LPVOID *ppvAudio1,
	                  LPDWORD pdwAudio1, LPVOID *ppvAudio2,
	                  LPDWORD pdwAudio2, DWORD dwFlags);
	STDMETHODIMP Play(DWORD dwReserved1, DWORD dwReserved2, DWORD dwFlags);
	STDMETHODIMP SetCurrentPosition(DWORD dwPosition);
	STDMETHODIMP SetFormat(LPCWAVEFORMATEX pcfxFormat);
	STDMETHODIMP SetVolume(LONG lVolume);
	STDMETHODIMP SetPan(LONG lPan);
	STDMETHODIMP SetFrequency(DWORD dwFrequency);
	STDMETHODIMP Stop(void);
	STDMETHODIMP Unlock(LPVOID pvAudio1, DWORD dwAudio1,
	                    LPVOID pvAudio2, DWORD dwAudio2);
	STDMETHODIMP Restore(void);

public:
	LONG               m_cRef;
	CA3dPrimaryBuffer *m_pOwner; /* Shares this interface's references. */
};

/* =============================================================
// Class: CA3dListener
//
// Description: Primary-buffer listener state and deferred source updates.
//
// Size: 0x4C
// =============================================================*/

class CA3dListener : public IDirectSound3DListener
{
public:
	CA3dListener(CA3dPrimaryBuffer *pOwner);

	STDMETHODIMP         QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG) AddRef(void);
	STDMETHODIMP_(ULONG) Release(void);

	STDMETHODIMP GetAllParameters(LPDS3DLISTENER pListener);
	STDMETHODIMP GetDistanceFactor(D3DVALUE *pflDistanceFactor);
	STDMETHODIMP GetDopplerFactor(D3DVALUE *pflDopplerFactor);
	STDMETHODIMP GetOrientation(D3DVECTOR *pvOrientFront,
	                            D3DVECTOR *pvOrientTop);
	STDMETHODIMP GetPosition(D3DVECTOR *pvPosition);
	STDMETHODIMP GetRolloffFactor(D3DVALUE *pflRolloffFactor);
	STDMETHODIMP GetVelocity(D3DVECTOR *pvVelocity);
	STDMETHODIMP SetAllParameters(LPCDS3DLISTENER pcListener,
	                              DWORD dwApply);
	STDMETHODIMP SetDistanceFactor(D3DVALUE flDistanceFactor,
	                               DWORD dwApply);
	STDMETHODIMP SetDopplerFactor(D3DVALUE flDopplerFactor,
	                              DWORD dwApply);
	STDMETHODIMP SetOrientation(D3DVALUE xFront, D3DVALUE yFront,
	                            D3DVALUE zFront, D3DVALUE xTop,
	                            D3DVALUE yTop, D3DVALUE zTop,
	                            DWORD dwApply);
	STDMETHODIMP SetPosition(D3DVALUE x, D3DVALUE y, D3DVALUE z,
	                         DWORD dwApply);
	STDMETHODIMP SetRolloffFactor(D3DVALUE flRolloffFactor,
	                              DWORD dwApply);
	STDMETHODIMP SetVelocity(D3DVALUE x, D3DVALUE y, D3DVALUE z,
	                         DWORD dwApply);
	STDMETHODIMP CommitDeferredSettings(void);

public:
	LONG               m_cRef;
	CA3dPrimaryBuffer *m_pOwner; /* Shares this interface's references. */
	DS3DLISTENER       m_ds3dl;  /* 0x0C; dwSize 64. */
};

/* =============================================================
// Class: CA3dPrimaryBuffer
//
// Description: Primary-buffer container with three IUnknown slots.
//
// Size: 0x20
// =============================================================*/

class CA3dPrimaryBuffer : public IUnknown
{
public:
	CA3dPrimaryBuffer(A3DPLEX *pPlex);
	~CA3dPrimaryBuffer(void);

	HRESULT		Init(LPDIRECTSOUNDBUFFER pBuffer);

	STDMETHODIMP         QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG) AddRef(void);
	STDMETHODIMP_(ULONG) Release(void);

public:
	LONG                m_cRef;
	DWORD               m_Unknown_0x08;
	CA3dPrimaryDSB     *m_pDSB;
	CA3dListener       *m_pListener;
	LPDIRECTSOUNDBUFFER m_pBuffer;      /* a3dapi.dll's buffer */
	DWORD               m_Unknown_0x18; /* Unverified storage. */
	A3DPLEX            *m_pPlex;        /* &pOwner->m_PrimaryBuffers */
};

extern DWORD	g_fListenerDirty;

#endif /* _A3D_LISTENER_H */
