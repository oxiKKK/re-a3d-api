/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * root_comparison_tests.cpp - Interface comparison: IA3d5, the root object.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling. See tests/README.md.
 *
 *---------------------------------------------------------------------------
 */

#include <cstring>

#include "comparison_fixture.hpp"

using namespace a3ddiff;

namespace {

/* -- lifecycle: InitEx, output gain, the sub-interface QIs ------------- */

void s_init(ScenarioContext *c, StepResult *o)
{
	/* InitEx with a real window and cooperative level, the way the samples
	   initialise; a source's primary buffer needs it. */
	o->hr = c->pRoot->InitEx(NULL, A3D_1ST_REFLECTIONS, A3D_DIRECT_PATH_A3D,
				 GetDesktopWindow(), A3D_CL_NORMAL);
	o->detail[0] = 0;
}

void s_setoutputgain(ScenarioContext *c, StepResult *o)
{
	o->hr = c->pRoot->SetOutputGain(1.0f);
	o->detail[0] = 0;
}

void s_getoutputgain(ScenarioContext *c, StepResult *o)
{
	A3DVAL f = -1.0f;
	o->hr = c->pRoot->GetOutputGain(&f);
	std::snprintf(o->detail, sizeof o->detail, "gain=%.3f", (double) f);
}

void s_qi_geom2(ScenarioContext *c, StepResult *o)    { qi(c, o, IID_IA3dGeom2); }
void s_qi_listener(ScenarioContext *c, StepResult *o) { qi(c, o, IID_IA3dListener); }
void s_qi_reverb(ScenarioContext *c, StepResult *o)   { qi(c, o, IID_IA3dReverb); }

/* -- IA3d5 parameter getters: a scalar out-parameter each ------------- */

void s_root_getoutputmode(ScenarioContext *c, StepResult *o)
{
	DWORD a = 0, b = 0, d = 0;
	o->hr = c->pRoot->GetOutputMode(&a, &b, &d);
	std::snprintf(o->detail, sizeof o->detail, "%lu,%lu,%lu", a, b, d);
}

void s_root_getrmmode(ScenarioContext *c, StepResult *o)
{
	DWORD d = 0;
	o->hr = c->pRoot->GetResourceManagerMode(&d);
	std::snprintf(o->detail, sizeof o->detail, "%lu", d);
}

void s_root_getcoop(ScenarioContext *c, StepResult *o)
{
	DWORD d = 0;
	o->hr = c->pRoot->GetCooperativeLevel(&d);
	std::snprintf(o->detail, sizeof o->detail, "%lu", d);
}

void s_root_getcoord(ScenarioContext *c, StepResult *o)
{
	DWORD d = 0;
	o->hr = c->pRoot->GetCoordinateSystem(&d);
	std::snprintf(o->detail, sizeof o->detail, "%lu", d);
}

void s_root_getfallback(ScenarioContext *c, StepResult *o)
{
	DWORD d = 0;
	o->hr = c->pRoot->GetNumFallbackSources(&d);
	std::snprintf(o->detail, sizeof o->detail, "%lu", d);
}

void s_root_getmaxhw(ScenarioContext *c, StepResult *o)
{
	DWORD d = 0;
	o->hr = c->pRoot->GetMaxHardwareSources(&d);
	std::snprintf(o->detail, sizeof o->detail, "%lu", d);
}

void s_root_gethfabsorb(ScenarioContext *c, StepResult *o)
{
	FLOAT f = -1.0f;
	o->hr = c->pRoot->GetHFAbsorbFactor(&f);
	std::snprintf(o->detail, sizeof o->detail, "%.3f", (double) f);
}

void s_root_getunits(ScenarioContext *c, StepResult *o)
{
	A3DVAL f = -1.0f;
	o->hr = c->pRoot->GetUnitsPerMeter(&f);
	std::snprintf(o->detail, sizeof o->detail, "%.3f", (double) f);
}

void s_root_getdoppler(ScenarioContext *c, StepResult *o)
{
	A3DVAL f = -1.0f;
	o->hr = c->pRoot->GetDopplerScale(&f);
	std::snprintf(o->detail, sizeof o->detail, "%.3f", (double) f);
}

void s_root_getdistmodel(ScenarioContext *c, StepResult *o)
{
	A3DVAL f = -1.0f;
	o->hr = c->pRoot->GetDistanceModelScale(&f);
	std::snprintf(o->detail, sizeof o->detail, "%.3f", (double) f);
}

void s_root_geteq(ScenarioContext *c, StepResult *o)
{
	A3DVAL f = -1.0f;
	o->hr = c->pRoot->GetEq(&f);
	std::snprintf(o->detail, sizeof o->detail, "%.3f", (double) f);
}

void s_root_getmaxrefl(ScenarioContext *c, StepResult *o)
{
	A3DVAL f = -1.0f;
	o->hr = c->pRoot->GetMaxReflectionDelayTime(&f);
	std::snprintf(o->detail, sizeof o->detail, "%.3f", (double) f);
}

void s_root_getrmbias(ScenarioContext *c, StepResult *o)
{
	A3DVAL f = -1.0f;
	o->hr = c->pRoot->GetRMPriorityBias(&f);
	std::snprintf(o->detail, sizeof o->detail, "%.3f", (double) f);
}

void s_root_getstreamprops(ScenarioContext *c, StepResult *o)
{
	DWORD a = 0, b = 0;
	o->hr = c->pRoot->GetStreamingProperties(&a, &b);
	std::snprintf(o->detail, sizeof o->detail, "%lu,%lu", a, b);
}

/* Set/get comparisons record the setter HRESULT and the returned value. */

void s_root_doppler_rt(ScenarioContext *c, StepResult *o)
{
	A3DVAL f = -1.0f;
	HRESULT hs = c->pRoot->SetDopplerScale(2.0f);
	o->hr = c->pRoot->GetDopplerScale(&f);
	std::snprintf(o->detail, sizeof o->detail, "set=0x%08lX get=%.3f",
		      (unsigned long) hs, (double) f);
}

void s_root_units_rt(ScenarioContext *c, StepResult *o)
{
	A3DVAL f = -1.0f;
	HRESULT hs = c->pRoot->SetUnitsPerMeter(3.0f);
	o->hr = c->pRoot->GetUnitsPerMeter(&f);
	std::snprintf(o->detail, sizeof o->detail, "set=0x%08lX get=%.3f",
		      (unsigned long) hs, (double) f);
}

void s_root_coord_rt(ScenarioContext *c, StepResult *o)
{
	HRESULT hs = c->pRoot->SetCoordinateSystem(A3D_LEFT_HANDED_CS);
	DWORD d = 0xFFFFFFFF;
	o->hr = c->pRoot->GetCoordinateSystem(&d);
	std::snprintf(o->detail, sizeof o->detail, "set=0x%08lX get=%lu",
		      (unsigned long) hs, d);
}

void s_root_eq_rt(ScenarioContext *c, StepResult *o)
{
	HRESULT hs = c->pRoot->SetEq(0.5f);
	A3DVAL f = -1.0f;
	o->hr = c->pRoot->GetEq(&f);
	std::snprintf(o->detail, sizeof o->detail, "set=0x%08lX get=%.3f",
		      (unsigned long) hs, (double) f);
}

void s_root_maxrefl_rt(ScenarioContext *c, StepResult *o)
{
	HRESULT hs = c->pRoot->SetMaxReflectionDelayTime(0.5f);
	A3DVAL f = -1.0f;
	o->hr = c->pRoot->GetMaxReflectionDelayTime(&f);
	std::snprintf(o->detail, sizeof o->detail, "set=0x%08lX get=%.3f",
		      (unsigned long) hs, (double) f);
}

void s_root_fallback_rt(ScenarioContext *c, StepResult *o)
{
	HRESULT hs = c->pRoot->SetNumFallbackSources(4);
	DWORD d = 0xFFFFFFFF;
	o->hr = c->pRoot->GetNumFallbackSources(&d);
	std::snprintf(o->detail, sizeof o->detail, "set=0x%08lX get=%lu",
		      (unsigned long) hs, d);
}

void s_root_rmbias_rt(ScenarioContext *c, StepResult *o)
{
	HRESULT hs = c->pRoot->SetRMPriorityBias(0.5f);
	A3DVAL f = -1.0f;
	o->hr = c->pRoot->GetRMPriorityBias(&f);
	std::snprintf(o->detail, sizeof o->detail, "set=0x%08lX get=%.3f",
		      (unsigned long) hs, (double) f);
}

void s_root_distmodel_rt(ScenarioContext *c, StepResult *o)
{
	HRESULT hs = c->pRoot->SetDistanceModelScale(2.0f);
	A3DVAL f = -1.0f;
	o->hr = c->pRoot->GetDistanceModelScale(&f);
	std::snprintf(o->detail, sizeof o->detail, "set=0x%08lX get=%.3f",
		      (unsigned long) hs, (double) f);
}

/* The IA3d and IA3d2 slots, 3 to 11: six forward to IA3d2, three have their
   own guards.  The null-pointer variants drive error checks. */

void s_legacy_setoutputmode(ScenarioContext *c, StepResult *o)
{
	o->hr = c->pRoot->SetOutputMode(0, 0, 2);
	o->detail[0] = 0;
}

void s_legacy_getoutputmode_null(ScenarioContext *c, StepResult *o)
{
	DWORD a = 0, b = 0;
	o->hr = c->pRoot->GetOutputMode(&a, &b, NULL);
	std::snprintf(o->detail, sizeof o->detail, "%lu,%lu", a, b);
}

void s_legacy_setrmmode(ScenarioContext *c, StepResult *o)
{
	o->hr = c->pRoot->SetResourceManagerMode(0);
	o->detail[0] = 0;
}

void s_legacy_sethfabsorb(ScenarioContext *c, StepResult *o)
{
	o->hr = c->pRoot->SetHFAbsorbFactor(0.5f);
	o->detail[0] = 0;
}

void s_legacy_registerversion(ScenarioContext *c, StepResult *o)
{
	o->hr = c->pRoot->RegisterVersion(0x00030003);
	o->detail[0] = 0;
}

void s_legacy_getsoftwarecaps(ScenarioContext *c, StepResult *o)
{
	A3DCAPS_SOFTWARE caps;
	std::memset(&caps, 0, sizeof caps);
	caps.dwSize = sizeof caps;
	o->hr = c->pRoot->GetSoftwareCaps(&caps);
	std::snprintf(o->detail, sizeof o->detail, "%lu,%lu",
		      (unsigned long) caps.dwSize, (unsigned long) caps.dwFlags);
}

void s_legacy_getsoftwarecaps_null(ScenarioContext *c, StepResult *o)
{
	o->hr = c->pRoot->GetSoftwareCaps(NULL);
	o->detail[0] = 0;
}

void s_legacy_gethardwarecaps(ScenarioContext *c, StepResult *o)
{
	A3DCAPS_HARDWARE caps;
	std::memset(&caps, 0, sizeof caps);
	caps.dwSize = sizeof caps;
	o->hr = c->pRoot->GetHardwareCaps(&caps);
	std::snprintf(o->detail, sizeof o->detail, "%lu,%lu",
		      (unsigned long) caps.dwSize, (unsigned long) caps.dwFlags);
}

void s_legacy_gethardwarecaps_null(ScenarioContext *c, StepResult *o)
{
	o->hr = c->pRoot->GetHardwareCaps(NULL);
	o->detail[0] = 0;
}

}	/* namespace */

