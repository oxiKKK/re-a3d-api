/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dMatrix.cpp
 *
 * Implements the root geometry matrix stack and the matrix arithmetic
 * shared by placement and tracing. API operations load, combine,
 * translate, rotate and scale the current transform before geometry or
 * sources are bound to it.
 *
 * The lower-level routines multiply and invert matrices, transform
 * directions and convert Cartesian directions to polar coordinates.
 * Listener transform and velocity preparation for a tracing pass also
 * lives here. CA3dFrame and the source tracing code use these operations
 * to relate world, object and listener coordinates.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "A3dMatrix.h"
#include "A3d3.h"
#include "A3dFrame.h"
#include "apimapper.h"
#include "A3dSource.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Frame-validation  */
#define A3D_FRAME_MAX_ABS_DOT           0.000001
#define A3D_FRAME_MIN_LENGTH_SQUARED    1.0e-12

/* (RE) Identity matrix: dbg:0x10146EF0. */

static const A3DVAL matIdentity[16] =
{
	1.0f, 0.0f, 0.0f, 0.0f,
	0.0f, 1.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 1.0f, 0.0f,
	0.0f, 0.0f, 0.0f, 1.0f
};

/* =============================================================
// PushMatrix()
// (RE) rtl:0x10004D70; dbg:0x10010440
//
// Duplicate the top matrix and its identity flag at the next stack level.
//
// Returns:
//   S_OK
//   E_FAIL  when the matrix stack is full
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dRoot::PushMatrix(void)
{
	m_nMatrixDepth++;

	if (m_nMatrixDepth == A3D_MATRIX_STACK_DEPTH)
	{
		m_nMatrixDepth = A3D_MATRIX_STACK_DEPTH - 1;

		return (E_FAIL);
	}

	CopyMemory(m_amatStack[m_nMatrixDepth], m_amatStack[m_nMatrixDepth - 1],
		   sizeof(m_amatStack[0]));

	m_adwMatrixIsIdentity[m_nMatrixDepth] =
		m_adwMatrixIsIdentity[m_nMatrixDepth - 1];

	return (S_OK);
}

/* =============================================================
// PopMatrix()
// (RE) rtl:0x10004DE0; dbg:0x10010520
//
// Remove the top matrix stack level.
//
// Returns:
//   S_OK
//   E_FAIL  when no matrix has been pushed
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dRoot::PopMatrix(void)
{
	if (m_nMatrixDepth-- != 0)
		return (S_OK);

	m_nMatrixDepth = 0;

	return (E_FAIL);
}

/* =============================================================
// LoadIdentity()
// (RE) rtl:0x10004E20; dbg:0x10010590
//
// Replace the top matrix with identity and set its identity flag.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dRoot::LoadIdentity(void)
{
	A3dMatrixIdentity(m_amatStack[m_nMatrixDepth]);

	m_adwMatrixIsIdentity[m_nMatrixDepth] = 1;

	return (S_OK);
}

/* =============================================================
// LoadMatrix()
// (RE) rtl:0x10004E60; dbg:0x10010600
//
// Replace the top matrix and update its identity flag.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dRoot::LoadMatrix(LPA3DVAL pMatrix)
{
	CopyMemory(m_amatStack[m_nMatrixDepth], pMatrix, sizeof(m_amatStack[0]));

	m_adwMatrixIsIdentity[m_nMatrixDepth] =
		!memcmp(m_amatStack[m_nMatrixDepth], matIdentity,
			sizeof(m_amatStack[0]));

	return (S_OK);
}

/* =============================================================
// GetMatrix()
// (RE) rtl:0x10004ED0; dbg:0x100106C0
//
// Copy the top matrix to the output.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetMatrix(LPA3DVAL pMatrix)
{
	if (!pMatrix)
		return (E_POINTER);

	CopyMemory(pMatrix, m_amatStack[m_nMatrixDepth], sizeof(m_amatStack[0]));

	return (S_OK);
}

/* =============================================================
// MultMatrix()
// (RE) rtl:0x10004F10; dbg:0x10010720
//
// Post-multiply the top matrix and update its identity flag.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dRoot::MultMatrix(LPA3DVAL pMatrix)
{
	A3dMatrixMultiply(m_amatStack[m_nMatrixDepth], pMatrix,
			  m_amatStack[m_nMatrixDepth]);

	m_adwMatrixIsIdentity[m_nMatrixDepth] =
		!memcmp(m_amatStack[m_nMatrixDepth], matIdentity,
			sizeof(m_amatStack[0]));

	return (S_OK);
}

/* =============================================================
// Translate3f()
// (RE) rtl:0x10005040; dbg:0x10010990
//
// Translate the top matrix by three scalar components.
//
// Returns: The Translate3fv result.
// =============================================================*/

