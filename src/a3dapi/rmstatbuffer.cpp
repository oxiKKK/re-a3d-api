/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * rmstatbuffer.cpp
 *
 * Implements resource-manager playback over a retained DAL audio buffer.
 * ResManStatBuffer forwards audio access and transport operations,
 * submits pending source controls and tracks playback and notification
 * state.
 *
 * The resource manager attaches the underlying buffer and services
 * callbacks and release state. Shared muting and deferred property
 * behavior comes from ResManBuffer. Streaming playback with separate
 * waveform storage is implemented in rmstreambuffer.cpp.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "rmstatbuffer.h"
#include "outqueue.h"
#include "resman.h"
#include "dalinfo.h"

#include <new>
#include <stdio.h>
#include <string.h>


/* =============================================================
// ResManStatBuffer()
// (RE) dbg:0x1006d0d0
//
// Initialize a stopped static buffer for the resource manager.
// =============================================================*/

ResManStatBuffer::ResManStatBuffer(ResMan *pResMan, int nResourceManagerMode)
{
	m_lpResMan              = pResMan;
	m_cRef                  = 1;
	m_pDalBufferInfo        = NULL;
	m_lpA3dCtrlSuper        = NULL;
	m_lpA3dCtrlSuperPending = NULL;
	m_nResourceManagerMode  = nResourceManagerMode;

	m_Unknown_0x89C         = 0;
	m_Unknown_0x8A0         = 0;
	m_dwFrequency           = 100;
	m_dwOriginalSampleRate  = 0;
	m_dwBufferState         = A3DVOICE_STATE_STOPPED;
	m_dwVoiceFlags          = 0;

	m_cNotify        = 0;
	m_paNotify       = NULL;
	m_hNotifyMutex   = NULL;
	m_dwCurrentPos   = 0;
	m_dwRenderMode   = RESMANBUFFER_RENDER_3D;
}

/* =============================================================
// ResManStatBuffer scalar deleting destructor
// (RE) rtl:0x100295f0; dbg:0x1006d280
// =============================================================*/

/* =============================================================
// ~ResManStatBuffer()
// (RE) rtl:0x10029620; dbg:0x1006d2d0
//
// Close the notification mutex. Original defect: closes a null notify
// handle.
// =============================================================*/

ResManStatBuffer::~ResManStatBuffer(void)
{
	if (m_hNotifyMutex != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_hNotifyMutex);

		m_hNotifyMutex = INVALID_HANDLE_VALUE;
	}
}

