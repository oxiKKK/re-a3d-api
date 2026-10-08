/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dRoom.cpp
 *
 * Implements placed rooms in the private scene model. A room copies walls
 * from a room builder, maintains its coordinate frame and compiles its
 * geometry into a retained list for tracing.
 *
 * Containment and wall-contact tests identify relationships between
 * rooms. Position and orientation changes invalidate the owning scene's
 * compiled geometry. A3dScene.cpp coordinates scene rebuilding, while
 * A3dWall.cpp handles individual wall geometry and openings.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "A3dRoom.h"
#include "A3d3.h"
#include "A3dFrame.h"
#include "A3dGeom.h"
#include "A3dList.h"
#include "A3dRoomBuilder.h"
#include "A3dScene.h"
#include "A3dWall.h"
#include "A3dWallBuilder.h"
#include "LinkList.h"
#include "A3dOpening.h"
#include "A3dOpeningBuilder.h"
#include "WallEdge.h"

#include <math.h>

/* =============================================================
// CA3dRoom()
// (RE) rtl:0x1002ee20; dbg:0x10079250
//
// Copy the builder walls and room placement. Original allocation failures
// can fault or leave inconsistent list counts.
// =============================================================*/

CA3dRoom::CA3dRoom(CA3dRoomBuilder *pBuilder, DWORD dwFlags,
				   LPA3DVAL pvPosition, LPA3DVAL pvFront, LPA3DVAL pvUp,
				   CA3dScene *pScene)
{
const CA3dPtrList<CA3dWall> *pFrom;
CA3dWall       *pWall;
CA3dWall       *pSource;
POSITION        pos;
int             i;

	m_cRef = 0;

	m_dwFlags = dwFlags;

	A3dFrameSet(&m_xform, this, pvPosition, pvFront, pvUp);

	/* (RE) Wall-copy helper, inlined here: dbg:0x10079790. */
	pFrom = pBuilder->GetWallList();

	pos = pFrom->GetHeadPosition();

	for (i = pFrom->GetCount(); i > 0; i--)
	{
		pSource = pFrom->GetNext(pos);

		pWall = new CA3dWall(pSource, this);

		m_WallList.AddTail(pWall);

		((IUnknown *) pWall)->AddRef();

		if (A3dWallIsShell(pWall))
			m_ShellList.AddTail(pWall);
	}

	m_pCompiled     = NULL;
	m_dwState       = A3D_ROOM_STATE_DEFAULT;

	m_link.CopyMaterialFrom(pBuilder->GetMaterialSlot());

	m_pScene        = pScene;
	m_fValidated    = pBuilder->IsValidated();

	m_fHasMarkedWall = 0;
}

/* =============================================================
// A3dListClear()
//
// Delete placeholder-list nodes without deleting their objects.
// =============================================================*/

void
A3dListClear(A3DLIST *pList)
{
A3DLISTNODE    *p;
A3DLISTNODE    *pNext;
int             i;

	p = pList->pHead;

	for (i = 0; i < pList->cNodes; i++)
	{
		pNext = p->pNext;

		if (p)
			delete[] (BYTE *) p;

		p = pNext;
	}

	pList->cNodes   = 0;
	pList->pCurr    = NULL;
	pList->pHead    = NULL;
	pList->pTail    = NULL;
}

/* =============================================================
// CA3dRoom scalar deleting destructor
// (RE) rtl:0x1002f040; dbg:0x10079470
// =============================================================*/

/* =============================================================
// ~CA3dRoom()
// (RE) rtl:0x1002f070; dbg:0x100794f0
//
// Release the walls and compiled geometry. Original defect: an excessive
// wall count calls Release through the null sentinel element.
// =============================================================*/

CA3dRoom::~CA3dRoom(void)
{
IUnknown       *pWall;
POSITION        pos;
int             i;

	pos = m_WallList.GetHeadPosition();

	for (i = m_WallList.GetCount(); i > 0; i--)
	{
		pWall = (IUnknown *) m_WallList.GetNext(pos);

		pWall->Release();
	}

	m_ShellList.RemoveAll();

	if (m_pCompiled)
	{
		m_pCompiled->Release();

		m_pCompiled = NULL;
	}

	m_TouchingList.RemoveAll();

	m_WallList.RemoveAll();
}

/* =============================================================
// QueryInterface()
// (RE) rtl:0x1002f220; dbg:0x10079620
//
// Return IUnknown or IA3dRoom. Original defect: an unsupported IID leaves
// *ppv unchanged and AddRefs any nonnull value already there.
//
// Returns:
//   S_OK           if *ppv is nonnull
//   E_NOINTERFACE  otherwise
//   E_INVALIDARG   if ppv is null
// =============================================================*/

