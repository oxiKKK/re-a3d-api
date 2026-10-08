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
 * NOT ONE OF AUREAL'S FILES.  The GUID values are Aureal's; this translation
 * unit was added by the reconstruction project.
 *
 * Defines the DirectSound, published A3D and recovered private COM class
 * and interface identifiers used throughout a3dapi.dll. This translation
 * unit supplies their storage so interface lookup and object activation
 * use the same identifiers across the engine.
 *
 * Values are supplied by the included interface headers. Keep
 * <initguid.h> before every header containing DEFINE_GUID declarations
 * so those headers emit definitions here rather than external references.
 *
 *---------------------------------------------------------------------------
 */

#include <windows.h>
#include <mmsystem.h>
#include <objbase.h>
#include <initguid.h>
#include <dsound.h>

/* (RE) IID_IUnknown: dbg:0x10140BD8; IID_IA3dReflection: dbg:0x10140938. */
/* (RE) IID_IA3dReverb: dbg:0x10140928. */
#include "ia3dapi.h"
#include "a3d33.h"
#include "A3dPrivate.h"