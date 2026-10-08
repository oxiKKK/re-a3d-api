/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * legacy_comparison_tests.cpp - Interface comparison: legacy IA3d/IA3d2/3/4 interfaces.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling. See tests/README.md.
 *
 * QueryInterface preserves the highest requested interface version
 * (m_dwInterfaceVersion at 0x14). The shared root is created as IA3d5,
 * so legacy queries cannot enable GetResourceManagerMode or
 * GetHFAbsorbFactor. Testing their forwarding path requires a root
 * created as IA3d3.
 *
 * The shared scenario runs these queries after the IA3d5 cases.
 *
 *---------------------------------------------------------------------------
 */

#include "comparison_fixture.hpp"

using namespace a3ddiff;

namespace {

void s_qi_ia3d(ScenarioContext *c, StepResult *o)  { qi(c, o, IID_IA3d); }
void s_qi_ia3d2(ScenarioContext *c, StepResult *o) { qi(c, o, IID_IA3d2); }
void s_qi_ia3d3(ScenarioContext *c, StepResult *o) { qi(c, o, IID_IA3d3); }
void s_qi_ia3d4(ScenarioContext *c, StepResult *o) { qi(c, o, IID_IA3d4); }

void s_ia3d3_rmmode(ScenarioContext *c, StepResult *o)
{
	IA3d3 *p = qi_as<IA3d3>(c->pRoot, IID_IA3d3);
	if (!p) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	DWORD mode = 0xFFFFFFFF;
	o->hr = p->GetResourceManagerMode(&mode);
	std::snprintf(o->detail, sizeof o->detail, "%lu", mode);
	p->Release();
}

void s_ia3d4_rmmode(ScenarioContext *c, StepResult *o)
{
	IA3d4 *p = qi_as<IA3d4>(c->pRoot, IID_IA3d4);
	if (!p) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	DWORD mode = 0xFFFFFFFF;
	o->hr = p->GetResourceManagerMode(&mode);
	std::snprintf(o->detail, sizeof o->detail, "%lu", mode);
	p->Release();
}

void s_ia3d2_hfabsorb(ScenarioContext *c, StepResult *o)
{
	IA3d2 *p = qi_as<IA3d2>(c->pRoot, IID_IA3d2);
	if (!p) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	FLOAT f = -1.0f;
	o->hr = p->GetHFAbsorbFactor(&f);
	std::snprintf(o->detail, sizeof o->detail, "%.3f", (double) f);
	p->Release();
}

}	/* namespace */

namespace a3ddiff {

void add_legacy_steps(Script &s)
{
	s.push_back({ "QI IA3d",           s_qi_ia3d });
	s.push_back({ "QI IA3d2",          s_qi_ia3d2 });
	s.push_back({ "QI IA3d3",          s_qi_ia3d3 });
	s.push_back({ "QI IA3d4",          s_qi_ia3d4 });
	s.push_back({ "IA3d3 GetRMMode",   s_ia3d3_rmmode });
	s.push_back({ "IA3d4 GetRMMode",   s_ia3d4_rmmode });
	s.push_back({ "IA3d2 GetHFAbsorb", s_ia3d2_hfabsorb });
}

}	/* namespace a3ddiff */

/* Legacy queries retain the IA3d5 version restriction on
   GetResourceManagerMode and GetHFAbsorbFactor. */
TEST_F(InterfaceVsReference, LegacyIA3d1to4)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("QI IA3d");
	expect_parity("QI IA3d2");
	expect_parity("QI IA3d3");
	expect_parity("QI IA3d4");
	expect_parity("IA3d3 GetRMMode");
	expect_parity("IA3d4 GetRMMode");
	expect_parity("IA3d2 GetHFAbsorb");
}
