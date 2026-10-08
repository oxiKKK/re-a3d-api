/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * propertyset_comparison_tests.cpp - Interface comparison: IA3dPropertySet.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling. See tests/README.md.
 *
 *---------------------------------------------------------------------------
 */

#include "comparison_fixture.hpp"

using namespace a3ddiff;

namespace {

/* A property-set GUID the engine does not define, so QuerySupport takes the
   not-supported path on both sides rather than any real property. */
static const GUID kUnknownPropertySet =
	{ 0x00000000, 0x0000, 0x0000, { 0, 0, 0, 0, 0, 0, 0, 0 } };


void s_qi_propset(ScenarioContext *c, StepResult *o)   { qi(c, o, IID_IA3dPropertySet); }

void s_propset_querysupport(ScenarioContext *c, StepResult *o)
{
	IA3dPropertySet *pP = qi_as<IA3dPropertySet>(c->pRoot, IID_IA3dPropertySet);
	if (!pP) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	ULONG support = 0xFFFFFFFF;
	o->hr = pP->QuerySupport(kUnknownPropertySet, 0, &support);
	std::snprintf(o->detail, sizeof o->detail, "sup=0x%08lX",
		      (unsigned long) support);
	pP->Release();
}

/* Neither DLL checks the null output pointer. Compare the exception
   recorded by the child's SEH handler. */
void s_propset_querysupport_null(ScenarioContext *c, StepResult *o)
{
	IA3dPropertySet *pP = qi_as<IA3dPropertySet>(c->pRoot, IID_IA3dPropertySet);
	if (!pP) {
		o->hr = E_NOINTERFACE;
		std::snprintf(o->detail, sizeof o->detail, "no-iface");
		return;
	}
	ULONG	support = 0xFFFFFFFF;
	HRESULT hn = pP->QuerySupport(kUnknownPropertySet, 0, NULL);

	o->hr = hn;
	std::snprintf(o->detail, sizeof o->detail,
		      "sup=0x%08lX null=0x%08lX",
		      (unsigned long) support, (unsigned long) hn);
	pP->Release();
}

/* Get, Set and AddInitialStateParameters block in a headless run on both
   DLLs and are left out of this script. */

/* Compare source-level IA3dPropertySet availability. Resource-manager
   buffers expose it, but the source may not forward that interface. */
void s_propset_on_source(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_POINTER;
		std::snprintf(o->detail, sizeof o->detail, "no-source");
		return;
	}
	void *p = NULL;
	o->hr = c->pSrc->QueryInterface(IID_IA3dPropertySet, &p);
	std::snprintf(o->detail, sizeof o->detail, "%s",
		      p ? "iface" : "null");
	if (p)
		((IA3dPropertySet *) p)->Release();
}

}	/* namespace */

namespace a3ddiff {

void add_propertyset_steps(Script &s)
{
	s.push_back({ "QI IA3dPropertySet",  s_qi_propset });
	s.push_back({ "PropSet QuerySupport", s_propset_querysupport });

	s.push_back({ "PropSet QuerySupportNull", s_propset_querysupport_null });
	s.push_back({ "PropSet OnSource",         s_propset_on_source });
}

}	/* namespace a3ddiff */

TEST_F(InterfaceVsReference, PropertySet)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("QI IA3dPropertySet");
	expect_parity("PropSet QuerySupport");
}

TEST_F(InterfaceVsReference, PropertySetRefusals)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("PropSet QuerySupportNull");
	expect_parity("PropSet OnSource");
}
