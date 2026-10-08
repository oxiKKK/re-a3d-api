/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * a3ddsp.cpp
 *
 * Implements spatial control calculation for the A3D 1.x compatibility
 * library. The engine tracks listener and source geometry, applies
 * coordinate scaling, distance attenuation and source cones, and computes
 * ear direction, gain, delay and Doppler pitch.
 *
 * It keeps a process-wide voice table and prepares either spatial or flat
 * source solutions according to the selected 3D mode. The resulting
 * integer solution is converted to a floating-point DAL control bank and
 * submitted to the playback buffer.
 *
 * Listener.cpp updates the shared listener state, and A3dSource.cpp
 * updates individual voices and requests commits. This engine is separate
 * from the geometry tracing implementation in a3dapi.dll.
 *
 *---------------------------------------------------------------------------
 */

#include "../A3dMath.h"
#include "a3dprv.h"
#include "a3ddsp.h"
#include "A3dSource.h"

#include <math.h>
#include <stdio.h>

FLOAT	g_matListener[4][4];

double	g_dMaxDistance = HUGE_VAL;

BOOL	g_aSourceDirty[A3D_SOURCE_TABLE_CAPACITY];

A3DVOICE		*g_apVoice[A3D_SOURCE_TABLE_CAPACITY];
LONG			 g_cVoices;

A3DLISTENERSTATE	 g_ListenerState;
A3DLISTENERSTATE	 g_ListenerShadow;
A3DLISTENERSTATE	*g_pListenerState;
A3DLISTENERSTATE	*g_pListenerShadow;

FLOAT	g_fUnitsPerMetre;
FLOAT	g_fScale;
FLOAT	g_fScaleRecip;

LONG	g_lQ15Full	= A3D_Q15_FULL_GAIN;
DWORD	g_dwEngineFlags	= 1;
DWORD	g_dwSampleRateMode22050;
DWORD	g_dwSampleRateModeOther;
DWORD	g_fAxesD3D;
DWORD	g_dwCoordFlag;

DWORD	g_fRolloffZero;
double	g_dRolloffExponent = 1.0;

/* Gain encodings used by A3dSourceSetCone(); they are not inverse mappings. */

#define A3D_MILLIBEL		0.001
#define A3D_CODE_UNITY		128.0
#define A3D_CODE_DB		0.02	/* 0.4 dB a step */

/* Modelled half ear separation in metres; reason for this value is unknown. */

#define A3D_HEAD_HALF_WIDTH	0.0254

/* Preserve the original pi approximation; it determines HRTF sector boundaries. */

#define A3D_PI			3.14159
#define A3D_PI_2		1.570795


/* Hardware geometry conversion */
#define A3D_HW_RANGE_MAX_MM 65535.0
#define A3D_EAR_IMPULSE_TAPS 50

/* Rolloff mapping from DirectSound [1,10] to the internal [128,256] code. */

#define A3D_ROLLOFF_SLOPE	14.222229f	/* 128/9 */
/* Rolloff code and absorption conversion */
#define A3D_ROLLOFF_UNITY_CODE_F 128.0f
#define A3D_ROLLOFF_UNITY_CODE 128.0
#define A3D_ROLLOFF_EXPONENT_DIVISOR 256.0
#define A3D_HF_ABSORB_ROLLOFF_NUMERATOR 17.0f
#define A3D_MAX_DISTANCE	1.0e9f
#define A3D_MIN_DISTANCE_FLOOR	1.0e-4f

#define A3D_Q15_RECIP		(1.0f / 32767.0f)
#define A3D_PITCH_RECIP		(1.0f / 1024.0f)
#define A3D_ABSORB_SCALE	5.0e-4f
#define A3D_DELAY_RATE		256000.0f
#define A3D_DELAY_BIAS		5120.0f
/* Delay integer units per sample */
#define A3D_DELAY_UNITS_PER_SAMPLE_F 256.0f
#define A3D_DELAY_UNITS_PER_SAMPLE 256.0
#define A3D_MILLI		1.0e-3f
#define A3D_HALF		0.5f
/* Axis deadband for polar conversion. */

#define A3D_ANGLE_EPSILON	1.0e-4

/* The right ear's tail word carries this when the frame is a bypass. */

#define A3D_BYPASS		0x8001

/* DAL mode values.*/
#define A3D_DAL_MODE_SPATIAL 1
#define A3D_DAL_MODE_BYPASS 2

#define EAR_LEFT(p)		((p)->ear[0])
#define EAR_RIGHT(p)		((p)->ear[1])

/* HRTF grid: 12 azimuth sectors by 6 elevation rows, plus poles and flat entry. */

#define A3D_HRTF_RING		72
#define A3D_HRTF_ZENITH		72
#define A3D_HRTF_NADIR		73
#define A3D_HRTF_FLAT		74
#define A3D_HRTF_EAR_STRIDE	75

/* Grid indexing constants */
#define A3D_HRTF_AZIMUTH_STRIDE 6
#define A3D_HRTF_AZIMUTH_INDEX_BIAS 6
#define A3D_HRTF_ELEVATION_INDEX_BIAS 3
#define A3D_HRTF_NEXT_UPPER_ROW_OFFSET 5
#define A3D_HRTF_BOTTOM_ROW 5
#define A3D_HRTF_NEXT_BOTTOM_ROW 11

#define A3D_AZ_STEP		0.5235983	/* pi/6 = 30 degrees */
#define A3D_AZ_RECIP		1.90986093	/* 6/pi */
#define A3D_EL_STEP		0.314159	/* pi/10 = 18 degrees */
#define A3D_EL_RECIP		3.18310155	/* 10/pi */
#define A3D_EL_TOP		0.942477	/* 3pi/10 = 54 degrees */
#define A3D_EL_BOTTOM		(-0.628318)	/* -pi/5 = -36 degrees */
#define A3D_EL_CAP_RECIP	1.59155078	/* 5/pi */
#define A3D_EL_NADIR_RECIP	1.06103385	/* 10/(3pi) */

/* Squared near-field distance in metres. */

#define A3D_NEAR_FIELD_SQ	0.00064516f
#define A3D_NEAR_FIELD_GAIN	0.66666669f	/* two thirds */

/* Doppler speed in metres/second; pitch ratio is limited to one octave. */

#define A3D_SPEED_OF_SOUND	340.5
#define A3D_PITCH_MIN		0.5
#define A3D_PITCH_MAX		2.0
#define A3D_PITCH_SCALE		1024.0f	/* 10 fractional bits */

/* Four-term azimuth series used for interaural delay, in milliseconds. */

#define A3D_ITD_A0		0.5729
#define A3D_ITD_A2		0.0429
#define A3D_ITD_A3		0.0195
#define A3D_ITD_A4		0.0371

#define A3D_DELAY_NOMINAL	20.0f	/* samples */
#define A3D_DELAY_MAX		10240		/* 40 samples in 1/256ths */

#define A3D_VOL_RECIP		1.5259022e-05f	/* 1/65535 */

/* Bit 2 selects hardware geometry. */

