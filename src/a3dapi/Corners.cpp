/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * Corners.cpp
 *
 * Provides shared support for polygon builders and scene geometry. Its
 * edge-against-primitive test is used by boundary checks, and its default
 * frame vectors and object counters support geometry construction and
 * naming.
 *
 * This file also implements the builder-base destructor and the room and
 * wall builders' COM identity and reference counting. The remaining
 * construction and validation methods are in the individual builder
 * files.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "Corners.h"
#include "A3dRoomBuilder.h"
#include "A3dWall.h"
#include "A3dWallBuilder.h"
#include "LinkList.h"

/*
 * Per-class counters used by generated object names.
 */

DWORD g_cOpeningBuilders = 0;
DWORD g_cRoomBuilders = 0;
DWORD g_cWallBuilders = 0;
DWORD g_cScenes = 0;

/* =============================================================
// ~CA3dBuilderIface()
//
// Destroy the builder interface base.
// =============================================================*/

CA3dBuilderIface::~CA3dBuilderIface(void)
{
}

/* =============================================================
// QueryInterface()
// (RE) rtl:0x100322e0; dbg:0x1007c170
//
// Reject interface requests without writing the output.
//
// Returns:
//   E_INVALIDARG   a null output
//   E_NOINTERFACE  otherwise
// =============================================================*/

STDMETHODIMP
CA3dRoomBuilder::QueryInterface(REFIID riid, void **ppv)
{
	return (ppv ? E_NOINTERFACE : E_INVALIDARG);
}

/* =============================================================
// AddRef()
// (RE) rtl:0x1002fe40; dbg:0x1007c1a0
//
// Increment the room-builder reference count.
//
// Returns: The updated reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dRoomBuilder::AddRef(void)
{
	return (++m_cRef);
}

/* =============================================================
// Release()
// (RE) rtl:0x1002fe60; dbg:0x1007c1e0
//
// Release a reference and delete the room builder at zero. The destructor
// dispatch through the 0x04 subobject does not match the declared layout.
//
// Returns: The remaining reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dRoomBuilder::Release(void)
{
	if (--m_cRef)
		return (m_cRef);

	delete this;

	return (0);
}

/* =============================================================
// QueryInterface()
// (RE) rtl:0x100322e0; dbg:0x10084490
//
// Reject interface requests without writing the output.
//
// Returns:
//   E_INVALIDARG   a null output
//   E_NOINTERFACE  otherwise
// =============================================================*/

STDMETHODIMP
CA3dWallBuilder::QueryInterface(REFIID riid, void **ppv)
{
	return (ppv ? E_NOINTERFACE : E_INVALIDARG);
}

/* =============================================================
// AddRef()
// (RE) rtl:0x10034300; dbg:0x100844c0
//
// Increment the wall-builder reference count.
//
// Returns: The updated reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dWallBuilder::AddRef(void)
{
	return (++m_cRef);
}

/* =============================================================
// Release()
// (RE) rtl:0x10034320; dbg:0x10084500
//
// Release a reference and delete the wall builder at zero.
//
// Returns: The remaining reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dWallBuilder::Release(void)
{
	if (--m_cRef)
		return (m_cRef);

	delete this;

	return (0);
}

/*
 * Default frame: origin, +Z front and +Y up.
 */

const A3DVAL vDefaultPosition[4] = {0.0f, 0.0f, 0.0f, 1.0f};
const A3DVAL vDefaultFront[4] = {0.0f, 0.0f, 1.0f, 0.0f};
const A3DVAL vDefaultUp[4] = {0.0f, 1.0f, 0.0f, 0.0f};

/* =============================================================
// A3dEdgeAgainstPrim()
//
// Test a primitive edge for containment. Preserve the original discarded
// results, which leave callers without an observable answer.
// =============================================================*/

void
A3dEdgeAgainstPrim(const A3DPRIMITIVE *pcPrim, int iA, int iB,
				   const A3DEDGE *pcEdge)
{
A3DEDGE edgePrim;
	A3DVAL dx, dy, dz;
	BOOL fFirstIn;
	BOOL fSecondIn;

	edgePrim.avEnd[0][0] = pcPrim->av[iA][0];
	edgePrim.avEnd[0][1] = pcPrim->av[iA][1];
	edgePrim.avEnd[0][2] = pcPrim->av[iA][2];

	edgePrim.avEnd[1][0] = pcPrim->av[iB][0];
	edgePrim.avEnd[1][1] = pcPrim->av[iB][1];
	edgePrim.avEnd[1][2] = pcPrim->av[iB][2];

	dx = pcEdge->avEnd[1][0] - edgePrim.avEnd[1][0];
	dy = pcEdge->avEnd[1][1] - edgePrim.avEnd[1][1];
	dz = pcEdge->avEnd[1][2] - edgePrim.avEnd[1][2];

	if (dx * dx + dy * dy + dz * dz < A3D_EPSILON_SQUARED)
		return;

	dx = pcEdge->avEnd[1][0] - edgePrim.avEnd[0][0];
	dy = pcEdge->avEnd[1][1] - edgePrim.avEnd[0][1];
	dz = pcEdge->avEnd[1][2] - edgePrim.avEnd[0][2];

	if (dx * dx + dy * dy + dz * dz < A3D_EPSILON_SQUARED)
		return;

	fFirstIn        = A3dPointWithinEdge(pcEdge->avEnd[0], edgePrim.avEnd[0]);
	fSecondIn       = A3dPointWithinEdge(pcEdge->avEnd[1], edgePrim.avEnd[0]);

	if (!fFirstIn || !fSecondIn)
	{
		A3dPointWithinEdge(edgePrim.avEnd[0], pcEdge->avEnd[0]);
		A3dPointWithinEdge(edgePrim.avEnd[1], pcEdge->avEnd[0]);
	}
}
