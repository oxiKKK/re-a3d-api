/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * softmix.cpp
 *
 * Implements the sample-processing routines used by software voices.
 * Format-specific readers handle 8-bit and 16-bit mono or stereo PCM,
 * including looping and end padding, while resamplers advance fractional
 * positions and ramp playback rate and gain.
 *
 * The routines generate per-ear sample streams, accumulate filtered
 * output, apply output filters and convert the floating-point mix to
 * signed 16-bit PCM. A2DBuffer supplies voice state and HRTF processing;
 * DAL_A2D schedules mixing and writes the final output buffer.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "softmix.h"
#include "Plex.h"

#include <stdio.h>

#define	frmBlock        -4
#define	frmSampleA      -2
#define	frmSampleB      -4
#define	frmLeft         -8
#define	frmFetchTail    -28h
#define	frmFetchWrap    -24h
#define	frmFetch        -20h
#define	frmStepRamp     -1Ch
#define	frmStepRampM16  -20h
#define	frmFrac         -18h
/* The step fraction and whole-part stack slots intentionally overlap. */
#define	frmStep         -12h
#define	frmStepWhole    -10h
#define	frmLooping      -8
#define	frmLimit        -4
#define	frmRampBlocks   -4
#define	frmDelay        -4

/* =============================================================
// A3dMixSetFormat()
//
// Clear the resampler state and select routines for the sample format.
// =============================================================*/

void
A3dMixSetFormat(A3DMIXSTATE *pState, DWORD dwFormat, int nRate, BYTE *pStart,
		int cb)
{
A3DMIXSTEP pfnFourTap;
A3DMIXSTEP pfnLinear;

	ZeroMemory(pState, sizeof(*pState));

	pState->pSamples    = pStart;
	pState->cbSamples   = (DWORD) cb;
	pState->nSampleRate = nRate;

	pfnFourTap = A3dMixStep4Tap;
	pfnLinear  = A3dMixStepLinear;

	if (dwFormat & A3DMIX_FORMAT_STEREO)
	{
		pState->pfnFetch2     = A3dMixFetchS8Two;
		pState->pfnFetch2Wrap = A3dMixFetchS8TwoWrap;
		pState->pfnFetch4     = A3dMixFetchS8Four;
		pState->pfnFetch4Wrap = A3dMixFetchS8FourWrap;
		pState->pfnFetch4Tail = A3dMixFetchS8FourTail;

		pState->cShift++;

		if (dwFormat & A3DMIX_FORMAT_SIXTEEN)
		{
			pState->pfnFetch2     = A3dMixFetchS16Two;
			pState->pfnFetch2Wrap = A3dMixFetchS16TwoWrap;
			pState->pfnFetch4     = A3dMixFetchS16Four;
			pState->pfnFetch4Wrap = A3dMixFetchS16FourWrap;
			pState->pfnFetch4Tail = A3dMixFetchS16FourTail;

			pState->cShift++;
		}
	}
	else if (dwFormat & A3DMIX_FORMAT_SIXTEEN)
	{
		pfnFourTap = A3dMixStepM16FourTap;
		pfnLinear  = A3dMixStepM16Linear;

		pState->cShift++;
	}
	else
	{
		pState->pfnFetch2     = A3dMixFetchM8Two;
		pState->pfnFetch2Wrap = A3dMixFetchM8TwoWrap;
		pState->pfnFetch4     = A3dMixFetchM8Four;
		pState->pfnFetch4Wrap = A3dMixFetchM8FourWrap;
		pState->pfnFetch4Tail = A3dMixFetchM8FourTail;
	}

	pState->pfnStepLinear = pfnLinear;
	pState->pfnStep4Tap   = pfnFourTap;
}

/* =============================================================
// A3dMixPlain()
//
// Accumulate both ears with linear interpolation and advance the read cursor.
// =============================================================*/

void __declspec(naked)
A3dMixPlain(A3DMIXSTATE *pState, float *pOut, DWORD cSamples)
{
	__asm
	{
		push	ebp
		mov	ebp, esp
		add	esp, 0FFFFFFFCh
		push	esi
		push	edi
		push	ebx
		push	ecx
		push	edx

		mov	esi, [ebp+8]
		mov	edi, [ebp+0Ch]
		mov	eax, [ebp+10h]
		mov	ebx, [esi]A3DMIXSTATE.pfnStepLinear
		mov	[ebp-4], ebx
		mov	ecx, [esi]A3DMIXSTATE.cShift
		xor	eax, eax
		mov	edx, [esi]A3DMIXSTATE.cbSamples
		or	edx, edx
		jz	short leave_
		mov	eax, [esi]A3DMIXSTATE.dwPos
		cmp	eax, edx
		jge	short leave_

		mov	eax, [ebp+10h]
		push	eax
		push	esi
		lea	eax, [esi]A3DMIXSTATE.earLeft
		push	eax
		push	edi
		call	dword ptr [ebp-4]

		mov	eax, [ebp+10h]
		push	eax
		push	esi
		lea	eax, [esi]A3DMIXSTATE.earRight
		push	eax
		add	edi, 4
		push	edi
		call	dword ptr [ebp-4]

		mov	ebx, [esi]A3DMIXSTATE.earLeft.nResidue
		mov	eax, [esi]A3DMIXSTATE.nStepTarget
		sar	ebx, 8
		sar	eax, 8
		imul	ebx
		shr	eax, 10h
		add	eax, [esi]A3DMIXSTATE.earLeft.nPos
		shl	eax, cl
		cmp	eax, [esi]A3DMIXSTATE.cbSamples
		jl	short store
		cmp	[esi]A3DMIXSTATE.dwLooping, 0
		jz	short stop
		sub	eax, [esi]A3DMIXSTATE.cbSamples
		jmp	short store
stop:
		mov	eax, [esi]A3DMIXSTATE.cbSamples
store:
		mov	[esi]A3DMIXSTATE.dwPos, eax

		mov	ecx, [ebp+10h]
		call	A3dMixLookahead
leave_:
		pop	edx
		pop	ecx
		pop	ebx
		pop	edi
		pop	esi
		leave
		ret
	}
}

/* =============================================================
// A3dFixedMul()
//
// Multiply two unsigned 16.16 values.
//
// Returns: The middle 32 bits of the 64-bit product.
// =============================================================*/

DWORD __declspec(naked) __stdcall
A3dFixedMul(DWORD dwA, DWORD dwB)
{
	__asm
	{
		push	ebp
		mov	ebp, esp
		add	esp, 0FFFFFFFCh
		push	ecx
		push	edx
		push	ebx

		mov	eax, [ebp+0Ch]
		mov	ebx, [ebp+8]
		shr	eax, 10h
		and	ebx, 0FFFFh
		mul	ebx
		mov	ecx, eax
		mov	eax, [ebp+0Ch]
		and	eax, 0FFFFh
		mul	ebx
		shr	eax, 10h
		add	ecx, eax
		mov	ebx, [ebp+8]
		shr	ebx, 10h
		mov	eax, [ebp+0Ch]
		and	eax, 0FFFFh
		mul	ebx
		add	ecx, eax
		mov	eax, [ebp+0Ch]
		shr	eax, 10h
		mul	ebx
		shl	eax, 10h
		add	eax, ecx

		pop	ebx
		pop	edx
		pop	ecx
		leave
		ret	8
	}
}

/* =============================================================
// A3dMixStepCount()
//
// Calculate the initial step and per-sample ramp, retaining the ear residue.
// The original breaks with INT 3 on a negative initial step, including Retail.
// =============================================================*/

void __declspec(naked)
A3dMixStepCount(void)
{
	__asm
	{
		push	ebp
		mov	ebp, esp
		add	esp, 0FFFFFFFCh
		push	edx

		mov	[ebp+frmBlock], ecx
		mov	eax, [edi]A3DMIXSTATE.nStepTarget
		sub	eax, [edi]A3DMIXSTATE.nStep
		cdq
		idiv	dword ptr [ebp+frmBlock]
		mov	ecx, eax

		push	dword ptr [esi]A3DMIXEAR.nLookahead
		push	dword ptr [edi]A3DMIXSTATE.nStepTarget
		call	A3dFixedMul
		mov	ebx, eax

		mov	eax, [esi]A3DMIXEAR.nResidue
		sub	eax, ebx
		cdq
		idiv	dword ptr [ebp+frmBlock]
		mov	ebx, eax
		imul	dword ptr [ebp+frmBlock]
		sub	[esi]A3DMIXEAR.nResidue, eax

		mov	eax, [edi]A3DMIXSTATE.nStep
		add	eax, ebx
		cmp	eax, 0
		jge	short ok
		int	3
ok:
		pop	edx
		leave
		ret
	}
}

/* =============================================================
// A3dMixLookahead()
//
// Advance the current step and cap the target at ten times that step.
// =============================================================*/

void __declspec(naked)
A3dMixLookahead(void)
{
	__asm
	{
		mov	eax, [esi]A3DMIXSTATE.nStepTarget
		sub	eax, [esi]A3DMIXSTATE.nStep
		cdq
		idiv	ecx
		imul	ecx
		add	[esi]A3DMIXSTATE.nStep, eax
		mov	eax, [esi]A3DMIXSTATE.nStep
		mov	edx, A3D_MIX_MAX_STEP_MULTIPLIER
		imul	edx
		mov	edx, [esi]A3DMIXSTATE.nStepWanted
		cmp	edx, eax
		jle	short store
		mov	edx, eax
store:
		mov	[esi]A3DMIXSTATE.nStepTarget, edx
		ret
	}
}

/* (RE) dbg:0x1014CC90 */
static double	g_dFixedToFloat = 1.0 / 65536.0;
static double	g_dHalf		= 0.5;

/* Shared scratch storage requires a single mixing thread. */
float	g_afMixScratch[0x1000 + 0x100];

