/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * apimapper.h
 *
 * Declares the DirectSound compatibility device and buffer wrappers for
 * a3dapi.dll. CA3dMapper represents the device; CA3dMapperPrimBuffer and
 * CA3dMapperSecBuffer expose primary-buffer, listener and source-related
 * interfaces.
 *
 * The stored root and source references connect DirectSound calls to the
 * A3D engine. apimapper.cpp implements the translations and shared object
 * lifetime. These wrappers are separate from the A3D 1.x compatibility
 * objects in src/a3d.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _APIMAPPER_H
#define _APIMAPPER_H

#include "A3dPrivate.h"

class CA3dRoot;

void A3dBroadcastDoppler(class CA3dRoot *pOwner, A3DVAL fFactor);
void A3dBroadcastRolloff(class CA3dRoot *pOwner, A3DVAL fFactor);

void A3dMapperCloseDebugLog(void);

/* =============================================================
// Class: CA3dMapperPrimBuffer
//
// Description: DirectSound primary buffer and 3D listener.
//
// Size: 0x20
//
// (RE) Constructor: dbg:0x10038420
// =============================================================*/

class CA3dMapperPrimBuffer : public IDirectSoundBuffer,
			     public IDirectSound3DListener
{
public:
	CA3dMapperPrimBuffer(CA3dRoot *pOwner);
	virtual ~CA3dMapperPrimBuffer(void); /* Slot 21. */

	STDMETHODIMP         QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG) AddRef(void);
	STDMETHODIMP_(ULONG) Release(void);

	/* IDirectSound3DListener */
	STDMETHODIMP CommitDeferredSettings(void);
	STDMETHODIMP GetDistanceFactor(D3DVALUE *pfFactor);
	STDMETHODIMP GetDopplerFactor(D3DVALUE *pfFactor);
	STDMETHODIMP GetRolloffFactor(D3DVALUE *pfFactor);
	STDMETHODIMP SetVelocity(D3DVALUE x, D3DVALUE y, D3DVALUE z,
	                         DWORD dwApply);
	STDMETHODIMP SetDistanceFactor(D3DVALUE fFactor, DWORD dwApply);
	STDMETHODIMP SetOrientation(D3DVALUE fFx, D3DVALUE fFy, D3DVALUE fFz,
	                            D3DVALUE fTx, D3DVALUE fTy, D3DVALUE fTz,
	                            DWORD dwApply);

	STDMETHODIMP SetDopplerFactor(D3DVALUE fFactor, DWORD dwApply);
	STDMETHODIMP SetRolloffFactor(D3DVALUE fFactor, DWORD dwApply);
	STDMETHODIMP SetAllParameters(LPCDS3DLISTENER pcDs3dl,
	                              DWORD dwApply);
	STDMETHODIMP SetPosition(D3DVALUE x, D3DVALUE y, D3DVALUE z,
	                         DWORD dwApply);

	STDMETHODIMP GetAllParameters(LPDS3DLISTENER pDs3dl);
	STDMETHODIMP GetOrientation(D3DVECTOR *pvFront,
	                            D3DVECTOR *pvTop);
	STDMETHODIMP GetPosition(D3DVECTOR *pvPosition);
	STDMETHODIMP GetVelocity(D3DVECTOR *pvVelocity);

	STDMETHODIMP Lock(DWORD dwOffset, DWORD dwBytes,
	                  LPVOID *ppvAudioPtr1, LPDWORD pdwAudioBytes1,
	                  LPVOID *ppvAudioPtr2, LPDWORD pdwAudioBytes2,
	                  DWORD dwFlags);

	STDMETHODIMP GetCaps(LPDSBCAPS pCaps);
	STDMETHODIMP GetCurrentPosition(DWORD *pdwPlay, DWORD *pdwWrite);
	STDMETHODIMP GetFormat(LPWAVEFORMATEX pwfx, DWORD cb, DWORD *pcb);
	STDMETHODIMP GetVolume(LONG *plVolume);
	STDMETHODIMP GetPan(LONG *plPan);
	STDMETHODIMP GetFrequency(DWORD *pdwFrequency);
	STDMETHODIMP GetStatus(DWORD *pdwStatus);
	STDMETHODIMP Initialize(LPDIRECTSOUND pDs, LPCDSBUFFERDESC pDesc);
	STDMETHODIMP Play(DWORD dwReserved1, DWORD dwPriority, DWORD dwFlags);
	STDMETHODIMP SetCurrentPosition(DWORD dwPlay);
	STDMETHODIMP SetFormat(LPCWAVEFORMATEX pwfx);
	STDMETHODIMP SetVolume(LONG lVolume);
	STDMETHODIMP SetPan(LONG lPan);
	STDMETHODIMP SetFrequency(DWORD dwFrequency);
	STDMETHODIMP Stop(void);
	STDMETHODIMP Unlock(LPVOID pvAudioPtr1, DWORD dwAudioBytes1,
	                    LPVOID pvAudioPtr2, DWORD dwAudioBytes2);
	STDMETHODIMP Restore(void);

