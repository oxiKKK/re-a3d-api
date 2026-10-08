/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * sceneman.cpp
 *
 * Implements the root's private entry points for the room-based scene
 * model. It enables geometry creation, constructs scene and room, wall
 * and opening builder objects, and gathers rooms from a supplied scene.
 *
 * The methods belong to CA3dRoot's IA3dPrv1 interface, declared in
 * a3d33.h and A3d3.h. Object construction, validation and compiled
 * geometry are implemented in the corresponding scene and builder files.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "A3d3.h"
#include "A3dScene.h"
#include "A3dRoomBuilder.h"
#include "A3dWallBuilder.h"
#include "A3dOpeningBuilder.h"

/* =============================================================
// CreatePrv1()
// (RE) rtl:0x100366D0; dbg:0x10088610
//
// Initialize geometry and enable the private creation interface.
//
// Returns:
//   S_OK
//   E_FAIL  if geometry initialization fails
// =============================================================*/

HRESULT
CA3dRoot::CreatePrv1(void)
{
	if (!m_fGeomReady)
	{
		if (FAILED(CreateGeom()))
			return (E_FAIL);
	}

	m_fPrv1Ready = 1;

	return (S_OK);
}

/* =============================================================
// DestroyPrv1()
// (RE) rtl:0x10036700; dbg:0x10088670
//
// Disable the private creation interface.
// =============================================================*/

void
CA3dRoot::DestroyPrv1(void)
{
	m_fPrv1Ready = 0;
}

/* =============================================================
// NewScene()
// (RE) rtl:0x10036710; dbg:0x100886A0
//
// Create a scene bound to this root.
//
// Returns:
//   S_OK
//   E_INVALIDARG                        a null output pointer
//   A3DERROR_2D_GEOMETRY_UNIMPLEMENTED  a 2D scene
//   A3DERROR_MEMORY_ALLOCATION          on allocation failure
// =============================================================*/

STDMETHODIMP
CA3dRoot::NewScene(DWORD dwType, CA3dScene **ppScene)
{
CA3dScene *pScene;

	if (!ppScene)
		return (E_INVALIDARG);

	if (dwType == A3D_SCENE_2D)
		return (A3DERROR_2D_GEOMETRY_UNIMPLEMENTED);

	pScene = new CA3dScene((int) dwType, this);

	if (!pScene)
	{
		*ppScene = NULL;

		return (A3DERROR_MEMORY_ALLOCATION);
	}

	pScene->AddRef();

	*ppScene = pScene;

	return (S_OK);
}

/* =============================================================
// NewRoomBuilder()
// (RE) rtl:0x100367E0; dbg:0x100887C0
//
// Create a room builder, clearing the output before validating the scene type.
//
// Returns:
//   S_OK
//   E_INVALIDARG                        a null output pointer
//   A3DERROR_2D_GEOMETRY_UNIMPLEMENTED  a 2D scene
//   A3DERROR_MEMORY_ALLOCATION          on allocation failure
// =============================================================*/

STDMETHODIMP
CA3dRoot::NewRoomBuilder(DWORD dwType, CA3dRoomBuilder **ppBuilder)
{
CA3dRoomBuilder *pBuilder;

	if (!ppBuilder)
		return (E_INVALIDARG);

	*ppBuilder = NULL;

	if (dwType == A3D_SCENE_2D)
		return (A3DERROR_2D_GEOMETRY_UNIMPLEMENTED);

	pBuilder = new CA3dRoomBuilder((int) dwType);

	if (!pBuilder)
	{
		*ppBuilder = NULL;

		return (A3DERROR_MEMORY_ALLOCATION);
	}

	pBuilder->AddRef();

	*ppBuilder = pBuilder;

	return (S_OK);
}

/* =============================================================
// NewWallBuilder()
// (RE) rtl:0x100368B0; dbg:0x100888E0
//
// Create a wall builder.
//
// Returns:
//   S_OK
//   E_INVALIDARG                        a null output pointer
//   A3DERROR_2D_GEOMETRY_UNIMPLEMENTED  a 2D scene
//   A3DERROR_MEMORY_ALLOCATION          on allocation failure
// =============================================================*/

STDMETHODIMP
CA3dRoot::NewWallBuilder(DWORD dwType, CA3dWallBuilder **ppBuilder)
{
CA3dWallBuilder *pBuilder;

	if (!ppBuilder)
		return (E_INVALIDARG);

	if (dwType == A3D_SCENE_2D)
		return (A3DERROR_2D_GEOMETRY_UNIMPLEMENTED);

	pBuilder = new CA3dWallBuilder((int) dwType);

	if (!pBuilder)
	{
		*ppBuilder = NULL;

		return (A3DERROR_MEMORY_ALLOCATION);
	}

	pBuilder->AddRef();

	*ppBuilder = pBuilder;

	return (S_OK);
}

/* =============================================================
// NewOpeningBuilder()
// (RE) rtl:0x10036970; dbg:0x100889F0
//
// Create an opening builder.
//
// Returns:
//   S_OK
//   E_INVALIDARG                        a null output pointer
//   A3DERROR_2D_GEOMETRY_UNIMPLEMENTED  a 2D scene
//   A3DERROR_MEMORY_ALLOCATION          on allocation failure
// =============================================================*/

STDMETHODIMP
CA3dRoot::NewOpeningBuilder(DWORD dwType, CA3dOpeningBuilder **ppBuilder)
{
CA3dOpeningBuilder *pBuilder;

	if (!ppBuilder)
		return (E_INVALIDARG);

	if (dwType == A3D_SCENE_2D)
		return (A3DERROR_2D_GEOMETRY_UNIMPLEMENTED);

	pBuilder = new CA3dOpeningBuilder((int) dwType);

	if (!pBuilder)
	{
		*ppBuilder = NULL;

		return (A3DERROR_MEMORY_ALLOCATION);
	}

	pBuilder->AddRef();

	*ppBuilder = pBuilder;

	return (S_OK);
}

/* =============================================================
// GatherRooms()
// (RE) rtl:0x10036A30; dbg:0x10088B00
//
// Gather rooms for the CA3dScene pointer carried in dwScene.
//
// Returns:
//   The A3dGatherRooms result
//   E_INVALIDARG               a null scene
// =============================================================*/

STDMETHODIMP
CA3dRoot::GatherRooms(DWORD dwScene)
{
	if (!dwScene)
		return (E_INVALIDARG);

	return (A3dGatherRooms((CA3dScene *) dwScene));
}
