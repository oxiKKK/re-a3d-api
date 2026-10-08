/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3d3.h
 *
 * Declares CA3dRoot, the shared engine object behind the A3D API,
 * geometry, listener and private scene interfaces. The class connects
 * application calls to the resource manager and owns the source registry,
 * geometry pools, matrix stack and rendering configuration.
 *
 * Its state also records listener placement, tracing progress, reflection
 * limits and reverb properties. These declarations are shared by the
 * files implementing different parts of the same object: A3dRoot.cpp
 * handles lifetime, A3d3.cpp handles core API operations, and the
 * geometry, listener, matrix and scene files implement their respective
 * interfaces.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _A3D3_H
#define _A3D3_H

#include "A3dPrivate.h"
#include "ChunkPage.h"
#include "LinkList.h"
#include "A3dFrame.h"
#include "A3dMatrix.h"
#include "dalinfo.h"

/* Name by role; original spelling unknown.
 * (RE) dbg:0x100087AD producer; dbg:0x1001DD28 consumer. */
#define A3D_ROOT_AU8830_ENABLE_VALUE 33

class CA3dList;
class CA3dOpeningBuilder;
class CA3dRoomBuilder;
class CA3dReverb;
class CA3dScene;
class CA3dSource;
class CA3dWallBuilder;
class CRefAudBin;

struct _A3DPRIMITIVE;

/* -------------------------------------------------------------------------- */

/* =============================================================
// Class: CA3dStdList
//
// Description: MSVC 6 list layout and node operations for pointer elements.
//
// Size: 0x0C
//
// (RE) Template routines: dbg:0x1000C230; dbg:0x10015F00 .. dbg:0x100166F0.
// Iterator/erase helpers, mapping unresolved: dbg:0x1002F180,
// dbg:0x1002F4D0, dbg:0x1002F500.
// MSVC 19 std::list has an incompatible 8-byte layout.
// =============================================================*/
template<class T>
class CA3dStdList
{
public:
/* =============================================================
// Class: CA3dStdList::_Node
//
// Description: Doubly linked node containing a pointer element.
//
// Size: 0x0C
// =============================================================*/
	struct _Node
	{
		/* 0x00 */ _Node	*_Next;	/* Next link; accessors dbg:0x1000CAA0, dbg:0x1002FAB0. */
		/* 0x04 */ _Node	*_Prev;	/* Previous link; accessor dbg:0x1000CE00. */
		/* 0x08 */ T		_Value;	/* Element; accessor dbg:0x1000CAC0. */
	};

/* =============================================================
// Class: CA3dStdList::iterator
//
// Description: List-node iterator.
//
// Size: 0x04
//
// (RE) Default-construction path: dbg:0x10015F90 -> dbg:0x100163D0;
// thunk dbg:0x100038EB. The reference leaves the pointer uninitialized;
// this implementation clears it.
// =============================================================*/
	class iterator
	{
	public:
		iterator(void)			{ _Ptr = NULL; }

		iterator(_Node *pNode)		{ _Ptr = pNode; }

		/* (RE) CA3dSource *: dbg:0x1000C4D0; thunk dbg:0x10001F00
		 * (RE) CA3dPool *: dbg:0x1000CD60; wrapper dbg:0x1000C660;
		 *    thunks dbg:0x1000360C, dbg:0x1000118B */
		T &operator*(void) const	{ return (_Ptr->_Value); }

		/* (RE) CA3dSource *: dbg:0x1000C500; thunk dbg:0x100019EC
		 * (RE) Additional instantiation, type unresolved:
		 *    dbg:0x1002F530
		 * (RE) CA3dPool *: dbg:0x1000CD20; thunk dbg:0x10002E96 */
		iterator &operator++(void)	{ _Ptr = _Ptr->_Next; return (*this); }

		/* (RE) dbg:0x1000CB70 */
		int operator==(const iterator &it) const { return (_Ptr == it._Ptr); }

		/* (RE) CA3dSource *: dbg:0x1000C540; thunk dbg:0x10003C9C
		 * (RE) Additional instantiation, type unresolved:
		 *    dbg:0x1002F570 */
		int operator!=(const iterator &it) const { return (_Ptr != it._Ptr); }

		/* 0x00 */ _Node	*_Ptr;	/* Current node. */
	};

	/* (RE) Additional instantiation, type unresolved: dbg:0x1002F5B0
	 * (RE) CA3dSource *: dbg:0x1001DFA0
	 * (RE) Constructor thunks: CA3dPool * dbg:0x100018C0; CA3dList *
	 *    dbg:0x100020F4 */
	CA3dStdList(void)
	{
		_Head = _Buynode(NULL, NULL);
		_Size = 0;
	}

	/* (RE) Additional instantiation, type unresolved: dbg:0x1002F200;
	 *    dbg:0x1002F600
	 * (RE) Destructor thunks: CA3dPool * dbg:0x100035A3; CA3dList *
	 *    dbg:0x100010C8 */
	~CA3dStdList(void)
	{
		clear();
		::operator delete(_Head);
	}

	/* (RE) CA3dSource *: thunk dbg:0x100010E1
	 * (RE) CA3dReflection *: dbg:0x1002F680
	 * (RE) Additional instantiation, type unresolved: dbg:0x1002F310
	 * (RE) dbg:0x10015F20; CA3dPool * dbg:0x1000C580; thunk dbg:0x10002211 */
	int		size(void) const	{ return (_Size); }

	int		empty(void) const	{ return (_Size == 0); }

	/* (RE) CA3dSource *: thunk dbg:0x10003D64
	 * (RE) Additional instantiation, type unresolved: dbg:0x1002F280
	 * (RE) dbg:0x1000C2C0; CA3dPool * dbg:0x1000CBA0; thunk dbg:0x10001CAD */
	iterator	begin(void) const	{ return (iterator(_Head->_Next)); }

