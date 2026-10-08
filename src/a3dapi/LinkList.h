/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * linklist.h
 *
 * Implements the linked-list containers shared by geometry, sources,
 * device voices and the resource manager. CList manages doubly linked
 * nodes allocated in CPlex blocks, and the typed wrappers provide
 * object-specific access over that storage.
 *
 * The file also declares the legacy list forms used by geometry helpers.
 * List nodes and allocation blocks belong to the container; pointed-to
 * objects remain the caller's responsibility. Plex.cpp supplies the block
 * allocation and release operations.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _LINKLIST_H
#define _LINKLIST_H

#include "A3dPrivate.h"
#include "Plex.h"

struct __POSITION;
typedef __POSITION *POSITION;

/* Default nodes per CPlex allocation */
#define A3D_LIST_DEFAULT_BLOCK_SIZE	10

/* =============================================================
// Class: CList
//
// Description: Block-allocated list of caller-owned pointers.
//
// Size: 0x18
//
// Whether the original uses one class or a base/derived pair is unknown.
// =============================================================*/

class CList
{
public:

/* =============================================================
// Class: CList::CNode
//
// Description: Links and caller-owned element pointer.
//
// Size: 0x0C
// =============================================================*/

	struct CNode
	{
		/* 0x00 */ CNode	*pNext;
		/* 0x04 */ CNode	*pPrev;
		/* 0x08 */ void		*data;	/* Caller-owned element. */
	};

	/* (RE) dbg:0x1004DF60 */
	CList(int nBlockSize = A3D_LIST_DEFAULT_BLOCK_SIZE)
	{
		ASSERT(nBlockSize > 0);

		m_nCount        = 0;
		m_pNodeHead     = m_pNodeTail = m_pNodeFree = NULL;
		m_pBlocks       = NULL;
		m_nBlockSize    = nBlockSize;
	}

	/* (RE) dbg:0x1004E110 */
	~CList(void)
	{
		RemoveAll();
		ASSERT(m_nCount == 0);
	}

	/* (RE) dbg:0x10057E20; thunk dbg:0x10001ED8; dbg:0x1006A260;
	 *    dbg:0x1006A9A0 */
	int		GetCount(void) const	{ return (m_nCount); }

	BOOL		IsEmpty(void) const	{ return (m_nCount == 0); }

	/* (RE) thunk dbg:0x100028D8 */
	POSITION	GetHeadPosition(void) const
			{ return ((POSITION) m_pNodeHead); }

	/* (RE) dbg:0x1006AB80 */
	POSITION	GetTailPosition(void) const
			{ return ((POSITION) m_pNodeTail); }

	/* (RE) thunk dbg:0x10002A4F */
	void *&GetNext(POSITION &rPosition)
	{
		CNode	*pNode = (CNode *) rPosition;

		ASSERT((pNode != 0 && !IsBadReadPtr(pNode, sizeof(CNode))));

		rPosition = (POSITION) pNode->pNext;
		return (pNode->data);
	}

	/* (RE) dbg:0x1006AC30 */
	void *&GetPrev(POSITION &rPosition)
	{
		CNode	*pNode = (CNode *) rPosition;

		ASSERT((pNode != 0 && !IsBadReadPtr(pNode, sizeof(CNode))));

		rPosition = (POSITION) pNode->pPrev;
		return (pNode->data);
	}

	/* (RE) dbg:0x1006AAD0; dbg:0x10043F90 */
	void RemoveAll(void)
	{
		CNode	*pNode;

		for (pNode = m_pNodeHead; pNode != NULL; pNode = pNode->pNext)
			;

		m_nCount        = 0;
		m_pNodeHead     = m_pNodeTail = m_pNodeFree = NULL;
		m_pBlocks->FreeDataChain();
		m_pBlocks = NULL;
	}

	/* (RE) dbg:0x1006A280; dbg:0x1006AA50; thunk dbg:0x100017C6 */
	POSITION AddTail(void *newElement)
	{
		CNode	*pNewNode = NewNode(m_pNodeTail, NULL);

		pNewNode->data = newElement;

		if (m_pNodeTail != NULL)
			m_pNodeTail->pNext = pNewNode;
		else
			m_pNodeHead = pNewNode;

		m_pNodeTail = pNewNode;

		return ((POSITION) pNewNode);
	}

