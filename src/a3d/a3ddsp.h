/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * a3ddsp.h
 *
 * Defines the compatibility DSP engine's listener, source, per-ear
 * solution and DAL control-bank records. It also declares coordinate
 * conventions, fixed-point scales, source-table limits and the routines
 * used to update or solve a voice.
 *
 * A3dSource and CA3dListener use these declarations to translate
 * DirectSound3D state into backend controls. a3ddsp.cpp implements the
 * calculations and shared engine storage. These types belong to a3d.dll's
 * compatibility engine.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _A3D_A3DDSP_H
#define _A3D_A3DDSP_H

#include "a3dprv.h"

/* Source-table capacity and unallocated index.  *  */
#define A3D_SOURCE_TABLE_CAPACITY	2048
#define A3D_SOURCE_UNALLOCATED	(-1)

/* Cone samples cover half-angles 0 through 180 */
#define A3D_CONE_TABLE_ENTRIES 181
#define A3D_CONE_TAIL_INDEX 180

/* Leave the sample-rate mode unchanged. */
#define A3D_KEEP_SAMPLE_RATE_MODE	0

/* DSP rate in kHz  */
#define A3D_DSP_KHZ 22.05f

/* Q15 full gain  */
#define A3D_Q15_FULL_GAIN 0x7FFF

/* Legacy scaling and reverb mode without RegisterVersion. */
#define A3D_COORD_LEGACY 1

/* HRTF interpolation width  */
#define A3D_HRTF_BLEND_COUNT 4

/* A3DEAR: 0xF0 bytes of per-ear solution state. */

typedef struct A3DEAR
{
	LONG	alHrtf[A3D_HRTF_BLEND_COUNT];	/* Filter indices; right bank adds 75. */
	LONG	alWeight[A3D_HRTF_BLEND_COUNT];	/* Q15 blend weights. */

	/* Fifty-tap impulse state; hardware geometry uses the first three values. */
	LONG	lAzimuth;	/* milliradians */
	LONG	lElevation;	/* milliradians */
	LONG	lRange;	/* Millimetres, capped at 65535. */
	LONG	alImpulseTail[47]; /* 0x2C; remaining impulse taps  */

	LONG	lGain;	/* Q15 */
	LONG	lReverbSendOrMode; /* 0xEC; left send, right mode (0x8001 bypass).
				 *  */
} A3DEAR;

typedef struct A3DSOLUTION
{
	A3DEAR	ear[2];	/* left, +0x0F0 right */
	LONG	lPitch;	/* 10.10 fixed point */
	LONG	lDelayLeft;	/* 1/256 sample, biased by 20 samples */
	LONG	lDelayRight;	/* likewise */
} A3DSOLUTION;				/* 0x1EC = 492 */

/* A3DDALEAR: 0x10 bytes of DAL parameters. */

typedef struct A3DDALEAR
{
	FLOAT	fAzimuth;	/* radians */
	FLOAT	fElevation;	/* radians */
	FLOAT	fGain;	/* 0..1 */
	FLOAT	fDelay;	/* seconds */
} A3DDALEAR;

/* A3DDALBANK: 0x40C-byte A3DCTRL_SRC_SUPER; dwChannels is published as dwMode. */

typedef struct A3DDALBANK
{
	DWORD		m_Unknown_0x00;
	DWORD		dwChannels;	/* 2 when the solution says bypass, else 1 */
	DWORD		dwExecuted;	/* 0x08; receiver bExecuted */
	FLOAT		fPitch;
	FLOAT		fReverbSend;
	BYTE		abFreqFactorAndNative[0x1C - 0x14]; /* Receiver fields */
	A3DDALEAR	ear[2];	/* left, +0x2C right */
	DWORD		dwPriorityBits;	/* 0x3C; receiver fPriority bits */
	FLOAT		fGainMean;	/* the mean of the two ear gains */
	FLOAT		fRange;	/* the two ranges summed and halved: metres */
	/* Receiver reflection/distance fields and reserved storage *  */
	BYTE		abReflectionAndDistanceControl[0x40C - 0x48]; /* 0x48 */
} A3DDALBANK;

/* Engine parameters for one 3D source. */