STDMETHODIMP
CA3dRoot::Translate3f(A3DVAL x, A3DVAL y, A3DVAL z)
{
A3DVAL v[3];

	v[0] = x;
	v[1] = y;
	v[2] = z;

	return (Translate3fv(v));
}

/* =============================================================
// Translate3fv()
// (RE) rtl:0x10005070; dbg:0x100109E0
//
// Translate the top matrix and update its identity flag.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dRoot::Translate3fv(LPA3DVAL pv)
{
A3DVAL mat[16];

	A3dMatrixIdentity(mat);

	mat[12] = pv[0];
	mat[13] = pv[1];
	mat[14] = pv[2];

	A3dMatrixMultiply(m_amatStack[m_nMatrixDepth], mat,
			  m_amatStack[m_nMatrixDepth]);

	m_adwMatrixIsIdentity[m_nMatrixDepth] =
		!memcmp(m_amatStack[m_nMatrixDepth], matIdentity,
			sizeof(m_amatStack[0]));

	return (S_OK);
}

/* =============================================================
// Rotate3f()
// (RE) rtl:0x100051B0; dbg:0x10010B20
//
// Rotate the top matrix about the supplied axis in degrees.
//
// Returns: The Rotate3fv result.
// =============================================================*/

STDMETHODIMP
CA3dRoot::Rotate3f(A3DVAL fDegrees, A3DVAL x, A3DVAL y, A3DVAL z)
{
A3DVAL v[3];

	v[0] = x;
	v[1] = y;
	v[2] = z;

	return (Rotate3fv(fDegrees, v));
}

/* =============================================================
// Rotate3fv()
// (RE) rtl:0x100051E0; dbg:0x10010B70
//
// Rotate the top matrix, normalizing the caller axis in place. Preserve the
// original y*z terms at matrix indices 2 and 8 in the general-axis path.
//
// Returns:
//   S_OK
//   E_FAIL  a zero-length axis
// =============================================================*/

STDMETHODIMP
CA3dRoot::Rotate3fv(A3DVAL fDegrees, LPA3DVAL pv)
{
A3DVAL mat[16];
double dAngle;
A3DVAL s, c, t;
A3DVAL x, y, z;
A3DVAL fLength;

	dAngle = fDegrees * A3D_DEGREES_TO_RADIANS;

	s = (A3DVAL) sin(dAngle);
	c = (A3DVAL) cos(dAngle);

	if (pv[0] == 1.0f && pv[1] == 0.0f && pv[2] == 0.0f)
	{
		A3dMatrixIdentity(mat);

		mat[5]  =  c;
		mat[6]  =  s;
		mat[9]  = -s;
		mat[10] =  c;
	}
	else if (pv[0] == 0.0f && pv[1] == 1.0f && pv[2] == 0.0f)
	{
		A3dMatrixIdentity(mat);

		mat[0]  =  c;
		mat[2]  = -s;
		mat[8]  =  s;
		mat[10] =  c;
	}
	else if (pv[0] == 0.0f && pv[1] == 0.0f && pv[2] == 1.0f)
	{
		A3dMatrixIdentity(mat);

		mat[0] =  c;
		mat[1] =  s;
		mat[4] = -s;
		mat[5] =  c;
	}
	else
	{
		fLength = (A3DVAL) sqrt(pv[0] * pv[0] + pv[1] * pv[1] + pv[2] * pv[2]);

		if (fLength == 0.0f)
			return (E_FAIL);

		x = pv[0] / fLength;
		y = pv[1] / fLength;
		z = pv[2] / fLength;

		pv[0] = x;
		pv[1] = y;
		pv[2] = z;

		t = 1.0f - c;

		mat[0]  = t * x * x + c;
		mat[1]  = t * x * y + s * z;
		mat[2]  = t * y * z - s * y;
		mat[3]  = 0.0f;

		mat[4]  = t * x * y - s * z;
		mat[5]  = t * y * y + c;
		mat[6]  = t * y * z + s * x;
		mat[7]  = 0.0f;

		mat[8]  = t * y * z + s * y;
		mat[9]  = t * y * z - s * x;
		mat[10] = t * z * z + c;
		mat[11] = 0.0f;

		mat[12] = 0.0f;
		mat[13] = 0.0f;
		mat[14] = 0.0f;
		mat[15] = 1.0f;
	}

	A3dMatrixMultiply(m_amatStack[m_nMatrixDepth], mat,
			  m_amatStack[m_nMatrixDepth]);

	m_adwMatrixIsIdentity[m_nMatrixDepth] =
		!memcmp(m_amatStack[m_nMatrixDepth], matIdentity,
			sizeof(m_amatStack[0]));

	return (S_OK);
}

