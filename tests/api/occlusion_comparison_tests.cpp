/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * occlusion_comparison_tests.cpp - Geometry comparison: IA3dGeom2 occlusion.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling. See docs/testing_coverage.md for geometry coverage.
 *
 * Runs the SDK polygon occlusion scenario in a separate process per DLL
 * through --occl-child <dll>. The shared root was initialized without
 * A3D_OCCLUSIONS; creating a second engine in that process returns
 * 0x80040054, also observed by the second-engine reverb test.
 *
 * Compare settled GetOcclusionFactor/GetAudibility readings with a quad
 * blocking the source, moved aside and absent. The quad is 2 units from
 * the listener. Setup follows tools/probes/sdk_loading/main.cpp, including
 * timeBeginPeriod(1) to avoid the zero-frame-time defect.
 *
 *---------------------------------------------------------------------------
 */

#include "comparison_fixture.hpp"

#include <mmreg.h>
#include "ia3ddal.h"

#include "com_server_isolation.hpp"
#include "com_lifetime.hpp"
#include "process_audio_mute.hpp"
#include "crt_error_reporting.hpp"
#include "com_dll_loader.hpp"

#include <mmsystem.h>

#include <cstdio>
#include <cstring>

using namespace a3ddiff;
using namespace a3dtest;

