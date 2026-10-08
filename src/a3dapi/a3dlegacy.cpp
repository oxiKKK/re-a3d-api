/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * a3dlegacy.cpp
 *
 * Implements the legacy tracing path on CA3dRoot for older A3D interface
 * versions. It updates source spatial controls and estimates room reverb
 * from submitted geometry.
 *
 * The room estimate uses primitive extents to derive the reverb
 * selection. TraceLegacy is selected by the core Flush operation for
 * older interfaces and omits the reflection traversal used by Trace in
 * A3dGeom.cpp. Both paths use the source and matrix helpers shared by the
 * main engine.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "A3d3.h"
#include "A3dMatrix.h"
#include "A3dSource.h"
#include "refaudbin.h"
#include "Corners.h"

#include <float.h>

/* =============================================================
// Class: A3DREVERBRANGE
//
// Description: Reverb threshold, EAX preset and Debug display name.
//
// Size: 0x0C
// =============================================================*/

struct A3DREVERBRANGE
{
	/* 0x00 */ A3DVAL		fThreshold;	/* Upper measure bound. */
	/* 0x04 */ int		nPreset;	/* EAX environment number. */
	/* 0x08 */ const char	*pszName;	/* Debug display name. */
};

static const A3DREVERBRANGE	s_aReverbByVolume[4] =	/* dbg:0x10146E58 */
{
	{ 0.3f,		A3DREVERB_PRESET_HALLWAY,	"A3DREVERB_PRESET_HALLWAY" },
	{ 1.0f,		 A3DREVERB_PRESET_ROOM,	"A3DREVERB_PRESET_ROOM" },
	{ 2.0f,		 A3DREVERB_PRESET_AUDITORIUM,	"A3DREVERB_PRESET_AUDITORIUM" },
	{ FLT_MAX,	A3DREVERB_PRESET_HANGAR,	"A3DREVERB_PRESET_HANGAR" },
};

static const A3DREVERBRANGE	s_aReverbByArea[4] =	/* dbg:0x10146E88 */
{
	{ 0.3f,		A3DREVERB_PRESET_ALLEY,	"A3DREVERB_PRESET_ALLEY" },
	{ 1.0f,		 A3DREVERB_PRESET_ROOM,	"A3DREVERB_PRESET_ROOM" },
	{ 2.0f,		 A3DREVERB_PRESET_GENERIC,	"A3DREVERB_PRESET_GENERIC" },
	{ FLT_MAX,	A3DREVERB_PRESET_PLAIN,	"A3DREVERB_PRESET_PLAIN" },
};

#define A3D_REVERB_HISTORY	20

static A3DVAL	s_afAreaHistory[A3D_REVERB_HISTORY];	/* dbg:0x101529B4 */
static A3DVAL	s_afVolumeHistory[A3D_REVERB_HISTORY];	/* dbg:0x10152964 */
static int	s_nAreaHistory = -1;			/* dbg:0x10146EB8 */
static int	s_nVolumeHistory = -1;			/* dbg:0x10146EBC */

/* =============================================================
// A3dPrimAxisRange()
// (RE) dbg:0x10015980
//
// Write the minimum and maximum corner coordinates for one axis.
// =============================================================*/

void
A3dPrimAxisRange(const A3DPRIMITIVE *pcPrim, A3DVAL *pfRange, int nAxis)
{
A3DVAL	fMin;
A3DVAL	fMax;
int	i;

	fMin =  FLT_MAX;
	fMax = -FLT_MAX;

	for (i = 0; i < pcPrim->cVertices; i++)
	{
		if (pcPrim->av[i][nAxis] < fMin)
			fMin = pcPrim->av[i][nAxis];

		if (pcPrim->av[i][nAxis] > fMax)
			fMax = pcPrim->av[i][nAxis];
	}

	pfRange[0] = fMin;
	pfRange[1] = fMax;
}

/* =============================================================
// EstimateReverb()
// (RE) rtl:0x100083D0; dbg:0x10015A70; thunk dbg:0x10001645
//
// Select an EAX preset from averaged room measures. Original defect: empty
// histories cause division by zero. Retail mapping is unresolved;
// funcmap.tsv pairs this with an unrelated method.
// =============================================================*/

