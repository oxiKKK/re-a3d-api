// Project-added A3D development tooling.
#include <initguid.h>
#include "capture_internal.hpp"
#include <cstdio>
#include <cmath>
#include <cstring>

#include <psapi.h>
#include "com_server_isolation.hpp"

namespace a3dcapture {
CaptureState capture_state = {};
static const char *StepName;

static LONG WINAPI
ReportFault(EXCEPTION_POINTERS *pep)
{
HMODULE	hMod;
char	szPath[MAX_PATH];
void	*pvAddr;

	RecordCaptureFault(pep->ExceptionRecord->ExceptionCode);
	pvAddr = (void *) pep->ExceptionRecord->ExceptionAddress;
	szPath[0] = '\0';

	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
			       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			       (LPCSTR) pvAddr, &hMod))
		GetModuleFileNameA(hMod, szPath, sizeof szPath);

	printf("\nAUDCAP_FAULT step=\"%s\" code=0x%08lX addr=0x%p rva=0x%lX module=%s\n",
	       StepName ? StepName : "(none)",
	       (unsigned long) pep->ExceptionRecord->ExceptionCode,
	       pvAddr,
	       (unsigned long) ((BYTE *) pvAddr - (BYTE *) hMod),
	       szPath[0] ? szPath : "(unknown)");
	fflush(stdout);

	return (EXCEPTION_EXECUTE_HANDLER);
}

#define STEP(what)	do { StepName = (what); printf("  %-40s ", StepName); fflush(stdout); } while (0)
#define OKHR(hr)	do { printf("hr=0x%08lX %s\n", (unsigned long) (hr), FAILED(hr) ? "FAILED" : "ok"); } while (0)