/* Four-tap interpolation kernel, indexed by the top seven fraction bits. */
static float	g_afInterp[128][4] =
{
	{  0.000000f,  0.999023f,  0.000000f,  0.000000f },
	{ -0.002930f,  0.995605f,  0.007813f, -0.001465f },
	{ -0.005829f,  0.992096f,  0.015717f, -0.002960f },
	{ -0.008637f,  0.988403f,  0.023713f, -0.004456f },
	{ -0.011383f,  0.984619f,  0.031739f, -0.005951f },
	{ -0.014069f,  0.980682f,  0.039857f, -0.007447f },
	{ -0.016694f,  0.976653f,  0.048006f, -0.008942f },
	{ -0.019257f,  0.972472f,  0.056246f, -0.010437f },
	{ -0.021729f,  0.968169f,  0.064516f, -0.011902f },
	{ -0.024171f,  0.963713f,  0.072878f, -0.013398f },
	{ -0.026521f,  0.959166f,  0.081271f, -0.014893f },
	{ -0.028840f,  0.954497f,  0.089724f, -0.016358f },
	{ -0.031068f,  0.949705f,  0.098239f, -0.017823f },
	{ -0.033265f,  0.944792f,  0.106784f, -0.019288f },
	{ -0.035371f,  0.939756f,  0.115390f, -0.020753f },
	{ -0.037446f,  0.934629f,  0.124058f, -0.022217f },
	{ -0.039430f,  0.929350f,  0.132756f, -0.023652f },
	{ -0.041353f,  0.923978f,  0.141484f, -0.025086f },
	{ -0.043245f,  0.918516f,  0.150273f, -0.026521f },
	{ -0.045045f,  0.912900f,  0.159124f, -0.027924f },
	{ -0.046815f,  0.907193f,  0.167974f, -0.029359f },
	{ -0.048524f,  0.901395f,  0.176885f, -0.030763f },
	{ -0.050142f,  0.895474f,  0.185858f, -0.032136f },
	{ -0.051729f,  0.889462f,  0.194830f, -0.033509f },
	{ -0.053255f,  0.883358f,  0.203833f, -0.034883f },
	{ -0.054720f,  0.877132f,  0.212897f, -0.036256f },
	{ -0.056154f,  0.870815f,  0.221961f, -0.037599f },
	{ -0.057497f,  0.864376f,  0.231056f, -0.038911f },
	{ -0.058809f,  0.857875f,  0.240181f, -0.040223f },
	{ -0.060060f,  0.851253f,  0.249336f, -0.041536f },
	{ -0.061251f,  0.844539f,  0.258522f, -0.042817f },
	{ -0.062380f,  0.837764f,  0.267739f, -0.044069f },
	{ -0.063448f,  0.830866f,  0.276955f, -0.045320f },
	{ -0.064486f,  0.823878f,  0.286172f, -0.046541f },
	{ -0.065462f,  0.816828f,  0.295450f, -0.047761f },
	{ -0.066408f,  0.809687f,  0.304697f, -0.048952f },
	{ -0.067263f,  0.802454f,  0.314005f, -0.050142f },
	{ -0.068087f,  0.795129f,  0.323283f, -0.051302f },
	{ -0.068850f,  0.787744f,  0.332591f, -0.052431f },
	{ -0.069582f,  0.780267f,  0.341899f, -0.053560f },
	{ -0.070254f,  0.772698f,  0.351238f, -0.054659f },
	{ -0.070894f,  0.765069f,  0.360546f, -0.055727f },
	{ -0.071444f,  0.757378f,  0.369884f, -0.056764f },
	{ -0.071993f,  0.749596f,  0.379192f, -0.057802f },
	{ -0.072451f,  0.741752f,  0.388531f, -0.058779f },
	{ -0.072909f,  0.733848f,  0.397870f, -0.059755f },
	{ -0.073275f,  0.725852f,  0.407178f, -0.060732f },
	{ -0.073611f,  0.717795f,  0.416486f, -0.061647f },
	{ -0.073916f,  0.709677f,  0.425794f, -0.062532f },
	{ -0.074160f,  0.701498f,  0.435102f, -0.063417f },
	{ -0.074374f,  0.693258f,  0.444411f, -0.064272f },
	{ -0.074526f,  0.684957f,  0.453658f, -0.065065f },
	{ -0.074648f,  0.676595f,  0.462935f, -0.065859f },
	{ -0.074709f,  0.668203f,  0.472182f, -0.066622f },
	{ -0.074740f,  0.659719f,  0.481399f, -0.067354f },
	{ -0.074740f,  0.651204f,  0.490616f, -0.068056f },
	{ -0.074679f,  0.642628f,  0.499802f, -0.068728f },
	{ -0.074587f,  0.633992f,  0.508988f, -0.069338f },
	{ -0.074465f,  0.625324f,  0.518113f, -0.069948f },
	{ -0.074282f,  0.616596f,  0.527238f, -0.070528f },
	{ -0.074099f,  0.607837f,  0.536332f, -0.071047f },
	{ -0.073824f,  0.599048f,  0.545396f, -0.071566f },
	{ -0.073550f,  0.590197f,  0.554430f, -0.072024f },
	{ -0.073214f,  0.581317f,  0.563402f, -0.072451f },
	{ -0.072878f,  0.572375f,  0.572375f, -0.072878f },
	{ -0.072451f,  0.563402f,  0.581317f, -0.073214f },
	{ -0.072024f,  0.554430f,  0.590197f, -0.073550f },
	{ -0.071566f,  0.545396f,  0.599048f, -0.073824f },
	{ -0.071047f,  0.536332f,  0.607837f, -0.074099f },
	{ -0.070528f,  0.527238f,  0.616596f, -0.074282f },
	{ -0.069948f,  0.518113f,  0.625324f, -0.074465f },
	{ -0.069338f,  0.508988f,  0.633992f, -0.074587f },
	{ -0.068728f,  0.499802f,  0.642628f, -0.074679f },
	{ -0.068056f,  0.490616f,  0.651204f, -0.074740f },
	{ -0.067354f,  0.481399f,  0.659719f, -0.074740f },
	{ -0.066622f,  0.472182f,  0.668203f, -0.074709f },
	{ -0.065859f,  0.462935f,  0.676595f, -0.074648f },
	{ -0.065065f,  0.453658f,  0.684957f, -0.074526f },
	{ -0.064272f,  0.444411f,  0.693258f, -0.074374f },
	{ -0.063417f,  0.435102f,  0.701498f, -0.074160f },
	{ -0.062532f,  0.425794f,  0.709677f, -0.073916f },
	{ -0.061647f,  0.416486f,  0.717795f, -0.073611f },
	{ -0.060732f,  0.407178f,  0.725852f, -0.073275f },
	{ -0.059755f,  0.397870f,  0.733848f, -0.072909f },
	{ -0.058779f,  0.388531f,  0.741752f, -0.072451f },
	{ -0.057802f,  0.379192f,  0.749596f, -0.071993f },
	{ -0.056764f,  0.369884f,  0.757378f, -0.071444f },
	{ -0.055727f,  0.360546f,  0.765069f, -0.070894f },
	{ -0.054659f,  0.351238f,  0.772698f, -0.070254f },
	{ -0.053560f,  0.341899f,  0.780267f, -0.069582f },
	{ -0.052431f,  0.332591f,  0.787744f, -0.068850f },
	{ -0.051302f,  0.323283f,  0.795129f, -0.068087f },
	{ -0.050142f,  0.314005f,  0.802454f, -0.067263f },
	{ -0.048952f,  0.304697f,  0.809687f, -0.066408f },
	{ -0.047761f,  0.295450f,  0.816828f, -0.065462f },
	{ -0.046541f,  0.286172f,  0.823878f, -0.064486f },
	{ -0.045320f,  0.276955f,  0.830866f, -0.063448f },
	{ -0.044069f,  0.267739f,  0.837764f, -0.062380f },
	{ -0.042817f,  0.258522f,  0.844539f, -0.061251f },
	{ -0.041536f,  0.249336f,  0.851253f, -0.060060f },
	{ -0.040223f,  0.240181f,  0.857875f, -0.058809f },
	{ -0.038911f,  0.231056f,  0.864376f, -0.057497f },
	{ -0.037599f,  0.221961f,  0.870815f, -0.056154f },
	{ -0.036256f,  0.212897f,  0.877132f, -0.054720f },
	{ -0.034883f,  0.203833f,  0.883358f, -0.053255f },
	{ -0.033509f,  0.194830f,  0.889462f, -0.051729f },
	{ -0.032136f,  0.185858f,  0.895474f, -0.050142f },
	{ -0.030763f,  0.176885f,  0.901395f, -0.048524f },
	{ -0.029359f,  0.167974f,  0.907193f, -0.046815f },
	{ -0.027924f,  0.159124f,  0.912900f, -0.045045f },
	{ -0.026521f,  0.150273f,  0.918516f, -0.043245f },
	{ -0.025086f,  0.141484f,  0.923978f, -0.041353f },
	{ -0.023652f,  0.132756f,  0.929350f, -0.039430f },
	{ -0.022217f,  0.124058f,  0.934629f, -0.037446f },
	{ -0.020753f,  0.115390f,  0.939756f, -0.035371f },
	{ -0.019288f,  0.106784f,  0.944792f, -0.033265f },
	{ -0.017823f,  0.098239f,  0.949705f, -0.031068f },
	{ -0.016358f,  0.089724f,  0.954497f, -0.028840f },
	{ -0.014893f,  0.081271f,  0.959166f, -0.026521f },
	{ -0.013398f,  0.072878f,  0.963713f, -0.024171f },
	{ -0.011902f,  0.064516f,  0.968169f, -0.021729f },
	{ -0.010437f,  0.056246f,  0.972472f, -0.019257f },
	{ -0.008942f,  0.048006f,  0.976653f, -0.016694f },
	{ -0.007447f,  0.039857f,  0.980682f, -0.014069f },
	{ -0.005951f,  0.031739f,  0.984619f, -0.011383f },
	{ -0.004456f,  0.023713f,  0.988403f, -0.008637f },
	{ -0.002960f,  0.015717f,  0.992096f, -0.005829f },
	{ -0.001465f,  0.007813f,  0.995605f, -0.002930f },
};

/* =============================================================
// A3dMixFetchS16Two()
//
//
// Push two consecutive 16-bit stereo samples onto the x87 stack.
// =============================================================*/

void __declspec(naked)
A3dMixFetchS16Two(void)
{
	__asm
	{
		fild	word ptr [esi+ecx*4]
		fild	word ptr [esi+ecx*4+2]
		faddp	st(1), st(0)
		fmul	st(0), st(4)
		fild	word ptr [esi+ecx*4+4]
		fild	word ptr [esi+ecx*4+6]
		faddp	st(1), st(0)
		fmul	st(0), st(5)
		ret
	}
}

/* =============================================================
// A3dMixFetchS16TwoWrap()
//
//
// Push two 16-bit stereo samples, wrapping the second to the buffer start.
// =============================================================*/

void __declspec(naked)
A3dMixFetchS16TwoWrap(void)
{
	__asm
	{
		fild	word ptr [esi+ecx*4]
		fild	word ptr [esi+ecx*4+2]
		faddp	st(1), st(0)
		fmul	st(0), st(4)
		fild	word ptr [esi]
		fild	word ptr [esi+2]
		faddp	st(1), st(0)
		fmul	st(0), st(5)
		ret
	}
}

/* =============================================================
// A3dMixFetchS16Four()
//
//
// Push four consecutive 16-bit stereo samples onto the x87 stack.
// =============================================================*/

void __declspec(naked)
A3dMixFetchS16Four(void)
{
	__asm
	{
		fild	word ptr [esi+ecx*4]
		fild	word ptr [esi+ecx*4+2]
		faddp	st(1), st(0)
		fmul	st(0), st(1)
		fild	word ptr [esi+ecx*4+4]
		fild	word ptr [esi+ecx*4+6]
		faddp	st(1), st(0)
		fmul	st(0), st(2)
		fild	word ptr [esi+ecx*4+8]
		fild	word ptr [esi+ecx*4+0Ah]
		faddp	st(1), st(0)
		fmul	st(0), st(3)
		fild	word ptr [esi+ecx*4+0Ch]
		fild	word ptr [esi+ecx*4+0Eh]
		faddp	st(1), st(0)
		fmul	st(0), st(4)
		ret
	}
}