#define A3D_FLAG_HW_GEOMETRY	4


/* =============================================================
// A3dFreeSourceSlot()
// (RE) a3d.dll rtl:0x10002760
//
// Clear the source active and object-table entries. The original __thiscall
// uses the active-table base in ecx.
// =============================================================*/

void
A3dFreeSourceSlot(LONG iSource)
{
	if (iSource < 0 || iSource >= g_cSources)
		return;

	g_apSourceActive[iSource] = 0;
	g_apSourceObject[iSource] = 0;
}

/* =============================================================
// A3dBuildDalBank()
// (RE) a3d.dll rtl:0x100037B0
//
// Convert an integer solution to DAL float parameters and write the bank size.
// =============================================================*/

void
A3dBuildDalBank(const A3DSOLUTION *pcSolution, A3DDALBANK *pBank,
		DWORD *pcbBank, FLOAT fKhz)
{
FLOAT	fDelayScale;

	fDelayScale = fKhz * A3D_DELAY_RATE;

	pBank->fReverbSend = EAR_LEFT(pcSolution).lReverbSendOrMode * A3D_Q15_RECIP;
	pBank->fPitch	= pcSolution->lPitch * A3D_PITCH_RECIP;

	pBank->fRange	= (EAR_RIGHT(pcSolution).lRange + EAR_LEFT(pcSolution).lRange)
			  * A3D_ABSORB_SCALE;

	EAR_LEFT(pBank).fDelay  = (pcSolution->lDelayLeft  - A3D_DELAY_BIAS) / fDelayScale;
	EAR_RIGHT(pBank).fDelay = (pcSolution->lDelayRight - A3D_DELAY_BIAS) / fDelayScale;

	EAR_LEFT(pBank).fAzimuth    = EAR_LEFT(pcSolution).lAzimuth    * A3D_MILLI;
	EAR_LEFT(pBank).fElevation  = EAR_LEFT(pcSolution).lElevation  * A3D_MILLI;
	EAR_RIGHT(pBank).fAzimuth   = EAR_RIGHT(pcSolution).lAzimuth   * A3D_MILLI;
	EAR_RIGHT(pBank).fElevation = EAR_RIGHT(pcSolution).lElevation * A3D_MILLI;

	EAR_LEFT(pBank).fGain  = EAR_LEFT(pcSolution).lGain  * A3D_Q15_RECIP;
	EAR_RIGHT(pBank).fGain = EAR_RIGHT(pcSolution).lGain * A3D_Q15_RECIP;

	pBank->fGainMean = (EAR_RIGHT(pBank).fGain + EAR_LEFT(pBank).fGain) * A3D_HALF;

	pBank->dwChannels = (EAR_RIGHT(pcSolution).lReverbSendOrMode == A3D_BYPASS)
		? A3D_DAL_MODE_BYPASS : A3D_DAL_MODE_SPATIAL;

	*pcbBank = sizeof(A3DDALBANK);
}

/* =============================================================
// A3dWrapF()
//
// Wrap a floating-point value to the half-open interval [dLow,dHigh).
//
// Returns: The wrapped value.
// =============================================================*/

double
A3dWrapF(double dValue, double dLow, double dHigh)
{
	while (dValue < dLow)
		dValue += dHigh - dLow;

	while (dValue >= dHigh)
		dValue += dLow - dHigh;

	return (dValue);
}

/* =============================================================
// A3dWrapI()
//
// Wrap an integer to the half-open interval [iLow,iHigh).
//
// Returns: The wrapped value.
// =============================================================*/

int
A3dWrapI(int iValue, int iLow, int iHigh)
{
	while (iValue < iLow)
		iValue += iHigh - iLow;

	while (iValue >= iHigh)
		iValue += iLow - iHigh;

	return (iValue);
}

/* =============================================================
// A3dSwizzle()
//
// Scale coordinates and convert DirectSound axes when the axes latch is set.
// =============================================================*/

static void
A3dSwizzle(double *pdOut, FLOAT x, FLOAT y, FLOAT z)
{
	if (g_fAxesD3D)
	{
		pdOut[0] = g_fScale *  z;
		pdOut[1] = g_fScale * -x;
		pdOut[2] = g_fScale *  y;
	}
	else
	{
		pdOut[0] = g_fScale * x;
		pdOut[1] = g_fScale * y;
		pdOut[2] = g_fScale * z;
	}
}

/* =============================================================
// A3dListenerSetTransform()
// (RE) a3d.dll rtl:0x100052F0
//
// Store listener vectors and derive orientation and ear positions.
// =============================================================*/

void
A3dListenerSetTransform(FLOAT px, FLOAT py, FLOAT pz,
			FLOAT fx, FLOAT fy, FLOAT fz,
			FLOAT tx, FLOAT ty, FLOAT tz,
			FLOAT vx, FLOAT vy, FLOAT vz)
{
A3DLISTENERSTATE	*pL;
FLOAT				 matTop[4][4];
FLOAT				 matOut[4][4];
double				 dYaw;
double				 dPitch;
int					 i;
int					 j;
int					 k;

	pL = g_pListenerState;

	pL->afRawPosition[0]	= px;
	pL->afRawPosition[1]	= py;
	pL->afRawPosition[2]	= pz;
	pL->afRawFront[0]		= fx;
	pL->afRawFront[1]		= fy;
	pL->afRawFront[2]		= fz;
	pL->afRawTop[0]			= tx;
	pL->afRawTop[1]			= ty;
	pL->afRawTop[2]			= tz;
	pL->afRawVelocity[0]	= vx;
	pL->afRawVelocity[1]	= vy;
	pL->afRawVelocity[2]	= vz;

	A3dSwizzle(&pL->dPosX,     px, py, pz);
	A3dSwizzle(&pL->adVelocity[0], vx, vy, vz);
	A3dSwizzle(&pL->adFront[0],    fx, fy, fz);
	A3dSwizzle(&pL->adTop[0],      tx, ty, tz);

	dYaw   = atan2(pL->adFront[1], pL->adFront[0]);
	dPitch = atan2(-pL->adFront[2],
		       sqrt(pL->adFront[0] * pL->adFront[0] +
			    pL->adFront[1] * pL->adFront[1]));

	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 4; j++)
			g_matListener[i][j] = (i == j) ? 1.0f : 0.0f;
	}

	A3dMatrixRotate('x', (FLOAT) (-dPitch * A3D_RADIANS_TO_DEGREES));
	A3dMatrixRotate('y', (FLOAT) ( dYaw   * A3D_RADIANS_TO_DEGREES));

	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 4; j++)
			matTop[i][j] = (i == j) ? 1.0f : 0.0f;
	}

	matTop[3][0] = (FLOAT) -pL->adTop[1];
	matTop[3][1] = (FLOAT)  pL->adTop[2];
	matTop[3][2] = (FLOAT)  pL->adTop[0];

	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 4; j++)
		{
			matOut[i][j] = 0.0f;

			for (k = 0; k < 4; k++)
				matOut[i][j] += matTop[i][k] * g_matListener[k][j];
		}
	}

	memcpy(g_matListener, matOut, sizeof(g_matListener));

	pL->dYaw   = dYaw;
	pL->dPitch = A3dWrapF(dPitch, -A3D_PI, A3D_PI);
	pL->dRoll  = atan2(g_matListener[3][0], g_matListener[3][1]);

	pL->dSinRoll	= sin(pL->dRoll);
	pL->dSinPitch	= sin(pL->dPitch);
	pL->dSinYaw		= sin(pL->dYaw);
	pL->dCosRoll	= cos(pL->dRoll);
	pL->dCosPitch	= cos(pL->dPitch);
	pL->dCosYaw		= cos(pL->dYaw);

	A3dComputeEarPositions(pL);
}