public:

	/* 0x08 */ CA3dRoot *m_pOwner;          /* Referenced API root. */
	/* 0x0C */ IUnknown *m_pListener;       /* Referenced IA3dListener. */
	/* 0x10 */ LONG     m_cRef;            /* COM reference count. */
	/* 0x14 */ D3DVALUE m_fDistanceFactor; /* Cached distance factor. */
	/* 0x18 */ D3DVALUE m_fDopplerFactor;  /* Source Doppler scale. */
	/* 0x1C */ D3DVALUE m_fRolloffFactor;  /* Source distance-model scale. */
};

class CA3dMapperPrimBuffer;

/* =============================================================
// Class: CA3dMapper
//
// Description: DirectSound and IA3d2 wrapper for the API root.
//
// Size: 0x14
//
// (RE) Constructor: dbg:0x10037060
// =============================================================*/

class CA3dMapper : public IDirectSound,
		   public IA3d2
{
public:
	CA3dMapper(CA3dRoot *pApi);

	virtual ~CA3dMapper(void); /* Slot 11. */

	STDMETHODIMP         QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG) AddRef(void);
	STDMETHODIMP_(ULONG) Release(void);

	/* IDirectSound */
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

	/* IA3d */
	STDMETHODIMP SetOutputMode(DWORD dwRelation, DWORD dwMode,
	                           DWORD dwChannels);
	STDMETHODIMP GetOutputMode(LPDWORD pdwRelation, LPDWORD pdwMode,
	                           LPDWORD pdwChannels);
	STDMETHODIMP SetResourceManagerMode(DWORD dwMode);
	STDMETHODIMP GetResourceManagerMode(LPDWORD pdwMode);
	STDMETHODIMP SetHFAbsorbFactor(FLOAT fFactor);
	STDMETHODIMP GetHFAbsorbFactor(FLOAT *pfFactor);

	/* IA3d2 */
	STDMETHODIMP RegisterVersion(DWORD dwVersion);
	STDMETHODIMP GetSoftwareCaps(LPA3DCAPS_SOFTWARE pCaps);
	STDMETHODIMP GetHardwareCaps(LPA3DCAPS_HARDWARE pCaps);

public:
	/* Referenced API root. */
	/* 0x08 */ CA3dRoot            *m_pApi;
	/* COM reference count. */
	/* 0x0C */ LONG                 m_cRef;
	/* Primary buffer created on demand. */
	/* 0x10 */ CA3dMapperPrimBuffer *m_pListener;
};

/* =============================================================
// Class: CA3dMapperSecBuffer
//
// Description: DirectSound buffer backed by an A3D source.
//
// Size: 0x1C
//
// (RE) Constructor: dbg:0x10039f70
// =============================================================*/

