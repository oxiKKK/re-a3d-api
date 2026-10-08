/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * NamedObject.cpp
 *
 * Implements fixed-size names shared by materials, retained lists and
 * scene builders. CA3dNamed creates default names from a class label and
 * sequence number and copies name buffers through the common accessors.
 *
 * Names occupy 256 bytes and need not contain a terminator. The copy
 * operations retain their original fixed-buffer behavior; callers use
 * this base rather than introducing separate naming storage in each
 * geometry object.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "NamedObject.h"

#include <stdio.h>

/* =============================================================
// CA3dNamed()
// (RE) rtl:0x10019c60; dbg:0x1003d6f0
//
// Clear the fixed-size name buffer.
// =============================================================*/

CA3dNamed::CA3dNamed(void)
{
	ZeroMemory(m_szName, sizeof(m_szName));
}

/* =============================================================
// ~CA3dNamed()
// (RE) rtl:0x10019cb0; dbg:0x1003d790
//
// Destroy the embedded name storage.
// =============================================================*/

CA3dNamed::~CA3dNamed(void)
{
}

/* =============================================================
// SetName()
// (RE) rtl:0x10019cc0; dbg:0x1003d7c0
//
// Format a default name from a class label and sequence number. The original
// return type is unresolved: the observed zero return is ignored by callers.
// =============================================================*/

void
CA3dNamed::SetName(const char *pszWhat, int nWhich)
{
	/* (RE) Format string: dbg:0x1012d4fc. */
	sprintf(m_szName, "%s-%d", pszWhat, nWhich);
}

/* =============================================================
// SetNameBuffer()
// (RE) rtl:0x10019cf0; dbg:0x1003d800
//
// Copy 256 readable bytes; the input need not contain a terminator.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null input
// =============================================================*/

HRESULT
CA3dNamed::SetNameBuffer(const void *pvName)
{
	if (!pvName)
		return (E_INVALIDARG);

	CopyMemory(m_szName, pvName, A3D_NAME_LENGTH);

	return (S_OK);
}

/* =============================================================
// GetNameBuffer()
// (RE) rtl:0x10019d20; dbg:0x1003d850
//
// Copy the full stored name. Preserve the original ignored cbName, which
// allows overwriting a smaller destination.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null output
// =============================================================*/

HRESULT
CA3dNamed::GetNameBuffer(void *pvName, INT cbName)
{
	if (!pvName)
		return (E_INVALIDARG);

	CopyMemory(pvName, m_szName, A3D_NAME_LENGTH);

	return (S_OK);
}
