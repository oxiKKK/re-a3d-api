/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * a3d33.h
 *
 * Declares private A3D interfaces used between the API engine, resource
 * manager, source objects and room-based scene builders. These interfaces
 * supplement the published SDK declarations in ia3dapi.h and provide the
 * internal COM contracts used across the implementation.
 *
 * The slot tables and parameter types describe the supported binary
 * interfaces, including unresolved operations and the retained,
 * unverified IA3dPrv2 declaration. Concrete classes and their storage are
 * declared in their owning headers; this file defines the interface
 * boundaries they implement.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _A3D33_H
#define _A3D33_H

#include "ia3dapi.h"

/* Private Compat identifiers. REFLECT_SCALE and WALK_NEAR take float bit patterns. */

#define A3DCOMPAT_ENABLE_RENDERING      1000    /* Enable playback pre-roll  */
#define A3DCOMPAT_INHERIT_MATRIX        1001    /* Enable matrix inheritance; value 1 also rebinds live sources. */
#define A3DCOMPAT_REFLECT_SCALE         1002    /* Reflection-distance scale; float bits. */
#define A3DCOMPAT_WALK_MAX              1003    /* Maximum primitives tested per walk. */
#define A3DCOMPAT_TRACE_PHASE           1004    /* Full-test source interval; registry override takes precedence. */
#define A3DCOMPAT_TRACE_PHASE2          1005    /* Secondary test interval; registry override takes precedence. */
#define A3DCOMPAT_TRACE_INTERVAL        1006    /* Trace interval in milliseconds. */
#define A3DCOMPAT_TINT_REFLECTIONS      1007    /* Attenuate occluded reflections instead of silencing them. */
#define A3DCOMPAT_1008                  1008    /* Accepted without effect. */
#define A3DCOMPAT_INFINITE_PLANES       1009    /* Mark new primitives as infinite planes. */
#define A3DCOMPAT_WALK_NEAR             1010    /* Distance threshold for the second walk; float bits. */
#define A3DCOMPAT_AU8830                1011    /* Value 33 enables AU8830 behavior; other values do nothing. */
#define A3DCOMPAT_REF_ORDERS            1012    /* Store ref_orders ; rendering effect unresolved. */
#define A3DCOMPAT_AUDIBLE_MAX           2000    /* Accepted without effect. */
#define A3DCOMPAT_BINAURAL_WALK         2001    /* Enable transforms for both ears. */
#define A3DCOMPAT_SET_INTERFACE_VERSION 2002    /* Set the requested interface version directly. */
#define A3DCOMPAT_GET_CALL_COUNT        2003    /* Return the API call count as HRESULT. */
#define A3DCOMPAT_SET_CALL_COUNT        2004    /* Set the API call count with InterlockedExchange and return it. */
#define A3DCOMPAT_MUTE_EAR_GAINS        2005    /* Force both ear gains to zero. */
#define A3DCOMPAT_FORCE_STATUS_BITS     3000    /* Return A3D status bits for all interface versions. */

/* IA3dPrv1: 8 slots; IID {103E7222-0113-11D2-90FB-006008A1F441}. */

#undef INTERFACE
#define INTERFACE IA3dPrv1

DECLARE_INTERFACE_(IA3dPrv1, IUnknown)
{
	STDMETHOD(QueryInterface)		(THIS_ REFIID, LPVOID FAR *) PURE;
	STDMETHOD_(ULONG,AddRef)		(THIS) PURE;
	STDMETHOD_(ULONG,Release)		(THIS) PURE;
	STDMETHOD(NewScene)			(THIS_ DWORD, class CA3dScene **) PURE;
	STDMETHOD(GatherRooms)			(THIS_ DWORD dwScene) PURE;
	STDMETHOD(NewRoomBuilder)		(THIS_ DWORD, class CA3dRoomBuilder **) PURE;
	STDMETHOD(NewWallBuilder)		(THIS_ DWORD, class CA3dWallBuilder **) PURE;
	STDMETHOD(NewOpeningBuilder)		(THIS_ DWORD, class CA3dOpeningBuilder **) PURE;
};

/* IA3dPrv2: 6 slots; IID {BFE2BE81-4CB7-11D2-90FB-006008A1F441}.
 * 3.3.677 evidence does not establish the signatures or shared-stub behavior. */

