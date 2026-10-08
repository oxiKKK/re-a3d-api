/* Project-added DLL-local configuration; not part of the original DLL. */
#include "A3dConfig.h"

#if defined(A3D_FIXES)

static A3DCONFIG g_Config = { TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, FALSE, FALSE };
static LONG     g_lConfigState;

/* =============================================================
// A3dReadBoolean()
//
// Read a boolean from the A3D section; invalid values retain the default.
//
// Returns: Parsed boolean, or bDefault for missing or invalid input.
// =============================================================*/

static BOOL
A3dReadBoolean(LPCWSTR pszPath, LPCWSTR pszKey, BOOL bDefault)
{
WCHAR szValue[16];
DWORD cch;

	cch = GetPrivateProfileStringW(L"A3D", pszKey, L"", szValue,
				      sizeof(szValue) / sizeof(WCHAR), pszPath);
	if (!cch || cch >= sizeof(szValue) / sizeof(WCHAR) - 1)
		return (bDefault);
	if (!lstrcmpiW(szValue, L"true") || !lstrcmpW(szValue, L"1"))
		return (TRUE);
	if (!lstrcmpiW(szValue, L"false") || !lstrcmpW(szValue, L"0"))
		return (FALSE);
	return (bDefault);
}

/* =============================================================
// A3dLoadConfig()
//
// Read settings from the containing module directory.
// Module/path failures leave the game-ready defaults intact.
// =============================================================*/

static void
A3dLoadConfig(void)
{
MEMORY_BASIC_INFORMATION info;
WCHAR                   szPath[32768];
DWORD                   cch;
DWORD                   i;

	if (!VirtualQuery(&g_Config, &info, sizeof(info)))
		return;
	cch = GetModuleFileNameW((HMODULE) info.AllocationBase,
				szPath, sizeof(szPath) / sizeof(WCHAR));
	if (!cch || cch >= sizeof(szPath) / sizeof(WCHAR))
		return;
	for (i = cch; i && szPath[i - 1] != L'\\'; --i)
		;
	if (!i || i + sizeof(L"a3dapi.conf") / sizeof(WCHAR) >
	    sizeof(szPath) / sizeof(WCHAR))
		return;
	lstrcpyW(szPath + i, L"a3dapi.conf");
	g_Config.bEmulateHardware        = A3dReadBoolean(szPath,
		L"EmulateHardware", g_Config.bEmulateHardware);
	g_Config.bSoftwareReverb         = A3dReadBoolean(szPath,
		L"SoftwareReverb", g_Config.bSoftwareReverb);
	g_Config.bSoftwareReflections    = A3dReadBoolean(szPath,
		L"SoftwareReflections", g_Config.bSoftwareReflections);
	g_Config.bFixPropertyDeadlocks   = A3dReadBoolean(szPath,
		L"FixPropertyDeadlocks", g_Config.bFixPropertyDeadlocks);
	g_Config.bEnableMP3Decoder       = A3dReadBoolean(szPath,
		L"EnableMP3Decoder", g_Config.bEnableMP3Decoder);
	g_Config.bEnableAC3Decoder       = A3dReadBoolean(szPath,
		L"EnableAC3Decoder", g_Config.bEnableAC3Decoder);
	g_Config.bUseNewCredits          = A3dReadBoolean(szPath,
		L"UseNewCredits", g_Config.bUseNewCredits);
	g_Config.bRelaxCreditsActivation = A3dReadBoolean(szPath,
		L"RelaxCreditsActivation", g_Config.bRelaxCreditsActivation);
}

/* =============================================================
// A3dGetConfig()
//
// Publish immutable settings once; callers must be outside DllMain.
//
// Returns: Module-lifetime settings, using defaults after read failures.
// =============================================================*/

const A3DCONFIG &
A3dGetConfig(void)
{
	if (InterlockedCompareExchange(&g_lConfigState, 1, 0) == 0)
	{
		A3dLoadConfig();
		InterlockedExchange(&g_lConfigState, 2);
	}
	else
	{
		while (InterlockedCompareExchange(&g_lConfigState, 2, 2) != 2)
			Sleep(0);
	}
	return (g_Config);
}
#endif