/* =============================================================
// QueryInterface()
// (RE) rtl:0x10029670; dbg:0x1006d370
//
// Obtain a supported interface and add a reference to it.
//
// Returns:
//   S_OK
//   E_INVALIDARG   if ppv is null
//   E_NOINTERFACE  if the interface is unsupported
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::QueryInterface(REFIID riid, void **ppv)
{
	if (!ppv)
		return (E_INVALIDARG);

	if (IsEqualIID(riid, IID_IUnknown) ||
	    IsEqualIID(riid, IID_IDirectSoundBuffer))
	{
		*ppv = static_cast<IResManBufferPrimary *>(this);
	}
	else if (IsEqualIID(riid, IID_IA3dDalBuffer))
	{
		*ppv = static_cast<IA3dDalBuffer *>(this);
	}
	else if (IsEqualIID(riid, IID_A3dVoiceCtl))
	{
		*ppv = static_cast<IResManBuffer *>(this);
	}
	else if (IsEqualIID(riid, IID_IDirectSoundNotify))
	{
		*ppv = static_cast<IDirectSoundNotify *>(this);
	}
	else if (IsEqualIID(riid, IID_ResManStatBuffer))
	{
		*ppv = static_cast<IResManBufferPrimary *>(this);
	}
	else if (IsEqualIID(riid, IID_IA3dPropertySet))
	{
		*ppv = static_cast<IA3dPropertySet *>(this);
	}
	else
	{
		*ppv = NULL;

		return (E_NOINTERFACE);
	}

	((IUnknown *) *ppv)->AddRef();

	return (S_OK);
}

/* =============================================================
// AddRef()
// (RE) rtl:0x1002a6c0; dbg:0x1006d550
//
// Add a reference to the buffer.
//
// Returns: The updated reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
ResManStatBuffer::AddRef(void)
{
	InterlockedIncrement(&m_cRef);

	return ((ULONG) m_cRef);
}

/* =============================================================
// Release()
// (RE) rtl:0x100297e0; dbg:0x1006d590
//
// Release a reference; stop playback and mark the buffer released at zero.
// The creator owns destruction.
//
// Returns: The remaining reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
ResManStatBuffer::Release(void)
{
	InterlockedDecrement(&m_cRef);
	if (m_cRef)
		return ((ULONG) m_cRef);

	if (m_pDalBufferInfo)
	{
		ASSERT(m_pDalBufferInfo->GetDSBuffer() != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo->GetDSBuffer(),
				     sizeof(IDirectSoundBuffer)));

		m_pDalBufferInfo->GetDSBuffer()->Stop();
	}

	m_dwBufferState = A3DVOICE_STATE_RELEASED;

	return (0);
}

/* =============================================================
// GetA3dCtrlSuper()
// (RE) rtl:0x1002a460; dbg:0x1006d250
//
// Write the current super control block pointer to the output.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
ResManStatBuffer::GetA3dCtrlSuper(LPVOID *lplpCtrl)
{
	*lplpCtrl = (LPVOID) m_lpA3dCtrlSuper;

	return (S_OK);
}

/* =============================================================
// GetCaps()
// (RE) rtl:0x10029820; dbg:0x1006d6a0
//
// Read the DAL buffer capabilities and mark successful results as static.
//
// Returns:
//   The DAL buffer's result
//   E_FAIL                   if no DAL buffer is attached
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::GetCaps(LPDSBCAPS lpDSBufferCaps)
{
HRESULT	hr;

	ASSERT(lpDSBufferCaps != 0 &&
	       !IsBadReadPtr(lpDSBufferCaps, sizeof(DSBCAPS)));
	ASSERT(lpDSBufferCaps->dwSize == sizeof(DSBCAPS));

	if (m_pDalBufferInfo)
	{
		ASSERT(m_pDalBufferInfo->GetDSBuffer() != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo->GetDSBuffer(),
				     sizeof(IDirectSoundBuffer)));

		hr = m_pDalBufferInfo->GetDSBuffer()->GetCaps(lpDSBufferCaps);

		if (FAILED(hr))
			DBGSTR("ResManStatBuffer::GetCaps() - m_pDalBufferInfo->GetDSBuffer()->GetCaps failed\n");
		else
			lpDSBufferCaps->dwFlags |= DSBCAPS_STATIC;

		return (hr);
	}

	DBGSTR("ResManStatBuffer::GetCaps() - m_pDalBufferInfo is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// GetCurrentPosition()
// (RE) rtl:0x10029860; dbg:0x1006d850
//
// Read the playback and write cursors from the DAL buffer.
//
// Returns:
//   The DAL buffer's result
//   E_FAIL                   if no DAL buffer is attached
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::GetCurrentPosition(DWORD *pdwPlay, DWORD *pdwWrite)
{
	if (m_pDalBufferInfo)
	{
		ASSERT(m_pDalBufferInfo->GetDSBuffer() != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo->GetDSBuffer(),
				     sizeof(IDirectSoundBuffer)));

		return (m_pDalBufferInfo->GetDSBuffer()->GetCurrentPosition(
				pdwPlay, pdwWrite));
	}

	DBGSTR("ResManStatBuffer::GetCurrentPosition() - m_pDalBufferInfo is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// GetFormat()
// (RE) rtl:0x10029890; dbg:0x1006d940
//
// Read the audio format from the DAL buffer.
//
// Returns:
//   The DAL buffer's result
//   E_FAIL                   if no DAL buffer is attached
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::GetFormat(LPWAVEFORMATEX lpwfxFormat, DWORD cb,
			    DWORD *lpdwSizeWritten)
{
	ASSERT(lpwfxFormat != 0 &&
	       !IsBadReadPtr(lpwfxFormat, sizeof(WAVEFORMATEX)));
	ASSERT(lpdwSizeWritten != 0 &&
	       !IsBadReadPtr(lpdwSizeWritten, sizeof(DWORD)));

	if (m_pDalBufferInfo)
	{
		ASSERT(m_pDalBufferInfo->GetDSBuffer() != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo->GetDSBuffer(),
				     sizeof(IDirectSoundBuffer)));

		return (m_pDalBufferInfo->GetDSBuffer()->GetFormat(lpwfxFormat, cb,
								    lpdwSizeWritten));
	}

	DBGSTR("ResManStatBuffer::GetFormat() - m_pDalBufferInfo is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// GetVolume()
// (RE) rtl:0x100298d0; dbg:0x1006dad0
//
// Read the volume from the DAL buffer.
//
// Returns:
//   The DAL buffer's result
//   E_INVALIDARG             if the output pointer is null
//   E_FAIL                   if no DAL buffer is attached
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::GetVolume(LONG *lplVolume)
{
	ASSERT(lplVolume != 0 && !IsBadReadPtr(lplVolume, sizeof(LONG)));

	if (!lplVolume)
		return (E_INVALIDARG);

	if (m_pDalBufferInfo)
	{
		ASSERT(m_pDalBufferInfo->GetDSBuffer() != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo->GetDSBuffer(),
				     sizeof(IDirectSoundBuffer)));

		return (m_pDalBufferInfo->GetDSBuffer()->GetVolume(lplVolume));
	}

	DBGSTR("ResManStatBuffer::GetVolume() - m_pDalBufferInfo is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// GetPan()
// (RE) rtl:0x10029910; dbg:0x1006dc10
//
// Read the pan from the DAL buffer.
//
// Returns:
//   The DAL buffer's result
//   E_INVALIDARG             if the output pointer is null
//   E_FAIL                   if no DAL buffer is attached
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::GetPan(LONG *lplPan)
{
	ASSERT(lplPan != 0 && !IsBadReadPtr(lplPan, sizeof(LONG)));

	if (!lplPan)
		return (E_INVALIDARG);

	if (m_pDalBufferInfo)
	{
		ASSERT(m_pDalBufferInfo->GetDSBuffer() != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo->GetDSBuffer(),
				     sizeof(IDirectSoundBuffer)));

		return (m_pDalBufferInfo->GetDSBuffer()->GetPan(lplPan));
	}

	DBGSTR("ResManStatBuffer::GetPan() - m_pDalBufferInfo is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// GetFrequency()
// (RE) rtl:0x10029950; dbg:0x1006dd50
//
// Read the playback frequency from the DAL buffer.
//
// Returns:
//   The DAL buffer's result
//   E_FAIL                   if no DAL buffer is attached
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::GetFrequency(DWORD *lpdwFrequency)
{
	ASSERT(lpdwFrequency != 0 && !IsBadReadPtr(lpdwFrequency, sizeof(DWORD)));

	if (m_pDalBufferInfo)
	{
		ASSERT(m_pDalBufferInfo->GetDSBuffer() != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo->GetDSBuffer(),
				     sizeof(IDirectSoundBuffer)));

		return (m_pDalBufferInfo->GetDSBuffer()->GetFrequency(lpdwFrequency));
	}

	DBGSTR("ResManStatBuffer::GetFrequency() - m_pDalBufferInfo is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// GetStatus()
// (RE) rtl:0x10029980; dbg:0x1006de80
//
// Read playback status and clear a stale playing state.
//
// Returns: S_OK; the DAL error on failure; E_FAIL if no DAL buffer is attached.
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::GetStatus(DWORD *lpdwStatus)
{
HRESULT	hr;

	ASSERT(lpdwStatus != 0 && !IsBadReadPtr(lpdwStatus, sizeof(DWORD)));

	if (m_pDalBufferInfo)
	{
		ASSERT(m_pDalBufferInfo->GetDSBuffer() != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo->GetDSBuffer(),
				     sizeof(IDirectSoundBuffer)));

		hr = m_pDalBufferInfo->GetDSBuffer()->GetStatus(lpdwStatus);

		if (FAILED(hr))
		{
			DBGSTR("ResManStatBuffer::GetStatus() - Failed GetStatus() call.\n");
			return (hr);
		}

		if (!(*lpdwStatus & DSBSTATUS_PLAYING) &&
		    m_dwBufferState == A3DVOICE_STATE_PLAYING)
			m_dwBufferState = A3DVOICE_STATE_STOPPED;

		return (S_OK);
	}

	DBGSTR("ResManStatBuffer::GetStatus() - m_pDalBufferInfo is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// Initialize()
// (RE) rtl:0x1001b380; dbg:0x1006e010
//
// Accept initialization without changing the buffer.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::Initialize(LPDIRECTSOUND lpDirectSound,
			     LPCDSBUFFERDESC lpcDSBufferDesc)
{
	ASSERT(lpDirectSound != 0 &&
	       !IsBadReadPtr(lpDirectSound, sizeof(IDirectSound)));
	ASSERT(lpcDSBufferDesc != 0 &&
	       !IsBadReadPtr(lpcDSBufferDesc, sizeof(DSBUFFERDESC1)));

	return (S_OK);
}

/* =============================================================
// Lock()
// (RE) rtl:0x100299d0; dbg:0x1006e0d0
//
// Lock audio data for writing; a zero-length request clears the outputs.
//
// Returns: E_FAIL if no DAL buffer is attached; otherwise S_OK for zero length,
//          or the DAL buffer's result.
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::Lock(DWORD dwOffset, DWORD dwWriteBytes, LPVOID *ppvAudioPtr1,
		       LPDWORD pdwAudioBytes1, LPVOID *ppvAudioPtr2,
		       LPDWORD pdwAudioBytes2, DWORD dwFlags)
{
	if (m_pDalBufferInfo)
	{
		if (dwWriteBytes)
		{
			ASSERT(m_pDalBufferInfo->GetDSBuffer() != 0 &&
			       !IsBadReadPtr(m_pDalBufferInfo->GetDSBuffer(),
					     sizeof(IDirectSoundBuffer)));

			return (m_pDalBufferInfo->GetDSBuffer()->Lock(
					dwOffset, dwWriteBytes,
					ppvAudioPtr1, pdwAudioBytes1,
					ppvAudioPtr2, pdwAudioBytes2, dwFlags));
		}

		*ppvAudioPtr1   = NULL;
		*pdwAudioBytes1 = 0;
		*ppvAudioPtr2   = NULL;
		*pdwAudioBytes2 = 0;

		return (S_OK);
	}

	DBGSTR("ResManStatBuffer::Lock() - m_pDalBufferInfo is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// Play()
// (RE) rtl:0x10029a50; dbg:0x1006e220
//
// Send pending controls and start playback on the DAL buffer.
//
// Returns:
//   The DAL buffer's result
//   E_FAIL                   if no DAL buffer is attached
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::Play(DWORD dwReserved1, DWORD dwPriority, DWORD dwFlags)
{
IDirectSoundBuffer     *pBuffer;
HRESULT                 hr;

	if (m_pDalBufferInfo)
	{
		SendPendingCtrl();

		pBuffer = m_pDalBufferInfo->GetDSBuffer();

		ASSERT(pBuffer != 0 &&
		       !IsBadReadPtr(pBuffer, sizeof(IDirectSoundBuffer)));

		hr = pBuffer->Play(dwReserved1, dwPriority, dwFlags);

		if (!FAILED(hr))
		{
			m_dwBufferState = A3DVOICE_STATE_PLAYING;
		}
		else
		{
			DBGSTR("ResManStatBuffer::Play() - Play on output buffer failed.\n");
		}

		return (hr);
	}

	DBGSTR("ResManStatBuffer::Play() - m_pDalBufferInfo is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// SetCurrentPosition()
// (RE) rtl:0x10029aa0; dbg:0x1006e330
//
// Remember the requested playback position and pass it to the DAL buffer.
//
// Returns:
//   The DAL buffer's result
//   E_FAIL                   if no DAL buffer is attached
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::SetCurrentPosition(DWORD dwPlay)
{
	if (m_pDalBufferInfo)
	{
		ASSERT(m_pDalBufferInfo->GetDSBuffer() != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo->GetDSBuffer(),
				     sizeof(IDirectSoundBuffer)));

		m_dwCurrentPos = dwPlay;

		return (m_pDalBufferInfo->GetDSBuffer()->SetCurrentPosition(dwPlay));
	}

	DBGSTR("ResManStatBuffer::SetCurrentPosition() - m_pDalBufferInfo is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// SetFormat()
// (RE) rtl:0x10029ad0; dbg:0x1006e420
//
// Set the audio format on the DAL buffer.
//
// Returns:
//   The DAL buffer's result
//   E_FAIL                   if no DAL buffer is attached
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::SetFormat(LPCWAVEFORMATEX pwfx)
{
	if (m_pDalBufferInfo)
	{
		ASSERT(m_pDalBufferInfo->GetDSBuffer() != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo->GetDSBuffer(),
				     sizeof(IDirectSoundBuffer)));

		return (m_pDalBufferInfo->GetDSBuffer()->SetFormat(pwfx));
	}

	DBGSTR("ResManStatBuffer::SetFormat() - m_pDalBufferInfo is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// SetVolume()
// (RE) rtl:0x10029b00; dbg:0x1006e500
//
// Set the volume on the DAL buffer.
//
// Returns:
//   The DAL buffer's result
//   E_FAIL                   if no DAL buffer is attached
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::SetVolume(LONG lVolume)
{
	if (m_pDalBufferInfo)
	{
		ASSERT(m_pDalBufferInfo->GetDSBuffer() != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo->GetDSBuffer(),
				     sizeof(IDirectSoundBuffer)));

		return (m_pDalBufferInfo->GetDSBuffer()->SetVolume(lVolume));
	}

	DBGSTR("ResManStatBuffer::SetVolume() - m_pDalBufferInfo is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// SetPan()
// (RE) rtl:0x10029b30; dbg:0x1006e5e0
//
// Accept and discard pan changes, as in the original.
//
// Returns:
//   S_OK
//   E_FAIL  if no DAL buffer is attached
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::SetPan(LONG lPan)
{
	if (m_pDalBufferInfo)
	{
		ASSERT(m_pDalBufferInfo->GetDSBuffer() != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo->GetDSBuffer(),
				     sizeof(IDirectSoundBuffer)));

		return (S_OK);
	}

	DBGSTR("ResManStatBuffer::SetPan() - m_pDalBufferInfo is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// SetFrequency()
// (RE) rtl:0x10029b50; dbg:0x1006e690
//
// Set the playback frequency on the DAL buffer.
//
// Returns:
//   The DAL buffer's result
//   E_FAIL                   if no DAL buffer is attached
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::SetFrequency(DWORD dwFrequency)
{
	if (m_pDalBufferInfo)
	{
		ASSERT(m_pDalBufferInfo->GetDSBuffer() != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo->GetDSBuffer(),
				     sizeof(IDirectSoundBuffer)));

		return (m_pDalBufferInfo->GetDSBuffer()->SetFrequency(dwFrequency));
	}

	DBGSTR("ResManStatBuffer::SetFrequency() - m_pDalBufferInfo is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// Stop()
// (RE) rtl:0x10029b80; dbg:0x1006e770
//
// Signal stop notifications, mark the buffer stopped and stop DAL playback.
//
// Returns:
//   The DAL buffer's result
//   E_FAIL                   if no DAL buffer is attached
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::Stop(void)
{
HRESULT		hr;
unsigned int	i;

	if (m_pDalBufferInfo)
	{
		if (m_cNotify)
		{
			WaitForSingleObject(m_hNotifyMutex, INFINITE);

			for (i = 0; i < m_cNotify; i++)
			{
				if (m_paNotify[i].dwOffset == DSBPN_OFFSETSTOP)
					SetEvent(m_paNotify[i].hEventNotify);
			}

			VERIFY(ReleaseMutex(m_hNotifyMutex));
		}

		m_dwBufferState = A3DVOICE_STATE_STOPPED;

		ASSERT(m_pDalBufferInfo->GetDSBuffer() != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo->GetDSBuffer(),
				     sizeof(IDirectSoundBuffer)));

		hr = m_pDalBufferInfo->GetDSBuffer()->Stop();

		if (FAILED(hr))
			DBGSTR("ResManStatBuffer::Stop - Failed to Stop the buffer.\n");

		return (hr);
	}

	DBGSTR("ResManStatBuffer::Stop() - m_pDalBufferInfo is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// Unlock()
// (RE) rtl:0x10029c20; dbg:0x1006e960
//
// Release locked audio data on the DAL buffer.
//
// Returns: E_FAIL if no DAL buffer is attached; otherwise S_OK when both byte
//          counts are zero, or the DAL buffer's result.
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::Unlock(LPVOID pvAudioPtr1, DWORD dwAudioBytes1,
			 LPVOID pvAudioPtr2, DWORD dwAudioBytes2)
{
HRESULT	hr;

	if (m_pDalBufferInfo)
	{
		ASSERT(m_pDalBufferInfo->GetDSBuffer() != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo->GetDSBuffer(),
				     sizeof(IDirectSoundBuffer)));

		hr = m_pDalBufferInfo->GetDSBuffer()->Unlock(
				pvAudioPtr1, dwAudioBytes1,
				pvAudioPtr2, dwAudioBytes2);

		if (!dwAudioBytes1 && !dwAudioBytes2)
			return (S_OK);

		return (hr);
	}

	DBGSTR("ResManStatBuffer::Unlock() - m_pDalBufferInfo is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// Restore()
// (RE) rtl:0x10029c70; dbg:0x1006ea70
//
// Ask the DAL buffer to restore its audio storage.
//
// Returns:
//   The DAL buffer's result
//   E_FAIL                   if no DAL buffer is attached
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::Restore(void)
{
	if (m_pDalBufferInfo)
	{
		ASSERT(m_pDalBufferInfo->GetDSBuffer() != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo->GetDSBuffer(),
				     sizeof(IDirectSoundBuffer)));

		return (m_pDalBufferInfo->GetDSBuffer()->Restore());
	}

	DBGSTR("ResManStatBuffer::Restore() - m_pDalBufferInfo is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// GetAllocationStatus()
// (RE) rtl:0x10029ca0; dbg:0x1006eb70
//
// Read the allocation status from the DAL buffer.
//
// Returns:
//   The DAL buffer's result
//   E_FAIL                   if no DAL buffer is attached
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::GetAllocationStatus(LPDWORD lpdwStatus)
{
	ASSERT(lpdwStatus != 0 && !IsBadReadPtr(lpdwStatus, sizeof(DWORD)));

	if (m_pDalBufferInfo != NULL)
	{
		ASSERT(m_pDalBufferInfo != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo, sizeof(DalBufferInfo)));
		ASSERT(m_pDalBufferInfo->GetIDalBuffer() != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo->GetIDalBuffer(),
				     sizeof(IA3dDalBuffer)));

		return (m_pDalBufferInfo->GetIDalBuffer()->GetAllocationStatus(lpdwStatus));
	}

	DBGSTR("ResManStatBuffer::GetAllocationStatus() - m_pDalBufferInfo is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// GetWave()
// (RE) rtl:0x10029cd0; dbg:0x1006ed00
//
// Obtain the wave data pointer from the DAL buffer.
//
// Returns:
//   The DAL buffer's result
//   E_FAIL                   if no DAL buffer is attached
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::GetWave(LPBYTE *lplpbWave)
{
	ASSERT(lplpbWave != 0);

	if (m_pDalBufferInfo != NULL)
	{
		ASSERT(m_pDalBufferInfo != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo, sizeof(DalBufferInfo)));
		ASSERT(m_pDalBufferInfo->GetIDalBuffer() != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo->GetIDalBuffer(),
				     sizeof(IA3dDalBuffer)));

		return (m_pDalBufferInfo->GetIDalBuffer()->GetWave(lplpbWave));
	}

	DBGSTR("ResManStatBuffer::GetWave() - m_pDalBufferInfo is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// GetDriverInfo()
// (RE) rtl:0x10029d00; dbg:0x1006ee80
//
// Obtain the underlying driver buffer interfaces from the DAL buffer.
//
// Returns:
//   The DAL buffer's result
//   E_FAIL                   if no DAL buffer is attached
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::GetDriverInfo(void **lplpIDsDriverBuffer,
				void **lplpIA3dDriverBuffer)
{
	ASSERT(lplpIDsDriverBuffer != 0);
	ASSERT(lplpIA3dDriverBuffer != 0);

	if (m_pDalBufferInfo != NULL)
	{
		ASSERT(m_pDalBufferInfo != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo, sizeof(DalBufferInfo)));
		ASSERT(m_pDalBufferInfo->GetIDalBuffer() != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo->GetIDalBuffer(),
				     sizeof(IA3dDalBuffer)));

		return (m_pDalBufferInfo->GetIDalBuffer()->GetDriverInfo(
				lplpIDsDriverBuffer, lplpIA3dDriverBuffer));
	}

	DBGSTR("ResManStatBuffer::GetDriverInfo() - m_pDalBufferInfo is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// SetNewBuffer()
// (RE) rtl:0x10029d30; dbg:0x1006f040
//
// Pass replacement wave storage to the DAL buffer.
//
// Returns:
//   The DAL buffer's result
//   E_FAIL                   if no DAL buffer is attached
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::SetNewBuffer(LPBYTE lpbWave, DWORD dwBufferBytes)
{
	ASSERT(lpbWave != 0);

	if (m_pDalBufferInfo != NULL)
	{
		ASSERT(m_pDalBufferInfo != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo, sizeof(DalBufferInfo)));
		ASSERT(m_pDalBufferInfo->GetIDalBuffer() != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo->GetIDalBuffer(),
				     sizeof(IA3dDalBuffer)));

		return (m_pDalBufferInfo->GetIDalBuffer()->SetNewBuffer(lpbWave,
									 dwBufferBytes));
	}

	DBGSTR("ResManStatBuffer::SetNewBuffer() - m_pDalBufferInfo is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// SetA3dDirectCtrl()
// (RE) rtl:0x10029d60; dbg:0x1006f1f0
//
// Pass direct source controls to the DAL buffer.
//
// Returns:
//   The DAL buffer's result
//   E_FAIL                   if no DAL buffer is attached
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::SetA3dDirectCtrl(LPA3DCTRL_SRC_SUPER lpA3dCtrlDirect,
				   DWORD dwSize)
{
	ASSERT(lpA3dCtrlDirect != 0);
	ASSERT(dwSize > 0);

	if (m_pDalBufferInfo != NULL)
	{
		ASSERT(m_pDalBufferInfo != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo, sizeof(DalBufferInfo)));
		ASSERT(m_pDalBufferInfo->GetIDalBuffer() != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo->GetIDalBuffer(),
				     sizeof(IA3dDalBuffer)));

		return (m_pDalBufferInfo->GetIDalBuffer()->SetA3dDirectCtrl(
				lpA3dCtrlDirect, dwSize));
	}

	DBGSTR("ResManStatBuffer::SetA3dDirectCtrl() - m_pDalBufferInfo is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// GetStatusEx()
// (RE) rtl:0x100034f0; dbg:0x1006f3b0
//
// Accept the status query without writing the output, as in the original.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::GetStatusEx(LPDWORD lpdwStatusEx)
{
	ASSERT(lpdwStatusEx != 0 && !IsBadReadPtr(lpdwStatusEx, sizeof(DWORD)));

	return (S_OK);
}

/* =============================================================
// GetPriority()
// (RE) rtl:0x10020b90; dbg:0x1006f420
//
// Reject priority queries for static buffers. The original uses the
// SetPriority diagnostic here.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::GetPriority(FLOAT *pfPriority)
{
	DBGSTR("ResManStatBuffer::SetPriority() - Static Buffers are not resource managed.\n");

	return (E_NOTIMPL);
}

/* =============================================================
// GetBufferState()
// (RE) rtl:0x1002ac90; dbg:0x1006f450
//
// Read the stored voice state.
//
// Unresolved: attribution of the state-range assertion to this body.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::GetBufferState(LPDWORD lpdwBufferState)
{
	ASSERT(lpdwBufferState != 0 &&
	       !IsBadReadPtr(lpdwBufferState, sizeof(DWORD)));
	ASSERT(m_dwBufferState >= 0x00000001 && m_dwBufferState <= 0x00000003);

	*lpdwBufferState = m_dwBufferState;

	return (S_OK);
}

/* =============================================================
// SetPriority()
// (RE) rtl:0x10020b90; dbg:0x1006f5b0
//
// Reject priority changes for static buffers.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::SetPriority(FLOAT fPriority)
{
	DBGSTR("ResManStatBuffer::SetPriority() - Static Buffers are not resource managed.\n");

	return (E_NOTIMPL);
}

/* =============================================================
// MuteForFocusLoss()
// (RE) rtl:0x10029d90; dbg:0x1006f5e0
//
// Mark the buffer muted for focus loss and request muting.
//
// Returns: S_OK; the mute result is ignored.
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::MuteForFocusLoss(void)
{
	m_dwVoiceFlags |= RESMANBUFFER_FLAG_FOCUSMUTE;

	MuteBuffer();

	return (S_OK);
}

/* =============================================================
// UnMuteForFocusGain()
// (RE) rtl:0x10029dc0; dbg:0x1006f630
//
// Clear the focus mute flag and request unmuting.
//
// Returns: S_OK; the unmute result is ignored.
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::UnMuteForFocusGain(void)
{
	m_dwVoiceFlags &= ~RESMANBUFFER_FLAG_FOCUSMUTE;

	UnMuteBuffer();

	return (S_OK);
}

/* =============================================================
// SetRenderMode()
// (RE) rtl:0x1002a290; dbg:0x10070260
//
// Store the render mode while reporting the operation as unsupported.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::SetRenderMode(DWORD dwRenderMode)
{
	m_dwRenderMode = dwRenderMode;

	return (E_NOTIMPL);
}

/* =============================================================
// GetRenderMode()
// (RE) rtl:0x1002a2b0; dbg:0x10070290
//
// Read the buffer's render mode.
//
// Returns:
//   S_OK
//   E_INVALIDARG  if the output pointer is null
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::GetRenderMode(LPDWORD lpdwRenderMode)
{
	if (lpdwRenderMode)
	{
		*lpdwRenderMode = m_dwRenderMode;

		return (S_OK);
	}

	return (E_INVALIDARG);
}

/* =============================================================
// SetNotificationPositions()
// (RE) rtl:0x10029df0; dbg:0x1006f680
//
// Replace or clear the notification array, retaining the caller's storage.
//
// Returns:
//   S_OK
//   E_INVALIDARG   a nonzero count with a null array
//   E_OUTOFMEMORY  if mutex creation fails
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::SetNotificationPositions(DWORD cPositions,
					   LPCDSBPOSITIONNOTIFY pcPositionNotifies)
{
	if (cPositions && !pcPositionNotifies)
		return (E_INVALIDARG);

	if (cPositions)
	{
		if (!m_hNotifyMutex)
		{
			m_hNotifyMutex = CreateMutexA(NULL, FALSE, NULL);

			if (!m_hNotifyMutex)
				return (E_OUTOFMEMORY);
		}

		WaitForSingleObject(m_hNotifyMutex, INFINITE);

		m_cNotify       = cPositions;
		m_paNotify      = (LPDSBPOSITIONNOTIFY) pcPositionNotifies;

		VERIFY(ReleaseMutex(m_hNotifyMutex));
	}
	else if (m_cNotify)
	{
		WaitForSingleObject(m_hNotifyMutex, INFINITE);

		m_cNotify       = 0;
		m_paNotify      = NULL;

		VERIFY(ReleaseMutex(m_hNotifyMutex));
	}

	return (S_OK);
}

/* =============================================================
// Init()
// (RE) rtl:0x10029ec0; dbg:0x1006f840
//
// Copy the buffer description and audio format, and select the initial
// render mode.
//
// Returns:
//   S_OK
//   E_POINTER  if the description is null
// =============================================================*/

