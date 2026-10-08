/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * guids.cpp - define the A3D GUIDs once for the a3d_reference_comparison_tests binary.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling.
 *
 * <initguid.h> before the header turns every DEFINE_GUID into a definition;
 * every other translation unit includes ia3dapi.h without it and resolves
 * the GUIDs here.  Same arrangement as src/a3dapi/a3dguid.cpp.
 *
 *---------------------------------------------------------------------------
 */

#include <windows.h>
#include <objbase.h>
#include <initguid.h>

#include "ia3dapi.h"

/* ia3ddal.h includes <dsound.h>, which needs WAVEFORMATEX. */
#include <mmreg.h>
#include "ia3ddal.h"
