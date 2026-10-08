/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * scene_comparison_tests.cpp - Interface comparison: the private room-based
 * scene interfaces reached through IA3dPrv1.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling. See tests/README.md.
 *
 *---------------------------------------------------------------------------
 */

#include "comparison_fixture.hpp"

#include <cstring>

#include "a3dapi/a3d33.h"

using namespace a3ddiff;

namespace {

const GUID IID_Prv1 =
	{ 0x103e7222, 0x0113, 0x11d2, { 0x90, 0xfb, 0x00, 0x60, 0x08, 0xa1, 0xf4, 0x41 } };

#define HX(h) ((unsigned long) (h))
#define SHELL 0x00000001

struct Objects {
	IA3dPrv1           *pPrv;
	IA3dScene          *pScene;
	IA3dRoomBuilder    *pRoom;
	IA3dWallBuilder    *pWall;
	IA3dOpeningBuilder *pOpening;
};

bool make_objects(ScenarioContext *c, StepResult *o, Objects *p)
{
	std::memset(p, 0, sizeof *p);
	o->hr = c->pRoot->QueryInterface(IID_Prv1, (void **) &p->pPrv);
	if (FAILED(o->hr) || !p->pPrv) {
		std::snprintf(o->detail, sizeof o->detail, "qi");
		return (false);
	}
	HRESULT hs = p->pPrv->NewScene(A3D_SCENE_3D, (class CA3dScene **) &p->pScene);
	HRESULT hr = p->pPrv->NewRoomBuilder(A3D_SCENE_3D, (class CA3dRoomBuilder **) &p->pRoom);
	HRESULT hw = p->pPrv->NewWallBuilder(A3D_SCENE_3D, (class CA3dWallBuilder **) &p->pWall);
	HRESULT ho = p->pPrv->NewOpeningBuilder(A3D_SCENE_3D, (class CA3dOpeningBuilder **) &p->pOpening);
	if (!p->pScene || !p->pRoom || !p->pWall || !p->pOpening) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "new=%08lX/%08lX/%08lX/%08lX",
			HX(hs), HX(hr), HX(hw), HX(ho));
		return (false);
	}
	return (true);
}

void release_objects(Objects *p)
{
	if (p->pScene)
		p->pScene->Release();
	if (p->pRoom)
		p->pRoom->Release();
	if (p->pWall)
		p->pWall->Release();
	if (p->pOpening)
		p->pOpening->Release();
	if (p->pPrv)
		p->pPrv->Release();
}

HRESULT square(IA3dWallBuilder *pWall, A3DVAL h)
{
	pWall->Begin(A3D_QUADS);
	pWall->Vertex3f(-h, -h, 0.0f);
	pWall->Vertex3f( h, -h, 0.0f);
	pWall->Vertex3f( h,  h, 0.0f);
	pWall->Vertex3f(-h,  h, 0.0f);
	return (pWall->End());
}

struct Face { A3DVAL pos[4], front[4], up[4]; };
const Face cube[6] = {
	{ {  0,  0,  2, 1 }, {  0,  0,  1, 0 }, { 0, 1, 0, 0 } },
	{ {  0,  0, -2, 1 }, {  0,  0, -1, 0 }, { 0, 1, 0, 0 } },
	{ {  2,  0,  0, 1 }, {  1,  0,  0, 0 }, { 0, 1, 0, 0 } },
	{ { -2,  0,  0, 1 }, { -1,  0,  0, 0 }, { 0, 1, 0, 0 } },
	{ {  0,  2,  0, 1 }, {  0,  1,  0, 0 }, { 0, 0, 1, 0 } },
	{ {  0, -2,  0, 1 }, {  0, -1,  0, 0 }, { 0, 0, 1, 0 } },
};

int add_cube(IA3dRoomBuilder *pRoom, IA3dWallBuilder *pWall, int cFaces)
{
	int r = 0;
	for (int i = 0; i < cFaces; i++)
		r = pRoom->AddWall(pWall, SHELL, (LPA3DVAL) cube[i].pos,
				   (LPA3DVAL) cube[i].front, (LPA3DVAL) cube[i].up);
	return (r);
}