/* =============================================================
// Scale3f()
// (RE) rtl:0x10005550; dbg:0x10011020
//
// Scale the top matrix by three scalar components.
//
// Returns: The Scale3fv result.
// =============================================================*/

STDMETHODIMP
CA3dRoot::Scale3f(A3DVAL x, A3DVAL y, A3DVAL z)
{
A3DVAL v[3];

	v[0] = x;
	v[1] = y;
	v[2] = z;

	return (Scale3fv(v));
}

/* =============================================================
// Scale3fv()
// (RE) rtl:0x10005580; dbg:0x10011070
//
// Scale the top matrix and update its identity flag.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dRoot::Scale3fv(LPA3DVAL pv)
{
A3DVAL mat[16];

	A3dMatrixIdentity(mat);

	mat[0]  = pv[0];
	mat[5]  = pv[1];
	mat[10] = pv[2];

	A3dMatrixMultiply(m_amatStack[m_nMatrixDepth], mat,
			  m_amatStack[m_nMatrixDepth]);

	m_adwMatrixIsIdentity[m_nMatrixDepth] =
		!memcmp(m_amatStack[m_nMatrixDepth], matIdentity,
			sizeof(m_amatStack[0]));

	return (S_OK);
}

/* =============================================================
// A3dMatrixTranslateBy()
// (RE) rtl:0x1000E310; dbg:0x10021860; thunk dbg:0x1000158C
//
// Post-multiply a matrix by a translation.
// =============================================================*/

void
A3dMatrixTranslateBy(const A3DVAL *pv, A3DMATRIX pm)
{
A3DVAL mat[16];

	A3dMatrixIdentity(mat);

	mat[12] = pv[0];
	mat[13] = pv[1];
	mat[14] = pv[2];

	A3dMatrixMultiply(pm, mat, pm);
}

/* =============================================================
// A3dMatrixRotateBy()
//
// Post-multiply by a rotation, normalizing the caller axis in place; zero
// axis leaves the matrix unchanged. Some sign-paired terms remain inferred
// from the rotation-matrix form.
// =============================================================*/

void
A3dMatrixRotateBy(A3DVAL fDegrees, A3DVAL *pv, A3DMATRIX pm)
{
A3DVAL	mRot[16];
double	fRadians;
double	c, s, t;
double	x, y, z;
double	fLength;

	fLength = sqrt(pv[0] * pv[0] + pv[1] * pv[1] + pv[2] * pv[2]);

	fRadians = fDegrees * A3D_DEGREES_TO_RADIANS;

	c = cos(fRadians);
	s = sin(fRadians);

	if (fLength == 0.0)
		return;

	x = pv[0] / fLength;
	y = pv[1] / fLength;
	z = pv[2] / fLength;

	pv[0] = (A3DVAL) x;
	pv[1] = (A3DVAL) y;
	pv[2] = (A3DVAL) z;

	t = 1.0 - c;

	mRot[0]  = (A3DVAL) (t * x * x + c);
	mRot[1]  = (A3DVAL) (t * x * y + s * z);
	mRot[2]  = (A3DVAL) (t * x * z - s * y);
	mRot[3]  = 0.0f;

	mRot[4]  = (A3DVAL) (t * x * y - s * z);
	mRot[5]  = (A3DVAL) (t * y * y + c);
	mRot[6]  = (A3DVAL) (t * y * z + s * x);
	mRot[7]  = 0.0f;

	mRot[8]  = (A3DVAL) (t * x * z + s * y);
	mRot[9]  = (A3DVAL) (t * y * z - s * x);
	mRot[10] = (A3DVAL) (t * z * z + c);
	mRot[11] = 0.0f;

	mRot[12] = 0.0f;
	mRot[13] = 0.0f;
	mRot[14] = 0.0f;
	mRot[15] = 1.0f;

	A3dMatrixMultiply(pm, mRot, pm);
}