class CA3dMapperSecBuffer : public IDirectSoundBuffer,
			    public IDirectSound3DBuffer
{
public:
	CA3dMapperSecBuffer(IA3dSource2 *pSource,
	                    CA3dMapperPrimBuffer *pListener,
	                    CA3dRoot *pOwner);
	virtual ~CA3dMapperSecBuffer(void); /* Slot 21. */

	STDMETHODIMP         QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG) AddRef(void);
	STDMETHODIMP_(ULONG) Release(void);

	STDMETHODIMP GetCurrentPosition(DWORD *pdwPlay, DWORD *pdwWrite);
	STDMETHODIMP GetVolume(LONG *plVolume);
	STDMETHODIMP GetFrequency(DWORD *pdwFrequency);
	STDMETHODIMP GetStatus(DWORD *pdwStatus);
	STDMETHODIMP Lock(DWORD dwOffset, DWORD dwBytes,
	                  LPVOID *ppvAudioPtr1, LPDWORD pdwAudioBytes1,
	                  LPVOID *ppvAudioPtr2, LPDWORD pdwAudioBytes2,
	                  DWORD dwFlags);
	STDMETHODIMP Play(DWORD dwReserved1, DWORD dwPriority,
	                  DWORD dwFlags);
	STDMETHODIMP SetCurrentPosition(DWORD dwPlay);
	STDMETHODIMP SetVolume(LONG lVolume);
	STDMETHODIMP SetFrequency(DWORD dwFrequency);
	STDMETHODIMP Stop(void);
	STDMETHODIMP Unlock(LPVOID pvAudioPtr1, DWORD dwAudioBytes1,
	                    LPVOID pvAudioPtr2, DWORD dwAudioBytes2);

	STDMETHODIMP GetCaps(LPDSBCAPS pCaps);
	STDMETHODIMP GetFormat(LPWAVEFORMATEX pwfx, DWORD cb, DWORD *pcb);
	STDMETHODIMP GetPan(LONG *plPan);
	STDMETHODIMP Initialize(LPDIRECTSOUND pDs, LPCDSBUFFERDESC pDesc);
	STDMETHODIMP SetFormat(LPCWAVEFORMATEX pwfx);
	STDMETHODIMP SetPan(LONG lPan);
	STDMETHODIMP Restore(void);

	STDMETHODIMP SetConeAngles(DWORD dwInside, DWORD dwOutside,
	                           DWORD dwApply);
	STDMETHODIMP SetConeOrientation(D3DVALUE x, D3DVALUE y, D3DVALUE z,
	                                DWORD dwApply);
	STDMETHODIMP SetMaxDistance(D3DVALUE fMax, DWORD dwApply);
	STDMETHODIMP SetMinDistance(D3DVALUE fMin, DWORD dwApply);
	STDMETHODIMP SetPosition(D3DVALUE x, D3DVALUE y, D3DVALUE z,
	                         DWORD dwApply);
	STDMETHODIMP SetVelocity(D3DVALUE x, D3DVALUE y, D3DVALUE z,
	                         DWORD dwApply);
	STDMETHODIMP GetMaxDistance(D3DVALUE *pfMax);
	STDMETHODIMP GetMinDistance(D3DVALUE *pfMin);

	STDMETHODIMP SetAllParameters(LPCDS3DBUFFER pcDs3db,
	                              DWORD dwApply);
	STDMETHODIMP SetConeOutsideVolume(LONG lVolume, DWORD dwApply);
	STDMETHODIMP SetMode(DWORD dwMode, DWORD dwApply);

	STDMETHODIMP GetAllParameters(LPDS3DBUFFER pDs3db);
	STDMETHODIMP GetConeAngles(LPDWORD pdwInside,
	                           LPDWORD pdwOutside);
	STDMETHODIMP GetConeOrientation(D3DVECTOR *pvOrientation);
	STDMETHODIMP GetConeOutsideVolume(LPLONG plVolume);
	STDMETHODIMP GetMode(LPDWORD pdwMode);
	STDMETHODIMP GetPosition(D3DVECTOR *pvPosition);
	STDMETHODIMP GetVelocity(D3DVECTOR *pvVelocity);

	HRESULT CommitDeferredSettings(void);

public:
	/* 0x08 */ LONG m_cRef; /* COM reference count. */

	/* 0x0C */ IA3dSource2 *m_pSource;  /* Referenced A3D source. */
	/* 0x10 */ CA3dRoot   *m_pOwner;   /* Referenced API root. */
	/* 0x14 */ LONG        m_lVolume;  /* Cached hundredths of a decibel. */

	/* 0x18 */ LONG m_lConeOutsideVolume; /* Cached outside volume; never read. */
};

#endif /* _APIMAPPER_H */
