/////////////////////////////////////////////////////////////////////////////
// A3dReverbEmu.cpp
// ================

// NOT PART OF THE ORIGINAL.  See A3dReverbEmu.h for what this is and is not.

// Implements the optional software reverb engine and the property adapter
// that connects A3D preset controls to it. Each channel processes samples
// through damped comb filters and allpass stages, with delay lengths
// adjusted for the mixer sample rate.
//
// DAL_A2D adds the result to the complete stereo mix. Processing uses a
// fixed wet level and does not implement per-source sends. The property
// adapter stores preset parameters and configures the shared engine;
// A3dReverbEmu.h declares both sides of that connection.
/////////////////////////////////////////////////////////////////////////////

#include "A3dReverbEmu.h"

#if defined(A3D_FIXES)

#include "A3dReverb.h"
#include <math.h>

/* Delay lengths at 44100 Hz, rescaled during initialization */
static const int	g_anCombDelaySamples[A3D_REVERB_EMU_NUM_COMBS] =
{
	1116, 1188, 1277, 1356
};

static const int	g_anAllpassDelaySamples[A3D_REVERB_EMU_NUM_ALLPASS] =
{
	556, 441
};

#define A3D_REVERB_EMU_TUNING_RATE	44100.0f
#define A3D_REVERB_EMU_STEREO_SPREAD	23

/* Software-emulation tuning values. */
#define A3D_REVERB_EMU_MIN_DELAY_SAMPLES        8
#define A3D_REVERB_EMU_MIN_DECAY_TIME_S         0.1f
#define A3D_REVERB_EMU_MAX_DAMPING              0.9f
#define A3D_REVERB_EMU_MAX_COMB_FEEDBACK        0.8f
#define A3D_REVERB_EMU_ALLPASS_FEEDBACK         0.5f
#define A3D_REVERB_EMU_WET_GAIN                 0.7f

/* Comb input attenuation. */
#define A3D_REVERB_EMU_INPUT_GAIN	0.9f

/* Wet-signal amplitude limit. */
#define A3D_REVERB_EMU_WET_CEILING	16000.0f

/* =============================================================
// CA3dReverbEmuChannel::CA3dReverbEmuChannel()
//
// Initialize empty delay lines and default decay and damping.
// =============================================================*/

CA3dReverbEmuChannel::CA3dReverbEmuChannel(void)
{
	ZeroMemory(m_aComb, sizeof(m_aComb));
	ZeroMemory(m_aAllpass, sizeof(m_aAllpass));

	m_fDecayTime    = 1.49f;
	m_fDamping      = 0.5f;
	m_fHaveRequest  = 0;
}

/* =============================================================
// CA3dReverbEmuChannel::~CA3dReverbEmuChannel()
//
// Free comb and allpass delay lines.
// =============================================================*/

CA3dReverbEmuChannel::~CA3dReverbEmuChannel(void)
{
	int	i;

	for (i = 0; i < A3D_REVERB_EMU_NUM_COMBS; i++)
	{
		delete[] m_aComb[i].pfLine;
	}

	for (i = 0; i < A3D_REVERB_EMU_NUM_ALLPASS; i++)
	{
		delete[] m_aAllpass[i].pfLine;
	}
}

/* =============================================================
// CA3dReverbEmuChannel::Init()
//
// Allocate delay lines for the mix rate and apply stored feedback settings.
// =============================================================*/

void
CA3dReverbEmuChannel::Init(DWORD dwSampleRate, int nChannelOffset)
{
float	fScale;
int	i;
int	cLine;

	fScale = (float) dwSampleRate / A3D_REVERB_EMU_TUNING_RATE;

	for (i = 0; i < A3D_REVERB_EMU_NUM_COMBS; i++)
	{
		cLine = (int) (fScale * (g_anCombDelaySamples[i] + nChannelOffset));
		if (cLine < A3D_REVERB_EMU_MIN_DELAY_SAMPLES)
			cLine = A3D_REVERB_EMU_MIN_DELAY_SAMPLES;

		m_aComb[i].pfLine       = new float[cLine];
		m_aComb[i].cLine        = cLine;
		m_aComb[i].nPos         = 0;
		m_aComb[i].fDampState   = 0.0f;

		ZeroMemory(m_aComb[i].pfLine, cLine * sizeof(float));
	}

	for (i = 0; i < A3D_REVERB_EMU_NUM_ALLPASS; i++)
	{
		cLine = (int) (fScale * (g_anAllpassDelaySamples[i] + nChannelOffset));
		if (cLine < A3D_REVERB_EMU_MIN_DELAY_SAMPLES)
			cLine = A3D_REVERB_EMU_MIN_DELAY_SAMPLES;

		m_aAllpass[i].pfLine    = new float[cLine];
		m_aAllpass[i].cLine     = cLine;
		m_aAllpass[i].nPos      = 0;
		m_aAllpass[i].fFeedback = A3D_REVERB_EMU_ALLPASS_FEEDBACK;

		ZeroMemory(m_aAllpass[i].pfLine, cLine * sizeof(float));
	}

	ApplyFeedback();
}

