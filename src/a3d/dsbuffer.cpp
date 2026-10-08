/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * dsbuffer.cpp
 *
 * Implements the compatibility DLL's secondary DirectSound buffer
 * wrapper. Audio locking and transport calls reach the underlying buffer,
 * while volume, pan and frequency changes update the associated
 * compatibility source and its DSP solution.
 *
 * The wrapper creates or duplicates a CA3dSource and exposes its
 * IDirectSound3DBuffer interface. It also computes the gain block used by
 * the spatial engine and manages membership in the owning device's buffer
 * list.
 *
 *---------------------------------------------------------------------------
 */

#include "a3dprv.h"
#include "dsbuffer.h"
#include "A3d.h"
#include "A3dSource.h"
#include "a3ddsp.h"

#include <math.h>
#include <stdlib.h>

#define A3D_GAIN_BASE   10.0
#define A3D_GAIN_SCALE  0.0005
#define A3D_GAIN_FULL   65535.0

/* Integer unity gain  */
#define A3D_GAIN_UNITY  0xFFFF

#define A3D_GAIN_MIN    (-10000)
#define A3D_GAIN_MAX    10000

/* =============================================================
// A3dComputeGains()
// (RE) a3d.dll rtl:0x10002140
//
// Compute buffer gains from volume and pan in hundredths of a decibel.
// =============================================================*/

static void
A3dComputeGains(CA3dSecondaryBuffer *pBuffer,
                A3DGAINS *pGains,
                LONG lVolume, LONG lPan)
{
LONG lPanLeft;
LONG lPanRight;
LONG lLeft;
LONG lRight;

	if (lPan > A3D_GAIN_MAX)
		lPan = A3D_GAIN_MAX;
	else if (lPan < A3D_GAIN_MIN)
		lPan = A3D_GAIN_MIN;

	lPanLeft  = 0;
	lPanRight = 0;

	if (lPan >= 0)
	{
		pGains->lPanLeft = (LONG) (pow(A3D_GAIN_BASE,
			(double) -lPan * A3D_GAIN_SCALE)
			* A3D_GAIN_FULL);
		pGains->lPanRight = 0;

		lPanLeft  = -lPan;
		lPanRight = 0;
	}
	else
	{
		pGains->lPanLeft  = 0;
		pGains->lPanRight = (LONG) (pow(A3D_GAIN_BASE,
			(double) lPan * A3D_GAIN_SCALE)
			* A3D_GAIN_FULL);

		lPanLeft  = 0;
		lPanRight = lPan;
	}

	pGains->lVolume = lVolume;

	lLeft = lVolume + lPanLeft;

	if (lLeft <= A3D_GAIN_MIN)
		lLeft = A3D_GAIN_MIN;

	lRight = lVolume + lPanRight;

	if (lRight <= A3D_GAIN_MIN)
		lRight = A3D_GAIN_MIN;

	pGains->lAmplitude = (LONG) (pow(A3D_GAIN_BASE,
		(double) lVolume * A3D_GAIN_SCALE)
		* A3D_GAIN_FULL);
	pGains->lGainLeft = (LONG) (pow(A3D_GAIN_BASE,
		(double) lLeft * A3D_GAIN_SCALE)
		* A3D_GAIN_FULL);
	pGains->lGainRight = (LONG) (pow(A3D_GAIN_BASE,
		(double) lRight * A3D_GAIN_SCALE)
		* A3D_GAIN_FULL);
}

/* =============================================================
// CA3dSecondaryBuffer()
//
// Initialize the secondary wrapper with unity gains and its owning list.
// =============================================================*/

