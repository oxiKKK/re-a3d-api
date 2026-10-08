// Project-added A3D development tooling.
#include "capture_internal.hpp"
#include <cstdio>
#include <cmath>
#include <cstring>

namespace a3dcapture {
static void
AppendStream(CapturedBuffer *pInfo, const void *pv, DWORD cb)
{
	if (pInfo->pStream == NULL)
	{
		pInfo->pStream = (BYTE *) HeapAlloc(GetProcessHeap(), 0,
						    CAPTURE_MAX_STREAM_BYTES);

		if (pInfo->pStream == NULL)
			return;
	}

	if (pInfo->cbStream + cb > CAPTURE_MAX_STREAM_BYTES)
	{
		pInfo->fOverflow = 1;

		cb = CAPTURE_MAX_STREAM_BYTES - pInfo->cbStream;
	}

	if (cb)
	{
		CopyMemory(pInfo->pStream + pInfo->cbStream, pv, cb);

		pInfo->cbStream += cb;
	}
}

class RecordingSoundNotify : public IDirectSoundNotify
{
public:
	RecordingSoundNotify(void) : m_cRef(1) {}

	STDMETHODIMP QueryInterface(REFIID riid, void **ppv)
	{
		if (ppv == NULL)
			return (E_POINTER);

		if (IsEqualIID(riid, IID_IUnknown) ||
		    IsEqualIID(riid, IID_IDirectSoundNotify))
		{
			*ppv = (IDirectSoundNotify *) this;
			AddRef();
			return (S_OK);
		}

		*ppv = NULL;
		return (E_NOINTERFACE);
	}

	STDMETHODIMP_(ULONG) AddRef(void)
	{
		return ((ULONG) InterlockedIncrement(&m_cRef));
	}

	STDMETHODIMP_(ULONG) Release(void)
	{
		long c = InterlockedDecrement(&m_cRef);

		if (c == 0)
			delete this;

		return ((ULONG) c);
	}

	STDMETHODIMP SetNotificationPositions(DWORD dwCount,
					      LPCDSBPOSITIONNOTIFY pcNotify)
	{
		return (S_OK);
	}

private:
	long	m_cRef;
};

/* -------------------------------------------------------------------------- */
/* The recording secondary buffer.  It holds a heap buffer of dwBufferBytes,
   hands spans of it back from Lock(), and sums what came back at Unlock(). */

class RecordingSoundBuffer : public IDirectSoundBuffer
{
public:
	RecordingSoundBuffer(const DSBUFFERDESC1 *pDesc);
	~RecordingSoundBuffer(void);

	/* IUnknown */
	STDMETHODIMP QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG) AddRef(void);
	STDMETHODIMP_(ULONG) Release(void);

	/* IDirectSoundBuffer */
	STDMETHODIMP GetCaps(LPDSBCAPS pCaps);
	STDMETHODIMP GetCurrentPosition(LPDWORD pdwPlay, LPDWORD pdwWrite);
	STDMETHODIMP GetFormat(LPWAVEFORMATEX pwfx, DWORD cbAlloc, LPDWORD pcbWritten);
	STDMETHODIMP GetVolume(LPLONG plVolume);
	STDMETHODIMP GetPan(LPLONG plPan);
	STDMETHODIMP GetFrequency(LPDWORD pdwFrequency);
	STDMETHODIMP GetStatus(LPDWORD pdwStatus);
	STDMETHODIMP Initialize(LPDIRECTSOUND pDS, LPCDSBUFFERDESC pcDesc);
	STDMETHODIMP Lock(DWORD dwOffset, DWORD dwBytes,
			  LPVOID *ppv1, LPDWORD pcb1,
			  LPVOID *ppv2, LPDWORD pcb2, DWORD dwFlags);
	STDMETHODIMP Play(DWORD dwReserved1, DWORD dwPriority, DWORD dwFlags);
	STDMETHODIMP SetCurrentPosition(DWORD dwPos);
	STDMETHODIMP SetFormat(LPCWAVEFORMATEX pcfx);
	STDMETHODIMP SetVolume(LONG lVolume);
	STDMETHODIMP SetPan(LONG lPan);
	STDMETHODIMP SetFrequency(DWORD dwFrequency);
	STDMETHODIMP Stop(void);
	STDMETHODIMP Unlock(LPVOID pv1, DWORD cb1, LPVOID pv2, DWORD cb2);
	STDMETHODIMP Restore(void);

private:
	long		m_cRef;
	BYTE		*m_pData;
	DWORD		m_cbData;
	WAVEFORMATEX	m_wfx;
	DWORD		m_dwBytesPerMs;
	DWORD		m_dwStartTick;
	int		m_nIndex;	/* into capture_state.buffers, -1 past CAPTURE_MAX_BUFFER_COUNT */
};