/* =============================================================
// A3dComputeEarPositions()
// (RE) a3d.dll rtl:0x10005800
//
// Compute both ear positions. Preserve the original shared z value
// instead of reversing the lateral contribution for the second ear.
// =============================================================*/

void
A3dComputeEarPositions(A3DLISTENERSTATE *pL)
{
double	dFwd;
double	dLat;
double	dUp;
double	dZ;

	dFwd = pL->dHeadForward;
	dLat = pL->dHeadHalfWidth;
	dUp  = pL->dHeadUp;

	dZ = pL->dPosZ
	     - pL->dSinPitch * dFwd
	     + pL->dCosPitch * pL->dCosRoll * dUp
	     + pL->dSinRoll * dLat;

	pL->adEarRight[0] = pL->dPosX
			    + dFwd * pL->dCosPitch * pL->dCosYaw
			    + dLat * pL->dSinYaw * pL->dCosRoll
			    + dUp * (pL->dSinRoll * pL->dSinYaw
				     + pL->dSinPitch * pL->dCosYaw);

	pL->adEarRight[1] = pL->dPosY
			    + dFwd * pL->dCosPitch * pL->dSinYaw
			    - dLat * pL->dCosYaw * pL->dCosRoll
			    + dUp * (pL->dSinPitch * pL->dSinYaw
				     - pL->dSinRoll * pL->dCosYaw);

	pL->adEarRight[2] = dZ;

	pL->adEarLeft[0] = pL->dPosX
			   + dFwd * pL->dCosPitch * pL->dCosYaw
			   - dLat * pL->dSinYaw * pL->dCosRoll
			   + dUp * (pL->dSinRoll * pL->dSinYaw
				    + pL->dSinPitch * pL->dCosYaw);

	pL->adEarLeft[1] = pL->dPosY
			   + dFwd * pL->dCosPitch * pL->dSinYaw
			   + dLat * pL->dCosYaw * pL->dCosRoll
			   + dUp * (pL->dSinPitch * pL->dSinYaw
				    - pL->dSinRoll * pL->dCosYaw);

	pL->adEarLeft[2] = dZ;
}

/* =============================================================
// A3dCartesianToPolar()
// (RE) a3d.dll rtl:0x10004AE0
//
// Convert a world delta to head-relative azimuth, elevation and range.
// =============================================================*/

void
A3dCartesianToPolar(const double *pcdDelta, const double *pcdCos,
		    const double *pcdSin, double *pdOut)
{
double	dFwd;
double	dLeft;
double	dUp;
double	dFlat;

	dFwd = pcdDelta[0] * pcdCos[1] * pcdCos[2]
	       + pcdDelta[1] * pcdCos[1] * pcdSin[2]
	       - pcdDelta[2] * pcdSin[1];

	dLeft = pcdDelta[0] * (pcdSin[0] * pcdSin[1] * pcdCos[2] - pcdCos[0] * pcdSin[2])
		+ pcdDelta[1] * (pcdSin[0] * pcdSin[1] * pcdSin[2] + pcdCos[0] * pcdCos[2])
		+ pcdDelta[2] * (pcdSin[0] * pcdCos[1]);

	dUp = pcdDelta[0] * (pcdCos[0] * pcdSin[1] * pcdCos[2] + pcdSin[0] * pcdSin[2])
	      + pcdDelta[1] * (pcdCos[0] * pcdSin[1] * pcdSin[2] - pcdSin[0] * pcdCos[2])
	      + pcdDelta[2] * (pcdCos[0] * pcdCos[1]);

	dFlat = dLeft * dLeft + dFwd * dFwd;

	if (fabs(dFwd) < A3D_ANGLE_EPSILON && fabs(dLeft) < A3D_ANGLE_EPSILON)
		pdOut[0] = 0.0;
	else if (fabs(dFwd) < A3D_ANGLE_EPSILON)
		pdOut[0] = (dLeft >= 0.0) ? A3D_PI_2 : -A3D_PI_2;
	else if (fabs(dLeft) < A3D_ANGLE_EPSILON)
		pdOut[0] = (dFwd >= 0.0) ? 0.0 : A3D_PI;
	else
	{
		pdOut[0] = atan(dLeft / dFwd);

		if (dFwd < 0.0)
			pdOut[0] += A3D_PI;
	}

	if (fabs(sqrt(dFlat)) < A3D_ANGLE_EPSILON || fabs(dUp) < A3D_ANGLE_EPSILON)
		pdOut[1] = 0.0;
	else
		pdOut[1] = atan(dUp / sqrt(dFlat));

	pdOut[2] = sqrt(dUp * dUp + dFlat);
}

/* =============================================================
// A3dSetDopplerAndRolloff()
// (RE) a3d.dll rtl:0x10004F20
//
// Update listener Doppler, distance rolloff and high-frequency absorption.
// =============================================================*/

void
A3dSetDopplerAndRolloff(FLOAT fDoppler, FLOAT fRolloff, FLOAT fRolloffAgain)
{
FLOAT	fCode;

	g_pListenerState->fDoppler = fDoppler;

	g_fRolloffZero = (fRolloff == 0.0f);

	fCode = fRolloff;

	if (fCode >= 1.0f)
		fCode = (fCode - 1.0f) * A3D_ROLLOFF_SLOPE + A3D_ROLLOFF_UNITY_CODE_F;

	if (fCode >= 0.0f && fCode < 1.0f)
		fCode = fCode * A3D_ROLLOFF_UNITY_CODE_F;

	if (fCode == 0.0f)
		g_dRolloffExponent = 0.0;
	else
		g_dRolloffExponent = (fCode - A3D_ROLLOFF_UNITY_CODE) *
			(1.0 / A3D_ROLLOFF_EXPONENT_DIVISOR) + 1.0;

	g_dHFAbsorb = (fRolloff <= 0.0f) ? 0.0 :
		A3D_HF_ABSORB_ROLLOFF_NUMERATOR / fRolloff;
}

/* =============================================================
// A3dSetDistanceFactor()
// (RE) a3d.dll rtl:0x10005C90
//
// Change world scale and rescale live voices. Preserve the stale listener
// transform and unchecked reciprocal of a zero factor.
// =============================================================*/

