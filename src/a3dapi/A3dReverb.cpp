/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dReverb.cpp
 *
 * Implements the reverb property object exposed through IA3dReverb. It
 * stores custom acoustic settings, supplies 26 presets and maps custom
 * settings to the closest preset when a backend requires that
 * representation.
 *
 * Property changes update masks used by CA3dRoot to submit only the
 * affected reverb controls. This file defines reverb parameters and
 * matching behavior; A3d3.cpp translates them into the supported device
 * property vocabulary.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "A3dReverb.h"

#include <math.h>
#include <string.h>


#define A3D_REVERB_MILLIBELS_PER_DECADE 2000.0
#define A3D_REVERB_PRESET_FIT_LIMIT     3.0f
#define A3D_REVERB_DECAY_FIT_SCALE      100.0f

/* Setter bounds.*/
#define A3D_REVERB_ROOM_MIN                     -10000
#define A3D_REVERB_ROOM_MAX                     0
#define A3D_REVERB_ROOMHF_MIN                   -10000
#define A3D_REVERB_ROOMHF_MAX                   0
#define A3D_REVERB_ROOM_ROLLOFF_MIN             0.0f
#define A3D_REVERB_ROOM_ROLLOFF_MAX             10.0f
#define A3D_REVERB_CUSTOM_DECAY_TIME_MIN        0.1f
#define A3D_REVERB_CUSTOM_DECAY_TIME_MAX        20.0f
#define A3D_REVERB_DECAY_HF_RATIO_MIN           0.0f
#define A3D_REVERB_DECAY_HF_RATIO_MAX           2.0f
#define A3D_REVERB_REFLECTIONS_MIN              -10000
#define A3D_REVERB_REFLECTIONS_MAX              1000
#define A3D_REVERB_REFLECTIONS_DELAY_MIN        0.0f
#define A3D_REVERB_REFLECTIONS_DELAY_MAX        0.3f
#define A3D_REVERB_REVERB_MIN                   -10000
#define A3D_REVERB_REVERB_MAX                   2000
#define A3D_REVERB_REVERB_DELAY_MIN             0.0f
#define A3D_REVERB_REVERB_DELAY_MAX             0.1f
#define A3D_REVERB_DIFFUSION_MIN                0.0f
#define A3D_REVERB_DIFFUSION_MAX                100.0f
#define A3D_REVERB_DENSITY_MIN                  0.0f
#define A3D_REVERB_DENSITY_MAX                  100.0f
#define A3D_REVERB_HF_REFERENCE_MIN             20.0f
#define A3D_REVERB_HF_REFERENCE_MAX             20000.0f
#define A3D_REVERB_PRESET_VOLUME_MIN            0.0f
#define A3D_REVERB_PRESET_VOLUME_MAX            1.0f
#define A3D_REVERB_PRESET_DECAY_TIME_MIN        0.1f
#define A3D_REVERB_PRESET_DECAY_TIME_MAX        100.0f
#define A3D_REVERB_PRESET_DAMPING_MIN           0.1f
#define A3D_REVERB_PRESET_DAMPING_MAX           100.0f

typedef struct
{
	/* 0x00 */ LONG		lPresetVolumeMillibels;
	/* 0x04 */ LONG		lRoom;
	/* 0x08 */ LONG		lUnknown_0x08;
	/* 0x0C */ LONG		lRoomHF;
	/* 0x10 */ FLOAT	flDecayTime;
	/* 0x14 */ FLOAT	flDecayHFRatio;
	/* 0x18 */ LONG		lReflections;
	/* 0x1C */ FLOAT	flReflectionsDelay;
	/* 0x20 */ LONG		lReverb;
	/* 0x24 */ FLOAT	flReverbDelay;
	/* 0x28 */ FLOAT	flDiffusion;
	/* 0x2C */ FLOAT	flDensity;
	/* 0x30 */ FLOAT	flHFReference;
	/* 0x34 */ FLOAT	flRoomRolloffFactor;
} REVERBCUSTOMROW;		/* 0x38 bytes */

typedef struct
{
	/* 0x00 */ DWORD	dwEnvPreset;
	/* 0x04 */ A3DVAL	fVolume;
	/* 0x08 */ A3DVAL	fDecayTime;
	/* 0x0C */ A3DVAL	fDamping;
} REVERBPRESETROW;		/* 0x10 bytes */

/* (RE) Custom preset rows: dbg:0x10146F48; rtl:0x1005D940. */

