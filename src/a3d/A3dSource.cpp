/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dSource.cpp
 *
 * Implements the IDirectSound3DBuffer interface associated with a
 * compatibility secondary buffer. It stores source position, velocity,
 * cone, distance and 3D mode, applying changes immediately or deferring
 * them until a commit.
 *
 * Each source claims a slot in the compatibility DSP engine. CommitOne
 * solves its spatial parameters and submits the DAL control bank, while
 * reference counting is tied to the owning secondary buffer. The
 * calculations are implemented in a3ddsp.cpp.
 *
 *---------------------------------------------------------------------------
 */

#include "a3dprv.h"
#include "A3dSource.h"
#include "dsbuffer.h"
#include "a3ddsp.h"

#include <stdlib.h>

LONG        g_cSources = A3D_SOURCE_TABLE_CAPACITY;
DWORD       g_apSourceActive[A3D_SOURCE_TABLE_CAPACITY];
CA3dSource *g_apSourceObject[A3D_SOURCE_TABLE_CAPACITY];

/* Engine volume index 128 maps to unity gain. */

#define A3D_VOLUME_UNITY	0x80

/* Stored cone half-angles wrap at 180 degrees */

/* =============================================================
// CA3dSource()
//
// Initialize DirectSound 3D defaults and source bookkeeping.
// =============================================================*/

CA3dSource::CA3dSource(CA3dSecondaryBuffer *pOwner)
{
	m_pOwner = pOwner;
	m_cRef   = 0;

	m_ds3db.dwSize = sizeof(DS3DBUFFER);

	m_ds3db.vPosition.x = 0.0f;
	m_ds3db.vPosition.y = 0.0f;
	m_ds3db.vPosition.z = 0.0f;

	m_ds3db.vVelocity.x = 0.0f;
	m_ds3db.vVelocity.y = 0.0f;
	m_ds3db.vVelocity.z = 0.0f;

	m_ds3db.dwInsideConeAngle  = 0;
	m_ds3db.dwOutsideConeAngle = 0;

	m_ds3db.vConeOrientation.x = 0.0f;
	m_ds3db.vConeOrientation.y = 0.0f;
	m_ds3db.vConeOrientation.z = 1.0f;

	m_ds3db.lConeOutsideVolume = DS3D_DEFAULTCONEOUTSIDEVOLUME;

	m_ds3db.flMinDistance = DS3D_DEFAULTMINDISTANCE;
	m_ds3db.flMaxDistance = (D3DVALUE) g_dMaxDistance;
	m_ds3db.dwMode        = DS3DMODE_NORMAL;

	m_iSource         = A3D_SOURCE_UNALLOCATED;
	m_pSolution       = NULL;
	m_pvAuxAllocation = NULL;
	m_pDal            = NULL;
	m_iBank           = 0;
}

/* =============================================================
// Init()
// (RE) a3d.dll rtl:0x10002FE0
//
// Claim an engine slot and initialize the source solution.
//
// Returns:
//   S_OK
//   E_FAIL         if no slot is free or engine reset fails
//   E_OUTOFMEMORY  if solution allocation fails
// =============================================================*/

HRESULT
CA3dSource::Init(DWORD dwSampleRate, IA3dDalBuffer *pDal)
{
WORD cbSolution;
LONG i;

	m_pDal = pDal;

	m_pDal->AddRef();

	for (i = 0; i < g_cSources; i++)
	{
		if (!g_apSourceActive[i])
		{
			g_apSourceActive[i] = 1;
			break;
		}
	}

	if (i >= g_cSources)
		i = A3D_SOURCE_UNALLOCATED;

	m_iSource = i;

	if (m_iSource < 0)
		return (E_FAIL);

	if (m_iSource < g_cSources && g_apSourceActive[m_iSource])
		g_apSourceObject[m_iSource] = this;

	cbSolution = 0;

	A3dSolutionSize(&cbSolution);

	m_pSolution = (A3DSOLUTION *) malloc(cbSolution);

	if (!m_pSolution)
		return (E_OUTOFMEMORY);

	memset(m_aBank, 0, sizeof(m_aBank));

	if (FAILED(A3dSourceReset(m_iSource, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
	                          A3D_VOLUME_UNITY, A3D_DSP_KHZ, dwSampleRate,
	                          1.0f)))
		return (E_FAIL);

	SetAllParameters(&m_ds3db, DS3D_IMMEDIATE);

	return (S_OK);
}

