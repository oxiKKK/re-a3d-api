/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dReflection.h
 *
 * Declares CA3dReflection, the manual reflection state exposed through
 * IA3dReflection. The object stores its source relationship and the gain,
 * delay, position, equalization and coordinate mode used to describe a
 * reflected path.
 *
 * CA3dSource consumes these properties during control generation.
 * A3dReflection.cpp implements the property interface and orientation
 * conversion helpers; backend buffer classes perform the resulting audio
 * processing.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _A3DREFLECTION_H
#define _A3DREFLECTION_H

#include "A3dPrivate.h"

class CA3dSource;

/* =============================================================
// Class: CA3dReflection
//
// Description: Source-owned manual reflection properties.
//
// Size: 0x30
//
// (RE) Constructor: rtl:0x1000aa40; dbg:0x1001b0b0
// =============================================================*/

/* Primary vtable: dbg:0x10127F08; IA3dReflection slots 0..14 in declaration
 * order, followed by destructor slot 15. Private base table: dbg:0x10127F54. */

class CA3dReflection : public IA3dReflection
{
public:

	CA3dReflection(CA3dSource *pSrc);
	virtual ~CA3dReflection(void);

	STDMETHODIMP         QueryInterface(REFIID riid, LPVOID *ppvObj);
	STDMETHODIMP_(ULONG) AddRef(void);
	STDMETHODIMP_(ULONG) Release(void);

	STDMETHODIMP SetGainScale(A3DVAL fScale);
	STDMETHODIMP GetGainScale(LPA3DVAL pfScale);
	STDMETHODIMP SetDelay(A3DVAL fDelay);
	STDMETHODIMP GetDelay(LPA3DVAL pfDelay);
	STDMETHODIMP SetPosition3f(A3DVAL x, A3DVAL y, A3DVAL z);
	STDMETHODIMP GetPosition3f(LPA3DVAL px, LPA3DVAL py, LPA3DVAL pz);
	STDMETHODIMP SetPosition3fv(A3DVAL *pv);
	STDMETHODIMP GetPosition3fv(A3DVAL *pv);
	STDMETHODIMP SetTransformMode(DWORD dwMode);
	STDMETHODIMP GetTransformMode(DWORD *pdwMode);
	STDMETHODIMP SetEQ(A3DVAL fEQ);
	STDMETHODIMP GetEQ(LPA3DVAL pfEQ);

	/* 0x04 */ DWORD  m_dwRefCount;   /* 0 at construction */
	/* 0x08 */ FLOAT  m_fGainScale;   /* Linear gain; initially 0.5. */
	/* 0x0C */ FLOAT  m_fDelay;       /* Seconds; initially 0.1. */
	/* 0x10 */ A3DVAL m_vPosition[3]; /* Position in the selected frame. */

	/* 0x1C */ DWORD m_Unknown_0x1C;
	/* 0x20 */ DWORD m_dwTransformMode; /* Initially 0; setter requires bit 0. */
	/* 0x24 */ FLOAT m_fEQ;             /* EQ scale in [0,1]. */

	/* Sequence identifier, at least 0x80000000. */
	/* 0x28 */ DWORD m_dwId;

	/* Borrowed source; Release removes this reflection from its list. */
	/* 0x2C */ CA3dSource *m_pSource;
};

typedef int A3dReflectionSizeCheck[(sizeof(CA3dReflection) == 0x30) ? 1 : -1];

void A3dAnglesToVectors(const A3DVAL *pAngles, A3DVAL *pvFront, A3DVAL *pvUp,
                        int nCoordSystem);
void A3dVectorsToAngles(const A3DVAL *pvFront, const A3DVAL *pvUp,
                        A3DVAL *pAngles, int nCoordSystem);

#endif /* _A3DREFLECTION_H */