void
CA3dRoot::EstimateReverb(A3DVAL *pfMeasure)
{
const A3DREVERBRANGE   *pRange;
A3DVAL                  fScale;
A3DVAL                  fArea;
A3DVAL                  fVolume;
A3DVAL                  fAreaMean;
A3DVAL                  fVolumeMean;
A3DVAL                  fMeasure;
A3DVAL                  fSwap;
int                     nAreaCount;
int                     nVolumeCount;
int                     nPreset;
const char             *pszName;
int                     i;
HRESULT                 hr;

	if (s_nAreaHistory == -1 || s_nVolumeHistory == -1)
	{
		for (i = 0; i < A3D_REVERB_HISTORY; i++)
		{
			s_afVolumeHistory[i]    = -1.0f;
			s_afAreaHistory[i]      = -1.0f;
		}

		s_nAreaHistory          = 0;
		s_nVolumeHistory        = 0;
	}

	for (;;)
	{
		if (pfMeasure[0] < pfMeasure[1])
		{
			fSwap           = pfMeasure[1];
			pfMeasure[1]    = pfMeasure[0];
			pfMeasure[0]    = fSwap;
		}

		if (pfMeasure[1] >= pfMeasure[2])
			break;

		fSwap           = pfMeasure[2];
		pfMeasure[2]    = pfMeasure[1];
		pfMeasure[1]    = fSwap;
	}

	fScale = m_fGeomScaling * m_fEffectScaling;

	if (pfMeasure[2] >= 0.0f)
	{
		fArea   = pfMeasure[1] * pfMeasure[2] * fScale;
		fVolume = fArea * pfMeasure[0] * fScale;
	}
	else
	{
		fVolume = -1.0f;

		if (pfMeasure[1] >= 0.0f)
			fArea = pfMeasure[0] * pfMeasure[1] * fScale;
		else
			fArea = -1.0f;
	}

	s_nAreaHistory                          = (s_nAreaHistory + 1) % A3D_REVERB_HISTORY;
	s_nVolumeHistory                        = (s_nVolumeHistory + 1) % A3D_REVERB_HISTORY;
	s_afAreaHistory[s_nAreaHistory]         = fArea;
	s_afVolumeHistory[s_nVolumeHistory]     = fVolume;

	fAreaMean    = 0.0f;
	fVolumeMean  = 0.0f;
	nAreaCount   = 0;
	nVolumeCount = 0;

	for (i = 0; i < A3D_REVERB_HISTORY; i++)
	{
		if (s_afAreaHistory[i] > 0.0f)
		{
			fAreaMean += s_afAreaHistory[i];
			nAreaCount++;
		}

		if (s_afVolumeHistory[i] > 0.0f)
		{
			fVolumeMean += s_afVolumeHistory[i];
			nVolumeCount++;
		}
	}

	fAreaMean   = fAreaMean / (A3DVAL) nAreaCount;
	fVolumeMean = fVolumeMean / (A3DVAL) nVolumeCount;

	nPreset = A3DREVERB_PRESET_GENERIC;

	if (fArea == -1.0f)
	{
		nPreset = -1;
		pszName = "NO REVERB";
	}
	else
	{
		if (fVolume <= 0.0f)
		{
			fMeasure = fAreaMean;
			pRange   = s_aReverbByArea;
		}
		else
		{
			fMeasure = fVolumeMean;
			pRange   = s_aReverbByVolume;
		}

		for (i = 0; i < 4 && fMeasure > pRange[i].fThreshold; i++)
			;

		nPreset = pRange[i].nPreset;
		pszName = pRange[i].pszName;
	}

	if (m_dwLegacyReverbPreset != (DWORD) nPreset)
	{
		m_dwLegacyReverbPreset = nPreset;

		hr = m_pReverbPropSet->Set(DSPROPSETID_EAX_ReverbProperties,
					   DSPROPERTY_EAX_ENVIRONMENT,
					   NULL, 0,
					   &nPreset, sizeof(nPreset),
					   A3DPROPSET_WAITFORRESULTS);

		TRACE("%s\n", pszName);

		if (FAILED(hr))
			DBGSTR("Failed Set() call.\n");
	}
}

/* =============================================================
// TraceLegacy()
// (RE) rtl:0x100085F0; dbg:0x10016740
//
// Update source occlusion, reflections and direct paths for IA3d through IA3d3.
// Retail mapping is unresolved; funcmap.tsv pairs this with an unrelated
// method.
//
// Returns: The number of registered sources.
// =============================================================*/

