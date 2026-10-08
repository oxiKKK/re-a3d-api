/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * outqueue.cpp
 *
 * Implements the retained output-queue helpers for measuring queued
 * audio, converting source samples and filling a looping DAL buffer.
 * Queue cursors and end marks distinguish playable data from padding and
 * determine when another fill is needed.
 *
 * The conversion path produces signed 16-bit mono output from supported
 * source formats. This file also supplies the legacy node-pool clearing
 * helper. Resource-manager device-buffer queue operations
 * are implemented separately in dalinfo.cpp.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "outqueue.h"
#include "LinkList.h"
#include "hrtfmgr.h"
#include "Plex.h"

#include <stdio.h>
#include <new>

/* =============================================================
// A3dQueueHasRoom()
//
// Test whether queued bytes are below the low-water threshold.
//
// Returns:
//   TRUE   if space is available
//   FALSE  on measurement failure or when the threshold is met
// =============================================================*/

BOOL
A3dQueueHasRoom(A3DQUEUE *pQueue, DWORD *pcb)
{
	return (SUCCEEDED(A3dQueueBehindNoMark(pQueue, pcb)) &&
		*pcb < pQueue->cbLowWater);
}

/* =============================================================
// A3dQueueBehindNoMark()
//
// Measure bytes between the play and write cursors.
//
// Returns: S_OK, or a failed GetCurrentPosition result.
// =============================================================*/

HRESULT
A3dQueueBehindNoMark(A3DQUEUE *pQueue, DWORD *pcb)
{
DWORD	dwPlay = 0;
DWORD	dwWrite;
DWORD	cbBuffer;
HRESULT	hr;

	hr = pQueue->pBuffer->GetCurrentPosition(&dwPlay, &dwWrite);

	if (FAILED(hr))
		return (hr);

	cbBuffer = pQueue->cbBuffer;

	if (dwPlay == cbBuffer)
	{
		*pcb = 0;

		return (S_OK);
	}

	if (dwPlay <= pQueue->dwWrite)
		*pcb = pQueue->dwWrite - dwPlay;
	else
		*pcb = cbBuffer + pQueue->dwWrite - dwPlay;

	return (S_OK);
}

/* =============================================================
// A3dQueueBehind()
//
// Measure bytes remaining before the end mark, accounting for looping.
//
// Returns: S_OK, or a failed GetCurrentPosition/GetStatus result; the no-mark
//          measurement error is ignored.
// =============================================================*/

HRESULT
A3dQueueBehind(A3DQUEUE *pQueue, DWORD *pcb)
{
DWORD	dwPlay = 0;
DWORD	dwWrite;
DWORD	dwStatus;
HRESULT	hr;

	if (pQueue->dwMark == A3DQUEUE_NO_MARK)
	{
		A3dQueueBehindNoMark(pQueue, pcb);

		return (S_OK);
	}

	hr = pQueue->pBuffer->GetCurrentPosition(&dwPlay, &dwWrite);

	if (FAILED(hr))
		return (hr);

	if (dwPlay <= pQueue->dwMark)
	{
		*pcb = pQueue->dwMark - dwPlay;

		return (S_OK);
	}

	hr = pQueue->pBuffer->GetStatus(&dwStatus);

	if (FAILED(hr))
		return (hr);

	if (dwStatus & DSBSTATUS_LOOPING)
		*pcb = pQueue->dwMark + pQueue->cbBuffer - dwPlay;
	else
		*pcb = 0;

	return (S_OK);
}