int room_index(IA3dScene *pScene, void *pFound)
{
	if (!pFound)
		return (-1);
	for (int i = 0; i < 4; i++) {
		void *p = NULL;
		if (SUCCEEDED(pScene->GetRoom(i, &p)) && p) {
			bool same = (p == pFound);
			((IUnknown *) p)->Release();
			if (same)
				return (i);
		}
	}
	return (-2);
}

void s_factories(ScenarioContext *c, StepResult *o)
{
	Objects ob;
	if (!make_objects(c, o, &ob)) {
		release_objects(&ob);
		return;
	}
	void *p2 = NULL, *p0 = NULL;
	HRESULT h2 = ob.pPrv->NewWallBuilder(A3D_SCENE_2D, (class CA3dWallBuilder **) &p2);
	HRESULT h0 = ob.pPrv->NewScene(0, (class CA3dScene **) &p0);
	if (p2)
		((IUnknown *) p2)->Release();
	if (p0)
		((IUnknown *) p0)->Release();
	HRESULT hn = ob.pPrv->NewScene(A3D_SCENE_3D, NULL);
	o->hr = S_OK;
	std::snprintf(o->detail, sizeof o->detail, "isscene=%d rooms=%08lX walls=%d 2d=%08lX/%d zero=%08lX/%d null=%08lX",
		ob.pScene->IsScene(), HX(ob.pRoom->Validate()),
		ob.pWall->GetPrimitiveCount(), HX(h2), p2 ? 1 : 0, HX(h0), p0 ? 1 : 0, HX(hn));
	release_objects(&ob);
}

void s_wall_primitives(ScenarioContext *c, StepResult *o)
{
	Objects ob;
	if (!make_objects(c, o, &ob)) {
		release_objects(&ob);
		return;
	}
	HRESULT h0 = ob.pWall->Validate();
	HRESULT hs = square(ob.pWall, 2.0f);
	int     n1 = ob.pWall->GetPrimitiveCount();
	HRESULT hv = ob.pWall->Validate();
	A3DVAL  v[4] = { -9, -9, -9, -9 };
	HRESULT hg = ob.pWall->GetVertex(0, 2, v);
	HRESULT hp = ob.pWall->SetVertex(0, 2, 2.0f, 2.0f, 1.0f);
	HRESULT hb = ob.pWall->Validate();
	HRESULT hr = ob.pWall->RemovePrimitive(0);
	int     n2 = ob.pWall->GetPrimitiveCount();
	HRESULT hx = ob.pWall->RemovePrimitive(5);
	HRESULT hc = ob.pWall->Clear();
	o->hr = hs;
	std::snprintf(o->detail, sizeof o->detail,
		"empty=%08lX n=%d val=%08lX get=%08lX/%.1f,%.1f,%.1f set=%08lX bent=%08lX rm=%08lX/%d bad=%08lX clr=%08lX",
		HX(h0), n1, HX(hv), HX(hg), (double) v[0], (double) v[1], (double) v[2],
		HX(hp), HX(hb), HX(hr), n2, HX(hx), HX(hc));
	release_objects(&ob);
}

void s_wall_bad_input(ScenarioContext *c, StepResult *o)
{
	Objects ob;
	if (!make_objects(c, o, &ob)) {
		release_objects(&ob);
		return;
	}
	HRESULT hv = ob.pWall->Vertex3f(0, 0, 0);
	HRESULT hb = ob.pWall->Begin(0x1234);
	ob.pWall->Begin(A3D_TRIANGLES);
	ob.pWall->Vertex3f(0, 0, 0);
	ob.pWall->Vertex3f(1, 0, 0);
	HRESULT he = ob.pWall->End();
	int     n  = ob.pWall->GetPrimitiveCount();
	HRESULT hl = ob.pWall->Validate();
	o->hr = hv;
	std::snprintf(o->detail, sizeof o->detail, "begin=%08lX partial=%08lX n=%d val=%08lX",
		HX(hb), HX(he), n, HX(hl));
	release_objects(&ob);
}

