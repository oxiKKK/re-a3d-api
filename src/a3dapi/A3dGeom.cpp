/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dGeom.cpp
 *
 * Implements geometry submission and acoustic tracing on CA3dRoot.
 * Application primitives, material bindings and opening factors are
 * stored in geometry pools, either for the current frame or for a
 * retained display list.
 *
 * The tracing passes test source paths against that geometry, apply
 * occlusion and transmission, and construct reflected paths for
 * audibility selection. The file also controls tracing intervals,
 * reflection scales and geometric reverb measurements.
 *
 * ChunkPage.cpp supplies pool storage, A3dList.cpp records reusable
 * geometry, and refaudbin.cpp selects reflection images. Source-specific
 * control calculation and submission are implemented in A3dSource.cpp.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "A3dGeom.h"
#include "A3d3.h"
#include "A3dList.h"
#include "A3dMatrix.h"
#include "A3dSource.h"
#include "a3dsourcecom.h"
#include "A3dReflection.h"
#include "refaudbin.h"
#include "ChunkPage.h"
#include "LinkList.h"
#include "MaterialObject.h"

#include <math.h>
#include <float.h>
#include <stdlib.h>


/* (RE) dbg:0x1000EFAA; rtl:0x10004342. */
#define A3D_GEOM_DEFAULT_WALK_BUDGET 16000000

/* Hit-test bounds  */
#define A3D_TRACE_DENOM_MIN             0.000001f
#define A3D_TRACE_LOOSE_ALPHA_MIN       (-0.000001f)
#define A3D_TRACE_LOOSE_SUM_MAX         1.000001f

/* Room-estimation constants */
#define A3D_MEASURE_END_NORMAL_THRESHOLD        0.9848f
#define A3D_MEASURE_LATERAL_INCIDENCE_MIN       0.707f

extern void	A3dPrimAxisRange(const A3DPRIMITIVE *pcPrim, A3DVAL *pfRange,
				 int nAxis);

/* =============================================================
// ReadA3dRegistryDword()
// (RE) rtl:0x10004190; dbg:0x1000E940
//
// Zero the output DWORD and query HKLM\Software\Aureal\A3D without
// checking the registry value type.
//
// Returns: RegQueryValueExA status; E_FAIL if opening the key fails.
// =============================================================*/

LSTATUS
ReadA3dRegistryDword(LPCSTR lpValueName, LPBYTE lpData)
{
DWORD	cbData;
HKEY	hKey;
LSTATUS	lResult;

	*(DWORD *) lpData = 0;

	lResult = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Aureal\\A3D",
				0, KEY_READ, &hKey);

	if (lResult)
		return (E_FAIL);

	cbData = 4;

	lResult = RegQueryValueExA(hKey, lpValueName, 0, 0, lpData, &cbData);

	if (hKey)
		RegCloseKey(hKey);

	return (lResult);
}

/* =============================================================
// CreateGeom()
// (RE) rtl:0x10004200; dbg:0x1000EE20
//
// Allocate the first geometry pool and initialize submission and tracing state.
//
// Returns:
//   S_OK
//   A3DERROR_MEMORY_ALLOCATION  if pool allocation fails
// =============================================================*/

HRESULT
CA3dRoot::CreateGeom(void)
{
	m_cPoolElements = A3D_POOL_ELEMENTS;

	m_pPool    = new CA3dPool(m_cPoolElements, 0);
	m_pElement = NULL;

	if (!m_pPool)
		return (A3DERROR_MEMORY_ALLOCATION);

	m_GeomPools.push_back(m_pPool);

	m_itGeomPool = m_GeomPools.begin();
	m_pSavedPool = NULL;

	m_nVertexIndex = 0;

	ZeroMemory(&m_dwTag, sizeof(A3DPRIMITIVE));

	m_dwWalkEnable          = 1;
	m_Unknown_0x14A4        = 1;

	Compat(A3DCOMPAT_TINT_REFLECTIONS, 1);

	m_afWalkMaterial[2] = 0.0f;
	m_afWalkMaterial[3] = 0.0f;
	m_afWalkMaterial[0] = 1.0f;
	m_afWalkMaterial[1] = 1.0f;

	m_fReflectScale = 2.0f;
	m_cWalkMax      = A3D_GEOM_DEFAULT_WALK_BUDGET;
	m_cWalkDone     = 0;

	m_fGeomReady = 1;

	m_dwRenderMode = A3D_1ST_REFLECTIONS | A3D_OCCLUSIONS;
	m_dwWalkSeq    = 0;

	if (m_dwDoRefsEvery)
		m_dwReflectionUpdateInterval = m_dwDoRefsEvery;

	if (m_dwDoOccsEvery)
		m_dwOcclusionUpdateInterval = m_dwDoOccsEvery;

	m_fOpening      = 0.0f;
	m_pfOpening     = NULL;

	m_dwGeomReverbParamSize = 16;
	m_fGeomScaling          = 1.0f;
	m_fEffectScaling        = 1.0f;
	m_nMeasureAxis          = 1;

	return (S_OK);
}

/* =============================================================
// Enable()
// (RE) rtl:0x10004430; dbg:0x1000F2C0; IA3dGeom2 slot 3
//
// Enable geometry features when any requested bit is available.
//
// Returns:
//   S_OK
//   A3DERROR_FEATURE_NOT_SUPPORTED  if no requested bit is available
// =============================================================*/

STDMETHODIMP
CA3dRoot::Enable(DWORD dwFeature)
{
	if (m_dwInterfaceVersion < 5 && (dwFeature & A3D_1ST_REFLECTIONS))
		dwFeature |= A3D_GEOMETRIC_REVERB;

	if (!(dwFeature & m_dwFeaturesAvailable))
		return (A3DERROR_FEATURE_NOT_SUPPORTED);

	m_dwFeaturesEnabled |= dwFeature;

	return (S_OK);
}

/* =============================================================
// Disable()
// (RE) rtl:0x10004470; dbg:0x1000F330; IA3dGeom2 slot 4
//
// Disable geometry features and reset unbound geometric reverb when needed.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dRoot::Disable(DWORD dwFeature)
{
	if (m_dwInterfaceVersion < 5 && (dwFeature & A3D_1ST_REFLECTIONS))
		dwFeature |= A3D_GEOMETRIC_REVERB;

	m_dwFeaturesEnabled &= ~dwFeature;

	if ((dwFeature & A3D_GEOMETRIC_REVERB) && !m_pBoundedReverb &&
	    m_pReverbPropSet && m_dwLegacyReverbPreset != -1)
	{
		ResetReverbVocabularyDefaults();

		m_dwLegacyReverbPreset = -1;
	}

	return (S_OK);
}

/* =============================================================
// IsEnabled()
// (RE) rtl:0x100044E0; dbg:0x1000F3F0; IA3dGeom2 slot 5
//
// Test whether any requested geometry feature is enabled.
//
// Returns:
//   TRUE   if any requested bit is enabled
//   FALSE  otherwise
// =============================================================*/

STDMETHODIMP_(BOOL)
CA3dRoot::IsEnabled(DWORD dwFeature)
{
	return ((dwFeature & m_dwFeaturesEnabled) != 0);
}

/* =============================================================
// SetReflectionGainScale()
// (RE) rtl:0x10004500; dbg:0x1000F430; IA3dGeom2 slot 10
//
// Set the global reflection gain scale.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a negative scale
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetReflectionGainScale(A3DVAL fScale)
{
	if (fScale < 0.0f)
		return (E_INVALIDARG);

	m_fGlobalReflectionGainScale = fScale;

	return (S_OK);
}

/* =============================================================
// GetReflectionGainScale()
// (RE) rtl:0x10004530; dbg:0x1000F480; IA3dGeom2 slot 11
//
// Read the global reflection gain scale.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetReflectionGainScale(LPA3DVAL pfScale)
{
	if (!pfScale)
		return (E_POINTER);

	*pfScale = m_fGlobalReflectionGainScale;

	return (S_OK);
}

/* =============================================================
// SetReflectionDelayScale()
// (RE) rtl:0x10004560; dbg:0x1000F4C0; IA3dGeom2 slot 12
//
// Set the global reflection delay scale. Negative values are ignored.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetReflectionDelayScale(A3DVAL fScale)
{
	if (fScale >= 0.0f)
		m_fGlobalReflectionDelayScale = fScale;

	return (S_OK);
}

/* =============================================================
// GetReflectionDelayScale()
// (RE) rtl:0x10004590; dbg:0x1000F510; IA3dGeom2 slot 13
//
// Read the global reflection delay scale.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetReflectionDelayScale(LPA3DVAL pfScale)
{
	if (!pfScale)
		return (E_POINTER);

	*pfScale = m_fGlobalReflectionDelayScale;

	return (S_OK);
}

/* =============================================================
// Begin()
// (RE) rtl:0x100045C0; dbg:0x1000F550; IA3dGeom2 slot 26
//
// Select a primitive input mode. An invalid mode closes the current block.
//
// Returns:
//   S_OK
//   A3DERROR_INVALID_BEGIN_MODE  an unsupported mode
// =============================================================*/

STDMETHODIMP
CA3dRoot::Begin(DWORD dwMode)
{
	switch (dwMode)
	{
	case A3D_LINES:
	case A3D_SUB_LINES:
		m_nVerticesPerPrimitive = 2;
		break;

	case A3D_TRIANGLES:
	case A3D_SUB_TRIANGLES:
		m_nVerticesPerPrimitive = 3;
		break;

	case A3D_QUADS:
	case A3D_SUB_QUADS:
		m_nVerticesPerPrimitive = 4;
		break;

	default:
		m_nBeginMode = A3D_INVALID_INPUTMODE;

#ifdef _DEBUG
		ReportError(A3DERROR_INVALID_BEGIN_MODE,
				   "A3dGeom::Begin() "
				   "A3DERROR_INVALID_BEGIN_MODE");
#endif
		return (A3DERROR_INVALID_BEGIN_MODE);
	}

	m_nBeginMode       = (int) dwMode;
	m_fNormalPending   = 1;
	m_dwPrimitiveFlags = 0;

	return (S_OK);
}

/* =============================================================
// End()
// (RE) rtl:0x10004650; dbg:0x1000F650; IA3dGeom2 slot 27
//
// Close the current primitive input block.
//
// Returns:
//   S_OK
//   A3DERROR_END_BEFORE_VALID_BEGIN_BLOCK
//                                       without an open block
// =============================================================*/

STDMETHODIMP
CA3dRoot::End(void)
{
	if (m_nBeginMode == A3D_INVALID_INPUTMODE)
	{
#ifdef _DEBUG
		ReportError(A3DERROR_END_BEFORE_VALID_BEGIN_BLOCK,
				   "A3dGeom::End() "
				   "A3DERROR_END_BEFORE_VALID_BEGIN_BLOCK");
#endif
		return (A3DERROR_END_BEFORE_VALID_BEGIN_BLOCK);
	}

	m_nBeginMode = A3D_INVALID_INPUTMODE;

	return (S_OK);
}

