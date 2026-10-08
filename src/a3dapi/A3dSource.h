/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dSource.h
 *
 * Declares CA3dSource and the supporting audio, decoder, playback-event
 * and reflection records used by source processing. The class connects
 * the public source interface to waveform storage, streaming state,
 * spatial parameters and resource-manager buffers.
 *
 * Its helper declarations cover direct-path and reflection control
 * generation as well as compressed-audio decoding. A3dSource.cpp
 * implements these operations; a3dsourcecom.h declares the
 * application-facing wrapper that owns a source implementation.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _A3DSOURCE_H
#define _A3DSOURCE_H

#include "A3dPrivate.h"
#include "LinkList.h"

class CA3dRoot;
class CA3dFrame;
class CA3dSourceCom;
class CA3dReflection;

template<class T> class CA3dStdList;

/* Initial EAX mix and accepted sentinel */
#define A3D_REVERB_MIX_DEFAULT (-1.0f)

struct _A3DPRIMITIVE;
typedef struct _A3DPRIMITIVE A3DPRIMITIVE;

/* A3DREFLECTIONREC: 0x74 bytes; solved reflection in a source-owned chain. */

typedef struct _A3DREFLECTIONREC
{
	/* 0x00 */ struct _A3DREFLECTIONREC *pNext;

	/* 0x04 */ DWORD  dwSurface;       /* the primitive's tag; matched frame to frame */
	/* 0x08 */ DWORD  dwSurface2;
		/* dwSurface again when dwKind is 1, else the caller's second tag */
	/* 0x0C */ A3DVAL avTruePoint[3];  /* True mirror point. */
	/* 0x18 */ A3DVAL avSoftPoint[3];  /* Scaled mirror point. */
	/* 0x24 */ int    nSlot;           /* -1 unplaced, -2 refused */
	/* 0x28 */ DWORD  dwKind;          /* 1 selects dwSurface for dwSurface2 */
	/* 0x2C */ BOOL   bNew;            /* a slot it did not hold last frame */
	/* 0x30 */ A3DVAL vHit[3];         /* the hit point */
	/* 0x3C */ A3DVAL fAzim;           /* that point in polar form */
	/* 0x40 */ A3DVAL fElev;
	/* 0x44 */ A3DVAL fRange;
	/* 0x48 */ A3DVAL fDistance;       /* fRange again; the delays work from it */
	/* 0x4C */ A3DVAL fHit;
	/* 0x50 */ A3DVAL afMaterial[2];
	/* 0x58 */ A3DVAL fEq;             /* the slot's fAlpha is 1 - this */
	/* 0x5C */ A3DVAL fGainLeft;
	/* 0x60 */ A3DVAL fGainRight;
	/* 0x64 */ A3DVAL fAudibility;
	/* 0x68 */ A3DVAL fDelayLeft;
	/* 0x6C */ A3DVAL fDelayRight;
	/* 0x70 */ DWORD  dwReserved70;
} A3DREFLECTIONREC, *LPA3DREFLECTIONREC;

typedef int A3dReflectionRecSizeCheck[(sizeof(A3DREFLECTIONREC) == 0x74) ? 1 : -1];

/* Source object size; (RE) allocation sites dbg:0x100305A5, dbg:0x1003098F. */

#define A3D_SOURCE_SIZE 0x3A8

/* Control block size; (RE) allocator dbg:0x1001F970. */

#define A3D_SOURCE_BUFFER_SIZE 0x40C

/* Distance below which ear placement collapses to a fixed pair, in metres. */

#define A3D_NEAR_FIELD_DISTANCE 0.071f

/* Unused legacy near-field gain; 677 does not scale ear gains here. */

#define A3D_NEAR_FIELD_GAIN 0.667f

/* Minimum-distance coefficient in the spread-gain falloff. */

#define A3D_SPREAD_KNEE 17.0f

/* AU8830 resampling rate, input-rate threshold and single-precision reciprocal. */

#define A3D_RESAMPLE_RATE            22050
#define A3D_RESAMPLE_BELOW           22000
#define A3D_RESAMPLE_RATE_RECIPROCAL 4.5351473e-05f

/* PENDING defers buffer playback until API rendering is enabled. */

#define A3D_PLAYSTATE_IDLE    0
#define A3D_PLAYSTATE_PENDING 1
#define A3D_PLAYSTATE_PLAYING 2

/* Streaming flag above the published A3DSOURCE_FORMAT_* low-word type.
 * (RE) SetPlayEvent format switch: dbg:0x1002CF50. */

#define A3DSOURCE_FORMAT_STREAMING 0x00010000

/* MP3 decoder table: rtl:0x10054D30; dbg:0x1013B5B8.
 * Constructor: dbg:0x100A5590.
 * Mp3Ssc failure statuses have top bits 10 or 11, unlike HRESULT.
 * The four unused slots below retain placeholder signatures and cannot be
 * called through these declarations. */

struct IA3dMp3Format;

/* Status severity bits used by the loader and formatter */
#define A3D_MP3_STATUS_SEVERITY_MASK 0xC0000000

struct IA3dMp3Decoder
{
	/* +0x00 */ virtual void  Destroy(void) = 0;
	/* +0x04 */ virtual void  Reset(void) = 0;
	/* (RE) dbg:0x100A5820; rtl:0x100413E0. Float PCM decode: three
	 *    arguments, ret 12, status in EAX. The declaration remains a
	 *    placeholder. */
	/* +0x08 */ virtual void  DecodeFloatBlock(void) = 0;
	/* +0x0C */ virtual int   DecodeBlock(void *pvOut, DWORD cbOut,
	                                     DWORD *pcbProduced) = 0;

	/* +0x10 */ virtual struct IA3dMp3Format  *GetFormat(void) = 0;
	/* (RE) dbg:0x100A58E0; rtl:0x100414A0. One reader pointer argument,
	 *    ret 4; reader interface and return contract unresolved. Declaration
	 *    is a placeholder. */
	/* +0x14 */ virtual void  SetInputSource(void) = 0;
	/* +0x18 */ virtual DWORD SupplyInput(void *pvIn, DWORD cbIn) = 0;
	/* (RE) dbg:0x100A5910; rtl:0x100414D0. Returns free input bytes in
	 *    EAX; the declaration remains a placeholder. */
	/* +0x1C */ virtual void  GetInputFreeBytes(void) = 0;
	/* (RE) dbg:0x100A5920; rtl:0x100414E0. Returns buffered input bytes in
	 *    EAX; the declaration remains a placeholder. */
	/* +0x20 */ virtual void  GetInputBufferedBytes(void) = 0;
	/* +0x24 */ virtual void  EndOfInput(void) = 0;
};

