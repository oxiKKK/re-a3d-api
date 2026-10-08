// Project-added A3D development tooling.
#pragma once
#include <windows.h>
#include <mmsystem.h>
#include <objbase.h>
#include <dsound.h>
#include "ia3dapi.h"
#include "ia3ddal.h"

namespace a3dcapture {
#define CAPTURE_MAX_BUFFER_COUNT 32
#define CAPTURE_MAX_STREAM_BYTES (16 * 1024 * 1024)
#define CAPTURE_KEEP_RENDER_MODE 0xFFFFFFFF
typedef struct CapturedBuffer
{
	DWORD		dwBufferBytes;
	DWORD		dwFlags;
	DWORD		dwSamplesPerSec;
	WORD		nChannels;
	WORD		wBitsPerSample;
	long		nLocks;
	long		nUnlocks;
	bool		playing;
	long		playingUnlocks;
	BYTE		*pStream;
	DWORD		cbStream;
	int		fOverflow;	/* the stream hit CAPTURE_MAX_STREAM_BYTES */
} CapturedBuffer;
struct CaptureState {
	CRITICAL_SECTION lock;
	CapturedBuffer	buffers[CAPTURE_MAX_BUFFER_COUNT];
	int		buffer_count;

	long		buffer_creation_count;		/* CreateSoundBuffer calls */
	long		lock_count;
	long		unlock_count;
	long		nonempty_unlock_count;	/* Unlock calls with cb > 0 */
	long		position_query_count;		/* GetCurrentPosition, one per mix pass */
	long		play_count;		/* Play on a captured buffer */
	long		statistics_enabled;	/* Enabled after warmup. */
	long		stream_started;		/* 1 once the scene is set up */
	long		stream_finished;		/* Capture interval ended. */
	unsigned __int64	sample_count;		/* int16 samples summed */
	unsigned __int64	absolute_sample_sum;		/* sum of abs(sample) */
	unsigned __int64	squared_sample_sum;		/* sum of sample*sample */
	long		peak_amplitude;		/* peak abs(sample) */
	unsigned __int64	byte_count;		/* bytes Unlock handed back */


};
extern CaptureState capture_state;
typedef struct CaptureScene
{
	const char	*pszName;
	DWORD		dwRenderMode;
	int		fPosition;
	A3DVAL		afPos[3];
	A3DVAL		fGain;
	A3DVAL		fOutputGain;
	A3DVAL		fPitch;		/* Zero preserves the default pitch. */
	int		fPan;
	A3DVAL		afPan[2];
	A3DVAL		afVelocity[3];	/* All zero preserves the default velocity. */
	A3DVAL		fEq;		/* Zero preserves the default EQ. */
	A3DVAL		afMinMax[2];	/* Zero maximum preserves the default distances. */
	int		fListener;	/* Place the listener through IA3dListener. */
	A3DVAL		afListenerPos[3];
	A3DVAL		fListenerYaw;	/* Degrees. */
} CaptureScene;
#define CAPTURE_SCENE_COUNT 11
extern const CaptureScene capture_scenes[CAPTURE_SCENE_COUNT];
IUnknown* RecordingClassFactory();
void AccumRegion(const void*, DWORD);
void StreamStats(const CapturedBuffer*, long*, unsigned __int64*, unsigned __int64*, unsigned __int64*);
int WriteWav(const char*, const CapturedBuffer*);
void RecordCaptureFault(DWORD code);
int RunCapture(int argc, char** argv);
} // namespace a3dcapture
