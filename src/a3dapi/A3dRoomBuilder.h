/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dRoomBuilder.h
 *
 * Declares CA3dRoomBuilder, the private interface for assembling a room
 * from wall builders. It retains placed walls, material and name state,
 * validation results and the normal planes used to inspect the shell.
 *
 * The resulting geometry is copied into CA3dRoom when added to a scene.
 * A3dRoomBuilder.cpp implements wall management and validation;
 * Corners.cpp supplies the room builder's shared COM lifetime operations.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _A3DROOMBUILDER_H
#define _A3DROOMBUILDER_H

#include "A3dPrivate.h"
#include "Corners.h"
#include "NamedObject.h"
#include "MaterialLink.h"
#include "LinkList.h"

class CA3dWall;

/* =============================================================
// Class: CA3dRoomBuilder
//
// Description: Named room builder with retained walls and distinct normal
// planes.
//
// Size: 0x134
//
// (RE) Constructor: rtl:0x1002fac0; dbg:0x1007bd20
// =============================================================*/

/* Deleting-destructor tables: rtl:0x10054170 at 0x04, rtl:0x1005416c
 * at 0x0C. Release uses slot 0 of the 0x04 subobject. */

class CA3dRoomBuilder : public IA3dRoomBuilder
{
public:
	CA3dRoomBuilder(int nType);
	virtual ~CA3dRoomBuilder(void);

	/* IA3dRoomBuilder vtable: rtl:0x10054174; dbg:0x1013942C. */

	STDMETHODIMP		QueryInterface(REFIID riid, void **ppv);	/* 0 */
	STDMETHODIMP_(ULONG)	AddRef(void);				/* 1 */
	STDMETHODIMP_(ULONG)	Release(void);				/* 2 */

	STDMETHODIMP		Unknown_0x0C(void *pv);			/* 3 */
	STDMETHODIMP		Unknown_0x10(void *pv);			/* 4 */
	STDMETHODIMP		SetMaterial(void *pMaterial);		/* 5 */
	STDMETHODIMP		GetMaterial(void **ppMaterial);		/* 6 */

	STDMETHODIMP_(int)	AddWall(void *pBuilder, DWORD dwFlags, LPA3DVAL pvPosition,
					LPA3DVAL pvFront, LPA3DVAL pvUp);	/* 7 */
	STDMETHODIMP		RemoveWall(int nWall);			/* 8 */
	STDMETHODIMP		GetWall(int nWall, void **ppWall);	/* 9 */
	STDMETHODIMP		Validate(void);				/* 10 */
	STDMETHODIMP		Clear(void);				/* 11 */

	STDMETHODIMP		Load(void *pv);				/* 12 */
	STDMETHODIMP		Save(void *pv);				/* 13 */
	STDMETHODIMP		UnSerialize(void *pv, UINT cb);		/* 14 */
	STDMETHODIMP		Serialize(void *pv, UINT cb);		/* 15 */
	STDMETHODIMP		Duplicate(void *pv);			/* 16 */

	STDMETHODIMP		SetName(LPCVOID pvName);		/* 17 */
	STDMETHODIMP		GetName(LPVOID pvName, int nSize);	/* 18 */

	CA3dPtrList<CA3dWall>	*GetWallList(void)	{ return (&m_WallList); }

	CA3dLink		*GetMaterialSlot(void)	{ return (&m_link); }

	DWORD			IsValidated(void)	{ return (m_fValidated); }

protected:
	void	ClearNormals(void);
	int	IsNewNormal(const A3DVAL *pvPlane);
	int	CountShellWalls(void);
	int	RebuildNormals(void);
	void	ReleaseWalls(void);

protected:
	/* 0x00 */ /* IA3dRoomBuilder vptr (no data) */

	/* 0x04 */ CA3dLink	m_link;
	/* 0x0C */ CA3dNamed	m_named;
	/* 0x110 */ LONG	m_cRef;
	/* 0x114 */ int		m_nType;

	/* 12-byte list of referenced walls. */
	/* 0x118 */ CA3dPtrList<CA3dWall>	m_WallList;

	/* 12-byte list of owned four-float planes, one per normal direction. */
	/* 0x124 */ CA3dPtrList<A3DVAL>	m_NormalList;

	/* Mutating or clearing the builder does not reset this validation result. */
	/* 0x130 */ DWORD	m_fValidated;
};

/* Defined in Corners.cpp and used for generated builder names. */
extern DWORD g_cRoomBuilders;

#endif /* _A3DROOMBUILDER_H */