void
A3dSetDistanceFactor(FLOAT fFactor)
{
A3DVOICE	*pVoice;
FLOAT		 fOld;
FLOAT		 fRatio;
LONG		 i;

	g_fUnitsPerMetre = fFactor;

	fOld = g_fScale;

	g_fScale      = fFactor;
	g_fScaleRecip = 1.0f / fFactor;

	if ((g_dwCoordFlag & A3D_COORD_LEGACY) || fOld == 0.0f)
		return;

	fRatio = fFactor / fOld;

	for (i = 0; i < g_cVoices; i++)
	{
		pVoice = g_apVoice[i];

		if (!pVoice)
			continue;

		pVoice->fMinDistance *= fRatio;
		pVoice->fMaxDistance *= fRatio;

		pVoice->adPosition[0] *= fRatio;
		pVoice->adPosition[1] *= fRatio;
		pVoice->adPosition[2] *= fRatio;

		pVoice->adVelocity[0] *= fRatio;
		pVoice->adVelocity[1] *= fRatio;
		pVoice->adVelocity[2] *= fRatio;
	}
}

/* =============================================================
// A3dSourceSetPosVel()
//
// Store raw and scaled source position and velocity.
// =============================================================*/

void
A3dSourceSetPosVel(LONG iSource, FLOAT x, FLOAT y, FLOAT z,
		   FLOAT vx, FLOAT vy, FLOAT vz)
{
A3DVOICE	*pVoice;

	pVoice = g_apVoice[iSource];

	pVoice->afRawPosition[0] = x;
	pVoice->afRawPosition[1] = y;
	pVoice->afRawPosition[2] = z;
	pVoice->afRawVelocity[0] = vx;
	pVoice->afRawVelocity[1] = vy;
	pVoice->afRawVelocity[2] = vz;

	A3dSwizzle(pVoice->adPosition, x, y, z);
	A3dSwizzle(pVoice->adVelocity, vx, vy, vz);
}

/* =============================================================
// A3dSourceSetGeometry()
// (RE) a3d.dll rtl:0x10005230
//
// Store source geometry. Preserve division by zero for a zero cone axis.
// Original __cdecl callers pass three extra zeros beyond the declared arguments.
// =============================================================*/

void
A3dSourceSetGeometry(LONG iSource, FLOAT x, FLOAT y, FLOAT z,
		     FLOAT vx, FLOAT vy, FLOAT vz,
		     FLOAT cx, FLOAT cy, FLOAT cz)
{
A3DVOICE	*pVoice;
double		 adAxis[3];
double		 dNorm;

	A3dSourceSetPosVel(iSource, x, y, z, vx, vy, vz);

	pVoice = g_apVoice[iSource];

	A3dSwizzle(adAxis, cx, cy, cz);

	dNorm = 1.0 / sqrt(adAxis[0] * adAxis[0] +
			   adAxis[1] * adAxis[1] +
			   adAxis[2] * adAxis[2]);

	pVoice->adAxis[0] = dNorm * adAxis[0];
	pVoice->adAxis[1] = dNorm * adAxis[1];
	pVoice->adAxis[2] = dNorm * adAxis[2];
}

/* =============================================================
// A3dSourceSetMinDistance()
//
// Store the scaled minimum distance, floored at 1e-4.
// =============================================================*/

void
A3dSourceSetMinDistance(LONG iSource, FLOAT fMin)
{
FLOAT	f;

	f = g_fScale * fMin;

	if (f < A3D_MIN_DISTANCE_FLOOR)
		f = A3D_MIN_DISTANCE_FLOOR;

	g_apVoice[iSource]->fMinDistance = f;
}

/* =============================================================
// A3dSourceSetMaxDistance()
//
// Store the scaled maximum distance.
// =============================================================*/

void
A3dSourceSetMaxDistance(LONG iSource, FLOAT fMax)
{
	g_apVoice[iSource]->fMaxDistance = g_fScale * fMax;
}

/* =============================================================
// A3dSourceReset()
//
// Initialize engine state for a claimed voice.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
A3dSourceReset(LONG iSource, FLOAT x, FLOAT y, FLOAT z,
	       FLOAT vx, FLOAT vy, FLOAT vz, DWORD dwVolume,
	       FLOAT fKhz, DWORD dwSampleRate, FLOAT fBasePitchRatio)
{
A3DVOICE	*pVoice;

	pVoice = g_apVoice[iSource];

	if (dwVolume)
		pVoice->fGain = (FLOAT) (pow(10.0, (dwVolume - A3D_CODE_UNITY)
						   * A3D_CODE_DB)
					 * (double) g_lQ15Full);
	else
		pVoice->fGain = 0.0f;

	pVoice->fSampleKhz = fKhz;

	A3dSourceSetPosVel(iSource, x, y, z, vx, vy, vz);

	pVoice->fDopplerCached	= 1.0f;
	pVoice->fNominalRate	= (FLOAT) dwSampleRate;
	pVoice->fFrequency		= (FLOAT) dwSampleRate;
	pVoice->fBasePitchRatio	= fBasePitchRatio;
	pVoice->fPitchRatio		= fBasePitchRatio;

	pVoice->fMinDistance = (g_fScale < A3D_MIN_DISTANCE_FLOOR)
			       ? A3D_MIN_DISTANCE_FLOOR : g_fScale;
	pVoice->fMaxDistance = g_fScale * A3D_MAX_DISTANCE;

	pVoice->dwSampleRateMode = (pVoice->fSampleKhz == A3D_DSP_KHZ)
		? g_dwSampleRateMode22050 : g_dwSampleRateModeOther;

	pVoice->m_Unknown_0x414 = 0;

	return (S_OK);
}

/* =============================================================
// A3dSourceSetCone()
// (RE) a3d.dll rtl:0x10005AE0
//
// Build a 181-entry half-angle attenuation table.
// =============================================================*/

void
A3dSourceSetCone(LONG iSource, LONG lInside, LONG lInsideAtten,
		 DWORD dwOutside, LONG lOutsideVolume)
{
LONG		 alCode[A3D_CONE_TABLE_ENTRIES];
A3DVOICE	*pVoice;
LONG		 lCodeInside;
LONG		 lCodeOutside;
double		 dStep;
LONG		 cRamp;
LONG		 i;

	memset(alCode, 0, sizeof(alCode));

	lCodeInside = (LONG) (pow(2.0, (double) lInsideAtten * A3D_MILLIBEL)
			      * A3D_CODE_UNITY);

	for (i = 0; i <= lInside; i++)
		alCode[i] = lCodeInside;

	lCodeOutside = (LONG) (pow(2.0, (double) (lOutsideVolume + lInsideAtten)
					* A3D_MILLIBEL) * A3D_CODE_UNITY);

	if (dwOutside <= A3D_CONE_TAIL_INDEX)
	{
		for (i = dwOutside; i <= A3D_CONE_TAIL_INDEX; i++)
			alCode[i] = lCodeOutside;
	}

	cRamp = dwOutside - lInside;
	dStep = (double) (lOutsideVolume + lInsideAtten) / (double) cRamp;

	for (i = 1; i <= cRamp; i++)
	{
		alCode[lInside + i] =
			(LONG) (pow(2.0, (double) (LONG) (i * dStep
							  + (FLOAT) lInsideAtten)
					* A3D_MILLIBEL) * A3D_CODE_UNITY);
	}

	pVoice = g_apVoice[iSource];

	for (i = 0; i < A3D_CONE_TABLE_ENTRIES; i++)
	{
		FLOAT	f;

		if (alCode[i])
			f = (FLOAT) pow(10.0, (double) (alCode[i] - A3D_CODE_UNITY)
					      * A3D_CODE_DB);
		else
			f = 0.0f;

		if (i < A3D_CONE_TAIL_INDEX)
			pVoice->afCone[i] = f;
		else
			pVoice->fConeTail = f;
	}

	pVoice->cCone = A3D_CONE_TABLE_ENTRIES;
}

