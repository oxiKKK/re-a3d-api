/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dRoomBuilder.cpp
 *
 * Implements the construction and validation of rooms from placed walls.
 * It retains wall instances, a material binding and a name, and builds
 * the distinct wall-normal planes needed to check the room shell.
 *
 * Validation examines shell counts, wall crossings and boundary coverage
 * before the builder is used to create a CA3dRoom. Load, save,
 * serialization and duplication operations remain unsupported. Shared
 * builder COM methods are implemented in Corners.cpp.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "A3dRoomBuilder.h"
#include "A3dFrame.h"
#include "A3dWall.h"
#include "A3dWallBuilder.h"
#include "LinkList.h"

#include <math.h>


#define A3D_ROOM_MIN_SHELL_WALLS 4
#define A3D_ROOM_MIN_UNIQUE_NORMALS 3
#define A3D_ROOM_NORMAL_MATCH_TOLERANCE 0.000001

/* =============================================================
// CA3dRoomBuilder()
// (RE) rtl:0x1002fac0; dbg:0x1007bd20
//
// Initialize an empty room builder and its default name. Material and name
// subobjects may be secondary bases; their original declaration is unresolved.
// Unidentified base constructor: dbg:0x1007bff0, calling dbg:0x1000DA00.
// =============================================================*/

CA3dRoomBuilder::CA3dRoomBuilder(int nType)
{
	m_nType         = nType;
	m_cRef          = 0;
	m_fValidated    = 0;

	m_named.SetName("RoomBuilder", ++g_cRoomBuilders);
}

/* =============================================================
// CA3dRoomBuilder::~CA3dRoomBuilder() scalar deleting destructor
// (RE) rtl:0x1002fbc0; dbg:0x1007bfa0
// =============================================================*/

/* =============================================================
// FAILED() compiler-generated helper
// (RE) dbg:0x1007c570
// =============================================================*/

/* =============================================================
// ~CA3dRoomBuilder()
// (RE) rtl:0x1002fbf0; dbg:0x1007c020
//
// Release stored walls and free stored normal planes.
// =============================================================*/

CA3dRoomBuilder::~CA3dRoomBuilder(void)
{
	ClearNormals();
	ReleaseWalls();
}

/* =============================================================
// Unknown_0x0C()
// (RE) rtl:0x10009810; dbg:0x1007be60
//
// Reject this operation; the purpose of interface slot 3 is unknown.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dRoomBuilder::Unknown_0x0C(void *pv)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// Unknown_0x10()
// (RE) rtl:0x10009810; dbg:0x1007be80
//
// Reject this operation; the purpose of interface slot 4 is unknown.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dRoomBuilder::Unknown_0x10(void *pv)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// SetMaterial()
// (RE) rtl:0x1002f970; dbg:0x1007cf00
//
// Bind the supplied material through the material link.
//
// Returns: The link SetMaterial result.
// =============================================================*/

STDMETHODIMP
CA3dRoomBuilder::SetMaterial(void *pMaterial)
{
	return (m_link.SetMaterial(pMaterial));
}

/* =============================================================
// GetMaterial()
// (RE) rtl:0x10030520; dbg:0x1007cf30
//
// Copy the bound material through the material link.
//
// Returns: The link GetMaterial result.
// =============================================================*/

STDMETHODIMP
CA3dRoomBuilder::GetMaterial(void **ppMaterial)
{
	return (m_link.GetMaterial(ppMaterial));
}

/* =============================================================
// AddWall()
// (RE) rtl:0x1002fe90; dbg:0x1007c270
//
// Build and retain a wall at the supplied or default placement. Allocation
// failure leaves the wall count unchanged.
//
// Returns: The resulting wall count; E_INVALIDARG for a null builder;
//          A3DERROR_INVALID_WALL or
//          A3DERROR_DIR_AND_UP_VECTORS_NOT_PERPENDICULAR on validation failure.
// =============================================================*/

