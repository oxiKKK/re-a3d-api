/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dList.cpp
 *
 * Implements IA3dList for geometry that an application records once and
 * reuses. Begin and End redirect root geometry submission into list-owned
 * pools; Call submits the recorded content for rendering.
 *
 * The list retains its material links and maintains a bounding volume
 * used to reject paths that cannot intersect its geometry. Destruction
 * releases recorded storage and clears source references to the list.
 * A3dGeom.cpp supplies the root-side recording and traversal operations.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "A3dList.h"
#include "A3d3.h"
#include "ChunkPage.h"
#include "Corners.h"


#define A3D_LIST_BOUND_INITIAL_EXTENT 1e38f
#define A3D_LIST_BOUND_MIN_PRIMITIVES 5
#define A3D_BOX_SIDE_ABOVE_MAX 0
#define A3D_BOX_SIDE_BELOW_MIN 1
#define A3D_BOX_SIDE_INSIDE 2

/* -------------------------------------------------------------------------- */

/* =============================================================
// CA3dList()
// (RE) rtl:0x10008d60; dbg:0x100173c0
//
// Initialize the geometry list and bounding box.
// =============================================================*/

CA3dList::CA3dList(CA3dRoot *pApi)
{
	m_cRef  = 0;
	m_pApi  = pApi;
	m_pPool = NULL;

	m_pPoolAtBegin = NULL;

	m_bCalled      = 0;
	m_dwRetestedSeq = 0;

	m_avBoundMin[0] = m_avBoundMin[1] = m_avBoundMin[2] =
		A3D_LIST_BOUND_INITIAL_EXTENT;
	m_avBoundMax[0] = m_avBoundMax[1] = m_avBoundMax[2] =
		-A3D_LIST_BOUND_INITIAL_EXTENT;
	m_cBoundUpdates = 0;

	m_bBoundVolEnabled      = 0;
	m_bEnded                = 0;
	m_bFailed               = 0;
	m_dwRecording           = 0;
}

/* =============================================================
// CA3dList scalar deleting destructor
// (RE) rtl:0x10008e10; dbg:0x10017590
// =============================================================*/

/* =============================================================
// ~CA3dList()
// (RE) rtl:0x10008ee0; dbg:0x10017610
//
// Close recording, unbind the list from sources and free its pool.
// =============================================================*/

CA3dList::~CA3dList(void)
{
	if (m_dwRecording)
		EndRecording();

	m_pApi->UnbindListFromSources(this);

	if (m_pPool)
		delete m_pPool;
}

/* =============================================================
// QueryInterface()
// (RE) rtl:0x10008ef0; dbg:0x10017720
//
// Return IUnknown or IA3dList. Original defect: an unsupported IID leaves
// *ppv unchanged and AddRefs any nonnull value already there.
//
// Returns:
//   S_OK           if *ppv is nonnull
//   E_NOINTERFACE  otherwise
//   E_INVALIDARG   if ppv is null
// =============================================================*/

STDMETHODIMP
CA3dList::QueryInterface(REFIID riid, void **ppv)
{
	if (!ppv)
		return (E_INVALIDARG);

	if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IA3dList))
		*ppv = (IA3dList *) this;

	if (!*ppv)
		return (E_NOINTERFACE);

	((IUnknown *) *ppv)->AddRef();

	return (S_OK);
}

/* =============================================================
// AddRef()
// (RE) rtl:0x10008f50; dbg:0x100177b0
//
// Add a reference to the list.
//
// Returns: The new reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dList::AddRef(void)
{
	return (++m_cRef);
}

/* =============================================================
// Release()
// (RE) rtl:0x10008f70; dbg:0x100177e0
//
// Release a reference and delete the list at zero.
//
// Returns: The remaining reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dList::Release(void)
{
	if (--m_cRef)
		return (m_cRef);

	delete this;

	return (0);
}

/* =============================================================
// FAILED compiler-generated helper
// (RE) dbg:0x10017940
// =============================================================*/

