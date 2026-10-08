/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dFrame.h
 *
 * Declares CA3dFrame and its IA3dTransform interface for placing geometry
 * relative to other geometry. Each frame contains a matrix and borrowed
 * owner and parent pointers, allowing rooms, walls and openings to form
 * transform chains.
 *
 * The methods support position and orientation access, matrix replacement
 * and point or vector conversion. Polygon.h embeds frames in polygon
 * objects; A3dFrame.cpp implements the transformations using the shared
 * matrix routines.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _A3DFRAME_H
#define _A3DFRAME_H

#include "A3dPrivate.h"
#include "LinkList.h"
#include "Corners.h"

class CA3dOpening;
class CA3dRoot;
class CA3dWall;

int  A3dFrameIsSquare(const A3DVAL *pvFront, const A3DVAL *pvUp);

void A3dFrameSet(class CA3dFrame *pTransform, void *pOwner,
		 LPA3DVAL pvPosition,
		 LPA3DVAL pvFront, LPA3DVAL pvUp);

/* Private transform interface, slots 0..7 in declaration order. */

#undef  INTERFACE
#define INTERFACE IA3dTransform

DECLARE_INTERFACE_(IA3dTransform, IUnknown)
{
	STDMETHOD(QueryInterface)	(THIS_ REFIID, void **) PURE;
	STDMETHOD_(ULONG, AddRef)	(THIS) PURE;
	STDMETHOD_(ULONG, Release)	(THIS) PURE;
	STDMETHOD(SetMatrix)		(THIS_ const A3DVAL *) PURE;
	STDMETHOD(GetMatrix)		(THIS_ A3DVAL *) PURE;
	STDMETHOD(Translate3fv)		(THIS_ LPA3DVAL) PURE;
	STDMETHOD(Rotate3fv)		(THIS_ A3DVAL, LPA3DVAL) PURE;
	STDMETHOD(Scale3f)		(THIS_ A3DVAL, A3DVAL,
					       A3DVAL) PURE;
};

/* =============================================================
// Class: CA3dFrame
//
// Description: Column-major transform with borrowed owner and parent
// references.
//
// Size: 0x50
// =============================================================*/

/* Transform vtable: dbg:0x101391c8; slot 8 scalar deleting destructor. */
class CA3dFrame : public IA3dTransform
{
public:
	CA3dFrame(void);
	virtual ~CA3dFrame(void);

	STDMETHODIMP		QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG)	AddRef(void);
	STDMETHODIMP_(ULONG)	Release(void);

	STDMETHODIMP	SetMatrix(const A3DVAL *pMatrix);
	STDMETHODIMP	GetMatrix(A3DVAL *pMatrix);
	STDMETHODIMP	Translate3fv(LPA3DVAL pv);
	STDMETHODIMP	Rotate3fv(A3DVAL fDegrees, LPA3DVAL pv);
	STDMETHODIMP	Scale3f(A3DVAL x, A3DVAL y, A3DVAL z);

	void	TransformPoint(A3DVAL *pv) const;
	void	GetParentMatrix(A3DVAL *pm) const;
	void	TransformParentPoint(A3DVAL *pv) const;
	void	PlaneInto(A3DVAL *pvOut, const A3DVAL *pcvIn) const;
	void	PrimitiveInto(struct _A3DPRIMITIVE *pOut,
			      const struct _A3DPRIMITIVE *pcIn) const;
	void	SegmentInto(A3DVAL *pvOut, const A3DVAL *pcvIn) const;
	void	PointInto(A3DVAL *pvOut,
			  const A3DVAL *pcvIn) const;
	const CA3dFrame *ChainTo(A3DVAL *pmAccum) const;
	void	VectorInto(A3DVAL *pvOut,
			   const A3DVAL *pcvIn) const;
	void	GetWorldPosition(A3DVAL *pvOut) const;
	void	CopyFrameFrom(void *pOwner, const CA3dFrame *pcFrom);

	HRESULT	GetPosition(const CA3dFrame *pcRelativeTo,
			    A3DVAL *pvOut) const;
	HRESULT	SetPosition(const CA3dFrame *pcRelativeTo,
			    const A3DVAL *pcv);
	HRESULT	GetOrientation(const CA3dFrame *pcRelativeTo,
			       A3DVAL *pvFront, A3DVAL *pvUp) const;
	HRESULT	SetOrientation(const CA3dFrame *pcRelativeTo,
			       const A3DVAL *pcvFront,
			       const A3DVAL *pcvUp);

public:
	LONG		m_cRef;			/* 0x04; non-interlocked reference count. */
	A3DVAL		m_mat[16];		/* 0x08; right, up, front and position columns. */
	void		*m_pOwner; /* 0x48; borrowed owner. */
	CA3dFrame	*m_pParent;		/* 0x4C; borrowed parent frame. */
};

void	A3dXformCopyMatrix(class CA3dFrame *pTo,
			   const void *pcFrom);

/* Default frame: origin, +Z front and +Y up. */

extern const A3DVAL vDefaultPosition[4];
extern const A3DVAL vDefaultFront[4];
extern const A3DVAL vDefaultUp[4];

#endif /* _A3DFRAME_H */
