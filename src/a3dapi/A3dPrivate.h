/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dPrivate.h
 *
 * Provides the common system, SDK and internal interface declarations
 * used throughout a3dapi.dll. It brings together Windows, COM,
 * DirectSound, the published A3D API and the private DAL contracts.
 *
 * Shared constants, diagnostic macros and supporting data declarations
 * live here so the implementation uses consistent types and build
 * conventions. Concrete engine classes are declared in their own headers;
 * this file is the common include layer for those declarations.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _A3DPRIVATE_H
#define _A3DPRIVATE_H

#include <windows.h>

#if defined(A3D_FIXES)
#include "A3dConfig.h"
#endif
#include <mmsystem.h>
#include <objbase.h>
#include <dsound.h>

#include "ia3dapi.h"
#include "a3d33.h"

#ifdef _DEBUG

#include <crtdbg.h>

/* These project-named macros wrap CRT assertions and Windows debugger output;
 *  In Debug, ASSERT uses _ASSERTE
 * to report a failed condition with its expression, file and line. We accept
 * the current CRT's message format instead of reproducing historical text.
 * VERIFY also evaluates the expression in Retail, so use it for calls that
 * must run in both builds. TRACE prints formatted CRT warnings; _RPT_BASE
 * lets one macro handle all argument counts with MSVC's traditional preprocessor.
 * DBGSTR prefixes text with "[A3D] " and sends it through OutputDebugStringA.
 * ASSERT, TRACE and DBGSTR do nothing in Retail and do not evaluate their
 * arguments.
 */

#ifndef ASSERT
#define ASSERT		_ASSERTE
#endif
#ifndef VERIFY
#define VERIFY		ASSERT
#endif

#ifndef TRACE
#define TRACE(...)	_RPT_BASE(_CRT_WARN, 0, 0, 0, __VA_ARGS__)
#endif

#ifndef DBGSTR
#define DBGSTR(s)	do { \
	OutputDebugStringA("[A3D] "); \
	OutputDebugStringA(s); \
} while (0)
#endif

#else	/* retail */

#ifndef ASSERT
#define ASSERT(f)		((void) 0)
#endif
#ifndef VERIFY
#define VERIFY(f)		((void) (f))
#endif
#ifndef TRACE
#define TRACE(...)		((void) 0)
#endif
#ifndef DBGSTR
#define DBGSTR(s)		((void) 0)
#endif

#endif

#include "../ia3ddal.h"

DEFINE_GUID(IID_IA3dScene,   0x103e7224, 0x0113, 0x11d2, 0x90, 0xfb, 0x00, 0x60, 0x08, 0xa1, 0xf4, 0x41);
DEFINE_GUID(IID_IA3dRoom,    0x103e7226, 0x0113, 0x11d2, 0x90, 0xfb, 0x00, 0x60, 0x08, 0xa1, 0xf4, 0x41);
DEFINE_GUID(IID_IA3dOpening, 0x103e722b, 0x0113, 0x11d2, 0x90, 0xfb, 0x00, 0x60, 0x08, 0xa1, 0xf4, 0x41);
DEFINE_GUID(IID_IA3dPrv1,    0x103e7222, 0x0113, 0x11d2, 0x90, 0xfb, 0x00, 0x60, 0x08, 0xa1, 0xf4, 0x41);
DEFINE_GUID(IID_IA3dPrv2,    0xbfe2be81, 0x4cb7, 0x11d2, 0x90, 0xfb, 0x00, 0x60, 0x08, 0xa1, 0xf4, 0x41);
DEFINE_GUID(IID_IA3dPrv3,    0x62507921, 0x3ea6, 0x11d2, 0x90, 0xfb, 0x00, 0x60, 0x08, 0xa1, 0xf4, 0x41);

#define IID_IA3dBuffer				IID_IA3dDalBuffer

/* Source private interface  */
DEFINE_GUID(IID_IA3dSrcPrv,   0xdf7a8824, 0x555b, 0x11d2, 0xa8, 0xe6, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);

/* Wrapper error for an unattached underlying object. */

#define _A3D_FACDS		0x878

#undef  MAKE_DSHRESULT
#define MAKE_DSHRESULT(code)	MAKE_HRESULT(1, _A3D_FACDS, code)

#define DSERR_UNINITIALIZED	MAKE_DSHRESULT(170)

DEFINE_GUID(IID_A3dVoiceCtl,  0xcbd91fa8, 0x41c6, 0x11d2, 0xbb, 0x48, 0x00, 0x60, 0x08, 0x2f, 0x3c, 0x00);

/* (RE) Static resource-manager buffer interface: dbg:0x10140AB8. */

DEFINE_GUID(IID_ResManStatBuffer,  0x2e56ef65, 0x74ce, 0x11d2, 0x97, 0x1b, 0x00, 0x60, 0x08, 0x8d, 0x87, 0x88);

