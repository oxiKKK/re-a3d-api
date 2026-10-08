/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * source_comparison_tests.cpp - Interface comparison: IA3dSource2.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling. See tests/README.md.
 *
 *---------------------------------------------------------------------------
 */

#include "comparison_fixture.hpp"

using namespace a3ddiff;

namespace {

void s_shutdown_init(ScenarioContext *c, StepResult *o)
{
	o->hr = c->pRoot->InitEx(NULL,
		A3D_DISABLE_SPLASHSCREEN | A3D_DISABLE_FOCUS_MUTE,
		A3DRENDERPREFS_DEFAULT, GetDesktopWindow(), A3D_CL_NORMAL);
}

void s_retained_sources(ScenarioContext *c, StepResult *o)
{
	for (int i = 0; i < 8; ++i) {
		o->hr = c->pRoot->NewSource(A3DSOURCE_TYPEDEFAULT, &c->pSrc);
		if (FAILED(o->hr) || !c->pSrc)
			return;
		o->hr = c->pSrc->LoadFile((char *) A3D_AB_WAV, A3DSOURCE_FORMAT_WAVE);
		if (FAILED(o->hr))
			return;
	}
	std::snprintf(o->detail, sizeof o->detail, "created=8");
}

void s_shutdown_sources(ScenarioContext *c, StepResult *o)
{
	o->hr = c->pRoot->Shutdown();
	c->pRoot = NULL;
	c->pSrc = NULL;
}

void s_ac3_failed_load(ScenarioContext *c, StepResult *o)
{
	IA3dSource2 *source = NULL;
	o->hr = c->pRoot->NewSource(A3DSOURCE_TYPEDEFAULT, &source);
	if (FAILED(o->hr) || !source)
		return;
	HRESULT load = source->LoadFile((char *) "missing-close-small-gap.ac3",
		A3DSOURCE_FORMAT_AC3);
	A3DVAL gain = -1.0f;
	o->hr = source->GetGain(&gain);
	std::snprintf(o->detail, sizeof o->detail, "load=0x%08lX gain=%.3f",
		(unsigned long) load, (double) gain);
	source->Release();
}

void s_stream_cycle(ScenarioContext *c, StepResult *o)
{
	IA3dSource2 *source = NULL;
	o->hr = c->pRoot->NewSource(A3DSOURCE_TYPESTREAMED, &source);
	if (FAILED(o->hr) || !source)
		return;
	size_t used = 0;
	DWORD cFirst = 0, cLast = 0;
	for (int i = 0; i < 3; i++) {
		HRESULT load = source->LoadFile((char *) A3D_AB_WAV,
			A3DSOURCE_FORMAT_WAVE | A3DSOURCE_STREAMING);
		HRESULT play = source->Play(A3D_LOOPED);
		HRESULT stop = source->Stop();
		HRESULT free = source->FreeAudioData();
		used += std::snprintf(o->detail + used, sizeof o->detail - used,
			"%s%08lX/%08lX/%08lX/%08lX", i ? " " : "",
			(unsigned long) load, (unsigned long) play,
			(unsigned long) stop, (unsigned long) free);
		if (used >= sizeof o->detail)
			break;
		GetProcessHandleCount(GetCurrentProcess(), i ? &cLast : &cFirst);
	}
	if (used < sizeof o->detail)
		std::snprintf(o->detail + used, sizeof o->detail - used, " dh=%ld",
			(long) cLast - (long) cFirst);
	source->Release();
}

/* -- the play path: make the source, load, gain, play, stop ------------ */

void s_newsource(ScenarioContext *c, StepResult *o)
{
	c->pSrc = NULL;
	o->hr = c->pRoot->NewSource(A3DSOURCE_TYPEDEFAULT, &c->pSrc);
	std::snprintf(o->detail, sizeof o->detail, "%s", c->pSrc ? "src" : "null");
}

void s_loadfile(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	o->hr = c->pSrc->LoadFile((char *) A3D_AB_WAV, A3DSOURCE_FORMAT_WAVE);
	o->detail[0] = 0;
}

void s_src_setgain(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	o->hr = c->pSrc->SetGain(1.0f);
	o->detail[0] = 0;
}

void s_play(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	o->hr = c->pSrc->Play(A3D_LOOPED);
	o->detail[0] = 0;
}

void s_stop(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	o->hr = c->pSrc->Stop();
	o->detail[0] = 0;
}

/* (RE) rtl:0x100155C0 and dbg:0x10030C20: a stopped wave still owns
   its buffer, so both file loaders refuse a replacement on IA3d5. */
void source_reload(ScenarioContext *c, StepResult *o, bool wave, bool null_name)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	DWORD before = c->pSrc->GetAudioSize();
	A3DVAL gain = -1.0f;
	char *name = null_name ? NULL : (char *) A3D_AB_WAV;
	o->hr = wave ? c->pSrc->LoadWaveFile(name)
		     : c->pSrc->LoadFile(name, A3DSOURCE_FORMAT_WAVE);
	DWORD after = c->pSrc->GetAudioSize();
	HRESULT hg = c->pSrc->GetGain(&gain);
	std::snprintf(o->detail, sizeof o->detail,
		"size=%lu/%lu gain=0x%08lX/%.3f",
		(unsigned long) before, (unsigned long) after,
		(unsigned long) hg, (double) gain);
}

