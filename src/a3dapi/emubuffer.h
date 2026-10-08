/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * emubuffer.h
 *
 * Declares EMUBuffer, the timed playback-state implementation used by
 * DAL_EMU. Its interfaces match the buffer operations expected by
 * DirectSound, the DAL and the resource manager, while its state stores
 * format, cursor timing and control values.
 *
 * The class has no waveform allocation for rendered audio. emubuffer.cpp
 * implements the elapsed-time cursor and stored controls, and dal_emu.cpp
 * manages the device's voice list.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _EMUBUFFER_H
#define _EMUBUFFER_H

#include "A3dPrivate.h"
#include "LinkList.h"
#include "d2dbuffer.h"

/* =============================================================
// Class: EMUBuffer
//
// Description: Silent voice with timed playback and resource manager controls.
//
// Size: 0x47C
//
// (RE) Constructor: rtl:0x10022720; dbg:0x10058490
// =============================================================*/

/* (RE) Vtables:
   0x00 IDirectSoundBuffer: dbg:0x101331A8; slots 0-20.
   0x04 IA3dDalBuffer: dbg:0x1013317C; slots 0-8.
   0x08 IResManBuffer: dbg:0x10133140; slots 0-11.
   0x0C IDirectSoundNotify: dbg:0x1013312C; slots 0-3.
   0x10 IKsPropertySet: dbg:0x10133110; slots 0-5. */

class EMUBuffer : public IDirectSoundBuffer,
		  public IA3dDalBuffer,
		  public IResManBuffer,
		  public IDirectSoundNotify,
		  public IKsPropertySet
{
public:
	EMUBuffer(void);
	~EMUBuffer(void);

	/* IUnknown, one set shared by the five vtables. */

	STDMETHODIMP		QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG)	AddRef(void);
	STDMETHODIMP_(ULONG)	Release(void);

	/* IDirectSoundBuffer, in vtable order. */

	STDMETHODIMP	GetCaps(LPDSBCAPS lpDSBufferCaps);
	STDMETHODIMP	GetCurrentPosition(LPDWORD pdwCurrentPlayCursor,
					   LPDWORD pdwCurrentWriteCursor);
	STDMETHODIMP	GetFormat(LPWAVEFORMATEX lpwfxFormat, DWORD dwSizeAllocated,
				  LPDWORD lpdwSizeWritten);
	STDMETHODIMP	GetVolume(LPLONG lplVolume);
	STDMETHODIMP	GetPan(LPLONG lplPan);
	STDMETHODIMP	GetFrequency(LPDWORD lpdwFrequency);
	STDMETHODIMP	GetStatus(LPDWORD lpdwStatus);
	STDMETHODIMP	Initialize(LPDIRECTSOUND lpDirectSound,
				   LPCDSBUFFERDESC lpcDSBufferDesc);
	STDMETHODIMP	Lock(DWORD dwOffset, DWORD dwWriteBytes,
			     LPVOID *lplpvAudioPtr1, LPDWORD lpdwAudioBytes1,
			     LPVOID *lplpvAudioPtr2, LPDWORD lpdwAudioBytes2,
			     DWORD dwFlags);
	STDMETHODIMP	Play(DWORD dwReserved1, DWORD dwReserved2, DWORD dwFlags);
	STDMETHODIMP	SetCurrentPosition(DWORD dwNewPosition);
	STDMETHODIMP	SetFormat(LPCWAVEFORMATEX lpcfxFormat);
	STDMETHODIMP	SetVolume(LONG lVolume);
	STDMETHODIMP	SetPan(LONG lPan);
	STDMETHODIMP	SetFrequency(DWORD dwFrequency);
	STDMETHODIMP	Stop(void);
	STDMETHODIMP	Unlock(LPVOID lpvAudioPtr1, DWORD dwAudioBytes1,
			       LPVOID lpvAudioPtr2, DWORD dwAudioBytes2);
	STDMETHODIMP	Restore(void);

	/* IA3dDalBuffer, in ia3ddal.h slot order. */

	STDMETHODIMP	SetA3dSuperCtrl(LPA3DCTRL_SRC_SUPER lpA3dCtrlSuper,
					DWORD dwSize);
	STDMETHODIMP	GetAllocationStatus(LPDWORD lpdwStatus);
	STDMETHODIMP	GetWave(LPBYTE *lplpbWave);
	STDMETHODIMP	GetDriverInfo(void **lplpIDsDriverBuffer,
				      void **lplpIA3dDriverBuffer);
	STDMETHODIMP	SetNewBuffer(LPBYTE lpbWave, DWORD dwBufferBytes);
	STDMETHODIMP	SetA3dDirectCtrl(LPA3DCTRL_SRC_SUPER lpA3dCtrlDirect,
					 DWORD dwSize);

	/* IResManBuffer, in the order d2dbuffer.h declares the table. */

	/* 3 */ STDMETHODIMP	GetStatusEx(LPDWORD lpdwStatusEx);
	/* 4 */ STDMETHODIMP	GetPriority(FLOAT *pfPriority);
	/* 5 */ STDMETHODIMP	GetBufferState(LPDWORD lpdwBufferState);
	/* 6 */ STDMETHODIMP	SetPriority(FLOAT fPriority);
	/* 7 */ STDMETHODIMP	Enable(void);
	/* 8 */ STDMETHODIMP	Disable(void);
	/* 9 */ STDMETHODIMP	SetNativeModeDisabled(DWORD);
	/* 10 */ STDMETHODIMP	Unknown_0x28(DWORD);
	/* 11 */ STDMETHODIMP	GetControlBufferPair(DWORD);

	/* IDirectSoundNotify. */

	STDMETHODIMP	SetNotificationPositions(DWORD cPositions,
						 LPCDSBPOSITIONNOTIFY pcPositionNotifies);

	/* IKsPropertySet, in dsound.h slot order. */

	STDMETHODIMP	Get(REFGUID rguidPropSet, ULONG ulId, LPVOID pInstanceData,
			    ULONG ulInstanceLength, LPVOID pPropertyData,
			    ULONG ulDataLength, PULONG pulBytesReturned);
	STDMETHODIMP	Set(REFGUID rguidPropSet, ULONG ulId, LPVOID pInstanceData,
			    ULONG ulInstanceLength, LPVOID pPropertyData,
			    ULONG ulDataLength);
	STDMETHODIMP	QuerySupport(REFGUID rguidPropSet, ULONG ulId,
				     PULONG pulTypeSupport);

	HRESULT		Init(const DSBUFFERDESC1 *pDesc, CList *pList);

