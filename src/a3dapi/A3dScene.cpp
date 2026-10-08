/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dScene.cpp
 *
 * Implements the private scene container for rooms and independent walls.
 * It creates placed objects from validated builders, manages their
 * lifetime and invalidates compiled geometry when the scene changes.
 *
 * Scene building classifies rooms, records wall contacts and coordinates
 * portal construction. Static geometry is compiled into retained lists,
 * while traversal gathers the room geometry needed by the root. The room,
 * wall and opening files implement the individual objects managed here.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "A3dScene.h"
#include "A3d3.h"
#include "A3dFrame.h"
#include "A3dList.h"
#include "A3dRoom.h"
#include "A3dRoomBuilder.h"
#include "A3dWall.h"
#include "A3dWallBuilder.h"
#include "ChunkPage.h"
#include "Corners.h"
#include "LinkList.h"

#include <math.h>
#include <stdlib.h>

/* =============================================================
// SetMaterial()
// (RE) rtl:0x1002f970; dbg:0x1007f400
//
// Bind the scene material.
//
// Returns: The material-link result.
// =============================================================*/

STDMETHODIMP
CA3dScene::SetMaterial(void *pMaterial)
{
	return (m_link.SetMaterial(pMaterial));
}

/* =============================================================
// GetMaterial()
// (RE) rtl:0x10030520; dbg:0x1007f430
//
// Return a new copy of the bound material.
//
// Returns: The material-link result.
// =============================================================*/

STDMETHODIMP
CA3dScene::GetMaterial(void **ppMaterial)
{
	return (m_link.GetMaterial(ppMaterial));
}

/* =============================================================
// Unknown_0x40()
// (RE) rtl:0x10009810; dbg:0x1007d2d0
//
// Reject this unresolved one-argument operation. Its argument role and
// type require a private interface declaration or an identified caller.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dScene::Unknown_0x40(void *pv)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// Unknown_0x44()
// (RE) rtl:0x10009810; dbg:0x1007d2f0
//
// Reject this unresolved one-argument operation. The argument role and
// type are unknown.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dScene::Unknown_0x44(void *pv)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// CA3dScene()
// (RE) rtl:0x10030560; dbg:0x1007d090
//
// Initialize the scene and retain the root object.
// =============================================================*/

CA3dScene::CA3dScene(int nSceneType, CA3dRoot *pApi)
{
	m_nSceneType = nSceneType;

	m_fBuilt                = 0;
	m_pCurrentRoom          = NULL;
	m_pCompiledList         = NULL;
	m_fCurrentRoomForced    = 0;
	m_cRef                  = 0;

	m_pWalkBlock    = NULL;
	m_nWalkVertex   = 0;

	m_pApi = pApi;
	m_pApi->AddRef();

	m_named.SetName("Scene", ++g_cScenes);
}

/* =============================================================
// IsScene()
// (RE) rtl:0x10030700; dbg:0x1007d2b0
//
// Identify the object as a scene.
//
// Returns: 1.
// =============================================================*/

STDMETHODIMP_(int)
CA3dScene::IsScene(void)
{
	return (1);
}

/* =============================================================
// SetName()
// (RE) rtl:0x10030710; dbg:0x1007d3b0
//
// Set the scene name.
//
// Returns: The named-object result.
// =============================================================*/

STDMETHODIMP
CA3dScene::SetName(LPCVOID pvName)
{
	return (m_named.SetNameBuffer(pvName));
}

/* =============================================================
// GetName()
// (RE) rtl:0x10030730; dbg:0x1007d3e0
//
// Read the scene name.
//
// Returns: The named-object result.
// =============================================================*/

STDMETHODIMP
CA3dScene::GetName(LPVOID pvName, int nSize)
{
	return (m_named.GetNameBuffer(pvName, nSize));
}

/* =============================================================
// ~CA3dScene()
// (RE) rtl:0x10030780; dbg:0x1007d490
//
// Release rooms and walls, free all list nodes and release the root.
// =============================================================*/

CA3dScene::~CA3dScene(void)
{
IUnknown *pOwned;

	Clear();

	pOwned = (IA3d3 *) m_pApi;

	pOwned->Release();

	m_MovingList.RemoveAll();
	m_StaticRoomList.RemoveAll();
	m_KeptList.RemoveAll();
	m_GatheredList.RemoveAll();
	m_WallList.RemoveAll();
	m_RoomList.RemoveAll();
}

