/////////////////////////////////////////////////////////////////////////////
// SceneRooms.cpp
// ==============
//
// NOT PART OF THE A3D 3.0 SDK.  Project-added, in the style of the seven
// samples beside it.  Aureal shipped no such program.
//
// Eight isolated effects: distance, panning, occlusion, Doppler, cone,
// head-relative positioning, reflections and reverb. The OpenGL camera
// matches the listener; sphere brightness indicates audibility and a red
// source-listener line indicates occlusion.
//
// Use --dll ref/ours/reg to select the reference, reconstruction or registered
// COM server. --script runs deterministic paths; --frames sets room duration,
// --every sets sampling interval and --log receives key=value records.
// See samples/README.md for controls and tests/audio/Compare-SceneLog.ps1
// for comparison. Establish a reference-versus-reference baseline first.
//
// Skip --settle frames after each room change (default 50, one second).
// Early occlusion readings varied by 1.0 between reference runs; settled
// readings matched in the recorded runs. Play position/status remain
// timing-dependent. Startup diagnostics and summaries use the console.
//
// AddVectoredExceptionHandler reports DLL faults before process exit.
#ifndef WINVER
#define WINVER		0x0501
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT	0x0501
#endif

#include <windows.h>
#include <objbase.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <GL/gl.h>
#include <GL/glu.h>

#include <initguid.h>
#include "ia3dapi.h"

/////////////////////////////////////////////////////////////////////////////
// Room distances use metres; motion depends only on elapsed room time.
enum
{
	MOTION_STATIC,		// parked
	MOTION_ORBIT,		// circles the base point in xz
	MOTION_SWEEP,		// slides along an axis and reverses at the end
	MOTION_PENDULUM		// swings along an axis
};

// Acoustic geometry is omitted from rooms testing source-only effects.
enum
{
	GEOM_OPEN,		// no polygons.  The floor drawn is not one
	GEOM_WALL,		// one wall, between the listener and the source
	GEOM_BOX		// a closed box, six walls
};

#define NO_PRESET	(-1)

typedef struct
{
	const char	*pszName;
	const char	*pszListenFor;

	const char	*pszFile;
	int		 nMotion;
	float		 fBase[3];	// where it is, or what it moves about
	float		 fAxis[3];	// which way it moves
	float		 fParam;	// radius, or half the travel
	float		 fPeriod;	// seconds for one cycle

	float		 fGain;
	float		 fPitch;
	float		 fMinDist;
	float		 fMaxDist;
	float		 fDopplerScale;
	DWORD		 dwRenderMode;
	DWORD		 dwTransformMode;
	BOOL		 bCone;

	int		 nGeom;
	float		 fGeomSize[3];	// the wall's half-width, height, or the box
	int		 nReverbPreset;	// NO_PRESET, or the one to start on
	BOOL		 bWalkPresets;	// step through them as the room runs

	float		 fStart[3];	// where the listener comes in
	float		 fStartYaw;

	float		 fColour[3];
} SCENEROOM;

static const SCENEROOM g_Room[] =
{
	{
		"distance", "the level falling away and coming back",
		"heli.wav", MOTION_SWEEP, { 0.0f, 0.0f, -4.0f }, { 0.0f, 0.0f, -1.0f },
		30.0f, 16.0f,
		1.0f, 1.0f, 1.0f, 70.0f, 0.0f,
		A3DSOURCE_RENDERMODE_DEFAULT, A3DSOURCE_TRANSFORMMODE_NORMAL, FALSE,
		GEOM_OPEN, { 0.0f, 0.0f, 0.0f }, NO_PRESET, FALSE,
		{ 0.0f, 0.0f, 4.0f }, 0.0f,
		{ 0.35f, 0.65f, 1.00f }
	},
	{
		"panning", "one source going round your head",
		"linn1.wav", MOTION_ORBIT, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f },
		5.0f, 9.0f,
		1.0f, 1.0f, 1.0f, 40.0f, 0.0f,
		A3DSOURCE_RENDERMODE_DEFAULT, A3DSOURCE_TRANSFORMMODE_NORMAL, FALSE,
		GEOM_OPEN, { 0.0f, 0.0f, 0.0f }, NO_PRESET, FALSE,
		{ 0.0f, 0.0f, 0.0f }, 0.0f,
		{ 0.40f, 1.00f, 0.55f }
	},
	{
		"occlusion", "one wall in the way.  F2 takes it away",
		"linn1.wav", MOTION_STATIC, { 0.0f, 0.0f, -9.0f }, { 1.0f, 0.0f, 0.0f },
		0.0f, 0.0f,
		1.0f, 1.0f, 1.0f, 40.0f, 0.0f,
		A3DSOURCE_RENDERMODE_DEFAULT, A3DSOURCE_TRANSFORMMODE_NORMAL, FALSE,
		GEOM_WALL, { 4.0f, 4.0f, -4.0f }, NO_PRESET, FALSE,
		{ 0.0f, 0.0f, 3.0f }, 0.0f,
		{ 1.00f, 0.50f, 0.25f }
	},
	{
		"doppler", "the pitch bending as it goes past",
		"linn48.wav", MOTION_SWEEP, { 0.0f, 0.0f, -3.0f }, { 1.0f, 0.0f, 0.0f },
		40.0f, 8.0f,
		1.0f, 1.0f, 2.0f, 90.0f, 1.0f,
		A3DSOURCE_RENDERMODE_DEFAULT, A3DSOURCE_TRANSFORMMODE_NORMAL, FALSE,
		GEOM_OPEN, { 0.0f, 0.0f, 0.0f }, NO_PRESET, FALSE,
		{ 0.0f, 0.0f, 2.0f }, 0.0f,
		{ 1.00f, 0.85f, 0.30f }
	},
	{
		"cone", "loud when it points at you, quiet when it turns away",
		"linn48.wav", MOTION_STATIC, { 0.0f, 0.0f, -5.0f }, { 0.0f, 1.0f, 0.0f },
		0.0f, 10.0f,
		1.0f, 1.0f, 1.0f, 40.0f, 0.0f,
		A3DSOURCE_RENDERMODE_DEFAULT, A3DSOURCE_TRANSFORMMODE_NORMAL, TRUE,
		GEOM_OPEN, { 0.0f, 0.0f, 0.0f }, NO_PRESET, FALSE,
		{ 0.0f, 0.0f, 0.0f }, 0.0f,
		{ 1.00f, 0.60f, 0.85f }
	},
	{
		"headrelative", "it does not move when you turn",
		"heli.wav", MOTION_STATIC, { 1.5f, 0.0f, -1.0f }, { 1.0f, 0.0f, 0.0f },
		0.0f, 0.0f,
		0.7f, 1.0f, 1.0f, 20.0f, 0.0f,
		A3DSOURCE_RENDERMODE_MONO, A3DSOURCE_TRANSFORMMODE_HEADRELATIVE, FALSE,
		GEOM_OPEN, { 0.0f, 0.0f, 0.0f }, NO_PRESET, FALSE,
		{ 0.0f, 0.0f, 0.0f }, 0.0f,
		{ 0.75f, 0.75f, 0.80f }
	},
	{
		"reflections", "a small hard box.  F4 turns them off",
		"linn1.wav", MOTION_STATIC, { 0.0f, 0.0f, -3.0f }, { 1.0f, 0.0f, 0.0f },
		0.0f, 0.0f,
		1.0f, 1.0f, 1.0f, 40.0f, 0.0f,
		A3DSOURCE_RENDERMODE_DEFAULT, A3DSOURCE_TRANSFORMMODE_NORMAL, FALSE,
		GEOM_BOX, { 6.0f, 4.0f, 6.0f }, NO_PRESET, FALSE,
		{ 0.0f, 0.0f, 3.0f }, 0.0f,
		{ 0.55f, 0.80f, 1.00f }
	},
	{
		"reverb", "the tail changing as the presets go by.  F5 toggles it",
		"clap_test.wav", MOTION_STATIC, { 0.0f, 0.0f, -4.0f }, { 1.0f, 0.0f, 0.0f },
		0.0f, 0.0f,
		1.0f, 1.0f, 1.0f, 40.0f, 0.0f,
		A3DSOURCE_RENDERMODE_DEFAULT, A3DSOURCE_TRANSFORMMODE_NORMAL, FALSE,
		GEOM_OPEN, { 0.0f, 0.0f, 0.0f }, A3DREVERB_PRESET_HANGAR, TRUE,
		{ 0.0f, 0.0f, 2.0f }, 0.0f,
		{ 0.85f, 0.45f, 1.00f }
	}
};

#define NUM_ROOMS	(sizeof(g_Room) / sizeof(g_Room[0]))

// The presets the reverb room walks, and how long it holds each.  On a frame
// boundary, so that two runs change it in the same place.
static const DWORD g_ReverbScript[] =
{
	A3DREVERB_PRESET_HANGAR,
	A3DREVERB_PRESET_STONEROOM,
	A3DREVERB_PRESET_CAVE,
	A3DREVERB_PRESET_CARPETEDHALLWAY
};

#define NUM_REVERB_PRESETS	(sizeof(g_ReverbScript) / sizeof(g_ReverbScript[0]))
#define REVERB_HOLD_SECONDS	4.0f

// How far the drawn floor goes.  It is scenery: no polygon is submitted for it.
#define FLOOR_EXTENT	40.0f
#define FLOOR_Y		(-2.0f)

// The three ways to look at a room.
enum
{
	VIEW_FIRSTPERSON,
	VIEW_CHASE,
	VIEW_PLAN,
	VIEW_COUNT
};

/////////////////////////////////////////////////////////////////////////////
// Options.
//
static const char	*g_pszDll	= "reg";
static const char	*g_pszLog	= NULL;
static const char	*g_pszData	= NULL;
static const char	*g_pszRoom	= NULL;
static int		 g_nFrames	= 500;		// per room, 10s at 50Hz
static int		 g_nTickMs	= 20;
static int		 g_nEvery	= 25;		// sample twice a second
static int		 g_nSettle	= 50;		// frames before sampling
static int		 g_nWidth	= 1024;
static int		 g_nHeight	= 640;
static BOOL		 g_bScripted	= FALSE;
static BOOL		 g_bReverb	= TRUE;
static BOOL		 g_bRequestReverb = FALSE;	// asks Init for A3D_REVERB, see InitEx
static BOOL		 g_bVerbose	= FALSE;