/* =============================================================
// A3dMixFetchS16FourWrap()
//
//
// Push four 16-bit stereo samples, wrapping at the buffer end.
// =============================================================*/

void __declspec(naked)
A3dMixFetchS16FourWrap(void)
{
	__asm
	{
		fild	word ptr [esi+ecx*4]
		fild	word ptr [esi+ecx*4+2]
		faddp	st(1), st(0)
		fmul	st(0), st(1)
		dec	eax
		jnz	short two_left
		fild	word ptr [esi]
		fild	word ptr [esi+2]
		faddp	st(1), st(0)
		fmul	st(0), st(2)
		fild	word ptr [esi+4]
		fild	word ptr [esi+6]
		faddp	st(1), st(0)
		fmul	st(0), st(3)
		fild	word ptr [esi+8]
		fild	word ptr [esi+0Ah]
		faddp	st(1), st(0)
		fmul	st(0), st(4)
		ret
two_left:
		fild	word ptr [esi+ecx*4+4]
		fild	word ptr [esi+ecx*4+6]
		faddp	st(1), st(0)
		fmul	st(0), st(2)
		dec	eax
		jnz	short three_left
		fild	word ptr [esi]
		fild	word ptr [esi+2]
		faddp	st(1), st(0)
		fmul	st(0), st(3)
		fild	word ptr [esi+4]
		fild	word ptr [esi+6]
		faddp	st(1), st(0)
		fmul	st(0), st(4)
		ret
three_left:
		fild	word ptr [esi+ecx*4+8]
		fild	word ptr [esi+ecx*4+0Ah]
		faddp	st(1), st(0)
		fmul	st(0), st(3)
		fild	word ptr [esi]
		fild	word ptr [esi+2]
		faddp	st(1), st(0)
		fmul	st(0), st(4)
		ret
	}
}

/* =============================================================
// A3dMixFetchS16FourTail()
//
//
// Push four 16-bit stereo samples, zero-padding at the buffer end.
// =============================================================*/

void __declspec(naked)
A3dMixFetchS16FourTail(void)
{
	__asm
	{
		fild	word ptr [esi+ecx*4]
		fild	word ptr [esi+ecx*4+2]
		faddp	st(1), st(0)
		fmul	st(0), st(1)
		dec	eax
		jz	short three_zeros
		fild	word ptr [esi+ecx*4+4]
		fild	word ptr [esi+ecx*4+6]
		faddp	st(1), st(0)
		fmul	st(0), st(2)
		dec	eax
		jz	short two_zeros
		fild	word ptr [esi+ecx*4+8]
		fild	word ptr [esi+ecx*4+0Ah]
		faddp	st(1), st(0)
		fmul	st(0), st(3)
		jmp	short one_zero
three_zeros:
		fldz
two_zeros:
		fldz
one_zero:
		fldz
		ret
	}
}

/* =============================================================
// A3dMixFetchM8Two()
//
//
// Push two consecutive 8-bit mono samples onto the x87 stack.
// =============================================================*/

void __declspec(naked)
A3dMixFetchM8Two(void)
{
	__asm
	{
		push	ebp
		mov	ebp, esp
		add	esp, 0FFFFFFFCh
		xor	eax, eax
		mov	ah, byte ptr [ecx+esi]
		xor	eax, 8000h
		mov	word ptr [ebp+frmSampleA], ax
		mov	ah, byte ptr [ecx+esi+1]
		xor	eax, 8000h
		mov	word ptr [ebp+frmSampleB], ax
		fild	word ptr [ebp+frmSampleA]
		fild	word ptr [ebp+frmSampleB]
		leave
		ret
	}
}

/* =============================================================
// A3dMixFetchM8TwoWrap()
//
//
// Push two 8-bit mono samples, wrapping the second to the buffer start.
// Original defect: the first fetch reads one byte past the current position.
// The first XOR intentionally uses AX rather than EAX.
// =============================================================*/

void __declspec(naked)
A3dMixFetchM8TwoWrap(void)
{
	__asm
	{
		push	ebp
		mov	ebp, esp
		add	esp, 0FFFFFFFCh
		xor	eax, eax
		mov	ah, byte ptr [ecx+esi+1]
		xor	ax, 8000h
		mov	word ptr [ebp+frmSampleA], ax
		mov	ah, byte ptr [esi]
		xor	eax, 8000h
		mov	word ptr [ebp+frmSampleB], ax
		fild	word ptr [ebp+frmSampleA]
		fild	word ptr [ebp+frmSampleB]
		leave
		ret
	}
}

/* =============================================================
// A3dMixFetchS8Two()
//
//
// Push two consecutive 8-bit stereo samples onto the x87 stack.
// =============================================================*/

void __declspec(naked)
A3dMixFetchS8Two(void)
{
	__asm
	{
		push	ebp
		mov	ebp, esp
		add	esp, 0FFFFFFFCh
		xor	eax, eax
		mov	ah, byte ptr [esi+ecx*2]
		xor	eax, 8000h
		sar	ax, 1
		mov	word ptr [ebp+frmSampleA], ax
		mov	ah, byte ptr [esi+ecx*2+1]
		xor	eax, 8000h
		sar	ax, 1
		add	word ptr [ebp+frmSampleA], ax
		mov	ah, byte ptr [esi+ecx*2+2]
		xor	eax, 8000h
		sar	ax, 1
		mov	word ptr [ebp+frmSampleB], ax
		mov	ah, byte ptr [esi+ecx*2+3]
		xor	eax, 8000h
		sar	ax, 1
		add	word ptr [ebp+frmSampleB], ax
		fild	word ptr [ebp+frmSampleA]
		fild	word ptr [ebp+frmSampleB]
		leave
		ret
	}
}

/* =============================================================
// A3dMixFetchS8TwoWrap()
//
//
// Push two 8-bit stereo samples, wrapping the second to the buffer start.
// =============================================================*/

void __declspec(naked)
A3dMixFetchS8TwoWrap(void)
{
	__asm
	{
		push	ebp
		mov	ebp, esp
		add	esp, 0FFFFFFFCh
		xor	eax, eax
		mov	ah, byte ptr [esi+ecx*2]
		xor	eax, 8000h
		sar	ax, 1
		mov	word ptr [ebp+frmSampleA], ax
		mov	ah, byte ptr [esi+ecx*2+1]
		xor	eax, 8000h
		sar	ax, 1
		add	word ptr [ebp+frmSampleA], ax
		mov	ah, byte ptr [esi]
		xor	eax, 8000h
		sar	ax, 1
		mov	word ptr [ebp+frmSampleB], ax
		mov	ah, byte ptr [esi+1]
		xor	eax, 8000h
		sar	ax, 1
		add	word ptr [ebp+frmSampleB], ax
		fild	word ptr [ebp+frmSampleA]
		fild	word ptr [ebp+frmSampleB]
		leave
		ret
	}
}

/* =============================================================
// A3dMixStepLinear()
//
//
// Accumulate one channel with linear interpolation and a gain ramp.
// =============================================================*/

void __declspec(naked) __stdcall
A3dMixStepLinear(float *pMix, A3DMIXEAR *pEar, A3DMIXSTATE *pState, int cSamples)
{
	__asm
	{
		push	ebp
		mov	ebp, esp
		add	esp, 0FFFFFFDCh
		push	esi
		push	edi
		push	ecx
		mov	edi, [ebp+10h]
		mov	esi, [ebp+0Ch]
		mov	eax, [edi]A3DMIXSTATE.pfnFetch2
		mov	[ebp+frmFetch], eax
		mov	eax, [edi]A3DMIXSTATE.pfnFetch2Wrap
		mov	[ebp+frmFetchWrap], eax
		mov	word ptr [ebp+frmStep], 0
		mov	dword ptr [ebp+frmStepWhole], 0
		mov	edx, [edi]A3DMIXSTATE.dwLooping
		mov	[ebp+frmLooping], edx
		mov	eax, [edi]A3DMIXSTATE.cbSamples
		shr	eax, cl
		or	edx, edx
		jnz	short have_limit
		push	eax
		mov	ebx, [esi]A3DMIXEAR.nLookahead
		mov	eax, [edi]A3DMIXSTATE.nStepTarget
		sar	ebx, 8
		sar	eax, 8
		imul	ebx
		shr	eax, 10h
		mov	ebx, eax
		pop	eax
		sub	eax, ebx
have_limit:
		dec	eax
		mov	[ebp+frmLimit], eax

		mov	ecx, [ebp+14h]
		call	A3dMixStepCount
		mov	[ebp+frmStep], eax
		mov	ebx, eax
		mov	[ebp+frmStepRamp], ecx

		finit
		fld	qword ptr [g_dHalf]
		fld	qword ptr [g_dFixedToFloat]
		fild	dword ptr [esi]A3DMIXEAR.nGainPrev
		fild	dword ptr [esi]A3DMIXEAR.nGain
		fsub	st(0), st(1)
		fxch	st(1)
		fmul	st(0), st(2)
		fxch	st(1)
		fmul	st(0), st(2)
		fild	dword ptr [ebp+14h]
		fdivp	st(1), st(0)
		fxch	st(2)
		fxch	st(1)
		fadd	st(0), st(2)

		mov	ecx, [esi]A3DMIXEAR.nGain
		mov	[esi]A3DMIXEAR.nGainPrev, ecx
		mov	ecx, [esi]A3DMIXEAR.nPos
		mov	eax, [esi]A3DMIXEAR.dwFrac
		mov	[ebp+frmFrac], eax

		push	esi
		mov	esi, [edi]A3DMIXSTATE.pSamples
		mov	edi, [ebp+8]
		mov	edx, [ebp+14h]
		cmp	ecx, [ebp+frmLimit]
		jz	short fetch_wrap
		jl	short fetch
		jmp	short at_limit
fetch:
		call	dword ptr [ebp+frmFetch]
accumulate:

		fild	dword ptr [ebp+frmFrac]
		add	edi, 8
		add	word ptr [ebp+frmFrac], bx
		fmul	st(0), st(4)
		fxch	st(1)
		fsub	st(0), st(2)
		fxch	st(1)
		fmul	st(0), st(3)
		fxch	st(2)
		fmul	st(0), st(3)
		fxch	st(3)
		fadd	st(0), st(5)
		fxch	st(2)
		fmulp	st(1), st(0)
		adc	ecx, [ebp+frmStepWhole]
		add	ebx, [ebp+frmStepRamp]
		faddp	st(2), st(0)
		fxch	st(1)
		fadd	dword ptr [edi-8]
		mov	[ebp+frmStep], ebx
		dec	edx
		fstp	dword ptr [edi-8]
		jz	short done
		cmp	ecx, [ebp+frmLimit]
		jb	short fetch
		ja	short at_limit
fetch_wrap:
		call	dword ptr [ebp+frmFetchWrap]
		jmp	short accumulate
at_limit:
		cmp	dword ptr [ebp+frmLooping], 0
		jz	short done
		mov	eax, [ebp+frmLimit]
		inc	eax
		sub	ecx, eax
		jmp	short fetch
done:
		pop	esi
		mov	ebx, [ebp+frmLimit]
		inc	ebx
		cmp	ecx, ebx
		jle	short store
		mov	ecx, ebx
store:
		mov	[esi]A3DMIXEAR.nPos, ecx
		mov	eax, [ebp+frmFrac]
		mov	[esi]A3DMIXEAR.dwFrac, eax
		pop	ecx
		pop	edi
		pop	esi
		leave
		ret	16
	}
}