namespace a3ddiff {

void add_root_steps(Script &s)
{
	s.push_back({ "InitEx",          s_init });
	s.push_back({ "SetOutputGain",   s_setoutputgain });
	s.push_back({ "GetOutputGain",   s_getoutputgain });
	s.push_back({ "QI IA3dGeom2",    s_qi_geom2 });
	s.push_back({ "QI IA3dListener", s_qi_listener });
	s.push_back({ "QI IA3dReverb",   s_qi_reverb });

	s.push_back({ "GetOutputMode",           s_root_getoutputmode });
	s.push_back({ "GetResourceManagerMode",  s_root_getrmmode });
	s.push_back({ "GetCooperativeLevel",     s_root_getcoop });
	s.push_back({ "GetCoordinateSystem",     s_root_getcoord });
	s.push_back({ "GetNumFallbackSources",   s_root_getfallback });
	s.push_back({ "GetMaxHardwareSources",   s_root_getmaxhw });
	s.push_back({ "GetHFAbsorbFactor",       s_root_gethfabsorb });
	s.push_back({ "GetUnitsPerMeter",        s_root_getunits });
	s.push_back({ "GetDopplerScale",         s_root_getdoppler });
	s.push_back({ "GetDistanceModelScale",   s_root_getdistmodel });
	s.push_back({ "GetEq",                   s_root_geteq });
	s.push_back({ "GetMaxReflectionDelay",   s_root_getmaxrefl });
	s.push_back({ "GetRMPriorityBias",       s_root_getrmbias });
	s.push_back({ "GetStreamingProperties",  s_root_getstreamprops });
	s.push_back({ "SetGetDopplerScale",      s_root_doppler_rt });
	s.push_back({ "SetGetUnitsPerMeter",     s_root_units_rt });

	s.push_back({ "Root SetGetCoordSystem",  s_root_coord_rt });
	s.push_back({ "Root SetGetEq",           s_root_eq_rt });
	s.push_back({ "Root SetGetMaxReflDelay", s_root_maxrefl_rt });
	s.push_back({ "Root SetGetFallback",     s_root_fallback_rt });
	s.push_back({ "Root SetGetRMPriority",   s_root_rmbias_rt });
	s.push_back({ "Root SetGetDistModel",    s_root_distmodel_rt });

	s.push_back({ "Legacy SetOutputMode",       s_legacy_setoutputmode });
	s.push_back({ "Legacy GetOutputMode null",  s_legacy_getoutputmode_null });
	s.push_back({ "Legacy SetRMMode",           s_legacy_setrmmode });
	s.push_back({ "Legacy SetHFAbsorbFactor",   s_legacy_sethfabsorb });
	s.push_back({ "Legacy RegisterVersion",     s_legacy_registerversion });
	s.push_back({ "Legacy GetSoftwareCaps",     s_legacy_getsoftwarecaps });
	s.push_back({ "Legacy GetSoftwareCaps null", s_legacy_getsoftwarecaps_null });
	s.push_back({ "Legacy GetHardwareCaps",     s_legacy_gethardwarecaps });
	s.push_back({ "Legacy GetHardwareCaps null", s_legacy_gethardwarecaps_null });
}

}	/* namespace a3ddiff */

