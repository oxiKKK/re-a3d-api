/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * d3dbuffer.h
 *
 * Declares D3DBuffer and CReflection for the hardware DirectSound3D
 * backend. The voice stores its DirectSound and DS3D interfaces, optional
 * A3D property access, source controls and reflection list.
 *
 * Each reflection tracks a duplicated playback buffer and the timing
 * state needed to follow the source at a delay. d3dbuffer.cpp implements
 * these controls; DAL_D3D owns the voice list and schedules reflection
 * processing.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _D3DBUFFER_H
#define _D3DBUFFER_H

#include "A3dPrivate.h"
#include "LinkList.h"
#include "d2dbuffer.h"

class DAL_D3D;

class CReflection;

/* =============================================================
// Class: D3DBuffer
//
// Description: DirectSound3D voice with A3D properties and reflection buffers.
//
// Size: 0x4C0
//
// (RE) Constructor: rtl:0x1001c160; dbg:0x100440b0
// =============================================================*/

/* (RE) Vtables:
 * 0x00 IDirectSoundBuffer dbg:0x1012f194 rtl:0x100537c4, 22 slots.
 * 0x04 IA3dDalBuffer      dbg:0x1012f168 rtl:0x100537a0, 9 slots.
 * 0x08 IResManBuffer      dbg:0x1012f12c rtl:0x10053770, 12 slots.
 * 0x0C IKsPropertySet     dbg:0x1012f110 rtl:0x10053758, 6 slots.
 */

class D3DBuffer : public IDirectSoundBuffer,
		  public IA3dDalBuffer,
		  public IResManBuffer,
		  public IKsPropertySet
{
public:
	D3DBuffer(void);
	~D3DBuffer(void);

	/* IUnknown, one set shared by the four vtables. */

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

	/* Slot 21: operation unresolved. */
	virtual STDMETHODIMP	Unknown_0x54(DWORD, DWORD, DWORD);

	/* IA3dDalBuffer, in ia3ddal.h slot order. */

	STDMETHODIMP	SetA3dSuperCtrl(LPA3DCTRL_SRC_SUPER lpA3dCtrlSuper,
					DWORD dwSize);
	STDMETHODIMP	GetAllocationStatus(LPDWORD lpdwStatus);
	STDMETHODIMP	GetWave(LPBYTE *lplpWave);
	STDMETHODIMP	GetDriverInfo(void **lplpIA3dDriverBuffer,
				      void **lplpIDsDriverBuffer);
	STDMETHODIMP	SetNewBuffer(LPBYTE lpBuffer, DWORD dwBufferBytes);
	STDMETHODIMP	SetA3dDirectCtrl(LPA3DCTRL_SRC_SUPER lpA3dCtrlDirect,
					 DWORD dwSize);

	/* IResManBuffer, in the order d2dbuffer.h declares. */

	STDMETHODIMP	GetStatusEx(LPDWORD lpdwStatusEx);
	STDMETHODIMP	GetPriority(FLOAT *pfPriority);
	STDMETHODIMP	GetBufferState(LPDWORD lpdwBufferState);
	STDMETHODIMP	SetPriority(FLOAT fPriority);
	STDMETHODIMP	Enable(void);
	STDMETHODIMP	Disable(void);
	STDMETHODIMP	SetNativeModeDisabled(DWORD);
	STDMETHODIMP	Unknown_0x28(DWORD);
	STDMETHODIMP	GetControlBufferPair(DWORD);

	/* IKsPropertySet, in dsound.h slot order. */

	STDMETHODIMP	Get(REFGUID rguidPropSet, ULONG ulId, LPVOID pInstanceData,
			    ULONG ulInstanceLength, LPVOID pPropertyData,
			    ULONG ulDataLength, PULONG pulBytesReturned);
	STDMETHODIMP	Set(REFGUID rguidPropSet, ULONG ulId, LPVOID pInstanceData,
			    ULONG ulInstanceLength, LPVOID pPropertyData,
			    ULONG ulDataLength);
	STDMETHODIMP	QuerySupport(REFGUID rguidPropSet, ULONG ulId,
				     PULONG pulTypeSupport);

	HRESULT		Init(IDirectSoundBuffer *pDSBuffer, const DSBUFFERDESC1 *pDesc,
			     CList *pList, HANDLE hBufferListMutex, DAL_D3D *pDevice);
	HRESULT		InitDuplicate(LPDIRECTSOUNDBUFFER lpOriginalBuffer,
				      LPDIRECTSOUNDBUFFER lpDuplicateSoundBuffer);

	HRESULT		PositionSound(const float *pLeftDir, const float *pRightDir);
	HRESULT		GainToDSVolume(float fLeftGain, float fRightGain);

	HRESULT		AdvanceReflectionAge(void);
	BOOL		AgeReflectionVoices(DWORD dwBudget, LPDWORD lpdwAged);
	HRESULT		PushReflectionSuperCtrl(LPA3DCTRL_SRC_SUPER lpA3dCtrl);

public:
	/* 0x10 */ WAVEFORMATEX		m_WaveFormatEx;	/* Embedded format referenced by m_DSBufferDesc. */
	/* 0x24 */ DSBUFFERDESC1		m_DSBufferDesc;	/* Copied source descriptor. */
	/* 0x38 */ HANDLE		m_hReflectionMutex;	/* Owned mutex guarding reflection slots. */
	/* 0x3C */ LONG			m_cRef;	/* Initially 1. */
	/* 0x40 */ FLOAT		m_fPriority;	/* 0..1. */
	/* 0x44 */ DWORD		m_dwBufferState;	/* Reported resource-manager state. */
	/* (RE) Accessor dbg:0x1004f0f0 is not declared; callers read this field. */
	/* 0x48 */ IDirectSoundBuffer	*m_lpDirectSoundBuffer;	/* Owned buffer reference. */
	/* 0x4C */ LPDIRECTSOUND3DBUFFER	m_lpDS3DBuffer;	/* Owned DS3D interface reference. */
	/* 0x50 */ A3DCTRL_SRC_SUPER	m_A3dCtrlSuper;	/* Last source control block. */
	/* 0x45C */ CList		*m_pList;	/* Device-owned buffer list. */
	/* 0x460 */ IKsPropertySet	*m_lpPropertySet;	/* Optional interface queried from the DS3D buffer. */
	/* 0x464 */ DWORD		m_fPropSetA3d;	/* A3D super-control property support. */
	/* 0x468 */ HANDLE		m_hBufferListMutex;	/* Device-owned list mutex. */
	/* 0x46C */ DAL_D3D		*m_lpDalD3D;	/* Owning device; borrowed pointer. */
	/* 0x470 */ FLOAT		m_fReflectionAge;	/* Seconds since Play; advances 0.01 per tick. */
	/* 0x474 */ DWORD		m_cReflectionCount;	/* Number of live reflection entries. */
	/* 0x478 */ CReflection		*m_apReflection[A3D_MAX_SOURCE_REFLECTIONS];	/* Owned reflection objects. */
	/* 0x4B8 */ DWORD		m_bIsReflection;	/* Voice created as a reflection. */
	/* 0x4BC */ DWORD		m_fReflectionsAged;	/* Skip this voice until the aging pass restarts. */
};

