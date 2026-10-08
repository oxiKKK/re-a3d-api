/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dFrame.cpp
 *
 * Implements coordinate frames for room, wall and opening geometry. A
 * frame stores placement relative to a parent and provides position,
 * orientation, scaling, rotation and matrix operations through
 * IA3dTransform.
 *
 * The conversion helpers move points and vectors through frame chains so
 * geometry can be compared or emitted in a common coordinate system. This
 * file also supplies geometry-base lifetime methods and small room and
 * wall state queries used by the scene implementation.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "A3dFrame.h"
#include "A3dGeom.h"
#include "A3dMatrix.h"
#include "A3dRoom.h"
#include "A3dWall.h"
#include "LinkList.h"
#include "A3dSource.h"

#include <math.h>

/* =============================================================
// CA3dFrame()
// (RE) dbg:0x10075fa0
//
// Initialize an identity frame at the origin.
// =============================================================*/

CA3dFrame::CA3dFrame(void)
{
A3DVAL	vPosition[4];
A3DVAL	vFront[4];
A3DVAL	vUp[4];

	vPosition[0] = 0.0f;
	vPosition[1] = 0.0f;
	vPosition[2] = 0.0f;
	vPosition[3] = 1.0f;

	vFront[0] = 0.0f;
	vFront[1] = 0.0f;
	vFront[2] = 1.0f;
	vFront[3] = 0.0f;

	vUp[0] = 0.0f;
	vUp[1] = 1.0f;
	vUp[2] = 0.0f;
	vUp[3] = 0.0f;

	m_pParent = NULL;

	A3dFrameSet(this, NULL, vPosition, vFront, vUp);

	m_cRef = 0;
}

/* =============================================================
// ~CA3dFrame()
//
// Destroy the frame without releasing borrowed owner or parent pointers.
// =============================================================*/

CA3dFrame::~CA3dFrame(void)
{
}

/* =============================================================
// CopyFrameFrom()
//
// Copy the matrix and parent, replacing the owner only when supplied.
// =============================================================*/

void
CA3dFrame::CopyFrameFrom(void *pOwner, const CA3dFrame *pcFrom)
{
	CopyMemory(m_mat, pcFrom->m_mat, sizeof(m_mat));

	m_pParent = pcFrom->m_pParent;

	if (pOwner)
		m_pOwner = pOwner;
}

/* =============================================================
// A3dFrameSet()
//
// Build the frame from position, front and up without normalizing the basis.
// =============================================================*/

void
A3dFrameSet(CA3dFrame *pTransform, void *pOwner, LPA3DVAL pvPosition,
			LPA3DVAL pvFront, LPA3DVAL pvUp)
{
A3DVAL *pmat;

	pmat = pTransform->m_mat;

	pmat[0] = pvUp[1] * pvFront[2] - pvFront[1] * pvUp[2];
	pmat[1] = pvFront[0] * pvUp[2] - pvUp[0] * pvFront[2];
	pmat[2] = pvUp[0] * pvFront[1] - pvFront[0] * pvUp[1];
	pmat[3] = 0.0f;

	pmat[4] = pvUp[0];
	pmat[5] = pvUp[1];
	pmat[6] = pvUp[2];
	pmat[7] = 0.0f;

	pmat[8]         = pvFront[0];
	pmat[9]         = pvFront[1];
	pmat[10]        = pvFront[2];
	pmat[11]        = 0.0f;

	pmat[12] = pvPosition[0];
	pmat[13] = pvPosition[1];
	pmat[14] = pvPosition[2];
	pmat[15] = 1.0f;

	if (pOwner)
		pTransform->m_pOwner = pOwner;
}

/* =============================================================
// A3dWallIsShell()
//
// Test whether the wall belongs to its room shell.
//
// Returns: Nonzero for a shell wall; zero otherwise.
// =============================================================*/

int
A3dWallIsShell(const CA3dWall *pcWall)
{
	return ((pcWall->m_dwFlags & A3DWALL_FLAG_SHELL) != 0);
}

/* =============================================================
// A3dRoomIsMoving()
//
// Test whether any room flag is set.
//
// Returns: Nonzero when the flags word is nonzero; zero otherwise.
// =============================================================*/

