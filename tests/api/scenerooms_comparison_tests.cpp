/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * scenerooms_comparison_tests.cpp - Geometry comparison: the SceneRooms pattern.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling. See docs/testing_coverage.md for geometry coverage.
 *
 * Runs the 8 samples/SceneRooms/SceneRooms.cpp rooms through
 * --scene-child <dll>, in a separate process per DLL. This permits the
 * sample's InitEx flags and CreateScene call order, which differ from
 * the shared scenario.
 *
 * Geometry, source parameters and reverb presets come from g_Room[] and
 * CreateScene(). Each room is sampled at 1 or 2 settled configurations,
 * replacing the sample's continuous animation.
 *
 * CreateScene configures geometry/occlusion, enables A3D_1ST_REFLECTIONS,
 * creates a material and reverb, then creates each source. g_bReverb
 * defaults TRUE but g_bRequestReverb defaults FALSE, so InitEx omits
 * A3D_REVERB. Without an A3D driver, Enable and NewReverb return
 * A3DERROR_FEATURE_NOT_SUPPORTED / A3DERROR_FEATURE_NOT_REQUESTED
 * (0x3E/0x3D in FACILITY_ITF). Compare these errors and the subsequent
 * direct-path audibility readings in the reflection and reverb rooms.
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

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace a3ddiff;
using namespace a3dtest;

