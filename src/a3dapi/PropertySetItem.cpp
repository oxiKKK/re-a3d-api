/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * PropertySetItem.cpp
 *
 * Implements saved property-call records for the resource manager.
 * CPropertySetItem copies request data for support queries, reads and
 * writes so calls can be queued or retained independently of the original
 * input buffers.
 *
 * Separate restore copies allow buffer properties to be replayed through
 * IKsPropertySet when a voice receives a new playback buffer. resman.cpp
 * and rmbuffer.cpp coordinate deferred calls; this file manages the
 * stored data and replay operation.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "PropertySetItem.h"

#include <string.h>

/* =============================================================
// CPropertySetItem()
// (RE) rtl:0x10019D50; dbg:0x1003D8C0
//
// Initialize an empty property-call record.
// =============================================================*/

CPropertySetItem::CPropertySetItem(void)
{
	m_bCreated	= 0;

	memset(&m_guidPropertySet, 0, sizeof(m_guidPropertySet));
	m_ulId                  = 0;
	m_ulTypeSupport         = 0;
	m_pInstanceData         = NULL;
	m_cbInstanceData        = 0;
	m_pPropertyData         = NULL;
	m_cbPropertyData        = 0;
	m_cbBytesReturned       = 0;
	m_hrCall                = E_FAIL;
	m_nCallType             = A3D_PROPERTY_RECORD_EMPTY;
	m_dwRetainAfterCall     = 1;
	m_dwBufferReplayEnabled = 0;
	m_dwApplied             = 0;
	m_pInstanceRestoreData  = NULL;
	m_cbInstanceRestoreData = 0;
	m_pPropertyRestoreData  = NULL;
	m_cbPropertyRestoreData = 0;
}

/* =============================================================
// CPropertySetItem::~CPropertySetItem() scalar deleting destructor
// (RE) rtl:0x10019DC0; dbg:0x1003D9F0
// =============================================================*/

/* =============================================================
// ~CPropertySetItem()
// (RE) dbg:0x1003DA40
//
// Free the request and restore data copies.
// =============================================================*/

CPropertySetItem::~CPropertySetItem(void)
{
	if (m_pInstanceData)
		operator delete(m_pInstanceData);

	if (m_pPropertyData)
		operator delete(m_pPropertyData);

	if (m_pInstanceRestoreData)
		operator delete(m_pInstanceRestoreData);

	if (m_pPropertyRestoreData)
		operator delete(m_pPropertyRestoreData);
}

/* =============================================================
// Create()
// (RE) rtl:0x10019E30; dbg:0x1003DB00
//
// Record a QuerySupport call and its support value.
//
// Returns: TRUE.
// =============================================================*/

BOOL
CPropertySetItem::Create(REFGUID rguidPropertySet, ULONG ulId,
			 PULONG pulTypeSupport)
{
	ASSERT(!IsEqualGUID(rguidPropertySet, GUID_NULL));
	ASSERT((pulTypeSupport != 0 &&
	       !IsBadReadPtr(pulTypeSupport, sizeof(ULONG))));
	ASSERT(!m_bCreated);

	m_bCreated	= 1;
	m_nCallType	= A3D_PROPERTY_RECORD_QUERY_SUPPORT;

	memcpy(&m_guidPropertySet, &rguidPropertySet, sizeof(GUID));
	m_ulId		= ulId;
	m_ulTypeSupport	= *pulTypeSupport;

	return (TRUE);
}

/* =============================================================
// Get()
// (RE) rtl:0x10019E70; dbg:0x1003DC60
//
// Record a Get call with copies of its instance and property data.
//
// Returns:
//   TRUE
//   FALSE  if a data-copy allocation fails
// =============================================================*/