/* Decoder-owned format descriptor; reference accessors dbg:0x10025150..dbg:0x100251F0.
 * Fields are accessed directly; the original struct/class form is unresolved. */

struct IA3dMp3Format
{
	/* 0x00 */ INT nMpegLayer;
	/* Accessor dbg:0x10025170; thunk dbg:0x10003BF2. */
	/* 0x04 */ INT nMpegVersion;
	/* Accessor dbg:0x10025190; thunk dbg:0x10001488. */
	/* 0x08 */ INT nBitrate;       /* Average bytes per second. */
	/* 0x0C */ INT nBitrateIndex;  /*  (RE) header bits 12..15, dbg:0x100AB2F4. */
	/* Accessor dbg:0x100251B0; thunk dbg:0x100016E0. */
	/* 0x10 */ INT nChannels;
	/* Accessor dbg:0x100251D0; thunk dbg:0x100020EF. */
	/* 0x14 */ INT nSamplerate;
	/*  (RE) format update dbg:0x100A5950. */
	/* 0x18 */ INT nOutputChannels;
	/* 0x1C */ INT nOutputSamplerate;
	/* 0x20 */ INT nBitsPerSample;
};

typedef struct IA3dMp3Decoder *LPA3DMP3DECODER;
typedef struct IA3dMp3Format  *LPA3DMP3FORMAT;

/* The reference factory takes three arguments (dbg:0x100A51B0,
 * wrapper for dbg:0x100A51D0); this adapter takes only the output pointer. */

int          Mp3SscCreateDecoder(LPA3DMP3DECODER *ppDecoder);
const char  *Mp3SscErrorString(int nStatus);

/* A3DMP3STATE: 0x2C bytes; compressed input and decoded PCM buffers.
 * (RE) Constructor: dbg:0x10025210; destructor: dbg:0x10025460. */

typedef struct _A3DMP3STATE
{
	/* 0x00 */ DWORD  dwInputSize;    /* pInput capacity, 0x2000 */
	/* 0x04 */ LPVOID pInput;         /* compressed input, one mmioRead at a time */
	/* 0x08 */ DWORD  dwInputAvail;   /* unread bytes in pInput */
	/* 0x0C */ DWORD  dwConsumed;     /* bytes the decoder took on the last supply */
	/* 0x10 */ DWORD  dwInputPos;     /* read cursor into pInput */
	/* 0x14 */ DWORD  dwHeaderBytes;  /* what the header decode produced */
	/* 0x18 */ DWORD  dwDecodedSize;  /* Estimated whole-file PCM size in bytes. */
	/* 0x1C */ DWORD  dwDecodeSize;   /* pDecode capacity, 4608 for a stream */
	/* 0x20 */ DWORD  dwDecodeFill;   /* bytes the last block put in pDecode */
	/* 0x24 */ DWORD  dwDecodeLeft;   /* bytes in pDecode not yet copied out */
	/* 0x28 */ LPVOID pDecode;        /* decode scratch buffer */
} A3DMP3STATE, *LPA3DMP3STATE;

typedef int A3dMp3StateSizeCheck[(sizeof(A3DMP3STATE) == 0x2C) ? 1 : -1];

/* A3DAC3STATE: 0x2C bytes; AC-3 decoder handle, buffers and read state.
 * (RE) Constructor: dbg:0x100264E0. */

typedef struct _A3DAC3STATE
{
	/* 0x00 */ DWORD  dwInputSize;            /* 3840 */
	/* 0x04 */ DWORD  dwDecodeSize;           /* 36864 */
	/* 0x08 */ WORD   wSyncWord;              /* 0 */
	/* 0x0A */ WORD   wReserved0A;
	/* 0x0C */ DWORD  dwCrcPending;           /* 1 */
	/* 0x10 */ A3DVAL fDefaultPositionScale;  /* Fallback PCM/input byte scale */
	/* 0x14 */ A3DVAL fSegmentPositionScale;  /* Segment PCM/input byte ratio */
	/* 0x18 */ DWORD  hDecoder;               /* handle from the bundled AC-3 OpenAudio */
	/* 0x1C */ LPVOID pInput;                 /* new(dwInputSize) */
	/* 0x20 */ LPVOID pDecode;                /* new(dwDecodeSize) */
	/* 0x24 */ LPVOID pDecodeLeft;
		/* start of the bytes decoded but not yet copied out */
	/* 0x28 */ DWORD  dwDecodeLeft;           /* Uncopied decoded bytes. */

	/* (RE) dbg:0x1002DD20; thunk dbg:0x10003440; deleting destructor
	 *    dbg:0x1002DCD0 */
	~_A3DAC3STATE(void)
	{
		::operator delete(pInput);
		::operator delete(pDecode);
	}
} A3DAC3STATE, *LPA3DAC3STATE;

typedef int A3dAc3StateSizeCheck[(sizeof(A3DAC3STATE) == 0x2C) ? 1 : -1];

/* A3DAC3CONFIG: 0x38 bytes; bundled decoder input and output configuration. */

typedef struct _A3DAC3CONFIG
{
	/* 0x00 */ DWORD  dwMode;         /* 2; passed to Ac3OpenAudio */
	/* 0x04 */ DWORD  dwFrameCount;
		/* Frame loop bound (RE) dbg:0x100AD073, dbg:0x100AD364. */
	/* 0x08 */ DWORD  dwDecodeSize;   /* the decode buffer size */
	/* 0x0C */ DWORD  dwProduced;     /* Produced PCM bytes. */
	/* 0x10 */ LPVOID pDecode;        /* the AC-3 decode buffer */
	/* 0x14 */ LPVOID pInput0;        /* the AC-3 input buffer */
	/* 0x18 */ LPVOID pFrameStart;    /* Current sync frame start in pInput0. */
	/* 0x1C */ LPVOID pFrameEnd;
		/* current sync frame end, pFrameStart + 2 * frame words */
	/* 0x20 */ DWORD  dwField20;      /* 0 */
	/* 0x24 */ DWORD  dwField24;      /* 2 */
	/* 0x28 */ DWORD  Reserved28[3];  /* 0 */
	/*  (RE) output dbg:0x100AD43D, table dbg:0x1014F820. */
	/* 0x34 */ DWORD dwFullBandwidthChannels;
} A3DAC3CONFIG, *LPA3DAC3CONFIG;