/* =============================================================
// QueryInterface()
// (RE) rtl:0x10030980; dbg:0x1007d620
//
// Return IUnknown or IA3dScene. Unsupported IIDs leave *ppv unchanged and
// AddRef any nonnull value already there; the reference control flow is
// unresolved.
//
// Returns:
//   S_OK           if *ppv is nonnull
//   E_NOINTERFACE  otherwise
//   E_INVALIDARG   if ppv is null
// =============================================================*/

STDMETHODIMP
CA3dScene::QueryInterface(REFIID riid, void **ppv)
{
	if (!ppv)
		return (E_INVALIDARG);

	if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IA3dScene))
		*ppv = this;

	if (!*ppv)
		return (E_NOINTERFACE);

	((IUnknown *) *ppv)->AddRef();

	return (S_OK);
}

/* =============================================================
// AddRef()
// (RE) rtl:0x100309e0; dbg:0x1007d6c0
//
// Add a reference to the scene.
//
// Returns: The new reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dScene::AddRef(void)
{
	return (++m_cRef);
}

/* =============================================================
// Release()
// (RE) rtl:0x10030a00; dbg:0x1007d700
//
// Release a reference and delete the scene at zero.
//
// Returns: The remaining reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dScene::Release(void)
{
	if (--m_cRef)
		return (m_cRef);

	delete this;

	return (0);
}

/* =============================================================
// AddWall()
// (RE) rtl:0x10030a30; dbg:0x1007d790
//
// Copy a validated wall directly into the scene. Null builders and
// allocation failure are unchecked.
//
// Returns: The new wall count; A3DERROR_DYNAMIC_OBJ_UNSUPPORTED;
//          A3DERROR_INVALID_WALL if validation fails;
//          A3DERROR_DIR_AND_UP_VECTORS_NOT_PERPENDICULAR for invalid axes.
// =============================================================*/

STDMETHODIMP_(int)
CA3dScene::AddWall(void *pDynamic, void *pBuilder, LPA3DVAL pvPosition,
				   LPA3DVAL pvFront, LPA3DVAL pvUp)
{
	static const A3DVAL vAt[4] = {0.0f, 0.0f, 0.0f, 1.0f};
	static const A3DVAL vFront[4] = {0.0f, 0.0f, 1.0f, 0.0f};
	static const A3DVAL vUp[4] = {0.0f, 1.0f, 0.0f, 0.0f};

	CA3dWall *pWall;

	if (pDynamic)
		return (A3DERROR_DYNAMIC_OBJ_UNSUPPORTED);

	if (FAILED(((IA3dWallBuilder *) pBuilder)->Validate()))
		return (A3DERROR_INVALID_WALL);

	if (!pvPosition)
		pvPosition = (LPA3DVAL) vAt;

	if (!pvFront)
		pvFront = (LPA3DVAL) vFront;

	if (!pvUp)
		pvUp = (LPA3DVAL) vUp;

	if (!A3dFrameIsSquare(pvFront, pvUp))
		return (A3DERROR_DIR_AND_UP_VECTORS_NOT_PERPENDICULAR);

	pWall = new CA3dWall((CA3dWallBuilder *) pBuilder, 0, pvPosition, pvFront,
						 pvUp);

	((IUnknown *) pWall)->AddRef();

	m_WallList.AddTail(pWall);

	return (m_WallList.GetCount());
}

/* =============================================================
// RemoveWall()
// (RE) rtl:0x10030c40; dbg:0x1007d9c0
//
// Remove and release a scene wall, then invalidate the build.
//
// Returns:
//   S_OK
//   A3DERROR_INVALID_WALL_INDEX  if the wall is absent
// =============================================================*/

STDMETHODIMP
CA3dScene::RemoveWall(int nWall)
{
IUnknown       *pWall;
POSITION        pos;

	if (nWall <= 0)
		return (A3DERROR_INVALID_WALL_INDEX);

	pos = m_WallList.FindIndex(nWall);

	pWall = pos ? (IUnknown *) m_WallList.GetAt(pos) : NULL;

	if (!pWall)
		return (A3DERROR_INVALID_WALL_INDEX);

	m_WallList.RemoveAt(pos);

	pWall->Release();

	m_fBuilt = 0;

	return (S_OK);
}

