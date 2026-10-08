/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * a2dbuffer.h
 *
 * Declares A2DBuffer and the private voice-control interface used by the
 * software mixer. The class combines DirectSound, DAL and
 * resource-manager interfaces with waveform, playback, resampling and
 * per-ear HRTF state.
 *
 * DAL_A2D keeps these voices in its mixing list, while CWaveForm supplies
 * audio storage that duplicates can share. The declarations also expose
 * the voice-processing helpers used by dal_a2d.cpp. Implementations are
 * split between a2dbuffer.cpp and d2dbuffer.cpp.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _A2DBUFFER_H
#define _A2DBUFFER_H

#include "A3dPrivate.h"
#include "softmix.h"
#include "hrtfmgr.h"

/* Pending-seek sentinel  */
#define A3D_A2D_NO_PENDING_SEEK (-1)

class CWaveForm;
class A2DBuffer;

/* Legacy voice control declaration used by A3dSourceTraceSkip. Its full
   3.3.677 slot contract is not established; A2DBuffer exposes IResManBuffer. */

#undef  INTERFACE
#define INTERFACE IA3dVoiceCtl

DECLARE_INTERFACE_(IA3dVoiceCtl, IUnknown)
{
	STDMETHOD(QueryInterface)	(THIS_ REFIID, void **) PURE;
	STDMETHOD_(ULONG, AddRef)	(THIS) PURE;
	STDMETHOD_(ULONG, Release)	(THIS) PURE;
	STDMETHOD(Ctl_03)		(THIS_ void *) PURE;
	STDMETHOD(Ctl_GetPosition)	(THIS_ DWORD *) PURE;
	STDMETHOD(Ctl_GetState)		(THIS_ DWORD *) PURE;
	STDMETHOD(Ctl_SetPosition)	(THIS_ DWORD) PURE;
	STDMETHOD(Ctl_Enable)		(THIS) PURE;
	STDMETHOD(Ctl_Disable)		(THIS) PURE;
};

/* Mixer gain and seconds-to-16.16-sample scales at 22050 Hz. */

#define A3D_MIX_GAIN_SCALE	49150.5
#define A3D_MIX_DELAY_SCALE	1445068800.0

/* Looping flag in A3DMIXPLAY::dwFormatBits. */

#define A3D_MIXFMT_LOOPING	0x40

void	A3dMixSeek(A3DMIXSTATE *pState, A3DMIXPLAY *pPlay, int cb);

short	A3dMixRun(A3DMIXPLAY *pPlay, float *pMix, A3DMIXSTATE *pState,
		  DWORD cSamples, int nMode);

void	A3dMixFilter(A3DMIXPLAY *pPlay, A3DMIXSTATE *pState, A3DMIXEAR *pEar,
		     DWORD cSamples, float *pOut);

#endif /* _A2DBUFFER_H */

/* =============================================================
// Class: A2DBuffer
//
// Description: Software voice with shared wave storage, resampling and HRTF
// state.
//
// Size: 0xB68
//
// (RE) Constructor: rtl:0x1001ac20; dbg:0x1003fd50
// =============================================================*/

/* (RE) Vtables:
   0x00 IDirectSoundBuffer: dbg:0x1012dd50; rtl:0x100535b4; slots 0-21.
   0x04 IA3dDalBuffer: dbg:0x1012dd24; rtl:0x10053590; slots 0-8.
   0x08 IResManBuffer: dbg:0x1012dce8; rtl:0x10053560; slots 0-11.
   0x0C IDirectSound3DListener: dbg:0x1012dc90; rtl:0x10053518; slots 0-17.
   0x10 IKsPropertySet: dbg:0x1012dc74; rtl:0x10053500; slots 0-5. */

/* (RE) Base constructors: dbg:0x1003fe90, dbg:0x1003fec0, dbg:0x1003fef0.
   Construction vtables: dbg:0x1012DDBC, dbg:0x1012DDE8, dbg:0x1012DE24. */

/* Outside the file guard to permit declaration after d2dbuffer.h; see its
   IResManBuffer include constraint. */

#if defined(_D2DBUFFER_IRESMANBUFFER) && !defined(_A2DBUFFER_CLASS_DECLARED)
#define _A2DBUFFER_CLASS_DECLARED

