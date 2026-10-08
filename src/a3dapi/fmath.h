/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * fmath.h
 *
 * Declares fmath::Init, the startup operation for the fast-square-root
 * lookup table. CA3dRoot calls it before source and geometry calculations
 * use the approximation.
 *
 * The table initialization is implemented in fmath.cpp. This header is
 * the small initialization interface, rather than a general collection of
 * math operations.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _FMATH_H
#define _FMATH_H

#include <windows.h>

namespace fmath
{
	HRESULT Init(void);
}

#endif /* _FMATH_H */
