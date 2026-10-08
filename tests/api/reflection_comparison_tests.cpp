/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * reflection_comparison_tests.cpp - Interface comparison: IA3dReflection.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling. See tests/README.md.
 *
 *---------------------------------------------------------------------------
 */

#include "comparison_fixture.hpp"

using namespace a3ddiff;

namespace {

/* The count with no reflection made.  A source that has placed none reports
   zero rather than refusing, and a null out-pointer is E_POINTER. */
void s_refl_count(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}
	int	n  = -1;
	HRESULT hc = c->pSrc->GetNumManualReflections(&n);
	HRESULT hn = c->pSrc->GetNumManualReflections(NULL);

	o->hr = hc;
	std::snprintf(o->detail, sizeof o->detail, "n=%d null=0x%08lX",
		      n, (unsigned long) hn);
}

/* Compare creation HRESULT, returned pointer and reflection count.
   Native sources and engines initialized without reflections may reject
   creation; compare those failures too. */
void s_refl_new(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}

	LPA3DREFLECTION pRefl = NULL;
	HRESULT		hr    = c->pSrc->NewManualReflection(&pRefl);

	int n = -1;
	c->pSrc->GetNumManualReflections(&n);

	std::snprintf(o->detail, sizeof o->detail, "new=0x%08lX %s n=%d",
		      (unsigned long) hr, pRefl ? "obj" : "null", n);
	o->hr = hr;

	if (pRefl)
		pRefl->Release();
}

/* Skip property roundtrips if reflection creation failed. The step
   records the same missing-object result for either DLL. */
void s_refl_properties(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}

	LPA3DREFLECTION pRefl = NULL;
	HRESULT		hr    = c->pSrc->NewManualReflection(&pRefl);
	if (FAILED(hr) || !pRefl) {
		o->hr = hr;
		std::snprintf(o->detail, sizeof o->detail, "no-refl=0x%08lX",
			      (unsigned long) hr);
		return;
	}

	A3DVAL	fGain = -1.0f, fDelay = -1.0f, fEq = -1.0f;
	DWORD	dwMode = 0xFFFFFFFF;

	pRefl->SetGainScale(0.25f);
	pRefl->GetGainScale(&fGain);
	pRefl->SetDelay(0.5f);
	pRefl->GetDelay(&fDelay);
	pRefl->SetEQ(0.75f);
	pRefl->GetEQ(&fEq);
	pRefl->SetTransformMode(A3DREFLECTION_TRANSFORMMODE_HEADRELATIVE);
	o->hr = pRefl->GetTransformMode(&dwMode);

	std::snprintf(o->detail, sizeof o->detail,
		      "gain=%.3f delay=%.3f eq=%.3f mode=%lu",
		      (double) fGain, (double) fDelay, (double) fEq, dwMode);

	pRefl->Release();
}

/* The position, through both the three-float and the vector form, and the
   refusals each gives a null pointer. */
void s_refl_position(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}

	LPA3DREFLECTION pRefl = NULL;
	HRESULT		hr    = c->pSrc->NewManualReflection(&pRefl);
	if (FAILED(hr) || !pRefl) {
		o->hr = hr;
		std::snprintf(o->detail, sizeof o->detail, "no-refl=0x%08lX",
			      (unsigned long) hr);
		return;
	}

	A3DVAL x = 0.0f, y = 0.0f, z = 0.0f;
	A3DVAL v[3] = { 0.0f, 0.0f, 0.0f };

	pRefl->SetPosition3f(1.0f, 2.0f, 3.0f);
	pRefl->GetPosition3f(&x, &y, &z);
	o->hr = pRefl->GetPosition3fv(v);

	HRESULT hn = pRefl->GetPosition3fv(NULL);

	std::snprintf(o->detail, sizeof o->detail,
		      "%.2f,%.2f,%.2f v=%.2f,%.2f,%.2f null=0x%08lX",
		      (double) x, (double) y, (double) z,
		      (double) v[0], (double) v[1], (double) v[2],
		      (unsigned long) hn);

	pRefl->Release();
}

/* Freeing them.  FreeManualReflections drops the whole list, so the count
   afterwards is what tells the two sides apart. */
void s_refl_free(ScenarioContext *c, StepResult *o)
{
	if (!c->pSrc) {
		o->hr = E_FAIL;
		std::snprintf(o->detail, sizeof o->detail, "no-src");
		return;
	}

	int nBefore = -1;
	c->pSrc->GetNumManualReflections(&nBefore);

	HRESULT hf = c->pSrc->FreeManualReflections();

	int nAfter = -1;
	c->pSrc->GetNumManualReflections(&nAfter);

	o->hr = hf;
	std::snprintf(o->detail, sizeof o->detail, "free=0x%08lX %d->%d",
		      (unsigned long) hf, nBefore, nAfter);
}

}	/* namespace */

namespace a3ddiff {

void add_reflection_steps(Script &s)
{
	s.push_back({ "Refl Count",      s_refl_count });
	s.push_back({ "Refl New",        s_refl_new });
	s.push_back({ "Refl Properties", s_refl_properties });
	s.push_back({ "Refl Position",   s_refl_position });
	s.push_back({ "Refl Free",       s_refl_free });
}

}	/* namespace a3ddiff */

TEST_F(InterfaceVsReference, ReflectionList)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Refl Count");
	expect_parity("Refl New");
	expect_parity("Refl Free");
}

TEST_F(InterfaceVsReference, ReflectionProperties)
{
	ASSERT_TRUE(a.loaded && b.loaded);
	expect_parity("Refl Properties");
	expect_parity("Refl Position");
}
