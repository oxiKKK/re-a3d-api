/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * MaterialObject.h
 *
 * Declares CA3dMaterial, the IA3dMaterial implementation for acoustic
 * surface properties. It combines naming and material-link bases with
 * reflectance, transmittance, derived gain curves and preset state.
 *
 * The root's geometry traversal reads these values to attenuate
 * transmitted and reflected sound. MaterialObject.cpp implements property
 * access and curve evaluation, while MaterialLink.h supplies the binding
 * used by geometry objects.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _MATERIALOBJECT_H
#define _MATERIALOBJECT_H

#include "A3dPrivate.h"
#include "NamedObject.h"
#include "MaterialLink.h"

/* =============================================================
// Class: CA3dMaterial
//
// Description: Named acoustic material with reflection and transmission gains.
//
// Size: 0x134
//
// (RE) Constructor: rtl:0x10009750; dbg:0x10018ee0
// =============================================================*/

/* IA3dMaterial at 0x00: dbg:0x10127e24; rtl:0x10052300.
 * Name access occupies slots 8/9. CA3dNamed at 0x04 and CA3dLink at 0x108
 * each have a one-slot destructor table. */

class CA3dMaterial : public IA3dMaterial,
                     public CA3dNamed,
                     public CA3dLink
{
public:
	CA3dMaterial(void);
	CA3dMaterial(const CA3dMaterial *pFrom);
	virtual ~CA3dMaterial(void);

	STDMETHODIMP        QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG) AddRef(void);
	STDMETHODIMP_(ULONG) Release(void);

	STDMETHODIMP Load(LPSTR pszFile);
	STDMETHODIMP Save(LPSTR pszFile);
	STDMETHODIMP UnSerialize(LPVOID pvData, UINT cbData);
	STDMETHODIMP Serialize(LPVOID *ppvData, UINT *pcbData);
	STDMETHODIMP Duplicate(LPA3DMATERIAL *ppMaterial);

	STDMETHODIMP SetNameID(LPSTR pszName);
	STDMETHODIMP GetNameID(LPSTR pszName, INT cbName);

	STDMETHODIMP SelectPreset(DWORD dwPreset);
	STDMETHODIMP GetClosestPreset(LPDWORD pdwPreset);

	STDMETHODIMP SetReflectance(A3DVAL fGain, A3DVAL fHFGain);
	STDMETHODIMP GetReflectance(LPA3DVAL pfGain, LPA3DVAL pfHFGain);
	STDMETHODIMP SetTransmittance(A3DVAL fGain, A3DVAL fHFGain);
	STDMETHODIMP GetTransmittance(LPA3DVAL pfGain, LPA3DVAL pfHFGain);

	friend class CA3dRoot;

protected:

	/* 0x110 */ DWORD m_Unknown_0x110;

	/* 0x114 */ LONG m_cRef;

	/* Broadband gains and polynomial high-frequency curves. */
	/* 0x118 */ A3DVAL m_fReflectGain;
	/* 0x11C */ A3DVAL m_fReflectCurve;
	/* 0x120 */ A3DVAL m_fTransmitGain;
	/* 0x124 */ A3DVAL m_fTransmitCurve;

	/* Original high-frequency gains; uninitialized until set. */
	/* 0x128 */ A3DVAL m_fReflectHF;
	/* 0x12C */ A3DVAL m_fTransmitHF;

	/* 0x130 */ int m_nPreset;		/* -1, meaning none */
};

#endif /* _MATERIALOBJECT_H */