namespace {

/* -- the eight rooms, copied from g_Room[] in SceneRooms.cpp ---------- */

enum { MOTION_STATIC, MOTION_ORBIT, MOTION_SWEEP };
enum { GEOM_OPEN, GEOM_WALL, GEOM_BOX };

#define NO_PRESET	(-1)

struct scene_room {
	const char	*pszName;
	const char	*pszFile;
	int		 nMotion;
	A3DVAL		 fBase[3];
	A3DVAL		 fAxis[3];
	A3DVAL		 fParam;
	A3DVAL		 fPeriod;
	A3DVAL		 fGain;
	A3DVAL		 fPitch;
	A3DVAL		 fMinDist;
	A3DVAL		 fMaxDist;
	A3DVAL		 fDopplerScale;
	DWORD		 dwRenderMode;
	DWORD		 dwTransformMode;
	BOOL		 bCone;
	int		 nGeom;
	A3DVAL		 fGeomSize[3];
	int		 nReverbPreset;
	A3DVAL		 fStart[3];
	A3DVAL		 fStartYaw;
};

/* Verbatim from SceneRooms.cpp's g_Room[] (fColour and pszListenFor dropped;
   they drive the sample's own window, not anything read back here). */
static const scene_room g_Room[] = {
	{ "distance", "heli.wav", MOTION_SWEEP,
	  { 0.0f, 0.0f, -4.0f }, { 0.0f, 0.0f, -1.0f }, 30.0f, 16.0f,
	  1.0f, 1.0f, 1.0f, 70.0f, 0.0f,
	  A3DSOURCE_RENDERMODE_DEFAULT, A3DSOURCE_TRANSFORMMODE_NORMAL, FALSE,
	  GEOM_OPEN, { 0.0f, 0.0f, 0.0f }, NO_PRESET,
	  { 0.0f, 0.0f, 4.0f }, 0.0f },
	{ "panning", "linn1.wav", MOTION_ORBIT,
	  { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, 5.0f, 9.0f,
	  1.0f, 1.0f, 1.0f, 40.0f, 0.0f,
	  A3DSOURCE_RENDERMODE_DEFAULT, A3DSOURCE_TRANSFORMMODE_NORMAL, FALSE,
	  GEOM_OPEN, { 0.0f, 0.0f, 0.0f }, NO_PRESET,
	  { 0.0f, 0.0f, 0.0f }, 0.0f },
	{ "occlusion", "linn1.wav", MOTION_STATIC,
	  { 0.0f, 0.0f, -9.0f }, { 1.0f, 0.0f, 0.0f }, 0.0f, 0.0f,
	  1.0f, 1.0f, 1.0f, 40.0f, 0.0f,
	  A3DSOURCE_RENDERMODE_DEFAULT, A3DSOURCE_TRANSFORMMODE_NORMAL, FALSE,
	  GEOM_WALL, { 4.0f, 4.0f, -4.0f }, NO_PRESET,
	  { 0.0f, 0.0f, 3.0f }, 0.0f },
	{ "doppler", "linn48.wav", MOTION_SWEEP,
	  { 0.0f, 0.0f, -3.0f }, { 1.0f, 0.0f, 0.0f }, 40.0f, 8.0f,
	  1.0f, 1.0f, 2.0f, 90.0f, 1.0f,
	  A3DSOURCE_RENDERMODE_DEFAULT, A3DSOURCE_TRANSFORMMODE_NORMAL, FALSE,
	  GEOM_OPEN, { 0.0f, 0.0f, 0.0f }, NO_PRESET,
	  { 0.0f, 0.0f, 2.0f }, 0.0f },
	{ "cone", "linn48.wav", MOTION_STATIC,
	  { 0.0f, 0.0f, -5.0f }, { 0.0f, 1.0f, 0.0f }, 0.0f, 10.0f,
	  1.0f, 1.0f, 1.0f, 40.0f, 0.0f,
	  A3DSOURCE_RENDERMODE_DEFAULT, A3DSOURCE_TRANSFORMMODE_NORMAL, TRUE,
	  GEOM_OPEN, { 0.0f, 0.0f, 0.0f }, NO_PRESET,
	  { 0.0f, 0.0f, 0.0f }, 0.0f },
	{ "headrelative", "heli.wav", MOTION_STATIC,
	  { 1.5f, 0.0f, -1.0f }, { 1.0f, 0.0f, 0.0f }, 0.0f, 0.0f,
	  0.7f, 1.0f, 1.0f, 20.0f, 0.0f,
	  A3DSOURCE_RENDERMODE_MONO, A3DSOURCE_TRANSFORMMODE_HEADRELATIVE, FALSE,
	  GEOM_OPEN, { 0.0f, 0.0f, 0.0f }, NO_PRESET,
	  { 0.0f, 0.0f, 0.0f }, 0.0f },
	{ "reflections", "linn1.wav", MOTION_STATIC,
	  { 0.0f, 0.0f, -3.0f }, { 1.0f, 0.0f, 0.0f }, 0.0f, 0.0f,
	  1.0f, 1.0f, 1.0f, 40.0f, 0.0f,
	  A3DSOURCE_RENDERMODE_DEFAULT, A3DSOURCE_TRANSFORMMODE_NORMAL, FALSE,
	  GEOM_BOX, { 6.0f, 4.0f, 6.0f }, NO_PRESET,
	  { 0.0f, 0.0f, 3.0f }, 0.0f },
	{ "reverb", "clap_test.wav", MOTION_STATIC,
	  { 0.0f, 0.0f, -4.0f }, { 1.0f, 0.0f, 0.0f }, 0.0f, 0.0f,
	  1.0f, 1.0f, 1.0f, 40.0f, 0.0f,
	  A3DSOURCE_RENDERMODE_DEFAULT, A3DSOURCE_TRANSFORMMODE_NORMAL, FALSE,
	  GEOM_OPEN, { 0.0f, 0.0f, 0.0f }, A3DREVERB_PRESET_HANGAR,
	  { 0.0f, 0.0f, 2.0f }, 0.0f },
};

#define ROOM_DISTANCE		0
#define ROOM_PANNING		1
#define ROOM_OCCLUSION		2
#define ROOM_DOPPLER		3
#define ROOM_CONE		4
#define ROOM_HEADRELATIVE	5
#define ROOM_REFLECTIONS	6
#define ROOM_REVERB		7
#define NUM_ROOMS		(sizeof(g_Room) / sizeof(g_Room[0]))

/* SceneRooms.cpp's Triangle(): a triangle wave in [-1, 1], for MOTION_SWEEP. */
static A3DVAL Triangle(A3DVAL fPhase)
{
	A3DVAL f = (A3DVAL) std::fmod((double) fPhase, 1.0);
	if (f < 0.0f)
		f += 1.0f;
	return (f < 0.5f) ? (4.0f * f - 1.0f) : (3.0f - 4.0f * f);
}

/* SceneRooms.cpp's EvalSource(), evaluated at a chosen room-clock time t
   instead of a running clock. */
static void EvalSource(const scene_room *p, A3DVAL t, A3DVAL fPos[3], A3DVAL fVel[3])
{
	fPos[0] = p->fBase[0];
	fPos[1] = p->fBase[1];
	fPos[2] = p->fBase[2];
	fVel[0] = fVel[1] = fVel[2] = 0.0f;

	switch (p->nMotion) {
	case MOTION_ORBIT: {
		A3DVAL w = 6.283185307f / p->fPeriod;
		fPos[0] += p->fParam * (A3DVAL) std::sin(w * t);
		fPos[2] += p->fParam * (A3DVAL) std::cos(w * t);
		fVel[0]  = p->fParam * w * (A3DVAL) std::cos(w * t);
		fVel[2]  = -p->fParam * w * (A3DVAL) std::sin(w * t);
		break;
	}
	case MOTION_SWEEP: {
		A3DVAL d = p->fParam * Triangle(t / p->fPeriod);
		fPos[0] += p->fAxis[0] * d;
		fPos[1] += p->fAxis[1] * d;
		fPos[2] += p->fAxis[2] * d;

		A3DVAL phase = (A3DVAL) std::fmod((double) (t / p->fPeriod), 1.0);
		if (phase < 0.0f)
			phase += 1.0f;

		A3DVAL speed = 4.0f * p->fParam / p->fPeriod;
		if (phase >= 0.5f)
			speed = -speed;

		fVel[0] = p->fAxis[0] * speed;
		fVel[1] = p->fAxis[1] * speed;
		fVel[2] = p->fAxis[2] * speed;
		break;
	}
	case MOTION_STATIC:
	default:
		break;
	}
}

/* -- one root, driven the way CreateScene()/RunAudioFrame() drive it -- */

const char *MediaPath(const char *pszFile, char *buf, size_t cb)
{
	std::snprintf(buf, cb, "%s\\%s", A3D_SCENE_DATA_DIR, pszFile);
	return buf;
}

/* One frame: listener at fListener facing fYaw, the room's source at fPos
   (rotated by fSourceYaw first, for the cone room), whatever geometry the
   room has if bGeometry.  Matches RunAudioFrame()/EmitRoomGeometry(). */
void scene_frame(IA3d5 *pA3d, IA3dGeom2 *pGeom, IA3dListener *pListener,
		 IA3dSource2 *pSource, IA3dMaterial *pMaterial,
		 const A3DVAL fListener[3], A3DVAL fYaw,
		 const A3DVAL fPos[3], const A3DVAL fVel[3], A3DVAL fSourceYaw,
		 const scene_room *p, BOOL bGeometry)
{
	pA3d->Clear();
	pGeom->LoadIdentity();

	pGeom->PushMatrix();
	pGeom->Translate3f(fListener[0], fListener[1], fListener[2]);
	pGeom->Rotate3f(fYaw, 0.0f, 1.0f, 0.0f);
	pGeom->BindListener();
	pGeom->PopMatrix();

	pListener->SetVelocity3f(0.0f, 0.0f, 0.0f);

	pSource->SetVelocity3fv(const_cast<A3DVAL *>(fVel));

	pGeom->PushMatrix();
	pGeom->Translate3f(fPos[0], fPos[1], fPos[2]);
	if (p->bCone)
		pGeom->Rotate3f(fSourceYaw, 0.0f, 1.0f, 0.0f);
	pGeom->BindSource((LPA3DSOURCE2) pSource);
	pGeom->PopMatrix();

	if (bGeometry && p->nGeom != GEOM_OPEN) {
		pGeom->BindMaterial(pMaterial);
		pGeom->Begin(A3D_QUADS);

		if (p->nGeom == GEOM_WALL) {
			A3DVAL hw = p->fGeomSize[0];
			A3DVAL h  = p->fGeomSize[1];
			A3DVAL z  = p->fGeomSize[2];
			const A3DVAL FLOOR_Y = -2.0f;

			pGeom->Tag(1);
			pGeom->Vertex3f(-hw, FLOOR_Y, z);
			pGeom->Vertex3f( hw, FLOOR_Y, z);
			pGeom->Vertex3f( hw, h,       z);
			pGeom->Vertex3f(-hw, h,       z);
		} else if (p->nGeom == GEOM_BOX) {
			A3DVAL x = p->fGeomSize[0];
			A3DVAL y = p->fGeomSize[1];
			A3DVAL z = p->fGeomSize[2];
			const A3DVAL FLOOR_Y = -2.0f;

			pGeom->Tag(1);
			pGeom->Vertex3f(-x, FLOOR_Y, -z);
			pGeom->Vertex3f( x, FLOOR_Y, -z);
			pGeom->Vertex3f( x, y,       -z);
			pGeom->Vertex3f(-x, y,       -z);

			pGeom->Tag(2);
			pGeom->Vertex3f( x, FLOOR_Y,  z);
			pGeom->Vertex3f(-x, FLOOR_Y,  z);
			pGeom->Vertex3f(-x, y,        z);
			pGeom->Vertex3f( x, y,        z);

			pGeom->Tag(3);
			pGeom->Vertex3f(-x, FLOOR_Y,  z);
			pGeom->Vertex3f(-x, FLOOR_Y, -z);
			pGeom->Vertex3f(-x, y,       -z);
			pGeom->Vertex3f(-x, y,        z);

			pGeom->Tag(4);
			pGeom->Vertex3f( x, FLOOR_Y, -z);
			pGeom->Vertex3f( x, FLOOR_Y,  z);
			pGeom->Vertex3f( x, y,        z);
			pGeom->Vertex3f( x, y,       -z);

			pGeom->Tag(5);
			pGeom->Vertex3f(-x, FLOOR_Y,  z);
			pGeom->Vertex3f( x, FLOOR_Y,  z);
			pGeom->Vertex3f( x, FLOOR_Y, -z);
			pGeom->Vertex3f(-x, FLOOR_Y, -z);

			pGeom->Tag(6);
			pGeom->Vertex3f(-x, y, -z);
			pGeom->Vertex3f( x, y, -z);
			pGeom->Vertex3f( x, y,  z);
			pGeom->Vertex3f(-x, y,  z);
		}

		pGeom->End();
	}

	pA3d->Flush();
}

/* Settle exactly as occl_settle does: frame until GetAudibility and
   GetOcclusionFactor stop moving, or give up. */
#define SCENE_SETTLE_STABLE	20
#define SCENE_SETTLE_MAX	400

void scene_settle(IA3d5 *pA3d, IA3dGeom2 *pGeom, IA3dListener *pListener,
		  IA3dSource2 *pSource, IA3dMaterial *pMaterial,
		  const A3DVAL fListener[3], A3DVAL fYaw,
		  const A3DVAL fPos[3], const A3DVAL fVel[3], A3DVAL fSourceYaw,
		  const scene_room *p, BOOL bGeometry,
		  A3DVAL *pfAud, A3DVAL *pfOcc)
{
	A3DVAL	fAud = -1.0f, fOcc = -1.0f;
	A3DVAL	fAudLast = -2.0f, fOccLast = -2.0f;
	int	cStable = 0;

	for (int i = 0; i < SCENE_SETTLE_MAX; i++) {
		scene_frame(pA3d, pGeom, pListener, pSource, pMaterial,
			   fListener, fYaw, fPos, fVel, fSourceYaw, p, bGeometry);
		Sleep(16);

		pSource->GetAudibility(&fAud);
		pSource->GetOcclusionFactor(&fOcc);

		if (fAud == fAudLast && fOcc == fOccLast) {
			if (++cStable >= SCENE_SETTLE_STABLE)
				break;
		} else {
			cStable  = 0;
			fAudLast = fAud;
			fOccLast = fOcc;
		}
	}

	*pfAud = fAud;
	*pfOcc = fOcc;
}

/* SEH requires this scope to contain no C++ objects needing unwinding.
   COM pointers are released explicitly on each path. */
bool guarded_scene(IA3d5 *pA3d, char (*apDetail)[192], HRESULT *aphr, size_t n)
{
	__try {
		IA3dGeom2    *pGeom     = NULL;
		IA3dListener *pListener = NULL;

		/* SceneRooms.cpp's InitEx call: A3D_1ST_REFLECTIONS |
		   A3D_OCCLUSIONS | A3D_DISABLE_SPLASHSCREEN |
		   A3D_DISABLE_FOCUS_MUTE, and no A3D_REVERB because
		   g_bRequestReverb defaults FALSE. */
		HRESULT hi = pA3d->InitEx(NULL,
					  A3D_1ST_REFLECTIONS | A3D_OCCLUSIONS |
					  A3D_DISABLE_SPLASHSCREEN |
					  A3D_DISABLE_FOCUS_MUTE,
					  A3DRENDERPREFS_DEFAULT,
					  GetDesktopWindow(), A3D_CL_NORMAL);
		HRESULT hg = pA3d->QueryInterface(IID_IA3dGeom2, (void **) &pGeom);
		HRESULT hl = pA3d->QueryInterface(IID_IA3dListener, (void **) &pListener);

		if (FAILED(hi) || !pGeom || !pListener) {
			for (size_t i = 0; i < n; i++) {
				aphr[i] = FAILED(hi) ? hi : (FAILED(hg) ? hg : hl);
				std::snprintf(apDetail[i], sizeof apDetail[i],
					      "init=0x%08lX qgeom=0x%08lX qlist=0x%08lX",
					      (unsigned long) hi, (unsigned long) hg,
					      (unsigned long) hl);
			}
			if (pListener)
				pListener->Release();
			if (pGeom)
				pGeom->Release();
			return (true);
		}

		/* CreateScene()'s root/geometry state, values as written there. */
		pA3d->SetCoordinateSystem(A3D_RIGHT_HANDED_CS);
		pA3d->SetUnitsPerMeter(1.0f);
		pA3d->SetDopplerScale(1.0f);
		pA3d->SetDistanceModelScale(1.0f);
		pA3d->SetOutputGain(1.0f);
		pA3d->SetResourceManagerMode(A3D_RESOURCE_MODE_DYNAMIC);
		pA3d->SetNumFallbackSources(4);
		pA3d->SetMaxReflectionDelayTime(0.3f);

		pGeom->Enable(A3D_OCCLUSIONS);
		/* Enable checks driver-reported availability. Without an A3D driver,
		   A3D_1ST_REFLECTIONS fails even though InitEx requested it. */
		HRESULT hEnableRefl = pGeom->Enable(A3D_1ST_REFLECTIONS);
		pGeom->SetOcclusionMode(A3D_QUICK);
		pGeom->SetReflectionMode(A3D_QUICK);
		pGeom->SetReflectionGainScale(1.0f);
		pGeom->SetReflectionDelayScale(1.0f);
		pGeom->SetPolygonBloatFactor(0.1f);
		pGeom->SetOcclusionUpdateInterval(1);
		pGeom->SetReflectionUpdateInterval(1);

		LPA3DMATERIAL pMaterial = NULL;
		pGeom->NewMaterial(&pMaterial);
		if (pMaterial) {
			pMaterial->SetTransmittance(0.05f, 0.2f);
			pMaterial->SetReflectance(0.9f, 0.8f);
		}

		/* g_bReverb defaults TRUE, so CreateScene() always reaches
		   NewReverb even though the InitEx A3D_REVERB bit (gated by
		   g_bRequestReverb, defaulting FALSE) was not requested. */
		LPA3DREVERB pReverb = NULL;
		HRESULT hNewReverb = pA3d->NewReverb(&pReverb);
		HRESULT hBindReverb = S_FALSE;
		if (SUCCEEDED(hNewReverb) && pReverb) {
			pReverb->SetReverbPreset(A3DREVERB_PRESET_HANGAR);
			hBindReverb = pA3d->BindReverb(pReverb);
		}

		std::snprintf(apDetail[0], sizeof apDetail[0],
			      "init=0x%08lX enable1stRefl=0x%08lX "
			      "newReverb=0x%08lX obj=%s bindReverb=0x%08lX",
			      (unsigned long) hi, (unsigned long) hEnableRefl,
			      (unsigned long) hNewReverb, pReverb ? "obj" : "null",
			      (unsigned long) hBindReverb);
		aphr[0] = hEnableRefl;

		/* Create one source per room. Play only one at a time to avoid
		   exceeding SetNumFallbackSources(4). */
		LPA3DSOURCE2 apSource[NUM_ROOMS];
		for (size_t i = 0; i < NUM_ROOMS; i++) {
			const scene_room *p = &g_Room[i];
			apSource[i] = NULL;

			HRESULT hs = pA3d->NewSource(A3DSOURCE_INITIAL_RENDERMODE_A3D,
						     &apSource[i]);
			if (FAILED(hs) || !apSource[i])
				continue;

			char path[MAX_PATH];
			apSource[i]->LoadFile((char *) MediaPath(p->pszFile, path,
								  sizeof path),
					      A3DSOURCE_FORMAT_WAVE);
			apSource[i]->SetGain(p->fGain);
			apSource[i]->SetPitch(p->fPitch);
			apSource[i]->SetMinMaxDistance(p->fMinDist, p->fMaxDist,
						       A3D_AUDIBLE);
			apSource[i]->SetRenderMode(p->dwRenderMode);
			apSource[i]->SetTransformMode(p->dwTransformMode);
			apSource[i]->SetPriority(0.5f);
			apSource[i]->SetDopplerScale(p->fDopplerScale);
			apSource[i]->SetEq(1.0f);
			apSource[i]->SetReverbMix(1.0f,
						 p->nReverbPreset == NO_PRESET
						 ? 0.0f : 0.6f);
			if (p->bCone)
				apSource[i]->SetCone(50.0f, 140.0f, 0.05f);
		}

		/* -- one settled reading per room, one source Play()ing at a
		   time (LeaveRoom()/EnterRoom()'s lifecycle) ------------- */

		A3DVAL fZero[3] = { 0.0f, 0.0f, 0.0f };

		/* Room 0: distance.  Two points on the sweep (t=0 and
		   t=period/2, Triangle()'s two extremes), same axis and
		   listener SceneRooms starts this room at. */
		{
			const scene_room *p = &g_Room[ROOM_DISTANCE];
			IA3dSource2 *pS = apSource[ROOM_DISTANCE];
			A3DVAL fPos0[3], fVel0[3], fPos1[3], fVel1[3];
			EvalSource(p, 0.0f, fPos0, fVel0);
			EvalSource(p, p->fPeriod * 0.5f, fPos1, fVel1);

			pS->Rewind();
			pS->Play(A3D_LOOPED);

			A3DVAL fAud0, fOcc0, fAud1, fOcc1, fGain0 = -1.0f, fGain1 = -1.0f;
			scene_settle(pA3d, pGeom, pListener, pS, pMaterial,
				    p->fStart, 0.0f, fPos0, fVel0, 0.0f, p, FALSE,
				    &fAud0, &fOcc0);
			pS->GetGain(&fGain0);
			scene_settle(pA3d, pGeom, pListener, pS, pMaterial,
				    p->fStart, 0.0f, fPos1, fVel1, 0.0f, p, FALSE,
				    &fAud1, &fOcc1);
			pS->GetGain(&fGain1);
			pS->Stop();

			std::snprintf(apDetail[1], sizeof apDetail[1],
				      "aud=%.4f/%.4f gain=%.4f/%.4f",
				      (double) fAud0, (double) fAud1,
				      (double) fGain0, (double) fGain1);
			aphr[1] = S_OK;
		}

		/* Room 1: panning.  Two orbit angles (t=0, front; t=period/4,
		   to the side), listener at the origin the room starts at. */
		{
			const scene_room *p = &g_Room[ROOM_PANNING];
			IA3dSource2 *pS = apSource[ROOM_PANNING];
			A3DVAL fPos0[3], fVel0[3], fPos1[3], fVel1[3];
			EvalSource(p, 0.0f, fPos0, fVel0);
			EvalSource(p, p->fPeriod * 0.25f, fPos1, fVel1);

			pS->Rewind();
			pS->Play(A3D_LOOPED);

			A3DVAL fAud0, fOcc0, fAud1, fOcc1;
			scene_settle(pA3d, pGeom, pListener, pS, pMaterial,
				    p->fStart, 0.0f, fPos0, fVel0, 0.0f, p, FALSE,
				    &fAud0, &fOcc0);
			A3DVAL px0 = 0, py0 = 0, pz0 = 0;
			pS->GetPosition3f(&px0, &py0, &pz0);
			scene_settle(pA3d, pGeom, pListener, pS, pMaterial,
				    p->fStart, 0.0f, fPos1, fVel1, 0.0f, p, FALSE,
				    &fAud1, &fOcc1);
			A3DVAL px1 = 0, py1 = 0, pz1 = 0;
			pS->GetPosition3f(&px1, &py1, &pz1);
			pS->Stop();

			std::snprintf(apDetail[2], sizeof apDetail[2],
				      "aud=%.4f/%.4f pos0=%.2f,%.2f,%.2f "
				      "pos1=%.2f,%.2f,%.2f",
				      (double) fAud0, (double) fAud1,
				      (double) px0, (double) py0, (double) pz0,
				      (double) px1, (double) py1, (double) pz1);
			aphr[2] = S_OK;
		}

		/* Room 2: test the SceneRooms wall, then move it along x to remove
		   occlusion. The separate polygon test uses different geometry. */
		{
			const scene_room *p = &g_Room[ROOM_OCCLUSION];
			IA3dSource2 *pS = apSource[ROOM_OCCLUSION];
			A3DVAL fPos[3], fVel[3];
			EvalSource(p, 0.0f, fPos, fVel);

			pS->Rewind();
			pS->Play(A3D_LOOPED);

			A3DVAL fAudBlocked, fOccBlocked, fAudOpen, fOccOpen;
			scene_settle(pA3d, pGeom, pListener, pS, pMaterial,
				    p->fStart, 0.0f, fPos, fVel, 0.0f, p, TRUE,
				    &fAudBlocked, &fOccBlocked);
			scene_settle(pA3d, pGeom, pListener, pS, pMaterial,
				    p->fStart, 0.0f, fPos, fVel, 0.0f, p, FALSE,
				    &fAudOpen, &fOccOpen);
			pS->Stop();

			std::snprintf(apDetail[3], sizeof apDetail[3],
				      "blocked=%.4f/%.4f open=%.4f/%.4f",
				      (double) fAudBlocked, (double) fOccBlocked,
				      (double) fAudOpen, (double) fOccOpen);
			aphr[3] = S_OK;
		}

		/* Room 3: sample doppler at t=0 with EvalSource's nonzero x velocity
		   and a stationary listener. */
		{
			const scene_room *p = &g_Room[ROOM_DOPPLER];
			IA3dSource2 *pS = apSource[ROOM_DOPPLER];
			A3DVAL fPos[3], fVel[3];
			EvalSource(p, 0.0f, fPos, fVel);

			pS->Rewind();
			pS->Play(A3D_LOOPED);

			A3DVAL fAud, fOcc;
			scene_settle(pA3d, pGeom, pListener, pS, pMaterial,
				    p->fStart, 0.0f, fPos, fVel, 0.0f, p, FALSE,
				    &fAud, &fOcc);

			A3DVAL vx = 0, vy = 0, vz = 0;
			HRESULT hv = pS->GetVelocity3f(&vx, &vy, &vz);
			A3DVAL fSrcDoppler = -1.0f, fRootDoppler = -1.0f;
			pS->GetDopplerScale(&fSrcDoppler);
			pA3d->GetDopplerScale(&fRootDoppler);
			pS->Stop();

			std::snprintf(apDetail[4], sizeof apDetail[4],
				      "aud=%.4f vel=%.3f,%.3f,%.3f (0x%08lX) "
				      "srcDop=%.3f rootDop=%.3f",
				      (double) fAud, (double) vx, (double) vy,
				      (double) vz, (unsigned long) hv,
				      (double) fSrcDoppler, (double) fRootDoppler);
			aphr[4] = S_OK;
		}

		/* Room 4: compare cone directions at t=0 and t=period/2, separated
		   by 180 degrees about y. Read back the SetCone angles and gain. */
		{
			const scene_room *p = &g_Room[ROOM_CONE];
			IA3dSource2 *pS = apSource[ROOM_CONE];
			A3DVAL fPos[3], fVel[3];
			EvalSource(p, 0.0f, fPos, fVel);

			A3DVAL fInside = -1.0f, fOutside = -1.0f, fOutGain = -1.0f;
			HRESULT hCone = pS->GetCone(&fInside, &fOutside, &fOutGain);

			pS->Rewind();
			pS->Play(A3D_LOOPED);

			A3DVAL w = 360.0f / (p->fPeriod > 0.0f ? p->fPeriod : 10.0f);
			A3DVAL fAud0, fOcc0, fAud1, fOcc1;
			scene_settle(pA3d, pGeom, pListener, pS, pMaterial,
				    p->fStart, 0.0f, fPos, fVel, w * 0.0f, p, FALSE,
				    &fAud0, &fOcc0);
			scene_settle(pA3d, pGeom, pListener, pS, pMaterial,
				    p->fStart, 0.0f, fPos, fVel,
				    w * (p->fPeriod * 0.5f), p, FALSE,
				    &fAud1, &fOcc1);
			pS->Stop();

			std::snprintf(apDetail[5], sizeof apDetail[5],
				      "cone=%.2f,%.2f,%.3f(0x%08lX) aud=%.4f/%.4f",
				      (double) fInside, (double) fOutside,
				      (double) fOutGain, (unsigned long) hCone,
				      (double) fAud0, (double) fAud1);
			aphr[5] = S_OK;
		}

		/* Room 5: compare the same head-relative source with the listener
		   at its initial position and after translation. */
		{
			const scene_room *p = &g_Room[ROOM_HEADRELATIVE];
			IA3dSource2 *pS = apSource[ROOM_HEADRELATIVE];
			A3DVAL fPos[3], fVel[3];
			EvalSource(p, 0.0f, fPos, fVel);

			DWORD dwMode0 = 0xFFFFFFFF;
			HRESULT hMode = pS->GetTransformMode(&dwMode0);

			pS->Rewind();
			pS->Play(A3D_LOOPED);

			A3DVAL fMoved[3] = { 2.0f, 0.0f, 2.0f };
			A3DVAL fAud0, fOcc0, fAud1, fOcc1;
			scene_settle(pA3d, pGeom, pListener, pS, pMaterial,
				    p->fStart, 0.0f, fPos, fVel, 0.0f, p, FALSE,
				    &fAud0, &fOcc0);
			A3DVAL px0 = 0, py0 = 0, pz0 = 0;
			pS->GetPosition3f(&px0, &py0, &pz0);
			scene_settle(pA3d, pGeom, pListener, pS, pMaterial,
				    fMoved, 0.0f, fPos, fVel, 0.0f, p, FALSE,
				    &fAud1, &fOcc1);
			A3DVAL px1 = 0, py1 = 0, pz1 = 0;
			pS->GetPosition3f(&px1, &py1, &pz1);
			pS->Stop();

			std::snprintf(apDetail[6], sizeof apDetail[6],
				      "mode=%lu(0x%08lX) pos0=%.2f,%.2f,%.2f "
				      "pos1=%.2f,%.2f,%.2f aud=%.4f/%.4f",
				      dwMode0, (unsigned long) hMode,
				      (double) px0, (double) py0, (double) pz0,
				      (double) px1, (double) py1, (double) pz1,
				      (double) fAud0, (double) fAud1);
			aphr[6] = S_OK;
		}

		/* Room 6: submit the g_Room[] box through EmitRoomGeometry and read
		   direct-path audibility. The Enable result is recorded in
		   aphr[0]/apDetail[0]. */
		{
			const scene_room *p = &g_Room[ROOM_REFLECTIONS];
			IA3dSource2 *pS = apSource[ROOM_REFLECTIONS];
			A3DVAL fPos[3], fVel[3];
			EvalSource(p, 0.0f, fPos, fVel);

			pS->Rewind();
			pS->Play(A3D_LOOPED);

			A3DVAL fAud, fOcc;
			scene_settle(pA3d, pGeom, pListener, pS, pMaterial,
				    p->fStart, 0.0f, fPos, fVel, 0.0f, p, TRUE,
				    &fAud, &fOcc);
			pS->Stop();

			std::snprintf(apDetail[7], sizeof apDetail[7],
				      "box aud=%.4f occ=%.4f", (double) fAud,
				      (double) fOcc);
			aphr[7] = S_OK;
		}

		/* Room 7: read direct-path audibility with CreateScene's reverb send.
		   NewReverb/BindReverb results were recorded above. */
		{
			const scene_room *p = &g_Room[ROOM_REVERB];
			IA3dSource2 *pS = apSource[ROOM_REVERB];
			A3DVAL fPos[3], fVel[3];
			EvalSource(p, 0.0f, fPos, fVel);

			pS->Rewind();
			pS->Play(A3D_LOOPED);

			A3DVAL fAud, fOcc;
			scene_settle(pA3d, pGeom, pListener, pS, pMaterial,
				    p->fStart, 0.0f, fPos, fVel, 0.0f, p, FALSE,
				    &fAud, &fOcc);
			pS->Stop();

			A3DVAL fMix = -1.0f, fMixReverb = -1.0f;
			HRESULT hMix = pS->GetReverbMix(&fMix, &fMixReverb);

			std::snprintf(apDetail[8], sizeof apDetail[8],
				      "aud=%.4f mix=%.3f,%.3f(0x%08lX)", (double) fAud,
				      (double) fMix, (double) fMixReverb,
				      (unsigned long) hMix);
			aphr[8] = S_OK;
		}

		for (size_t i = 0; i < NUM_ROOMS; i++)
			if (apSource[i])
				apSource[i]->Release();
		if (pReverb)
			pReverb->Release();
		if (pMaterial)
			pMaterial->Release();
		pListener->Release();
		pGeom->Release();
		return (true);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return (false);
	}
}

}	/* namespace */

namespace a3ddiff {

/* Names streamed back, one per aphr/apDetail slot guarded_scene fills in;
   index 0 is the CreateScene()-level Enable/NewReverb refusal check, 1..8
   are the eight rooms. */
static const char *const g_apszSceneStep[] = {
	"Scene Setup",
	"Scene Distance",
	"Scene Panning",
	"Scene Occlusion",
	"Scene Doppler",
	"Scene Cone",
	"Scene Headrelative",
	"Scene Reflections",
	"Scene Reverb",
};

#define NUM_SCENE_STEPS	(sizeof(g_apszSceneStep) / sizeof(g_apszSceneStep[0]))

int scenerooms_diff_child(const char *pszDll)
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
		for (size_t i = 0; i < NUM_SCENE_STEPS; i++)
			std::printf("STEP\t%s\t0x%08lX\t0\tno-root\n",
				    g_apszSceneStep[i], (unsigned long) hr);
		std::printf("END\n");
		std::fflush(stdout);
		return (0);
	}
	IA3d5 *pA3d = reinterpret_cast<IA3d5 *>(root);

