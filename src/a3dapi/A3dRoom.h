/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dRoom.h
 *
 * Declares CA3dRoom, a placed collection of walls belonging to a scene.
 * Its state links the room transform, wall list, room contacts and
 * compiled geometry used during scene traversal.
 *
 * The interface provides wall access and placement changes; internal
 * methods support containment, contact detection and geometry
 * compilation. A3dRoomBuilder supplies construction input, and A3dScene
 * owns the resulting room instances.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _A3DROOM_H
#define _A3DROOM_H

#include "A3dPrivate.h"
#include "A3dGeom.h"
#include "A3dFrame.h"
#include "MaterialLink.h"
#include "LinkList.h"

class CA3dList;
class CA3dRoomBuilder;
class CA3dRoot;
class CA3dScene;
class CA3dWall;

int  A3dRoomIsMoving(const class CA3dRoom *pcRoom);

/* (RE) Initial room state: dbg:0x1007933b; rtl:0x1002ee20. */

#define A3D_ROOM_STATE_DEFAULT 66

/* =============================================================
// Class: CA3dRoom
//
// Description: Scene room with retained walls and compiled geometry.
//
// Size: 0x9C
//
// (RE) Constructor: rtl:0x1002ee20; dbg:0x10079250
// =============================================================*/

/* (RE) IA3dRoom vtable: rtl:0x100540f0; dbg:0x10139350.
 * Material-link destructor table at 0x04: rtl:0x100540ec;
 * dbg:0x1013934c, slot 0.
 */

class CA3dRoom : public CA3dGeomIface
{
public:
	CA3dRoom(class CA3dRoomBuilder *pBuilder, DWORD dwFlags,
	         LPA3DVAL pvPosition, LPA3DVAL pvFront, LPA3DVAL pvUp,
	         class CA3dScene *pScene);
	virtual ~CA3dRoom(void);

	STDMETHODIMP         QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG) AddRef(void);
	STDMETHODIMP_(ULONG) Release(void);

	HRESULT GetScene(void **ppScene);
	HRESULT GetWall(int nWall, void **ppWall);
	HRESULT SetPosition(const CA3dFrame *pcRelativeTo, const A3DVAL *pcv);
	HRESULT GetPosition(const CA3dFrame *pcRelativeTo, A3DVAL *pv);
	HRESULT GetOrientation(const CA3dFrame *pcRelativeTo, A3DVAL *pvFront,
	                       A3DVAL *pvUp);
	HRESULT SetOrientation(const CA3dFrame *pcRelativeTo, const A3DVAL *pcvFront,
	                       const A3DVAL *pcvUp);
	HRESULT CopyFrom(const void *pcFrom);
	HRESULT NewTransform(void **ppXform);

	void MarkSceneStale(void);

	CA3dFrame *Xform(void)
		{ return (&m_xform); }

	const CA3dFrame *Xform(void) const
		{ return (&m_xform); }

	void    Emit(CA3dRoot *pApi);
	void    ClearWallCrossings(void);
	int     HasUncrossedShellWall(void);
	void    FinishWalls(void);
	HRESULT Rebuild(CA3dRoot *pApi);
	void    FindTouching(CA3dRoom *pOther);
	HRESULT CheckAgainst(CA3dRoom *pOther);

	friend BOOL A3dPointInside(CA3dRoom *pRoom, const A3DVAL *pcvPoint);
	friend HRESULT A3dGatherRooms(class CA3dScene *pScene);

	friend class CA3dScene;
	friend int A3dRoomIsMoving(const CA3dRoom *pcRoom);

protected:
	/* 0x04 */ CA3dLink m_link; /* the bound material */

	/* 0x0C */ CA3dFrame m_xform; /* Room placement. */

	/* Owned wall references. */
	CA3dPtrList<CA3dWall> m_WallList; /* 0x5C */

	/* Borrowed subset of m_WallList with the shell flag set. */
	CA3dPtrList<CA3dWall> m_ShellList; /* 0x68 */

	/* Borrowed rooms found to touch this room through shell walls. */
	CA3dPtrList<CA3dRoom> m_TouchingList; /* 0x74 */

	DWORD    m_dwFlags;    /* 0x80 */
	CA3dList *m_pCompiled;  /* 0x84, owned compiled room list */

	/* 0x88 */ DWORD m_dwState; /* Zero skips compiled geometry. */
	CA3dScene       *m_pScene;     /* 0x8C, borrowed owning scene */
	DWORD            m_fValidated; /* 0x90, copied from the builder */

	/* 0x94 */ DWORD m_fHasMarkedWall; /* Sticky marked-wall flag. */
	LONG             m_cRef; /* 0x98 */
};

BOOL A3dPointInside(class CA3dRoom *pRoom, const A3DVAL *pcvPoint);

#endif /* _A3DROOM_H */