/* =============================================================
// Tag()
// (RE) rtl:0x10004670; dbg:0x1000F6B0; IA3dGeom2 slot 32
//
// Set the tag for subsequently submitted primitives.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dRoot::Tag(DWORD dwTag)
{
	m_dwTag = dwTag;

	return (S_OK);
}

/* =============================================================
// NextFreeChunk()
// (RE) rtl:0x10004A30; dbg:0x1000FEC0
//
// Select the next pool element, allocating a geometry or recording pool
// when needed. Allocation failure leaves m_pElement null.
// =============================================================*/

void
CA3dRoot::NextFreeChunk(void)
{
	m_pElement = m_pPool->NextFreeElement();

	if (m_pElement)
		return;

	m_pPool = new CA3dPool(m_cPoolElements, 0);

	if (!m_pPool)
	{
#ifdef _DEBUG
		ReportError(A3DERROR_MEMORY_ALLOCATION,
				   "A3dGeom::NextFreeChunk() "
				   "A3DERROR_MEMORY_ALLOCATION");
#endif
		return;
	}

	if (m_cListsRecording)
		m_ScratchPools.push_back(m_pPool);
	else
		m_GeomPools.push_back(m_pPool);

	m_pElement = m_pPool->NextFreeElement();
}

/* =============================================================
// Vertex3fv()
// (RE) rtl:0x10004690; dbg:0x1000F6E0; IA3dGeom2 slot 29
//
// Submit a vertex from a three-component vector.
//
// Returns: Vertex3f result.
// =============================================================*/

STDMETHODIMP
CA3dRoot::Vertex3fv(LPA3DVAL pv)
{
	return (Vertex3f(pv[0], pv[1], pv[2]));
}

/* =============================================================
// Vertex3f()
// (RE) rtl:0x100046B0; dbg:0x1000F720; IA3dGeom2 slot 28
//
// Transform and append a vertex, storing completed primitives and updating open
// lists. The original overwrites supplied normals and accepts zero normals.
//
// Returns:
//   S_OK
//   A3DERROR_GEOMETRY_INPUT_OUTSIDE_BEGIN_END_BLOCK
//                                       outside Begin/End
//   A3DERROR_MEMORY_ALLOCATION          if no pool element is available
// =============================================================*/

STDMETHODIMP
CA3dRoot::Vertex3f(A3DVAL x, A3DVAL y, A3DVAL z)
{
CA3dStdList<CA3dList *>::iterator it;
A3DELEMENT     *pPrim;
const A3DVAL   *m;
A3DVAL         *pv;
A3DVAL ax, ay, az, bx, by, bz;
A3DVAL fLength;

	if (m_nBeginMode == A3D_INVALID_INPUTMODE)
	{
#ifdef _DEBUG
		ReportError(
			A3DERROR_GEOMETRY_INPUT_OUTSIDE_BEGIN_END_BLOCK,
			"A3dGeom::Vertex3f() "
			"A3DERROR_GEOMETRY_INPUT_OUTSIDE_BEGIN_END_BLOCK");
#endif
		return (A3DERROR_GEOMETRY_INPUT_OUTSIDE_BEGIN_END_BLOCK);
	}

	pv = m_avVertex[m_nVertexIndex];

	if (m_adwMatrixIsIdentity[m_nMatrixDepth])
	{
		pv[0] = x;
		pv[1] = y;
		pv[2] = z;
		pv[3] = 1.0f;
	}
	else
	{
		m = m_amatStack[m_nMatrixDepth];

		pv[0] = m[0] * x + m[4] * y + m[8]  * z + m[12];
		pv[1] = m[1] * x + m[5] * y + m[9]  * z + m[13];
		pv[2] = m[2] * x + m[6] * y + m[10] * z + m[14];
		pv[3] = 1.0f;
	}

	if (m_nVertexIndex < m_nVerticesPerPrimitive - 1)
	{
		m_nVertexIndex++;

		return (S_OK);
	}

	if (m_dwInfinitePlanes)
		m_dwPrimitiveFlags |= A3D_PRIM_INFINITE_PLANE;

	if (!(m_dwRenderMode & A3D_OCCLUSIONS))
		m_dwPrimitiveFlags |= A3D_PRIM_NO_OCCLUDE;

	if (!(m_dwRenderMode & A3D_1ST_REFLECTIONS))
		m_dwPrimitiveFlags |= A3D_PRIM_NO_REFLECT;

	ax = m_avVertex[1][0] - m_avVertex[0][0];
	ay = m_avVertex[1][1] - m_avVertex[0][1];
	az = m_avVertex[1][2] - m_avVertex[0][2];

	bx = m_avVertex[2][0] - m_avVertex[0][0];
	by = m_avVertex[2][1] - m_avVertex[0][1];
	bz = m_avVertex[2][2] - m_avVertex[0][2];

	m_vNormal[0] = by * az - bz * ay;
	m_vNormal[1] = bz * ax - bx * az;
	m_vNormal[2] = bx * ay - by * ax;

	fLength = A3dFastSqrt(m_vNormal[0] * m_vNormal[0] +
			      m_vNormal[1] * m_vNormal[1] +
			      m_vNormal[2] * m_vNormal[2]);

#ifdef _DEBUG
	if (fLength == 0.0f)
		ReportError(A3DERROR_INVALID_NORMAL,
				   "A3dGeom::Vertex3f() "
				   "A3DERROR_INVALID_NORMAL");
#endif

	if (fLength != 0.0f)
	{
		m_vNormal[0] = (1.0f / fLength) * m_vNormal[0];
		m_vNormal[1] = (1.0f / fLength) * m_vNormal[1];
		m_vNormal[2] = (1.0f / fLength) * m_vNormal[2];
	}

	NextFreeChunk();

	if (!m_pElement)
		return (A3DERROR_MEMORY_ALLOCATION);

	pPrim = (A3DELEMENT *) m_pElement;

	pPrim->dwMode    = (DWORD) m_nBeginMode;
	pPrim->fOpening  = m_fOpening;
	pPrim->pfOpening = m_pfOpening;

	CopyMemory(&pPrim->prim, &m_dwTag, sizeof(A3DPRIMITIVE));

	m_nVertexIndex = 0;

	if (m_dwRenderMode & A3D_OCCLUSIONS)
		for (it = m_OpenLists.begin();
		     it != m_OpenLists.end();
		     ++it)
			(*it)->UpdateBoundingVol((const A3DPRIMITIVE *)
						  &m_dwTag);

	return (S_OK);
}

/* =============================================================
// Normal3f()
// (RE) rtl:0x10004BC0; dbg:0x100100F0; IA3dGeom2 slot 30
//
// Store an untransformed normal, including before rejecting a zero normal.
//
// Returns:
//   S_OK
//   A3DERROR_INVALID_NORMAL  zero length
// =============================================================*/

STDMETHODIMP
CA3dRoot::Normal3f(A3DVAL x, A3DVAL y, A3DVAL z)
{
	m_vNormal[0] = x;
	m_vNormal[1] = y;
	m_vNormal[2] = z;

	m_fNormalPending = 0;

	if ((A3DVAL) sqrt(m_vNormal[0] * m_vNormal[0] +
			  m_vNormal[1] * m_vNormal[1] +
			  m_vNormal[2] * m_vNormal[2]) != 0.0f)
		return (S_OK);

#ifdef _DEBUG
	ReportError(A3DERROR_INVALID_NORMAL,
			   "A3dGeom::Normal3f() A3DERROR_INVALID_NORMAL");
#endif
	return (A3DERROR_INVALID_NORMAL);
}

/* =============================================================
// Normal3fv()
// (RE) rtl:0x10004BA0; dbg:0x100100B0; IA3dGeom2 slot 31
//
// Store a normal from a three-component vector.
//
// Returns: Normal3f result.
// =============================================================*/

STDMETHODIMP
CA3dRoot::Normal3fv(LPA3DVAL pv)
{
	return (Normal3f(pv[0], pv[1], pv[2]));
}

/* =============================================================
// NewMaterial()
// (RE) rtl:0x10004C00; dbg:0x100101E0; IA3dGeom2 slot 35
//
// Create a material with one client reference.
//
// Returns:
//   S_OK
//   E_POINTER                   a null output
//   A3DERROR_MEMORY_ALLOCATION  if allocation fails
// =============================================================*/

STDMETHODIMP
CA3dRoot::NewMaterial(LPA3DMATERIAL *ppMaterial)
{
CA3dMaterial *pMaterial;

	if (!ppMaterial)
		return (E_POINTER);

	*ppMaterial = NULL;

	pMaterial = new CA3dMaterial;

	*ppMaterial = pMaterial;

	if (!pMaterial)
		return (A3DERROR_MEMORY_ALLOCATION);

	pMaterial->AddRef();

	return (S_OK);
}

/* =============================================================
// BindMaterial()
// (RE) rtl:0x10004CA0; dbg:0x100102E0; IA3dGeom2 slot 36
//
// Append the material coefficients to the geometry stream.
//
// Returns:
//   S_OK
//   A3DERROR_MEMORY_ALLOCATION  if no pool element is available
// =============================================================*/

STDMETHODIMP
CA3dRoot::BindMaterial(LPA3DMATERIAL pMaterial)
{
A3DELEMENT *pPrim;

	NextFreeChunk();

	pPrim = (A3DELEMENT *) m_pElement;

	if (!pPrim)
		return (A3DERROR_MEMORY_ALLOCATION);

	pPrim->dwMode = A3D_MATERIAL;

	CopyMemory(&pPrim->prim, &((CA3dMaterial *) pMaterial)->m_fReflectGain,
		   6 * sizeof(A3DVAL));

	return (S_OK);
}

/* =============================================================
// BindListener()
// (RE) rtl:0x10004CF0; dbg:0x10010370; IA3dGeom2 slot 38
//
// Copy the current geometry transform to the listener.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dRoot::BindListener(void)
{
	CopyMemory(m_matListener, m_amatStack[m_nMatrixDepth],
		   sizeof(m_matListener));

	return (S_OK);
}

/* =============================================================
// BindSource()
// (RE) rtl:0x10004D20; dbg:0x100103C0; IA3dGeom2 slot 39
//
// Copy the current geometry transform to a wave source.
//
// Returns:
//   S_OK
//   E_POINTER  a null or non-wave source
// =============================================================*/

STDMETHODIMP
CA3dRoot::BindSource(LPA3DSOURCE2 pSource)
{
CA3dSourceCom  *pCom;
CA3dSource     *pObj;

	pCom = (CA3dSourceCom *) pSource;

	if (!pCom)
		return (E_POINTER);

	if (pCom->m_dwPointerType != A3D_SOURCE_POINTER_WAVE)
		return (E_POINTER);

	pObj = (CA3dSource *) pCom->m_pSource;

	CopyMemory(pObj->m_matSource, m_amatStack[m_nMatrixDepth],
		   sizeof(pObj->m_matSource));

	return (S_OK);
}