void s_room_box(ScenarioContext *c, StepResult *o)
{
	Objects ob;
	if (!make_objects(c, o, &ob)) {
		release_objects(&ob);
		return;
	}
	square(ob.pWall, 2.0f);
	HRESULT h0 = ob.pRoom->Validate();
	int     r3 = add_cube(ob.pRoom, ob.pWall, 3);
	HRESULT h3 = ob.pRoom->Validate();
	ob.pRoom->Clear();
	int     r6 = add_cube(ob.pRoom, ob.pWall, 6);
	HRESULT h6 = ob.pRoom->Validate();
	void   *pw = NULL;
	HRESULT hg = ob.pRoom->GetWall(1, &pw);
	if (pw)
		((IUnknown *) pw)->Release();
	HRESULT hr = ob.pRoom->RemoveWall(1);
	HRESULT h5 = ob.pRoom->Validate();
	o->hr = h6;
	std::snprintf(o->detail, sizeof o->detail,
		"empty=%08lX add3=%d val3=%08lX add6=%d getwall=%08lX/%d rm=%08lX val5=%08lX",
		HX(h0), r3, HX(h3), r6, HX(hg), pw ? 1 : 0, HX(hr), HX(h5));
	release_objects(&ob);
}

void s_room_bad_walls(ScenarioContext *c, StepResult *o)
{
	Objects ob;
	if (!make_objects(c, o, &ob)) {
		release_objects(&ob);
		return;
	}
	A3DVAL front[4] = { 0, 0, 1, 0 }, slanted[4] = { 0, 1, 1, 0 };
	int    hn = ob.pRoom->AddWall(NULL, SHELL, NULL, NULL, NULL);
	int    he = ob.pRoom->AddWall(ob.pWall, SHELL, NULL, NULL, NULL);
	square(ob.pWall, 2.0f);
	int    hs = ob.pRoom->AddWall(ob.pWall, SHELL, NULL, front, slanted);
	for (int i = 0; i < 4; i++)
		ob.pRoom->AddWall(ob.pWall, SHELL, (LPA3DVAL) cube[0].pos,
				  (LPA3DVAL) cube[0].front, (LPA3DVAL) cube[0].up);
	o->hr = ob.pRoom->Validate();
	std::snprintf(o->detail, sizeof o->detail, "null=%08X empty=%08X skew=%08X",
		(unsigned) hn, (unsigned) he, (unsigned) hs);
	release_objects(&ob);
}

void s_scene_two_rooms(ScenarioContext *c, StepResult *o)
{
	Objects ob;
	if (!make_objects(c, o, &ob)) {
		release_objects(&ob);
		return;
	}
	square(ob.pWall, 2.0f);
	add_cube(ob.pRoom, ob.pWall, 6);
	A3DVAL a[4] = { 0, 0, 0, 1 }, b[4] = { 4, 0, 0, 1 };
	int ra = ob.pScene->AddRoom(NULL, ob.pRoom, a, NULL, NULL);
	int rb = ob.pScene->AddRoom(NULL, ob.pRoom, b, NULL, NULL);
	void *pf = NULL;
	HRESULT hu = ob.pScene->FindByPoint(0, 0, 0, &pf);
	HRESULT hb = ob.pScene->Build();
	int idx[5];
	const A3DVAL pts[5][3] = { { 0, 0, 0 }, { 4, 0, 0 }, { 10, 0, 0 }, { 1.9f, 1, 1 }, { 5.5f, -1, 1 } };
	HRESULT hp = S_OK;
	for (int i = 0; i < 5; i++) {
		void *p = NULL;
		hp = ob.pScene->FindByPoint(pts[i][0], pts[i][1], pts[i][2], &p);
		idx[i] = room_index(ob.pScene, p);
		if (p)
			((IUnknown *) p)->Release();
	}
	HRESULT hn = ob.pScene->FindByPoint(0, 0, 0, NULL);
	o->hr = hb;
	std::snprintf(o->detail, sizeof o->detail,
		"add=%d,%d unbuilt=%08lX/%d find=%08lX/%d,%d,%d,%d,%d null=%08lX",
		ra, rb, HX(hu), pf ? 1 : 0, HX(hp), idx[0], idx[1], idx[2], idx[3], idx[4], HX(hn));
	if (pf)
		((IUnknown *) pf)->Release();
	release_objects(&ob);
}