/* =============================================================
// CA3dReverbEmuChannel::Configure()
//
// Store decay and damping, clamping damping below unity for filter stability.
// Apply settings immediately when delay lines are initialized.
// =============================================================*/

void
CA3dReverbEmuChannel::Configure(float fDecayTime, float fDamping)
{

	if (fDecayTime < A3D_REVERB_EMU_MIN_DECAY_TIME_S)
		fDecayTime = A3D_REVERB_EMU_MIN_DECAY_TIME_S;

	if (fDamping < 0.0f)
		fDamping = 0.0f;
	if (fDamping > A3D_REVERB_EMU_MAX_DAMPING)
		fDamping = A3D_REVERB_EMU_MAX_DAMPING;

	m_fDecayTime   = fDecayTime;
	m_fDamping     = fDamping;
	m_fHaveRequest = 1;

	if (m_aComb[0].cLine > 0)
		ApplyFeedback();
}

/* =============================================================
// CA3dReverbEmuChannel::ApplyFeedback()
//
// Apply decay feedback and damping to initialized comb filters.
// =============================================================*/

void
CA3dReverbEmuChannel::ApplyFeedback(void)
{
int	i;

	for (i = 0; i < A3D_REVERB_EMU_NUM_COMBS; i++)
	{
		if (m_aComb[i].cLine <= 0)
			continue;

		m_aComb[i].fFeedback = (float) pow(10.0,
			-3.0 * m_aComb[i].cLine /
			(A3D_REVERB_EMU_TUNING_RATE * m_fDecayTime));

		if (m_aComb[i].fFeedback > A3D_REVERB_EMU_MAX_COMB_FEEDBACK)
			m_aComb[i].fFeedback = A3D_REVERB_EMU_MAX_COMB_FEEDBACK;

		m_aComb[i].fDamping = m_fDamping;
	}
}

/* =============================================================
// CA3dReverbEmuChannel::Process()
//
// Filter one sample through the comb and allpass stages.
//
// Returns: The reverberated sample.
// =============================================================*/

float
CA3dReverbEmuChannel::Process(float fIn)
{
float	fOut;
float	fCombSum;
int	i;
float	fDelayed;

	fCombSum = 0.0f;

	for (i = 0; i < A3D_REVERB_EMU_NUM_COMBS; i++)
	{
		A3DREVERBEMU_COMB *pComb = &m_aComb[i];

		fDelayed = pComb->pfLine[pComb->nPos];

		pComb->fDampState = fDelayed * (1.0f - pComb->fDamping) +
				     pComb->fDampState * pComb->fDamping;

		pComb->pfLine[pComb->nPos] = fIn +
					     pComb->fDampState * pComb->fFeedback;

		pComb->nPos++;
		if (pComb->nPos >= pComb->cLine)
			pComb->nPos = 0;

		fCombSum += fDelayed;
	}

	fOut = fCombSum / A3D_REVERB_EMU_NUM_COMBS;

	for (i = 0; i < A3D_REVERB_EMU_NUM_ALLPASS; i++)
	{
		A3DREVERBEMU_ALLPASS *pAp = &m_aAllpass[i];
		float			fBuf;
		float			fIn2;

		fBuf = pAp->pfLine[pAp->nPos];
		fIn2 = fOut;

		fOut                    = -fIn2 + fBuf;
		pAp->pfLine[pAp->nPos]  = fIn2 + fBuf * pAp->fFeedback;

		pAp->nPos++;
		if (pAp->nPos >= pAp->cLine)
			pAp->nPos = 0;
	}

	return (fOut);
}

/* =============================================================
// CA3dReverbEmuEngine::CA3dReverbEmuEngine()
//
// Initialize an enabled engine awaiting configuration.
// =============================================================*/

CA3dReverbEmuEngine::CA3dReverbEmuEngine(void)
{
	m_dwSampleRate = 0;
	m_fWetMix      = A3D_REVERB_EMU_WET_GAIN;
	m_lConfigured  = 0;
	m_lEnabled     = 1;

	InitializeCriticalSection(&m_cs);
}

/* =============================================================
// CA3dReverbEmuEngine::~CA3dReverbEmuEngine()
//
// Destroy the configuration lock and both channels.
// =============================================================*/

CA3dReverbEmuEngine::~CA3dReverbEmuEngine(void)
{
	DeleteCriticalSection(&m_cs);
}

