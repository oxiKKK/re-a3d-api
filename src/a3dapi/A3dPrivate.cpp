/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dPrivate.cpp
 *
 * Contains the retained no-op methods for CA3dRoot's legacy private
 * interface. These bodies accept their calls without implementing
 * additional engine behavior.
 *
 * IA3dPrv2 has no established 3.3.677 correspondence, and CA3dRoot does
 * not expose it through QueryInterface. a3d33.h declares the interface;
 * A3d3.h declares the root layout and methods.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "A3d3.h"

/* =============================================================
// Prv2_03()
//
// Ignore the arguments. This legacy method has no root vtable slot in
// 677; its argument semantics are unknown.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dRoot::Prv2_03(DWORD dw)
{
	return (S_OK);
}

/* =============================================================
// Prv2_04()
//
// Ignore the arguments. This legacy method has no root vtable slot in
// 677; its argument semantics are unknown.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dRoot::Prv2_04(DWORD dw1, DWORD dw2)
{
	return (S_OK);
}

/* =============================================================
// Prv2_05()
//
// Ignore the arguments. This legacy method has no root vtable slot in
// 677; its argument semantics are unknown.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dRoot::Prv2_05(DWORD dw)
{
	return (S_OK);
}
