/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * ac3_liba52.cpp
 *
 * NOT ONE OF AUREAL'S FILES.  Project-added, compiled only when the build
 * option A3D_FIXES is on.
 * liba52 AC-3 decoding adapter. PCM parity with Aureal's decoder remains
 * unverified. See docs/DECODERS.md and third_party/liba52 for the GPL code.
 *
 * Implements the Ac3 audio-decoder entry points used by source loading
 * and streaming. A decoder session owns liba52 state, decodes supplied
 * frames and converts decoded samples into the PCM output expected by
 * the source code.
 *
 * A3dSource.h declares the shared decoder records and entry points.
 * DirectShow fallback is implemented separately in ac3fgraph.cpp; this
 * adapter supplies decoding within the normal source playback path.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"

#if defined(A3D_FIXES)
#include "A3dSource.h"

#include <stdint.h>		/* a52.h uses uint8_t/uint32_t but does not include it */

extern "C" {
#include "a52.h"
}

/* =============================================================
// Ac3ToS16()
//
// Convert a normalized liba52 sample to signed 16-bit PCM.
//
// Returns: The rounded sample, clamped to the signed 16-bit range.
// =============================================================*/

static short
Ac3ToS16(sample_t fSample)
{
	int	nValue;

	nValue = (int) (fSample * 32767.0f + (fSample >= 0.0f ? 0.5f : -0.5f));

	if (nValue > 32767)
		nValue = 32767;
	else if (nValue < -32768)
		nValue = -32768;

	return ((short) nValue);
}

/* =============================================================
// Ac3OpenAudio()
//
// Allocate a liba52 decoder; output mode is selected per frame.
//
// Returns: The decoder handle, or zero on allocation failure.
// =============================================================*/

DWORD
Ac3OpenAudio(DWORD dwMode)
{
	(void) dwMode;

	if (!A3dGetConfig().bEnableAC3Decoder)
		return (0);

	return ((DWORD) a52_init(0));		/* 0: no SIMD acceleration */
}

/* =============================================================
// Ac3CloseAudio()
//
// Release a liba52 decoder handle.
// =============================================================*/

void
Ac3CloseAudio(DWORD hDecoder)
{
	if (hDecoder)
		a52_free((a52_state_t *) hDecoder);
}

/* =============================================================
// Ac3ResetAudio()
//
// Leave decoder state unchanged; liba52 resynchronizes on the next frame.
//
// Returns: Zero.
// =============================================================*/

int
Ac3ResetAudio(DWORD hDecoder, DWORD dwMode)
{
	(void) hDecoder;
	(void) dwMode;

	return (0);
}

/* =============================================================
// Ac3DecodeAudio()
//
// Decode a CRC-checked AC-3 frame to interleaved stereo PCM.
// A block failure leaves partial output and still reports success.
//
// Returns: Zero after frame setup succeeds; 139 for an invalid handle or frame,
//          or failed frame setup (the caller treats 139 as needing more input).
// =============================================================*/

int
Ac3DecodeAudio(DWORD hDecoder, LPA3DAC3CONFIG pConfig)
{
	a52_state_t    *pState;
	uint8_t        *pFrame;
	sample_t       *pSamples;
	sample_t        fLevel;
	sample_t        fBias;
	short          *pOut;
	int             cbFrame;
	int             nFrameLen;
	int             flags;
	int             nSampleRate;
	int             nBitRate;
	int             nBlock;
	int             i;
	int             nCount;

	pState  = (a52_state_t *) hDecoder;
	pFrame  = (uint8_t *) pConfig->pFrameStart;
	cbFrame = (int) ((BYTE *) pConfig->pFrameEnd - (BYTE *) pConfig->pFrameStart);

	if (!pState || !pFrame || cbFrame <= 0)
		return (A3D_AC3_MORE_INPUT);

	nFrameLen = a52_syncinfo(pFrame, &flags, &nSampleRate, &nBitRate);

	if (nFrameLen <= 0 || nFrameLen > cbFrame)
		return (A3D_AC3_MORE_INPUT);

	flags  = A52_STEREO;
	fLevel = 1.0f;
	fBias  = 0.0f;

	if (a52_frame(pState, pFrame, &flags, &fLevel, fBias))
		return (A3D_AC3_MORE_INPUT);

	pOut   = (short *) pConfig->pDecode;
	nCount = 0;

	for (nBlock = 0; nBlock < 6; nBlock++)
	{
		if (a52_block(pState))
			break;

		pSamples = a52_samples(pState);

		for (i = 0; i < 256; i++)
		{
			pOut[nCount++] = Ac3ToS16(pSamples[i]);		/* left */
			pOut[nCount++] = Ac3ToS16(pSamples[256 + i]);	/* right */
		}
	}

	pConfig->dwProduced = (DWORD) (nCount * 2);

	return (0);
}

#endif /* A3D_FIXES */