/* =============================================================
// Trace()
// (RE) rtl:0x100056D0; dbg:0x10011180
//
// Update source placement, occlusion, reflections and geometric reverb.
//
// Returns: The source count. The reference leaves EstimateReverb's result in
//          eax when that tail runs; callers ignore the result.
// =============================================================*/

int
CA3dRoot::Trace(void)
{
CA3dStdList<CA3dSource *>::iterator	it;
CA3dStdList<CA3dSource *>::iterator	itEnd;
CA3dSource	*pSource;
A3DVAL		matListenerLeft[16], matListenerRight[16];
A3DVAL		matLeftInv[16], matRightInv[16];
A3DVAL		vListenerVel[4];
A3DVAL		vMidPoint[4], vLeftPoint[4], vRightPoint[4];
A3DVAL		vMidPolar[3], vLeftPolar[3], vRightPolar[3];
A3DVAL  vEars[4];
A3DVAL  afMeasure[3];
A3DVAL  fDistance;
int     cSources;
int     cReverb;
int     fThisPass;
int     fTight;
DWORD   dwNow;

	m_dwWalkSeq = 0;

	cSources = m_SourceArray.size();

	if (cSources < 1)
		return (cSources);

	/* Preserve the original unused reverb count. */
	if (m_dwUseDalInterface)
	{
		cReverb = 0;

		for (it = m_SourceArray.begin(), itEnd = m_SourceArray.end();
		     it != itEnd; ++it)
		{
			pSource = *it;

			if (pSource->m_dwPlayState &&
			    (pSource->m_dwRenderMode & A3DSOURCE_RENDERMODE_1ST_REFLECTIONS))
			{
				cReverb++;
			}
		}
	}
	else
	{
		cReverb = 1;
	}

	A3dTraceBegin(this, matListenerLeft, m_matListenerXform, matListenerRight);

	A3dMatrixInvert(m_matListenerXform, m_matListenerXform2);

	if (m_fBinauralWalk)
	{
		A3dMatrixInvert(matListenerLeft, matLeftInv);
		A3dMatrixInvert(matListenerRight, matRightInv);
	}

	A3dMatrixGetTranslation(m_matListenerXform, m_vListenerPos);

	GetListenerVelocity(vListenerVel);

	dwNow = timeGetTime();

	if (dwNow - m_dwLastTrace >= m_dwTraceInterval)
	{
		fThisPass       = 1;
		m_dwLastTrace   = dwNow;
	}
	else
	{
		fThisPass = 0;
	}

	for (it = m_SourceArray.begin(), itEnd = m_SourceArray.end();
	     it != itEnd; ++it)
	{
		pSource = *it;

		if (!pSource->m_dwPlayState || A3dSourceTraceSkip(pSource))
			continue;

		m_pTraceSource = pSource;

		if (m_pReverbPropSet && pSource->m_dwReverbMixDirty &&
		    m_pBoundedReverb)
			m_listReverbSources.push_back(pSource);

		/* Original defect: native sources use stale or uninitialized polar locals. */
		if (!(pSource->m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE))
		{

			A3dSourceGetMatrix(pSource, (A3DVAL *) pSource->m_adwVolumeWorkingSet);

			A3dMatrixGetTranslation((const A3DVAL *) pSource->m_adwVolumeWorkingSet,
						pSource->m_avReflectionPoint);

			A3dTransformPoint(pSource->m_avReflectionPoint, m_matListenerXform2,
					  vMidPoint);

			if (m_fBinauralWalk)
			{
				A3dTransformPoint(pSource->m_avReflectionPoint, matLeftInv,
						  vLeftPoint);
				A3dTransformPoint(pSource->m_avReflectionPoint, matRightInv,
						  vRightPoint);

				A3dToPolar(vLeftPoint, vLeftPolar);
				A3dToPolar(vRightPoint, vRightPolar);

				vMidPolar[0] = (vLeftPolar[0] + vRightPolar[0]) * 0.5f;
				vMidPolar[1] = (vLeftPolar[1] + vRightPolar[1]) * 0.5f;
				vMidPolar[2] = (vLeftPolar[2] + vRightPolar[2]) * 0.5f;
			}
			else
			{
				A3dToPolar(vMidPoint, vMidPolar);
			}

			m_vToListener[0] = pSource->m_avReflectionPoint[0] - m_vListenerPos[0];
			m_vToListener[1] = pSource->m_avReflectionPoint[1] - m_vListenerPos[1];
			m_vToListener[2] = pSource->m_avReflectionPoint[2] - m_vListenerPos[2];

			A3dSetDelay(pSource, vMidPolar);
			A3dTraceSetEars(pSource, vEars);
			A3dDoppler(pSource, vEars, vListenerVel, m_vToListener,
				   vMidPolar[2]);

			fDistance = A3dFastSqrt(m_vToListener[0] * m_vToListener[0] +
						m_vToListener[1] * m_vToListener[1] +
						m_vToListener[2] * m_vToListener[2]);

			A3dTraceSetVector(pSource,
					  (const A3DVAL *) pSource->m_adwVolumeWorkingSet,
					  m_vToListener, fDistance);

			/* The original supplies distance for both spread operands. */
			A3dTraceSetSpread(pSource, fDistance, fDistance);

			if (fThisPass)
			{
				pSource->m_fTraced =
					(pSource->m_dwTraceCount % m_dwReflectionUpdateInterval) ? 0 : 1;

				pSource->m_dwTraceCount++;

				fTight = (pSource->m_dwTraceCount % m_dwOcclusionUpdateInterval) == 0;
			}
			else
			{
				if (pSource->m_fManualReflections)
				{
					pSource->m_fTraced             = 1;
					pSource->m_fManualReflections  = 0;
				}
				else
				{
					pSource->m_fTraced = 0;
				}

				fTight = 0;
			}

			if (m_fGeomReady)
			{
				m_fOccludeFactor = 0.0f;
				m_fCurOcclude[0] = 1.0f;
				m_fCurOcclude[1] = 1.0f;

				if ((m_dwFeaturesEnabled & A3D_OCCLUSIONS) &&
				    !m_dwDisableOcclusions &&
				    (pSource->m_dwRenderMode & A3DSOURCE_RENDERMODE_OCCLUSIONS))
				{
					A3DVAL	*pafCache = pSource->m_afOcclusionCache;

					if (fTight)
					{
						TraceWalk();

						pafCache[0] = m_fOccludeFactor;
						pafCache[1] = m_fCurOcclude[0];
						pafCache[2] = m_fCurOcclude[1];
					}
					else
					{
						m_fOccludeFactor        = pafCache[0];
						m_fCurOcclude[0]        = pafCache[1];
						m_fCurOcclude[1]        = pafCache[2];
					}
				}

				A3dTraceApply(pSource, m_fOccludeFactor, m_fCurOcclude);

				if (((m_dwFeaturesEnabled & A3D_1ST_REFLECTIONS) &&
				     !m_dwDisableReflections &&
				     pSource->m_fTraced &&
				     (pSource->m_dwRenderMode & A3DSOURCE_RENDERMODE_1ST_REFLECTIONS)) ||
				    pSource->m_pManualReflections)
				{
					CRefAudBin	bin;

					m_pReflectBin = &bin;
					bin.SetWindow(pSource);
					ReflectSurfaces();

					if ((m_dwFeaturesEnabled & A3D_1ST_REFLECTIONS) &&
					    !m_dwDisableReflections &&
					    pSource->m_fTraced &&
					    (pSource->m_dwRenderMode & A3DSOURCE_RENDERMODE_1ST_REFLECTIONS))
					{
						ReflectWalk();
					}

					bin.SaveWindow(pSource);
					bin.Update(this, m_matListenerXform, pSource);
					m_pReflectBin = NULL;
				}
			}
			else if (pSource->m_pManualReflections)
			{

				CRefAudBin	bin;

				m_pReflectBin = &bin;
				bin.SetWindow(pSource);
				ReflectSurfaces();
				bin.SaveWindow(pSource);
				bin.Update(this, m_matListenerXform, pSource);
				m_pReflectBin = NULL;
			}
		}

		if (m_fBinauralWalk)
			A3dSourceStep(pSource, vLeftPolar, vRightPolar);
		else
			A3dSourceStep(pSource, vMidPolar, vMidPolar);

		if ((m_dwFeaturesEnabled & A3D_1ST_REFLECTIONS) &&
		    !m_dwDisableReflections &&
		    (pSource->m_dwRenderMode & A3DSOURCE_RENDERMODE_1ST_REFLECTIONS) &&
		    !(pSource->m_dwRenderMode & A3DSOURCE_RENDERMODE_NATIVE))
		{
			if (!pSource->m_fTraced)
				A3dSourceCarry(pSource);
		}
		else
		{
			A3dSourceQuiet(pSource);
		}

		pSource->m_fTraced = (pSource->m_fTraced == 0);

		A3dSourceStepEnd(pSource, 0);
		A3dSourceEmit(pSource);
	}

	if (m_pReverbPropSet &&
	    (m_dwFeaturesEnabled & A3D_GEOMETRIC_REVERB) &&
	    !m_pBoundedReverb)
	{
		ZeroMemory(afMeasure, sizeof(afMeasure));

		MeasureRoom(afMeasure);

		EstimateReverb(afMeasure);
	}

	return (cSources);
}

/* =============================================================
// TraceWalk()
// (RE) rtl:0x10006EC0; dbg:0x100139C0; thunk dbg:0x1000312A
//
// Walk geometry for source occlusion within the traversal budget without
// room-cache culling or walk-sequence cache updates.  (RE) entry helper
// dbg:0x10014350; thunk dbg:0x1000374C.
// =============================================================*/

