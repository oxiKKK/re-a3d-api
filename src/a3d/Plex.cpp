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
 * Implements the block-backed pointer lists used to track compatibility
 * primary and secondary buffers. Nodes are taken from a free list,
 * replenished by block allocations and recycled as objects are removed.
 *
 * The operations initialize, search, prepend, unlink and clear list
 * storage. They free nodes and blocks without destroying the tracked
 * objects; CA3d and its buffer wrappers manage object lifetime
 * separately.
 *
 *---------------------------------------------------------------------------
 */

#include "a3dprv.h"
#include "Plex.h"

/* =============================================================
// A3dPlexInit()
//
// Initialize an empty block list.
// =============================================================*/

void
A3dPlexInit(A3DPLEX *pPlex, LONG cPerBlock)
{
	pPlex->cNodes    = 0;
	pPlex->pFree     = NULL;
	pPlex->pHead     = NULL;
	pPlex->pBlocks   = NULL;
	pPlex->cPerBlock = cPerBlock;
}

/* =============================================================
// A3dPlexFree()
//
// Free node blocks without destroying their objects.
// =============================================================*/

void
A3dPlexFree(A3DPLEX *pPlex)
{
void **ppBlock;
void **ppNext;

	pPlex->cNodes = 0;
	pPlex->pFree  = NULL;
	pPlex->pHead  = NULL;

	ppBlock = (void **) pPlex->pBlocks;

	while (ppBlock)
	{
		ppNext = (void **) ppBlock[1];

		delete ppBlock;

		ppBlock = ppNext;
	}

	pPlex->pBlocks = NULL;
}

/* =============================================================
// A3dPlexReleaseBlocks()
//
// Reset an empty list and release its blocks.
// =============================================================*/

static void
A3dPlexReleaseBlocks(A3DPLEX *pPlex)
{
A3DPLEXBLOCK *pBlock;
A3DPLEXBLOCK *pNext;

	pPlex->cNodes = 0;
	pPlex->pFree  = NULL;
	pPlex->pHead  = NULL;

	pBlock = pPlex->pBlocks;

	while (pBlock)
	{
		pNext = pBlock->pNext;

		delete pBlock;

		pBlock = pNext;
	}

	pPlex->pBlocks = NULL;
}

/* =============================================================
// A3dPlexAddNode()
// (RE) a3d.dll rtl:0x10004D50
//
// Take a free node, allocating a block when needed. Preserve the original
// allocation-failure fault before the null check.
//
// Returns: The initialized node.
// =============================================================*/

static A3DPLEXNODE *
A3dPlexAddNode(A3DPLEX *pPlex, void *pvNext)
{
A3DPLEXBLOCK *pBlock;
A3DPLEXNODE  *pNode;
LONG         cb;
LONG         i;

	if (!pPlex->pFree)
	{
		cb = 8 * pPlex->cPerBlock + 12;

		pBlock = (A3DPLEXBLOCK *) new BYTE[cb];

		pBlock->cb      = cb;
		pBlock->pNext   = pPlex->pBlocks;
		pBlock->pvNodes = (void *) (pBlock + 1);

		if (!pBlock)
			return (NULL);

		pPlex->pBlocks = pBlock;

		pNode = (A3DPLEXNODE *) pBlock->pvNodes + (pPlex->cPerBlock - 1);

		for (i = pPlex->cPerBlock; i > 0; i--, pNode--)
		{
			pNode->pNext = (A3DPLEXNODE *) pPlex->pFree;
			pPlex->pFree = pNode;
		}
	}

	pNode = (A3DPLEXNODE *) pPlex->pFree;

	pPlex->pFree   = pNode->pNext;
	pNode->pNext   = (A3DPLEXNODE *) pvNext;
	pNode->pObject = NULL;

	pPlex->cNodes++;

	return (pNode);
}

/* =============================================================
// A3dPlexPushFront()
// (RE) a3d.dll rtl:0x10004DD0
//
// Add an object at the list head.
// =============================================================*/

void
A3dPlexPushFront(A3DPLEX *pPlex, void *pvObject)
{
A3DPLEXNODE *pNode;

	pNode = A3dPlexAddNode(pPlex, pPlex->pHead);

	if (!pNode)
		return;

	pNode->pObject = pvObject;
	pNode->pNext   = (A3DPLEXNODE *) pPlex->pHead;

	pPlex->pHead = pNode;
}

/* =============================================================
// A3dPlexPopFront()
//
// Remove the head node, releasing all blocks when the list becomes empty.
//
// Returns: The removed object; NULL for an empty list.
// =============================================================*/

void *
A3dPlexPopFront(A3DPLEX *pPlex)
{
A3DPLEXNODE *pNode;
void       *pvObject;

	pNode = (A3DPLEXNODE *) pPlex->pHead;

	if (!pNode)
		return (NULL);

	pvObject = pNode->pObject;

	pPlex->pHead = pNode->pNext;
	pNode->pNext = (A3DPLEXNODE *) pPlex->pFree;
	pPlex->pFree = pNode;

	if (--pPlex->cNodes == 0)
		A3dPlexReleaseBlocks(pPlex);

	return (pvObject);
}

/* =============================================================
// A3dPlexRemove()
// (RE) a3d.dll rtl:0x10004E70
//
// Unlink a node and recycle it. Preserve the original list corruption
// when a nonnull node does not belong to the list.
// =============================================================*/

void
A3dPlexRemove(A3DPLEX *pPlex, A3DPLEXNODE *pNode)
{
A3DPLEXNODE *pWalk;

	if (!pNode)
		return;

	if (pNode == (A3DPLEXNODE *) pPlex->pHead)
	{
		pPlex->pHead = pNode->pNext;
	}
	else
	{
		pWalk = (A3DPLEXNODE *) pPlex->pHead;

		while (pWalk)
		{
			if (pWalk->pNext == pNode)
			{
				pWalk->pNext = pNode->pNext;
				break;
			}

			pWalk = pWalk->pNext;
		}
	}

	pNode->pNext = (A3DPLEXNODE *) pPlex->pFree;
	pPlex->pFree = pNode;

	if (--pPlex->cNodes == 0)
		A3dPlexReleaseBlocks(pPlex);
}

/* =============================================================
// A3dPlexFind()
//
// Search from the supplied start node, or the head when ppStart is null.
//
// Returns: The matching object node; NULL when absent.
// =============================================================*/

A3DPLEXNODE *
A3dPlexFind(A3DPLEX *pPlex, void *pvObject, A3DPLEXNODE **ppStart)
{
A3DPLEXNODE *pNode;

	if (ppStart)
		pNode = *ppStart;
	else
		pNode = (A3DPLEXNODE *) pPlex->pHead;

	while (pNode)
	{
		if (pNode->pObject == pvObject)
			return (pNode);

		pNode = pNode->pNext;
	}

	return (NULL);
}
