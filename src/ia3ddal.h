/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * ia3ddal.h
 *
 * Private DAL interfaces and control structures shared by a3dapi.dll and
 * a3d.dll. CA3dRoot and the A3D 1.x shim obtain IA3dDal through
 * QueryInterface on the DirectSound object.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _A3D_IA3DDAL_H
#define _A3D_IA3DDAL_H

#include <windows.h>
#include <objbase.h>
#include <dsound.h>

/* -------------------------------------------------------------------------- */

/*
 * The private class and interface identifiers.
 *
 * a3d.dll serves two coclasses out of one implementation: CLSID_A3d, the A3D
 * 1.x object the published SDK documents, and CLSID_A3dDal, which the SDK
 * registers but publishes no interface for.
*/

DEFINE_GUID(CLSID_A3dDal,   0x442d12a1, 0x2641, 0x11d2, 0x90, 0xfb, 0x00, 0x60, 0x08, 0xa1, 0xf4, 0x41);
DEFINE_GUID(IID_IA3dDal,    0xa3c62f2a, 0x2671, 0x11d2, 0xbb, 0x48, 0x00, 0x60, 0x08, 0x2f, 0x3c, 0x00);
DEFINE_GUID(IID_IA3dPrv4,   0xcbd91fa7, 0x41c6, 0x11d2, 0xbb, 0x48, 0x00, 0x60, 0x08, 0x2f, 0x3c, 0x00);

DEFINE_GUID(IID_IA3dDalBuffer,  0xe04814d8, 0x26fc, 0x11d2, 0xbb, 0x48, 0x00, 0x60, 0x08, 0x2f, 0x3c, 0x00);
DEFINE_GUID(IID_IA3dDalBuffer2, 0xc9d08fe1, 0x646c, 0x11d2, 0xbe, 0x63, 0x00, 0x60, 0x97, 0xce, 0xcf, 0x9f);
DEFINE_GUID(IID_IA3dScaleHack,  0xd77cfaac, 0xca67, 0x11d2, 0xbb, 0x49, 0x00, 0x60, 0x08, 0x2f, 0x3c, 0x00);
DEFINE_GUID(IID_IA3dScaleHackBuffer, 0xb4279161, 0xcb06, 0x11d2, 0xbb, 0x49, 0x00, 0x60, 0x08, 0x2f, 0x3c, 0x00);

/* -------------------------------------------------------------------------- */

/* What IA3dDal::GetA3dCaps() fills. */

struct A3DDALCAPS
{
	WORD		wDeviceType;	/* 0x0104 is the AU8830 */
	WORD		wVersion;	/* the DAL interface version, in BCD */
	DWORD		dwCalcFactor;	/* fixed-point scale; 32767 */
	WORD		wChannelCount;	/* hardware channels, 2 or 4 */
	WORD		wChannels;
	WORD		wRefMask;	/* reflections */
	WORD		wReserved0E;
	DWORD		dwMaxSampleRate;
	WORD		wMaxBuffers;
	WORD		wReserved16;
	DWORD		dwSamplesMask;	/* which sample capabilities are present */
	DWORD		dwReserved1C;
	DWORD		dwFeatureFlags;	/* the A3DCAPS_* feature bits */
	DWORD		dwReserved24;
	DWORD		dwMaxReflections;
	DWORD		dwMaxSrcReflections;	/* per source; 16 */
	DWORD		dwReserved30;
	DWORD		dwReserved34;
	DWORD		dwReserved38;
	DWORD		dwReserved3C;
	DWORD		dwReserved40;
	DWORD		dwUnknown_0x44;	/* Init() wants it zero */
};

/*
 * (RE) GetA3dCaps() fills 0x48 bytes into whatever it is handed, so a
 * structure of any other size overruns the caller's buffer on the first
 * machine that has a DAL.
*/

typedef int A3dDalCapsSizeCheck[(sizeof(A3DDALCAPS) == 0x48) ? 1 : -1];

typedef A3DDALCAPS *LPA3DDALCAPS;