/* =============================================================
// Unknown_0x1C()
// (RE) rtl:0x10009810; dbg:0x1007d290
//
// Reject this unresolved one-argument operation. The argument role and
// type are unknown.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dScene::Unknown_0x1C(void *pv)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// GetWall()
// (RE) rtl:0x10030cf0; dbg:0x1007da80
//
// Return a referenced wall from the scene (nRoom == 0) or a numbered room.
//
// Returns:
//   S_OK
//   A3DERROR_INVALID_ROOM_INDEX  or A3DERROR_INVALID_WALL_INDEX for invalid
//                                indices
//   E_INVALIDARG                 if ppWall is null
// =============================================================*/

STDMETHODIMP
CA3dScene::GetWall(int nRoom, int nWall, void **ppWall)
{
CA3dRoom       *pRoom;
void           *pWall;
POSITION        pos;
int             fTakeReference;

	if (nRoom < 0)
		return (A3DERROR_INVALID_ROOM_INDEX);

	if (nWall <= 0)
		return (A3DERROR_INVALID_WALL_INDEX);

	if (!ppWall)
		return (E_INVALIDARG);

	*ppWall = NULL;

	pWall = NULL;

	if (!nRoom)
	{
		if (m_WallList.GetCount() <= 0)
			return (A3DERROR_INVALID_WALL_INDEX);

		pos = m_WallList.FindIndex(nWall);

		if (pos)
			pWall = m_WallList.GetAt(pos);

		fTakeReference = 1;
	}
	else
	{
		if (m_RoomList.GetCount() <= 0)
			return (A3DERROR_INVALID_ROOM_INDEX);

		pos = m_RoomList.FindIndex(nRoom);

		pRoom = pos ? m_RoomList.GetAt(pos) : NULL;

		if (!pRoom)
			return (A3DERROR_INVALID_ROOM_INDEX);

		if (FAILED(pRoom->GetWall(nWall, &pWall)))
			return (A3DERROR_INVALID_WALL_INDEX);

		fTakeReference = 0;
	}

	if (!pWall)
		return (A3DERROR_INVALID_WALL_INDEX);

	*ppWall = pWall;

	if (fTakeReference)
		((IUnknown *) pWall)->AddRef();

	return (S_OK);
}

/* =============================================================
// AddRoom()
// (RE) rtl:0x10030e10; dbg:0x1007dc50
//
// Copy a validated room into the scene and invalidate the build.
//
// Returns: The new room count; A3DERROR_DYNAMIC_OBJ_UNSUPPORTED;
//          A3DERROR_INVALID_ROOM for a null or invalid builder or allocation
//          failure; A3DERROR_DIR_AND_UP_VECTORS_NOT_PERPENDICULAR for invalid
//          axes.
// =============================================================*/

STDMETHODIMP_(int)
CA3dScene::AddRoom(void *pDynamic, void *pBuilder, LPA3DVAL pvPosition,
				   LPA3DVAL pvFront, LPA3DVAL pvUp)
{
	static const A3DVAL vAt[4] = {0.0f, 0.0f, 0.0f, 1.0f};
	static const A3DVAL vFront[4] = {0.0f, 0.0f, 1.0f, 0.0f};
	static const A3DVAL vUp[4] = {0.0f, 1.0f, 0.0f, 0.0f};

	CA3dRoom *pRoom;

	if (pDynamic)
		return (A3DERROR_DYNAMIC_OBJ_UNSUPPORTED);

	if (!pBuilder)
		return (A3DERROR_INVALID_ROOM);

	if (FAILED(((IA3dRoomBuilder *) pBuilder)->Validate()))
		return (A3DERROR_INVALID_ROOM);

	if (!pvPosition)
		pvPosition = (LPA3DVAL) vAt;

	if (!pvFront)
		pvFront = (LPA3DVAL) vFront;

	if (!pvUp)
		pvUp = (LPA3DVAL) vUp;

	if (!A3dFrameIsSquare(pvFront, pvUp))
		return (A3DERROR_DIR_AND_UP_VECTORS_NOT_PERPENDICULAR);

	pRoom = new CA3dRoom((CA3dRoomBuilder *) pBuilder, 0, pvPosition, pvFront,
						 pvUp, this);

	if (!pRoom)
		return (A3DERROR_INVALID_ROOM);

	pRoom->AddRef();

	m_RoomList.AddTail(pRoom);

	m_fBuilt = 0;

	return (m_RoomList.GetCount());
}