void s_scene_compile(ScenarioContext *c, StepResult *o)
{
	Objects ob;
	if (!make_objects(c, o, &ob)) {
		release_objects(&ob);
		return;
	}
	square(ob.pWall, 2.0f);
	add_cube(ob.pRoom, ob.pWall, 6);
	A3DVAL a[4] = { 0, 0, 0, 1 }, b[4] = { 4, 0, 0, 1 };
	ob.pScene->AddRoom(NULL, ob.pRoom, a, NULL, NULL);
	ob.pScene->AddRoom(NULL, ob.pRoom, b, NULL, NULL);
	HRESULT hb = ob.pScene->Build();
	HRESULT hs = ob.pScene->SetCurrentRoom(1);
	HRESULT hf = ob.pScene->SetRoomFlags(1, 1);
	HRESULT hk = ob.pScene->ClearRoomFlags(1, 1);
	HRESULT hc = ob.pScene->Compile();
	HRESULT hg = ob.pPrv->GatherRooms((DWORD) (DWORD_PTR) ob.pScene);
	HRESULT hr = ob.pScene->ReleaseCompiledScene();
	HRESULT hx = ob.pScene->SetCurrentRoom(9);
	HRESULT hl = ob.pScene->Clear();
	o->hr = hc;
	std::snprintf(o->detail, sizeof o->detail,
		"build=%08lX cur=%08lX flags=%08lX/%08lX gather=%08lX rel=%08lX badcur=%08lX clear=%08lX",
		HX(hb), HX(hs), HX(hf), HX(hk), HX(hg), HX(hr), HX(hx), HX(hl));
	release_objects(&ob);
}

void s_scene_walls(ScenarioContext *c, StepResult *o)
{
	Objects ob;
	if (!make_objects(c, o, &ob)) {
		release_objects(&ob);
		return;
	}
	square(ob.pWall, 2.0f);
	A3DVAL at[4] = { 0, 0, -3, 1 };
	int    w1 = ob.pScene->AddWall(NULL, ob.pWall, at, NULL, NULL);
	int    w2 = ob.pScene->AddWall(NULL, ob.pWall, NULL, NULL, NULL);
	void  *pw = NULL;
	HRESULT hg = ob.pScene->GetWall(0, 1, &pw);
	if (pw)
		((IUnknown *) pw)->Release();
	HRESULT hr = ob.pScene->RemoveWall(1);
	HRESULT hx = ob.pScene->RemoveWall(7);
	HRESULT hy = ob.pScene->RemoveRoom(3);
	o->hr = ob.pScene->Build();
	std::snprintf(o->detail, sizeof o->detail, "add=%d,%d get=%08lX/%d rm=%08lX bad=%08lX badroom=%08lX",
		w1, w2, HX(hg), pw ? 1 : 0, HX(hr), HX(hx), HX(hy));
	release_objects(&ob);
}

/* Save, Load and Duplicate with null arguments end both processes with a
   fail-fast exception, so persistence is not compared. */

void s_wall_opening(ScenarioContext *c, StepResult *o)
{
	Objects ob;
	if (!make_objects(c, o, &ob)) {
		release_objects(&ob);
		return;
	}
	HRESULT h0 = ob.pOpening->Validate();
	ob.pOpening->Begin(A3D_QUADS);
	ob.pOpening->Vertex3f(-0.5f, -1.0f, 0.0f);
	ob.pOpening->Vertex3f( 0.5f, -1.0f, 0.0f);
	ob.pOpening->Vertex3f( 0.5f,  1.0f, 0.0f);
	ob.pOpening->Vertex3f(-0.5f,  1.0f, 0.0f);
	HRESULT he = ob.pOpening->End();
	HRESULT hv = ob.pOpening->Validate();
	square(ob.pWall, 2.0f);
	int     n1 = ob.pWall->AddOpening(ob.pOpening, NULL, NULL, NULL);
	A3DVAL  off[4] = { 0, 0, 1, 1 };
	int     n2 = ob.pWall->AddOpening(ob.pOpening, off, NULL, NULL);
	HRESULT hw = ob.pWall->Validate();
	void   *pg = NULL;
	HRESULT hg = ob.pWall->GetOpening(0, &pg);
	if (pg)
		((IUnknown *) pg)->Release();
	HRESULT hr = ob.pWall->RemoveOpening(1);
	HRESULT hw2 = ob.pWall->Validate();
	o->hr = hv;
	std::snprintf(o->detail, sizeof o->detail,
		"empty=%08lX end=%08lX add=%d,%d val=%08lX get=%08lX/%d rm=%08lX val2=%08lX",
		HX(h0), HX(he), n1, n2, HX(hw), HX(hg), pg ? 1 : 0, HX(hr), HX(hw2));
	release_objects(&ob);
}