/////////////////////////////////////////////////////////////////////////////
// State.
//
static HMODULE		 g_hA3dApi;		// only when loaded by path
static char		 g_szModule[MAX_PATH];	// what actually served us

static IA3d5		*g_pA3d;
static IA3dGeom2	*g_pGeom;
static IA3dListener	*g_pListener;
static IA3dReverb	*g_pReverb;
static IA3dMaterial	*g_pMaterial;
static IA3dSource2	*g_pSource[NUM_ROOMS];

static FILE		*g_fpLog;

// Which room is running, and its own clock, which restarts when it is entered
// so that a room behaves the same whether it is walked into or scripted.
static int		 g_nRoom;
static DWORD		 g_dwRoomFrame;

// The listener, and the camera, which are the same thing.
static float		 g_fListener[3];
static float		 g_fYaw;		// degrees, into Rotate3f
static float		 g_fPitch;		// the camera's alone

// What the last frame read back, for drawing.
static float		 g_fAudibility;
static float		 g_fOcclusion;
static DWORD		 g_dwStatus;
static float		 g_fSourcePos[3];

// Toggles.
static BOOL		 g_bGeometry	= TRUE;		// the room's walls
static BOOL		 g_bOcclusions	= TRUE;
static BOOL		 g_bReflections	= TRUE;
static BOOL		 g_bReverbOn	= TRUE;		// F5, see BindReverb below
static BOOL		 g_bPaused;
static int		 g_nPreset;
static int		 g_nView	= VIEW_FIRSTPERSON;

// The window.
static HWND		 g_hWnd;
static HDC		 g_hDC;
static HGLRC		 g_hRC;
static GLuint		 g_uFontBase;
static GLUquadric	*g_pQuadric;
static BOOL		 g_bRunning	= TRUE;
static BOOL		 g_bMouseLook	= TRUE;
static BOOL		 g_abKey[256];
static float		 g_fFps;

// Per-room aggregates, printed at the end.
typedef struct
{
	int	nSamples;
	double	dAudSum;
	float	fAudMin;
	float	fAudMax;
	double	dOccSum;
	float	fOccMin;
	float	fOccMax;
	DWORD	dwStatusSeen;
	DWORD	dwLastPlayPos;
} ROOMSTAT;

static ROOMSTAT		 g_Stat[NUM_ROOMS];

// Every interface call is counted, and the first failure of each call site is
// kept, so a run that goes wrong names the method rather than a step number.
#define MAX_FAILURES	32

typedef struct
{
	const char	*pszWhat;
	HRESULT		 hr;
	int		 nRoom;
	int		 nFrame;
	int		 nCount;
} FAILURE;

static FAILURE		 g_Failure[MAX_FAILURES];
static int		 g_nFailures;
static long		 g_lCalls;
static long		 g_lFailedCalls;
static int		 g_nFrame;

// What the run is in the middle of, so that a fault names it.  Teardown is the
// part with no frame number to report.
static const char	*g_pszStage = "startup";

/////////////////////////////////////////////////////////////////////////////
// Plumbing.
//
static void RecordFailure(const char* pszWhat, HRESULT hr)
{
	for(int i = 0; i < g_nFailures; i++)
	{
		if(g_Failure[i].pszWhat == pszWhat && g_Failure[i].hr == hr)
		{
			g_Failure[i].nCount++;
			return;
		}
	}

	if(g_nFailures < MAX_FAILURES)
	{
		g_Failure[g_nFailures].pszWhat	= pszWhat;
		g_Failure[g_nFailures].hr	= hr;
		g_Failure[g_nFailures].nRoom	= g_nRoom;
		g_Failure[g_nFailures].nFrame	= g_nFrame;
		g_Failure[g_nFailures].nCount	= 1;
		g_nFailures++;
	}
}

static HRESULT Checked(const char* pszWhat, HRESULT hr)
{
	g_lCalls++;

	if(FAILED(hr))
	{
		g_lFailedCalls++;
		RecordFailure(pszWhat, hr);

		if(g_bVerbose)
		{
			fprintf(stderr, "%s frame %d: %s hr=0x%08lX\n",
				g_Room[g_nRoom].pszName, g_nFrame, pszWhat,
				(unsigned long)hr);
		}
	}

	return hr;
}

#define CALL(what, expr)	Checked((what), (expr))

