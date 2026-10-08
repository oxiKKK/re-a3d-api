/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * MaterialObject.cpp
 *
 * Implements CA3dMaterial, the surface properties used by acoustic
 * tracing. Reflectance and transmittance setters store broadband and
 * high-frequency gains and derive the curves evaluated when sound meets a
 * surface.
 *
 * The object also provides shared naming and reference-counted lifetime.
 * Geometry builders and placed objects bind materials through CA3dLink.
 * Persistence and preset selection entry points are retained but
 * unsupported in the current implementation.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "MaterialObject.h"

static A3DVAL A3dMaterialCurve(A3DVAL fHFGain);

/* Constructor and gain setters clear the preset. *  */
#define A3D_MATERIAL_NO_PRESET	(-1)

/* Process-wide default-name sequence for materials. */

static DWORD g_cMaterials = 0;

/* =============================================================
// CA3dMaterial()
// (RE) rtl:0x10009750; dbg:0x10018ee0
//
// Initialize full reflectance, zero transmittance and a default name. Preserve
// uninitialized high-frequency gains until the corresponding setters run.
// =============================================================*/

CA3dMaterial::CA3dMaterial(void)
{
	m_cRef = 0;

	m_fReflectGain   = 1.0f;
	m_fReflectCurve  = 1.0f;
	m_fTransmitGain  = 0.0f;
	m_fTransmitCurve = 0.0f;

	m_nPreset = A3D_MATERIAL_NO_PRESET;

	SetName("Material", ++g_cMaterials);
}

/* =============================================================
// SetNameID()
// (RE) rtl:0x10009830; dbg:0x100190c0
//
// Copy 256 readable bytes from pszName; no terminator is required.
//
// Returns: The SetNameBuffer result.
// =============================================================*/

STDMETHODIMP
CA3dMaterial::SetNameID(LPSTR pszName)
{
	return (SetNameBuffer(pszName));
}

/* =============================================================
// GetNameID()
// (RE) rtl:0x10009850; dbg:0x100190f0
//
// Copy 256 bytes to pszName; cbName is ignored by the original.
//
// Returns: The GetNameBuffer result.
// =============================================================*/

STDMETHODIMP
CA3dMaterial::GetNameID(LPSTR pszName, INT cbName)
{
	return (GetNameBuffer(pszName, cbName));
}

/* =============================================================
// CA3dMaterial()
// (RE) rtl:0x100098a0; dbg:0x100191a0
//
// Copy material coefficients and preset, leaving the bases and count empty.
// =============================================================*/

CA3dMaterial::CA3dMaterial(const CA3dMaterial *pFrom)
{
	m_cRef = 0;

	CopyMemory(&m_fReflectGain, &pFrom->m_fReflectGain, 7 * sizeof(A3DVAL));
}

/* =============================================================
// CA3dMaterial::~CA3dMaterial() scalar deleting destructor
// (RE) rtl:0x10009870; dbg:0x10019120
// =============================================================*/

/* =============================================================
// CA3dMaterial subobject destructor
// (RE) dbg:0x10019170
// =============================================================*/

/* =============================================================
// ~CA3dMaterial()
// (RE) rtl:0x10009940; dbg:0x100192a0
//
// Destroy the material and its name and material-link bases.
// =============================================================*/

CA3dMaterial::~CA3dMaterial(void)
{
}

/* =============================================================
// QueryInterface()
// (RE) rtl:0x100099c0; dbg:0x10019380
//
// Preserve the original successful query without writing an interface
// or taking a reference.
//
// Returns:
//   E_INVALIDARG  a null output
//   S_OK          otherwise
// =============================================================*/

STDMETHODIMP
CA3dMaterial::QueryInterface(REFIID riid, void **ppv)
{
	if (!ppv)
		return (E_INVALIDARG);

	return (S_OK);
}

/* =============================================================
// AddRef()
// (RE) rtl:0x100099e0; dbg:0x100193b0
//
// Increment the reference count.
//
// Returns: The updated reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dMaterial::AddRef(void)
{
	return (++m_cRef);
}

/* =============================================================
// Release()
// (RE) rtl:0x10009a00; dbg:0x100193f0
//
// Release a reference and delete the material at zero.
//
// Returns: The remaining reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dMaterial::Release(void)
{
	if (--m_cRef)
		return (m_cRef);

	delete this;

	return (0);
}

/* =============================================================
// SetReflectance()
// (RE) rtl:0x10009a30; dbg:0x10019480
//
// Store reflectance gains and their derived curve, clearing the preset.
//
// Returns:
//   S_OK
//   E_INVALIDARG  either gain outside [0,1]
// =============================================================*/

STDMETHODIMP
CA3dMaterial::SetReflectance(A3DVAL fGain, A3DVAL fHFGain)
{
	if (fGain < 0.0f || fGain > 1.0f)
		return (E_INVALIDARG);

	if (fHFGain < 0.0f || fHFGain > 1.0f)
		return (E_INVALIDARG);

	m_fReflectGain  = fGain;
	m_fReflectCurve = A3dMaterialCurve(fHFGain);

	m_fReflectHF = fHFGain;

	m_nPreset = A3D_MATERIAL_NO_PRESET;

	return (S_OK);
}