/* =============================================================
// A3dSourceClearCone()
//
// Disable source directivity.
// =============================================================*/

void
A3dSourceClearCone(LONG iSource)
{
	g_apVoice[iSource]->cCone = 0;
}

/* =============================================================
// A3dSolutionSize()
//
// Write the solution size in bytes to the WORD output.
// =============================================================*/

void
A3dSolutionSize(WORD *pcb)
{
	*pcb = sizeof(A3DSOLUTION);
}

/* =============================================================
// A3dLatchD3DAxes()
// (RE) a3d.dll rtl:0x100058F0
//
// Latch DirectSound axes for subsequent transforms and engine initializations.
// =============================================================*/

void
A3dLatchD3DAxes(void)
{
	g_fAxesD3D = 1;
}

/* =============================================================
// A3dEngineInit()
// (RE) a3d.dll rtl:0x10005900
//
// Allocate the voice pool and initialize live and head-relative listener state.
//
// Returns:
//   S_OK
//   E_FAIL  if a voice allocation fails
// =============================================================*/

HRESULT
A3dEngineInit(LONG cVoices, FLOAT fUnitsPerMetre,
	      DWORD dwSampleRateMode22050, DWORD dwSampleRateModeOther,
	      LONG lQ15Full, DWORD dwFlags, DWORD dwCoordFlag)
{
A3DVOICE	*pVoice;
LONG		 i;

	g_dwCoordFlag	= dwCoordFlag;
	g_lQ15Full		= lQ15Full;
	g_dwEngineFlags	= dwFlags;

	if (dwSampleRateMode22050 != A3D_KEEP_SAMPLE_RATE_MODE)
		g_dwSampleRateMode22050 = dwSampleRateMode22050;

	if (dwSampleRateModeOther != A3D_KEEP_SAMPLE_RATE_MODE)
		g_dwSampleRateModeOther = dwSampleRateModeOther;

	g_fUnitsPerMetre	= fUnitsPerMetre;
	g_fScale			= fUnitsPerMetre;
	g_fScaleRecip		= 1.0f / g_fUnitsPerMetre;

	g_cVoices	= cVoices;
	g_pListenerState = &g_ListenerState;

	memset(&g_ListenerState, 0, sizeof(g_ListenerState));

	g_ListenerState.m_Unknown_0x12C	= 1;
	g_ListenerState.fDoppler		= 1.0f;
	g_ListenerState.dHeadHalfWidth	= A3D_HEAD_HALF_WIDTH;

	if (g_fAxesD3D)
	{
		A3dListenerSetTransform(0.0f, 0.0f, 0.0f,
					0.0f, 0.0f, g_fScaleRecip,
					0.0f, g_fScaleRecip, 0.0f,
					0.0f, 0.0f, 0.0f);
	}
	else
	{
		A3dListenerSetTransform(0.0f, 0.0f, 0.0f,
					g_fScaleRecip, 0.0f, 0.0f,
					0.0f, 0.0f, g_fScaleRecip,
					0.0f, 0.0f, 0.0f);
	}

	memcpy(&g_ListenerShadow, &g_ListenerState, sizeof(g_ListenerShadow));

	g_pListenerShadow = &g_ListenerShadow;

	for (i = 0; i < g_cVoices; i++)
	{
		pVoice = (A3DVOICE *) calloc(sizeof(A3DVOICE), 1);

		g_apVoice[i] = pVoice;

		if (!pVoice)
			return (E_FAIL);

		memset(pVoice, 0, sizeof(A3DVOICE));

		pVoice->adAxis[0]		= 1.0;
		pVoice->fGain			= (FLOAT) g_lQ15Full;
		pVoice->fPitchRatio		= 1.0f;
		pVoice->fDopplerCached	= 1.0f;
		pVoice->cCone			= 0;
		pVoice->fVolLeft		= 1.0f;
		pVoice->fVolRight		= 1.0f;
		pVoice->lRawGainLeft	= 0;
		pVoice->lRawGainRight	= 0;
	}

	g_pListenerState->m_Unknown_0x128 = 0xFF;

	return (S_OK);
}

/* =============================================================
// A3dConeGain()
// (RE) a3d.dll rtl:0x100071D0
//
// Interpolate cone attenuation. Preserve the 181-entry cone endpoint defect:
// an angle of pi reads fSampleKhz as the multiplier.
// Retail compares an x87 intermediate; exact boundary precision is unresolved.
//
// Returns: The attenuated gain; base gain for a disabled cone, invalid dot
//          product or near-zero single-entry cone.
// =============================================================*/

double
A3dConeGain(const double *pcdDir, const double *pcdAxis, A3DVOICE *pVoice)
{
double	dStep;
double	dDot;
double	dAngle;
double	dLow;
double	dHigh;
LONG	i;

	if (pVoice->cCone < 1)
		return (pVoice->fGain);

	dStep = A3D_PI;

	if (pVoice->cCone > 2)
		dStep = A3D_PI / (double) (pVoice->cCone - 1);

	dDot = pcdDir[2] * pcdAxis[2]
	       + pcdDir[1] * pcdAxis[1]
	       + pcdDir[0] * pcdAxis[0];

	if (!(dDot <= 1.0 && dDot >= -1.0))
	{
		fprintf(stderr, "\n DOT_PRODUCT: %f domain error.", dDot);

		return (pVoice->fGain);
	}

	dAngle = acos(dDot);

	if (pVoice->cCone >= 2)
	{
		i = (LONG) (dAngle / dStep);

		dLow  = pVoice->afCone[i];
		dHigh = pVoice->afCone[i + 1];
	}
	else
	{
		dLow = pVoice->afCone[0];

		if (dLow <= A3D_ANGLE_EPSILON)
			return (pVoice->fGain);

		dHigh = pVoice->fConeTail;
	}

	if (dAngle + A3D_ANGLE_EPSILON <= A3D_PI)
		dHigh = fmod(dAngle, dStep) / dStep * (dHigh - dLow) + dLow;

	return (dHigh * pVoice->fGain);
}

/* =============================================================
// A3dReverbSend()
//
// Compute the distance-based reverb send.
//
// Returns: The send in Q15, clamped to [0,g_lQ15Full].
// =============================================================*/