HRESULT
ResManStatBuffer::Init(const DSBUFFERDESC1 *lpDSBdesc)
{
const WAVEFORMATEX	*pwfx;

	ASSERT(lpDSBdesc != 0 &&
	       !IsBadReadPtr(lpDSBdesc, sizeof(DSBUFFERDESC1)));

	if (!lpDSBdesc)
	{
		DBGSTR("ResManStatBuffer::Init() - NULL pointer passed in for lpDSBdesc.\n");

		return (E_POINTER);
	}

	m_DSBufferDesc = *lpDSBdesc;

	pwfx = lpDSBdesc->lpwfxFormat;

	m_wfxFormat                     = *pwfx;
	m_DSBufferDesc.lpwfxFormat      = &m_wfxFormat;

	m_dwOriginalSampleRate  = pwfx->nSamplesPerSec;
	m_dwFrequency           = pwfx->nSamplesPerSec;

	if (pwfx->nChannels == 2)
		m_dwVoiceFlags |= A3DVOICE_FLAG_STEREO;

	if (pwfx->wBitsPerSample == 16)
		m_dwVoiceFlags |= A3DVOICE_FLAG_16BIT;

	if (lpDSBdesc->dwFlags & DSBCAPS_CTRL3D)
	{
		m_dwRenderMode = RESMANBUFFER_RENDER_3D;
	}
	else
	{
		m_dwRenderMode = RESMANBUFFER_RENDER_DEFAULT;

		if (m_wfxFormat.nChannels == 4 &&
		    (m_lpResMan->GetDalTypeMask() & RESMANBUFFER_RENDER_FOUR_CHANNEL))
			m_dwRenderMode = RESMANBUFFER_RENDER_FOUR_CHANNEL;
	}

	return (S_OK);
}

