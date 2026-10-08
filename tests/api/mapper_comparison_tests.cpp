/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * mapper_comparison_tests.cpp - Interface comparison: the DirectSound
 * compatibility objects CLSID_A3dApi returns for IID_IDirectSound,
 * IID_IA3d and IID_IA3d2.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling. See tests/README.md.
 *
 * Uses --mapper-child because a second engine cannot share the root's process.
 *
 *---------------------------------------------------------------------------
 */

#include "comparison_fixture.hpp"

#include <cstring>
#include <mmreg.h>
#include <dsound.h>
#include "ia3ddal.h"

#include "com_server_isolation.hpp"
#include "com_lifetime.hpp"
#include "process_audio_mute.hpp"
#include "crt_error_reporting.hpp"
#include "com_dll_loader.hpp"

using namespace a3ddiff;
using namespace a3dtest;

namespace {

#define HX(h) ((unsigned long) (h))

typedef void (*mapper_fn)(const ComDllLoader *pDll, StepResult *o);

IDirectSound *new_mapper(const ComDllLoader *pDll, StepResult *o)
{
	IDirectSound *pDS = NULL;
	o->hr = pDll->create(CLSID_A3dApi, IID_IDirectSound, (void **) &pDS);
	if (FAILED(o->hr) || !pDS) {
		std::snprintf(o->detail, sizeof o->detail, "create");
		return (NULL);
	}
	o->hr = pDS->SetCooperativeLevel(GetDesktopWindow(), DSSCL_PRIORITY);
	return (pDS);
}

WAVEFORMATEX pcm(WORD ch, DWORD rate, WORD bits)
{
	WAVEFORMATEX w;
	std::memset(&w, 0, sizeof w);
	w.wFormatTag      = WAVE_FORMAT_PCM;
	w.nChannels       = ch;
	w.nSamplesPerSec  = rate;
	w.wBitsPerSample  = bits;
	w.nBlockAlign     = (WORD) (ch * bits / 8);
	w.nAvgBytesPerSec = rate * w.nBlockAlign;
	return w;
}

IDirectSoundBuffer *new_secondary(IDirectSound *pDS, DWORD dwFlags, HRESULT *phr)
{
	WAVEFORMATEX w = pcm(1, 22050, 16);
	DSBUFFERDESC d;
	std::memset(&d, 0, sizeof d);
	d.dwSize        = sizeof d;
	d.dwFlags       = dwFlags;
	d.dwBufferBytes = 22050 * 2;
	d.lpwfxFormat   = &w;
	IDirectSoundBuffer *pBuf = NULL;
	*phr = pDS->CreateSoundBuffer(&d, &pBuf, NULL);
	return (pBuf);
}

void m_create(const ComDllLoader *pDll, StepResult *o)
{
	IDirectSound *pDS = new_mapper(pDll, o);
	if (!pDS)
		return;
	HRESULT hc = o->hr;
	void *p1 = NULL, *p2 = NULL, *p5 = NULL, *pu = NULL;
	HRESULT q1 = pDS->QueryInterface(IID_IA3d, &p1);
	HRESULT q2 = pDS->QueryInterface(IID_IA3d2, &p2);
	HRESULT q5 = pDS->QueryInterface(IID_IA3d5, &p5);
	HRESULT qu = pDS->QueryInterface(IID_IUnknown, &pu);
	for (void *p : { p1, p2, p5, pu })
		if (p)
			((IUnknown *) p)->Release();
	o->hr = hc;
	std::snprintf(o->detail, sizeof o->detail, "qi=%08lX/%08lX/%08lX/%08lX",
		HX(q1), HX(q2), HX(q5), HX(qu));
	pDS->Release();
}

void m_create_other(const ComDllLoader *pDll, StepResult *o)
{
	void *pA = NULL, *pB = NULL;
	o->hr = pDll->create(CLSID_A3dApi, IID_IA3d, &pA);
	if (pA)
		((IUnknown *) pA)->Release();
	HRESULT h2 = pDll->create(CLSID_A3dApi, IID_IA3d2, &pB);
	if (pB)
		((IUnknown *) pB)->Release();
	std::snprintf(o->detail, sizeof o->detail, "ia3d=%d ia3d2=%08lX/%d",
		pA ? 1 : 0, HX(h2), pB ? 1 : 0);
}

void m_device(const ComDllLoader *pDll, StepResult *o)
{
	IDirectSound *pDS = new_mapper(pDll, o);
	if (!pDS)
		return;
	DSCAPS caps;
	std::memset(&caps, 0, sizeof caps);
	caps.dwSize = sizeof caps;
	HRESULT hc = pDS->GetCaps(&caps);
	DWORD   spk = 0xFFFFFFFF;
	HRESULT hg = pDS->GetSpeakerConfig(&spk);
	HRESULT hs = pDS->SetSpeakerConfig(DSSPEAKER_STEREO);
	HRESULT hk = pDS->Compact();
	HRESULT hi = pDS->Initialize(NULL);
	DSCAPS bad;
	std::memset(&bad, 0, sizeof bad);
	HRESULT hb = pDS->GetCaps(&bad);
	o->hr = hc;
	std::snprintf(o->detail, sizeof o->detail,
		"flags=%08lX hw=%lu/%lu spk=%08lX/%lu set=%08lX compact=%08lX init=%08lX bad=%08lX",
		HX(caps.dwFlags), (unsigned long) caps.dwMaxHwMixingAllBuffers,
		(unsigned long) caps.dwMaxHw3DAllBuffers, HX(hg), (unsigned long) spk,
		HX(hs), HX(hk), HX(hi), HX(hb));
	pDS->Release();
}

void m_primary(const ComDllLoader *pDll, StepResult *o)
{
	IDirectSound *pDS = new_mapper(pDll, o);
	if (!pDS)
		return;
	DSBUFFERDESC d;
	std::memset(&d, 0, sizeof d);
	d.dwSize  = sizeof d;
	d.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D;
	IDirectSoundBuffer *pPrim = NULL;
	o->hr = pDS->CreateSoundBuffer(&d, &pPrim, NULL);
	if (FAILED(o->hr) || !pPrim) {
		std::snprintf(o->detail, sizeof o->detail, "primary");
		pDS->Release();
		return;
	}
	WAVEFORMATEX f0, f1, set = pcm(2, 22050, 16);
	std::memset(&f0, 0, sizeof f0);
	std::memset(&f1, 0, sizeof f1);
	DWORD cb = 0;
	pPrim->GetFormat(&f0, sizeof f0, &cb);
	HRESULT hs = pPrim->SetFormat(&set);
	pPrim->GetFormat(&f1, sizeof f1, NULL);
	LONG vol = 1;
	HRESULT hv = pPrim->GetVolume(&vol);
	DSBCAPS bc;
	std::memset(&bc, 0, sizeof bc);
	bc.dwSize = sizeof bc;
	HRESULT hc = pPrim->GetCaps(&bc);
	std::snprintf(o->detail, sizeof o->detail,
		"fmt=%u/%lu/%u set=%08lX now=%u/%lu/%u vol=%08lX/%ld caps=%08lX/%08lX",
		f0.nChannels, (unsigned long) f0.nSamplesPerSec, f0.wBitsPerSample, HX(hs),
		f1.nChannels, (unsigned long) f1.nSamplesPerSec, f1.wBitsPerSample,
		HX(hv), (long) vol, HX(hc), HX(bc.dwFlags));
	pPrim->Release();
	pDS->Release();
}

void m_listener(const ComDllLoader *pDll, StepResult *o)
{
	IDirectSound *pDS = new_mapper(pDll, o);
	if (!pDS)
		return;
	DSBUFFERDESC d;
	std::memset(&d, 0, sizeof d);
	d.dwSize  = sizeof d;
	d.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D;
	IDirectSoundBuffer *pPrim = NULL;
	pDS->CreateSoundBuffer(&d, &pPrim, NULL);
	IDirectSound3DListener *pL = NULL;
	o->hr = pPrim ? pPrim->QueryInterface(IID_IDirectSound3DListener, (void **) &pL) : E_FAIL;
	if (!pL) {
		std::snprintf(o->detail, sizeof o->detail, "no-listener");
		if (pPrim)
			pPrim->Release();
		pDS->Release();
		return;
	}
	D3DVECTOR p = { 0, 0, 0 }, f = { 0, 0, 0 }, u = { 0, 0, 0 };
	D3DVALUE df = 0, rf = 0, dp = 0;
	HRESULT h1 = pL->SetPosition(1.0f, 2.0f, 3.0f, DS3D_IMMEDIATE);
	pL->GetPosition(&p);
	HRESULT h2 = pL->SetDistanceFactor(2.0f, DS3D_IMMEDIATE);
	pL->GetDistanceFactor(&df);
	HRESULT h3 = pL->SetRolloffFactor(1.5f, DS3D_IMMEDIATE);
	pL->GetRolloffFactor(&rf);
	HRESULT h4 = pL->SetDopplerFactor(0.5f, DS3D_IMMEDIATE);
	pL->GetDopplerFactor(&dp);
	HRESULT h5 = pL->SetOrientation(0, 0, 1, 0, 1, 0, DS3D_DEFERRED);
	HRESULT h6 = pL->CommitDeferredSettings();
	pL->GetOrientation(&f, &u);
	HRESULT h7 = pL->SetVelocity(1, 0, 0, DS3D_IMMEDIATE);
	std::snprintf(o->detail, sizeof o->detail,
		"set=%08lX,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX pos=%.1f,%.1f,%.1f df=%.2f rf=%.2f dp=%.2f front=%.1f,%.1f,%.1f",
		HX(h1), HX(h2), HX(h3), HX(h4), HX(h5), HX(h6), HX(h7), p.x, p.y, p.z,
		df, rf, dp, f.x, f.y, f.z);
	pL->Release();
	pPrim->Release();
	pDS->Release();
}

void m_secondary(const ComDllLoader *pDll, StepResult *o)
{
	IDirectSound *pDS = new_mapper(pDll, o);
	if (!pDS)
		return;
	IDirectSoundBuffer *pBuf = new_secondary(pDS,
		DSBCAPS_CTRL3D | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLFREQUENCY, &o->hr);
	if (!pBuf) {
		std::snprintf(o->detail, sizeof o->detail, "secondary");
		pDS->Release();
		return;
	}
	DSBCAPS bc;
	std::memset(&bc, 0, sizeof bc);
	bc.dwSize = sizeof bc;
	HRESULT hc = pBuf->GetCaps(&bc);
	void *p1 = NULL, *p2 = NULL;
	DWORD c1 = 0, c2 = 0;
	HRESULT hl = pBuf->Lock(0, 1000, &p1, &c1, &p2, &c2, 0);
	if (SUCCEEDED(hl) && p1 && c1 <= 44100)
		std::memset(p1, 0, c1);
	HRESULT hu = SUCCEEDED(hl) ? pBuf->Unlock(p1, c1, p2, c2) : E_FAIL;
	LONG vol = 1;
	HRESULT hv = pBuf->SetVolume(-1000);
	pBuf->GetVolume(&vol);
	DWORD freq = 0;
	HRESULT hf = pBuf->SetFrequency(11025);
	pBuf->GetFrequency(&freq);
	HRESULT hp = pBuf->SetCurrentPosition(100);
	DWORD st0 = 0, st1 = 0, st2 = 0;
	pBuf->GetStatus(&st0);
	HRESULT hy = pBuf->Play(0, 0, DSBPLAY_LOOPING);
	pBuf->GetStatus(&st1);
	HRESULT hs = pBuf->Stop();
	pBuf->GetStatus(&st2);
	std::snprintf(o->detail, sizeof o->detail,
		"caps=%08lX/%08lX/%lu lock=%08lX/%lu/%lu unlock=%08lX vol=%08lX/%ld freq=%08lX/%lu pos=%08lX st=%lu,%lu,%lu play=%08lX stop=%08lX",
		HX(hc), HX(bc.dwFlags), (unsigned long) bc.dwBufferBytes, HX(hl),
		(unsigned long) c1, (unsigned long) c2, HX(hu), HX(hv), (long) vol,
		HX(hf), (unsigned long) freq, HX(hp), (unsigned long) st0,
		(unsigned long) st1, (unsigned long) st2, HX(hy), HX(hs));
	pBuf->Release();
	pDS->Release();
}

void m_secondary_3d(const ComDllLoader *pDll, StepResult *o)
{
	IDirectSound *pDS = new_mapper(pDll, o);
	if (!pDS)
		return;
	IDirectSoundBuffer *pBuf = new_secondary(pDS, DSBCAPS_CTRL3D, &o->hr);
	IDirectSound3DBuffer *p3 = NULL;
	if (pBuf)
		o->hr = pBuf->QueryInterface(IID_IDirectSound3DBuffer, (void **) &p3);
	if (!p3) {
		std::snprintf(o->detail, sizeof o->detail, "no-3d");
		if (pBuf)
			pBuf->Release();
		pDS->Release();
		return;
	}
	D3DVECTOR pos = { 0, 0, 0 }, ori = { 0, 0, 0 };
	D3DVALUE  mn = 0, mx = 0;
	DWORD     mode = 9, ci = 0, co = 0;
	LONG      cov = 1;
	HRESULT h1 = p3->SetPosition(3, 0, -4, DS3D_IMMEDIATE);
	p3->GetPosition(&pos);
	HRESULT h2 = p3->SetMinDistance(2.0f, DS3D_IMMEDIATE);
	p3->GetMinDistance(&mn);
	HRESULT h3 = p3->SetMaxDistance(50.0f, DS3D_IMMEDIATE);
	p3->GetMaxDistance(&mx);
	HRESULT h4 = p3->SetMode(DS3DMODE_HEADRELATIVE, DS3D_IMMEDIATE);
	p3->GetMode(&mode);
	HRESULT h5 = p3->SetConeAngles(30, 90, DS3D_IMMEDIATE);
	p3->GetConeAngles(&ci, &co);
	HRESULT h6 = p3->SetConeOrientation(1, 0, 0, DS3D_IMMEDIATE);
	p3->GetConeOrientation(&ori);
	HRESULT h7 = p3->SetConeOutsideVolume(-2000, DS3D_IMMEDIATE);
	p3->GetConeOutsideVolume(&cov);
	DS3DBUFFER all;
	std::memset(&all, 0, sizeof all);
	all.dwSize = sizeof all;
	HRESULT h8 = p3->GetAllParameters(&all);
	std::snprintf(o->detail, sizeof o->detail,
		"set=%08lX,%08lX,%08lX,%08lX,%08lX,%08lX,%08lX pos=%.1f,%.1f,%.1f d=%.1f/%.1f mode=%lu cone=%lu/%lu/%.0f/%ld all=%08lX/%lu",
		HX(h1), HX(h2), HX(h3), HX(h4), HX(h5), HX(h6), HX(h7), pos.x, pos.y,
		pos.z, mn, mx, (unsigned long) mode, (unsigned long) ci,
		(unsigned long) co, ori.x, (long) cov, HX(h8), (unsigned long) all.dwMode);
	p3->Release();
	pBuf->Release();
	pDS->Release();
}

void m_duplicate(const ComDllLoader *pDll, StepResult *o)
{
	IDirectSound *pDS = new_mapper(pDll, o);
	if (!pDS)
		return;
	HRESULT hc;
	IDirectSoundBuffer *pBuf = new_secondary(pDS, DSBCAPS_CTRLFREQUENCY, &hc);
	IDirectSoundBuffer *pDup = NULL;
	o->hr = pBuf ? pDS->DuplicateSoundBuffer(pBuf, &pDup) : hc;
	DWORD freq = 0;
	if (pDup)
		pDup->GetFrequency(&freq);
	IDirectSoundBuffer *pNull = NULL;
	HRESULT hn = pDS->CreateSoundBuffer(NULL, &pNull, NULL);
	std::snprintf(o->detail, sizeof o->detail, "create=%08lX dup=%d freq=%lu null=%08lX/%d",
		HX(hc), pDup ? 1 : 0, (unsigned long) freq, HX(hn), pNull ? 1 : 0);
	if (pNull)
		pNull->Release();
	if (pDup)
		pDup->Release();
	if (pBuf)
		pBuf->Release();
	pDS->Release();
}

void m_ia3d(const ComDllLoader *pDll, StepResult *o)
{
	IDirectSound *pDS = new_mapper(pDll, o);
	if (!pDS)
		return;
	IA3d2 *p2 = NULL;
	o->hr = pDS->QueryInterface(IID_IA3d2, (void **) &p2);
	if (!p2) {
		std::snprintf(o->detail, sizeof o->detail, "no-ia3d2");
		pDS->Release();
		return;
	}
	DWORD a = 9, b = 9, c = 9, rm = 9;
	FLOAT hf = -1;
	HRESULT h1 = p2->SetOutputMode(OUTPUT_HEADPHONES, OUTPUT_HEADPHONES, OUTPUT_MODE_STEREO);
	p2->GetOutputMode(&a, &b, &c);
	HRESULT h2 = p2->SetResourceManagerMode(A3D_RESOURCE_MODE_DYNAMIC);
	p2->GetResourceManagerMode(&rm);
	HRESULT h3 = p2->SetHFAbsorbFactor(0.5f);
	p2->GetHFAbsorbFactor(&hf);
	HRESULT h4 = p2->RegisterVersion(A3D_CURRENT_VERSION);
	A3DCAPS_SOFTWARE sw;
	std::memset(&sw, 0, sizeof sw);
	sw.dwSize = sizeof sw;
	HRESULT h5 = p2->GetSoftwareCaps(&sw);
	A3DCAPS_HARDWARE hw;
	std::memset(&hw, 0, sizeof hw);
	hw.dwSize = sizeof hw;
	HRESULT h6 = p2->GetHardwareCaps(&hw);
	std::snprintf(o->detail, sizeof o->detail,
		"out=%08lX/%lu,%lu,%lu rm=%08lX/%lu hf=%08lX/%.2f reg=%08lX sw=%08lX/%08lX hw=%08lX/%08lX",
		HX(h1), (unsigned long) a, (unsigned long) b, (unsigned long) c, HX(h2),
		(unsigned long) rm, HX(h3), (double) hf, HX(h4), HX(h5), HX(sw.dwFlags),
		HX(h6), HX(hw.dwFlags));
	p2->Release();
	pDS->Release();
}

const struct { const char *name; mapper_fn fn; } mapper_steps[] = {
	{ "Mapper Create",      m_create },
	{ "Mapper CreateOther", m_create_other },
	{ "Mapper Device",      m_device },
	{ "Mapper Primary",     m_primary },
	{ "Mapper Listener",    m_listener },
	{ "Mapper Secondary",   m_secondary },
	{ "Mapper Secondary3D", m_secondary_3d },
	{ "Mapper Duplicate",   m_duplicate },
	{ "Mapper IA3d2",       m_ia3d },
};

/* No C++ object needing unwinding lives in a step, so __try is allowed. */
bool guarded(mapper_fn fn, const ComDllLoader *pDll, StepResult *o)
{
	__try {
		fn(pDll, o);
		return (true);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return (false);
	}
}

}	/* namespace */

