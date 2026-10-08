/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * softmix.h
 *
 * Defines software mixer formats, resampler state, filter history and
 * sample-processing entry points. These declarations are shared by
 * A2DBuffer, DAL_A2D and the optional software effects.
 *
 * The state records fractional playback positions, rate and gain ramps
 * and per-ear processing data. softmix.cpp implements sample reading,
 * interpolation, accumulation and PCM conversion; device and voice
 * classes own the storage and decide when those routines run.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _SOFTMIX_H
#define _SOFTMIX_H

#include "A3dPrivate.h"

/* Mixer format bits differ from the outqueue.h encoding. */
#define A3DMIX_FORMAT_STEREO		0x00000002
#define A3DMIX_FORMAT_SIXTEEN		0x00000010
#define A3DMIX_FORMAT_LOOPING		0x00000040

/* Declared filter capacities  */
#define A3D_MIX_FILTER_TAP_CAPACITY     50
#define A3D_MIX_FILTER_HISTORY_CAPACITY 52

/* Target-step cap  */
#define A3D_MIX_MAX_STEP_MULTIPLIER 10

/* =============================================================
// Class: A3DMIXPLAY
//
// Description: Source sample bounds, format and playback cursor.
//
// Size: 0x14
// =============================================================*/

typedef struct A3DMIXPLAY
{
	/* 0x00 */ BYTE		*pStart;	/* Source sample data. */
	/* 0x04 */ BYTE		*pEnd;	/* End of the source data. */
	/* 0x08 */ DWORD		cSamples;	/* Source frames. */
	/* 0x0C */ BYTE		*pCur;	/* Current byte position. */
	/* 0x10 */ DWORD		dwFormatBits;	/* A3DMIX_FORMAT_* flags. */
} A3DMIXPLAY;

/* =============================================================
// Class: A3DMIXEAR
//
// Description: Resampling position, gain and convolution state for one ear.
//
// Size: 0x34C
// =============================================================*/

typedef struct A3DMIXEAR
{
	/* 0x000 */ int		nGainPrev;	/* Previous block gain, 16.16. */
	/* 0x004 */ int		nGain;	/* Current block gain, 16.16. */
	/* 0x008 */ int		nResidue;	/* Step correction carried between blocks. */
	/* 0x00C */ int		nLookahead;	/* Lookahead, 16.16. */
	/* 0x010 */ DWORD		dwFrac;	/* Fractional read position in the low word. */
	/* 0x014 */ int		nPos;	/* Read position in frames. */
	/* 0x018 */ DWORD		cTaps;	/* Active filter length, at most 50. */
	/* 0x01C */ float		afTap[A3D_MIX_FILTER_TAP_CAPACITY];	/* Current convolution coefficients. */
	/* 0x0E4 */ float		afTapWanted[A3D_MIX_FILTER_TAP_CAPACITY];	/* Target coefficients from Blend. */
	/* 0x1AC */ float		afTapStep[A3D_MIX_FILTER_TAP_CAPACITY];	/* Coefficient increments for this block. */
	/* 0x274 */ float		afHistory[A3D_MIX_FILTER_HISTORY_CAPACITY];	/* Previous block tail for convolution overlap. */
	/* 0x344 */ int		nGainWanted;	/* Requested gain, 17.15. */
	/* 0x348 */ int		nGainScale;	/* Additional gain for the plain path, 17.15. */
} A3DMIXEAR;

/* Register ABI: ESI = source data, ECX = frame index, EAX = frames left for FourWrap/FourTail.
 * Push two or four samples in order onto the existing x87 stack.
 * S16 fetches require 0.5 at entry ST(3) for two-frame fetches or ST(0) for four-frame fetches.
 */
typedef void (*A3DMIXFETCH)(void);

struct A3DMIXSTATE;

/* Extra register ABI: CL = frame shift (A3dMixStep4Tap reloads it).
 * Four-tap steps return the unproduced count in EDX and output end in EDI,
 * clobbering EBX, ESI and EDI. Linear steps preserve ECX, ESI and EDI, not EBX.
 */
typedef void (__stdcall *A3DMIXSTEP)(float *pMix, struct A3DMIXEAR *pEar,
				     struct A3DMIXSTATE *pState, int cSamples);

