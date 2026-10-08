/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dList.h
 *
 * Declares CA3dList, the retained geometry object exposed through
 * IA3dList. It stores recorded geometry pools, material references,
 * recording state and the bounding volume used during tracing.
 *
 * The class works with CA3dRoot to begin and end recording and to replay
 * a list under the current transform. ChunkPage.h provides its pool
 * storage, while A3dList.cpp implements lifetime, playback and
 * bounding-volume tests.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _A3DLIST_H
#define _A3DLIST_H

#include "A3dPrivate.h"
#include "NamedObject.h"
#include "MaterialLink.h"
#include "ChunkPage.h"

class CA3dRoot;

/* =============================================================
// Class: CA3dList
//
// Description: Recorded geometry with optional bounding-box tracking.
//
// Size: 0x60
//
// (RE) Constructor: rtl:0x10008d60; dbg:0x100173c0
// The CA3dChained base at 0x04 is undeclared.
// =============================================================*/

/* (RE) IA3dList vtable: rtl:0x100522b8; dbg:0x10127d94.
 * Slots 0..6: QueryInterface, AddRef, Release, Begin, End, Call,
 * EnableBoundingVol. Destructor table at 0x04: dbg:0x10127d90, slot 0.
 * IA3dList base constructor: dbg:0x100175e0; vtable: dbg:0x10127dbc.
 */

class CA3dList : public IA3dList
{
public:
	friend class CA3dRoot;

	CA3dList(CA3dRoot *pApi);
	virtual ~CA3dList(void);

	STDMETHODIMP         QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG) AddRef(void);
	STDMETHODIMP_(ULONG) Release(void);

	STDMETHODIMP         Begin(void);

	CA3dPool            *GetPool(void) { return (m_pPool); }

	void                 RewindPool(void)
			     { if (m_pPool) m_pPool->m_cUsed = 0; }

	/* (RE) dbg:0x1000A5F0 */
	void                 ClearCalled(void) { m_bCalled = 0; }

	STDMETHODIMP         End(void);
	STDMETHODIMP         Call(void);
	STDMETHODIMP         EnableBoundingVol(void);

	BOOL                 TestBoundingVol(void);

	void                 UpdateBoundingVol(const struct _A3DPRIMITIVE
					       *pcPrim);

	HRESULT              BeginRecording(void);
	HRESULT              EndRecording(void);

protected:
	/* 0x04 */ BYTE      m_abUnknown_0x04[0x0C]; /* Unmodeled CA3dChained base. */

	/* 0x10 */ DWORD     m_cRef;
	/* 0x14 */ CA3dRoot *m_pApi;                 /* Borrowed root. */

	/* Owned geometry captured between Begin and End. */
	/* 0x18 */ CA3dPool *m_pPool;

	/* Pool position at Begin. */
	/* 0x1C */ CA3dPool *m_pPoolAtBegin;
	/* 0x20 */ int       m_cUsedAtBegin;

	/* Pool position at End. */
	/* 0x24 */ CA3dPool *m_pPoolAtEnd;
	/* 0x28 */ int       m_cUsedAtEnd;

	/* 0x2C */ DWORD     m_bCalled;              /* Set when submitted. */

	/* Walk sequence in which TraceWalk's entry retest already covered this
	 * list; the geometry walks skip its pools for the rest of that walk. */
	/* 0x30 */ DWORD     m_dwRetestedSeq;

	/* Bounding-box corners and number of contributing primitives. */
	/* 0x34 */ A3DVAL    m_avBoundMin[3];
	/* 0x40 */ A3DVAL    m_avBoundMax[3];
	/* 0x4C */ DWORD     m_cBoundUpdates;

	/* Register for updates while recording. */
	/* 0x50 */ DWORD     m_bBoundVolEnabled;

	/* 0x54 */ DWORD     m_bEnded;
	/* 0x58 */ DWORD     m_bFailed;

	/* Holds a root recording bracket. */
	/* 0x5C */ DWORD     m_dwRecording;
};

typedef int CA3dListSizeCheck[(sizeof(CA3dList) == 0x60) ? 1 : -1];

#endif /* _A3DLIST_H */