// An access violation inside the DLL is the interesting failure, so print it as
// an RVA of whichever a3dapi.dll is loaded before the process dies.
static LONG CALLBACK FaultFilter(EXCEPTION_POINTERS* ep)
{
	if(ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
	{
		BYTE*	pb   = (BYTE*)ep->ExceptionRecord->ExceptionAddress;
		HMODULE	hMod = g_hA3dApi ? g_hA3dApi : GetModuleHandleA("a3dapi.dll");

		printf("\n*** FAULT at %p", (void*)pb);

		if(hMod && pb > (BYTE*)hMod && pb - (BYTE*)hMod < 0x400000)
			printf("  a3dapi RVA=0x%08lX", (unsigned long)(pb - (BYTE*)hMod));

		printf("  stage=%s room=%s frame=%d ***\n", g_pszStage,
		       g_Room[g_nRoom].pszName, g_nFrame);
		fflush(stdout);

		if(g_fpLog)
		{
			fprintf(g_fpLog, "# FAULT stage=%s room=%s frame=%d\n",
				g_pszStage, g_Room[g_nRoom].pszName, g_nFrame);
			fflush(g_fpLog);
		}
	}

	return EXCEPTION_CONTINUE_SEARCH;
}

/////////////////////////////////////////////////////////////////////////////
// Resolve relative paths from the working directory, then the executable
// directory and its ancestors.
//
static const char* FindNearby(const char* pszRelative)
{
	static char	szPath[MAX_PATH];
	char		szBase[MAX_PATH];

	if(GetFileAttributesA(pszRelative) != INVALID_FILE_ATTRIBUTES)
	{
		strcpy(szPath, pszRelative);
		return szPath;
	}

	if(!GetModuleFileNameA(NULL, szBase, sizeof(szBase)))
	{
		strcpy(szPath, pszRelative);
		return szPath;
	}

	for(int i = 0; i < 6; i++)
	{
		char* pszSlash = strrchr(szBase, '\\');
		if(!pszSlash)
			break;

		*pszSlash = '\0';
		sprintf(szPath, "%s\\%s", szBase, pszRelative);

		if(GetFileAttributesA(szPath) != INVALID_FILE_ATTRIBUTES)
			return szPath;
	}

	strcpy(szPath, pszRelative);
	return szPath;
}

static const char* MediaPath(const char* pszFile)
{
	static char	szPath[MAX_PATH];
	char		szTry[MAX_PATH];

	if(g_pszData)
	{
		sprintf(szPath, "%s\\%s", g_pszData, pszFile);
		return szPath;
	}

	// Beside the executable, which is where the build stages them.
	if(GetFileAttributesA(pszFile) != INVALID_FILE_ATTRIBUTES)
	{
		strcpy(szPath, pszFile);
		return szPath;
	}

	sprintf(szTry, "samples\\data\\%s", pszFile);
	strcpy(szPath, FindNearby(szTry));

	return szPath;
}

/////////////////////////////////////////////////////////////////////////////
// Explicit DLL paths use DllGetClassObject; registry mode uses CoCreateInstance.
//
static HRESULT CreateA3dFromPath(const char* pszPath, IA3d5** ppA3d)
{
	g_hA3dApi = LoadLibraryA(pszPath);
	if(!g_hA3dApi)
	{
		printf("LoadLibrary(\"%s\") failed, GetLastError=%lu\n", pszPath,
		       GetLastError());
		return E_FAIL;
	}

	LPFNGETCLASSOBJECT pfn = (LPFNGETCLASSOBJECT)
				 GetProcAddress(g_hA3dApi, "DllGetClassObject");
	if(!pfn)
	{
		printf("%s exports no DllGetClassObject\n", pszPath);
		return E_FAIL;
	}

	IClassFactory*	pcf;
	HRESULT		hr = pfn(CLSID_A3dApi, IID_IClassFactory, (void**)&pcf);

	if(FAILED(hr))
		return hr;

	hr = pcf->CreateInstance(NULL, IID_IA3d5, (void**)ppA3d);
	pcf->Release();

	return hr;
}

// Name the file that is actually serving the object.  Under --dll reg this is
// the only way to know which of the two ran.
static void NoteServingModule(void)
{
	// Loaded by path, the file is whatever --dll named and its module name
	// is not a3dapi.dll, so the handle from LoadLibrary is the only one
	// that finds it.
	HMODULE hMod = g_hA3dApi ? g_hA3dApi : GetModuleHandleA("a3dapi.dll");

	if(hMod)
		GetModuleFileNameA(hMod, g_szModule, sizeof(g_szModule));
	else
		strcpy(g_szModule, "(a3dapi.dll not loaded)");
}

static void PrintModuleIdentity(void)
{
	printf("server : %s\n", g_szModule);

	HANDLE hFile = CreateFileA(g_szModule, GENERIC_READ, FILE_SHARE_READ,
				   NULL, OPEN_EXISTING, 0, NULL);
	if(hFile != INVALID_HANDLE_VALUE)
	{
		LARGE_INTEGER li;

		if(GetFileSizeEx(hFile, &li))
			printf("bytes  : %ld\n", (long)li.QuadPart);

		CloseHandle(hFile);
	}

	DWORD	dwHandle;
	DWORD	dwSize = GetFileVersionInfoSizeA(g_szModule, &dwHandle);

	if(dwSize)
	{
		void* pv = malloc(dwSize);

		VS_FIXEDFILEINFO*	pffi;
		UINT			cb;

		if(pv && GetFileVersionInfoA(g_szModule, dwHandle, dwSize, pv) &&
		   VerQueryValueA(pv, "\\", (void**)&pffi, &cb))
		{
			printf("version: %u.%u.%u.%u\n",
			       HIWORD(pffi->dwFileVersionMS),
			       LOWORD(pffi->dwFileVersionMS),
			       HIWORD(pffi->dwFileVersionLS),
			       LOWORD(pffi->dwFileVersionLS));
		}

		free(pv);
	}
}

/////////////////////////////////////////////////////////////////////////////
// Triangle wave in [-1,1] for reversing source motion.
static float Triangle(float fPhase)
{
	float f = (float)fmod((double)fPhase, 1.0);

	if(f < 0.0f)
		f += 1.0f;

	return (f < 0.5f) ? (4.0f * f - 1.0f) : (3.0f - 4.0f * f);
}

static void EvalSource(int nRoom, float t, float fPos[3], float fVel[3])
{
	const SCENEROOM*	p = &g_Room[nRoom];
	float			w;

	fPos[0] = p->fBase[0];
	fPos[1] = p->fBase[1];
	fPos[2] = p->fBase[2];
	fVel[0] = fVel[1] = fVel[2] = 0.0f;

	switch(p->nMotion)
	{
	case MOTION_ORBIT:
		w = 6.283185307f / p->fPeriod;
		fPos[0] += p->fParam * (float)sin(w * t);
		fPos[2] += p->fParam * (float)cos(w * t);
		fVel[0]  = p->fParam * w * (float)cos(w * t);
		fVel[2]  = -p->fParam * w * (float)sin(w * t);
		break;

	case MOTION_SWEEP:
	{
		float d = p->fParam * Triangle(t / p->fPeriod);

		fPos[0] += p->fAxis[0] * d;
		fPos[1] += p->fAxis[1] * d;
		fPos[2] += p->fAxis[2] * d;

		// The triangle covers four half-travels in a period, so the
		// speed is constant and only the sign turns over.
		float phase = (float)fmod((double)(t / p->fPeriod), 1.0);
		if(phase < 0.0f)
			phase += 1.0f;

		float speed = 4.0f * p->fParam / p->fPeriod;
		if(phase >= 0.5f)
			speed = -speed;

		fVel[0] = p->fAxis[0] * speed;
		fVel[1] = p->fAxis[1] * speed;
		fVel[2] = p->fAxis[2] * speed;
		break;
	}

	case MOTION_PENDULUM:
	{
		w = 6.283185307f / p->fPeriod;

		float d = p->fParam * (float)sin(w * t);
		float v = p->fParam * w * (float)cos(w * t);

		fPos[0] += p->fAxis[0] * d;
		fPos[1] += p->fAxis[1] * d;
		fPos[2] += p->fAxis[2] * d;

		fVel[0] = p->fAxis[0] * v;
		fVel[1] = p->fAxis[1] * v;
		fVel[2] = p->fAxis[2] * v;
		break;
	}

	case MOTION_STATIC:
	default:
		break;
	}
}

// Scripted listener motion samples a circle while facing the source.
// Rotate3f about +y maps forward -z to (-sin a,0,-cos a), so the facing
// angle is atan2(-x,-z).
static void EvalListener(int nRoom, float t, float fPos[3], float* pfYaw)
{
	const SCENEROOM*	p = &g_Room[nRoom];
	float			w = 6.283185307f / 12.0f;

	fPos[0] = p->fStart[0] + 3.0f * (float)sin(w * t);
	fPos[1] = p->fStart[1];
	fPos[2] = p->fStart[2] + 2.0f * (float)cos(w * t);

	// Face the source's resting place, so a room where the source moves is
	// heard sweeping past a listener that is not chasing it.
	float dx = p->fBase[0] - fPos[0];
	float dz = p->fBase[2] - fPos[2];

	*pfYaw = (float)(atan2((double)-dx, (double)-dz) * 57.295779513);
}

// The way the listener is facing, from the angle handed to Rotate3f.
static void ForwardVector(float fYaw, float fPitch, float fOut[3])
{
	float a = fYaw * 0.017453293f;
	float p = fPitch * 0.017453293f;
	float c = (float)cos(p);

	fOut[0] = -(float)sin(a) * c;
	fOut[1] = (float)sin(p);
	fOut[2] = -(float)cos(a) * c;
}

/////////////////////////////////////////////////////////////////////////////

//
static HRESULT CreateScene(void)
{
	printf("scene  : geometry ");

	HRESULT hr = CALL("QueryInterface(IID_IA3dGeom2)",
			  g_pA3d->QueryInterface(IID_IA3dGeom2, (void**)&g_pGeom));
	if(FAILED(hr))
		return hr;

	printf("listener ");

	hr = CALL("QueryInterface(IID_IA3dListener)",
		  g_pA3d->QueryInterface(IID_IA3dListener, (void**)&g_pListener));
	if(FAILED(hr))
		return hr;

	printf("root ");

	CALL("SetCoordinateSystem", g_pA3d->SetCoordinateSystem(A3D_RIGHT_HANDED_CS));
	CALL("SetUnitsPerMeter", g_pA3d->SetUnitsPerMeter(1.0f));
	CALL("SetDopplerScale", g_pA3d->SetDopplerScale(1.0f));
	CALL("SetDistanceModelScale", g_pA3d->SetDistanceModelScale(1.0f));
	CALL("SetOutputGain", g_pA3d->SetOutputGain(1.0f));
	CALL("SetResourceManagerMode",
	     g_pA3d->SetResourceManagerMode(A3D_RESOURCE_MODE_DYNAMIC));
	CALL("SetNumFallbackSources", g_pA3d->SetNumFallbackSources(4));
	CALL("SetMaxReflectionDelayTime", g_pA3d->SetMaxReflectionDelayTime(0.3f));

	printf("geomstate ");

	CALL("Geom::Enable(A3D_OCCLUSIONS)", g_pGeom->Enable(A3D_OCCLUSIONS));
	CALL("Geom::Enable(A3D_1ST_REFLECTIONS)", g_pGeom->Enable(A3D_1ST_REFLECTIONS));
	CALL("Geom::SetOcclusionMode", g_pGeom->SetOcclusionMode(A3D_QUICK));
	CALL("Geom::SetReflectionMode", g_pGeom->SetReflectionMode(A3D_QUICK));
	CALL("Geom::SetReflectionGainScale", g_pGeom->SetReflectionGainScale(1.0f));
	CALL("Geom::SetReflectionDelayScale", g_pGeom->SetReflectionDelayScale(1.0f));
	CALL("Geom::SetPolygonBloatFactor", g_pGeom->SetPolygonBloatFactor(0.1f));
	CALL("Geom::SetOcclusionUpdateInterval", g_pGeom->SetOcclusionUpdateInterval(1));
	CALL("Geom::SetReflectionUpdateInterval", g_pGeom->SetReflectionUpdateInterval(1));

	// One material, on whatever walls a room has.  Rooms differ in their
	// geometry, not in what it is made of, so that the material is not a
	// second variable.
	CALL("Geom::NewMaterial", g_pGeom->NewMaterial(&g_pMaterial));

	if(g_pMaterial)
	{
		CALL("Material::SetTransmittance",
		     g_pMaterial->SetTransmittance(0.05f, 0.2f));
		CALL("Material::SetReflectance",
		     g_pMaterial->SetReflectance(0.9f, 0.8f));
	}

	printf("material ");

	if(g_bReverb)
	{
		hr = CALL("NewReverb", g_pA3d->NewReverb(&g_pReverb));

		if(SUCCEEDED(hr) && g_pReverb)
		{
			CALL("Reverb::SetReverbPreset",
			     g_pReverb->SetReverbPreset(g_ReverbScript[0]));
			CALL("BindReverb", g_pA3d->BindReverb(g_pReverb));
		}
	}

	printf("reverb\n");

	// Create one source per room; play and bind only the active room's source.
	for(int i = 0; i < (int)NUM_ROOMS; i++)
	{
		const SCENEROOM* p = &g_Room[i];

		hr = CALL("NewSource",
			  g_pA3d->NewSource(A3DSOURCE_INITIAL_RENDERMODE_A3D,
					    &g_pSource[i]));
		if(FAILED(hr) || !g_pSource[i])
		{
			printf("room %s: NewSource failed, hr=0x%08lX\n",
			       p->pszName, (unsigned long)hr);
			return hr;
		}

		hr = CALL("LoadFile",
			  g_pSource[i]->LoadFile((char*)MediaPath(p->pszFile),
						 A3DSOURCE_FORMAT_WAVE));
		if(FAILED(hr))
		{
			printf("room %s: LoadFile(\"%s\") failed, hr=0x%08lX\n",
			       p->pszName, MediaPath(p->pszFile), (unsigned long)hr);
			return hr;
		}

		CALL("Source::SetGain", g_pSource[i]->SetGain(p->fGain));
		CALL("Source::SetPitch", g_pSource[i]->SetPitch(p->fPitch));
		CALL("Source::SetMinMaxDistance",
		     g_pSource[i]->SetMinMaxDistance(p->fMinDist, p->fMaxDist,
						     A3D_AUDIBLE));
		CALL("Source::SetRenderMode",
		     g_pSource[i]->SetRenderMode(p->dwRenderMode));
		CALL("Source::SetTransformMode",
		     g_pSource[i]->SetTransformMode(p->dwTransformMode));
		CALL("Source::SetPriority", g_pSource[i]->SetPriority(0.5f));

		// Doppler is off everywhere but in the room that is for it.
		CALL("Source::SetDopplerScale",
		     g_pSource[i]->SetDopplerScale(p->fDopplerScale));
		CALL("Source::SetEq", g_pSource[i]->SetEq(1.0f));

		// So is the reverb send.
		CALL("Source::SetReverbMix",
		     g_pSource[i]->SetReverbMix(1.0f,
						p->nReverbPreset == NO_PRESET
						? 0.0f : 0.6f));

		if(p->bCone)
			CALL("Source::SetCone", g_pSource[i]->SetCone(50.0f, 140.0f, 0.05f));

		printf("room %d: %-13s %-12s %s\n", i, p->pszName, p->pszFile,
		       p->pszListenFor);
	}

	return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// Entering and leaving a room.
//
static void LeaveRoom(void)
{
	if(g_pSource[g_nRoom])
		CALL("Source::Stop", g_pSource[g_nRoom]->Stop());
}

static void EnterRoom(int nRoom)
{
	const SCENEROOM* p;

	if(nRoom < 0)
		nRoom = (int)NUM_ROOMS - 1;
	if(nRoom >= (int)NUM_ROOMS)
		nRoom = 0;

	g_nRoom	      = nRoom;
	g_dwRoomFrame = 0;
	p	      = &g_Room[nRoom];

	g_fListener[0] = p->fStart[0];
	g_fListener[1] = p->fStart[1];
	g_fListener[2] = p->fStart[2];
	g_fYaw	       = p->fStartYaw;
	g_fPitch       = 0.0f;

	g_bGeometry   = TRUE;
	g_fAudibility = 0.0f;
	g_fOcclusion  = 0.0f;
	g_dwStatus    = 0;

	if(g_pReverb && p->nReverbPreset != NO_PRESET)
	{
		g_nPreset = 0;
		CALL("Reverb::SetReverbPreset",
		     g_pReverb->SetReverbPreset((DWORD)p->nReverbPreset));
	}

	if(g_pSource[nRoom])
	{
		CALL("Source::Rewind", g_pSource[nRoom]->Rewind());
		CALL("Source::Play", g_pSource[nRoom]->Play(A3D_LOOPED));
	}
}

static void GoToRoom(int nRoom)
{
	LeaveRoom();
	EnterRoom(nRoom);
}

/////////////////////////////////////////////////////////////////////////////
// The polygons a room submits.  A room that is not about geometry submits none.
//
static void EmitRoomGeometry(void)
{
	const SCENEROOM* p = &g_Room[g_nRoom];

	if(!g_bGeometry || p->nGeom == GEOM_OPEN)
		return;

	CALL("Geom::BindMaterial", g_pGeom->BindMaterial(g_pMaterial));
	CALL("Geom::Begin(QUADS)", g_pGeom->Begin(A3D_QUADS));

	if(p->nGeom == GEOM_WALL)
	{
		// One wall across the line between the listener and the source.
		// fGeomSize is its half-width, its height, and where it stands
		// on z.
		float hw = p->fGeomSize[0];
		float h  = p->fGeomSize[1];
		float z  = p->fGeomSize[2];

		g_pGeom->Tag(1);
		g_pGeom->Vertex3f(-hw, FLOOR_Y, z);
		g_pGeom->Vertex3f( hw, FLOOR_Y, z);
		g_pGeom->Vertex3f( hw, h,	z);
		g_pGeom->Vertex3f(-hw, h,	z);
	}
	else if(p->nGeom == GEOM_BOX)
	{
		float x = p->fGeomSize[0];
		float y = p->fGeomSize[1];
		float z = p->fGeomSize[2];

		g_pGeom->Tag(1);				// -z
		g_pGeom->Vertex3f(-x, FLOOR_Y, -z);
		g_pGeom->Vertex3f( x, FLOOR_Y, -z);
		g_pGeom->Vertex3f( x, y,       -z);
		g_pGeom->Vertex3f(-x, y,       -z);

		g_pGeom->Tag(2);				// +z
		g_pGeom->Vertex3f( x, FLOOR_Y,  z);
		g_pGeom->Vertex3f(-x, FLOOR_Y,  z);
		g_pGeom->Vertex3f(-x, y,        z);
		g_pGeom->Vertex3f( x, y,        z);

		g_pGeom->Tag(3);				// -x
		g_pGeom->Vertex3f(-x, FLOOR_Y,  z);
		g_pGeom->Vertex3f(-x, FLOOR_Y, -z);
		g_pGeom->Vertex3f(-x, y,       -z);
		g_pGeom->Vertex3f(-x, y,        z);

		g_pGeom->Tag(4);				// +x
		g_pGeom->Vertex3f( x, FLOOR_Y, -z);
		g_pGeom->Vertex3f( x, FLOOR_Y,  z);
		g_pGeom->Vertex3f( x, y,        z);
		g_pGeom->Vertex3f( x, y,       -z);

		g_pGeom->Tag(5);				// floor
		g_pGeom->Vertex3f(-x, FLOOR_Y,  z);
		g_pGeom->Vertex3f( x, FLOOR_Y,  z);
		g_pGeom->Vertex3f( x, FLOOR_Y, -z);
		g_pGeom->Vertex3f(-x, FLOOR_Y, -z);

		g_pGeom->Tag(6);				// ceiling
		g_pGeom->Vertex3f(-x, y, -z);
		g_pGeom->Vertex3f( x, y, -z);
		g_pGeom->Vertex3f( x, y,  z);
		g_pGeom->Vertex3f(-x, y,  z);
	}

	CALL("Geom::End", g_pGeom->End());
}

/////////////////////////////////////////////////////////////////////////////
// One audio frame.  One listener, one source, whatever walls the room has.
//
static void RunAudioFrame(float t)
{
	const SCENEROOM*	p = &g_Room[g_nRoom];
	float			fPos[3];
	float			fVel[3];

	CALL("Clear", g_pA3d->Clear());
	CALL("Geom::LoadIdentity", g_pGeom->LoadIdentity());

	// PushMatrix/PopMatrix return stack depth, not HRESULT; omit CALL checks.
	g_pGeom->PushMatrix();
		CALL("Geom::Translate3fv", g_pGeom->Translate3fv(g_fListener));
		CALL("Geom::Rotate3f", g_pGeom->Rotate3f(g_fYaw, 0.0f, 1.0f, 0.0f));
		CALL("Geom::BindListener", g_pGeom->BindListener());
	g_pGeom->PopMatrix();

	CALL("Listener::SetVelocity3f", g_pListener->SetVelocity3f(0.0f, 0.0f, 0.0f));

	if(g_pSource[g_nRoom])
	{
		EvalSource(g_nRoom, t, fPos, fVel);

		g_fSourcePos[0] = fPos[0];
		g_fSourcePos[1] = fPos[1];
		g_fSourcePos[2] = fPos[2];

		CALL("Source::SetVelocity3fv",
		     g_pSource[g_nRoom]->SetVelocity3fv(fVel));

		g_pGeom->PushMatrix();
			CALL("Geom::Translate3fv", g_pGeom->Translate3fv(fPos));

			if(p->bCone)
			{
				// The directional source turns on the spot, so
				// the listener passes in and out of the beam
				// without either of them moving.
				float w = 360.0f / (p->fPeriod > 0.0f ? p->fPeriod : 10.0f);

				CALL("Geom::Rotate3f",
				     g_pGeom->Rotate3f(w * t, 0.0f, 1.0f, 0.0f));
			}

			CALL("Geom::BindSource",
			     g_pGeom->BindSource(g_pSource[g_nRoom]));
		g_pGeom->PopMatrix();
	}

	EmitRoomGeometry();

	CALL("Flush", g_pA3d->Flush());
}

/////////////////////////////////////////////////////////////////////////////
// Read state each frame for display; log only sampled frames.
//
static void ReadBack(float t, BOOL bSample)
{
	IA3dSource2* pSource = g_pSource[g_nRoom];

	if(!pSource)
		return;

	float	fAud	= 0.0f;
	float	fOcc	= 0.0f;
	float	fGain	= 0.0f;
	float	fx = 0.0f, fy = 0.0f, fz = 0.0f;
	DWORD	dwStatus = 0;
	DWORD	dwPlayPos = 0;

	CALL("Source::GetAudibility", pSource->GetAudibility(&fAud));
	CALL("Source::GetOcclusionFactor", pSource->GetOcclusionFactor(&fOcc));
	CALL("Source::GetGain", pSource->GetGain(&fGain));
	CALL("Source::GetPosition3f", pSource->GetPosition3f(&fx, &fy, &fz));
	CALL("Source::GetStatus", pSource->GetStatus(&dwStatus));
	CALL("Source::GetPlayPosition", pSource->GetPlayPosition(&dwPlayPos));

	g_fAudibility = fAud;
	g_fOcclusion  = fOcc;
	g_dwStatus    = dwStatus;

	if(!bSample)
		return;

	ROOMSTAT* s = &g_Stat[g_nRoom];

	if(s->nSamples == 0)
	{
		s->fAudMin = s->fAudMax = fAud;
		s->fOccMin = s->fOccMax = fOcc;
	}
	else
	{
		if(fAud < s->fAudMin) s->fAudMin = fAud;
		if(fAud > s->fAudMax) s->fAudMax = fAud;
		if(fOcc < s->fOccMin) s->fOccMin = fOcc;
		if(fOcc > s->fOccMax) s->fOccMax = fOcc;
	}

	s->nSamples++;
	s->dAudSum	 += fAud;
	s->dOccSum	 += fOcc;
	s->dwStatusSeen	 |= dwStatus;
	s->dwLastPlayPos  = dwPlayPos;

	if(g_fpLog)
	{
		fprintf(g_fpLog,
			"f=%05d t=%.3f room=%s aud=%.6f occ=%.6f gain=%.6f "
			"px=%.4f py=%.4f pz=%.4f lx=%.4f ly=%.4f lz=%.4f "
			"yaw=%.3f st=0x%08lX pp=%lu\n",
			g_nFrame, t, g_Room[g_nRoom].pszName, fAud, fOcc, fGain,
			fx, fy, fz, g_fListener[0], g_fListener[1], g_fListener[2],
			g_fYaw, (unsigned long)dwStatus, (unsigned long)dwPlayPos);
	}
}

/////////////////////////////////////////////////////////////////////////////

//
static void DrawTextAt(float x, float y, const char* pszText)
{
	glRasterPos2f(x, y);
	glListBase(g_uFontBase);
	glCallLists((GLsizei)strlen(pszText), GL_UNSIGNED_BYTE, pszText);
}

static void DrawTextAt3f(float x, float y, float z, const char* pszText)
{
	glRasterPos3f(x, y, z);
	glListBase(g_uFontBase);
	glCallLists((GLsizei)strlen(pszText), GL_UNSIGNED_BYTE, pszText);
}

// One face: filled and translucent so the far side of a room can be seen from
// outside it, then outlined so the edges read.
static void DrawQuad(const float v[4][3], float r, float g, float b, float a)
{
	glEnable(GL_BLEND);
	glDepthMask(GL_FALSE);
	glColor4f(r, g, b, a);

	glBegin(GL_QUADS);
		for(int i = 0; i < 4; i++)
			glVertex3fv(v[i]);
	glEnd();

	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);

	glColor4f(r * 1.6f, g * 1.6f, b * 1.6f, 1.0f);
	glBegin(GL_LINE_LOOP);
		for(int i = 0; i < 4; i++)
			glVertex3fv(v[i]);
	glEnd();
}

// Scenery.  No polygon is submitted for it, so it occludes and reflects
// nothing; it is there so that distance can be read off the ground.
static void DrawGround(void)
{
	glColor3f(0.13f, 0.15f, 0.19f);
	glBegin(GL_QUADS);
		glVertex3f(-FLOOR_EXTENT, FLOOR_Y,  FLOOR_EXTENT);
		glVertex3f( FLOOR_EXTENT, FLOOR_Y,  FLOOR_EXTENT);
		glVertex3f( FLOOR_EXTENT, FLOOR_Y, -FLOOR_EXTENT);
		glVertex3f(-FLOOR_EXTENT, FLOOR_Y, -FLOOR_EXTENT);
	glEnd();

	glColor3f(0.22f, 0.26f, 0.32f);
	glBegin(GL_LINES);
		for(float x = -FLOOR_EXTENT; x <= FLOOR_EXTENT + 0.01f; x += 2.0f)
		{
			glVertex3f(x, FLOOR_Y + 0.01f, -FLOOR_EXTENT);
			glVertex3f(x, FLOOR_Y + 0.01f,  FLOOR_EXTENT);
		}
		for(float z = -FLOOR_EXTENT; z <= FLOOR_EXTENT + 0.01f; z += 2.0f)
		{
			glVertex3f(-FLOOR_EXTENT, FLOOR_Y + 0.01f, z);
			glVertex3f( FLOOR_EXTENT, FLOOR_Y + 0.01f, z);
		}
	glEnd();
}

static void DrawRoomGeometry(void)
{
	const SCENEROOM* p = &g_Room[g_nRoom];

	if(!g_bGeometry || p->nGeom == GEOM_OPEN)
		return;

	if(p->nGeom == GEOM_WALL)
	{
		float		hw = p->fGeomSize[0];
		float		h  = p->fGeomSize[1];
		float		z  = p->fGeomSize[2];
		const float	wall[4][3] =
		{
			{ -hw, FLOOR_Y, z }, {  hw, FLOOR_Y, z },
			{  hw, h,	z }, { -hw, h,	     z }
		};

		DrawQuad(wall, 0.55f, 0.42f, 0.20f, 0.55f);
		DrawTextAt3f(0.0f, h + 0.4f, z, "wall");
		return;
	}

	float x = p->fGeomSize[0];
	float y = p->fGeomSize[1];
	float z = p->fGeomSize[2];

	const float faces[6][4][3] =
	{
		{ { -x, FLOOR_Y, -z }, {  x, FLOOR_Y, -z }, {  x, y, -z }, { -x, y, -z } },
		{ {  x, FLOOR_Y,  z }, { -x, FLOOR_Y,  z }, { -x, y,  z }, {  x, y,  z } },
		{ { -x, FLOOR_Y,  z }, { -x, FLOOR_Y, -z }, { -x, y, -z }, { -x, y,  z } },
		{ {  x, FLOOR_Y, -z }, {  x, FLOOR_Y,  z }, {  x, y,  z }, {  x, y, -z } },
		{ { -x, FLOOR_Y,  z }, {  x, FLOOR_Y,  z }, {  x, FLOOR_Y, -z }, { -x, FLOOR_Y, -z } },
		{ { -x, y, -z }, {  x, y, -z }, {  x, y,  z }, { -x, y,  z } }
	};

	for(int i = 0; i < 6; i++)
		DrawQuad(faces[i], 0.22f, 0.30f, 0.40f, i == 5 ? 0.10f : 0.20f);
}

// The source: a sphere the colour of what the engine says about it, a stalk
// down to the ground so its height reads, and its name.
static void DrawSource(void)
{
	const SCENEROOM*	p = &g_Room[g_nRoom];
	float			fPos[3];
	char			szText[96];

	fPos[0] = g_fSourcePos[0];
	fPos[1] = g_fSourcePos[1];
	fPos[2] = g_fSourcePos[2];

	// A head-relative source is placed relative to the listener, so it is
	// drawn where the listener is, which is where it is heard from.
	if(p->dwTransformMode == A3DSOURCE_TRANSFORMMODE_HEADRELATIVE)
	{
		float fFwd[3], fRight[3];

		ForwardVector(g_fYaw, 0.0f, fFwd);
		fRight[0] = -fFwd[2];
		fRight[1] = 0.0f;
		fRight[2] = fFwd[0];

		fPos[0] = g_fListener[0] + fRight[0] * p->fBase[0] +
			  fFwd[0] * -p->fBase[2];
		fPos[1] = g_fListener[1] + p->fBase[1];
		fPos[2] = g_fListener[2] + fRight[2] * p->fBase[0] +
			  fFwd[2] * -p->fBase[2];
	}

	// Silent is the room's own colour at a quarter; audible drives it
	// towards full, so a loud source is a bright one.
	float k = g_fAudibility;
	if(k > 1.0f) k = 1.0f;
	if(k < 0.0f) k = 0.0f;

	float r = p->fColour[0] * (0.25f + 0.75f * k);
	float g = p->fColour[1] * (0.25f + 0.75f * k);
	float b = p->fColour[2] * (0.25f + 0.75f * k);

	glColor3f(r * 0.5f, g * 0.5f, b * 0.5f);
	glBegin(GL_LINES);
		glVertex3f(fPos[0], FLOOR_Y, fPos[2]);
		glVertex3f(fPos[0], fPos[1], fPos[2]);
	glEnd();

	// The line to the listener.  Red when the engine says it is occluded,
	// which in the occlusion room is the whole point.
	if(g_fOcclusion > 0.001f)
		glColor3f(1.0f, 0.25f, 0.20f);
	else
		glColor3f(r * 0.4f, g * 0.4f, b * 0.4f);

	glBegin(GL_LINES);
		glVertex3f(fPos[0], fPos[1], fPos[2]);
		glVertex3fv(g_fListener);
	glEnd();

	glPushMatrix();
		glTranslatef(fPos[0], fPos[1], fPos[2]);
		glColor3f(r, g, b);
		gluSphere(g_pQuadric, 0.30f + 0.5f * k, 16, 12);
	glPopMatrix();

	// Which way a directional source is pointing.
	if(p->bCone)
	{
		float w = 360.0f / (p->fPeriod > 0.0f ? p->fPeriod : 10.0f);
		float a = (w * (float)g_dwRoomFrame * (float)g_nTickMs / 1000.0f) *
			  0.017453293f;

		glColor3f(1.0f, 0.9f, 0.4f);
		glBegin(GL_LINES);
			glVertex3f(fPos[0], fPos[1], fPos[2]);
			glVertex3f(fPos[0] - (float)sin(a) * 4.0f, fPos[1],
				   fPos[2] - (float)cos(a) * 4.0f);
		glEnd();
	}

	glColor3f(0.85f, 0.88f, 0.92f);
	sprintf(szText, "%s   aud %.3f   occ %.3f", p->pszName, g_fAudibility,
		g_fOcclusion);
	DrawTextAt3f(fPos[0], fPos[1] + 0.9f, fPos[2], szText);
}

// The listener, in the views that are not through its own eyes.
static void DrawListener(void)
{
	float fFwd[3];

	ForwardVector(g_fYaw, 0.0f, fFwd);

	glPushMatrix();
		glTranslatef(g_fListener[0], g_fListener[1], g_fListener[2]);
		glColor3f(0.95f, 0.95f, 0.35f);
		gluSphere(g_pQuadric, 0.35f, 12, 10);
	glPopMatrix();

	glColor3f(0.95f, 0.95f, 0.35f);
	glBegin(GL_LINES);
		glVertex3fv(g_fListener);
		glVertex3f(g_fListener[0] + fFwd[0] * 2.0f,
			   g_fListener[1] + fFwd[1] * 2.0f,
			   g_fListener[2] + fFwd[2] * 2.0f);
	glEnd();
}

static void DrawHud(float t)
{
	const SCENEROOM*	p = &g_Room[g_nRoom];
	char			szLine[256];

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0.0, (double)g_nWidth, 0.0, (double)g_nHeight, -1.0, 1.0);

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	glDisable(GL_DEPTH_TEST);

	float y = (float)g_nHeight - 18.0f;

	glColor3f(0.55f, 0.85f, 1.00f);
	DrawTextAt(12.0f, y, g_szModule);
	y -= 20.0f;

	glColor3f(p->fColour[0], p->fColour[1], p->fColour[2]);
	sprintf(szLine, "room %d/%d  %s", g_nRoom + 1, (int)NUM_ROOMS, p->pszName);
	DrawTextAt(12.0f, y, szLine);
	y -= 16.0f;

	glColor3f(0.80f, 0.84f, 0.90f);
	sprintf(szLine, "listen for: %s", p->pszListenFor);
	DrawTextAt(12.0f, y, szLine);
	y -= 22.0f;

	sprintf(szLine, "audibility %6.3f    occlusion %6.3f    status 0x%08lX",
		g_fAudibility, g_fOcclusion, (unsigned long)g_dwStatus);
	DrawTextAt(12.0f, y, szLine);
	y -= 16.0f;

	sprintf(szLine, "frame %5lu  t %6.2f  %4.0f fps  %s%s", g_dwRoomFrame, t,
		g_fFps, g_bScripted ? "scripted" : "free", g_bPaused ? "  PAUSED" : "");
	DrawTextAt(12.0f, y, szLine);
	y -= 16.0f;

	sprintf(szLine, "listener (%6.2f,%6.2f,%6.2f) yaw %7.2f", g_fListener[0],
		g_fListener[1], g_fListener[2], g_fYaw);
	DrawTextAt(12.0f, y, szLine);
	y -= 16.0f;

	sprintf(szLine, "walls %-3s  occlusions %-3s  reflections %-3s%s",
		p->nGeom == GEOM_OPEN ? "n/a" : (g_bGeometry ? "on" : "off"),
		g_bOcclusions ? "on" : "off", g_bReflections ? "on" : "off",
		p->nReverbPreset == NO_PRESET ? "" :
		(g_bReverbOn ? "  reverb on" : "  reverb OFF"));
	DrawTextAt(12.0f, y, szLine);
	y -= 16.0f;

	glColor3f(g_lFailedCalls ? 1.0f : 0.55f, g_lFailedCalls ? 0.5f : 0.85f,
		  g_lFailedCalls ? 0.4f : 0.60f);
	sprintf(szLine, "calls %-9ld failed %-6ld", g_lCalls, g_lFailedCalls);
	DrawTextAt(12.0f, y, szLine);

	// The rooms, down the right, so what else there is to hear is visible.
	y = (float)g_nHeight - 38.0f;

	for(int i = 0; i < (int)NUM_ROOMS; i++)
	{
		if(i == g_nRoom)
			glColor3f(1.0f, 1.0f, 1.0f);
		else
			glColor3f(0.45f, 0.48f, 0.54f);

		sprintf(szLine, "%d %-13s", i + 1, g_Room[i].pszName);
		DrawTextAt((float)g_nWidth - 190.0f, y, szLine);
		y -= 15.0f;
	}

	glColor3f(0.55f, 0.58f, 0.64f);
	DrawTextAt(12.0f, 40.0f,
		   "WASD move   mouse look   space/ctrl up/down   shift faster"
		   "   tab release mouse");
	DrawTextAt(12.0f, 24.0f,
		   "PgUp/PgDn or 1-8 room   F1 view   F2 walls   F3 occlusions"
		   "   F4 reflections   F5 reverb   P pause   Esc quit");

	glEnable(GL_DEPTH_TEST);

	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
}