/* =============================================================
// CA3dReverbEmuEngine::Configure()
//
// Configure both channels, ignoring the preset identifier and volume.
// =============================================================*/

void
CA3dReverbEmuEngine::Configure(DWORD dwEnvPreset, float fVolume,
			       float fDecayTime, float fDamping)
{
	UNREFERENCED_PARAMETER(dwEnvPreset);
	UNREFERENCED_PARAMETER(fVolume);

	EnterCriticalSection(&m_cs);

	m_Left.Configure(fDecayTime, fDamping);
	m_Right.Configure(fDecayTime, fDamping);

	m_lConfigured = 1;

	LeaveCriticalSection(&m_cs);
}

/* =============================================================
// CA3dReverbEmuEngine::SetEnabled()
//
// Enable or mute reverb processing while retaining delay state.
// =============================================================*/

void
CA3dReverbEmuEngine::SetEnabled(int fEnabled)
{
	InterlockedExchange(&m_lEnabled, fEnabled ? 1 : 0);
}

/* =============================================================
// CA3dReverbEmuEngine::Process()
//
// Add reverb at a fixed level to the whole stereo mix; per-source sends
// remain unsupported. Reset the FPU because reconstructed x87 routines
// may leave its stack corrupted.
// =============================================================*/

void
CA3dReverbEmuEngine::Process(float *pflInterleavedStereo, DWORD cFrames,
			     DWORD dwSampleRate)
{
DWORD	i;
float	fL, fR;

	if (!m_lConfigured || !m_lEnabled)
		return;

	EnterCriticalSection(&m_cs);

	__asm { fninit }

	if (dwSampleRate != m_dwSampleRate)
	{
		m_dwSampleRate = dwSampleRate;
		m_Left.Init(dwSampleRate, 0);
		m_Right.Init(dwSampleRate, A3D_REVERB_EMU_STEREO_SPREAD);
	}

	for (i = 0; i < cFrames; i++)
	{
	float	fWetL, fWetR;

		fL = pflInterleavedStereo[i * 2 + 0];
		fR = pflInterleavedStereo[i * 2 + 1];

		fWetL = m_fWetMix * m_Left.Process(fL * A3D_REVERB_EMU_INPUT_GAIN);
		fWetR = m_fWetMix * m_Right.Process(fR * A3D_REVERB_EMU_INPUT_GAIN);

		if (fWetL > A3D_REVERB_EMU_WET_CEILING)
			fWetL = A3D_REVERB_EMU_WET_CEILING;
		if (fWetL < -A3D_REVERB_EMU_WET_CEILING)
			fWetL = -A3D_REVERB_EMU_WET_CEILING;
		if (fWetR > A3D_REVERB_EMU_WET_CEILING)
			fWetR = A3D_REVERB_EMU_WET_CEILING;
		if (fWetR < -A3D_REVERB_EMU_WET_CEILING)
			fWetR = -A3D_REVERB_EMU_WET_CEILING;

		pflInterleavedStereo[i * 2 + 0] = fL + fWetL;
		pflInterleavedStereo[i * 2 + 1] = fR + fWetR;
	}

	LeaveCriticalSection(&m_cs);
}

static CA3dReverbEmuEngine	g_ReverbEmuEngine;

/* =============================================================
// A3dReverbEmuGetEngine()
//
// Read the process-wide reverb engine.
//
// Returns: The borrowed engine pointer.
// =============================================================*/

CA3dReverbEmuEngine *
A3dReverbEmuGetEngine(void)
{
	return (&g_ReverbEmuEngine);
}

/* =============================================================
// CA3dReverbEmuPropertySet::CA3dReverbEmuPropertySet()
//
// Initialize stored preset parameters and a zero reference count.
// =============================================================*/

CA3dReverbEmuPropertySet::CA3dReverbEmuPropertySet(void)
{
	m_cRef        = 0;
	m_dwEnvPreset = 0;
	m_fVolume     = 0.0f;
	m_fDecayTime  = 1.49f;
	m_fDamping    = 0.5f;
}

/* =============================================================
// CA3dReverbEmuPropertySet::QueryInterface()
//
// Query IUnknown or IA3dPropertySet, clearing unsupported outputs.
//
// Returns:
//   S_OK
//   E_POINTER      a null output
//   E_NOINTERFACE  other IIDs
// =============================================================*/

STDMETHODIMP
CA3dReverbEmuPropertySet::QueryInterface(REFIID riid, void **ppv)
{
	if (!ppv)
		return (E_POINTER);

	if (IsEqualIID(riid, IID_IUnknown) ||
	    IsEqualIID(riid, IID_IA3dPropertySet))
	{
		*ppv = (IA3dPropertySet *) this;
		AddRef();

		return (S_OK);
	}

	*ppv = NULL;

	return (E_NOINTERFACE);
}