	/* (RE) dbg:0x1006A3B0 */
	void *&GetAt(POSITION position)
	{
		CNode	*pNode = (CNode *) position;

		ASSERT((pNode != 0 && !IsBadReadPtr(pNode, sizeof(CNode))));

		return (pNode->data);
	}

	/* (RE) dbg:0x100744B0
	 * Whether original callers use the returned position is unresolved. */
	POSITION SetAt(POSITION position, void *newElement)
	{
		CNode	*pNode = (CNode *) position;

		ASSERT((pNode != 0 && !IsBadReadPtr(pNode, sizeof(CNode))));

		pNode->data = newElement;

		return ((POSITION) pNode);
	}

	/* (RE) dbg:0x1006A430 */
	void RemoveAt(POSITION position)
	{
		CNode	*pOldNode = (CNode *) position;

		ASSERT((pOldNode != 0 && !IsBadReadPtr(pOldNode, sizeof(CNode))));

		if (pOldNode == m_pNodeHead)
		{
			m_pNodeHead = pOldNode->pNext;
		}
		else
		{
			ASSERT((pOldNode->pPrev != 0 &&
			       !IsBadReadPtr(pOldNode->pPrev, sizeof(CNode))));
			pOldNode->pPrev->pNext = pOldNode->pNext;
		}

		if (pOldNode == m_pNodeTail)
		{
			m_pNodeTail = pOldNode->pPrev;
		}
		else
		{
			ASSERT((pOldNode->pNext != 0 &&
			       !IsBadReadPtr(pOldNode->pNext, sizeof(CNode))));
			pOldNode->pNext->pPrev = pOldNode->pPrev;
		}

		FreeNode(pOldNode);
	}

	/* (RE) dbg:0x10043E30 */
	POSITION Find(void *searchValue, POSITION startAfter = NULL)
	{
		CNode	*pNode = (CNode *) startAfter;

		if (pNode == NULL)
		{
			pNode = m_pNodeHead;
		}
		else
		{
			ASSERT((pNode != 0 &&
			       !IsBadReadPtr(pNode, sizeof(CNode))));
			pNode = pNode->pNext;
		}

		for ( ; pNode != NULL; pNode = pNode->pNext)
			if (pNode->data == searchValue)
				return ((POSITION) pNode);

		return (NULL);
	}

	/* (RE) dbg:0x10057E40 */
	void *RemoveHead(void)
	{
		if (m_pNodeHead == NULL)
			return (NULL);

		CNode	*pOldNode = m_pNodeHead;
		void	*pData = pOldNode->data;

		m_pNodeHead = pOldNode->pNext;

		if (m_pNodeHead != NULL)
			m_pNodeHead->pPrev = NULL;
		else
			m_pNodeTail = NULL;

		FreeNode(pOldNode);

		return (pData);
	}

protected:

	/* (RE) dbg:0x10053930 */
	CNode *NewNode(CNode *pPrev, CNode *pNext)
	{
		if (m_pNodeFree == NULL)
		{
			CPlex  *pNewBlock;
			CNode  *pNode;
			int     i;

			pNewBlock = CPlex::Create(m_pBlocks, m_nBlockSize,
						  sizeof(CNode));

			pNode = (CNode *) pNewBlock->data();
			pNode += m_nBlockSize - 1;

			for (i = m_nBlockSize - 1; i >= 0; i--, pNode--)
			{
				pNode->pNext    = m_pNodeFree;
				m_pNodeFree     = pNode;
			}
		}

		ASSERT(m_pNodeFree != 0);

		CNode	*pNode = m_pNodeFree;

		m_pNodeFree     = m_pNodeFree->pNext;
		pNode->pPrev    = pPrev;
		pNode->pNext    = pNext;
		m_nCount++;
		ASSERT(m_nCount > 0);

		return (pNode);
	}

