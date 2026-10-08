/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * a3dprv.h
 *
 * Collects the Windows, multimedia, COM, DirectSound and A3D interface
 * includes shared by the compatibility DLL. It also brings in the private
 * DAL contract used to submit source controls to the main API backend.
 *
 * The concrete compatibility classes and DSP records are declared in
 * their own headers. This file supplies their common interface types
 * without depending on the a3dapi.dll implementation classes.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _A3D_A3DPRV_H
#define _A3D_A3DPRV_H

#include <windows.h>
#include <mmsystem.h>
#include <objbase.h>
#include <dsound.h>

#include "ia3dapi.h"

#include "../ia3ddal.h"

#endif /* _A3D_A3DPRV_H */
