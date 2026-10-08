/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * a3d_sdk_loading_probe.cpp
 *
 * NOT PART OF THE ORIGINAL.  Project tooling.
 *
 * Replay the SDK loading sample through DllGetClassObject using the DLL
 * specified on the command line. Print and flush each step before calling
 * it so the last message identifies a faulting call.
 *
 * Use GetDesktopWindow for headless execution. The sample's title-based
 * GetConsoleHwnd lookup fails when hosted by Windows Terminal.
 *
 * Usage:
 *	a3d_sdk_loading_probe [path\to\a3dapi.dll] [media directory] [shutdown|occlusion]
 *
 *---------------------------------------------------------------------------
 */

#include <windows.h>
#include <objbase.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>

#include <initguid.h>
#include "ia3dapi.h"

static HMODULE		g_hDll;
static const char	*g_pszStep = "(none)";

/* Report the current step and fault RVA for lookup in the DLL map,
   then continue normal exception handling. */

static LONG CALLBACK
FaultFilter(EXCEPTION_POINTERS *ep)
{
	if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
	{
		void	*a = ep->ExceptionRecord->ExceptionAddress;
		LONG_PTR rva = (BYTE *) a - (BYTE *) g_hDll;

		printf("\n\n*** ACCESS VIOLATION in step: %s\n", g_pszStep);
		printf("*** address %p", a);

		if (rva >= 0 && rva < 0x400000)
			printf("   a3dapi.dll + 0x%08lX", (unsigned long) rva);
		else
			printf("   (outside the DLL under test)");

		printf("\n*** %s address %p\n",
		       ep->ExceptionRecord->NumberParameters >= 2 &&
		       ep->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
		       (void *) (ep->ExceptionRecord->NumberParameters >= 2
				 ? ep->ExceptionRecord->ExceptionInformation[1] : 0));

		fflush(stdout);
	}

	return (EXCEPTION_CONTINUE_SEARCH);
}

#define STEP(what)	do { g_pszStep = (what); \
			     printf("  %-46s ", g_pszStep); fflush(stdout); } while (0)

#define OKHR(hr)	do { printf("hr=0x%08lX %s\n", (unsigned long) (hr), \
				    FAILED(hr) ? "FAILED" : "ok"); fflush(stdout); } while (0)



static IA3dSource2 *
NewSource(IA3d5 *pA3d, DWORD dwType, const char *pszWhat)
{
IA3dSource2	*pSource = NULL;
HRESULT		hr;

	STEP(pszWhat);
	hr = pA3d->NewSource(dwType, &pSource);
	OKHR(hr);

	return (pSource);
}



static void
LoadFile(IA3dSource2 *pSource, const char *pszFile, DWORD dwFormat,
	 const char *pszWhat)
{
HRESULT	hr;

	STEP(pszWhat);

	if (!pSource)
	{
		printf("skipped, no source\n");
		fflush(stdout);

		return;
	}

	hr = pSource->LoadFile((char *) pszFile, dwFormat);
	OKHR(hr);
}

/* Compare settled polygon-sample readings with the wall blocking the
   source, moved aside and absent. */

static void
Frame(IA3d5 *pA3d, IA3dGeom2 *pGeom, IA3dSource2 *pSource, A3DVAL fWallX,
      int fWall)
{
	pA3d->Clear();
	pGeom->LoadIdentity();

	pGeom->PushMatrix();
	pGeom->Translate3f(0.0f, 0.0f, 0.0f);
	pGeom->BindListener();
	pGeom->PopMatrix();

	pGeom->PushMatrix();
	pGeom->Translate3f(0.0f, 0.0f, -5.0f);
	pGeom->BindSource((LPA3DSOURCE2) pSource);
	pGeom->PopMatrix();

	/* A quad across the line of sight, two units in front of the listener. */

	if (!fWall)
	{
		pA3d->Flush();

		return;
	}

	pGeom->PushMatrix();
	pGeom->Translate3f(fWallX, 0.0f, -2.0f);
	pGeom->Begin(A3D_QUADS);
	pGeom->Vertex3f(-2.0f, -2.0f, 0.0f);
	pGeom->Vertex3f( 2.0f, -2.0f, 0.0f);
	pGeom->Vertex3f( 2.0f,  2.0f, 0.0f);
	pGeom->Vertex3f(-2.0f,  2.0f, 0.0f);
	pGeom->End();
	pGeom->PopMatrix();

	pA3d->Flush();
}

/*
 * One frame's readings on stderr, diffable against runs of each DLL.
 * Off unless A3DFRAMETRACE is set.
 */