	/* (RE) CA3dSource *: dbg:0x1000C4A0; thunks dbg:0x10003D5F,
	 *    dbg:0x1000177B
	 * (RE) Additional instantiation, type unresolved: dbg:0x1002F2D0
	 * (RE) dbg:0x1000C310 */
	iterator	end(void) const		{ return (iterator(_Head)); }

	/* (RE) dbg:0x1000C5A0 */
	T &front(void) const			{ return (*begin()); }

	/* (RE) dbg:0x1000C5D0 */
	T &back(void) const
	{
		iterator	it = end();

		it._Ptr = it._Ptr->_Prev;

		return (*it);
	}

	/* (RE) dbg:0x1000C610 */
	void pop_back(void)
	{
		iterator	it = end();

		it._Ptr = it._Ptr->_Prev;

		erase(it);
	}

	/* (RE) CA3dList * dbg:0x100185B0; CA3dSource * dbg:0x1000c350
	 *    CA3dSource * thunk dbg:0x1000156e; CA3dPool * dbg:0x10015F40 */
	void push_back(T value)			{ _Insert(_Head, value); }

	iterator erase(iterator it)
	{
		_Node	*pNode = it._Ptr;
		_Node	*pNext = pNode->_Next;

		pNode->_Prev->_Next = pNode->_Next;
		pNode->_Next->_Prev = pNode->_Prev;
		::operator delete(pNode);
		_Size--;

		return (iterator(pNext));
	}

	/* (RE) CA3dReflection *: dbg:0x1002F6F0; thunk dbg:0x10001CEE
	 * (RE) Additional instantiation, type unresolved: dbg:0x1002F420
	 * (RE) CA3dSource * dbg:0x1000C3F0; CA3dList * dbg:0x10018600 */
	void remove(T value)
	{
		iterator	it = begin();

		while (it != end())
		{
			if (*it == value)
				it = erase(it);
			else
				++it;
		}
	}

	/* (RE) Additional instantiation, type unresolved: dbg:0x1002F3D0
	 * (RE) dbg:0x1000C3A0; thunk dbg:0x10001780 */
	void clear(void)
	{
		while (!empty())
			erase(begin());
	}

	/* (RE) LIST_HAS walk: dbg:0x1000D280 */
	int contains(T value) const
	{
		iterator	it;

		for (it = begin(); it != end(); ++it)
			if (*it == value)
				return (1);

		return (0);
	}

protected:
	/* (RE) dbg:0x1000C9E0 */
	_Node *_Buynode(_Node *pNext, _Node *pPrev)
	{
		_Node	*pNode = (_Node *) ::operator new(sizeof(_Node));

		pNode->_Next = pNext ? pNext : pNode;
		pNode->_Prev = pPrev ? pPrev : pNode;

		return (pNode);
	}

	void _Insert(_Node *pWhere, T value)
	{
		_Node	*pNode = _Buynode(pWhere, pWhere->_Prev);

		pNode->_Value        = value;
		pWhere->_Prev->_Next = pNode;
		pWhere->_Prev        = pNode;
		_Size++;
	}

public:
	/* 0x00 */ char		_Alloc;	/* Allocator byte, padded to four bytes. */
	/* 0x04 */ _Node	*_Head;	/* Sentinel node. */
	/* 0x08 */ int		_Size;	/* Element count. */
};

#define LIST_HAS(list, value)	((list).contains(value))

/* (RE) Reciprocal sentinel for zero bloat: immediate dbg:0x10014A93. */
#define A3D_BLOAT_NONE		9.9999997e37f

extern A3DVAL g_fLastFlush;
extern A3DVAL g_fLastFrameTime;
extern A3DVAL g_fLastRefOrders;

/* =============================================================
// Class: CA3dRoot
//
// Description: A3D engine, geometry, listener and private scene interfaces.
//
// Size: 0x1694 Retail/Debug; DebugViewer adds 0x1C unresolved bytes.
//
// (RE) Constructor: rtl:0x1000B720; dbg:0x1001CE40
// DebugViewer allocation: dbgv:0x10010A15, size 0x16B0.
// IA3dGeom2 base constructor: dbg:0x1001D430.
// Reference secondary methods receive subobject-relative this;
// member offsets below are relative to the root object.
// =============================================================*/