int
A3dRoomIsMoving(const CA3dRoom *pcRoom)
{
	return (pcRoom->m_dwFlags != 0);
}

/* =============================================================
// A3dWallIsMarked()
//
// Test the wall marker flag.
//
// Returns: Nonzero for a marked wall; zero otherwise.
// =============================================================*/

int
A3dWallIsMarked(const CA3dWall *pcWall)
{
	return ((pcWall->m_dwFlags & A3DWALL_FLAG_MARKED) != 0);
}

/* =============================================================
// Scale3f()
//
// Post-multiply the frame matrix by a scale.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dFrame::Scale3f(A3DVAL x, A3DVAL y, A3DVAL z)
{
A3DVAL	v[4];

	v[0] = x;
	v[1] = y;
	v[2] = z;
	v[3] = 0.0f;

	A3dMatrixScaleBy(v, m_mat);

	return (S_OK);
}

/* =============================================================
// Rotate3fv()
//
// Rotate the frame, normalizing a nonzero caller axis in place.
//
// Returns: S_OK, including a zero-axis no-op.
// =============================================================*/

STDMETHODIMP
CA3dFrame::Rotate3fv(A3DVAL fDegrees, LPA3DVAL pv)
{
	A3dMatrixRotateBy(fDegrees, pv, m_mat);

	return (S_OK);
}

/* =============================================================
// Translate3fv()
//
// Post-multiply the frame matrix by a translation.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dFrame::Translate3fv(LPA3DVAL pv)
{
	A3dMatrixTranslateBy(pv, m_mat);

	return (S_OK);
}

/* =============================================================
// GetOrientation()
//
// Read front and up, optionally relative to another frame.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
CA3dFrame::GetOrientation(const CA3dFrame *pcRelativeTo,
			      A3DVAL *pvFront, A3DVAL *pvUp) const
{
A3DVAL	vFront[4];
A3DVAL	vUp[4];

	if (pcRelativeTo)
	{
		vFront[0] = 0.0f;
		vFront[1] = 0.0f;
		vFront[2] = 1.0f;
		vFront[3] = 0.0f;

		vUp[0] = 0.0f;
		vUp[1] = 1.0f;
		vUp[2] = 0.0f;
		vUp[3] = 0.0f;

		TransformPoint(vFront);
		TransformPoint(vUp);

		pcRelativeTo->VectorInto(pvFront, vFront);
		pcRelativeTo->VectorInto(pvUp, vUp);
	}
	else
	{
		pvUp[0] = m_mat[4];
		pvUp[1] = m_mat[5];
		pvUp[2] = m_mat[6];

		pvFront[0] = m_mat[8];
		pvFront[1] = m_mat[9];
		pvFront[2] = m_mat[10];
	}

	return (S_OK);
}

/* =============================================================
// SetOrientation()
//
// Set front and up without normalization, optionally relative to another frame.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
CA3dFrame::SetOrientation(const CA3dFrame *pcRelativeTo,
			      const A3DVAL *pcvFront, const A3DVAL *pcvUp)
{
A3DVAL	vFront[4];
A3DVAL	vUp[4];

	vFront[0] = pcvFront[0];
	vFront[1] = pcvFront[1];
	vFront[2] = pcvFront[2];
	vFront[3] = 0.0f;

	vUp[0] = pcvUp[0];
	vUp[1] = pcvUp[1];
	vUp[2] = pcvUp[2];
	vUp[3] = 0.0f;

	if (pcRelativeTo)
	{
		pcRelativeTo->TransformPoint(vFront);
		pcRelativeTo->TransformPoint(vUp);

		if (m_pParent)
		{
		A3DVAL	vLocalFront[4];
		A3DVAL	vLocalUp[4];

			VectorInto(vLocalFront, vFront);
			VectorInto(vLocalUp, vUp);

			CopyMemory(vFront, vLocalFront, sizeof(vFront));
			CopyMemory(vUp, vLocalUp, sizeof(vUp));
		}
	}

	m_mat[0] = vUp[1] * vFront[2] - vFront[1] * vUp[2];
	m_mat[1] = vFront[0] * vUp[2] - vUp[0] * vFront[2];
	m_mat[2] = vUp[0] * vFront[1] - vFront[0] * vUp[1];

	m_mat[4] = vUp[0];
	m_mat[5] = vUp[1];
	m_mat[6] = vUp[2];

	m_mat[8]  = vFront[0];
	m_mat[9]  = vFront[1];
	m_mat[10] = vFront[2];

	return (S_OK);
}

typedef int A3dFrameSizeCheck[(sizeof(CA3dFrame) == 0x50) ? 1 : -1];

/* =============================================================
// VectorInto()
//
// Transform a homogeneous vector through the inverse frame chain.
// =============================================================*/