/* =============================================================
// A3dListNodeInit()
//
// Initialize a tail node; its 677 field initialization is unknown.
//
// Returns: The initialized node.
// =============================================================*/

A3DLISTNODE *
A3dListNodeInit(A3DLISTNODE *pNode, A3DLISTNODE *pPrev, void *pObject)
{
	pNode->pPrev   = pPrev;
	pNode->pNext   = NULL;
	pNode->pObject = pObject;

	return (pNode);
}

/* =============================================================
// RemoveRoom()
// (RE) rtl:0x10031050; dbg:0x1007de80
//
// Release and remove a room, then invalidate the build.
//
// Returns:
//   S_OK
//   A3DERROR_INVALID_ROOM_INDEX  if the room is absent
// =============================================================*/

STDMETHODIMP
CA3dScene::RemoveRoom(int nRoom)
{
IUnknown       *pRoom;
POSITION        pos;

	if (nRoom <= 0)
		return (A3DERROR_INVALID_ROOM_INDEX);

	pos = m_RoomList.FindIndex(nRoom);

	pRoom = pos ? (IUnknown *) m_RoomList.GetAt(pos) : NULL;

	if (!pRoom)
		return (A3DERROR_INVALID_ROOM_INDEX);

	pRoom->Release();

	m_RoomList.RemoveAt(pos);

	m_fBuilt = 0;

	return (S_OK);
}

/* =============================================================
// GetOpening()
// (RE) rtl:0x10031100; dbg:0x1007df30
//
// Return a referenced opening through GetWall. The declaration still
// retains pointer types for the integer index and void ** output.
//
// Returns: The GetWall or wall GetOpening failure; S_OK on success;
//          E_INVALIDARG if pOpeningOutput is null.
// =============================================================*/

STDMETHODIMP
CA3dScene::GetOpening(int nRoom, int nWall, const CA3dFrame *pOpeningIndex,
		   const A3DVAL *pOpeningOutput)
{
void           *pWall;
HRESULT         hr;

	if (!pOpeningOutput)
		return (E_INVALIDARG);

	pWall = (void *) pOpeningOutput;

	hr = GetWall(nRoom, nWall, &pWall);

	if (FAILED(hr))
		return (hr);

	hr = ((CA3dWall *) pWall)->GetOpening((int) (LONG_PTR) pOpeningIndex,
					      (void **) pOpeningOutput);

	((IUnknown *) pWall)->Release();

	if (FAILED(hr))
		return (hr);

	return (S_OK);
}

/* =============================================================
// GetRoom()
// (RE) rtl:0x10031160; dbg:0x1007dfe0
//
// Return a referenced room by one-based index.
//
// Returns:
//   S_OK
//   A3DERROR_INVALID_ROOM_INDEX  an invalid index
//   A3DERROR_NO_ROOMS_IN_SCENE   if empty
//   E_INVALIDARG                 if ppRoom is null
// =============================================================*/

STDMETHODIMP
CA3dScene::GetRoom(int nRoom, void **ppRoom)
{
IUnknown       *pRoom;
POSITION        pos;

	if (nRoom < 0)
		return (A3DERROR_INVALID_ROOM_INDEX);

	if (!ppRoom)
		return (E_INVALIDARG);

	*ppRoom = NULL;

	if (m_RoomList.GetCount() <= 0)
		return (A3DERROR_NO_ROOMS_IN_SCENE);

	pos = nRoom > 0 ? m_RoomList.FindIndex(nRoom) : NULL;

	pRoom = pos ? (IUnknown *) m_RoomList.GetAt(pos) : NULL;

	if (!pRoom)
		return (A3DERROR_INVALID_ROOM_INDEX);

	*ppRoom = pRoom;

	pRoom->AddRef();

	return (S_OK);
}

