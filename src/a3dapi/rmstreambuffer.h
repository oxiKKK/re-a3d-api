/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * rmstreambuffer.h
 *
 * Declares ResManStreamBuffer, the resource-manager playback object with
 * shared CWaveForm storage and a replaceable DAL binding. It tracks
 * source and seek positions, looping, priority, audibility and playback
 * notifications.
 *
 * The service thread uses these records to fill the assigned device
 * buffer and preserve source progress. rmstreambuffer.cpp implements
 * transport and callback behavior, with shared controls in ResManBuffer
 * and device queue handling in DalBufferInfo.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _RMSTREAMBUFFER_H
#define _RMSTREAMBUFFER_H

#include "A3dPrivate.h"
#include "rmbuffer.h"
#include "resman.h"
#include "a2dbuffer.h"

/* No deferred seek. */
#define RESMANSTREAMBUFFER_NO_PENDING_SEEK (-1)

class CWaveForm;

/* =============================================================
// Class: ResManStreamBuffer
//
// Description: Resource-manager stream with waveform storage and DAL binding.
//
// Size: 0x8EC
//
// (RE) Constructor: dbg:0x100703f0
// =============================================================*/

/* (RE) Vtables: primary dbg:0x10138900, 33 slots;
 * IResManBuffer at 0x08: dbg:0x10138898, 12 slots.
 */

class ResManStreamBuffer : public ResManBuffer
{
public:
	ResManStreamBuffer(ResMan *pResMan, int nResourceManagerMode);
	/* Primary slot 21; the compiler supplies the deleting destructor. */
	virtual ~ResManStreamBuffer(void);

	/* IResManBuffer at 0x08: GetPriority slot 4, SetPriority slot 6. */
	STDMETHODIMP GetPriority(FLOAT *pfPriority);
	STDMETHODIMP SetPriority(FLOAT fPriority);

