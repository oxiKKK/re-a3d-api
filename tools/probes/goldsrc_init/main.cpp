/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * a3d_goldsrc_init_probe.cpp
 *
 * NOT PART OF THE ORIGINAL.  Project tooling.
 *
 * Replay GoldSrc's A3D initialization against a selected DLL to locate
 * failures or stalls in CA3DRenderer::CreateA3dRenderer.
 *
 * The sequence is CA3DRenderer::CreateA3dRenderer and CreateSoundBuffer from
 * GoldSrc build 8684's snd_a3d.cpp, in that file's order:
 *
 *	A3dCreate			IClassFactory::CreateInstance, IID_IA3d3
 *	IA3d3::SetCooperativeLevel	the app window, A3D_CL_EXCLUSIVE
 *	IA3d3::SetResourceManagerMode	A3DSOURCE_TYPEUNMANAGED
 *	IA3d3::QueryInterface		IID_IA3dSound
 *	IA3dSound::CreateSoundBuffer	the primary buffer
 *
 * A3dCreate()'s own Init is part of the sequence: ia3dutil.cpp:385 calls it with
 * a feature mask of 127.  `noinit' leaves it out and `nosplash' adds the
 * A3D_DISABLE_SPLASHSCREEN bit that 127 does not carry.
 *
 * A watchdog thread prints the step every second and the process exits 2 after
 * the deadline, identifying the stalled call.
 *
 * Use a process-owned window, as GoldSrc does. `pump' services it on
 * a second thread; otherwise leave it unpumped to reproduce S_Init setup.
 *
 * Usage:
 *	a3d_goldsrc_init_probe <path\to\a3dapi.dll> [init] [pump] [desktop] [secs=N]
 *
 *---------------------------------------------------------------------------
 */

#include <windows.h>
#include <objbase.h>
#include <mmsystem.h>
#include <dsound.h>
#include <dbghelp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <initguid.h>
#include "ia3dapi.h"

/*
 * IID_IA3dSound is the DirectSound-compatible interface GoldSrc requests from the root.
 * inc/ia3dapi.h does not declare it; src/a3dapi/A3dPrivate.h has the same GUID
 * under the name IID_IA3dPrv3.  The value is in all three 677 images.
 */

DEFINE_GUID(IID_IA3dSound, 0x62507921, 0x3ea6, 0x11d2,
	    0x90, 0xfb, 0x00, 0x60, 0x08, 0xa1, 0xf4, 0x41);

static const char	*g_pszStep = "(none)";
static HMODULE		g_hDll;
static DWORD		g_dwDeadline = 20;
static DWORD probe_timeout_ms = 0;
static DWORD		g_dwFeatures = 127;
static HANDLE		g_hMainThread;

/* ------------------------------------------------------------------------ */

/* Print the blocked thread's stack as module + RVA with available symbols.
   For Retail a3dapi.dll at image base 0x10000000,
   a3dapi.dll+0x00023FA0 maps to rtl:0x10023fa0. */

static void
Backtrace(void)
{
CONTEXT		ScenarioContext;
STACKFRAME64	sf;
HANDLE		hProc;
char		szMod[MAX_PATH];
int		i;

	hProc = GetCurrentProcess();

	SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
	SymInitialize(hProc, NULL, TRUE);

	if (SuspendThread(g_hMainThread) == (DWORD) -1)
	{
		printf("*** could not suspend the main thread\n");

		return;
	}

	ZeroMemory(&ScenarioContext, sizeof(ScenarioContext));
	ScenarioContext.ContextFlags = CONTEXT_FULL;

	if (!GetThreadContext(g_hMainThread, &ScenarioContext))
	{
		printf("*** could not read the main thread's context\n");
		ResumeThread(g_hMainThread);

		return;
	}

	ZeroMemory(&sf, sizeof(sf));

	sf.AddrPC.Offset    = ScenarioContext.Eip;
	sf.AddrPC.Mode	    = AddrModeFlat;
	sf.AddrFrame.Offset = ScenarioContext.Ebp;
	sf.AddrFrame.Mode   = AddrModeFlat;
	sf.AddrStack.Offset = ScenarioContext.Esp;
	sf.AddrStack.Mode   = AddrModeFlat;

	printf("\n*** stack of the blocked thread:\n");

	for (i = 0; i < 40; i++)
	{
		BYTE		abSym[sizeof(SYMBOL_INFO) + 256];
		SYMBOL_INFO	*psi = (SYMBOL_INFO *) abSym;
		HMODULE		hMod;
		DWORD64		dw64;

		if (!StackWalk64(IMAGE_FILE_MACHINE_I386, hProc, g_hMainThread,
				 &sf, &ScenarioContext, NULL, SymFunctionTableAccess64,
				 SymGetModuleBase64, NULL))
			break;

		if (!sf.AddrPC.Offset)
			break;

		hMod	 = NULL;
		szMod[0] = '\0';

		if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
				       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				       (LPCSTR) (UINT_PTR) sf.AddrPC.Offset,
				       &hMod) && hMod)
		{
			char	*p;

			GetModuleFileNameA(hMod, szMod, sizeof(szMod));

			p = strrchr(szMod, '\\');

			printf("  %2d  %-22s +0x%08lX", i, p ? p + 1 : szMod,
			       (unsigned long) (sf.AddrPC.Offset -
						(UINT_PTR) hMod));
		}
		else
		{
			printf("  %2d  %-22s  0x%08lX", i, "(no module)",
			       (unsigned long) sf.AddrPC.Offset);
		}

		ZeroMemory(abSym, sizeof(abSym));

		psi->SizeOfStruct = sizeof(SYMBOL_INFO);
		psi->MaxNameLen	  = 255;

		if (SymFromAddr(hProc, sf.AddrPC.Offset, &dw64, psi))
			printf("  %s", psi->Name);

		printf("\n");
	}

	ResumeThread(g_hMainThread);
}

/* ------------------------------------------------------------------------ */

/* The watchdog prints the step once a second and kills the process at the deadline. */

static DWORD WINAPI
Watchdog(LPVOID pv)
{
DWORD	dwSecs;

	(void) pv;

	DWORD start = GetTickCount();
	DWORD timeout = probe_timeout_ms ? probe_timeout_ms : g_dwDeadline * 1000;
	for (dwSecs = 0; GetTickCount() - start < timeout; dwSecs++)
	{
		DWORD elapsed = GetTickCount() - start;
		if (elapsed >= timeout) break;
		DWORD remaining = timeout - elapsed;
		Sleep(remaining < 1000 ? remaining : 1000);

		fprintf(stderr, "    [%2lu s] still in: %s\n",
			(unsigned long) dwSecs + 1, g_pszStep);
		fflush(stderr);
	}

	printf("\n*** STOPPED in step: %s\n", g_pszStep);
	printf("*** no return after %lu milliseconds\n",
	       (unsigned long) timeout);

	Backtrace();

	fflush(stdout);

	ExitProcess(2);

	return (0);
}

#define STEP(what)	do { g_pszStep = (what); \
			     printf("  %-42s ", g_pszStep); fflush(stdout); } while (0)

#define OKHR(hr)	do { printf("hr=0x%08lX %s\n", (unsigned long) (hr), \
				    FAILED(hr) ? "FAILED" : "ok"); fflush(stdout); } while (0)

/* ------------------------------------------------------------------------ */

/* The message loop runs on its own thread so the main thread can block on A3D calls. */

static DWORD WINAPI
PumpThread(LPVOID pv)
{
MSG	msg;

	(void) pv;

	while (GetMessage(&msg, NULL, 0, 0) > 0)
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return (0);
}

static HWND
MakeWindow(void)
{
WNDCLASSA	wc;
HWND		hWnd;

	ZeroMemory(&wc, sizeof(wc));

	wc.lpfnWndProc	 = DefWindowProcA;
	wc.hInstance	 = GetModuleHandleA(NULL);
	wc.lpszClassName = "a3d_goldsrc_init_probe";

	RegisterClassA(&wc);

	hWnd = CreateWindowExA(0, "a3d_goldsrc_init_probe", "a3d_goldsrc_init_probe", WS_OVERLAPPEDWINDOW,
			       CW_USEDEFAULT, CW_USEDEFAULT, 320, 240,
			       NULL, NULL, wc.hInstance, NULL);

	return (hWnd);
}

/* ------------------------------------------------------------------------ */

int
RunLegacy(int argc, char **argv)
{
HRESULT			(WINAPI *pfnGetClassObject)(REFCLSID, REFIID, void **);
IClassFactory		*pcf;
IA3d3			*pA3d;
IDirectSound		*pSound;		/* the IA3dSound interface */
IDirectSoundBuffer	*pPrimary;
DSBUFFERDESC1		desc;
WAVEFORMATEX		wfx;
const char		*pszDll;
HWND			hWnd;
DWORD			dwId;
HRESULT			hr;
int			fNoInit, fNoSplash, fPump, fDesktop, fDsCaps, i;

	if (argc < 2)
	{
		printf("usage: a3d_goldsrc_init_probe <a3dapi.dll> [init] [pump] [desktop] [secs=N]\n");

		return (1);
	}

	pszDll	 = argv[1];
	fNoInit	  = 0;
	fNoSplash = 0;
	fPump	  = 0;
	fDesktop  = 0;
	fDsCaps	  = 0;

	for (i = 2; i < argc; i++)
	{
		if (!strcmp(argv[i], "noinit"))
			fNoInit = 1;
		else if (!strcmp(argv[i], "nosplash"))
			fNoSplash = 1;
		else if (!strcmp(argv[i], "pump"))
			fPump = 1;
		else if (!strcmp(argv[i], "dscaps"))
			fDsCaps = 1;
		else if (!strcmp(argv[i], "desktop"))
			fDesktop = 1;
		else if (!strncmp(argv[i], "features=", 9))
			g_dwFeatures = (DWORD) strtoul(argv[i] + 9, NULL, 0);
		else if (!strncmp(argv[i], "secs=", 5))
			g_dwDeadline = (DWORD) atoi(argv[i] + 5);
	}

	printf("a3d_goldsrc_init_probe: %s\n", pszDll);
	printf("  noinit=%d nosplash=%d pump=%d desktop=%d deadline=%lus\n\n",
	       fNoInit, fNoSplash, fPump, fDesktop, (unsigned long) g_dwDeadline);
	fflush(stdout);

	DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
			GetCurrentProcess(), &g_hMainThread,
			0, FALSE, DUPLICATE_SAME_ACCESS);

	CreateThread(NULL, 0, Watchdog, NULL, 0, &dwId);



	if (fDesktop)
	{
		hWnd = GetDesktopWindow();
		printf("  window: GetDesktopWindow() %p\n", (void *) hWnd);
	}
	else
	{
		hWnd = MakeWindow();
		printf("  window: CreateWindowEx %p\n", (void *) hWnd);
	}

	if (fPump)
		CreateThread(NULL, 0, PumpThread, NULL, 0, &dwId);

	fflush(stdout);

	/* Probe what FlushPropertySetCache queries: hardware acceleration and 3D control. */

	if (fDsCaps)
	{
		IDirectSound		*pDS = NULL;
		IDirectSoundBuffer	*pB = NULL;
		IUnknown		*pKs = NULL;
		WAVEFORMATEX		w;
		DSBUFFERDESC1		d;
		DSBCAPS			c;

		CoInitialize(NULL);

		printf("  --- DSBCAPS_LOCHARDWARE|DSBCAPS_CTRL3D probe ---\n");

		hr = CoCreateInstance(CLSID_DirectSound, NULL,
				      CLSCTX_INPROC_SERVER, IID_IDirectSound,
				      (void **) &pDS);
		printf("  CoCreateInstance CLSID_DirectSound   hr=0x%08lX\n",
		       (unsigned long) hr);

		if (SUCCEEDED(hr))
		{
			hr = pDS->Initialize(NULL);
			printf("  IDirectSound::Initialize             hr=0x%08lX\n",
			       (unsigned long) hr);

			pDS->SetCooperativeLevel(hWnd, DSSCL_PRIORITY);

			ZeroMemory(&w, sizeof(w));
			w.wFormatTag	  = WAVE_FORMAT_PCM;
			w.nChannels	  = 1;
			w.nSamplesPerSec  = 11025;
			w.nAvgBytesPerSec = 22050;
			w.nBlockAlign	  = 2;
			w.wBitsPerSample  = 16;

			ZeroMemory(&d, sizeof(d));
			d.dwSize	= sizeof(d);
			d.dwFlags	= DSBCAPS_LOCHARDWARE | DSBCAPS_CTRL3D;
			d.dwBufferBytes	= 1024;
			d.lpwfxFormat	= &w;

			hr = pDS->CreateSoundBuffer((LPCDSBUFFERDESC) &d,
						    &pB, NULL);
			printf("  CreateSoundBuffer LOCHARDWARE|CTRL3D hr=0x%08lX  %s\n",
			       (unsigned long) hr,
			       FAILED(hr) ? "REFUSED" : "granted");

			if (SUCCEEDED(hr) && pB)
			{
				ZeroMemory(&c, sizeof(c));
				c.dwSize = sizeof(c);

				if (SUCCEEDED(pB->GetCaps(&c)))
					printf("  GetCaps dwFlags 0x%08lX  LOCHARDWARE %s\n",
					       (unsigned long) c.dwFlags,
					       (c.dwFlags & DSBCAPS_LOCHARDWARE)
					       ? "SET" : "CLEAR");

				hr = pB->QueryInterface(IID_IKsPropertySet,
							(void **) &pKs);
				printf("  QueryInterface IKsPropertySet        hr=0x%08lX\n",
				       (unsigned long) hr);

				if (pKs)
					pKs->Release();

				pB->Release();
			}

			pDS->Release();
		}

		printf("\n");
		fflush(stdout);

		return (0);
	}

	STEP("CoInitialize");
	hr = CoInitialize(NULL);
	OKHR(hr);

	STEP("LoadLibrary");
	g_hDll = LoadLibraryA(pszDll);
	printf("%s\n", g_hDll ? "ok" : "FAILED");
	fflush(stdout);

	if (!g_hDll)
		return (1);

	STEP("GetProcAddress DllGetClassObject");
	*(FARPROC *) &pfnGetClassObject =
			GetProcAddress(g_hDll, "DllGetClassObject");
	printf("%s\n", pfnGetClassObject ? "ok" : "FAILED");
	fflush(stdout);

	if (!pfnGetClassObject)
		return (1);

	STEP("DllGetClassObject CLSID_A3dApi");
	pcf = NULL;
	hr = pfnGetClassObject(CLSID_A3dApi, IID_IClassFactory, (void **) &pcf);
	OKHR(hr);

	if (FAILED(hr))
		return (1);

	/* A3dCreate(): the engine asks for IID_IA3d3 and nothing later. */

	STEP("CreateInstance IID_IA3d3");
	pA3d = NULL;
	hr = pcf->CreateInstance(NULL, IID_IA3d3, (void **) &pA3d);
	OKHR(hr);

	if (FAILED(hr))
		return (1);

	/*
	 * A3dCreate()'s own last step, ia3dutil.cpp:385. The mask 127 (0x7F) is
	 * the engine's literal value and does not include A3D_DISABLE_SPLASHSCREEN
	 * (0x80), so the splash is asked for. `nosplash' adds the bit.
	 */

	if (!fNoInit)
	{
		DWORD	dwFeatures = fNoSplash
				? (g_dwFeatures | A3D_DISABLE_SPLASHSCREEN)
				: g_dwFeatures;

		printf("  Init features 0x%02lX%s\n",
		       (unsigned long) dwFeatures,
		       fNoSplash ? " (splash disabled)" : " (splash asked for)");
		fflush(stdout);

		STEP("IA3d3::Init");
		hr = pA3d->Init(NULL, dwFeatures, A3DRENDERPREFS_DEFAULT);
		OKHR(hr);
	}

	STEP("SetCooperativeLevel EXCLUSIVE");
	hr = pA3d->SetCooperativeLevel(hWnd, A3D_CL_EXCLUSIVE);
	OKHR(hr);

	STEP("SetResourceManagerMode UNMANAGED");
	hr = pA3d->SetResourceManagerMode(A3DSOURCE_TYPEUNMANAGED);
	OKHR(hr);

	/* CA3DRenderer::CreateSoundBuffer(), snd_a3d.cpp:723. */

	STEP("QueryInterface IID_IA3dSound");
	pSound = NULL;
	hr = pA3d->QueryInterface(IID_IA3dSound, (void **) &pSound);
	OKHR(hr);

	if (SUCCEEDED(hr) && pSound)
	{
		ZeroMemory(&wfx, sizeof(wfx));

		wfx.wFormatTag	    = WAVE_FORMAT_PCM;
		wfx.nChannels	    = 2;
		wfx.wBitsPerSample  = 16;
		wfx.nSamplesPerSec  = 22050;
		wfx.nBlockAlign	    = 4;
		wfx.nAvgBytesPerSec = 22050 * 4;

		ZeroMemory(&desc, sizeof(desc));

		desc.dwSize	   = sizeof(desc);
		desc.dwFlags	   = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_CTRLVOLUME;
		desc.dwBufferBytes = 65536;
		desc.lpwfxFormat   = &wfx;

		STEP("IA3dSound::CreateSoundBuffer");
		pPrimary = NULL;
		hr = pSound->CreateSoundBuffer((LPCDSBUFFERDESC) &desc,
					       &pPrimary, NULL);
		OKHR(hr);

		if (pPrimary)
			pPrimary->Release();

		pSound->Release();
	}

	STEP("IA3d3::Release");
	pA3d->Release();
	printf("ok\n");
	fflush(stdout);

	STEP("IClassFactory::Release");
	pcf->Release();
	printf("ok\n");
	fflush(stdout);

	printf("\na3d_goldsrc_init_probe: sequence completed\n");
	fflush(stdout);

	return (0);
}

