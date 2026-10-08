/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * Listener.cpp
 *
 * Implements the IA3dListener operations on CA3dRoot. It stores listener
 * position, velocity and orientation, supporting both angular and
 * front/up-vector representations.
 *
 * Listener initialization establishes the state used by tracing, and the
 * scale helpers propagate Doppler and distance-model changes to
 * registered sources. The root layout is declared in A3d3.h;
 * A3dMatrix.cpp prepares listener transforms and velocity for a trace
 * pass.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "A3d3.h"
#include "A3dReflection.h"
#include "A3dMatrix.h"
#include "A3dSource.h"
#include "apimapper.h"

/* Default lateral ear offset, metres */
#define A3D_LISTENER_DEFAULT_EAR_OFFSET 0.07f

/* =============================================================
// SetPosition3f()
// (RE) rtl:0x100193C0; dbg:0x1003CAF0; thunk dbg:0x10003F53
//
// Set listener position from scalar coordinates.
//
// Returns: The SetPosition3fv result.
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetPosition3f(A3DVAL x, A3DVAL y, A3DVAL z)
{
A3DVAL v[3];

	v[0] = x;
	v[1] = y;
	v[2] = z;

	return (SetPosition3fv(v));
}

/* =============================================================
// GetPosition3f()
// (RE) rtl:0x10019A60; dbg:0x1003D3F0
//
// Read listener position into three scalar outputs.
//
// Returns:
//   S_OK
//   E_POINTER  if any output is null
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetPosition3f(LPA3DVAL px, LPA3DVAL py, LPA3DVAL pz)
{
	if (!px || !py || !pz)
		return (E_POINTER);

	*px = m_vPosition[0];
	*py = m_vPosition[1];
	*pz = m_vPosition[2];

	return (S_OK);
}

/* =============================================================
// SetPosition3fv()
// (RE) rtl:0x100193F0; dbg:0x1003CB40
//
// Set the three listener position components.
//
// Returns:
//   S_OK
//   E_POINTER  a null input
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetPosition3fv(LPA3DVAL pv)
{
	if (!pv)
		return (E_POINTER);

	m_vPosition[0] = pv[0];
	m_vPosition[1] = pv[1];
	m_vPosition[2] = pv[2];

	return (S_OK);
}

/* =============================================================
// GetPosition3fv()
// (RE) rtl:0x10019AB0; dbg:0x1003D460
//
// Read the three listener position components.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetPosition3fv(LPA3DVAL pv)
{
	if (!pv)
		return (E_POINTER);

	pv[0] = m_vPosition[0];
	pv[1] = m_vPosition[1];
	pv[2] = m_vPosition[2];

	return (S_OK);
}

/* =============================================================
// SetOrientationAngles3f()
// (RE) rtl:0x10019430; dbg:0x1003CBA0
//
// Set listener heading, pitch and roll in degrees.
//
// Returns: The SetOrientationAngles3fv result.
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetOrientationAngles3f(A3DVAL h, A3DVAL p, A3DVAL r)
{
A3DVAL v[3];

	v[0] = h;
	v[1] = p;
	v[2] = r;

	return (SetOrientationAngles3fv(v));
}

/* =============================================================
// GetOrientationAngles3f()
// (RE) rtl:0x100194C0; dbg:0x1003CCA0
//
// Read listener heading, pitch and roll into scalar outputs.
//
// Returns: S_OK, ignoring the vector getter result; E_POINTER if any output is
//          null.
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetOrientationAngles3f(LPA3DVAL ph, LPA3DVAL pp, LPA3DVAL pr)
{
A3DVAL v[3];

	if (!ph || !pp || !pr)
		return (E_POINTER);

	GetOrientationAngles3fv(v);

	*ph = v[0];
	*pp = v[1];
	*pr = v[2];

	return (S_OK);
}

/* =============================================================
// SetOrientationAngles3fv()
// (RE) rtl:0x10019460; dbg:0x1003CBF0
//
// Cache heading, pitch and roll in degrees; derive front and up vectors.
//
// Returns:
//   S_OK
//   E_POINTER  a null input
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetOrientationAngles3fv(LPA3DVAL pv)
{
	if (!pv)
		return (E_POINTER);

	m_vOrientAngles[0] = pv[0];
	m_vOrientAngles[1] = pv[1];
	m_vOrientAngles[2] = pv[2];

	m_dwOrientAsAngles = 1;

	A3dAnglesToVectors(pv, m_vOrientFront, m_vOrientUp, m_dwCoordSystem);

	return (S_OK);
}

/* =============================================================
// GetOrientationAngles3fv()
// (RE) rtl:0x10019520; dbg:0x1003CD30
//
// Read cached listener angles or derive them from the orientation vectors.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetOrientationAngles3fv(LPA3DVAL pv)
{
	if (!pv)
		return (E_POINTER);

	if (m_dwOrientAsAngles)
	{
		pv[0] = m_vOrientAngles[0];
		pv[1] = m_vOrientAngles[1];
		pv[2] = m_vOrientAngles[2];
	}
	else
	{
		A3dVectorsToAngles(m_vOrientFront, m_vOrientUp, pv, m_dwCoordSystem);
	}

	return (S_OK);
}

/* =============================================================
// SetOrientation6f()
// (RE) rtl:0x10019590; dbg:0x1003CDE0
//
// Set listener front and up directions from scalar components.
//
// Returns: The SetOrientation6fv result.
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetOrientation6f(A3DVAL fx, A3DVAL fy, A3DVAL fz,
			   A3DVAL ux, A3DVAL uy, A3DVAL uz)
{
A3DVAL v[6];

	v[0] = fx;
	v[1] = fy;
	v[2] = fz;
	v[3] = ux;
	v[4] = uy;
	v[5] = uz;

	return (SetOrientation6fv(v));
}

/* =============================================================
// GetOrientation6f()
// (RE) rtl:0x10019AF0; dbg:0x1003D4C0
//
// Read listener front and up directions into scalar outputs.
//
// Returns:
//   S_OK
//   E_POINTER  if any output is null
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetOrientation6f(LPA3DVAL pfx, LPA3DVAL pfy, LPA3DVAL pfz,
			   LPA3DVAL pux, LPA3DVAL puy, LPA3DVAL puz)
{
	if (!pfx || !pfy || !pfz || !pux || !puy || !puz)
		return (E_POINTER);

	*pfx = m_vOrientFront[0];
	*pfy = m_vOrientFront[1];
	*pfz = m_vOrientFront[2];
	*pux = m_vOrientUp[0];
	*puy = m_vOrientUp[1];
	*puz = m_vOrientUp[2];

	return (S_OK);
}

/* =============================================================
// SetOrientation6fv()
// (RE) rtl:0x100195D0; dbg:0x1003CE40
//
// Set listener front and up directions and select vector orientation mode.
//
// Returns:
//   S_OK
//   E_POINTER  a null input
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetOrientation6fv(LPA3DVAL pv)
{
	if (!pv)
		return (E_POINTER);

	m_vOrientFront[0] = pv[0];
	m_vOrientFront[1] = pv[1];
	m_vOrientFront[2] = pv[2];
	m_vOrientUp[0]    = pv[3];
	m_vOrientUp[1]    = pv[4];
	m_vOrientUp[2]    = pv[5];

	m_dwOrientAsAngles = 0;

	return (S_OK);
}

/* =============================================================
// GetOrientation6fv()
// (RE) rtl:0x10019B70; dbg:0x1003D580
//
// Read listener front and up directions as six components.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetOrientation6fv(LPA3DVAL pv)
{
	if (!pv)
		return (E_POINTER);

	pv[0] = m_vOrientFront[0];
	pv[1] = m_vOrientFront[1];
	pv[2] = m_vOrientFront[2];
	pv[3] = m_vOrientUp[0];
	pv[4] = m_vOrientUp[1];
	pv[5] = m_vOrientUp[2];

	return (S_OK);
}

/* =============================================================
// SetVelocity3f()
// (RE) rtl:0x10019630; dbg:0x1003CEF0
//
// Set listener velocity from scalar components.
//
// Returns: The SetVelocity3fv result.
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetVelocity3f(A3DVAL x, A3DVAL y, A3DVAL z)
{
A3DVAL v[3];

	v[0] = x;
	v[1] = y;
	v[2] = z;

	return (SetVelocity3fv(v));
}

/* =============================================================
// GetVelocity3f()
// (RE) rtl:0x10019BD0; dbg:0x1003D620
//
// Read listener velocity into three scalar outputs.
//
// Returns:
//   S_OK
//   E_POINTER  if any output is null
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetVelocity3f(LPA3DVAL px, LPA3DVAL py, LPA3DVAL pz)
{
	if (!px || !py || !pz)
		return (E_POINTER);

	*px = m_vVelocity[0];
	*py = m_vVelocity[1];
	*pz = m_vVelocity[2];

	return (S_OK);
}

/* =============================================================
// SetVelocity3fv()
// (RE) rtl:0x10019660; dbg:0x1003CF40
//
// Set listener velocity and mark it present.
//
// Returns:
//   S_OK
//   E_POINTER  a null input
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetVelocity3fv(LPA3DVAL pv)
{
	if (!pv)
		return (E_POINTER);

	m_vVelocity[0] = pv[0];
	m_vVelocity[1] = pv[1];
	m_vVelocity[2] = pv[2];

	m_dwVelocitySet = 1;

	return (S_OK);
}

/* =============================================================
// GetVelocity3fv()
// (RE) rtl:0x10019C20; dbg:0x1003D690
//
// Read the three listener velocity components.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetVelocity3fv(LPA3DVAL pv)
{
	if (!pv)
		return (E_POINTER);

	pv[0] = m_vVelocity[0];
	pv[1] = m_vVelocity[1];
	pv[2] = m_vVelocity[2];

	return (S_OK);
}

/* =============================================================
// CreateListener()
// (RE) rtl:0x100192A0; dbg:0x1003C860
//
// Initialize listener state, preserving uninitialized ear-offset w components.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
CA3dRoot::CreateListener(void)
{
	m_vPosition[0] = 0.0f;
	m_vPosition[1] = 0.0f;
	m_vPosition[2] = 0.0f;
	m_vPosition[3] = 1.0f;

	m_vVelocity[0] = 0.0f;
	m_vVelocity[1] = 0.0f;
	m_vVelocity[2] = 0.0f;
	m_vVelocity[3] = 0.0f;

	m_vOrientAngles[0] = 0.0f;
	m_vOrientAngles[1] = 0.0f;
	m_vOrientAngles[2] = 0.0f;
	m_vOrientAngles[3] = 0.0f;

	m_vOrientFront[0] = 0.0f;
	m_vOrientFront[1] = 0.0f;

	if (m_dwCoordSystem)
		m_vOrientFront[2] = 1.0f;
	else
		m_vOrientFront[2] = -1.0f;

	m_vOrientFront[3] = 0.0f;

	m_vOrientUp[0] = 0.0f;
	m_vOrientUp[1] = 1.0f;
	m_vOrientUp[2] = 0.0f;
	m_vOrientUp[3] = 0.0f;

	ZeroMemory(m_matListener, sizeof(m_matListener));

	m_matListener[0]  = 1.0f;
	m_matListener[5]  = 1.0f;
	m_matListener[10] = 1.0f;
	m_matListener[15] = 1.0f;

	m_vEarOffsetLeft[0] = -A3D_LISTENER_DEFAULT_EAR_OFFSET;
	m_vEarOffsetLeft[1] = 0.0f;
	m_vEarOffsetLeft[2] = 0.0f;

	m_vEarOffsetRight[0] = A3D_LISTENER_DEFAULT_EAR_OFFSET;
	m_vEarOffsetRight[1] = 0.0f;
	m_vEarOffsetRight[2] = 0.0f;

	m_fListenerReady = 1;
	m_dwVelocitySet  = 0;

	return (S_OK);
}

/* =============================================================
// A3dBroadcastDoppler()
// (RE) rtl:0x100199E0; dbg:0x1003D170
//
// Set the Doppler scale on every registered source.
// Original root method reconstructed with an explicit owner parameter.
// =============================================================*/