STDMETHODIMP
CA3dRoom::QueryInterface(REFIID riid, void **ppv)
{
	if (!ppv)
		return (E_INVALIDARG);

	if (!memcmp(&riid, &IID_IUnknown, sizeof(IID)) ||
		!memcmp(&riid, &IID_IA3dRoom, sizeof(IID)))
		*ppv = this;

	if (!*ppv)
		return (E_NOINTERFACE);

	((IUnknown *) *ppv)->AddRef();

	return (S_OK);
}

/* =============================================================
// AddRef()
// (RE) rtl:0x1002f280; dbg:0x100796c0
//
// Add a reference to the room.
//
// Returns: The new reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dRoom::AddRef(void)
{
	return (++m_cRef);
}

/* =============================================================
// Release()
// (RE) rtl:0x1002f2a0; dbg:0x10079700
//
// Release a reference and delete the room at zero.
//
// Returns: The remaining reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dRoom::Release(void)
{
	if (--m_cRef)
		return (m_cRef);

	delete this;

	return (0);
}

/* =============================================================
// ClearWallCrossings()
// (RE) dbg:0x100799d0
//
// Clear borrowed wall contacts and touching-room entries.
// =============================================================*/

void
CA3dRoom::ClearWallCrossings(void)
{
CA3dPtrList<CA3dWall>::Iterator it;

	for (it = m_ShellList.Begin(); it != m_ShellList.End(); ++it)
	{
	CA3dWall *pWall = *it;

		pWall->GetTouchList()->RemoveAll();
	}

	m_TouchingList.RemoveAll();
}

/* =============================================================
// HasUncrossedShellWall()
// (RE) dbg:0x10079f90
//
// Check whether any shell wall lacks contacts.
//
// Returns: 1 if any shell wall has no contacts; 0 otherwise.
// =============================================================*/

int
CA3dRoom::HasUncrossedShellWall(void)
{
CA3dPtrList<CA3dWall>::Iterator it;

	for (it = m_ShellList.Begin(); it != m_ShellList.End(); ++it)
	{
	CA3dWall *pWall = *it;

		if (!pWall->GetTouchList()->GetCount())
			return (1);
	}

	return (0);
}

typedef int A3dRoomSizeCheck[(sizeof(CA3dRoom) == 0x9C) ? 1 : -1];

/* =============================================================
// A3dPointInside()
// (RE) rtl:0x1002F380; dbg:0x10079a80; thunk dbg:0x100036C5
//
// Test room containment with a positive-X ray through shell primitives.
//
// Returns:
//   TRUE   an odd hit count
//   FALSE  if unvalidated or outside
// =============================================================*/

BOOL
A3dPointInside(CA3dRoom *pRoom, const A3DVAL *pcvPoint)
{
A3DVAL          vFrom[4];
A3DVAL          vDir[4];
POSITION        posShell;
int             cHits;
int             i;

	if (!pRoom->m_fValidated)
		return (FALSE);

	vDir[0] = 1.0f;
	vDir[1] = 0.0f;
	vDir[2] = 0.0f;
	vDir[3] = 0.0f;

	vFrom[0] = pcvPoint[0];
	vFrom[1] = pcvPoint[1];
	vFrom[2] = pcvPoint[2];

	posShell = pRoom->m_ShellList.GetHeadPosition();

	cHits = 0;

	for (i = pRoom->m_ShellList.GetCount(); i > 0; i--)
	{
		CA3dWall       *pWall;
		POSITION        posPrim;
		int             k;

		pWall = pRoom->m_ShellList.GetNext(posShell);

		posPrim = pWall->m_PrimList.GetHeadPosition();

		for (k = pWall->m_PrimList.GetCount(); k > 0; k--)
		{
			const A3DPRIMITIVE     *pPrim;
			A3DPRIMITIVE            prim;

			pPrim = pWall->m_PrimList.GetNext(posPrim);

			pWall->FetchPrimitive(&prim, pPrim);

			if (pPrim->cVertices == 4)
			{
				if (A3dTraceHitQuad(vFrom, vDir, &prim, 1, 1))
					cHits++;
			}
			else
			{
				if (A3dTraceHitTri(vFrom, vDir, &prim, 1, 1))
					cHits++;
			}
		}
	}

	return (cHits % 2 != 0);
}

/* =============================================================
// FindTouching()
// (RE) dbg:0x10079e60
//
// Record the other room if any shell-wall pair meets.
// =============================================================*/

void
CA3dRoom::FindTouching(CA3dRoom *pOther)
{
CA3dWall       *pMine;
CA3dWall       *pTheirs;
POSITION        pos;
	int i, n;

	pos = m_ShellList.GetHeadPosition();

	for (i = m_ShellList.GetCount(); i > 0; i--)
	{
		POSITION posOther;

		pMine = m_ShellList.GetNext(pos);

		posOther = pOther->m_ShellList.GetHeadPosition();

		for (n = pOther->m_ShellList.GetCount(); n > 0; n--)
		{
			pTheirs = pOther->m_ShellList.GetNext(posOther);

			if (pMine->Meets(pTheirs))
			{
				m_TouchingList.AddTail(pOther);

				return;
			}
		}
	}
}