/* =============================================================
// A3dMatrixScaleBy()
//
// Post-multiply a matrix by a three-component scale.
// =============================================================*/

void
A3dMatrixScaleBy(const A3DVAL *pv, A3DMATRIX pm)
{
A3DVAL	mScale[16];

	A3dMatrixIdentity(mScale);

	mScale[0]  = pv[0];
	mScale[5]  = pv[1];
	mScale[10] = pv[2];

	A3dMatrixMultiply(pm, mScale, pm);
}

/* =============================================================
// A3dMatrixMultiply()
// (RE) dbg:0x10010800; thunk dbg:0x10002BAD
//
// Multiply two column-major matrices; the output may alias either input.
// =============================================================*/

void
A3dMatrixMultiply(
	const A3DMATRIX pA,
	const A3DMATRIX pB,
	A3DMATRIX pOut)
{
A3DVAL matWork[16];
A3DVAL a0, a1, a2, a3;
int i;

	for (i = 0; i < 4; i++)
	{

		a0 = pA[i];
		a1 = pA[i + 4];
		a2 = pA[i + 8];
		a3 = pA[i + 12];

		matWork[i]      = a2 * pB[2]  + a3 * pB[3]  + a1 * pB[1]  + a0 * pB[0];
		matWork[i + 4]  = a3 * pB[7]  + a2 * pB[6]  + a0 * pB[4]  + a1 * pB[5];
		matWork[i + 8]  = a0 * pB[8]  + a2 * pB[10] + a1 * pB[9]  + a3 * pB[11];
		matWork[i + 12] = a2 * pB[14] + a3 * pB[15] + a1 * pB[13] + a0 * pB[12];
	}

	CopyMemory(pOut, matWork, sizeof(matWork));
}

/* =============================================================
// A3dMatrixIdentity()
// (RE) dbg:0x10010AF0
//
// Copy the identity matrix to the output.
// =============================================================*/

void
A3dMatrixIdentity(A3DMATRIX pMatrix)
{
	CopyMemory(pMatrix, matIdentity, sizeof(matIdentity));
}

/* =============================================================
// A3dFrameIsSquare()
// (RE) dbg:0x1007c470
//
// Test whether front and up are nonzero and perpendicular within tolerance.
//
// Returns: 1 when the vectors pass both tests; 0 otherwise.
// =============================================================*/

int
A3dFrameIsSquare(const A3DVAL *pvFront, const A3DVAL *pvUp)
{
double dDot;

	dDot = pvUp[0] * pvFront[0] + pvFront[2] * pvUp[2] + pvFront[1] * pvUp[1];

	if (fabs(dDot) > A3D_FRAME_MAX_ABS_DOT)
		return (0);

	if (pvFront[1] * pvFront[1] + pvFront[2] * pvFront[2] + pvFront[0] * pvFront[0] < A3D_FRAME_MIN_LENGTH_SQUARED)
		return (0);

	if (pvUp[1] * pvUp[1] + pvUp[2] * pvUp[2] + pvUp[0] * pvUp[0] < A3D_FRAME_MIN_LENGTH_SQUARED)
		return (0);

	return (1);
}

/* =============================================================
// A3dMatrixInvert()
// (RE) rtl:0x10009f10; dbg:0x10019bc0; thunk dbg:0x10003C42
//
// Invert a matrix, writing identity for a singular affine input.
// =============================================================*/

