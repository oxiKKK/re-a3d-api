/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * a3d_software_playback_probe.cpp
 *
 * NOT PART OF THE ORIGINAL.  Project tooling.
 *
 * Exercise the volsrc software playback path without the DirectX 6 sample
 * framework. Load the selected DLL through DllGetClassObject, create and
 * initialize IA3d5, then load and play a wave source. No COM registration
 * is required. Print each step's HRESULT to identify failures.
 *
 *---------------------------------------------------------------------------
 */

#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <string.h>

#include <initguid.h>
#include "ia3dapi.h"

static const char *StepName;
static HMODULE	g_hDll;

/* Print fault address as a3dapi.dll RVA (maps against the .map). */
static LONG CALLBACK
FaultFilter(EXCEPTION_POINTERS *ep)
{
	if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
	{
		void *a = ep->ExceptionRecord->ExceptionAddress;

		printf("\n*** FAULT at %p  a3dapi RVA=0x%08lX ***\n",
		       a, (unsigned long) ((BYTE *) a - (BYTE *) g_hDll));
		fflush(stdout);
	}

	return (EXCEPTION_CONTINUE_SEARCH);
}

#define STEP(what)	do { StepName = (what); printf("  %-40s ", StepName); fflush(stdout); } while (0)
#define OKHR(hr)	do { printf("hr=0x%08lX %s\n", (unsigned long) (hr), FAILED(hr) ? "FAILED" : "ok"); } while (0)

int
RunLegacy(int argc, char **argv)
{
HMODULE			hDll;
LPFNGETCLASSOBJECT	pfnGetClassObject;
IClassFactory		*pcf;
IA3d5			*pA3d;
IA3dSource2		*pSource;
HRESULT			hr;
const char		*pszWave;

	pszWave = (argc > 1) ? argv[1] : "samples\\data\\heli.wav";

	printf("M1 probe: software route, no DAL registered.\n");
	printf("wave: %s\n\n", pszWave);

	/* Keep a fault from raising a modal box on an unattended run. */
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
		     SEM_NOOPENFILEERRORBOX);

	CoInitialize(NULL);

	AddVectoredExceptionHandler(1, FaultFilter);

	STEP("LoadLibrary a3dapi.dll");
	hDll = LoadLibraryA(argc > 2 ? argv[2] : "a3dapi.dll");
	g_hDll = hDll;
	printf("%s\n", hDll ? "ok" : "FAILED");
	if (!hDll)
		return (1);

	STEP("GetProcAddress DllGetClassObject");
	pfnGetClassObject = (LPFNGETCLASSOBJECT)
			    GetProcAddress(hDll, "DllGetClassObject");
	printf("%s\n", pfnGetClassObject ? "ok" : "FAILED");
	if (!pfnGetClassObject)
		return (1);

	STEP("DllGetClassObject CLSID_A3dApi");
	hr = pfnGetClassObject(CLSID_A3dApi, IID_IClassFactory, (void **) &pcf);
	OKHR(hr);
	if (FAILED(hr))
		return (1);

	STEP("CreateInstance IID_IA3d5");
	hr = pcf->CreateInstance(NULL, IID_IA3d5, (void **) &pA3d);
	OKHR(hr);
	if (FAILED(hr))
		return (1);

	/* InitEx is Init plus SetCooperativeLevel in one; sources need a window's cooperative level. */
	STEP("IA3d5::InitEx (desktop window)");
	hr = pA3d->InitEx(NULL, A3D_1ST_REFLECTIONS, A3D_DIRECT_PATH_A3D,
			  GetDesktopWindow(), A3D_CL_NORMAL);
	OKHR(hr);

	/* Compat(1000, 1) enables rendering; sources do not play until it is on. */
	STEP("IA3d5::Compat(1000, 1) enable render");
	hr = pA3d->Compat(1000, 1);
	OKHR(hr);

	STEP("IA3d5::SetOutputGain 1.0");
	hr = pA3d->SetOutputGain(1.0f);
	OKHR(hr);

	STEP("IA3d5::NewSource");
	pSource = NULL;
	hr = pA3d->NewSource(A3DSOURCE_TYPEDEFAULT, &pSource);
	OKHR(hr);
	if (FAILED(hr) || !pSource)
	{
		printf("\nNewSource did not return a source; stopping.\n");
		return (1);
	}

	/* Pick format from the file extension to test MP3 and AC-3 decode paths. */
	{
	const char	*pszExt;
	DWORD		dwFormat;
	const char	*pszFmt;

		pszExt = strrchr(pszWave, '.');
		if (pszExt && (!_stricmp(pszExt, ".mp3")))
		{
			dwFormat = A3DSOURCE_FORMAT_MP3;
			pszFmt = "MP3";
		}
		else if (pszExt && (!_stricmp(pszExt, ".ac3")))
		{
			dwFormat = A3DSOURCE_FORMAT_AC3;
			pszFmt = "AC3";
		}
		else
		{
			dwFormat = A3DSOURCE_FORMAT_WAVE;
			pszFmt = "WAVE";
		}

		printf("  %-40s %s\n", "IA3dSource2::LoadFile format", pszFmt);
		STEP("IA3dSource2::LoadFile");
		hr = pSource->LoadFile((char *) pszWave, dwFormat);
		OKHR(hr);
	}

	STEP("IA3dSource2::SetGain 1.0");
	hr = pSource->SetGain(1.0f);
	OKHR(hr);

	STEP("IA3dSource2::Play A3D_LOOPED");
	hr = pSource->Play(A3D_LOOPED);
	OKHR(hr);

	/* Source loops; play for N seconds (arg 2, default 10) or loop until Ctrl+C if N <= 0. */
	{
	int	nSeconds = (argc > 2) ? atoi(argv[2]) : 10;

		if (nSeconds <= 0)
		{
			printf("\nlooping - press Ctrl+C to stop...\n");

			for (;;)
				Sleep(1000);
		}

		printf("\nlooping for %d seconds (Ctrl+C to stop early)...\n",
		       nSeconds);
		Sleep((DWORD) nSeconds * 1000);
	}

	STEP("IA3dSource2::Stop");
	hr = pSource->Stop();
	OKHR(hr);

	pSource->Release();
	pA3d->Release();
	pcf->Release();

	printf("\nM1 probe reached the end of the play path.\n");

	return (0);
}

#include "command_line.hpp"
int main(int argc, char** argv)
{
    try {
        a3dtools::CommandLine args(argc, argv, "dll wave", "");
        if (args.Has("help")) { puts(R"HELP(a3d_software_playback_probe --dll <path> --wave <path>
Create an A3D source, load a wave file, and exercise software playback.
Requires Windows x86, an A3D 3.0 DLL, and an audio device. Produces audio.
Exit: 0 sequence completed, 1 runtime failure, 2 invalid arguments.
Legacy positional wave input remains supported. Use a3d_pcm_capture to verify PCM output.)HELP"); return 0; }

        if (!args.positional.empty()) return RunLegacy(argc, argv);
        if (!args.Has("dll") || !args.Has("wave")) throw std::runtime_error("--dll and --wave are required");
        return a3dtools::InvokeLegacy(RunLegacy, {argv[0], args.Get("wave"), args.Get("dll")});
    } catch (const std::exception& error) { fprintf(stderr, "%s\n", error.what()); return 2; }
}