void
CA3dFrame::VectorInto(A3DVAL *pvOut, const A3DVAL *pcvIn) const
{
A3DVAL	m[16];
int	i;

	for (i = 0; i < 16; i++)
		m[i] = 0.0f;

	m[0]  = 1.0f;
	m[5]  = 1.0f;
	m[10] = 1.0f;
	m[15] = 1.0f;

	ChainTo(m);

	A3dMatrixInvert(m, m);

	pvOut[0] = pcvIn[0] * m[0] + pcvIn[1] * m[4] + pcvIn[2] * m[8]  + pcvIn[3] * m[12];
	pvOut[1] = pcvIn[0] * m[1] + pcvIn[1] * m[5] + pcvIn[2] * m[9]  + pcvIn[3] * m[13];
	pvOut[2] = pcvIn[0] * m[2] + pcvIn[1] * m[6] + pcvIn[2] * m[10] + pcvIn[3] * m[14];
	pvOut[3] = pcvIn[0] * m[3] + pcvIn[1] * m[7] + pcvIn[2] * m[11] + pcvIn[3] * m[15];
}

/* =============================================================
// ChainTo()
//
// Accumulate this frame and its ancestors, excluding the parentless root.
//
// Returns: The parentless root frame.
// =============================================================*/

const CA3dFrame *
CA3dFrame::ChainTo(A3DVAL *pmAccum) const
{
const CA3dFrame        *p;
A3DVAL                  mIn[16];
int			i, j;

	for (p = this; p->m_pParent; p = p->m_pParent)
	{
		CopyMemory(mIn, pmAccum, sizeof(mIn));

		for (i = 0; i < 4; i++)
		{
			for (j = 0; j < 4; j++)
			{
				pmAccum[4 * i + j] =
					mIn[4 * i + 0] * p->m_mat[j] +
					mIn[4 * i + 1] * p->m_mat[4 + j] +
					mIn[4 * i + 2] * p->m_mat[8 + j] +
					mIn[4 * i + 3] * p->m_mat[12 + j];
			}
		}
	}

	return (p);
}

/* =============================================================
// SetPosition()
//
// Set position relative to the supplied origin, ignoring its rotation.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null position
// =============================================================*/

HRESULT
CA3dFrame::SetPosition(const CA3dFrame *pcRelativeTo, const A3DVAL *pcv)
{
A3DVAL	vBase[4];
A3DVAL	vParent[4];
A3DVAL	vWorld[4];

	if (!pcv)
		return (E_INVALIDARG);

	if (pcRelativeTo)
	{
		pcRelativeTo->GetWorldPosition(vBase);
	}
	else
	{
		vBase[0] = 0.0f;
		vBase[1] = 0.0f;
		vBase[2] = 0.0f;
		vBase[3] = 1.0f;
	}

	vWorld[0] = pcv[0] + vBase[0];
	vWorld[1] = pcv[1] + vBase[1];
	vWorld[2] = pcv[2] + vBase[2];

	if (m_pParent)
	{
		m_pParent->GetWorldPosition(vParent);
	}
	else
	{
		vParent[0] = 0.0f;
		vParent[1] = 0.0f;
		vParent[2] = 0.0f;
	}

	m_mat[12] = vWorld[0] - vParent[0];
	m_mat[13] = vWorld[1] - vParent[1];
	m_mat[14] = vWorld[2] - vParent[2];

	return (S_OK);
}