/* =============================================================
// AttachDalBufferInfo()
// (RE) rtl:0x1002a250; dbg:0x100701a0
//
// Attach the DAL buffer and queue any saved super controls.
//
// Returns: S_OK; the control update result is ignored.
// =============================================================*/

HRESULT
ResManStatBuffer::AttachDalBufferInfo(DalBufferInfo *lpDalBufferInfo)
{
	ASSERT(lpDalBufferInfo != 0 &&
	       !IsBadReadPtr(lpDalBufferInfo, sizeof(DalBufferInfo)));

	m_pDalBufferInfo = lpDalBufferInfo;

	if (m_lpA3dCtrlSuper)
		SetA3dSuperCtrl(m_lpA3dCtrlSuper, m_dwA3dCtrlSuperSize);

	return (S_OK);
}

/* =============================================================
// Duplicate()
// (RE) rtl:0x10029fe0; dbg:0x1006fb30
//
// Copy static buffer settings and saved controls from another buffer.
//
// Returns: S_OK; the QueryInterface error if IID_ResManStatBuffer is
//          unavailable.
// =============================================================*/

HRESULT
ResManStatBuffer::Duplicate(LPDIRECTSOUNDBUFFER lpDSB)
{
ResManStatBuffer       *pOriginalResManStatBuffer;
HRESULT                 hr;

	ASSERT(lpDSB != 0 && !IsBadReadPtr(lpDSB, sizeof(IDirectSoundBuffer)));

	pOriginalResManStatBuffer = NULL;

	hr = lpDSB->QueryInterface(IID_ResManStatBuffer,
				   (void **) &pOriginalResManStatBuffer);

	if (FAILED(hr))
	{
		DBGSTR("ResManStatBuffer::Duplicate() - Failed to get ResManStat interface.\n");

		return (hr);
	}

	ASSERT(pOriginalResManStatBuffer != 0 &&
	       !IsBadReadPtr(pOriginalResManStatBuffer, sizeof(ResManStatBuffer)));

	if (pOriginalResManStatBuffer)
	{
		m_DSBufferDesc                  = pOriginalResManStatBuffer->m_DSBufferDesc;
		m_wfxFormat                     = pOriginalResManStatBuffer->m_wfxFormat;
		m_DSBufferDesc.lpwfxFormat      = &m_wfxFormat;

		m_dwVoiceFlags          = pOriginalResManStatBuffer->m_dwVoiceFlags;
		m_dwFrequency           = pOriginalResManStatBuffer->m_dwFrequency;
		m_dwOriginalSampleRate  = pOriginalResManStatBuffer->m_dwOriginalSampleRate;
		m_dwRenderMode          = pOriginalResManStatBuffer->m_dwRenderMode;

		if (pOriginalResManStatBuffer->m_lpA3dCtrlSuper)
			memcpy(m_aCtrlBuffers, pOriginalResManStatBuffer->m_lpA3dCtrlSuper,
			       sizeof(m_aCtrlBuffers[0]));

		m_lpA3dCtrlSuper        = &m_aCtrlBuffers[0];
		m_lpA3dCtrlSuperPending = &m_aCtrlBuffers[0];
		m_dwA3dCtrlSuperSize    = sizeof(m_aCtrlBuffers[0]);

		if (pOriginalResManStatBuffer)
		{
			pOriginalResManStatBuffer->Release();

			pOriginalResManStatBuffer = NULL;
		}
	}

	return (S_OK);
}

