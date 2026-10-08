/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * geometry_comparison_tests.cpp - Interface comparison: IA3dGeom2.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling. See tests/README.md.
 *
 * Compare IA3dGeom2 modes, reflection scales, object factories and matrix
 * stack operations through the shared comparison fixture.
 *
 *---------------------------------------------------------------------------
 */

#include "comparison_fixture.hpp"

#include <cstring>

using namespace a3ddiff;

namespace {

/* -- getters ---------------------------------------------------------- */

void s_geom_getocclusion(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	DWORD d = 0xFFFFFFFF;
	o->hr = pG->GetOcclusionMode(&d);
	std::snprintf(o->detail, sizeof o->detail, "%lu", d);
	pG->Release();
}

void s_geom_getreflection(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	DWORD d = 0xFFFFFFFF;
	o->hr = pG->GetReflectionMode(&d);
	std::snprintf(o->detail, sizeof o->detail, "%lu", d);
	pG->Release();
}

void s_geom_getgainscale(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	A3DVAL f = -1.0f;
	o->hr = pG->GetReflectionGainScale(&f);
	std::snprintf(o->detail, sizeof o->detail, "%.3f", (double) f);
	pG->Release();
}

void s_geom_getdelayscale(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	A3DVAL f = -1.0f;
	o->hr = pG->GetReflectionDelayScale(&f);
	std::snprintf(o->detail, sizeof o->detail, "%.3f", (double) f);
	pG->Release();
}

void s_geom_openingrefusal(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	HRESULT hf = pG->SetOpeningFactorf(0.5f);
	o->hr = pG->SetOpeningFactorfv(NULL);
	std::snprintf(o->detail, sizeof o->detail, "f=0x%08lX", (unsigned long) hf);
	pG->Release();
}

/* -- factories: NewList and NewMaterial, and the property-set QI ------- */

void s_geom_newlist(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	LPA3DLIST pList = NULL;
	o->hr = pG->NewList(&pList);
	std::snprintf(o->detail, sizeof o->detail, "%s", pList ? "obj" : "null");
	if (pList)
		((IUnknown *) pList)->Release();
	pG->Release();
}

void s_geom_newmaterial(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	LPA3DMATERIAL pMat = NULL;
	o->hr = pG->NewMaterial(&pMat);
	std::snprintf(o->detail, sizeof o->detail, "%s", pMat ? "obj" : "null");
	if (pMat)
		((IUnknown *) pMat)->Release();
	pG->Release();
}

/* -- matrix stack: load, transform, read back ------------------------- */

void s_geom_identity(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	pG->LoadIdentity();
	A3DMATRIX m;
	std::memset(m, 0, sizeof m);
	o->hr = pG->GetMatrix(m);
	std::snprintf(o->detail, sizeof o->detail, "%.1f,%.1f,%.1f,%.1f",
		      (double) m[0], (double) m[5], (double) m[10], (double) m[15]);
	pG->Release();
}

void s_geom_translate(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	pG->LoadIdentity();
	pG->Translate3f(1.5f, 2.5f, 3.5f);
	A3DMATRIX m;
	std::memset(m, 0, sizeof m);
	o->hr = pG->GetMatrix(m);
	std::snprintf(o->detail, sizeof o->detail,
		      "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f",
		      (double) m[3], (double) m[7], (double) m[11],
		      (double) m[12], (double) m[13], (double) m[14]);
	pG->Release();
}

void s_geom_scale(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	pG->LoadIdentity();
	pG->Scale3f(2.0f, 3.0f, 4.0f);
	A3DMATRIX m;
	std::memset(m, 0, sizeof m);
	o->hr = pG->GetMatrix(m);
	std::snprintf(o->detail, sizeof o->detail, "%.2f,%.2f,%.2f",
		      (double) m[0], (double) m[5], (double) m[10]);
	pG->Release();
}

void s_geom_pushpop(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	pG->LoadIdentity();
	ULONG dPush = pG->PushMatrix();
	ULONG dPop = pG->PopMatrix();
	o->hr = S_OK;
	std::snprintf(o->detail, sizeof o->detail, "push=%lu pop=%lu",
		      (unsigned long) dPush, (unsigned long) dPop);
	pG->Release();
}

/* -- reflection-scale set/get roundtrips ------------------------------ */

void s_geom_gainscale_rt(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	HRESULT hs = pG->SetReflectionGainScale(0.5f);
	A3DVAL f = -1.0f;
	o->hr = pG->GetReflectionGainScale(&f);
	std::snprintf(o->detail, sizeof o->detail, "set=0x%08lX get=%.3f",
		      (unsigned long) hs, (double) f);
	pG->Release();
}

void s_geom_delayscale_rt(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	HRESULT hs = pG->SetReflectionDelayScale(0.5f);
	A3DVAL f = -1.0f;
	o->hr = pG->GetReflectionDelayScale(&f);
	std::snprintf(o->detail, sizeof o->detail, "set=0x%08lX get=%.3f",
		      (unsigned long) hs, (double) f);
	pG->Release();
}

/* -- modes and intervals ---------------------------------------------- */



void s_geom_occlmode_rt(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	DWORD d0 = 0;
	pG->GetOcclusionMode(&d0);
	HRESULT hs = pG->SetOcclusionMode(d0);
	DWORD d1 = 0xFFFFFFFF;
	o->hr = pG->GetOcclusionMode(&d1);
	std::snprintf(o->detail, sizeof o->detail, "set=0x%08lX get=%lu",
		      (unsigned long) hs, d1);
	pG->Release();
}

void s_geom_reflmode_rt(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	DWORD d0 = 0;
	pG->GetReflectionMode(&d0);
	HRESULT hs = pG->SetReflectionMode(d0);
	DWORD d1 = 0xFFFFFFFF;
	o->hr = pG->GetReflectionMode(&d1);
	std::snprintf(o->detail, sizeof o->detail, "set=0x%08lX get=%lu",
		      (unsigned long) hs, d1);
	pG->Release();
}

void s_geom_isenabled(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	/* IsEnabled returns the flag state directly, not an HRESULT. */
	BOOL enabled = pG->IsEnabled(A3D_1ST_REFLECTIONS);
	o->hr = S_OK;
	std::snprintf(o->detail, sizeof o->detail, "%d", enabled ? 1 : 0);
	pG->Release();
}

void s_geom_loadmatrix(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	A3DMATRIX m;
	for (int i = 0; i < 16; ++i)
		m[i] = (A3DVAL) (i + 1);
	pG->LoadMatrix(m);
	A3DMATRIX r;
	std::memset(r, 0, sizeof r);
	o->hr = pG->GetMatrix(r);
	std::snprintf(o->detail, sizeof o->detail, "%.1f,%.1f,%.1f,%.1f",
		      (double) r[0], (double) r[5], (double) r[10], (double) r[15]);
	pG->Release();
}

void s_geom_rotate(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	pG->LoadIdentity();
	pG->Rotate3f(90.0f, 0.0f, 1.0f, 0.0f);
	A3DMATRIX m;
	std::memset(m, 0, sizeof m);
	o->hr = pG->GetMatrix(m);
	std::snprintf(o->detail, sizeof o->detail, "%.3f,%.3f,%.3f,%.3f",
		      (double) m[0], (double) m[2], (double) m[8], (double) m[10]);
	pG->Release();
}

/* -- the slot 42..49 surface ------------------------------------------- */

/* Set then get; constructor does not initialize the field. Invalid case is a bit outside A3D_1ST_REFLECTIONS|A3D_OCCLUSIONS. */
void s_geom_rendermode_rt(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	HRESULT hs  = pG->SetRenderMode(A3D_1ST_REFLECTIONS | A3D_OCCLUSIONS);
	DWORD	d   = 0xFFFFFFFF;
	HRESULT hg  = pG->GetRenderMode(&d);
	HRESULT hb  = pG->SetRenderMode(0x80);
	HRESULT hn  = pG->GetRenderMode(NULL);

	o->hr = hg;
	std::snprintf(o->detail, sizeof o->detail,
		      "set=0x%08lX get=0x%08lX val=%lu bad=0x%08lX null=0x%08lX",
		      (unsigned long) hs, (unsigned long) hg, d,
		      (unsigned long) hb, (unsigned long) hn);
	pG->Release();
}

/* Field holds a reciprocal. Printed as raw bits and decimal to distinguish the zero case (2e-38, not 0.0, due to getter's dead store). */
void s_geom_bloat_rt(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}

