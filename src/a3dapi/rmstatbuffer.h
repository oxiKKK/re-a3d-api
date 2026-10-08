/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * rmstatbuffer.h
 *
 * Declares ResManStatBuffer, the resource-manager object for playback
 * backed by a retained DAL buffer. It combines DirectSound and internal
 * buffer interfaces with the shared ResManBuffer control and property
 * state.
 *
 * The class records its buffer description, format, playback state,
 * render mode and notifications. rmstatbuffer.cpp forwards playback to
 * the binding and handles service callbacks; ResMan coordinates
 * allocation and collection.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _RMSTATBUFFER_H
#define _RMSTATBUFFER_H

#include "A3dPrivate.h"
#include "rmbuffer.h"
#include "resman.h"
#include "a2dbuffer.h"
#include "outqueue.h"

/* =============================================================
// Class: ResManStatBuffer
//
// Description: Resource manager static buffer backed by a DAL buffer.
//
// Size: 0x8D0
//
// (RE) Constructor: dbg:0x1006d0d0
// =============================================================*/

/* (RE) Vtables:
 * 0x00 IResManBufferPrimary dbg:0x10137B0C, 33 slots.
 * 0x04 IA3dDalBuffer        dbg:0x10137AE0, 9 slots.
 * 0x08 IResManBuffer        dbg:0x10137AA4, 12 slots.
 * 0x0C IDirectSoundNotify   dbg:0x10137A90, 4 slots.
 * 0x10 IA3dPropertySet      dbg:0x10137A6C, 7 slots.
 */

class ResManStatBuffer : public ResManBuffer
{
public:
	ResManStatBuffer(ResMan *pResMan, int nResourceManagerMode);
	/* Primary slot 21; the compiler supplies the deleting destructor. */
	virtual ~ResManStatBuffer(void);

	/* IDirectSoundBuffer at 0x00, slots 0 to 20. */
	STDMETHODIMP		QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG)	AddRef(void);
	STDMETHODIMP_(ULONG)	Release(void);

	STDMETHODIMP	GetCaps(LPDSBCAPS lpDSBufferCaps);
	STDMETHODIMP	GetCurrentPosition(DWORD *pdwPlay, DWORD *pdwWrite);
	STDMETHODIMP	GetFormat(LPWAVEFORMATEX pwfx, DWORD cb, DWORD *pcb);
	STDMETHODIMP	GetVolume(LONG *plVolume);
	STDMETHODIMP	GetPan(LONG *plPan);
	STDMETHODIMP	GetFrequency(DWORD *pdwFrequency);
	STDMETHODIMP	GetStatus(DWORD *pdwStatus);
	STDMETHODIMP	Initialize(LPDIRECTSOUND pDs, LPCDSBUFFERDESC pDesc);
	STDMETHODIMP	Lock(DWORD dwOffset, DWORD dwWriteBytes,
			     LPVOID *ppvAudioPtr1, LPDWORD pdwAudioBytes1,
			     LPVOID *ppvAudioPtr2, LPDWORD pdwAudioBytes2,
			     DWORD dwFlags);
	STDMETHODIMP	Play(DWORD dwReserved1, DWORD dwPriority, DWORD dwFlags);
	STDMETHODIMP	SetCurrentPosition(DWORD dwPlay);
	STDMETHODIMP	SetFormat(LPCWAVEFORMATEX pwfx);
	STDMETHODIMP	SetVolume(LONG lVolume);
	STDMETHODIMP	SetPan(LONG lPan);
	STDMETHODIMP	SetFrequency(DWORD dwFrequency);
	STDMETHODIMP	Stop(void);
	STDMETHODIMP	Unlock(LPVOID pvAudioPtr1, DWORD dwAudioBytes1,
			       LPVOID pvAudioPtr2, DWORD dwAudioBytes2);
	STDMETHODIMP	Restore(void);

	/* IA3dDalBuffer at 0x04, slots 4 to 8. */
	STDMETHODIMP	GetAllocationStatus(LPDWORD lpdwStatus);
	STDMETHODIMP	GetWave(LPBYTE *lplpWave);
	STDMETHODIMP	GetDriverInfo(void **lplpIDsDriverBuffer,
				      void **lplpIA3dDriverBuffer);
	STDMETHODIMP	SetNewBuffer(LPBYTE lpbWave, DWORD dwBufferBytes);
	STDMETHODIMP	SetA3dDirectCtrl(LPA3DCTRL_SRC_SUPER lpA3dCtrlDirect,
					 DWORD dwSize);

	/* IResManBuffer at 0x08, slots 3 to 10. */
	STDMETHODIMP	GetStatusEx(LPDWORD lpdwStatusEx);	/* slot 3 */
	STDMETHODIMP	GetPriority(FLOAT *pfPriority);		/* slot 4 */
	STDMETHODIMP	GetBufferState(LPDWORD lpdwBufferState);/* slot 5 */
	STDMETHODIMP	SetPriority(FLOAT fPriority);		/* slot 6 */
	STDMETHODIMP	MuteForFocusLoss(void);			/* slot 7 */
	STDMETHODIMP	UnMuteForFocusGain(void);			/* slot 8 */
	STDMETHODIMP	SetRenderMode(DWORD dwRenderMode);	/* slot 9 */
	STDMETHODIMP	GetRenderMode(LPDWORD lpdwRenderMode);	/* slot 10 */

	/* IDirectSoundNotify at 0x0C, slot 3. */
	STDMETHODIMP	SetNotificationPositions(DWORD cPositions,
						 LPCDSBPOSITIONNOTIFY pcPositionNotifies);

	/* Primary slots 22 to 29: __thiscall except slot 29 (__stdcall). */
	HRESULT		Init(const DSBUFFERDESC1 *lpDSBdesc);		/* slot 22 */
	HRESULT		AttachDalBufferInfo(DalBufferInfo *lpDalBufferInfo);	/* slot 23 */
	HRESULT		Duplicate(LPDIRECTSOUNDBUFFER lpDSB);		/* slot 24 */
	HRESULT		GetDalBufferInfo(DalBufferInfo **lplpDalBufferInfo);	/* slot 25 */
	HRESULT		SetDalBufferInfoBare(DalBufferInfo *lpDalBufferInfo);	/* slot 26 */
	HRESULT		GetA3dCtrlSuper(LPVOID *lplpCtrl);				/* slot 27 */
	HRESULT		Tick(int nWhen);					/* slot 28 */
	STDMETHODIMP	Unknown_0x74(DWORD dwArg);					/* slot 29 */