typedef struct A3DVOICE
{
	double	adPosition[3];	/* swizzled and scaled */
	double	adVelocity[3];	/* likewise */
	double	adAxis[3];	/* unit directivity axis, (1,0,0) by default */
	FLOAT	fMinDistance;	/* floored at 1e-4 */
	FLOAT	fMaxDistance;					/* +0x04C scale * 1e9 */
	FLOAT	afRawPosition[3];	/* as the caller gave them */
	FLOAT	afRawVelocity[3];
	BYTE	m_Unknown_0x68[0x84 - 0x68];
	FLOAT	fGain;	/* base amplitude, the Q15 ceiling */
	FLOAT	fPitchRatio;	/* current, before doppler */
	FLOAT	fDopplerCached;	/* last good ratio */
	LONG	cCone;	/* 181 when a cone is set, else 0 */
	FLOAT	afCone[A3D_CONE_TAIL_INDEX];	/* indexed by half-angle in degrees */
	FLOAT	fConeTail;	/* 0x364; 181st cone entry. */
	FLOAT	fSampleKhz;	/* 0x368; sample rate in kHz. */
	FLOAT	fNominalRate;	/* the rate the buffer was made at */
	FLOAT	fFrequency;	/* current; 0 restores the nominal */
	FLOAT	fBasePitchRatio;				/* +0x374 fFrequency / fNominalRate * this */
	/* Selected by sample rate; its effect has no established consumer. */
	DWORD	dwSampleRateMode;	/* 0x378 */

	/* Buffer-volume scalars and cached gains before volume is applied. */
	FLOAT	fVolLeft;	/* 0..1 */
	FLOAT	fVolRight;
	LONG	lRawGainLeft; /* 0x384 */
	LONG	lRawGainRight; /* 0x388 */
	BYTE	m_Unknown_0x38C[0x414 - 0x38C];
	DWORD	m_Unknown_0x414;
	BYTE	m_Unknown_0x418[0x420 - 0x418];
} A3DVOICE;				/* 0x420 = 1056 */

/* Listener transform and ear model, with a fixed head-relative snapshot. */

typedef struct A3DLISTENERSTATE
{
	BYTE	m_Unknown_0x00[0x08];

	/* The swizzled, scaled listener frame, all doubles. */

	double	dPosX;
	double	dPosY;
	double	dPosZ;

	/* Sine and cosine triples must remain contiguous for A3dCartesianToPolar. */

	double	dRoll;
	double	dPitch;	/* folded into [-pi, pi) */
	double	dYaw;
	double	dSinRoll; /* 0x38 */
	double	dSinPitch;
	double	dSinYaw;
	double	dCosRoll; /* 0x50 */
	double	dCosPitch;
	double	dCosYaw;

	/* Head-model offsets in metres. */

	double	dHeadForward;	/* 0x68; stays zero. */
	double	dHeadHalfWidth;	/* 0x70; 0.0254 metres. */
	double	dHeadUp;	/* 0x78; stays zero. */

	double	adEarRight[3];
	double	adEarLeft[3];

	double	adVelocity[3];
	double	adFront[3];
	double	adTop[3];

	/* The same four vectors again, unswizzled and unscaled. */

	FLOAT	afRawPosition[3];
	FLOAT	afRawVelocity[3];
	FLOAT	afRawFront[3];
	FLOAT	afRawTop[3];

	DWORD	m_Unknown_0x128;	/* 0xFF in the live record, 0 in the shadow */
	DWORD	m_Unknown_0x12C;	/* 1 */
	BYTE	m_Unknown_0x130[0x138 - 0x130];
	FLOAT	fDoppler;	/* listener Doppler factor used by the DSP solver */
	BYTE	m_Unknown_0x13C[0x148 - 0x13C];
} A3DLISTENERSTATE;			/* 0x148 = 328 */

extern FLOAT	g_matListener[4][4];

extern A3DVOICE		*g_apVoice[];	/* 2048 entries */
extern LONG		 g_cVoices;

extern A3DLISTENERSTATE	 g_ListenerState;
extern A3DLISTENERSTATE	 g_ListenerShadow;
extern A3DLISTENERSTATE	*g_pListenerState;
extern A3DLISTENERSTATE	*g_pListenerShadow;

/* Position scale and its cached reciprocal. */

extern FLOAT	g_fUnitsPerMetre;
extern FLOAT	g_fScale;
extern FLOAT	g_fScaleRecip;