/* =============================================================
// CA3dReverbEmuPropertySet::AddRef()
//
// Increment the reference count atomically.
//
// Returns: The updated reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dReverbEmuPropertySet::AddRef(void)
{
	return (InterlockedIncrement(&m_cRef));
}

/* =============================================================
// CA3dReverbEmuPropertySet::Release()
//
// Release a reference atomically and delete the adapter at zero.
//
// Returns: The remaining reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dReverbEmuPropertySet::Release(void)
{
LONG	c;

	c = InterlockedDecrement(&m_cRef);

	if (c == 0)
		delete this;

	return (c);
}

/* =============================================================
// CA3dReverbEmuPropertySet::QuerySupport()
//
// Report read and write support for every property.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
CA3dReverbEmuPropertySet::QuerySupport(REFGUID guidPropSet, ULONG ulId,
				       ULONG *pulSupport)
{
	UNREFERENCED_PARAMETER(guidPropSet);
	UNREFERENCED_PARAMETER(ulId);

	if (!pulSupport)
		return (E_POINTER);

	*pulSupport = KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET;

	return (S_OK);
}

/* =============================================================
// CA3dReverbEmuPropertySet::Get()
//
// Reject property reads without writing outputs.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
CA3dReverbEmuPropertySet::Get(REFGUID guidPropSet, ULONG ulId,
			      LPVOID pInstanceData, ULONG cbInstanceData,
			      LPVOID pPropData, ULONG cbPropData,
			      ULONG *pcbReturned)
{
	UNREFERENCED_PARAMETER(guidPropSet);
	UNREFERENCED_PARAMETER(ulId);
	UNREFERENCED_PARAMETER(pInstanceData);
	UNREFERENCED_PARAMETER(cbInstanceData);
	UNREFERENCED_PARAMETER(pPropData);
	UNREFERENCED_PARAMETER(cbPropData);
	UNREFERENCED_PARAMETER(pcbReturned);

	return (E_NOTIMPL);
}

/* =============================================================
// CA3dReverbEmuPropertySet::Set()
//
// Store recognized four-byte preset parameters and configure the engine.
// Property IDs follow the sender's A3D change bits.
// Unknown property identifiers are accepted without effect.
//
// Returns:
//   S_OK
//   E_INVALIDARG  null data or a size other than four bytes
// =============================================================*/

STDMETHODIMP
CA3dReverbEmuPropertySet::Set(REFGUID guidPropSet, ULONG ulId,
			      LPVOID pInstanceData, ULONG cbInstanceData,
			      LPVOID pPropData, ULONG cbPropData,
			      DWORD dwFlags)
{
	UNREFERENCED_PARAMETER(guidPropSet);
	UNREFERENCED_PARAMETER(pInstanceData);
	UNREFERENCED_PARAMETER(cbInstanceData);
	UNREFERENCED_PARAMETER(dwFlags);

	if (!pPropData || cbPropData != sizeof(DWORD))
		return (E_INVALIDARG);

	switch (ulId)
	{
	case A3DREVERB_CHANGED_ENVIRONMENT:
		m_dwEnvPreset = *(DWORD *) pPropData;
		break;

	case A3DREVERB_CHANGED_VOLUME:
		m_fVolume = *(float *) pPropData;
		break;

	case A3DREVERB_CHANGED_DECAYTIME:
		m_fDecayTime = *(float *) pPropData;
		break;

	case A3DREVERB_CHANGED_DAMPING:
		m_fDamping = *(float *) pPropData;
		break;

	default:
		return (S_OK);
	}

	A3dReverbEmuGetEngine()->Configure(m_dwEnvPreset, m_fVolume,
					   m_fDecayTime, m_fDamping);

	return (S_OK);
}

/* =============================================================
// CA3dReverbEmuPropertySet::AddInitialStateParameters()
//
// Accept initial-state parameters without storing them.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dReverbEmuPropertySet::AddInitialStateParameters(REFGUID guidPropSet,
						     ULONG ulId,
						     LPVOID pInstanceData,
						     ULONG cbInstanceData,
						     LPVOID pPropData,
						     ULONG cbPropData)
{
	UNREFERENCED_PARAMETER(guidPropSet);
	UNREFERENCED_PARAMETER(ulId);
	UNREFERENCED_PARAMETER(pInstanceData);
	UNREFERENCED_PARAMETER(cbInstanceData);
	UNREFERENCED_PARAMETER(pPropData);
	UNREFERENCED_PARAMETER(cbPropData);

	return (S_OK);
}

#endif /* A3D_FIXES */
