/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * splash.h
 *
 * Declares the shared splash::splash_screen entry point used during
 * device startup. The caller supplies a window handle for the launch and
 * restoration behavior.
 *
 * splash.cpp reads the registry settings and starts A3DSplsh.exe when
 * requested. This header exposes the launcher without adding
 * splash-program implementation or state to device classes.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _SPLASH_H
#define _SPLASH_H

#include "A3dPrivate.h"

/* =============================================================
// Class: splash
//
// Description: Registry-configured A3D splash-program launcher.
// =============================================================*/

class splash
{
public:
	static void splash_screen(HWND hWnd);
};

#endif /* _SPLASH_H */