/* (RE) Streaming resource-manager buffer interface: dbg:0x10140AA8. */

DEFINE_GUID(IID_ResManStreamBuffer, 0x03509d01, 0x74d0, 0x11d2, 0x97, 0x1b, 0x00, 0x60, 0x08, 0x8d, 0x87, 0x88);

/* (RE) Source super-control property set: dbg:0x10140B78; property 0. */

DEFINE_GUID(DSPROPSETID_A3dSourceSuper, 0x3f9e77a1, 0x3852, 0x11d3, 0xbb, 0x49, 0x00, 0x60, 0x08, 0x2f, 0x3c, 0x00);

/* =============================================================================
 * NOT ONE OF AUREAL'S DECLARATIONS AT THIS LOCATION.  Aureal had a header for
 * each vocabulary, Creative's eax.h among them. These are project-owned
 * compatibility declarations; Aureal's original headers are unavailable.
 * ========================================================================== */

/* (RE) dbg:0x10140A18; rtl:0x1005A180. */
DEFINE_GUID(DSPROPSETID_I3DL2_ListenerProperties,   0xda0f0520, 0x300a, 0x11d3, 0x8a, 0x2b, 0x00, 0x60, 0x97, 0x0d, 0xb0, 0x11);
/* (RE) dbg:0x10140A28; rtl:0x1005A190. */
DEFINE_GUID(DSPROPSETID_I3DL2_BufferProperties,     0xda0f0521, 0x300a, 0x11d3, 0x8a, 0x2b, 0x00, 0x60, 0x97, 0x0d, 0xb0, 0x11);
/* (RE) dbg:0x10140A38; rtl:0x1005A1A0. */
DEFINE_GUID(DSPROPSETID_EAX_ReverbProperties,       0x4a4e6fc1, 0xc341, 0x11d1, 0xb7, 0x3a, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
/* (RE) dbg:0x10140A48; rtl:0x1005A1B0. */
DEFINE_GUID(DSPROPSETID_EAXBUFFER_ReverbProperties, 0x4a4e6fc0, 0xc341, 0x11d1, 0xb7, 0x3a, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);

/* ========================================================================== */

/* (RE) Preserve DIRECTHF value 4 (dbg:0x1000962E) despite the IASIG index 2.
 * Reverb bulk senders likewise use change-mask bits as property identifiers;
 * Aureal's original header definitions are unavailable. */

#define DSPROPERTY_I3DL2LISTENER_ALL		0
#define DSPROPERTY_I3DL2BUFFER_DIRECTHF		4

/* EAX 1.0 property IDs, cross-checked against OpenAL Soft al/eax/api.h:
 * https://github.com/kcat/openal-soft/blob/master/al/eax/api.h
 * These sequential IDs are not the reverb sender's changed-bit masks. */
#define DSPROPERTY_EAX_ALL              0
#define DSPROPERTY_EAX_ENVIRONMENT      1
#define DSPROPERTY_EAX_VOLUME           2
#define DSPROPERTY_EAX_DECAYTIME        3
#define DSPROPERTY_EAX_DAMPING          4
#define DSPROPERTY_EAXBUFFER_REVERBMIX  1

/* I3DL2 listener properties: 0x30 bytes.
 * (RE) Block writer: rtl:0x10002540; dbg:0x10009D50. */

typedef struct
{
	LONG	lRoom;
	LONG	lRoomHF;
	FLOAT	flRoomRolloffFactor;
	FLOAT	flDecayTime;
	FLOAT	flDecayHFRatio;
	LONG	lReflections;
	FLOAT	flReflectionsDelay;
	LONG	lReverb;
	FLOAT	flReverbDelay;
	FLOAT	flDiffusion;
	FLOAT	flDensity;
	FLOAT	flHFReference;
} I3DL2_LISTENERPROPERTIES;

/* I3DL2 buffer properties: 0x24 bytes.
 * (RE) Detector: rtl:0x100025C0; dbg:0x1000A020. Only the total size and
 * final FLOAT are established; preceding names and types follow IASIG. */

typedef struct
{
	LONG	lDirect;
	LONG	lDirectHF;
	LONG	lRoom;
	LONG	lRoomHF;
	FLOAT	flRoomRolloffFactor;
	LONG	lObstruction;
	FLOAT	flObstructionLFRatio;
	LONG	lOcclusion;
	FLOAT	flOcclusionLFRatio;
} I3DL2_BUFFERPROPERTIES;

/* EAX listener properties: 0x10 bytes.
 * (RE) Block writer: dbg:0x10009E70. Type name follows Creative. */

typedef struct
{
	DWORD	dwEnvironment;
	FLOAT	fVolume;
	FLOAT	fDecayTime;
	FLOAT	fDamping;
} EAX_REVERBPROPERTIES;

#endif /* _A3DPRIVATE_H */