RecordingSoundBuffer::RecordingSoundBuffer(const DSBUFFERDESC1 *pDesc)
{
	m_cRef = 1;

	ZeroMemory(&m_wfx, sizeof(m_wfx));

	m_cbData = (pDesc && pDesc->dwBufferBytes) ? pDesc->dwBufferBytes : 0x8000;

	m_pData = (BYTE *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, m_cbData);

	if (pDesc && pDesc->lpwfxFormat &&
	    !(pDesc->dwFlags & DSBCAPS_PRIMARYBUFFER))
		m_wfx = *pDesc->lpwfxFormat;

	if (m_wfx.nAvgBytesPerSec == 0)
	{
		/* The DAL always makes the output at 22050 Hz, 16-bit, stereo. */
		m_wfx.wFormatTag      = WAVE_FORMAT_PCM;
		m_wfx.nChannels       = 2;
		m_wfx.nSamplesPerSec  = 22050;
		m_wfx.wBitsPerSample  = 16;
		m_wfx.nBlockAlign     = 4;
		m_wfx.nAvgBytesPerSec = 22050 * 4;
	}

	m_dwBytesPerMs = m_wfx.nAvgBytesPerSec / 1000;

	if (m_dwBytesPerMs == 0)
		m_dwBytesPerMs = 88;

	m_dwStartTick = GetTickCount();

	/* Take a record slot here rather than in CreateSoundBuffer, so a buffer
	   made by DuplicateSoundBuffer is recorded too. */

	EnterCriticalSection(&capture_state.lock);

	if (capture_state.buffer_count < CAPTURE_MAX_BUFFER_COUNT)
	{
		CapturedBuffer *pInfo = &capture_state.buffers[capture_state.buffer_count];

		m_nIndex = capture_state.buffer_count++;

		pInfo->dwBufferBytes   = pDesc ? pDesc->dwBufferBytes : 0;
		pInfo->dwFlags         = pDesc ? pDesc->dwFlags : 0;
		pInfo->dwSamplesPerSec = m_wfx.nSamplesPerSec;
		pInfo->nChannels       = m_wfx.nChannels;
		pInfo->wBitsPerSample  = m_wfx.wBitsPerSample;
	}
	else
	{
		m_nIndex = -1;
	}

	LeaveCriticalSection(&capture_state.lock);
}

RecordingSoundBuffer::~RecordingSoundBuffer(void)
{
	if (m_pData)
		HeapFree(GetProcessHeap(), 0, m_pData);
}

STDMETHODIMP
RecordingSoundBuffer::QueryInterface(REFIID riid, void **ppv)
{
	if (ppv == NULL)
		return (E_POINTER);

	if (IsEqualIID(riid, IID_IUnknown) ||
	    IsEqualIID(riid, IID_IDirectSoundBuffer))
	{
		*ppv = (IDirectSoundBuffer *) this;
		AddRef();
		return (S_OK);
	}

	if (IsEqualIID(riid, IID_IDirectSoundNotify))
	{
		*ppv = new RecordingSoundNotify;
		return (*ppv ? S_OK : E_OUTOFMEMORY);
	}

	/* IID_IDirectSound3DBuffer and anything else are refused.  DAL_A2D does
	   the 3D work in software and does not query the output buffer for it. */

	*ppv = NULL;
	return (E_NOINTERFACE);
}