void s_reload_file(ScenarioContext *c, StepResult *o) { source_reload(c, o, false, false); }
void s_reload_wave(ScenarioContext *c, StepResult *o) { source_reload(c, o, true, false); }
void s_reload_file_null(ScenarioContext *c, StepResult *o) { source_reload(c, o, false, true); }
void s_reload_wave_null(ScenarioContext *c, StepResult *o) { source_reload(c, o, true, true); }

/* -- getters on the source, read after it has played ------------------ */

void s_src_getgain(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	A3DVAL f = -1.0f;
	o->hr = c->pSrc->GetGain(&f);
	std::snprintf(o->detail, sizeof o->detail, "%.3f", (double) f);
}

void s_src_getpitch(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	A3DVAL f = -1.0f;
	o->hr = c->pSrc->GetPitch(&f);
	std::snprintf(o->detail, sizeof o->detail, "%.3f", (double) f);
}

void s_src_getpos(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	A3DVAL x = -1.0f, y = -1.0f, z = -1.0f;
	o->hr = c->pSrc->GetPosition3f(&x, &y, &z);
	std::snprintf(o->detail, sizeof o->detail, "%.3f,%.3f,%.3f",
		      (double) x, (double) y, (double) z);
}

void s_src_getrendermode(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	DWORD d = 0xFFFFFFFF;
	o->hr = c->pSrc->GetRenderMode(&d);
	std::snprintf(o->detail, sizeof o->detail, "%lu", d);
}

/* -- set/get roundtrips on the source --------------------------------- */

void s_src_gain_rt(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	HRESULT hs = c->pSrc->SetGain(0.5f);
	A3DVAL f = -1.0f;
	o->hr = c->pSrc->GetGain(&f);
	std::snprintf(o->detail, sizeof o->detail, "set=0x%08lX get=%.3f",
		      (unsigned long) hs, (double) f);
}

void s_src_pitch_rt(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	HRESULT hs = c->pSrc->SetPitch(2.0f);
	A3DVAL f = -1.0f;
	o->hr = c->pSrc->GetPitch(&f);
	std::snprintf(o->detail, sizeof o->detail, "set=0x%08lX get=%.3f",
		      (unsigned long) hs, (double) f);
}

void s_src_doppler_rt(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	HRESULT hs = c->pSrc->SetDopplerScale(2.0f);
	A3DVAL f = -1.0f;
	o->hr = c->pSrc->GetDopplerScale(&f);
	std::snprintf(o->detail, sizeof o->detail, "set=0x%08lX get=%.3f",
		      (unsigned long) hs, (double) f);
}

