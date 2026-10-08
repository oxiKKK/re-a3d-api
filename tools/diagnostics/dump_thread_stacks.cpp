/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * dump_thread_stacks.cpp
 *
 * NOT PART OF THE ORIGINAL.  Project tooling.
 *
 * Print each process thread's stack as module + RVA, with symbols when
 * available. For the Retail a3dapi.dll image base 0x10000000,
 * a3dapi.dll+0x00027BB0 maps to rtl:0x10027bb0. Select the matching build
 * when resolving addresses.
 *
 * Built Win32, because hl.exe is Win32 and a 64-bit StackWalk64 cannot walk a
 * WOW64 thread without the WOW64 context calls.
 *
 * Usage:
 *	dump_thread_stacks <pid|name.exe> [module=<substr>] [repeat=N]
 *
 * `module' filters threads by a substring in any stack module name.
 * `repeat' samples stacks N times at 1-second intervals.
 *
 *---------------------------------------------------------------------------
 */

#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------------ */

static HANDLE	g_hProc;
static DWORD stack_sample_interval_ms = 1000;
static char	g_szFilter[64];

/* Print module + RVA and any available symbol for one stack frame. */

static void
PrintFrame(int i, DWORD64 dwPc)
{
BYTE		abSym[sizeof(SYMBOL_INFO) + 256];
SYMBOL_INFO	*psi = (SYMBOL_INFO *) abSym;
IMAGEHLP_MODULE64 mi;
DWORD64		dwDisp;
char		szMod[MAX_PATH];
const char	*pszName;
char		*p;

	pszName = "(no module)";
	szMod[0] = '\0';

	ZeroMemory(&mi, sizeof(mi));
	mi.SizeOfStruct = sizeof(mi);

	if (SymGetModuleInfo64(g_hProc, dwPc, &mi))
	{
		strncpy(szMod, mi.ImageName, sizeof(szMod) - 1);
		szMod[sizeof(szMod) - 1] = '\0';

		p	= strrchr(szMod, '\\');
		pszName = p ? p + 1 : szMod;

		printf("   %2d  %-20s +0x%08lX", i, pszName,
		       (unsigned long) (dwPc - mi.BaseOfImage));
	}
	else
		printf("   %2d  %-20s  0x%08llX", i, pszName,
		       (unsigned long long) dwPc);

	ZeroMemory(abSym, sizeof(abSym));

	psi->SizeOfStruct = sizeof(SYMBOL_INFO);
	psi->MaxNameLen	  = 255;

	if (SymFromAddr(g_hProc, dwPc, &dwDisp, psi))
		printf("  %s+0x%lX", psi->Name, (unsigned long) dwDisp);

	printf("\n");
}

/* ------------------------------------------------------------------------ */

/* Walk one thread. The thread stays suspended for the walk and is resumed afterwards. */

static void
WalkThread(DWORD dwTid)
{
CONTEXT		ScenarioContext;
STACKFRAME64	sf;
HANDLE		hThread;
DWORD64		adwPc[64];
int		cFrames, fHit, i;

	hThread = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION |
			     THREAD_SUSPEND_RESUME, FALSE, dwTid);

	if (!hThread)
		return;

	if (SuspendThread(hThread) == (DWORD) -1)
	{
		CloseHandle(hThread);

		return;
	}

	ZeroMemory(&ScenarioContext, sizeof(ScenarioContext));
	ScenarioContext.ContextFlags = CONTEXT_FULL;

	cFrames = 0;

	if (GetThreadContext(hThread, &ScenarioContext))
	{
		ZeroMemory(&sf, sizeof(sf));

		sf.AddrPC.Offset    = ScenarioContext.Eip;
		sf.AddrPC.Mode	    = AddrModeFlat;
		sf.AddrFrame.Offset = ScenarioContext.Ebp;
		sf.AddrFrame.Mode   = AddrModeFlat;
		sf.AddrStack.Offset = ScenarioContext.Esp;
		sf.AddrStack.Mode   = AddrModeFlat;

		while (cFrames < (int) (sizeof(adwPc) / sizeof(adwPc[0])))
		{
			if (!StackWalk64(IMAGE_FILE_MACHINE_I386, g_hProc,
					 hThread, &sf, &ScenarioContext, NULL,
					 SymFunctionTableAccess64,
					 SymGetModuleBase64, NULL))
				break;

			if (!sf.AddrPC.Offset)
				break;

			adwPc[cFrames++] = sf.AddrPC.Offset;
		}
	}

	ResumeThread(hThread);
	CloseHandle(hThread);

	if (!cFrames)
		return;

	/* Frames are collected first so the filter can be decided over the whole stack. */

	fHit = 0;

	if (g_szFilter[0])
	{
		for (i = 0; i < cFrames; i++)
		{
			IMAGEHLP_MODULE64	mi;
			char			*p;

			ZeroMemory(&mi, sizeof(mi));
			mi.SizeOfStruct = sizeof(mi);

			if (!SymGetModuleInfo64(g_hProc, adwPc[i], &mi))
				continue;

			p = strrchr(mi.ImageName, '\\');

			if (_strnicmp(p ? p + 1 : mi.ImageName, g_szFilter,
				      strlen(g_szFilter)) == 0)
			{
				fHit = 1;

				break;
			}
		}

		if (!fHit)
			return;
	}

	printf("\n  thread %lu:\n", (unsigned long) dwTid);

	for (i = 0; i < cFrames; i++)
		PrintFrame(i, adwPc[i]);
}