	/* Same frame-time defect occlusion_comparison_tests.cpp guards against: two
	   flushes inside one ~15.6ms tick give a zero frame time and stall
	   the occlusion/audibility ramp. */
	timeBeginPeriod(1);

	char	adetail[NUM_SCENE_STEPS][192];
	HRESULT	ahr[NUM_SCENE_STEPS];
	for (size_t i = 0; i < NUM_SCENE_STEPS; i++) {
		adetail[i][0] = 0;
		ahr[i] = E_FAIL;
	}

	bool ok = guarded_scene(pA3d, adetail, ahr, NUM_SCENE_STEPS);

	timeEndPeriod(1);

	for (size_t i = 0; i < NUM_SCENE_STEPS; i++)
		std::printf("STEP\t%s\t0x%08lX\t%d\t%s\n", g_apszSceneStep[i],
			    (unsigned long) ahr[i], ok ? 0 : 1,
			    ok ? adetail[i] : "");
	std::printf("END\n");
	std::fflush(stdout);

	pA3d->Release();
	return (0);
}

}	/* namespace a3ddiff */

namespace {

/* Compare records from 2 --scene-child runs, one per DLL. */
class SceneRoomsVsReference : public ::testing::Test {
protected:
	static a3ddiff::ScenarioRunResult a;
	static a3ddiff::ScenarioRunResult b;