	/* (RE) dbg:0x1004C5C0 */
	void FreeNode(CNode *pNode)
	{
		pNode->pNext    = m_pNodeFree;
		m_pNodeFree     = pNode;
		m_nCount--;
		ASSERT(m_nCount >= 0);

		if (m_nCount == 0)
			RemoveAll();
	}

public:
	/* 0x00 */ CNode	*m_pNodeHead;
	/* 0x04 */ CNode	*m_pNodeTail;
	/* 0x08 */ int		m_nCount;
	/* 0x0C */ CNode	*m_pNodeFree;	/* Available nodes in allocated blocks. */
	/* 0x10 */ CPlex	*m_pBlocks;	/* Owned allocation chain. */
	/* 0x14 */ int		m_nBlockSize;	/* Nodes per allocation. */
};

/* =============================================================
// Class: CA3dPtrList
//
// Description: Circular pointer list with a heap sentinel.
//
// Size: 0x0C
// =============================================================*/

template<class T>
class CA3dPtrList
{
public:
/* =============================================================
// Class: CA3dPtrList::CNode
//
// Description: Links and element pointer.
//
// Size: 0x0C
// =============================================================*/

	struct CNode
	{
		/* 0x00 */ CNode	*pNext;	/* Accessor dbg:0x10078A60. */
		/* 0x04 */ CNode	*pPrev;	/* Accessor dbg:0x10085D50. */
		/* 0x08 */ void		*pData;	/* Accessor dbg:0x10078A80. */
	};

	/* (RE) dbg:0x100858D0; dbg:0x1007A8E0; CA3dRoom dbg:0x1007AC70
	 * Preserve the original uninitialized flag. */
	CA3dPtrList(void)
	{
		m_pHead         = NewNode(NULL, NULL);
		m_nCount        = 0;
	}

	/* (RE) dbg:0x10085920; A3DVAL dbg:0x100773A0; CA3dWall dbg:0x1007A9C0;
	 *    CA3dRoom dbg:0x1007ACC0 */
	~CA3dPtrList(void)
	{
		m_pHead->pPrev->pNext = m_pHead->pNext;
		m_pHead->pNext->pPrev = m_pHead->pPrev;
		operator delete(m_pHead);
		m_pHead         = NULL;
		m_nCount        = 0;
	}

	/* (RE) A3DVAL dbg:0x1007D010 */
	int		GetCount(void) const	{ return (m_nCount); }

	BOOL		IsEmpty(void) const	{ return (m_nCount == 0); }

	POSITION	GetHeadPosition(void) const
			{ return ((POSITION) m_pHead->pNext); }

	BOOL		IsEnd(POSITION position) const
			{ return ((CNode *) position == m_pHead); }

	T *GetNext(POSITION &rPosition) const
	{
		CNode	*pNode = (CNode *) rPosition;

		rPosition = (POSITION) pNode->pNext;

		return ((T *) pNode->pData);
	}

	T *GetAt(POSITION position) const
	{
		return ((T *) ((CNode *) position)->pData);
	}

	/* (RE) dbg:0x10085A40 */
	POSITION AddTail(T *newElement)
	{
		CNode	*pNewNode = NewNode(m_pHead->pPrev, m_pHead);

		pNewNode->pData         = (void *) newElement;
		m_pHead->pPrev->pNext   = pNewNode;
		m_pHead->pPrev          = pNewNode;
		m_nCount++;

		return ((POSITION) pNewNode);
	}

	/* (RE) dbg:0x10085C60 */
	void RemoveAt(POSITION position)
	{
		CNode	*pOldNode = (CNode *) position;

		pOldNode->pPrev->pNext = pOldNode->pNext;
		pOldNode->pNext->pPrev = pOldNode->pPrev;
		operator delete(pOldNode);
		m_nCount--;
	}

	POSITION FindIndex(int nIndex) const
	{
		CNode	*pNode = m_pHead->pNext;
		int	i;

		for (i = 1; i < nIndex && pNode != m_pHead; i++)
			pNode = pNode->pNext;

		if (pNode == m_pHead)
			return (NULL);

		return ((POSITION) pNode);
	}