LONG
A3dReverbSend(A3DVOICE *pVoice, double dDistance)
{
double	dFull;
double	dSend;

	dFull = (double) g_lQ15Full;

	if (g_dwCoordFlag & A3D_COORD_LEGACY)
	{
		dSend = dDistance
			/ (pVoice->fMinDistance + g_dHFAbsorb + dDistance)
			* dFull;
	}
	else
	{
		dSend = dDistance
			/ (pVoice->fMinDistance * g_dHFAbsorb
			   + pVoice->fMinDistance + dDistance)
			* dFull;
	}

	if (dSend > dFull)
		return ((LONG) dFull);

	if (dSend < 0.0)
		return (0);

	return ((LONG) dSend);
}

/* =============================================================
// A3dSolveBypass()
//
// Build a flat, centered solution without geometry.
// =============================================================*/

static void
A3dSolveBypass(A3DSOLUTION *pSolution, LONG lGain)
{
LONG	iEar;
LONG	i;

	for (iEar = 0; iEar < 2; iEar++)
	{
		for (i = 0; i < A3D_HRTF_BLEND_COUNT; i++)
			pSolution->ear[iEar].alHrtf[i] = A3D_HRTF_FLAT + iEar * A3D_HRTF_EAR_STRIDE;

		pSolution->ear[iEar].alWeight[0] = g_lQ15Full;
		pSolution->ear[iEar].alWeight[1] = 0;
		pSolution->ear[iEar].alWeight[2] = 0;
		pSolution->ear[iEar].alWeight[3] = 0;

		pSolution->ear[iEar].lGain = lGain;
	}

	EAR_LEFT(pSolution).lReverbSendOrMode  = 0;
	EAR_RIGHT(pSolution).lReverbSendOrMode = A3D_BYPASS;

	pSolution->lDelayLeft  = (LONG) (A3D_DELAY_NOMINAL * A3D_DELAY_UNITS_PER_SAMPLE_F);
	pSolution->lDelayRight = (LONG) (A3D_DELAY_NOMINAL * A3D_DELAY_UNITS_PER_SAMPLE_F);

	if (g_dwEngineFlags & A3D_FLAG_HW_GEOMETRY)
	{
		EAR_LEFT(pSolution).lAzimuth   = 0;
		EAR_LEFT(pSolution).lElevation = 0;
		EAR_LEFT(pSolution).lRange     = 0;
	}
	else
	{
		memset(&EAR_LEFT(pSolution).lAzimuth,  0, A3D_EAR_IMPULSE_TAPS * sizeof(LONG));
		memset(&EAR_RIGHT(pSolution).lAzimuth, 0, A3D_EAR_IMPULSE_TAPS * sizeof(LONG));

		EAR_LEFT(pSolution).lAzimuth  = g_lQ15Full;
		EAR_RIGHT(pSolution).lAzimuth = g_lQ15Full;
	}
}

/* =============================================================
// A3dApplyVolumePan()
// (RE) a3d.dll rtl:0x10006490
//
// Rescale cached ear gains by buffer volume and pan.
// =============================================================*/

void
A3dApplyVolumePan(LONG iSource, DWORD dwVolLeft, DWORD dwVolRight,
		  A3DSOLUTION *pSolution)
{
A3DVOICE	*pVoice;

	pVoice = g_apVoice[iSource];

	pVoice->fVolLeft  = (FLOAT) (LONG) dwVolLeft  * A3D_VOL_RECIP;
	pVoice->fVolRight = (FLOAT) (LONG) dwVolRight * A3D_VOL_RECIP;

	EAR_LEFT(pSolution).lGain  = (LONG) ((double) pVoice->lRawGainLeft
					     * pVoice->fVolLeft);
	EAR_RIGHT(pSolution).lGain = (LONG) ((double) pVoice->lRawGainRight
					     * pVoice->fVolRight);
}

/* =============================================================
// A3dCacheAndScaleGains()
//
// Cache raw ear gains and apply buffer volume.
// =============================================================*/

static void
A3dCacheAndScaleGains(A3DVOICE *pVoice, A3DSOLUTION *pSolution)
{
	pVoice->lRawGainLeft	  = EAR_LEFT(pSolution).lGain;
	EAR_LEFT(pSolution).lGain = (LONG) ((double) EAR_LEFT(pSolution).lGain
					    * pVoice->fVolLeft);

	pVoice->lRawGainRight	   = EAR_RIGHT(pSolution).lGain;
	EAR_RIGHT(pSolution).lGain = (LONG) ((double) EAR_RIGHT(pSolution).lGain
					     * pVoice->fVolRight);
}

/* =============================================================
// A3dDoppler()
//
// Compute and cache the Doppler pitch ratio.
//
// Returns: The ratio clamped to [0.5,2]; the cached ratio for zero range or
//          zero ratio; unity when Doppler is disabled, both are stationary or
//          the denominator is zero.
// =============================================================*/

static FLOAT
A3dDoppler(A3DVOICE *pVoice, const A3DLISTENERSTATE *pcL)
{
double	dC;
double	dLimit;
double	adDelta[3];
double	dRange;
double	dSource;
double	dListener;
double	dDen;
double	dRatio;

	if (pcL->fDoppler == 0.0f ||
	    (pcL->afRawVelocity[0] == 0.0f && pcL->afRawVelocity[1] == 0.0f &&
	     pcL->afRawVelocity[2] == 0.0f &&
	     pVoice->afRawVelocity[0] == 0.0f && pVoice->afRawVelocity[1] == 0.0f &&
	     pVoice->afRawVelocity[2] == 0.0f))
	{
		pVoice->fDopplerCached = 1.0f;

		return (1.0f);
	}

	dC     = A3D_SPEED_OF_SOUND / pcL->fDoppler;
	dLimit = (double) (LONG) (dC - 1.0);

	adDelta[0] = pcL->dPosX - pVoice->adPosition[0];
	adDelta[1] = pcL->dPosY - pVoice->adPosition[1];
	adDelta[2] = pcL->dPosZ - pVoice->adPosition[2];

	dRange = sqrt(adDelta[0] * adDelta[0] + adDelta[1] * adDelta[1] +
		      adDelta[2] * adDelta[2]);

	if (dRange == 0.0)
		return (pVoice->fDopplerCached);

	dSource = (adDelta[0] * pVoice->adVelocity[0] +
		   adDelta[1] * pVoice->adVelocity[1] +
		   adDelta[2] * pVoice->adVelocity[2]) / dRange;

	dListener = (adDelta[0] * pcL->adVelocity[0] +
		     adDelta[1] * pcL->adVelocity[1] +
		     adDelta[2] * pcL->adVelocity[2]) / dRange;

	if (dSource >  dLimit) dSource =  dLimit;
	if (dSource < -dLimit) dSource = -dLimit;
	if (dListener >  dLimit) dListener =  dLimit;
	if (dListener < -dLimit) dListener = -dLimit;

	dDen = dC - dSource;

	if (dDen == 0.0)
	{
		pVoice->fDopplerCached = 1.0f;

		return (1.0f);
	}

	dRatio = (dC - dListener) / dDen;

	if (dRatio == 0.0)
		return (pVoice->fDopplerCached);

	if (dRatio < A3D_PITCH_MIN)
		dRatio = A3D_PITCH_MIN;
	else if (dRatio > A3D_PITCH_MAX)
		dRatio = A3D_PITCH_MAX;

	pVoice->fDopplerCached = (FLOAT) dRatio;

	return ((FLOAT) dRatio);
}