static void RenderScene(float t)
{
	glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(65.0, (double)g_nWidth / (double)(g_nHeight ? g_nHeight : 1),
		       0.1, 400.0);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	float fFwd[3];
	ForwardVector(g_fYaw, g_fPitch, fFwd);

	switch(g_nView)
	{
	case VIEW_CHASE:
		gluLookAt(g_fListener[0] - fFwd[0] * 9.0f, g_fListener[1] + 5.0f,
			  g_fListener[2] - fFwd[2] * 9.0f,
			  g_fListener[0], g_fListener[1], g_fListener[2],
			  0.0, 1.0, 0.0);
		break;

	case VIEW_PLAN:
		// Straight down, with -z at the top, which is the way the
		// listener starts out facing.
		gluLookAt(0.0, 45.0, 0.01, 0.0, 0.0, 0.0, 0.0, 0.0, -1.0);
		break;

	case VIEW_FIRSTPERSON:
	default:
		gluLookAt(g_fListener[0], g_fListener[1], g_fListener[2],
			  g_fListener[0] + fFwd[0], g_fListener[1] + fFwd[1],
			  g_fListener[2] + fFwd[2], 0.0, 1.0, 0.0);
		break;
	}

	DrawGround();
	DrawRoomGeometry();
	DrawSource();

	if(g_nView != VIEW_FIRSTPERSON)
		DrawListener();

	DrawHud(t);

	SwapBuffers(g_hDC);
}