STDMETHODIMP_(ULONG)
RecordingSoundBuffer::AddRef(void)
{
	return ((ULONG) InterlockedIncrement(&m_cRef));
}

STDMETHODIMP_(ULONG)
RecordingSoundBuffer::Release(void)
{
	long c = InterlockedDecrement(&m_cRef);

	if (c == 0)
		delete this;

	return ((ULONG) c);
}

STDMETHODIMP
RecordingSoundBuffer::GetCaps(LPDSBCAPS pCaps)
{
	if (pCaps == NULL)
		return (E_POINTER);

	pCaps->dwFlags              = DSBCAPS_GETCURRENTPOSITION2 |
				      DSBCAPS_GLOBALFOCUS |
				      DSBCAPS_CTRLPOSITIONNOTIFY |
				      DSBCAPS_CTRLFREQUENCY;
	pCaps->dwBufferBytes        = m_cbData;
	pCaps->dwUnlockTransferRate = 0;
	pCaps->dwPlayCpuOverhead    = 0;

	return (S_OK);
}

/* Advance the play cursor by real time so GetBytesQueued() sees the output
   draining and the mixer keeps filling it. */

STDMETHODIMP
RecordingSoundBuffer::GetCurrentPosition(LPDWORD pdwPlay, LPDWORD pdwWrite)
{
DWORD	dwElapsed;
DWORD	dwPlay;

	InterlockedIncrement(&capture_state.position_query_count);

	dwElapsed = GetTickCount() - m_dwStartTick;
	dwPlay    = (dwElapsed * m_dwBytesPerMs) % m_cbData;

	/* Keep the cursor on a sample boundary. */
	dwPlay -= dwPlay % m_wfx.nBlockAlign;

	if (pdwPlay)
		*pdwPlay = dwPlay;

	if (pdwWrite)
		*pdwWrite = (dwPlay + m_wfx.nBlockAlign) % m_cbData;

	return (S_OK);
}

STDMETHODIMP
RecordingSoundBuffer::GetFormat(LPWAVEFORMATEX pwfx, DWORD cbAlloc, LPDWORD pcbWritten)
{
DWORD	cb;

	cb = sizeof(WAVEFORMATEX);

	if (pwfx && cbAlloc >= cb)
		CopyMemory(pwfx, &m_wfx, cb);
	else
		cb = 0;

	if (pcbWritten)
		*pcbWritten = cb;

	return (S_OK);
}

STDMETHODIMP RecordingSoundBuffer::GetVolume(LPLONG plVolume)
{
	if (plVolume) *plVolume = 0;
	return (S_OK);
}

STDMETHODIMP RecordingSoundBuffer::GetPan(LPLONG plPan)
{
	if (plPan) *plPan = 0;
	return (S_OK);
}

STDMETHODIMP RecordingSoundBuffer::GetFrequency(LPDWORD pdwFrequency)
{
	if (pdwFrequency) *pdwFrequency = m_wfx.nSamplesPerSec;
	return (S_OK);
}

STDMETHODIMP RecordingSoundBuffer::GetStatus(LPDWORD pdwStatus)
{
	if (pdwStatus)
		*pdwStatus = DSBSTATUS_PLAYING | DSBSTATUS_LOOPING;
	return (S_OK);
}

STDMETHODIMP RecordingSoundBuffer::Initialize(LPDIRECTSOUND pDS, LPCDSBUFFERDESC pcDesc)
{
	return (S_OK);
}