#include "command_line.hpp"
int main(int argc, char** argv)
{
    try {
        a3dtools::CommandLine args(argc, argv, "dll timeout-ms features", "no-init no-splash pump desktop ds-caps");
        if (args.Has("help")) { puts(R"HELP(a3d_goldsrc_init_probe --dll <path> [--timeout-ms N] [--features N] [--no-init] [--no-splash] [--pump] [--desktop] [--ds-caps]
Reproduce GoldSrc A3D initialization and primary-buffer creation, logging each HRESULT.
Requires Windows x86, an A3D DLL, and an audio device. May create a probe window.
Exit: 0 sequence completed, 1 setup failure, 2 timeout or invalid arguments.
Legacy positional DLL and mode arguments remain supported.)HELP"); return 0; }

        if (!args.positional.empty()) return RunLegacy(argc, argv);
        if (!args.Has("dll")) throw std::runtime_error("--dll is required");
        probe_timeout_ms = args.Number("timeout-ms", 20000, 1, 600000);
        std::vector<std::string> legacy = {argv[0], args.Get("dll")};
        if (args.Has("features")) legacy.push_back("features=" + std::to_string(args.Number("features", 127, 0, 0xFFFFFFFFu)));
        const char* flags[][2] = {{"no-init", "noinit"}, {"no-splash", "nosplash"}, {"pump", "pump"}, {"desktop", "desktop"}, {"ds-caps", "dscaps"}};
        for (const auto& flag : flags) if (args.Has(flag[0])) legacy.push_back(flag[1]);
        return a3dtools::InvokeLegacy(RunLegacy, legacy);
    } catch (const std::exception& error) { fprintf(stderr, "%s\n", error.what()); return 2; }
}
