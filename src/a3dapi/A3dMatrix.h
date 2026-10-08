/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dMatrix.h
 *
 * Declares matrix-stack limits and shared transformation routines for
 * geometry, coordinate frames and source tracing. A3D matrices are
 * column-major, indexed as column*4+row, as defined by ia3dapi.h.
 *
 * The declarations cover multiplication, inversion, translation,
 * rotation, scaling and direction conversion, along with listener
 * preparation for tracing. A3dMatrix.cpp implements the routines and the
 * CA3dRoot matrix API; the root's stack storage is declared in A3d3.h.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _A3DMATRIX_H
#define _A3DMATRIX_H

#include "A3dPrivate.h"
#include "../A3dMath.h"

class CA3dRoot;

void A3dMatrixIdentity(A3DMATRIX pMatrix);

void A3dMatrixMultiply(const A3DMATRIX pA, const A3DMATRIX pB, A3DMATRIX pOut);
void A3dMatrixInvert(const A3DMATRIX pIn, A3DMATRIX pOut);

void A3dMatrixInvertGeneral(const A3DMATRIX pIn, A3DMATRIX pOut);

/* Right angle in radians. */

#define A3D_HALF_PI  1.5707964f

/* Four-by-four matrix element count. */

#define A3D_MATRIX_ELEMENTS     16

#define A3D_MATRIX_STACK_DEPTH  32

void A3dTraceBegin(class CA3dRoot *pApi, A3DMATRIX pmLeft,
                   A3DMATRIX pmMid, A3DMATRIX pmRight);

void A3dTransformDir(const A3DVAL *pv, const A3DMATRIX pm, A3DVAL *pvOut);

void A3dMatrixTranslateBy(const A3DVAL *pv, A3DMATRIX pm);
void A3dMatrixScaleBy(const A3DVAL *pv, A3DMATRIX pm);
void A3dMatrixRotateBy(A3DVAL fDegrees, A3DVAL *pv, A3DMATRIX pm);
void A3dMatrixOrient(A3DMATRIX pm, const A3DVAL *pvFront, const A3DVAL *pvUp,
		     DWORD dwCoordSystem);

void A3dMatrixGetTranslation(const A3DMATRIX pm, A3DVAL *pv);

void A3dToPolar(const A3DVAL *pv, A3DVAL *pvPolar);
void A3dToPolarDeg(const A3DVAL *pv, A3DVAL *pvPolar);

#endif /* _A3DMATRIX_H */