/////////////////////////////////////////////////////////////////////////////
// The window.
//
static void CentreMouse(void)
{
	RECT rc;

	GetClientRect(g_hWnd, &rc);

	POINT pt;
	pt.x = rc.right / 2;
	pt.y = rc.bottom / 2;

	ClientToScreen(g_hWnd, &pt);
	SetCursorPos(pt.x, pt.y);
}

static void SetMouseLook(BOOL bOn)
{
	g_bMouseLook = bOn;

	ShowCursor(!bOn);

	if(bOn)
	{
		SetCapture(g_hWnd);
		CentreMouse();
	}
	else
	{
		ReleaseCapture();
	}
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch(uMsg)
	{
	case WM_CLOSE:
	case WM_DESTROY:
		g_bRunning = FALSE;
		return 0;

	case WM_SIZE:
		g_nWidth  = LOWORD(lParam);
		g_nHeight = HIWORD(lParam);

		if(g_nHeight == 0)
			g_nHeight = 1;

		glViewport(0, 0, g_nWidth, g_nHeight);
		return 0;

	case WM_ACTIVATE:
		if(LOWORD(wParam) == WA_INACTIVE && g_bMouseLook)
			SetMouseLook(FALSE);
		return 0;

	case WM_LBUTTONDOWN:
		if(!g_bMouseLook)
			SetMouseLook(TRUE);
		return 0;

	case WM_KEYDOWN:
		g_abKey[wParam & 0xFF] = TRUE;

		// A scripted run is walking the rooms to a timetable, so the
		// keys that would move it off that are not taken.
		switch(wParam)
		{
		case VK_ESCAPE:
			g_bRunning = FALSE;
			break;

		case VK_TAB:
			SetMouseLook(!g_bMouseLook);
			break;

		case VK_F1:
			g_nView = (g_nView + 1) % VIEW_COUNT;
			break;

		case VK_F2:
			g_bGeometry = !g_bGeometry;
			break;

		case VK_F3:
			g_bOcclusions = !g_bOcclusions;
			CALL("Geom::Enable/Disable(A3D_OCCLUSIONS)",
			     g_bOcclusions ? g_pGeom->Enable(A3D_OCCLUSIONS)
					   : g_pGeom->Disable(A3D_OCCLUSIONS));
			break;

		case VK_F4:
			g_bReflections = !g_bReflections;
			CALL("Geom::Enable/Disable(A3D_1ST_REFLECTIONS)",
			     g_bReflections ? g_pGeom->Enable(A3D_1ST_REFLECTIONS)
					    : g_pGeom->Disable(A3D_1ST_REFLECTIONS));
			break;

		// IA3d5 toggles reverb through BindReverb; Enable(A3D_REVERB) is unsupported.
// CA3dRoot::BindReverb also updates the emulation state.
		case VK_F5:
			if(g_pReverb)
			{
				g_bReverbOn = !g_bReverbOn;
				CALL("BindReverb",
				     g_pA3d->BindReverb(g_bReverbOn ? g_pReverb : NULL));
			}
			break;

		case 'P':
			g_bPaused = !g_bPaused;
			break;

		case VK_PRIOR:
			if(!g_bScripted)
				GoToRoom(g_nRoom - 1);
			break;

		case VK_NEXT:
			if(!g_bScripted)
				GoToRoom(g_nRoom + 1);
			break;

		default:
			if(!g_bScripted && wParam >= '1' &&
			   wParam <= '0' + (int)NUM_ROOMS)
				GoToRoom((int)wParam - '1');
			break;
		}

		return 0;

	case WM_KEYUP:
		g_abKey[wParam & 0xFF] = FALSE;
		return 0;
	}

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

static BOOL CreateSceneWindow(void)
{
	WNDCLASSA wc;

	memset(&wc, 0, sizeof(wc));
	wc.style	 = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc	 = WndProc;
	wc.hInstance	 = GetModuleHandle(NULL);
	wc.hCursor	 = LoadCursor(NULL, IDC_ARROW);
	wc.lpszClassName = "SceneRoomsWindow";

	if(!RegisterClassA(&wc))
		return FALSE;

	char szTitle[MAX_PATH + 64];
	sprintf(szTitle, "SceneRooms - %s", g_szModule);

	RECT rc;
	rc.left	  = 0;
	rc.top	  = 0;
	rc.right  = g_nWidth;
	rc.bottom = g_nHeight;
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

	g_hWnd = CreateWindowA("SceneRoomsWindow", szTitle, WS_OVERLAPPEDWINDOW,
			       CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left,
			       rc.bottom - rc.top, NULL, NULL, wc.hInstance, NULL);
	if(!g_hWnd)
		return FALSE;

	g_hDC = GetDC(g_hWnd);

	PIXELFORMATDESCRIPTOR pfd;

	memset(&pfd, 0, sizeof(pfd));
	pfd.nSize	 = sizeof(pfd);
	pfd.nVersion	 = 1;
	pfd.dwFlags	 = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType	 = PFD_TYPE_RGBA;
	pfd.cColorBits	 = 32;
	pfd.cDepthBits	 = 24;
	pfd.iLayerType	 = PFD_MAIN_PLANE;

	int nFormat = ChoosePixelFormat(g_hDC, &pfd);
	if(!nFormat || !SetPixelFormat(g_hDC, nFormat, &pfd))
	{
		printf("no pixel format with a depth buffer\n");
		return FALSE;
	}

	g_hRC = wglCreateContext(g_hDC);
	if(!g_hRC || !wglMakeCurrent(g_hDC, g_hRC))
	{
		printf("wglCreateContext failed\n");
		return FALSE;
	}

	// The HUD and the in-world labels are a bitmap font out of the DC,
	// which is what an OpenGL program of this era did for text.
	HFONT hFont = CreateFontA(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
				  ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
				  ANTIALIASED_QUALITY, FF_DONTCARE | FIXED_PITCH,
				  "Consolas");
	SelectObject(g_hDC, hFont);

	g_uFontBase = glGenLists(256);
	wglUseFontBitmaps(g_hDC, 0, 256, g_uFontBase);

	g_pQuadric = gluNewQuadric();

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_LINE_SMOOTH);
	glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

	glViewport(0, 0, g_nWidth, g_nHeight);

	ShowWindow(g_hWnd, SW_SHOW);
	SetForegroundWindow(g_hWnd);
	SetFocus(g_hWnd);

	if(!g_bScripted)
		SetMouseLook(TRUE);

	return TRUE;
}