typedef int D3DBufferSizeCheck[(sizeof(D3DBuffer) == 0x4C0) ? 1 : -1];

/* =============================================================
// Class: CReflection
//
// Description: Duplicate voice with delay and pitch synchronization to its
// source.
//
// Size: 0x48
//
// (RE) Constructor: dbg:0x10047a60
// =============================================================*/

/* (RE) Scalar deleting destructor thunk: dbg:0x10003594. */

class CReflection
{
public:
	CReflection(void);
	~CReflection(void);

	void		Reset(void);
	HRESULT		Init(D3DBuffer *pSource, DAL_D3D *pDevice);
	HRESULT		Update(const A3DCTRL_SRC_SUPER *pSrc, int nReflection,
			       float fFrequency, float fAge);
	HRESULT		Start(DWORD dwFlags, float fAge);
	HRESULT		Stop(void);
	void		AgeStep(LPDWORD lpfAged);
	void		ClearAged(void);
	HRESULT		Age(void);
	HRESULT		Sync(void);

public:
	/* 0x00 */ DWORD	m_dwStartFlags;	/* Playback flags reused on synchronization. */
	/* 0x04 */ DWORD	m_fStarted;	/* Waiting for the initial delay. */
	/* 0x08 */ DWORD	m_fSynced;	/* Cursor synchronized to the source delay. */
	/* 0x0C */ DWORD	m_fPitchSettled;	/* Pitch restored to 1.0. */
	/* 0x10 */ FLOAT	m_fCurrentDelay;	/* Seconds remaining before delayed playback. */
	/* 0x14 */ FLOAT	m_fTargetDelay;	/* Seconds; mean of the reflection ear delays. */
	/* 0x18 */ FLOAT	m_fFrequency;	/* Source playback frequency, Hz. */
	/* 0x1C */ FLOAT	m_fFreqFactor;	/* Source frequency multiplier. */
	/* 0x20 */ DWORD	m_fApplyPending;	/* Restore muted gains when playback starts. */
	/* 0x24 */ D3DBuffer	*m_pReflectionBuffer;	/* Owned reflection voice. */
	/* 0x28 */ D3DBuffer	*m_pSource;	/* Borrowed source voice. */
	/* 0x2C */ HANDLE	m_hMutex;	/* Shared source reflection mutex. */
	/* 0x30 */ DWORD	m_dwBytesInBuffer;	/* Source buffer length, bytes. */
	/* 0x34 */ DWORD	m_nBytesPerSample;	/* Source block alignment, bytes. */
	/* 0x38 */ D3DBuffer	*m_pReflectionDal;	/* Same voice as m_pReflectionBuffer; DAL operations. */
	/* 0x3C */ LPA3DCTRL_SRC_SUPER	m_pA3dCtrl;	/* Owned reflection control block. */
	/* 0x40 */ FLOAT	m_fPitch;	/* Pitch correction: 0.99, 1.0 or 1.01. */
	/* 0x44 */ DWORD	m_fAgedThisTick;	/* Already processed in this aging pass. */
};

typedef int CReflectionSizeCheck[(sizeof(CReflection) == 0x48) ? 1 : -1];

#endif /* _D3DBUFFER_H */
