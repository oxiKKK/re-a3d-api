/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * rmbuffer.h
 *
 * Declares ResManBuffer, the shared base for static and streaming
 * resource-manager buffers. It stores the current DAL binding, pending
 * source control blocks, mute state and synchronization for deferred
 * property requests.
 *
 * Cached CPropertySetItem records preserve property data across buffer
 * assignments. rmbuffer.cpp implements the common operations, while
 * rmstatbuffer.h and rmstreambuffer.h declare the playback-specific
 * interfaces and state.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _RMBUFFER_H
#define _RMBUFFER_H

#include "A3dPrivate.h"
#include "LinkList.h"

class ResMan;
class DalBufferInfo;



#define RESMANBUFFER_RENDER_3D                  1
#define RESMANBUFFER_RENDER_DEFAULT             2
#define RESMANBUFFER_RENDER_FOUR_CHANNEL        4

/* Buffer muted by focus loss. */
#define RESMANBUFFER_FLAG_FOCUSMUTE	1

/* (RE) Resource-manager buffer interface, 12 __stdcall slots.
 * Base table: dbg:0x10137540; derived tables: ResManStatBuffer dbg:0x10137AA4,
 * ResManStreamBuffer dbg:0x10138898. All base slots are __purecall.
 * Incompatible with d2dbuffer.h's IResManBuffer; use separate translation units.
 */

#undef  INTERFACE
#define INTERFACE IResManBuffer

DECLARE_INTERFACE_(IResManBuffer, IUnknown)
{
	STDMETHOD(QueryInterface)	(THIS_ REFIID, void **) PURE;	/*  0 */
	STDMETHOD_(ULONG, AddRef)	(THIS) PURE;			/*  1 */
	STDMETHOD_(ULONG, Release)	(THIS) PURE;			/*  2 */
	STDMETHOD(GetStatusEx)		(THIS_ LPDWORD) PURE;		/*  3 */
	STDMETHOD(GetPriority)		(THIS_ FLOAT *) PURE;		/*  4 */
	STDMETHOD(GetBufferState)	(THIS_ LPDWORD) PURE;		/*  5 */
	STDMETHOD(SetPriority)		(THIS_ FLOAT) PURE;		/*  6 */
	STDMETHOD(MuteForFocusLoss)	(THIS) PURE;			/*  7 */
	STDMETHOD(UnMuteForFocusGain)	(THIS) PURE;			/*  8 */
	STDMETHOD(SetRenderMode)	(THIS_ DWORD) PURE;		/*  9 */
	STDMETHOD(GetRenderMode)	(THIS_ LPDWORD) PURE;		/* 10 */
	STDMETHOD(GetCtrlBuffers)	(THIS_ LPVOID *) PURE;		/* 11 */
};

/* (RE) Primary buffer table: dbg:0x101375a8, 33 slots.
 * Slots 0..20 are IDirectSoundBuffer (__stdcall); slots 21..32 are
 * __thiscall except slot 29 (__stdcall).
 */

struct IResManBufferPrimary : public IDirectSoundBuffer
{
	virtual ~IResManBufferPrimary(void) {}			/* 21 */
	/* (RE) Init labels: dbg:0x1013862c, dbg:0x10138e9c. */
	virtual HRESULT	 Init(const DSBUFFERDESC1 *lpDesc) = 0;		/* 22 */
	/* Reapplies saved controls  */
	virtual HRESULT	 AttachDalBufferInfo(DalBufferInfo *lpInfo) = 0;	/* 23 */
	/* (RE) Duplicate labels: dbg:0x10138760, dbg:0x101390b4. */
	virtual HRESULT	 Duplicate(LPDIRECTSOUNDBUFFER lpDSB) = 0;	/* 24 */
	virtual HRESULT	 GetDalBufferInfo(DalBufferInfo **lplpInfo) = 0;		/* 25 */
	virtual HRESULT	 SetDalBufferInfoBare(DalBufferInfo *lpInfo) = 0;	/* 26 */
	virtual HRESULT	 GetA3dCtrlSuper(LPVOID *lplpCtrl) = 0;				/* 27 */
	virtual HRESULT	 Tick(int nWhen) = 0;					/* 28 */
	virtual HRESULT	 STDMETHODCALLTYPE Unknown_0x74(DWORD dwArg) = 0;		/* 29, __stdcall */
	virtual BOOL	 HasUnappliedPropertySet(void) = 0;				/* 30 */
	virtual HRESULT	 MuteBuffer(void) = 0;					/* 31 */
	virtual HRESULT	 UnMuteBuffer(void) = 0;				/* 32 */
};

/* =============================================================
// Class: ResManBuffer
//
// Description: Shared resource-manager buffer controls and property cache.
//
// Size: 0x868
//
// (RE) Constructor: rtl:0x100287e0; dbg:0x1006b6e0
// =============================================================*/

