/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dOpeningBuilder.h
 *
 * Declares CA3dOpeningBuilder, the private interface object used to
 * prepare opening geometry before it is placed in a wall. It combines
 * polygon-building state with naming and material-link support.
 *
 * The interface accepts primitives and vertex edits and exposes
 * validation and clearing operations. PolygonBuilder.h supplies the
 * shared construction machinery; A3dOpening.h declares the placed object
 * created from the builder.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _A3DOPENINGBUILDER_H
#define _A3DOPENINGBUILDER_H

#include "A3dPrivate.h"
#include "PolygonBuilder.h"
#include "NamedObject.h"
#include "MaterialLink.h"
#include "LinkList.h"

/* =============================================================
// Class: CA3dOpeningBuilder
//
// Description: Named polygon builder for opening geometry.
//
// Size: 0x19C
//
// (RE) Constructor: dbg:0x10078b50
// =============================================================*/

/* Polygon-base override table at 0x04: dbg:0x10139288. */

class CA3dOpeningBuilder : public IA3dOpeningBuilder,
			   public CA3dPolygonBuilder
{
public:
	CA3dOpeningBuilder(int nType);
	virtual ~CA3dOpeningBuilder(void);

	/* IA3dOpeningBuilder slots 0..19, in declaration order. */

	STDMETHODIMP         QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG) AddRef(void);
	STDMETHODIMP_(ULONG) Release(void);

	/* Primitive-input slots 3..10. */
	STDMETHODIMP       Begin(DWORD dwMode);
	STDMETHODIMP       Vertex3f(A3DVAL x, A3DVAL y, A3DVAL z);
	STDMETHODIMP       Vertex3fv(LPA3DVAL pv);
	STDMETHODIMP       End(void);
	STDMETHODIMP       RemovePrimitive(int nPrim);
	STDMETHODIMP_(int) GetPrimitiveCount(void);
	STDMETHODIMP       SetVertex(int nPrim, int nVertex,
	                             A3DVAL x, A3DVAL y, A3DVAL z);
	STDMETHODIMP GetVertex(int nPrim, int nVertex, LPA3DVAL pv);

	STDMETHODIMP Validate(void);
	STDMETHODIMP Clear(void);

	/* Persistence slots 13..17. */
	STDMETHODIMP Load(void *pv);
	STDMETHODIMP Save(void *pv);
	STDMETHODIMP UnSerialize(void *pv, UINT cb);
	STDMETHODIMP Serialize(void *pv, UINT cb);
	STDMETHODIMP Duplicate(void *pv);

	STDMETHODIMP SetName(LPCVOID pvName);
	STDMETHODIMP GetName(LPVOID pvName, int nSize);

protected:
	/* 0x00 */ /* IA3dOpeningBuilder vptr (no data) */
	/* 0x04 */ /* CA3dPolygonBuilder base subobject, +0x04..+0x94 */

	/* 0x94 */ CA3dNamed m_named;
	/* 0x198 */ LONG     m_cRef;
};

typedef int A3dOpeningBuilderSizeCheck[(sizeof(CA3dOpeningBuilder) == 0x19C) ? 1 : -1];

/* Default-name sequence, defined in Corners.cpp. */

extern DWORD g_cOpeningBuilders;

#endif /* _A3DOPENINGBUILDER_H */