static void DestroySceneWindow(void)
{
	if(g_pQuadric)
	{
		gluDeleteQuadric(g_pQuadric);
		g_pQuadric = NULL;
	}

	if(g_hRC)
	{
		wglMakeCurrent(NULL, NULL);
		wglDeleteContext(g_hRC);
		g_hRC = NULL;
	}

	if(g_hDC && g_hWnd)
	{
		ReleaseDC(g_hWnd, g_hDC);
		g_hDC = NULL;
	}

	if(g_hWnd)
	{
		DestroyWindow(g_hWnd);
		g_hWnd = NULL;
	}
}

// Walking.  The listener is the camera, so this moves both.
static void MoveListener(float fSeconds)
{
	float fFwd[3];
	float fSpeed = g_abKey[VK_SHIFT] ? 14.0f : 5.0f;

	ForwardVector(g_fYaw, 0.0f, fFwd);

	float fRight[3];
	fRight[0] = -fFwd[2];
	fRight[1] = 0.0f;
	fRight[2] = fFwd[0];

	float fStep = fSpeed * fSeconds;

	if(g_abKey['W']) { g_fListener[0] += fFwd[0] * fStep;   g_fListener[2] += fFwd[2] * fStep; }
	if(g_abKey['S']) { g_fListener[0] -= fFwd[0] * fStep;   g_fListener[2] -= fFwd[2] * fStep; }
	if(g_abKey['D']) { g_fListener[0] += fRight[0] * fStep; g_fListener[2] += fRight[2] * fStep; }
	if(g_abKey['A']) { g_fListener[0] -= fRight[0] * fStep; g_fListener[2] -= fRight[2] * fStep; }

	if(g_abKey[VK_SPACE])	g_fListener[1] += fStep;
	if(g_abKey[VK_CONTROL])	g_fListener[1] -= fStep;

	if(g_fListener[1] < FLOOR_Y + 0.5f)
		g_fListener[1] = FLOOR_Y + 0.5f;

	// Mouse look.  The cursor is put back in the middle every frame, so how
	// far it moved is how far to turn.
	if(g_bMouseLook)
	{
		RECT	rc;
		POINT	pt;

		GetClientRect(g_hWnd, &rc);
		GetCursorPos(&pt);
		ScreenToClient(g_hWnd, &pt);

		int dx = pt.x - rc.right / 2;
		int dy = pt.y - rc.bottom / 2;

		if(dx || dy)
		{
			// Decreasing the angle turns to the right, because
			// Rotate3f turns by the right-hand rule about +y.
			g_fYaw	 -= dx * 0.15f;
			g_fPitch -= dy * 0.15f;

			if(g_fPitch >  89.0f) g_fPitch =  89.0f;
			if(g_fPitch < -89.0f) g_fPitch = -89.0f;

			while(g_fYaw >  180.0f) g_fYaw -= 360.0f;
			while(g_fYaw < -180.0f) g_fYaw += 360.0f;

			CentreMouse();
		}
	}
}