CA3dSecondaryBuffer::CA3dSecondaryBuffer(A3DPLEX *pPlex)
{
	m_pPlex = pPlex;

	m_cRef    = 0;
	m_pSource = NULL;
	m_pOwner  = NULL;

	m_pBuffer    = NULL;
	m_pDalBuffer = NULL;

	m_Unknown_0x38 = 0;

	m_Gains.lGainLeft  = A3D_GAIN_UNITY;
	m_Gains.lGainRight = A3D_GAIN_UNITY;
	m_Gains.lVolume    = 0;
	m_Gains.lAmplitude = A3D_GAIN_UNITY;
	m_Gains.lPan       = 0;
	m_Gains.lPanLeft   = 0;
	m_Gains.lPanRight  = 0;

	m_Unknown_0x7C = 0x400;
	m_Unknown_0x88 = 0x400;
}

/* =============================================================
// Init()
//
// Attach the wrapped buffer and create a source, using disabled 3D mode
// for non-3D buffers.
//
// Returns: S_OK; E_OUTOFMEMORY for source allocation failure; the DAL
//          QueryInterface or source Init failure otherwise.
// =============================================================*/

HRESULT
CA3dSecondaryBuffer::Init(CA3d *pOwner, LPCDSBUFFERDESC pcDesc,
                          LPDIRECTSOUNDBUFFER pBuffer)
{
HRESULT hr;

	m_pOwner  = pOwner;
	m_pBuffer = pBuffer;

	m_dwSampleRate = pcDesc->lpwfxFormat->nSamplesPerSec;

	hr = m_pBuffer->QueryInterface(IID_IA3dDalBuffer, (void **) &m_pDalBuffer);

	if (FAILED(hr))
	{
		m_pBuffer->Stop();

		return (hr);
	}

	m_pSource = new CA3dSource(this);

	if (!m_pSource)
	{
		m_pBuffer->Stop();

		return (E_OUTOFMEMORY);
	}

	hr = m_pSource->Init(m_dwSampleRate, m_pDalBuffer);

	if (FAILED(hr))
	{
		m_pBuffer->Stop();

		return (hr);
	}

	if (!(pcDesc->dwFlags & DSBCAPS_CTRL3D))
		m_pSource->SetMode(DS3DMODE_DISABLE, DS3D_IMMEDIATE);

	return (S_OK);
}

/* =============================================================
// InitFromDuplicate()
// (RE) a3d.dll rtl:0x100027A0
//
// Attach the duplicate buffer and copy source state from the original.
//
// Returns: S_OK; E_OUTOFMEMORY for source allocation failure; the DAL
//          QueryInterface or source Init failure otherwise.
// =============================================================*/

HRESULT
CA3dSecondaryBuffer::InitFromDuplicate(CA3d *pOwner,
                                       CA3dSecondaryBuffer *pOriginal,
                                       LPDIRECTSOUNDBUFFER pBuffer)
{
DS3DBUFFER ds3db;
HRESULT    hr;

	m_pOwner  = pOwner;
	m_pBuffer = pBuffer;

	hr = m_pBuffer->QueryInterface(IID_IA3dDalBuffer, (void **) &m_pDalBuffer);

	if (FAILED(hr))
	{
		m_pBuffer->Stop();

		return (hr);
	}

	memcpy(m_Unknown_0x18, pOriginal->m_Unknown_0x18, sizeof(m_Unknown_0x18));

	m_dwSampleRate = pOriginal->m_dwSampleRate;

	m_Gains.lGainLeft  = pOriginal->m_Gains.lGainLeft;
	m_Gains.lGainRight = pOriginal->m_Gains.lGainRight;
	m_Gains.lVolume    = pOriginal->m_Gains.lVolume;
	m_Gains.lAmplitude = pOriginal->m_Gains.lAmplitude;
	m_Gains.lPan       = pOriginal->m_Gains.lPan;
	m_Gains.lPanLeft   = pOriginal->m_Gains.lPanLeft;
	m_Gains.lPanRight  = pOriginal->m_Gains.lPanRight;

	memcpy(m_Unknown_0x58, pOriginal->m_Unknown_0x58, sizeof(m_Unknown_0x58));

	m_Unknown_0x7C = pOriginal->m_Unknown_0x7C;
	m_Unknown_0x80 = pOriginal->m_Unknown_0x80;
	m_Unknown_0x84 = pOriginal->m_Unknown_0x84;
	m_Unknown_0x88 = pOriginal->m_Unknown_0x88;

	m_pSource = new CA3dSource(this);

	if (!m_pSource)
	{
		m_pBuffer->Stop();

		return (E_OUTOFMEMORY);
	}

	hr = m_pSource->Init(m_dwSampleRate, m_pDalBuffer);

	if (FAILED(hr))
	{
		m_pBuffer->Stop();

		return (hr);
	}

	memset(&ds3db, 0, sizeof(ds3db));

	ds3db.dwSize = sizeof(DS3DBUFFER);

	pOriginal->m_pSource->GetAllParameters(&ds3db);
	m_pSource->SetAllParameters(&ds3db, DS3D_IMMEDIATE);

	return (S_OK);
}