namespace {

/* One frame of the polygon sample's setup: listener at the origin, source
   five units ahead, and the quad at fWallX, two units in front of the
   listener, when fWall is set.  Matches tools/probes/sdk_loading/main.cpp's Frame(). */
void occl_frame(IA3d5 *pA3d, IA3dGeom2 *pGeom, IA3dSource2 *pSource,
		A3DVAL fWallX, int fWall)
{
	pA3d->Clear();
	pGeom->LoadIdentity();

	pGeom->PushMatrix();
	pGeom->Translate3f(0.0f, 0.0f, 0.0f);
	pGeom->BindListener();
	pGeom->PopMatrix();

	pGeom->PushMatrix();
	pGeom->Translate3f(0.0f, 0.0f, -5.0f);
	pGeom->BindSource((LPA3DSOURCE2) pSource);
	pGeom->PopMatrix();

	if (!fWall) {
		pA3d->Flush();
		return;
	}

	pGeom->PushMatrix();
	pGeom->Translate3f(fWallX, 0.0f, -2.0f);
	pGeom->Begin(A3D_QUADS);
	pGeom->Vertex3f(-2.0f, -2.0f, 0.0f);
	pGeom->Vertex3f( 2.0f, -2.0f, 0.0f);
	pGeom->Vertex3f( 2.0f,  2.0f, 0.0f);
	pGeom->Vertex3f(-2.0f,  2.0f, 0.0f);
	pGeom->End();
	pGeom->PopMatrix();

	pA3d->Flush();
}

/* Wait for stable occlusion and audibility readings. A fixed frame count
   can sample different points in each DLL's gain ramp. */
#define OCCL_SETTLE_STABLE	20	/* unchanged frames that count as settled */
#define OCCL_SETTLE_MAX		400	/* give up rather than frame for ever */

void occl_settle(IA3d5 *pA3d, IA3dGeom2 *pGeom, IA3dSource2 *pSource,
		 A3DVAL fWallX, int fWall, A3DVAL *pfOcc, A3DVAL *pfAud)
{
	A3DVAL	fOcc = -1.0f, fAud = -1.0f;
	A3DVAL	fOccLast = -2.0f, fAudLast = -2.0f;
	int	cStable = 0;

	for (int i = 0; i < OCCL_SETTLE_MAX; i++) {
		occl_frame(pA3d, pGeom, pSource, fWallX, fWall);
		Sleep(16);

		pSource->GetOcclusionFactor(&fOcc);
		pSource->GetAudibility(&fAud);

		if (fOcc == fOccLast && fAud == fAudLast) {
			if (++cStable >= OCCL_SETTLE_STABLE)
				break;
		} else {
			cStable  = 0;
			fOccLast = fOcc;
			fAudLast = fAud;
		}
	}

	*pfOcc = fOcc;
	*pfAud = fAud;
}

/* SEH requires this scope to contain no C++ objects needing unwinding.
   COM pointers are released explicitly on each path. */
bool guarded_wall(IA3d5 *pA3d, char *pszDetail, size_t cbDetail, HRESULT *phr)
{
	__try {
		IA3dGeom2 *pGeom = NULL;
		HRESULT hi = pA3d->InitEx(NULL, A3D_OCCLUSIONS,
					  A3DRENDERPREFS_DEFAULT,
					  GetDesktopWindow(), A3D_CL_NORMAL);
		HRESULT hq = pA3d->QueryInterface(IID_IA3dGeom2, (void **) &pGeom);

		if (FAILED(hi) || !pGeom) {
			*phr = FAILED(hi) ? hi : hq;
			std::snprintf(pszDetail, cbDetail,
				      "init=0x%08lX qi=0x%08lX",
				      (unsigned long) hi, (unsigned long) hq);
			if (pGeom)
				pGeom->Release();
			return (true);
		}

		HRESULT he = pGeom->Enable(A3D_OCCLUSIONS);
		pGeom->SetOcclusionUpdateInterval(1);
		pGeom->SetOcclusionMode(A3D_OCCLUSIONS);

		LPA3DSOURCE2	pSource = NULL;
		HRESULT		hs = pA3d->NewSource(A3DSOURCE_INITIAL_RENDERMODE_A3D,
						     &pSource);

		if (FAILED(hs) || !pSource) {
			*phr = hs;
			std::snprintf(pszDetail, cbDetail,
				      "enable=0x%08lX newsrc=0x%08lX",
				      (unsigned long) he, (unsigned long) hs);
			pGeom->Release();
			return (true);
		}

		HRESULT hl = pSource->LoadFile((char *) A3D_AB_WAV,
					       A3DSOURCE_FORMAT_WAVE);
		HRESULT hp = pSource->Play(A3D_LOOPED);

		A3DVAL fOccBlocked, fOccClear, fOccNone;
		A3DVAL fAudBlocked, fAudClear, fAudNone;

		occl_settle(pA3d, pGeom, pSource, 0.0f,  1, &fOccBlocked, &fAudBlocked);
		occl_settle(pA3d, pGeom, pSource, 40.0f, 1, &fOccClear,   &fAudClear);
		occl_settle(pA3d, pGeom, pSource, 0.0f,  0, &fOccNone,    &fAudNone);

		pSource->Stop();

		*phr = hp;
		std::snprintf(pszDetail, cbDetail,
			      "init=0x%08lX enable=0x%08lX load=0x%08lX "
			      "play=0x%08lX blocked=%.3f/%.3f aside=%.3f/%.3f "
			      "none=%.3f/%.3f",
			      (unsigned long) hi, (unsigned long) he,
			      (unsigned long) hl, (unsigned long) hp,
			      (double) fOccBlocked, (double) fAudBlocked,
			      (double) fOccClear, (double) fAudClear,
			      (double) fOccNone, (double) fAudNone);

		pSource->Release();
		pGeom->Release();
		return (true);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return (false);
	}
}

static const A3DVAL walk_stops[] = { -5.0f, -3.0f, -1.5f, 0.0f, 1.5f, 3.0f, 5.0f, -5.0f };

HRESULT walk_record(IA3dGeom2 *pGeom, IA3dList *pList, IA3dMaterial *pMaterial)
{
	HRESULT hr = pList->Begin();
	if (FAILED(hr))
		return (hr);

	pGeom->BindMaterial(pMaterial);

	pGeom->PushMatrix();
	pGeom->Translate3f(0.0f, 0.0f, -3.0f);

	pGeom->Begin(A3D_QUADS);
	pGeom->Tag(1);
	pGeom->Vertex3f(-8.0f, -2.0f, 0.0f);
	pGeom->Vertex3f( 8.0f, -2.0f, 0.0f);
	pGeom->Vertex3f( 8.0f,  2.0f, 0.0f);
	pGeom->Vertex3f(-8.0f,  2.0f, 0.0f);
	pGeom->End();

	pGeom->Begin(A3D_SUB_QUADS);
	pGeom->Tag(2);
	pGeom->SetOpeningFactorf(1.0f);
	pGeom->Vertex3f(-1.0f, -2.0f, 0.0f);
	pGeom->Vertex3f( 1.0f, -2.0f, 0.0f);
	pGeom->Vertex3f( 1.0f,  2.0f, 0.0f);
	pGeom->Vertex3f(-1.0f,  2.0f, 0.0f);
	pGeom->End();

	pGeom->PopMatrix();

	return (pList->End());
}

void walk_frame(IA3d5 *pA3d, IA3dGeom2 *pGeom, IA3dSource2 *pSource,
		IA3dList *pList, A3DVAL fX)
{
	pA3d->Clear();
	pGeom->LoadIdentity();

	pGeom->PushMatrix();
	pGeom->Translate3f(fX, 0.0f, 0.0f);
	pGeom->BindListener();
	pGeom->PopMatrix();

	pGeom->PushMatrix();
	pGeom->Translate3f(0.0f, 0.0f, -6.0f);
	pGeom->BindSource((LPA3DSOURCE2) pSource);
	pGeom->PopMatrix();

	pList->Call();

	pA3d->Flush();
}

void walk_settle(IA3d5 *pA3d, IA3dGeom2 *pGeom, IA3dSource2 *pSource,
		 IA3dList *pList, A3DVAL fX, A3DVAL *pfOcc, A3DVAL *pfAud)
{
	A3DVAL	fOcc = -1.0f, fAud = -1.0f;
	A3DVAL	fOccLast = -2.0f, fAudLast = -2.0f;
	int	cStable = 0;

	for (int i = 0; i < OCCL_SETTLE_MAX; i++) {
		walk_frame(pA3d, pGeom, pSource, pList, fX);
		Sleep(16);

		pSource->GetOcclusionFactor(&fOcc);
		pSource->GetAudibility(&fAud);

		if (fOcc == fOccLast && fAud == fAudLast) {
			if (++cStable >= OCCL_SETTLE_STABLE)
				break;
		} else {
			cStable  = 0;
			fOccLast = fOcc;
			fAudLast = fAud;
		}
	}

	*pfOcc = fOcc;
	*pfAud = fAud;
}

/* Private Compat selector that sets the interface version directly; Flush
   uses TraceLegacy for versions 3 and below. */
#define OCCL_COMPAT_SET_INTERFACE_VERSION 2002

/* SEH requires this scope to contain no C++ objects needing unwinding. */
bool guarded_walk(IA3d5 *pA3d, char *pszDetail, size_t cbDetail, HRESULT *phr)
{
	__try {
		IA3dGeom2    *pGeom     = NULL;
		IA3dList     *pList     = NULL;
		IA3dMaterial *pMaterial = NULL;
		IA3dSource2  *pSource   = NULL;
		size_t        used      = 0;

		*phr = pA3d->QueryInterface(IID_IA3dGeom2, (void **) &pGeom);
		if (FAILED(*phr) || !pGeom) {
			std::snprintf(pszDetail, cbDetail, "qi");
			return (true);
		}

		HRESULT hn = pGeom->NewList(&pList);
		HRESULT hm = pGeom->NewMaterial(&pMaterial);
		HRESULT hs = pA3d->NewSource(A3DSOURCE_INITIAL_RENDERMODE_A3D, &pSource);

		if (FAILED(hn) || FAILED(hm) || FAILED(hs) || !pList || !pMaterial || !pSource) {
			*phr = E_FAIL;
			std::snprintf(pszDetail, cbDetail, "list=0x%08lX mat=0x%08lX src=0x%08lX",
				      (unsigned long) hn, (unsigned long) hm, (unsigned long) hs);
		} else {
			pMaterial->SetTransmittance(0.1f, 0.3f);

			HRESULT hr = walk_record(pGeom, pList, pMaterial);
			HRESULT hl = pSource->LoadFile((char *) A3D_AB_WAV, A3DSOURCE_FORMAT_WAVE);
			HRESULT hp = pSource->Play(A3D_LOOPED);

			*phr = hp;
			used = std::snprintf(pszDetail, cbDetail, "rec=0x%08lX load=0x%08lX play=0x%08lX",
					     (unsigned long) hr, (unsigned long) hl, (unsigned long) hp);

			for (size_t i = 0; i < sizeof walk_stops / sizeof walk_stops[0]; i++) {
				A3DVAL fOcc, fAud;

				walk_settle(pA3d, pGeom, pSource, pList, walk_stops[i], &fOcc, &fAud);

				if (used < cbDetail)
					used += std::snprintf(pszDetail + used, cbDetail - used,
							      " %.1f:%.3f/%.3f", (double) walk_stops[i],
							      (double) fOcc, (double) fAud);
			}

			pSource->Stop();
		}

		if (pSource)
			pSource->Release();
		if (pMaterial)
			pMaterial->Release();
		if (pList)
			pList->Release();
		pGeom->Release();
		return (true);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return (false);
	}
}

}	/* namespace */