void
A3dMatrixInvert(const A3DMATRIX pIn, A3DMATRIX pOut)
{
A3DVAL out[16];
A3DVAL c0, c1, c2;
A3DVAL a, b, c, d;
A3DVAL e0, e1, e2, e3;
A3DVAL f0, f1;
double dDet;
double r;

	if (pIn[3] != 0.0f || pIn[7] != 0.0f ||
	    pIn[11] != 0.0f || pIn[15] != 1.0f)
	{
		A3dMatrixInvertGeneral(pIn, pOut);

		return;
	}

	c0 = pIn[5] * pIn[10] - pIn[6] * pIn[9];
	c1 = pIn[9] * pIn[2]  - pIn[1] * pIn[10];
	c2 = pIn[6] * pIn[1]  - pIn[2] * pIn[5];

	dDet = pIn[4] * c1 + pIn[8] * c2 + pIn[0] * c0;

	if (dDet == 0.0)
	{
		A3dMatrixIdentity(pOut);

		return;
	}

	r = 1.0 / dDet;

	out[0] = (A3DVAL) (c0 * r);
	out[1] = (A3DVAL) (c1 * r);
	out[2] = (A3DVAL) (c2 * r);
	out[3] = 0.0f;

	a = (A3DVAL) (r * pIn[0]);
	b = (A3DVAL) (pIn[4] * r);
	c = (A3DVAL) (pIn[8] * r);
	d = (A3DVAL) (pIn[12] * r);

	out[4] = pIn[6] * c - pIn[10] * b;
	out[5] = pIn[10] * a - pIn[2] * c;
	out[6] = pIn[2] * b - pIn[6] * a;
	out[7] = 0.0f;

	e0 = pIn[5] * a - pIn[1] * b;
	e1 = pIn[9] * a - pIn[1] * c;
	e2 = pIn[9] * b - pIn[5] * c;
	e3 = pIn[13] * b - pIn[5] * d;

	out[8]  = e2;
	out[9]  = -e1;
	out[10] = e0;
	out[11] = 0.0f;

	f0 = pIn[1] * d - pIn[13] * a;
	f1 = pIn[13] * c - pIn[9] * d;

	out[12] = -(pIn[6] * f1 - pIn[10] * e3 + pIn[14] * e2);
	out[13] = pIn[2] * f1 + pIn[14] * e1 + pIn[10] * f0;
	out[14] = -(pIn[2] * e3 + pIn[6] * f0 + pIn[14] * e0);
	out[15] = 1.0f;

	CopyMemory(pOut, out, sizeof(out));
}

/* =============================================================
// A3dMatrixInvertGeneral()
//
// Invert a general matrix. Preserve unchecked determinants; differential
// comparisons do not match the x87 spill precision.
// =============================================================*/

void
A3dMatrixInvertGeneral(const A3DMATRIX pIn, A3DMATRIX pOut)
{
A3DVAL a00, a01, a10, a11;
A3DVAL b00, b01, b10, b11;
A3DVAL c00, c01, c10, c11;
A3DVAL s00, s01, s10, s11;
A3DVAL i00, i01, i10, i11;
double r;

	r = 1.0 / (pIn[0] * pIn[5] - pIn[1] * pIn[4]);

	a00 = (A3DVAL)  (r * pIn[5]);
	a01 = (A3DVAL) -(r * pIn[1]);
	a10 = (A3DVAL) -(r * pIn[4]);
	a11 = (A3DVAL)  (pIn[0] * r);

	b00 = a00 * pIn[2] + a01 * pIn[6];
	b10 = a10 * pIn[2] + a11 * pIn[6];
	b01 = a01 * pIn[7] + a00 * pIn[3];
	b11 = a11 * pIn[7] + a10 * pIn[3];

	c00 = a10 * pIn[9]  + a00 * pIn[8];
	c10 = a10 * pIn[13] + a00 * pIn[12];
	c01 = a11 * pIn[9]  + a01 * pIn[8];
	c11 = a11 * pIn[13] + a01 * pIn[12];

	s00 = c00 * pIn[2] + c01 * pIn[6] - pIn[10];
	s10 = c10 * pIn[2] + c11 * pIn[6] - pIn[14];
	s01 = c01 * pIn[7] + c00 * pIn[3] - pIn[11];
	s11 = c11 * pIn[7] + c10 * pIn[3] - pIn[15];

	r = 1.0 / (s00 * s11 - s01 * s10);

	i00 = (A3DVAL)  (r * s11);
	i01 = (A3DVAL) -(r * s01);
	i10 = (A3DVAL) -(r * s10);
	i11 = (A3DVAL)  (r * s00);

	pOut[8]  = i00 * c00 + i01 * c10;
	pOut[12] = i10 * c00 + i11 * c10;
	pOut[9]  = i00 * c01 + i01 * c11;
	pOut[13] = i10 * c01 + i11 * c11;

	pOut[2] = i00 * b00 + i10 * b01;
	pOut[6] = i00 * b10 + i10 * b11;
	pOut[3] = i01 * b00 + i11 * b01;
	pOut[7] = i01 * b10 + i11 * b11;

	pOut[0] = a00 - (c10 * pOut[3] + c00 * pOut[2]);
	pOut[4] = a10 - (c10 * pOut[7] + c00 * pOut[6]);
	pOut[1] = a01 - (c11 * pOut[3] + c01 * pOut[2]);
	pOut[5] = a11 - (c11 * pOut[7] + c01 * pOut[6]);

	pOut[10] = -i00;
	pOut[11] = -i01;
	pOut[14] = -i10;
	pOut[15] = -i11;
}
/* =============================================================
// A3dMatrixGetTranslation()
// (RE) dbg:0x10011E70; thunk dbg:0x1000306C
//
// Copy the three translation components from the last column.
// =============================================================*/

