/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * a3dguid.cpp
 *
 * Defines the COM class and interface identifiers used by a3d.dll's
 * compatibility objects, including the published A3D and private DAL
 * interfaces. This translation unit provides their storage for linking.
 *
 * The values come from the included interface headers. <initguid.h> must
 * precede headers containing DEFINE_GUID so they emit definitions here;
 * other implementation files use the corresponding declarations.
 *
 *---------------------------------------------------------------------------
 */

#include <windows.h>
#include <mmsystem.h>
#include <objbase.h>
#include <initguid.h>
#include <dsound.h>

#include "ia3dapi.h"
#include "a3dprv.h"
#include "A3d.h"
#include "a3dclsfc.h"
