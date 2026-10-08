// NOT PART OF THE ORIGINAL. Diagnostic macro evaluation contract.
#include "A3dPrivate.h"
#include <gtest/gtest.h>
#include <string>

#ifdef _DEBUG
namespace {
std::string warningReport;
std::wstring wideAssertionReport;

int __cdecl CaptureWideAssertion(int reportType, wchar_t *message, int *result)
{
    if (reportType != _CRT_ASSERT)
        return FALSE;
    wideAssertionReport = message;
    *result = 0;
    return TRUE;
}

int __cdecl CaptureWarning(int reportType, char *message, int *result)
{
    if (reportType != _CRT_WARN)
        return FALSE;
    warningReport = message;
    *result = 0;
    return TRUE;
}

}

TEST(DiagnosticMacros, AssertionTextPreservesExpressionParentheses)
{
    void *pSrc = NULL;
    _CrtSetReportHookW2(_CRT_RPTHOOK_INSTALL, CaptureWideAssertion);
    ASSERT(pSrc);
    const std::wstring bare = wideAssertionReport;
    ASSERT((pSrc));
    const std::wstring parenthesized = wideAssertionReport;
    VERIFY(SUCCEEDED(E_FAIL));
    const std::wstring verified = wideAssertionReport;
    _CrtSetReportHookW2(_CRT_RPTHOOK_REMOVE, CaptureWideAssertion);

    EXPECT_NE(bare.find(L"pSrc"), std::wstring::npos);
    EXPECT_EQ(bare.find(L"(pSrc)"), std::wstring::npos);
    EXPECT_NE(parenthesized.find(L"(pSrc)"), std::wstring::npos);
    EXPECT_EQ(parenthesized.find(L"((pSrc))"), std::wstring::npos);
    EXPECT_NE(verified.find(L"SUCCEEDED(E_FAIL)"), std::wstring::npos);
}

TEST(DiagnosticMacros, TraceAcceptsZeroAndMultipleFormattingArguments)
{
    _CRT_REPORT_HOOK previous = _CrtSetReportHook(CaptureWarning);
    TRACE("plain warning\n");
    const std::string plain = warningReport;
    int count = 0;
    TRACE("value=%d text=%s\n", ++count, "example");
    const std::string formatted = warningReport;
    _CrtSetReportHook(previous);

    EXPECT_EQ(plain, "plain warning\n");
    EXPECT_EQ(formatted, "value=1 text=example\n");
    EXPECT_EQ(count, 1);
}
#endif

TEST(DiagnosticMacros, RequiredExpressionExecutesOnce)
{
    int count = 0;
    VERIFY(++count == 1);
    EXPECT_EQ(count, 1);
}

TEST(DiagnosticMacros, AssertionEvaluationMatchesConfiguration)
{
    int count = 0;
    ASSERT(++count == 1);
#ifdef _DEBUG
    EXPECT_EQ(count, 1);
#else
    EXPECT_EQ(count, 0);
#endif
}

TEST(DiagnosticMacros, LoggingEvaluationMatchesConfiguration)
{
    int count = 0;
    TRACE("plain diagnostic macro test\n");
    TRACE("diagnostic macro test: %d\n", ++count);
    DBGSTR((++count, "diagnostic macro test\n"));
#ifdef _DEBUG
    EXPECT_EQ(count, 2);
#else
    EXPECT_EQ(count, 0);
#endif
}