void
A3dBroadcastDoppler(CA3dRoot *pOwner, A3DVAL fFactor)
{
CA3dStdList<CA3dSource *>::iterator	it;
CA3dSource				*pSource;

	if (pOwner->m_SourceArray.size() < 1)
		return;

	for (it = pOwner->m_SourceArray.begin();
	     it != pOwner->m_SourceArray.end();
	     ++it)
	{
		pSource = *it;

		ASSERT((pSource != 0 &&
		       !IsBadReadPtr(pSource, sizeof(CA3dSource))));

		((IA3dSource2 *) pSource)->SetDopplerScale(fFactor);
	}
}

/* =============================================================
// A3dBroadcastRolloff()
// (RE) rtl:0x10019A20; dbg:0x1003D2C0
//
// Set the distance-model scale on every registered source.
// Original root method reconstructed with an explicit owner parameter.
// =============================================================*/

void
A3dBroadcastRolloff(CA3dRoot *pOwner, A3DVAL fFactor)
{
CA3dStdList<CA3dSource *>::iterator	it;
CA3dSource				*pSource;
int					cLeft;	/* Original unused countdown. */

	cLeft = pOwner->m_SourceArray.size();

	if (cLeft < 1)
		return;

	for (it = pOwner->m_SourceArray.begin();
	     it != pOwner->m_SourceArray.end();
	     ++it)
	{
		pSource = *it;

		ASSERT((pSource != 0 &&
		       !IsBadReadPtr(pSource, sizeof(CA3dSource))));

		((IA3dSource2 *) pSource)->SetDistanceModelScale(fFactor);

		cLeft--;
	}
}