namespace a3ddiff {

/* Entry point for --mapper-child <dll>: one STEP record per mapper step. */
int mapper_diff_child(const char *pszDll)
{
	crt_quiet();
	com_init ci;
	(void) ci;

	process_audio_mute mute;	/* Keep the real DirectSound device off the speakers. */

	clsid_isolation iso;
	if (!iso.deny(CLSID_A3d) || !iso.deny(CLSID_A3dDal)) {
		std::printf("ISOLATE\tFAIL\n");
		std::fflush(stdout);
		return (1);
	}

	ComDllLoader dll(pszDll);
	if (!dll.ok()) {
		std::printf("LOAD\tFAIL\t%s\n", pszDll);
		std::fflush(stdout);
		return (1);
	}

	for (const auto &step : mapper_steps) {
		StepResult o;
		o.hr = E_FAIL;
		o.detail[0] = 0;
		bool ok = guarded(step.fn, &dll, &o);
		emit(step.name, o.hr, !ok, o.detail);
	}

	std::printf("END\n");
	std::fflush(stdout);
	return (0);
}

}	/* namespace a3ddiff */

class MapperVsReference : public ::testing::TestWithParam<const char *> {
protected:
	static a3ddiff::ScenarioRunResult a;
	static a3ddiff::ScenarioRunResult b;