/* =============================================================
// Clear()
// (RE) rtl:0x100311e0; dbg:0x1007e0c0
//
// Release rooms and walls and invalidate the build. Original defects:
// list nodes and counts retain released objects, and excessive counts
// call Release through the null sentinel element.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dScene::Clear(void)
{
IUnknown       *pObject;
POSITION        pos;
int             i;

	pos = m_RoomList.GetHeadPosition();

	for (i = m_RoomList.GetCount(); i > 0; i--)
	{
		pObject = (IUnknown *) m_RoomList.GetNext(pos);

		pObject->Release();
	}

	pos = m_WallList.GetHeadPosition();

	for (i = m_WallList.GetCount(); i > 0; i--)
	{
		pObject = (IUnknown *) m_WallList.GetNext(pos);

		pObject->Release();
	}

	m_fBuilt = 0;

	return (S_OK);
}

/* =============================================================
// Load()
// (RE) rtl:0x10009810; dbg:0x1007d310
//
// Reject scene loading.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dScene::Load(void *pv)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// Save()
// (RE) rtl:0x10009810; dbg:0x1007d330
//
// Reject scene saving.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dScene::Save(void *pv)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// UnSerialize()
// (RE) rtl:0x10009820; dbg:0x1007d350
//
// Reject scene deserialization.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dScene::UnSerialize(void *pv, UINT cb)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// Serialize()
// (RE) rtl:0x10009820; dbg:0x1007d370
//
// Reject scene serialization.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dScene::Serialize(void *pv, UINT cb)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// Duplicate()
// (RE) rtl:0x10009810; dbg:0x1007d390
//
// Reject scene duplication.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dScene::Duplicate(void *pv)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// Build()
// (RE) rtl:0x10031240; dbg:0x1007e1f0
//
// Classify rooms, record contacts, construct portals and rebuild geometry.
//
// Returns: S_OK; the original overlapping-room error branch is unreachable
//          because CheckAgainst always succeeds. Rebuild failures are ignored.
// =============================================================*/

STDMETHODIMP
CA3dScene::Build(void)
{
A3DLIST         listWork;
CA3dRoom       *pRoom;
CA3dRoom       *pOther;
POSITION        pos;
	int i, n;

	m_fBuilt = 0;

	if (m_RoomList.GetCount() <= 0)
	{
		m_fBuilt = 1;

		return (S_OK);
	}

	m_StaticRoomList.RemoveAll();
	m_MovingList.RemoveAll();

	pos = m_RoomList.GetHeadPosition();

	for (i = m_RoomList.GetCount(); i > 0; i--)
	{
		pRoom = m_RoomList.GetNext(pos);

		if (A3dRoomIsMoving(pRoom))
			m_MovingList.AddTail(pRoom);
		else
			m_StaticRoomList.AddTail(pRoom);
	}

	m_KeptList.RemoveAll();

	listWork.pHead  = NULL;
	listWork.pTail  = NULL;
	listWork.pCurr  = NULL;
	listWork.cNodes = 0;

	A3dListClear(&listWork);

	pos = m_StaticRoomList.GetHeadPosition();

	for (i = m_StaticRoomList.GetCount(); i > 0; i--)
		A3dListAdd(&listWork, m_StaticRoomList.GetNext(pos));

	pos = m_StaticRoomList.GetHeadPosition();

	for (i = m_StaticRoomList.GetCount(); i > 0; i--)
	{
		pRoom = m_StaticRoomList.GetNext(pos);

		pRoom->ClearWallCrossings();

		listWork.pCurr = listWork.pHead;

		for (n = listWork.cNodes; n > 0; n--)
		{
			pOther = (CA3dRoom *) A3dListNext(&listWork);

			if (pOther == pRoom)
				continue;

			pRoom->FindTouching(pOther);

			if (FAILED(pRoom->CheckAgainst(pOther)))
			{
				A3dListClear(&listWork);

				return (A3DERROR_SCENE_HAS_OVERLAPPING_STATIC_ROOMS);
			}
		}

		if (pRoom->HasUncrossedShellWall())
			m_KeptList.AddTail(pRoom);
	}

	pos = m_StaticRoomList.GetHeadPosition();

	for (i = m_StaticRoomList.GetCount(); i > 0; i--)
		m_StaticRoomList.GetNext(pos)->FinishWalls();

	pos = m_StaticRoomList.GetHeadPosition();

	for (i = m_StaticRoomList.GetCount(); i > 0; i--)
		m_StaticRoomList.GetNext(pos)->Rebuild(m_pApi);

	A3dListClear(&listWork);

	m_pCurrentRoom = NULL;

	m_fBuilt = 1;

	return (S_OK);
}