extern LONG	g_lQ15Full;	/* 0x7FFF */
extern DWORD	g_dwEngineFlags;	/* Engine feature flags. */

/* Voice modes selected at 22.05 kHz and at other rates. */
extern DWORD	g_dwSampleRateMode22050;
extern DWORD	g_dwSampleRateModeOther;
extern DWORD	g_fAxesD3D;
extern DWORD	g_dwCoordFlag;

/* Positive infinity: default sources are not culled by distance. */

extern double	g_dMaxDistance;

/* Per-source deferred state, set by deferred setters, cleared by CommitOne().
*/

extern BOOL	g_aSourceDirty[];

extern double	g_dHFAbsorb;

extern DWORD	g_fRolloffZero;
extern double	g_dRolloffExponent;

void	A3dMatrixRotate(char chAxis, FLOAT fDegrees);
double	A3dWrapF(double dValue, double dLow, double dHigh);
int	A3dWrapI(int iValue, int iLow, int iHigh);
void	A3dListenerSetTransform(FLOAT px, FLOAT py, FLOAT pz,
				FLOAT fx, FLOAT fy, FLOAT fz,
				FLOAT tx, FLOAT ty, FLOAT tz,
				FLOAT vx, FLOAT vy, FLOAT vz);
void	A3dComputeEarPositions(A3DLISTENERSTATE *pL);
void	A3dCartesianToPolar(const double *pcdDelta, const double *pcdCos,
			    const double *pcdSin, double *pdOut);
void	A3dSetDopplerAndRolloff(FLOAT fDoppler, FLOAT fRolloff,
				FLOAT fRolloffAgain);
void	A3dSetDistanceFactor(FLOAT fFactor);
void	A3dSourceSetPosVel(LONG iSource, FLOAT x, FLOAT y, FLOAT z,
			   FLOAT vx, FLOAT vy, FLOAT vz);
void	A3dFreeSourceSlot(LONG iSource);
HRESULT	A3dEngineInit(LONG cVoices, FLOAT fUnitsPerMetre,
		      DWORD dwSampleRateMode22050, DWORD dwSampleRateModeOther,
		      LONG lQ15Full, DWORD dwFlags, DWORD dwCoordFlag);
void	A3dLatchD3DAxes(void);
void	A3dSolutionSize(WORD *pcb);
HRESULT	A3dSourceReset(LONG iSource, FLOAT x, FLOAT y, FLOAT z,
		       FLOAT vx, FLOAT vy, FLOAT vz, DWORD dwVolume,
		       FLOAT fKhz, DWORD dwSampleRate, FLOAT fBasePitchRatio);
void	A3dSourceSetGeometry(LONG iSource, FLOAT x, FLOAT y, FLOAT z,
			     FLOAT vx, FLOAT vy, FLOAT vz,
			     FLOAT cx, FLOAT cy, FLOAT cz);
void	A3dSourceSetMinDistance(LONG iSource, FLOAT fMin);
void	A3dSourceSetMaxDistance(LONG iSource, FLOAT fMax);
void	A3dSourceSetCone(LONG iSource, LONG lInside, LONG lInsideAtten,
			 DWORD dwOutside, LONG lOutsideVolume);
void	A3dSourceClearCone(LONG iSource);

void	A3dSolveNormal(A3DSOLUTION *pSolution, LONG iSource);
void	A3dSolveHeadRelative(A3DSOLUTION *pSolution, LONG iSource);
void	A3dSolveDisabled(A3DSOLUTION *pSolution, LONG iSource);

void	A3dRender(A3DLISTENERSTATE *pL, A3DVOICE *pVoice,
		  A3DSOLUTION *pSolution);
double	A3dConeGain(const double *pcdDir, const double *pcdAxis,
		    A3DVOICE *pVoice);
LONG	A3dReverbSend(A3DVOICE *pVoice, double dDistance);
void	A3dApplyVolumePan(LONG iSource, DWORD dwVolLeft, DWORD dwVolRight,
			  A3DSOLUTION *pSolution);
void	A3dSourceSetFrequency(LONG iSource, DWORD dwFrequency,
			      A3DSOLUTION *pSolution);

void	A3dBuildDalBank(const A3DSOLUTION *pcSolution, A3DDALBANK *pBank,
			DWORD *pcbBank, FLOAT fKhz);

#endif /* _A3D_A3DDSP_H */