void
CA3dRoot::TraceWalk(void)
{
CA3dStdList<CA3dPool *>::iterator it;
CA3dStdList<CA3dPool *>::iterator itEnd;
CA3dPool   *pPool;
CA3dList   *pList;
CA3dSource *pSource;
int         cLeft;

	++m_dwWalkSeq;

	pSource = m_pTraceSource;

	/* Retest the element that occluded this source last time. A still-solid
	 * occluder ends the walk before any pool is visited. */
	if (pSource->m_pOccludingList && pSource->m_pOccludingList->m_bCalled)
	{
		if (TraceRetestOccluder(pSource->m_pOccludingList,
					(int) pSource->m_dwOccludingElementIndex,
					(const A3DVAL *)
						pSource->m_adwOcclusionMaterialCache))
		{
			return;
		}

		pSource->m_pOccludingList->m_dwRetestedSeq = m_dwWalkSeq;
	}

	it    = m_GeomPools.begin();
	itEnd = m_GeomPools.end();

	cLeft       = m_GeomPools.size();
	m_cWalkDone = 0;

	if (it == itEnd)
		return;

	pPool = *it;
	++it;
	--cLeft;
	pPool->Rewind();

	for (;;)
	{
		m_pElement = pPool->NextElement();

		if (!m_pElement)
		{
			pList = (CA3dList *) pPool->m_dwListOwner;

			if (pList)
				pList->m_dwRetestedSeq = 0;

			/* Advance past pools the entry retest already covered
			 * and past lists the listener segment cannot reach. */
			for (;;)
			{
				if (cLeft-- <= 0)
					break;

				pPool = *it;
				++it;

				pList = (CA3dList *) pPool->m_dwListOwner;

				if (!pList)
					break;

				if (pList->m_dwRetestedSeq >= m_dwWalkSeq)
				{
					pList->m_dwRetestedSeq = 0;

					continue;
				}

				pList->m_dwRetestedSeq = 0;

				if (!pList->m_bBoundVolEnabled ||
				    pList->TestBoundingVol())
					break;
			}

			if (cLeft < 0)
				break;

			pPool->Rewind();

			m_pElement = pPool->NextElement();

			if (!m_pElement)
				break;
		}

		if (!TraceStep(pPool))
			break;

		if ((int) ++m_cWalkDone > (int) m_cWalkMax)
			break;
	}
}

/* =============================================================
// TraceRetestOccluder()
// (RE) dbg:0x10014350; thunk dbg:0x1000374C
//
// Restore a source's cached occluding material and retest its list from the
// cached element, wrapping to the elements before it. The reference uses
// m_pPool.
//
// Returns: 1 while the walk should continue; 0 after a solid occluding hit or
//          a list the listener segment cannot reach.
// =============================================================*/

int
CA3dRoot::TraceRetestOccluder(CA3dList *pList, int iElement,
			      const A3DVAL *pafMaterial)
{
CA3dPool *pPool;
int       cTested;
int       i;

	cTested = 0;

	CopyMemory(m_afWalkMaterial, pafMaterial, sizeof(m_afWalkMaterial));

	pPool   = pList->m_pPool;
	m_pPool = pPool;

	pPool->SetCursor(iElement);

	for (;;)
	{
		m_pElement = pPool->NextElement();

		if (!m_pElement)
			break;

		++cTested;

		if (!TraceStep(pPool))
			return (1);

		/* Once the cached element is past, a list the segment cannot
		 * reach needs no further testing. */
		if (cTested == 1 && pList->m_bBoundVolEnabled &&
		    !pList->TestBoundingVol())
		{
			return (0);
		}
	}

	pPool->Rewind();

	for (i = 0; i < iElement; i++)
	{
		m_pElement = pPool->NextElement();

		if (m_pElement && !TraceStep(pPool))
			return (1);
	}

	return (0);
}

/* =============================================================
// TraceStep()
// (RE) dbg:0x10013D40; thunk dbg:0x100010E6
//
// Apply a material record or test an occluding shape. The reference uses
// m_pPool; this implementation receives the current pool as an argument.
//
// Returns: 1 to continue the walk; 0 after a solid occluding hit.
// =============================================================*/

int
CA3dRoot::TraceStep(CA3dPool *pPool)
{
A3DELEMENT *pPrim;

	pPrim = (A3DELEMENT *) m_pElement;

	m_fOcclusion = 1.0f;

	switch (pPrim->dwMode)
	{
	case A3D_TRIANGLES:

		if (pPrim->prim.dwFlags & A3D_PRIM_NO_OCCLUDE)
			return (1);

		return (TraceHitOpen(pPool, A3dTraceHitTri));

	case A3D_QUADS:
		if (pPrim->prim.dwFlags & A3D_PRIM_NO_OCCLUDE)
			return (1);

		return (TraceHitOpen(pPool, A3dTraceHitQuad));

	case A3D_MATERIAL:
		CopyMemory(m_afWalkMaterial, &pPrim->prim,
			   sizeof(m_afWalkMaterial));

		return (1);

	default:
		return (1);
	}
}

/* =============================================================
// TraceHitOpen()
// (RE) dbg:0x10013E30; thunk dbg:0x10002DE2
//
// Test a near occluding face and its following openings, refine the gains for a
// volumetric source, and cache the occluder on the source for the next walk.
// Volume-axis initialization remains unresolved.
//
// Returns: 1 to continue; 0 for a solid occluding face.
// =============================================================*/

int
CA3dRoot::TraceHitOpen(CA3dPool *pPool,
		       BOOL (*pfnHit)(const A3DVAL *, const A3DVAL *,
				      A3DPRIMITIVE *, int, int))
{
A3DELEMENT     *pPrim;
CA3dSource     *pSource;
A3DVAL          fCoverage;
A3DVAL          fSizeDamp;
A3DVAL          fWeight;
A3DVAL          fVolumetric;
int             iElement;

	pPrim = (A3DELEMENT *) m_pElement;

	if (m_dwWalkEnable != 1 ||
	    !pfnHit(m_vListenerPos, m_vToListener, &pPrim->prim, 1, 0) ||
	    pPrim->prim.fDistance >= m_fWalkNear)
	{
		return (1);
	}

	/* Read before TraceOpeningWalk advances the pool cursor. */
	iElement = pPool->m_cUsed - 1;

	if (TraceOpeningWalk(pPool))
	{

		m_fCurOcclude[0] = m_fOcclusion;
		m_fCurOcclude[1] = m_fOcclusion;
		m_fOccludeFactor = m_fOcclusion;

		return (1);
	}

	m_fOccludeFactor = 1.0f;
	m_fCurOcclude[0] = m_afWalkMaterial[2];
	m_fCurOcclude[1] = m_afWalkMaterial[3];

	pSource = m_pTraceSource;

	if (pSource->m_dwVolumetricEnable)
	{
		fSizeDamp = A3dVolumetricSizeDamp(pSource, &pPrim->prim);
		fCoverage = A3dVolumetricCoverage(pSource, pfnHit, m_vListenerPos,
						  &pPrim->prim);

		fWeight     = pSource->m_VolDampInfo.fDampWeighting;
		fVolumetric = (1.0f - fWeight) * fSizeDamp + fCoverage * fWeight;

		m_fOccludeFactor = fVolumetric;

		m_fCurOcclude[0] = 1.0f - (1.0f - m_fCurOcclude[0]) * fVolumetric;
		m_fCurOcclude[1] = 1.0f - (1.0f - m_fCurOcclude[1]) * fVolumetric;
	}

	if (pPool->m_dwListOwner)
	{
		pSource->m_pOccludingList          = (CA3dList *) pPool->m_dwListOwner;
		pSource->m_dwOccludingElementIndex = iElement;

		CopyMemory(pSource->m_adwOcclusionMaterialCache, m_afWalkMaterial,
			   sizeof(pSource->m_adwOcclusionMaterialCache));
	}

	return (0);
}

/* =============================================================
// TraceOpeningWalk()
// (RE) dbg:0x10014130; thunk dbg:0x10001F0F
//
// Search following subfaces in the current pool for an opening.
// The reference uses m_pPool instead of a pool argument.
//
// Returns: 1 for an intersected open subface; 0 at a non-subface or pool
//          boundary.
// =============================================================*/

int
CA3dRoot::TraceOpeningWalk(CA3dPool *pPool)
{
A3DELEMENT     *pPrim;
DWORD           dwMode;

	do
	{
		for (;;)
		{
			m_pElement = pPool->NextElement();

			if (!m_pElement)
				return (0);

			pPrim  = (A3DELEMENT *) m_pElement;
			dwMode = pPrim->dwMode;

			if (dwMode != A3D_SUB_TRIANGLES)
				break;

			if (A3dTraceHitTri(m_vListenerPos, m_vToListener,
					   &pPrim->prim, 1, 0) &&
			    TraceOpeningFactor())
			{
				return (1);
			}
		}

		if (dwMode != A3D_SUB_QUADS)
			return (0);

		pPrim = (A3DELEMENT *) m_pElement;
	}
	while (!A3dTraceHitQuad(m_vListenerPos, m_vToListener, &pPrim->prim, 1, 0) ||
	       !TraceOpeningFactor());

	return (1);
}

/* =============================================================
// TraceOpeningFactor()
// (RE) dbg:0x10014280; thunk dbg:0x1000189D
//
// Read the subface opening factor into the current occlusion gain.
//
// Returns: For IA3d4+, whether the factor is positive; for earlier interfaces,
//          whether it is below 1, storing its complement as the gain.
// =============================================================*/

BOOL
CA3dRoot::TraceOpeningFactor(void)
{
A3DELEMENT     *pPrim;
A3DVAL          fFactor;

	pPrim = (A3DELEMENT *) m_pElement;

	fFactor = pPrim->pfOpening ? *pPrim->pfOpening : pPrim->fOpening;

	if (m_dwInterfaceVersion > 3)
	{
		m_fOcclusion = fFactor;

		return (m_fOcclusion > 0.0f);
	}

	m_fOcclusion = 1.0f - fFactor;

	return (fFactor < 1.0f);
}

/* =============================================================
// BeginListRecording()
// (RE) rtl:0x10007860; dbg:0x10014650
//
// Redirect submission to scratch pools for display-list recording.
// The original leaks a newly allocated pool when its capacity is insufficient.
//
// Returns:
//   S_OK
//   A3DERROR_MEMORY_ALLOCATION  allocation or capacity failure
// =============================================================*/

HRESULT
CA3dRoot::BeginListRecording(void)
{
CA3dPool *pPool;

	if (!m_cListsRecording)
	{
		m_pSavedPool = m_pPool;

		pPool = NULL;

		if (!m_ScratchPools.size())
		{
			pPool = new CA3dPool(m_cPoolElements, 0);

			if (!pPool)
				return (A3DERROR_MEMORY_ALLOCATION);

			if (pPool->m_nChunks != m_cPoolElements)
				return (A3DERROR_MEMORY_ALLOCATION);

			m_ScratchPools.push_back(pPool);
		}

		m_pPool = m_ScratchPools.front();
	}

	m_cListsRecording++;

	return (S_OK);
}

/* =============================================================
// EndListRecording()
// (RE) rtl:0x10007990; dbg:0x100147E0
//
// Finish recording; on the outermost close restore geometry submission and
// reset the retained scratch pool. An unmatched close underflows the count.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
CA3dRoot::EndListRecording(void)
{
int i;

	if (!--m_cListsRecording)
	{
		m_pPool = m_pSavedPool;

		for (i = m_ScratchPools.size(); i > 1; i--)
		{
			delete m_ScratchPools.back();

			m_ScratchPools.pop_back();
		}

		m_ScratchPools.front()->Clear();
	}

	return (S_OK);
}

/* =============================================================
// UnbindListFromSources()
// (RE) rtl:0x10007A90; dbg:0x100149B0
//
// Clear borrowed source pointers to a display list being destroyed.
// =============================================================*/

