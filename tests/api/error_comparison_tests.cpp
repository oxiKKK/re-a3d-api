/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * error_comparison_tests.cpp - Interface comparison: null-argument refusals.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling. See tests/README.md.
 *
 * Compare null-output-pointer errors on root and source interfaces.
 * Source cases require the setup in source_comparison_tests.cpp.
 *
 *---------------------------------------------------------------------------
 */

#include "comparison_fixture.hpp"

using namespace a3ddiff;

namespace {

void s_src_getgain_null(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	o->hr = c->pSrc->GetGain(NULL);
	o->detail[0] = 0;
}

void s_root_getdoppler_null(ScenarioContext *c, StepResult *o)
{
	o->hr = c->pRoot->GetDopplerScale(NULL);
	o->detail[0] = 0;
}

/* -- what QueryInterface answers, and what it declines ---------------- */

/* An interface nothing implements. Both sides must decline it. */
static const IID kUnsupportedIID =
	{ 0xDEADBEEF, 0xFFFF, 0xFFFF, { 0xDE, 0xAD, 0xBE, 0xEF, 0, 0, 0, 1 } };

/* IID_IDirectSound: Windows SDK value. */
static const IID kIID_IDirectSound =
	{ 0x279AFA83, 0x4981, 0x11CE,
	  { 0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60 } };

void s_qi_unknown(ScenarioContext *c, StepResult *o)     { qi(c, o, IID_IUnknown); }
void s_qi_unsupported(ScenarioContext *c, StepResult *o) { qi(c, o, kUnsupportedIID); }

/* Test both DLLs' answer for IDirectSound and IID_IA3dDal. */
void s_qi_directsound(ScenarioContext *c, StepResult *o) { qi(c, o, kIID_IDirectSound); }


void s_qi_null_out(ScenarioContext *c, StepResult *o)
{
	o->hr = c->pRoot->QueryInterface(IID_IA3d5, NULL);
	o->detail[0] = 0;
}

}	/* namespace */

namespace a3ddiff {

void add_error_steps(Script &s)
{
	s.push_back({ "Source GetGain NULL", s_src_getgain_null });
	s.push_back({ "Root GetDoppler NULL", s_root_getdoppler_null });

	s.push_back({ "QI IUnknown",       s_qi_unknown });
	s.push_back({ "QI Unsupported",    s_qi_unsupported });
	s.push_back({ "QI IDirectSound",   s_qi_directsound });
	s.push_back({ "QI NULL out",       s_qi_null_out });
}

}	/* namespace a3ddiff */

TEST_F(InterfaceVsReference, NullPointerRefusals)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Source GetGain NULL");
	expect_parity("Root GetDoppler NULL");
}

TEST_F(InterfaceVsReference, QueryInterfaceSurface)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("QI IUnknown");
	expect_parity("QI Unsupported");
	expect_parity("QI IDirectSound");
	expect_parity("QI NULL out");
}
