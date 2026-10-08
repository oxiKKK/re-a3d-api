/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * dalinfo.h
 *
 * Declares DalInfo and DalBufferInfo, the resource manager's device and
 * playback-buffer records. They hold cached DAL interfaces and
 * capabilities, mode thresholds, reusable buffer lists, source ownership
 * and streaming cursors.
 *
 * The file also defines the DAL mode and capability structures and
 * scale-hack interfaces needed by those records. dalinfo.cpp implements
 * allocation, reuse, PCM conversion and refilling; resman.cpp coordinates
 * which source receives each buffer.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _DALINFO_H
#define _DALINFO_H

#include "A3dPrivate.h"
#include "LinkList.h"
#include "Plex.h"
#include "a2dbuffer.h"

/* Idle-cache timeout in milliseconds. */
#define A3D_CACHE_TIMEOUT_MS	10000


#define A3D_DALMODE_LOCKED_BUFFER_FILL  1
#define A3D_DALMODE_HARDWARE_STATUS     2
#define A3D_DALMODE_SOFTWARE_STATUS     4
#define A3D_DALMODE_MAIN_ELIGIBLE       8
#define A3D_DAL_VOICE_COUNT_3D          1
#define A3D_DAL_VOICE_COUNT_MIXING      2
#define A3D_DALMODE_DISABLE_SCALE_HACK  0x10

void	*A3dBlockAlloc(void **ppBlocks, int nMax, int cbElement);

/* =============================================================
// Class: DALMODEDESC
//
// Description: DAL output mode and streaming limits.
//
// Size: 0x2C
//
// (RE) Copied by: dbg:0x10053bd0; dbg:0x10055bb0
// =============================================================*/

typedef struct _DALMODEDESC
{
	/* 0x00 */ DWORD	dwFlags;	/* Mode flags; bit 0x10 enables the scale-hack probe. */
	/* 0x04 */ DWORD	dwVoiceCountMode;	/* 1 selects 3D voice caps; other values select mixing caps. */
	/* 0x08 */ DWORD	dwStreamingBufferSize;	/* DAL buffer bytes. */
	/* 0x0C */ DWORD	dwSampleRate;	/* Samples per second. */
	/* 0x10 */ DWORD	dwBitsPerSample;	/* PCM sample depth. */
	/* 0x14 */ DWORD	dwRefillThreshold;	/* Queued-byte threshold for refilling. */
	/* 0x18 */ DWORD	dwRefillTarget;	/* Target queued bytes. */
	/* 0x1C */ DWORD	dwBufferFlags;	/* DirectSound buffer flags. */
	/* 0x20 */ DWORD	dwChannels;	/* PCM channel count. */
	/* 0x24 */ DWORD	dwTotalBufferCap;	/* Combined static/streaming buffer limit. */
	/* 0x28 */ DWORD	dwName;	/* Packed three-character DAL tag. */
} DALMODEDESC;

typedef int DalModeDescSizeCheck[(sizeof(DALMODEDESC) == 0x2C) ? 1 : -1];

/* =============================================================
// Class: A3DDALCAPS564
//
// Description: Extended DAL capability buffer; unknown tail contents.
//
// Size: 0x234
//
// (RE) Read by: dbg:0x10054640
// =============================================================*/

typedef struct _A3DDALCAPS564
{
	/* 0x00 */ A3DDALCAPS	caps;
	/* 0x48 */ DWORD	adwUnknown_0x48[6];
	/* 0x60 */ WORD		wUnknown_0x60;
	/* 0x62 */ WORD		wUnknown_0x62;	/* Control-panel override compares this word to 13. */
	/* 0x64 */ DWORD	adwUnknown_0x64[116];
} A3DDALCAPS564;

typedef int A3dDalCaps564SizeCheck[(sizeof(A3DDALCAPS564) == 564) ? 1 : -1];

#undef  INTERFACE
#define INTERFACE IA3dScaleHack

/* (RE) DAL scale-hack control, used at dbg:0x10054850.
   Slot 3 behavior is unknown. */