/* =============================================================
// FAILED compiler-generated helper
// (RE) dbg:0x1007a170
// =============================================================*/

/* =============================================================
// CheckAgainst()
// (RE) dbg:0x1007a040
//
// Record contacts between both rooms shell walls.
//
// Returns: S_OK; the original E_FAIL branch is unreachable because NoteTouching
//          always returns zero.
// =============================================================*/

HRESULT
CA3dRoom::CheckAgainst(CA3dRoom *pOther)
{
CA3dWall       *pMine;
CA3dWall       *pTheirs;
POSITION        pos;
	int i, n;

	pos = m_ShellList.GetHeadPosition();

	for (i = m_ShellList.GetCount(); i > 0; i--)
	{
		POSITION posOther;

		pMine = m_ShellList.GetNext(pos);

		posOther = pOther->m_ShellList.GetHeadPosition();

		for (n = pOther->m_ShellList.GetCount(); n > 0; n--)
		{
			pTheirs = pOther->m_ShellList.GetNext(posOther);

			if (FAILED(pMine->NoteTouching(pTheirs)))
				return (E_FAIL);
		}
	}

	return (S_OK);
}

/* =============================================================
// Emit()
// (RE) dbg:0x1007a190
//
// Submit the room material and all walls.
// =============================================================*/

void
CA3dRoom::Emit(CA3dRoot *pApi)
{
CA3dWall       *pWall;
POSITION        pos;
int             i;

	if (m_link.HasMaterial())
		((IA3dGeom2 *) pApi)->BindMaterial(m_link.GetMaterialRaw());

	pos = m_WallList.GetHeadPosition();

	for (i = m_WallList.GetCount(); i > 0; i--)
	{
		pWall = m_WallList.GetNext(pos);

		pWall->Emit(pApi);

		if (A3dWallIsMarked(pWall))
			m_fHasMarkedWall = 1;
	}
}

/* =============================================================
// Rebuild()
// (RE) dbg:0x1007a2a0
//
// Compile room geometry into a retained list. Original defect: NewList
// failure is ignored before dereferencing the list.
//
// Returns: The result of the compiled list End call.
// =============================================================*/

HRESULT
CA3dRoom::Rebuild(CA3dRoot *pApi)
{
	if (m_pCompiled)
	{
		((IUnknown *) m_pCompiled)->Release();

		m_pCompiled = NULL;
	}

	((IA3dGeom2 *) pApi)->NewList((LPA3DLIST *) &m_pCompiled);

	pApi->Clear();

	((IA3dGeom2 *) pApi)->LoadIdentity();

	m_pCompiled->Begin();

	Emit(pApi);

	return (m_pCompiled->End());
}

/* =============================================================
// GetWall()
// (RE) rtl:0x1002f6c0; dbg:0x1007a390
//
// Return a referenced wall by one-based index.
//
// Returns:
//   S_OK
//   A3DERROR_INVALID_WALL_INDEX  an invalid index
//   E_INVALIDARG                 if ppWall is null and nWall is positive
// =============================================================*/

HRESULT
CA3dRoom::GetWall(int nWall, void **ppWall)
{
CA3dWall       *pWall;
POSITION        pos;

	if (nWall <= 0)
		return (A3DERROR_INVALID_WALL_INDEX);

	if (!ppWall)
		return (E_INVALIDARG);

	*ppWall = NULL;

	if (m_WallList.GetCount() <= 0)
		return (A3DERROR_INVALID_WALL_INDEX);

	pos = m_WallList.FindIndex(nWall);

	pWall = pos ? m_WallList.GetAt(pos) : NULL;

	if (!pWall)
		return (A3DERROR_INVALID_WALL_INDEX);

	*ppWall = pWall;

	((IUnknown *) pWall)->AddRef();

	return (S_OK);
}

/* =============================================================
// NewTransform()
// (RE) rtl:0x1002f740; dbg:0x1007a460
//
// Allocate a snapshot of the room transform. Original defect: ppXform
// is not checked.
//
// Returns:
//   S_OK
//   A3DERROR_MEMORY_ALLOCATION  if allocation fails
// =============================================================*/

HRESULT
CA3dRoom::NewTransform(void **ppXform)
{
CA3dFrame *pXform;

	pXform = new CA3dFrame;
	if (!pXform)
		return (A3DERROR_MEMORY_ALLOCATION);

	pXform->CopyFrameFrom(this, Xform());

	((IUnknown *) pXform)->AddRef();

	*ppXform = pXform;

	return (S_OK);
}