BOOL
CPropertySetItem::Get(REFGUID rguidPropertySet, ULONG ulId,
		      LPVOID lpvInstanceData, ULONG ulInstanceDataSize,
		      LPVOID lpvPropertyData, ULONG ulPropertyDataSize,
		      PULONG pulBytesReturned)
{
	ASSERT(!IsEqualGUID(rguidPropertySet, GUID_NULL));
	ASSERT(IsBadReadPtr(lpvInstanceData, ulInstanceDataSize) == 0);
	ASSERT(IsBadReadPtr(lpvPropertyData, ulPropertyDataSize) == 0);
	ASSERT((pulBytesReturned != 0 &&
	       !IsBadReadPtr(pulBytesReturned, sizeof(ULONG))));
	ASSERT(!m_bCreated);

	m_bCreated	= 1;
	m_nCallType	= A3D_PROPERTY_RECORD_GET;

	memcpy(&m_guidPropertySet, &rguidPropertySet, sizeof(GUID));
	m_ulId			= ulId;
	m_cbInstanceData	= ulInstanceDataSize;
	m_cbPropertyData	= ulPropertyDataSize;
	m_cbBytesReturned	= *pulBytesReturned;

	if (m_cbInstanceData)
	{
		m_pInstanceData = operator new(m_cbInstanceData);
		if (!m_pInstanceData)
		{
			DBGSTR("CPropertySetItem::Create() - Could not allocated memory for Instance Data.\n");
			return (FALSE);
		}

		memcpy(m_pInstanceData, lpvInstanceData, m_cbInstanceData);
	}

	if (m_cbPropertyData)
	{
		m_pPropertyData = operator new(m_cbPropertyData);
		if (!m_pPropertyData)
		{
			DBGSTR("CPropertySetItem::Get() - Could not allocated memory for Property Data.\n");
			return (FALSE);
		}

		memcpy(m_pPropertyData, lpvPropertyData, m_cbPropertyData);
	}

	return (TRUE);
}

/* =============================================================
// AddSet()
// (RE) rtl:0x10019F20; dbg:0x1003DF50
//
// Record a Set call with data copies, flags and buffer-replay state.
//
// Returns:
//   TRUE
//   FALSE  if a data-copy allocation fails
// =============================================================*/

BOOL
CPropertySetItem::AddSet(REFGUID rguidPropertySet, ULONG ulId,
			 LPVOID lpvInstanceData, ULONG ulInstanceDataSize,
			 LPVOID lpvPropertyData, ULONG ulPropertyDataSize,
			 DWORD dwRetainAfterCall, DWORD dwBufferReplayEnabled)
{
	ASSERT(!IsEqualGUID(rguidPropertySet, GUID_NULL));
	ASSERT(IsBadReadPtr(lpvInstanceData, ulInstanceDataSize) == 0);
	ASSERT(IsBadReadPtr(lpvPropertyData, ulPropertyDataSize) == 0);
	ASSERT(!m_bCreated);

	m_bCreated              = 1;
	m_nCallType             = A3D_PROPERTY_RECORD_SET;
	m_dwRetainAfterCall     = dwRetainAfterCall;
	m_dwBufferReplayEnabled = dwBufferReplayEnabled;

	memcpy(&m_guidPropertySet, &rguidPropertySet, sizeof(GUID));
	m_ulId			= ulId;
	m_cbInstanceData	= ulInstanceDataSize;
	m_cbPropertyData	= ulPropertyDataSize;

	if (m_cbInstanceData)
	{
		m_pInstanceData = operator new(m_cbInstanceData);
		if (!m_pInstanceData)
		{
			DBGSTR("CPropertySetItem::Create() - Could not allocated memory for Instance Data.\n");
			return (FALSE);
		}

		memcpy(m_pInstanceData, lpvInstanceData, m_cbInstanceData);
	}

	if (m_cbPropertyData)
	{
		m_pPropertyData = operator new(m_cbPropertyData);
		if (!m_pPropertyData)
		{
			DBGSTR("CPropertySetItem::AddSet() - Could not allocated memory for Property Data.\n");
			return (FALSE);
		}

		memcpy(m_pPropertyData, lpvPropertyData, m_cbPropertyData);
	}

	return (TRUE);
}