/* =============================================================
// ~CA3dSource()
// (RE) a3d.dll rtl:0x10002F60
//
// Release the engine slot, allocations and DAL interface.
// =============================================================*/

CA3dSource::~CA3dSource(void)
{
	A3dFreeSourceSlot(m_iSource);

	m_iSource = A3D_SOURCE_UNALLOCATED;

	if (m_pSolution)
		free(m_pSolution);

	if (m_pvAuxAllocation)
		free(m_pvAuxAllocation);

	if (m_pDal)
	{
		m_pDal->Release();
		m_pDal = NULL;
	}
}

/* =============================================================
// QueryInterface()
//
// Acquire an interface from the owning secondary buffer.
//
// Returns: The owner QueryInterface result.
// =============================================================*/

STDMETHODIMP
CA3dSource::QueryInterface(REFIID riid, void **ppv)
{
	return (m_pOwner->QueryInterface(riid, ppv));
}

/* =============================================================
// AddRef()
//
// Increment local and owner reference counts.
//
// Returns: The owner AddRef result.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dSource::AddRef(void)
{
	m_cRef++;

	return (m_pOwner->AddRef());
}

/* =============================================================
// Release()
//
// Decrement local and owner reference counts.
//
// Returns: The owner Release result.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dSource::Release(void)
{
	m_cRef--;

	return (m_pOwner->Release());
}

/* =============================================================
// GetAllParameters()
// (RE) a3d.dll rtl:0x10003140
//
// Copy source parameters. Preserve the original two-byte skip and
// unbounded caller size, including underflow and over-read.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null output
// =============================================================*/

STDMETHODIMP
CA3dSource::GetAllParameters(LPDS3DBUFFER pDs3dBuffer)
{
	if (!pDs3dBuffer)
		return (E_INVALIDARG);

	memcpy((BYTE *) pDs3dBuffer + sizeof(WORD),
	       (BYTE *) &m_ds3db + sizeof(WORD),
	       pDs3dBuffer->dwSize - sizeof(WORD));

	return (S_OK);
}

/* =============================================================
// GetConeAngles()
//
// Read the stored half-angles as full angles.
//
// Returns:
//   S_OK
//   E_INVALIDARG  if either output is null
// =============================================================*/

STDMETHODIMP
CA3dSource::GetConeAngles(LPDWORD pdwInside, LPDWORD pdwOutside)
{
	if (!pdwInside || !pdwOutside)
		return (E_INVALIDARG);

	*pdwInside  = 2 * m_ds3db.dwInsideConeAngle;
	*pdwOutside = 2 * m_ds3db.dwOutsideConeAngle;

	return (S_OK);
}

/* =============================================================
// GetConeOrientation()
//
// Read the stored source cone orientation.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null output
// =============================================================*/

STDMETHODIMP
CA3dSource::GetConeOrientation(D3DVECTOR *pvOrientation)
{
	if (!pvOrientation)
		return (E_INVALIDARG);

	*pvOrientation = m_ds3db.vConeOrientation;

	return (S_OK);
}

/* =============================================================
// GetConeOutsideVolume()
//
// Read the stored source outside-cone volume.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null output
// =============================================================*/

STDMETHODIMP
CA3dSource::GetConeOutsideVolume(LPLONG plVolume)
{
	if (!plVolume)
		return (E_INVALIDARG);

	*plVolume = m_ds3db.lConeOutsideVolume;

	return (S_OK);
}

/* =============================================================
// GetMaxDistance()
//
// Read the stored source maximum distance.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null output
// =============================================================*/

STDMETHODIMP
CA3dSource::GetMaxDistance(D3DVALUE *pflMaxDistance)
{
	if (!pflMaxDistance)
		return (E_INVALIDARG);

	*pflMaxDistance = m_ds3db.flMaxDistance;

	return (S_OK);
}

