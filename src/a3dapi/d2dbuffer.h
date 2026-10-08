/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * d2dbuffer.h
 *
 * Declares D2DBuffer and the internal resource-manager buffer interface
 * used by the DAL voice classes. D2DBuffer retains a DirectSound buffer
 * and optional property set together with format, source controls,
 * priority and device-list membership.
 *
 * DAL_D2D creates these adapters to render through ordinary DirectSound
 * volume, pan and frequency controls. The shared interface declarations
 * also connect software, DS3D and emulation voices to the resource
 * manager.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _D2DBUFFER_H
#define _D2DBUFFER_H

#include "A3dPrivate.h"
#include "LinkList.h"
#include "softmix.h"

/* Private resource manager buffer interface; 12 slots.
   (RE) D2DBuffer vtable: dbg:0x1012E7B0. Shares IID_A3dVoiceCtl with
   Slot 10's operation is unresolved. DWORD argument types are retained. */

#undef  INTERFACE
#define INTERFACE IResManBuffer

DECLARE_INTERFACE_(IResManBuffer, IUnknown)
{
	/* 0 */ STDMETHOD(QueryInterface)	(THIS_ REFIID, void **) PURE;
	/* 1 */ STDMETHOD_(ULONG, AddRef)	(THIS) PURE;
	/* 2 */ STDMETHOD_(ULONG, Release)	(THIS) PURE;

	/* 3 */ STDMETHOD(GetStatusEx)		(THIS_ LPDWORD lpdwStatusEx) PURE;
	/* 4 */ STDMETHOD(GetPriority)		(THIS_ FLOAT *pfPriority) PURE;
	/* 5 */ STDMETHOD(GetBufferState)		(THIS_ LPDWORD lpdwBufferState) PURE;
	/* 6 */ STDMETHOD(SetPriority)		(THIS_ FLOAT fPriority) PURE;
	/* 7 */ STDMETHOD(Enable)		(THIS) PURE;
	/* 8 */ STDMETHOD(Disable)		(THIS) PURE;
	/* 9 */ STDMETHOD(SetNativeModeDisabled)		(THIS_ DWORD) PURE;
	/* 10 */ STDMETHOD(Unknown_0x28)		(THIS_ DWORD) PURE;
	/* 11 */ STDMETHOD(GetControlBufferPair)		(THIS_ DWORD) PURE;
};

/* Emit A2DBuffer only with this IResManBuffer declaration. rmbuffer.h
   declares an incompatible interface of the same name; keep them in separate
   translation units. */

#define _D2DBUFFER_IRESMANBUFFER

#include "a2dbuffer.h"

/* =============================================================
// Class: D2DBuffer
//
// Description: DirectSound buffer adapter with source control storage.
//
// Size: 0x45C
//
// (RE) Constructor: rtl:0x1001b7a0; dbg:0x10041f10
// =============================================================*/

/* (RE) Vtables:
   0x00 IDirectSoundBuffer: dbg:0x1012E818; slots 0-21.
   0x04 IA3dDalBuffer: dbg:0x1012E7EC; slots 0-8.
   0x08 IResManBuffer: dbg:0x1012E7B0; slots 0-11.
   0x0C IKsPropertySet: dbg:0x1012E794; slots 0-5. */

class D2DBuffer : public IDirectSoundBuffer,
		  public IA3dDalBuffer,
		  public IResManBuffer,
		  public IKsPropertySet
{
public:
	D2DBuffer(void);
	~D2DBuffer(void);

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

	/* Primary vtable slot 21; operation unresolved. */
	virtual STDMETHODIMP	Unknown_0x54(DWORD, DWORD);

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

	/* IResManBuffer, in the order of the table declared above. */

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

	HRESULT		Init(IDirectSoundBuffer *pDSBuffer,
			     const DSBUFFERDESC1 *pDesc, CList *pList);
	HRESULT		InitDuplicate(LPDIRECTSOUNDBUFFER lpOriginalBuffer,
				      LPDIRECTSOUNDBUFFER lpDuplicateSoundBuffer);

public:
	/* 0x10 */ LONG			m_cRef;			/* starts at 1 */
	/* 0x14 */ FLOAT		m_fPriority;		/* 0..1. */
	/* 0x18 */ DWORD		m_dwBufferState;
	/* (RE) Accessor dbg:0x1004d090 is not declared; callers read this field. */
	/* 0x1C */ IDirectSoundBuffer	*m_lpDirectSoundBuffer;	/* Owned DirectSound buffer reference. */
	/* 0x20 */ DSBUFFERDESC1		m_DSBufferDesc;		/* Descriptor with embedded format pointer. */
	/* 0x34 */ WAVEFORMATEX		m_wfx;			/* Stored format. */
	/* 0x48 */ A3DCTRL_SRC_SUPER	m_A3dCtrlSuper;		/* Source controls; size 0x40C. */
	/* 0x454 */ CList		*m_pList;		/* Device-owned buffer list. */
	/* 0x458 */ IKsPropertySet	*m_lpPropertySet;	/* Optional owned property set reference. */
};

typedef int D2DBufferSizeCheck[(sizeof(D2DBuffer) == 0x45C) ? 1 : -1];

#endif /* _D2DBUFFER_H */