/* =============================================================
// SaveBufferState()
// (RE) rtl:0x10019FE0; dbg:0x1003E1F0
//
// Copy buffer restore data and enable replay. Preserve the partial-copy
// leak on retry after property-data allocation failure.
//
// Returns:
//   S_OK
//   E_ABORT        if buffer replay is already enabled
//   E_OUTOFMEMORY  if a restore-copy allocation fails
// =============================================================*/

HRESULT
CPropertySetItem::SaveBufferState(REFGUID rguidPropertySet, ULONG ulId,
				  LPVOID lpvInstanceData, ULONG ulInstanceDataSize,
				  LPVOID lpvPropertyData, ULONG ulPropertyDataSize)
{
	ASSERT(!IsEqualGUID(rguidPropertySet, GUID_NULL));
	ASSERT(IsBadReadPtr(lpvInstanceData, ulInstanceDataSize) == 0);
	ASSERT(IsBadReadPtr(lpvPropertyData, ulPropertyDataSize) == 0);

	if (m_dwBufferReplayEnabled)
	{
		DBGSTR("CPropertySetItem::SaveBufferState() - You have already made this call.\n");
		return (E_ABORT);
	}

	m_cbInstanceRestoreData	= ulInstanceDataSize;
	m_cbPropertyRestoreData	= ulPropertyDataSize;

	if (m_cbInstanceRestoreData)
	{
		m_pInstanceRestoreData = operator new(m_cbInstanceRestoreData);
		if (!m_pInstanceRestoreData)
		{
			DBGSTR("CPropertySetItem::SaveBufferState() - Could not allocated memory for Instance Restore Data.\n");
			return (E_OUTOFMEMORY);
		}

		memcpy(m_pInstanceRestoreData, lpvInstanceData, m_cbInstanceRestoreData);
	}

	if (m_cbPropertyRestoreData)
	{
		m_pPropertyRestoreData = operator new(m_cbPropertyRestoreData);
		if (!m_pPropertyRestoreData)
		{
			DBGSTR("CPropertySetItem::SaveBufferState() - Could not allocated memory for Property Data.\n");
			return (E_OUTOFMEMORY);
		}

		memcpy(m_pPropertyRestoreData, lpvPropertyData, m_cbPropertyRestoreData);
	}

	m_dwBufferReplayEnabled = 1;
	m_dwRetainAfterCall     = 1;
	m_nCallType             = A3D_PROPERTY_RECORD_RESTORE;

	return (S_OK);
}

/* =============================================================
// A3dIsGuidEqual()
// (RE) dbg:0x1003e440; thunk dbg:0x100043c2
//
// Compare two GUID-sized buffers.
//
// Returns:
//   TRUE   when equal
//   FALSE  otherwise
// =============================================================*/

BOOL
__cdecl A3dIsGuidEqual(const void *pGuid1, const void *pGuid2)
{
	return (memcmp(pGuid1, pGuid2, sizeof(GUID)) == 0);
}

/* =============================================================
// Apply()
// (RE) rtl:0x1001A090; dbg:0x1003e470; thunk dbg:0x10003f3f
//
// Replay restore copies through IKsPropertySet::Set when enabled. Preserve
// the use of request lengths; equality with restore lengths is unestablished.
//
// Returns: The property-set Set result; S_OK if buffer replay is disabled.
// =============================================================*/

HRESULT
CPropertySetItem::Apply(IKsPropertySet *lpPropertySet)
{
	ASSERT((lpPropertySet != 0 &&
	       !IsBadReadPtr(lpPropertySet, sizeof(IKsPropertySet))));

	if (m_dwBufferReplayEnabled)
	{
		m_dwApplied = 1;

		return (lpPropertySet->Set(m_guidPropertySet, m_ulId,
					   m_pInstanceRestoreData, m_cbInstanceData,
					   m_pPropertyRestoreData, m_cbPropertyData));
	}
	else
	{
		m_dwApplied = 0;

		return (S_OK);
	}
}