/* =============================================================
// Begin()
// (RE) rtl:0x10008fa0; dbg:0x10017860
//
// Begin recording geometry and mark the current pool position.
//
// Returns:
//   S_OK
//   A3DERROR_CLOSED_LIST_CANNOT_BE_CHANGED
//                                       if closed
//   E_FAIL                              if recording cannot begin or the list
//                                       has failed
// =============================================================*/

STDMETHODIMP
CA3dList::Begin(void)
{
	if (m_pPool)
		return (A3DERROR_CLOSED_LIST_CANNOT_BE_CHANGED);

	if (m_bEnded)
		return (A3DERROR_CLOSED_LIST_CANNOT_BE_CHANGED);

	if (m_bFailed)
		return (E_FAIL);

	if (FAILED(BeginRecording()))
	{
		m_bFailed = 1;

		return (E_FAIL);
	}

	m_pPoolAtBegin = m_pApi->GetPool();
	m_cUsedAtBegin = m_pPoolAtBegin->m_cUsed;

	if (m_bBoundVolEnabled)
		m_pApi->m_OpenLists.push_back(this);

	return (S_OK);
}

/* =============================================================
// End()
// (RE) rtl:0x10009070; dbg:0x10017960
//
// Capture the recorded geometry. Original defects: intermediate pools are
// counted without copying, and pool indices are compared with element counts.
//
// Returns: The captured block count; A3DERROR_END_CALLED_BEFORE_BEGIN;
//          A3DERROR_CLOSED_LIST_CANNOT_BE_CHANGED if ended; E_FAIL on failure.
// =============================================================*/

STDMETHODIMP
CA3dList::End(void)
{
CA3dPool       *pPool;
CA3dPool       *pMiddle;
int             cPerPool;
int             cBlocks;
int nBegin, nEnd;
int nSpan;
int nPools;
int i;
CA3dStdList<CA3dPool *>::iterator it;

	if (!m_pPoolAtBegin)
		return (A3DERROR_END_CALLED_BEFORE_BEGIN);

	if (m_bEnded)
		return (A3DERROR_CLOSED_LIST_CANNOT_BE_CHANGED);

	if (m_bFailed)
		return (E_FAIL);

	m_pPoolAtEnd = m_pApi->GetPool();
	m_cUsedAtEnd = m_pPoolAtEnd->m_cUsed;

	cPerPool = m_pApi->GetPoolElements();

	nSpan = 0;

	if (m_pPoolAtBegin == m_pPoolAtEnd)
	{
		cBlocks = m_cUsedAtEnd - m_cUsedAtBegin;
	}
	else
	{
		nBegin = m_pApi->FindGeom(m_pPoolAtBegin);
		nEnd   = m_pApi->FindGeom(m_pPoolAtEnd);

		nSpan = nEnd - nBegin;

		cBlocks = cPerPool - m_cUsedAtBegin;

		if (nSpan > 1)
			cBlocks += cPerPool * (nSpan - 1);

		cBlocks += m_cUsedAtEnd;
	}

	if (cBlocks <= 0)
	{
		m_bFailed = 1;

		EndRecording();

		return (E_FAIL);
	}

	pPool = new CA3dPool(cBlocks, (DWORD) this);

	m_pPool = pPool;

	if (!pPool)
	{
		m_bFailed = 1;

		EndRecording();

		return (E_FAIL);
	}

	if (pPool->m_nChunks != cBlocks)
	{
		m_bFailed = 1;

		EndRecording();

		return (E_FAIL);
	}

	if (m_pPoolAtBegin == m_pPoolAtEnd)
	{
		pPool->m_cBlocks = pPool->CopyFrom(0, m_pPoolAtBegin,
						   m_cUsedAtBegin, cBlocks);
	}
	else
	{
		pPool->m_cBlocks = pPool->CopyFrom(0, m_pPoolAtBegin,
						   m_cUsedAtBegin,
						   cPerPool - m_cUsedAtBegin);

		if (nSpan > 1)
		{
			nPools = m_pApi->m_ScratchPools.size();

			i = 0;

			for (it = m_pApi->m_ScratchPools.begin();
			     it != m_pApi->m_ScratchPools.end();
			     ++it)
			{
				pMiddle = *it;

				if (i > m_cUsedAtBegin && i < m_cUsedAtEnd - 1)
					pPool->m_cBlocks +=
						pPool->CopyAllocatedFrom(pPool->m_cBlocks,
								 pMiddle);

				i++;
			}
		}

		pPool->m_cBlocks += pPool->CopyFrom(pPool->m_cBlocks,
						    m_pPoolAtEnd, 0, m_cUsedAtEnd);
	}

	if (m_bBoundVolEnabled)
		m_pApi->m_OpenLists.remove(this);

	m_bEnded = 1;

	EndRecording();

	return ((HRESULT) pPool->m_cBlocks);
}