void s_src_priority_rt(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	HRESULT hs = c->pSrc->SetPriority(0.5f);
	A3DVAL f = -1.0f;
	o->hr = c->pSrc->GetPriority(&f);
	std::snprintf(o->detail, sizeof o->detail, "set=0x%08lX get=%.3f",
		      (unsigned long) hs, (double) f);
}

void s_src_pos_rt(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	c->pSrc->SetPosition3f(1.5f, 2.5f, 3.5f);
	A3DVAL x = -1.0f, y = -1.0f, z = -1.0f;
	o->hr = c->pSrc->GetPosition3f(&x, &y, &z);
	std::snprintf(o->detail, sizeof o->detail, "%.3f,%.3f,%.3f",
		      (double) x, (double) y, (double) z);
}

/* -- more getters: the computed and status values ---------------------- */

void s_src_getaudibility(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	A3DVAL f = -1.0f;
	o->hr = c->pSrc->GetAudibility(&f);
	std::snprintf(o->detail, sizeof o->detail, "%.3f", (double) f);
}

void s_src_getocclusion(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	A3DVAL f = -1.0f;
	o->hr = c->pSrc->GetOcclusionFactor(&f);
	std::snprintf(o->detail, sizeof o->detail, "%.3f", (double) f);
}

void s_src_getstatus(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	DWORD d = 0xFFFFFFFF;
	o->hr = c->pSrc->GetStatus(&d);
	std::snprintf(o->detail, sizeof o->detail, "%lu", d);
}

void s_src_gettype(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	DWORD d = 0xFFFFFFFF;
	o->hr = c->pSrc->GetType(&d);
	std::snprintf(o->detail, sizeof o->detail, "%lu", d);
}

void s_src_getplaypos(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	DWORD d = 0xFFFFFFFF;
	o->hr = c->pSrc->GetPlayPosition(&d);
	std::snprintf(o->detail, sizeof o->detail, "%lu", d);
}

void s_src_getplaytime(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	A3DVAL f = -1.0f;
	o->hr = c->pSrc->GetPlayTime(&f);
	std::snprintf(o->detail, sizeof o->detail, "%.3f", (double) f);
}

void s_src_getnumrefl(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	int n = -1;
	o->hr = c->pSrc->GetNumManualReflections(&n);
	std::snprintf(o->detail, sizeof o->detail, "%d", n);
}

void s_src_getaudiosize(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	/* GetAudioSize returns the size directly, not through an out-parameter. */
	DWORD sz = c->pSrc->GetAudioSize();
	o->hr = S_OK;
	std::snprintf(o->detail, sizeof o->detail, "%lu", (unsigned long) sz);
}

/* -- more set/get roundtrips ------------------------------------------ */

void s_src_eq_rt(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	HRESULT hs = c->pSrc->SetEq(0.5f);
	A3DVAL f = -1.0f;
	o->hr = c->pSrc->GetEq(&f);
	std::snprintf(o->detail, sizeof o->detail, "set=0x%08lX get=%.3f",
		      (unsigned long) hs, (double) f);
}

void s_src_distmodel_rt(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	HRESULT hs = c->pSrc->SetDistanceModelScale(2.0f);
	A3DVAL f = -1.0f;
	o->hr = c->pSrc->GetDistanceModelScale(&f);
	std::snprintf(o->detail, sizeof o->detail, "set=0x%08lX get=%.3f",
		      (unsigned long) hs, (double) f);
}

void s_src_vel_rt(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	c->pSrc->SetVelocity3f(1.5f, 2.5f, 3.5f);
	A3DVAL x = -1.0f, y = -1.0f, z = -1.0f;
	o->hr = c->pSrc->GetVelocity3f(&x, &y, &z);
	std::snprintf(o->detail, sizeof o->detail, "%.3f,%.3f,%.3f",
		      (double) x, (double) y, (double) z);
}

void s_src_cone_rt(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	c->pSrc->SetCone(90.0f, 180.0f, 0.5f);
	A3DVAL a = -1.0f, b = -1.0f, g = -1.0f;
	o->hr = c->pSrc->GetCone(&a, &b, &g);
	std::snprintf(o->detail, sizeof o->detail, "%.3f,%.3f,%.3f",
		      (double) a, (double) b, (double) g);
}