/* =============================================================
// GetPosition()
//
// Read world position or displacement from the reference origin, with w = 1.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null output
// =============================================================*/

HRESULT
CA3dFrame::GetPosition(const CA3dFrame *pcRelativeTo, A3DVAL *pvOut) const
{
A3DVAL	vBase[4];
A3DVAL	vHere[4];

	if (!pvOut)
		return (E_INVALIDARG);

	pvOut[3] = 1.0f;

	if (pcRelativeTo)
	{
		pcRelativeTo->GetWorldPosition(vBase);

		GetWorldPosition(vHere);

		pvOut[0] = vHere[0] - vBase[0];
		pvOut[1] = vHere[1] - vBase[1];
		pvOut[2] = vHere[2] - vBase[2];
	}
	else
	{
		GetWorldPosition(pvOut);
	}

	return (S_OK);
}

/* =============================================================
// SetMatrix()
//
// Copy the supplied frame matrix without validating its values.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null matrix
// =============================================================*/

STDMETHODIMP
CA3dFrame::SetMatrix(const A3DVAL *pMatrix)
{
	if (!pMatrix)
		return (E_INVALIDARG);

	memcpy(m_mat, pMatrix, sizeof(m_mat));

	return (S_OK);
}

/* =============================================================
// GetMatrix()
//
// Copy the frame matrix to the output.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null matrix
// =============================================================*/

STDMETHODIMP
CA3dFrame::GetMatrix(A3DVAL *pMatrix)
{
	if (!pMatrix)
		return (E_INVALIDARG);

	memcpy(pMatrix, m_mat, sizeof(m_mat));

	return (S_OK);
}

/* =============================================================
// TransformPoint()
//
// Transform a homogeneous point through this frame and its parents.
// =============================================================*/

void
CA3dFrame::TransformPoint(A3DVAL *pv) const
{
const CA3dFrame        *p;
const A3DVAL           *m;
A3DVAL			x, y, z, w;

	for (p = this; p; p = p->m_pParent)
	{
		m = p->m_mat;

		x = m[0] * pv[0] + m[4] * pv[1] + m[8]  * pv[2] + m[12] * pv[3];
		y = m[1] * pv[0] + m[5] * pv[1] + m[9]  * pv[2] + m[13] * pv[3];
		z = m[2] * pv[0] + m[6] * pv[1] + m[10] * pv[2] + m[14] * pv[3];
		w = m[3] * pv[0] + m[7] * pv[1] + m[11] * pv[2] + m[15] * pv[3];

		pv[0] = x;
		pv[1] = y;
		pv[2] = z;
		pv[3] = w;
	}
}

/* =============================================================
// GetParentMatrix()
// (RE) dbg:0x10076ED0; thunk dbg:0x1000101E
//
// Compose the ancestor matrices into pm, nearest parent first.
// =============================================================*/

void
CA3dFrame::GetParentMatrix(A3DVAL *pm) const
{
const CA3dFrame        *p;

	A3dMatrixIdentity(pm);

	for (p = m_pParent; p; p = p->m_pParent)
		A3dMatrixMultiply(pm, p->m_mat, pm);
}

/* =============================================================
// TransformParentPoint()
// (RE) dbg:0x10077010; thunk dbg:0x10002522
//
// Transform a point through the ancestor frames with w taken as 1.
// =============================================================*/

void
CA3dFrame::TransformParentPoint(A3DVAL *pv) const
{
A3DVAL                  m[16];

	GetParentMatrix(m);

	A3dTransformPoint(pv, m, pv);
}

/* =============================================================
// PlaneInto()
// (RE) dbg:0x100770B0; thunk dbg:0x100016D6
//
// Copy a plane and transform it as a point through the ancestor frames.
// =============================================================*/

void
CA3dFrame::PlaneInto(A3DVAL *pvOut, const A3DVAL *pcvIn) const
{
	CopyMemory(pvOut, pcvIn, 4 * sizeof(A3DVAL));

	TransformParentPoint(pvOut);
}