DECLARE_INTERFACE_(IA3dScaleHack, IUnknown)
{
	STDMETHOD(QueryInterface)	(THIS_ REFIID, void **) PURE;	/* Slot 0. */
	STDMETHOD_(ULONG,AddRef)	(THIS) PURE;	/* Slot 1. */
	STDMETHOD_(ULONG,Release)	(THIS) PURE;	/* Slot 2. */

	STDMETHOD(ScaleHack03)		(THIS) PURE;	/* Slot 3. */
	STDMETHOD(ScaleHackDisable)	(THIS) PURE;	/* Slot 4. */
	STDMETHOD(GetScaleHackState)		(THIS_ LPDWORD lpdwState) PURE;	/* Slot 5. */
};

#undef  INTERFACE
#define INTERFACE IA3dScaleHackBuffer

/* (RE) Buffer scale-hack control, used at dbg:0x10057c00. */

DECLARE_INTERFACE_(IA3dScaleHackBuffer, IUnknown)
{
	STDMETHOD(QueryInterface)	(THIS_ REFIID, void **) PURE;	/* Slot 0. */
	STDMETHOD_(ULONG,AddRef)	(THIS) PURE;	/* Slot 1. */
	STDMETHOD_(ULONG,Release)	(THIS) PURE;	/* Slot 2. */

	STDMETHOD(SetScaleHack)		(THIS_ DWORD dwOn) PURE;	/* Slot 3. */
};

class DalBufferInfo;

/* =============================================================
// Class: DalInfo
//
// Description: Attached DAL and its cached buffers.
//
// Size: 0x74
//
// (RE) Constructor: dbg:0x10053bd0
// =============================================================*/

class DalInfo
{
public:
	DalInfo(IA3dDal *pIA3dDal, const DALMODEDESC *pModeDesc);

	~DalInfo(void);

	HRESULT		Init(void);

	HRESULT		ReadVoiceCount(DWORD *pdwVoices);
	HRESULT		ReadAlphaGainAdjustment(void);

	HRESULT		GetDalCaps(A3DDALCAPS564 *pCaps, DWORD *pcbSize);

	HRESULT		DisableScaleHack(void);

	/* (RE) dbg:0x10055b90; thunk dbg:0x100014c4 */
	IA3dDal		*GetIDal(void)		{ return (m_pIA3dDal); }

	/* (RE) dbg:0x1005ca60; thunk dbg:0x10001a82 */
	IDirectSound	*GetIDirectSound(void)	{ return (m_pDalDS); }

	/* (RE) dbg:0x1005f1c0 */
	IA3d2		*GetIA3d2(void)		{ return (m_pIA3d2); }

	HRESULT		GetModeDesc(DALMODEDESC *pDesc);
	HRESULT		SetModeDesc(const DALMODEDESC *pDesc);

	HRESULT		SetCooperativeLevel(HWND hWnd, DWORD dwLevel);

	DWORD		GetNumAvailable(void);

	HRESULT		CreateStaticDalBuffer(DSBUFFERDESC1 *pDesc,
					      DalBufferInfo **ppDalBufferInfo);
	HRESULT		CreateDalBuffer(DalBufferInfo *pSource,
					DalBufferInfo **ppDalBufferInfo);
	HRESULT		CreateDalBuffer(DalBufferInfo **ppDalBufferInfo);

	HRESULT		FindIdle(DalBufferInfo **ppDalBufferInfo);

	HRESULT		RemoveBuffer(DalBufferInfo *pDalBufferInfo);

	HRESULT		Reap(DWORD dwNow);

	int		GetNumActiveDalBuffers(void);
	int		GetNumActiveStaticBuffers(void);

	/* (RE) dbg:0x10057b30 */
	DWORD		GetAlphaGainAdjustment(void)	{ return (m_dwAlphaGainAdjustment); }

	/* (RE) dbg:0x1005FCE0; thunk dbg:0x1000285B */
	BOOL		ReportsHardwareStatus(void) const
			{ return ((m_sDalModeDesc.dwFlags & A3D_DALMODE_HARDWARE_STATUS) != 0); }

	/* (RE) dbg:0x100619F0; thunk dbg:0x1000157D */
	BOOL		Has3DVoiceCountMode(void) const
			{ return ((m_sDalModeDesc.dwVoiceCountMode & A3D_DAL_VOICE_COUNT_3D) != 0); }