void
CA3dRoot::UnbindListFromSources(CA3dList *pList)
{
CA3dStdList<CA3dSource *>::iterator it;

	for (it = m_SourceArray.begin(); it != m_SourceArray.end(); ++it)
		if ((*it)->m_pOccludingList == pList)
			(*it)->m_pOccludingList = NULL;
}

/* =============================================================
// A3dListNext()
//
// Read the current list object and advance its cursor.
//
// Returns: The current object; NULL at the end of the list.
// =============================================================*/

void *
A3dListNext(A3DLIST *pList)
{
A3DLISTNODE *pNode;

	pNode = pList->pCurr;

	if (!pNode)
		return (NULL);

	pList->pCurr = pNode->pNext;

	return (pNode->pObject);
}

/* =============================================================
// A3dTransformPoint()
// (RE) dbg:0x10011EB0; thunk dbg:0x10001B3B
//
// Transform a point, writing four output values with w = 1.
// The reference returns the output pointer; this void form discards it.
// =============================================================*/

void
A3dTransformPoint(const A3DVAL *pv, const A3DVAL *pm, A3DVAL *pvOut)
{
A3DVAL av[3];

	av[0] = pv[0];
	av[1] = pv[1];
	av[2] = pv[2];

	pvOut[0] = av[0] * pm[0] + av[1] * pm[4] + av[2] * pm[8]  + pm[12];
	pvOut[1] = av[0] * pm[1] + av[1] * pm[5] + av[2] * pm[9]  + pm[13];
	pvOut[2] = av[0] * pm[2] + av[1] * pm[6] + av[2] * pm[10] + pm[14];
	pvOut[3] = 1.0f;
}

/* =============================================================
// A3dTraceHitTri()
// (RE) dbg:0x10012380; thunk dbg:0x10002F0E
//
// Test a segment or ray against a triangle and store the plane distance.
// The original may reverse the primitive normal even on a miss.
//
// Returns:
//   TRUE   an intersection
//   FALSE  otherwise
// =============================================================*/

BOOL
A3dTraceHitTri(const A3DVAL *pvFrom, const A3DVAL *pvTo, A3DPRIMITIVE *pBlock,
	       int nLoose, int nInfinite)
{
A3DVAL fDenom, fT;
A3DVAL fPx, fPy, fPz;
A3DVAL fNx, fNy, fNz;
A3DVAL fU0, fV0, fU1, fV1, fU2, fV2;
A3DVAL fAlpha, fBeta, fDet;
int fOut;

	fDenom = pvTo[0] * pBlock->vNormal[0] + pvTo[1] * pBlock->vNormal[1] +
		 pvTo[2] * pBlock->vNormal[2];

	if ((fDenom < 0.0f ? -fDenom : fDenom) < A3D_TRACE_DENOM_MIN)
		return (FALSE);

	fT = (pBlock->av[0][0] * pBlock->vNormal[0] +
	      pBlock->av[0][1] * pBlock->vNormal[1] +
	      pBlock->av[0][2] * pBlock->vNormal[2] -
	      (pvFrom[0] * pBlock->vNormal[0] + pvFrom[1] * pBlock->vNormal[1] +
	       pvFrom[2] * pBlock->vNormal[2])) / fDenom;

	if (fT < 0.0f)
	{

		pBlock->vNormal[0] = -pBlock->vNormal[0];
		pBlock->vNormal[1] = -pBlock->vNormal[1];
		pBlock->vNormal[2] = -pBlock->vNormal[2];

		fDenom = pvTo[0] * pBlock->vNormal[0] +
			 pvTo[1] * pBlock->vNormal[1] +
			 pvTo[2] * pBlock->vNormal[2];

		fT = (pBlock->av[0][0] * pBlock->vNormal[0] +
		      pBlock->av[0][1] * pBlock->vNormal[1] +
		      pBlock->av[0][2] * pBlock->vNormal[2] -
		      (pvFrom[0] * pBlock->vNormal[0] +
		       pvFrom[1] * pBlock->vNormal[1] +
		       pvFrom[2] * pBlock->vNormal[2])) / fDenom;
	}

	pBlock->fDistance = fT;

	if (fT < 0.0f)
		return (FALSE);

	if (!nInfinite && fT > 1.0f)
		return (FALSE);

	fPx = pvFrom[0] + fT * pvTo[0];
	fPy = pvFrom[1] + fT * pvTo[1];
	fPz = pvFrom[2] + fT * pvTo[2];

	fNx = pBlock->vNormal[0] < 0.0f ? -pBlock->vNormal[0] : pBlock->vNormal[0];
	fNy = pBlock->vNormal[1] < 0.0f ? -pBlock->vNormal[1] : pBlock->vNormal[1];
	fNz = pBlock->vNormal[2] < 0.0f ? -pBlock->vNormal[2] : pBlock->vNormal[2];

	if (fNx > fNy && fNx > fNz)
	{
		fU0 = fPy - pBlock->av[0][1];
		fV0 = fPz - pBlock->av[0][2];
		fU1 = pBlock->av[1][1] - pBlock->av[0][1];
		fV1 = pBlock->av[1][2] - pBlock->av[0][2];
		fU2 = pBlock->av[2][1] - pBlock->av[0][1];
		fV2 = pBlock->av[2][2] - pBlock->av[0][2];
	}
	else if (fNy > fNx && fNy > fNz)
	{
		fU0 = fPx - pBlock->av[0][0];
		fV0 = fPz - pBlock->av[0][2];
		fU1 = pBlock->av[1][0] - pBlock->av[0][0];
		fV1 = pBlock->av[1][2] - pBlock->av[0][2];
		fU2 = pBlock->av[2][0] - pBlock->av[0][0];
		fV2 = pBlock->av[2][2] - pBlock->av[0][2];
	}
	else
	{
		fU0 = fPx - pBlock->av[0][0];
		fV0 = fPy - pBlock->av[0][1];
		fU1 = pBlock->av[1][0] - pBlock->av[0][0];
		fV1 = pBlock->av[1][1] - pBlock->av[0][1];
		fU2 = pBlock->av[2][0] - pBlock->av[0][0];
		fV2 = pBlock->av[2][1] - pBlock->av[0][1];
	}

	if (fU1 == 0.0f)
	{
		if (fU2 == 0.0f)
			return (FALSE);

		fBeta = fU0 / fU2;

		if (fBeta <= 0.0f || fBeta >= 1.0f || fV1 == 0.0f)
			return (FALSE);

		fAlpha = (fV0 - fBeta * fV2) / fV1;
	}
	else
	{
		fDet = fV2 * fU1 - fV1 * fU2;

		if (fDet == 0.0f)
			return (FALSE);

		fBeta = (fV0 * fU1 - fV1 * fU0) / fDet;

		if (fBeta <= 0.0f || fBeta >= 1.0f)
			return (FALSE);

		fAlpha = (fU0 - fBeta * fU2) / fU1;
	}

	if (!nLoose)
	{
		if (fAlpha <= 0.0f)
			return (FALSE);

		fOut = (fAlpha + fBeta >= 1.0f);
	}
	else
	{
		if (fAlpha < A3D_TRACE_LOOSE_ALPHA_MIN)
			return (FALSE);

		fOut = (fAlpha + fBeta > A3D_TRACE_LOOSE_SUM_MAX);
	}

	return (!fOut);
}

/* =============================================================
// A3dTraceHitQuad()
// (RE) dbg:0x10012AA0; thunk dbg:0x10003CFB
//
// Test a segment or ray against a quad and store the plane distance.
// The original may reverse the primitive normal even on a miss.
//
// Returns:
//   TRUE   an intersection
//   FALSE  otherwise
// =============================================================*/