/* =============================================================
// GetScene()
// (RE) rtl:0x1002f7f0; dbg:0x1007a560
//
// Return the owning scene with a new reference. Original defect: a room
// without a scene dereferences null.
//
// Returns:
//   S_OK
//   E_INVALIDARG  if ppScene is null
// =============================================================*/

HRESULT
CA3dRoom::GetScene(void **ppScene)
{
	if (!ppScene)
		return (E_INVALIDARG);

	*ppScene = m_pScene;

	((IUnknown *) m_pScene)->AddRef();

	return (S_OK);
}

/* =============================================================
// CopyFrom()
// (RE) rtl:0x1002f820; dbg:0x1007a5c0
//
// Copy the matrix from a CA3dFrame and invalidate the scene.
//
// Returns:
//   S_OK
//   A3DERROR_INVALID_ARGUMENT  if pcFrom is null
// =============================================================*/

HRESULT
CA3dRoom::CopyFrom(const void *pcFrom)
{
	if (!pcFrom)
		return (A3DERROR_INVALID_ARGUMENT);

	A3dXformCopyMatrix(Xform(), pcFrom);

	MarkSceneStale();

	return (S_OK);
}

/* =============================================================
// MarkSceneStale()
// (RE) dbg:0x1007a610
//
// Invalidate the owning scene compiled state.
// =============================================================*/

void
CA3dRoom::MarkSceneStale(void)
{
	if (m_pScene)
		m_pScene->m_fBuilt = 0;
}

/* =============================================================
// SUCCEEDED compiler-generated helper
// (RE) dbg:0x1007a710
// =============================================================*/

/* =============================================================
// SetPosition()
// (RE) rtl:0x1002f8a0; dbg:0x1007a6b0
//
// Set the room position and invalidate the scene on success.
//
// Returns: The embedded transform result.
// =============================================================*/

HRESULT
CA3dRoom::SetPosition(const CA3dFrame *pcRelativeTo, const A3DVAL *pcv)
{
HRESULT hr;

	hr = Xform()->SetPosition(pcRelativeTo, pcv);

	if (SUCCEEDED(hr))
		MarkSceneStale();

	return (hr);
}

/* =============================================================
// GetPosition()
// (RE) rtl:0x1002f880; dbg:0x1007a680
//
// Read the room position relative to a frame.
//
// Returns: The embedded transform result.
// =============================================================*/

HRESULT
CA3dRoom::GetPosition(const CA3dFrame *pcRelativeTo, A3DVAL *pv)
{
	return (Xform()->GetPosition(pcRelativeTo, pv));
}

/* =============================================================
// GetOrientation()
// (RE) rtl:0x1002f8e0; dbg:0x1007a740
//
// Read the room axes relative to a frame.
//
// Returns: The embedded transform result.
// =============================================================*/

HRESULT
CA3dRoom::GetOrientation(const CA3dFrame *pcRelativeTo, A3DVAL *pvFront, A3DVAL *pvUp)
{
	return (Xform()->GetOrientation(pcRelativeTo,
									pvFront, pvUp));
}

/* -------------------------------------------------------------------------- */

/* =============================================================
// CA3dRoom::SetOrientation()
// (RE) rtl:0x1002f900; dbg:0x1007a780
//
// Set the room axes and invalidate the scene on success.
//
// Returns: The embedded transform result.
// =============================================================*/

HRESULT
CA3dRoom::SetOrientation(const CA3dFrame *pcRelativeTo, const A3DVAL *pcvFront,
						 const A3DVAL *pcvUp)
{
HRESULT hr;

	hr = Xform()->SetOrientation(pcRelativeTo, pcvFront,
								 pcvUp);

	if (SUCCEEDED(hr))
		MarkSceneStale();

	return (hr);
}

/* =============================================================
// CA3dWall::SetOrientation()
// (RE) rtl:0x10033b30; dbg:0x100821b0
//
// Set the wall axes and propagate staleness on success.
//
// Returns: The embedded transform result.
// =============================================================*/

HRESULT
CA3dWall::SetOrientation(const CA3dFrame *pcRelativeTo, const A3DVAL *pcvFront,
						 const A3DVAL *pcvUp)
{
HRESULT hr;

	hr = Xform()->SetOrientation(pcRelativeTo, pcvFront,
								 pcvUp);

	if (SUCCEEDED(hr))
		MarkSceneStale();

	return (hr);
}

/* =============================================================
// FinishWalls()
//
// Build portals for shell walls.
// =============================================================*/

void
CA3dRoom::FinishWalls(void)
{
CA3dWall       *pWall;
POSITION        pos;
int             i;

	pos = m_WallList.GetHeadPosition();

	for (i = m_WallList.GetCount(); i > 0; i--)
	{
		pWall = m_WallList.GetNext(pos);

		if (A3dWallIsShell(pWall))
			pWall->MakePortals();
	}
}