/* =============================================================
// ~CA3dSecondaryBuffer()
//
// Remove the wrapper from its list, stop playback and release buffer resources.
// =============================================================*/

CA3dSecondaryBuffer::~CA3dSecondaryBuffer(void)
{
	if (m_pPlex)
		A3dPlexRemove(m_pPlex, A3dPlexFind(m_pPlex, this, NULL));

	m_pBuffer->Stop();

	if (m_pDalBuffer)
	{
		m_pDalBuffer->Release();
		m_pDalBuffer = NULL;
	}

	if (m_pBuffer)
	{
		m_pBuffer->Release();
		m_pBuffer = NULL;
	}

	if (m_pSource)
	{
		delete m_pSource;
		m_pSource = NULL;
	}
}

/* =============================================================
// QueryInterface()
//
// Acquire the buffer or its 3D source interface.
//
// Returns:
//   S_OK
//   E_INVALIDARG   a null output
//   E_NOINTERFACE  otherwise
// =============================================================*/

STDMETHODIMP
CA3dSecondaryBuffer::QueryInterface(REFIID riid, void **ppv)
{
	if (!ppv)
		return (E_INVALIDARG);

	if (riid == IID_IUnknown ||
	    riid == IID_IDirectSoundBuffer ||
	    riid == IID_IA3dDalBuffer ||
	    riid == IID_IA3dDalBuffer2 ||
	    riid == IID_IA3dScaleHackBuffer)
	{
		*ppv = (void *) this;

		AddRef();

		return (S_OK);
	}

	if (riid == IID_IDirectSound3DBuffer)
	{
		*ppv = (void *) m_pSource;

		m_pSource->AddRef();

		return (S_OK);
	}

	*ppv = NULL;

	return (E_NOINTERFACE);
}

/* =============================================================
// AddRef()
// (RE) a3d.dll rtl:0x10003E30
//
// Increment the reference count.
//
// Returns: The updated reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dSecondaryBuffer::AddRef(void)
{
	return (++m_cRef);
}

/* =============================================================
// Release()
//
// Release a reference and delete the wrapper at zero.
//
// Returns: The remaining reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dSecondaryBuffer::Release(void)
{
	if (--m_cRef)
		return (m_cRef);

	delete this;

	return (0);
}

/* =============================================================
// GetCaps()
// (RE) a3d.dll rtl:0x10002460
//
// Read capabilities and mark the wrapper as hardware, even on query failure.
//
// Returns:
//   The wrapped GetCaps result
//   E_INVALIDARG                a null output
// =============================================================*/

STDMETHODIMP
CA3dSecondaryBuffer::GetCaps(LPDSBCAPS pCaps)
{
HRESULT hr;

	if (!pCaps)
		return (E_INVALIDARG);

	pCaps->dwSize = sizeof(DSBCAPS);

	hr = m_pBuffer->GetCaps(pCaps);

	pCaps->dwFlags = (pCaps->dwFlags & ~(DSBCAPS_LOCHARDWARE | DSBCAPS_LOCSOFTWARE))
			 | DSBCAPS_LOCHARDWARE;

	return (hr);
}