void
A3dMatrixGetTranslation(const A3DMATRIX pm, A3DVAL *pv)
{
	pv[0] = pm[12];
	pv[1] = pm[13];
	pv[2] = pm[14];
}

/* =============================================================
// A3dTransformDir()
//
// Transform xyz through the first three stored columns, including their
// fourth elements; the output may alias the input.
// =============================================================*/

void
A3dTransformDir(const A3DVAL *pv, const A3DMATRIX pm, A3DVAL *pvOut)
{
A3DVAL  av[3];
int     i;

	for (i = 0; i < 3; i++)
	{
		av[i] = pm[4 * i + 0] * pv[0] +
			pm[4 * i + 1] * pv[1] +
			pm[4 * i + 2] * pv[2] +
			pm[4 * i + 3];
	}

	pvOut[0] = av[0];
	pvOut[1] = av[1];
	pvOut[2] = av[2];
}

/* =============================================================
// A3dToPolarDeg()
//
// Write azimuth and elevation in degrees, followed by distance.
// =============================================================*/

void
A3dToPolarDeg(const A3DVAL *pv, A3DVAL *pvPolar)
{
A3DVAL	vRadians[3];

	A3dToPolar(pv, vRadians);

	pvPolar[0] = vRadians[0] * A3D_RADIANS_TO_DEGREES;
	pvPolar[1] = vRadians[1] * A3D_RADIANS_TO_DEGREES;
	pvPolar[2] = vRadians[2];
}
/* =============================================================
// A3dToPolar()
// (RE) rtl:0x1000a3b0; dbg:0x1001a380; thunk dbg:0x10002CB6
//
// Write azimuth and elevation in radians, followed by distance.
// =============================================================*/

void
A3dToPolar(const A3DVAL *pv, A3DVAL *pvPolar)
{
A3DVAL fFlat;
A3DVAL fAzimuth;
A3DVAL fElevation;

	if (pv[0] == 0.0f && pv[2] == 0.0f)
	{
		fAzimuth = 0.0f;
		fFlat    = 0.0f;
	}
	else
	{
		fAzimuth = (A3DVAL) atan2(-pv[0], -pv[2]);
		fFlat    = (A3DVAL) sqrt(pv[0] * pv[0] + pv[2] * pv[2]);
	}

	if (fFlat == 0.0f)
	{
		if (pv[1] > 0.0f)
			fElevation = A3D_HALF_PI;
		else if (pv[1] < 0.0f)
			fElevation = -A3D_HALF_PI;
		else
			fElevation = 0.0f;
	}
	else
	{
		fElevation = (A3DVAL) atan2(pv[1], fFlat);
	}

	pvPolar[0] = fAzimuth;
	pvPolar[1] = fElevation;
	pvPolar[2] = (A3DVAL) sqrt(pv[0] * pv[0] + pv[1] * pv[1] +
				   pv[2] * pv[2]);
}

/* =============================================================
// A3dTraceBegin()
// (RE) rtl:0x100196A0; dbg:0x1003CFB0; thunk dbg:0x10001212
//
// Build the middle, left and right listener matrices for tracing.
// =============================================================*/