BOOL
A3dTraceHitQuad(const A3DVAL *pvFrom, const A3DVAL *pvTo, A3DPRIMITIVE *pBlock,
		int nLoose, int nInfinite)
{
A3DVAL fDenom, fT;
A3DVAL fPx, fPy, fPz;
A3DVAL fNx, fNy, fNz;
A3DVAL fU0, fV0, fU1, fV1, fU2, fV2;
A3DVAL fAlpha, fBeta, fDet;
int nDrop;
int fOut;

	fDenom = pvTo[0] * pBlock->vNormal[0] + pvTo[1] * pBlock->vNormal[1] +
		 pvTo[2] * pBlock->vNormal[2];

	if ((fDenom < 0.0f ? -fDenom : fDenom) < A3D_TRACE_DENOM_MIN)
		return (FALSE);

	fT = (pBlock->av[0][0] * pBlock->vNormal[0] +
	      pBlock->av[0][1] * pBlock->vNormal[1] +
	      pBlock->av[0][2] * pBlock->vNormal[2] -
	      (pvFrom[0] * pBlock->vNormal[0] + pvFrom[1] * pBlock->vNormal[1] +
	       pvFrom[2] * pBlock->vNormal[2])) / fDenom;

	if (fT < 0.0f)
	{
		pBlock->vNormal[0] = -pBlock->vNormal[0];
		pBlock->vNormal[1] = -pBlock->vNormal[1];
		pBlock->vNormal[2] = -pBlock->vNormal[2];

		fDenom = pvTo[0] * pBlock->vNormal[0] +
			 pvTo[1] * pBlock->vNormal[1] +
			 pvTo[2] * pBlock->vNormal[2];

		fT = (pBlock->av[0][0] * pBlock->vNormal[0] +
		      pBlock->av[0][1] * pBlock->vNormal[1] +
		      pBlock->av[0][2] * pBlock->vNormal[2] -
		      (pvFrom[0] * pBlock->vNormal[0] +
		       pvFrom[1] * pBlock->vNormal[1] +
		       pvFrom[2] * pBlock->vNormal[2])) / fDenom;
	}

	pBlock->fDistance = fT;

	if (fT < 0.0f)
		return (FALSE);

	if (!nInfinite && fT > 1.0f)
		return (FALSE);

	fPx = pvFrom[0] + fT * pvTo[0];
	fPy = pvFrom[1] + fT * pvTo[1];
	fPz = pvFrom[2] + fT * pvTo[2];

	fNx = pBlock->vNormal[0] < 0.0f ? -pBlock->vNormal[0] : pBlock->vNormal[0];
	fNy = pBlock->vNormal[1] < 0.0f ? -pBlock->vNormal[1] : pBlock->vNormal[1];
	fNz = pBlock->vNormal[2] < 0.0f ? -pBlock->vNormal[2] : pBlock->vNormal[2];

	if (fNx > fNy && fNx > fNz)
		nDrop = 0;
	else if (fNy > fNx && fNy > fNz)
		nDrop = 1;
	else
		nDrop = 2;

	if (nDrop == 0)
	{
		fU0 = fPy - pBlock->av[0][1];
		fV0 = fPz - pBlock->av[0][2];
		fU1 = pBlock->av[1][1] - pBlock->av[0][1];
		fV1 = pBlock->av[1][2] - pBlock->av[0][2];
		fU2 = pBlock->av[2][1] - pBlock->av[0][1];
		fV2 = pBlock->av[2][2] - pBlock->av[0][2];
	}
	else if (nDrop == 1)
	{
		fU0 = fPx - pBlock->av[0][0];
		fV0 = fPz - pBlock->av[0][2];
		fU1 = pBlock->av[1][0] - pBlock->av[0][0];
		fV1 = pBlock->av[1][2] - pBlock->av[0][2];
		fU2 = pBlock->av[2][0] - pBlock->av[0][0];
		fV2 = pBlock->av[2][2] - pBlock->av[0][2];
	}
	else
	{
		fU0 = fPx - pBlock->av[0][0];
		fV0 = fPy - pBlock->av[0][1];
		fU1 = pBlock->av[1][0] - pBlock->av[0][0];
		fV1 = pBlock->av[1][1] - pBlock->av[0][1];
		fU2 = pBlock->av[2][0] - pBlock->av[0][0];
		fV2 = pBlock->av[2][1] - pBlock->av[0][1];
	}

	fOut = FALSE;

	if (fU1 == 0.0f)
	{
		if (fU2 != 0.0f)
		{
			fBeta = fU0 / fU2;

			if (fBeta > 0.0f && fBeta < 1.0f && fV1 != 0.0f)
			{
				fAlpha = (fV0 - fBeta * fV2) / fV1;

				if (nLoose)
					fOut = (fAlpha >= A3D_TRACE_LOOSE_ALPHA_MIN &&
						fAlpha + fBeta <= A3D_TRACE_LOOSE_SUM_MAX);
				else
					fOut = (fAlpha > 0.0f &&
						fAlpha + fBeta < 1.0f);
			}
		}
	}
	else if (fV2 * fU1 - fU2 * fV1 != 0.0f)
	{
		fDet = fV2 * fU1 - fU2 * fV1;

		fBeta = (fV0 * fU1 - fU0 * fV1) / fDet;

		if (fBeta > 0.0f && fBeta < 1.0f)
		{
			fAlpha = (fU0 - fBeta * fU2) / fU1;

			if (nLoose)
				fOut = (fAlpha >= A3D_TRACE_LOOSE_ALPHA_MIN &&
					fAlpha + fBeta <= A3D_TRACE_LOOSE_SUM_MAX);
			else
				fOut = (fAlpha > 0.0f &&
					fAlpha + fBeta < 1.0f);
		}
	}

	if (fOut)
		return (TRUE);

	if (nDrop == 0)
	{
		fU0 = fPy - pBlock->av[0][1];
		fV0 = fPz - pBlock->av[0][2];
		fU1 = pBlock->av[2][1] - pBlock->av[0][1];
		fV1 = pBlock->av[2][2] - pBlock->av[0][2];
		fU2 = pBlock->av[3][1] - pBlock->av[0][1];
		fV2 = pBlock->av[3][2] - pBlock->av[0][2];
	}
	else if (nDrop == 1)
	{
		fU0 = fPx - pBlock->av[0][0];
		fV0 = fPz - pBlock->av[0][2];
		fU1 = pBlock->av[2][0] - pBlock->av[0][0];
		fV1 = pBlock->av[2][2] - pBlock->av[0][2];
		fU2 = pBlock->av[3][0] - pBlock->av[0][0];
		fV2 = pBlock->av[3][2] - pBlock->av[0][2];
	}
	else
	{
		fU0 = fPx - pBlock->av[0][0];
		fV0 = fPy - pBlock->av[0][1];
		fU1 = pBlock->av[2][0] - pBlock->av[0][0];
		fV1 = pBlock->av[2][1] - pBlock->av[0][1];
		fU2 = pBlock->av[3][0] - pBlock->av[0][0];
		fV2 = pBlock->av[3][1] - pBlock->av[0][1];
	}

	if (fU1 == 0.0f)
	{
		if (fU2 != 0.0f)
		{
			fBeta = fU0 / fU2;

			/* Original defect: fV2 guards division by fV1. */
			if (fBeta > 0.0f && fBeta < 1.0f && fV2 != 0.0f)
			{
				fAlpha = (fV0 - fBeta * fV2) / fV1;

				if (nLoose)
					return (fAlpha >= A3D_TRACE_LOOSE_ALPHA_MIN &&
						fAlpha + fBeta <= A3D_TRACE_LOOSE_SUM_MAX);

				return (fAlpha > 0.0f && fAlpha + fBeta < 1.0f);
			}
		}
	}
	else if (fV2 * fU1 - fU2 * fV1 != 0.0f)
	{
		fDet = fV2 * fU1 - fU2 * fV1;

		fBeta = (fV0 * fU1 - fU0 * fV1) / fDet;

		if (fBeta > 0.0f && fBeta < 1.0f)
		{
			fAlpha = (fU0 - fBeta * fU2) / fU1;

			if (nLoose)
				return (fAlpha >= A3D_TRACE_LOOSE_ALPHA_MIN &&
					fAlpha + fBeta <= A3D_TRACE_LOOSE_SUM_MAX);

			return (fAlpha > 0.0f && fAlpha + fBeta < 1.0f);
		}
	}

	return (fOut);
}

/* =============================================================
// A3dSourceOcclude()
//
// Evaluate geometry reflections. The trailing true/soft vector argument mapping
// is not established from 677.
// =============================================================*/

void
A3dSourceOcclude(CA3dRoot *pApi, CA3dSource *pSource)
{
CA3dStdList<CA3dPool *>::iterator it;
CA3dStdList<CA3dPool *>::iterator itEnd;
CA3dPool       *pPool;
A3DELEMENT     *pPrim;
A3DPRIMITIVE   *pBlock;
A3DVAL          avTrue[3];
A3DVAL          avSoft[3];
A3DVAL          avBoth[6];
A3DVAL fDist, fFar;
A3DVAL  fHit;
DWORD   dwMode;
int     i;

	it    = pApi->m_GeomPools.begin();
	itEnd = pApi->m_GeomPools.end();

	pApi->m_cWalkDone = 0;

	if (it == itEnd)
		return;

	pPool = *it;
	++it;
	pPool->Rewind();

	for (;;)
	{
		pPrim = pPool->NextElement();

		if (!pPrim)
		{
			if (it == itEnd)
				break;

			pPool = *it;
			++it;
			pPool->Rewind();

			pPrim = pPool->NextElement();

			if (!pPrim)
				break;
		}

		dwMode = pPrim->dwMode;

		if (dwMode == A3D_MATERIAL)
		{
			CopyMemory(pApi->m_afWalkMaterial, &pPrim->prim,
				   sizeof(pApi->m_afWalkMaterial));

			continue;
		}

		if (dwMode != A3D_TRIANGLES && dwMode != A3D_QUADS)
			goto next;

		pBlock = &pPrim->prim;

		if (dwMode == A3D_TRIANGLES)
		{
			fDist = (pSource->m_avReflectionPoint[0] - pBlock->av[0][0]) * pBlock->vNormal[0] +
				(pSource->m_avReflectionPoint[1] - pBlock->av[0][1]) * pBlock->vNormal[1] +
				(pSource->m_avReflectionPoint[2] - pBlock->av[0][2]) * pBlock->vNormal[2];

			for (i = 0; i < 3; i++)
			{
				avTrue[i] = pSource->m_avReflectionPoint[i] -
					    (fDist + fDist) * pBlock->vNormal[i];

				avSoft[i] = pSource->m_avReflectionPoint[i] -
					    (pApi->m_fReflectScale * fDist) *
					    pBlock->vNormal[i];
			}
		}
		else
		{
			fDist = A3dMirrorPoint(pApi, &pSource->m_avReflectionPoint[0], pBlock->vNormal,
					       pBlock->av[0],
					       avTrue, avSoft);
		}

		for (i = 0; i < 3; i++)
		{
			avBoth[i]     = avTrue[i] - pApi->m_vListenerPos[i];
			avBoth[i + 3] = avSoft[i] - pApi->m_vListenerPos[i];
		}

		fHit = 0.0f;

		if (pBlock->dwFlags & A3D_PRIM_INFINITE_PLANE)
		{

			fFar = (pApi->m_vListenerPos[0] - pBlock->av[0][0]) *
			       pBlock->vNormal[0] +
			       (pApi->m_vListenerPos[1] - pBlock->av[0][1]) *
			       pBlock->vNormal[1] +
			       (pApi->m_vListenerPos[2] - pBlock->av[0][2]) *
			       pBlock->vNormal[2];

			if (fDist * fFar > 0.0f)
				fHit = 1.0f;
		}
		else if (dwMode == A3D_TRIANGLES)
		{
			if (A3dTraceHitTri(pApi->m_vListenerPos, &avBoth[3],
					   pBlock, 1, 0))
			{
				fHit = 1.0f;
			}
		}
		else
		{
			if (A3dTraceHitQuad(pApi->m_vListenerPos, &avBoth[3],
					    pBlock, 1, 0))
			{
				fHit = 1.0f;
			}
		}

		if (fHit > 0.0f)
		{
			A3dReflection(pSource, pBlock->dwTag, pBlock->dwTag, 1,
				      avBoth, fHit, pApi->m_afWalkMaterial,
				      avBoth, &avBoth[3]);
		}

	next:
		if ((int) ++pApi->m_cWalkDone > (int) pApi->m_cWalkMax)
			break;
	}
}

/* =============================================================
// ReflectStep()
// (RE) dbg:0x100135B0; thunk dbg:0x100011AE
//
// Mirror the current primitive and record an intersecting reflection.
// =============================================================*/