/* =============================================================
// A3dRenderEar()
//
// Quantize one ear direction and compute its gain. Preserve division by
// zero for a directional source exactly at the ear.
// =============================================================*/

static void
A3dRenderEar(A3DEAR *pEar, const double *pcdEarPos,
	     const A3DLISTENERSTATE *pcL, A3DVOICE *pVoice)
{
double	adDelta[3];
double	adPolar[3];
double	dAzFrac;
double	dElFrac;
double	dRange;
double	dGain;
LONG	lBase;
LONG	k;

	adDelta[0] = pVoice->adPosition[0] - pcdEarPos[0];
	adDelta[1] = pVoice->adPosition[1] - pcdEarPos[1];
	adDelta[2] = pVoice->adPosition[2] - pcdEarPos[2];

	A3dCartesianToPolar(adDelta, &pcL->dCosRoll, &pcL->dSinRoll, adPolar);

	if (g_dwEngineFlags & A3D_FLAG_HW_GEOMETRY)
	{
		pEar->lAzimuth   = (LONG) (adPolar[0] * 1000.0);
		pEar->lElevation = (LONG) (adPolar[1] * 1000.0);
		pEar->lRange     = (LONG) ((adPolar[2] * 1000.0 > A3D_HW_RANGE_MAX_MM)
					   ? A3D_HW_RANGE_MAX_MM : adPolar[2] * 1000.0);
	}

	dAzFrac = fmod(adPolar[0] + A3D_PI, A3D_AZ_STEP) * A3D_AZ_RECIP;

	lBase = A3D_HRTF_AZIMUTH_STRIDE *
		((LONG) floor(adPolar[0] * A3D_AZ_RECIP) + A3D_HRTF_AZIMUTH_INDEX_BIAS);

	if (adPolar[1] > A3D_EL_TOP)
	{
		pEar->alHrtf[1] = A3dWrapI(lBase,     0, A3D_HRTF_RING);
		pEar->alHrtf[3] = A3dWrapI(lBase + A3D_HRTF_AZIMUTH_STRIDE, 0, A3D_HRTF_RING);
		pEar->alHrtf[0] = A3D_HRTF_ZENITH;
		pEar->alHrtf[2] = A3D_HRTF_ZENITH;

		dElFrac = (adPolar[1] - A3D_EL_TOP) * A3D_EL_CAP_RECIP;
	}
	else if (adPolar[1] > A3D_EL_BOTTOM)
	{
		k = lBase - ((LONG) floor(adPolar[1] * A3D_EL_RECIP) - A3D_HRTF_ELEVATION_INDEX_BIAS);

		pEar->alHrtf[0] = A3dWrapI(k - 1, 0, A3D_HRTF_RING);
		pEar->alHrtf[1] = A3dWrapI(k,     0, A3D_HRTF_RING);
		pEar->alHrtf[2] = A3dWrapI(k + A3D_HRTF_NEXT_UPPER_ROW_OFFSET, 0, A3D_HRTF_RING);
		pEar->alHrtf[3] = A3dWrapI(k + A3D_HRTF_AZIMUTH_STRIDE, 0, A3D_HRTF_RING);

		dElFrac = fmod(adPolar[1] + A3D_PI, A3D_EL_STEP) * A3D_EL_RECIP;
	}
	else
	{
		pEar->alHrtf[0] = A3dWrapI(lBase + A3D_HRTF_BOTTOM_ROW, 0, A3D_HRTF_RING);
		pEar->alHrtf[2] = A3dWrapI(lBase + A3D_HRTF_NEXT_BOTTOM_ROW, 0, A3D_HRTF_RING);
		pEar->alHrtf[1] = A3D_HRTF_NADIR;
		pEar->alHrtf[3] = A3D_HRTF_NADIR;

		dElFrac = (adPolar[1] + A3D_PI_2) * A3D_EL_NADIR_RECIP;
	}

	pEar->alWeight[0] = (LONG) (g_lQ15Full * dElFrac * (1.0 - dAzFrac) + 0.5);
	pEar->alWeight[1] = (LONG) (g_lQ15Full * (1.0 - dElFrac) * (1.0 - dAzFrac) + 0.5);
	pEar->alWeight[2] = (LONG) (g_lQ15Full * dElFrac * dAzFrac + 0.5);
	pEar->alWeight[3] = (LONG) (g_lQ15Full * (1.0 - dElFrac) * dAzFrac + 0.5);

	dRange = adPolar[2];

	if (dRange < pVoice->fMinDistance)
		dRange = pVoice->fMinDistance;
	else if (dRange > pVoice->fMaxDistance)
		dRange = pVoice->fMaxDistance;

	if (pVoice->cCone != 0)
	{
		adDelta[0] = -adDelta[0] / adPolar[2];
		adDelta[1] = -adDelta[1] / adPolar[2];
		adDelta[2] = -adDelta[2] / adPolar[2];
	}

	dGain = pow(1.0 / dRange, g_dRolloffExponent)
		/ pow(1.0 / pVoice->fMinDistance, g_dRolloffExponent);

	dGain *= A3dConeGain(adDelta, pVoice->adAxis, pVoice);

	if (dGain > (double) g_lQ15Full)
		dGain = (double) g_lQ15Full;
	else if (dGain < 0.0)
		dGain = 0.0;

	pEar->lGain = (LONG) dGain;
}

/* =============================================================
// A3dRender()
// (RE) a3d.dll rtl:0x100067B0
//
// Generate spatial parameters for one listener/source pair.
// =============================================================*/

