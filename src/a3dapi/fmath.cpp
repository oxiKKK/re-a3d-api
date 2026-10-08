/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * fmath.cpp
 *
 * Initializes the lookup table used by the engine's fast square-root
 * routine. The initializer builds the table once and is called during
 * root construction before tracing uses the approximation.
 *
 * The table declaration and square-root consumer are part of the shared
 * math support. This file supplies their required startup step; fmath.h
 * exposes that step to the root without exposing the table-building
 * implementation.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "fmath.h"
#include "A3dSource.h"

/* (RE) Initialization flag: dbg:0x10152A7C; rtl:0x10067ABC. */

static BOOL g_fInitialized;

/* =============================================================
// fmath::Init()
// (RE) rtl:0x10019210; dbg:0x1003C700
// Debug thunk: dbg:0x10001302
//
// Initialize the fast-square-root table on the first call.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
fmath::Init(void)
{
	if (!g_fInitialized)
	{
		A3dInitSqrtTable();

		g_fInitialized = TRUE;
	}

	return (S_OK);
}