/* =============================================================
// GetDalBufferInfo()
// (RE) rtl:0x10029fa0; dbg:0x1006fa70
//
// Write the attached DAL buffer pointer to the output.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
ResManStatBuffer::GetDalBufferInfo(DalBufferInfo **lplpDalBufferInfo)
{
	ASSERT(lplpDalBufferInfo != 0 &&
	       !IsBadReadPtr(lplpDalBufferInfo, sizeof(DalBufferInfo *)));

	*lplpDalBufferInfo = m_pDalBufferInfo;

	return (S_OK);
}

/* =============================================================
// SetDalBufferInfoBare()
// (RE) rtl:0x10029fc0; dbg:0x1006fb00
//
// Store the DAL buffer pointer without applying saved controls.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
ResManStatBuffer::SetDalBufferInfoBare(DalBufferInfo *lpDalBufferInfo)
{
	m_pDalBufferInfo = lpDalBufferInfo;

	return (S_OK);
}

/* =============================================================
// Tick()
// (RE) rtl:0x1002a0d0; dbg:0x1006fda0
//
// Signal notifications for playback progress and stopped playback. Original
// defects: ignores GetCurrentPosition failure and dereferences a null DAL
// buffer when notifications are registered.
//
// Returns: S_OK; cursor and status query failures are not propagated.
// =============================================================*/