#undef INTERFACE
#define INTERFACE IA3dPrv2

DECLARE_INTERFACE_(IA3dPrv2, IUnknown)
{
	STDMETHOD(QueryInterface)		(THIS_ REFIID, LPVOID FAR *) PURE;
	STDMETHOD_(ULONG,AddRef)		(THIS) PURE;
	STDMETHOD_(ULONG,Release)		(THIS) PURE;
	STDMETHOD(Prv2_03)			(THIS_ DWORD) PURE;
	STDMETHOD(Prv2_04)			(THIS_ DWORD, DWORD) PURE;
	STDMETHOD(Prv2_05)			(THIS_ DWORD) PURE;
};

/* IA3dPrv4: 12 slots; IID {CBD91FA7-41C6-11D2-BB48-0060082F3C00}.
 * ResMan subobject at 0x0C; rtl:0x10053BC0; dbg:0x10133508. */

#undef INTERFACE
#define INTERFACE IA3dPrv4

DECLARE_INTERFACE_(IA3dPrv4, IUnknown)
{
	STDMETHOD(QueryInterface)		(THIS_ REFIID, LPVOID FAR *) PURE;
	STDMETHOD_(ULONG,AddRef)		(THIS) PURE;
	STDMETHOD_(ULONG,Release)		(THIS) PURE;

	STDMETHOD(GetPriorityWeight)		(THIS_ LPA3DVAL) PURE;	/* 3 */
	STDMETHOD(SetPriorityWeight)		(THIS_ A3DVAL) PURE;	/* 4 */
	STDMETHOD(GetBufferLatency)		(THIS_ LPA3DVAL) PURE;	/* 5 */
	STDMETHOD(SetBufferLatency)		(THIS_ A3DVAL) PURE;	/* 6 */
	STDMETHOD(GetBufferRefreshThreshold)	(THIS_ LPA3DVAL) PURE;	/* 7 */
	STDMETHOD(SetBufferRefreshThreshold)	(THIS_ A3DVAL) PURE;	/* 8 */

	STDMETHOD(GetHardwareSourceLimit)		(THIS_ LPDWORD) PURE;	/* 9 */
	STDMETHOD(SetHardwareSourceLimit)		(THIS_ DWORD) PURE;	/* 10 */
	STDMETHOD(GetHardwareSourceCapacity)	(THIS_ LPDWORD) PURE;	/* 11 */
};

/* IA3dSrcPrv: 5 slots; dbg:0x101292CC. */

#undef INTERFACE
#define INTERFACE IA3dSrcPrv

DECLARE_INTERFACE_(IA3dSrcPrv, IUnknown)
{
	STDMETHOD(QueryInterface)		(THIS_ REFIID, LPVOID FAR *) PURE;
	STDMETHOD_(ULONG,AddRef)		(THIS) PURE;
	STDMETHOD_(ULONG,Release)		(THIS) PURE;

	STDMETHOD(Unknown_0x0C)			(THIS_ LPVOID) PURE;	/* 3 */
	STDMETHOD(RenumberReflections)			(THIS_ LPVOID *) PURE;	/* 4; array of 16 reflection pointers. */
};

/* Private wall-builder interface. */

#undef INTERFACE
#define INTERFACE IA3dWallBuilder