static void
FrameTrace(const char *pszPhase, int iFrame, A3DVAL fOcc, A3DVAL fAud)
{
	if (!GetEnvironmentVariableA("A3DFRAMETRACE", NULL, 0))
		return;

	fprintf(stderr, "%s %3d occ=%.6f aud=%.6f\n", pszPhase, iFrame,
		fOcc, fAud);
}

/*
 * Frame until both readings stop moving. Fixed frame counts catch different
 * points of the gain ramp on each run; settling first measures the same state
 * on both DLLs.
 */

#define SETTLE_STABLE	20	/* unchanged frames that count as settled */
#define SETTLE_MAX	400	/* give up rather than frame for ever */

static int
Settle(IA3d5 *pA3d, IA3dGeom2 *pGeom, IA3dSource2 *pSource, A3DVAL fWallX,
       int fWall, const char *pszPhase, A3DVAL *pfOcc, A3DVAL *pfAud)
{
A3DVAL	fOcc = -1.0f, fAud = -1.0f;
A3DVAL	fOccLast = -2.0f, fAudLast = -2.0f;
int	cStable = 0;
int	i;

	for (i = 0; i < SETTLE_MAX; i++)
	{
		Frame(pA3d, pGeom, pSource, fWallX, fWall);
		Sleep(16);

		pSource->GetOcclusionFactor(&fOcc);
		pSource->GetAudibility(&fAud);

		FrameTrace(pszPhase, i, fOcc, fAud);

		if (fOcc == fOccLast && fAud == fAudLast)
		{
			if (++cStable >= SETTLE_STABLE)
				break;
		}
		else
		{
			cStable  = 0;
			fOccLast = fOcc;
			fAudLast = fAud;
		}
	}

	*pfOcc = fOcc;
	*pfAud = fAud;

	return (i);
}

static int
Occlusion(IA3d5 *pA3d)
{
IA3dGeom2	*pGeom = NULL;
IA3dSource2	*pSource = NULL;
A3DVAL		fOccBlocked, fOccClear, fOccNone;
A3DVAL		fAudBlocked, fAudClear, fAudNone;
int		cwBlocked, cwClear, cwNone;
DWORD		dwMode;
HRESULT		hr;

	STEP("QueryInterface IID_IA3dGeom2");
	hr = pA3d->QueryInterface(IID_IA3dGeom2, (void **) &pGeom);
	OKHR(hr);

	if (FAILED(hr))
		return (1);

	STEP("IA3d5::IsFeatureAvailable A3D_OCCLUSIONS");
	hr = pA3d->IsFeatureAvailable(A3D_OCCLUSIONS);
	printf("hr=0x%08lX %s\n", (unsigned long) hr,
	       (hr == S_OK) ? "available" : "NOT available");
	fflush(stdout);

	STEP("IA3dGeom2::Enable A3D_OCCLUSIONS");
	hr = pGeom->Enable(A3D_OCCLUSIONS);
	OKHR(hr);

	dwMode = 0;
	STEP("IA3dGeom2::GetOcclusionMode");
	hr = pGeom->GetOcclusionMode(&dwMode);
	printf("hr=0x%08lX  mode=%lu\n", (unsigned long) hr,
	       (unsigned long) dwMode);
	fflush(stdout);

	/* Trace geometry every frame. */

	STEP("IA3dGeom2::SetOcclusionUpdateInterval 1");
	hr = pGeom->SetOcclusionUpdateInterval(1);
	OKHR(hr);

	STEP("IA3dGeom2::SetOcclusionMode A3D_OCCLUSIONS");
	hr = pGeom->SetOcclusionMode(A3D_OCCLUSIONS);
	OKHR(hr);

	pSource = NewSource(pA3d, A3DSOURCE_INITIAL_RENDERMODE_A3D,
			    "IA3d5::NewSource A3D");

	if (!pSource)
		return (1);

	LoadFile(pSource, "heli.wav", A3DSOURCE_FORMAT_WAVE,
		 "LoadFile heli.wav");

	STEP("IA3dSource2::Play looped");
	hr = pSource->Play(A3D_LOOPED);
	OKHR(hr);

	/* Pace frames like the sample's message loop; see Settle() for why pacing matters. */

	cwBlocked = Settle(pA3d, pGeom, pSource, 0.0f, 1, "wall",
			   &fOccBlocked, &fAudBlocked);
	cwClear   = Settle(pA3d, pGeom, pSource, 40.0f, 1, "aside",
			   &fOccClear, &fAudClear);
	cwNone    = Settle(pA3d, pGeom, pSource, 0.0f, 0, "none",
			   &fOccNone, &fAudNone);

	pSource->Stop();

	printf("\n  wall in the way   occlusion=%.6f  audibility=%.6f  "
	       "(%d frames)\n", fOccBlocked, fAudBlocked, cwBlocked);
	printf("  wall moved aside  occlusion=%.6f  audibility=%.6f  "
	       "(%d frames)\n", fOccClear, fAudClear, cwClear);
	printf("  no wall at all    occlusion=%.6f  audibility=%.6f  "
	       "(%d frames)\n", fOccNone, fAudNone, cwNone);
	printf("\n  %s\n",
	       (fOccBlocked != fOccClear || fAudBlocked != fAudClear)
	       ? "OCCLUDES: the wall changed the reading"
	       : "DOES NOT OCCLUDE: the wall changed nothing");
	fflush(stdout);

	return (0);
}