namespace a3ddiff {

/* Entry point for --occl-child <dll>. Runs the SDK polygon occlusion
   scenario and the listener walk, and writes one STEP record for each. */
int occlusion_diff_child(const char *pszDll)
{
	crt_quiet();
	com_init ci;
	(void) ci;

	process_audio_mute mute;
	(void) mute;

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

	void *root = NULL;
	HRESULT hr = dll.create(CLSID_A3dApi, IID_IA3d5, &root);
	if (FAILED(hr) || !root) {
		std::printf("STEP\tOccl WallSettle\t0x%08lX\t0\tno-root\n",
			    (unsigned long) hr);
		std::printf("END\n");
		std::fflush(stdout);
		return (0);
	}
	IA3d5 *pA3d = reinterpret_cast<IA3d5 *>(root);

	/* CA3dRoot::Flush computes frame time as
	   (timeGetTime() - last) * 0.001f (dbg:0x1000AC60, rtl:0x10002BA0).
	   At the default ~15.6ms resolution two flushes in one tick give a
	   zero frame time, which stalls A3dTraceApply's occlusion ramp
	   (dbg:0x10022A00, rtl:0x1000F180); tools/probes/sdk_loading/main.cpp measured one
	   in ~12 runs freezing at the occluded gain without this. */
	timeBeginPeriod(1);

	char	detail[128];
	HRESULT	hStep = E_FAIL;
	bool	ok = guarded_wall(pA3d, detail, sizeof detail, &hStep);

	std::printf("STEP\tOccl WallSettle\t0x%08lX\t%d\t%s\n",
		    (unsigned long) hStep, ok ? 0 : 1, ok ? detail : "");

	char	walk[256];
	HRESULT	hWalk = E_FAIL;
	bool	walked = guarded_walk(pA3d, walk, sizeof walk, &hWalk);

	std::printf("STEP\tOccl ListenerWalk\t0x%08lX\t%d\t%s\n",
		    (unsigned long) hWalk, walked ? 0 : 1, walked ? walk : "");

	HRESULT	hVer = pA3d->Compat(OCCL_COMPAT_SET_INTERFACE_VERSION, 3);
	char	legacy[256];
	HRESULT	hLegacy = E_FAIL;
	bool	legacied = guarded_walk(pA3d, legacy, sizeof legacy, &hLegacy);
	pA3d->Compat(OCCL_COMPAT_SET_INTERFACE_VERSION, 5);

	std::printf("STEP\tOccl LegacyWalk\t0x%08lX\t%d\tver=%08lX %s\n",
		    (unsigned long) hLegacy, legacied ? 0 : 1, (unsigned long) hVer,
		    legacied ? legacy : "");

	timeEndPeriod(1);
	std::printf("END\n");
	std::fflush(stdout);

	pA3d->Release();
	return (0);
}

}	/* namespace a3ddiff */