void
CA3dRoot::ReflectStep(BOOL (*pfnHit)(const A3DVAL *, const A3DVAL *,
				     A3DPRIMITIVE *, int, int))
{
A3DPRIMITIVE   *pBlock;
A3DVAL          avTrue[3];
A3DVAL          avSoft[3];
A3DVAL          avTrueRel[3];
A3DVAL          avSoftRel[3];
A3DVAL          fDist;
A3DVAL          fFar;
A3DVAL          fHit;
A3DVAL          fDistance;
A3DVAL fMean, fEq, fGain;
DWORD   adwMaterialPrefix[2];
int     i;

	pBlock = &((A3DELEMENT *) m_pElement)->prim;

	fDist = A3dMirrorPoint(this, m_pTraceSource->m_avReflectionPoint,
			       pBlock->vNormal, pBlock->av[0], avTrue, avSoft);

	for (i = 0; i < 3; i++)
	{
		avTrueRel[i] = avTrue[i] - m_vListenerPos[i];
		avSoftRel[i] = avSoft[i] - m_vListenerPos[i];
	}

	if (pBlock->dwFlags & A3D_PRIM_INFINITE_PLANE)
	{

		fFar = (m_vListenerPos[0] - pBlock->av[0][0]) * pBlock->vNormal[0] +
		       (m_vListenerPos[1] - pBlock->av[0][1]) * pBlock->vNormal[1] +
		       (m_vListenerPos[2] - pBlock->av[0][2]) * pBlock->vNormal[2];

		fHit = (fDist * fFar > 0.0f) ? 1.0f : 0.0f;
	}
	else if (pfnHit(m_vListenerPos, avSoftRel, pBlock, 1, 0))
	{
		fHit = 1.0f;
	}
	else
	{
		fHit = 0.0f;
	}

	if (fHit > 0.0f)
	{
		CopyMemory(adwMaterialPrefix, m_afWalkMaterial, sizeof(adwMaterialPrefix));

		fMean = 1.0f;
		fEq   = 1.0f;
		fGain = 1.0f;

		fDistance = A3dFastSqrt(avTrueRel[0] * avTrueRel[0] +
					avTrueRel[1] * avTrueRel[1] +
					avTrueRel[2] * avTrueRel[2]);

		m_pTraceSource->ReflectionGains(fHit, fDistance, m_afWalkMaterial,
						&fMean, &fEq, &fGain);

		m_pReflectBin->Add(pBlock->dwTag, fDistance, fMean, fEq, fGain,
				      avTrueRel, fHit, adwMaterialPrefix, A3D_REF_DELAY_FROM_DISTANCE);
	}
}

/* =============================================================
// ReflectWalk()
// (RE) dbg:0x10011FB0; thunk dbg:0x100034EF
//
// Walk geometry for reflecting surfaces within the traversal budget.
// =============================================================*/

void
CA3dRoot::ReflectWalk(void)
{
A3DELEMENT     *pElement;
DWORD           dwMode;

	++m_dwWalkSeq;

	m_cGeomPoolsLeft = m_GeomPools.size();
	m_itGeomPool     = m_GeomPools.begin();
	m_pPool          = *m_itGeomPool;
	m_pPool->Rewind();
	--m_cGeomPoolsLeft;

	m_cWalkDone = 0;

	for (;;)
	{
		NextWalkElement();

		if (!m_pElement)
			break;

		pElement = (A3DELEMENT *) m_pElement;
		dwMode   = pElement->dwMode;

		if (dwMode == A3D_TRIANGLES)
		{

			if (pElement->prim.dwFlags & A3D_PRIM_NO_REFLECT)
				continue;

			ReflectStep(A3dTraceHitTri);
		}
		else if (dwMode == A3D_QUADS)
		{
			if (pElement->prim.dwFlags & A3D_PRIM_NO_REFLECT)
				continue;

			ReflectStep(A3dTraceHitQuad);
		}
		else if (dwMode == A3D_MATERIAL)
		{
			CopyMemory(m_afWalkMaterial, &pElement->prim,
				   sizeof(m_afWalkMaterial));

			continue;
		}

		if ((int) ++m_cWalkDone > (int) m_cWalkMax)
			break;
	}
}

/* =============================================================
// NextWalkElement()
// (RE) dbg:0x100121E0; thunk dbg:0x10002B85
//
// Read the next walk element into m_pElement, skipping already covered lists.
// =============================================================*/

void
CA3dRoot::NextWalkElement(void)
{
CA3dList *pList;

	m_pElement = m_pPool->NextElement();

	if (m_pElement)
		return;

	pList = (CA3dList *) m_pPool->m_dwListOwner;

	if (pList)
		pList->m_dwRetestedSeq = 0;

	while (m_cGeomPoolsLeft-- > 0)
	{
		++m_itGeomPool;
		m_pPool = *m_itGeomPool;

		pList = (CA3dList *) m_pPool->m_dwListOwner;

		if (!pList || pList->m_dwRetestedSeq < m_dwWalkSeq)
		{
			m_pPool->Rewind();
			m_pElement = m_pPool->NextElement();

			return;
		}
	}
}

/* =============================================================
// SetRenderMode()
// (RE) rtl:0x10007A30; dbg:0x10014900
//
// Set the geometry render mode.
//
// Returns:
//   S_OK
//   E_INVALIDARG  unsupported bits on IA3d5 or later
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetRenderMode(DWORD dwMode)
{
	if (m_dwInterfaceVersion > 4 &&
	    (dwMode & ~(A3D_1ST_REFLECTIONS | A3D_OCCLUSIONS)))
	{
		DBGSTR("CA3dRoot::SetRenderMode() - Invalid argument.\n");

		return (E_INVALIDARG);
	}

	m_dwRenderMode = dwMode;

	return (S_OK);
}

/* =============================================================
// GetRenderMode()
// (RE) rtl:0x10007A60; dbg:0x10014970
//
// Read the geometry render mode.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetRenderMode(LPDWORD pdwMode)
{
	if (!pdwMode)
		return (E_POINTER);

	*pdwMode = m_dwRenderMode;

	return (S_OK);
}

/* =============================================================
// SetPolygonBloatFactor()
// (RE) rtl:0x10007AD0; dbg:0x10014A60
//
// Set the polygon bloat factor through the reflection scale.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a negative factor
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetPolygonBloatFactor(A3DVAL fBloat)
{
	if (fBloat < 0.0f)
		return (E_INVALIDARG);

	if (fBloat == 0.0f)
		m_fReflectScale = A3D_BLOAT_NONE;
	else if (fBloat <= 1.0f)
		m_fReflectScale = 2.0f / fBloat;
	else
		m_fReflectScale = 1.0f / fBloat + 1.0f;

	return (S_OK);
}

/* =============================================================
// GetPolygonBloatFactor()
// (RE) rtl:0x10007B60; dbg:0x10014B10
//
// Read the polygon bloat factor.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetPolygonBloatFactor(LPA3DVAL pfBloat)
{
	if (!pfBloat)
		return (E_POINTER);

	/* Original defect: the inverse below overwrites the zero sentinel result. */
	if (m_fReflectScale == A3D_BLOAT_NONE)
		*pfBloat = 0.0f;

	if (m_fReflectScale <= 2.0f)
		*pfBloat = 1.0f / (m_fReflectScale - 1.0f);
	else
		*pfBloat = 2.0f / m_fReflectScale;

	return (S_OK);
}

/* =============================================================
// SetReflectionUpdateInterval()
// (RE) rtl:0x10007BD0; dbg:0x10014BC0
//
// Set the reflection update interval in frames.
//
// Returns:
//   S_OK
//   E_INVALIDARG  zero
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetReflectionUpdateInterval(DWORD dwInterval)
{
	if (!dwInterval)
		return (E_INVALIDARG);

	m_dwReflectionUpdateInterval = dwInterval;

	return (S_OK);
}

/* =============================================================
// GetReflectionUpdateInterval()
// (RE) rtl:0x10007C00; dbg:0x10014C00
//
// Read the reflection update interval in frames.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetReflectionUpdateInterval(LPDWORD pdwInterval)
{
	if (!pdwInterval)
		return (E_POINTER);

	*pdwInterval = m_dwReflectionUpdateInterval;

	return (S_OK);
}

/* =============================================================
// SetOcclusionUpdateInterval()
// (RE) rtl:0x10007C30; dbg:0x10014C40
//
// Set the occlusion update interval in frames.
//
// Returns:
//   S_OK
//   E_INVALIDARG  zero
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetOcclusionUpdateInterval(DWORD dwInterval)
{
	if (!dwInterval)
		return (E_INVALIDARG);

	m_dwOcclusionUpdateInterval = dwInterval;

	return (S_OK);
}

/* =============================================================
// GetOcclusionUpdateInterval()
// (RE) rtl:0x10007C60; dbg:0x10014C80
//
// Read the occlusion update interval in frames.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dRoot::GetOcclusionUpdateInterval(LPDWORD pdwInterval)
{
	if (!pdwInterval)
		return (E_POINTER);

	*pdwInterval = m_dwOcclusionUpdateInterval;

	return (S_OK);
}

/* =============================================================
// ReflectSurfaces()
// (RE) dbg:0x10014CC0; thunk dbg:0x10002DA1
//
// Record the source's manual reflections in the current reflection bin.
// =============================================================*/

void
CA3dRoot::ReflectSurfaces(void)
{
CA3dStdList<CA3dReflection *> *pList;
CA3dStdList<CA3dReflection *>::iterator it;
CA3dStdList<CA3dReflection *>::iterator itEnd;
CA3dReflection *pRefl;
A3DVAL          vPosition[3];
A3DVAL          vRel[3];
A3DVAL          fDistance;
DWORD           adwMaterialPrefix[2];

	pList = m_pTraceSource->m_pManualReflections;

	if (!pList)
		return;

	for (it = pList->begin(), itEnd = pList->end(); it != itEnd; ++it)
	{
		pRefl = *it;

		if (pRefl->m_dwTransformMode == A3DREFLECTION_TRANSFORMMODE_HEADRELATIVE)
			A3dTransformPoint(pRefl->m_vPosition, m_matListenerXform,
					  vPosition);
		else
			pRefl->GetPosition3fv(vPosition);

		vRel[0] = vPosition[0] - m_vListenerPos[0];
		vRel[1] = vPosition[1] - m_vListenerPos[1];
		vRel[2] = vPosition[2] - m_vListenerPos[2];

		fDistance = A3dFastSqrt(vRel[0] * vRel[0] + vRel[1] * vRel[1] +
					vRel[2] * vRel[2]);

		CopyMemory(adwMaterialPrefix, m_afWalkMaterial, sizeof(adwMaterialPrefix));

		m_pReflectBin->Add(pRefl->m_dwId, fDistance,
				      pRefl->m_fGainScale, pRefl->m_fGainScale,
				      pRefl->m_fGainScale, vRel, 1.0f, adwMaterialPrefix,
				      pRefl->m_fDelay);
	}
}

/* =============================================================
// AddGeom()
// (RE) dbg:0x100144C0; thunk dbg:0x10001960
//
// Append a pool to the geometry list.
// =============================================================*/

void
CA3dRoot::AddGeom(CA3dPool *pPool)
{
	m_GeomPools.push_back(pPool);
}

/* =============================================================
// FindGeom()
// (RE) dbg:0x10018C70
//
// Find a pool in the scratch list. The reference is a __cdecl free function
// taking the list by value; this member makes the copy internally.
//
// Returns: The one-based pool index; 0 if absent.
// =============================================================*/

int
CA3dRoot::FindGeom(const CA3dPool *pcPool)
{
CA3dStdList<CA3dPool *>			list = m_ScratchPools;
CA3dStdList<CA3dPool *>::iterator	it;
int					i;

	i = 0;

	for (it = list.begin(); it != list.end(); ++it)
	{
		i++;

		if (*it == pcPool)
			return (i);
	}

	return (0);
}

