/////////////////////////////////////////////////////////////////////////////
// A3dReflectionEmu.h
// ===================
//
// NOT PART OF THE ORIGINAL.  Compiled in only when A3D_FIXES is
// on. Reverb processing has its own independent option.
//
// Declares the per-voice software reflection processor and its tap state.
// Each tap contains resampling and mixing state plus a stereo delay ring
// used to add a reflected path to the software output.
//
// Effect state is kept outside the reconstructed A2DBuffer layout and
// retrieved through A3dReflectionEmuGetVoice. A3dReflectionEmu.cpp performs
// the processing, and dal_a2d.cpp calls it while mixing active voices.
/////////////////////////////////////////////////////////////////////////////

#ifndef _A3DREFLECTIONEMU_H
#define _A3DREFLECTIONEMU_H

#include "A3dPrivate.h"

#if defined(A3D_FIXES)

#include "softmix.h"

class A2DBuffer;
class CHrtfMgr;

/* Maximum delay in seconds; independent of SetMaxReflectionDelayTime. */
#define A3D_REFLECTIONEMU_MAX_DELAY_S 0.5f

typedef struct
{
	int         bActive;
	A3DMIXPLAY  play;
	A3DMIXSTATE mix;
	float      *pfDelayLine;  /* interleaved stereo ring, cDelayFrames long */
	int         cDelayFrames; /* Stereo frame capacity. */
	int         nWritePos;    /* Next frame in the delay ring. */
} A3DREFLECTIONEMU_TAP;

/* =============================================================
// Class: CA3dReflectionEmuVoice
//
// Description: Software reflection state stored outside the A2DBuffer ABI.
// =============================================================*/

class CA3dReflectionEmuVoice
{
public:
	CA3dReflectionEmuVoice(void);
	~CA3dReflectionEmuVoice(void);

	void Process(A2DBuffer *pVoice, float *pMix, DWORD cFrames,
	             DWORD dwSampleRate);

private:
	A3DREFLECTIONEMU_TAP m_aTap[A3D_MAX_SOURCE_REFLECTIONS];
};

/* Entries persist after voice destruction. */
CA3dReflectionEmuVoice *A3dReflectionEmuGetVoice(A2DBuffer *pVoice);

#endif /* A3D_FIXES */

#endif /* _A3DREFLECTIONEMU_H */