STDMETHODIMP_(int)
CA3dRoomBuilder::AddWall(void *pBuilder, DWORD dwFlags, LPA3DVAL pvPosition,
						 LPA3DVAL pvFront, LPA3DVAL pvUp)
{
const A3DVAL   *pvAt;
const A3DVAL   *pvDir;
const A3DVAL   *pvTop;
CA3dWall       *pWall;

	if (!pBuilder)
		return (E_INVALIDARG);

	if (FAILED(((IA3dWallBuilder *) pBuilder)->Validate()))
		return (A3DERROR_INVALID_WALL);

	pvAt    = pvPosition ? pvPosition : vDefaultPosition;
	pvDir   = pvFront ? pvFront : vDefaultFront;
	pvTop   = pvUp ? pvUp : vDefaultUp;

	if (!A3dFrameIsSquare(pvDir, pvTop))
		return (A3DERROR_DIR_AND_UP_VECTORS_NOT_PERPENDICULAR);

	pWall = new CA3dWall((CA3dWallBuilder *) pBuilder, dwFlags,
						 (LPA3DVAL) pvAt, (LPA3DVAL) pvDir,
						 (LPA3DVAL) pvTop);

	if (pWall)
	{
		((IUnknown *) pWall)->AddRef();

		m_WallList.AddTail(pWall);
	}

	return (m_WallList.GetCount());
}

/* =============================================================
// GetWall()
// (RE) rtl:0x100300a0; dbg:0x1007c590
//
// Return a referenced wall by one-based index. Preserve the unchecked
// output pointer.
//
// Returns:
//   S_OK
//   A3DERROR_INVALID_WALL_INDEX  a missing wall
// =============================================================*/

STDMETHODIMP
CA3dRoomBuilder::GetWall(int nWall, void **ppWall)
{
IUnknown       *pWall;
POSITION        pos;

	pos = nWall > 0 ? m_WallList.FindIndex(nWall) : NULL;

	pWall = pos ? (IUnknown *) m_WallList.GetAt(pos) : NULL;

	if (!pWall)
		return (A3DERROR_INVALID_WALL_INDEX);

	pWall->AddRef();

	*ppWall = pWall;

	return (S_OK);
}

/* =============================================================
// RemoveWall()
// (RE) rtl:0x100300f0; dbg:0x1007c620
//
// Unlink and release a wall by one-based index.
//
// Returns:
//   S_OK
//   A3DERROR_INVALID_WALL_INDEX  a missing wall
// =============================================================*/

STDMETHODIMP
CA3dRoomBuilder::RemoveWall(int nWall)
{
IUnknown       *pWall;
POSITION        pos;

	pos = nWall > 0 ? m_WallList.FindIndex(nWall) : NULL;

	pWall = pos ? (IUnknown *) m_WallList.GetAt(pos) : NULL;

	if (!pWall)
		return (A3DERROR_INVALID_WALL_INDEX);

	m_WallList.RemoveAt(pos);

	pWall->Release();

	return (S_OK);
}

/* =============================================================
// CountShellWalls()
// (RE) dbg:0x1007cdc0
//
// Count shell-flagged walls in the builder.
//
// Returns: The shell-wall count.
// =============================================================*/

int
CA3dRoomBuilder::CountShellWalls(void)
{
CA3dWall       *pWall;
int             cShell;
CA3dPtrList<CA3dWall>::Iterator it;
CA3dPtrList<CA3dWall>::Iterator itEnd;

	cShell = 0;

	itEnd = m_WallList.End();

	for (it = m_WallList.Begin(); it != itEnd; ++it)
	{
		pWall = *it;

		if (A3dWallIsShell(pWall))
			cShell++;
	}

	return (cShell);
}

/* =============================================================
// RebuildNormals()
// (RE) dbg:0x1007cb70
//
// Rebuild one transformed plane per distinct shell-wall normal. Preserve
// the unchecked plane allocation.
//
// Returns: The distinct-normal count.
// =============================================================*/