/*
 * CreateSoundBufferEx's descriptor is `const DSBUFFERDESC1 *' and not
 * LPCDSBUFFERDESC, which is a spelling difference and not a disagreement.  In
 * 1998 dsound.h the structure was called DSBUFFERDESC and was twenty bytes; a
 * later DirectX grew a 3D-algorithm GUID onto it, renamed the old form
 * DSBUFFERDESC1 and left the new one under the old name.  Aureal wrote
 * DSBUFFERDESC and meant the twenty-byte one, which is what the binary handles.
*/

/*
 * DSDRIVERDESC - the DirectSound driver description GetDSDriverDesc() fills.
 *
 * (SDK) This is the DDK structure from dsdriver.h, the DirectSound driver
 * header the tree does not vendor.  IA3dDal::GetDSDriverDesc() writes one of
 * these through its void * argument, and four DAL asserts bound the buffer with
 * sizeof(DSDRIVERDESC) before it is written.  The layout is the era DDK one;
 * it is 556 bytes.
*/

typedef struct _DSDRIVERDESC
{
	DWORD		dwFlags;
	CHAR		szDesc[256];
	CHAR		szDrvname[256];
	DWORD		dnDevNode;
	WORD		wVxdId;
	WORD		wReserved;
	ULONG		ulDeviceNum;
	DWORD		dwHeapType;
	LPVOID		pvDirectDrawHeap;
	DWORD		dwMemStartAddress;
	DWORD		dwMemEndAddress;
	DWORD		dwMemAllocExtra;
	LPVOID		pvReserved1;
	LPVOID		pvReserved2;
} DSDRIVERDESC, *PDSDRIVERDESC, *LPDSDRIVERDESC;

/* -------------------------------------------------------------------------- */

#undef  INTERFACE
#define INTERFACE IA3dDal

DECLARE_INTERFACE_(IA3dDal, IUnknown)
{
	STDMETHOD(QueryInterface)	(THIS_ REFIID, void **) PURE;
	STDMETHOD_(ULONG,AddRef)	(THIS) PURE;
	STDMETHOD_(ULONG,Release)	(THIS) PURE;

	STDMETHOD(InitializeEx)		(THIS_ LPGUID pGuidDevice, DWORD dwFlags,
					 DWORD dwReserved,
					 LPDWORD lpdwAvailable) PURE;
	STDMETHOD(CreateSoundBufferEx)	(THIS_ const DSBUFFERDESC1 *lpcDSBufferDesc,
					 LPBYTE lpBuffer,
					 LPDIRECTSOUNDBUFFER *lplpDirectSoundBuffer,
					 LPUNKNOWN pUnkOuter) PURE;
	STDMETHOD(GetA3dCaps)		(THIS_ LPA3DDALCAPS lpA3dCaps,
					 LPDWORD lpdwSize) PURE;
	STDMETHOD(GetDriverInfo)	(THIS_ LPHANDLE lphA3dVxd,
					 void **lplpIA3dDriver,
					 void **lplpIDsDriver,
					 LPDWORD lpdwSize) PURE;
	STDMETHOD(GetDS)		(THIS_ LPDIRECTSOUND *lplpDirectSound) PURE;
	STDMETHOD(GetDSDriverDesc)	(THIS_ void *lpsDSDriverDesc,
					 LPDWORD lpdwSize) PURE;
	STDMETHOD(QueryFunctionality)	(THIS_ DWORD dwFunction,
					 LPDWORD lpdwResult) PURE;
	STDMETHOD(Verify)		(THIS_ LPSTR lpcString,
					 LPSTR *lplpcStringCrypted,
					 LPSTR *lplpcCopyright) PURE;
};

/* -------------------------------------------------------------------------- */

/*
 * The buffer side of the DAL: what one source's worth of control data looks
 * like on its way to the hardware.
 *
 * a3d.dll's A3DDALBANK is this same structure, recovered independently from
 * that binary's DSP block: the same size, the same ear array at the same
 * offset, the same field order.  Where the two disagree they disagree about
 * meaning rather than shape - a3d.dll calls dwMode `dwChannels' because it
 * writes 2 when the solution says bypass and 1 otherwise.
*/