typedef int A3dAc3ConfigSizeCheck[(sizeof(A3DAC3CONFIG) == 0x38) ? 1 : -1];

/* Loader requests another frame  */
#define A3D_AC3_MORE_INPUT 139

DWORD Ac3OpenAudio(DWORD dwMode);
void  Ac3CloseAudio(DWORD hDecoder);
int   Ac3DecodeAudio(DWORD hDecoder, LPA3DAC3CONFIG pConfig);
int   Ac3ResetAudio(DWORD hDecoder, DWORD dwMode);

/* A3DPLAYEVENT: 0x0C bytes; position mark and notification event. */

typedef struct _A3DPLAYEVENT
{
	/* 0x00 */ DWORD  dwMark;      /* effective mark; -2 in all-buffers mode */
	/* 0x04 */ DWORD  dwPosition;  /* the requested position, the search key */
	/* 0x08 */ HANDLE hEvent;      /* signalled when the position is reached */
} A3DPLAYEVENT, *LPA3DPLAYEVENT;

class CA3dFrame;
class CA3dRoot;

/* =============================================================
// Class: CA3dSource
//
// Description: Audio source with playback, streaming and wavetracing state.
//
// Size: 0x3A8
//
// (RE) Constructor: rtl:0x1000C360; dbg:0x1001E330
// =============================================================*/

/* IA3dSource2: 79 slots, rtl:0x10052CFC; dbg:0x101292E4;
 * slot 78 is the deleting destructor.
 * IA3dSrcPrv at +4: 5 slots, dbg:0x101292CC; slots 0..2 adjust this by -4. */