/* =============================================================
// A3dMixStepM16FourTap()
//
//
// Resample 16-bit mono into a contiguous float buffer using four taps.
// =============================================================*/

void __declspec(naked) __stdcall
A3dMixStepM16FourTap(float *pMix, A3DMIXEAR *pEar, A3DMIXSTATE *pState, int cSamples)
{
	__asm
	{
		push	ebp
		mov	ebp, esp
		add	esp, 0FFFFFFE4h
		mov	edi, [ebp+10h]
		mov	esi, [ebp+0Ch]
		mov	word ptr [ebp+frmStep], 0
		mov	dword ptr [ebp+frmStepWhole], 0
		mov	edx, [edi]A3DMIXSTATE.dwLooping
		mov	[ebp+frmLooping], edx
		mov	eax, [edi]A3DMIXSTATE.cbSamples
		shr	eax, cl
		or	edx, edx
		jnz	short have_limit
		push	eax
		mov	ebx, [esi]A3DMIXEAR.nLookahead
		mov	eax, [edi]A3DMIXSTATE.nStepTarget
		sar	ebx, 8
		sar	eax, 8
		imul	ebx
		shr	eax, 10h
		mov	ebx, eax
		pop	eax
		sub	eax, ebx
have_limit:
		sub	eax, 4
		mov	[ebp+frmLimit], eax

		mov	ecx, [ebp+14h]
		call	A3dMixStepCount
		mov	[ebp+frmStep], eax
		mov	ebx, eax
		mov	[ebp+frmStepRamp], ecx
		mov	[ebp+frmStep], ebx

		finit

		mov	ecx, [esi]A3DMIXEAR.nGain
		mov	[esi]A3DMIXEAR.nGainPrev, ecx
		mov	ecx, [esi]A3DMIXEAR.nPos
		mov	eax, [esi]A3DMIXEAR.dwFrac
		mov	[ebp+frmFrac], eax

		push	esi
		mov	esi, [edi]A3DMIXSTATE.pSamples
		mov	edi, [ebp+8]
		mov	edx, [ebp+14h]
test_pos:
		cmp	ecx, [ebp+frmLimit]
		jg	short at_limit
fetch:
		fild	word ptr [esi+ecx*2]
		fild	word ptr [esi+ecx*2+2]
		fild	word ptr [esi+ecx*2+4]
		fild	word ptr [esi+ecx*2+6]
accumulate:

		fxch	st(3)
		mov	eax, [ebp+frmFrac]
		shr	eax, 5
		and	eax, 7F0h
		lea	eax, g_afInterp[eax]
		fmul	dword ptr [eax]
		fxch	st(2)
		add	edi, 4
		add	word ptr [ebp+frmFrac], bx
		fmul	dword ptr [eax+4]
		fxch	st(1)
		adc	ecx, [ebp+frmStepWhole]
		add	ebx, [ebp+frmStepRamp]
		fmul	dword ptr [eax+8]
		fxch	st(1)
		faddp	st(2), st(0)
		fxch	st(2)
		fmul	dword ptr [eax+0Ch]
		fxch	st(2)
		faddp	st(1), st(0)
		faddp	st(1), st(0)
		mov	[ebp+frmStep], ebx
		fstp	dword ptr [edi-4]

		dec	edx
		jz	short done
		cmp	ecx, [ebp+frmLimit]
		jle	short fetch
at_limit:
		mov	eax, [ebp+frmLimit]
		add	eax, 4
		cmp	dword ptr [ebp+frmLooping], 0
		jz	short at_end
		sub	eax, ecx
		jnz	short past_or_across
		xor	ecx, ecx
		jmp	short fetch
past_or_across:
		jnb	short fetch_wrap
		neg	eax
		mov	ecx, eax
		jmp	short test_pos
fetch_wrap:
		fild	word ptr [esi+ecx*2]
		dec	eax
		jnz	short wrap_two
		fild	word ptr [esi]
		fild	word ptr [esi+2]
		fild	word ptr [esi+4]
		jmp	short accumulate
wrap_two:
		fild	word ptr [esi+ecx*2+2]
		dec	eax
		jnz	short wrap_three
		fild	word ptr [esi]
		fild	word ptr [esi+2]
		jmp	accumulate
wrap_three:
		fild	word ptr [esi+ecx*2+4]
		fild	word ptr [esi]
		jmp	accumulate
at_end:
		sub	eax, ecx
		jz	short done
		jnb	short fetch_tail
		mov	ecx, [ebp+frmLimit]
		add	ecx, 4
		jmp	short done
fetch_tail:
		fild	word ptr [esi+ecx*2]
		dec	eax
		jz	short three_zeros
		fild	word ptr [esi+ecx*2+2]
		dec	eax
		jz	short two_zeros
		fild	word ptr [esi+ecx*2+4]
		jmp	short one_zero
three_zeros:
		fldz
two_zeros:
		fldz
one_zero:
		fldz
		jmp	accumulate
done:
		pop	esi
		mov	[esi]A3DMIXEAR.nPos, ecx
		mov	eax, [ebp+frmFrac]
		mov	[esi]A3DMIXEAR.dwFrac, eax
		leave
		ret	16
	}
}

/* =============================================================
// A3dMixStepM16Linear()
//
//
// Accumulate 16-bit mono into one channel with linear interpolation
// and a gain ramp.
// =============================================================*/

void __declspec(naked) __stdcall
A3dMixStepM16Linear(float *pMix, A3DMIXEAR *pEar, A3DMIXSTATE *pState, int cSamples)
{
	__asm
	{
		push	ebp
		mov	ebp, esp
		add	esp, 0FFFFFFE0h
		push	esi
		push	edi
		push	ecx
		mov	edi, [ebp+10h]
		mov	esi, [ebp+0Ch]
		mov	word ptr [ebp+frmStep], 0
		mov	dword ptr [ebp+frmStepWhole], 0
		mov	edx, [edi]A3DMIXSTATE.dwLooping
		mov	[ebp+frmLooping], edx
		mov	eax, [edi]A3DMIXSTATE.cbSamples
		shr	eax, cl
		or	edx, edx
		jnz	short have_limit
		mov	ebx, [esi]A3DMIXEAR.nLookahead
		shr	ebx, 10h
		sub	eax, ebx
have_limit:
		dec	eax
		mov	[ebp+frmLimit], eax

		mov	ecx, [ebp+14h]
		call	A3dMixStepCount
		mov	[ebp+frmStep], eax
		mov	ebx, eax
		mov	[ebp+frmStepRampM16], ecx

		finit
		fld	qword ptr [g_dFixedToFloat]
		fild	dword ptr [esi]A3DMIXEAR.nGainPrev
		fild	dword ptr [esi]A3DMIXEAR.nGain
		fsub	st(0), st(1)
		fxch	st(1)
		fmul	st(0), st(2)
		fxch	st(1)
		fmul	st(0), st(2)
		fild	dword ptr [ebp+14h]
		fdivp	st(1), st(0)
		fxch	st(2)
		fxch	st(1)
		fadd	st(0), st(2)

		mov	ecx, [esi]A3DMIXEAR.nGain
		mov	[esi]A3DMIXEAR.nGainPrev, ecx
		mov	ecx, [esi]A3DMIXEAR.nPos
		mov	eax, [esi]A3DMIXEAR.dwFrac
		mov	[ebp+frmFrac], eax

		push	esi
		mov	esi, [edi]A3DMIXSTATE.pSamples
		mov	edi, [ebp+8]
		mov	edx, [ebp+14h]
		cmp	ecx, [ebp+frmLimit]
		jz	short fetch_wrap
		jl	short fetch
		jmp	short at_limit
fetch:
		fild	word ptr [esi+ecx*2]
		fild	word ptr [esi+ecx*2+2]
accumulate:

		fild	dword ptr [ebp+frmFrac]
		add	edi, 8
		add	word ptr [ebp+frmFrac], bx
		fmul	st(0), st(4)
		fxch	st(1)
		fsub	st(0), st(2)
		fxch	st(1)
		fmul	st(0), st(3)
		fxch	st(2)
		fmul	st(0), st(3)
		fxch	st(3)
		fadd	st(0), st(5)
		fxch	st(2)
		fmulp	st(1), st(0)
		adc	ecx, [ebp+frmStepWhole]
		add	ebx, [ebp+frmStepRampM16]
		faddp	st(2), st(0)
		fxch	st(1)
		fadd	dword ptr [edi-8]
		mov	[ebp+frmStep], ebx
		dec	edx
		fstp	dword ptr [edi-8]
		jz	short done
		cmp	ecx, [ebp+frmLimit]
		jb	short fetch
		ja	short at_limit
fetch_wrap:
		fild	word ptr [esi+ecx*2]
		fild	word ptr [esi]
		jmp	short accumulate
at_limit:
		cmp	dword ptr [ebp+frmLooping], 0
		jz	short done
		mov	eax, [ebp+frmLimit]
		inc	eax
		sub	ecx, eax
		jmp	short fetch
done:
		pop	esi
		mov	ebx, [ebp+frmLimit]
		inc	ebx
		cmp	ecx, ebx
		jle	short store
		mov	ecx, ebx
store:
		mov	[esi]A3DMIXEAR.nPos, ecx
		mov	eax, [ebp+frmFrac]
		mov	[esi]A3DMIXEAR.dwFrac, eax
		pop	ecx
		pop	edi
		pop	esi
		leave
		ret	16
	}
}

/* =============================================================
// A3dMixFetchM8Four()
//
//
// Push four consecutive 8-bit mono samples onto the x87 stack.
// =============================================================*/

void __declspec(naked)
A3dMixFetchM8Four(void)
{
	__asm
	{
		push	ebp
		mov	ebp, esp
		add	esp, 0FFFFFFFCh
		xor	eax, eax
		mov	ah, byte ptr [ecx+esi]
		xor	eax, 8000h
		mov	word ptr [ebp+frmSampleA], ax
		mov	ah, byte ptr [ecx+esi+1]
		xor	eax, 8000h
		mov	word ptr [ebp+frmSampleB], ax
		fild	word ptr [ebp+frmSampleA]
		mov	ah, byte ptr [ecx+esi+2]
		xor	eax, 8000h
		mov	word ptr [ebp+frmSampleA], ax
		fild	word ptr [ebp+frmSampleB]
		mov	ah, byte ptr [ecx+esi+3]
		xor	eax, 8000h
		mov	word ptr [ebp+frmSampleB], ax
		fild	word ptr [ebp+frmSampleA]
		fild	word ptr [ebp+frmSampleB]
		leave
		ret
	}
}

/* =============================================================
// A3dMixFetchM8FourWrap()
//
//
// Push four 8-bit mono samples, wrapping at the buffer end.
// =============================================================*/