void s_src_minmax_rt(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	c->pSrc->SetMinMaxDistance(1.0f, 100.0f, 0);
	A3DVAL mn = -1.0f, mx = -1.0f;
	DWORD mode = 0xFFFFFFFF;
	o->hr = c->pSrc->GetMinMaxDistance(&mn, &mx, &mode);
	std::snprintf(o->detail, sizeof o->detail, "%.3f,%.3f,%lu",
		      (double) mn, (double) mx, mode);
}

void s_src_reflgain_rt(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	HRESULT hs = c->pSrc->SetReflectionGainScale(0.5f);
	A3DVAL f = -1.0f;
	o->hr = c->pSrc->GetReflectionGainScale(&f);
	std::snprintf(o->detail, sizeof o->detail, "set=0x%08lX get=%.3f",
		      (unsigned long) hs, (double) f);
}

void s_src_refldelay_rt(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	HRESULT hs = c->pSrc->SetReflectionDelayScale(0.5f);
	A3DVAL f = -1.0f;
	o->hr = c->pSrc->GetReflectionDelayScale(&f);
	std::snprintf(o->detail, sizeof o->detail, "set=0x%08lX get=%.3f",
		      (unsigned long) hs, (double) f);
}

}	/* namespace */

namespace a3ddiff {

void add_source_shutdown_steps(Script &s)
{
	s.push_back({ "Shutdown InitEx", s_shutdown_init });
	s.push_back({ "Create retained sources", s_retained_sources });
	s.push_back({ "Shutdown with sources", s_shutdown_sources });
}

void add_source_steps(Script &s)
{
	s.push_back({ "Source FailedAc3Load", s_ac3_failed_load });
	s.push_back({ "Source StreamCycle", s_stream_cycle });
	s.push_back({ "NewSource",      s_newsource });
	s.push_back({ "LoadFile",       s_loadfile });
	s.push_back({ "Source SetGain", s_src_setgain });
	s.push_back({ "Play",           s_play });
	s.push_back({ "Stop",           s_stop });
	s.push_back({ "Source ReloadFile", s_reload_file });
	s.push_back({ "Source ReloadWave", s_reload_wave });
	s.push_back({ "Source ReloadFileNull", s_reload_file_null });
	s.push_back({ "Source ReloadWaveNull", s_reload_wave_null });

	s.push_back({ "Source GetGain",       s_src_getgain });
	s.push_back({ "Source GetPitch",      s_src_getpitch });
	s.push_back({ "Source GetPosition",   s_src_getpos });
	s.push_back({ "Source GetRenderMode", s_src_getrendermode });

	s.push_back({ "Source SetGetGain",     s_src_gain_rt });
	s.push_back({ "Source SetGetPitch",    s_src_pitch_rt });
	s.push_back({ "Source SetGetDoppler",  s_src_doppler_rt });
	s.push_back({ "Source SetGetPriority", s_src_priority_rt });
	s.push_back({ "Source SetGetPosition", s_src_pos_rt });

	s.push_back({ "Source GetAudibility",     s_src_getaudibility });
	s.push_back({ "Source GetOcclusionFactor", s_src_getocclusion });
	s.push_back({ "Source GetStatus",         s_src_getstatus });
	s.push_back({ "Source GetType",           s_src_gettype });
	s.push_back({ "Source GetPlayPosition",   s_src_getplaypos });
	s.push_back({ "Source GetPlayTime",       s_src_getplaytime });
	s.push_back({ "Source GetNumReflections", s_src_getnumrefl });
	s.push_back({ "Source GetAudioSize",      s_src_getaudiosize });

	s.push_back({ "Source SetGetEq",        s_src_eq_rt });
	s.push_back({ "Source SetGetDistModel", s_src_distmodel_rt });
	s.push_back({ "Source SetGetVelocity",  s_src_vel_rt });
	s.push_back({ "Source SetGetCone",      s_src_cone_rt });
	s.push_back({ "Source SetGetMinMax",    s_src_minmax_rt });
	s.push_back({ "Source SetGetReflGain",  s_src_reflgain_rt });
	s.push_back({ "Source SetGetReflDelay", s_src_refldelay_rt });
}

}	/* namespace a3ddiff */