/* =============================================================
// Compile()
// (RE) rtl:0x100314e0; dbg:0x1007e6e0
//
// Compile static rooms and scene walls into one list and reset traversal.
// Original defect: the old list is deleted regardless of references.
//
// Returns:
//   S_OK
//   A3DERROR_SCENE_INVALID  if not built
// =============================================================*/

STDMETHODIMP
CA3dScene::Compile(void)
{
CA3dList       *pList;
CA3dRoom       *pRoom;
CA3dWall       *pWall;
POSITION        pos;
int             i;

	if (!m_fBuilt)
		return (A3DERROR_SCENE_INVALID);

	if (m_pCompiledList)
		delete m_pCompiledList;

	pList = new CA3dList(m_pApi);

	m_pCompiledList = pList;

	m_pApi->Clear();

	((IA3dGeom2 *) m_pApi)->LoadIdentity();

	pList->Begin();

	pos = m_StaticRoomList.GetHeadPosition();

	for (i = m_StaticRoomList.GetCount(); i > 0; i--)
	{
		pRoom = m_StaticRoomList.GetNext(pos);

		pRoom->Emit(m_pApi);
	}

	pos = m_WallList.GetHeadPosition();

	for (i = m_WallList.GetCount(); i > 0; i--)
	{
		pWall = m_WallList.GetNext(pos);

		pWall->Emit(m_pApi);
	}

	pList->End();

	m_nWalkVertex = 0;

	pList->RewindPool();

	return (S_OK);
}

/* =============================================================
// ReleaseCompiledScene()
// (RE) rtl:0x10031640; dbg:0x1007ea10
//
// Release the compiled scene list. Original defect: a built scene with
// no compiled list dereferences null.
//
// Returns:
//   S_OK
//   A3DERROR_SCENE_INVALID  if not built
// =============================================================*/

STDMETHODIMP
CA3dScene::ReleaseCompiledScene(void)
{
	if (!m_fBuilt)
		return (A3DERROR_SCENE_INVALID);

	m_pCompiledList->Release();

	m_pCompiledList = NULL;

	return (S_OK);
}

/* =============================================================
// NextVertex()
// (RE) rtl:0x10031680; dbg:0x1007ea70
//
// Read the next xyz vertex and its primitive-local index.
//
// Returns: 1 if a vertex was read; 0 if unbuilt or a required list, pool or
//          next block is absent.
// =============================================================*/

STDMETHODIMP
CA3dScene::NextVertex(LPA3DVAL pvOut, LPDWORD pdwIndex, CA3dPool *pPool)
{
CA3dList       *pList;
A3DVAL         *pv;
DWORD           dwMode;

	if (!m_fBuilt)
		return ((HRESULT) 0);

	pList = m_pCompiledList;

	if (!pList)
		return ((HRESULT) 0);

	if (!pPool)
		return ((HRESULT) 0);

	if (!m_nWalkVertex)
	{
		m_pWalkBlock = NULL;

		if (pList->GetPool())
			m_pWalkBlock = pPool->NextBlock(&dwMode);

		if (!m_pWalkBlock)
			return ((HRESULT) 0);
	}

	pv = m_pWalkBlock->av[m_nWalkVertex];

	pvOut[0] = pv[0];
	pvOut[1] = pv[1];
	pvOut[2] = pv[2];

	*pdwIndex = (DWORD) m_nWalkVertex;

	m_nWalkVertex++;

	if (m_nWalkVertex == m_pWalkBlock->cVertices)
		m_nWalkVertex = 0;

	return ((HRESULT) 1);
}

/* =============================================================
// A3dWhichRoom()
// (RE) dbg:0x1007ec30
//
// Find and cache the listener room, honoring one forced selection.
//
// Returns: A borrowed containing or forced room; null if none is found.
// =============================================================*/

CA3dRoom *
A3dWhichRoom(CA3dScene *pScene)
{
const A3DVAL   *pcvPoint;
CA3dRoom       *pRoom;

	pcvPoint = pScene->m_pApi->m_vListenerPos;

	pRoom = pScene->m_pCurrentRoom;

	if (pRoom)
	{
		if (pScene->m_fCurrentRoomForced)
		{
			pScene->m_fCurrentRoomForced = 0;

			return (pRoom);
		}

		if (A3dPointInside(pRoom, pcvPoint))
			return (pRoom);
	}

	pRoom = pScene->FindRoom(pcvPoint);

	pScene->m_pCurrentRoom = pRoom;

	return (pRoom);
}

