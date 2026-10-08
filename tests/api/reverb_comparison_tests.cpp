/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * reverb_comparison_tests.cpp - Interface comparison: IA3dReverb.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling. See tests/README.md.
 *
 *---------------------------------------------------------------------------
 */

#include "comparison_fixture.hpp"

#include <cstring>

using namespace a3ddiff;

namespace {

void s_reverb_getpreset(ScenarioContext *c, StepResult *o)
{
	IA3dReverb *pR = qi_as<IA3dReverb>(c->pRoot, IID_IA3dReverb);
	if (!pR) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	DWORD d = 0xFFFFFFFF;
	o->hr = pR->GetReverbPreset(&d);
	std::snprintf(o->detail, sizeof o->detail, "%lu", d);
	pR->Release();
}

void s_reverb_getvol(ScenarioContext *c, StepResult *o)
{
	IA3dReverb *pR = qi_as<IA3dReverb>(c->pRoot, IID_IA3dReverb);
	if (!pR) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	A3DVAL f = -1.0f;
	o->hr = pR->GetPresetVolume(&f);
	std::snprintf(o->detail, sizeof o->detail, "%.3f", (double) f);
	pR->Release();
}

void s_reverb_preset_rt(ScenarioContext *c, StepResult *o)
{
	IA3dReverb *pR = qi_as<IA3dReverb>(c->pRoot, IID_IA3dReverb);
	if (!pR) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	HRESULT hs = pR->SetReverbPreset(A3DREVERB_PRESET_HANGAR);
	DWORD d = 0xFFFFFFFF;
	o->hr = pR->GetReverbPreset(&d);
	std::snprintf(o->detail, sizeof o->detail, "set=0x%08lX get=%lu",
		      (unsigned long) hs, d);
	pR->Release();
}

void s_reverb_vol_rt(ScenarioContext *c, StepResult *o)
{
	IA3dReverb *pR = qi_as<IA3dReverb>(c->pRoot, IID_IA3dReverb);
	if (!pR) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	HRESULT hs = pR->SetPresetVolume(0.5f);
	A3DVAL f = -1.0f;
	o->hr = pR->GetPresetVolume(&f);
	std::snprintf(o->detail, sizeof o->detail, "set=0x%08lX get=%.3f",
		      (unsigned long) hs, (double) f);
	pR->Release();
}

void s_reverb_decay_rt(ScenarioContext *c, StepResult *o)
{
	IA3dReverb *pR = qi_as<IA3dReverb>(c->pRoot, IID_IA3dReverb);
	if (!pR) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	HRESULT hs = pR->SetPresetDecayTime(1.5f);
	A3DVAL f = -1.0f;
	o->hr = pR->GetPresetDecayTime(&f);
	std::snprintf(o->detail, sizeof o->detail, "set=0x%08lX get=%.3f",
		      (unsigned long) hs, (double) f);
	pR->Release();
}

void s_reverb_damping_rt(ScenarioContext *c, StepResult *o)
{
	IA3dReverb *pR = qi_as<IA3dReverb>(c->pRoot, IID_IA3dReverb);
	if (!pR) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	HRESULT hs = pR->SetPresetDamping(0.5f);
	A3DVAL f = -1.0f;
	o->hr = pR->GetPresetDamping(&f);
	std::snprintf(o->detail, sizeof o->detail, "set=0x%08lX get=%.3f",
		      (unsigned long) hs, (double) f);
	pR->Release();
}

/* Read the whole property block back after selecting a preset.  dwSize and
   dwType are the two fields defined independently of the preset/custom union. */
void s_reverb_getallprops(ScenarioContext *c, StepResult *o)
{
	IA3dReverb *pR = qi_as<IA3dReverb>(c->pRoot, IID_IA3dReverb);
	if (!pR) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	pR->SetReverbPreset(A3DREVERB_PRESET_HANGAR);
	A3DREVERB_PROPERTIES props;
	std::memset(&props, 0, sizeof props);
	props.dwSize = sizeof props;
	o->hr = pR->GetAllProperties(&props);
	std::snprintf(o->detail, sizeof o->detail, "size=%lu type=%lu",
		      (unsigned long) props.dwSize, (unsigned long) props.dwType);
	pR->Release();
}

/* The script's root was initialised without A3D_REVERB, so the reverb slots
   take their refusal path. */

void s_reverb_new_null(ScenarioContext *c, StepResult *o)
{
	o->hr = c->pRoot->NewReverb(NULL);
	std::snprintf(o->detail, sizeof o->detail, "-");
}

void s_reverb_new_unrequested(ScenarioContext *c, StepResult *o)
{
	LPA3DREVERB p = (LPA3DREVERB) (LONG_PTR) -1;
	o->hr = c->pRoot->NewReverb(&p);
	std::snprintf(o->detail, sizeof o->detail, "%s",
		      p == (LPA3DREVERB) (LONG_PTR) -1 ? "untouched"
						       : (p ? "obj" : "null"));
}

void s_reverb_bind_beforeinit(ScenarioContext *c, StepResult *o)
{
	o->hr = c->pRoot->BindReverb(NULL);
	std::snprintf(o->detail, sizeof o->detail, "-");
}

void s_reverb_feature(ScenarioContext *c, StepResult *o)
{
	o->hr = c->pRoot->IsFeatureAvailable(A3D_REVERB);
	std::snprintf(o->detail, sizeof o->detail, "-");
}

/* Create a second engine to reach the reverb-requested path; the script's
   own root cannot reach it because InitEx has already run without A3D_REVERB. */
void s_reverb_second_engine(ScenarioContext *c, StepResult *o)
{
	void	*p = NULL;
	HRESULT	 hc = c->create(CLSID_A3dApi, IID_IA3d5, &p);

	if (FAILED(hc) || !p) {
		o->hr = hc;
		std::snprintf(o->detail, sizeof o->detail, "no-root");
		return;
	}

	IA3d5 *pRoot = reinterpret_cast<IA3d5 *>(p);

	HRESULT hi = pRoot->InitEx(NULL, A3D_REVERB, A3D_DIRECT_PATH_A3D,
				   NULL, A3D_CL_NORMAL);
	HRESULT hf = pRoot->IsFeatureAvailable(A3D_REVERB);

	LPA3DREVERB pReverb = NULL;
	HRESULT hn = pRoot->NewReverb(&pReverb);
	HRESULT hb = pRoot->BindReverb(pReverb);

	if (pReverb)
		pReverb->Release();

	o->hr = hi;
	std::snprintf(o->detail, sizeof o->detail,
		      "avail=0x%08lX new=0x%08lX obj=%s bind=0x%08lX",
		      (unsigned long) hf, (unsigned long) hn,
		      pReverb ? "obj" : "null", (unsigned long) hb);

	pRoot->Release();
}

}	/* namespace */