TEST_F(InterfaceVsReference, SourcePlayPath)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("NewSource");
	expect_parity("LoadFile");
	expect_parity("Source SetGain");
	expect_parity("Play");
	expect_parity("Stop");
}

TEST_F(InterfaceVsReference, SourceStreamCycle)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Source StreamCycle");
}

TEST_F(InterfaceVsReference, SourceFailedAc3Load)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Source FailedAc3Load");
	for (const ScenarioRunResult *run : { &a, &b }) {
		const RecordedStepResult *step = find_step(*run, "Source FailedAc3Load");
		ASSERT_NE(step, nullptr);
		EXPECT_FALSE(step->faulted);
		EXPECT_EQ(step->hr,
			A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
		EXPECT_EQ(step->detail, "load=0x8004005A gain=-1.000");
	}
}

TEST_F(InterfaceVsReference, SourceGetters)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Source GetGain");
	expect_parity("Source GetPitch");
	expect_parity("Source GetPosition");
	expect_parity("Source GetRenderMode");
}

TEST_F(InterfaceVsReference, SourceSetGet)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Source SetGetGain");
	expect_parity("Source SetGetPitch");
	expect_parity("Source SetGetDoppler");
	expect_parity("Source SetGetPriority");
	expect_parity("Source SetGetPosition");
}

TEST_F(InterfaceVsReference, SourceMoreGetters)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Source GetAudibility");
	expect_parity("Source GetOcclusionFactor");
	expect_parity("Source GetStatus");
	expect_parity("Source GetType");
	expect_parity("Source GetPlayPosition");
	expect_parity("Source GetPlayTime");
	expect_parity("Source GetNumReflections");
	expect_parity("Source GetAudioSize");
}

TEST_F(InterfaceVsReference, SourceMoreSetGet)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Source SetGetEq");
	expect_parity("Source SetGetDistModel");
	expect_parity("Source SetGetVelocity");
	expect_parity("Source SetGetCone");
	expect_parity("Source SetGetMinMax");
	expect_parity("Source SetGetReflGain");
	expect_parity("Source SetGetReflDelay");
}

TEST_F(InterfaceVsReference, SourceReloadInUse)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	for (const char *name : { "Source ReloadFile", "Source ReloadWave",
		"Source ReloadFileNull", "Source ReloadWaveNull" }) {
		expect_parity(name);
		for (const ScenarioRunResult *run : { &a, &b }) {
			const RecordedStepResult *step = find_step(*run, name);
			ASSERT_NE(step, nullptr);
			EXPECT_FALSE(step->faulted);
			EXPECT_EQ(step->hr, A3DERROR_SOURCE_IN_USE);
			unsigned long before = 0, after = 0;
			ASSERT_EQ(std::sscanf(step->detail.c_str(), "size=%lu/%lu",
				&before, &after), 2);
			EXPECT_GT(before, 0u);
			EXPECT_EQ(after, before);
		}
	}
}

TEST(SourceLifetimeVsReference, ShutdownWithRetainedSources)
{
	for (const char *dll : { A3D_AB_OUR_DLL, A3D_AB_REF_DLL }) {
		SCOPED_TRACE(dll);
		const auto run = run_child(self_path(), dll, "--child", "source_shutdown");
		ASSERT_TRUE(run.loaded && run.ended);
		ASSERT_FALSE(run.timed_out);
		ASSERT_EQ(run.exit_code, 0u);
		for (const char *name : { "Shutdown InitEx", "Create retained sources",
			"Shutdown with sources" }) {
			const auto *step = find_step(run, name);
			ASSERT_NE(step, nullptr) << name;
			EXPECT_FALSE(step->faulted) << name;
			EXPECT_EQ(step->hr, S_OK) << name;
		}
		EXPECT_EQ(find_step(run, "Create retained sources")->detail, "created=8");
	}
}