public:
	/* 0x14 */ LONG			m_cRef;			/* starts at 1 */
	/* 0x18 */ DWORD		m_dwFlags;		/* the A3DVOICE_FLAG_* bits */
	/* 0x1C */ LONG			m_lVolume;
	/* 0x20 */ LONG			m_lPan;
	/* 0x24 */ DWORD		m_dwSampleRate;		/* Original sample rate, Hz. */
	/* 0x28 */ DWORD		m_dwFrequency;		/* Playback rate, Hz. */
	/* 0x2C */ DWORD		m_dwBufferBytes; /* Playback length, bytes. */
	/* 0x30 */ DWORD		m_dwPlayCursor; /* Byte offset. */
	/* 0x34 */ FLOAT		m_fPriority;		/* 0..1. */
	/* 0x38 */ DWORD		m_dwState;		/* the A3DVOICE_STATE_* word; starts at 2 */
	/* 0x3C */ DWORD		m_dwStartTick;		/* Initial tick, ms. */
	/* 0x40 */ DWORD		m_dwLastTick;		/* Last cursor update, ms. */
	/* 0x44 */ DSBUFFERDESC1		m_desc;			/* Descriptor with embedded format pointer. */
	/* 0x58 */ WAVEFORMATEX		m_wfx;			/* Stored PCM format. */
	/* 0x6C */ A3DCTRL_SRC_SUPER	m_A3dCtrlSuper;		/* Source controls; size 0x40C. */
	/* 0x478 */ CList		*m_pList;		/* Device-owned buffer list. */
};

typedef int EMUBufferSizeCheck[(sizeof(EMUBuffer) == 0x47C) ? 1 : -1];

#endif /* _EMUBUFFER_H */