	static void SetUpTestSuite()
	{
		std::string self = a3ddiff::self_path();
		a = a3ddiff::run_child(self, A3D_AB_OUR_DLL, "--scene-child");
		b = a3ddiff::run_child(self, A3D_AB_REF_DLL, "--scene-child");
	}

	void expect_parity(const char *name)
	{
		const a3ddiff::RecordedStepResult *sa = a3ddiff::find_step(a, name);
		const a3ddiff::RecordedStepResult *sb = a3ddiff::find_step(b, name);
		ASSERT_TRUE(sa && sb) << name << ": step missing (a child "
			<< "stopped early: ours " << (a.ended ? "ok" : "crashed")
			<< ", ref " << (b.ended ? "ok" : "crashed") << ")";
		EXPECT_EQ(sa->faulted, sb->faulted) << name << ": fault state "
			<< "differs (ours " << (sa->faulted ? "FAULT" : "ok")
			<< ", ref " << (sb->faulted ? "FAULT" : "ok") << ")";
		EXPECT_EQ(sa->hr, sb->hr) << name << ": HRESULT differs (ours "
			<< a3ddiff::hrs(sa->hr) << " [" << sa->detail << "], ref "
			<< a3ddiff::hrs(sb->hr) << " [" << sb->detail << "])";
		EXPECT_EQ(sa->detail, sb->detail) << name << ": out-parameter "
			<< "differs (ours [" << sa->detail << "], ref ["
			<< sb->detail << "])";
	}
};

a3ddiff::ScenarioRunResult SceneRoomsVsReference::a;
a3ddiff::ScenarioRunResult SceneRoomsVsReference::b;

}	/* namespace */

/* Compare Enable(A3D_1ST_REFLECTIONS) and NewReverb failures in
   CreateScene order, with no A3D driver or A3D_REVERB request. */
TEST_F(SceneRoomsVsReference, Setup)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Scene Setup");
}

TEST_F(SceneRoomsVsReference, Distance)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Scene Distance");
}

TEST_F(SceneRoomsVsReference, Panning)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Scene Panning");
}

/* Use SceneRooms geometry here; WallSettle uses the SDK polygon geometry. */
TEST_F(SceneRoomsVsReference, Occlusion)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Scene Occlusion");
}

TEST_F(SceneRoomsVsReference, Doppler)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Scene Doppler");
}

TEST_F(SceneRoomsVsReference, Cone)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Scene Cone");
}

TEST_F(SceneRoomsVsReference, Headrelative)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Scene Headrelative");
}

TEST_F(SceneRoomsVsReference, Reflections)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Scene Reflections");
}

TEST_F(SceneRoomsVsReference, Reverb)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Scene Reverb");
}
