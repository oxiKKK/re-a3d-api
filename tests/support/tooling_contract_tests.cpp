// Validate the command-line and child-result interfaces used by developer tools.
#include <gtest/gtest.h>
#include "command_line.hpp"
#include "scenario_runner.hpp"

namespace {
a3dtools::CommandLine Parse(std::vector<std::string> arguments)
{
    std::vector<char*> pointers;
    for (auto& argument : arguments) pointers.push_back(argument.data());
    return a3dtools::CommandLine(static_cast<int>(pointers.size()), pointers.data(), "dll timeout-ms", "");
}

void ReadLine(a3ddiff::ScenarioRunResult& result, std::string line)
{
    a3ddiff::take_line(result, line.data());
}
}

TEST(ToolingContracts, RejectsAmbiguousArguments)
{
    EXPECT_THROW(Parse({"tool", "--dll"}), std::runtime_error);
    EXPECT_THROW(Parse({"tool", "--unknown"}), std::runtime_error);
    EXPECT_THROW(Parse({"tool", "--dll", "a", "--dll", "b"}), std::runtime_error);
    EXPECT_THROW(Parse({"tool", "--dll", "a", "legacy"}), std::runtime_error);
    EXPECT_EQ(Parse({"tool", "legacy"}).positional.size(), 1u);
}

TEST(ToolingContracts, ValidatesDeadlineRange)
{
    EXPECT_EQ(Parse({"tool", "--timeout-ms", "2000"}).Number("timeout-ms", 1, 1, 60000), 2000u);
    for (const char* value : {"-1", "0", "60001", "100x", "99999999999999999999"}) {
        EXPECT_THROW(Parse({"tool", "--timeout-ms", value}).Number("timeout-ms", 1, 1, 60000), std::runtime_error);
    }
}

TEST(ToolingContracts, ParsesFaultsAndEmptyDetails)
{
    a3ddiff::ScenarioRunResult result;
    ReadLine(result, "STEP\tCreate\t0x80004005\t1\t\r\n");
    ASSERT_EQ(result.steps.size(), 1u);
    EXPECT_EQ(result.steps[0].hr, E_FAIL);
    EXPECT_TRUE(result.steps[0].faulted);
    EXPECT_TRUE(result.steps[0].detail.empty());
    ReadLine(result, "END\r\n");
    EXPECT_TRUE(result.ended);
}

TEST(ToolingContracts, RejectsMalformedChildRecords)
{
    a3ddiff::ScenarioRunResult result;
    ReadLine(result, "ENDING\n");
    ReadLine(result, "STEP\tmissing fields\n");
    ReadLine(result, "STEP\tCreate\tinvalid\t0\tobj\n");
    ReadLine(result, "STEP\tCreate\t0x00000000\t2\tobj\n");
    EXPECT_FALSE(result.ended);
    EXPECT_TRUE(result.steps.empty());
    ReadLine(result, "ISOLATE\tFAIL\n");
    EXPECT_FALSE(result.loaded);
}