	/* (RE) dbg:0x10060B10; thunk dbg:0x1000252C */
	BOOL		ReportsSoftwareStatus(void) const
			{ return ((m_sDalModeDesc.dwFlags & A3D_DALMODE_SOFTWARE_STATUS) != 0); }

	BOOL		IsD3dHardware(void);

	/* (RE) dbg:0x100657D0; thunk dbg:0x10001A78 */
	DWORD		GetTotalBufferCap(void) const
			{ return (m_sDalModeDesc.dwTotalBufferCap); }

	/* (RE) dbg:0x100609f0; thunk dbg:0x10001d98 */
	DWORD		GetMode(void) const
			{ return (m_sDalModeDesc.dwVoiceCountMode); }

	int		GetNumDalBuffers(void);

	int		GetNumStaticBuffers(void);

	DWORD		GetName(void);

public:
	/* 0x00 */ IA3dDal	*m_pIA3dDal;	/* Referenced DAL. */
	/* 0x04 */ IDirectSound	*m_pDalDS;	/* Referenced DirectSound interface. */
	/* 0x08 */ IA3d2	*m_pIA3d2;	/* Referenced A3D interface. */
	/* 0x0C */ DALMODEDESC	m_sDalModeDesc;	/* Copied DAL mode. */

	/* 0x38 */ DWORD	m_cVoices;	/* Maximum of DirectSound and DAL voice limits. */

	/* List  */
	/* 0x3C */ CList	m_listDalBuffers;	/* Owned streaming/duplicate DalBufferInfo entries. */
	/* 0x54 */ CList	m_listStaticBuffers;	/* Owned static DalBufferInfo entries. */

	/* 0x6C */ BOOL		m_fScaleHackDisabled;	/* DAL scale hack was disabled. */

	/* 0x70 */ DWORD	m_dwAlphaGainAdjustment;	/*  Adjust gain and clear alpha for DAL submission. */
};

typedef int DalInfoSizeCheck[(sizeof(DalInfo) == 0x74) ? 1 : -1];

/* =============================================================
// Class: DalBufferInfo
//
// Description: Cached DirectSound buffer and streaming state.
//
// Size: 0x5C
//
// (RE) Constructor: dbg:0x10055bb0; rtl:0x10021ce0
// =============================================================*/

class DalBufferInfo
{
public:
	/* Kind and optional captured base  */
	DalBufferInfo(DalInfo *lpDalInfo, IDirectSoundBuffer *lpDSBuffer,
		      const DALMODEDESC *pModeDesc, int nKind, int nBufferBase);

	~DalBufferInfo(void);

	BOOL		IsIdle(void) const;
	BOOL		IsExpired(DWORD dwNow) const;

	HRESULT		GetBufferState(DWORD *pdwBufferState);

	HRESULT		GetBytesQueued(DWORD *pdwBytesQueued,
				       DWORD *pdwPlayCursor);

	HRESULT		ApplyScaleHack(void);

	/* (RE) dbg:0x100552f0; thunk dbg:0x10002c39 */
	IDirectSoundBuffer	*GetDSBuffer(void)	{ return (m_pDSBuffer); }

	/* (RE) dbg:0x1002aed0 */
	IA3dDalBuffer		*GetIDalBuffer(void)	{ return (m_pIDalBuffer); }
	HRESULT			GetBufferBase(DWORD *pdwBase);

	HRESULT			SetA3dSuperCtrl(LPA3DCTRL_SRC_SUPER lpA3dCtrlSuper,
						DWORD dwSize);

	/* (RE) dbg:0x1005d5e0; thunk dbg:0x100026a8 */
	DalInfo			*GetDalInfo(void)	{ return (m_pDalInfo); }

	/* (RE) dbg:0x10061730; thunk dbg:0x10003d91 */
	void			SetOwner(DWORD dwOwner)		{ m_dwOwnerBuffer = dwOwner; }

	/* (RE) dbg:0x10065600; thunk dbg:0x100031c5 */
	void			SetTick(DWORD dwTick)		{ m_dwTick = dwTick; }

