/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * gtest_main.cpp - the test runner entry.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling.
 *
 * Installs CRT quieting through a testing::Environment.  When built with
 * A3D_INTERFACE_DIFF_CHILD, honours the `--child <dll>` protocol and returns
 * before gtest starts; otherwise runs the tests.
 *
 *---------------------------------------------------------------------------
 */

#include <gtest/gtest.h>

#include <cstring>

#include "crt_error_reporting.hpp"

#ifdef A3D_INTERFACE_DIFF_CHILD
namespace a3ddiff {
	int interface_diff_child(const char *pszDll, const char *scenario);	/* support/scenario_runner.cpp */
	int occlusion_diff_child(const char *pszDll);	/* api/occlusion_comparison_tests.cpp */
	int scenerooms_diff_child(const char *pszDll);	/* api/scenerooms_comparison_tests.cpp */
	int mapper_diff_child(const char *pszDll);	/* api/mapper_comparison_tests.cpp */
}
#endif

namespace {

class crt_quiet_env : public ::testing::Environment {
public:
	void SetUp() override { a3dtest::crt_quiet(); }
};

}	/* namespace */

int
main(int argc, char **argv)
{
#ifdef A3D_INTERFACE_DIFF_CHILD
	if (argc >= 3 && std::strcmp(argv[1], "--child") == 0)
		return a3ddiff::interface_diff_child(argv[2],
            argc >= 5 && std::strcmp(argv[3], "--scenario") == 0 ? argv[4] : "all");
	if (argc >= 3 && std::strcmp(argv[1], "--occl-child") == 0)
		return a3ddiff::occlusion_diff_child(argv[2]);
	if (argc >= 3 && std::strcmp(argv[1], "--scene-child") == 0)
		return a3ddiff::scenerooms_diff_child(argv[2]);
	if (argc >= 3 && std::strcmp(argv[1], "--mapper-child") == 0)
		return a3ddiff::mapper_diff_child(argv[2]);
#endif

	::testing::InitGoogleTest(&argc, argv);
	::testing::AddGlobalTestEnvironment(new crt_quiet_env);
	return RUN_ALL_TESTS();
}
