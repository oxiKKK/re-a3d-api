/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dReflection.cpp
 *
 * Implements IA3dReflection, the application-controlled reflection object
 * attached to a source. It stores gain, delay, equalization, position and
 * transform mode for a reflected sound path.
 *
 * The file also converts between angular orientation and direction
 * vectors for the source and listener APIs. Source tracing reads the
 * stored reflection properties when preparing playback controls; actual
 * voice rendering belongs to the selected backend.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "A3dReflection.h"
#include "A3dMatrix.h"

#include "A3dSource.h"

#include <math.h>

/* Manual-reflection ID floor  */
#define A3D_MANUAL_REFLECTION_ID_MIN 0x80000000

/* Angular constants by role */

static DWORD	g_dwReflectionId;	/* (RE) dbg:0x10146F3C */

/* =============================================================
// CA3dReflection()
// (RE) rtl:0x1000AA40; dbg:0x1001B0B0
//
// Initialize a source reflection with half gain, 0.1-second delay and flat EQ.
// The private base constructor (dbg:0x1001B230) remains represented by
// IA3dReflection directly.
// =============================================================*/

CA3dReflection::CA3dReflection(CA3dSource *pSrc)
{
	ASSERT(pSrc);

	m_dwRefCount	  = 0;
	m_pSource	  = pSrc;
	m_fGainScale	  = 0.5f;
	m_fDelay	  = 0.1f;
	m_vPosition[0]	  = 0.0f;
	m_vPosition[1]	  = 0.0f;
	m_vPosition[2]	  = 0.0f;
	m_dwTransformMode = 0;
	m_fEQ		  = 1.0f;

	m_dwId = ++g_dwReflectionId;

	if (m_dwId < A3D_MANUAL_REFLECTION_ID_MIN)
	{
		g_dwReflectionId = A3D_MANUAL_REFLECTION_ID_MIN;
		m_dwId		 = A3D_MANUAL_REFLECTION_ID_MIN;
	}
}

/* =============================================================
// CA3dReflection::~CA3dReflection() scalar deleting destructor
// (RE) rtl:0x1000AAA0; dbg:0x1001B1E0
// =============================================================*/

/* =============================================================
// ~CA3dReflection()
// (RE) dbg:0x1001B260
//
// Destroy the reflection.
// =============================================================*/

CA3dReflection::~CA3dReflection(void)
{
}

/* =============================================================
// QueryInterface()
// (RE) rtl:0x1000AAD0; dbg:0x1001B290
//
// Query IUnknown or IA3dReflection, clearing unsupported outputs.
//
// Returns:
//   S_OK
//   E_POINTER      a null output
//   E_NOINTERFACE  other IIDs
// =============================================================*/

STDMETHODIMP
CA3dReflection::QueryInterface(REFIID riid, LPVOID *ppvObj)
{
	if (!ppvObj)
		return (E_POINTER);

	if (IsEqualGUID(riid, IID_IUnknown) ||
	    IsEqualGUID(riid, IID_IA3dReflection))
	{
		*ppvObj = this;
	}
	else
	{
		*ppvObj = NULL;

		return (E_NOINTERFACE);
	}

	((IUnknown *) *ppvObj)->AddRef();

	return (S_OK);
}

/* =============================================================
// AddRef()
// (RE) rtl:0x1000AB40; dbg:0x1001B330
//
// Increment the reference count.
//
// Returns: The updated reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dReflection::AddRef(void)
{
	return (++m_dwRefCount);
}

/* =============================================================
// Release()
// (RE) rtl:0x1000AB60; dbg:0x1001B360
//
// Release a reference, removing the reflection from its source and deleting
// it at zero.
//
// Returns: The remaining reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dReflection::Release(void)
{
	if (--m_dwRefCount)
		return (m_dwRefCount);

	m_pSource->RemoveManualReflection(this);

	delete this;

	return (0);
}

/* Correspondence to the declared matrix helpers is unresolved:
 * rotation: dbg:0x10019950; rtl:0x10009d40;
 * block inverse: dbg:0x10019f60; rtl:0x1000a150;
 * basis multiply: dbg:0x1001a680; rtl:0x1000a590; thunk dbg:0x1000245A. */

/* =============================================================
// A3dAnglesToVectors()
// (RE) rtl:0x1000A4A0; dbg:0x1001A530; thunk dbg:0x10001B22
//
// Convert orientation angles in degrees to front and up vectors, negating
// front when nCoordSystem is zero.
// =============================================================*/

