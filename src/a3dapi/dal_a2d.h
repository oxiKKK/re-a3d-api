/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * dal_a2d.h
 *
 * Declares DAL_A2D, the software mixing device, and its private startup
 * interface. The class exposes DirectSound and A3D device interfaces and
 * owns the voice list, looping output buffer, mixing workspace and HRTF
 * manager.
 *
 * The declarations describe mixer timing, output format, queue thresholds
 * and worker-thread synchronization. A2DBuffer holds each sound's
 * playback and filter state; dal_a2d.cpp coordinates those voices and
 * writes their combined PCM output through DirectSound.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _DALA2D_H
#define _DALA2D_H

#include "A3dPrivate.h"
#include "LinkList.h"
#include "Plex.h"
#include "softmix.h"

class CHrtfMgr;
class A2DBuffer;

/* (RE) Private startup interface;
 * Vtable at DAL_A2D+0x0C: dbg:0x1012FEC0; rtl:0x10053830, 4 slots.
 */

#undef  INTERFACE
#define INTERFACE IA3dPrvA2D

DECLARE_INTERFACE_(IA3dPrvA2D, IUnknown)
{
	/* Slot 0. */
	STDMETHOD(QueryInterface)  (THIS_ REFIID, void **) PURE;
	/* Slot 1. */
	STDMETHOD_(ULONG, AddRef)  (THIS)                  PURE;
	/* Slot 2. */
	STDMETHOD_(ULONG, Release) (THIS)                  PURE;

	/* Slot 3: create output and start the mix thread. */
	STDMETHOD(StartupDal) (THIS) PURE;
};

#define A3D_MIN_SECONDARY_RATE 6000   /* GetCaps low end */
#define A3D_MAX_SECONDARY_RATE 100000 /* GetCaps high end */

#define A3D_MIX_WAIT_MS      20     /* how long the thread sleeps */
#define A3D_MIX_BUFFER_BYTES 0x3000 /* default output buffer size */
#define A3D_MIX_BUFFER_NT4   0x4000 /* the NT 4 output buffer size */
#define A3D_MIX_BUFFER_MAX   0x8000 /* the registry override's cap */
#define A3D_MIX_SAMPLE_RATE  22050
/* Integer filter rate  */
#define A3D_MIX_FILTER_RATE_KHZ 22
#define A3D_MIX_CHANNELS 2
#define A3D_MIX_BITS     16
#define A3D_MIX_VOICES   64 /* the cap GetCaps answers with */

/* =============================================================
// Class: DAL_A2D
//
// Description: Software mixer with a DirectSound output buffer.
//
// Size: 0x258
//
// (RE) Constructor: dbg:0x10048D00
// =============================================================*/

/* (RE) Vtables:
 * 0x00 IDirectSound rtl:0x1005389c dbg:0x1012FF44, 11 slots.
 * 0x04 IA3dDal     rtl:0x10053870 dbg:0x1012FF10, 11 slots.
 * 0x08 IA3d2       rtl:0x10053840 dbg:0x1012FED4, 12 slots.
 * 0x0C IA3dPrvA2D  rtl:0x10053830 dbg:0x1012FEC0, 4 slots.
 */