class CA3dRoot : public IA3d5,
		 public IA3dGeom2,
		 public IA3dListener,
		 public IA3dPrv1
{
public:
	CA3dRoot(int nBackend);

	/* (RE) Destructor: rtl:0x1000B9D0; dbg:0x1001D4C0. Scalar deleting
	 *    destructor, IA3d5 slot 51: rtl:0x1000B9A0; dbg:0x1001D3B0. */
	virtual ~CA3dRoot(void);

	/* IA3d5 at 0x00: 52 slots; rtl:0x100525A4; dbg:0x10128914.
	   IUnknown slots 0..2; shared by all four interfaces. */
	STDMETHODIMP		QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG)	AddRef(void);
	STDMETHODIMP_(ULONG)	Release(void);

	/* IA3d5 slots 3..8 (IA3d). */
	STDMETHODIMP	SetOutputMode(DWORD dwRelation, DWORD dwMode, DWORD dwChannels);
	STDMETHODIMP	GetOutputMode(LPDWORD pdwRelation, LPDWORD pdwMode, LPDWORD pdwChannels);
	STDMETHODIMP	SetResourceManagerMode(DWORD dwMode);
	STDMETHODIMP	GetResourceManagerMode(LPDWORD pdwMode);
	STDMETHODIMP	SetHFAbsorbFactor(FLOAT fFactor);
	STDMETHODIMP	GetHFAbsorbFactor(FLOAT *pfFactor);

	/* IA3d5 slots 9..11 (IA3d2). */
	STDMETHODIMP	RegisterVersion(DWORD dwVersion);
	STDMETHODIMP	GetSoftwareCaps(LPA3DCAPS_SOFTWARE pCaps);
	STDMETHODIMP	GetHardwareCaps(LPA3DCAPS_HARDWARE pCaps);

	/* IA3d5 slots 12..28 (IA3d3). */
	STDMETHODIMP	Clear(void);
	STDMETHODIMP	Flush(void);
	STDMETHODIMP	Compat(DWORD dwWhich, DWORD dwValue);
	STDMETHODIMP	Init(LPGUID pGuid, DWORD dwFlags, DWORD dwReserved);
	STDMETHODIMP	IsFeatureAvailable(DWORD dwFeature);
	STDMETHODIMP	NewSource(DWORD dwFlags, LPA3DSOURCE2 *ppSource);
	STDMETHODIMP	DuplicateSource(LPA3DSOURCE2 pSource, LPA3DSOURCE2 *ppSource);
	STDMETHODIMP	SetCooperativeLevel(HWND hWnd, DWORD dwLevel);
	STDMETHODIMP	GetCooperativeLevel(LPDWORD pdwLevel);
	STDMETHODIMP	SetMaxReflectionDelayTime(A3DVAL fDelay);
	STDMETHODIMP	GetMaxReflectionDelayTime(LPA3DVAL pfDelay);
	STDMETHODIMP	SetCoordinateSystem(DWORD dwSystem);
	STDMETHODIMP	GetCoordinateSystem(LPDWORD pdwSystem);
	STDMETHODIMP	SetOutputGain(A3DVAL fGain);
	STDMETHODIMP	GetOutputGain(LPA3DVAL pfGain);
	STDMETHODIMP	SetNumFallbackSources(DWORD dwNum);
	STDMETHODIMP	GetNumFallbackSources(LPDWORD pdwNum);

	/* IA3dListener at 0x08: 19 slots; rtl:0x10052488; dbg:0x101287BC. Methods 3..18. */
	STDMETHODIMP	SetPosition3f(A3DVAL x, A3DVAL y, A3DVAL z);	/* 3 */
	STDMETHODIMP	GetPosition3f(LPA3DVAL px, LPA3DVAL py, LPA3DVAL pz);	/* 4 */
	STDMETHODIMP	SetPosition3fv(LPA3DVAL pv);	/* 5 */
	STDMETHODIMP	GetPosition3fv(LPA3DVAL pv);	/* 6 */
	STDMETHODIMP	SetOrientationAngles3f(A3DVAL h, A3DVAL p, A3DVAL r);	/* 7 */
	STDMETHODIMP	GetOrientationAngles3f(LPA3DVAL ph, LPA3DVAL pp, LPA3DVAL pr);	/* 8 */
	STDMETHODIMP	SetOrientationAngles3fv(LPA3DVAL pv);	/* 9 */
	STDMETHODIMP	GetOrientationAngles3fv(LPA3DVAL pv);	/* 10 */
	STDMETHODIMP	SetOrientation6f(A3DVAL fx, A3DVAL fy, A3DVAL fz,
					 A3DVAL ux, A3DVAL uy, A3DVAL uz);	/* 11 */
	STDMETHODIMP	GetOrientation6f(LPA3DVAL pfx, LPA3DVAL pfy, LPA3DVAL pfz,
					 LPA3DVAL pux, LPA3DVAL puy, LPA3DVAL puz);	/* 12 */
	STDMETHODIMP	SetOrientation6fv(LPA3DVAL pv);	/* 13 */
	STDMETHODIMP	GetOrientation6fv(LPA3DVAL pv);	/* 14 */
	STDMETHODIMP	SetVelocity3f(A3DVAL x, A3DVAL y, A3DVAL z);	/* 15 */
	STDMETHODIMP	GetVelocity3f(LPA3DVAL px, LPA3DVAL py, LPA3DVAL pz);	/* 16 */
	STDMETHODIMP	SetVelocity3fv(LPA3DVAL pv);	/* 17 */
	STDMETHODIMP	GetVelocity3fv(LPA3DVAL pv);	/* 18 */

	/* Nonvirtual DirectSound entry points for CA3dMapper. */
	HRESULT	DsGetCaps(LPDSCAPS pCaps);
	HRESULT	DsCompact(void);
	HRESULT	DsGetSpeakerConfig(DWORD *pdwConfig);
	HRESULT	DsSetSpeakerConfig(DWORD dwConfig);

	/* IA3dGeom2 at 0x04: 52 slots; rtl:0x100524D4; dbg:0x10128818. Methods 3..41. */
	STDMETHODIMP	Enable(DWORD dwFeature);
	STDMETHODIMP	Disable(DWORD dwFeature);
	STDMETHODIMP_(BOOL)	IsEnabled(DWORD dwFeature);
	STDMETHODIMP	SetOcclusionMode(DWORD dwMode);	/* 6 */
	STDMETHODIMP	GetOcclusionMode(LPDWORD pdwMode);	/* 7 */
	STDMETHODIMP	SetReflectionMode(DWORD dwMode);	/* 8 */
	STDMETHODIMP	GetReflectionMode(LPDWORD pdwMode);	/* 9 */
	STDMETHODIMP	SetReflectionGainScale(A3DVAL fDelay);
	STDMETHODIMP	GetReflectionGainScale(LPA3DVAL pfDelay);
	STDMETHODIMP	SetReflectionDelayScale(A3DVAL fScale);
	STDMETHODIMP	GetReflectionDelayScale(LPA3DVAL pfScale);

	/* IA3dGeom2 matrix slots 14..25. */
	STDMETHODIMP_(ULONG)	PushMatrix(void);
	STDMETHODIMP_(ULONG)	PopMatrix(void);
	STDMETHODIMP	LoadIdentity(void);
	STDMETHODIMP	LoadMatrix(A3DMATRIX pMatrix);
	STDMETHODIMP	GetMatrix(A3DMATRIX pMatrix);
	STDMETHODIMP	MultMatrix(A3DMATRIX pMatrix);
	STDMETHODIMP	Translate3f(A3DVAL x, A3DVAL y, A3DVAL z);
	STDMETHODIMP	Translate3fv(LPA3DVAL pv);
	STDMETHODIMP	Rotate3f(A3DVAL fDegrees, A3DVAL x, A3DVAL y, A3DVAL z);
	STDMETHODIMP	Rotate3fv(A3DVAL fDegrees, LPA3DVAL pv);
	STDMETHODIMP	Scale3f(A3DVAL x, A3DVAL y, A3DVAL z);
	STDMETHODIMP	Scale3fv(LPA3DVAL pv);
	STDMETHODIMP	Begin(DWORD dwMode);
	STDMETHODIMP	End(void);
	STDMETHODIMP	Vertex3f(A3DVAL x, A3DVAL y, A3DVAL z);
	STDMETHODIMP	Vertex3fv(LPA3DVAL pv);
	STDMETHODIMP	Normal3f(A3DVAL x, A3DVAL y, A3DVAL z);
	STDMETHODIMP	Normal3fv(LPA3DVAL pv);
	STDMETHODIMP	Tag(DWORD dwTag);
	STDMETHODIMP	SetOpeningFactorf(A3DVAL fFactor);
	STDMETHODIMP	SetOpeningFactorfv(LPA3DVAL pv);	/* Slot 34. */
	STDMETHODIMP	NewMaterial(LPA3DMATERIAL *ppMaterial);
	STDMETHODIMP	BindMaterial(LPA3DMATERIAL pMaterial);
	STDMETHODIMP	NewList(LPA3DLIST *ppList);
	STDMETHODIMP	BindListener(void);
	STDMETHODIMP	BindSource(LPA3DSOURCE2 pSource);
	STDMETHODIMP	NewEnvironment(LPA3DENVIRONMENT *ppEnvironment);	/* 40 */
	STDMETHODIMP	BindEnvironment(LPA3DENVIRONMENT pEnvironment);	/* 41 */

	/* IA3dPrv1 at 0x0C: 8 slots; rtl:0x10052468; dbg:0x10128794. */
	STDMETHODIMP	NewScene(DWORD dwType, CA3dScene **ppScene);	/* 3 */
	STDMETHODIMP	GatherRooms(DWORD dwScene);	/* 4 */
	STDMETHODIMP	NewRoomBuilder(DWORD dwType, CA3dRoomBuilder **ppBuilder);	/* 5 */
	STDMETHODIMP	NewWallBuilder(DWORD dwType, CA3dWallBuilder **ppBuilder);	/* 6 */
	STDMETHODIMP	NewOpeningBuilder(DWORD dwType, CA3dOpeningBuilder **ppBuilder);	/* 7 */

	/* Legacy plain members; 677 has no IA3dPrv2 table. Behavior and arities unverified. */
	STDMETHODIMP	Prv2_03(DWORD dw);
	STDMETHODIMP	Prv2_04(DWORD dw1, DWORD dw2);
	STDMETHODIMP	Prv2_05(DWORD dw);

	HRESULT		Attach(IDirectSound *pDS, IA3d2 *pA3d2, IA3d *pA3d);

	int		IsRenderingEnabled(void)	{ return (m_fRenderingEnabled); }

	int		IsCompat1011Set(void)		{ return (m_fCompatAu8830); }

	int		HasCurrentMatrix(void)	{ return (m_fInheritMatrix); }

	const A3DVAL	*GetCurrentMatrix(void)	{ return (m_matCurrent); }

	CA3dPool	*GetPool(void)		{ return (m_pPool); }

	int		GetPoolElements(void)	{ return (m_cPoolElements); }

	int		GetGeomCount(void)	{ return (m_GeomPools.size()); }
	int		FindGeom(const class CA3dPool *pcPool);
	void		AddGeom(class CA3dPool *pPool);
	void		SetBackends(IDirectSound *pDS, IA3d2 *pA3d2, IA3d *pA3d);

	void		GetListenerVelocity(A3DVAL *fXFormedVel);

	friend class CA3dSource;
	friend class CA3dSourceCom;
	friend class CRefAudBin;
	friend class CA3dList;

	friend void A3dTraceApply(class CA3dSource *, A3DVAL, const A3DVAL *);
	friend void A3dTraceBegin(CA3dRoot *, A3DVAL *, A3DVAL *, A3DVAL *);
	friend class CA3dRoom *A3dWhichRoom(CA3dScene *);
	friend void A3dBroadcastDoppler(CA3dRoot *, A3DVAL);
	friend void A3dBroadcastRolloff(CA3dRoot *, A3DVAL);
	friend void A3dSourceOcclude(CA3dRoot *, class CA3dSource *);
	friend void A3dSourceStepEnd(class CA3dSource *, int);
	friend HRESULT A3dSourceEmit(class CA3dSource *);
	friend void A3dSourceGetMatrix(class CA3dSource *, A3DVAL *);
	friend void A3dSourceVolumetric(class CA3dSource *, A3DVAL *);
	friend void A3dTraceSetEars(class CA3dSource *, A3DVAL *);
	friend void A3dTraceSetVector(class CA3dSource *, const A3DVAL *,
				      const A3DVAL *, A3DVAL);
	friend A3DVAL A3dConeAngle(class CA3dSource *, const A3DVAL *,
				   const A3DVAL *, A3DVAL);
	friend void A3dReflection(class CA3dSource *, DWORD, DWORD, DWORD,
				  const A3DVAL *, A3DVAL, const A3DVAL *,
				  const A3DVAL *, const A3DVAL *);
	friend A3DVAL A3dMirrorPoint(CA3dRoot *, const A3DVAL *, const A3DVAL *,
				     const A3DVAL *, A3DVAL *, A3DVAL *);

	friend void A3dSourceQuiet(class CA3dSource *);
	friend void A3dStepEq(class CA3dSource *);
	friend int A3dSourcePushBudget(class CA3dSource *, int);
	friend int A3dSourcePushMatch(class CA3dSource *, int, DWORD *);
	friend void A3dSourcePushB(class CA3dSource *, int);