int
RunCapture(int argc, char **argv)
{
HMODULE			hDll;
LPFNGETCLASSOBJECT	pfnGetClassObject;
IClassFactory		*pcf;
IA3d5			*pA3d;
IA3dSource2		*pSource;
HRESULT			hr;
DWORD			dwCookie;
DWORD			nCaptureMs;
const char		*pszWave;
const char		*pszDll;
const char		*pszWavPrefix;
int			nScene;
const CaptureScene		*pScene;
a3dtest::clsid_isolation iso;
int			fReverb;
IA3dReverb		*pReverb;
int			fReflect;
int			fManual;
IA3dGeom2		*pGeom;
IA3dMaterial		*pMaterial;

	pszWave = (argc > 1) ? argv[1] : "samples\\data\\heli.wav";
	pszDll = (argc > 2) ? argv[2] : "a3dapi.dll";
	nCaptureMs = (argc > 3 && atoi(argv[3]) > 0) ? (DWORD) atoi(argv[3]) : 500;
	pszWavPrefix = (argc > 4 && argv[4][0]) ? argv[4] : NULL;
	nScene = (argc > 5) ? atoi(argv[5]) : 0;

	/* Optional effect selected by the legacy positional interface. */
	fReverb = (argc > 6 && !strcmp(argv[6], "reverb"));
	fReflect = (argc > 6 && !strcmp(argv[6], "reflect"));
	fManual = (argc > 6 && !strcmp(argv[6], "manual"));

	if (nScene < 0 || nScene >= CAPTURE_SCENE_COUNT)
	{
		printf("a3d_pcm_capture: scene %d is out of range (0..%d)\n",
		       nScene, CAPTURE_SCENE_COUNT - 1);
		return (1);
	}

	pScene = &capture_scenes[nScene];

	printf("a3d_pcm_capture: recording software-mixer PCM.\n");
	printf("wave:  %s\n", pszWave);
	printf("dll:   %s\n", pszDll);
	printf("scene: %d (%s)\n\n", nScene, pScene->pszName);

	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
		     SEM_NOOPENFILEERRORBOX);

	SetUnhandledExceptionFilter(ReportFault);

	InitializeCriticalSection(&capture_state.lock);

	CoInitialize(NULL);

	STEP("CoRegisterClassObject CLSID_DirectSound");
	dwCookie = 0;
	hr = CoRegisterClassObject(CLSID_DirectSound, RecordingClassFactory(),
				   CLSCTX_INPROC_SERVER, REGCLS_MULTIPLEUSE,
				   &dwCookie);
	OKHR(hr);
	if (FAILED(hr))
		return (1);

	STEP("CoRegisterClassObject deny CLSID_A3d, CLSID_A3dDal");
	printf("%s\n", (iso.deny(CLSID_A3d) && iso.deny(CLSID_A3dDal)) ?
	       "ok" : "FAILED");

	STEP("LoadLibrary");
	hDll = LoadLibraryA(pszDll);
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

	const bool q3 = argc > 7 && !strcmp(argv[7], "q3");
	if (q3)
	{
		// quake3.exe 1.11 demo: 0x448A00 activation, 0x4458F0 hardware gate.
		IA3d4 *api = NULL;
		STEP("Q3 CreateInstance IID_IA3d4");
		hr = pcf->CreateInstance(NULL, IID_IA3d4, (void **) &api);
		OKHR(hr);
		if (FAILED(hr)) return 1;
		STEP("Q3 Init(NULL, 0x42, 0)");
		hr = api->Init(NULL, A3D_1ST_REFLECTIONS | A3D_OCCLUSIONS, 0);
		OKHR(hr);
		if (FAILED(hr)) return 1;
		STEP("Q3 SetCooperativeLevel(NORMAL)");
		hr = api->SetCooperativeLevel(GetDesktopWindow(), A3D_CL_NORMAL);
		OKHR(hr);
		if (FAILED(hr)) return 1;
		A3DCAPS_HARDWARE hardware = {};
		hardware.dwSize = sizeof(hardware);
		STEP("Q3 GetHardwareCaps");
		hr = api->GetHardwareCaps(&hardware);
		OKHR(hr);
		printf("Q3 hardware flags=0x%lX voices=%lu rate=%lu..%lu channels=%lu\n",
			hardware.dwFlags, hardware.dwMax3DBuffers, hardware.dwMinSampleRate,
			hardware.dwMaxSampleRate, hardware.dwOutputChannels);
		if (FAILED(hr) || !(hardware.dwFlags & 0x28)) return 1;
		if (api->IsFeatureAvailable(A3D_DIRECT_PATH_A3D) != TRUE ||
			api->IsFeatureAvailable(A3D_OCCLUSIONS) != TRUE) return 1;
		A3DCAPS_SOFTWARE software = {};
		software.dwSize = sizeof(software);
		if (FAILED(api->GetSoftwareCaps(&software))) return 1;
		if (hardware.dwFlags != software.dwFlags ||
			hardware.dwMax3DBuffers != software.dwMax3DBuffers ||
			hardware.dwMax2DBuffers != software.dwMax2DBuffers ||
			hardware.dwMinSampleRate != software.dwMinSampleRate ||
			hardware.dwMaxSampleRate != software.dwMaxSampleRate ||
			hardware.dwOutputChannels != software.dwOutputChannels ||
			!hardware.dwMax3DBuffers) return 1;
		// The existing PCM capture uses IA3d5 after the game's startup sequence.
		hr = api->QueryInterface(IID_IA3d5, (void **) &pA3d);
		api->Release();
		if (FAILED(hr)) return 1;
		DWORD limit = 0;
		if (FAILED(pA3d->GetMaxHardwareSources(&limit)) ||
			limit != hardware.dwMax3DBuffers) return 1;
		if (FAILED(pA3d->SetMaxHardwareSources(limit / 2))) return 1;
		DWORD reduced = 0;
		if (FAILED(pA3d->GetMaxHardwareSources(&reduced)) || reduced != limit / 2)
			return 1;
		if (FAILED(pA3d->SetMaxHardwareSources(limit))) return 1;
		printf("Q3 capability contract and source limit round-trip passed\n");
		// Exercise restart after the mixer has stopped its empty output buffer.
		Sleep(100);
	}
	else
	{
		STEP("CreateInstance IID_IA3d5");
		hr = pcf->CreateInstance(NULL, IID_IA3d5, (void **) &pA3d);
	}
	OKHR(hr);
	if (FAILED(hr))
		return (1);

	pReverb = NULL;

	if (!q3)
	{
		STEP("IA3d5::InitEx (desktop window)");
		hr = pA3d->InitEx(NULL,
				  A3D_1ST_REFLECTIONS | (fReverb ? A3D_REVERB : 0),
				  A3D_DIRECT_PATH_A3D, GetDesktopWindow(),
				  A3D_CL_NORMAL);
		OKHR(hr);
	}
	if (FAILED(hr)) return 1;

	if (fReverb)
	{
		STEP("IA3d5::NewReverb");
		hr = pA3d->NewReverb(&pReverb);
		OKHR(hr);

		if (SUCCEEDED(hr) && pReverb)
		{
			STEP("IA3dReverb::SetReverbPreset(CAVE)");
			hr = pReverb->SetReverbPreset(A3DREVERB_PRESET_CAVE);
			OKHR(hr);

			STEP("IA3d5::BindReverb");
			hr = pA3d->BindReverb(pReverb);
			OKHR(hr);
		}
	}

	/* Enable rendering: the source play path marks a source pending unless
	   rendering is on, and it is off until Compat code 1000 turns it on. */
	if (!q3)
	{
		STEP("IA3d5::Compat(1000, 1) enable render");
		hr = pA3d->Compat(1000, 1);
		OKHR(hr);
	}

	STEP("IA3d5::SetOutputGain");
	hr = pA3d->SetOutputGain(pScene->fOutputGain);
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

	STEP("IA3dSource2::LoadFile WAVE");
	hr = pSource->LoadFile((char *) pszWave, A3DSOURCE_FORMAT_WAVE);
	OKHR(hr);

	STEP("IA3dSource2::SetGain");
	hr = pSource->SetGain(pScene->fGain);
	OKHR(hr);

	if (fReverb)
	{
		STEP("IA3dSource2::SetReverbMix");
		hr = pSource->SetReverbMix(1.0f, 0.0f);
		OKHR(hr);
	}

	if (fReflect)
	{
		/* CAPTURE_KEEP_RENDER_MODE (scene 0) never calls SetRenderMode below,
		   so nothing turns the source's own reflections bit on. */
		STEP("IA3dSource2::SetRenderMode(A3DSOURCE_RENDERMODE_DEFAULT)");
		hr = pSource->SetRenderMode(A3DSOURCE_RENDERMODE_DEFAULT);
		OKHR(hr);
	}

	if (pScene->dwRenderMode != CAPTURE_KEEP_RENDER_MODE)
	{
		STEP("IA3dSource2::SetRenderMode");
		hr = pSource->SetRenderMode(pScene->dwRenderMode);
		OKHR(hr);
	}

	if (pScene->fPosition)
	{
		STEP("IA3dSource2::SetPosition3f");
		hr = pSource->SetPosition3f(pScene->afPos[0], pScene->afPos[1],
					    pScene->afPos[2]);
		OKHR(hr);
	}

	if (pScene->fPitch > 0.0f)
	{
		STEP("IA3dSource2::SetPitch");
		hr = pSource->SetPitch(pScene->fPitch);
		OKHR(hr);
	}

	if (pScene->fPan)
	{
		A3DVAL afPan[2];

		afPan[0] = pScene->afPan[0];
		afPan[1] = pScene->afPan[1];

		STEP("IA3dSource2::SetPanValues");
		hr = pSource->SetPanValues(2, afPan);
		OKHR(hr);
	}

	if (pScene->afVelocity[0] != 0.0f || pScene->afVelocity[1] != 0.0f ||
	    pScene->afVelocity[2] != 0.0f)
	{
		STEP("IA3dSource2::SetVelocity3f");
		hr = pSource->SetVelocity3f(pScene->afVelocity[0],
					    pScene->afVelocity[1],
					    pScene->afVelocity[2]);
		OKHR(hr);
	}

	if (pScene->fEq > 0.0f)
	{
		STEP("IA3dSource2::SetEq");
		hr = pSource->SetEq(pScene->fEq);
		OKHR(hr);
	}

	if (pScene->afMinMax[1] > 0.0f)
	{
		STEP("IA3dSource2::SetMinMaxDistance");
		hr = pSource->SetMinMaxDistance(pScene->afMinMax[0],
						pScene->afMinMax[1], A3D_AUDIBLE);
		OKHR(hr);
	}

	if (fManual)
	{
		static const A3DVAL afManual[2][6] = {
			{ 0.020f, 0.50f, -3.0f, 0.0f, -4.0f, 0.50f },
			{ 0.045f, 0.30f,  2.0f, 1.0f,  3.0f, 0.80f },
		};
		int i;

		for (i = 0; i < 2; i++)
		{
			IA3dReflection *pRefl = NULL;

			STEP("IA3dSource2::NewManualReflection");
			hr = pSource->NewManualReflection(&pRefl);
			OKHR(hr);

			if (SUCCEEDED(hr) && pRefl)
			{
				pRefl->SetDelay(afManual[i][0]);
				pRefl->SetGainScale(afManual[i][1]);
				pRefl->SetPosition3f(afManual[i][2], afManual[i][3],
						     afManual[i][4]);
				pRefl->SetEQ(afManual[i][5]);
				pRefl->Release();
			}
		}
	}

	if (pScene->fListener)
	{
		IA3dListener *pListener = NULL;

		STEP("QueryInterface(IID_IA3dListener)");
		hr = pA3d->QueryInterface(IID_IA3dListener, (void **) &pListener);
		OKHR(hr);

		STEP("IA3dListener::SetPosition3f");
		hr = pListener->SetPosition3f(pScene->afListenerPos[0],
					      pScene->afListenerPos[1],
					      pScene->afListenerPos[2]);
		OKHR(hr);

		STEP("IA3dListener::SetOrientationAngles3f");
		hr = pListener->SetOrientationAngles3f(pScene->fListenerYaw, 0.0f, 0.0f);
		OKHR(hr);

		pListener->Release();
	}

	pGeom = NULL;
	pMaterial = NULL;

	if (fReflect)
	{
	/* Use the closed box from the SceneRooms reflection case, enclosing
	   both source and listener. */
	float	afListenerPos[3] = { 0.0f, 0.0f, 3.0f };
	float	afSourcePos[3]   = { 0.0f, 0.0f, -3.0f };
	float	x = 6.0f, y = 4.0f, z = 6.0f, floorY = -2.0f;
	float	afFaces[6][4][3] = {
		{ { -x, floorY, -z }, {  x, floorY, -z }, {  x, y, -z }, { -x, y, -z } },
		{ {  x, floorY,  z }, { -x, floorY,  z }, { -x, y,  z }, {  x, y,  z } },
		{ { -x, floorY,  z }, { -x, floorY, -z }, { -x, y, -z }, { -x, y,  z } },
		{ {  x, floorY, -z }, {  x, floorY,  z }, {  x, y,  z }, {  x, y, -z } },
		{ { -x, floorY,  z }, {  x, floorY,  z }, {  x, floorY, -z }, { -x, floorY, -z } },
		{ { -x, y, -z }, {  x, y, -z }, {  x, y,  z }, { -x, y,  z } }
	};
	int	i, j;

		STEP("QueryInterface(IID_IA3dGeom2)");
		hr = pA3d->QueryInterface(IID_IA3dGeom2, (void **) &pGeom);
		OKHR(hr);

		if (SUCCEEDED(hr) && pGeom)
		{
			STEP("Geom::SetReflectionUpdateInterval(1)");
			hr = pGeom->SetReflectionUpdateInterval(1);
			OKHR(hr);

			STEP("Geom::SetOcclusionUpdateInterval(1)");
			hr = pGeom->SetOcclusionUpdateInterval(1);
			OKHR(hr);

			pGeom->SetReflectionGainScale(1.0f);
			pGeom->SetReflectionDelayScale(1.0f);
			pGeom->SetPolygonBloatFactor(0.1f);
			pA3d->SetMaxReflectionDelayTime(0.3f);

			STEP("Geom::Enable(A3D_1ST_REFLECTIONS)");
			hr = pGeom->Enable(A3D_1ST_REFLECTIONS);
			OKHR(hr);

			STEP("Geom::NewMaterial");
			hr = pGeom->NewMaterial(&pMaterial);
			OKHR(hr);

			if (SUCCEEDED(hr) && pMaterial)
			{
				pMaterial->SetTransmittance(0.05f, 0.2f);
				pMaterial->SetReflectance(0.9f, 0.8f);

				STEP("Geom::BindMaterial");
				hr = pGeom->BindMaterial(pMaterial);
				OKHR(hr);
			}

			STEP("Geom::LoadIdentity");
			hr = pGeom->LoadIdentity();
			OKHR(hr);

			STEP("Geom::Begin/Vertex3f x24/End (a box)");
			pGeom->Begin(A3D_QUADS);
			for (i = 0; i < 6; i++)
			{
				pGeom->Tag(i + 1);
				for (j = 0; j < 4; j++)
					pGeom->Vertex3fv(afFaces[i][j]);
			}
			hr = pGeom->End();
			OKHR(hr);

			pGeom->PushMatrix();
			pGeom->Translate3fv(afListenerPos);
			hr = pGeom->BindListener();
			OKHR(hr);
			pGeom->PopMatrix();

			pGeom->PushMatrix();
			pGeom->Translate3fv(afSourcePos);
			STEP("Geom::BindSource");
			hr = pGeom->BindSource(pSource);
			OKHR(hr);
			pGeom->PopMatrix();

			/*
			 * NOT PART OF THE ORIGINAL.  Repeat geometry submission and Flush()
			 * to allow reflection slots to settle, as in SceneRooms.cpp --settle.
			*/
			for (i = 0; i < 10; i++)
			{
			int	k;

				pA3d->Clear();
				pGeom->LoadIdentity();

				pGeom->BindMaterial(pMaterial);
				pGeom->Begin(A3D_QUADS);
				for (j = 0; j < 6; j++)
				{
					pGeom->Tag(j + 1);
					for (k = 0; k < 4; k++)
						pGeom->Vertex3fv(afFaces[j][k]);
				}
				pGeom->End();

				pGeom->PushMatrix();
				pGeom->Translate3fv(afListenerPos);
				pGeom->BindListener();
				pGeom->PopMatrix();

				pGeom->PushMatrix();
				pGeom->Translate3fv(afSourcePos);
				pGeom->BindSource(pSource);
				pGeom->PopMatrix();

				pA3d->Flush();
			}
		}
	}

	/* Run the trace to set up the scene: play, flush, stop, rewind.  The stream
	   is opened only on the second play, so both DLLs start at the same point. */

	STEP("IA3dSource2::Play (for the trace)");
	hr = pSource->Play(A3D_LOOPED);
	OKHR(hr);

	STEP("IA3d5::Flush");
	hr = pA3d->Flush();
	OKHR(hr);

	STEP("IA3dSource2::Stop");
	hr = pSource->Stop();
	OKHR(hr);

	STEP("IA3dSource2::Rewind");
	hr = pSource->Rewind();
	OKHR(hr);

	EnterCriticalSection(&capture_state.lock);
	capture_state.stream_started = 1;
	LeaveCriticalSection(&capture_state.lock);

	STEP("IA3dSource2::Play A3D_LOOPED");
	hr = pSource->Play(A3D_LOOPED);
	OKHR(hr);
	if (q3)
	{
		STEP("Q3 Flush queued playback");
		hr = pA3d->Flush();
		OKHR(hr);
		if (FAILED(hr)) return 1;
	}

	/* Let the mixer reach steady state before measuring: the first fills carry
	   a startup transient that clips the reference's captured peak. */
	Sleep(500);
	capture_state.statistics_enabled = 1;

	printf("\ncapturing for %lu ms after a 500 ms warm-up (MixThread runs one pass per 20 ms)...\n",
	       (unsigned long) nCaptureMs);
	Sleep(nCaptureMs);

	/* Close the per-buffer streams here, under the same lock Unlock takes, so
	   nothing the teardown provokes reaches them. */

	EnterCriticalSection(&capture_state.lock);
	capture_state.stream_finished = 1;
	LeaveCriticalSection(&capture_state.lock);

	/* Read observables before stopping the source. */

	{
	A3DVAL	fAudibility  = -1.0f;
	A3DVAL	fSrcGain     = -1.0f;
	A3DVAL	fOcclusion   = -1.0f;
	A3DVAL	fOutGain     = -1.0f;
	A3DVAL	fDistScale   = -1.0f;
	DWORD	dwRenderMode = 0;

		pSource->GetAudibility(&fAudibility);
		pSource->GetGain(&fSrcGain);
		pSource->GetOcclusionFactor(&fOcclusion);
		pSource->GetRenderMode(&dwRenderMode);
		pA3d->GetOutputGain(&fOutGain);
		pA3d->GetDistanceModelScale(&fDistScale);

		printf("\nAUDCAP_GAIN audibility=%.9g gain=%.9g occlusion=%.9g "
		       "rendermode=0x%lX outgain=%.9g distscale=%.9g\n",
		       (double) fAudibility, (double) fSrcGain, (double) fOcclusion,
		       (unsigned long) dwRenderMode, (double) fOutGain,
		       (double) fDistScale);
	}



	{
	HMODULE	ahMod[256];
	DWORD	cbNeeded;
	DWORD	i;

		if (EnumProcessModules(GetCurrentProcess(), ahMod, sizeof ahMod,
				       &cbNeeded))
		{
			for (i = 0; i < cbNeeded / sizeof(HMODULE); i++)
			{
				char szPath[MAX_PATH];

				if (!GetModuleFileNameA(ahMod[i], szPath,
							sizeof szPath))
					continue;

				if (strstr(szPath, "a3d") || strstr(szPath, "A3D"))
					printf("AUDCAP_MODULE %s\n", szPath);
			}
		}
	}

	STEP("IA3dSource2::Stop");
	hr = pSource->Stop();
	OKHR(hr);

	pSource->Release();
	pA3d->Release();
	pcf->Release();

	CoRevokeClassObject(dwCookie);



	printf("\n=== capture ===\n");

	EnterCriticalSection(&capture_state.lock);

	printf("CreateSoundBuffer calls : %ld\n", capture_state.buffer_creation_count);

	printf("buffer sizes            :");
	for (int i = 0; i < capture_state.buffer_count; i++)
		printf(" 0x%lX", (unsigned long) capture_state.buffers[i].dwBufferBytes);
	printf("\n");

	printf("GetCurrentPosition calls: %ld  (one per mix pass)\n", capture_state.position_query_count);
	printf("Play calls (on buffers) : %ld\n", capture_state.play_count);
	printf("Lock calls              : %ld\n", capture_state.lock_count);
	printf("Unlock calls            : %ld\n", capture_state.unlock_count);
	printf("Unlock calls with data  : %ld\n", capture_state.nonempty_unlock_count);
	printf("total bytes written     : %I64u\n", capture_state.byte_count);
	printf("total int16 samples     : %I64u\n", capture_state.sample_count);
	printf("sum abs                 : %I64u\n", capture_state.absolute_sample_sum);
	printf("peak abs                : %ld\n", capture_state.peak_amplitude);

	if (capture_state.sample_count)
	{
		double mean = (double) capture_state.absolute_sample_sum / (double) capture_state.sample_count;
		double rms  = sqrt((double) capture_state.squared_sample_sum / (double) capture_state.sample_count);

		printf("mean abs                : %.2f\n", mean);
		printf("rms                     : %.2f\n", rms);
	}

	printf("AUDCAP_STATS create=%ld samples=%I64u peak=%ld sumabs=%I64u sumsq=%I64u\n",
	       capture_state.buffer_creation_count, capture_state.sample_count, capture_state.peak_amplitude, capture_state.absolute_sample_sum, capture_state.squared_sample_sum);

	for (int i = 0; i < capture_state.buffer_count; i++)
	{
		CapturedBuffer		*pInfo = &capture_state.buffers[i];
		long			nPeak;
		unsigned __int64	absolute_sample_sum;
		unsigned __int64	squared_sample_sum;
		unsigned __int64	cSamples;

		StreamStats(pInfo, &nPeak, &absolute_sample_sum, &squared_sample_sum, &cSamples);

		printf("AUDCAP_BUF idx=%d size=0x%lX flags=0x%lX rate=%lu ch=%u "
		       "locks=%ld unlocks=%ld bytes=%lu samples=%I64u peak=%ld "
		       "sumabs=%I64u sumsq=%I64u overflow=%d\n",
		       i,
		       (unsigned long) pInfo->dwBufferBytes,
		       (unsigned long) pInfo->dwFlags,
		       (unsigned long) pInfo->dwSamplesPerSec,
		       (unsigned) pInfo->nChannels,
		       pInfo->nLocks, pInfo->nUnlocks,
		       (unsigned long) pInfo->cbStream,
		       cSamples, nPeak, absolute_sample_sum, squared_sample_sum,
		       pInfo->fOverflow);

		if (pszWavPrefix && pInfo->cbStream)
		{
			char szPath[MAX_PATH];

			_snprintf(szPath, sizeof szPath, "%s-%d.wav",
				  pszWavPrefix, i);
			szPath[sizeof szPath - 1] = '\0';

			if (WriteWav(szPath, pInfo))
				printf("AUDCAP_WAV idx=%d path=%s\n", i, szPath);
		}
	}

	bool runningOutput = false;
	for (int i = 0; i < capture_state.buffer_count; ++i)
	{
		const CapturedBuffer &buffer = capture_state.buffers[i];
		long peak = 0;
		unsigned __int64 samples = 0, sum = 0, squares = 0;
		StreamStats(&buffer, &peak, &sum, &squares, &samples);
		if (peak > 100 && buffer.playingUnlocks) runningOutput = true;
	}
	printf("\n=== verdict ===\n");

	if (capture_state.buffer_creation_count == 0)
	{
		printf("INTERCEPTION FAILED: CreateSoundBuffer was never called.\n");
		printf("The process-local CoRegisterClassObject did not intercept the\n");
		printf("DLL's in-proc CoCreateInstance(CLSID_DirectSound).\n");
	}
	else if (capture_state.sample_count == 0)
	{
		printf("INTERCEPTED but no PCM was written (no Unlock with data).\n");
	}
	else if (capture_state.peak_amplitude > 100 && !runningOutput)
	{
		printf("STOPPED: PCM was written but the output buffer was not playing.\n");
	}
	else if (capture_state.peak_amplitude > 100)
	{
		printf("AUDIBLE: the reconstruction rendered non-zero PCM.\n");
		printf("peak abs %ld across %I64u samples.\n",
		       capture_state.peak_amplitude, capture_state.sample_count);
	}
	else
	{
		printf("SILENT: intercepted and the mixer ran, but the PCM is (near)\n");
		printf("zero (peak abs %ld).  The mixer is producing silence.\n",
		       capture_state.peak_amplitude);
	}

	LeaveCriticalSection(&capture_state.lock);

	if (q3 && nScene == 2)
	{
		DWORD different = 0;
		for (int i = 0; i < capture_state.buffer_count; ++i)
		{
			const CapturedBuffer &buffer = capture_state.buffers[i];
			if (buffer.nChannels != 2 || buffer.wBitsPerSample != 16) continue;
			const short *samples = (const short *) buffer.pStream;
			for (DWORD j = 0; j + 1 < buffer.cbStream / sizeof(short); j += 2)
				if (samples[j] != samples[j + 1]) ++different;
		}
		printf("Q3 positioned stereo: %lu frames differ between ears\n", different);
		if (!different) return 1;
	}

	printf("Output buffer playback: %s\n", runningOutput ? "running" : "STOPPED or silent");
	if (!runningOutput) return 1;

	/* Exit code is the gate: 0 only when the reconstruction rendered audible
	   PCM, so a test runner reads "green means audible" straight from it. */
	return ((capture_state.buffer_creation_count != 0 && capture_state.peak_amplitude > 100) ? 0 : 1);
}

} // namespace a3dcapture