namespace {

/* Compare the STEP records from 2 --occl-child runs, one per DLL. */
class OcclusionVsReference : public ::testing::Test {
protected:
	static a3ddiff::ScenarioRunResult a;
	static a3ddiff::ScenarioRunResult b;

	static void SetUpTestSuite()
	{
		std::string self = a3ddiff::self_path();
		a = a3ddiff::run_child(self, A3D_AB_OUR_DLL, "--occl-child");
		b = a3ddiff::run_child(self, A3D_AB_REF_DLL, "--occl-child");
	}
};

a3ddiff::ScenarioRunResult OcclusionVsReference::a;
a3ddiff::ScenarioRunResult OcclusionVsReference::b;

/* IsFeatureAvailable(A3D_OCCLUSIONS) requires no additional InitEx flags,
   so this query uses the shared root. */
void s_occl_feature(ScenarioContext *c, StepResult *o)
{
	o->hr = c->pRoot->IsFeatureAvailable(A3D_OCCLUSIONS);
	std::snprintf(o->detail, sizeof o->detail, "-");
}

}	/* namespace */

namespace a3ddiff {

void add_occlusion_steps(Script &s)
{
	s.push_back({ "Occl IsFeatureAvailable", s_occl_feature });
}

}	/* namespace a3ddiff */