/* =============================================================
// BeginRecording()
// (RE) dbg:0x10017E60
//
// Open the root recording bracket for this list.
//
// Returns:
//   The root result
//   E_FAIL           if already recording
// =============================================================*/

HRESULT
CA3dList::BeginRecording(void)
{
	if (m_dwRecording)
		return (E_FAIL);

	m_dwRecording = 1;

	return (m_pApi->BeginListRecording());
}

/* =============================================================
// EndRecording()
// (RE) dbg:0x10017EB0
//
// Close the root recording bracket for this list.
//
// Returns:
//   The root result
//   E_FAIL           if not recording
// =============================================================*/

HRESULT
CA3dList::EndRecording(void)
{
	if (!m_dwRecording)
		return (E_FAIL);

	m_dwRecording = 0;

	return (m_pApi->EndListRecording());
}

/* =============================================================
// Call()
// (RE) rtl:0x10009340; dbg:0x10017f00
//
// Submit the captured pool and retain the list without guarding active root
// list recording.
//
// Returns: The block count; S_OK if not ended, failed or empty.
// =============================================================*/

STDMETHODIMP
CA3dList::Call(void)
{
	if (!m_bEnded)
		return (S_OK);

	if (m_bFailed)
		return (S_OK);

	if (!m_pPool->m_cBlocks)
		return (S_OK);

	m_pApi->AddGeom(m_pPool);

	m_bCalled = 1;

	AddRef();

	return ((HRESULT) m_pPool->m_cBlocks);
}

/* =============================================================
// UpdateBoundingVol()
// (RE) dbg:0x10017FA0; thunk dbg:0x100041FB
//
// Expand the bounding box to include a primitive.
// =============================================================*/

void
CA3dList::UpdateBoundingVol(const A3DPRIMITIVE *pcPrim)
{
int	i;

	for (i = 0; i < pcPrim->cVertices; i++)
	{
		if (pcPrim->av[i][0] < m_avBoundMin[0])
			m_avBoundMin[0] = pcPrim->av[i][0];

		if (pcPrim->av[i][1] < m_avBoundMin[1])
			m_avBoundMin[1] = pcPrim->av[i][1];

		if (pcPrim->av[i][2] < m_avBoundMin[2])
			m_avBoundMin[2] = pcPrim->av[i][2];

		if (pcPrim->av[i][0] > m_avBoundMax[0])
			m_avBoundMax[0] = pcPrim->av[i][0];

		if (pcPrim->av[i][1] > m_avBoundMax[1])
			m_avBoundMax[1] = pcPrim->av[i][1];

		if (pcPrim->av[i][2] > m_avBoundMax[2])
			m_avBoundMax[2] = pcPrim->av[i][2];
	}

	m_cBoundUpdates++;
}

