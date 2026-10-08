/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * material_comparison_tests.cpp - Interface comparison: IA3dMaterial and IA3dList.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling. See tests/README.md.
 *
 * Material and list objects made off IA3dGeom2 factory: reflectance/transmittance roundtrips,
 * closest preset, IA3dList Begin/End. Harness and fixture in comparison_fixture.hpp.
 *
 *---------------------------------------------------------------------------
 */

#include "comparison_fixture.hpp"

#include <cstring>

using namespace a3ddiff;

namespace {

void s_mat_reflectance_rt(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	IA3dMaterial *pMat = NULL;
	HRESULT hc = pG->NewMaterial(&pMat);
	if (FAILED(hc) || !pMat) {
		o->hr = hc;
		std::snprintf(o->detail, sizeof o->detail, "no-mat");
		pG->Release();
		return;
	}
	pMat->SetReflectance(0.5f, 0.3f);
	A3DVAL a = -1.0f, b = -1.0f;
	o->hr = pMat->GetReflectance(&a, &b);
	std::snprintf(o->detail, sizeof o->detail, "%.3f,%.3f", (double) a, (double) b);
	pMat->Release();
	pG->Release();
}

void s_mat_transmittance_rt(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	IA3dMaterial *pMat = NULL;
	HRESULT hc = pG->NewMaterial(&pMat);
	if (FAILED(hc) || !pMat) {
		o->hr = hc;
		std::snprintf(o->detail, sizeof o->detail, "no-mat");
		pG->Release();
		return;
	}
	pMat->SetTransmittance(0.5f, 0.3f);
	A3DVAL a = -1.0f, b = -1.0f;
	o->hr = pMat->GetTransmittance(&a, &b);
	std::snprintf(o->detail, sizeof o->detail, "%.3f,%.3f", (double) a, (double) b);
	pMat->Release();
	pG->Release();
}

void s_mat_preset(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	IA3dMaterial *pMat = NULL;
	HRESULT hc = pG->NewMaterial(&pMat);
	if (FAILED(hc) || !pMat) {
		o->hr = hc;
		std::snprintf(o->detail, sizeof o->detail, "no-mat");
		pG->Release();
		return;
	}
	DWORD d = 0xFFFFFFFF;
	o->hr = pMat->GetClosestPreset(&d);
	std::snprintf(o->detail, sizeof o->detail, "%lu", d);
	pMat->Release();
	pG->Release();
}

void s_list_beginend(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	IA3dList *pList = NULL;
	HRESULT hc = pG->NewList(&pList);
	if (FAILED(hc) || !pList) {
		o->hr = hc;
		std::snprintf(o->detail, sizeof o->detail, "no-list");
		pG->Release();
		return;
	}
	HRESULT hb = pList->Begin();
	o->hr = pList->End();
	std::snprintf(o->detail, sizeof o->detail, "begin=0x%08lX", (unsigned long) hb);
	pList->Release();
	pG->Release();
}

void s_list_call(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	IA3dList *pList = NULL;
	HRESULT hc = pG->NewList(&pList);
	if (FAILED(hc) || !pList) {
		o->hr = hc;
		std::snprintf(o->detail, sizeof o->detail, "no-list");
		pG->Release();
		return;
	}
	pList->Begin();
	pList->End();
	o->hr = pList->Call();
	o->detail[0] = 0;
	pList->Release();
	pG->Release();
}

void s_list_boundingvol(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	IA3dList *pList = NULL;
	HRESULT hc = pG->NewList(&pList);
	if (FAILED(hc) || !pList) {
		o->hr = hc;
		std::snprintf(o->detail, sizeof o->detail, "no-list");
		pG->Release();
		return;
	}
	o->hr = pList->EnableBoundingVol();
	o->detail[0] = 0;
	pList->Release();
	pG->Release();
}

/* Display-list recording increments a counter on the root; Clear() returns
   E_FAIL while it is nonzero. Ending one of 2 recording lists must leave
   the restriction active. */

void s_list_recording_gate(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	IA3dList *pList = NULL;
	HRESULT hc = pG->NewList(&pList);
	if (FAILED(hc) || !pList) {
		o->hr = hc;
		std::snprintf(o->detail, sizeof o->detail, "no-list");
		pG->Release();
		return;
	}
	HRESULT hIdle = c->pRoot->Clear();
	HRESULT hb    = pList->Begin();
	HRESULT hOpen = c->pRoot->Clear();
	pList->End();
	o->hr = c->pRoot->Clear();
	std::snprintf(o->detail, sizeof o->detail,
		      "idle=0x%08lX begin=0x%08lX open=0x%08lX",
		      (unsigned long) hIdle, (unsigned long) hb,
		      (unsigned long) hOpen);
	pList->Release();
	pG->Release();
}

void s_list_recording_nested(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	IA3dList *pA = NULL, *pB = NULL;
	HRESULT ha = pG->NewList(&pA);
	HRESULT hb = pG->NewList(&pB);
	if (FAILED(ha) || !pA || FAILED(hb) || !pB) {
		o->hr = FAILED(ha) ? ha : hb;
		std::snprintf(o->detail, sizeof o->detail, "no-list");
		if (pA) pA->Release();
		if (pB) pB->Release();
		pG->Release();
		return;
	}
	pA->Begin();
	pB->Begin();
	HRESULT hBoth = c->pRoot->Clear();
	pA->End();
	HRESULT hOne = c->pRoot->Clear();
	pB->End();
	o->hr = c->pRoot->Clear();
	std::snprintf(o->detail, sizeof o->detail, "both=0x%08lX one=0x%08lX",
		      (unsigned long) hBoth, (unsigned long) hOne);
	pA->Release();
	pB->Release();
	pG->Release();
}

/* -- IA3dMaterial builder methods: preset, name, duplicate ------------ */

void s_mat_selectpreset_rt(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	IA3dMaterial *pMat = NULL;
	HRESULT hc = pG->NewMaterial(&pMat);
	if (FAILED(hc) || !pMat) {
		o->hr = hc;
		std::snprintf(o->detail, sizeof o->detail, "no-mat");
		pG->Release();
		return;
	}
	HRESULT hs = pMat->SelectPreset(0);
	A3DVAL a = -1.0f, b = -1.0f;
	o->hr = pMat->GetReflectance(&a, &b);
	std::snprintf(o->detail, sizeof o->detail, "set=0x%08lX %.3f,%.3f",
		      (unsigned long) hs, (double) a, (double) b);
	pMat->Release();
	pG->Release();
}

void s_mat_nameid_rt(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	IA3dMaterial *pMat = NULL;
	HRESULT hc = pG->NewMaterial(&pMat);
	if (FAILED(hc) || !pMat) {
		o->hr = hc;
		std::snprintf(o->detail, sizeof o->detail, "no-mat");
		pG->Release();
		return;
	}
	/* SetNameBuffer/GetNameBuffer copy fixed A3D_NAME_LENGTH (256) bytes, ignore size argument.
	   Defect reproduced from binary (NamedObject.cpp, dbg:0x1003d850). Both buffers are full size. */
	char name[256];
	std::memset(name, 0, sizeof name);
	std::strcpy(name, "a3dtest");
	pMat->SetNameID(name);
	char buf[256];
	std::memset(buf, 0, sizeof buf);
	o->hr = pMat->GetNameID(buf, (int) sizeof buf);
	std::snprintf(o->detail, sizeof o->detail, "%s", buf);
	pMat->Release();
	pG->Release();
}

void s_mat_duplicate(ScenarioContext *c, StepResult *o)
{
	IA3dGeom2 *pG = qi_as<IA3dGeom2>(c->pRoot, IID_IA3dGeom2);
	if (!pG) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	IA3dMaterial *pMat = NULL;
	HRESULT hc = pG->NewMaterial(&pMat);
	if (FAILED(hc) || !pMat) {
		o->hr = hc;
		std::snprintf(o->detail, sizeof o->detail, "no-mat");
		pG->Release();
		return;
	}
	LPA3DMATERIAL pDup = NULL;
	o->hr = pMat->Duplicate(&pDup);
	std::snprintf(o->detail, sizeof o->detail, "%s", pDup ? "obj" : "null");
	if (pDup)
		((IUnknown *) pDup)->Release();
	pMat->Release();
	pG->Release();
}

}	/* namespace */