TEST_F(InterfaceVsReference, OcclusionFeature)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Occl IsFeatureAvailable");
}

TEST_F(OcclusionVsReference, WallSettle)
{
	const a3ddiff::RecordedStepResult *sa = a3ddiff::find_step(a, "Occl WallSettle");
	const a3ddiff::RecordedStepResult *sb = a3ddiff::find_step(b, "Occl WallSettle");
	ASSERT_TRUE(sa && sb) << "step missing (a child stopped early: ours "
		<< (a.ended ? "ok" : "crashed") << ", ref "
		<< (b.ended ? "ok" : "crashed") << ")";
	EXPECT_EQ(sa->faulted, sb->faulted) << "fault state differs (ours "
		<< (sa->faulted ? "FAULT" : "ok") << ", ref "
		<< (sb->faulted ? "FAULT" : "ok") << ")";
	EXPECT_EQ(sa->hr, sb->hr) << "HRESULT differs (ours "
		<< a3ddiff::hrs(sa->hr) << " [" << sa->detail << "], ref "
		<< a3ddiff::hrs(sb->hr) << " [" << sb->detail << "])";
	EXPECT_EQ(sa->detail, sb->detail) << "out-parameter differs (ours ["
		<< sa->detail << "], ref [" << sb->detail << "])";
}

TEST_F(OcclusionVsReference, ListenerWalk)
{
	const a3ddiff::RecordedStepResult *sa = a3ddiff::find_step(a, "Occl ListenerWalk");
	const a3ddiff::RecordedStepResult *sb = a3ddiff::find_step(b, "Occl ListenerWalk");
	ASSERT_TRUE(sa && sb) << "step missing (a child stopped early: ours "
		<< (a.ended ? "ok" : "crashed") << ", ref "
		<< (b.ended ? "ok" : "crashed") << ")";
	EXPECT_EQ(sa->faulted, sb->faulted);
	EXPECT_EQ(sa->hr, sb->hr) << "HRESULT differs (ours "
		<< a3ddiff::hrs(sa->hr) << ", ref " << a3ddiff::hrs(sb->hr) << ")";
	EXPECT_EQ(sa->detail, sb->detail) << "walk differs (ours ["
		<< sa->detail << "], ref [" << sb->detail << "])";
}

TEST_F(OcclusionVsReference, LegacyWalk)
{
	const a3ddiff::RecordedStepResult *sa = a3ddiff::find_step(a, "Occl LegacyWalk");
	const a3ddiff::RecordedStepResult *sb = a3ddiff::find_step(b, "Occl LegacyWalk");
	ASSERT_TRUE(sa && sb) << "step missing (a child stopped early: ours "
		<< (a.ended ? "ok" : "crashed") << ", ref "
		<< (b.ended ? "ok" : "crashed") << ")";
	EXPECT_EQ(sa->faulted, sb->faulted);
	EXPECT_EQ(sa->hr, sb->hr);
	EXPECT_EQ(sa->detail, sb->detail) << "legacy walk differs (ours ["
		<< sa->detail << "], ref [" << sb->detail << "])";
}
