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
 * Declares CPlex, the allocation-block header used by the linked lists in
 * LinkList.h. Each block links to the preceding allocation and exposes
 * the node storage immediately after its header.
 *
 * The file also declares the retained legacy node-pool structure and its
 * allocation, append and clearing helpers. Those legacy declarations
 * remain qualified separately from CPlex. The containers own their nodes
 * and blocks, while the stored object pointers remain caller-owned.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _PLEX_H
#define _PLEX_H

#include "A3dPrivate.h"

/* =============================================================
// Class: CPlex
//
// Description: Node allocation block with a link followed by element storage.
//
// Size: 0x04
// =============================================================*/

struct CPlex
{
	/* 0x00 */ CPlex	*pNext;

	void *data(void)	{ return (this + 1); }

	static CPlex * PASCAL Create(CPlex *&pHead, UINT nMax, UINT cbElement);

	void FreeDataChain(void);
};

/* Legacy node pool with no established 3.3.677 correspondence. */

struct A3DLISTNODE;

/* List nodes are reused through pFree; pObject remains caller-owned. */

typedef struct _A3DNODEPOOL
{
	A3DLISTNODE    *pHead;
	A3DLISTNODE    *pTail;
	int             cNodes;
	A3DLISTNODE	*pFree;		/* threaded through each node's pNext */
	void		*pBlocks;	/* Owned allocation chain. */
	int		cGrow;		/* Nodes per allocation. */
} A3DNODEPOOL;

/* Frees a block chain created by A3dBlockAlloc(). */

void	A3dBlockFree(void *pBlocks);

/* Clears nodes and blocks, leaves cGrow, and does not release referenced items. */

void	A3dNodePoolClear(A3DNODEPOOL *pPool);

/* Appends an item, growing by cGrow nodes when the free chain is empty. */
void	A3dNodePoolAppend(A3DNODEPOOL *pPool, void *pItem);

#endif /* _PLEX_H */