int
RunLegacy(int argc, char **argv)
{
const char		*pszDll;
const char		*pszDir;
HMODULE			hDll;
LPFNGETCLASSOBJECT	pfnGetClassObject;
IClassFactory		*pcf;
IA3d5			*pA3d;
IA3dListener		*pListener;
IA3dSource2		*pSrcWav;
IA3dSource2		*pSrcWavStream;
IA3dSource2		*pSrcMP3;
IA3dSource2		*pSrcMp3Stream;
IA3dSource2		*pSrcAc3Stream;
DWORD			dwBufferLength;
DWORD			dwThreadPriority;
HRESULT			hr;
int			fOcclusion;

	pszDll = (argc > 1) ? argv[1] : "a3dapi.dll";
	pszDir = (argc > 2) ? argv[2] : NULL;

	fOcclusion = (argc > 3 && !strcmp(argv[3], "occlusion"));

	if (pszDir)
		SetCurrentDirectoryA(pszDir);

	printf("a3d_sdk_loading_probe: the `loading' sample's setup, headless.\n");
	printf("dll: %s\n\n", pszDll);
	fflush(stdout);

	/* Keep a fault from raising a modal box on an unattended run. */

	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
		     SEM_NOOPENFILEERRORBOX);

	/*
	 * Timer resolution 1ms for occlusion mode. CA3dRoot::Flush computes
	 * frame time as (timeGetTime() - last) * 0.001f (dbg:0x1000AC60, rtl:0x10002BA0).
	 * At default ~15.6ms resolution, two flushes in one tick give 0 frame time,
	 * which makes A3dTraceApply's occlusion ramp stop (dbg:0x10022A00, rtl:0x1000F180).
	 * Empirically: one in ~12 occlusion runs froze at the occluded gain value.
	 */

	timeBeginPeriod(1);

	CoInitialize(NULL);

	AddVectoredExceptionHandler(1, FaultFilter);

	STEP("LoadLibrary");
	hDll = LoadLibraryA(pszDll);
	g_hDll = hDll;
	printf("%s\n", hDll ? "ok" : "FAILED");
	fflush(stdout);

	if (!hDll)
		return (1);

	STEP("GetProcAddress DllGetClassObject");
	pfnGetClassObject = (LPFNGETCLASSOBJECT)
			    GetProcAddress(hDll, "DllGetClassObject");
	printf("%s\n", pfnGetClassObject ? "ok" : "FAILED");
	fflush(stdout);

	if (!pfnGetClassObject)
		return (1);

	STEP("DllGetClassObject CLSID_A3dApi");
	hr = pfnGetClassObject(CLSID_A3dApi, IID_IClassFactory, (void **) &pcf);
	OKHR(hr);

	if (FAILED(hr))
		return (1);

	STEP("IClassFactory::CreateInstance IID_IA3d5");
	hr = pcf->CreateInstance(NULL, IID_IA3d5, (void **) &pA3d);
	OKHR(hr);

	if (FAILED(hr))
		return (1);

	/* `shutdown' tests Shutdown on a root created but never initialised. */

	if (argc > 3 && !strcmp(argv[3], "shutdown"))
	{
		STEP("IA3d5::Shutdown (InitEx never ran)");
		hr = pA3d->Shutdown();
		OKHR(hr);

		printf("\nreached the end of the uninitialised teardown.\n");
		fflush(stdout);

		return (0);
	}

	/* Ask for no features to avoid the reverb path that does not return on this machine. */

	STEP("IA3d5::InitEx (desktop window)");
	hr = pA3d->InitEx(NULL, fOcclusion ? A3D_OCCLUSIONS : 0,
			  A3DRENDERPREFS_DEFAULT,
			  GetDesktopWindow(), A3D_CL_NORMAL);
	OKHR(hr);

	if (FAILED(hr))
		return (1);

	/*
	 * `occlusion' tests the polygon sample's occlusion: listener at origin,
	 * source behind a quad. That sample occludes on Aureal's DLL but did not here.
	 */

	if (fOcclusion)
		return (Occlusion(pA3d));

	STEP("QueryInterface IID_IA3dListener");
	hr = pA3d->QueryInterface(IID_IA3dListener, (void **) &pListener);
	OKHR(hr);

	dwBufferLength   = 0;
	dwThreadPriority = 0;

	STEP("IA3d5::GetStreamingProperties");
	hr = pA3d->GetStreamingProperties(&dwBufferLength, &dwThreadPriority);
	OKHR(hr);
	printf("  %-46s %lu ms, priority %lu\n", "  ->",
	       (unsigned long) dwBufferLength, (unsigned long) dwThreadPriority);
	fflush(stdout);

	STEP("IA3d5::SetStreamingProperties (HIGH)");
	hr = pA3d->SetStreamingProperties(dwBufferLength,
					  A3D_STREAMING_PRIORITY_HIGH);
	OKHR(hr);

	/* The five sources, in the sample's order and with its render modes. */

	pSrcWav       = NewSource(pA3d, A3DSOURCE_INITIAL_RENDERMODE_NATIVE,
				  "IA3d5::NewSource NATIVE  (wav)");
	pSrcWavStream = NewSource(pA3d, A3DSOURCE_INITIAL_RENDERMODE_A3D,
				  "IA3d5::NewSource A3D     (wav stream)");
	pSrcMP3       = NewSource(pA3d, A3DSOURCE_INITIAL_RENDERMODE_NATIVE,
				  "IA3d5::NewSource NATIVE  (mp3)");
	pSrcMp3Stream = NewSource(pA3d, A3DSOURCE_INITIAL_RENDERMODE_NATIVE,
				  "IA3d5::NewSource NATIVE  (mp3 stream)");
	pSrcAc3Stream = NewSource(pA3d, A3DSOURCE_INITIAL_RENDERMODE_NATIVE,
				  "IA3d5::NewSource NATIVE  (ac3 stream)");



	LoadFile(pSrcWav, "linn48.wav", A3DSOURCE_FORMAT_WAVE,
		 "LoadFile linn48.wav");
	LoadFile(pSrcWavStream, "heli.wav",
		 A3DSOURCE_FORMAT_WAVE | A3DSOURCE_STREAMING,
		 "LoadFile heli.wav (streaming)");
	LoadFile(pSrcMP3, "Scooter.mp3", A3DSOURCE_FORMAT_MP3,
		 "LoadFile Scooter.mp3");
	LoadFile(pSrcMp3Stream, "Scooter.mp3",
		 A3DSOURCE_FORMAT_MP3 | A3DSOURCE_STREAMING,
		 "LoadFile Scooter.mp3 (streaming)");
	LoadFile(pSrcAc3Stream, "spoken_chan1.ac3",
		 A3DSOURCE_FORMAT_AC3 | A3DSOURCE_STREAMING,
		 "LoadFile spoken_chan1.ac3 (streaming)");

	printf("\nreached the end of the sample's setup.\n");
	fflush(stdout);

	return (0);
}