/* =============================================================
// FindRoom()
// (RE) dbg:0x1007ED00; thunk dbg:0x10001299; inlined at rtl:0x1003199F
//
// Return the first static room containing the point.
//
// Returns: A borrowed room; null if none contains the point.
// =============================================================*/

CA3dRoom *
CA3dScene::FindRoom(const A3DVAL *pcvPoint)
{
CA3dRoom       *pRoom;
POSITION        pos;

	pos = m_StaticRoomList.GetHeadPosition();

	while (!m_StaticRoomList.IsEnd(pos))
	{
		pRoom = m_StaticRoomList.GetNext(pos);

		if (A3dPointInside(pRoom, pcvPoint))
			return (pRoom);
	}

	return (NULL);
}

/* =============================================================
// A3dGatherRooms()
// (RE) rtl:0x10031760; dbg:0x1007edb0; thunk dbg:0x1000319D
//
// Select active rooms and submit scene geometry. Original defect: null
// selected-room entries are dereferenced.
//
// Returns:
//   S_OK
//   A3DERROR_SCENE_INVALID  if not built
// =============================================================*/

HRESULT
A3dGatherRooms(CA3dScene *pScene)
{
CA3dPtrList<CA3dRoom>	*pList;
CA3dRoom       *pRoom;
POSITION        pos;
int             i;

	if (!pScene->m_fBuilt)
		return (A3DERROR_SCENE_INVALID);

	pScene->m_GatheredList.RemoveAll();

	A3dWhichRoom(pScene);

	pRoom  = pScene->m_pCurrentRoom;
	pList  = &pScene->m_KeptList;

	if (pRoom && pRoom->m_TouchingList.GetCount() > 0 && !pRoom->m_fHasMarkedWall)
	{
	POSITION        posTouch;
	int             cItems;

		pScene->m_GatheredList.RemoveAll();

		cItems = pRoom->m_TouchingList.GetCount();

		posTouch = pRoom->m_TouchingList.GetHeadPosition();

		pScene->m_GatheredList.RemoveAll();

		for (i = cItems; i > 0; i--)
		{
		CA3dRoom	*pItem;

			pItem = pRoom->m_TouchingList.GetNext(posTouch);

			pScene->m_GatheredList.AddTail(pItem);
		}

		pScene->m_GatheredList.AddTail(pRoom);

		pList = &pScene->m_GatheredList;
	}

	if (pScene->m_link.HasMaterial())
	{
		((IA3dGeom2 *) pScene->m_pApi)->BindMaterial(
			(LPA3DMATERIAL) pScene->m_link.GetMaterialRaw());
	}

	((IA3dGeom2 *) pScene->m_pApi)->LoadIdentity();

	pos = pScene->m_WallList.GetHeadPosition();

	for (i = pScene->m_WallList.GetCount(); i > 0; i--)
	{
	CA3dWall	*pWall;

		pWall = pScene->m_WallList.GetNext(pos);

		if (pWall)
			pWall->Emit((CA3dRoot *) pScene->m_pApi);
	}

	pos = pList->GetHeadPosition();

	for (i = pList->GetCount(); i > 0; i--)
	{
	CA3dRoom	*pItem;

		pItem = pList->GetNext(pos);

		if (pItem->m_dwState)
		{
		CA3dList	*pInner;

			pInner = pItem->m_pCompiled;

			if (pInner)
				pInner->Call();
		}
	}

	return (S_OK);
}

/* =============================================================
// FindByPoint()
// (RE) rtl:0x10031960; dbg:0x1007f040
//
// Set ppFound to the first containing static room, with a reference, or null.
//
// Returns:
//   S_OK
//   A3DERROR_SCENE_INVALID  if unbuilt
//   E_INVALIDARG            if ppFound is null
// =============================================================*/