static REVERBCUSTOMROW ReverbCustomTable[A3DREVERB_PRESET_COUNT] =
{
	{  -602, -1000, 0,  -100,  1.493f, 0.5f,    -2602, 0.007f,   200, 0.011f, 100.0f, 100.0f, 5000.0f, 0.0f },	/* GENERIC */
	{ -1204, -1000, 0, -6000,  0.1f,   0.0f,    -1204, 0.001f,   207, 0.002f, 100.0f, 100.0f, 5000.0f, 0.0f },	/* PADDEDCELL */
	{  -760, -1000, 0,  -454,  0.4f,   0.666f,  -1646, 0.002f,    53, 0.003f, 100.0f, 100.0f, 5000.0f, 0.0f },	/* ROOM */
	{  -370, -1000, 0,     0,  1.499f, 0.166f,   -370, 0.007f,  1030, 0.011f, 100.0f,  60.0f, 5000.0f, 0.0f },	/* BATHROOM */
	{ -1364, -1000, 0, -6000,  0.478f, 0.0f,    -1376, 0.003f,  -600, 0.004f, 100.0f, 100.0f, 5000.0f, 0.0f },	/* LIVINGROOM */
	{  -602, -1500, 0,  -530,  2.309f, 0.888f,   -711, 0.012f,    83, 0.017f, 100.0f, 100.0f, 5000.0f, 0.0f },	/* STONEROOM */
	{  -789, -1000, 0, -1000,  4.279f, 0.5f,     -789, 0.02f,   -289, 0.03f,  100.0f, 100.0f, 5000.0f, 0.0f },	/* AUDITORIUM */
	{  -602, -1000, 0,  -500,  3.961f, 0.5f,    -1230, 0.02f,     -2, 0.029f, 100.0f, 100.0f, 5000.0f, 0.0f },	/* CONCERTHALL */
	{  -602, -1000, 0,     0,  2.886f, 1.304f,   -602, 0.015f,  -302, 0.022f, 100.0f, 100.0f, 5000.0f, 0.0f },	/* CAVE */
	{  -885, -1000, 0,  -698,  7.284f, 0.332f,  -1166, 0.02f,     16, 0.03f,  100.0f, 100.0f, 5000.0f, 0.0f },	/* ARENA */
	{  -602, -1000, 0, -1000, 10.0f,   0.3f,     -602, 0.02f,      0, 0.03f,  100.0f, 100.0f, 5000.0f, 0.0f },	/* HANGAR */
	{ -1631, -1000, 0, -4000,  0.259f, 2.0f,    -1831, 0.002f,  -300, 0.03f,  100.0f, 100.0f, 5000.0f, 0.0f },	/* CARPETEDHALLWAY */
	{  -885, -1000, 0,     0,  1.493f, 0.0f,    -1219, 0.007f,   441, 0.011f, 100.0f, 100.0f, 5000.0f, 0.0f },	/* HALLWAY */
	{  -705, -1000, 0,  -237,  2.697f, 0.638f,  -1214, 0.013f,   395, 0.02f,  100.0f, 100.0f, 5000.0f, 0.0f },	/* STONECORRIDOR */
	{ -1204, -1000, 0,  -270,  1.752f, 0.776f,  -1204, 0.15f,     -4, 0.051f, 100.0f, 100.0f, 5000.0f, 0.0f },	/* ALLEY */
	{ -1909, -1000, 0, -1300,  3.145f, 0.472f,  -2560, 0.051f,  -300, 0.011f,  79.0f, 100.0f, 5000.0f, 0.0f },	/* FOREST */
	{ -1909, -1000, 0,  -800,  2.767f, 0.224f,  -2273, 0.007f, -1000, 0.011f,  50.0f, 100.0f, 5000.0f, 0.0f },	/* CITY */
	{ -1424, -1000, 0, -2500,  7.841f, 0.472f,   -500, 0.3f,   -2014, 0.1f,    27.0f, 100.0f, 5000.0f, 0.0f },	/* MOUNTAINS */
	{     0, -1000, 0, -1000,  1.499f, 0.5f,   -10000, 0.061f,   500, 0.025f, 100.0f, 100.0f, 5000.0f, 0.0f },	/* QUARRY */
	{ -2026, -1000, 0,     0,  2.767f, 0.224f,      0, 0.279f, -2100, 0.041f,  21.0f, 100.0f, 5000.0f, 0.0f },	/* PLAIN */
	{ -1364, -1000, 0,     0,  1.652f, 1.5f,     -300, 0.008f, -1153, 0.012f, 100.0f, 100.0f, 5000.0f, 0.0f },	/* PARKINGLOT */
	{  -372, -1000, 0, -1000,  2.886f, 0.25f,       0, 0.014f,     0, 0.021f,  80.0f,  60.0f, 5000.0f, 0.0f },	/* SEWERPIPE */
	{     0, -1000, 0, -4000,  1.499f, 0.0f,     -449, 0.007f,  1700, 0.011f, 100.0f, 100.0f, 5000.0f, 0.0f },	/* UNDERWATER */
	{  -116, -1000, 0,  -130,  8.392f, 1.388f, -10000, 0.02f,      0, 0.03f,  100.0f, 100.0f, 5000.0f, 0.0f },	/* DRUGGED */
	{ -1714, -1000, 0,  -180, 17.234f, 0.666f, -10000, 0.02f,      0, 0.03f,  100.0f, 100.0f, 5000.0f, 0.0f },	/* DIZZY */
	{  -627, -1000, 0,  -150,  7.563f, 0.806f, -10000, 0.02f,      0, 0.03f,  100.0f, 100.0f, 5000.0f, 0.0f },	/* PSYCHOTIC */
};

/* (RE) Preset triples: dbg:0x101474F8; rtl:0x1005DEF4. */