class CA3dSource : public IA3dSource2,
                   public IA3dSrcPrv
{
public:
	CA3dSource(IDirectSound *pDS, CA3dRoot *pApi, CA3dSourceCom *pSourceCom,
	           DWORD dwFlags, HRESULT *phr);

	CA3dSource(CA3dSource *pSource, CA3dSourceCom *pSourceCom, HRESULT *phr);
	virtual ~CA3dSource(void);

	STDMETHODIMP         QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG) AddRef(void);
	STDMETHODIMP_(ULONG) Release(void);

	STDMETHODIMP         LoadWaveFile(LPSTR pszFileName);            /* slot 3 */
	STDMETHODIMP         LoadWaveData(LPVOID pvData, DWORD dwSize);  /* slot 4 */
	STDMETHODIMP         AllocateWaveData(INT nBytes);
	STDMETHODIMP         FreeWaveData(void);
	STDMETHODIMP         SetWaveFormat(LPVOID pvFormat);
	STDMETHODIMP         GetWaveFormat(LPVOID pvFormat);
	STDMETHODIMP_(DWORD) GetWaveSize(void);
	STDMETHODIMP         GetType(LPDWORD pdwType);
	STDMETHODIMP         Lock(DWORD dwOffset, DWORD dwBytes, LPVOID *ppvAudioPtr1,
	                          LPDWORD pdwAudioBytes1, LPVOID *ppvAudioPtr2,
	                          LPDWORD pdwAudioBytes2, DWORD dwFlags);      /* slot 11 */
	STDMETHODIMP         Unlock(LPVOID pvAudioPtr1, DWORD dwAudioBytes1,
	                            LPVOID pvAudioPtr2, DWORD dwAudioBytes2);  /* slot 12 */
	STDMETHODIMP         Play(INT nMode);  /* slot 13 */
	STDMETHODIMP         Stop(void);       /* slot 14 */
	STDMETHODIMP         Rewind(void);
	STDMETHODIMP         SetWaveTime(A3DVAL fTime);
	STDMETHODIMP         GetWaveTime(LPA3DVAL pfTime);
	STDMETHODIMP         SetWavePosition(DWORD dwPosition);
	STDMETHODIMP         GetWavePosition(LPDWORD pdwPosition);
	STDMETHODIMP         SetPosition3f(A3DVAL x, A3DVAL y, A3DVAL z);
	STDMETHODIMP         GetPosition3f(LPA3DVAL px, LPA3DVAL py, LPA3DVAL pz);
	STDMETHODIMP         SetPosition3fv(LPA3DVAL pv);
	STDMETHODIMP         GetPosition3fv(LPA3DVAL pv);
	STDMETHODIMP         SetOrientationAngles3f(A3DVAL h, A3DVAL p, A3DVAL r);
	STDMETHODIMP         GetOrientationAngles3f(LPA3DVAL ph, LPA3DVAL pp,
	                                            LPA3DVAL pr);
	STDMETHODIMP         SetOrientationAngles3fv(LPA3DVAL pv);
	STDMETHODIMP         GetOrientationAngles3fv(LPA3DVAL pv);
	STDMETHODIMP         SetOrientation6f(A3DVAL fx, A3DVAL fy, A3DVAL fz,
	                                      A3DVAL ux, A3DVAL uy, A3DVAL uz);
	STDMETHODIMP         GetOrientation6f(LPA3DVAL pfx, LPA3DVAL pfy, LPA3DVAL pfz,
	                                      LPA3DVAL pux, LPA3DVAL puy, LPA3DVAL puz);
	STDMETHODIMP         SetOrientation6fv(LPA3DVAL pv);
	STDMETHODIMP         GetOrientation6fv(LPA3DVAL pv);
	STDMETHODIMP         SetVelocity3f(A3DVAL x, A3DVAL y, A3DVAL z);
	STDMETHODIMP         GetVelocity3f(LPA3DVAL px, LPA3DVAL py, LPA3DVAL pz);
	STDMETHODIMP         SetVelocity3fv(LPA3DVAL pv);
	STDMETHODIMP         GetVelocity3fv(LPA3DVAL pv);
	STDMETHODIMP         SetCone(A3DVAL fInnerAngle, A3DVAL fOuterAngle,
	                             A3DVAL fOutsideGain);
	STDMETHODIMP         GetCone(LPA3DVAL pfInnerAngle, LPA3DVAL pfOuterAngle,
	                             LPA3DVAL pfOutsideGain);
	STDMETHODIMP         SetMinMaxDistance(A3DVAL fMin, A3DVAL fMax, DWORD dwMode);
	STDMETHODIMP         GetMinMaxDistance(LPA3DVAL pfMin, LPA3DVAL pfMax,
	                                       LPDWORD pdwMode);
	STDMETHODIMP         SetGain(A3DVAL fGain);
	STDMETHODIMP         GetGain(LPA3DVAL pfGain);
	STDMETHODIMP         SetPitch(A3DVAL fPitch);
	STDMETHODIMP         GetPitch(LPA3DVAL pfPitch);
	STDMETHODIMP         SetDopplerScale(A3DVAL fScale);  /* 44 */
	STDMETHODIMP         GetDopplerScale(LPA3DVAL pfScale);
	STDMETHODIMP         SetDistanceModelScale(A3DVAL fScale);  /* 46 */
	STDMETHODIMP         GetDistanceModelScale(LPA3DVAL pfScale);
	STDMETHODIMP         SetEq(A3DVAL fEq);
	STDMETHODIMP         GetEq(LPA3DVAL pfEq);
	STDMETHODIMP         SetPriority(A3DVAL fPriority);
	STDMETHODIMP         GetPriority(LPA3DVAL pfPriority);
	STDMETHODIMP         SetRenderMode(DWORD dwMode);
	STDMETHODIMP         GetRenderMode(LPDWORD pdwMode);
	STDMETHODIMP         GetAudibility(LPA3DVAL pfAudibility);
	STDMETHODIMP         GetOcclusionFactor(LPA3DVAL pfFactor);
	STDMETHODIMP         GetStatus(LPDWORD pdwStatus);  /* slot 56 */

	STDMETHODIMP Unknown_0x0C(LPVOID pv);                     /* slot 3 */
	STDMETHODIMP RenumberReflections(LPVOID *paReflections);  /* slot 4 */

	friend class CA3dRoot;
	friend class CA3dSourceCom;
	friend class CA3dReflection;
	friend class CRefAudBin;

	friend void    A3dTraceApply(CA3dSource *pSource, A3DVAL fOcclusion,
	                             const A3DVAL *pav);
	friend void    A3dSourceOcclude(class CA3dRoot *pApi,
	                                CA3dSource *pSource);
	friend void    A3dTraceSetVector(CA3dSource *pSource,
	                                 const A3DVAL *pvAxis,
	                                 const A3DVAL *pvToListener,
	                                 A3DVAL fDistance);
	friend A3DVAL  A3dConeAngle(CA3dSource *pSource, const A3DVAL *pvAxis,
	                            const A3DVAL *pvToListener,
	                            A3DVAL fDistance);
	friend A3DVAL  A3dConeAngleXform(CA3dSource *pSource);
	friend void    A3dTraceSetEars(CA3dSource *pSource, A3DVAL *pvOut);
	friend void    A3dSetDelay(CA3dSource *pSource, const A3DVAL *pvPolar);
	friend void    A3dDistanceGain(CA3dSource *pSource, A3DVAL fDistance,
	                               A3DVAL *pfGain);
	friend void    A3dSourceQuiet(CA3dSource *pSource);
	friend void    A3dSourceResetReflections(CA3dSource *pSource);
	friend void    A3dSourceSeedDirect(class CA3dSource *pSource);
	friend void    A3dSourceGetMatrix(CA3dSource *pSource, A3DVAL *pm);
	friend void    A3dSourceVolumetric(CA3dSource *pSource, A3DVAL *pm);
	friend void    A3dSourceRecalc(CA3dSource *pSource);
	friend void    A3dSourceCarry(CA3dSource *pSource);
	friend void    A3dSourceStep(CA3dSource *pSource,
	                             const A3DVAL *pvLeftEar,
	                             const A3DVAL *pvRightEar);
	friend void    A3dSourceStepEnd(CA3dSource *pSource, int fForce);
	friend void    PushRenderMode(CA3dSource *pSource);
	friend HRESULT A3dSourceEmit(CA3dSource *pSource);
	friend int     A3dSourceTraceSkip(CA3dSource *pSource);
	friend void    A3dSourcePushB(CA3dSource *pSource, int cActive);

	friend LPA3DREFLECTIONREC A3dChainNext(CA3dSource *pSource,
	                                       LPA3DREFLECTIONREC pLink);
	friend LPA3DREFLECTIONREC A3dReflectionNew(CA3dSource *pSource);

	friend void   A3dSourcePushA(CA3dSource *pSource, int cActive);
	friend int    A3dSourcePushBudget(CA3dSource *pSource, int cActive);
	friend int    A3dSourcePushMatch(CA3dSource *pSource, int cWanted,
	                                 DWORD *pdwUsed);
	friend void   A3dSourcePushAssign(CA3dSource *pSource, int cUnplaced,
	                                  DWORD dwUsed);
	friend void   A3dStepDelayPair(CA3dSource *pSource);
	friend void   A3dStepAngles(CA3dSource *pSource,
	                            const A3DVAL *pvLeftEar,
	                            const A3DVAL *pvRightEar);
	friend void   A3dStepEarGains(CA3dSource *pSource);
	friend void   A3dStepEq(CA3dSource *pSource);
	friend void   A3dSourcePushLocal(CA3dSource *pSource);
	friend void   A3dReflection(CA3dSource *pSource, DWORD dwSurface,
	                            DWORD dwSurface2, DWORD dwKind,
	                            const A3DVAL *pvHit, A3DVAL fHit,
	                            const A3DVAL *pavMaterial,
	                            const A3DVAL *pavTruePoint,
	                            const A3DVAL *pavSoftPoint);
	friend void   A3dStepMidGain(CA3dSource *pSource);
	friend void   A3dStepPitchOut(CA3dSource *pSource);
	friend void   A3dSpreadGain(CA3dSource *pSource, A3DVAL fDistance,
	                            A3DVAL *pfGain);
	friend void   A3dTraceSetSpread(CA3dSource *pSource, A3DVAL fLeft,
	                                A3DVAL fRight);
	friend void   A3dDoppler(CA3dSource *pSource, const A3DVAL *pvVelSource,
	                         const A3DVAL *pvVelListener,
	                         const A3DVAL *pvTo, A3DVAL fDistance);
	friend A3DVAL A3dVolumetricCoverage(CA3dSource *pSource,
		int (*pfnHit)(const A3DVAL *pvFrom, const A3DVAL *pvTo,
		             A3DPRIMITIVE *pPrim, int nLoose, int nInfinite),
		const A3DVAL *pvListener, A3DPRIMITIVE *pPrim);
	friend A3DVAL A3dVolumetricSizeDamp(CA3dSource *pSource,
	                                    A3DPRIMITIVE *pPrim);