	static void SetUpTestSuite()
	{
		const std::string self = a3ddiff::self_path();
		a = a3ddiff::run_child(self, A3D_AB_OUR_DLL, "--mapper-child");
		b = a3ddiff::run_child(self, A3D_AB_REF_DLL, "--mapper-child");
	}
};

a3ddiff::ScenarioRunResult MapperVsReference::a;
a3ddiff::ScenarioRunResult MapperVsReference::b;

TEST_P(MapperVsReference, Step)
{
	ASSERT_TRUE(a.ended && b.ended) << "a child stopped before END (ours "
		<< (a.ended ? "ok" : "crashed") << ", ref " << (b.ended ? "ok" : "crashed") << ")";
	const a3ddiff::RecordedStepResult *sa = a3ddiff::find_step(a, GetParam());
	const a3ddiff::RecordedStepResult *sb = a3ddiff::find_step(b, GetParam());
	ASSERT_TRUE(sa && sb);
	EXPECT_EQ(sa->faulted, sb->faulted);
	EXPECT_EQ(sa->hr, sb->hr) << GetParam() << ": HRESULT differs (ours "
		<< a3ddiff::hrs(sa->hr) << " [" << sa->detail << "], ref "
		<< a3ddiff::hrs(sb->hr) << " [" << sb->detail << "])";
	EXPECT_EQ(sa->detail, sb->detail) << GetParam() << ": out-parameter differs (ours ["
		<< sa->detail << "], ref [" << sb->detail << "])";
}

INSTANTIATE_TEST_SUITE_P(Legacy, MapperVsReference, ::testing::Values(
	"Mapper Create", "Mapper CreateOther", "Mapper Device", "Mapper Primary",
	"Mapper Listener", "Mapper Secondary", "Mapper Secondary3D",
	"Mapper Duplicate", "Mapper IA3d2"));
