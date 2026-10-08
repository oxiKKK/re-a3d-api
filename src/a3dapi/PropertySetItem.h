/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * PropertySetItem.h
 *
 * Declares CPropertySetItem and its record kinds for deferred and cached
 * property operations. Each record identifies a property set and
 * property, stores copied request data and tracks call results or
 * buffer-restore data.
 *
 * The resource manager and its buffers use these records to queue
 * operations and reapply saved properties after a DAL binding changes.
 * PropertySetItem.cpp implements data ownership and replay through
 * IKsPropertySet.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _PROPERTYSETITEM_H
#define _PROPERTYSETITEM_H

#include "A3dPrivate.h"

/* Record kinds.*/
#define A3D_PROPERTY_RECORD_EMPTY               0
#define A3D_PROPERTY_RECORD_QUERY_SUPPORT       1
#define A3D_PROPERTY_RECORD_GET                 2
#define A3D_PROPERTY_RECORD_SET                 3
#define A3D_PROPERTY_RECORD_RESTORE             4

/* =============================================================
// Class: CPropertySetItem
//
// Description: Owned copies of property-call and buffer-restore data.
//
// Size: 0x58
//
// (RE) Constructor: rtl:0x10019d50; dbg:0x1003d8c0
// =============================================================*/

/* Vtable at 0x00; slot 0 is the scalar deleting destructor. */

class CPropertySetItem
{
public:
	CPropertySetItem(void);
	virtual ~CPropertySetItem(void);

	BOOL	Create(REFGUID rguidPropertySet, ULONG ulId,
		       PULONG pulTypeSupport);
	BOOL	Get(REFGUID rguidPropertySet, ULONG ulId,
		    LPVOID lpvInstanceData, ULONG ulInstanceDataSize,
		    LPVOID lpvPropertyData, ULONG ulPropertyDataSize,
		    PULONG pulBytesReturned);

	BOOL	AddSet(REFGUID rguidPropertySet, ULONG ulId,
		       LPVOID lpvInstanceData, ULONG ulInstanceDataSize,
		       LPVOID lpvPropertyData, ULONG ulPropertyDataSize,
		       DWORD dwRetainAfterCall, DWORD dwBufferReplayEnabled);

	HRESULT	SaveBufferState(REFGUID rguidPropertySet, ULONG ulId,
				LPVOID lpvInstanceData, ULONG ulInstanceDataSize,
				LPVOID lpvPropertyData, ULONG ulPropertyDataSize);

	HRESULT	Apply(IKsPropertySet *lpPropertySet);

public:
	/* 0x04 */ BOOL		m_bCreated;		/* Record has been created. */
	/* 0x08 */ GUID		m_guidPropertySet;	/* Property-set GUID. */
	/* 0x18 */ DWORD	m_ulId;			/* Property identifier. */
	/* 0x1C */ ULONG	m_ulTypeSupport;	/* the QuerySupport result value */
	/* 0x20 */ LPVOID	m_pInstanceData;	/* Owned instance request copy. */
	/* 0x24 */ ULONG	m_cbInstanceData;	/* Request length in bytes, also used by Apply. */
	/* 0x28 */ LPVOID	m_pPropertyData;	/* Owned property request copy. */
	/* 0x2C */ ULONG	m_cbPropertyData;	/* Request length in bytes, also used by Apply. */
	/* 0x30 */ ULONG	m_cbBytesReturned;	/* the bytes-returned value */
	/* 0x34 */ HRESULT	m_hrCall;		/* Replayed call result; E_FAIL before replay. */
	/* 0x38 */ SHORT		m_nCallType;		/* 1: QuerySupport, 2: Get, 3: Set, 4: restore. */
	/* 0x3C */ DWORD	m_dwRetainAfterCall;		/* Nonzero retains the processed record */
	/* (RE) Enables Apply and retains the record during buffer replay
	 * (dbg:0x100691a0); does not establish valid restore copies. */
	/* 0x40 */ DWORD	m_dwBufferReplayEnabled;
	/* 0x44 */ DWORD	m_dwApplied;		/* Whether the last Apply called Set. */
	/* 0x48 */ LPVOID	m_pInstanceRestoreData;	/* Owned instance restore copy. */
	/* 0x4C */ ULONG	m_cbInstanceRestoreData;/* Restore length in bytes. */
	/* 0x50 */ LPVOID	m_pPropertyRestoreData;	/* Owned property restore copy. */
	/* 0x54 */ ULONG	m_cbPropertyRestoreData;/* Restore length in bytes. */
};

BOOL	__cdecl A3dIsGuidEqual(const void *pGuid1, const void *pGuid2);

#endif /* _PROPERTYSETITEM_H */