void __declspec(naked)
A3dMixFetchM8FourWrap(void)
{
	__asm
	{
		push	ebp
		mov	ebp, esp
		add	esp, 0FFFFFFF8h
		mov	[ebp+frmLeft], eax
		xor	eax, eax
		mov	ah, byte ptr [ecx+esi]
		xor	eax, 8000h
		mov	word ptr [ebp+frmSampleA], ax
		fild	word ptr [ebp+frmSampleA]
		dec	dword ptr [ebp+frmLeft]
		jnz	short two_left
		mov	ah, byte ptr [esi]
		xor	eax, 8000h
		mov	word ptr [ebp+frmSampleA], ax
		fild	word ptr [ebp+frmSampleA]
		mov	ah, byte ptr [esi+1]
		xor	eax, 8000h
		mov	word ptr [ebp+frmSampleA], ax
		fild	word ptr [ebp+frmSampleA]
		mov	ah, byte ptr [esi+2]
		xor	eax, 8000h
		mov	word ptr [ebp+frmSampleA], ax
		fild	word ptr [ebp+frmSampleA]
		leave
		ret
two_left:
		mov	ah, byte ptr [ecx+esi+1]
		xor	eax, 8000h
		mov	word ptr [ebp+frmSampleA], ax
		fild	word ptr [ebp+frmSampleA]
		dec	dword ptr [ebp+frmLeft]
		jnz	short three_left
		mov	ah, byte ptr [esi]
		xor	eax, 8000h
		mov	word ptr [ebp+frmSampleA], ax
		fild	word ptr [ebp+frmSampleA]
		mov	ah, byte ptr [esi+1]
		xor	eax, 8000h
		mov	word ptr [ebp+frmSampleA], ax
		fild	word ptr [ebp+frmSampleA]
		leave
		ret
three_left:
		mov	ah, byte ptr [ecx+esi+1]
		xor	eax, 8000h
		mov	word ptr [ebp+frmSampleA], ax
		fild	word ptr [ebp+frmSampleA]
		mov	ah, byte ptr [esi]
		xor	eax, 8000h
		mov	word ptr [ebp+frmSampleA], ax
		fild	word ptr [ebp+frmSampleA]
		leave
		ret
	}
}

/* =============================================================
// A3dMixFetchM8FourTail()
//
//
// Push four 8-bit mono samples, zero-padding at the buffer end.
// =============================================================*/

void __declspec(naked)
A3dMixFetchM8FourTail(void)
{
	__asm
	{
		push	ebp
		mov	ebp, esp
		add	esp, 0FFFFFFF8h
		mov	[ebp+frmLeft], eax
		xor	eax, eax
		mov	ah, byte ptr [ecx+esi]
		xor	eax, 8000h
		mov	word ptr [ebp+frmSampleA], ax
		fild	word ptr [ebp+frmSampleA]
		dec	dword ptr [ebp+frmLeft]
		jz	short three_zeros
		mov	ah, byte ptr [ecx+esi+1]
		xor	eax, 8000h
		mov	word ptr [ebp+frmSampleA], ax
		fild	word ptr [ebp+frmSampleA]
		dec	dword ptr [ebp+frmLeft]
		jz	short two_zeros
		mov	ah, byte ptr [ecx+esi+2]
		xor	eax, 8000h
		mov	word ptr [ebp+frmSampleA], ax
		fild	word ptr [ebp+frmSampleA]
		jmp	short one_zero
three_zeros:
		fldz
two_zeros:
		fldz
one_zero:
		fldz
		leave
		ret
	}
}

/* =============================================================
// A3dMixFetchS8Four()
//
//
// Push four consecutive 8-bit stereo samples onto the x87 stack.
// =============================================================*/

void __declspec(naked)
A3dMixFetchS8Four(void)
{
	__asm
	{
		push	ebp
		mov	ebp, esp
		add	esp, 0FFFFFFFCh
		xor	eax, eax
		mov	ah, byte ptr [esi+ecx*2]
		xor	eax, 8000h
		sar	ax, 1
		mov	word ptr [ebp+frmSampleA], ax
		mov	ah, byte ptr [esi+ecx*2+1]
		xor	eax, 8000h
		sar	ax, 1
		add	word ptr [ebp+frmSampleA], ax
		mov	ah, byte ptr [esi+ecx*2+2]
		xor	eax, 8000h
		sar	ax, 1
		mov	word ptr [ebp+frmSampleB], ax
		mov	ah, byte ptr [esi+ecx*2+3]
		xor	eax, 8000h
		sar	ax, 1
		add	word ptr [ebp+frmSampleB], ax
		fild	word ptr [ebp+frmSampleA]
		mov	ah, byte ptr [esi+ecx*2+4]
		xor	eax, 8000h
		sar	ax, 1
		mov	word ptr [ebp+frmSampleA], ax
		mov	ah, byte ptr [esi+ecx*2+5]
		xor	eax, 8000h
		sar	ax, 1
		add	word ptr [ebp+frmSampleA], ax
		fild	word ptr [ebp+frmSampleB]
		mov	ah, byte ptr [esi+ecx*2+6]
		xor	eax, 8000h
		sar	ax, 1
		mov	word ptr [ebp+frmSampleB], ax
		mov	ah, byte ptr [esi+ecx*2+7]
		xor	eax, 8000h
		sar	ax, 1
		add	word ptr [ebp+frmSampleB], ax
		fild	word ptr [ebp+frmSampleA]
		fild	word ptr [ebp+frmSampleB]
		leave
		ret
	}
}

/* =============================================================
// A3dMixFetchS8FourWrap()
//
//
// Push four 8-bit stereo samples, wrapping at the buffer end.
// =============================================================*/

void __declspec(naked)
A3dMixFetchS8FourWrap(void)
{
	__asm
	{
		push	ebp
		mov	ebp, esp
		add	esp, 0FFFFFFF8h
		mov	[ebp+frmLeft], eax
		xor	eax, eax
		mov	ah, byte ptr [esi+ecx*2]
		xor	eax, 8000h
		sar	ax, 1
		mov	word ptr [ebp+frmSampleA], ax
		mov	ah, byte ptr [esi+ecx*2+1]
		xor	eax, 8000h
		sar	ax, 1
		add	word ptr [ebp+frmSampleA], ax
		fild	word ptr [ebp+frmSampleA]
		dec	dword ptr [ebp+frmLeft]
		jnz	short two_left
		mov	ah, byte ptr [esi]
		xor	eax, 8000h
		sar	ax, 1
		mov	word ptr [ebp+frmSampleA], ax
		mov	ah, byte ptr [esi+1]
		xor	eax, 8000h
		sar	ax, 1
		add	word ptr [ebp+frmSampleA], ax
		fild	word ptr [ebp+frmSampleA]
		mov	ah, byte ptr [esi+2]
		xor	eax, 8000h
		sar	ax, 1
		mov	word ptr [ebp+frmSampleA], ax
		mov	ah, byte ptr [esi+3]
		xor	eax, 8000h
		sar	ax, 1
		add	word ptr [ebp+frmSampleA], ax
		fild	word ptr [ebp+frmSampleA]
		mov	ah, byte ptr [esi+4]
		xor	eax, 8000h
		sar	ax, 1
		mov	word ptr [ebp+frmSampleA], ax
		mov	ah, byte ptr [esi+5]
		xor	eax, 8000h
		sar	ax, 1
		add	word ptr [ebp+frmSampleA], ax
		fild	word ptr [ebp+frmSampleA]
		leave
		ret
two_left:
		mov	ah, byte ptr [esi+ecx*2+2]
		xor	eax, 8000h
		sar	ax, 1
		mov	word ptr [ebp+frmSampleA], ax
		mov	ah, byte ptr [esi+ecx*2+3]
		xor	eax, 8000h
		sar	ax, 1
		add	word ptr [ebp+frmSampleA], ax
		fild	word ptr [ebp+frmSampleA]
		dec	dword ptr [ebp+frmLeft]
		jnz	short three_left
		mov	ah, byte ptr [esi]
		xor	eax, 8000h
		sar	ax, 1
		mov	word ptr [ebp+frmSampleA], ax
		mov	ah, byte ptr [esi+1]
		xor	eax, 8000h
		sar	ax, 1
		add	word ptr [ebp+frmSampleA], ax
		fild	word ptr [ebp+frmSampleA]
		mov	ah, byte ptr [esi+2]
		xor	eax, 8000h
		sar	ax, 1
		mov	word ptr [ebp+frmSampleA], ax
		mov	ah, byte ptr [esi+3]
		xor	eax, 8000h
		sar	ax, 1
		add	word ptr [ebp+frmSampleA], ax
		fild	word ptr [ebp+frmSampleA]
		leave
		ret
three_left:
		mov	ah, byte ptr [esi+ecx*2+4]
		xor	eax, 8000h
		sar	ax, 1
		mov	word ptr [ebp+frmSampleA], ax
		mov	ah, byte ptr [esi+ecx*2+5]
		xor	eax, 8000h
		sar	ax, 1
		add	word ptr [ebp+frmSampleA], ax
		fild	word ptr [ebp+frmSampleA]
		mov	ah, byte ptr [esi]
		xor	eax, 8000h
		sar	ax, 1
		mov	word ptr [ebp+frmSampleA], ax
		mov	ah, byte ptr [esi+1]
		xor	eax, 8000h
		sar	ax, 1
		add	word ptr [ebp+frmSampleA], ax
		fild	word ptr [ebp+frmSampleA]
		leave
		ret
	}
}

/* =============================================================
// A3dMixFetchS8FourTail()
//
//
// Push four 8-bit stereo samples, zero-padding at the buffer end.
// =============================================================*/

void __declspec(naked)
A3dMixFetchS8FourTail(void)
{
	__asm
	{
		push	ebp
		mov	ebp, esp
		add	esp, 0FFFFFFF8h
		mov	[ebp+frmLeft], eax
		xor	eax, eax
		mov	ah, byte ptr [esi+ecx*2]
		xor	eax, 8000h
		sar	ax, 1
		mov	word ptr [ebp+frmSampleA], ax
		mov	ah, byte ptr [esi+ecx*2+1]
		xor	eax, 8000h
		sar	ax, 1
		add	word ptr [ebp+frmSampleA], ax
		fild	word ptr [ebp+frmSampleA]
		dec	dword ptr [ebp+frmLeft]
		jz	short three_zeros
		mov	ah, byte ptr [esi+ecx*2+2]
		xor	eax, 8000h
		sar	ax, 1
		mov	word ptr [ebp+frmSampleA], ax
		mov	ah, byte ptr [esi+ecx*2+3]
		xor	eax, 8000h
		sar	ax, 1
		add	word ptr [ebp+frmSampleA], ax
		fild	word ptr [ebp+frmSampleA]
		dec	dword ptr [ebp+frmLeft]
		jz	short two_zeros
		mov	ah, byte ptr [esi+ecx*2+4]
		xor	eax, 8000h
		sar	ax, 1
		mov	word ptr [ebp+frmSampleA], ax
		mov	ah, byte ptr [esi+ecx*2+5]
		xor	eax, 8000h
		sar	ax, 1
		add	word ptr [ebp+frmSampleA], ax
		fild	word ptr [ebp+frmSampleA]
		jmp	short one_zero
three_zeros:
		fldz
two_zeros:
		fldz
one_zero:
		fldz
		leave
		ret
	}
}