DECLARE_INTERFACE_(IA3dWallBuilder, IUnknown)
{
	STDMETHOD(QueryInterface)		(THIS_ REFIID, LPVOID FAR *) PURE;
	STDMETHOD_(ULONG,AddRef)		(THIS) PURE;
	STDMETHOD_(ULONG,Release)		(THIS) PURE;

	STDMETHOD(SetMaterial)			(THIS_ LPVOID) PURE;
	STDMETHOD(GetMaterial)			(THIS_ LPVOID *) PURE;

	STDMETHOD_(int,AddOpening)		(THIS_ LPVOID, LPA3DVAL, LPA3DVAL, LPA3DVAL) PURE;
	STDMETHOD(RemoveOpening)		(THIS_ int) PURE;
	STDMETHOD(GetOpening)			(THIS_ int, LPVOID *) PURE;

	STDMETHOD(Begin)			(THIS_ DWORD) PURE;
	STDMETHOD(Vertex3f)			(THIS_ A3DVAL, A3DVAL, A3DVAL) PURE;
	STDMETHOD(Vertex3fv)			(THIS_ LPA3DVAL) PURE;
	STDMETHOD(End)				(THIS) PURE;
	STDMETHOD(RemovePrimitive)		(THIS_ int) PURE;
	STDMETHOD_(int,GetPrimitiveCount)	(THIS) PURE;
	STDMETHOD(SetVertex)			(THIS_ int, int, A3DVAL, A3DVAL, A3DVAL) PURE;
	STDMETHOD(GetVertex)			(THIS_ int, int, LPA3DVAL) PURE;

	STDMETHOD(Validate)			(THIS) PURE;
	STDMETHOD(Clear)			(THIS) PURE;

	/* Persistence stubs preserve verified arity; argument types are unknown. */

	STDMETHOD(Load)				(THIS_ LPVOID) PURE;
	STDMETHOD(Save)				(THIS_ LPVOID) PURE;
	STDMETHOD(UnSerialize)			(THIS_ LPVOID, UINT) PURE;
	STDMETHOD(Serialize)			(THIS_ LPVOID, UINT) PURE;
	STDMETHOD(Duplicate)			(THIS_ LPVOID) PURE;

	STDMETHOD(SetName)			(THIS_ LPCVOID) PURE;
	STDMETHOD(GetName)			(THIS_ LPVOID, int) PURE;
};

/* Private opening-builder interface. */

#undef INTERFACE
#define INTERFACE IA3dOpeningBuilder

DECLARE_INTERFACE_(IA3dOpeningBuilder, IUnknown)
{
	STDMETHOD(QueryInterface)		(THIS_ REFIID, LPVOID FAR *) PURE;
	STDMETHOD_(ULONG,AddRef)		(THIS) PURE;
	STDMETHOD_(ULONG,Release)		(THIS) PURE;

	STDMETHOD(Begin)			(THIS_ DWORD) PURE;
	STDMETHOD(Vertex3f)			(THIS_ A3DVAL, A3DVAL, A3DVAL) PURE;
	STDMETHOD(Vertex3fv)			(THIS_ LPA3DVAL) PURE;
	STDMETHOD(End)				(THIS) PURE;
	STDMETHOD(RemovePrimitive)		(THIS_ int) PURE;
	STDMETHOD_(int,GetPrimitiveCount)	(THIS) PURE;
	STDMETHOD(SetVertex)			(THIS_ int, int, A3DVAL, A3DVAL, A3DVAL) PURE;
	STDMETHOD(GetVertex)			(THIS_ int, int, LPA3DVAL) PURE;

	STDMETHOD(Validate)			(THIS) PURE;
	STDMETHOD(Clear)			(THIS) PURE;

	STDMETHOD(Load)				(THIS_ LPVOID) PURE;
	STDMETHOD(Save)				(THIS_ LPVOID) PURE;
	STDMETHOD(UnSerialize)			(THIS_ LPVOID, UINT) PURE;
	STDMETHOD(Serialize)			(THIS_ LPVOID, UINT) PURE;
	STDMETHOD(Duplicate)			(THIS_ LPVOID) PURE;

	STDMETHOD(SetName)			(THIS_ LPCVOID) PURE;
	STDMETHOD(GetName)			(THIS_ LPVOID, int) PURE;
};

/* Private room-builder interface. */

#undef INTERFACE
#define INTERFACE IA3dRoomBuilder

DECLARE_INTERFACE_(IA3dRoomBuilder, IUnknown)
{
	STDMETHOD(QueryInterface)		(THIS_ REFIID, LPVOID FAR *) PURE;
	STDMETHOD_(ULONG,AddRef)		(THIS) PURE;
	STDMETHOD_(ULONG,Release)		(THIS) PURE;

	STDMETHOD(Unknown_0x0C)			(THIS_ LPVOID) PURE;
	STDMETHOD(Unknown_0x10)			(THIS_ LPVOID) PURE;
	STDMETHOD(SetMaterial)			(THIS_ LPVOID) PURE;
	STDMETHOD(GetMaterial)			(THIS_ LPVOID *) PURE;

	STDMETHOD_(int,AddWall)			(THIS_ LPVOID, DWORD, LPA3DVAL, LPA3DVAL, LPA3DVAL) PURE;
	STDMETHOD(RemoveWall)			(THIS_ int) PURE;
	STDMETHOD(GetWall)			(THIS_ int, LPVOID *) PURE;
	STDMETHOD(Validate)			(THIS) PURE;
	STDMETHOD(Clear)			(THIS) PURE;

	STDMETHOD(Load)				(THIS_ LPVOID) PURE;
	STDMETHOD(Save)				(THIS_ LPVOID) PURE;
	STDMETHOD(UnSerialize)			(THIS_ LPVOID, UINT) PURE;
	STDMETHOD(Serialize)			(THIS_ LPVOID, UINT) PURE;
	STDMETHOD(Duplicate)			(THIS_ LPVOID) PURE;

	STDMETHOD(SetName)			(THIS_ LPCVOID) PURE;
	STDMETHOD(GetName)			(THIS_ LPVOID, int) PURE;
};