protected:
	/* Current and previous reflection chains, counts and active-chain index. */
	/* 0x08 */ LPA3DREFLECTIONREC m_apChain[2];
	/* 0x10 */ int                m_acChain[2];
	/* 0x18 */ int                m_nChain;

	/* Factor and two occlusion values; name by role. */
	/* 0x1C */ A3DVAL m_afOcclusionCache[3];

	/* Application position marks and internal streaming marks.
	 * Reference list constructor dbg:0x1002F1B0; base-class mapping unresolved:
	 * constructors dbg:0x1001EF10 / dbg:0x1001EF40, purecall tables
	 * dbg:0x10129460 / dbg:0x101295D8. */
	/* 0x28 */ CA3dPtrList<A3DPLAYEVENT> *m_pUserWaveEvents;
	/* 0x2C */ CA3dPtrList<A3DPLAYEVENT> *m_pStreamWaveEvents;

	/* Packed notify array for IDirectSoundNotify::SetNotificationPositions(). */
	/* 0x30 */ LPDSBPOSITIONNOTIFY m_pNotifyPositions;
	/* 0x34 */ DWORD               m_dwVelocitySet;         /* 0 at construction */
	/* 0x38 */ LONG                m_cRef;                  /* plain ++/-- */
	/* 0x3C */ A3DVAL              m_avReflectionPoint[3];
		/* Point mirrored by the reflection walk. */
	/* 0x48 */ A3DVAL              m_fReserved38;           /* 1.0f */
	/* 0x4C */ DWORD               m_dwOnSourceList;
		/* Controls destructor unlink; cleared during API teardown. */

	/* Alternates per trace pass; modulus decides phase. */
	/* 0x50 */ DWORD m_fTraced;
	/* 0x54 */ DWORD m_fManualReflections;  /* NewManualReflection() sets it to 1 */
	/* 0x58 */ DWORD m_dwPlayState;         /* A3D_PLAYSTATE_* */
	/* 0x5C */ DWORD m_fLooping;            /* the mode Play() was given */
	/* 0x60 */ DWORD m_dwPlayFlags;
		/* a streaming source takes Play()'s mode here instead */

	/* 0x64 */ DWORD                 m_dwRenderMode;   /* A3DSOURCE_RENDERMODE_DEFAULT */
	/* 0x68 */ IDirectSound         *m_pDS;            /* held with a reference */
	/* 0x6C */ IDirectSoundBuffer   *m_pBuffer;        /* Wave buffer. */
	/* 0x70 */ IDirectSound3DBuffer *m_pBuffer3D;      /* queried from m_pBuffer */
	/* 0x74 */ IA3dDalBuffer        *m_pAurealBuffer;
		/* optional; zeroed when the query fails */

	/* IResManBuffer at buffer +8, queried with IID_A3dVoiceCtl.
	 * Slot 11 returns the control blocks; GetStatus requires the primary interface. */
	/* 0x78 */ IUnknown *m_pA3dVoiceCtl;
	/* 0x7C */ A3DVAL    m_fRateRatio;    /* Source-to-resampled rate ratio. */

	/* Position of each ear along the line between them. */
	/* 0x80 */ A3DVAL m_fEarLeft;
	/* 0x84 */ A3DVAL m_fEarRight;
	/* 0x88 */ A3DVAL m_afPanValues[2];       /* One or two pan values. */
	/* 0x90 */ A3DVAL m_fGain;                /* 1.0f */
	/* 0x94 */ A3DVAL m_fEq;                  /* Requested equalisation. */
	/* 0x98 */ A3DVAL m_fEqCoefficient;       /* Mapped equalisation coefficient. */
	/* 0x9C */ A3DVAL m_fPitch;               /* 1.0f */
	/* 0xA0 */ A3DVAL m_fDopplerScale;        /* 1.0f */
	/* 0xA4 */ A3DVAL m_fDistanceModelScale;  /* 1.0f */

	/* 0xA8 */ A3DVAL m_fReflectionDelayScale;  /* 1.0f */
	/* 0xAC */ A3DVAL m_fReflectionGainScale;   /* 1.0f */
	/* 0xB0 */ A3DVAL m_fMinDistance;           /* Scaled by the API distance unit. */
	/* 0xB4 */ A3DVAL m_fMaxDistance;           /* Scaled by the API distance unit. */
	/* 0xB8 */ DWORD  m_dwDistanceMode;

	/* Interaural delay, in seconds, from azimuth (see A3dSetDelay()). */
	/* 0xBC */ A3DVAL m_fDelay;

	/* The Doppler result: a pitch factor, one for no shift. */
	/* 0xC0 */ A3DVAL m_fDoppler;  /* 1.0f */

	/* Trace output: gain per ear and equalisation amount. */
	/* 0xC4 */ A3DVAL m_afEarGain[2];  /* 1.0f */
	/* 0xCC */ A3DVAL m_fEqAmount;     /* 1.0f */

	/* Occlusion ramp: [0] gain for both ears, [1] equalisation. */
	/* 0xD0 */ A3DVAL m_afOcclusionTarget[2];  /* 1.0f */
	/* 0xD8 */ A3DVAL m_afOcclusionValue[2];   /* 1.0f */
	/* 0xE0 */ A3DVAL m_afOcclusionRate[2];

	/* The third occlusion gain, which GetOcclusionFactor() hands back. */
	/* 0xE8 */ A3DVAL m_fOcclusion;
	/* 0xEC */ A3DVAL m_fConeInnerAngle;
	/* 0xF0 */ A3DVAL m_fConeOuterAngle;
	/* 0xF4 */ A3DVAL m_fConeOutsideGain;  /* 1.0f */

	/* Cone result from angle between source axis and listener. */
	/* 0xF8 */ A3DVAL m_fConeGain;    /* 1.0f */
	/* 0xFC */ A3DVAL m_fReservedCC;  /* 1.0f */

	/* 0x100 */ DWORD m_dwTransformMode;
	/* 0x104 */ DWORD m_dwNativeModeDirty;  /* 1 at construction */
	/* 0x108 */ DWORD m_dwFlags;            /* as handed to NewSource; GetType() returns it */

	/* Stepped once per trace pass, modulo phases. Spreads tracing over sources. */
	/* 0x10C */ DWORD m_dwTraceCount;

	/* Reverb property set and pending mix update. */
	/* 0x110 */ LPA3DPROPERTYSET m_pReverbPropSet;
	/* 0x114 */ DWORD            m_dwReverbMixDirty;  /* Pending mix or direct-HF update. */
	/* 0x118 */ A3DVAL           m_fReverbMix;        /* -2.0f */
	/* 0x11C */ A3DVAL           m_fReverbDirectHF;   /* 1.0f */
	/* 0x120 */ A3DVAL           m_matSource[16];
		/* the API's current matrix, or identity */

	/* Volumetric working set: 128 bytes, containing box axes and inverse frame. */
	union
	{
		/* 0x160 */ DWORD m_adwVolumeWorkingSet[32];
		struct
		{
			/* 0x160 */ A3DVAL m_aavVolumeAxis[3][4];
				/* three box half-axis vectors, homogeneous */
			/* 0x190 */ DWORD  m_adwVolumeTranslationRow[4];
				/* Matrix elements 12..15  */
			/* 0x1A0 */ A3DVAL m_matVolumeInverse[16];
				/* Inverse volumetric frame. */
		};
	};

	/* 0x1E0 */ A3DVAL         m_vPosition[3];
	/* 0x1EC */ A3DVAL         m_fReserved128;      /* 1.0f */
	/* 0x1F0 */ A3DVAL         m_vOrientAngles[3];
	/* 0x1FC */ DWORD          m_dwReserved138;
	/* 0x200 */ A3DVAL         m_vVelocity[3];
	/* 0x20C */ DWORD          m_dwReserved148;
	/* 0x210 */ A3DVAL         m_vFront[3];
		/* 0, 0, 1, or 0, 0, -1 in a left-handed API */
	/* 0x21C */ DWORD          m_dwReserved158;
	/* 0x220 */ A3DVAL         m_vUp[3];            /* 0, 1, 0 */
	/* 0x22C */ DWORD          m_dwReserved168;
	/* 0x230 */ CA3dRoot      *m_pApi;              /* the object that made it */
	/* 0x234 */ CA3dSourceCom *m_pSourceCom;        /* the wrapper that allocated it */

	/* Set when angles are the authoritative orientation. */
	/* 0x238 */ DWORD m_fOrientAngles;  /* 1 at construction */

	/* A3DSOURCE_FORMAT_* in low word; 0x10000 set for streamed source. */
	/* 0x23C */ DWORD m_dwFormat;

	/* Returned by GetCaps(), kept as published structure. dwSize = 44. */
	/* 0x240 */ A3DCAPS_SOURCE m_SourceCaps;

	/* Wave format, held as allocated WAVEFORMATEX. SetAudioFormat() copies 18 bytes. */
	/* 0x26C */ LPWAVEFORMATEX  m_pwfxFormat;
	/* 0x270 */ DWORD           m_cbFormat;
	/* 0x274 */ DWORD           m_dwWaveSize;           /* Resident audio byte count. */
	/* 0x278 */ DWORD           m_dwStreamSegmentSize;  /* Streaming segment length in bytes. */
	/* 0x27C */ DWORD           m_dwStreamSize;         /* Logical stream byte count. */
	/* 0x280 */ DWORD           m_dwStreamRemaining;    /* Unread bytes in the data chunk. */
	/* 0x284 */ LPA3DMP3DECODER m_pMp3Decoder;          /* Owned MP3 decoder. */
	/* 0x288 */ LPA3DMP3STATE   m_pMp3Decode;           /* Owned MP3 state and buffers. */
	/* 0x28C */ LPA3DAC3CONFIG  m_pAc3Format;           /* Owned AC-3 I/O configuration. */
	/* 0x290 */ LPA3DAC3STATE   m_pAc3Decode;           /* Owned AC-3 state and buffers. */

	/* Owned 12-byte container of manual reflection pointers. */
	/* 0x294 */ CA3dStdList<CA3dReflection *> *m_pManualReflections;

	/* 0x298 */ DWORD m_fStreaming;
	/* 0x29C */ DWORD m_dwStreamEnded;
		/* Non-looping stream exhausted; suppresses worker refills. */

	/* Non-streaming: {mark, value}; streaming: mmio handle and data offset. */
	union
	{
		/* 0x2A0 */ DWORD m_adwPlayMark[2];
		struct
		{
			/* 0x2A0 */ HMMIO m_hmmio;
			/* 0x2A4 */ DWORD m_dwStreamDataOffset;
		};
	};

	/* Four {position, value} pairs. GetPlayPosition() walks backwards. */
	/* 0x2A8 */ DWORD m_aPlayEventMark[4][2];

	/* One flag per buffer segment. UpdateStreamBuffer() stores decode success. */
	/* 0x2C8 */ DWORD m_adwStreamSegmentValid[3];
	/* 0x2D4 */ DWORD m_dwSeekPosition;

	/* Manual-reset streaming events and event-list mutex. */
	/* 0x2D8 */ HANDLE m_ahStreamEvents[4];
	/* 0x2E8 */ HANDLE m_hEventMutex;
	/* 0x2EC */ A3DVAL m_fPriority;          /* 0.5f; stored, never acted on */

	/* Two source control blocks and the active index. Owned when
	 * m_pApi->m_dwUseDalInterface is set; otherwise borrowed from the driver. */
	/* 0x2F0 */ LPA3DCTRL_SRC_SUPER m_apBuffer[2];
	/* 0x2F8 */ DWORD               m_nBuffer;
	/* 0x2FC */ LPA3DCTRL_SRC_SUPER m_pBufferCurr;  /* starts at m_apBuffer[0] */
	/* Cached occluding list (borrowed) and element index; material follows at 0x308. */
	/* 0x300 */ class CA3dList *m_pOccludingList;
	/* 0x304 */ DWORD           m_dwOccludingElementIndex;

	/* 24-byte occlusion-material cache, overlaid with legacy wave-format
	 * aliases carried from 2.02; 677 keeps the format in m_pwfxFormat. */
	union
	{
		/* 0x308 */ DWORD m_adwOcclusionMaterialCache[6];
			/* (RE) dbg:0x10014042. */
		struct
		{
			DWORD m_dwSamplesPerSec;
			WORD  m_wBitsPerSample;
			WORD  m_wBlockAlign;
			WORD  m_wChannels;
			WORD  m_wFormatTag;
			DWORD m_dwAvgBytesPerSec;
			DWORD m_fAudible;
		};
	};

	/* Previous image ID per reflection slot; 0 marks an empty slot. */
	/* 0x320 */ DWORD m_adwReflectionSlotIds[16];

	/* 0x360 */ A3DVAL m_fAudibilityWindowMin;   /* 0 at construction */
	/* 0x364 */ A3DVAL m_fAudibilityWindowMax;   /* 1.0f */
	/* 0x368 */ A3DVAL m_avVolumetricOrigin[3];

	/* 0x374 */ A3DVAL m_avVolumetricBounds[3];
	/* 0x380 */ DWORD  m_dwVolumetricEnable;       /* 0 at construction */
	/* 0x384 */ DWORD  m_fInsideVolumetricBounds;  /* Nonzero inside the volumetric bounds. */
	/* 0x388 */ DWORD  m_adwVolumetricHit[2];

	/* Volumetric damping parameters; dwSize must be 24. */
	/* 0x390 */ A3DVOLSRCDAMPINFO m_VolDampInfo;

	/* IA3dSource2 methods. */
	STDMETHODIMP         AllocateAudioData(INT nBytes);
	STDMETHODIMP         FreeAudioData();
	STDMETHODIMP         SetAudioFormat(LPVOID pvFormat);
	STDMETHODIMP         GetAudioFormat(LPVOID pvFormat);
	STDMETHODIMP_(DWORD) GetAudioSize();
	STDMETHODIMP         SetPlayTime(A3DVAL fTime);
	STDMETHODIMP         GetPlayTime(LPA3DVAL pfTime);
	STDMETHODIMP         SetPlayPosition(DWORD dwPosition);
	STDMETHODIMP         GetPlayPosition(LPDWORD pdwPosition);
	STDMETHODIMP         SetPanValues(DWORD dwNumValues, LPA3DVAL pValues);
	STDMETHODIMP         GetPanValues(DWORD dwNumValues, LPA3DVAL pValues);
	STDMETHODIMP         SetPlayEvent(DWORD dwPosition, HANDLE hEvent);
	STDMETHODIMP         ClearPlayEvents();
	STDMETHODIMP         SetTransformMode(DWORD dwMode);
	STDMETHODIMP         GetTransformMode(LPDWORD pdwMode);
	STDMETHODIMP         SetReflectionDelayScale(A3DVAL fScale);
	STDMETHODIMP         GetReflectionDelayScale(LPA3DVAL pfScale);
	STDMETHODIMP         SetReflectionGainScale(A3DVAL fScale);
	STDMETHODIMP         GetReflectionGainScale(LPA3DVAL pfScale);
	STDMETHODIMP         SetVolumetricBounds(A3DVAL x, A3DVAL y, A3DVAL z);
	STDMETHODIMP         GetVolumetricBounds(LPA3DVAL px, LPA3DVAL py,
	                                         LPA3DVAL pz);
	STDMETHODIMP         SetVolumetricDamping(A3DVOLSRCDAMPINFO *pDampInfo);
	STDMETHODIMP         GetVolumetricDamping(A3DVOLSRCDAMPINFO *pDampInfo);
	STDMETHODIMP         SetReverbMix(A3DVAL fMix, A3DVAL fDirectHF);
	STDMETHODIMP         GetReverbMix(A3DVAL *pfMix, A3DVAL *pfDirectHF);
	STDMETHODIMP         NewManualReflection(LPA3DREFLECTION *ppReflection);
	STDMETHODIMP         FreeManualReflections();
	STDMETHODIMP         GetNumManualReflections(int *pnReflections);

	void         RemoveManualReflection(CA3dReflection *pReflection);
	STDMETHODIMP LoadFile(char *szFile, DWORD dwFormat);
	STDMETHODIMP GetCaps(LPA3DCAPS_SOURCE lpSourceCaps);  /* slot 77 */

	A3DVAL ReflectionGains(A3DVAL fHit, A3DVAL fDistance, const A3DVAL *pavMaterial,
	                       A3DVAL *pfMean, A3DVAL *pfEq, A3DVAL *pfGain);

	HRESULT AllocBuffers(void);

	HRESULT UpdatePlayEvent(DWORD dwPosition, HANDLE hEvent,
	                        CA3dPtrList<A3DPLAYEVENT> *pList);
	HRESULT RebuildNotifyPositions(BOOL bExcludeLoopMark);
	BOOL    MapWaveEvents(int nMark, DWORD dwSegLen, BOOL bWrap);
	BOOL    MapMp3Events(int nMark, DWORD dwSegLen, BOOL bWrap);
	BOOL    MapAc3Events(int nMark, DWORD dwSegLen, BOOL bWrap);

	HRESULT LoadStreamingWaveFile(LPSTR pszFileName);
	HRESULT LoadMp3File(LPSTR pszFileName);
	HRESULT LoadStreamingMp3File(LPSTR pszFileName);
	HRESULT LoadAc3File(LPSTR pszFileName);

	HRESULT OpenMp3Stream(LPSTR pszFileName);
	HRESULT FillMp3Decoder(void);
	HRESULT CloseMp3Stream(void);
	HRESULT OpenAc3File(LPSTR pszFileName);
	void    CloseAc3File(void);

	int     FillAc3Decoder(HRESULT *phrStatus);
	HRESULT Ac3GetSyncFrame(DWORD *pcbRead, BOOL *pfBigEndian, DWORD *pdwBitrate,
	                        DWORD *pdwFrameWords);
	HRESULT Ac3CrcCheck(BOOL fBigEndian, int cbData);

	HRESULT OpenMmioStream(LPSTR pszFileName, HMMIO *phmmio, LPMMIOINFO pmmioinfo,
	                       DWORD fdwOpen, LPMMCKINFO pmmcki);

	HRESULT ReadWaveData(HMMIO *phmmio, LPMMCKINFO pmmckiParent);

	HRESULT SizeStreamBuffer(LPMMCKINFO pmmckiParent);
	HRESULT AllocStreamBuffer(DWORD dwLatencyBytes, DWORD dwMaxSegment);
	HRESULT StartStreaming(void);
	HRESULT CreateStreamThread(void);
	HRESULT InitStreamEvents(void);
	HRESULT SetStreamingPriority(void);

	static DWORD WINAPI StreamingCallback(LPVOID pParam);

	HRESULT BuildStreamHandleArray(void);
	HRESULT AddStreamSource(void);
	HRESULT RemoveStreamSource(CA3dSource **ppCurrent);
	HRESULT SeekStreamBuffer(DWORD dwPosition);
	void    ClearStreamPlayEvents(void);
	void    StopStreaming(void);
	HRESULT SetStreamEvent(DWORD dwPosition, HANDLE hEvent);
	HRESULT UpdateStreamBuffer(int nSegment);
	HRESULT FillWaveStreamBuffer(void *pvBuffer, DWORD dwOffset, DWORD dwLength,
	                             int nSegment);
	HRESULT FillMp3StreamBuffer(void *pvBuffer, DWORD dwOffset, DWORD dwLength,
	                            int nSegment);
	HRESULT FillAc3StreamBuffer(void *pvBuffer, DWORD dwOffset, DWORD dwLength,
	                            int nSegment);
	void    FillStreamSilence(void *pvBuffer, DWORD dwSize);
	HRESULT FillStreamTail(DWORD dwStart, DWORD dwEnd);
};

