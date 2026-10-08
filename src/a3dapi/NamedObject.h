/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * NamedObject.h
 *
 * Declares CA3dNamed, the common name-storage base for geometry and
 * material objects. It contains a 256-byte buffer and methods for
 * default-name formatting and whole-buffer reads and writes.
 *
 * The buffer is not necessarily null-terminated. NamedObject.cpp
 * implements the accessors used by builders and other named objects,
 * whose public interfaces forward to this shared storage.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _NAMEDOBJECT_H
#define _NAMEDOBJECT_H

#include "A3dPrivate.h"

/* Fixed name-buffer capacity in bytes. */

#define A3D_NAME_LENGTH 256

/* =============================================================
// Class: CA3dNamed
//
// Description: Fixed-size name storage shared by geometry and materials.
//
// Size: 0x104
//
// (RE) Constructor: rtl:0x10019c60; dbg:0x1003d6f0
// =============================================================*/

/* (RE) Vtable: rtl:0x100534f4; dbg:0x1012d4f8.
 * Slot 0, scalar deleting destructor: rtl:0x10019c80; dbg:0x1003d740. */

class CA3dNamed
{
public:
	CA3dNamed(void);
	virtual ~CA3dNamed(void);

	void SetName(const char *pszWhat, int nWhich);

	HRESULT SetNameBuffer(const void *pvName);
	HRESULT GetNameBuffer(void *pvName, INT cbName);

protected:
	/* 0x00 */ /* vptr, from the virtual destructor */
	/* 0x04 */ char m_szName[A3D_NAME_LENGTH]; /* Not necessarily terminated. */
};

typedef int CA3dNamedSizeCheck[(sizeof(CA3dNamed) == 0x104) ? 1 : -1];

#endif /* _NAMEDOBJECT_H */