class DAL_A2D : public IDirectSound,
		public IA3dDal,
		public IA3d2,
		public IA3dPrvA2D
{
public:
	DAL_A2D(void);
	~DAL_A2D(void);

	/* IUnknown, one set shared by the four vtables. */

	STDMETHODIMP         QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG) AddRef(void);
	STDMETHODIMP_(ULONG) Release(void);

	/* IDirectSound. */

	STDMETHODIMP CreateSoundBuffer(LPCDSBUFFERDESC lpcDSBufferDesc,
	                               LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
	                               LPUNKNOWN pUnkOuter);
	STDMETHODIMP GetCaps(LPDSCAPS lpDirectSoundCaps);
	STDMETHODIMP DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER pOriginal,
	                                  LPDIRECTSOUNDBUFFER *ppCopy);
	STDMETHODIMP SetCooperativeLevel(HWND hWnd, DWORD dwLevel);
	STDMETHODIMP Compact(void);
	STDMETHODIMP GetSpeakerConfig(LPDWORD pdwConfig);
	STDMETHODIMP SetSpeakerConfig(DWORD dwConfig);
	STDMETHODIMP Initialize(LPCGUID pGuid);

	/* IA3dDal, in ia3ddal.h slot order. */

	STDMETHODIMP InitializeEx(LPGUID pGuidDevice, DWORD dwFlags,
	                          DWORD dwReserved, LPDWORD lpdwFeaturesEnabled);
	STDMETHODIMP CreateSoundBufferEx(const DSBUFFERDESC1 *lpcDSBufferDesc,
	                                 LPBYTE lpbWave,
	                                 LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
	                                 LPUNKNOWN pUnkOuter);
	STDMETHODIMP GetA3dCaps(LPA3DDALCAPS lpA3dCaps, LPDWORD lpdwSize);
	STDMETHODIMP GetDriverInfo(LPHANDLE lphA3dVxd, void **lplpIDsDriver,
	                           void **lplpIA3dDriver, LPDWORD lpdwCardIndex);
	STDMETHODIMP GetDS(LPDIRECTSOUND *lplpDirectSound);
	STDMETHODIMP GetDSDriverDesc(void *lpsDSDriverDesc, LPDWORD lpdwSize);
	STDMETHODIMP QueryFunctionality(DWORD dwFunction, LPDWORD lpdwStatus);
	STDMETHODIMP Verify(LPSTR lpcString, LPSTR *lplpcStringCrypted,
	                    LPSTR *lplpcCopyright);

	/* IA3d2. */

	STDMETHODIMP SetOutputMode(DWORD dwFrontXtalkMode,
	                           DWORD dwBackXtalkMode, DWORD dwQuadMode);
	STDMETHODIMP GetOutputMode(LPDWORD lpdwFrontXtalkMode,
	                           LPDWORD lpdwBackXtalkMode,
	                           LPDWORD lpdwQuadMode);
	STDMETHODIMP SetResourceManagerMode(DWORD dwResourceManagerMode);
	STDMETHODIMP GetResourceManagerMode(LPDWORD lpdwResourceManagerMode);
	STDMETHODIMP SetHFAbsorbFactor(FLOAT fFactor);
	STDMETHODIMP GetHFAbsorbFactor(FLOAT *pfFactor);
	STDMETHODIMP RegisterVersion(DWORD dwVersion);
	STDMETHODIMP GetSoftwareCaps(LPA3DCAPS_SOFTWARE pCaps);
	STDMETHODIMP GetHardwareCaps(LPA3DCAPS_HARDWARE pCaps);

	/* IA3dPrvA2D slot 3. */

	STDMETHODIMP StartupDal(void);

	HRESULT Init(void);
	HRESULT CreateSecondaryBuffer(const DSBUFFERDESC1 *pDesc,
	                              LPBYTE lpWave,
	                              LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
	                              LPUNKNOWN pUnkOuter);

	int     CountActive(void);
	HRESULT GetBytesQueued(DWORD *pdwBytesQueued);
	HRESULT IsStarved(DWORD dwNow, DWORD *pdwBytesToFill);
	HRESULT WriteOut(DWORD cb);

	static DWORD WINAPI MixThread(void *pv);

public:
	/* 0x10 */ LONG m_cRef; /* Initially 1. */

	/* Mixer thread ID. */
	/* 0x14 */ DWORD  m_dwThreadId;
	/* Initially -1. */
	/* 0x18 */ HANDLE m_hThread;
	/* Owned wake event; initially -1. */
	/* 0x1C */ HANDLE m_hWake;
	/* Owned completion event; initially -1. */
	/* 0x20 */ HANDLE m_hEventCallbackInactive;

	/* 0x24 */ DWORD m_dwWakeTimeoutMs; /* Wake timeout, milliseconds  */
	/* 0x28 */ DWORD m_cbFillThreshold; /* Queued-byte threshold for refilling. */
	/* 0x2C */ DWORD m_Unknown_0x2C;    /* Zero; not read. */

	/* Output buffer size, bytes. */
	/* 0x30 */ DWORD m_cbBuffer;
	/* Output rate, Hz. */
	/* 0x34 */ DWORD m_dwSampleRate;
	/* Output channel count. */
	/* 0x38 */ DWORD m_dwChannels;
	/* Output sample depth. */
	/* 0x3C */ DWORD m_dwBitsPerSample;
	/* Maximum active voices. */
	/* 0x40 */ DWORD m_cVoices;
	/* Output write offset, bytes. */
	/* 0x44 */ DWORD m_dwWritePos;
	/* Allocation size of each work buffer, bytes. */
	/* 0x48 */ DWORD m_cbWork;

	/* 0x4C */ short *m_pStaging;  /* Owned 16-bit PCM staging buffer. */
	/* 0x50 */ float *m_pMix;      /* Owned floating-point accumulation buffer. */

	/* (RE) InitFilterState leaves m_FilterMode uninitialized: dbg:0x100756EF. */
	/* 0x054 */ A3DFILTERMODE  m_FilterMode; /* Output filter mode. */
	/* 0x064 */ A3DFILTERSTATE m_Filter;     /* Output filter state. */
	/* 0x228 */ DWORD          m_Unknown_0x228;

	/* Owned DirectSound reference. */
	/* 0x22C */ IDirectSound      *m_pDirectSound;
	/* Owned looping output buffer. */
	/* 0x230 */ IDirectSoundBuffer *m_pDSBuffer;
	/* Owned A2DBuffer pointers  */
	/* 0x234 */ CList              m_VoiceList;
	/* Owned HRTF manager shared by voices. */
	/* 0x24C */ CHrtfMgr          *m_pHrtfMgr;
	/* Continue the mixer loop while set. */
	/* 0x250 */ DWORD              m_fThreadRun;
	/* StartupDal completed. */
	/* 0x254 */ DWORD              m_fInitialized;
};

typedef int DalA2dSizeCheck[(sizeof(DAL_A2D) == 0x258) ? 1 : -1];

HRESULT A3dMakeOutputBuffer(DAL_A2D *pDevice, IDirectSound *pDS,
                            IDirectSoundBuffer **ppBuffer);

void A3dMixVoice(short *pStaging, float *pMix, A2DBuffer *pVoice,
                 DWORD cb, int nMode);

#endif /* _DALA2D_H */
