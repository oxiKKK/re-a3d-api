/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * A3dReverb.h
 *
 * Declares CA3dReverb and the property-change masks used by the renderer.
 * The object holds custom reverb parameters, preset selection and preset
 * volume, decay and damping settings.
 *
 * The root can bind this object and translate its changes into device
 * properties. Preset data and matching are implemented in A3dReverb.cpp,
 * while A3d3.cpp handles backend submission. Audio filtering for the
 * optional project emulation is separate in A3dReverbEmu.cpp.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _A3DREVERB_H
#define _A3DREVERB_H

#include "A3dPrivate.h"
#include "refaudbin.h"

/* (RE) Preset sender: dbg:0x100092A0; mask test dbg:0x10009368.
 * These changed bits are also sent unchanged as property IDs. */
#define A3DREVERB_CHANGED_ENVIRONMENT   0x01
#define A3DREVERB_CHANGED_VOLUME        0x02
#define A3DREVERB_CHANGED_DECAYTIME     0x04
#define A3DREVERB_CHANGED_DAMPING       0x08
#define A3DREVERB_CHANGED_PRESET_ALL    0x0F

/* (RE) Custom sender: dbg:0x10008D00; setter: dbg:0x1001BC70. */
#define A3DREVERB_CHANGED_CUSTOM_ROOM                   0x0010
#define A3DREVERB_CHANGED_CUSTOM_ROOMHF                 0x0020
#define A3DREVERB_CHANGED_CUSTOM_ROOMROLLOFFFACTOR      0x0040
#define A3DREVERB_CHANGED_CUSTOM_DECAYTIME              0x0080
#define A3DREVERB_CHANGED_CUSTOM_DECAYHFRATIO           0x0100
#define A3DREVERB_CHANGED_CUSTOM_REFLECTIONS            0x0200
#define A3DREVERB_CHANGED_CUSTOM_REFLECTIONSDELAY       0x0400
#define A3DREVERB_CHANGED_CUSTOM_REVERB                 0x0800
#define A3DREVERB_CHANGED_CUSTOM_REVERBDELAY            0x1000
#define A3DREVERB_CHANGED_CUSTOM_DIFFUSION              0x2000
#define A3DREVERB_CHANGED_CUSTOM_DENSITY                0x4000
#define A3DREVERB_CHANGED_CUSTOM_HFREFERENCE            0x8000
#define A3DREVERB_CHANGED_CUSTOM_ALL                    0xFFF0

/* (RE) Custom-to-EAX conversion trigger: dbg:0x10008B1B. */
#define A3DREVERB_CHANGED_CUSTOM_TO_EAX \
	(A3DREVERB_CHANGED_CUSTOM_ROOM | A3DREVERB_CHANGED_CUSTOM_DECAYTIME | \
	 A3DREVERB_CHANGED_CUSTOM_DECAYHFRATIO | A3DREVERB_CHANGED_CUSTOM_REFLECTIONS | \
	 A3DREVERB_CHANGED_CUSTOM_REVERB)

/* =============================================================
// Class: CA3dReverb
//
// Description: Preset or custom reverb properties with pending change masks.
//
// Size: 0x58
//
// (RE) Constructor: rtl:0x1000add0; dbg:0x1001b780
// =============================================================*/

/* IA3dReverb at 0x00: dbg:0x10127FB0; slots 0..12 in declaration order.
 * Interface base table: dbg:0x10127FF0; 13 slots of __purecall (dbg:0x1008c630).
 * CA3dChained at 0x04: dbg:0x10127FAC; slot 0 vector deleting destructor. */

class CA3dReverb : public IA3dReverb,
		   public CA3dChained
{
public:
	CA3dReverb(void);

	STDMETHODIMP		QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG)	AddRef(void);
	STDMETHODIMP_(ULONG)	Release(void);

	STDMETHODIMP	SetReverbPreset(DWORD dwEnvPreset);
	STDMETHODIMP	GetReverbPreset(DWORD *pdwEnvPreset);
	STDMETHODIMP	SetAllProperties(A3DREVERB_PROPERTIES *pReverbProp);
	STDMETHODIMP	GetAllProperties(A3DREVERB_PROPERTIES *pReverbProp);
	STDMETHODIMP	SetPresetVolume(A3DVAL fVolume);
	STDMETHODIMP	GetPresetVolume(A3DVAL *pfVolume);
	STDMETHODIMP	SetPresetDecayTime(A3DVAL fDecayTime);
	STDMETHODIMP	GetPresetDecayTime(A3DVAL *pfDecayTime);
	STDMETHODIMP	SetPresetDamping(A3DVAL fDamping);
	STDMETHODIMP	GetPresetDamping(A3DVAL *pfDamping);

	/* 0x10 */ DWORD	m_cRef;		/* 0 at construction */

	/* 60-byte block; type remains zero until selected by a setter. */
	/* 0x14 */ A3DREVERB_PROPERTIES	m_Properties;

	/* Property bits: preset 1, volume 2, decay 4, damping 8; custom fields
	 * use 0x10..0x8000 in A3DREVERB_CUSTOM declaration order. */
	/* 0x50 */ DWORD	m_dwChangedPending; /* Cleared after delivery. */
	/* 0x54 */ DWORD	m_dwChangedEver; /* Accumulated changes, resent on binding. */

	/* (RE) dbg:0x10009AB0 */
	void ClearChangedPending(void)	{ m_dwChangedPending = 0; }
};

typedef int CA3dReverbSizeCheck[(sizeof(CA3dReverb) == 0x58) ? 1 : -1];

A3DREVERB_PROPERTIES *__cdecl MapCustomToPreset(A3DREVERB_PROPERTIES *pCustomProp,
						 A3DREVERB_PROPERTIES *pPresetProp);

#endif /* _A3DREVERB_H */