/* (RE) Vtables:
 * 0x00 IResManBufferPrimary dbg:0x101375a8 rtl:0x10053d50, 33 slots.
 * 0x04 IA3dDalBuffer        dbg:0x1013757c rtl:0x10053d2c, 9 slots.
 * 0x08 IResManBuffer        dbg:0x10137540 rtl:0x10053cfc, 12 slots.
 * 0x0C Unresolved; declared IDirectSoundNotify:
 *                          dbg:0x1013752c rtl:0x10053cec, 4 slots.
 * 0x10 IA3dPropertySet      dbg:0x10137508 rtl:0x10053cd0, 7 slots.
 */

class ResManBuffer : public IResManBufferPrimary,
		     public IA3dDalBuffer,
		     public IResManBuffer,
		     public IDirectSoundNotify,
		     public IA3dPropertySet
{
public:
	ResManBuffer(void);
	/* (RE) Compiler-generated deleting destructor, primary slot 21:
	 * rtl:0x100288b0; dbg:0x1006b850. */
	virtual ~ResManBuffer(void);

	/* (RE) IA3dDalBuffer slot 3. The reference body is __thiscall;
           this declaration serves the inherited __stdcall interface. */
	STDMETHODIMP	SetA3dSuperCtrl(LPA3DCTRL_SRC_SUPER lpA3dCtrlSuper,
					DWORD dwSize);

	/* (RE) IResManBuffer slot 11. The reference body is __thiscall;
           this declaration serves the inherited __stdcall interface.
           Adjustor: dbg:0x100702e0; rtl:0x1002b490. */
	STDMETHODIMP	GetCtrlBuffers(LPVOID *lplpCtrlBuffers);

	/* IA3dPropertySet slots 3 to 6, through the subobject at 0x10. */

	STDMETHODIMP	QuerySupport(REFGUID rguidPropSet, ULONG ulId,
				     PULONG pulTypeSupport);
	STDMETHODIMP	Get(REFGUID rguidPropSet, ULONG ulId,
			    LPVOID pInstanceData, ULONG cbInstanceData,
			    LPVOID pPropertyData, ULONG cbPropertyData,
			    PULONG pulBytesReturned);
	STDMETHODIMP	Set(REFGUID rguidPropSet, ULONG ulId,
			    LPVOID pInstanceData, ULONG cbInstanceData,
			    LPVOID pPropertyData, ULONG cbPropertyData,
			    DWORD dwFlags);
	STDMETHODIMP	AddInitialStateParameters(REFGUID rguidPropSet,
						  ULONG ulId,
						  LPVOID pInstanceData,
						  ULONG cbInstanceData,
						  LPVOID pPropertyData,
						  ULONG cbPropertyData);

	/* Primary table slots 30..32, __thiscall. */
	BOOL		HasUnappliedPropertySet(void);
	HRESULT		MuteBuffer(void);
	HRESULT		UnMuteBuffer(void);

	/* (RE) SendPendingCtrl thunk: dbg:0x10001460. */
	HRESULT		SendPendingCtrl(void);

public:
	/* 0x14 */ ResMan	*m_lpResMan;	/* Borrowed owner; null after successful construction. */

	/* 0x18 */ A3DCTRL_SRC_SUPER		m_aCtrlBuffers[2];	/* Two contiguous A3DCTRL_SRC_SUPER blocks. */

	/* 0x830 */ A3DCTRL_SRC_SUPER	*m_lpA3dCtrlSuper;	/* Retained caller-owned control block. */
	/* 0x834 */ DWORD	m_dwA3dCtrlSuperSize;	/* Control-block size, bytes. */
	/* 0x838 */ A3DCTRL_SRC_SUPER	*m_lpA3dCtrlSuperPending;	/* Pending block; cleared after a send. */

	/* 0x83C */ DalBufferInfo	*m_pDalBufferInfo;	/* Current DAL buffer binding. */

	/* 0x840 */ BOOL	m_bMuted;	/* Send mute controls while set. */

	/* 0x844 */ CList	m_listPropSetItems;	/* Owned CPropertySetItem records; block size 10. */

	/* 0x85C */ HANDLE	m_hPropSetCacheFlushed;	/* Owned auto-reset completion event. */
	/* 0x860 */ HANDLE	m_hPropSetMutex;	/* Owned property-queue mutex. */

	/* 0x864 */ A3DCTRL_SRC_SUPER	*m_lpReflectionSuperCtrl;	/* Borrowed reflection-availability source set by ResMan. */
};

typedef int ResManBufferSizeCheck[(sizeof(ResManBuffer) == 0x868) ? 1 : -1];

void CheckEasterEgg(LPCSTR lpszMessage);

#endif /* _RMBUFFER_H */