#include "command_line.hpp"
#include <filesystem>
int main(int argc, char** argv)
{
    try {
        a3dtools::CommandLine args(argc, argv, "dll media-dir scenario", "");
        if (args.Has("help")) { puts(R"HELP(a3d_sdk_loading_probe --dll <path> --media-dir <directory> [--scenario loading|shutdown|occlusion]
Reproduce SDK sample wave loading, resource cleanup, or occlusion behavior.
Requires Windows x86, an A3D DLL, SDK sample media, and an audio device. Produces audio.
Exit: 0 sequence completed, 1 runtime failure, 2 invalid arguments.
Legacy positional DLL, media directory, and mode arguments remain supported.)HELP"); return 0; }

        if (!args.positional.empty()) return RunLegacy(argc, argv);
        if (!args.Has("dll") || !args.Has("media-dir")) throw std::runtime_error("--dll and --media-dir are required");
        const auto scenario = args.Get("scenario", "loading");
        if (scenario != "loading" && scenario != "shutdown" && scenario != "occlusion") throw std::runtime_error("Unknown loading scenario");
        // RunLegacy changes the working directory before loading the DLL.
        const auto dll = std::filesystem::absolute(args.Get("dll"));
        const auto media = std::filesystem::absolute(args.Get("media-dir"));
        if (!std::filesystem::is_directory(media)) throw std::runtime_error("Media directory does not exist");
        return a3dtools::InvokeLegacy(RunLegacy, {argv[0], dll.string(), media.string(), scenario});
    } catch (const std::exception& error) { fprintf(stderr, "%s\n", error.what()); return 2; }
}