	/* (RE) dbg:0x10065dc0; thunk dbg:0x10002abd */
	void			SetChannels(DWORD dwValue)	{ m_dwChannels = dwValue; }

	/* (RE) dbg:0x1002AE70; thunk dbg:0x10002B3F */
	BOOL		UsesLockedBufferFill(void) const
			{ return ((m_sDalModeDesc.dwFlags & A3D_DALMODE_LOCKED_BUFFER_FILL) != 0); }

	/* (RE) dbg:0x1002AE40; thunk dbg:0x10003742 */
	BOOL		ReportsHardwareStatus(void) const
			{ return ((m_sDalModeDesc.dwFlags & A3D_DALMODE_HARDWARE_STATUS) != 0); }

	/* (RE) dbg:0x1002AEA0; thunk dbg:0x1000385A */
	BOOL		ReportsSoftwareStatus(void) const
			{ return ((m_sDalModeDesc.dwFlags & A3D_DALMODE_SOFTWARE_STATUS) != 0); }

	/* (RE) dbg:0x10061B50; thunk dbg:0x10004336 */
	BOOL		Has3DVoiceCountMode(void) const
			{ return ((m_sDalModeDesc.dwVoiceCountMode & A3D_DAL_VOICE_COUNT_3D) != 0); }

	BOOL		Refresh(DWORD *pdwBytesToFill, DWORD *pdwPlayCursor);

	HRESULT		GetSourceBytesQueued(DWORD *pdwSourceBytesQueued,
					     DWORD dwFlags);
	HRESULT		GetAudioBytesQueued(DWORD *pdwAudioBytesQueued);

	void		SilenceAndRewind(void);

	HRESULT		FillFromSource(void *lpvDest, const void *lpvSource,
					   DWORD dwSourceSize,
					   DWORD *lpdwSourcePosition,
					   DWORD dwFlags);

	HRESULT		FormatAndFillBuffer(void *lpvTarget, DWORD dwTargetBytes,
					    const void *lpvSource,
					    DWORD dwSourceSize,
					    DWORD *lpdwSourcePosition,
					    DWORD dwFlags, DWORD dwPlayBase);
	HRESULT		FormatToMono(short *lvpDest, const void *lvpSrc,
					   DWORD dwSamples, DWORD dwFlags);
	HRESULT		FormatToStereo(short *lvpDest, const void *lvpSrc,
					   DWORD dwSamples, DWORD dwFlags);

public:
	/* 0x00 */ DalInfo		*m_pDalInfo;	/* Borrowed owning DAL metadata. */
	/* 0x04 */ DALMODEDESC		m_sDalModeDesc;	/* Copied DAL mode. */

	/* 0x30 */ IA3dDalBuffer	*m_pIDalBuffer;	/* Referenced voice interface; may be null. */

	/* 0x34 */ IDirectSoundBuffer	*m_pDSBuffer;	/* Referenced DirectSound buffer. */

	/* 0x38 */ DWORD		m_dwOwnerBuffer;	/* Bound ResManBuffer pointer; zero means idle. */

	/* 0x3C */ DWORD		m_dwTick;	/* Idle-timeout starting tick. */
	/* 0x40 */ DWORD		m_dwTimeout;	/* Idle timeout in milliseconds. */

	/* 0x44 */ DWORD		m_dwLastWriteOffset;	/* End of the last write in DAL-buffer bytes. */

	/* 0x48 */ int			m_nAudioEndOffset;	/* Non-looping audio end in DAL bytes; -1 until recorded. */

	/* 0x4C */ DWORD		m_dwChannels;	/* Format selector: 1 mono, 2 stereo. */

	/* 0x50 */ int			m_nKind;	/* 2 for streaming; 0 for static/duplicate. */

	/* 0x54 */ DWORD		m_dwBufferBase;	/* Supplied or captured audio address, retained after Unlock. */

	/* 0x58 */ DWORD		m_dwScaleHackShift;	/* Sample right shift; 1 halves amplitude. */
};

typedef int DalBufferInfoSizeCheck[(sizeof(DalBufferInfo) == 0x5C) ? 1 : -1];

#endif /* _DALINFO_H */
