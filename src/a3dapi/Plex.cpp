/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * Plex.cpp
 *
 * Implements block allocation for linked-list nodes. CPlex prepends
 * allocations to a chain and frees that chain when a list releases its
 * storage, allowing list nodes to be reused without a separate allocation
 * for each insertion.
 *
 * LinkList.h uses this allocator for its containers. The file also
 * also declares legacy block-allocation helpers with no established 677
 * correspondence; their declarations are in Plex.h.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "Plex.h"
#include "dalinfo.h"

#include <stdio.h>

/* Legacy block-chain link bytes */
#define A3D_LEGACY_BLOCK_LINK_BYTES 4

/* =============================================================
// CPlex::Create()
// (RE) rtl:0x100232B0; dbg:0x1005B590
//
// Allocate and prepend a node block. Preserve the unchecked allocation.
//
// Returns: The new block, including its chain link.
// =============================================================*/

CPlex * PASCAL
CPlex::Create(CPlex *&pHead, UINT nMax, UINT cbElement)
{
CPlex	*p;

	ASSERT(nMax > 0 && cbElement > 0);

	p = (CPlex *) new BYTE[nMax * cbElement + sizeof(CPlex)];

	p->pNext        = pHead;
	pHead           = p;

	return (p);
}

/* =============================================================
// CPlex::FreeDataChain()
// (RE) dbg:0x1005B650
//
// Free this block and the rest of its chain. Calls with a null this
// are intentional and must return without dereferencing it.
// =============================================================*/

void
CPlex::FreeDataChain(void)
{
CPlex	*p;
CPlex	*pNext;

	for (p = this; p != NULL; p = pNext)
	{
		pNext = p->pNext;
		delete (BYTE *) p;
	}
}

/* =============================================================
// A3dBlockAlloc()
//
// Allocate and prepend a legacy block, preserving unchecked allocation.
//
// Returns: The new block, including its chain link.
// =============================================================*/

void *
A3dBlockAlloc(void **ppBlocks, int nMax, int cbElement)
{
void **ppBlock;

	ppBlock = (void **) new BYTE[nMax * cbElement + A3D_LEGACY_BLOCK_LINK_BYTES];

	*ppBlock = *ppBlocks;
	*ppBlocks = ppBlock;

	return (ppBlock);
}

/* =============================================================
// A3dBlockFree()
//
// Free a legacy allocation chain.
// =============================================================*/

void
A3dBlockFree(void *pBlocks)
{
void **ppBlock;
void **ppNext;

	ppBlock = (void **) pBlocks;

	while (ppBlock)
	{
		ppNext = (void **) *ppBlock;

		delete [] (BYTE *) ppBlock;

		ppBlock = ppNext;
	}
}