typedef struct _A3DCTRL_EAR
{
	float		fAzim;		/* direction, azimuth   */
	float		fElev;		/* direction, elevation */
	float		fGain;
	float		fDelay;
} A3DCTRL_EAR, *LPA3DCTRL_EAR;

typedef struct _A3DCTRL_REFLECTION
{
	BOOL		bEnable;
	BOOL		bAvailable;
	BOOL		bMute;
	DWORD		dwReserved0C;
	float		fAlpha;		/* equalisation */
	A3DCTRL_EAR	LeftEar;
	A3DCTRL_EAR	RightEar;
	float		fAudibility;
} A3DCTRL_REFLECTION, *LPA3DCTRL_REFLECTION;

#define A3D_MAX_SOURCE_REFLECTIONS	16

typedef struct _A3DCTRL_SRC_SUPER
{
	DWORD		dwReserved00;
	DWORD		dwMode;		/* 1, or 2 for mono */
	BOOL		bExecuted;
	float		fPitch;
	float		fAlpha;		/* equalisation */
	float		fFreqFactor;
	float		fNative;
	A3DCTRL_EAR	LeftEar;
	A3DCTRL_EAR	RightEar;
	float		fPriority;
	float		fAudibility;
	float		fDistance;
	float		fReserved48;
	A3DCTRL_REFLECTION	Reflections[A3D_MAX_SOURCE_REFLECTIONS];
	float		fMinDist;
	float		fMaxDist;
	float		fDistScale;
	BYTE		abReserved3D8[52];
} A3DCTRL_SRC_SUPER, *LPA3DCTRL_SRC_SUPER;

/*
 * The two sizes the binary states in literals, so a field added or dropped
 * stops the build instead of writing past a caller's buffer.
*/

typedef int A3dCtrlSuperSizeCheck[(sizeof(A3DCTRL_SRC_SUPER) == 0x40C) ? 1 : -1];
typedef int A3dCtrlReflSizeCheck[(sizeof(A3DCTRL_REFLECTION) == 56) ? 1 : -1];

/*
 * IA3dDalBuffer - nine slots, six of its own behind IUnknown.  IA3dDalBuffer2
 * adds two on the end of the same table, so the two identifiers are separate
 * and the table is continuous; it is written out in full below the way
 * ia3dapi.h writes IA3d2 and IA3d3 out in full rather than by inheritance.
 *
 * lpA3dCtrlSuper and lpA3dCtrlDirect are what settle the pair at slots 3 and 8:
 * both take the same 0x40C structure and they differ in where it is bound for,
 * not in what it is.
 *
 * (RE) Nine is what a3dapi.dll's four voice classes implement.  Each answers
 * IID_IA3dDalBuffer with a subobject whose table ends at slot 8 and none
 * answers IID_IA3dDalBuffer2, so an implementer of the nine exists separately
 * from an implementer of the eleven.
*/

#undef  INTERFACE
#define INTERFACE IA3dDalBuffer

DECLARE_INTERFACE_(IA3dDalBuffer, IUnknown)
{
	STDMETHOD(QueryInterface)	(THIS_ REFIID, void **) PURE;
	STDMETHOD_(ULONG,AddRef)	(THIS) PURE;
	STDMETHOD_(ULONG,Release)	(THIS) PURE;

	STDMETHOD(SetA3dSuperCtrl)	(THIS_ LPA3DCTRL_SRC_SUPER lpA3dCtrlSuper,
					 DWORD dwSize) PURE;
	STDMETHOD(GetAllocationStatus)	(THIS_ LPDWORD lpdwStatus) PURE;
	STDMETHOD(GetWave)		(THIS_ LPBYTE *lplpWave) PURE;
	STDMETHOD(GetDriverInfo)	(THIS_ void **lplpIA3dDriverBuffer,
					 void **lplpIDsDriverBuffer) PURE;
	STDMETHOD(SetNewBuffer)		(THIS_ LPBYTE lpBuffer,
					 DWORD dwBufferBytes) PURE;
	STDMETHOD(SetA3dDirectCtrl)	(THIS_ LPA3DCTRL_SRC_SUPER lpA3dCtrlDirect,
					 DWORD dwSize) PURE;
};