static REVERBPRESETROW ReverbPresetTable[A3DREVERB_PRESET_COUNT] =
{
	{ A3DREVERB_PRESET_GENERIC,         0.5f,    1.493f, 0.5f   },
	{ A3DREVERB_PRESET_PADDEDCELL,      0.25f,   0.1f,   0.0f   },
	{ A3DREVERB_PRESET_ROOM,            0.417f,  0.4f,   0.666f },
	{ A3DREVERB_PRESET_BATHROOM,        0.653f,  1.499f, 0.166f },
	{ A3DREVERB_PRESET_LIVINGROOM,      0.208f,  0.478f, 0.0f   },
	{ A3DREVERB_PRESET_STONEROOM,       0.5f,    2.309f, 0.888f },
	{ A3DREVERB_PRESET_AUDITORIUM,      0.403f,  4.279f, 0.5f   },
	{ A3DREVERB_PRESET_CONCERTHALL,     0.5f,    3.961f, 0.5f   },
	{ A3DREVERB_PRESET_CAVE,            0.5f,    2.886f, 1.304f },
	{ A3DREVERB_PRESET_ARENA,           0.361f,  7.284f, 0.332f },
	{ A3DREVERB_PRESET_HANGAR,          0.5f,   10.0f,   0.3f   },
	{ A3DREVERB_PRESET_CARPETEDHALLWAY, 0.153f,  0.259f, 2.0f   },
	{ A3DREVERB_PRESET_HALLWAY,         0.361f,  1.493f, 0.0f   },
	{ A3DREVERB_PRESET_STONECORRIDOR,   0.444f,  2.697f, 0.638f },
	{ A3DREVERB_PRESET_ALLEY,           0.25f,   1.752f, 0.776f },
	{ A3DREVERB_PRESET_FOREST,          0.111f,  3.145f, 0.472f },
	{ A3DREVERB_PRESET_CITY,            0.111f,  2.767f, 0.224f },
	{ A3DREVERB_PRESET_MOUNTAINS,       0.194f,  7.841f, 0.472f },
	{ A3DREVERB_PRESET_QUARRY,          1.0f,    1.499f, 0.5f   },
	{ A3DREVERB_PRESET_PLAIN,           0.097f,  2.767f, 0.224f },
	{ A3DREVERB_PRESET_PARKINGLOT,      0.208f,  1.652f, 1.5f   },
	{ A3DREVERB_PRESET_SEWERPIPE,       0.652f,  2.886f, 0.25f  },
	{ A3DREVERB_PRESET_UNDERWATER,      1.0f,    1.499f, 0.0f   },
	{ A3DREVERB_PRESET_DRUGGED,         0.875f,  8.392f, 1.388f },
	{ A3DREVERB_PRESET_DIZZY,           0.139f, 17.234f, 0.666f },
	{ A3DREVERB_PRESET_PSYCHOTIC,       0.486f,  7.563f, 0.806f },
};

/* =============================================================
// CA3dReverb::~CA3dReverb() vector deleting destructor
// (RE) rtl:0x1000ae20; dbg:0x1001b820
// =============================================================*/

/* =============================================================
// IA3dReverb::IA3dReverb() compiler-generated interface constructor
// (RE) dbg:0x1001b870
// =============================================================*/

/* =============================================================
// CA3dReverb::~CA3dReverb() compiler-generated destructor
// (RE) rtl:0x1000ae50; dbg:0x1001b8a0
// =============================================================*/

/* =============================================================
// CA3dReverb()
// (RE) rtl:0x1000ADD0; dbg:0x1001B780
//
// Initialize an empty property block and clear both change masks.
// =============================================================*/

CA3dReverb::CA3dReverb(void)
{
	m_cRef = 0;

	memset(&m_Properties, 0, sizeof(A3DREVERB_PROPERTIES));
	m_Properties.dwSize = sizeof(A3DREVERB_PROPERTIES);

	m_dwChangedPending      = 0;
	m_dwChangedEver         = 0;
}

/* =============================================================
// QueryInterface()
// (RE) rtl:0x1000AE80; dbg:0x1001B900
//
// Query IUnknown or IA3dReverb. Preserve the unchanged output on an
// unsupported IID.
//
// Returns:
//   S_OK
//   E_POINTER      a null output
//   E_NOINTERFACE  other IIDs
// =============================================================*/

STDMETHODIMP
CA3dReverb::QueryInterface(REFIID riid, void **ppv)
{
	if (!ppv)
	{
		DBGSTR("CA3dReverb::QueryInterface() - ppv is NULL.\n");

		return (E_POINTER);
	}

	if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IA3dReverb))
	{
		*ppv = this;
		((IUnknown *) *ppv)->AddRef();
		return (S_OK);
	}

	return (E_NOINTERFACE);
}

/* =============================================================
// AddRef()
// (RE) rtl:0x10008F50; dbg:0x1001B9A0
//
// Increment the reference count.
//
// Returns: The updated reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dReverb::AddRef(void)
{
	return (++m_cRef);
}

/* =============================================================
// Release()
// (RE) rtl:0x1000AEE0; dbg:0x1001B9D0
//
// Release a reference and delete the reverb at zero.
//
// Returns: The remaining reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dReverb::Release(void)
{
	if (--m_cRef != 0)
		return (m_cRef);

	delete this;

	return (0);
}

/* =============================================================
// MapCustomToPreset()
// (RE) rtl:0x1000AF10; dbg:0x1001BA50
//
// Write estimated preset parameters, choosing the closest preset with a
// score below 3. The preset identifier stays unchanged if no row qualifies.
//
// Returns: pPresetProp.
// =============================================================*/