void s_names(ScenarioContext *c, StepResult *o)
{
	Objects ob;
	if (!make_objects(c, o, &ob)) {
		release_objects(&ob);
		return;
	}
	/* GetName writes past the stated size, so give it room. */
	char sn[512], rn[512], wn[512];
	std::memset(sn, 0, sizeof sn);
	std::memset(rn, 0, sizeof rn);
	std::memset(wn, 0, sizeof wn);
	HRESULT h1 = ob.pScene->SetName("hall");
	HRESULT h2 = ob.pScene->GetName(sn, 32);
	ob.pRoom->SetName("room");
	ob.pRoom->GetName(rn, 32);
	ob.pWall->SetName("wall");
	ob.pWall->GetName(wn, 3);
	o->hr = h1;
	std::snprintf(o->detail, sizeof o->detail,
		"get=%08lX/%s room=%s wall=%s",
		HX(h2), sn, rn, wn);
	release_objects(&ob);
}

/* A wall builder's own plane is what the room sees, so each face of the box
   gets its own builder with vertices already on that face. */
const A3DVAL box_faces[6][4][3] = {
	{ {  2, -2, -2 }, {  2,  2, -2 }, {  2,  2,  2 }, {  2, -2,  2 } },
	{ { -2, -2,  2 }, { -2,  2,  2 }, { -2,  2, -2 }, { -2, -2, -2 } },
	{ { -2,  2, -2 }, { -2,  2,  2 }, {  2,  2,  2 }, {  2,  2, -2 } },
	{ { -2, -2,  2 }, { -2, -2, -2 }, {  2, -2, -2 }, {  2, -2,  2 } },
	{ { -2, -2,  2 }, {  2, -2,  2 }, {  2,  2,  2 }, { -2,  2,  2 } },
	{ {  2, -2, -2 }, { -2, -2, -2 }, { -2,  2, -2 }, {  2,  2, -2 } },
};

int add_box(IA3dPrv1 *pPrv, IA3dRoomBuilder *pRoom)
{
	int r = 0;
	for (int f = 0; f < 6; f++) {
		IA3dWallBuilder *pWall = NULL;
		pPrv->NewWallBuilder(A3D_SCENE_3D, (class CA3dWallBuilder **) &pWall);
		if (!pWall)
			return (-1);
		pWall->Begin(A3D_QUADS);
		for (int v = 0; v < 4; v++)
			pWall->Vertex3f(box_faces[f][v][0], box_faces[f][v][1], box_faces[f][v][2]);
		pWall->End();
		r = pRoom->AddWall(pWall, SHELL, NULL, NULL, NULL);
		pWall->Release();
	}
	return (r);
}

void s_box_room(ScenarioContext *c, StepResult *o)
{
	Objects ob;
	if (!make_objects(c, o, &ob)) {
		release_objects(&ob);
		return;
	}
	int r = add_box(ob.pPrv, ob.pRoom);
	o->hr = ob.pRoom->Validate();
	A3DVAL at[4] = { 0, 0, 0, 1 }, away[4] = { 10, 0, 0, 1 };
	int ra = ob.pScene->AddRoom(NULL, ob.pRoom, at, NULL, NULL);
	int rb = ob.pScene->AddRoom(NULL, ob.pRoom, away, NULL, NULL);
	HRESULT hb = ob.pScene->Build();
	int idx[6];
	const A3DVAL pts[6][3] = { { 0, 0, 0 }, { 10, 0, 0 }, { 5, 0, 0 },
				   { 1.9f, 1.9f, 1.9f }, { 11.5f, -1, 1 }, { 0, 3, 0 } };
	for (int i = 0; i < 6; i++) {
		void *p = NULL;
		ob.pScene->FindByPoint(pts[i][0], pts[i][1], pts[i][2], &p);
		idx[i] = room_index(ob.pScene, p);
		if (p)
			((IUnknown *) p)->Release();
	}
	std::snprintf(o->detail, sizeof o->detail,
		"walls=%d add=%d,%d build=%08lX find=%d,%d,%d,%d,%d,%d",
		r, ra, rb, HX(hb), idx[0], idx[1], idx[2], idx[3], idx[4], idx[5]);
	release_objects(&ob);
}

