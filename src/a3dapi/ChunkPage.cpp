/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * ChunkPage.cpp
 *
 * Implements CA3dPool, fixed-size storage for geometry-stream elements.
 * Pools allocate records, copy used or allocated content, and maintain
 * the traversal cursor used to read submitted geometry.
 *
 * CA3dRoot and CA3dList use these pools for transient and retained
 * geometry. The pool handles storage and iteration; A3dGeom.cpp
 * interprets the primitive and material records held in that storage.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "ChunkPage.h"
#include "A3dGeom.h"

/* =============================================================
// CA3dPool()
// (RE) rtl:0x10019040; dbg:0x1003c290
//
// Allocate and clear a fixed-capacity element array.
// =============================================================*/

CA3dPool::CA3dPool(int cElements, DWORD dwListOwner)
{
	m_pElements     = NULL;
	m_cUsed         = 0;
	m_nChunks       = 0;
	m_cBlocks       = 0;

	m_pElements = new BYTE[A3D_POOL_ELEMENT_SIZE * cElements];

	if (m_pElements)
	{
		m_nChunks = cElements;
		memset(m_pElements, 0, A3D_POOL_ELEMENT_SIZE * cElements);
	}

	m_dwListOwner = dwListOwner;
}

/* =============================================================
// ~CA3dPool()
// (RE) dbg:0x1003c3b0
//
// Free the element array when the allocated capacity is positive.
// =============================================================*/

CA3dPool::~CA3dPool(void)
{
	if (m_nChunks > 0)
		delete [] m_pElements;
}

/* =============================================================
// CopyFrom()
// (RE) rtl:0x10019140; dbg:0x1003c4c0
//
// Copy elements from another pool. Preserve the unchecked source extent.
// The 677 cdecl helper takes pDest explicitly.
//
// Returns: The copied element count; zero if the destination lacks capacity.
// =============================================================*/

int
CA3dPool::CopyFrom(int iDestStart, const CA3dPool *pSource, int iSrcStart,
		   int cChunks)
{
	ASSERT((iDestStart >= 0) && (iDestStart < m_nChunks));
	ASSERT((iSrcStart >= 0) && (iSrcStart < pSource->m_nChunks));

	if (cChunks > m_nChunks - iDestStart)
		return (0);

	CopyMemory(m_pElements + A3D_POOL_ELEMENT_SIZE * iDestStart,
		   pSource->m_pElements + A3D_POOL_ELEMENT_SIZE * iSrcStart,
		   A3D_POOL_ELEMENT_SIZE * cChunks);

	return (cChunks);
}

/* =============================================================
// CopyAllocatedFrom()
// (RE) rtl:0x100190F0; dbg:0x1003c400
//
// Copy the full allocated source pool to the destination start index.
//
// The 677 cdecl helper takes pDest explicitly.
//
// Returns: The source capacity; zero if it exceeds the available destination
//          space.
// =============================================================*/

int
CA3dPool::CopyAllocatedFrom(int iStart, const CA3dPool *pSource)
{
	if (pSource->m_nChunks > m_nChunks - iStart)
		return (0);

	ASSERT((iStart >= 0) && (iStart < m_nChunks));

	CopyMemory(m_pElements + A3D_POOL_ELEMENT_SIZE * iStart,
		   pSource->m_pElements,
		   A3D_POOL_ELEMENT_SIZE * pSource->m_nChunks);

	return (pSource->m_nChunks);
}

/* =============================================================
// NextBlock()
// (RE) dbg:0x1003c5d0
//
// Advance to the next shape record and write its mode.
//
// Returns: The primitive pointer; NULL when populated elements are exhausted.
// =============================================================*/

A3DPRIMITIVE *
CA3dPool::NextBlock(DWORD *pdwMode)
{
A3DELEMENT	*pElement;

	if (m_cUsed >= (int) m_cBlocks)
		return (NULL);

	*pdwMode = 0;

	while (!IsShape(((A3DELEMENT *) m_pElements)[m_cUsed].dwMode))
	{
		if (++m_cUsed >= (int) m_cBlocks)
			return (NULL);
	}

	pElement = (A3DELEMENT *) m_pElements + m_cUsed;

	*pdwMode = pElement->dwMode;

	m_cUsed++;

	return (&pElement->prim);
}

/* =============================================================
// NextElement()
// (RE) rtl:0x1000CBD0; dbg:0x10012320; thunk dbg:0x100025F9
//
// Advance through populated elements.
//
// Returns: The next element; NULL at the populated-element limit.
// =============================================================*/

A3DELEMENT *
CA3dPool::NextElement(void)
{
	if (m_cUsed >= (int) m_cBlocks)
		return (NULL);

	return ((A3DELEMENT *) m_pElements + m_cUsed++);
}

/* =============================================================
// GetUsed()
// (RE) dbg:0x1000C580
//
// Read the allocation or traversal cursor.
//
// Returns: The current element index.
// =============================================================*/

int
CA3dPool::GetUsed(void) const
{
	return (m_cUsed);
}

/* =============================================================
// Rewind()
// (RE) dbg:0x100121B0; thunk dbg:0x1000223E
//
// Reset the read cursor to the first element.
// =============================================================*/

void
CA3dPool::Rewind(void)
{
	m_cUsed = 0;
}

/* =============================================================
// IsShape()
// (RE) dbg:0x1003c6b0
//
// Test the low mode nibble for a drawable shape.
//
// Returns:
//   TRUE   lines, triangles or quads
//   FALSE  otherwise
// =============================================================*/

BOOL
CA3dPool::IsShape(DWORD dwMode)
{
DWORD	dwShape;

	dwShape = dwMode & A3D_POOL_MODE_MASK;

	return (dwShape >= A3D_LINES && dwShape <= A3D_QUADS);
}
