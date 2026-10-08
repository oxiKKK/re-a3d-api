/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * source_data_comparison_tests.cpp - Interface comparison: IA3dSource2
 * audio data, play position, events, pan, volumetric and capability calls.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling. See tests/README.md.
 *
 *---------------------------------------------------------------------------
 */

#include "comparison_fixture.hpp"

#include <cstring>
#include <mmreg.h>
#include <dsound.h>

using namespace a3ddiff;

namespace {

WAVEFORMATEX mono16(void)
{
	WAVEFORMATEX wfx;
	std::memset(&wfx, 0, sizeof wfx);
	wfx.wFormatTag      = WAVE_FORMAT_PCM;
	wfx.nChannels       = 1;
	wfx.nSamplesPerSec  = 22050;
	wfx.wBitsPerSample  = 16;
	wfx.nBlockAlign     = 2;
	wfx.nAvgBytesPerSec = 44100;
	return wfx;
}

IA3dSource2 *new_source(ScenarioContext *c, DWORD dwFlags, StepResult *o)
{
	IA3dSource2 *src = NULL;
	o->hr = c->pRoot->NewSource(dwFlags, &src);
	if (FAILED(o->hr) || !src) {
		std::snprintf(o->detail, sizeof o->detail, "newsrc");
		return (NULL);
	}
	return (src);
}

IA3dSource2 *loaded_source(ScenarioContext *c, StepResult *o)
{
	IA3dSource2 *src = new_source(c, A3DSOURCE_TYPEDEFAULT, o);
	if (!src)
		return (NULL);
	o->hr = src->LoadFile((char *) A3D_AB_WAV, A3DSOURCE_FORMAT_WAVE);
	if (FAILED(o->hr)) {
		std::snprintf(o->detail, sizeof o->detail, "load");
		src->Release();
		return (NULL);
	}
	return (src);
}

#define HX(h) ((unsigned long) (h))

void s_audio_format(ScenarioContext *c, StepResult *o)
{
	IA3dSource2 *src = new_source(c, A3DSOURCE_TYPEDEFAULT, o);
	if (!src)
		return;
	WAVEFORMATEX in = mono16(), out;
	std::memset(&out, 0xCD, sizeof out);
	HRESULT hs = src->SetAudioFormat(&in);
	HRESULT ha = src->AllocateAudioData(4410);
	HRESULT hg = src->GetAudioFormat(&out);
	DWORD   cb = src->GetAudioSize();
	DWORD   ty = 0xFFFFFFFF;
	HRESULT ht = src->GetType(&ty);
	o->hr = hg;
	std::snprintf(o->detail, sizeof o->detail,
		"set=%08lX alloc=%08lX type=%08lX/%lu size=%lu fmt=%u/%u/%lu/%lu/%u/%u/%u",
		HX(hs), HX(ha), HX(ht), (unsigned long) ty, (unsigned long) cb,
		out.wFormatTag, out.nChannels, (unsigned long) out.nSamplesPerSec,
		(unsigned long) out.nAvgBytesPerSec, out.nBlockAlign,
		out.wBitsPerSample, out.cbSize);
	src->Release();
}

void s_allocate_order(ScenarioContext *c, StepResult *o)
{
	IA3dSource2 *src = new_source(c, A3DSOURCE_TYPEDEFAULT, o);
	if (!src)
		return;
	HRESULT ha = src->AllocateAudioData(4410);
	DWORD   c1 = src->GetAudioSize();
	HRESULT hf = src->FreeAudioData();
	HRESULT h2 = src->FreeAudioData();
	WAVEFORMATEX in = mono16();
	HRESULT hs = src->SetAudioFormat(&in);
	HRESULT hz = src->AllocateAudioData(0);
	HRESULT hn = src->AllocateAudioData(-1);
	o->hr = ha;
	std::snprintf(o->detail, sizeof o->detail,
		"alloc=%08lX size=%lu free=%08lX free2=%08lX set=%08lX zero=%08lX neg=%08lX",
		HX(ha), (unsigned long) c1, HX(hf), HX(h2), HX(hs), HX(hz), HX(hn));
	src->Release();
}

void s_audio_format_null(ScenarioContext *c, StepResult *o)
{
	IA3dSource2 *src = new_source(c, A3DSOURCE_TYPEDEFAULT, o);
	if (!src)
		return;
	HRESULT hs = src->SetAudioFormat(NULL);
	o->hr = src->GetAudioFormat(NULL);
	std::snprintf(o->detail, sizeof o->detail, "set=%08lX", HX(hs));
	src->Release();
}

void s_lock_unlock(ScenarioContext *c, StepResult *o)
{
	IA3dSource2 *src = new_source(c, A3DSOURCE_TYPEDEFAULT, o);
	if (!src)
		return;
	WAVEFORMATEX in = mono16();
	src->SetAudioFormat(&in);
	HRESULT ha = src->AllocateAudioData(4410);

	void   *p1, *p2;
	DWORD   c1, c2;
	char    part[4][32];
	struct { DWORD off, cb, flags; } req[4] = {
		{ 0,    1000, 0 },
		{ 4000, 1000, 0 },
		{ 0,    0,    DSBLOCK_ENTIREBUFFER },
		{ 5000, 10,   0 },
	};
	for (int i = 0; i < 4; i++) {
		p1 = p2 = NULL;
		c1 = c2 = 0xFFFFFFFF;
		HRESULT hl = src->Lock(req[i].off, req[i].cb, &p1, &c1, &p2, &c2, req[i].flags);
		if (SUCCEEDED(hl))
			src->Unlock(p1, c1, p2, c2);
		std::snprintf(part[i], sizeof part[i], "%08lX:%ld/%ld/%d", HX(hl),
			(long) c1, (long) c2, p2 ? 1 : 0);
	}
	o->hr = ha;
	std::snprintf(o->detail, sizeof o->detail, "%s %s %s %s",
		part[0], part[1], part[2], part[3]);
	src->Release();
}

void s_lock_without_data(ScenarioContext *c, StepResult *o)
{
	IA3dSource2 *src = new_source(c, A3DSOURCE_TYPEDEFAULT, o);
	if (!src)
		return;
	void  *p1 = NULL, *p2 = NULL;
	DWORD  c1 = 0, c2 = 0;
	o->hr = src->Lock(0, 100, &p1, &c1, &p2, &c2, 0);
	HRESULT hu = src->Unlock(NULL, 0, NULL, 0);
	std::snprintf(o->detail, sizeof o->detail, "unlock=%08lX", HX(hu));
	src->Release();
}

void s_play_position(ScenarioContext *c, StepResult *o)
{
	IA3dSource2 *src = loaded_source(c, o);
	if (!src)
		return;
	DWORD  pos[3] = { 0, 0, 0 };
	A3DVAL tm = -1.0f;
	HRESULT h1 = src->SetPlayPosition(1000);
	src->GetPlayPosition(&pos[0]);
	HRESULT h2 = src->SetPlayTime(0.05f);
	src->GetPlayTime(&tm);
	src->GetPlayPosition(&pos[1]);
	HRESULT h3 = src->Rewind();
	src->GetPlayPosition(&pos[2]);
	HRESULT h4 = src->SetPlayPosition(0x7FFFFFF0);
	HRESULT h5 = src->SetPlayTime(1000.0f);
	HRESULT h6 = src->SetPlayTime(-1.0f);
	o->hr = h1;
	std::snprintf(o->detail, sizeof o->detail,
		"pos=%lu time=%08lX/%.4f/%lu rew=%08lX/%lu big=%08lX late=%08lX neg=%08lX",
		(unsigned long) pos[0], HX(h2), (double) tm, (unsigned long) pos[1],
		HX(h3), (unsigned long) pos[2], HX(h4), HX(h5), HX(h6));
	src->Release();
}

void s_play_position_empty(ScenarioContext *c, StepResult *o)
{
	IA3dSource2 *src = new_source(c, A3DSOURCE_TYPEDEFAULT, o);
	if (!src)
		return;
	DWORD  pos = 0xFFFFFFFF;
	A3DVAL tm = -1.0f;
	HRESULT h1 = src->SetPlayPosition(10);
	HRESULT h2 = src->GetPlayPosition(&pos);
	HRESULT h3 = src->SetPlayTime(0.1f);
	HRESULT h4 = src->GetPlayTime(&tm);
	HRESULT h5 = src->Rewind();
	o->hr = h1;
	std::snprintf(o->detail, sizeof o->detail,
		"getpos=%08lX/%lu settime=%08lX gettime=%08lX/%.3f rew=%08lX",
		HX(h2), (unsigned long) pos, HX(h3), HX(h4), (double) tm, HX(h5));
	src->Release();
}

void s_play_events(ScenarioContext *c, StepResult *o)
{
	IA3dSource2 *src = loaded_source(c, o);
	if (!src)
		return;
	HANDLE h = CreateEventA(NULL, FALSE, FALSE, NULL);
	HRESULT e1 = src->SetPlayEvent(100, h);
	HRESULT e2 = src->SetPlayEvent(200, h);
	HRESULT e3 = src->SetPlayEvent(100, h);
	HRESULT e4 = src->SetPlayEvent(0x7FFFFFF0, h);
	HRESULT e5 = src->SetPlayEvent(300, NULL);
	HRESULT c1 = src->ClearPlayEvents();
	HRESULT c2 = src->ClearPlayEvents();
	o->hr = e1;
	std::snprintf(o->detail, sizeof o->detail,
		"set2=%08lX dup=%08lX big=%08lX null=%08lX clear=%08lX/%08lX",
		HX(e2), HX(e3), HX(e4), HX(e5), HX(c1), HX(c2));
	src->Release();
	CloseHandle(h);
}

void s_play_events_empty(ScenarioContext *c, StepResult *o)
{
	IA3dSource2 *src = new_source(c, A3DSOURCE_TYPEDEFAULT, o);
	if (!src)
		return;
	HANDLE h = CreateEventA(NULL, FALSE, FALSE, NULL);
	o->hr = src->SetPlayEvent(100, h);
	HRESULT hc = src->ClearPlayEvents();
	std::snprintf(o->detail, sizeof o->detail, "clear=%08lX", HX(hc));
	src->Release();
	CloseHandle(h);
}

void s_pan_values(ScenarioContext *c, StepResult *o)
{
	IA3dSource2 *src = loaded_source(c, o);
	if (!src)
		return;
	A3DVAL in[2] = { 0.25f, 0.75f };
	A3DVAL out[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
	A3DVAL one[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
	HRESULT hs = src->SetPanValues(2, in);
	HRESULT hg = src->GetPanValues(2, out);
	HRESULT h1 = src->GetPanValues(1, one);
	HRESULT h0 = src->SetPanValues(0, in);
	HRESULT h4 = src->GetPanValues(4, out);
	o->hr = hs;
	std::snprintf(o->detail, sizeof o->detail,
		"get=%08lX/%.3f,%.3f one=%08lX/%.3f,%.3f zero=%08lX four=%08lX/%.3f,%.3f",
		HX(hg), (double) out[0], (double) out[1], HX(h1), (double) one[0],
		(double) one[1], HX(h0), HX(h4), (double) out[2], (double) out[3]);
	src->Release();
}

void s_pan_values_empty(ScenarioContext *c, StepResult *o)
{
	IA3dSource2 *src = new_source(c, A3DSOURCE_TYPEDEFAULT, o);
	if (!src)
		return;
	A3DVAL v[2] = { 0.5f, 0.5f };
	o->hr = src->SetPanValues(2, v);
	v[0] = v[1] = -1.0f;
	HRESULT hg = src->GetPanValues(2, v);
	std::snprintf(o->detail, sizeof o->detail, "get=%08lX/%.3f,%.3f",
		HX(hg), (double) v[0], (double) v[1]);
	src->Release();
}

void s_volumetric(ScenarioContext *c, StepResult *o)
{
	IA3dSource2 *src = loaded_source(c, o);
	if (!src)
		return;
	A3DVAL x = -1, y = -1, z = -1, x0 = -1, y0 = -1, z0 = -1;
	src->GetVolumetricBounds(&x0, &y0, &z0);
	HRESULT hb = src->SetVolumetricBounds(1.0f, 2.0f, 3.0f);
	HRESULT hg = src->GetVolumetricBounds(&x, &y, &z);

	A3DVOLSRCDAMPINFO def, in, out;
	std::memset(&def, 0, sizeof def);
	def.dwSize = sizeof def;
	src->GetVolumetricDamping(&def);
	in.dwSize = sizeof in;
	in.fAzimuthPan     = 0.5f;
	in.fSizeDampMin    = 0.25f;
	in.fDampWeighting  = 0.75f;
	in.nTestPointsMax  = 4;
	in.bMonoInside     = TRUE;
	HRESULT hd = src->SetVolumetricDamping(&in);
	std::memset(&out, 0, sizeof out);
	out.dwSize = sizeof out;
	HRESULT hr = src->GetVolumetricDamping(&out);
	o->hr = hb;
	std::snprintf(o->detail, sizeof o->detail,
		"def=%.2f,%.2f,%.2f/%.2f,%.2f,%.2f,%d,%d get=%08lX/%.1f,%.1f,%.1f "
		"damp=%08lX/%08lX/%.2f,%.2f,%.2f,%d,%d",
		(double) x0, (double) y0, (double) z0, (double) def.fAzimuthPan,
		(double) def.fSizeDampMin, (double) def.fDampWeighting,
		def.nTestPointsMax, def.bMonoInside, HX(hg), (double) x, (double) y,
		(double) z, HX(hd), HX(hr), (double) out.fAzimuthPan,
		(double) out.fSizeDampMin, (double) out.fDampWeighting,
		out.nTestPointsMax, out.bMonoInside);
	src->Release();
}

void s_volumetric_bad_size(ScenarioContext *c, StepResult *o)
{
	IA3dSource2 *src = loaded_source(c, o);
	if (!src)
		return;
	A3DVOLSRCDAMPINFO info;
	std::memset(&info, 0, sizeof info);
	info.dwSize = 0;
	HRESULT hs = src->SetVolumetricDamping(&info);
	info.dwSize = 0;
	HRESULT hg = src->GetVolumetricDamping(&info);
	HRESULT hb = src->SetVolumetricBounds(-1.0f, 0.0f, 1.0f);
	A3DVAL x = 9, y = 9, z = 9;
	src->GetVolumetricBounds(&x, &y, &z);
	o->hr = hs;
	std::snprintf(o->detail, sizeof o->detail, "get=%08lX neg=%08lX/%.1f,%.1f,%.1f",
		HX(hg), HX(hb), (double) x, (double) y, (double) z);
	src->Release();
}

void s_duplicate(ScenarioContext *c, StepResult *o)
{
	IA3dSource2 *src = loaded_source(c, o);
	if (!src)
		return;
	src->SetGain(0.5f);
	src->SetPitch(1.5f);
	IA3dSource2 *dup = NULL;
	o->hr = c->pRoot->DuplicateSource(src, &dup);
	DWORD  ty = 0, cb = 0;
	A3DVAL g = -1, p = -1;
	if (dup) {
		dup->GetType(&ty);
		cb = dup->GetAudioSize();
		dup->GetGain(&g);
		dup->GetPitch(&p);
	}
	IA3dSource2 *dup2 = NULL;
	HRESULT hn = c->pRoot->DuplicateSource(NULL, &dup2);
	std::snprintf(o->detail, sizeof o->detail,
		"dup=%d type=%lu size=%lu gain=%.3f pitch=%.3f null=%08lX/%d",
		dup ? 1 : 0, (unsigned long) ty, (unsigned long) cb, (double) g,
		(double) p, HX(hn), dup2 ? 1 : 0);
	if (dup2)
		dup2->Release();
	if (dup)
		dup->Release();
	src->Release();
}

void s_duplicate_empty(ScenarioContext *c, StepResult *o)
{
	IA3dSource2 *src = new_source(c, A3DSOURCE_TYPEDEFAULT, o);
	if (!src)
		return;
	IA3dSource2 *dup = NULL;
	o->hr = c->pRoot->DuplicateSource(src, &dup);
	std::snprintf(o->detail, sizeof o->detail, "dup=%d", dup ? 1 : 0);
	if (dup)
		dup->Release();
	src->Release();
}

void s_get_caps(ScenarioContext *c, StepResult *o)
{
	IA3dSource2 *src = loaded_source(c, o);
	if (!src)
		return;
	A3DCAPS_SOURCE caps;
	std::memset(&caps, 0, sizeof caps);
	caps.dwSize = sizeof caps;
	o->hr = src->GetCaps(&caps);
	A3DCAPS_SOURCE bad;
	std::memset(&bad, 0, sizeof bad);
	bad.dwSize = 4;
	HRESULT hb = src->GetCaps(&bad);
	std::snprintf(o->detail, sizeof o->detail,
		"size=%lu type=%lu name=%d wave=%lu/%u/%lu/%lu/%u/%u bad=%08lX",
		(unsigned long) sizeof caps, (unsigned long) caps.dwType,
		caps.szFilename ? 1 : 0, (unsigned long) caps.data.waveFormat.dwSize,
		caps.data.waveFormat.nChannels,
		(unsigned long) caps.data.waveFormat.nSamplesPerSec,
		(unsigned long) caps.data.waveFormat.nAvgBytesPerSec,
		caps.data.waveFormat.nBlockAlign, caps.data.waveFormat.wBitsPerSample,
		HX(hb));
	src->Release();
}

void s_get_caps_empty(ScenarioContext *c, StepResult *o)
{
	IA3dSource2 *src = new_source(c, A3DSOURCE_TYPEDEFAULT, o);
	if (!src)
		return;
	A3DCAPS_SOURCE caps;
	std::memset(&caps, 0, sizeof caps);
	caps.dwSize = sizeof caps;
	o->hr = src->GetCaps(&caps);
	std::snprintf(o->detail, sizeof o->detail, "type=%lu name=%d",
		(unsigned long) caps.dwType, caps.szFilename ? 1 : 0);
	src->Release();
}

void s_streaming_props(ScenarioContext *c, StepResult *o)
{
	DWORD a0 = 0, b0 = 0, a1 = 0, b1 = 0, a2 = 0, b2 = 0;
	c->pRoot->GetStreamingProperties(&a0, &b0);
	o->hr = c->pRoot->SetStreamingProperties(500, A3D_STREAMING_PRIORITY_HIGH);
	c->pRoot->GetStreamingProperties(&a1, &b1);
	HRESULT h0 = c->pRoot->SetStreamingProperties(0, 99);
	c->pRoot->GetStreamingProperties(&a2, &b2);
	c->pRoot->SetStreamingProperties(a0, b0);
	std::snprintf(o->detail, sizeof o->detail,
		"def=%lu/%lu set=%lu/%lu bad=%08lX/%lu/%lu",
		(unsigned long) a0, (unsigned long) b0, (unsigned long) a1,
		(unsigned long) b1, HX(h0), (unsigned long) a2, (unsigned long) b2);
}

void s_null_playpos(ScenarioContext *c, StepResult *o)
{
	IA3dSource2 *src = loaded_source(c, o);
	if (!src)
		return;
	o->hr = src->GetPlayPosition(NULL);
	o->detail[0] = 0;
	src->Release();
}

void s_null_playtime(ScenarioContext *c, StepResult *o)
{
	IA3dSource2 *src = loaded_source(c, o);
	if (!src)
		return;
	o->hr = src->GetPlayTime(NULL);
	o->detail[0] = 0;
	src->Release();
}

void s_null_pan(ScenarioContext *c, StepResult *o)
{
	IA3dSource2 *src = loaded_source(c, o);
	if (!src)
		return;
	o->hr = src->GetPanValues(2, NULL);
	o->detail[0] = 0;
	src->Release();
}

void s_null_bounds(ScenarioContext *c, StepResult *o)
{
	IA3dSource2 *src = loaded_source(c, o);
	if (!src)
		return;
	o->hr = src->GetVolumetricBounds(NULL, NULL, NULL);
	o->detail[0] = 0;
	src->Release();
}

void s_null_damping(ScenarioContext *c, StepResult *o)
{
	IA3dSource2 *src = loaded_source(c, o);
	if (!src)
		return;
	o->hr = src->SetVolumetricDamping(NULL);
	HRESULT hg = src->GetVolumetricDamping(NULL);
	std::snprintf(o->detail, sizeof o->detail, "get=%08lX", HX(hg));
	src->Release();
}

void s_null_caps(ScenarioContext *c, StepResult *o)
{
	IA3dSource2 *src = loaded_source(c, o);
	if (!src)
		return;
	o->hr = src->GetCaps(NULL);
	o->detail[0] = 0;
	src->Release();
}

void s_null_type(ScenarioContext *c, StepResult *o)
{
	IA3dSource2 *src = loaded_source(c, o);
	if (!src)
		return;
	o->hr = src->GetType(NULL);
	o->detail[0] = 0;
	src->Release();
}

#define SD_MANY_SOURCES 48

void s_many_playing(ScenarioContext *c, StepResult *o)
{
	IA3dSource2 *apSrc[SD_MANY_SOURCES];
	int n, i, nPlaying = 0, nFailed = 0;
	HRESULT hr;

	std::memset(apSrc, 0, sizeof apSrc);
	HRESULT hc = c->pRoot->Compat(1000, 1);
	for (n = 0; n < SD_MANY_SOURCES; n++) {
		apSrc[n] = loaded_source(c, o);
		if (!apSrc[n])
			break;
		apSrc[n]->SetPosition3f((A3DVAL) (n % 8) - 4.0f, 0.0f, -2.0f - (A3DVAL) (n / 8));
		if (FAILED(apSrc[n]->Play(A3D_LOOPED)))
			nFailed++;
	}
	hr = c->pRoot->Flush();
	Sleep(200);
	c->pRoot->Flush();
	for (i = 0; i < n; i++) {
		DWORD dwStatus = 0;
		apSrc[i]->GetStatus(&dwStatus);
		if (dwStatus & A3DSTATUS_PLAYING)
			nPlaying++;
	}
	for (i = 0; i < n; i++)
		apSrc[i]->Stop();
	c->pRoot->Flush();
	for (i = 0; i < n; i++)
		apSrc[i]->Release();
	c->pRoot->Compat(1000, 0);
	o->hr = hr;
	std::snprintf(o->detail, sizeof o->detail, "compat=%08lX created=%d playfail=%d playing=%d",
		      HX(hc), n, nFailed, nPlaying);
}
const struct { const char *name; step_fn fn; } source_data_steps[] = {
	{ "SrcData AudioFormat",       s_audio_format },
	{ "SrcData AllocateOrder",     s_allocate_order },
	{ "SrcData AudioFormatNull",   s_audio_format_null },
	{ "SrcData LockUnlock",        s_lock_unlock },
	{ "SrcData LockWithoutData",   s_lock_without_data },
	{ "SrcData PlayPosition",      s_play_position },
	{ "SrcData PlayPositionEmpty", s_play_position_empty },
	{ "SrcData PlayEvents",        s_play_events },
	{ "SrcData PlayEventsEmpty",   s_play_events_empty },
	{ "SrcData PanValues",         s_pan_values },
	{ "SrcData PanValuesEmpty",    s_pan_values_empty },
	{ "SrcData Volumetric",        s_volumetric },
	{ "SrcData VolumetricBadSize", s_volumetric_bad_size },
	{ "SrcData Duplicate",         s_duplicate },
	{ "SrcData DuplicateEmpty",    s_duplicate_empty },
	{ "SrcData GetCaps",           s_get_caps },
	{ "SrcData GetCapsEmpty",      s_get_caps_empty },
	{ "SrcData StreamingProps",    s_streaming_props },
	{ "SrcData NullPlayPosition",  s_null_playpos },
	{ "SrcData NullPlayTime",      s_null_playtime },
	{ "SrcData NullPanValues",     s_null_pan },
	{ "SrcData NullBounds",        s_null_bounds },
	{ "SrcData NullDamping",       s_null_damping },
	{ "SrcData NullCaps",          s_null_caps },
	{ "SrcData NullType",          s_null_type },
	{ "SrcData ManyPlaying",       s_many_playing },
};

}	/* namespace */

namespace a3ddiff {

void add_source_data_steps(Script &s)
{
	for (const auto &step : source_data_steps)
		s.push_back({ step.name, step.fn });
}

}	/* namespace a3ddiff */

class SourceDataVsReference : public InterfaceVsReference,
			      public ::testing::WithParamInterface<const char *> {};

TEST_P(SourceDataVsReference, Step)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity(GetParam());
}

INSTANTIATE_TEST_SUITE_P(Source, SourceDataVsReference, ::testing::Values(
	"SrcData AudioFormat", "SrcData AllocateOrder", "SrcData AudioFormatNull",
	"SrcData LockUnlock", "SrcData LockWithoutData", "SrcData PlayPosition",
	"SrcData PlayPositionEmpty", "SrcData PlayEvents", "SrcData PlayEventsEmpty",
	"SrcData PanValues", "SrcData PanValuesEmpty", "SrcData Volumetric",
	"SrcData VolumetricBadSize", "SrcData Duplicate", "SrcData DuplicateEmpty",
	"SrcData GetCaps", "SrcData GetCapsEmpty", "SrcData StreamingProps",
	"SrcData NullPlayPosition", "SrcData NullPlayTime", "SrcData NullPanValues",
	"SrcData NullBounds", "SrcData NullDamping", "SrcData NullCaps",
	"SrcData NullType", "SrcData ManyPlaying"));