/* =============================================================
// GetMinDistance()
//
// Read the stored source minimum distance.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null output
// =============================================================*/

STDMETHODIMP
CA3dSource::GetMinDistance(D3DVALUE *pflMinDistance)
{
	if (!pflMinDistance)
		return (E_INVALIDARG);

	*pflMinDistance = m_ds3db.flMinDistance;

	return (S_OK);
}

/* =============================================================
// GetMode()
//
// Read the stored source 3D mode.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null output
// =============================================================*/

STDMETHODIMP
CA3dSource::GetMode(LPDWORD pdwMode)
{
	if (!pdwMode)
		return (E_INVALIDARG);

	*pdwMode = m_ds3db.dwMode;

	return (S_OK);
}

/* =============================================================
// GetPosition()
//
// Read the stored source position.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null output
// =============================================================*/

STDMETHODIMP
CA3dSource::GetPosition(D3DVECTOR *pvPosition)
{
	if (!pvPosition)
		return (E_INVALIDARG);

	*pvPosition = m_ds3db.vPosition;

	return (S_OK);
}

/* =============================================================
// GetVelocity()
//
// Read the stored source velocity.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null output
// =============================================================*/

STDMETHODIMP
CA3dSource::GetVelocity(D3DVECTOR *pvVelocity)
{
	if (!pvVelocity)
		return (E_INVALIDARG);

	*pvVelocity = m_ds3db.vVelocity;

	return (S_OK);
}

/* =============================================================
// SetAllParameters()
// (RE) a3d.dll rtl:0x100032B0
//
// Copy and apply source parameters. Preserve the original two-byte skip,
// stored-size copy length and discarded commit failures; truncated input can
// be over-read and a nonzero high size word corrupts later copy lengths.
//
// Returns:
//   S_OK
//   E_INVALIDARG  null input or a mode above DS3DMODE_DISABLE
// =============================================================*/

STDMETHODIMP
CA3dSource::SetAllParameters(LPCDS3DBUFFER pcDs3dBuffer, DWORD dwApply)
{
	if (!pcDs3dBuffer)
		return (E_INVALIDARG);

	if (pcDs3dBuffer->dwMode > DS3DMODE_DISABLE)
		return (E_INVALIDARG);

	memcpy((BYTE *) &m_ds3db + sizeof(WORD),
	       (BYTE *) pcDs3dBuffer + sizeof(WORD),
	       m_ds3db.dwSize - sizeof(WORD));

	SetPosition(m_ds3db.vPosition.x, m_ds3db.vPosition.y,
	            m_ds3db.vPosition.z, DS3D_DEFERRED);
	SetVelocity(m_ds3db.vVelocity.x, m_ds3db.vVelocity.y,
	            m_ds3db.vVelocity.z, DS3D_DEFERRED);
	SetConeOrientation(m_ds3db.vConeOrientation.x, m_ds3db.vConeOrientation.y,
	                   m_ds3db.vConeOrientation.z, DS3D_DEFERRED);
	SetConeAngles(2 * m_ds3db.dwInsideConeAngle,
	              2 * m_ds3db.dwOutsideConeAngle, DS3D_DEFERRED);
	SetConeOutsideVolume(m_ds3db.lConeOutsideVolume, DS3D_DEFERRED);
	SetMinDistance(m_ds3db.flMinDistance, DS3D_DEFERRED);
	SetMaxDistance(m_ds3db.flMaxDistance, DS3D_DEFERRED);

	if (dwApply == DS3D_IMMEDIATE)
		CommitOne();
	else
		g_aSourceDirty[m_iSource] = TRUE;

	return (S_OK);
}

/* =============================================================
// SetConeAngles()
// (RE) a3d.dll rtl:0x100033A0
//
// Store half-angles and update an active cone, immediately or deferred.
//
// Returns: S_OK, including commit failure.
// =============================================================*/