STDMETHODIMP
CA3dScene::FindByPoint(A3DVAL x, A3DVAL y, A3DVAL z, void **ppFound)
{
A3DVAL          vPoint[3];
void           *pFound;

	if (!m_fBuilt)
		return (A3DERROR_SCENE_INVALID);

	if (!ppFound)
		return (E_INVALIDARG);

	vPoint[0] = x;
	vPoint[1] = y;
	vPoint[2] = z;

	pFound = FindRoom(vPoint);

	*ppFound = pFound;

	if (pFound)
		((IUnknown *) pFound)->AddRef();

	return (S_OK);
}

/* =============================================================
// NewTransform()
// (RE) rtl:0x10031a00; dbg:0x1007f0d0
//
// Allocate a transform frame with one reference.
//
// Returns:
//   S_OK
//   E_INVALIDARG                if ppXform is null
//   A3DERROR_MEMORY_ALLOCATION  if allocation fails
// =============================================================*/

STDMETHODIMP
CA3dScene::NewTransform(CA3dFrame **ppXform)
{
CA3dFrame *pNew;

	if (!ppXform)
		return (E_INVALIDARG);

	pNew = new CA3dFrame();

	if (!pNew)
		return (A3DERROR_MEMORY_ALLOCATION);

	*ppXform = pNew;

	pNew->AddRef();

	return (S_OK);
}

/* =============================================================
// SetRoomFlags()
// (RE) rtl:0x10031aa0; dbg:0x1007f1c0
//
// Set bits in a numbered room state word.
//
// Returns:
//   S_OK
//   A3DERROR_INVALID_ROOM_INDEX  if the room is absent
// =============================================================*/

STDMETHODIMP
CA3dScene::SetRoomFlags(int nRoom, DWORD dwFlags)
{
CA3dRoom       *pRoom;
POSITION        pos;

	if (nRoom < 0)
		return (A3DERROR_INVALID_ROOM_INDEX);

	pos = nRoom > 0 ? m_RoomList.FindIndex(nRoom) : NULL;

	pRoom = pos ? m_RoomList.GetAt(pos) : NULL;

	if (!pRoom)
		return (A3DERROR_INVALID_ROOM_INDEX);

	/* (RE) Room state helper, inlined here: dbg:0x1007f250. */
	pRoom->m_dwState |= dwFlags;

	return (S_OK);
}

/* =============================================================
// ClearRoomFlags()
// (RE) rtl:0x10031b00; dbg:0x1007f290
//
// Clear bits in a numbered room state word.
//
// Returns:
//   S_OK
//   A3DERROR_INVALID_ROOM_INDEX  if the room is absent
// =============================================================*/

STDMETHODIMP
CA3dScene::ClearRoomFlags(int nRoom, DWORD dwFlags)
{
CA3dRoom       *pRoom;
POSITION        pos;

	if (nRoom < 0)
		return (A3DERROR_INVALID_ROOM_INDEX);

	pos = nRoom > 0 ? m_RoomList.FindIndex(nRoom) : NULL;

	pRoom = pos ? m_RoomList.GetAt(pos) : NULL;

	if (!pRoom)
		return (A3DERROR_INVALID_ROOM_INDEX);

	/* (RE) Room state helper, inlined here: dbg:0x1007f320. */
	pRoom->m_dwState &= ~dwFlags;

	return (S_OK);
}

/* =============================================================
// SetCurrentRoom()
// (RE) rtl:0x10031b60; dbg:0x1007f360
//
// Force a numbered room for the next listener-room lookup.
//
// Returns:
//   S_OK
//   A3DERROR_INVALID_ROOM_INDEX  if the room is absent
// =============================================================*/

STDMETHODIMP
CA3dScene::SetCurrentRoom(int nRoom)
{
CA3dRoom       *pRoom;
POSITION        pos;

	if (nRoom < 0)
		return (A3DERROR_INVALID_ROOM_INDEX);

	pos = nRoom > 0 ? m_RoomList.FindIndex(nRoom) : NULL;

	pRoom = pos ? m_RoomList.GetAt(pos) : NULL;

	if (!pRoom)
		return (A3DERROR_INVALID_ROOM_INDEX);

	m_pCurrentRoom          = pRoom;
	m_fCurrentRoomForced    = 1;

	return (S_OK);
}

/* -------------------------------------------------------------------------- */

typedef int A3dSceneSizeCheck[(sizeof(CA3dScene) == 0x17C) ? 1 : -1];