/////////////////////////////////////////////////////////////////////////////
// Fixed timestep keeps scripted API calls and rendering deterministic.
//
static void RunScene(void)
{
	DWORD	dwStart	   = timeGetTime();
	DWORD	dwFps	   = dwStart;
	int	nFpsFrames = 0;
	float	t	   = 0.0f;

	g_pszStage = "RunScene";

	for(g_nFrame = 0; g_bRunning; g_nFrame++)
	{
		MSG msg;

		while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		if(!g_bRunning)
			break;

		// A scripted run gives every room the same number of frames and
		// then stops.
		if(g_bScripted && (int)g_dwRoomFrame >= g_nFrames)
		{
			if(g_nRoom + 1 >= (int)NUM_ROOMS)
				break;

			GoToRoom(g_nRoom + 1);
		}

		if(!g_bPaused)
			t = (float)g_dwRoomFrame * (float)g_nTickMs / 1000.0f;

		if(g_bScripted)
		{
			EvalListener(g_nRoom, t, g_fListener, &g_fYaw);
		}
		else
		{
			MoveListener((float)g_nTickMs / 1000.0f);
		}

		// The reverb room, and only it, walks the presets.  On a frame
		// boundary, so two runs change it in the same place.
		if(g_pReverb && g_Room[g_nRoom].bWalkPresets)
		{
			int nHold = (int)(t / REVERB_HOLD_SECONDS) %
				    (int)NUM_REVERB_PRESETS;

			if(nHold != g_nPreset)
			{
				g_nPreset = nHold;
				CALL("Reverb::SetReverbPreset",
				     g_pReverb->SetReverbPreset(g_ReverbScript[nHold]));
			}
		}

		RunAudioFrame(t);
		// Wait for geometry to settle. Reference runs differed by 1.0 in the
// occlusion room's first sample; later samples matched.
		ReadBack(t, (int)g_dwRoomFrame >= g_nSettle &&
			    (g_dwRoomFrame % (DWORD)g_nEvery) == 0);
		RenderScene(t);

		if(!g_bPaused)
			g_dwRoomFrame++;

		nFpsFrames++;

		if(timeGetTime() - dwFps >= 500)
		{
			g_fFps = nFpsFrames * 1000.0f / (float)(timeGetTime() - dwFps);
			dwFps = timeGetTime();
			nFpsFrames = 0;
		}

		// Paced off the start of the run rather than off the last frame,
		// so a slow frame does not push the rest along with it.
		DWORD dwDue = dwStart + (DWORD)(g_nFrame + 1) * g_nTickMs;
		DWORD dwNow = timeGetTime();

		if((long)(dwDue - dwNow) > 0)
			Sleep(dwDue - dwNow);
	}
}

/////////////////////////////////////////////////////////////////////////////

//
static void PrintSummary(void)
{
	printf("\n");
	printf("================================================================\n");
	printf("scene summary\n");
	printf("================================================================\n");
	printf("server        : %s\n", g_szModule);
	printf("frames        : %d at %d ms, sampled every %d after a %d frame"
	       " settle, %s\n", g_nFrame, g_nTickMs, g_nEvery, g_nSettle,
	       g_bScripted ? "scripted" : "free");
	printf("calls         : %ld, of which failed %ld\n", g_lCalls, g_lFailedCalls);
	printf("\n");
	printf(" room          samples   aud mean   aud min   aud max   "
	       "occ mean   occ min   occ max   status    playpos\n");

	for(int i = 0; i < (int)NUM_ROOMS; i++)
	{
		if(g_Stat[i].nSamples == 0)
		{
			printf(" %-13s       0   (not entered)\n", g_Room[i].pszName);
			continue;
		}

		printf(" %-13s %7d  %8.5f  %8.5f  %8.5f  %8.5f  %8.5f  %8.5f"
		       "  0x%08lX %10lu\n",
		       g_Room[i].pszName, g_Stat[i].nSamples,
		       g_Stat[i].dAudSum / g_Stat[i].nSamples, g_Stat[i].fAudMin,
		       g_Stat[i].fAudMax, g_Stat[i].dOccSum / g_Stat[i].nSamples,
		       g_Stat[i].fOccMin, g_Stat[i].fOccMax,
		       (unsigned long)g_Stat[i].dwStatusSeen,
		       (unsigned long)g_Stat[i].dwLastPlayPos);
	}

	if(g_nFailures)
	{
		printf("\nfailures, first one of each:\n");

		for(int i = 0; i < g_nFailures; i++)
		{
			printf("  %-40s hr=0x%08lX  %s frame %-6d x%d\n",
			       g_Failure[i].pszWhat, (unsigned long)g_Failure[i].hr,
			       g_Room[g_Failure[i].nRoom].pszName,
			       g_Failure[i].nFrame, g_Failure[i].nCount);
		}

		if(g_nFailures == MAX_FAILURES)
			printf("  (the table is full; there may be more)\n");
	}
	else
	{
		printf("\nno call failed.\n");
	}

	if(g_bScripted)
	{
		printf("\nEverything above except play position and status"
		       " repeats between two runs,\n");
		printf("as long as the first %d frames of each room are left"
		       " out, which is what\n", g_nSettle);
		printf("--settle does.  Compare this against another run with"
		       "\n\n");
		printf("    powershell -File scripts\\Compare-SceneLog.ps1"
		       " <other>.log <this>.log\n");
	}
	else
	{
		printf("\nA free run is walked by hand and lines up against"
		       " nothing.  Use --script\n");
		printf("for a run that can be compared.\n");
	}

	if(g_fpLog)
		fprintf(g_fpLog, "# calls=%ld failed=%ld\n", g_lCalls, g_lFailedCalls);
}

//
// Shutdown destroys the root and invalidates its child interfaces, matching
// SDK AudioExit. Do not Release them afterwards: this caused a stale-pointer
// call with a3dapi_33_rtl.dll.
//
static void DestroyScene(void)
{
	for(int i = 0; i < (int)NUM_ROOMS; i++)
	{
		if(g_pSource[i])
		{
			g_pszStage = "Source::Stop";
			g_pSource[i]->Stop();
		}
	}

	if(g_pA3d)
	{
		g_pszStage = "Shutdown";
		g_pA3d->Shutdown();
	}

	// Clear invalidated pointers without releasing them.
	for(int i = 0; i < (int)NUM_ROOMS; i++)
		g_pSource[i] = NULL;

	g_pMaterial	= NULL;
	g_pReverb	= NULL;
	g_pListener	= NULL;
	g_pGeom		= NULL;
	g_pA3d		= NULL;

	g_pszStage = "torn down";
}