/* -------------------------------------------------------------------------- */

#undef  INTERFACE
#define INTERFACE IA3dDalBuffer2

DECLARE_INTERFACE_(IA3dDalBuffer2, IUnknown)
{
	STDMETHOD(QueryInterface)	(THIS_ REFIID, void **) PURE;
	STDMETHOD_(ULONG,AddRef)	(THIS) PURE;
	STDMETHOD_(ULONG,Release)	(THIS) PURE;

	STDMETHOD(SetA3dSuperCtrl)	(THIS_ LPA3DCTRL_SRC_SUPER lpA3dCtrlSuper,
					 DWORD dwSize) PURE;
	STDMETHOD(GetAllocationStatus)	(THIS_ LPDWORD lpdwStatus) PURE;
	STDMETHOD(GetWave)		(THIS_ LPBYTE *lplpWave) PURE;
	STDMETHOD(GetDriverInfo)	(THIS_ void **lplpIA3dDriverBuffer,
					 void **lplpIDsDriverBuffer) PURE;
	STDMETHOD(SetNewBuffer)		(THIS_ LPBYTE lpBuffer,
					 DWORD dwBufferBytes) PURE;
	STDMETHOD(SetA3dDirectCtrl)	(THIS_ LPA3DCTRL_SRC_SUPER lpA3dCtrlDirect,
					 DWORD dwSize) PURE;

	/* The two IA3dDalBuffer2 adds. */

	STDMETHOD(GetCurrentPositionEx)	(THIS_ LPDWORD lpdwPlay, LPDWORD lpdwWrite,
					 LPDWORD lpdwReserved) PURE;
	STDMETHOD(GetStatusEx)		(THIS_ LPDWORD lpdwStatus) PURE;
};

/* -------------------------------------------------------------------------- */

/* Initialize flags. */

#define A3DINIT_DEFAULT1			0x00000001
#define A3DINIT_DISABLE_SPLASHSCREEN		0x00000002
#define A3DINIT_DEFAULT2			0x00000008
#define A3DINIT_DISABLE_DS3D			0x00000010
#define A3DINIT_DISABLE_DS			0x00000020
#define A3DINIT_DISABLE_A3D			0x00000040

/* IA3d2::RegisterVersion() accepts only these two. */

#define A3D_APP_VERSION_10			10
#define A3D_APP_VERSION_12			12

/* What GetA3dCaps() puts in the first field. */

#define A3DCAPS_DEVICE_TYPE_AU8810		0x0100
#define A3DCAPS_DEVICE_TYPE_AU8820		0x0101
#define A3DCAPS_DEVICE_TYPE_AU8830		0x0104
#define A3DCAPS_DEVICE_TYPE_A2D			0x010C
#define A3DCAPS_DEVICE_TYPE_D3D			0x010D

/* -------------------------------------------------------------------------- */

/*
 * Voice state - the word every buffer in this DLL keeps and answers with.
 * Four classes lay it at four different offsets and all give it the same three
 * values, which is why it is one vocabulary and not four.  A voice constructed
 * and not yet played holds nought, which is no state of these three.
*/

#define A3DVOICE_STATE_PLAYING			1
#define A3DVOICE_STATE_STOPPED			2
#define A3DVOICE_STATE_RELEASED			3

/*
 * Voice flags - the word beside it, and the same two bits everywhere.
 *
 * LOOPING is Play()'s DSBPLAY_LOOPING kept for later: the position walk reads
 * it to decide whether running off the end wraps or stops.  ENABLED is the
 * control interface's alone.
*/

#define A3DVOICE_FLAG_ENABLED			0x00000001
#define A3DVOICE_FLAG_LOOPING			0x00000002

/*
 * The two format bits in the same word, which A3dVoiceOpen() sets.  They are
 * not the encoding A2DBuffer uses for the same two facts, which is 32 or 16 for
 * the width and 1 or 6 for the channels, in a word of its own.
*/

#define A3DVOICE_FLAG_STEREO			0x00000004
#define A3DVOICE_FLAG_16BIT			0x00000008

#endif /* _A3D_IA3DDAL_H */