STDMETHODIMP
CA3dSource::SetConeAngles(DWORD dwInside, DWORD dwOutside, DWORD dwApply)
{
DWORD dwInsideHalfAngle;
DWORD dwOutsideHalfAngle;

	dwInsideHalfAngle  = (dwInside  >> 1) % 180;
	dwOutsideHalfAngle = (dwOutside >> 1) % 180;

	if (dwInsideHalfAngle > dwOutsideHalfAngle)
		dwInsideHalfAngle = dwOutsideHalfAngle;

	if (dwInsideHalfAngle == m_ds3db.dwInsideConeAngle &&
	    dwOutsideHalfAngle == m_ds3db.dwOutsideConeAngle)
		return (S_OK);

	m_ds3db.dwInsideConeAngle  = dwInsideHalfAngle;
	m_ds3db.dwOutsideConeAngle = dwOutsideHalfAngle;

	if (m_ds3db.lConeOutsideVolume)
		A3dSourceSetCone(m_iSource, dwInsideHalfAngle, 0, dwOutsideHalfAngle,
		                 m_ds3db.lConeOutsideVolume);

	if (dwApply == DS3D_IMMEDIATE)
		CommitOne();
	else
		g_aSourceDirty[m_iSource] = TRUE;

	return (S_OK);
}

/* =============================================================
// SetConeOrientation()
//
// Set the source cone orientation, immediately or deferred.
//
// Returns: S_OK, including commit failure.
// =============================================================*/

STDMETHODIMP
CA3dSource::SetConeOrientation(D3DVALUE x, D3DVALUE y, D3DVALUE z,
                               DWORD dwApply)
{
	m_ds3db.vConeOrientation.x = x;
	m_ds3db.vConeOrientation.y = y;
	m_ds3db.vConeOrientation.z = z;

	A3dSourceSetGeometry(m_iSource,
	                     m_ds3db.vPosition.x, m_ds3db.vPosition.y,
	                     m_ds3db.vPosition.z,
	                     m_ds3db.vVelocity.x, m_ds3db.vVelocity.y,
	                     m_ds3db.vVelocity.z,
	                     x, y, z);

	if (dwApply == DS3D_IMMEDIATE)
		CommitOne();
	else
		g_aSourceDirty[m_iSource] = TRUE;

	return (S_OK);
}

/* =============================================================
// SetConeOutsideVolume()
//
// Set cone attenuation, or remove the cone for zero volume, immediately
// or deferred.
//
// Returns: S_OK, including commit failure.
// =============================================================*/

STDMETHODIMP
CA3dSource::SetConeOutsideVolume(LONG lVolume, DWORD dwApply)
{
	m_ds3db.lConeOutsideVolume = lVolume;

	if (lVolume)
	{
		A3dSourceSetCone(m_iSource, m_ds3db.dwInsideConeAngle, 0,
		                 m_ds3db.dwOutsideConeAngle, lVolume);
	}
	else
	{
		A3dSourceClearCone(m_iSource);
	}

	if (dwApply == DS3D_IMMEDIATE)
		CommitOne();
	else
		g_aSourceDirty[m_iSource] = TRUE;

	return (S_OK);
}

/* =============================================================
// SetMaxDistance()
//
// Set the source maximum distance, immediately or deferred.
//
// Returns: S_OK, including commit failure.
// =============================================================*/

STDMETHODIMP
CA3dSource::SetMaxDistance(D3DVALUE flMaxDistance, DWORD dwApply)
{
	m_ds3db.flMaxDistance = flMaxDistance;

	A3dSourceSetMaxDistance(m_iSource, flMaxDistance);

	if (dwApply == DS3D_IMMEDIATE)
		CommitOne();
	else
		g_aSourceDirty[m_iSource] = TRUE;

	return (S_OK);
}

/* =============================================================
// SetMinDistance()
//
// Set the source minimum distance, immediately or deferred.
//
// Returns: S_OK, including commit failure.
// =============================================================*/

STDMETHODIMP
CA3dSource::SetMinDistance(D3DVALUE flMinDistance, DWORD dwApply)
{
	m_ds3db.flMinDistance = flMinDistance;

	A3dSourceSetMinDistance(m_iSource, flMinDistance);

	if (dwApply == DS3D_IMMEDIATE)
		CommitOne();
	else
		g_aSourceDirty[m_iSource] = TRUE;

	return (S_OK);
}

