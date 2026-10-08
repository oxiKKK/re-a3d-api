/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * Plex.h
 *
 * Defines the compatibility DLL's pointer-list nodes, allocation blocks
 * and A3DPLEX state. A list tracks live nodes, reusable nodes and the
 * block chain used to allocate them in groups.
 *
 * Plex.cpp implements insertion, removal, search and storage release.
 * CA3d uses these lists for buffer tracking, with object lifetime managed
 * by the device and wrappers. This allocator is separate from CPlex in
 * src/a3dapi.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _A3D_PLEX_H
#define _A3D_PLEX_H

#include "a3dprv.h"

/* A3DPLEXNODE: 0x08 bytes. */

typedef struct A3DPLEXNODE
{
	struct A3DPLEXNODE *pNext;
	void               *pObject; /* Tracked object. */
} A3DPLEXNODE;

/* A3DPLEXBLOCK: 0x0C-byte header followed by the nodes. */
typedef struct A3DPLEXBLOCK
{
	LONG                 cb;      /* +0x00 8 * cPerBlock + 12 */
	struct A3DPLEXBLOCK *pNext;   /* 0x04; next allocated block. */
	void                *pvNodes; /* First node after the header. */
} A3DPLEXBLOCK;

typedef struct A3DPLEX
{
	void         *pHead;     /* live list */
	LONG          cNodes;
	void         *pFree;     /* free list */
	A3DPLEXBLOCK *pBlocks;   /* block chain */
	LONG          cPerBlock; /* Nodes per allocation; 32 for CA3d buffers. */
} A3DPLEX;

void         A3dPlexInit(A3DPLEX *pPlex, LONG cPerBlock);
void         A3dPlexFree(A3DPLEX *pPlex);
void         A3dPlexPushFront(A3DPLEX *pPlex, void *pvObject);
void        *A3dPlexPopFront(A3DPLEX *pPlex);
void         A3dPlexRemove(A3DPLEX *pPlex, A3DPLEXNODE *pNode);
A3DPLEXNODE *A3dPlexFind(A3DPLEX *pPlex, void *pvObject,
                         A3DPLEXNODE **ppStart);

#endif /* _A3D_PLEX_H */