/* (RE) dbg:0x10147C70. HRTF coefficient bank. */
static const short g_asHrtfCoefficients[150] =
{
	 11882,  11823,  10877,  10763,  11566,  11238,   9454,   8992,
	  8457,   8495,   8645,   8761,   8367,   7796,   7194,   7049,
	  6786,   6724,   8053,   7515,   8951,   8962,   7903,   7000,
	  9136,   8310,   8166,   8292,   8115,   7694,  10901,   9484,
	  8795,   8686,   8962,   9065,  15623,  13909,  14258,  15065,
	 14655,  13924,  22476,  23215,  24734,  24398,  24846,  21578,
	 25345,  28732,  31294,  30470,  31127,  26823,  24134,  26822,
	 29465,  30489,  30578,  27415,  18701,  19797,  19806,  19060,
	 17962,  19807,  14604,  14464,  13491,  12929,  13487,  13813,
	 14896,  14495,  32767,  13339,  11702,  11382,  11350,  12364,
	 12319,  16361,  14963,  15099,  14942,  15995,  17489,  20129,
	 20066,  21278,  22197,  24470,  26115,  24353,  26514,  29444,
	 30918,  32767,  32112,  24379,  27571,  28263,  27300,  27818,
	 26070,  20528,  22712,  22446,  21809,  20538,  18543,  13936,
	 13949,  13274,  12744,  12107,  12000,  10314,   9646,   8630,
	  8307,   8294,   8420,   8709,   8405,   8049,   8080,   7835,
	  7431,   8127,   7573,   8868,   9083,   7877,   7010,   8690,
	  7815,   7318,   7240,   7209,   6969,  10268,   9183,   8890,
	  9103,  10016,  10051,  14934,  15383,  32767,
};

/* (RE) dbg:0x10147D9C. Interaural delay bank. */
static const short g_asInterauralDelays[150] =
{
	   256,    256,    512,    512,    512,    256,   2304,   3072,
	  3584,   3584,   3584,   3072,   3584,   5120,   5888,   6144,
	  5632,   4864,   4096,   5888,   7680,   7936,   6912,   5632,
	  3328,   4608,   5376,   5376,   5120,   4096,   2048,   2560,
	  2816,   2816,   2560,   2048,    256,    256,      0,      0,
	     0,      0,      0,      0,      0,      0,      0,      0,
	     0,      0,      0,      0,      0,      0,      0,      0,
	     0,      0,      0,      0,      0,      0,      0,      0,
	     0,      0,      0,      0,      0,      0,      0,      0,
	     0,      0,      0,      0,      0,      0,      0,      0,
	     0,      0,      0,      0,      0,      0,      0,      0,
	     0,      0,      0,      0,      0,      0,      0,      0,
	     0,      0,      0,      0,      0,      0,      0,      0,
	     0,      0,      0,      0,      0,      0,      0,      0,
	     0,      0,    256,    256,    256,   1280,   1792,   2816,
	  3072,   2816,   2560,   2816,   4096,   5120,   5376,   5376,
	  4608,   3584,   5120,   7424,   7936,   6912,   5632,   3328,
	  4608,   5632,   5632,   4864,   4352,   1536,   2304,   2304,
	  2560,   2304,   2048,      0,      0,      0,
};

/* (RE) dbg:0x10147EC8. Built-in 22050-Hz coefficient entry. */
static A3DCOEFFENTRY g_coeffEntry22050 =
{
	22050,
	1,
	1,
	0,
	(BYTE *) g_asHrtfCoefficients,
	(BYTE *) g_asInterauralDelays,
	NULL,
	0,
	12,
	10,
	2,
	7,
};

A3DCOEFFENTRY *g_apCoeffTable[A3D_COEFF_TABLE_COUNT] = { &g_coeffEntry22050 };

/* =============================================================
// A3dBlockTo16Mono()
//
// Convert 8/16-bit mono/stereo samples to signed 16-bit mono.
// =============================================================*/

