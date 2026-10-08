// Project-added A3D development tooling.
#pragma once
#include <gtest/gtest.h>
#include "scenario_runner.hpp"

/*
 * The fixture every per-interface file's tests use.  It lives in the global
 * namespace so TEST_F names it unqualified from any file.  SetUpTestSuite drives
 * both DLLs once, each in its own child process, so a hard fault takes only that
 * child down; expect_parity diffs one step by name.
 */
class InterfaceVsReference : public ::testing::Test {
protected:

	static a3ddiff::ScenarioRunResult a, b;
	static void SetUpTestSuite();
	void expect_parity(const char *name);
};