HRESULT
ResManStatBuffer::Tick(int nWhen)
{
DWORD		dwOldPos;
DWORD		dwPlay;
DWORD		dwWrite;
DWORD		dwStatus;
unsigned int	i;

	if (m_cNotify)
	{
		dwOldPos = m_dwCurrentPos;
		dwPlay   = 0;
		dwWrite  = 0;

		GetCurrentPosition(&dwPlay, &dwWrite);

		m_dwCurrentPos = dwPlay;

		WaitForSingleObject(m_hNotifyMutex, INFINITE);

		if (dwOldPos > dwPlay)
		{
			for (i = 0; i < m_cNotify; i++)
			{
				if (m_paNotify[i].dwOffset >= dwOldPos &&
				    m_paNotify[i].dwOffset < m_DSBufferDesc.dwBufferBytes)
					SetEvent(m_paNotify[i].hEventNotify);
			}

			for (i = 0; i < m_cNotify; i++)
			{
				if (m_paNotify[i].dwOffset < dwPlay)
					SetEvent(m_paNotify[i].hEventNotify);
			}
		}
		else
		{
			for (i = 0; i < m_cNotify; i++)
			{
				if (m_paNotify[i].dwOffset >= dwOldPos &&
				    m_paNotify[i].dwOffset < dwPlay)
					SetEvent(m_paNotify[i].hEventNotify);
			}
		}

		ASSERT(m_pDalBufferInfo != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo, sizeof(DalBufferInfo)));
		ASSERT(m_pDalBufferInfo->GetDSBuffer() != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo->GetDSBuffer(),
				     sizeof(IDirectSoundBuffer)));

		dwStatus = 0;

		m_pDalBufferInfo->GetDSBuffer()->GetStatus(&dwStatus);

		if (!(dwStatus & DSBSTATUS_PLAYING))
		{
			for (i = 0; i < m_cNotify; i++)
			{
				if (m_paNotify[i].dwOffset == DSBPN_OFFSETSTOP)
					SetEvent(m_paNotify[i].hEventNotify);
			}

			m_dwBufferState = A3DVOICE_STATE_STOPPED;
		}

		VERIFY(ReleaseMutex(m_hNotifyMutex));
	}

	return (S_OK);
}

/* =============================================================
// Unknown_0x74()
// (RE) rtl:0x10020b90; dbg:0x10070310
//
// Reject the call; the purpose of primary slot 29 is unknown.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
ResManStatBuffer::Unknown_0x74(DWORD dwArg)
{
	return (E_NOTIMPL);
}

/* (RE) Compiler-generated adjustors:
 * IA3dDalBuffer slot 3 -> ResManBuffer::SetA3dSuperCtrl:
 * dbg:0x1006eb40; rtl:0x1002b3e0.
 * IResManBuffer slot 11 -> ResManBuffer::GetCtrlBuffers:
 * dbg:0x100702e0; rtl:0x1002b490.
 */