void
A3dBlockTo16Mono(short *pOut, const void *pcIn, int cSamples, DWORD dwFormat)
{
const BYTE     *pb;
const short    *ps;
int             i;

	if (!(dwFormat & A3DQUEUE_FORMAT_SIXTEEN) &&
	    !(dwFormat & A3DQUEUE_FORMAT_STEREO))
	{

		pb = (const BYTE *) pcIn;

		for (i = 0; i < cSamples; i++)
			pOut[i] = (short) (((pb[i] ^ 0x80) << 8) + pb[i]);

		return;
	}

	if ((dwFormat & A3DQUEUE_FORMAT_SIXTEEN) &&
	    !(dwFormat & A3DQUEUE_FORMAT_STEREO))
	{

		CopyMemory(pOut, pcIn, 2 * cSamples);

		return;
	}

	if (!(dwFormat & A3DQUEUE_FORMAT_SIXTEEN))
	{

		pb = (const BYTE *) pcIn;

		for (i = 0; i < cSamples; i++)
		{
		short	l, r;

			l = (short) (((pb[2 * i]     ^ 0x80) << 8) + pb[2 * i]);
			r = (short) (((pb[2 * i + 1] ^ 0x80) << 8) + pb[2 * i + 1]);

			pOut[i] = (short) ((l >> 1) + (r >> 1));
		}

		return;
	}

	ps = (const short *) pcIn;

	for (i = 0; i < cSamples; i++)
		pOut[i] = (short) ((ps[2 * i] >> 1) + (ps[2 * i + 1] >> 1));
}

/* =============================================================
// A3dQueueReset()
//
// Silence a buffered queue and reset its marks. Original defects: ignore Lock
// failure and pass the first pointer twice to Unlock.
//
// Returns: The Unlock result for a buffered queue, otherwise S_OK.
// =============================================================*/

HRESULT
A3dQueueReset(A3DQUEUE *pQueue)
{
void   *pvAudio1;
void   *pvAudio2;
DWORD   cbAudio1;
DWORD   cbAudio2;
HRESULT	hr = S_OK;

	if (pQueue->dwFlags & A3DQUEUE_FLAG_HASBUFFER)
	{
		pQueue->pBuffer->Lock(0, pQueue->cbBuffer, &pvAudio1, &cbAudio1,
				      &pvAudio2, &cbAudio2,
				      DSBLOCK_ENTIREBUFFER);

		memset(pvAudio1, 0, pQueue->cbBuffer);

		hr = pQueue->pBuffer->Unlock(pvAudio1, cbAudio1,
					     pvAudio1, cbAudio2);
	}

	pQueue->dwWrite = 0;
	pQueue->dwMark  = A3DQUEUE_NO_MARK;

	return (hr);
}

/* =============================================================
// A3dQueueConvert()
//
// Fill a locked span with converted wave data, wrapping or padding
// the source end and updating the stop mark.
//
// Returns: S_OK; E_INVALIDARG for a null output, wave or cursor; a failed
//          GetStatus result.
// =============================================================*/

HRESULT
A3dQueueConvert(A3DQUEUE *pQueue, void *pvOut, DWORD cbOut, BYTE *pbWave,
		DWORD cbWave, DWORD *pdwCursor, DWORD dwFormat)
{
BYTE           *pbBase;
DWORD           cbDone;
DWORD           cAvail;
DWORD           cWanted;
DWORD           dwStatus;
int             nShift;
HRESULT         hr;

	cbDone = 0;

	if (!pvOut)
		return (E_INVALIDARG);

	if (!pbWave)
		return (E_INVALIDARG);

	if (!pdwCursor)
		return (E_INVALIDARG);

	nShift = (dwFormat & A3DQUEUE_FORMAT_SIXTEEN) ? 1 : 0;

	if (dwFormat & A3DQUEUE_FORMAT_STEREO)
		nShift++;

	pbBase = pbWave;

	if (cbOut)
	{
		for (;;)
		{
			cAvail  = (cbWave - *pdwCursor) >> nShift;
			cWanted = (cbOut - cbDone) >> 1;

			if (cWanted <= cAvail)
			{
				A3dBlockTo16Mono((short *) ((BYTE *) pvOut + cbDone),
						 pbBase + *pdwCursor, cWanted, dwFormat);

				*pdwCursor += cWanted << nShift;

				goto done;
			}

			A3dBlockTo16Mono((short *) ((BYTE *) pvOut + cbDone),
					 pbBase + *pdwCursor, cAvail, dwFormat);

			cbDone += 2 * cAvail;

			if (!(dwFormat & A3DQUEUE_FORMAT_LOOPING))
				break;

			*pdwCursor = 0;

			if (cbDone >= cbOut)
				goto done;

			pbBase = pbWave;
		}

		ZeroMemory((BYTE *) pvOut + cbDone, cbOut - cbDone);

		*pdwCursor = cbWave;

		if (pQueue->dwMark == A3DQUEUE_NO_MARK)
			pQueue->dwMark = cbDone + pQueue->dwWrite;
	}

done:
	if (cbWave == *pdwCursor && !(dwFormat & A3DQUEUE_FORMAT_LOOPING))
	{
		dwStatus = 0;

		hr = pQueue->pBuffer->GetStatus(&dwStatus);
		if (FAILED(hr))
			return (hr);

		if ((dwStatus & DSBSTATUS_PLAYING) && (dwStatus & DSBSTATUS_LOOPING))
			pQueue->pBuffer->Play(0, 0, 0);
	}

	return (S_OK);
}