/* =============================================================
// GetCurrentPosition()
// (RE) a3d.dll rtl:0x10002AD0
//
// Forward the cursor query, discarding its result.
//
// Returns: S_OK unconditionally.
// =============================================================*/

STDMETHODIMP
CA3dSecondaryBuffer::GetCurrentPosition(LPDWORD pdwPlay, LPDWORD pdwWrite)
{
	m_pBuffer->GetCurrentPosition(pdwPlay, pdwWrite);

	return (S_OK);
}

/* =============================================================
// GetFormat()
//
// Reject the unsupported format query.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
CA3dSecondaryBuffer::GetFormat(LPWAVEFORMATEX pwfx, DWORD cbSize,
                               LPDWORD pcbWritten)
{
	return (E_NOTIMPL);
}

/* =============================================================
// GetVolume()
// (RE) a3d.dll rtl:0x10002B80
//
// Read the stored volume. Preserve the original read of uninitialized
// alternate-volume state.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null output
// =============================================================*/

STDMETHODIMP
CA3dSecondaryBuffer::GetVolume(LPLONG plVolume)
{
	if (!plVolume)
		return (E_INVALIDARG);

	if (m_fVolumeAlt)
		*plVolume = m_lVolumeAlt;
	else
		*plVolume = m_Gains.lVolume;

	return (S_OK);
}

/* =============================================================
// GetPan()
//
// Read the stored pan.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null output
// =============================================================*/

STDMETHODIMP
CA3dSecondaryBuffer::GetPan(LPLONG plPan)
{
	if (!plPan)
		return (E_INVALIDARG);

	*plPan = m_Gains.lPan;

	return (S_OK);
}

/* =============================================================
// GetFrequency()
// (RE) a3d.dll rtl:0x10002AF0
//
// Read the creation sample rate, including after SetFrequency changes playback.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null output
// =============================================================*/

STDMETHODIMP
CA3dSecondaryBuffer::GetFrequency(LPDWORD pdwFrequency)
{
	if (!pdwFrequency)
		return (E_INVALIDARG);

	*pdwFrequency = m_dwSampleRate;

	return (S_OK);
}

/* =============================================================
// GetStatus()
//
// Read the wrapped buffer status.
//
// Returns:
//   The wrapped GetStatus result
//   E_INVALIDARG                  a null output
// =============================================================*/

STDMETHODIMP
CA3dSecondaryBuffer::GetStatus(LPDWORD pdwStatus)
{
	if (!pdwStatus)
		return (E_INVALIDARG);

	return (m_pBuffer->GetStatus(pdwStatus));
}

/* =============================================================
// Initialize()
//
// Reject explicit buffer initialization.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
CA3dSecondaryBuffer::Initialize(LPDIRECTSOUND pDS, LPCDSBUFFERDESC pcDesc)
{
	return (E_NOTIMPL);
}

/* =============================================================
// Lock()
//
// Lock buffer regions for audio access.
//
// Returns: The wrapped Lock result.
// =============================================================*/

STDMETHODIMP
CA3dSecondaryBuffer::Lock(DWORD dwOffset, DWORD dwBytes, LPVOID *ppvAudio1,
                          LPDWORD pdwAudio1, LPVOID *ppvAudio2,
                          LPDWORD pdwAudio2, DWORD dwFlags)
{
	return (m_pBuffer->Lock(dwOffset, dwBytes, ppvAudio1, pdwAudio1,
	                        ppvAudio2, pdwAudio2, dwFlags));
}

/* =============================================================
// Play()
//
// Start buffer playback.
//
// Returns: The wrapped Play result.
// =============================================================*/

STDMETHODIMP
CA3dSecondaryBuffer::Play(DWORD dwReserved1, DWORD dwReserved2, DWORD dwFlags)
{
	return (m_pBuffer->Play(dwReserved1, dwReserved2, dwFlags));
}