A3DREVERB_PROPERTIES * __cdecl
MapCustomToPreset(A3DREVERB_PROPERTIES *pCustomProp,
		   A3DREVERB_PROPERTIES *pPresetProp)
{
A3DVAL	fRoom;
A3DVAL	fReflections;
A3DVAL	fVolume;
A3DVAL	fDecayTime;
A3DVAL	fDamping;
A3DVAL	fBestFit;
A3DVAL	fVolumeFit;
A3DVAL	fDecayFit;
A3DVAL	fDampingFit;
A3DVAL	fFit;
DWORD	i;

	fRoom        = (A3DVAL) pCustomProp->uval.custom.lRoom;
	fReflections = (A3DVAL) pow(10.0,
		(double) pCustomProp->uval.custom.lReflections / A3D_REVERB_MILLIBELS_PER_DECADE);

	fVolume = (A3DVAL) pow(10.0,
		(double) (LONG) (log10(pow(10.0,
			(double) pCustomProp->uval.custom.lReverb / A3D_REVERB_MILLIBELS_PER_DECADE) +
			fReflections) * A3D_REVERB_MILLIBELS_PER_DECADE + fRoom) / A3D_REVERB_MILLIBELS_PER_DECADE);

	if (fVolume > 1.0f)
		fVolume = 1.0f;

	fDecayTime = pCustomProp->uval.custom.flDecayTime;
	fDamping   = pCustomProp->uval.custom.flDecayHFRatio;

	fBestFit = A3D_REVERB_PRESET_FIT_LIMIT;

	for (i = 0; i < A3DREVERB_PRESET_COUNT; i++)
	{
		fVolumeFit  = (A3DVAL)
			(fabs(ReverbPresetTable[i].fVolume - fVolume) / 1.0f);
		fDecayFit   = (A3DVAL)
			(fabs(ReverbPresetTable[i].fDecayTime - fDecayTime) / A3D_REVERB_DECAY_FIT_SCALE);
		fDampingFit = (A3DVAL)
			(fabs(ReverbPresetTable[i].fDamping - fDamping) / 1.0f);

		fFit = fVolumeFit + fDecayFit + fDampingFit;

		if (fFit < fBestFit)
		{
			fBestFit                                = fFit;
			pPresetProp->uval.preset.dwEnvPreset    = i;
		}
	}

	pPresetProp->uval.preset.fVolume    = fVolume;
	pPresetProp->uval.preset.fDecayTime = fDecayTime;
	pPresetProp->uval.preset.fDamping   = fDamping;

	return (pPresetProp);
}

/* =============================================================
// SetAllProperties()
// (RE) rtl:0x1000B020; dbg:0x1001BC70
//
// Store preset or custom properties, skipping invalid custom values.
// Preserve passing decay time as preset damping.
//
// Returns:
//   S_OK
//   E_POINTER     a null block
//   E_INVALIDARG  invalid sizes or type
// =============================================================*/