void s_box_compile(ScenarioContext *c, StepResult *o)
{
	Objects ob;
	if (!make_objects(c, o, &ob)) {
		release_objects(&ob);
		return;
	}
	add_box(ob.pPrv, ob.pRoom);
	A3DVAL at[4] = { 0, 0, 0, 1 }, next[4] = { 4, 0, 0, 1 };
	ob.pScene->AddRoom(NULL, ob.pRoom, at, NULL, NULL);
	ob.pScene->AddRoom(NULL, ob.pRoom, next, NULL, NULL);
	HRESULT hb = ob.pScene->Build();
	HRESULT hs = ob.pScene->SetCurrentRoom(1);
	HRESULT hf = ob.pScene->SetRoomFlags(1, 1);
	HRESULT hk = ob.pScene->ClearRoomFlags(1, 1);
	HRESULT hc = ob.pScene->Compile();
	HRESULT hg = ob.pPrv->GatherRooms((DWORD) (DWORD_PTR) ob.pScene);
	void   *pw = NULL;
	HRESULT hw = ob.pScene->GetWall(1, 2, &pw);
	if (pw)
		((IUnknown *) pw)->Release();
	HRESULT hr = ob.pScene->ReleaseCompiledScene();
	HRESULT hd = ob.pScene->RemoveRoom(1);
	HRESULT hb2 = ob.pScene->Build();
	o->hr = hc;
	std::snprintf(o->detail, sizeof o->detail,
		"build=%08lX cur=%08lX flags=%08lX/%08lX gather=%08lX wall=%08lX/%d rel=%08lX rm=%08lX rebuild=%08lX",
		HX(hb), HX(hs), HX(hf), HX(hk), HX(hg), HX(hw), pw ? 1 : 0, HX(hr), HX(hd), HX(hb2));
	release_objects(&ob);
}

const struct { const char *name; step_fn fn; } scene_steps[] = {
	{ "Scene Factories",      s_factories },
	{ "Scene WallPrimitives", s_wall_primitives },
	{ "Scene WallBadInput",   s_wall_bad_input },
	{ "Scene RoomBox",        s_room_box },
	{ "Scene RoomBadWalls",   s_room_bad_walls },
	{ "Scene TwoRooms",       s_scene_two_rooms },
	{ "Scene Compile",        s_scene_compile },
	{ "Scene Walls",          s_scene_walls },
	{ "Scene WallOpening",    s_wall_opening },
	{ "Scene Names",          s_names },
	{ "Scene BoxRoom",        s_box_room },
	{ "Scene BoxCompile",     s_box_compile },
};

}	/* namespace */

namespace a3ddiff {

void add_scene_steps(Script &s)
{
	for (const auto &step : scene_steps)
		s.push_back({ step.name, step.fn });
}

}	/* namespace a3ddiff */

class SceneVsReference : public InterfaceVsReference,
			 public ::testing::WithParamInterface<const char *> {};

TEST_P(SceneVsReference, Step)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity(GetParam());
}

INSTANTIATE_TEST_SUITE_P(Private, SceneVsReference, ::testing::Values(
	"Scene Factories", "Scene WallPrimitives", "Scene WallBadInput",
	"Scene RoomBox", "Scene RoomBadWalls", "Scene TwoRooms", "Scene Compile",
	"Scene Walls", "Scene WallOpening", "Scene Names", "Scene BoxRoom",
	"Scene BoxCompile"));