void
A3dAnglesToVectors(const A3DVAL *pAngles, A3DVAL *pvFront, A3DVAL *pvUp,
		   int nCoordSystem)
{
A3DVAL	mat[16];
A3DVAL	vAxis[3];

	A3dMatrixIdentity(mat);

	vAxis[0] = 0.0f;
	vAxis[1] = 1.0f;
	vAxis[2] = 0.0f;

	A3dMatrixRotateBy(pAngles[0], vAxis, mat);

	vAxis[0] = 1.0f;
	vAxis[1] = 0.0f;

	A3dMatrixRotateBy(pAngles[1], vAxis, mat);

	vAxis[0] = 0.0f;
	vAxis[2] = 1.0f;

	A3dMatrixRotateBy(pAngles[2], vAxis, mat);

	if (nCoordSystem)
	{
		pvFront[0] = mat[8];
		pvFront[1] = mat[9];
		pvFront[2] = mat[10];
	}
	else
	{
		pvFront[0] = -1.0f * mat[8];
		pvFront[1] = -1.0f * mat[9];
		pvFront[2] = -1.0f * mat[10];
	}

	pvUp[0] = mat[4];
	pvUp[1] = mat[5];
	pvUp[2] = mat[6];
}

/* =============================================================
// A3dVectorsToAngles()
// (RE) rtl:0x1000A880; dbg:0x1001AC70; thunk dbg:0x1000317A
//
// Convert front and up vectors to azimuth, elevation and roll in degrees.
// nCoordSystem selects the cross-product order and azimuth convention.
// =============================================================*/

void
A3dVectorsToAngles(const A3DVAL *pvFront, const A3DVAL *pvUp, A3DVAL *pAngles,
		   int nCoordSystem)
{
A3DVAL	vCross[3];
A3DVAL	fX, fY, fZ;
A3DVAL	fFlat, fSide;

	if (nCoordSystem)
	{
		vCross[0] = pvFront[1] * pvUp[2] - pvFront[2] * pvUp[1];
		vCross[1] = pvFront[2] * pvUp[0] - pvFront[0] * pvUp[2];
		vCross[2] = pvFront[0] * pvUp[1] - pvFront[1] * pvUp[0];
	}
	else
	{
		vCross[0] = pvUp[1] * pvFront[2] - pvUp[2] * pvFront[1];
		vCross[1] = pvUp[2] * pvFront[0] - pvUp[0] * pvFront[2];
		vCross[2] = pvUp[0] * pvFront[1] - pvUp[1] * pvFront[0];
	}

	fX = pvFront[0];
	fY = pvFront[1];
	fZ = pvFront[2];

	pAngles[0] = 0.0f;
	pAngles[1] = 0.0f;
	pAngles[2] = 0.0f;

	if (!nCoordSystem && fZ != 0.0f)
	{
		pAngles[0] = (A3DVAL) (atan(fabs(fX) / fabs(fZ)) *
				       A3D_RADIANS_TO_DEGREES);

		if (fX <= 0.0f)
		{
			if (fZ > 0.0f)
				pAngles[0] = 180.0f - pAngles[0];
		}
		else
		{
			if (fZ <= 0.0f)
				pAngles[0] = 360.0f - pAngles[0];
			else
				pAngles[0] = pAngles[0] + 180.0f;
		}
	}

	if (nCoordSystem == 1 && fZ != 0.0f)
	{
		pAngles[0] = (A3DVAL) (atan(fabs(fX) / fabs(fZ)) *
				       A3D_RADIANS_TO_DEGREES);

		if (fX <= 0.0f)
		{
			if (fZ >= 0.0f)
				pAngles[0] = 360.0f - pAngles[0];
			else
				pAngles[0] = pAngles[0] + 180.0f;
		}
		else if (fZ < 0.0f)
		{
			pAngles[0] = 180.0f - pAngles[0];
		}
	}

	fFlat = (A3DVAL) sqrt(fX * fX + fZ * fZ);
	if (fFlat != 0.0f)
		pAngles[1] = (A3DVAL) (atan(fY / fFlat) * A3D_RADIANS_TO_DEGREES);

	fSide = (A3DVAL) sqrt(vCross[0] * vCross[0] + vCross[2] * vCross[2]);
	if (fSide != 0.0f)
	{
		if (nCoordSystem)
			pAngles[2] = (A3DVAL) (atan(vCross[1] / fSide) *
					       A3D_RADIANS_TO_DEGREES);
		else
			pAngles[2] = (A3DVAL) (atan(-vCross[1] / fSide) *
					       A3D_RADIANS_TO_DEGREES);
	}
}

/* =============================================================
// SetGainScale()
// (RE) rtl:0x1000AB90; dbg:0x1001B3F0
//
// Store the gain scale. Preserve validation of the old value instead of
// the argument.
//
// Returns:
//   S_OK
//   E_INVALIDARG  when the stored gain is outside [0,1]
// =============================================================*/

STDMETHODIMP
CA3dReflection::SetGainScale(A3DVAL fScale)
{
	if (m_fGainScale < 0.0 || m_fGainScale > 1.0)
		return (E_INVALIDARG);

	m_fGainScale = fScale;

	return (S_OK);
}