STDMETHODIMP
CA3dReverb::SetAllProperties(A3DREVERB_PROPERTIES *pReverbProp)
{
DWORD	dwType;

	if (!pReverbProp)
	{
		DBGSTR("CA3dReverb::SetAllProperties() - pReverbProp is NULL.\n");

		return (E_POINTER);
	}

	ASSERT((pReverbProp != 0 &&
	       !IsBadReadPtr(pReverbProp, sizeof(A3DREVERB_PROPERTIES))));

	if (pReverbProp->dwSize != sizeof(A3DREVERB_PROPERTIES))
	{
		TRACE("CA3dReverb::SetAllProperties() - dwSize is not valid.\n"
		      "\tSize Passed: %u\tActual Size: %u.\n",
		      pReverbProp->dwSize, sizeof(A3DREVERB_PROPERTIES));
		return (E_INVALIDARG);
	}

	if (!memcmp(pReverbProp, &m_Properties, sizeof(A3DREVERB_PROPERTIES)))
		return (S_OK);

	dwType = pReverbProp->dwType;

	if (dwType == A3DREVERB_TYPE_PRESET)
	{
		if (pReverbProp->uval.preset.dwSize != sizeof(A3DREVERB_PRESET))
		{
			DBGSTR("CA3dReverb::SetAllProperties() - Invalid size for reverb preset structure.\n");

			return (E_INVALIDARG);
		}

		SetReverbPreset(pReverbProp->uval.preset.dwEnvPreset);
		SetPresetVolume(pReverbProp->uval.preset.fVolume);
		SetPresetDecayTime(pReverbProp->uval.preset.fDecayTime);

		SetPresetDamping(pReverbProp->uval.preset.fDecayTime);
	}
	else
	{
		if (dwType != A3DREVERB_TYPE_CUSTOM)
		{
			DBGSTR("CA3dReverb::SetAllProperties() - Invalid requested type.\n\tMust be A3DREVERB_TYPE_PRESET or A3DREVERB_TYPE_CUSTOM.\n");

			return (E_INVALIDARG);
		}

		if (pReverbProp->uval.custom.dwSize != sizeof(A3DREVERB_CUSTOM))
		{
			DBGSTR("CA3dReverb::SetAllProperties() - Invalid size for reverb custom structure.\n");

			return (E_INVALIDARG);
		}

		m_Properties.dwType = A3DREVERB_TYPE_CUSTOM;

		if (pReverbProp->uval.custom.lRoom < A3D_REVERB_ROOM_MIN ||
		    pReverbProp->uval.custom.lRoom > A3D_REVERB_ROOM_MAX)
		{
			TRACE("Invalid parameter range propchange bit = %x\n", A3DREVERB_CHANGED_CUSTOM_ROOM);
		}
		else if (pReverbProp->uval.custom.lRoom !=
			 m_Properties.uval.custom.lRoom)
		{
			m_Properties.uval.custom.lRoom =
				pReverbProp->uval.custom.lRoom;
			m_dwChangedPending |= A3DREVERB_CHANGED_CUSTOM_ROOM;
			m_dwChangedEver |= A3DREVERB_CHANGED_CUSTOM_ROOM;
		}

		if (pReverbProp->uval.custom.lRoomHF < A3D_REVERB_ROOMHF_MIN ||
		    pReverbProp->uval.custom.lRoomHF > A3D_REVERB_ROOMHF_MAX)
		{
			TRACE("Invalid parameter range propchange bit = %x\n", A3DREVERB_CHANGED_CUSTOM_ROOMHF);
		}
		else if (pReverbProp->uval.custom.lRoomHF !=
			 m_Properties.uval.custom.lRoomHF)
		{
			m_Properties.uval.custom.lRoomHF =
				pReverbProp->uval.custom.lRoomHF;
			m_dwChangedPending |= A3DREVERB_CHANGED_CUSTOM_ROOMHF;
			m_dwChangedEver |= A3DREVERB_CHANGED_CUSTOM_ROOMHF;
		}

		if (pReverbProp->uval.custom.flRoomRolloffFactor < A3D_REVERB_ROOM_ROLLOFF_MIN ||
		    pReverbProp->uval.custom.flRoomRolloffFactor > A3D_REVERB_ROOM_ROLLOFF_MAX)
		{
			TRACE("Invalid parameter range propchange bit = %x\n", A3DREVERB_CHANGED_CUSTOM_ROOMROLLOFFFACTOR);
		}
		else if (pReverbProp->uval.custom.flRoomRolloffFactor !=
			 m_Properties.uval.custom.flRoomRolloffFactor)
		{
			m_Properties.uval.custom.flRoomRolloffFactor =
				pReverbProp->uval.custom.flRoomRolloffFactor;
			m_dwChangedPending |= A3DREVERB_CHANGED_CUSTOM_ROOMROLLOFFFACTOR;
			m_dwChangedEver |= A3DREVERB_CHANGED_CUSTOM_ROOMROLLOFFFACTOR;
		}

		if (pReverbProp->uval.custom.flDecayTime < A3D_REVERB_CUSTOM_DECAY_TIME_MIN ||
		    pReverbProp->uval.custom.flDecayTime > A3D_REVERB_CUSTOM_DECAY_TIME_MAX)
		{
			TRACE("Invalid parameter range propchange bit = %x\n", A3DREVERB_CHANGED_CUSTOM_DECAYTIME);
		}
		else if (pReverbProp->uval.custom.flDecayTime !=
			 m_Properties.uval.custom.flDecayTime)
		{
			m_Properties.uval.custom.flDecayTime =
				pReverbProp->uval.custom.flDecayTime;
			m_dwChangedPending |= A3DREVERB_CHANGED_CUSTOM_DECAYTIME;
			m_dwChangedEver |= A3DREVERB_CHANGED_CUSTOM_DECAYTIME;
		}

		if (pReverbProp->uval.custom.flDecayHFRatio < A3D_REVERB_DECAY_HF_RATIO_MIN ||
		    pReverbProp->uval.custom.flDecayHFRatio > A3D_REVERB_DECAY_HF_RATIO_MAX)
		{
			TRACE("Invalid parameter range propchange bit = %x\n", A3DREVERB_CHANGED_CUSTOM_DECAYHFRATIO);
		}
		else if (pReverbProp->uval.custom.flDecayHFRatio !=
			 m_Properties.uval.custom.flDecayHFRatio)
		{
			m_Properties.uval.custom.flDecayHFRatio =
				pReverbProp->uval.custom.flDecayHFRatio;
			m_dwChangedPending |= A3DREVERB_CHANGED_CUSTOM_DECAYHFRATIO;
			m_dwChangedEver |= A3DREVERB_CHANGED_CUSTOM_DECAYHFRATIO;
		}

		if (pReverbProp->uval.custom.lReflections < A3D_REVERB_REFLECTIONS_MIN ||
		    pReverbProp->uval.custom.lReflections > A3D_REVERB_REFLECTIONS_MAX)
		{
			TRACE("Invalid parameter range propchange bit = %x\n", A3DREVERB_CHANGED_CUSTOM_REFLECTIONS);
		}
		else if (pReverbProp->uval.custom.lReflections !=
			 m_Properties.uval.custom.lReflections)
		{
			m_Properties.uval.custom.lReflections =
				pReverbProp->uval.custom.lReflections;
			m_dwChangedPending |= A3DREVERB_CHANGED_CUSTOM_REFLECTIONS;
			m_dwChangedEver |= A3DREVERB_CHANGED_CUSTOM_REFLECTIONS;
		}

		if (pReverbProp->uval.custom.flReflectionsDelay < A3D_REVERB_REFLECTIONS_DELAY_MIN ||
		    pReverbProp->uval.custom.flReflectionsDelay > A3D_REVERB_REFLECTIONS_DELAY_MAX)
		{
			TRACE("Invalid parameter range propchange bit = %x\n", A3DREVERB_CHANGED_CUSTOM_REFLECTIONSDELAY);
		}
		else if (pReverbProp->uval.custom.flReflectionsDelay !=
			 m_Properties.uval.custom.flReflectionsDelay)
		{
			m_Properties.uval.custom.flReflectionsDelay =
				pReverbProp->uval.custom.flReflectionsDelay;
			m_dwChangedPending |= A3DREVERB_CHANGED_CUSTOM_REFLECTIONSDELAY;
			m_dwChangedEver |= A3DREVERB_CHANGED_CUSTOM_REFLECTIONSDELAY;
		}

		if (pReverbProp->uval.custom.lReverb < A3D_REVERB_REVERB_MIN ||
		    pReverbProp->uval.custom.lReverb > A3D_REVERB_REVERB_MAX)
		{
			TRACE("Invalid parameter range propchange bit = %x\n", A3DREVERB_CHANGED_CUSTOM_REVERB);
		}
		else if (pReverbProp->uval.custom.lReverb !=
			 m_Properties.uval.custom.lReverb)
		{
			m_Properties.uval.custom.lReverb =
				pReverbProp->uval.custom.lReverb;
			m_dwChangedPending |= A3DREVERB_CHANGED_CUSTOM_REVERB;
			m_dwChangedEver |= A3DREVERB_CHANGED_CUSTOM_REVERB;
		}

		if (pReverbProp->uval.custom.flReverbDelay < A3D_REVERB_REVERB_DELAY_MIN ||
		    pReverbProp->uval.custom.flReverbDelay > A3D_REVERB_REVERB_DELAY_MAX)
		{
			TRACE("Invalid parameter range propchange bit = %x\n", A3DREVERB_CHANGED_CUSTOM_REVERBDELAY);
		}
		else if (pReverbProp->uval.custom.flReverbDelay !=
			 m_Properties.uval.custom.flReverbDelay)
		{
			m_Properties.uval.custom.flReverbDelay =
				pReverbProp->uval.custom.flReverbDelay;
			m_dwChangedPending |= A3DREVERB_CHANGED_CUSTOM_REVERBDELAY;
			m_dwChangedEver |= A3DREVERB_CHANGED_CUSTOM_REVERBDELAY;
		}

		if (pReverbProp->uval.custom.flDiffusion < A3D_REVERB_DIFFUSION_MIN ||
		    pReverbProp->uval.custom.flDiffusion > A3D_REVERB_DIFFUSION_MAX)
		{
			TRACE("Invalid parameter range propchange bit = %x\n", A3DREVERB_CHANGED_CUSTOM_DIFFUSION);
		}
		else if (pReverbProp->uval.custom.flDiffusion !=
			 m_Properties.uval.custom.flDiffusion)
		{
			m_Properties.uval.custom.flDiffusion =
				pReverbProp->uval.custom.flDiffusion;
			m_dwChangedPending |= A3DREVERB_CHANGED_CUSTOM_DIFFUSION;
			m_dwChangedEver |= A3DREVERB_CHANGED_CUSTOM_DIFFUSION;
		}

		if (pReverbProp->uval.custom.flDensity < A3D_REVERB_DENSITY_MIN ||
		    pReverbProp->uval.custom.flDensity > A3D_REVERB_DENSITY_MAX)
		{
			TRACE("Invalid parameter range propchange bit = %x\n", A3DREVERB_CHANGED_CUSTOM_DENSITY);
		}
		else if (pReverbProp->uval.custom.flDensity !=
			 m_Properties.uval.custom.flDensity)
		{
			m_Properties.uval.custom.flDensity =
				pReverbProp->uval.custom.flDensity;
			m_dwChangedPending |= A3DREVERB_CHANGED_CUSTOM_DENSITY;
			m_dwChangedEver |= A3DREVERB_CHANGED_CUSTOM_DENSITY;
		}

		if (pReverbProp->uval.custom.flHFReference < A3D_REVERB_HF_REFERENCE_MIN ||
		    pReverbProp->uval.custom.flHFReference > A3D_REVERB_HF_REFERENCE_MAX)
		{
			TRACE("Invalid parameter range propchange bit = %x\n", A3DREVERB_CHANGED_CUSTOM_HFREFERENCE);
		}
		else if (pReverbProp->uval.custom.flHFReference !=
			 m_Properties.uval.custom.flHFReference)
		{
			m_Properties.uval.custom.flHFReference =
				pReverbProp->uval.custom.flHFReference;
			m_dwChangedPending |= A3DREVERB_CHANGED_CUSTOM_HFREFERENCE;
			m_dwChangedEver |= A3DREVERB_CHANGED_CUSTOM_HFREFERENCE;
		}
	}

	return (S_OK);
}