class A2DBuffer : public IDirectSoundBuffer,
		  public IA3dDalBuffer,
		  public IResManBuffer,
		  public IDirectSound3DListener,
		  public IKsPropertySet
{
public:
	A2DBuffer(void);
	~A2DBuffer(void);

	/* IUnknown and IDirectSoundBuffer, in slot order. */

	STDMETHODIMP		QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG)	AddRef(void);
	STDMETHODIMP_(ULONG)	Release(void);

	STDMETHODIMP	GetCaps(LPDSBCAPS lpDSBufferCaps);
	STDMETHODIMP	GetCurrentPosition(DWORD *pdwPlay, DWORD *pdwWrite);
	STDMETHODIMP	GetFormat(LPWAVEFORMATEX lpwfxFormat, DWORD cb, DWORD *lpdwSizeWritten);
	STDMETHODIMP	GetVolume(LONG *lplVolume);
	STDMETHODIMP	GetPan(LONG *lplPan);
	STDMETHODIMP	GetFrequency(DWORD *lpdwFrequency);
	STDMETHODIMP	GetStatus(DWORD *lpdwStatus);
	STDMETHODIMP	Initialize(LPDIRECTSOUND lpDirectSound, LPCDSBUFFERDESC lpcDSBufferDesc);
	STDMETHODIMP	Lock(DWORD dwOffset, DWORD dwWriteBytes,
			     LPVOID *lplpvAudioPtr1, LPDWORD lpdwAudioBytes1,
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

	/* IResManBuffer, in d2dbuffer.h slot order. */

	STDMETHODIMP	GetStatusEx(LPDWORD lpdwStatusEx);
	STDMETHODIMP	GetPriority(FLOAT *pfPriority);
	STDMETHODIMP	GetBufferState(LPDWORD lpdwBufferState);
	STDMETHODIMP	SetPriority(FLOAT fPriority);
	STDMETHODIMP	Enable(void);
	STDMETHODIMP	Disable(void);
	STDMETHODIMP	SetNativeModeDisabled(DWORD);
	STDMETHODIMP	Unknown_0x28(DWORD);
	STDMETHODIMP	GetControlBufferPair(DWORD);

	/* IDirectSound3DListener at 0x0C; never exposed by QueryInterface. */

	STDMETHODIMP	GetAllParameters(LPDS3DLISTENER pListener);
	STDMETHODIMP	GetDistanceFactor(D3DVALUE *pflDistanceFactor);
	STDMETHODIMP	GetDopplerFactor(D3DVALUE *pflDopplerFactor);
	STDMETHODIMP	GetOrientation(D3DVECTOR *pvOrientFront,
				       D3DVECTOR *pvOrientTop);
	STDMETHODIMP	GetPosition(D3DVECTOR *pvPosition);
	STDMETHODIMP	GetRolloffFactor(D3DVALUE *pflRolloffFactor);
	STDMETHODIMP	GetVelocity(D3DVECTOR *pvVelocity);
	STDMETHODIMP	SetAllParameters(LPCDS3DLISTENER pcListener, DWORD dwApply);
	STDMETHODIMP	SetDistanceFactor(D3DVALUE flDistanceFactor, DWORD dwApply);
	STDMETHODIMP	SetDopplerFactor(D3DVALUE flDopplerFactor, DWORD dwApply);
	STDMETHODIMP	SetOrientation(D3DVALUE xFront, D3DVALUE yFront,
				       D3DVALUE zFront, D3DVALUE xTop,
				       D3DVALUE yTop, D3DVALUE zTop, DWORD dwApply);
	STDMETHODIMP	SetPosition(D3DVALUE x, D3DVALUE y, D3DVALUE z,
				    DWORD dwApply);
	STDMETHODIMP	SetRolloffFactor(D3DVALUE flRolloffFactor, DWORD dwApply);
	STDMETHODIMP	SetVelocity(D3DVALUE x, D3DVALUE y, D3DVALUE z,
				    DWORD dwApply);
	STDMETHODIMP	CommitDeferredSettings(void);

	/* IKsPropertySet at 0x10. */

	STDMETHODIMP	Get(REFGUID rguidPropSet, ULONG ulId, LPVOID pInstanceData,
			    ULONG ulInstanceLength, LPVOID pPropertyData,
			    ULONG ulDataLength, PULONG pulBytesReturned);
	STDMETHODIMP	Set(REFGUID rguidPropSet, ULONG ulId, LPVOID pInstanceData,
			    ULONG ulInstanceLength, LPVOID pPropertyData,
			    ULONG ulDataLength);
	STDMETHODIMP	QuerySupport(REFGUID rguidPropSet, ULONG ulId,
				     PULONG pulTypeSupport);

	DWORD		SetRate(DWORD dwRate);

	DWORD		GetWaveSize(void);

	HRESULT		Init(const DSBUFFERDESC1 *pDesc, class CHrtfMgr *pHrtfMgr);
	HRESULT		InitDuplicate(A2DBuffer *lpOriginalBuffer);

public:
	/* 0x14 */ LONG		m_cRef;			/* Starts at 1; zero marks for collection. */
	/* 0x18 */ DWORD	m_dwFlags;		/* bit 0 enabled, bit 1 looping */
	/* 0x1C */ LONG		m_lVolume;		/* -10000..0, hundredths of a dB */
	/* 0x20 */ LONG		m_lPan;
	/* 0x24 */ DWORD	m_dwSampleRate;		/* Original sample rate, Hz. */
	/* 0x28 */ DWORD	m_dwFrequency;		/* Playback frequency, Hz. */
	/* 0x2C */ DWORD	m_dwWrite;		/* Byte offset. */
	/* 0x30 */ int		m_nPendingSeek;		/* Pending byte offset  */

	/* m_cbBuffer is a legacy alias still read by A3dMixVoice; its 677 path
	   must use GetWaveSize instead. */
	/* 0x34 */ union
	{
		FLOAT		m_fPriority; /* 0..1. */
		DWORD		m_cbBuffer;
	};

	/* 0x38 */ DWORD	m_dwState;		/* A3DVOICE_STATE_* value. */

	/* 0x3C */ DSBUFFERDESC1	m_DSBufferDesc;		/* Descriptor with embedded format pointer. */
	/* 0x50 */ WAVEFORMATEX	m_wfx;			/* Stored format. */

	/* 0x64 */ A3DCTRL_SRC_SUPER	m_A3dCtrlSuper;	/* Source controls; size 0x40C. */

	/* 0x470 */ A3DMIXPLAY	m_play;
	/* 0x484 */ A3DMIXSTATE	m_mix;

	/* 0xB60 */ class CHrtfMgr	*m_pHrtfMgr;	/* Device-owned HRTF bank. */
	/* 0xB64 */ CWaveForm		*m_pWaveForm;	/* Owned reference to shared wave data. */
};

typedef int A2DBufferSizeCheck[(sizeof(A2DBuffer) == 0xB68) ? 1 : -1];

#endif /* _D2DBUFFER_IRESMANBUFFER && !_A2DBUFFER_CLASS_DECLARED */