/* IA3dScene: 33 slots; rtl:0x10054220; dbg:0x1013953C.
 * Unknown_0x1C, Unknown_0x40 and Unknown_0x44 argument roles and types are unknown. */

#undef INTERFACE
#define INTERFACE IA3dScene

DECLARE_INTERFACE_(IA3dScene, IUnknown)
{
	STDMETHOD(QueryInterface)		(THIS_ REFIID, LPVOID FAR *) PURE;
	STDMETHOD_(ULONG,AddRef)		(THIS) PURE;
	STDMETHOD_(ULONG,Release)		(THIS) PURE;

	STDMETHOD_(int,AddRoom)	(THIS_ LPVOID, LPVOID, LPA3DVAL, LPA3DVAL, LPA3DVAL) PURE;
	STDMETHOD_(int,AddWall)	(THIS_ LPVOID, LPVOID, LPA3DVAL, LPA3DVAL, LPA3DVAL) PURE;
	STDMETHOD(RemoveRoom)	(THIS_ int) PURE;
	STDMETHOD(RemoveWall)	(THIS_ int) PURE;
	STDMETHOD(Unknown_0x1C)	(THIS_ LPVOID) PURE;

	STDMETHOD_(int,IsScene)	(THIS) PURE;

	STDMETHOD(GetRoom)	(THIS_ int, LPVOID *) PURE;
	STDMETHOD(GetWall)	(THIS_ int, int, LPVOID *) PURE;
	STDMETHOD(GetOpening)	(THIS_ int, int, const class CA3dFrame *,
				       const A3DVAL *) PURE;
	STDMETHOD(FindByPoint)	(THIS_ A3DVAL, A3DVAL, A3DVAL, LPVOID *) PURE;
	STDMETHOD(NewTransform)	(THIS_ class CA3dFrame **) PURE;
	STDMETHOD(SetMaterial)	(THIS_ LPVOID) PURE;
	STDMETHOD(GetMaterial)	(THIS_ LPVOID *) PURE;
	STDMETHOD(Unknown_0x40)	(THIS_ LPVOID) PURE;
	STDMETHOD(Unknown_0x44)	(THIS_ LPVOID) PURE;
	STDMETHOD(SetRoomFlags)	(THIS_ int, DWORD) PURE;
	STDMETHOD(ClearRoomFlags)	(THIS_ int, DWORD) PURE;
	STDMETHOD(SetCurrentRoom)(THIS_ int) PURE;
	STDMETHOD(Compile)	(THIS) PURE;
	STDMETHOD(NextVertex)	(THIS_ LPA3DVAL, LPDWORD, class CA3dPool *) PURE;
	STDMETHOD(ReleaseCompiledScene)	(THIS) PURE;
	STDMETHOD(Build)	(THIS) PURE;
	STDMETHOD(Clear)	(THIS) PURE;
	STDMETHOD(Load)		(THIS_ LPVOID) PURE;
	STDMETHOD(Save)		(THIS_ LPVOID) PURE;
	STDMETHOD(UnSerialize)	(THIS_ LPVOID, UINT) PURE;
	STDMETHOD(Serialize)	(THIS_ LPVOID, UINT) PURE;
	STDMETHOD(Duplicate)	(THIS_ LPVOID) PURE;

	STDMETHOD(SetName)	(THIS_ LPCVOID) PURE;
	STDMETHOD(GetName)	(THIS_ LPVOID, int) PURE;
};

#endif /* _A3D33_H */