STDMETHODIMP
RecordingSoundBuffer::Lock(DWORD dwOffset, DWORD dwBytes,
	      LPVOID *ppv1, LPDWORD pcb1,
	      LPVOID *ppv2, LPDWORD pcb2, DWORD dwFlags)
{
DWORD	cbFirst;

	InterlockedIncrement(&capture_state.lock_count);

	if (m_nIndex >= 0)
		InterlockedIncrement(&capture_state.buffers[m_nIndex].nLocks);

	if (ppv1 == NULL || pcb1 == NULL)
		return (E_POINTER);

	if (dwFlags & DSBLOCK_ENTIREBUFFER)
	{
		dwOffset = 0;
		dwBytes  = m_cbData;
	}

	if (dwOffset >= m_cbData)
		dwOffset %= m_cbData;

	if (dwBytes > m_cbData)
		dwBytes = m_cbData;

	cbFirst = m_cbData - dwOffset;

	if (cbFirst > dwBytes)
		cbFirst = dwBytes;

	*ppv1 = m_pData + dwOffset;
	*pcb1 = cbFirst;

	if (ppv2)
		*ppv2 = (dwBytes > cbFirst) ? m_pData : NULL;
	if (pcb2)
		*pcb2 = dwBytes - cbFirst;

	return (S_OK);
}

STDMETHODIMP RecordingSoundBuffer::Play(DWORD dwReserved1, DWORD dwPriority, DWORD dwFlags)
{
	InterlockedIncrement(&capture_state.play_count);
	EnterCriticalSection(&capture_state.lock);
	if (m_nIndex >= 0) capture_state.buffers[m_nIndex].playing = true;
	LeaveCriticalSection(&capture_state.lock);
	return (S_OK);
}

STDMETHODIMP RecordingSoundBuffer::SetCurrentPosition(DWORD dwPos)
{
	/* Re-anchor the play cursor to the requested byte so the next
	   GetCurrentPosition() advances from there. */
	m_dwStartTick = GetTickCount() -
			(m_dwBytesPerMs ? dwPos / m_dwBytesPerMs : 0);
	return (S_OK);
}

STDMETHODIMP RecordingSoundBuffer::SetFormat(LPCWAVEFORMATEX pcfx)
{
	if (pcfx)
		m_wfx = *pcfx;
	return (S_OK);
}

STDMETHODIMP RecordingSoundBuffer::SetVolume(LONG lVolume)   { return (S_OK); }
STDMETHODIMP RecordingSoundBuffer::SetPan(LONG lPan)         { return (S_OK); }
STDMETHODIMP RecordingSoundBuffer::SetFrequency(DWORD dwF)   { return (S_OK); }
STDMETHODIMP RecordingSoundBuffer::Stop(void)
{
	EnterCriticalSection(&capture_state.lock);
	if (m_nIndex >= 0) capture_state.buffers[m_nIndex].playing = false;
	LeaveCriticalSection(&capture_state.lock);
	return (S_OK);
}
STDMETHODIMP RecordingSoundBuffer::Restore(void)             { return (S_OK); }

STDMETHODIMP
RecordingSoundBuffer::Unlock(LPVOID pv1, DWORD cb1, LPVOID pv2, DWORD cb2)
{
	EnterCriticalSection(&capture_state.lock);

	capture_state.unlock_count++;

	if (cb1 || cb2)
		capture_state.nonempty_unlock_count++;

	/* Record per-buffer PCM from the first write so both DLL captures start
	   at the same wave position. Aggregate statistics skip warm-up samples.
	   Stop recording when capture_state.stream_finished is set, before
	   Stop() and release: teardown can append timing-dependent silence. */

	if (m_nIndex >= 0 && capture_state.stream_started && !capture_state.stream_finished)
	{
		CapturedBuffer *pInfo = &capture_state.buffers[m_nIndex];

		pInfo->nUnlocks++;
		if (pInfo->playing && (cb1 || cb2)) ++pInfo->playingUnlocks;

		if (pv1 && cb1)
			AppendStream(pInfo, pv1, cb1);

		if (pv2 && cb2)
			AppendStream(pInfo, pv2, cb2);
	}

	/* Skip startup transients in aggregate statistics. Recorded peaks were
	   22595 for the reconstruction and either 15063 / sum-abs 390M or
	   32768 / 945M for the reference. The reference still clipped intermittently
	   after warm-up; skipping samples does not eliminate that variation. */
	if (capture_state.statistics_enabled)
	{
		if (pv1 && cb1)
			AccumRegion(pv1, cb1);

		if (pv2 && cb2)
			AccumRegion(pv2, cb2);
	}

	LeaveCriticalSection(&capture_state.lock);

	return (S_OK);
}