int
CA3dRoomBuilder::RebuildNormals(void)
{
CA3dWall       *pWall;
A3DVAL          vPlane[4];
A3DVAL         *pvKept;
CA3dPtrList<CA3dWall>::Iterator it;
CA3dPtrList<CA3dWall>::Iterator itEnd;

	ClearNormals();

	itEnd = m_WallList.End();

	for (it = m_WallList.Begin(); it != itEnd; ++it)
	{
		pWall = *it;

		if (!A3dWallIsShell(pWall))
			continue;

		pWall->GetPlane(vPlane);

		if (!IsNewNormal(vPlane))
			continue;

		pvKept = (A3DVAL *) new BYTE[4 * sizeof(A3DVAL)];

		pvKept[0] = vPlane[0];
		pvKept[1] = vPlane[1];
		pvKept[2] = vPlane[2];
		pvKept[3] = vPlane[3];

		m_NormalList.AddTail(pvKept);
	}

	return (m_NormalList.GetCount());
}

/* =============================================================
// ReleaseWalls()
// (RE) dbg:0x1007c6c0
//
// Release all stored walls and empty the list.
// =============================================================*/

void
CA3dRoomBuilder::ReleaseWalls(void)
{
CA3dWall *pWall;
CA3dPtrList<CA3dWall>::Iterator it;
CA3dPtrList<CA3dWall>::Iterator itEnd;

	itEnd = m_WallList.End();

	for (it = m_WallList.Begin(); it != itEnd; ++it)
	{
		pWall = *it;

		((IUnknown *) pWall)->Release();
	}

	m_WallList.RemoveAll();
}

/* =============================================================
// Validate()
// (RE) rtl:0x10030180; dbg:0x1007c830
//
// Check shell-wall counts, distinct normals, crossings and edge coverage.
// Preserve stale validation on count failures and S_OK on crossing failures.
// Unknown effects: snapshot constructor dbg:0x1007a930 and crossing-result
// wrapper dbg:0x1007ce80.
//
// Returns: S_OK, with validity in IsValidated();
//          A3DERROR_ROOM_HAS_NO_SHELL_WALLS,
//          A3DERROR_ROOM_HAS_LESS_THAN_4SHELL_WALLS or
//          A3DERROR_ROOM_HAS_LESS_THAN_3UNIQUE_NORMALS for count failures.
// =============================================================*/

STDMETHODIMP
CA3dRoomBuilder::Validate(void)
{
CA3dPtrList<CA3dWall> listWork;
CA3dWall       *pWall;
CA3dWall       *pOther;
int             cShell;
CA3dPtrList<CA3dWall>::Iterator it;
CA3dPtrList<CA3dWall>::Iterator itEnd;
CA3dPtrList<CA3dWall>::Iterator jt;
CA3dPtrList<CA3dWall>::Iterator jtEnd;

	cShell = CountShellWalls();

	if (cShell <= 0)
		return (A3DERROR_ROOM_HAS_NO_SHELL_WALLS);

	if (cShell < A3D_ROOM_MIN_SHELL_WALLS)
		return (A3DERROR_ROOM_HAS_LESS_THAN_4SHELL_WALLS);

	if (RebuildNormals() < A3D_ROOM_MIN_UNIQUE_NORMALS)
		return (A3DERROR_ROOM_HAS_LESS_THAN_3UNIQUE_NORMALS);

	m_fValidated = 1;

	itEnd = m_WallList.End();

	for (it = m_WallList.Begin(); it != itEnd; ++it)
		listWork.AddTail(*it);

	itEnd = m_WallList.End();

	for (it = m_WallList.Begin(); it != itEnd; ++it)
	{
		pWall = *it;

		if (!A3dWallIsShell(pWall))
			continue;

		pWall->ClearEdgeCrossings();

		jtEnd = listWork.End();

		for (jt = listWork.Begin(); jt != jtEnd; ++jt)
		{
			pOther = *jt;

			if (!A3dWallIsShell(pOther) || pOther == pWall)
				continue;

			if (A3dWallsMeet(pWall, pOther) == A3D_WALLS_CROSS)
			{
				m_fValidated = 0;

				return (S_OK);
			}
		}
	}

	itEnd = m_WallList.End();

	for (it = m_WallList.Begin(); it != itEnd; ++it)
	{
		pWall = *it;

		if (A3dWallIsShell(pWall) && pWall->HasCrossing())
		{
			m_fValidated = 0;
			break;
		}
	}

	return (S_OK);
}