	const A3DVAL	afSet[] = { 0.5f, 1.0f, 2.0f, 0.0f };
	A3DVAL		afGot[4];
	DWORD		adwBits[4];
	HRESULT		hr = S_OK;

	for (int i = 0; i < 4; i++) {
		afGot[i] = -1.0f;
		hr = pG->SetPolygonBloatFactor(afSet[i]);
		if (SUCCEEDED(hr))
			hr = pG->GetPolygonBloatFactor(&afGot[i]);
		std::memcpy(&adwBits[i], &afGot[i], sizeof adwBits[i]);
	}

	HRESULT hneg  = pG->SetPolygonBloatFactor(-1.0f);
	HRESULT hnull = pG->GetPolygonBloatFactor(NULL);

	o->hr = hr;
	std::snprintf(o->detail, sizeof o->detail,
		      "%.3f/%08lX %.3f/%08lX %.3f/%08lX %.3g/%08lX "
		      "neg=0x%08lX null=0x%08lX",
		      (double) afGot[0], (unsigned long) adwBits[0],
		      (double) afGot[1], (unsigned long) adwBits[1],
		      (double) afGot[2], (unsigned long) adwBits[2],
		      (double) afGot[3], (unsigned long) adwBits[3],
		      (unsigned long) hneg, (unsigned long) hnull);
	pG->Release();
}