/* =============================================================
// Class: A3DMIXSTATE
//
// Description: Source format, rate and state for both resampler ears.
//
// Size: 0x6DC
// =============================================================*/

typedef struct A3DMIXSTATE
{
	/* 0x000 */ BYTE		*pSamples;	/* Source sample data. */
	/* 0x004 */ DWORD		cbSamples;	/* Source length in bytes. */
	/* 0x008 */ DWORD		nStep;	/* Current rate step, 16.16. */
	/* 0x00C */ DWORD		nStepWanted;	/* Requested rate step, 16.16. */
	/* 0x010 */ DWORD		nStepTarget;	/* Capped target step, 16.16. */
	/* 0x014 */ DWORD		dwLooping;	/* A3DMIX_FORMAT_LOOPING bit. */
	/* 0x018 */ DWORD		dwPos;	/* Read position in bytes. */
	/* 0x01C */ DWORD		nSampleRate;	/* Output rate in Hz after A3dVoiceReset. */
	/* 0x020 */ DWORD		dwUnknown_0x20;
	/* 0x024 */ DWORD		cShift;	/* log2 of the source frame size. */
	/* 0x028 */ A3DMIXFETCH	pfnFetch2;
	/* 0x02C */ A3DMIXFETCH	pfnFetch2Wrap;
	/* 0x030 */ A3DMIXSTEP	pfnStepLinear;
	/* 0x034 */ A3DMIXFETCH	pfnFetch4;
	/* 0x038 */ A3DMIXFETCH	pfnFetch4Wrap;
	/* 0x03C */ A3DMIXFETCH	pfnFetch4Tail;
	/* 0x040 */ A3DMIXSTEP	pfnStep4Tap;

	/* MSVC inline assembly indexes arrays in bytes, so the ears are separate. */
	/* 0x044 */ A3DMIXEAR	earLeft;
	/* 0x390 */ A3DMIXEAR	earRight;
} A3DMIXSTATE;

/* =============================================================
// Class: A3DFILTERCHANNEL
//
// Description: Delay and recursive state for one output-filter channel.
//
// Size: 0x9C
// =============================================================*/

typedef struct A3DFILTERCHANNEL
{
	/* 0x00 */ float	fLastCombInput;	/* Comb history */
	/* 0x04 */ float	fLastCombFeedback;
	/* 0x08 */ float	afLine[34];	/* Delay line; cDelay selects 5 or 10 entries. */

	/* 0x90 */ float	fLastIntermediate;	/* Transform history */
	/* 0x94 */ float	fPreviousIntermediate;
	/* 0x98 */ float	fLastOutput;
} A3DFILTERCHANNEL;

/* =============================================================
// Class: A3DFILTERSTATE
//
// Description: Stereo output-filter history and rate.
//
// Size: 0x1C4
// =============================================================*/

typedef struct A3DFILTERSTATE
{
	/* 0x000 */ int		nRateKHz;	/* 22 or 44. */
	/* 0x004 */ DWORD	dwUnknown_0x004;
	/* 0x008 */ int		nDelayPos;	/* Delay-line cursor, wrapped at cDelay. */

	/* 0x00C */ float	afOverlap[32];	/* First four floats hold the previous block tail. */

	/* 0x08C */ A3DFILTERCHANNEL	chLeft;
	/* 0x128 */ A3DFILTERCHANNEL	chRight;
} A3DFILTERSTATE;

/* =============================================================
// Class: A3DFILTERCOEFF
//
// Description: Delay length and coefficients for the stereo output filter.
//
// Size: 0x30
// =============================================================*/

typedef struct A3DFILTERCOEFF
{
	/* 0x00 */ int		cDelay;	/* Delay length and wrap bound, in frames. */
	/* 0x04 */ float	afComb[3];
	/* 0x10 */ float	afTransform[8];
} A3DFILTERCOEFF;

/* =============================================================
// Class: A3DFILTERMODE
//
// Description: Output-filter mode selection.
//
// Size: 0x10
// =============================================================*/

