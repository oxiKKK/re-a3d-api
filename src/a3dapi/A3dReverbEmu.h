/////////////////////////////////////////////////////////////////////////////
// A3dReverbEmu.h
// ==============
//
// NOT PART OF THE ORIGINAL.  Compiled in only when A3D_FIXES is
// on; a from-scratch I3DL2-style software reverb driven by CA3dReverb's own
// numbers, standing in for the Vortex hardware DSP nothing here reconstructs.
//
// Declares the per-channel delay filters, shared stereo reverb engine and
// property-set adapter used by the optional software mixer path. Channel
// state owns the delay storage; engine state coordinates configuration and
// processing, and the adapter accepts reverb property updates.
//
// A3dReverbEmu.cpp implements filtering and property handling. DAL_A2D calls
// the engine on the complete stereo mix, using a fixed wet level without
// per-source sends. CA3dReverb remains the API's separate parameter object.
/////////////////////////////////////////////////////////////////////////////

#ifndef _A3DREVERBEMU_H
#define _A3DREVERBEMU_H

#include "A3dPrivate.h"

#if defined(A3D_FIXES)

/* Four damped comb filters followed by two allpass stages. */

#define A3D_REVERB_EMU_NUM_COMBS   4
#define A3D_REVERB_EMU_NUM_ALLPASS 2

typedef struct
{
	float *pfLine;  /* Owned delay samples. */
	int   cLine;  /* Sample capacity. */
	int   nPos;   /* Current delay-line sample. */
	float fFeedback;
	float fDampState;
	float fDamping;
} A3DREVERBEMU_COMB;

typedef struct
{
	float *pfLine;  /* Owned delay samples. */
	int   cLine;  /* Sample capacity. */
	int   nPos;   /* Current delay-line sample. */
	float fFeedback;
} A3DREVERBEMU_ALLPASS;

/* =============================================================
// Class: CA3dReverbEmuChannel
//
// Description: Mono reverb filters with deferred parameter configuration.
// =============================================================*/

class CA3dReverbEmuChannel
{
public:
	CA3dReverbEmuChannel(void);
	~CA3dReverbEmuChannel(void);

	void  Init(DWORD dwSampleRate, int nChannelOffset);
	void  Configure(float fDecayTime, float fDamping);
	float Process(float fIn);

private:
	void ApplyFeedback(void);

	A3DREVERBEMU_COMB    m_aComb[A3D_REVERB_EMU_NUM_COMBS];
	A3DREVERBEMU_ALLPASS m_aAllpass[A3D_REVERB_EMU_NUM_ALLPASS];

	/* Requested parameters, retained before delay-line initialization. */
	float m_fDecayTime; /* Seconds. */
	float m_fDamping;
	int   m_fHaveRequest;
};

/* =============================================================
// Class: CA3dReverbEmuEngine
//
// Description: Shared stereo reverb engine for the DAL_A2D mix thread.
// =============================================================*/

class CA3dReverbEmuEngine
{
public:
	CA3dReverbEmuEngine(void);
	~CA3dReverbEmuEngine(void);

	void Configure(DWORD dwEnvPreset, float fVolume, float fDecayTime,
	               float fDamping);
	void Process(float *pflInterleavedStereo, DWORD cFrames,
	             DWORD dwSampleRate);

	void SetEnabled(int fEnabled);

private:
	CA3dReverbEmuChannel m_Left;
	CA3dReverbEmuChannel m_Right;
	DWORD                m_dwSampleRate;
	float                m_fWetMix; /* Fixed wet-signal gain. */
	volatile LONG        m_lConfigured;
	volatile LONG        m_lEnabled;
	CRITICAL_SECTION     m_cs;      /* Serializes configuration and mixing. */
};

CA3dReverbEmuEngine *A3dReverbEmuGetEngine(void);

/* =============================================================
// Class: CA3dReverbEmuPropertySet
//
// Description: Driver property-set adapter for software reverb.
// =============================================================*/

class CA3dReverbEmuPropertySet : public IA3dPropertySet
{
public:
	CA3dReverbEmuPropertySet(void);

	STDMETHODIMP         QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG) AddRef(void);
	STDMETHODIMP_(ULONG) Release(void);

	STDMETHODIMP QuerySupport(REFGUID guidPropSet, ULONG ulId,
	                          ULONG *pulSupport);
	STDMETHODIMP Get(REFGUID guidPropSet, ULONG ulId, LPVOID pInstanceData,
	                 ULONG cbInstanceData, LPVOID pPropData,
	                 ULONG cbPropData, ULONG *pcbReturned);
	STDMETHODIMP Set(REFGUID guidPropSet, ULONG ulId, LPVOID pInstanceData,
	                 ULONG cbInstanceData, LPVOID pPropData,
	                 ULONG cbPropData, DWORD dwFlags);
	STDMETHODIMP AddInitialStateParameters(REFGUID guidPropSet, ULONG ulId,
	                                       LPVOID pInstanceData,
	                                       ULONG cbInstanceData,
	                                       LPVOID pPropData,
	                                       ULONG cbPropData);

private:
	LONG m_cRef;

	DWORD m_dwEnvPreset;
	float m_fVolume;
	float m_fDecayTime; /* Seconds. */
	float m_fDamping;
};

#endif /* A3D_FIXES */

#endif /* _A3DREVERBEMU_H */