/* =============================================================
// GetReflectance()
// (RE) rtl:0x10009b50; dbg:0x10019650
//
// Read broadband and high-frequency reflectance.
//
// Returns:
//   S_OK
//   E_POINTER  if either output is null
// =============================================================*/

STDMETHODIMP
CA3dMaterial::GetReflectance(LPA3DVAL pfGain, LPA3DVAL pfHFGain)
{
	if (!pfGain)
		return (E_POINTER);

	if (!pfHFGain)
		return (E_POINTER);

	*pfGain   = m_fReflectGain;
	*pfHFGain = m_fReflectHF;

	return (S_OK);
}

/* =============================================================
// SetTransmittance()
// (RE) rtl:0x10009b90; dbg:0x100196b0
//
// Store transmittance gains and their derived curve, clearing the preset.
//
// Returns:
//   S_OK
//   E_INVALIDARG  either gain outside [0,1]
// =============================================================*/

STDMETHODIMP
CA3dMaterial::SetTransmittance(A3DVAL fGain, A3DVAL fHFGain)
{
	if (fGain < 0.0f || fGain > 1.0f)
		return (E_INVALIDARG);

	if (fHFGain < 0.0f || fHFGain > 1.0f)
		return (E_INVALIDARG);

	m_fTransmitGain  = fGain;
	m_fTransmitCurve = A3dMaterialCurve(fHFGain);

	m_fTransmitHF = fHFGain;

	m_nPreset = A3D_MATERIAL_NO_PRESET;

	return (S_OK);
}

/* =============================================================
// GetTransmittance()
// (RE) rtl:0x10009cb0; dbg:0x10019880
//
// Read broadband and high-frequency transmittance.
//
// Returns:
//   S_OK
//   E_POINTER  if either output is null
// =============================================================*/

STDMETHODIMP
CA3dMaterial::GetTransmittance(LPA3DVAL pfGain, LPA3DVAL pfHFGain)
{
	if (!pfGain)
		return (E_POINTER);

	if (!pfHFGain)
		return (E_POINTER);

	*pfGain   = m_fTransmitGain;
	*pfHFGain = m_fTransmitHF;

	return (S_OK);
}

typedef int A3dMaterialSizeCheck[(sizeof(CA3dMaterial) == 0x134) ? 1 : -1];

/* =============================================================
// Load()
// (RE) rtl:0x10009810; dbg:0x10019020
//
// Reject material loading.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dMaterial::Load(LPSTR pszFile)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// Save()
// (RE) rtl:0x10009810; dbg:0x10019040
//
// Reject material saving.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dMaterial::Save(LPSTR pszFile)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// UnSerialize()
// (RE) rtl:0x10009820; dbg:0x10019060
//
// Reject material deserialization.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dMaterial::UnSerialize(LPVOID pvData, UINT cbData)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// Serialize()
// (RE) rtl:0x10009820; dbg:0x10019080
//
// Reject material serialization.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dMaterial::Serialize(LPVOID *ppvData, UINT *pcbData)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// Duplicate()
// (RE) rtl:0x10009810; dbg:0x100190a0
//
// Reject material duplication.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dMaterial::Duplicate(LPA3DMATERIAL *ppMaterial)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// SelectPreset()
// (RE) rtl:0x10009cf0; dbg:0x100198e0
//
// Reject preset selection.
//
// Returns: A3DERROR_UNIMPLEMENTED_FUNCTION.
// =============================================================*/

STDMETHODIMP
CA3dMaterial::SelectPreset(DWORD dwPreset)
{
	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// GetClosestPreset()
// (RE) rtl:0x10009d00; dbg:0x10019900
//
// Write the current preset while reporting the operation as unimplemented.
//
// Returns:
//   E_POINTER                        a null output
//   A3DERROR_UNIMPLEMENTED_FUNCTION  otherwise
// =============================================================*/

STDMETHODIMP
CA3dMaterial::GetClosestPreset(LPDWORD pdwPreset)
{
	if (!pdwPreset)
		return (E_POINTER);

	*pdwPreset = (DWORD) m_nPreset;

	return (A3DERROR_UNIMPLEMENTED_FUNCTION);
}

/* =============================================================
// A3dMaterialCurve()
//
// Evaluate the high-frequency gain curve.
//
// Returns: The curve value clamped to [0,1].
// =============================================================*/

static A3DVAL
A3dMaterialCurve(A3DVAL fHFGain)
{
A3DVAL f2, f3, f4, f5, f6, f7;
A3DVAL fCurve;

	f2 = fHFGain * fHFGain;
	f3 = fHFGain * f2;
	f4 = fHFGain * f3;
	f5 = fHFGain * f4;
	f6 = fHFGain * f5;
	f7 = fHFGain * f6;

	fCurve = fHFGain * 3.7264376f
	       - f2 * 17.449827f
	       + f3 * 43.63063f
	       - f4 * 52.999165f
	       + f5 * 27.087683f
	       + f6 * 0.77741671f
	       - f7 * 3.7731748f;

	if (fCurve < 0.0f)
		return (0.0f);

	if (fCurve > 1.0f)
		return (1.0f);

	return (fCurve);
}