/* =============================================================
// A3dMixStep4Tap()
//
//
// Resample one ear into a contiguous float buffer using four taps.
// =============================================================*/

void __declspec(naked) __stdcall
A3dMixStep4Tap(float *pMix, A3DMIXEAR *pEar, A3DMIXSTATE *pState, int cSamples)
{
	__asm
	{
		push	ebp
		mov	ebp, esp
		add	esp, 0FFFFFFD8h
		mov	edi, [ebp+10h]
		mov	esi, [ebp+0Ch]
		mov	eax, [edi]A3DMIXSTATE.pfnFetch4
		mov	[ebp+frmFetch], eax
		mov	eax, [edi]A3DMIXSTATE.pfnFetch4Wrap
		mov	[ebp+frmFetchWrap], eax
		mov	eax, [edi]A3DMIXSTATE.pfnFetch4Tail
		mov	[ebp+frmFetchTail], eax
		mov	word ptr [ebp+frmStep], 0
		mov	dword ptr [ebp+frmStepWhole], 0
		mov	edx, [edi]A3DMIXSTATE.dwLooping
		mov	[ebp+frmLooping], edx
		mov	eax, [edi]A3DMIXSTATE.cbSamples
		mov	ecx, [edi]A3DMIXSTATE.cShift
		shr	eax, cl
		or	edx, edx
		jnz	short have_limit
		mov	ebx, [esi]A3DMIXEAR.nLookahead
		shr	ebx, 10h
		sub	eax, ebx
have_limit:
		sub	eax, 4
		mov	[ebp+frmLimit], eax

		mov	ecx, [ebp+14h]
		call	A3dMixStepCount
		mov	[ebp+frmStep], eax
		mov	ebx, eax
		mov	[ebp+frmStepRamp], ecx
		mov	[ebp+frmStep], ebx

		finit
		fld	qword ptr [g_dHalf]

		mov	ecx, [esi]A3DMIXEAR.nGain
		mov	[esi]A3DMIXEAR.nGainPrev, ecx
		mov	ecx, [esi]A3DMIXEAR.nPos
		mov	eax, [esi]A3DMIXEAR.dwFrac
		mov	[ebp+frmFrac], eax

		push	esi
		mov	esi, [edi]A3DMIXSTATE.pSamples
		mov	edi, [ebp+8]
		mov	edx, [ebp+14h]
test_pos:
		cmp	ecx, [ebp+frmLimit]
		jg	short at_limit
fetch:
		call	dword ptr [ebp+frmFetch]
accumulate:

		fxch	st(3)
		mov	eax, [ebp+frmFrac]
		shr	eax, 5
		and	eax, 7F0h
		lea	eax, g_afInterp[eax]
		fmul	dword ptr [eax]
		fxch	st(2)
		add	edi, 4
		add	word ptr [ebp+frmFrac], bx
		fmul	dword ptr [eax+4]
		fxch	st(1)
		adc	ecx, [ebp+frmStepWhole]
		add	ebx, [ebp+frmStepRamp]
		fmul	dword ptr [eax+8]
		fxch	st(1)
		faddp	st(2), st(0)
		fxch	st(2)
		fmul	dword ptr [eax+0Ch]
		fxch	st(2)
		faddp	st(1), st(0)
		faddp	st(1), st(0)
		mov	[ebp+frmStep], ebx
		fstp	dword ptr [edi-4]

		dec	edx
		jz	short done
		cmp	ecx, [ebp+frmLimit]
		jle	short fetch
at_limit:
		mov	eax, [ebp+frmLimit]
		add	eax, 4
		cmp	dword ptr [ebp+frmLooping], 0
		jz	short at_end
		sub	eax, ecx
		jnz	short past_or_across
		xor	ecx, ecx
		jmp	short fetch
past_or_across:
		jnb	short fetch_wrap
		neg	eax
		mov	ecx, eax
		jmp	short test_pos
fetch_wrap:
		call	dword ptr [ebp+frmFetchWrap]
		jmp	short accumulate
at_end:
		sub	eax, ecx
		jz	short done
		jnb	short fetch_tail
		mov	ecx, [ebp+frmLimit]
		add	ecx, 4
		jmp	short done
fetch_tail:
		call	dword ptr [ebp+frmFetchTail]
		jmp	short accumulate
done:
		pop	esi
		mov	[esi]A3DMIXEAR.nPos, ecx
		mov	eax, [ebp+frmFrac]
		mov	[esi]A3DMIXEAR.dwFrac, eax
		leave
		ret	16
	}
}

/* =============================================================
// A3dMixGenerate()
//
// Generate resampled samples for one ear and zero-fill the unproduced tail.
// =============================================================*/

void __declspec(naked)
A3dMixGenerate(A3DMIXSTATE *pState, A3DMIXEAR *pEar, float *pOut,
	       DWORD cSamples)
{
	__asm
	{
		push	ebp
		mov	ebp, esp
		add	esp, 0FFFFFFFCh
		push	esi
		push	edi
		push	ebx
		push	ecx
		push	edx

		mov	esi, [ebp+8]
		mov	edx, [ebp+0Ch]
		mov	edi, [ebp+10h]
		mov	ebx, [esi]A3DMIXSTATE.pfnStep4Tap
		mov	[ebp-4], ebx
		mov	ecx, [esi]A3DMIXSTATE.cShift
		xor	eax, eax
		cmp	[esi]A3DMIXSTATE.cbSamples, 0
		jnz	short have_samples
		mov	edx, [ebp+14h]
		jmp	short silence
have_samples:
		mov	eax, [esi]A3DMIXSTATE.dwPos
		cmp	eax, [esi]A3DMIXSTATE.cbSamples
		jl	short interpolate
		mov	edx, [ebp+14h]
		jmp	short silence
interpolate:
		mov	eax, [ebp+14h]
		push	eax
		push	esi
		push	edx
		push	edi
		call	dword ptr [ebp-4]
		or	edx, edx
		jz	short done
silence:
		xor	eax, eax
pad:
		mov	[edi], eax
		add	edi, 4
		dec	edx
		jnz	short pad
done:
		finit

		pop	edx
		pop	ecx
		pop	ebx
		pop	edi
		pop	esi
		leave
		ret
	}
}

/* =============================================================
// A3dMixFinish()
//
// Advance the byte cursor from the left ear and update the step target.
// =============================================================*/

void __declspec(naked)
A3dMixFinish(A3DMIXSTATE *pState, DWORD cSamples)
{
	__asm
	{
		push	ebp
		mov	ebp, esp
		push	esi
		push	ebx
		push	ecx
		push	edx

		mov	esi, [ebp+8]
		mov	ebx, [esi]A3DMIXSTATE.earLeft.nResidue
		mov	eax, [esi]A3DMIXSTATE.nStepTarget
		sar	ebx, 8
		sar	eax, 8
		imul	ebx
		shr	eax, 10h
		add	eax, [esi]A3DMIXSTATE.earLeft.nPos
		mov	ecx, [esi]A3DMIXSTATE.cShift
		shl	eax, cl
		cmp	eax, [esi]A3DMIXSTATE.cbSamples
		jl	short store
		cmp	[esi]A3DMIXSTATE.dwLooping, 0
		jz	short stop
		sub	eax, [esi]A3DMIXSTATE.cbSamples
		jmp	short store
stop:
		mov	eax, [esi]A3DMIXSTATE.cbSamples
store:
		mov	[esi]A3DMIXSTATE.dwPos, eax

		mov	ecx, [ebp+0Ch]
		call	A3dMixLookahead

		pop	edx
		pop	ecx
		pop	ebx
		pop	esi
		leave
		ret
	}
}

/* =============================================================
// A3dMixConvolve()
// (RE) rtl:0x1002CBED; dbg:0x1007554D; thunk dbg:0x10001627
//
// Accumulate filtered samples into one interleaved channel while ramping
// the coefficients.
// =============================================================*/

void __declspec(naked)
A3dMixConvolve(A3DMIXEAR *pEar, const float *pIn, float *pOut, DWORD cSamples)
{
	__asm
	{
		push	ebp
		mov	ebp, esp
		add	esp, -4
		push	esi
		push	edi
		push	ebx
		push	ecx
		push	edx

		cmp	dword ptr [ebp+14h], 0
		jnz	short have_samples
		jmp	done
have_samples:
		finit

		mov	esi, [ebp+8]

		mov	ecx, [esi]A3DMIXEAR.nGain
		cmp	ecx, [esi]A3DMIXEAR.nGainPrev
		jnz	short have_gain
		or	ecx, ecx
		jnz	short have_gain
		pop	edx
		pop	ecx
		pop	ebx
		pop	edi
		pop	esi
		leave
		ret
have_gain:
		mov	ecx, [esi]A3DMIXEAR.cTaps
		or	ecx, ecx
		jnz	short have_taps
		pop	edx
		pop	ecx
		pop	ebx
		pop	edi
		pop	esi
		leave
		ret
have_taps:

		mov	eax, [ebp+14h]
		add	eax, ecx
		dec	eax
		cdq
		idiv	ecx
		mov	[ebp+frmRampBlocks], eax

		lea	eax, [esi]A3DMIXEAR.afTap
		lea	edx, [esi]A3DMIXEAR.afTapStep
		lea	ebx, [esi]A3DMIXEAR.afTapWanted

		fild	dword ptr [esi]A3DMIXEAR.nGain
		fild	dword ptr [ebp+frmRampBlocks]
		fxch	st(1)
		fmul	qword ptr [g_dFixedToFloat]
		fld1
		fxch	st(2)
		fdivp	st(2), st
ramp:
		fld	dword ptr [ebx]
		fmul	st(0), st(1)
		fsub	dword ptr [eax]
		fmul	st(0), st(2)
		fstp	dword ptr [edx]
		add	eax, 4
		add	edx, 4
		add	ebx, 4
		dec	ecx
		jnz	short ramp

		finit

		mov	edi, [ebp+10h]
		lea	ebx, [esi]A3DMIXEAR.afTap
		lea	eax, [esi]A3DMIXEAR.afTapStep
		mov	ecx, [esi]A3DMIXEAR.cTaps
		mov	esi, [ebp+0Ch]
		add	dword ptr [ebp+0Ch], 4
		xor	edx, edx
sample:
		fld	dword ptr [edi]

		shr	ecx, 1
		jnb	short taps2
		fld	dword ptr [esi]
		fmul	dword ptr [ebx]
		add	esi, 4
		add	ebx, 4
		faddp	st(1), st
taps2:
		shr	ecx, 1
		jnb	short taps4
		fld	dword ptr [esi]
		fld	dword ptr [esi+4]
		fxch	st(1)
		fmul	dword ptr [ebx]
		fxch	st(1)
		fmul	dword ptr [ebx+4]
		fxch	st(1)
		faddp	st(2), st
		add	esi, 8
		add	ebx, 8
		faddp	st(1), st
taps4:
		shr	ecx, 1
		jnb	short taps8
		fld	dword ptr [esi]
		fld	dword ptr [esi+4]
		fld	dword ptr [esi+8]
		fxch	st(2)
		fmul	dword ptr [ebx]
		fld	dword ptr [esi+0Ch]
		fxch	st(2)
		fmul	dword ptr [ebx+4]
		fxch	st(1)
		faddp	st(4), st
		fxch	st(2)
		fmul	dword ptr [ebx+8]
		fxch	st(2)
		faddp	st(3), st
		fmul	dword ptr [ebx+0Ch]
		fxch	st(1)
		faddp	st(2), st
		add	esi, 10h
		add	ebx, 10h
		faddp	st(1), st
taps8:
		or	ecx, ecx
		jz	short store
eight:
		fld	dword ptr [esi]
		fld	dword ptr [esi+4]
		fld	dword ptr [esi+8]
		fxch	st(2)
		fmul	dword ptr [ebx]
		fld	dword ptr [esi+0Ch]
		fxch	st(2)
		fmul	dword ptr [ebx+4]
		fxch	st(1)
		faddp	st(4), st
		fxch	st(2)
		fmul	dword ptr [ebx+8]
		fxch	st(2)
		faddp	st(3), st
		fld	dword ptr [esi+10h]
		fxch	st(1)
		fmul	dword ptr [ebx+0Ch]
		fxch	st(2)
		faddp	st(3), st
		fmul	dword ptr [ebx+10h]
		fld	dword ptr [esi+14h]
		fld	dword ptr [esi+18h]
		fxch	st(3)
		faddp	st(4), st
		fmul	dword ptr [ebx+14h]
		fld	dword ptr [esi+1Ch]
		fxch	st(2)
		faddp	st(4), st
		fxch	st(2)
		fmul	dword ptr [ebx+18h]
		fxch	st(2)
		faddp	st(3), st
		fmul	dword ptr [ebx+1Ch]
		fxch	st(1)
		faddp	st(2), st
		add	esi, 20h
		add	ebx, 20h
		faddp	st(1), st
		dec	ecx
		jnz	short eight
store:
		fstp	dword ptr [edi]
		dec	dword ptr [ebp+14h]
		jz	short done

		mov	esi, [ebp+8]
		lea	ebx, [esi]A3DMIXEAR.afTap
		fld	dword ptr [ebx+edx*4]
		lea	eax, [esi]A3DMIXEAR.afTapStep
		mov	ecx, [esi]A3DMIXEAR.cTaps
		add	edi, 8
		fadd	dword ptr [eax+edx*4]
		mov	esi, [ebp+0Ch]
		add	dword ptr [ebp+0Ch], 4
		fstp	dword ptr [ebx+edx*4]
		inc	edx
		cmp	edx, ecx
		jl	short wrapped
		xor	edx, edx
wrapped:
		jmp	sample
done:
		finit

		pop	edx
		pop	ecx
		pop	ebx
		pop	edi
		pop	esi
		leave
		ret
	}
}