protected:
	HRESULT		CreateListener(void);
	HRESULT		CreateGeom(void);

	DWORD		IsListenerReady(void) const { return (m_fListenerReady); }

	int		Trace(void);

	int		TraceLegacy(void);

	void		TraceWalk(void);

	/* Reference TraceStep/TraceHitOpen/TraceOpeningWalk read m_pPool;
	   these declarations pass it explicitly. */
	int		TraceStep(class CA3dPool *pPool);
	int		TraceHitOpen(class CA3dPool *pPool,
				     BOOL (*pfnHit)(const A3DVAL *, const A3DVAL *,
						    struct _A3DPRIMITIVE *,
						    int, int));
	int		TraceRetestOccluder(class CA3dList *pList, int iElement,
				    const A3DVAL *pafMaterial);
	int		TraceOpeningWalk(class CA3dPool *pPool);
	BOOL		TraceOpeningFactor(void);

	void		ReflectWalk(void);
	void		NextWalkElement(void);
	void		ReflectStep(BOOL (*pfnHit)(const A3DVAL *, const A3DVAL *,
						   struct _A3DPRIMITIVE *,
						   int, int));
	void		ReflectSurfaces(void);

	void		EstimateReverb(A3DVAL *pfMeasure);

	HRESULT		EnableGeometricReverb(void);

	HRESULT		InitReverb(void);

	HRESULT		SendReverbEaxCustom(void);
	HRESULT		SendReverbI3dl2(void);
	HRESULT		SendReverbEaxPreset(void);
	void		SendSourceI3dl2DirectHF(class CA3dSource *pSource);
	void		SendSourceEaxReverbMix(class CA3dSource *pSource);
	void		FlushReverb(void);
	void		ResetReverbVocabularyDefaults(void);
	void		SendI3dl2ListenerDefaults(void);
	void		SendEaxReverbDefaults(void);
	void		DetectI3dl2Vocabulary(void);
	void		DetectEaxVocabulary(void);

	/* Original member versus file-static ownership is unresolved;
	   reference uses __thiscall with one output argument (dbg:0x10015180). */
	A3DVAL		*MeasureRoom(A3DVAL *pfMeasure);

	void		DestroyListener(void);
	void		DestroyGeom(void);
	void		DestroyPrv1(void);
	HRESULT		CreatePrv1(void);
	void		RebindAllSources(void);
	void		RemoveSource(class CA3dSource *pSource);
	HRESULT		Detach(void);

	void		NextFreeChunk(void);

	HRESULT		BeginListRecording(void);
	HRESULT		EndListRecording(void);

	void		UnbindListFromSources(class CA3dList *pList);

	HRESULT		NewPrimaryBuffer(void);

	HRESULT		GetResManMode(LPDWORD pdwMode);
	HRESULT		SetResManMode(DWORD dwMode);

	HRESULT		InitResmanLayer(LPGUID pGuid, DWORD dwFlags, DWORD dwReserved,
					HWND hWnd, DWORD dwLevel);

	HRESULT		ReportError(HRESULT hr, LPCSTR pszMessage);