public:
	/* 0x868 */ LONG	m_cRef;		/* Interlocked client reference count. */
	/* 0x86C */ DWORD	m_Unknown_0x86C;
	/* Creation-time resource-manager mode  */
	/* 0x870 */ int		m_nResourceManagerMode;

	/* Local description and format; lpwfxFormat points to m_wfxFormat. */
	/* 0x874 */ DSBUFFERDESC1	m_DSBufferDesc;		/* 0x14 */
	/* 0x888 */ WAVEFORMATEX	m_wfxFormat;		/* 0x14 with tail pad */

	/* 0x89C */ DWORD	m_Unknown_0x89C;
	/* 0x8A0 */ DWORD	m_Unknown_0x8A0;

	/* 0x8A4 */ DWORD	m_dwOriginalSampleRate;	/* Source format sample rate, Hz. */
	/* Cached format frequency, Hz; SetFrequency only updates the DAL. */
	/* 0x8A8 */ DWORD	m_dwFrequency;
	/* 0x8AC */ DWORD	m_Unknown_0x8AC;
	/* 0x8B0 */ DWORD	m_Unknown_0x8B0;

	/* 0x8B4 */ DWORD	m_dwBufferState;	/* A3DVOICE_STATE_* lifecycle state. */

	/* 0x8B8 */ DWORD	m_dwVoiceFlags;	/* Focus mute, stereo and 16-bit format flags. */

	/* 0x8BC */ DWORD	m_cNotify;	/* Number of notification entries. */
	/* 0x8C0 */ LPDSBPOSITIONNOTIFY	m_paNotify;	/* Caller-owned array. */
	/* 0x8C4 */ HANDLE	m_hNotifyMutex;	/* Serializes notification updates and signaling. */

	/* Previous notification cursor or explicit seek position, bytes. */
	/* 0x8C8 */ DWORD	m_dwCurrentPos;

	/* Init selects 1 for 3D, 2 otherwise, or 4 for supported four-channel output. */
	/* 0x8CC */ DWORD	m_dwRenderMode;
};

typedef int ResManStatBufferSizeCheck[(sizeof(ResManStatBuffer) == 0x8D0) ? 1 : -1];

#endif /* _RMSTATBUFFER_H */