/* =============================================================
// GetGainScale()
// (RE) rtl:0x1000ABD0; dbg:0x1001B450
//
// Read the reflection gain scale.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dReflection::GetGainScale(LPA3DVAL pfScale)
{
	if (!pfScale)
		return (E_POINTER);

	*pfScale = m_fGainScale;

	return (S_OK);
}

/* =============================================================
// SetDelay()
// (RE) rtl:0x1000AC00; dbg:0x1001B490
//
// Store the delay in seconds. Preserve validation of the old delay instead
// of the argument.
//
// Returns:
//   S_OK
//   E_INVALIDARG  when the stored delay is negative
// =============================================================*/

STDMETHODIMP
CA3dReflection::SetDelay(A3DVAL fDelay)
{
	if (m_fDelay < 0.0)
		return (E_INVALIDARG);

	m_fDelay = fDelay;

	return (S_OK);
}

/* =============================================================
// GetDelay()
// (RE) rtl:0x1000AC30; dbg:0x1001B4E0
//
// Read the reflection delay in seconds.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dReflection::GetDelay(LPA3DVAL pfDelay)
{
	if (!pfDelay)
		return (E_POINTER);

	*pfDelay = m_fDelay;

	return (S_OK);
}

/* =============================================================
// SetPosition3f()
// (RE) rtl:0x1000AC60; dbg:0x1001B520
//
// Set the reflection position from three scalar components.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dReflection::SetPosition3f(A3DVAL x, A3DVAL y, A3DVAL z)
{
	m_vPosition[0] = x;
	m_vPosition[1] = y;
	m_vPosition[2] = z;

	return (S_OK);
}

/* =============================================================
// GetPosition3f()
// (RE) rtl:0x1000AC80; dbg:0x1001B560
//
// Read the reflection position through three unchecked output pointers.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dReflection::GetPosition3f(LPA3DVAL px, LPA3DVAL py, LPA3DVAL pz)
{
	*px = m_vPosition[0];
	*py = m_vPosition[1];
	*pz = m_vPosition[2];

	return (S_OK);
}

/* =============================================================
// SetPosition3fv()
// (RE) rtl:0x1000ACB0; dbg:0x1001B5B0
//
// Set the reflection position from a vector.
//
// Returns:
//   The SetPosition3f result
//   E_POINTER                 a null vector
// =============================================================*/

STDMETHODIMP
CA3dReflection::SetPosition3fv(A3DVAL *pv)
{
	if (!pv)
		return (E_POINTER);

	return (SetPosition3f(pv[0], pv[1], pv[2]));
}

/* =============================================================
// GetPosition3fv()
// (RE) rtl:0x1000ACE0; dbg:0x1001B600
//
// Copy the reflection position into a vector.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dReflection::GetPosition3fv(A3DVAL *pv)
{
	if (!pv)
		return (E_POINTER);

	pv[0] = m_vPosition[0];
	pv[1] = m_vPosition[1];
	pv[2] = m_vPosition[2];

	return (S_OK);
}

/* =============================================================
// SetTransformMode()
// (RE) rtl:0x1000AD10; dbg:0x1001B660
//
// Store the reflection transform mode.
//
// Returns:
//   S_OK
//   E_INVALIDARG  when bit 0 is clear
// =============================================================*/

STDMETHODIMP
CA3dReflection::SetTransformMode(DWORD dwMode)
{
	if ((dwMode & A3DREFLECTION_TRANSFORMMODE_HEADRELATIVE) == 0)
		return (E_INVALIDARG);

	m_dwTransformMode = dwMode;

	return (S_OK);
}

/* =============================================================
// GetTransformMode()
// (RE) rtl:0x1000AD30; dbg:0x1001B6A0
//
// Read the reflection transform mode.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dReflection::GetTransformMode(DWORD *pdwMode)
{
	if (!pdwMode)
		return (E_POINTER);

	*pdwMode = m_dwTransformMode;

	return (S_OK);
}

/* =============================================================
// SetEQ()
// (RE) rtl:0x1000AD60; dbg:0x1001B6E0
//
// Store the reflection EQ value.
//
// Returns:
//   S_OK
//   E_INVALIDARG  outside [0,1]
// =============================================================*/

STDMETHODIMP
CA3dReflection::SetEQ(A3DVAL fEQ)
{
	if (fEQ < 0.0 || fEQ > 1.0f)
		return (E_INVALIDARG);

	m_fEQ = fEQ;

	return (S_OK);
}

/* =============================================================
// GetEQ()
// (RE) rtl:0x1000ADA0; dbg:0x1001B740
//
// Read the reflection EQ value.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dReflection::GetEQ(LPA3DVAL pfEQ)
{
	if (!pfEQ)
		return (E_POINTER);

	*pfEQ = m_fEQ;

	return (S_OK);
}