protected:

	/* 0x0010 */ A3DVAL	m_fFrameTime;	/* Last frame duration in seconds; invalid gaps reuse the previous duration. */
	/* 0x0014 */ DWORD	m_dwInterfaceVersion;	/* Highest queried interface generation; Compat can override it. */
	/* 0x0018 */ DWORD	m_fRenderingEnabled;	/* A3DCOMPAT_ENABLE_RENDERING; permits source playback. */
	/* 0x001C */ DWORD	m_fTintReflections;	/* Tint occluded reflections instead of silencing them. */
	/* 0x0020 */ DWORD	m_fCompatAu8830;	/* A3DCOMPAT_AU8830 value 33 enables this flag. */
	/* 0x0024 */ DWORD	m_dwCompatWalkNear;	/* Near-walk override is active; suppresses near-field ear collapse. */
	/* 0x0028 */ int	m_nBackend;	/* Constructor backend selector. */
	/* 0x002C */ DWORD	m_fBinauralWalk;	/* Enable transforms for both ears. */
	/* 0x0030 */ LONG	m_cRef;	/* Interlocked API call count; m_cRefCount owns lifetime. */
	/* 0x0034 */ DWORD	m_dwCompatRefOrders;	/* A3DCOMPAT_REF_ORDERS value; rendering effect unresolved. */
	/* 0x0038 */ DWORD	m_fMuteEarGains;	/* Force both ear gains to zero. */
	/* 0x003C */ DWORD	m_fForceStatusBits;	/* Force A3D status fields regardless of interface generation. */
	/* 0x0040 */ LONG	m_cRefCount;	/* Non-interlocked COM reference count. */
	/* 0x0044 */ DWORD	m_dwPrimaryBufferBuilt;	/* Primary-buffer creation has succeeded. */
	/* 0x0048 */ DWORD	m_fInitialized;	/* Resource-manager initialization completed. */
	/* 0x004C */ DWORD	m_fBackendsAttached;	/* Backend pointers have been attached. */
	/* 0x0050 */ DWORD	m_dwCoopLevel;	/* A3D_CL_NORMAL or A3D_CL_EXCLUSIVE. */
	/* 0x0054 */ DWORD	m_Unknown_0x54;

	/* 0x0058 */ FLOAT	m_fDopplerScale;	/* Global nonnegative Doppler scale. */
	/* 0x005C */ FLOAT	m_fDistanceModelScale;	/* Global nonnegative distance-model scale. */
	/* 0x0060 */ FLOAT	m_fEq;	/* Global equalization setting, 0.0 to 1.0. */
	/* 0x0064 */ DWORD	m_dwFeaturesRequested;	/* Requested A3D feature bits. */
	/* 0x0068 */ DWORD	m_dwFeaturesAvailable;	/* Granted A3D feature bits. */
	/* 0x006C */ A3DDALCAPS564	m_SourceCaps;	/* 564-byte DAL capability buffer. */
	/* 0x02A0 */ DWORD	m_dwReverbSearched;	/* Reverb property interface and vocabulary were found. */
	/* 0x02A4 */ DWORD	m_dwReverbVocabulary;	/* Bit 0: EAX; bit 1: I3DL2. */
	/* 0x02A8 */ CA3dReverb		*m_pBoundedReverb;	/* Referenced reverb bound to this engine. */
	/* 0x02AC */ IA3dPropertySet	*m_pReverbPropSet;	/* Referenced listener reverb property interface. */
	/* 0x02B0 */ CA3dStdList<CA3dSource *>	m_listReverbSources;	/* Sources requiring reverb updates this frame. */
	/* 0x02BC */ DWORD	m_dwLastReverbType;	/* Reverb type last applied. */
	/* 0x02C0 */ FLOAT	m_fEqCurve;	/* Polynomial equalization curve, clamped to 0.0 to 1.0. */
	/* 0x02C4 */ IDirectSound	*m_pDirectSound;	/* Owned backend DirectSound interface. */
	/* 0x02C8 */ IDirectSound3DListener	*m_pDS3dListener;	/* Primary-buffer 3D listener for a nonzero backend selector. */
	/* 0x02CC */ IA3d		*m_pA3d;	/* Owned IA3d fallback interface. */
	/* 0x02D0 */ IA3d2	*m_pA3d2;	/* Owned preferred IA3d2 backend interface. */
	/* 0x02D4 */ IA3dDal	*m_pDal;	/* Owned resource-manager DAL interface. */
	/* 0x02D8 */ IDirectSoundBuffer	*m_pPrimary;	/* Owned primary sound buffer. */
	/* 0x02DC */ CA3dStdList<CA3dSource *>	m_SourceArray;	/* Sources registered with this engine. */
	/* 0x02E8 */ HRESULT	m_hrReported;	/* Last successful HRESULT passed to ReportError. */
	/* 0x02EC */ FLOAT	m_fUnitsPerMeter;	/* World units per meter; greater than zero. */
	/* 0x02F0 */ DWORD	m_dwStreamBufferLength;	/* Default source buffer length in milliseconds. */
	/* 0x02F4 */ DWORD	m_dwStreamThreadPriority;	/* Default source streaming priority, 0 to 2. */
	/* 0x02F8 */ DWORD	m_dwStreamingPropertiesSet;	/* Streaming defaults explicitly set. */
	/* 0x02FC */ A3DVAL	m_amatStack[A3D_MATRIX_STACK_DEPTH][16];	/* 32 matrix levels; the constructor leaves them uninitialized. */
	/* 0x0AFC */ BYTE	m_abUnknown_0xAFC[0x800];	/* (RE) No reference in dbg; purpose is unresolved. */

	/* 0x12FC */ DWORD	m_adwMatrixIsIdentity[A3D_MATRIX_STACK_DEPTH];	/* Exact-identity flags; the constructor leaves them uninitialized. */
	/* 0x137C */ int	m_nMatrixDepth;	/* Current transform-stack index. */
	/* 0x1380 */ DWORD	m_fInheritMatrix;	/* A3DCOMPAT_1001; sources inherit the current transform. */
	/* 0x1384 */ A3DVAL	m_matCurrent[16];	/* Initial source transform. */
	/* 0x13C4 */ DWORD	m_Unknown_0x13C4;	/* (RE) No reference in dbg. */

	/* 0x13C8 */ CRefAudBin	*m_pReflectBin;	/* Borrowed stack-local reflection bin during Trace. */
	/* 0x13CC */ FLOAT	m_fMaxReflectionDelayTime;	/* Maximum reflection delay in seconds; default 0.3. */
	/* 0x13D0 */ FLOAT	m_fGlobalReflectionGainScale;	/* Geometry reflection gain scale; default 1.0. */
	/* 0x13D4 */ FLOAT	m_fGlobalReflectionDelayScale;	/* Geometry reflection delay scale; default 1.0. */
	/* 0x13D8 */ DWORD	m_dwUseDalInterface;	/* UseDalInterface registry setting; selects shared DAL controls. */
	/* 0x13DC */ DWORD	m_fGeomReady;	/* Geometry interface initialized. */
	/* 0x13E0 */ DWORD	m_dwLegacyReverbPreset;	/* Applied EAX environment preset; -1 forces reevaluation. */
	/* 0x13E4 */ DWORD	m_dwGeomReverbParamSize;	/* Cached A3DGEOMREVERBPARAM size, normally 16. */
	/* 0x13E8 */ FLOAT	m_fGeomScaling;	/* Geometry scale for reverb room measures. */
	/* 0x13EC */ FLOAT	m_fEffectScaling;	/* Reverb effect scale for room measures. */
	/* 0x13F0 */ DWORD	m_nMeasureAxis;	/* Vertical A3DAXIS for room measures; default Y. */
	/* 0x13F4 */ DWORD	m_dwGeomReverbCtrl;	/* geom_reverb_ctrl registry override. */
	/* 0x13F8 */ int	m_cPoolElements;	/* Elements per geometry pool; default 2048. */
	/* 0x13FC */ CA3dStdList<CA3dPool *>	m_GeomPools;	/* Engine geometry pools. */
	/* 0x1408 */ CA3dStdList<CA3dPool *>	m_ScratchPools;	/* Display-list recording pools; the first is reused. */
	/* 0x1414 */ int	m_cGeomPoolsLeft;	/* Geometry pools remaining in the current walk. */
	/* 0x1418 */ CA3dStdList<CA3dPool *>::iterator	m_itGeomPool;	/* Current geometry-pool iterator. */
	/* 0x141C */ CA3dPool	*m_pPool;	/* Current geometry or recording pool. */
	/* 0x1420 */ CA3dPool	*m_pSavedPool;	/* Engine pool saved during display-list recording. */
	/* 0x1424 */ void	*m_pElement;	/* Geometry element being built or walked. */
	/* 0x1428 */ A3DVAL	m_fOpening;	/* Staged scalar opening factor. */
	/* 0x142C */ LPA3DVAL	m_pfOpening;	/* Staged opening-factor pointer. */
	/* 0x1430 */ DWORD	m_dwTag;	/* Current primitive tag. */
	/* 0x1434 */ BYTE	m_nVerticesPerPrimitive;	/* 2, 3 or 4 vertices, selected by Begin. */
	/* 0x1435 */ BYTE	m_abPad_0x1435[3];	/* Alignment before m_avVertex */
	/* 0x1438 */ A3DVAL	m_avVertex[4][4];	/* Transformed homogeneous vertices. */
	/* 0x1478 */ A3DVAL	m_vNormal[3];	/* Supplied or computed primitive normal. */
	/* 0x1484 */ A3DVAL	m_fNormalW;	/* Homogeneous w of the staged normal; A3DPRIMITIVE::vNormal is an A3DVECTOR. */
	/* 0x1488 */ A3DVAL	m_fDistance;	/* Staged A3DPRIMITIVE::fDistance; only the element copy is written. */
	/* 0x148C */ DWORD	m_dwPrimitiveFlags;	/* Bit 4 marks infinite planes. */
	/* 0x1490 */ int	m_nVertexIndex;	/* Next vertex in the current primitive. */
	/* 0x1494 */ DWORD	m_fNormalPending;	/* Begin has not yet received an explicit normal. */
	/* 0x1498 */ int	m_nBeginMode;	/* Primitive mode; -1 outside Begin/End. */
	/* 0x149C */ DWORD	m_dwFeaturesEnabled;	/* Geometry features enabled by the client. */
	/* 0x14A0 */ DWORD	m_dwWalkEnable;	/* Occlusion walk mode; known value 1. */
	/* 0x14A4 */ DWORD	m_Unknown_0x14A4;	/* (RE) Written 1 at dbg:0x1000EF49 and dbg:0x1001CF5A; no reader. */

	/* 0x14A8 */ A3DVAL	m_fCurOcclude[2];	/* Current occlusion gains. */
	/* 0x14B0 */ A3DVAL	m_fOccludeFactor;	/* Occlusion blend factor. */
	/* 0x14B4 */ A3DVAL	m_vToListener[3];	/* Per-source position minus listener position. */
	/* 0x14C0 */ DWORD	m_Unknown_0x14C0;	/* (RE) No reference in dbg. */

	/* 0x14C4 */ A3DVAL	m_afWalkMaterial[6];	/* Six-value material record for the current walk. */
	/* 0x14DC */ A3DVAL	m_fOcclusion;	/* Occlusion accumulated for the current primitive. */
	/* 0x14E0 */ FLOAT	m_fOutputGain;	/* Listener output gain, 0.0 to 1.0. */
	/* 0x14E4 */ A3DVAL	m_fReflectScale;	/* Reciprocal polygon bloat; A3D_BLOAT_NONE represents zero. */
	/* 0x14E8 */ DWORD	m_cWalkMax;	/* Maximum geometry-walk count; A3DCOMPAT_1003. */
	/* 0x14EC */ DWORD	m_cWalkDone;	/* Geometry-walk count so far. */
	/* 0x14F0 */ DWORD	m_dwDisableReflections;	/* DisableReflections registry setting. */
	/* 0x14F4 */ DWORD	m_dwDisableOcclusions;	/* DisableOcclusions registry setting. */
	/* 0x14F8 */ DWORD	m_dwDoRefsEvery;	/* do_refs_every registry override. */
	/* 0x14FC */ DWORD	m_dwDoOccsEvery;	/* do_occs_every registry override. */
	/* 0x1500 */ DWORD	m_dwReflectionUpdateInterval;	/*  A3DCOMPAT_1004. */
	/* 0x1504 */ DWORD	m_dwOcclusionUpdateInterval;	/*  A3DCOMPAT_1005. */
	/* 0x1508 */ DWORD	m_dwLastTrace;	/* Last trace timestamp. */
	/* 0x150C */ DWORD	m_dwTraceInterval;	/* Trace interval; A3DCOMPAT_1006. */
	/* 0x1510 */ DWORD	m_dwInfinitePlanes;	/* Marks completed primitives as infinite planes  */
	/* 0x1514 */ FLOAT	m_fWalkNear;	/* Near-walk distance; A3DCOMPAT_1010. */
	/* 0x1518 */ DWORD	m_dwRenderMode;	/* Geometry render-mode bits for recorded primitives. */
	/* 0x151C */ CA3dSource	*m_pTraceSource;	/* Source currently being traced. */
	/* 0x1520 */ DWORD	m_dwWalkSeq;	/* Sequence stamped on pools visited in the current trace. */
	/* 0x1524 */ CA3dStdList<CA3dList *>	m_OpenLists;	/* Display lists currently recording. */
	/* 0x1530 */ DWORD	m_cListsRecording;	/* Recording nesting depth; blocks Clear and Flush while nonzero. */
	/* 0x1534 */ DWORD	m_fShutdownRequested;	/* Shutdown requested; no reader established. */
	/* 0x1538 */ DWORD	m_dwSoftAC3Unlocked;	/* Fallback AC3 decoder unlocked. */
	/* 0x153C */ DWORD	m_fListenerReady;	/* Listener interface initialized. */
	/* 0x1540 */ DWORD	m_dwOrientAsAngles;	/* Listener orientation was set as angles. */
	/* 0x1544 */ A3DVAL	m_vOrientAngles[4];	/* Listener heading, pitch and roll. */
	/* 0x1554 */ A3DVAL	m_vEarOffsetLeft[4];	/* Left ear offset in meters; xyz (-0.07, 0, 0), w uninitialized. */
	/* 0x1564 */ A3DVAL	m_vEarOffsetRight[4];	/* Right ear offset in meters; xyz (+0.07, 0, 0), w uninitialized. */
	/* 0x1574 */ A3DVAL	m_vVelocity[4];	/* Listener velocity before world transformation. */
	/* 0x1584 */ A3DVAL	m_vListenerPos[4];	/* Listener world position for source tracing. */
	/* 0x1594 */ DWORD	m_dwVelocitySet;	/* Listener velocity explicitly supplied. */
	/* 0x1598 */ DWORD	m_dwCoordSystemSet;	/* Coordinate system already selected; later changes fail. */
	/* 0x159C */ DWORD	m_dwCoordSystem;	/* A3D_RIGHT_HANDED_CS or A3D_LEFT_HANDED_CS. */
	/* 0x15A0 */ A3DVAL	m_matListener[16];	/* Listener transform. */
	/* 0x15E0 */ A3DVAL	m_matListenerXform[16];	/* Listener midpoint transform. */
	/* 0x1620 */ A3DVAL	m_matListenerXform2[16];	/* Inverse listener midpoint transform. */
	/* 0x1660 */ A3DVAL	m_vPosition[4];	/* Listener position, w = 1. */
	/* 0x1670 */ A3DVAL	m_vOrientFront[4];	/* Listener front vector. */
	/* 0x1680 */ A3DVAL	m_vOrientUp[4];	/* Listener up vector. */
	/* 0x1690 */ DWORD	m_fPrv1Ready;	/* Private scene interface initialized. */

	/* IA3d5 slots 29..50; slot 51 is the scalar deleting destructor. */
	STDMETHODIMP	SetRMPriorityBias(A3DVAL);
	STDMETHODIMP	GetRMPriorityBias(LPA3DVAL);
	STDMETHODIMP	DisableViewer();
	STDMETHODIMP	SetUnitsPerMeter(A3DVAL);
	STDMETHODIMP	GetUnitsPerMeter(LPA3DVAL);
	STDMETHODIMP	SetDopplerScale(A3DVAL);
	STDMETHODIMP	GetDopplerScale(LPA3DVAL);
	STDMETHODIMP	SetDistanceModelScale(A3DVAL);
	STDMETHODIMP	GetDistanceModelScale(LPA3DVAL);
	STDMETHODIMP	SetEq(A3DVAL);
	STDMETHODIMP	GetEq(LPA3DVAL);
	STDMETHODIMP	Shutdown();
	STDMETHODIMP	RegisterApp(REFIID);
	STDMETHODIMP	InitEx(LPGUID, DWORD, DWORD, HWND, DWORD);
	STDMETHODIMP	NewReverb(LPA3DREVERB *);
	STDMETHODIMP	BindReverb(LPA3DREVERB);
	STDMETHODIMP	GetStreamingProperties(DWORD *, DWORD *);
	STDMETHODIMP	SetStreamingProperties(DWORD, DWORD);
	STDMETHODIMP	A3dEnumerate(LPA3DENUMCALLBACK, LPVOID);
	STDMETHODIMP	UnlockFallbackAC3Decoder(LPSTR , DWORD);
	STDMETHODIMP	SetMaxHardwareSources(DWORD);
	STDMETHODIMP	GetMaxHardwareSources(LPDWORD);

	/* IA3dGeom2 slots 42..51. */
	STDMETHODIMP	SetRenderMode(DWORD);
	STDMETHODIMP	GetRenderMode(LPDWORD);
	STDMETHODIMP	SetPolygonBloatFactor(A3DVAL);
	STDMETHODIMP	GetPolygonBloatFactor(LPA3DVAL);
	STDMETHODIMP	SetReflectionUpdateInterval(DWORD);
	STDMETHODIMP	GetReflectionUpdateInterval(LPDWORD);
	STDMETHODIMP	SetOcclusionUpdateInterval(DWORD);
	STDMETHODIMP	GetOcclusionUpdateInterval(LPDWORD);
	STDMETHODIMP	SetGeomReverbParam(LPA3DGEOMREVERBPARAM);
	STDMETHODIMP	GetGeomReverbParam(LPA3DGEOMREVERBPARAM);
};

#endif /* _A3D3_H */
