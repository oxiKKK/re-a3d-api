/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * listener_comparison_tests.cpp - Interface comparison: IA3dListener.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling. See tests/README.md.
 *
 * Listener singleton: position, orientation, velocity getters and set/get roundtrips.
 * Harness and fixture in comparison_fixture.hpp.
 *
 *---------------------------------------------------------------------------
 */

#include "comparison_fixture.hpp"

using namespace a3ddiff;

namespace {

void s_listener_getpos(ScenarioContext *c, StepResult *o)
{
	IA3dListener *pL = qi_as<IA3dListener>(c->pRoot, IID_IA3dListener);
	if (!pL) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	A3DVAL x = -1.0f, y = -1.0f, z = -1.0f;
	o->hr = pL->GetPosition3f(&x, &y, &z);
	std::snprintf(o->detail, sizeof o->detail, "%.3f,%.3f,%.3f",
		      (double) x, (double) y, (double) z);
	pL->Release();
}

void s_listener_pos_rt(ScenarioContext *c, StepResult *o)
{
	IA3dListener *pL = qi_as<IA3dListener>(c->pRoot, IID_IA3dListener);
	if (!pL) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	pL->SetPosition3f(1.5f, 2.5f, 3.5f);
	A3DVAL x = -1.0f, y = -1.0f, z = -1.0f;
	o->hr = pL->GetPosition3f(&x, &y, &z);
	std::snprintf(o->detail, sizeof o->detail, "%.3f,%.3f,%.3f",
		      (double) x, (double) y, (double) z);
	pL->Release();
}

void s_listener_getorient(ScenarioContext *c, StepResult *o)
{
	IA3dListener *pL = qi_as<IA3dListener>(c->pRoot, IID_IA3dListener);
	if (!pL) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	A3DVAL a = -1.0f, b = -1.0f, d = -1.0f;
	o->hr = pL->GetOrientationAngles3f(&a, &b, &d);
	std::snprintf(o->detail, sizeof o->detail, "%.3f,%.3f,%.3f",
		      (double) a, (double) b, (double) d);
	pL->Release();
}

void s_listener_getvel(ScenarioContext *c, StepResult *o)
{
	IA3dListener *pL = qi_as<IA3dListener>(c->pRoot, IID_IA3dListener);
	if (!pL) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	A3DVAL x = -1.0f, y = -1.0f, z = -1.0f;
	o->hr = pL->GetVelocity3f(&x, &y, &z);
	std::snprintf(o->detail, sizeof o->detail, "%.3f,%.3f,%.3f",
		      (double) x, (double) y, (double) z);
	pL->Release();
}

void s_listener_vel_rt(ScenarioContext *c, StepResult *o)
{
	IA3dListener *pL = qi_as<IA3dListener>(c->pRoot, IID_IA3dListener);
	if (!pL) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	pL->SetVelocity3f(1.5f, 2.5f, 3.5f);
	A3DVAL x = -1.0f, y = -1.0f, z = -1.0f;
	o->hr = pL->GetVelocity3f(&x, &y, &z);
	std::snprintf(o->detail, sizeof o->detail, "%.3f,%.3f,%.3f",
		      (double) x, (double) y, (double) z);
	pL->Release();
}

void s_listener_orient6_rt(ScenarioContext *c, StepResult *o)
{
	IA3dListener *pL = qi_as<IA3dListener>(c->pRoot, IID_IA3dListener);
	if (!pL) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	pL->SetOrientation6f(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
	A3DVAL fx = -1.0f, fy = -1.0f, fz = -1.0f, ux = -1.0f, uy = -1.0f, uz = -1.0f;
	o->hr = pL->GetOrientation6f(&fx, &fy, &fz, &ux, &uy, &uz);
	std::snprintf(o->detail, sizeof o->detail, "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f",
		      (double) fx, (double) fy, (double) fz,
		      (double) ux, (double) uy, (double) uz);
	pL->Release();
}

/* Vector forms (SetX3fv/GetX3fv take pointer to three floats) and orientation-angles roundtrip. */

void s_listener_pos3fv_rt(ScenarioContext *c, StepResult *o)
{
	IA3dListener *pL = qi_as<IA3dListener>(c->pRoot, IID_IA3dListener);
	if (!pL) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	A3DVAL in[4] = { 4.5f, 5.5f, 6.5f, 0.0f };
	pL->SetPosition3fv(in);
	A3DVAL v[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
	o->hr = pL->GetPosition3fv(v);
	std::snprintf(o->detail, sizeof o->detail, "%.3f,%.3f,%.3f",
		      (double) v[0], (double) v[1], (double) v[2]);
	pL->Release();
}

void s_listener_orient_angles_rt(ScenarioContext *c, StepResult *o)
{
	IA3dListener *pL = qi_as<IA3dListener>(c->pRoot, IID_IA3dListener);
	if (!pL) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	pL->SetOrientationAngles3f(0.25f, 0.5f, 0.75f);
	A3DVAL a = -1.0f, b = -1.0f, d = -1.0f;
	o->hr = pL->GetOrientationAngles3f(&a, &b, &d);
	std::snprintf(o->detail, sizeof o->detail, "%.3f,%.3f,%.3f",
		      (double) a, (double) b, (double) d);
	pL->Release();
}

void s_listener_vel3fv_rt(ScenarioContext *c, StepResult *o)
{
	IA3dListener *pL = qi_as<IA3dListener>(c->pRoot, IID_IA3dListener);
	if (!pL) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	A3DVAL in[4] = { 7.5f, 8.5f, 9.5f, 0.0f };
	pL->SetVelocity3fv(in);
	A3DVAL v[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
	o->hr = pL->GetVelocity3fv(v);
	std::snprintf(o->detail, sizeof o->detail, "%.3f,%.3f,%.3f",
		      (double) v[0], (double) v[1], (double) v[2]);
	pL->Release();
}

}	/* namespace */

namespace a3ddiff {

void add_listener_steps(Script &s)
{
	s.push_back({ "Listener GetPosition",    s_listener_getpos });
	s.push_back({ "Listener SetGetPosition", s_listener_pos_rt });
	s.push_back({ "Listener GetOrientation", s_listener_getorient });
	s.push_back({ "Listener GetVelocity",    s_listener_getvel });
	s.push_back({ "Listener SetGetVelocity", s_listener_vel_rt });
	s.push_back({ "Listener SetGetOrient6",  s_listener_orient6_rt });
	s.push_back({ "Listener SetGetPos3fv",   s_listener_pos3fv_rt });
	s.push_back({ "Listener SetGetOrientAngles", s_listener_orient_angles_rt });
	s.push_back({ "Listener SetGetVel3fv",   s_listener_vel3fv_rt });
}

}	/* namespace a3ddiff */

TEST_F(InterfaceVsReference, ListenerGetters)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Listener GetPosition");
	expect_parity("Listener SetGetPosition");
	expect_parity("Listener GetOrientation");
	expect_parity("Listener GetVelocity");
}

TEST_F(InterfaceVsReference, ListenerSetGetVectors)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Listener SetGetVelocity");
	expect_parity("Listener SetGetOrient6");
	expect_parity("Listener SetGetPos3fv");
	expect_parity("Listener SetGetOrientAngles");
	expect_parity("Listener SetGetVel3fv");
}