	/* (RE) A3DVAL dbg:0x1007D030 */
	void RemoveAll(void)
	{
		CNode	*pNode = m_pHead->pNext;

		while (pNode != m_pHead)
		{
			CNode	*pNext = pNode->pNext;

			operator delete(pNode);
			pNode = pNext;
		}

		m_pHead->pNext  = m_pHead;
		m_pHead->pPrev  = m_pHead;
		m_nCount        = 0;
	}

	/* (RE) dbg:0x10085290 */
	void DeleteAll(void)
	{
		CNode	*pNode = m_pHead->pNext;

		while (pNode != m_pHead)
		{
			operator delete(pNode->pData);
			pNode = pNode->pNext;
		}

		RemoveAll();
	}

/* =============================================================
// Class: CA3dPtrList::Iterator
//
// Description: Node position for list traversal.
//
// Size: 0x04
//
// MSVC6 returns Begin/End through a hidden pointer because the constructor
// makes the iterator non-POD.
// (RE) Identity conversions: dbg:0x1007B100; dbg:0x1007B140.
// Assignment helpers: dbg:0x1007B160; dbg:0x1007B610.
// Element-slot accessor: dbg:0x1007B120; equality helper: dbg:0x1007B190.
// =============================================================*/

	class Iterator
	{
	public:
		/* (RE) CA3dWall dbg:0x1007AB90
		 * Leave the iterator position uninitialized. */
		Iterator(void) { }

		/* (RE) CA3dWall dbg:0x1007AC30 */
		bool operator!=(const Iterator &rhs) const
			{ return (m_pNode != rhs.m_pNode); }

		/* (RE) CA3dWall dbg:0x1007ABC0 */
		T *&operator*(void) const
			{ return (*(T **) &m_pNode->pData); }

		/* (RE) CA3dWall dbg:0x1007ABF0 */
		Iterator &operator++(void)
			{ m_pNode = m_pNode->pNext; return (*this); }

		/* 0x00 */ CNode	*m_pNode;	/* Current node or end sentinel. */
	};

	/* (RE) CA3dWall dbg:0x1007AA40 Unverified CA3dRoom instantiation:
	 *    dbg:0x1007B1C0 */
	Iterator Begin(void) const
	{
	Iterator	it;

		it.m_pNode = m_pHead->pNext;

		return (it);
	}

	/* (RE) CA3dWall dbg:0x1007AA90 Unverified CA3dRoom instantiation:
	 *    dbg:0x1007B210 */
	Iterator End(void) const
	{
	Iterator	it;

		it.m_pNode = m_pHead;

		return (it);
	}

protected:

	/* (RE) dbg:0x10085BA0; CA3dWall dbg:0x1007B040; CA3dRoom
	 *    dbg:0x1007B3B0 */
	CNode *NewNode(CNode *pPrev, CNode *pNext)
	{
		CNode	*pNode = (CNode *) operator new(sizeof(CNode));

		pNode->pPrev = pPrev != NULL ? pPrev : pNode;
		pNode->pNext = pNext != NULL ? pNext : pNode;
		pNode->pData = NULL;

		return (pNode);
	}

public:
	/* 0x00 */ BYTE		m_bOwnsElements;	/* Uninitialized; ownership-guard behavior unconfirmed. */
	/* 0x04 */ CNode	*m_pHead;	/* Owned sentinel, excluded from the count. */
	/* 0x08 */ int		m_nCount;
};

/* Legacy helpers have no established 3.3.677 contract. */

typedef struct A3DLISTNODE
{
	struct A3DLISTNODE	*pPrev;
	struct A3DLISTNODE	*pNext;
	void			*pObject;
} A3DLISTNODE;

A3DLISTNODE *A3dListAppend(A3DLISTNODE *pNode, void *pObject);

typedef struct
{
	A3DLISTNODE    *pHead;
	A3DLISTNODE    *pTail;
	A3DLISTNODE    *pCurr;
	int             cNodes;
} A3DLIST;

A3DLISTNODE *A3dListAdd(A3DLIST *pList, void *pObject);

void	    *A3dListNext(A3DLIST *pList);

A3DLISTNODE *A3dListNodeInit(A3DLISTNODE *pNode, A3DLISTNODE *pPrev,
			     void *pObject);

void A3dListClear(A3DLIST *pList);

#endif /* _LINKLIST_H */