int
CA3dRoot::TraceLegacy(void)
{
CA3dStdList<CA3dSource *>::iterator	it;
CA3dStdList<CA3dSource *>::iterator	itEnd;
CA3dSource	*pSource;
A3DVAL		matListenerLeft[16], matListenerRight[16];
A3DVAL		matLeftInv[16], matRightInv[16];
A3DVAL		matMid[16], matLeft[16], matRight[16];
A3DVAL		vListenerVel[4];
A3DVAL		vMidTrans[4], vLeftTrans[4], vRightTrans[4];
A3DVAL		vMidPolar[3], vLeftPolar[3], vRightPolar[3];
A3DVAL		vEars[4];
A3DVAL		fDistance;
int		cSources;
int		cReverb;
int		fThisPass;
int		fTight;
DWORD		dwNow;

	m_dwWalkSeq = 0;

	cSources = m_SourceArray.size();

	if (cSources < 1)
		return (cSources);

	/* Original unused reverb census. */

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

	A3dMatrixInvert(matListenerLeft, matLeftInv);
	A3dMatrixInvert(m_matListenerXform, m_matListenerXform2);
	A3dMatrixInvert(matListenerRight, matRightInv);

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

		ASSERT(pSource);

		if (!pSource->m_dwPlayState || A3dSourceTraceSkip(pSource))
			continue;

		m_pTraceSource = pSource;

		A3dSourceGetMatrix(pSource, (A3DVAL *) pSource->m_adwVolumeWorkingSet);

		A3dMatrixMultiply(m_matListenerXform2,
				  (const A3DVAL *) pSource->m_adwVolumeWorkingSet, matMid);

		if (m_fBinauralWalk)
		{
			A3dMatrixMultiply(matLeftInv,
					  (const A3DVAL *) pSource->m_adwVolumeWorkingSet,
					  matLeft);
			A3dMatrixMultiply(matRightInv,
					  (const A3DVAL *) pSource->m_adwVolumeWorkingSet,
					  matRight);
		}

		A3dMatrixGetTranslation((const A3DVAL *) pSource->m_adwVolumeWorkingSet,
					pSource->m_avReflectionPoint);
		A3dMatrixGetTranslation(matMid, vMidTrans);

		if (m_fBinauralWalk)
		{
			A3dMatrixGetTranslation(matLeft, vLeftTrans);
			A3dMatrixGetTranslation(matRight, vRightTrans);

			A3dToPolar(vLeftTrans, vLeftPolar);
			A3dToPolar(vRightTrans, vRightPolar);

			vMidPolar[0] = (vLeftPolar[0] + vRightPolar[0]) * 0.5f;
			vMidPolar[1] = (vLeftPolar[1] + vRightPolar[1]) * 0.5f;
			vMidPolar[2] = (vLeftPolar[2] + vRightPolar[2]) * 0.5f;
		}
		else
		{
			A3dToPolar(vMidTrans, vMidPolar);
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

		if (m_fBinauralWalk)
			A3dTraceSetSpread(pSource, vLeftPolar[2], vRightPolar[2]);
		else
			A3dTraceSetSpread(pSource, vMidPolar[2], vMidPolar[2]);

		if (fThisPass)
		{
			pSource->m_fTraced =
				(pSource->m_dwTraceCount % m_dwReflectionUpdateInterval) ? 0 : 1;

			pSource->m_dwTraceCount++;

			fTight = (pSource->m_dwTraceCount % m_dwOcclusionUpdateInterval) == 0;
		}
		else
		{
			pSource->m_fTraced      = 0;
			fTight                  = 0;
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

			ASSERT((m_fOccludeFactor >= 0.f) && (m_fOccludeFactor <= 1.f));
			ASSERT((m_fCurOcclude[0] >= 0.f) && (m_fCurOcclude[0] <= 1.f));
			ASSERT((m_fCurOcclude[1] >= 0.f) && (m_fCurOcclude[1] <= 1.f));

			A3dTraceApply(pSource, m_fOccludeFactor, m_fCurOcclude);

			if ((m_dwFeaturesEnabled & A3D_1ST_REFLECTIONS) &&
			    !m_dwDisableReflections &&
			    pSource->m_fTraced &&
			    (pSource->m_dwRenderMode & A3DSOURCE_RENDERMODE_1ST_REFLECTIONS))
			{
				CRefAudBin	bin;

				m_pReflectBin = &bin;
				ReflectWalk();
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

	return (cSources);
}
