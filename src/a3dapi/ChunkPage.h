/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * ChunkPage.h
 *
 * Declares CA3dPool, the fixed-size geometry-element pool shared by the
 * root and retained lists. It tracks the allocated storage, used elements
 * and read cursor, with operations for copying, rewinding and advancing
 * through records.
 *
 * ChunkPage.cpp implements storage management. Record contents are
 * defined in A3dGeom.h and Corners.h and interpreted by the geometry
 * tracing code.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _CHUNKPAGE_H
#define _CHUNKPAGE_H

#include "A3dPrivate.h"

/* Element size in bytes. */

#define A3D_POOL_ELEMENT_SIZE	108

/* (RE) CA3dRoot::CreateGeom default: dbg:0x1000EE44; rtl:0x1000421F. */

#define A3D_POOL_ELEMENTS	2048

/* Shape mode excludes flags such as A3D_SUBFACE. */

#define A3D_POOL_MODE_MASK	0x0000000FU

struct _A3DPRIMITIVE;
struct _A3DELEMENT;

/* =============================================================
// Class: CA3dPool
//
// Description: Fixed-capacity geometry element pool with a shared read/write
// cursor.
//
// Size: 0x18
//
// (RE) Constructor: rtl:0x10019040; dbg:0x1003c290
// =============================================================*/

/* (RE) Vtable: dbg:0x1012d3bc; rtl:0x100534f0.
 * Slot 0, scalar deleting destructor: dbg:0x1003c360; rtl:0x100190b0. */
class CA3dPool
{
public:
	CA3dPool(int cElements, DWORD dwListOwner);
	virtual ~CA3dPool(void);

	struct _A3DPRIMITIVE *NextBlock(DWORD *pdwMode);

	struct _A3DELEMENT	*NextElement(void);
	int			GetUsed(void) const;
	void			Rewind(void);

	/* (RE) dbg:0x1000A5C0; thunk dbg:0x10001E74 */
	void			Clear(void)
					{ m_cBlocks = 0; m_cUsed = 0; }

	/* (RE) dbg:0x10014490; thunk dbg:0x10003102 */
	void			SetCursor(int iElement)
					{ m_cUsed = iElement; }

	/* (RE) dbg:0x100033A5 (thunk) */
	struct _A3DELEMENT	*NextFreeElement(void)
				{
					if (m_cUsed >= m_nChunks)
						return (NULL);

					m_cBlocks++;

					return ((struct _A3DELEMENT *)
						(m_pElements +
						 A3D_POOL_ELEMENT_SIZE *
						 m_cUsed++));
				}

	int		CopyFrom(int iDestStart, const CA3dPool *pSource,
				 int iSrcStart, int cChunks);
	int		CopyAllocatedFrom(int iStart, const CA3dPool *pSource);

	/* 0x00 is the vtable pointer, from the virtual destructor. */

	/* 0x04 */ BYTE		*m_pElements; /* Owned element array. */

	/* 0x08 */ int		m_cUsed;	/* allocation or read cursor */
	/* 0x0C */ int		m_nChunks;	/* Element capacity. */
	/* 0x10 */ DWORD	m_cBlocks;	/* populated elements */


	/* 0x14 */ DWORD	m_dwListOwner;	/* CA3dList owner for traversal/culling; 0 for geometry. */

private:
	BOOL		IsShape(DWORD dwMode);
};

typedef int CA3dPoolSizeCheck[(sizeof(CA3dPool) == 0x18) ? 1 : -1];

#endif /* _CHUNKPAGE_H */
