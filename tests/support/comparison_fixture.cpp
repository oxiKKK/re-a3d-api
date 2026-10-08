// NOT PART OF THE ORIGINAL. Project tooling.
#include "comparison_fixture.hpp"

a3ddiff::ScenarioRunResult InterfaceVsReference::a;
a3ddiff::ScenarioRunResult InterfaceVsReference::b;

void InterfaceVsReference::SetUpTestSuite()
{
    // Preserve the ordered setup used by the reference regression tests.
    const std::string self = a3ddiff::self_path();
    a = a3ddiff::run_child(self, A3D_AB_OUR_DLL);
    b = a3ddiff::run_child(self, A3D_AB_REF_DLL);
}

void InterfaceVsReference::expect_parity(const char* name)
{
    ASSERT_TRUE(a.loaded && b.loaded) << "DLL load or child launch failed";
    ASSERT_FALSE(a.timed_out || b.timed_out) << "Child deadline exceeded";
    ASSERT_TRUE(a.ended && b.ended) << "Child stopped before END";
    ASSERT_EQ(a.exit_code, 0u);
    ASSERT_EQ(b.exit_code, 0u);
	const a3ddiff::RecordedStepResult *sa = a3ddiff::find_step(a, name);
	const a3ddiff::RecordedStepResult *sb = a3ddiff::find_step(b, name);
	ASSERT_TRUE(sa && sb) << name << ": step missing (a child stopped "
		<< "early: ours " << (a.ended ? "ok" : "crashed") << ", ref "
		<< (b.ended ? "ok" : "crashed") << ")";
	EXPECT_EQ(sa->faulted, sb->faulted) << name << ": fault state differs "
		<< "(ours " << (sa->faulted ? "FAULT" : "ok") << ", ref "
		<< (sb->faulted ? "FAULT" : "ok") << ")";
	EXPECT_EQ(sa->hr, sb->hr) << name << ": HRESULT differs (ours "
		<< a3ddiff::hrs(sa->hr) << " [" << sa->detail << "], ref "
		<< a3ddiff::hrs(sb->hr) << " [" << sb->detail << "])";
	EXPECT_EQ(sa->detail, sb->detail) << name << ": out-parameter differs "
		<< "(ours [" << sa->detail << "], ref [" << sb->detail << "])";
}