/* -------------------------------------------------------------------------- */
/* CreateSoundBuffer returns RecordingSoundBuffer to intercept mixer writes. */

class RecordingDirectSound : public IDirectSound
{
public:
	RecordingDirectSound(void) : m_cRef(1) {}

	STDMETHODIMP QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG) AddRef(void);
	STDMETHODIMP_(ULONG) Release(void);

	STDMETHODIMP CreateSoundBuffer(LPCDSBUFFERDESC pcDesc,
				       LPDIRECTSOUNDBUFFER *ppBuf,
				       LPUNKNOWN pUnkOuter);
	STDMETHODIMP GetCaps(LPDSCAPS pCaps);
	STDMETHODIMP DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER pOrig,
					  LPDIRECTSOUNDBUFFER *ppDup);
	STDMETHODIMP SetCooperativeLevel(HWND hwnd, DWORD dwLevel);
	STDMETHODIMP Compact(void);
	STDMETHODIMP GetSpeakerConfig(LPDWORD pdwConfig);
	STDMETHODIMP SetSpeakerConfig(DWORD dwConfig);
	STDMETHODIMP Initialize(LPCGUID pcGuidDevice);

private:
	long	m_cRef;
};

STDMETHODIMP
RecordingDirectSound::QueryInterface(REFIID riid, void **ppv)
{
	if (ppv == NULL)
		return (E_POINTER);

	if (IsEqualIID(riid, IID_IUnknown) ||
	    IsEqualIID(riid, IID_IDirectSound))
	{
		*ppv = (IDirectSound *) this;
		AddRef();
		return (S_OK);
	}

	*ppv = NULL;
	return (E_NOINTERFACE);
}

STDMETHODIMP_(ULONG)
RecordingDirectSound::AddRef(void)
{
	return ((ULONG) InterlockedIncrement(&m_cRef));
}

STDMETHODIMP_(ULONG)
RecordingDirectSound::Release(void)
{
	long c = InterlockedDecrement(&m_cRef);

	if (c == 0)
		delete this;

	return ((ULONG) c);
}

STDMETHODIMP
RecordingDirectSound::CreateSoundBuffer(LPCDSBUFFERDESC pcDesc,
			  LPDIRECTSOUNDBUFFER *ppBuf, LPUNKNOWN pUnkOuter)
{
RecordingSoundBuffer	*pBuf;

	if (ppBuf == NULL)
		return (E_POINTER);

	if (pUnkOuter != NULL)
		return (DSERR_INVALIDPARAM);

	EnterCriticalSection(&capture_state.lock);

	capture_state.buffer_creation_count++;

	LeaveCriticalSection(&capture_state.lock);

	pBuf = new RecordingSoundBuffer((const DSBUFFERDESC1 *) pcDesc);

	if (pBuf == NULL)
		return (E_OUTOFMEMORY);

	*ppBuf = (LPDIRECTSOUNDBUFFER) pBuf;

	return (S_OK);
}