void
A3dTraceBegin(CA3dRoot *pApi, A3DMATRIX pmLeft, A3DMATRIX pmMid, A3DMATRIX pmRight)
{
	CopyMemory(pmMid, pApi->m_matListener,
		   A3D_MATRIX_ELEMENTS * sizeof(A3DVAL));

	A3dMatrixTranslateBy(pApi->m_vPosition, pmMid);

	A3dMatrixOrient(pmMid, pApi->m_vOrientFront, pApi->m_vOrientUp,
			pApi->m_dwCoordSystem);

	CopyMemory(pmLeft, pmMid, A3D_MATRIX_ELEMENTS * sizeof(A3DVAL));

	A3dMatrixTranslateBy(pApi->m_vEarOffsetLeft, pmLeft);

	CopyMemory(pmRight, pmMid, A3D_MATRIX_ELEMENTS * sizeof(A3DVAL));

	A3dMatrixTranslateBy(pApi->m_vEarOffsetRight, pmRight);
}

/* =============================================================
// A3dMatrixOrient()
// (RE) rtl:0x1000A590; dbg:0x1001A680; thunk dbg:0x1000245A
//
// Post-multiply pm by the basis of right, up and negated front, keeping its
// translation column. A nonzero coordinate system reverses the right vector.
// Original defect: normalize up with front's length, then front with up's.
// =============================================================*/

void
A3dMatrixOrient(A3DMATRIX pm, const A3DVAL *pvFront, const A3DVAL *pvUp,
		DWORD dwCoordSystem)
{
A3DVAL mat[16];
A3DVAL fFx, fFy, fFz;
A3DVAL fUx, fUy, fUz;
A3DVAL fRx, fRy, fRz;
A3DVAL  fLen;
int     i;

	fFx = pvFront[0];
	fFy = pvFront[1];
	fFz = pvFront[2];

	fUx = pvUp[0];
	fUy = pvUp[1];
	fUz = pvUp[2];

	fLen = A3dFastSqrt(fFx * fFx + fFy * fFy + fFz * fFz);

	fUx = 1.0f / fLen * fUx;
	fUy = 1.0f / fLen * fUy;
	fUz = 1.0f / fLen * fUz;

	fLen = A3dFastSqrt(fUx * fUx + fUy * fUy + fUz * fUz);

	fFx = -1.0f / fLen * fFx;
	fFy = -1.0f / fLen * fFy;
	fFz = -1.0f / fLen * fFz;

	if (dwCoordSystem)
	{
		fRx = fFy * fUz - fFz * fUy;
		fRy = fFz * fUx - fFx * fUz;
		fRz = fFx * fUy - fFy * fUx;
	}
	else
	{
		fRx = fUy * fFz - fUz * fFy;
		fRy = fUz * fFx - fUx * fFz;
		fRz = fUx * fFy - fUy * fFx;
	}

	CopyMemory(mat, pm, A3D_MATRIX_ELEMENTS * sizeof(A3DVAL));

	/* The basis column's zero w term turns a negative-zero sum positive. */
	for (i = 0; i < 4; i++)
	{
		pm[i]     = mat[i] * fRx + mat[i + 4] * fRy + mat[i + 8] * fRz +
			    mat[i + 12] * 0.0f;
		pm[i + 4] = mat[i] * fUx + mat[i + 4] * fUy + mat[i + 8] * fUz +
			    mat[i + 12] * 0.0f;
		pm[i + 8] = mat[i] * fFx + mat[i + 4] * fFy + mat[i + 8] * fFz +
			    mat[i + 12] * 0.0f;
	}

	pm[12] = mat[12];
	pm[13] = mat[13];
	pm[14] = mat[14];
	pm[15] = mat[15];
}

/* =============================================================
// GetListenerVelocity()
// (RE) rtl:0x10019920; dbg:0x1003D0A0; thunk dbg:0x10001267
//
// Transform listener velocity into its own frame. Clearing the unread
// translation column may be original redundancy or a helper mismatch.
// Both references skip the transform when the velocity-presence field is zero;
// this body always performs it.
// =============================================================*/

void
CA3dRoot::GetListenerVelocity(A3DVAL *fXFormedVel)
{
A3DVAL mat[16];

	ASSERT((fXFormedVel != 0 && !IsBadReadPtr(fXFormedVel, sizeof(A3DVAL))));

	CopyMemory(mat, m_matListener, sizeof(mat));

	mat[12] = 0.0f;
	mat[13] = 0.0f;
	mat[14] = 0.0f;

	A3dTransformDir(m_vVelocity, mat, fXFormedVel);
}