typedef struct A3DFILTERMODE
{
	/* 0x00 */ DWORD	adwUnknown_0x00[2];
	/* 0x08 */ short	nMode;	/* 0 disables; -3 selects the alternate coefficient set. */
	/* 0x0A */ short	nUnknown_0x0A;
	/* 0x0C */ DWORD	dwUnknown_0x0C;
} A3DFILTERMODE;

/* (RE) Clear-loop count: dbg:0x10075709. */
#define A3D_FILTER_STATE_DWORDS	113
/* (RE) Coefficient-table stride: dbg:0x10075749. */
#define A3D_FILTER_COEFF_SIZE	0x30

/* nRateKHz: 11, 22 or 44 selects 11025, 22050 or 44100 Hz. */
void	A3dVoiceReset(A3DMIXSTATE *pState, A3DMIXPLAY *pPlay, int nRateKHz);
void	A3dMixSetFormat(A3DMIXSTATE *pState, DWORD dwFormat, int nRate,
			BYTE *pStart, int cb);

void	A3dMixInterleave(const float *pIn, short *pOut,
			 unsigned int cSamples);

extern float	g_afMixScratch[0x1000 + 0x100];

void	A3dMixFinish(A3DMIXSTATE *pState, DWORD cSamples);
void	A3dMixGenerate(A3DMIXSTATE *pState, A3DMIXEAR *pEar, float *pOut,
		       DWORD cSamples);
void	A3dMixConvolve(A3DMIXEAR *pEar, const float *pIn, float *pOut,
		       DWORD cSamples);

void	InitFilterState(void *pUnused, A3DFILTERSTATE *pState,
			   int nRateKHz);
int	ApplyFilter(float *pBuffer, const A3DFILTERMODE *pMode,
			   A3DFILTERSTATE *pState, DWORD cSamples);

void __stdcall CombFilter(DWORD cSamples, A3DFILTERSTATE *pState,
				  const A3DFILTERCOEFF *pCoeff, float *pBuffer);
void __stdcall RunStereo(DWORD cSamples, A3DFILTERSTATE *pState,
				  const A3DFILTERCOEFF *pCoeff, float *pBuffer);

/* Register ABI: ECX = frames, EDI = cursor, EBX = coefficients; all preserved.
 * Three recursive values occupy ST(0..2) on entry and return. */
void	RunChannel(void);

/* Register ABI: ESI = state, ECX = block length; clobbers EAX and EDX. */
void	A3dMixLookahead(void);

void __stdcall A3dMixStep4Tap(float *pMix, A3DMIXEAR *pEar,
			    A3DMIXSTATE *pState, int cSamples);
void __stdcall A3dMixStepLinear(float *pMix, A3DMIXEAR *pEar,
			    A3DMIXSTATE *pState, int cSamples);
void __stdcall A3dMixStepM16FourTap(float *pMix, A3DMIXEAR *pEar,
			       A3DMIXSTATE *pState, int cSamples);
void __stdcall A3dMixStepM16Linear(float *pMix, A3DMIXEAR *pEar,
			       A3DMIXSTATE *pState, int cSamples);

void A3dMixFetchM8Two(void);	void A3dMixFetchM8TwoWrap(void);
void A3dMixFetchM8Four(void);	void A3dMixFetchM8FourWrap(void);
void A3dMixFetchM8FourTail(void);
void A3dMixFetchS8Two(void);	void A3dMixFetchS8TwoWrap(void);
void A3dMixFetchS8Four(void);	void A3dMixFetchS8FourWrap(void);
void A3dMixFetchS8FourTail(void);
void A3dMixFetchS16Two(void);	void A3dMixFetchS16TwoWrap(void);
void A3dMixFetchS16Four(void);	void A3dMixFetchS16FourWrap(void);
void A3dMixFetchS16FourTail(void);

/* Register ABI: ECX = block length, EDI = state, ESI = ear.
 * Outputs: EAX = initial step, ECX = per-sample ramp; preserves EDX. */
void A3dMixStepCount(void);
void	A3dMixPlain(A3DMIXSTATE *pState, float *pOut, DWORD cSamples);

/* Preserves ECX and EDX as well as EBX; A3dMixStepCount depends on this. */
DWORD __stdcall A3dFixedMul(DWORD dwA, DWORD dwB);

#endif /* _SOFTMIX_H */