/* =============================================================
// GetAllProperties()
// (RE) rtl:0x1000B3D0; dbg:0x1001C630
//
// Convert property values to the requested form while preserving the
// stored type and size fields.
//
// Returns:
//   S_OK
//   E_POINTER     a null block
//   E_INVALIDARG  invalid size or type
// =============================================================*/

STDMETHODIMP
CA3dReverb::GetAllProperties(A3DREVERB_PROPERTIES *pReverbProp)
{
DWORD			dwType;
REVERBCUSTOMROW		Row;
A3DREVERB_PROPERTIES	Props;
DWORD			dwSize;

	if (!pReverbProp)
	{
		DBGSTR("CA3dReverb::GetAllProperties() - pReverbProp is NULL.\n");

		return (E_POINTER);
	}

	ASSERT((pReverbProp != 0 &&
	       !IsBadReadPtr(pReverbProp, sizeof(A3DREVERB_PROPERTIES))));

	dwSize = sizeof(A3DREVERB_PROPERTIES);

	if (pReverbProp->dwSize != sizeof(A3DREVERB_PROPERTIES))
	{
		TRACE("CA3dReverb::GetAllProperties() - dwSize is not valid.\n"
		      "\tSize Passed: %u\tActual Size: %u.\n",
		      pReverbProp->dwSize, sizeof(A3DREVERB_PROPERTIES));
		return (E_INVALIDARG);
	}

	memcpy(&Props, &m_Properties, sizeof(Props));

	dwType = pReverbProp->dwType;

	if (dwType == A3DREVERB_TYPE_PRESET)
	{
		if (m_Properties.dwType == A3DREVERB_TYPE_CUSTOM)
			MapCustomToPreset(&m_Properties, &Props);
	}
	else
	{
		if (dwType != A3DREVERB_TYPE_CUSTOM)
		{
			DBGSTR("CA3dReverb::GetAllProperties() - Invalid requested type.\n\tMust be A3DREVERB_TYPE_PRESET or A3DREVERB_TYPE_CUSTOM.\n");

			return (E_INVALIDARG);
		}

		if (m_Properties.dwType == A3DREVERB_TYPE_PRESET)
		{
			Row = ReverbCustomTable[m_Properties.uval.preset.dwEnvPreset];

			Props.uval.custom.lRoom                 = Row.lRoom;
			Props.uval.custom.lRoomHF               = Row.lRoomHF;
			Props.uval.custom.flDecayTime           = Row.flDecayTime;
			Props.uval.custom.flDecayHFRatio        = Row.flDecayHFRatio;
			Props.uval.custom.lReflections          = Row.lReflections;
			Props.uval.custom.flReflectionsDelay    = Row.flReflectionsDelay;
			Props.uval.custom.lReverb               = Row.lReverb;
			Props.uval.custom.flReverbDelay         = Row.flReverbDelay;
			Props.uval.custom.flDiffusion           = Row.flDiffusion;
			Props.uval.custom.flDensity             = Row.flDensity;
			Props.uval.custom.flHFReference         = Row.flHFReference;
			Props.uval.custom.flRoomRolloffFactor   = Row.flRoomRolloffFactor;
		}
	}

	memcpy(pReverbProp, &Props, dwSize);

	return (S_OK);
}