void A3dDistanceGain(class CA3dSource *pSource, A3DVAL fDistance, A3DVAL *pfGain);

void A3dSourceGetMatrix(class CA3dSource *pSource, A3DVAL *pm);

void A3dSourceResetReflections(class CA3dSource *pSource);
void A3dSetDelay(class CA3dSource *pSource, const A3DVAL *pvPolar);

void A3dTraceSetEars(class CA3dSource *pSource, A3DVAL *pvOut);
void A3dTraceSetGains(int nEars, int nTrace, const A3DVAL *pv, A3DVAL f);
void A3dTraceSetVector(class CA3dSource *pSource, const A3DVAL *pvAxis,
                       const A3DVAL *pvToListener, A3DVAL fDistance);

A3DVAL A3dConeAngle(class CA3dSource *pSource, const A3DVAL *pvAxis,
                    const A3DVAL *pvToListener, A3DVAL fDistance);
A3DVAL A3dConeAngleXform(class CA3dSource *pSource);

A3DVAL A3dFastSqrt(A3DVAL fValue);
void   A3dInitSqrtTable(void);
void   A3dTransformPoint(const A3DVAL *pv, const A3DVAL *pm, A3DVAL *pvOut);

void A3dTraceTransformDir(const A3DVAL *pvDir, const A3DVAL *pm, A3DVAL *pvOut);

