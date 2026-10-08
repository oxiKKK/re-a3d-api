/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dOpeningBuilder.cpp
 *
 * Implements the editable polygon input for a wall opening. It exposes
 * the private opening-builder interface, stores an object name and
 * forwards primitive submission, vertex editing and validation to
 * CA3dPolygonBuilder.
 *
 * Validated input is used to construct CA3dOpening instances when
 * openings are added to walls. Persistence and duplication entry points
 * are present but reject those operations; this file does not provide a
 * saved opening format.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "A3dOpeningBuilder.h"
#include "Corners.h"

/* =============================================================
// CA3dOpeningBuilder()
// (RE) rtl:0x1002eab0; dbg:0x10078b50
//
// Initialize an opening builder and its default name.
// =============================================================*/

CA3dOpeningBuilder::CA3dOpeningBuilder(int nType) : CA3dPolygonBuilder(nType)
{
	m_cRef = 0;

	m_named.SetName("OpeningBuilder", ++g_cOpeningBuilders);
}

/* =============================================================
// QueryInterface()
//
// Reject every interface query without changing the output.
//
// Returns:
//   E_INVALIDARG   a null output
//   E_NOINTERFACE  otherwise
// =============================================================*/

STDMETHODIMP
CA3dOpeningBuilder::QueryInterface(REFIID riid, void **ppv)
{
	return (ppv ? E_NOINTERFACE : E_INVALIDARG);
}

/* =============================================================
// AddRef()
//
// Increment the reference count.
//
// Returns: The updated reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dOpeningBuilder::AddRef(void)
{
	return (++m_cRef);
}

/* =============================================================
// Release()
//
// Release a reference and delete the builder at zero.
//
// Returns: The remaining reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dOpeningBuilder::Release(void)
{
	if (--m_cRef)
		return (m_cRef);

	delete this;

	return (0);
}

/* =============================================================
// SetName()
//
// Copy the supplied fixed-size name buffer.
//
// Returns: The SetNameBuffer result.
// =============================================================*/

STDMETHODIMP
CA3dOpeningBuilder::SetName(LPCVOID pvName)
{
	return (m_named.SetNameBuffer(pvName));
}

/* =============================================================
// GetName()
//
// Copy the stored fixed-size name buffer.
//
// Returns: The GetNameBuffer result.
// =============================================================*/

STDMETHODIMP
CA3dOpeningBuilder::GetName(LPVOID pvName, int nSize)
{
	return (m_named.GetNameBuffer(pvName, nSize));
}

/* =============================================================
// ~CA3dOpeningBuilder()
//
// Destroy the builder, its name and its polygon state.
// =============================================================*/

CA3dOpeningBuilder::~CA3dOpeningBuilder(void)
{
}

/* =============================================================
// Begin()
//
// Begin a polygon primitive.
//
// Returns: The polygon builder Begin result.
// =============================================================*/

STDMETHODIMP
CA3dOpeningBuilder::Begin(DWORD dwMode)
{
	return (CA3dPolygonBuilder::Begin(dwMode));
}

/* =============================================================
// Vertex3f()
//
// Append a vertex from three scalar components.
//
// Returns: The polygon builder Vertex3f result.
// =============================================================*/

STDMETHODIMP
CA3dOpeningBuilder::Vertex3f(A3DVAL x, A3DVAL y, A3DVAL z)
{
	return (CA3dPolygonBuilder::Vertex3f(x, y, z));
}

/* =============================================================
// Vertex3fv()
//
// Append a vertex from a vector.
//
// Returns: The polygon builder Vertex3fv result.
// =============================================================*/

STDMETHODIMP
CA3dOpeningBuilder::Vertex3fv(LPA3DVAL pv)
{
	return (CA3dPolygonBuilder::Vertex3fv(pv));
}

/* =============================================================
// End()
//
// Finish the current polygon primitive.
//
// Returns: The polygon builder End result.
// =============================================================*/

STDMETHODIMP
CA3dOpeningBuilder::End(void)
{
	return (CA3dPolygonBuilder::End());
}

/* =============================================================
// RemovePrimitive()
//
// Remove the selected polygon primitive.
//
// Returns: The polygon builder RemovePrimitive result.
// =============================================================*/

STDMETHODIMP
CA3dOpeningBuilder::RemovePrimitive(int nPrim)
{
	return (CA3dPolygonBuilder::RemovePrimitive(nPrim));
}

/* =============================================================
// GetPrimitiveCount()
//
// Read the number of stored polygon primitives.
//
// Returns: The polygon builder primitive count.
// =============================================================*/

STDMETHODIMP_(int)
CA3dOpeningBuilder::GetPrimitiveCount(void)
{
	return (CA3dPolygonBuilder::GetPrimitiveCount());
}

/* =============================================================
// SetVertex()
//
// Replace a vertex in the selected primitive.
//
// Returns: The polygon builder SetVertex result.
// =============================================================*/

STDMETHODIMP
CA3dOpeningBuilder::SetVertex(int nPrim, int nVertex, A3DVAL x, A3DVAL y,
							  A3DVAL z)
{
	return (CA3dPolygonBuilder::SetVertex(nPrim, nVertex, x, y, z));
}

/* =============================================================
// GetVertex()
//
// Read a vertex from the selected primitive.
//
// Returns: The polygon builder GetVertex result.
// =============================================================*/

STDMETHODIMP
CA3dOpeningBuilder::GetVertex(int nPrim, int nVertex, LPA3DVAL pv)
{
	return (CA3dPolygonBuilder::GetVertex(nPrim, nVertex, pv));
}

/* =============================================================
// Validate()
//
// Validate the polygon primitive state.
//
// Returns: The CheckPrimitives result.
// =============================================================*/

STDMETHODIMP
CA3dOpeningBuilder::Validate(void)
{
	return (CheckPrimitives());
}

/* =============================================================
// Clear()
//
// Clear primitive, edge and plane state.
//
// Returns: The polygon builder Clear result.
// =============================================================*/

STDMETHODIMP
CA3dOpeningBuilder::Clear(void)
{
	return (CA3dPolygonBuilder::Clear());
}

/* =============================================================
// Load()
//
// Reject opening-builder loading.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dOpeningBuilder::Load(void *pv)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// Save()
//
// Reject opening-builder saving.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dOpeningBuilder::Save(void *pv)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// UnSerialize()
//
// Reject opening-builder deserialization.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dOpeningBuilder::UnSerialize(void *pv, UINT cb)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// Serialize()
//
// Reject opening-builder serialization.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dOpeningBuilder::Serialize(void *pv, UINT cb)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// Duplicate()
//
// Reject opening-builder duplication.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dOpeningBuilder::Duplicate(void *pv)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}