/* =============================================================
// SetCurrentPosition()
//
// Set the playback cursor.
//
// Returns: The wrapped SetCurrentPosition result.
// =============================================================*/

STDMETHODIMP
CA3dSecondaryBuffer::SetCurrentPosition(DWORD dwPosition)
{
	return (m_pBuffer->SetCurrentPosition(dwPosition));
}

/* =============================================================
// SetFormat()
//
// Reject format changes.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
CA3dSecondaryBuffer::SetFormat(LPCWAVEFORMATEX pcfxFormat)
{
	return (E_NOTIMPL);
}

/* =============================================================
// SetVolume()
//
// Recompute volume gains and commit the source.
//
// Returns:
//   The source CommitOne result
//   E_INVALIDARG                 positive volume
// =============================================================*/

STDMETHODIMP
CA3dSecondaryBuffer::SetVolume(LONG lVolume)
{
DWORD dwMode;
LONG  lPan;

	if (lVolume > 0)
		return (E_INVALIDARG);

	m_pSource->GetMode(&dwMode);

	if (dwMode & DS3DMODE_DISABLE)
		lPan = m_Gains.lPan;
	else
		lPan = 0;

	A3dComputeGains(this, &m_Gains, lVolume, lPan);

	A3dApplyVolumePan(m_pSource->m_iSource, m_Gains.lGainLeft,
	                  m_Gains.lGainRight, m_pSource->m_pSolution);

	return (m_pSource->CommitOne());
}

/* =============================================================
// SetPan()
// (RE) a3d.dll rtl:0x10002270
//
// Apply stereo pan to a disabled 3D source. Preserve the original failure
// to store lPan, leaving GetPan stale and allowing SetVolume to undo the pan.
//
// Returns:
//   The source CommitOne result
//   E_FAIL                       if the source mode lacks DS3DMODE_DISABLE
// =============================================================*/

STDMETHODIMP
CA3dSecondaryBuffer::SetPan(LONG lPan)
{
DWORD dwMode;

	m_pSource->GetMode(&dwMode);

	if (!(dwMode & DS3DMODE_DISABLE))
		return (E_FAIL);

	A3dComputeGains(this, &m_Gains, m_Gains.lVolume, lPan);

	A3dApplyVolumePan(m_pSource->m_iSource, m_Gains.lGainLeft,
	                  m_Gains.lGainRight, m_pSource->m_pSolution);

	return (m_pSource->CommitOne());
}

/* =============================================================
// SetFrequency()
// (RE) a3d.dll rtl:0x10002E50
//
// Set the engine frequency and commit without validating range or sentinels.
//
// Returns: The source CommitOne result.
// =============================================================*/

STDMETHODIMP
CA3dSecondaryBuffer::SetFrequency(DWORD dwFrequency)
{
	A3dSourceSetFrequency(m_pSource->m_iSource, dwFrequency,
	                      m_pSource->m_pSolution);

	return (m_pSource->CommitOne());
}

/* =============================================================
// Stop()
//
// Stop buffer playback.
//
// Returns: The wrapped Stop result.
// =============================================================*/

STDMETHODIMP
CA3dSecondaryBuffer::Stop(void)
{
	return (m_pBuffer->Stop());
}

/* =============================================================
// Unlock()
//
// Unlock audio regions.
//
// Returns: The wrapped Unlock result.
// =============================================================*/

STDMETHODIMP
CA3dSecondaryBuffer::Unlock(LPVOID pvAudio1, DWORD dwAudio1, LPVOID pvAudio2,
                            DWORD dwAudio2)
{
	return (m_pBuffer->Unlock(pvAudio1, dwAudio1, pvAudio2, dwAudio2));
}

/* =============================================================
// Restore()
//
// Reject buffer restoration.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
CA3dSecondaryBuffer::Restore(void)
{
	return (E_NOTIMPL);
}