void A3dTraceTransformDirInverse(const A3DVAL *pvDir, const A3DVAL *pm, A3DVAL *pvOut);

void A3dTraceSetSpread(class CA3dSource *pSource, A3DVAL fLeft, A3DVAL fRight);
void A3dSourceRecalc(class CA3dSource *pSource);

void A3dTraceApply(class CA3dSource *pSource, A3DVAL fOcclusion, const A3DVAL *pav);

void A3dReflection(class CA3dSource *pSource, DWORD dwSurface, DWORD dwSurface2,
                   DWORD dwKind, const A3DVAL *pvHit, A3DVAL fHit,
                   const A3DVAL *pavMaterial, const A3DVAL *pavTruePoint,
                   const A3DVAL *pavSoftPoint);

LPA3DREFLECTIONREC A3dReflectionNew(class CA3dSource *pSource);

A3DVAL A3dVolumetricCoverage(class CA3dSource *pSource,
	int (*pfnHit)(const A3DVAL *pvFrom, const A3DVAL *pvTo,
	             A3DPRIMITIVE *pPrim, int nLoose, int nInfinite),
	const A3DVAL *pvListener, A3DPRIMITIVE *pPrim);
A3DVAL A3dVolumetricSizeDamp(class CA3dSource *pSource, A3DPRIMITIVE *pPrim);