/* =============================================================
// SetReverbPreset()
// (RE) rtl:0x1000B4D0; dbg:0x1001C830
//
// Select a preset and mark a changed value. Preserve the unsigned lower bound
// check.
//
// Returns:
//   S_OK
//   E_INVALIDARG  beyond A3DREVERB_MAX_PRESET
// =============================================================*/

STDMETHODIMP
CA3dReverb::SetReverbPreset(DWORD dwEnvPreset)
{
HRESULT	hr;

	hr = S_OK;

	if (dwEnvPreset < 0 || dwEnvPreset > A3DREVERB_MAX_PRESET)
	{
		TRACE("Invalid parameter range propchange bit %x\n", A3DREVERB_CHANGED_ENVIRONMENT);
		return (E_INVALIDARG);
	}
	else if (dwEnvPreset != m_Properties.uval.preset.dwEnvPreset)
	{
		m_Properties.uval.preset.dwEnvPreset = dwEnvPreset;
		m_dwChangedPending |= A3DREVERB_CHANGED_ENVIRONMENT;
		m_dwChangedEver |= A3DREVERB_CHANGED_ENVIRONMENT;
		m_Properties.dwType = A3DREVERB_TYPE_PRESET;
	}

	return (hr);
}

/* =============================================================
// GetReverbPreset()
// (RE) rtl:0x1000B510; dbg:0x1001C8F0
//
// Read the preset number.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dReverb::GetReverbPreset(DWORD *pdwEnvPreset)
{
	if (!pdwEnvPreset)
	{
		DBGSTR("CA3dReverb::GetReverbPreset() - pdwEnvPreset is NULL.\n");

		return (E_POINTER);
	}

	ASSERT((pdwEnvPreset != 0 &&
	       !IsBadReadPtr(pdwEnvPreset, sizeof(DWORD))));

	*pdwEnvPreset = m_Properties.uval.preset.dwEnvPreset;

	return (S_OK);
}

/* =============================================================
// SetPresetVolume()
// (RE) rtl:0x1000B540; dbg:0x1001C9B0
//
// Set preset volume and mark a changed value.
//
// Returns:
//   S_OK
//   E_INVALIDARG  outside [0,1]
// =============================================================*/