namespace a3ddiff {

void add_material_steps(Script &s)
{
	s.push_back({ "Material SetGetReflect",  s_mat_reflectance_rt });
	s.push_back({ "Material SetGetTransmit", s_mat_transmittance_rt });
	s.push_back({ "Material GetPreset",      s_mat_preset });
	s.push_back({ "Material SelectPreset",   s_mat_selectpreset_rt });
	s.push_back({ "Material NameID",         s_mat_nameid_rt });
	s.push_back({ "Material Duplicate",      s_mat_duplicate });
	s.push_back({ "List BeginEnd",           s_list_beginend });
	s.push_back({ "List Call",               s_list_call });
	s.push_back({ "List EnableBoundingVol",  s_list_boundingvol });
	s.push_back({ "List RecordingGate",      s_list_recording_gate });
	s.push_back({ "List RecordingNested",    s_list_recording_nested });
}

}	/* namespace a3ddiff */

TEST_F(InterfaceVsReference, MaterialAndList)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Material SetGetReflect");
	expect_parity("Material SetGetTransmit");
	expect_parity("Material GetPreset");
	expect_parity("List BeginEnd");
	expect_parity("List Call");
	expect_parity("List EnableBoundingVol");
	expect_parity("List RecordingGate");
	expect_parity("List RecordingNested");
}

TEST_F(InterfaceVsReference, MaterialBuilder)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Material SelectPreset");
	expect_parity("Material NameID");
	expect_parity("Material Duplicate");
}
