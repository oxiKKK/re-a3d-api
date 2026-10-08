/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * outqueue.h
 *
 * Declares A3DQUEUE and the retained queue helpers for buffered DAL
 * playback. The record holds buffer interfaces, write and end marks,
 * capacity and a low-water threshold used to decide when to refill.
 *
 * The queue has its own source-format flag encoding, distinct from
 * softmix.h. outqueue.cpp implements queue measurements, sample
 * conversion and filling; DalBufferInfo in dalinfo.h describes the
 * resource manager's separate queue state.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _OUTQUEUE_H
#define _OUTQUEUE_H

#include "A3dPrivate.h"
#include "a2dbuffer.h"

/* Queue has writable audio storage. */

#define A3DQUEUE_FLAG_HASBUFFER 0x00000001

/* End-of-audio mark has not been recorded. */

#define A3DQUEUE_NO_MARK ((DWORD) -1)

/* Maximum bytes filled per pass. */

#define A3DQUEUE_FILL_MAX 0x4000

/* Voice format bits; incompatible with the softmix.h mixer encoding. */

#define A3DQUEUE_FORMAT_LOOPING 0x00000002
#define A3DQUEUE_FORMAT_STEREO  0x00000004
#define A3DQUEUE_FORMAT_SIXTEEN 0x00000008

/* =============================================================
// Class: A3DQUEUE
//
// Description: DAL output-queue layout with unknown 677 offsets and ownership.
//
// Size: Declared prefix 0x34; total reference size unknown.
// =============================================================*/

struct A3DQUEUE
{
	/* 0x00 */ DWORD              dwReserved00;
	/* A3DQUEUE_FLAG_HASBUFFER. */
	/* 0x04 */ DWORD              dwFlags;
	/* Ring-buffer bytes. */
	/* 0x08 */ DWORD              cbBuffer;
	/* 0x0C */ BYTE               abReserved0C[0x14 - 0x0C];
	/* Refill below this queued-byte count. */
	/* 0x14 */ DWORD              cbLowWater;
	/* DAL voice interface. */
	/* 0x18 */ IA3dDalBuffer     *pDalBuffer;
	/* DirectSound audio buffer. */
	/* 0x1C */ IDirectSoundBuffer *pBuffer;
	/* 0x20 */ BYTE               abReserved20[0x2C - 0x20];
	/* Next write offset in bytes. */
	/* 0x2C */ DWORD              dwWrite;
	/* End-of-audio offset or A3DQUEUE_NO_MARK. */
	/* 0x30 */ DWORD              dwMark;
};

HRESULT A3dQueueBehindNoMark(A3DQUEUE *pQueue, DWORD *pcb);
BOOL    A3dQueueHasRoom(A3DQUEUE *pQueue, DWORD *pcb);
HRESULT A3dQueueReset(A3DQUEUE *pQueue);
HRESULT A3dQueueBehind(A3DQUEUE *pQueue, DWORD *pcb);
HRESULT A3dQueueConvert(A3DQUEUE *pQueue, void *pvOut, DWORD cbOut,
                        BYTE *pbWave, DWORD cbWave, DWORD *pdwCursor,
                        DWORD dwFormat);
HRESULT A3dQueueFill(A3DQUEUE *pQueue, DWORD dwUnused, BYTE *pbWave,
                     DWORD cbWave, DWORD *pdwCursor, DWORD dwFormat);

void A3dBlockTo16Mono(short *pOut, const void *pcIn, int cSamples,
                      DWORD dwFormat);

#endif /* _OUTQUEUE_H */