	/* IDirectSoundBuffer at 0x00, slots 0..20. */
	STDMETHODIMP         QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG) AddRef(void);
	STDMETHODIMP_(ULONG) Release(void);

	STDMETHODIMP GetCaps(LPDSBCAPS lpDSBufferCaps);                    /* slot 3 */
	STDMETHODIMP GetCurrentPosition(DWORD *pdwPlay, DWORD *pdwWrite);  /* slot 4 */
	STDMETHODIMP GetFormat(LPWAVEFORMATEX pwfx, DWORD cb, DWORD *pcb); /* slot 5 */
	STDMETHODIMP GetVolume(LONG *plVolume);                            /* slot 6 */
	STDMETHODIMP GetPan(LONG *plPan);                                  /* slot 7 */
	STDMETHODIMP GetFrequency(DWORD *pdwFrequency);                    /* slot 8 */
	STDMETHODIMP GetStatus(DWORD *pdwStatus);                          /* slot 9 */
	STDMETHODIMP Initialize(LPDIRECTSOUND pDs, LPCDSBUFFERDESC pDesc); /* slot 10 */
	STDMETHODIMP SetFormat(LPCWAVEFORMATEX pwfx);                      /* slot 14 */
	STDMETHODIMP SetVolume(LONG lVolume);                              /* slot 15 */
	STDMETHODIMP SetPan(LONG lPan);                                    /* slot 16 */
	STDMETHODIMP SetFrequency(DWORD dwFrequency);                      /* slot 17 */
	STDMETHODIMP Unlock(LPVOID pvAudioPtr1, DWORD dwAudioBytes1,
	                    LPVOID pvAudioPtr2, DWORD dwAudioBytes2); /* slot 19 */
	STDMETHODIMP Restore(void); /* slot 20 */

	/* IA3dDalBuffer at 0x04, slots 4..8; slot 3 inherited. */
	STDMETHODIMP GetAllocationStatus(LPDWORD lpdwStatus); /* slot 4 */
	STDMETHODIMP GetWave(LPBYTE *lplpWave);               /* slot 5 */
	STDMETHODIMP GetDriverInfo(void **lplpIDsDriverBuffer,
	                           void **lplpIA3dDriverBuffer);  /* slot 6 */
	STDMETHODIMP SetNewBuffer(LPBYTE lpBuffer, DWORD dwBufferBytes); /* slot 7 */
	STDMETHODIMP SetA3dDirectCtrl(LPA3DCTRL_SRC_SUPER lpA3dCtrlDirect,
	                              DWORD dwSize); /* slot 8 */

	/* IResManBuffer at 0x08; slot 11 inherited. */
	STDMETHODIMP GetStatusEx(LPDWORD lpdwStatusEx); /* slot 3 */
	STDMETHODIMP MuteForFocusLoss(void);            /* slot 7 */
	STDMETHODIMP UnMuteForFocusGain(void);          /* slot 8 */

	/* IDirectSoundNotify at 0x0C, slot 3. */
	STDMETHODIMP SetNotificationPositions(DWORD cPositions,
	                                      LPCDSBPOSITIONNOTIFY pcPositionNotifies);

	/* IA3dPropertySet at 0x10, slot 3. */
	STDMETHODIMP QuerySupport(REFGUID rguidPropSet, ULONG ulId,
	                          PULONG pulTypeSupport);

	/* Primary slots 22..29: __thiscall except slot 29 (__stdcall). */
	HRESULT      AttachDalBufferInfo(DalBufferInfo *lpDalBufferInfo);  /* slot 23 */
	HRESULT      Duplicate(LPDIRECTSOUNDBUFFER lpDSB);                 /* slot 24 */
	HRESULT      SetDalBufferInfoBare(DalBufferInfo *lpDalBufferInfo); /* slot 26 */
	HRESULT      GetA3dCtrlSuper(LPVOID *lplpCtrl);                    /* slot 27 */
	STDMETHODIMP Unknown_0x74(DWORD dwArg);                            /* slot 29 */

	/* IDirectSoundBuffer, on the subobject at +0x00. */
	/* slot 13 */
	STDMETHODIMP SetCurrentPosition(DWORD dwPlay);
	/* Slot 11. */
	STDMETHODIMP Lock(DWORD dwWriteCursor, DWORD dwWriteBytes,
	                  LPVOID *ppvAudioPtr1, LPDWORD pdwAudioBytes1,
	                  LPVOID *ppvAudioPtr2, LPDWORD pdwAudioBytes2,
	                  DWORD dwFlags);
	/* slot 12 */
	STDMETHODIMP Play(DWORD dwReserved1, DWORD dwPriority, DWORD dwFlags);

	/* IDirectSoundBuffer slot 18. */
	STDMETHODIMP Stop(void);

	/* Primary slot 22. */
	HRESULT Init(const DSBUFFERDESC1 *lpDSBdesc);

	/* IResManBuffer slot 9. */
	STDMETHODIMP SetRenderMode(DWORD dwRequested);

	/* IResManBuffer slots 5 and 10; primary slot 25. */
	STDMETHODIMP GetBufferState(LPDWORD lpdwBufferState);             /* slot 5 */
	STDMETHODIMP GetRenderMode(LPDWORD lpdwRenderMode);               /* slot 10 */
	HRESULT      GetDalBufferInfo(DalBufferInfo **lplpDalBufferInfo); /* slot 25 */

	/* IA3dPropertySet at 0x10, slots 4 and 5. */
	STDMETHODIMP Get(REFGUID rguidPropSet, ULONG ulId,
	                 LPVOID pInstanceData, ULONG cbInstanceData,
	                 LPVOID pPropertyData, ULONG cbPropertyData,
	                 PULONG pulBytesReturned);
	STDMETHODIMP Set(REFGUID rguidPropSet, ULONG ulId,
	                 LPVOID pInstanceData, ULONG cbInstanceData,
	                 LPVOID pPropertyData, ULONG cbPropertyData,
	                 DWORD dwFlags);

	/* Primary slot 28. */
	HRESULT Tick(int nWhen);

	HRESULT GetFillPosition(DWORD *pdwFillPos,
	                        DWORD *pdwFillPosCopy);

	void RecomputeSortKey(void);

	/* (RE) dbg:0x10066fe0 */
	FLOAT GetSortKey(void) { return (m_fSortKey); }

	/* (RE) dbg:0x10067010 */
	DWORD GetRepeatCount(void) { return (m_dwRepeatCount); }

	void SetRepeatGate(DWORD dwGate);