/* Each pair is self-consistent; setting them to different values and reading both separates them. */
void s_geom_intervals_rt(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}

	DWORD dwRefl0 = 0xFFFFFFFF, dwOccl0 = 0xFFFFFFFF;
	pG->GetReflectionUpdateInterval(&dwRefl0);
	pG->GetOcclusionUpdateInterval(&dwOccl0);

	HRESULT hsr = pG->SetReflectionUpdateInterval(5);
	HRESULT hso = pG->SetOcclusionUpdateInterval(7);

	DWORD dwRefl = 0xFFFFFFFF, dwOccl = 0xFFFFFFFF;
	pG->GetReflectionUpdateInterval(&dwRefl);
	o->hr = pG->GetOcclusionUpdateInterval(&dwOccl);

	std::snprintf(o->detail, sizeof o->detail,
		      "def=%lu/%lu set=0x%08lX/0x%08lX got=%lu/%lu",
		      dwRefl0, dwOccl0, (unsigned long) hsr,
		      (unsigned long) hso, dwRefl, dwOccl);
	pG->Release();
}


void s_geom_intervals_refuse(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	HRESULT hzr = pG->SetReflectionUpdateInterval(0);
	HRESULT hzo = pG->SetOcclusionUpdateInterval(0);
	HRESULT hnr = pG->GetReflectionUpdateInterval(NULL);
	HRESULT hno = pG->GetOcclusionUpdateInterval(NULL);

	o->hr = hzr;
	std::snprintf(o->detail, sizeof o->detail,
		      "zero=0x%08lX/0x%08lX null=0x%08lX/0x%08lX",
		      (unsigned long) hzr, (unsigned long) hzo,
		      (unsigned long) hnr, (unsigned long) hno);
	pG->Release();
}

}	/* namespace */

namespace a3ddiff {

void add_geometry_steps(Script &s)
{
	s.push_back({ "Geom GetOcclusionMode",  s_geom_getocclusion });
	s.push_back({ "Geom GetReflectionMode", s_geom_getreflection });
	s.push_back({ "Geom GetReflGainScale",  s_geom_getgainscale });
	s.push_back({ "Geom GetReflDelayScale", s_geom_getdelayscale });

	s.push_back({ "Geom NewList",       s_geom_newlist });
	s.push_back({ "Geom OpeningRefusal", s_geom_openingrefusal });
	s.push_back({ "Geom NewMaterial",   s_geom_newmaterial });

	s.push_back({ "Geom Identity",  s_geom_identity });
	s.push_back({ "Geom Translate", s_geom_translate });
	s.push_back({ "Geom Scale",     s_geom_scale });
	s.push_back({ "Geom PushPop",   s_geom_pushpop });

	s.push_back({ "Geom SetGetGainScale",  s_geom_gainscale_rt });
	s.push_back({ "Geom SetGetDelayScale", s_geom_delayscale_rt });

	s.push_back({ "Geom LoadGetMatrix", s_geom_loadmatrix });
	s.push_back({ "Geom Rotate",        s_geom_rotate });

	s.push_back({ "Geom SetGetOcclMode", s_geom_occlmode_rt });
	s.push_back({ "Geom SetGetReflMode", s_geom_reflmode_rt });
	s.push_back({ "Geom IsEnabled",      s_geom_isenabled });

	s.push_back({ "Geom SetGetRenderMode", s_geom_rendermode_rt });
	s.push_back({ "Geom SetGetBloat",      s_geom_bloat_rt });
	s.push_back({ "Geom SetGetIntervals",  s_geom_intervals_rt });
	s.push_back({ "Geom IntervalRefusals", s_geom_intervals_refuse });
}

}	/* namespace a3ddiff */

TEST_F(InterfaceVsReference, GeometryGetters)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Geom GetOcclusionMode");
	expect_parity("Geom GetReflectionMode");
	expect_parity("Geom GetReflGainScale");
	expect_parity("Geom GetReflDelayScale");
}

TEST_F(InterfaceVsReference, GeometrySetGet)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Geom SetGetGainScale");
	expect_parity("Geom SetGetDelayScale");
}


TEST_F(InterfaceVsReference, GeometryMatrixStack)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Geom Identity");
	expect_parity("Geom Translate");
	expect_parity("Geom Scale");
	expect_parity("Geom PushPop");
	expect_parity("Geom LoadGetMatrix");
	expect_parity("Geom Rotate");
}

TEST_F(InterfaceVsReference, GeometryModes)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Geom SetGetOcclMode");
	expect_parity("Geom SetGetReflMode");
	expect_parity("Geom IsEnabled");
}

/* Bloat step compares raw float bits exactly, not to three decimals. */
TEST_F(InterfaceVsReference, GeometryRenderModeAndIntervals)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Geom SetGetRenderMode");
	expect_parity("Geom SetGetBloat");
	expect_parity("Geom SetGetIntervals");
	expect_parity("Geom IntervalRefusals");
}

TEST_F(InterfaceVsReference, GeometryFactories)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Geom NewList");
	expect_parity("Geom NewMaterial");
	expect_parity("Geom OpeningRefusal");
}