void
A3dRender(A3DLISTENERSTATE *pL, A3DVOICE *pVoice, A3DSOLUTION *pSolution)
{
double	adDelta[3];
double	adPolar[3];
double	dRange;
double	dItd;
double	dTheta;
LONG	i;

	adDelta[0] = pVoice->adPosition[0] - pL->dPosX;
	adDelta[1] = pVoice->adPosition[1] - pL->dPosY;
	adDelta[2] = pVoice->adPosition[2] - pL->dPosZ;

	dRange = adDelta[0] * adDelta[0] + adDelta[1] * adDelta[1]
		 + adDelta[2] * adDelta[2];

	if (dRange < A3D_NEAR_FIELD_SQ)
	{
		A3dSolveBypass(pSolution,
			       (LONG) (g_lQ15Full * A3D_NEAR_FIELD_GAIN));

		return;
	}

	A3dRenderEar(&EAR_LEFT(pSolution),  pL->adEarLeft,  pL, pVoice);
	A3dRenderEar(&EAR_RIGHT(pSolution), pL->adEarRight, pL, pVoice);

	for (i = 0; i < A3D_HRTF_BLEND_COUNT; i++)
		EAR_RIGHT(pSolution).alHrtf[i] += A3D_HRTF_EAR_STRIDE;

	dRange = sqrt(dRange);

	if (dRange <= pVoice->fMinDistance || g_dHFAbsorb == 0.0)
	{
		EAR_LEFT(pSolution).lReverbSendOrMode = 0;
	}
	else
	{
		EAR_LEFT(pSolution).lReverbSendOrMode = A3dReverbSend(pVoice,
			((dRange > pVoice->fMaxDistance)
			 ? pVoice->fMaxDistance : dRange) - pVoice->fMinDistance);
	}

	adDelta[0] = pVoice->adPosition[0] - pL->dPosX;
	adDelta[1] = pVoice->adPosition[1] - pL->dPosY;
	adDelta[2] = pVoice->adPosition[2] - pL->dPosZ;

	A3dCartesianToPolar(adDelta, &pL->dCosRoll, &pL->dSinRoll, adPolar);

	dTheta = adPolar[0];

	dItd = (A3D_ITD_A0
		- A3D_ITD_A2 * cos(2.0 * dTheta)
		+ A3D_ITD_A3 * cos(3.0 * dTheta)
		+ A3D_ITD_A4 * cos(4.0 * dTheta))
	       * sin(dTheta) * cos(adPolar[1])
	       * pVoice->fSampleKhz * 0.5;

	pSolution->lDelayLeft  = (LONG) ((A3D_DELAY_NOMINAL - dItd) * A3D_DELAY_UNITS_PER_SAMPLE);
	pSolution->lDelayRight = (LONG) ((A3D_DELAY_NOMINAL + dItd) * A3D_DELAY_UNITS_PER_SAMPLE);

	if (pSolution->lDelayLeft > A3D_DELAY_MAX)
		pSolution->lDelayLeft = A3D_DELAY_MAX;

	if (pSolution->lDelayRight > A3D_DELAY_MAX)
		pSolution->lDelayRight = A3D_DELAY_MAX;

	EAR_RIGHT(pSolution).lReverbSendOrMode = 0;
}

/* =============================================================
// A3dSolveNormal()
//
// Solve source geometry and Doppler using the live listener.
// =============================================================*/

void
A3dSolveNormal(A3DSOLUTION *pSolution, LONG iSource)
{
A3DVOICE			*pVoice;
A3DLISTENERSTATE	*pL;

	pVoice = g_apVoice[iSource];
	pL     = g_pListenerState;

	pSolution->lPitch = (LONG) (A3dDoppler(pVoice, pL) * pVoice->fPitchRatio
				    * A3D_PITCH_SCALE);

	A3dRender(pL, pVoice, pSolution);

	A3dCacheAndScaleGains(pVoice, pSolution);
}

/* =============================================================
// A3dSolveHeadRelative()
//
// Solve using the initial listener snapshot with the live Doppler factor.
// =============================================================*/

void
A3dSolveHeadRelative(A3DSOLUTION *pSolution, LONG iSource)
{
A3DVOICE			*pVoice;
A3DLISTENERSTATE	*pL;

	pVoice = g_apVoice[iSource];
	pL     = g_pListenerShadow;

	pL->fDoppler = g_pListenerState->fDoppler;

	pSolution->lPitch = (LONG) (A3dDoppler(pVoice, pL) * pVoice->fPitchRatio
				    * A3D_PITCH_SCALE);

	A3dRender(pL, pVoice, pSolution);

	A3dCacheAndScaleGains(pVoice, pSolution);
}

/* =============================================================
// A3dSolveDisabled()
//
// Build a flat solution retaining source pitch and buffer gains.
// =============================================================*/

void
A3dSolveDisabled(A3DSOLUTION *pSolution, LONG iSource)
{
A3DVOICE	*pVoice;

	pVoice = g_apVoice[iSource];

	pSolution->lPitch = (LONG) (pVoice->fPitchRatio * A3D_PITCH_SCALE);

	A3dSolveBypass(pSolution, g_lQ15Full);

	A3dCacheAndScaleGains(pVoice, pSolution);
}

/* =============================================================
// A3dSourceSetFrequency()
// (RE) a3d.dll rtl:0x10006510
//
// Update source pitch and Doppler; zero frequency restores the nominal rate.
// =============================================================*/

void
A3dSourceSetFrequency(LONG iSource, DWORD dwFrequency, A3DSOLUTION *pSolution)
{
A3DVOICE	*pVoice;
double		 dRatio;

	pVoice = g_apVoice[iSource];

	if (dwFrequency == DSBFREQUENCY_ORIGINAL)
		dwFrequency	= (DWORD) pVoice->fNominalRate;

	pVoice->fFrequency = (FLOAT) dwFrequency;

	dRatio	= (double) dwFrequency / (double) pVoice->fNominalRate * pVoice->fBasePitchRatio;

	pVoice->fPitchRatio = (FLOAT) dRatio;

	pSolution->lPitch = (LONG) (A3dDoppler(pVoice, g_pListenerState) * dRatio
				    * A3D_PITCH_SCALE);
}

/* =============================================================
// A3dMatrixRotate()
// (RE) a3d.dll rtl:0x10007420
//
// Premultiply the listener matrix by the requested axis rotation in degrees.
// =============================================================*/

void
A3dMatrixRotate(char chAxis, FLOAT fDegrees)
{
FLOAT	matRot[4][4];
FLOAT	matOut[4][4];
double	dRadians;
FLOAT	fSin;
FLOAT	fCos;
int		i;
int		j;
int		k;

	dRadians = fDegrees * A3D_LEGACY_DEGREES_TO_RADIANS;

	fCos = (FLOAT) cos(dRadians);
	fSin = (FLOAT) sin(dRadians);

	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 4; j++)
			matRot[i][j] = (i == j) ? 1.0f : 0.0f;
	}

	switch (chAxis)
	{
	case 'h':
	case 'y':
		matRot[0][0] =  fCos;
		matRot[0][2] = -fSin;
		matRot[2][0] =  fSin;
		matRot[2][2] =  fCos;
		break;

	case 'p':
	case 'x':
		matRot[1][1] =  fCos;
		matRot[1][2] =  fSin;
		matRot[2][1] = -fSin;
		matRot[2][2] =  fCos;
		break;

	case 'r':
	case 'z':
		matRot[0][0] =  fCos;
		matRot[0][1] =  fSin;
		matRot[1][0] = -fSin;
		matRot[1][1] =  fCos;
		break;

	default:
		break;
	}

	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 4; j++)
		{
			matOut[i][j] = 0.0f;

			for (k = 0; k < 4; k++)
				matOut[i][j] += matRot[i][k] * g_matListener[k][j];
		}
	}

	memcpy(g_matListener, matOut, sizeof(g_matListener));
}