/* (RE) dbg:0x1014CBD0 */
static A3DFILTERCOEFF	g_aFilterCoeff[4] =
{
	{ 5,  { 0.24488053f, -0.022858365f, -0.77053136f },
	      { 0.93972594f, 0.5445418f, 0.66765344f, 0.6754662f,
		0.7714774f, 0.7120579f, -0.6432081f, -0.9355754f } },

	{ 5,  { 0.24488053f, -0.022858365f, -0.77053136f },
	      { 0.93972594f, 0.5445418f, 0.66765344f, 0.6754662f,
		0.7714774f, 0.7120579f, -0.6432081f, -0.9355754f } },

	{ 10, { 0.20249031f, -0.089571826f, -0.8836024f },
	      { 0.991302f, 0.763146f, -0.933744f, 0.82577f,
		-0.96173f, 1.0f, -0.95349f, -0.969237f } },

	{ 10, { 0.20249031f, -0.089571826f, -0.8836024f },
	      { 0.991302f, 0.763146f, -0.933744f, 0.82577f,
		-0.96173f, 1.0f, -0.95349f, -0.969237f } }
};

C_ASSERT(sizeof(A3DFILTERSTATE) == A3D_FILTER_STATE_DWORDS * 4);
C_ASSERT(sizeof(A3DFILTERCOEFF) == A3D_FILTER_COEFF_SIZE);
C_ASSERT(sizeof(A3DFILTERMODE) == 0x10);

/* =============================================================
// InitFilterState()
// (RE) rtl:0x1002CD8F; dbg:0x100756EF
//
// Clear the output-filter state and record the rate in kHz. Original defect:
// pUnused is ignored, leaving the caller's mode block uninitialized.
// =============================================================*/

void __declspec(naked)
InitFilterState(void *pUnused, A3DFILTERSTATE *pState, int nRateKHz)
{
	__asm
	{
		push	ebp
		mov	ebp, esp
		push	esi
		push	ecx

		xor	eax, eax
		mov	esi, [ebp+8]
		mov	ecx, 4
		mov	esi, [ebp+0Ch]
head:
		mov	[esi], eax
		add	esi, 4
		dec	ecx
		jnz	short head

		mov	ecx, A3D_FILTER_STATE_DWORDS
		mov	esi, [ebp+0Ch]
all:
		mov	[esi], eax
		add	esi, 4
		dec	ecx
		jnz	short all

		mov	esi, [ebp+0Ch]
		mov	eax, [ebp+10h]
		mov	[esi], eax

		pop	ecx
		pop	esi
		leave
		ret
	}
}

/* =============================================================
// ApplyFilter()
// (RE) rtl:0x1002CDC5; dbg:0x10075725
//
// Select output-filter coefficients and process the stereo buffer.
// The filter's purpose and intended input are unresolved; its Debug caller
// DAL_A2D at dbg:0x1004B7C0 (thunk dbg:0x10002BDA) has no known callers.
//
// Returns: 0, including when the sample count or mode is zero.
// =============================================================*/

/* Assembly mode values  */
#define A3D_FILTER_MODE_DISABLED	0
#define A3D_FILTER_MODE_ALTERNATE	0FFFDh

int __declspec(naked)
ApplyFilter(float *pBuffer, const A3DFILTERMODE *pMode,
		   A3DFILTERSTATE *pState, DWORD cSamples)
{
	__asm
	{
		push	ebp
		mov	ebp, esp
		push	ebx
		push	ecx
		push	esi

		mov	ecx, [ebp+14h]
		or	ecx, ecx
		jz	short finished

		mov	esi, [ebp+0Ch]
		cmp	[esi]A3DFILTERMODE.nMode, A3D_FILTER_MODE_DISABLED
		jz	short finished

		lea	ebx, [g_aFilterCoeff]

		cmp	[esi]A3DFILTERMODE.nMode, A3D_FILTER_MODE_ALTERNATE
		jnz	short not_mode
		add	ebx, A3D_FILTER_COEFF_SIZE
not_mode:
		mov	esi, [ebp+10h]
		cmp	[esi]A3DFILTERSTATE.nRateKHz, 2Ch
		jnz	short not_44k
		add	ebx, A3D_FILTER_COEFF_SIZE * 2
not_44k:
		mov	eax, [ebp+8]
		push	dword ptr [ebp+8]
		push	ebx
		push	esi
		push	ecx
		call	CombFilter

		mov	eax, [ebp+8]
		push	eax
		push	ebx
		push	esi
		push	ecx
		call	RunStereo
finished:
		finit
		xor	eax, eax

		pop	esi
		pop	ecx
		pop	ebx
		leave
		ret
	}
}

/* =============================================================
// CombFilter()
// (RE) rtl:0x1002CE1B; dbg:0x1007577B
//
// Apply a stereo comb filter with cross-channel delay feedback.
// Leaves three coefficients on the x87 stack; the caller must reset it.
// =============================================================*/

void __declspec(naked) __stdcall
CombFilter(DWORD cSamples, A3DFILTERSTATE *pState,
		   const A3DFILTERCOEFF *pCoeff, float *pBuffer)
{
	__asm
	{
		push	ebp
		mov	ebp, esp
		add	esp, -4
		push	esi
		push	edi
		push	ebx
		push	ecx
		push	edx

		mov	esi, [ebp+0Ch]
		mov	ebx, [ebp+10h]
		mov	edi, [ebp+14h]

		lea	eax, [ebx]A3DFILTERCOEFF.afComb
		finit
		fld	dword ptr [eax]
		fld	dword ptr [eax+4]
		fld	dword ptr [eax+8]

		fld	dword ptr [esi]A3DFILTERSTATE.chLeft.fLastCombInput
		fld	dword ptr [esi]A3DFILTERSTATE.chRight.fLastCombInput
		fld	dword ptr [esi]A3DFILTERSTATE.chLeft.fLastCombFeedback
		fld	dword ptr [esi]A3DFILTERSTATE.chRight.fLastCombFeedback

		mov	eax, [ebx]A3DFILTERCOEFF.cDelay
		mov	[ebp+frmDelay], eax

		lea	eax, [esi]A3DFILTERSTATE.chLeft.afLine
		lea	ebx, [esi]A3DFILTERSTATE.chRight.afLine

		mov	edx, [esi]A3DFILTERSTATE.nDelayPos
		cmp	edx, [ebp+frmDelay]
		jl	short in_range
		xor	edx, edx
in_range:
		mov	ecx, [ebp+8]
frame:
		fld	dword ptr [edi]
		fmul	st(0), st(7)
		fxch	st(4)
		fmul	st(0), st(6)
		fxch	st(2)
		fmul	st(0), st(5)
		fxch	st(4)
		faddp	st(2), st
		fld	dword ptr [edi]
		fxch	st(2)
		fsubr	st(4), st
		fxch	st(2)
		fst	st(2)
		fsub	dword ptr [ebx+edx*4]
		fstp	dword ptr [edi]

		fld	dword ptr [edi+4]
		fxch	st(3)
		fmul	st(0), st(6)
		fxch	st(3)
		fmul	st(0), st(7)
		fxch	st(1)
		fmul	st(0), st(5)
		fxch	st(3)
		faddp	st(1), st
		fld	dword ptr [edi+4]
		fxch	st(1)
		fsubr	st(3), st
		fxch	st(1)
		fst	st(1)
		fsub	dword ptr [eax+edx*4]
		fstp	dword ptr [edi+4]

		fxch	st(3)
		fst	dword ptr [eax+edx*4]
		fxch	st(1)
		fxch	st(2)
		fst	dword ptr [ebx+edx*4]
		fxch	st(2)
		fxch	st(3)
		fxch	st(2)

		inc	edx
		add	edi, 8
		cmp	edx, [ebp+frmDelay]
		jl	short stepped
		xor	edx, edx
stepped:
		dec	ecx
		jnz	short frame

		fstp	dword ptr [esi]A3DFILTERSTATE.chRight.fLastCombFeedback
		fstp	dword ptr [esi]A3DFILTERSTATE.chLeft.fLastCombFeedback
		fstp	dword ptr [esi]A3DFILTERSTATE.chRight.fLastCombInput
		fstp	dword ptr [esi]A3DFILTERSTATE.chLeft.fLastCombInput

		mov	[esi]A3DFILTERSTATE.nDelayPos, edx

		pop	edx
		pop	ecx
		pop	ebx
		pop	edi
		pop	esi
		leave
		ret	10h
	}
}