/* =============================================================
// A3dQueueFill()
//
// Fill or advance the queue by its available capacity, capped per pass.
//
// Returns: S_OK; Lock, conversion and Unlock errors are ignored.
// =============================================================*/

HRESULT
A3dQueueFill(A3DQUEUE *pQueue, DWORD dwUnused, BYTE *pbWave, DWORD cbWave,
	     DWORD *pdwCursor, DWORD dwFormat)
{
void   *pvAudio1;
void   *pvAudio2;
DWORD   cbAudio1;
DWORD   cbAudio2;
DWORD   cbBehind;
DWORD   cb;

	if (!A3dQueueHasRoom(pQueue, &cbBehind))
		return (S_OK);

	cb = pQueue->cbBuffer - cbBehind;

	if (cb > A3DQUEUE_FILL_MAX)
		cb = A3DQUEUE_FILL_MAX;

	if (pQueue->dwFlags & A3DQUEUE_FLAG_HASBUFFER)
	{
		pvAudio1 = NULL;
		pvAudio2 = NULL;

		pQueue->pBuffer->Lock(pQueue->dwWrite, cb, &pvAudio1, &cbAudio1,
				      &pvAudio2, &cbAudio2, 0);

		if (pvAudio1)
			A3dQueueConvert(pQueue, pvAudio1, cbAudio1, pbWave, cbWave,
					pdwCursor, dwFormat);

		if (pvAudio2)
			A3dQueueConvert(pQueue, pvAudio2, cbAudio2, pbWave, cbWave,
					pdwCursor, dwFormat);

		pQueue->pBuffer->Unlock(pvAudio1, cbAudio1, pvAudio2, cbAudio2);
	}
	else if ((dwFormat & A3DQUEUE_FORMAT_LOOPING) ||
		 cb + *pdwCursor <= cbWave)
	{
		*pdwCursor = (cb + *pdwCursor) % cbWave;
	}
	else
	{
		*pdwCursor = cbWave;
	}

	pQueue->dwWrite = (pQueue->dwWrite + cb) % pQueue->cbBuffer;

	return (S_OK);
}

/* =============================================================
// A3dNodePoolClear()
//
// Release the node-pool blocks and clear its lists and count.
// =============================================================*/

void
A3dNodePoolClear(A3DNODEPOOL *pPool)
{
A3DLISTNODE *p;

	for (p = pPool->pHead; p; p = p->pNext)
		;

	pPool->cNodes = 0;
	pPool->pFree  = NULL;
	pPool->pTail  = NULL;
	pPool->pHead  = NULL;

	A3dBlockFree(pPool->pBlocks);

	pPool->pBlocks = NULL;
}