/* =============================================================
// PrimitiveInto()
// (RE) dbg:0x100770F0; thunk dbg:0x10003076
//
// Copy a primitive and transform its vertices and normal as points through
// the ancestor frames.
// =============================================================*/

void
CA3dFrame::PrimitiveInto(A3DPRIMITIVE *pOut, const A3DPRIMITIVE *pcIn) const
{
A3DVAL                  m[16];

	CopyMemory(pOut, pcIn, sizeof(A3DPRIMITIVE));

	GetParentMatrix(m);

	A3dTransformPoint(pOut->av[0], m, pOut->av[0]);
	A3dTransformPoint(pOut->av[1], m, pOut->av[1]);
	A3dTransformPoint(pOut->av[2], m, pOut->av[2]);

	if (pOut->cVertices == 4)
		A3dTransformPoint(pOut->av[3], m, pOut->av[3]);

	A3dTransformPoint(pOut->vNormal, m, pOut->vNormal);
}

/* =============================================================
// SegmentInto()
// (RE) dbg:0x100771F0; thunk dbg:0x10001C03
//
// Transform a segment's two padded endpoints through the ancestor frames.
// =============================================================*/

void
CA3dFrame::SegmentInto(A3DVAL *pvOut, const A3DVAL *pcvIn) const
{
A3DVAL                  m[16];

	pvOut[0] = pcvIn[0];
	pvOut[1] = pcvIn[1];
	pvOut[2] = pcvIn[2];

	pvOut[4] = pcvIn[4];
	pvOut[5] = pcvIn[5];
	pvOut[6] = pcvIn[6];

	GetParentMatrix(m);

	A3dTransformPoint(&pvOut[0], m, &pvOut[0]);
	A3dTransformPoint(&pvOut[4], m, &pvOut[4]);
}

/* =============================================================
// PointInto()
//
// Copy a point and transform the copy through the frame chain.
// =============================================================*/

void
CA3dFrame::PointInto(A3DVAL *pvOut, const A3DVAL *pcvIn) const
{
	pvOut[0] = pcvIn[0];
	pvOut[1] = pcvIn[1];
	pvOut[2] = pcvIn[2];
	pvOut[3] = pcvIn[3];

	TransformPoint(pvOut);
}

/* =============================================================
// ~CA3dGeomIface()
//
// Destroy the geometry interface base.
// =============================================================*/

CA3dGeomIface::~CA3dGeomIface(void)
{
}

/* =============================================================
// A3dXformCopyMatrix()
//
// Copy the source frame matrix. Preserve the original unchecked cast:
// passing a wall or room instead of its transform reads the wrong fields.
// =============================================================*/

void
A3dXformCopyMatrix(CA3dFrame *pTo, const void *pcFrom)
{
const CA3dFrame *pcXform = (const CA3dFrame *) pcFrom;

	CopyMemory(pTo->m_mat, pcXform->m_mat, sizeof(pTo->m_mat));
}

/* =============================================================
// GetWorldPosition()
//
// Transform the origin through the full parent chain.
// =============================================================*/

void
CA3dFrame::GetWorldPosition(A3DVAL *pvOut) const
{
A3DVAL	vOrigin[4];

	vOrigin[0] = 0.0f;
	vOrigin[1] = 0.0f;
	vOrigin[2] = 0.0f;
	vOrigin[3] = 1.0f;

	PointInto(pvOut, vOrigin);
}

/* =============================================================
// QueryInterface()
//
// Reject interface requests without writing the output.
//
// Returns:
//   E_INVALIDARG   a null output
//   E_NOINTERFACE  otherwise
// =============================================================*/

STDMETHODIMP
CA3dFrame::QueryInterface(REFIID riid, void **ppv)
{
	if (!ppv)
		return (E_INVALIDARG);

	return (E_NOINTERFACE);
}

/* =============================================================
// AddRef()
//
// Increment the reference count.
//
// Returns: The updated reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dFrame::AddRef(void)
{
	return (++m_cRef);
}

/* =============================================================
// Release()
//
// Release a reference and delete the frame at zero.
//
// Returns: The remaining reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dFrame::Release(void)
{
	if (--m_cRef)
		return (m_cRef);

	delete this;

	return (0);
}