void A3dSourcePushA(class CA3dSource *pSource, int cActive);
int  A3dSourcePushBudget(class CA3dSource *pSource, int cActive);
int  A3dSourcePushMatch(class CA3dSource *pSource, int cWanted, DWORD *pdwUsed);
void A3dSourcePushAssign(class CA3dSource *pSource, int cUnplaced, DWORD dwUsed);
void A3dSourcePushB(class CA3dSource *pSource, int cActive);

LPA3DREFLECTIONREC A3dChainNext(class CA3dSource *pSource, LPA3DREFLECTIONREC pLink);

int     A3dReflectionCompare(const void *pv1, const void *pv2);
int     A3dReflectionCompareDelay(const void *pv1, const void *pv2);
void    A3dSourceCarry(class CA3dSource *pSource);
HRESULT A3dSourceEmit(class CA3dSource *pSource);
int     A3dSourceTraceSkip(class CA3dSource *pSource);

void A3dSourceStep(class CA3dSource *pSource, const A3DVAL *pvLeftEar,
                   const A3DVAL *pvRightEar);

void A3dStepAngles(class CA3dSource *pSource, const A3DVAL *pvLeftEar,
                   const A3DVAL *pvRightEar);
void A3dStepDelayPair(class CA3dSource *pSource);
void A3dStepEarGains(class CA3dSource *pSource);
void A3dStepEq(class CA3dSource *pSource);
void A3dStepMidGain(class CA3dSource *pSource);
void A3dStepPitchOut(class CA3dSource *pSource);
void A3dSourceStepEnd(class CA3dSource *pSource, int fForce);
void PushRenderMode(class CA3dSource *pSource);
void A3dSourceSeedDirect(class CA3dSource *pSource);
void A3dSourcePushLocal(class CA3dSource *pSource);
void A3dSourceQuiet(class CA3dSource *pSource);

#endif /* _A3DSOURCE_H */
