/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * dsbuffer.h
 *
 * Declares CA3dSecondaryBuffer and its volume/pan gain record for
 * a3d.dll. The wrapper retains the underlying DirectSound buffer,
 * associated CA3dSource, creation format and device-list membership.
 *
 * dsbuffer.cpp implements audio access and playback forwarding and
 * translates gain changes into source commits. A3dSource.h declares the
 * separate 3D interface, and a3ddsp.h defines the engine state that
 * receives its controls.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _A3D_DSBUFFER_H
#define _A3D_DSBUFFER_H

#include "a3dprv.h"
#include "Plex.h"

class CA3d;
class CA3dSource;

/* Secondary-buffer gains; amplitudes use full scale 65535. */

typedef struct A3DGAINS
{
	LONG lGainLeft;  /* Left gain after volume and pan. */
	LONG lGainRight; /* Right gain after volume and pan. */
	LONG lVolume;    /* Hundredths of a decibel, <= 0. */
	LONG lAmplitude; /* Volume-only amplitude. */
	LONG lPan;       /* 0x10; hundredths of a decibel; SetPan leaves it unchanged. */
	LONG lPanLeft;   /* Pan-only left amplitude; zero for negative pan. */
	LONG lPanRight;  /* Pan-only right amplitude; zero for nonnegative pan. */
} A3DGAINS;				/* 0x1C = 28 */

/* =============================================================
// Class: CA3dSecondaryBuffer
//
// Description: Secondary DirectSound buffer with a separate 3D interface.
//
// Size: 0xAC
// =============================================================*/

/*
 * IDirectSoundBuffer occupies slots 0..20 at 0x00. QueryInterface also accepts
 * IA3dDalBuffer, IA3dDalBuffer2 and IA3dScaleHackBuffer, whose methods exceed
 * this table; their original interface declarations are unresolved.
 */

class CA3dSecondaryBuffer : public IDirectSoundBuffer
{
public:
	CA3dSecondaryBuffer(A3DPLEX *pPlex);
	~CA3dSecondaryBuffer(void);

	HRESULT Init(CA3d *pOwner, LPCDSBUFFERDESC pcDesc,
	             LPDIRECTSOUNDBUFFER pBuffer);
	HRESULT InitFromDuplicate(CA3d *pOwner,
	                          CA3dSecondaryBuffer *pOriginal,
	                          LPDIRECTSOUNDBUFFER pBuffer);

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
	LONG        m_cRef;
	CA3dSource *m_pSource; /* the 3D interface */
	CA3d       *m_pOwner;  /* the wrapper that made it */
	A3DPLEX    *m_pPlex;   /* &pOwner->m_SecondaryBuffers */

	DWORD m_Unknown_0x14;
	BYTE  m_Unknown_0x18[0x38 - 0x18];
	DWORD m_Unknown_0x38;

	A3DGAINS m_Gains; /* 0x3C */

	BYTE m_Unknown_0x58[0x6C - 0x58];

	LONG  m_lVolumeAlt; /* 0x6C; uninitialized alternate volume. */
	DWORD m_fVolumeAlt; /* 0x70; uninitialized alternate-volume selector. */
	DWORD m_Unknown_0x74;

	DWORD m_dwSampleRate; /* 0x78; creation sample rate in Hz. */

	DWORD m_Unknown_0x7C;
	DWORD m_Unknown_0x80;
	DWORD m_Unknown_0x84;
	DWORD m_Unknown_0x88;

	BYTE m_Unknown_0x8C[0xA4 - 0x8C];

	LPDIRECTSOUNDBUFFER m_pBuffer;    /* 0xA4; owned a3dapi.dll buffer. */
	IA3dDalBuffer      *m_pDalBuffer; /* 0xA8; owned DAL interface. */
};

#endif /* _A3D_DSBUFFER_H */