/* =============================================================
// SetMode()
//
// Select the source solver, immediately or deferred.
//
// Returns: S_OK, including commit failure; E_INVALIDARG above DS3DMODE_DISABLE.
// =============================================================*/

STDMETHODIMP
CA3dSource::SetMode(DWORD dwMode, DWORD dwApply)
{
	if (dwMode > DS3DMODE_DISABLE)
		return (E_INVALIDARG);

	m_ds3db.dwMode = dwMode;

	if (dwApply == DS3D_IMMEDIATE)
		CommitOne();
	else
		g_aSourceDirty[m_iSource] = TRUE;

	return (S_OK);
}

/* =============================================================
// SetPosition()
//
// Set the source position, immediately or deferred.
//
// Returns: S_OK, including commit failure.
// =============================================================*/

STDMETHODIMP
CA3dSource::SetPosition(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD dwApply)
{
	m_ds3db.vPosition.x = x;
	m_ds3db.vPosition.y = y;
	m_ds3db.vPosition.z = z;

	A3dSourceSetGeometry(m_iSource, x, y, z,
	                     m_ds3db.vVelocity.x, m_ds3db.vVelocity.y,
	                     m_ds3db.vVelocity.z,
	                     m_ds3db.vConeOrientation.x,
	                     m_ds3db.vConeOrientation.y,
	                     m_ds3db.vConeOrientation.z);

	if (dwApply == DS3D_IMMEDIATE)
		CommitOne();
	else
		g_aSourceDirty[m_iSource] = TRUE;

	return (S_OK);
}

/* =============================================================
// SetVelocity()
//
// Set the source velocity, immediately or deferred.
//
// Returns: S_OK, including commit failure.
// =============================================================*/

STDMETHODIMP
CA3dSource::SetVelocity(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD dwApply)
{
	m_ds3db.vVelocity.x = x;
	m_ds3db.vVelocity.y = y;
	m_ds3db.vVelocity.z = z;

	A3dSourceSetGeometry(m_iSource,
	                     m_ds3db.vPosition.x, m_ds3db.vPosition.y,
	                     m_ds3db.vPosition.z,
	                     x, y, z,
	                     m_ds3db.vConeOrientation.x,
	                     m_ds3db.vConeOrientation.y,
	                     m_ds3db.vConeOrientation.z);

	if (dwApply == DS3D_IMMEDIATE)
		CommitOne();
	else
		g_aSourceDirty[m_iSource] = TRUE;

	return (S_OK);
}

/* =============================================================
// CommitOne()
// (RE) a3d.dll rtl:0x100036F0
//
// Solve the source and submit its DAL parameter bank. Preserve clearing
// the dirty flag before submission, so failed submissions are not retried.
//
// Returns:
//   The DAL submission result
//   E_FAIL                     an invalid or inactive source
// =============================================================*/

HRESULT
CA3dSource::CommitOne(void)
{
A3DDALBANK *pBank;
DWORD       cbBank;

	if (m_iSource < 0 || m_iSource >= g_cSources ||
	    !g_apSourceActive[m_iSource])
		return (E_FAIL);

	switch (m_ds3db.dwMode)
	{
	case DS3DMODE_NORMAL:
		A3dSolveNormal(m_pSolution, m_iSource);
		break;

	case DS3DMODE_HEADRELATIVE:
		A3dSolveHeadRelative(m_pSolution, m_iSource);
		break;

	case DS3DMODE_DISABLE:
		A3dSolveDisabled(m_pSolution, m_iSource);
		break;

	default:
		break;
	}

	cbBank = 0;
	pBank  = &m_aBank[m_iBank];

	A3dBuildDalBank(m_pSolution, pBank, &cbBank, A3D_DSP_KHZ);

	g_aSourceDirty[m_iSource] = FALSE;

	return (m_pDal->SetA3dSuperCtrl((LPA3DCTRL_SRC_SUPER) pBank, cbBank));
}