STDMETHODIMP
CA3dReverb::SetPresetVolume(A3DVAL fVolume)
{
HRESULT	hr;

	hr = S_OK;

	if (fVolume < A3D_REVERB_PRESET_VOLUME_MIN || fVolume > A3D_REVERB_PRESET_VOLUME_MAX)
	{
		TRACE("Invalid parameter range propchange bit %x\n", A3DREVERB_CHANGED_VOLUME);
		return (E_INVALIDARG);
	}
	else if (fVolume != m_Properties.uval.preset.fVolume)
	{
		m_Properties.uval.preset.fVolume = fVolume;
		m_dwChangedPending |= A3DREVERB_CHANGED_VOLUME;
		m_dwChangedEver |= A3DREVERB_CHANGED_VOLUME;
		m_Properties.dwType = A3DREVERB_TYPE_PRESET;
	}

	return (hr);
}

/* =============================================================
// GetPresetVolume()
// (RE) rtl:0x1000B5B0; dbg:0x1001CA90
//
// Read preset volume.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dReverb::GetPresetVolume(A3DVAL *pfVolume)
{
	if (!pfVolume)
	{
		DBGSTR("CA3dReverb::GetPresetVolume() - pfVolume is NULL.\n");

		return (E_POINTER);
	}

	ASSERT((pfVolume != 0 && !IsBadReadPtr(pfVolume, sizeof(A3DVAL))));

	*pfVolume = m_Properties.uval.preset.fVolume;

	return (S_OK);
}

/* =============================================================
// SetPresetDecayTime()
// (RE) rtl:0x1000B5E0; dbg:0x1001CB50
//
// Set preset decay time and mark a changed value.
//
// Returns:
//   S_OK
//   E_INVALIDARG  outside [0.1,100] seconds
// =============================================================*/

STDMETHODIMP
CA3dReverb::SetPresetDecayTime(A3DVAL fDecayTime)
{
HRESULT	hr;

	hr = S_OK;

	if (fDecayTime < A3D_REVERB_PRESET_DECAY_TIME_MIN || fDecayTime > A3D_REVERB_PRESET_DECAY_TIME_MAX)
	{
		TRACE("Invalid parameter range propchange bit %x\n", A3DREVERB_CHANGED_DECAYTIME);
		return (E_INVALIDARG);
	}
	else if (fDecayTime != m_Properties.uval.preset.fDecayTime)
	{
		m_Properties.uval.preset.fDecayTime = fDecayTime;
		m_dwChangedPending |= A3DREVERB_CHANGED_DECAYTIME;
		m_dwChangedEver |= A3DREVERB_CHANGED_DECAYTIME;
		m_Properties.dwType = A3DREVERB_TYPE_PRESET;
	}

	return (hr);
}

/* =============================================================
// GetPresetDecayTime()
// (RE) rtl:0x1000B650; dbg:0x1001CC30
//
// Read preset decay time in seconds.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dReverb::GetPresetDecayTime(A3DVAL *pfDecayTime)
{
	if (!pfDecayTime)
	{
		DBGSTR("CA3dReverb::GetPresetDecayTime() - pfDecayTime is NULL.\n");

		return (E_POINTER);
	}

	ASSERT((pfDecayTime != 0 &&
	       !IsBadReadPtr(pfDecayTime, sizeof(A3DVAL))));

	*pfDecayTime = m_Properties.uval.preset.fDecayTime;

	return (S_OK);
}

/* =============================================================
// SetPresetDamping()
// (RE) rtl:0x1000B680; dbg:0x1001CCF0
//
// Set preset damping and mark a changed value.
//
// Returns:
//   S_OK
//   E_INVALIDARG  outside [0.1,100]
// =============================================================*/

STDMETHODIMP
CA3dReverb::SetPresetDamping(A3DVAL fDamping)
{
HRESULT	hr;

	hr = S_OK;

	if (fDamping < A3D_REVERB_PRESET_DAMPING_MIN || fDamping > A3D_REVERB_PRESET_DAMPING_MAX)
	{
		TRACE("Invalid parameter range propchange bit %x\n", A3DREVERB_CHANGED_DAMPING);
		return (E_INVALIDARG);
	}
	else if (fDamping != m_Properties.uval.preset.fDamping)
	{
		m_Properties.uval.preset.fDamping = fDamping;
		m_dwChangedPending |= A3DREVERB_CHANGED_DAMPING;
		m_dwChangedEver |= A3DREVERB_CHANGED_DAMPING;
		m_Properties.dwType = A3DREVERB_TYPE_PRESET;
	}

	return (hr);
}

/* =============================================================
// GetPresetDamping()
// (RE) rtl:0x1000B6F0; dbg:0x1001CDD0
//
// Read preset damping.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dReverb::GetPresetDamping(A3DVAL *pfDamping)
{
	if (!pfDamping)
	{
		DBGSTR("CA3dReverb::GetPresetDamping() - pfDamping is NULL.\n");

		return (E_POINTER);
	}

	*pfDamping = m_Properties.uval.preset.fDamping;

	return (S_OK);
}