/////////////////////////////////////////////////////////////////////////////
// Command line.
//
static void Usage(void)
{
	printf(
"SceneRooms - eight rooms, one source in each, one thing happening in it,\n"
"             drawn in OpenGL and driven through IA3d5.\n"
"\n"
"  --dll <what>     ref   ref\\a3dapi_33_rtl.dll, Aureal's 3.3.677 Retail\n"
"                   678   ref\\a3dapi_33_678.dll, the driver build\n"
"                   dbg   ref\\a3dapi_33_dbg.dll, Aureal's Debug build\n"
"                   ours  build\\Release\\a3dapi.dll, this tree\n"
"                   reg   whatever CLSID_A3dApi is registered against\n"
"                   <path to any a3dapi.dll>\n"
"  --room <name>    start in that room rather than the first\n"
"  --script         walk every room in turn on a fixed path\n"
"  --frames N       frames a scripted run spends in each room, default 500\n"
"  --tick MS        milliseconds a frame, default 20\n"
"  --every N        sample every N frames, default 25\n"
"  --settle N       take no sample in a room's first N frames, default 50\n"
"  --log FILE       write the sampled state to FILE, for diffing\n"
"  --data DIR       where the wave files are\n"
"  --size W H       window size, default 1024 640\n"
"  --noreverb       do not create or bind a reverb\n"
"  --verbose        print every failing call as it happens\n"
"\n"
"The rooms:\n"
"\n");

	for(int i = 0; i < (int)NUM_ROOMS; i++)
		printf("  %d %-13s %s\n", i + 1, g_Room[i].pszName,
		       g_Room[i].pszListenFor);

	printf(
"\n"
"Walk one room against one DLL, then the other, and listen:\n"
"\n"
"    SceneRooms --dll ref  --room occlusion\n"
"    SceneRooms --dll ours --room occlusion\n"
"\n"
"Or walk every room on the same fixed path against both and compare:\n"
"\n"
"    SceneRooms --dll ref  --script --log ref.log\n"
"    SceneRooms --dll ours --script --log ours.log\n"
"    powershell -File scripts\\Compare-SceneLog.ps1 ref.log ours.log\n");
}

static const char* ResolveDll(const char* pszWhat)
{
	if(!strcmp(pszWhat, "ref") || !strcmp(pszWhat, "vanilla") ||
	   !strcmp(pszWhat, "orig"))
		return FindNearby("ref\\a3dapi_33_rtl.dll");

	if(!strcmp(pszWhat, "678"))
		return FindNearby("ref\\a3dapi_33_678.dll");

	if(!strcmp(pszWhat, "dbg"))
		return FindNearby("ref\\a3dapi_33_dbg.dll");

	if(!strcmp(pszWhat, "ours") || !strcmp(pszWhat, "recon") ||
	   !strcmp(pszWhat, "reverse"))
	{
		// Beside this executable first: that is where the build puts it,
		// and it is what "ours" means during a build-and-run.
		if(GetFileAttributesA("a3dapi.dll") != INVALID_FILE_ATTRIBUTES)
			return "a3dapi.dll";

		return FindNearby("build\\Release\\a3dapi.dll");
	}

	return pszWhat;
}

static int FindRoom(const char* pszName)
{
	for(int i = 0; i < (int)NUM_ROOMS; i++)
	{
		if(!_stricmp(pszName, g_Room[i].pszName))
			return i;
	}

	return -1;
}

static BOOL ParseArgs(int argc, char** argv)
{
	for(int i = 1; i < argc; i++)
	{
		if(!strcmp(argv[i], "--dll") && i + 1 < argc)
			g_pszDll = argv[++i];
		else if(!strcmp(argv[i], "--room") && i + 1 < argc)
			g_pszRoom = argv[++i];
		else if(!strcmp(argv[i], "--frames") && i + 1 < argc)
			g_nFrames = atoi(argv[++i]);
		else if(!strcmp(argv[i], "--tick") && i + 1 < argc)
			g_nTickMs = atoi(argv[++i]);
		else if(!strcmp(argv[i], "--every") && i + 1 < argc)
			g_nEvery = atoi(argv[++i]);
		else if(!strcmp(argv[i], "--settle") && i + 1 < argc)
			g_nSettle = atoi(argv[++i]);
		else if(!strcmp(argv[i], "--log") && i + 1 < argc)
			g_pszLog = argv[++i];
		else if(!strcmp(argv[i], "--data") && i + 1 < argc)
			g_pszData = argv[++i];
		else if(!strcmp(argv[i], "--size") && i + 2 < argc)
		{
			g_nWidth  = atoi(argv[++i]);
			g_nHeight = atoi(argv[++i]);
		}
		else if(!strcmp(argv[i], "--script"))
			g_bScripted = TRUE;
		else if(!strcmp(argv[i], "--noreverb"))
			g_bReverb = FALSE;
		else if(!strcmp(argv[i], "--request-reverb"))
			g_bRequestReverb = TRUE;
		else if(!strcmp(argv[i], "--verbose"))
			g_bVerbose = TRUE;
		else if(!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h"))
		{
			Usage();
			return FALSE;
		}
		else
		{
			printf("unknown option: %s\n\n", argv[i]);
			Usage();
			return FALSE;
		}
	}

	if(g_nTickMs < 1)
		g_nTickMs = 1;
	if(g_nEvery < 1)
		g_nEvery = 1;
	if(g_nSettle < 0)
		g_nSettle = 0;
	if(g_nFrames < 1)
		g_nFrames = 1;
	if(g_nWidth < 320)
		g_nWidth = 320;
	if(g_nHeight < 240)
		g_nHeight = 240;

	// A log only lines up against another log if the run was scripted.
	if(g_pszLog)
		g_bScripted = TRUE;

	if(g_pszRoom && FindRoom(g_pszRoom) < 0)
	{
		printf("no room called \"%s\".  There are:\n\n", g_pszRoom);

		for(int i = 0; i < (int)NUM_ROOMS; i++)
			printf("  %s\n", g_Room[i].pszName);

		return FALSE;
	}

	// A scripted run walks every room in order, so it starts at the first.
	if(g_bScripted)
		g_pszRoom = NULL;

	return TRUE;
}

int main(int argc, char** argv)
{
	if(!ParseArgs(argc, argv))
		return 1;

	// Disable buffering so redirected logs retain the last completed step
// when a DLL crashes or hangs.
	setvbuf(stdout, NULL, _IONBF, 0);

	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
		     SEM_NOOPENFILEERRORBOX);

	AddVectoredExceptionHandler(1, FaultFilter);

	CoInitialize(NULL);

	HRESULT hr;

	if(!strcmp(g_pszDll, "reg"))
	{
		hr = CoCreateInstance(CLSID_A3dApi, NULL, CLSCTX_INPROC_SERVER,
				      IID_IA3d5, (void**)&g_pA3d);
		if(FAILED(hr))
		{
			printf("CoCreateInstance(CLSID_A3dApi) failed, hr=0x%08lX\n",
			       (unsigned long)hr);
			printf("Register one with scripts\\Set-A3dApiServer.ps1, "
			       "or name a file with --dll.\n");
			return 1;
		}
	}
	else
	{
		hr = CreateA3dFromPath(ResolveDll(g_pszDll), &g_pA3d);
		if(FAILED(hr))
		{
			printf("could not create the object out of %s, hr=0x%08lX\n",
			       ResolveDll(g_pszDll), (unsigned long)hr);
			return 1;
		}
	}

	NoteServingModule();
	PrintModuleIdentity();

	//
// Use InitEx with GetDesktopWindow: passing the console HWND stalled both
// DLLs in SetCooperativeLevel, and the scene window does not exist yet.
// See tools/probes/sdk_loading/main.cpp.
	//
// Omit A3D_REVERB by default because requesting it hung both DLLs in testing.
// NewReverb then returns A3DERROR_FEATURE_NOT_REQUESTED. --request-reverb
// supports SoftwareReverb in A3D_FIXES builds, whose property-set implementation
// avoids that device call; reference/default builds can still hang.
	//
// Disable the splash and focus-loss muting for concurrent comparisons.
	//
	printf("init   : ");

	hr = CALL("InitEx", g_pA3d->InitEx(NULL,
					   A3D_1ST_REFLECTIONS | A3D_OCCLUSIONS |
					   A3D_DISABLE_SPLASHSCREEN |
					   A3D_DISABLE_FOCUS_MUTE |
					   (g_bRequestReverb ? A3D_REVERB : 0),
					   A3DRENDERPREFS_DEFAULT,
					   GetDesktopWindow(), A3D_CL_NORMAL));
	if(FAILED(hr))
	{
		printf("InitEx failed, hr=0x%08lX\n", (unsigned long)hr);

		// The samples' path when their init fails is AudioExit(), which
		// is Shutdown() on a root that was never initialised.
		g_pA3d->Shutdown();
		return 1;
	}

	printf("ok\n");

	g_pszStage = "CreateScene";

	hr = CreateScene();
	if(FAILED(hr))
	{
		printf("the scene did not come up, hr=0x%08lX\n", (unsigned long)hr);
		DestroyScene();
		return 1;
	}

	if(g_pszLog)
	{
		g_fpLog = fopen(g_pszLog, "w");

		if(!g_fpLog)
		{
			printf("could not write %s\n", g_pszLog);
		}
		else
		{
			fprintf(g_fpLog, "# SceneRooms log\n");
			fprintf(g_fpLog, "# server=%s\n", g_szModule);
			fprintf(g_fpLog, "# rooms=%d frames=%d tick=%d every=%d "
					 "settle=%d reverb=%d\n",
				(int)NUM_ROOMS, g_nFrames, g_nTickMs, g_nEvery,
				g_nSettle, g_bReverb);
		}
	}

	g_pszStage = "CreateSceneWindow";

	if(!CreateSceneWindow())
	{
		printf("could not open the scene window\n");
		DestroyScene();
		return 1;
	}

	printf("window : %dx%d, OpenGL %s\n", g_nWidth, g_nHeight,
	       (const char*)glGetString(GL_VERSION));

	EnterRoom(g_pszRoom ? FindRoom(g_pszRoom) : 0);

	printf("\nrunning.  %s  Esc closes the window.\n",
	       g_bScripted ? "Walking every room in turn."
			   : "PgUp and PgDn move between rooms.");

	RunScene();

	g_pszStage = "PrintSummary";
	PrintSummary();

	if(g_fpLog)
		fclose(g_fpLog);

	g_pszStage = "DestroySceneWindow";
	DestroySceneWindow();

	g_pszStage = "DestroyScene";
	DestroyScene();

	g_pszStage = "CoUninitialize";
	CoUninitialize();

	g_pszStage = "exit";


	return g_lFailedCalls ? 3 : 0;
}