/* =============================================================
// A3dListAdd()
//
// Append an object to the list. The count increases even if allocation fails.
//
// Returns: The new node; NULL on allocation failure.
// =============================================================*/

A3DLISTNODE *
A3dListAdd(A3DLIST *pList, void *pObject)
{
A3DLISTNODE *pTail;
A3DLISTNODE *p;
A3DLISTNODE *pNode;

	if (!pList->cNodes)
	{
		pNode = (A3DLISTNODE *) new BYTE[sizeof(A3DLISTNODE)];

		if (pNode)
		{
			pNode->pPrev   = NULL;
			pNode->pNext   = NULL;
			pNode->pObject = pObject;

			pList->pHead = pNode;
			pList->pCurr = pNode;
		}
		else
		{
			pList->pHead = NULL;
			pList->pCurr = NULL;
		}

		pList->cNodes++;

		return (pNode);
	}

	pTail = pList->pHead;

	for (p = pTail->pNext; p; p = p->pNext)
		pTail = p;

	pList->pTail = pTail;

	if (pTail->pNext)
	{
		p = pTail->pNext;

		while (p->pNext)
			p = p->pNext;

		pNode = A3dListAppend(p, pObject);

		pList->pTail = pNode;
		pList->pCurr = pNode;
	}
	else
	{
		pNode = (A3DLISTNODE *) new BYTE[sizeof(A3DLISTNODE)];

		if (pNode)
		{
			pNode->pPrev   = pTail;
			pNode->pNext   = NULL;
			pNode->pObject = pObject;

			pTail->pNext = pNode;
		}
		else
		{
			pTail->pNext = NULL;
		}

		pList->pTail = pNode;
		pList->pCurr = pNode;
	}

	pList->cNodes++;

	return (pNode);
}

/* =============================================================
// SetOpeningFactorf()
// (RE) rtl:0x10007780; dbg:0x100144F0
//
// Stage a scalar opening factor for subsequent primitives, replacing the pointer.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetOpeningFactorf(A3DVAL fFactor)
{
	m_fOpening  = fFactor;
	m_pfOpening = NULL;

	return (S_OK);
}

/* =============================================================
// SetOpeningFactorfv()
// (RE) rtl:0x100077A0; dbg:0x10014530
//
// Stage a borrowed opening-factor pointer for subsequent primitives.
//
// Returns:
//   S_OK
//   E_POINTER  a null pointer
// =============================================================*/

STDMETHODIMP
CA3dRoot::SetOpeningFactorfv(LPA3DVAL pv)
{
	if (!pv)
		return (E_POINTER);

	m_pfOpening = pv;

	return (S_OK);
}

/* =============================================================
// NewList()
// (RE) rtl:0x100077D0; dbg:0x10014570
//
// Create a display list associated with this API object.
//
// Returns:
//   S_OK
//   A3DERROR_MEMORY_ALLOCATION  on allocation failure
// =============================================================*/

STDMETHODIMP
CA3dRoot::NewList(LPA3DLIST *ppList)
{
CA3dList *pList;

	pList = new CA3dList(this);

	if (!pList)
		return (A3DERROR_MEMORY_ALLOCATION);

	pList->AddRef();

	*ppList = pList;

	return (S_OK);
}

/* =============================================================
// A3dMirrorPoint()
// (RE) dbg:0x10013880; thunk dbg:0x10004340
//
// Compute true and scaled mirror points across a plane.
//
// Returns: The signed distance from the input point to the plane.
// =============================================================*/

A3DVAL
A3dMirrorPoint(CA3dRoot *pApi, const A3DVAL *pv, const A3DVAL *pvNormal,
	       const A3DVAL *pvOn, A3DVAL *pvTrue, A3DVAL *pvSoft)
{
A3DVAL  fDist;
A3DVAL  fTwice;
A3DVAL  fScaled;
int     i;

	fDist = (pv[0] - pvOn[0]) * pvNormal[0] +
		(pv[1] - pvOn[1]) * pvNormal[1] +
		(pv[2] - pvOn[2]) * pvNormal[2];

	fTwice  = fDist + fDist;
	fScaled = fDist * pApi->m_fReflectScale;

	for (i = 0; i < 3; i++)
	{
		pvTrue[i] = pv[i] - fTwice * pvNormal[i];
		pvSoft[i] = pv[i] - fScaled * pvNormal[i];
	}

	return (fDist);
}

/* =============================================================
// EnableGeometricReverb()
// (RE) rtl:0x100073B0; dbg:0x10015140
//
// Invalidate the legacy reverb preset for geometric estimation.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
CA3dRoot::EnableGeometricReverb(void)
{
	m_dwLegacyReverbPreset = (DWORD) -1;

	return (S_OK);
}

/* =============================================================
// MeasureRoom()
// (RE) dbg:0x10015180; thunk dbg:0x10001CF3
//
// Estimate two lateral room dimensions and one along the measurement axis.
//
// Returns: pfMeasure, with -1 for unavailable dimensions.
// =============================================================*/

A3DVAL *
CA3dRoot::MeasureRoom(A3DVAL *pfMeasure)
{
A3DELEMENT     *pElement;
A3DPRIMITIVE   *pShape;
A3DVAL          afQuadRange[8];
A3DVAL          afQuadDist[4];
A3DVAL          afEndNear[2];
A3DVAL          afLateral[2];
A3DVAL          avCentroid[3];
A3DVAL          avToCentroid[3];
A3DVAL          avPolar[3];
A3DVAL fRangeMin, fRangeMax;
A3DVAL fIncidence, fDist, fDot;
A3DVAL fMin, fMax;
A3DVAL fFar, fNear, fAlong;
int nAxis;
int i;

	nAxis = (int) m_nMeasureAxis;

	fRangeMin =  FLT_MAX;
	fRangeMax = -FLT_MAX;

	for (i = 0; i < 2; i++)
	{
		afQuadDist[i]     = -1.0f;
		afQuadDist[i + 2] = -1.0f;
	}

	afEndNear[0] = -1.0f;
	afEndNear[1] = -1.0f;

	pfMeasure[0] = -1.0f;
	pfMeasure[1] = -1.0f;
	pfMeasure[2] = -1.0f;

	++m_dwWalkSeq;

	m_cGeomPoolsLeft = m_GeomPools.size();
	m_itGeomPool     = m_GeomPools.begin();
	m_pPool          = *m_itGeomPool;
	m_pPool->Rewind();
	--m_cGeomPoolsLeft;

	m_cWalkDone = 0;

	for (;;)
	{
		NextWalkElement();

		if (!m_pElement)
			break;

		pElement = (A3DELEMENT *) m_pElement;

		pShape = NULL;

		if (pElement->dwMode == A3D_TRIANGLES)
		{
			pShape = &pElement->prim;

			avCentroid[0] = (pShape->av[0][0] + pShape->av[1][0] +
					 pShape->av[2][0]) * 0.33333f;
			avCentroid[1] = (pShape->av[0][1] + pShape->av[1][1] +
					 pShape->av[2][1]) * 0.33333f;
			avCentroid[2] = (pShape->av[0][2] + pShape->av[1][2] +
					 pShape->av[2][2]) * 0.33333f;
		}
		else if (pElement->dwMode == A3D_QUADS)
		{
			pShape = &pElement->prim;

			avCentroid[0] = (pShape->av[0][0] + pShape->av[1][0] +
					 pShape->av[2][0] + pShape->av[3][0]) * 0.25f;
			avCentroid[1] = (pShape->av[0][1] + pShape->av[1][1] +
					 pShape->av[2][1] + pShape->av[3][1]) * 0.25f;
			avCentroid[2] = (pShape->av[0][2] + pShape->av[1][2] +
					 pShape->av[2][2] + pShape->av[3][2]) * 0.25f;
		}

		if (!pShape)
			continue;

		avToCentroid[0] = avCentroid[0] - m_vListenerPos[0];
		avToCentroid[1] = avCentroid[1] - m_vListenerPos[1];
		avToCentroid[2] = avCentroid[2] - m_vListenerPos[2];

		A3dToPolar(avToCentroid, avPolar);

		fDist = avPolar[2];

		fDot = avToCentroid[0] * pShape->vNormal[0] +
		       avToCentroid[1] * pShape->vNormal[1] +
		       avToCentroid[2] * pShape->vNormal[2];

		/* Original defect: a centroid at the listener divides by zero. */
		fIncidence = (A3DVAL) fabs(fDot) / fDist;

		if ((A3DVAL) fabs(pShape->vNormal[nAxis]) <= A3D_MEASURE_END_NORMAL_THRESHOLD)
		{

			if (fIncidence > A3D_MEASURE_LATERAL_INCIDENCE_MIN)
			{
				A3DVAL  fAzimuth;
				int     nQuad;

				fAzimuth = avPolar[0] + 3.1415927f;

				nQuad = (int) (fAzimuth / 3.1415927f +
					       fAzimuth / 3.1415927f);

				if (nQuad == 4)
					nQuad = 3;

				if (afQuadDist[nQuad] < 0.0f ||
				    afQuadDist[nQuad] > fDist)
				{
					afQuadDist[nQuad] = fDist;

					A3dPrimAxisRange(pShape,
							 &afQuadRange[2 * nQuad],
							 nAxis);
				}
			}
		}
		else
		{

			int nSide;

			nSide = (avCentroid[nAxis] <= m_vListenerPos[nAxis]) ? 1 : 0;

			if (afEndNear[nSide] < 0.0f || afEndNear[nSide] > fDist)
				afEndNear[nSide] = fDist;
		}
	}

	for (i = 0; i < 2; i++)
	{
		if (afQuadDist[i] < 0.0f || afQuadDist[i + 2] < 0.0f)
		{
			afLateral[i] = -1.0f;
		}
		else
		{
			afLateral[i] = (afQuadDist[i] + afQuadDist[i + 2]) * 0.5f;

			fMin = (afQuadRange[2 * i + 4] <= afQuadRange[2 * i])
				? afQuadRange[2 * i + 4]
				: afQuadRange[2 * i];

			if (fMin < fRangeMin)
				fRangeMin = fMin;

			fMax = (afQuadRange[2 * i + 5] >= afQuadRange[2 * i + 1])
				? afQuadRange[2 * i + 5]
				: afQuadRange[2 * i + 1];

			if (fMax > fRangeMax)
				fRangeMax = fMax;
		}
	}

	fFar  = afEndNear[0] + m_vListenerPos[nAxis];
	fNear = m_vListenerPos[nAxis] - afEndNear[1];

	if (afEndNear[0] < 0.0f || afEndNear[1] <= 0.0f ||
	    fFar < fRangeMax || fNear > fRangeMin)
		fAlong = -1.0f;
	else
		fAlong = (afEndNear[0] + afEndNear[1]) * 0.5f;

	pfMeasure[0] = afLateral[0];
	pfMeasure[1] = afLateral[1];
	pfMeasure[2] = fAlong;

	return (pfMeasure);
}