namespace a3ddiff {

void add_reverb_steps(Script &s)
{
	s.push_back({ "Reverb GetPreset",      s_reverb_getpreset });
	s.push_back({ "Reverb GetVolume",      s_reverb_getvol });
	s.push_back({ "Reverb SetGetPreset",   s_reverb_preset_rt });
	s.push_back({ "Reverb SetGetVolume",   s_reverb_vol_rt });
	s.push_back({ "Reverb SetGetDecayTime", s_reverb_decay_rt });
	s.push_back({ "Reverb SetGetDamping",  s_reverb_damping_rt });
	s.push_back({ "Reverb GetAllProperties", s_reverb_getallprops });
	s.push_back({ "Reverb NewNull",        s_reverb_new_null });
	s.push_back({ "Reverb NewUnrequested", s_reverb_new_unrequested });
	s.push_back({ "Reverb BindBeforeInit", s_reverb_bind_beforeinit });
	s.push_back({ "Reverb IsFeatureAvailable", s_reverb_feature });
}

/* Appended after every other interface's steps; see the note on the body. */
void add_reverb_engine_steps(Script &s)
{
	s.push_back({ "Reverb SecondEngine",   s_reverb_second_engine });
}

}	/* namespace a3ddiff */

TEST_F(InterfaceVsReference, ReverbSecondEngine)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Reverb SecondEngine");
}

TEST_F(InterfaceVsReference, ReverbFactoryRefusals)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Reverb NewNull");
	expect_parity("Reverb NewUnrequested");
	expect_parity("Reverb BindBeforeInit");
	expect_parity("Reverb IsFeatureAvailable");
}

TEST_F(InterfaceVsReference, ReverbGetters)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Reverb GetPreset");
	expect_parity("Reverb GetVolume");
	expect_parity("Reverb SetGetPreset");
}

TEST_F(InterfaceVsReference, ReverbSetGet)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Reverb SetGetVolume");
	expect_parity("Reverb SetGetDecayTime");
	expect_parity("Reverb SetGetDamping");
	expect_parity("Reverb GetAllProperties");
}