/* =============================================================
// A3dSegmentHitsBox()
// (RE) dbg:0x100181C0; thunk dbg:0x100033FF
//
// Test whether the segment from pvFrom to pvFrom + pvDir reaches the box.
//
// Returns:
//   TRUE   if the segment reaches the box
//   FALSE  otherwise
// =============================================================*/

BOOL
A3dSegmentHitsBox(const A3DVAL *pvMin, const A3DVAL *pvMax,
		  const A3DVAL *pvFrom, const A3DVAL *pvDir)
{
A3DVAL	avCandidate[3];
A3DVAL	afEntry[3];
A3DVAL	avHit[3];
A3DVAL	dx, dy, dz;
int	anSide[3];
int	fInside;
int	nEntry;
int	i;

	fInside = TRUE;

	for (i = 0; i < 3; i++)
	{
		if (pvFrom[i] < pvMin[i])
		{
			anSide[i]      = A3D_BOX_SIDE_BELOW_MIN;
			avCandidate[i] = pvMin[i];
			fInside        = FALSE;
		}
		else if (pvFrom[i] > pvMax[i])
		{
			anSide[i]      = A3D_BOX_SIDE_ABOVE_MAX;
			avCandidate[i] = pvMax[i];
			fInside        = FALSE;
		}
		else
		{
			anSide[i] = A3D_BOX_SIDE_INSIDE;
		}
	}

	if (fInside)
		return (TRUE);

	for (i = 0; i < 3; i++)
	{
		if (anSide[i] == A3D_BOX_SIDE_INSIDE || pvDir[i] == 0.0f)
			afEntry[i] = -1.0f;
		else
			afEntry[i] = (avCandidate[i] - pvFrom[i]) / pvDir[i];
	}

	nEntry = 0;

	for (i = 1; i < 3; i++)
		if (afEntry[nEntry] < afEntry[i])
			nEntry = i;

	if (afEntry[nEntry] < 0.0f)
		return (FALSE);

	for (i = 0; i < 3; i++)
	{
		if (i == nEntry)
		{
			avHit[i] = avCandidate[i];
		}
		else
		{
			avHit[i] = afEntry[nEntry] * pvDir[i] + pvFrom[i];

			if (avHit[i] < pvMin[i] || avHit[i] > pvMax[i])
				return (FALSE);
		}
	}

	dx = avHit[0] - pvFrom[0];
	dy = avHit[1] - pvFrom[1];
	dz = avHit[2] - pvFrom[2];

	return (pvDir[0] * pvDir[0] + pvDir[1] * pvDir[1] + pvDir[2] * pvDir[2] >=
		dx * dx + dy * dy + dz * dz);
}

/* =============================================================
// CA3dList::TestBoundingVol()
// (RE) dbg:0x10018150; thunk dbg:0x1000172B
//
// Test the listener segment against the list bounding box.
//
// Returns:
//   TRUE   if fewer than five primitives contributed or the segment reaches the
//          box
//   FALSE  otherwise
// =============================================================*/

BOOL
CA3dList::TestBoundingVol(void)
{
	if (m_cBoundUpdates < A3D_LIST_BOUND_MIN_PRIMITIVES)
		return (TRUE);

	return (A3dSegmentHitsBox(m_avBoundMin, m_avBoundMax,
				  m_pApi->m_vListenerPos,
				  m_pApi->m_vToListener));
}

/* =============================================================
// EnableBoundingVol()
// (RE) rtl:0x10009650; dbg:0x100184e0
//
// Enable bounding-box tracking before recording begins.
//
// Returns:
//   S_OK
//   A3DERROR_BBOX_CANNOT_ENABLE_AFTER_BEGIN_LIST_CALL
//                                       if Begin has marked a pool
// =============================================================*/

STDMETHODIMP
CA3dList::EnableBoundingVol(void)
{
	if (m_pPoolAtBegin)
		return (A3DERROR_BBOX_CANNOT_ENABLE_AFTER_BEGIN_LIST_CALL);

	m_bBoundVolEnabled = 1;

	return (S_OK);
}
