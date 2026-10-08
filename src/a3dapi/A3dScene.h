/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dScene.h
 *
 * Declares CA3dScene, the private scene object that groups rooms,
 * independent walls and compiled geometry. It retains the root engine and
 * tracks scene build state, room relationships and traversal lists.
 *
 * Its interface adds, removes and retrieves placed geometry and exposes
 * scene construction and gathering operations. A3dScene.cpp coordinates
 * these operations using CA3dRoom, CA3dWall and CA3dList; sceneman.cpp
 * creates scenes through the root's private interface.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _A3DSCENE_H
#define _A3DSCENE_H

#include "A3dPrivate.h"
#include "NamedObject.h"
#include "MaterialLink.h"
#include "LinkList.h"
#include "Corners.h"

class CA3dList;
class CA3dPool;
class CA3dRoom;
class CA3dRoot;
class CA3dFrame;
class CA3dWall;

/* =============================================================
// Class: CA3dScene
//
// Description: Room and wall ownership, lookup and compiled geometry.
//
// Size: 0x17C
//
// (RE) Constructor: rtl:0x10030560; dbg:0x1007d090
// =============================================================*/

class CA3dScene : public IA3dScene
{
public:
	CA3dScene(int nSceneType, CA3dRoot *pApi);
	virtual ~CA3dScene(void);

	/* IA3dScene vtable: rtl:0x10054220; dbg:0x1013953c, slots 0..32. */

	STDMETHODIMP         QueryInterface(REFIID riid, void **ppv); /* 0 */
	STDMETHODIMP_(ULONG) AddRef(void); /* 1 */
	STDMETHODIMP_(ULONG) Release(void); /* 2 */

	STDMETHODIMP_(int) AddRoom(void *pDynamic, void *pBuilder,
	                           LPA3DVAL pvPosition, LPA3DVAL pvFront,
	                           LPA3DVAL pvUp); /* 3 */
	STDMETHODIMP_(int) AddWall(void *pDynamic, void *pBuilder,
	                           LPA3DVAL pvPosition, LPA3DVAL pvFront,
	                           LPA3DVAL pvUp); /* 4 */
	STDMETHODIMP RemoveRoom(int nRoom); /* 5 */
	STDMETHODIMP RemoveWall(int nWall); /* 6 */
	STDMETHODIMP Unknown_0x1C(void *pv); /* 7 */

	STDMETHODIMP_(int) IsScene(void); /* 8 */

	STDMETHODIMP GetRoom(int nRoom, void **ppRoom); /* 9 */
	STDMETHODIMP GetWall(int nRoom, int nWall, void **ppWall); /* 10 */
	STDMETHODIMP GetOpening(int nRoom, int nWall,
	                        const CA3dFrame *pOpeningIndex,
	                        const A3DVAL *pOpeningOutput); /* 11 */
	STDMETHODIMP FindByPoint(A3DVAL x, A3DVAL y, A3DVAL z,
	                         void **ppFound); /* 12 */
	STDMETHODIMP NewTransform(CA3dFrame **ppXform); /* 13 */
	STDMETHODIMP SetMaterial(void *pMaterial); /* 14 */
	STDMETHODIMP GetMaterial(void **ppMaterial); /* 15 */
	STDMETHODIMP Unknown_0x40(void *pv); /* 16 */
	STDMETHODIMP Unknown_0x44(void *pv); /* 17 */
	STDMETHODIMP SetRoomFlags(int nRoom, DWORD dwFlags); /* 18 */
	STDMETHODIMP ClearRoomFlags(int nRoom, DWORD dwFlags); /* 19 */
	STDMETHODIMP SetCurrentRoom(int nRoom); /* 20 */
	STDMETHODIMP Compile(void); /* 21 */
	STDMETHODIMP NextVertex(LPA3DVAL pvOut, LPDWORD pdwIndex,
	                        CA3dPool *pPool); /* 22 */
	STDMETHODIMP ReleaseCompiledScene(void); /* 23 */
	STDMETHODIMP Build(void); /* 24 */
	STDMETHODIMP Clear(void); /* 25 */
	STDMETHODIMP Load(void *pv); /* 26 */
	STDMETHODIMP Save(void *pv); /* 27 */
	STDMETHODIMP UnSerialize(void *pv, UINT cb); /* 28 */
	STDMETHODIMP Serialize(void *pv, UINT cb); /* 29 */
	STDMETHODIMP Duplicate(void *pv); /* 30 */

	STDMETHODIMP SetName(LPCVOID pvName); /* 31 */
	STDMETHODIMP GetName(LPVOID pvName, int nSize); /* 32 */

	friend CA3dRoom *A3dWhichRoom(CA3dScene *pScene);
	friend HRESULT  A3dGatherRooms(CA3dScene *pScene);

	friend class CA3dRoom;

protected:
	CA3dRoom *FindRoom(const A3DVAL *pcvPoint);

	/* 0x004 */ CA3dLink  m_link;
	/* 0x00C */ CA3dNamed m_named;

	/* 0x110 */ DWORD m_fBuilt; /* Set after Build succeeds. */

	/* 0x114 */ CA3dRoom *m_pCurrentRoom; /* Borrowed listener-room cache. */

	CA3dPtrList<CA3dRoom> m_RoomList; /* 0x118, owned room references */

	CA3dPtrList<CA3dWall> m_WallList; /* 0x124, owned wall references */

	/* 0x130, borrowed current room and neighbors */
	CA3dPtrList<CA3dRoom> m_GatheredList;

	/* 0x13C, borrowed rooms with uncrossed shell walls */
	CA3dPtrList<CA3dRoom> m_KeptList;

	CA3dPtrList<CA3dRoom> m_StaticRoomList; /* 0x148, borrowed zero-flag rooms */

	CA3dPtrList<CA3dRoom> m_MovingList; /* 0x154, borrowed nonzero-flag rooms */

	/* 0x160 */ int m_nSceneType; /* Creation type */

	/* 0x164 */ CA3dList *m_pCompiledList; /* Owned compiled scene. */

	/* 0x168 */ CA3dRoot *m_pApi;  /* held with a reference */

	/* 0x16C */ A3DPRIMITIVE *m_pWalkBlock; /* Current traversal primitive. */
	/* 0x170 */ int          m_nWalkVertex;

	/* 0x174 */ DWORD m_fCurrentRoomForced; /* Skip containment once. */

	/* 0x178 */ LONG m_cRef;
};

CA3dRoom *A3dWhichRoom(class CA3dScene *pScene);
HRESULT   A3dGatherRooms(class CA3dScene *pScene);

extern DWORD g_cScenes;

#endif /* _A3DSCENE_H */
