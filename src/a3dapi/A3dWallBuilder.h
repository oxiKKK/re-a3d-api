/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dWallBuilder.h
 *
 * Declares CA3dWallBuilder, the private interface for preparing a wall
 * before placement in a room or scene. It combines polygon input with a
 * name, material link and retained openings.
 *
 * The interface exposes vertex editing, opening management and
 * validation. PolygonBuilder.h defines the shared geometry-building
 * state, and A3dWall.h declares the placed wall created from validated
 * input. Shared builder COM methods are implemented in Corners.cpp.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _A3DWALLBUILDER_H
#define _A3DWALLBUILDER_H

#include "A3dPrivate.h"
#include "PolygonBuilder.h"
#include "NamedObject.h"
#include "MaterialLink.h"
#include "LinkList.h"

class CA3dOpening;

/* =============================================================
// Class: CA3dWallBuilder
//
// Description: Polygon builder with material, name and opening storage.
//
// Size: 0x1B0
//
// (RE) Constructor: rtl:0x10034070; dbg:0x10084070
// =============================================================*/

/* Polygon-base override table at 0x04: dbg:0x10139784. */

class CA3dWallBuilder : public IA3dWallBuilder,
			public CA3dPolygonBuilder
{
public:

	BOOL IsOpeningInPlane(CA3dOpening *pOpening) const;

	CA3dWallBuilder(int nType);
	virtual ~CA3dWallBuilder(void);

	/* IA3dWallBuilder vtable: dbg:0x10139788; rtl:0x100543a8. */

	STDMETHODIMP         QueryInterface(REFIID riid, void **ppv); /* 0 */
	STDMETHODIMP_(ULONG) AddRef(void);                            /* 1 */
	STDMETHODIMP_(ULONG) Release(void);                           /* 2 */

	STDMETHODIMP SetMaterial(void *pMaterial);   /* 3 */
	STDMETHODIMP GetMaterial(void **ppMaterial); /* 4 */

	STDMETHODIMP_(int) AddOpening(void *pOpening, LPA3DVAL pvPosition,
	                              LPA3DVAL pvFront, LPA3DVAL pvUp); /* 5 */
	STDMETHODIMP RemoveOpening(int nOpening); /* 6 */
	STDMETHODIMP GetOpening(int nOpening, void **ppOpening); /* 7 */

	/* Slots 8..15 delegate to CA3dPolygonBuilder. */
	STDMETHODIMP       Begin(DWORD dwMode);                    /* 8 */
	STDMETHODIMP       Vertex3f(A3DVAL x, A3DVAL y, A3DVAL z); /* 9 */
	STDMETHODIMP       Vertex3fv(LPA3DVAL pv);                 /* 10 */
	STDMETHODIMP       End(void);                              /* 11 */
	STDMETHODIMP       RemovePrimitive(int nPrim);             /* 12 */
	STDMETHODIMP_(int) GetPrimitiveCount(void);                /* 13 */
	STDMETHODIMP       SetVertex(int nPrim, int nVertex,
	                             A3DVAL x, A3DVAL y, A3DVAL z); /* 14 */
	STDMETHODIMP GetVertex(int nPrim, int nVertex, LPA3DVAL pv); /* 15 */

	STDMETHODIMP Validate(void); /* 16 */
	STDMETHODIMP Clear(void);    /* 17 */
	void         ClearOpenings(void);

	STDMETHODIMP Load(void *pv);                 /* 18 */
	STDMETHODIMP Save(void *pv);                 /* 19 */
	STDMETHODIMP UnSerialize(void *pv, UINT cb); /* 20 */
	STDMETHODIMP Serialize(void *pv, UINT cb);   /* 21 */
	STDMETHODIMP Duplicate(void *pv);            /* 22 */

	STDMETHODIMP SetName(LPCVOID pvName);           /* 23 */
	STDMETHODIMP GetName(LPVOID pvName, int nSize); /* 24 */

	void *GetOpeningList(void)  { return (&m_OpeningList); }

	CA3dLink *GetMaterialSlot(void)  { return (&m_link); }

protected:
	/* 0x00 */ /* IA3dWallBuilder vptr (no data) */
	/* 0x04 */ /* CA3dPolygonBuilder base subobject, +0x04..+0x94 */

	/* 0x94 */ CA3dLink  m_link;
	/* 0x9C */ CA3dNamed m_named;
	/* 0x1A0 */ LONG     m_cRef;

	/* 12-byte list of referenced openings. */
	/* 0x1A4 */ CA3dPtrList<CA3dOpening> m_OpeningList;
};

/* Default-name sequence, defined in Corners.cpp. */

extern DWORD g_cWallBuilders;

#endif /* _A3DWALLBUILDER_H */