TEST_F(InterfaceVsReference, RootCreateInitQI)
{
	ASSERT_TRUE(a.loaded) << "our DLL failed to load: " << A3D_AB_OUR_DLL;
	ASSERT_TRUE(b.loaded) << "ref DLL failed to load: " << A3D_AB_REF_DLL;

	expect_parity("CreateInstance IA3d5");
	expect_parity("InitEx");
	expect_parity("SetOutputGain");
	expect_parity("GetOutputGain");
	expect_parity("QI IA3dGeom2");
	expect_parity("QI IA3dListener");
	expect_parity("QI IA3dReverb");
}

TEST_F(InterfaceVsReference, RootGetters)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("GetOutputMode");
	expect_parity("GetResourceManagerMode");
	expect_parity("GetCooperativeLevel");
	expect_parity("GetCoordinateSystem");
	expect_parity("GetNumFallbackSources");
	expect_parity("GetMaxHardwareSources");
	expect_parity("GetHFAbsorbFactor");
	expect_parity("GetUnitsPerMeter");
	expect_parity("GetDopplerScale");
	expect_parity("GetDistanceModelScale");
	expect_parity("GetEq");
	expect_parity("GetMaxReflectionDelay");
	expect_parity("GetRMPriorityBias");
	expect_parity("GetStreamingProperties");
	expect_parity("SetGetDopplerScale");
	expect_parity("SetGetUnitsPerMeter");
}

TEST_F(InterfaceVsReference, RootSetGet)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Root SetGetCoordSystem");
	expect_parity("Root SetGetEq");
	expect_parity("Root SetGetMaxReflDelay");
	expect_parity("Root SetGetFallback");
	expect_parity("Root SetGetRMPriority");
	expect_parity("Root SetGetDistModel");
}

TEST_F(InterfaceVsReference, RootLegacySlots)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Legacy SetOutputMode");
	expect_parity("Legacy GetOutputMode null");
	expect_parity("Legacy SetRMMode");
	expect_parity("Legacy SetHFAbsorbFactor");
	expect_parity("Legacy RegisterVersion");
	expect_parity("Legacy GetSoftwareCaps");
	expect_parity("Legacy GetSoftwareCaps null");
	expect_parity("Legacy GetHardwareCaps");
	expect_parity("Legacy GetHardwareCaps null");
}