public:
	/* Interlocked client reference count. */
	/* 0x868 */ LONG      m_cRef;
	/* Owned waveform reference; shared by duplicates. */
	/* 0x86C */ CWaveForm *m_pWaveForm;
	/* Creation mode  */
	/* 0x870 */ int       m_nResourceManagerMode;

	/* Local description and format; lpwfxFormat points to m_wfxFormat. */
	/* 0x874 */ DSBUFFERDESC1 m_DSBufferDesc; /* 0x14 */
	/* 0x888 */ WAVEFORMATEX  m_wfxFormat;    /* 0x14 with tail pad */

	/* Volume in hundredths of a decibel, [-10000,0]. */
	/* 0x89C */ LONG                m_lVolume;
	/* Pan in hundredths of a decibel, [-10000,10000]. */
	/* 0x8A0 */ LONG                m_lPan;
	/* Source sample rate, Hz; restored by SetFrequency(0). */
	/* 0x8A4 */ DWORD               m_dwOriginalSampleRate;
	/* Playback frequency, Hz; [100,100000]. */
	/* 0x8A8 */ DWORD               m_dwFrequency;
	/* Source byte position advanced by DAL fills. */
	/* 0x8AC */ DWORD               m_dwPosition;
	/* Deferred seek, bytes; -1 means none. */
	/* 0x8B0 */ int                 m_dwTargetPosition;
	/* A3DVOICE_STATE_* lifecycle state. */
	/* 0x8B4 */ DWORD               m_dwState;
	/* Focus mute, looping, stereo and 16-bit format flags. */
	/* 0x8B8 */ DWORD               m_dwFlags;
	/* Notification entry count. */
	/* 0x8BC */ DWORD               m_cNotify;
	/* Caller-owned notification array. */
	/* 0x8C0 */ LPDSBPOSITIONNOTIFY m_paNotify;
	/* Owned notification mutex. */
	/* 0x8C4 */ HANDLE              m_hNotifyMutex;
	/* Previous notification cursor, bytes. */
	/* 0x8C8 */ DWORD               m_dwCurrentPos;
	/* Caller priority, [0,1]. */
	/* 0x8CC */ FLOAT               m_fPriority;
	/* Selected render mode; 1 reports DSBCAPS_CTRL3D. */
	/* 0x8D0 */ DWORD               m_dwRenderMode;
	/* Priority/audibility sort key. */
	/* 0x8D4 */ FLOAT               m_fSortKey;
	/* Fill-pass count used to break priority ties; cleared by Stop. */
	/* 0x8D8 */ DWORD               m_dwRepeatCount;
	/* Enables the fill-pass count. */
	/* 0x8DC */ DWORD               m_dwRepeatGate;
	/* Source playback cursor derived from queued DAL bytes. */
	/* 0x8E0 */ DWORD               m_dwFillPos;
	/* Copy of the fill cursor. */
	/* 0x8E4 */ DWORD               m_dwFillPosCopy;
	/* Nonzero for a bound hardware DAL; gates property readback. */
	/* 0x8E8 */ DWORD               m_dwHardwareBound;
};

typedef int ResManStreamBufferSizeCheck[(sizeof(ResManStreamBuffer) == 0x8EC) ? 1 : -1];

#endif /* _RMSTREAMBUFFER_H */