/* =============================================================
// Clear()
// (RE) rtl:0x1002fd80; dbg:0x1007c140
//
// Release walls and normal planes while retaining the validation result.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dRoomBuilder::Clear(void)
{
	ClearNormals();
	ReleaseWalls();

	return (S_OK);
}

/* =============================================================
// Load()
// (RE) rtl:0x10009810
//
// Reject room-builder loading.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dRoomBuilder::Load(void *pv)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// Save()
// (RE) rtl:0x10009810
//
// Reject room-builder saving.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dRoomBuilder::Save(void *pv)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// UnSerialize()
// (RE) rtl:0x10009820
//
// Reject room-builder deserialization.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dRoomBuilder::UnSerialize(void *pv, UINT cb)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// Serialize()
// (RE) rtl:0x10009820
//
// Reject room-builder serialization.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dRoomBuilder::Serialize(void *pv, UINT cb)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// Duplicate()
// (RE) rtl:0x10009810
//
// Reject room-builder duplication.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dRoomBuilder::Duplicate(void *pv)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// SetName()
// (RE) rtl:0x10030710; dbg:0x1007bf40
//
// Copy the supplied fixed-size name buffer.
//
// Returns: The SetNameBuffer result.
// =============================================================*/

STDMETHODIMP
CA3dRoomBuilder::SetName(LPCVOID pvName)
{
	return (m_named.SetNameBuffer(pvName));
}

/* =============================================================
// GetName()
// (RE) rtl:0x10030730; dbg:0x1007bf70
//
// Copy the stored fixed-size name buffer.
//
// Returns: The GetNameBuffer result.
// =============================================================*/

STDMETHODIMP
CA3dRoomBuilder::GetName(LPVOID pvName, int nSize)
{
	return (m_named.GetNameBuffer(pvName, nSize));
}

/* =============================================================
// ClearNormals()
// (RE) dbg:0x1007c770
//
// Free owned normal planes and empty their list.
// =============================================================*/

void
CA3dRoomBuilder::ClearNormals(void)
{
void *pvPlane;
CA3dPtrList<A3DVAL>::Iterator it;
CA3dPtrList<A3DVAL>::Iterator itEnd;

	itEnd = m_NormalList.End();

	for (it = m_NormalList.Begin(); it != itEnd; ++it)
	{
		pvPlane = *it;

		delete[] (BYTE *) pvPlane;
	}

	m_NormalList.RemoveAll();
}

/* =============================================================
// IsNewNormal()
// (RE) dbg:0x1007ccc0
//
// Test for a distinct normal direction. Opposite directions and dot
// magnitudes above one match existing normals.
//
// Returns: 1 for a new direction; 0 for a match.
// =============================================================*/

int
CA3dRoomBuilder::IsNewNormal(const A3DVAL *pvPlane)
{
const A3DVAL *pvKept;
CA3dPtrList<A3DVAL>::Iterator it;
CA3dPtrList<A3DVAL>::Iterator itEnd;

	itEnd = m_NormalList.End();

	for (it = m_NormalList.Begin(); it != itEnd; ++it)
	{
		pvKept = *it;

		if (1.0 - fabs(pvKept[2] * pvPlane[2] + pvKept[1] * pvPlane[1] +
					   pvKept[0] * pvPlane[0]) <
			A3D_ROOM_NORMAL_MATCH_TOLERANCE)
			return (0);
	}

	return (1);
}

typedef int A3dRoomBuilderSizeCheck[(sizeof(CA3dRoomBuilder) == 0x134) ? 1 : -1];