STDMETHODIMP
RecordingDirectSound::GetCaps(LPDSCAPS pCaps)
{
	if (pCaps == NULL)
		return (E_POINTER);

	/*
	 * Preserve the caller's dwSize. Report no hardware mixing or 3D buffers
	 * to select software A2D. Advertising hardware 3D selects DAL_D3D, whose
	 * IID_IDirectSound3DBuffer query fails on the recording buffer. The
	 * reference then faults in ~D3DBuffer (rtl:0x1001C22C), reading
	 * m_pDevice->m_fUseReflections through null m_pDevice.
	 */

	pCaps->dwFlags                    = DSCAPS_PRIMARYSTEREO | DSCAPS_PRIMARY16BIT |
					    DSCAPS_SECONDARYSTEREO | DSCAPS_SECONDARY16BIT |
					    DSCAPS_CONTINUOUSRATE;
	pCaps->dwMinSecondarySampleRate   = 100;
	pCaps->dwMaxSecondarySampleRate   = 100000;
	pCaps->dwPrimaryBuffers           = 1;
	pCaps->dwMaxHwMixingAllBuffers    = 0;
	pCaps->dwMaxHwMixingStaticBuffers = 0;
	pCaps->dwMaxHwMixingStreamingBuffers = 0;
	pCaps->dwFreeHwMixingAllBuffers   = 0;
	pCaps->dwFreeHwMixingStaticBuffers = 0;
	pCaps->dwFreeHwMixingStreamingBuffers = 0;
	pCaps->dwMaxHw3DAllBuffers        = 0;
	pCaps->dwMaxHw3DStaticBuffers     = 0;
	pCaps->dwMaxHw3DStreamingBuffers  = 0;
	pCaps->dwFreeHw3DAllBuffers       = 0;
	pCaps->dwFreeHw3DStaticBuffers    = 0;
	pCaps->dwFreeHw3DStreamingBuffers = 0;

	return (S_OK);
}

STDMETHODIMP
RecordingDirectSound::DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER pOrig,
			     LPDIRECTSOUNDBUFFER *ppDup)
{
	if (ppDup == NULL)
		return (E_POINTER);

	/* A fresh empty recorder is enough; nothing here reads a duplicate. */
	*ppDup = (LPDIRECTSOUNDBUFFER) new RecordingSoundBuffer((const DSBUFFERDESC1 *) NULL);

	return (*ppDup ? S_OK : E_OUTOFMEMORY);
}

STDMETHODIMP RecordingDirectSound::SetCooperativeLevel(HWND hwnd, DWORD dwLevel)
{
	return (S_OK);
}

STDMETHODIMP RecordingDirectSound::Compact(void)
{
	return (S_OK);
}

STDMETHODIMP RecordingDirectSound::GetSpeakerConfig(LPDWORD pdwConfig)
{
	if (pdwConfig)
		*pdwConfig = DSSPEAKER_STEREO;
	return (S_OK);
}

STDMETHODIMP RecordingDirectSound::SetSpeakerConfig(DWORD dwConfig)
{
	return (S_OK);
}

STDMETHODIMP RecordingDirectSound::Initialize(LPCGUID pcGuidDevice)
{
	return (S_OK);
}

/* -------------------------------------------------------------------------- */
/* The class factory registered under CLSID_DirectSound.  It is a process-wide
   singleton, never freed, so AddRef/Release are constants. */

class RecordingSoundFactory : public IClassFactory
{
public:
	STDMETHODIMP QueryInterface(REFIID riid, void **ppv)
	{
		if (ppv == NULL)
			return (E_POINTER);

		if (IsEqualIID(riid, IID_IUnknown) ||
		    IsEqualIID(riid, IID_IClassFactory))
		{
			*ppv = (IClassFactory *) this;
			return (S_OK);
		}

		*ppv = NULL;
		return (E_NOINTERFACE);
	}

	STDMETHODIMP_(ULONG) AddRef(void)  { return (2); }
	STDMETHODIMP_(ULONG) Release(void) { return (1); }

	STDMETHODIMP CreateInstance(LPUNKNOWN pUnkOuter, REFIID riid, void **ppv)
	{
	RecordingDirectSound	*pDS;
	HRESULT	hr;

		if (ppv == NULL)
			return (E_POINTER);

		*ppv = NULL;

		if (pUnkOuter != NULL)
			return (CLASS_E_NOAGGREGATION);

		pDS = new RecordingDirectSound;

		if (pDS == NULL)
			return (E_OUTOFMEMORY);

		hr = pDS->QueryInterface(riid, ppv);

		pDS->Release();

		return (hr);
	}

	STDMETHODIMP LockServer(BOOL fLock) { return (S_OK); }
};

static RecordingSoundFactory	g_factory;

/* -------------------------------------------------------------------------- */

IUnknown* RecordingClassFactory() { return &g_factory; }

} // namespace a3dcapture