/* =============================================================
// RunChannel()
// (RE) rtl:0x1002CEFA; dbg:0x1007585A
//
// Transform one interleaved channel, writing two frames behind the cursor.
// The individual coefficient roles are unresolved.
// =============================================================*/

void __declspec(naked)
RunChannel(void)
{
	__asm
	{
		push	ecx
		push	edi
frame:
		fld	dword ptr [ebx+18h]
		fld	dword ptr [ebx+10h]
		fld	dword ptr [edi-10h]
		fxch	st(1)
		fmul	st(0), st(5)
		fld	dword ptr [edi-8]
		fxch	st(5)
		fmul	dword ptr [ebx+0Ch]
		fld	dword ptr [edi]
		fxch	st(3)
		fmul	dword ptr [ebx+4]
		fxch	st(1)
		faddp	st(2), st
		fxch	st(5)
		fmul	dword ptr [ebx+8]
		fxch	st(1)
		fsubp	st(5), st
		fxch	st(1)
		fmul	dword ptr [ebx]
		fxch	st(1)
		faddp	st(4), st
		fxch	st(2)
		fmul	dword ptr [ebx+1Ch]
		fxch	st(2)
		faddp	st(3), st
		fmul	st(0), st(3)
		fxch	st(2)
		fst	st(4)
		fmul	dword ptr [ebx+14h]
		fxch	st(1)
		fsubp	st(2), st
		faddp	st(1), st
		add	edi, 8
		dec	ecx
		fst	dword ptr [edi-18h]
		jnz	short frame

		pop	edi
		pop	ecx
		ret
	}
}

/* =============================================================
// RunStereo()
// (RE) rtl:0x1002CF51; dbg:0x100758B1
//
// Transform both channels and retain overlap and recursive state.
// The buffer requires four writable floats before its start.
// =============================================================*/

void __declspec(naked) __stdcall
RunStereo(DWORD cSamples, A3DFILTERSTATE *pState,
		   const A3DFILTERCOEFF *pCoeff, float *pBuffer)
{
	__asm
	{
		push	ebp
		mov	ebp, esp
		push	esi
		push	edi
		push	ebx
		push	ecx
		push	edx

		mov	esi, [ebp+0Ch]
		mov	ebx, [ebp+10h]
		lea	ebx, [ebx]A3DFILTERCOEFF.afTransform
		mov	edi, [ebp+14h]
		mov	ecx, [ebp+8]

		finit

		sub	edi, 10h
		lea	edx, [esi]A3DFILTERSTATE.afOverlap

		mov	eax, [edx]
		mov	[edi], eax
		mov	eax, [edx+4]
		mov	[edi+4], eax
		mov	eax, [edx+8]
		mov	[edi+8], eax
		mov	eax, [edx+0Ch]
		mov	[edi+0Ch], eax

		shl	ecx, 1

		mov	eax, [edi+ecx*4]
		mov	[edx], eax
		mov	eax, [edi+ecx*4+4]
		mov	[edx+4], eax
		mov	eax, [edi+ecx*4+8]
		mov	[edx+8], eax
		mov	eax, [edi+ecx*4+0Ch]
		mov	[edx+0Ch], eax

		shr	ecx, 1
		add	edi, 10h

		fld	dword ptr [esi]A3DFILTERSTATE.chLeft.fLastIntermediate
		fld	dword ptr [esi]A3DFILTERSTATE.chLeft.fPreviousIntermediate
		fld	dword ptr [esi]A3DFILTERSTATE.chLeft.fLastOutput
		call	RunChannel
		fstp	dword ptr [esi]A3DFILTERSTATE.chLeft.fLastOutput
		fstp	dword ptr [esi]A3DFILTERSTATE.chLeft.fPreviousIntermediate
		fstp	dword ptr [esi]A3DFILTERSTATE.chLeft.fLastIntermediate

		fld	dword ptr [esi]A3DFILTERSTATE.chRight.fLastIntermediate
		fld	dword ptr [esi]A3DFILTERSTATE.chRight.fPreviousIntermediate
		fld	dword ptr [esi]A3DFILTERSTATE.chRight.fLastOutput
		add	edi, 4
		call	RunChannel
		fstp	dword ptr [esi]A3DFILTERSTATE.chRight.fLastOutput
		fstp	dword ptr [esi]A3DFILTERSTATE.chRight.fPreviousIntermediate
		fstp	dword ptr [esi]A3DFILTERSTATE.chRight.fLastIntermediate

		pop	edx
		pop	ecx
		pop	ebx
		pop	edi
		pop	esi
		leave
		ret	10h
	}
}

/* =============================================================
// A3dMixInterleave()
// (RE) dbg:0x10075966; thunk dbg:0x10003CA1
//
// Convert floats to signed 16-bit samples with nearest-even rounding
// and saturation.
// =============================================================*/

/* Assembly saturation values */
#define A3D_MIX_PCM16_MIN_EXTENDED      0FFFF8000h
#define A3D_MIX_PCM16_MAX_HIGH_WORD     7FFF0000h
#define A3D_MIX_PCM16_MIN_HIGH_WORD     80000000h
#define A3D_MIX_PCM16_MAX               7FFFh
#define A3D_MIX_PCM16_MIN_WORD          8000h

void __declspec(naked)
A3dMixInterleave(const float *pIn, short *pOut, unsigned int cSamples)
{
	__asm
	{
		push	ebp
		mov	ebp, esp
		add	esp, 0FFFFFFF0h
		push	esi
		push	edi
		push	ebx
		push	ecx
		push	edx

		mov	ecx, [ebp+10h]
		or	ecx, ecx
		jnz	short start_

		pop	edx
		pop	ecx
		pop	ebx
		pop	edi
		pop	esi
		leave
		ret

start_:
		finit

		xor	edx, edx
		mov	esi, [ebp+8]
		mov	edi, [ebp+0Ch]

		shr	ecx, 1
		jnb	short pair_

		fld	dword ptr [esi+edx*4]
		fistp	dword ptr [ebp-10h]

		mov	eax, [ebp-10h]
		cmp	eax, A3D_MIX_PCM16_MAX
		jg	short one_max
		cmp	eax, A3D_MIX_PCM16_MIN_EXTENDED
		jl	short one_min

		mov	[edi+edx*2], ax
		inc	edx
		jmp	short pair_

one_max:
		mov	word ptr [edi+edx*2], A3D_MIX_PCM16_MAX
		inc	edx
		jmp	short pair_

one_min:
		mov	word ptr [edi+edx*2], A3D_MIX_PCM16_MIN_WORD
		inc	edx

pair_:
		shr	ecx, 1
		jnb	short quads_

		fld	dword ptr [esi+edx*4]
		fld	dword ptr [esi+edx*4+4]
		fxch	st(1)
		fistp	dword ptr [ebp-10h]
		fistp	dword ptr [ebp-0Ch]

		mov	eax, [ebp-10h]
		cmp	eax, A3D_MIX_PCM16_MAX
		jg	short pair_lmax
		cmp	eax, A3D_MIX_PCM16_MIN_EXTENDED
		jl	short pair_lmin

		mov	bx, ax

pair_right:
		mov	eax, [ebp-0Ch]
		cmp	eax, A3D_MIX_PCM16_MAX
		jg	short pair_rmax
		cmp	eax, A3D_MIX_PCM16_MIN_EXTENDED
		jl	short pair_rmin

		shl	eax, 10h
		mov	ax, bx
		mov	[edi+edx*2], eax
		add	edx, 2
		jmp	short quads_

pair_lmax:
		mov	ebx, A3D_MIX_PCM16_MAX
		jmp	short pair_right

pair_lmin:
		mov	ebx, A3D_MIX_PCM16_MIN_WORD
		jmp	short pair_right

pair_rmax:
		mov	eax, A3D_MIX_PCM16_MAX_HIGH_WORD
		mov	ax, bx
		mov	[edi+edx*2], eax
		add	edx, 2
		jmp	short quads_

pair_rmin:
		mov	eax, A3D_MIX_PCM16_MIN_HIGH_WORD
		mov	ax, bx
		mov	[edi+edx*2], eax
		add	edx, 2

quads_:
		cmp	ecx, 0
		jz	done_

quad_:
		fld	dword ptr [esi+edx*4]
		fld	dword ptr [esi+edx*4+4]
		fld	dword ptr [esi+edx*4+8]
		fld	dword ptr [esi+edx*4+0Ch]
		fxch	st(3)
		fistp	dword ptr [ebp-10h]
		fxch	st(1)
		fistp	dword ptr [ebp-0Ch]
		fistp	dword ptr [ebp-8]
		fistp	dword ptr [ebp-4]

		mov	eax, [ebp-0Ch]
		cmp	eax, A3D_MIX_PCM16_MAX
		jle	short s1_low
		mov	ebx, A3D_MIX_PCM16_MAX_HIGH_WORD
		jmp	short s1_done
s1_low:
		cmp	eax, A3D_MIX_PCM16_MIN_EXTENDED
		jge	short s1_in
		mov	ebx, A3D_MIX_PCM16_MIN_HIGH_WORD
		jmp	short s1_done
s1_in:
		mov	ebx, eax
		shl	ebx, 10h
s1_done:
		mov	eax, [ebp-10h]
		cmp	eax, A3D_MIX_PCM16_MAX
		jle	short s0_low
		mov	bx, A3D_MIX_PCM16_MAX
		jmp	short s0_done
s0_low:
		cmp	eax, A3D_MIX_PCM16_MIN_EXTENDED
		jge	short s0_in
		mov	bx, A3D_MIX_PCM16_MIN_WORD
		jmp	short s0_done
s0_in:
		mov	bx, ax
s0_done:
		mov	eax, [ebp-4]
		mov	[edi+edx*2], ebx

		cmp	eax, A3D_MIX_PCM16_MAX
		jle	short s3_low
		mov	ebx, A3D_MIX_PCM16_MAX_HIGH_WORD
		jmp	short s3_done
s3_low:
		cmp	eax, A3D_MIX_PCM16_MIN_EXTENDED
		jge	short s3_in
		mov	ebx, A3D_MIX_PCM16_MIN_HIGH_WORD
		jmp	short s3_done
s3_in:
		mov	ebx, eax
		shl	ebx, 10h
s3_done:
		mov	eax, [ebp-8]
		cmp	eax, A3D_MIX_PCM16_MAX
		jle	short s2_low
		mov	bx, A3D_MIX_PCM16_MAX
		jmp	short s2_done
s2_low:
		cmp	eax, A3D_MIX_PCM16_MIN_EXTENDED
		jge	short s2_in
		mov	bx, A3D_MIX_PCM16_MIN_WORD
		jmp	short s2_done
s2_in:
		mov	bx, ax
s2_done:
		mov	[edi+edx*2+4], ebx

		add	edx, 4
		dec	ecx
		jnz	quad_

done_:
		pop	edx
		pop	ecx
		pop	ebx
		pop	edi
		pop	esi
		leave
		ret
	}
}