/* ------------------------------------------------------------------------ */

static DWORD
FindProcess(const char *pszWhat)
{
HANDLE		hSnap;
PROCESSENTRY32	pe;
DWORD		dwPid;

	dwPid = (DWORD) strtoul(pszWhat, NULL, 10);

	if (dwPid)
		return (dwPid);

	hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

	if (hSnap == INVALID_HANDLE_VALUE)
		return (0);

	ZeroMemory(&pe, sizeof(pe));
	pe.dwSize = sizeof(pe);

	if (Process32First(hSnap, &pe))
	{
		do
		{
			if (_stricmp(pe.szExeFile, pszWhat) == 0)
			{
				CloseHandle(hSnap);

				return (pe.th32ProcessID);
			}
		}
		while (Process32Next(hSnap, &pe));
	}

	CloseHandle(hSnap);

	return (0);
}

/* ------------------------------------------------------------------------ */

int
RunLegacy(int argc, char **argv)
{
HANDLE		hSnap;
THREADENTRY32	te;
DWORD		dwPid;
int		cRepeat, iPass, i;

	if (argc < 2)
	{
		printf("usage: dump_thread_stacks <pid|name.exe> [module=<substr>] [repeat=N]\n");

		return (1);
	}

	cRepeat = 1;
	g_szFilter[0] = '\0';

	for (i = 2; i < argc; i++)
	{
		if (!strncmp(argv[i], "module=", 7))
		{
			strncpy(g_szFilter, argv[i] + 7, sizeof(g_szFilter) - 1);
			g_szFilter[sizeof(g_szFilter) - 1] = '\0';
		}
		else if (!strncmp(argv[i], "repeat=", 7))
			cRepeat = atoi(argv[i] + 7);
	}

	dwPid = FindProcess(argv[1]);

	if (!dwPid)
	{
		printf("dump_thread_stacks: no such process: %s\n", argv[1]);

		return (1);
	}

	g_hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ |
			      PROCESS_SUSPEND_RESUME, FALSE, dwPid);

	if (!g_hProc)
	{
		printf("dump_thread_stacks: OpenProcess %lu failed, error %lu\n",
		       (unsigned long) dwPid, (unsigned long) GetLastError());

		return (1);
	}

	SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME |
		      SYMOPT_LOAD_LINES | SYMOPT_NO_PROMPTS);

	if (!SymInitialize(g_hProc, NULL, TRUE))
		printf("dump_thread_stacks: SymInitialize failed, error %lu\n",
		       (unsigned long) GetLastError());

	for (iPass = 0; iPass < cRepeat; iPass++)
	{
		printf("\n===== pid %lu  pass %d/%d%s%s =====\n",
		       (unsigned long) dwPid, iPass + 1, cRepeat,
		       g_szFilter[0] ? "  module=" : "", g_szFilter);

		hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);

		if (hSnap == INVALID_HANDLE_VALUE)
			break;

		ZeroMemory(&te, sizeof(te));
		te.dwSize = sizeof(te);

		if (Thread32First(hSnap, &te))
		{
			do
			{
				if (te.th32OwnerProcessID == dwPid)
					WalkThread(te.th32ThreadID);
			}
			while (Thread32Next(hSnap, &te));
		}

		CloseHandle(hSnap);
		fflush(stdout);

		if (iPass + 1 < cRepeat)
			Sleep(stack_sample_interval_ms);
	}

	SymCleanup(g_hProc);
	CloseHandle(g_hProc);

	return (0);
}

#include "command_line.hpp"
int main(int argc, char** argv)
{
    try {
        a3dtools::CommandLine args(argc, argv, "pid process module samples interval-ms", "");
        if (args.Has("help")) { puts(R"HELP(dump_thread_stacks --pid <id> | --process <name.exe> [--module <substring>] [--samples N] [--interval-ms N]
Print module-relative frames and available symbols for each matching thread.
Requires Windows x86 and a 32-bit target. Briefly suspends each inspected thread.
Exit: 0 completed, 1 process/access failure, 2 invalid arguments.
Legacy positional process, module=, and repeat= arguments remain supported.)HELP"); return 0; }

        if (!args.positional.empty()) return RunLegacy(argc, argv);
        if (args.Has("pid") == args.Has("process")) throw std::runtime_error("Specify one of --pid or --process");
        if (args.Has("pid")) args.Number("pid", 0, 1, 0xFFFFFFFFu);
        stack_sample_interval_ms = args.Number("interval-ms", 1000, 1, 60000);
        std::vector<std::string> legacy = {argv[0], args.Get(args.Has("pid") ? "pid" : "process"),
            "module=" + args.Get("module"), "repeat=" + std::to_string(args.Number("samples", 1, 1, 10000))};
        return a3dtools::InvokeLegacy(RunLegacy, legacy);
    } catch (const std::exception& error) { fprintf(stderr, "%s\n", error.what()); return 2; }
}
