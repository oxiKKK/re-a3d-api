/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * d3dbuffer.cpp
 *
 * Implements D3DBuffer, a DAL voice backed by a hardware DirectSound3D
 * buffer. It forwards buffer operations and translates source controls
 * into DS3D parameters or the available A3D property-set controls.
 *
 * CReflection manages duplicated buffers used for reflected paths,
 * including their gain, pitch, age and delayed playback synchronization.
 * DAL_D3D creates the voices and services their reflections on its worker
 * thread. This file owns the per-voice control and reflection behavior.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "dal_d3d.h"
#include "d3dbuffer.h"

#include <math.h>

/* Reflection timing and pitch factors */
#define A3D_D3D_REFLECTION_AGE_STEP_S           0.01f
#define A3D_D3D_REFLECTION_START_MARGIN_S       0.01f
#define A3D_D3D_REFLECTION_START_THRESHOLD_S    0.005f
#define A3D_D3D_REFLECTION_DRIFT_THRESHOLD_S    0.01
#define A3D_D3D_REFLECTION_SETTLE_THRESHOLD_S   0.005
#define A3D_D3D_REFLECTION_PITCH_DOWN           0.99f
#define A3D_D3D_REFLECTION_PITCH_UP             1.01f

/* DS3D fallback constants  */
#define A3D_D3D_LOG_GAIN_MULTIPLIER     0.2
#define A3D_D3D_VOLUME_SCALE            10000.0
#define A3D_D3D_MIN_DISTANCE            0.5f
#define A3D_D3D_MAX_DISTANCE            2.0f

/* =============================================================
// D3DBuffer::D3DBuffer()
// (RE) rtl:0x1001c160; dbg:0x100440b0
//
// Construct an empty DirectSound3D voice.
// =============================================================*/

D3DBuffer::D3DBuffer(void)
{
int	i;

	m_cRef          = 1;
	m_fPriority     = 0;
	m_dwBufferState = 0;
	m_fPropSetA3d   = 0;

	m_lpDirectSoundBuffer = NULL;
	m_lpDS3DBuffer        = NULL;

	m_pList                 = NULL;
	m_lpPropertySet         = NULL;
	m_hBufferListMutex      = NULL;
	m_lpDalD3D              = NULL;

	m_bIsReflection         = 0;
	m_fReflectionsAged      = 0;
	m_fReflectionAge        = 0;

	m_hReflectionMutex = NULL;
	m_cReflectionCount = 0;

	for (i = 0; i < A3D_MAX_SOURCE_REFLECTIONS; i++)
		m_apReflection[i] = NULL;
}

/* =============================================================
// D3DBuffer::~D3DBuffer()
// (RE) rtl:0x1001C200; dbg:0x10044250
//
// Delete reflections, release the wrapped interfaces and unlink the voice.
// Original destruction before Init assigns the device dereferences null.
// =============================================================*/

D3DBuffer::~D3DBuffer(void)
{
int	i;

	if (m_lpDalD3D->m_fUseReflections)
	{
		if (!m_bIsReflection)
		{
			VERIFY(WaitForSingleObject(m_hBufferListMutex, INFINITE) ==
			       WAIT_OBJECT_0);
		}

		if (m_hReflectionMutex != NULL)
		{
			VERIFY(WaitForSingleObject(m_hReflectionMutex, INFINITE) ==
			       WAIT_OBJECT_0);
		}

		if (m_cReflectionCount)
		{
			for (i = 0; i < A3D_MAX_SOURCE_REFLECTIONS; i++)
			{
				if (m_apReflection[i] != NULL)
				{
					delete m_apReflection[i];
					m_apReflection[i] = NULL;

					VERIFY((LONG) --m_cReflectionCount >= 0);

					if (!m_cReflectionCount)
						break;
				}
			}
		}

		if (m_hReflectionMutex != NULL)
		{
			VERIFY(ReleaseMutex(m_hReflectionMutex));
			VERIFY(CloseHandle(m_hReflectionMutex));

			m_hReflectionMutex = NULL;
		}
	}

	if (m_lpPropertySet != NULL)
	{
		m_lpPropertySet->Release();
		m_lpPropertySet = NULL;
	}

	if (m_lpDS3DBuffer != NULL)
	{
		m_lpDS3DBuffer->Release();
		m_lpDS3DBuffer = NULL;
	}

	if (m_lpDirectSoundBuffer != NULL)
	{
		m_lpDirectSoundBuffer->Release();
		m_lpDirectSoundBuffer = NULL;
	}

	if (m_pList != NULL)
	{
		if (!m_bIsReflection)
		{
			if (m_pList->Find(this) != NULL)
				m_pList->RemoveAt(m_pList->Find(this));
		}
	}

	if (m_lpDalD3D->m_fUseReflections && !m_bIsReflection)
		VERIFY(ReleaseMutex(m_hBufferListMutex));
}

/* =============================================================
// D3DBuffer::QueryInterface()
// (RE) rtl:0x1001b8f0; dbg:0x10044710
//
// Return an AddRef'd interface. The original also returns IDirectSoundBuffer
// for IID_IDirectSoundNotify.
//
// Returns:
//   S_OK
//   E_INVALIDARG   a null output
//   E_NOINTERFACE  an unknown IID
// =============================================================*/

STDMETHODIMP
D3DBuffer::QueryInterface(REFIID riid, void **ppv)
{
	if (ppv == NULL)
	{
		DBGSTR("D3DBuffer::QueryInterface() - ppv is NULL.\n");

		return (E_INVALIDARG);
	}

	if (IsEqualIID(riid, IID_IUnknown) ||
	    IsEqualIID(riid, IID_IDirectSoundBuffer))
	{
		*ppv = (IDirectSoundBuffer *) this;
	}
	else if (IsEqualIID(riid, IID_IA3dDalBuffer))
	{
		*ppv = (IA3dDalBuffer *) this;
	}
	else if (IsEqualIID(riid, IID_A3dVoiceCtl))
	{
		*ppv = (IResManBuffer *) this;
	}
	else if (IsEqualIID(riid, IID_IDirectSoundNotify))
	{
		*ppv = (IDirectSoundBuffer *) this;
	}
	else if (IsEqualIID(riid, IID_IKsPropertySet))
	{
		*ppv = (IKsPropertySet *) this;
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
// D3DBuffer::AddRef()
// (RE) rtl:0x1001c3e0; dbg:0x100448b0
//
// Take a reference.
//
// Returns: Reference count read after the interlocked increment.
// =============================================================*/

STDMETHODIMP_(ULONG)
D3DBuffer::AddRef(void)
{
	InterlockedIncrement(&m_cRef);

	return ((ULONG) m_cRef);
}

/* =============================================================
// D3DBuffer::Release()
// (RE) rtl:0x1001c400; dbg:0x100448e0
//
// Release a reference and delete the voice at zero.
//
// Returns: Reference count read after the interlocked decrement; zero after
//          deletion.
// =============================================================*/

STDMETHODIMP_(ULONG)
D3DBuffer::Release(void)
{
	InterlockedDecrement(&m_cRef);

	if (m_cRef != 0)
		return ((ULONG) m_cRef);

	delete this;

	return (0);
}

/* =============================================================
// D3DBuffer::GetCaps()
// (RE) rtl:0x1001c440; dbg:0x100449a0
//
// Forward to the wrapped buffer's GetCaps.
//
// Returns:
//   DirectSound GetCaps result
//   E_FAIL                      without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D3DBuffer::GetCaps(LPDSBCAPS lpDSBufferCaps)
{
	if (m_lpDirectSoundBuffer != NULL)
	{
		ASSERT(lpDSBufferCaps != 0 &&
		       !IsBadReadPtr(lpDSBufferCaps, sizeof(DSBCAPS)));
		ASSERT(m_lpDirectSoundBuffer != 0 &&
		       !IsBadReadPtr(m_lpDirectSoundBuffer, sizeof(IDirectSoundBuffer)));

		return (m_lpDirectSoundBuffer->GetCaps(lpDSBufferCaps));
	}

	DBGSTR("D3DBuffer::GetCaps - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D3DBuffer::GetCurrentPosition()
// (RE) rtl:0x1001c470; dbg:0x10044ab0
//
// Forward to the wrapped buffer's GetCurrentPosition.
//
// Returns:
//   DirectSound GetCurrentPosition result
//   E_FAIL                              without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D3DBuffer::GetCurrentPosition(LPDWORD pdwCurrentPlayCursor,
			      LPDWORD pdwCurrentWriteCursor)
{
	if (m_lpDirectSoundBuffer != NULL)
	{
		ASSERT(m_lpDirectSoundBuffer != 0 &&
		       !IsBadReadPtr(m_lpDirectSoundBuffer, sizeof(IDirectSoundBuffer)));

		return (m_lpDirectSoundBuffer->GetCurrentPosition(pdwCurrentPlayCursor,
								  pdwCurrentWriteCursor));
	}

	DBGSTR("D3DBuffer::GetCurrentPosition - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D3DBuffer::GetFormat()
// (RE) rtl:0x1001c4a0; dbg:0x10044b70
//
// Forward to the wrapped buffer's GetFormat.
//
// Returns:
//   DirectSound GetFormat result
//   E_FAIL                        without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D3DBuffer::GetFormat(LPWAVEFORMATEX lpwfxFormat, DWORD dwSizeAllocated,
		     LPDWORD lpdwSizeWritten)
{
	ASSERT(lpwfxFormat != 0 &&
	       !IsBadReadPtr(lpwfxFormat, sizeof(WAVEFORMATEX)));
	ASSERT(lpdwSizeWritten != 0 &&
	       !IsBadReadPtr(lpdwSizeWritten, sizeof(DWORD)));

	if (m_lpDirectSoundBuffer != NULL)
	{
		ASSERT(m_lpDirectSoundBuffer != 0 &&
		       !IsBadReadPtr(m_lpDirectSoundBuffer, sizeof(IDirectSoundBuffer)));

		return (m_lpDirectSoundBuffer->GetFormat(lpwfxFormat, dwSizeAllocated,
							 lpdwSizeWritten));
	}

	DBGSTR("D3DBuffer::GetFormat - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D3DBuffer::GetVolume()
// (RE) rtl:0x1001c4d0; dbg:0x10044cd0
//
// Forward to the wrapped buffer's GetVolume.
//
// Returns:
//   DirectSound GetVolume result
//   E_FAIL                        without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D3DBuffer::GetVolume(LPLONG lplVolume)
{
	ASSERT(lplVolume != 0 && !IsBadReadPtr(lplVolume, sizeof(LONG)));

	if (m_lpDirectSoundBuffer != NULL)
	{
		ASSERT(m_lpDirectSoundBuffer != 0 &&
		       !IsBadReadPtr(m_lpDirectSoundBuffer, sizeof(IDirectSoundBuffer)));

		return (m_lpDirectSoundBuffer->GetVolume(lplVolume));
	}

	DBGSTR("D3DBuffer::GetVolume - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D3DBuffer::GetPan()
// (RE) rtl:0x1001c500; dbg:0x10044de0
//
// Forward to the wrapped buffer's GetPan.
//
// Returns:
//   DirectSound GetPan result
//   E_FAIL                     without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D3DBuffer::GetPan(LPLONG lplPan)
{
	ASSERT(lplPan != 0 && !IsBadReadPtr(lplPan, sizeof(LONG)));

	if (m_lpDirectSoundBuffer != NULL)
	{
		ASSERT(m_lpDirectSoundBuffer != 0 &&
		       !IsBadReadPtr(m_lpDirectSoundBuffer, sizeof(IDirectSoundBuffer)));

		return (m_lpDirectSoundBuffer->GetPan(lplPan));
	}

	DBGSTR("D3DBuffer::GetPan - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D3DBuffer::GetFrequency()
// (RE) rtl:0x1001c530; dbg:0x10044ef0
//
// Forward to the wrapped buffer's GetFrequency.
//
// Returns:
//   DirectSound GetFrequency result
//   E_FAIL                           without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D3DBuffer::GetFrequency(LPDWORD lpdwFrequency)
{
	ASSERT(lpdwFrequency != 0 && !IsBadReadPtr(lpdwFrequency, sizeof(DWORD)));

	if (m_lpDirectSoundBuffer != NULL)
	{
		ASSERT(m_lpDirectSoundBuffer != 0 &&
		       !IsBadReadPtr(m_lpDirectSoundBuffer, sizeof(IDirectSoundBuffer)));

		return (m_lpDirectSoundBuffer->GetFrequency(lpdwFrequency));
	}

	DBGSTR("D3DBuffer::GetFrequency - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D3DBuffer::GetStatus()
// (RE) rtl:0x1001c560; dbg:0x10045000
//
// Forward to the wrapped buffer's GetStatus.
//
// Returns:
//   DirectSound GetStatus result
//   E_FAIL                        without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D3DBuffer::GetStatus(LPDWORD lpdwStatus)
{
	ASSERT(lpdwStatus != 0 && !IsBadReadPtr(lpdwStatus, sizeof(DWORD)));

	if (m_lpDirectSoundBuffer != NULL)
	{
		ASSERT(m_lpDirectSoundBuffer != 0 &&
		       !IsBadReadPtr(m_lpDirectSoundBuffer, sizeof(IDirectSoundBuffer)));

		return (m_lpDirectSoundBuffer->GetStatus(lpdwStatus));
	}

	DBGSTR("D3DBuffer::GetStatus - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D3DBuffer::Initialize()
// (RE) rtl:0x1001c590; dbg:0x10045110
//
// Forward to the wrapped buffer's Initialize.
//
// Returns:
//   DirectSound Initialize result
//   E_FAIL                         without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D3DBuffer::Initialize(LPDIRECTSOUND lpDirectSound, LPCDSBUFFERDESC lpcDSBufferDesc)
{
	ASSERT(lpDirectSound != 0 &&
	       !IsBadReadPtr(lpDirectSound, sizeof(IDirectSound)));
	ASSERT(lpcDSBufferDesc != 0 &&
	       !IsBadReadPtr(lpcDSBufferDesc, sizeof(DSBUFFERDESC)));

	if (m_lpDirectSoundBuffer != NULL)
	{
		ASSERT(m_lpDirectSoundBuffer != 0 &&
		       !IsBadReadPtr(m_lpDirectSoundBuffer, sizeof(IDirectSoundBuffer)));

		return (m_lpDirectSoundBuffer->Initialize(lpDirectSound, lpcDSBufferDesc));
	}

	DBGSTR("D3DBuffer::Initialize - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D3DBuffer::Lock()
// (RE) rtl:0x1001c5c0; dbg:0x10045270
//
// Forward to the wrapped buffer's Lock.
//
// Returns:
//   DirectSound Lock result
//   E_FAIL                   without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D3DBuffer::Lock(DWORD dwOffset, DWORD dwWriteBytes, LPVOID *lplpvAudioPtr1,
		LPDWORD lpdwAudioBytes1, LPVOID *lplpvAudioPtr2,
		LPDWORD lpdwAudioBytes2, DWORD dwFlags)
{
	ASSERT(lplpvAudioPtr1 != 0);
	ASSERT(lpdwAudioBytes1 != 0 &&
	       !IsBadReadPtr(lpdwAudioBytes1, sizeof(DWORD)));
	ASSERT(dwWriteBytes <= m_DSBufferDesc.dwBufferBytes);

	if (m_lpDirectSoundBuffer != NULL)
	{
		ASSERT(m_lpDirectSoundBuffer != 0 &&
		       !IsBadReadPtr(m_lpDirectSoundBuffer, sizeof(IDirectSoundBuffer)));

		return (m_lpDirectSoundBuffer->Lock(dwOffset, dwWriteBytes,
						    lplpvAudioPtr1, lpdwAudioBytes1,
						    lplpvAudioPtr2, lpdwAudioBytes2,
						    dwFlags));
	}

	DBGSTR("D3DBuffer::Lock - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D3DBuffer::Play()
// (RE) rtl:0x1001c600; dbg:0x10045420
//
// Reset reflection age and start the reflection voices and wrapped buffer.
//
// Returns:
//   DirectSound Play result
//   E_FAIL                   without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D3DBuffer::Play(DWORD dwReserved1, DWORD dwReserved2, DWORD dwFlags)
{
HRESULT	hr;
int	i;

	if (m_lpDirectSoundBuffer != NULL)
	{
		m_fReflectionAge = 0;

		if (m_cReflectionCount)
		{
			for (i = 0; i < A3D_MAX_SOURCE_REFLECTIONS; i++)
			{
				if (m_apReflection[i] != NULL)
					m_apReflection[i]->Start(dwFlags, m_fReflectionAge);
			}
		}

		ASSERT(m_lpDirectSoundBuffer != 0 &&
		       !IsBadReadPtr(m_lpDirectSoundBuffer, sizeof(IDirectSoundBuffer)));

		hr = m_lpDirectSoundBuffer->Play(dwReserved1, dwReserved2, dwFlags);

		ASSERT(SUCCEEDED(hr));

		return (hr);
	}

	DBGSTR("D3DBuffer::Play - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D3DBuffer::SetCurrentPosition()
// (RE) rtl:0x1001c680; dbg:0x100455b0
//
// Forward to the wrapped buffer's SetCurrentPosition.
//
// Returns:
//   DirectSound SetCurrentPosition result
//   E_FAIL                              without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D3DBuffer::SetCurrentPosition(DWORD dwNewPosition)
{
	if (m_lpDirectSoundBuffer != NULL)
	{
		ASSERT(m_lpDirectSoundBuffer != 0 &&
		       !IsBadReadPtr(m_lpDirectSoundBuffer, sizeof(IDirectSoundBuffer)));

		return (m_lpDirectSoundBuffer->SetCurrentPosition(dwNewPosition));
	}

	DBGSTR("D3DBuffer::SetCurrentPosition - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D3DBuffer::SetFormat()
// (RE) rtl:0x1001c6b0; dbg:0x10045660
//
// Forward to the wrapped buffer's SetFormat.
//
// Returns:
//   DirectSound SetFormat result
//   E_FAIL                        without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D3DBuffer::SetFormat(LPCWAVEFORMATEX lpcfxFormat)
{
	if (m_lpDirectSoundBuffer != NULL)
	{
		ASSERT(m_lpDirectSoundBuffer != 0 &&
		       !IsBadReadPtr(m_lpDirectSoundBuffer, sizeof(IDirectSoundBuffer)));

		return (m_lpDirectSoundBuffer->SetFormat(lpcfxFormat));
	}

	DBGSTR("D3DBuffer::SetFormat - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D3DBuffer::SetVolume()
// (RE) rtl:0x1001c6e0; dbg:0x10045710
//
// Forward to the wrapped buffer's SetVolume.
//
// Returns:
//   DirectSound SetVolume result
//   E_FAIL                        without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D3DBuffer::SetVolume(LONG lVolume)
{
	if (m_lpDirectSoundBuffer != NULL)
	{
		ASSERT(m_lpDirectSoundBuffer != 0 &&
		       !IsBadReadPtr(m_lpDirectSoundBuffer, sizeof(IDirectSoundBuffer)));

		return (m_lpDirectSoundBuffer->SetVolume(lVolume));
	}

	DBGSTR("D3DBuffer::SetVolume - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D3DBuffer::SetPan()
// (RE) rtl:0x1001c710; dbg:0x100457c0
//
// Forward to the wrapped buffer's SetPan.
//
// Returns:
//   DirectSound SetPan result
//   E_FAIL                     without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D3DBuffer::SetPan(LONG lPan)
{
	if (m_lpDirectSoundBuffer != NULL)
	{
		ASSERT(m_lpDirectSoundBuffer != 0 &&
		       !IsBadReadPtr(m_lpDirectSoundBuffer, sizeof(IDirectSoundBuffer)));

		return (m_lpDirectSoundBuffer->SetPan(lPan));
	}

	DBGSTR("D3DBuffer::SetPan - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D3DBuffer::SetFrequency()
// (RE) rtl:0x1001c740; dbg:0x10045870
//
// Forward to the wrapped buffer's SetFrequency.
//
// Returns:
//   DirectSound SetFrequency result
//   E_FAIL                           without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D3DBuffer::SetFrequency(DWORD dwFrequency)
{
	if (m_lpDirectSoundBuffer != NULL)
	{
		ASSERT(m_lpDirectSoundBuffer != 0 &&
		       !IsBadReadPtr(m_lpDirectSoundBuffer, sizeof(IDirectSoundBuffer)));

		return (m_lpDirectSoundBuffer->SetFrequency(dwFrequency));
	}

	DBGSTR("D3DBuffer::SetFrequency - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D3DBuffer::Stop()
// (RE) rtl:0x1001c770; dbg:0x10045920
//
// Delete live reflections and stop the wrapped buffer.
//
// Returns:
//   DirectSound Stop result
//   E_FAIL                   without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D3DBuffer::Stop(void)
{
int	i;

	if (m_lpDirectSoundBuffer != NULL)
	{
		if (m_hReflectionMutex != NULL)
		{
			VERIFY(WaitForSingleObject(m_hReflectionMutex, INFINITE) ==
			       WAIT_OBJECT_0);

			if (m_cReflectionCount)
			{
				for (i = 0; i < A3D_MAX_SOURCE_REFLECTIONS; i++)
				{
					if (m_apReflection[i] != NULL)
					{
						m_apReflection[i]->Stop();

						if (m_apReflection[i] != NULL)
						{
							delete m_apReflection[i];
							m_apReflection[i] = NULL;
						}
					}
				}
			}

			m_cReflectionCount = 0;

			VERIFY(ReleaseMutex(m_hReflectionMutex));
		}

		ASSERT(m_lpDirectSoundBuffer != 0 &&
		       !IsBadReadPtr(m_lpDirectSoundBuffer, sizeof(IDirectSoundBuffer)));

		return (m_lpDirectSoundBuffer->Stop());
	}

	DBGSTR("D3DBuffer::Stop - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D3DBuffer::Unlock()
// (RE) rtl:0x1001c840; dbg:0x10045b60
//
// Forward to the wrapped buffer's Unlock.
//
// Returns:
//   DirectSound Unlock result
//   E_FAIL                     without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D3DBuffer::Unlock(LPVOID lpvAudioPtr1, DWORD dwAudioBytes1, LPVOID lpvAudioPtr2,
		  DWORD dwAudioBytes2)
{
	if (m_lpDirectSoundBuffer != NULL)
	{
		ASSERT(m_lpDirectSoundBuffer != 0 &&
		       !IsBadReadPtr(m_lpDirectSoundBuffer, sizeof(IDirectSoundBuffer)));

		return (m_lpDirectSoundBuffer->Unlock(lpvAudioPtr1, dwAudioBytes1,
						      lpvAudioPtr2, dwAudioBytes2));
	}

	DBGSTR("D3DBuffer::Unlock - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D3DBuffer::Restore()
// (RE) rtl:0x1001c870; dbg:0x10045c20
//
// Forward to the wrapped buffer's Restore.
//
// Returns:
//   DirectSound Restore result
//   E_FAIL                      without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D3DBuffer::Restore(void)
{
	if (m_lpDirectSoundBuffer != NULL)
	{
		ASSERT(m_lpDirectSoundBuffer != 0 &&
		       !IsBadReadPtr(m_lpDirectSoundBuffer, sizeof(IDirectSoundBuffer)));

		return (m_lpDirectSoundBuffer->Restore());
	}

	DBGSTR("D3DBuffer::Restore - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D3DBuffer::Unknown_0x54()
// (RE) rtl:0x10003500; dbg:0x10046fa0
//
// Reject the unresolved operation in IDirectSoundBuffer slot 21.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
D3DBuffer::Unknown_0x54(DWORD, DWORD, DWORD)
{
	return (E_NOTIMPL);
}

/* =============================================================
// D3DBuffer::GainToDSVolume()
// (RE) dbg:0x10045cd0
//
// Set DirectSound volume from the mean ear gain.
//
// Returns: S_OK on success; the SetVolume failure otherwise.
// =============================================================*/

HRESULT
D3DBuffer::GainToDSVolume(float fLeftGain, float fRightGain)
{
FLOAT	fMean;
LONG	lVolume;
HRESULT	hr;

	fMean = (fLeftGain + fRightGain) * 0.5f;

	if (fMean == 0.0f)
		lVolume = DSBVOLUME_MIN;
	else
		lVolume = (LONG) (log10((double) fMean) * A3D_D3D_LOG_GAIN_MULTIPLIER * A3D_D3D_VOLUME_SCALE);

	ASSERT(m_lpDirectSoundBuffer != 0 &&
	       !IsBadReadPtr(m_lpDirectSoundBuffer, sizeof(IDirectSoundBuffer)));

	hr = m_lpDirectSoundBuffer->SetVolume(lVolume);
	if (SUCCEEDED(hr))
		return (S_OK);

	DBGSTR("D3DBuffer::GainToDSVolume() - SetVolume() Failed.\n");

	return (hr);
}

/* =============================================================
// D3DBuffer::PositionSound()
// (RE) dbg:0x10045e00
//
// Set head-relative DS3D parameters from the mean ear direction.
//
// Returns: S_OK on success; the SetAllParameters failure otherwise.
// =============================================================*/

HRESULT
D3DBuffer::PositionSound(const float *pLeftDir, const float *pRightDir)
{
DS3DBUFFER	ds3d;
FLOAT		fLeftX, fLeftY, fLeftZ;
FLOAT		fRightX, fRightY, fRightZ;
HRESULT		hr;

	fLeftX  = (FLOAT) -sin((double) pLeftDir[0]);
	fLeftZ  = (FLOAT)  cos((double) pLeftDir[0]);
	fLeftY  = (FLOAT)  sin((double) pLeftDir[1]);

	fRightX = (FLOAT) -sin((double) pRightDir[0]);
	fRightZ = (FLOAT)  cos((double) pRightDir[0]);
	fRightY = (FLOAT)  sin((double) pRightDir[1]);

	ds3d.dwSize             = sizeof(DS3DBUFFER);
	ds3d.vPosition.x        = (fLeftX + fRightX) * 0.5f;
	ds3d.vPosition.y        = (fLeftY + fRightY) * 0.5f;
	ds3d.vPosition.z        = (fLeftZ + fRightZ) * 0.5f;
	ds3d.vVelocity.x        = 0.0f;
	ds3d.vVelocity.y        = 0.0f;
	ds3d.vVelocity.z        = 0.0f;
	ds3d.dwInsideConeAngle  = 360;
	ds3d.dwOutsideConeAngle = 360;
	ds3d.vConeOrientation.x = 0.0f;
	ds3d.vConeOrientation.y = 0.0f;
	ds3d.vConeOrientation.z = 1.0f;
	ds3d.lConeOutsideVolume = 0;
	ds3d.flMinDistance      = A3D_D3D_MIN_DISTANCE;
	ds3d.flMaxDistance      = A3D_D3D_MAX_DISTANCE;
	ds3d.dwMode             = DS3DMODE_HEADRELATIVE;

	ASSERT(m_lpDirectSoundBuffer != 0 &&
	       !IsBadReadPtr(m_lpDirectSoundBuffer, sizeof(IDirectSoundBuffer)));

	hr = m_lpDS3DBuffer->SetAllParameters(&ds3d, DS3D_IMMEDIATE);
	if (SUCCEEDED(hr))
		return (S_OK);

	DBGSTR("D3DBuffer::PositionSound() - SetAllParameters Failed.\n");

	return (hr);
}

/* =============================================================
// D3DBuffer::SetA3dSuperCtrl()
// (RE) rtl:0x1001c960; dbg:0x10046020
//
// Apply source and reflection controls through the A3D property set,
// or set DS3D position, volume and frequency.
//
// Returns: S_OK on the property-set path; otherwise S_OK or the SetFrequency
//          failure. Other delegated failures are ignored.
// =============================================================*/

STDMETHODIMP
D3DBuffer::SetA3dSuperCtrl(LPA3DCTRL_SRC_SUPER lpA3dCtrlSuper, DWORD dwSize)
{
BOOL	afAvailable[A3D_MAX_SOURCE_REFLECTIONS];
DWORD	dwStatus;
DWORD	dwFrequency;
FLOAT	fFrequency;
HRESULT	hr;
int	i;

	dwStatus = 0;
	GetStatus(&dwStatus);

	CopyMemory(&m_A3dCtrlSuper, lpA3dCtrlSuper, sizeof(m_A3dCtrlSuper));

	fFrequency = (FLOAT) ((double) m_WaveFormatEx.nSamplesPerSec *
			      lpA3dCtrlSuper->fFreqFactor);
	dwFrequency = (DWORD) fFrequency;

	if (m_fPropSetA3d)
	{
		if (m_lpDalD3D->m_fUseReflections)
		{
			for (i = 0; i < A3D_MAX_SOURCE_REFLECTIONS; i++)
				afAvailable[i] = lpA3dCtrlSuper->Reflections[i].bAvailable;
		}

		if (m_bIsReflection)
			PushReflectionSuperCtrl(lpA3dCtrlSuper);
		else
			Set(DSPROPSETID_A3dSourceSuper, 0, NULL, 0, lpA3dCtrlSuper, dwSize);

		if (m_lpDalD3D->m_fUseReflections)
		{
			VERIFY(WaitForSingleObject(m_hReflectionMutex, INFINITE) ==
			       WAIT_OBJECT_0);

			for (i = 0; i < A3D_MAX_SOURCE_REFLECTIONS; i++)
			{
				if (afAvailable[i])
				{
					if (m_apReflection[i] != NULL &&
					    lpA3dCtrlSuper->Reflections[i].bMute)
					{
						m_apReflection[i]->Stop();
						m_apReflection[i]->Reset();
					}

					if (m_apReflection[i] == NULL)
					{
						m_apReflection[i] = new CReflection;

						VERIFY((LONG) ++m_cReflectionCount > 0);

						if (FAILED(m_apReflection[i]->Init(this, m_lpDalD3D)))
						{
							if (m_apReflection[i] != NULL)
							{
								delete m_apReflection[i];
								m_apReflection[i] = NULL;
							}

							VERIFY((LONG) --m_cReflectionCount >= 0);
							VERIFY(ReleaseMutex(m_hReflectionMutex));

							return (S_OK);
						}
					}

					m_apReflection[i]->Update(lpA3dCtrlSuper, i, fFrequency,
								  m_fReflectionAge);
				}
				else if (m_apReflection[i] != NULL)
				{
					delete m_apReflection[i];
					m_apReflection[i] = NULL;

					VERIFY((LONG) --m_cReflectionCount >= 0);
				}
			}

			VERIFY(ReleaseMutex(m_hReflectionMutex));
		}

		return (S_OK);
	}

	PositionSound(&lpA3dCtrlSuper->LeftEar.fAzim,
		      &lpA3dCtrlSuper->RightEar.fAzim);
	GainToDSVolume(lpA3dCtrlSuper->LeftEar.fGain,
		       lpA3dCtrlSuper->RightEar.fGain);

	ASSERT(m_lpDirectSoundBuffer != 0 &&
	       !IsBadReadPtr(m_lpDirectSoundBuffer, sizeof(IDirectSoundBuffer)));

	hr = m_lpDirectSoundBuffer->SetFrequency(dwFrequency);
	if (SUCCEEDED(hr))
		return (S_OK);

	return (hr);
}

/* =============================================================
// D3DBuffer::GetAllocationStatus()
// (RE) rtl:0x100034f0; dbg:0x10046970
//
// Accept the call without writing the allocation status.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
D3DBuffer::GetAllocationStatus(LPDWORD lpdwStatus)
{
	ASSERT(lpdwStatus != 0 && !IsBadReadPtr(lpdwStatus, sizeof(DWORD)));

	return (S_OK);
}

/* =============================================================
// D3DBuffer::GetWave()
// (RE) rtl:0x100034f0; dbg:0x100469e0
//
// Accept the call without writing the waveform pointer.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
D3DBuffer::GetWave(LPBYTE *lplpbWave)
{
	ASSERT(lplpbWave != 0);

	return (S_OK);
}

/* =============================================================
// D3DBuffer::GetDriverInfo()
// (RE) rtl:0x1001b380; dbg:0x10046a40
//
// Accept the call without writing either driver pointer.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
D3DBuffer::GetDriverInfo(void **lplpIDsDriverBuffer, void **lplpIA3dDriverBuffer)
{
	ASSERT(lplpIDsDriverBuffer != 0);
	ASSERT(lplpIA3dDriverBuffer != 0);

	return (S_OK);
}

/* =============================================================
// D3DBuffer::SetNewBuffer()
// (RE) rtl:0x1001b380; dbg:0x10046ae0
//
// Accept the call without replacing the buffer.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
D3DBuffer::SetNewBuffer(LPBYTE lpbWave, DWORD dwBufferBytes)
{
	ASSERT(lpbWave != 0);

	return (S_OK);
}

/* =============================================================
// D3DBuffer::SetA3dDirectCtrl()
// (RE) rtl:0x1001b380; dbg:0x10046b40
//
// Accept the call without applying direct controls.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
D3DBuffer::SetA3dDirectCtrl(LPA3DCTRL_SRC_SUPER lpA3dCtrlDirect, DWORD dwSize)
{
	ASSERT(lpA3dCtrlDirect != 0);

	return (S_OK);
}

/* =============================================================
// D3DBuffer::GetStatusEx()
// (RE) rtl:0x1001ce20; dbg:0x10046ba0
//
// Forward to the wrapped buffer's GetStatus.
//
// Returns:
//   DirectSound GetStatus result
//   E_FAIL                        without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D3DBuffer::GetStatusEx(LPDWORD lpdwStatusEx)
{
	ASSERT(lpdwStatusEx != 0 && !IsBadReadPtr(lpdwStatusEx, sizeof(DWORD)));

	if (m_lpDirectSoundBuffer != NULL)
	{
		ASSERT(m_lpDirectSoundBuffer != 0 &&
		       !IsBadReadPtr(m_lpDirectSoundBuffer, sizeof(IDirectSoundBuffer)));

		return (m_lpDirectSoundBuffer->GetStatus(lpdwStatusEx));
	}

	DBGSTR("D3DBuffer::GetStatus - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D3DBuffer::GetPriority()
// (RE) rtl:0x1001ce50; dbg:0x10046cb0
//
// Read the priority in the range 0..1.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
D3DBuffer::GetPriority(FLOAT *pfPriority)
{
	ASSERT(pfPriority != 0 && !IsBadReadPtr(pfPriority, sizeof(FLOAT)));

	if (pfPriority != NULL)
	{
		ASSERT(m_fPriority >= 0.0 && m_fPriority <= 1.0);

		*pfPriority = m_fPriority;

		return (S_OK);
	}

	DBGSTR("D3DBuffer::GetPriority() - Invalid pointer passed in.\n");

	return (E_POINTER);
}

/* =============================================================
// D3DBuffer::GetBufferState()
// (RE) rtl:0x1001ce80; dbg:0x10046db0
//
// Read the buffer state.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
D3DBuffer::GetBufferState(LPDWORD lpdwBufferState)
{
	ASSERT(lpdwBufferState != 0 &&
	       !IsBadReadPtr(lpdwBufferState, sizeof(DWORD)));

	*lpdwBufferState = m_dwBufferState;

	return (S_OK);
}

/* =============================================================
// D3DBuffer::SetPriority()
// (RE) rtl:0x1001cea0; dbg:0x10046e30
//
// Set the priority in the range 0..1.
//
// Returns:
//   S_OK
//   E_INVALIDARG  outside 0..1
// =============================================================*/

STDMETHODIMP
D3DBuffer::SetPriority(FLOAT fPriority)
{
	ASSERT(fPriority >= 0.0 && fPriority <= 1.0);

	if (fPriority >= 0.0 && fPriority <= 1.0)
	{
		m_fPriority = fPriority;

		return (S_OK);
	}

	DBGSTR("D3DBuffer::SetPriority() - Invalid priority value sent.\n");

	return (E_INVALIDARG);
}

/* =============================================================
// D3DBuffer::Enable()
// (RE) rtl:0x1001b1c0; dbg:0x10046f00
//
// Accept the call without changing the voice.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
D3DBuffer::Enable(void)
{
	return (S_OK);
}

/* =============================================================
// D3DBuffer::Disable()
// (RE) rtl:0x1001b1c0; dbg:0x10046f20
//
// Accept the call without changing the voice.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
D3DBuffer::Disable(void)
{
	return (S_OK);
}

/* =============================================================
// D3DBuffer::SetNativeModeDisabled()
// (RE) rtl:0x10020b90; dbg:0x10046f40
//
// Reject the native-mode-disable request
//
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
D3DBuffer::SetNativeModeDisabled(DWORD)
{
	return (E_NOTIMPL);
}

/* =============================================================
// D3DBuffer::Unknown_0x28()
// (RE) rtl:0x10020b90; dbg:0x10046f60
//
// Reject the unresolved operation in IResManBuffer slot 10.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
D3DBuffer::Unknown_0x28(DWORD)
{
	return (E_NOTIMPL);
}

/* =============================================================
// D3DBuffer::GetControlBufferPair()
// (RE) rtl:0x10020b90; dbg:0x10046f80
//
// Reject the control-buffer-pair query
//
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
D3DBuffer::GetControlBufferPair(DWORD)
{
	return (E_NOTIMPL);
}

/* =============================================================
// D3DBuffer::Init()
// (RE) rtl:0x1001cee0; dbg:0x10046fc0
//
// Retain a DS3D buffer, copy its format and descriptor, query optional A3D
// property support and create the reflection mutex.
//
// Returns:
//   S_OK
//   E_INVALIDARG  without a DS3D interface
//   E_FAIL        if reflection mutex creation fails
// =============================================================*/

HRESULT
D3DBuffer::Init(IDirectSoundBuffer *lpDirectSoundBuffer, const DSBUFFERDESC1 *pDesc,
		CList *pList, HANDLE hBufferListMutex, DAL_D3D *lpDalD3D)
{
DWORD	dwSupport;

	if (FAILED(lpDirectSoundBuffer->QueryInterface(IID_IDirectSound3DBuffer,
						       (void **) &m_lpDS3DBuffer)))
	{
		DBGSTR("D3DBuffer::Init() - couldn't query interface for DS3D buffer\n");

		return (E_INVALIDARG);
	}

	ASSERT(lpDirectSoundBuffer != 0 &&
	       !IsBadReadPtr(lpDirectSoundBuffer, sizeof(IDirectSoundBuffer)));

	m_lpDirectSoundBuffer = lpDirectSoundBuffer;
	m_lpDirectSoundBuffer->AddRef();

	ASSERT(lpDalD3D != 0 && !IsBadReadPtr(lpDalD3D, sizeof(DAL_D3D)));

	m_lpDalD3D              = lpDalD3D;
	m_hBufferListMutex      = hBufferListMutex;

	CopyMemory(&m_DSBufferDesc, pDesc, sizeof(m_DSBufferDesc));

	m_WaveFormatEx                  = *pDesc->lpwfxFormat;
	m_DSBufferDesc.lpwfxFormat      = &m_WaveFormatEx;

	m_pList = pList;

	if (FAILED(m_lpDS3DBuffer->QueryInterface(IID_IKsPropertySet,
						  (void **) &m_lpPropertySet)))
	{
		m_lpPropertySet = NULL;
	}
	else
	{
		dwSupport = 0;

		if (SUCCEEDED(m_lpPropertySet->QuerySupport(DSPROPSETID_A3dSourceSuper,
							    0, &dwSupport)) &&
		    dwSupport)
		{
			m_fPropSetA3d = 1;
		}
	}

	m_hReflectionMutex = CreateMutexA(NULL, FALSE, NULL);
	if (m_hReflectionMutex != NULL)
		return (S_OK);

	DBGSTR("D3DBuffer::Init - couldn't create reflection mutex.\n");

	return (E_FAIL);
}

/* =============================================================
// D3DBuffer::InitDuplicate()
// (RE) rtl:0x1001cfe0; dbg:0x10047270
//
// Take ownership of a duplicate and copy the original voice's configuration.
// The original dereferences null if the DS3D query fails.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
D3DBuffer::InitDuplicate(LPDIRECTSOUNDBUFFER lpOriginalBuffer,
			 LPDIRECTSOUNDBUFFER lpDuplicateSoundBuffer)
{
D3DBuffer      *pOriginalBuffer;
DWORD           dwSupport;

	ASSERT(lpOriginalBuffer != 0 &&
	       !IsBadReadPtr(lpOriginalBuffer, sizeof(IDirectSoundBuffer)));
	ASSERT(lpDuplicateSoundBuffer != 0 &&
	       !IsBadReadPtr(lpDuplicateSoundBuffer, sizeof(IDirectSoundBuffer)));

	pOriginalBuffer = (D3DBuffer *) lpOriginalBuffer;

	CopyMemory(&m_DSBufferDesc, &pOriginalBuffer->m_DSBufferDesc,
		   sizeof(m_DSBufferDesc));

	m_WaveFormatEx                  = pOriginalBuffer->m_WaveFormatEx;
	m_DSBufferDesc.lpwfxFormat      = &m_WaveFormatEx;

	m_pList = pOriginalBuffer->m_pList;

	ASSERT(lpDuplicateSoundBuffer != 0 &&
	       !IsBadReadPtr(lpDuplicateSoundBuffer, sizeof(IDirectSoundBuffer)));

	m_lpDirectSoundBuffer = lpDuplicateSoundBuffer;

	ASSERT(pOriginalBuffer->m_lpDalD3D != 0 &&
	       !IsBadReadPtr(pOriginalBuffer->m_lpDalD3D, sizeof(DAL_D3D)));

	m_lpDalD3D         = pOriginalBuffer->m_lpDalD3D;
	m_hBufferListMutex = pOriginalBuffer->m_hBufferListMutex;

	if (FAILED(lpDuplicateSoundBuffer->QueryInterface(IID_IDirectSound3DBuffer,
							  (void **) &m_lpDS3DBuffer)))
	{
		DBGSTR("D3DBuffer::Duplicate() - Failed to QI for DS3D Buffer.\n");

		m_lpDS3DBuffer = NULL;
	}

	if (FAILED(m_lpDS3DBuffer->QueryInterface(IID_IKsPropertySet,
						  (void **) &m_lpPropertySet)))
	{
		m_lpPropertySet = NULL;
	}
	else
	{
		dwSupport = 3;

		if (SUCCEEDED(QuerySupport(DSPROPSETID_A3dSourceSuper, 0, &dwSupport)))
			m_fPropSetA3d = 1;
	}

	return (S_OK);
}

/* =============================================================
// D3DBuffer::Get()
// (RE) rtl:0x1001d130; dbg:0x10047930
//
// Forward to the wrapped property set's Get.
//
// Returns: Property-set Get result; E_FAIL without a property set.
// =============================================================*/

STDMETHODIMP
D3DBuffer::Get(REFGUID rguidPropSet, ULONG ulId, LPVOID pInstanceData,
	       ULONG ulInstanceLength, LPVOID pPropertyData, ULONG ulDataLength,
	       PULONG pulBytesReturned)
{
	if (m_lpPropertySet != NULL)
	{
		ASSERT(m_lpPropertySet != 0 &&
		       !IsBadReadPtr(m_lpPropertySet, sizeof(IKsPropertySet)));

		return (m_lpPropertySet->Get(rguidPropSet, ulId, pInstanceData,
					     ulInstanceLength, pPropertyData,
					     ulDataLength, pulBytesReturned));
	}

	DBGSTR("D3DBuffer::Get - m_lpPropertySet is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D3DBuffer::Set()
// (RE) rtl:0x1001d0f0; dbg:0x100477b0
//
// Forward to the wrapped property set's Set.
//
// Returns: Property-set Set result; E_FAIL without a property set.
// =============================================================*/

STDMETHODIMP
D3DBuffer::Set(REFGUID rguidPropSet, ULONG ulId, LPVOID pInstanceData,
	       ULONG ulInstanceLength, LPVOID pPropertyData, ULONG ulDataLength)
{
HRESULT	hr;

	ASSERT(!m_bIsReflection);

	if (m_lpPropertySet != NULL)
	{
		ASSERT(m_lpPropertySet != 0 &&
		       !IsBadReadPtr(m_lpPropertySet, sizeof(IKsPropertySet)));

		hr = m_lpPropertySet->Set(rguidPropSet, ulId, pInstanceData,
					  ulInstanceLength, pPropertyData,
					  ulDataLength);

		/* (RE) Original TRACE omits the argument for the Error field. */
		if (FAILED(hr))
			TRACE("Failed to set PropSet super control on buffer "
			      "0x%08X.\n\tError: 0x%08X.\n", this);

		return (hr);
	}

	DBGSTR("D3DBuffer::Set - m_lpPropertySet is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D3DBuffer::QuerySupport()
// (RE) rtl:0x1001d0c0; dbg:0x10047590
//
// Forward to the wrapped property set's QuerySupport.
//
// Returns: Property-set QuerySupport result; E_FAIL without a property set.
// =============================================================*/

STDMETHODIMP
D3DBuffer::QuerySupport(REFGUID rguidPropSet, ULONG ulId, PULONG pulTypeSupport)
{
	if (m_lpPropertySet != NULL)
	{
		ASSERT(m_lpPropertySet != 0 &&
		       !IsBadReadPtr(m_lpPropertySet, sizeof(IKsPropertySet)));

		return (m_lpPropertySet->QuerySupport(rguidPropSet, ulId,
						      pulTypeSupport));
	}

	DBGSTR("D3DBuffer::QuerySupport - m_lpPropertySet is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D3DBuffer::PushReflectionSuperCtrl()
// (RE) dbg:0x10047660
//
// Set reflection volume and frequency from the control block.
//
// Returns: S_OK on success; the SetFrequency failure otherwise. Volume errors
//          are ignored.
// =============================================================*/

HRESULT
D3DBuffer::PushReflectionSuperCtrl(LPA3DCTRL_SRC_SUPER lpA3dCtrl)
{
DWORD	dwFrequency;
HRESULT	hr;

	ASSERT(m_WaveFormatEx.nSamplesPerSec > 0);

	dwFrequency = (DWORD) ((double) m_WaveFormatEx.nSamplesPerSec *
			       lpA3dCtrl->fFreqFactor);

	GainToDSVolume(lpA3dCtrl->LeftEar.fGain, lpA3dCtrl->RightEar.fGain);

	ASSERT(m_lpDirectSoundBuffer != 0 &&
	       !IsBadReadPtr(m_lpDirectSoundBuffer, sizeof(IDirectSoundBuffer)));

	hr = m_lpDirectSoundBuffer->SetFrequency(dwFrequency);
	if (SUCCEEDED(hr))
		return (S_OK);

	/* (RE) Original TRACE omits the hResult argument. */
	TRACE("D3DBuffer::SetA3DSuperCtrl - SetFrequency() Failed.\n"
	      "\tFrequency: %u.\n\thResult: 0x%08X.\n", dwFrequency);

	return (hr);
}

/* =============================================================
// D3DBuffer::AdvanceReflectionAge()
// (RE) dbg:0x10047a20
//
// Advance reflection age by 0.01 seconds.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
D3DBuffer::AdvanceReflectionAge(void)
{
	m_fReflectionAge = m_fReflectionAge + A3D_D3D_REFLECTION_AGE_STEP_S;

	return (S_OK);
}

/* =============================================================
// D3DBuffer::AgeReflectionVoices()
// (RE) dbg:0x100467a0
//
// Advance age and process reflections within the budget, reporting the count
// through lpdwAged. Clear per-reflection pass flags when all slots are visited.
//
// Returns:
//   TRUE   when all slots were visited
//   FALSE  when the budget stopped the scan
// =============================================================*/

BOOL
D3DBuffer::AgeReflectionVoices(DWORD dwBudget, LPDWORD lpdwAged)
{
BOOL	fAllAged;
DWORD	fAged;
int	i;

	fAllAged = TRUE;
	*lpdwAged = 0;

	AdvanceReflectionAge();

	VERIFY(WaitForSingleObject(m_hReflectionMutex, INFINITE) == WAIT_OBJECT_0);

	if (m_cReflectionCount)
	{
		for (i = 0; i < A3D_MAX_SOURCE_REFLECTIONS; i++)
		{
			if (m_apReflection[i] != NULL)
			{
				if (*lpdwAged >= dwBudget)
					break;

				fAged = 0;
				m_apReflection[i]->AgeStep(&fAged);

				if (fAged)
					++*lpdwAged;
			}
		}

		if (i == A3D_MAX_SOURCE_REFLECTIONS)
		{
			for (i = 0; i < A3D_MAX_SOURCE_REFLECTIONS; i++)
			{
				if (m_apReflection[i] != NULL)
					m_apReflection[i]->ClearAged();
			}
		}
		else
		{
			fAllAged = FALSE;
		}
	}

	VERIFY(ReleaseMutex(m_hReflectionMutex));

	return (fAllAged);
}

/* =============================================================
// CReflection::CReflection()
// (RE) dbg:0x10047a60
//
// Construct an empty reflection with playback and delay state cleared.
// =============================================================*/

CReflection::CReflection(void)
{
	m_pReflectionBuffer = NULL;
	m_pSource           = NULL;
	m_hMutex            = NULL;
	m_nBytesPerSample   = 0;
	m_dwBytesInBuffer   = 0;
	m_pA3dCtrl          = NULL;
	m_fAgedThisTick     = 0;

	Reset();
}

/* =============================================================
// CReflection::~CReflection()
// (RE) dbg:0x10047ae0
//
// Delete the control block, stop the reflection voice and release it.
// =============================================================*/

CReflection::~CReflection(void)
{
	if (m_pA3dCtrl != NULL)
	{
		delete m_pA3dCtrl;
		m_pA3dCtrl = NULL;
	}

	if (m_pReflectionBuffer != NULL)
	{
		m_pReflectionBuffer->Stop();

		if (m_pReflectionBuffer != NULL)
		{
			m_pReflectionBuffer->Release();
			m_pReflectionBuffer = NULL;
		}
	}
}

/* =============================================================
// CReflection::Reset()
// (RE) dbg:0x10047ca0
//
// Clear playback and delay state and reset pitch to 1.0.
// =============================================================*/

void
CReflection::Reset(void)
{
	m_dwStartFlags  = 0;
	m_fStarted      = 0;
	m_fSynced       = 0;
	m_fPitchSettled = 0;
	m_fTargetDelay  = 0;
	m_fCurrentDelay = 0;
	m_fFrequency    = 0;
	m_fFreqFactor   = 0;
	m_fApplyPending = 0;
	m_fPitch        = 1.0f;
}

/* =============================================================
// CReflection::Init()
// (RE) dbg:0x10047b90
//
// Duplicate the source voice and allocate its reflection control block.
//
// Returns:
//   S_OK
//   E_FAIL  if duplication or allocation fails
// =============================================================*/

HRESULT
CReflection::Init(D3DBuffer *pSource, DAL_D3D *pDevice)
{
	if (FAILED(pDevice->CreateReflection(pSource, &m_pReflectionBuffer,
					     &m_pReflectionDal)))
		return (E_FAIL);

	m_dwBytesInBuffer = pSource->m_DSBufferDesc.dwBufferBytes;
	m_nBytesPerSample = pSource->m_WaveFormatEx.nBlockAlign;
	m_pSource         = pSource;
	m_hMutex          = pSource->m_hReflectionMutex;

	m_pA3dCtrl = new A3DCTRL_SRC_SUPER;
	if (m_pA3dCtrl != NULL)
		return (S_OK);

	return (E_FAIL);
}

/* =============================================================
// CReflection::Update()
// (RE) rtl:0x1001d190; dbg:0x10047da0
//
// Apply one reflection slot, keeping it muted until synchronized, and
// start it when the source is playing.
//
// Returns: S_OK; delegated failures are ignored.
// =============================================================*/

HRESULT
CReflection::Update(const A3DCTRL_SRC_SUPER *pSrc, int nReflection,
		    float fFrequency, float fAge)
{
const A3DCTRL_REFLECTION       *pSlot;
FLOAT                           fLeftGain;
FLOAT                           fRightGain;
DWORD                           dwStatus;

	pSlot = &pSrc->Reflections[nReflection];

	m_fCurrentDelay = (pSlot->LeftEar.fDelay + pSlot->RightEar.fDelay) / 2.0f;
	m_fTargetDelay  = m_fCurrentDelay;
	m_fFrequency    = fFrequency;

	memset(m_pA3dCtrl, 0, sizeof(A3DCTRL_SRC_SUPER));
	CopyMemory(m_pA3dCtrl, pSrc, offsetof(A3DCTRL_SRC_SUPER, Reflections));

	m_pA3dCtrl->fAlpha   = pSlot->fAlpha;
	m_pA3dCtrl->LeftEar  = pSlot->LeftEar;
	m_pA3dCtrl->RightEar = pSlot->RightEar;
	m_fFreqFactor        = pSrc->fFreqFactor;

	/* (RE) Original dead store, overwritten before the control push. */
	m_pA3dCtrl->fFreqFactor = m_pA3dCtrl->fFreqFactor * m_fPitch;

	fLeftGain  = 0;
	fRightGain = 0;

	if (m_fSynced)
	{
		m_fApplyPending = 0;
	}
	else
	{
		fLeftGain  = m_pA3dCtrl->LeftEar.fGain;
		fRightGain = m_pA3dCtrl->RightEar.fGain;

		m_pA3dCtrl->LeftEar.fGain  = 0;
		m_pA3dCtrl->RightEar.fGain = 0;

		m_fApplyPending = 1;
	}

	m_pA3dCtrl->fFreqFactor = (FLOAT) (m_fFrequency /
			(double) m_pReflectionDal->m_WaveFormatEx.nSamplesPerSec);

	m_pReflectionDal->PushReflectionSuperCtrl(m_pA3dCtrl);

	if (!m_fSynced)
	{
		m_pA3dCtrl->LeftEar.fGain  = fLeftGain;
		m_pA3dCtrl->RightEar.fGain = fRightGain;
	}

	dwStatus = 0;
	m_pSource->GetStatus(&dwStatus);

	if (dwStatus & DSBSTATUS_PLAYING)
		Start((dwStatus & DSBSTATUS_LOOPING) != 0, fAge);

	return (S_OK);
}

/* =============================================================
// CReflection::Start()
// (RE) dbg:0x10048580; thunk dbg:0x100043b3
//
// Start or resynchronize playback according to the target delay and flags.
//
// Returns: S_OK; delegated failures are ignored.
// =============================================================*/

HRESULT
CReflection::Start(DWORD dwFlags, float fAge)
{
DWORD	dwStatus;

	VERIFY(WaitForSingleObject(m_hMutex, INFINITE) == WAIT_OBJECT_0);

	m_dwStartFlags = dwFlags;

	dwStatus = 0;
	m_pReflectionBuffer->GetStatus(&dwStatus);

	if (dwStatus & DSBSTATUS_PLAYING)
	{
		if (((dwStatus & DSBSTATUS_LOOPING) == 0) !=
		    ((dwFlags & DSBPLAY_LOOPING) == 0))
			m_pReflectionBuffer->Play(0, 0, dwFlags);
	}
	else if (m_fTargetDelay + A3D_D3D_REFLECTION_START_MARGIN_S <= fAge)
	{
		Sync();
	}
	else
	{
		m_fCurrentDelay = m_fTargetDelay;
		m_fStarted      = 1;
	}

	VERIFY(ReleaseMutex(m_hMutex));

	return (S_OK);
}

/* =============================================================
// CReflection::Stop()
// (RE) dbg:0x100488b0; thunk dbg:0x100043ea
//
// Stop the reflection voice.
//
// Returns:
//   Reflection voice Stop result
//   E_FAIL                        without a voice
// =============================================================*/

HRESULT
CReflection::Stop(void)
{
	if (m_pReflectionBuffer != NULL)
		return (m_pReflectionBuffer->Stop());

	return (E_FAIL);
}

/* =============================================================
// CReflection::AgeStep()
// (RE) dbg:0x10047d40; thunk dbg:0x10002c75
//
// Age once per pass and write 1 when processed, or 0 when already aged.
// =============================================================*/

void
CReflection::AgeStep(LPDWORD lpfAged)
{
	if (m_fAgedThisTick)
	{
		*lpfAged = 0;
		return;
	}

	*lpfAged = 1;
	m_fAgedThisTick = 1;

	Age();
}

/* =============================================================
// CReflection::ClearAged()
// (RE) dbg:0x10047c70; thunk dbg:0x10001f55
//
// Clear the flag for aging in the next pass.
// =============================================================*/

void
CReflection::ClearAged(void)
{
	m_fAgedThisTick = 0;
}

/* =============================================================
// CReflection::Age()
// (RE) dbg:0x10047fd0
//
// Advance delayed playback and adjust pitch to maintain the target delay.
//
// Returns: S_OK; delegated failures are ignored.
// =============================================================*/

HRESULT
CReflection::Age(void)
{
DWORD	dwStatus;
DWORD	dwReflPlay;
DWORD	dwReflWrite;
DWORD	dwSrcPlay;
DWORD	dwSrcWrite;
DWORD	dwSamples;
double	fTargetDelay;
double	fMeasuredDelay;
double	fWrapDelay;
double	fError;
double	fChosen;

	m_fAgedThisTick = 1;

	if (m_fStarted)
	{
		m_fCurrentDelay = m_fCurrentDelay - A3D_D3D_REFLECTION_AGE_STEP_S;

		if (m_fCurrentDelay <= A3D_D3D_REFLECTION_START_THRESHOLD_S)
		{
			m_pReflectionBuffer->SetCurrentPosition(0);
			Sync();
		}
	}

	dwStatus = 0;
	m_pReflectionBuffer->GetStatus(&dwStatus);

	if (dwStatus & DSBSTATUS_PLAYING)
	{
		if (m_fApplyPending)
		{
			m_pA3dCtrl->fFreqFactor = m_fFreqFactor * m_fPitch;
			m_pReflectionDal->PushReflectionSuperCtrl(m_pA3dCtrl);
			m_fApplyPending = 0;
		}

		dwStatus = 0;
		m_pSource->GetStatus(&dwStatus);

		if (dwStatus & DSBSTATUS_PLAYING)
		{
			m_pReflectionBuffer->GetCurrentPosition(&dwReflPlay, &dwReflWrite);
			m_pSource->GetCurrentPosition(&dwSrcPlay, &dwSrcWrite);

			ASSERT(m_nBytesPerSample);
			ASSERT(m_dwBytesInBuffer / m_nBytesPerSample);

			fTargetDelay = (double)
			    ((__int64) (m_fTargetDelay * m_fFrequency) %
			     (m_dwBytesInBuffer / m_nBytesPerSample)) / m_fFrequency;

			if (dwSrcPlay >= dwReflPlay)
			{
				ASSERT(m_nBytesPerSample);
				dwSamples = (dwSrcPlay - dwReflPlay) / m_nBytesPerSample;
			}
			else
			{
				ASSERT(m_nBytesPerSample);
				dwSamples = (m_dwBytesInBuffer - dwReflPlay + dwSrcPlay + 1) /
					    m_nBytesPerSample;
			}

			fMeasuredDelay = (double) dwSamples / m_fFrequency;

			ASSERT(m_nBytesPerSample);
			fWrapDelay = -((double) (m_dwBytesInBuffer / m_nBytesPerSample -
						 dwSamples) / m_fFrequency);

			fError = fabs(fMeasuredDelay - fTargetDelay);

			if (fabs(fTargetDelay + fWrapDelay) <= fError)
				fChosen = fWrapDelay;
			else
				fChosen = fMeasuredDelay;

			if (fChosen - fTargetDelay <= A3D_D3D_REFLECTION_DRIFT_THRESHOLD_S)
			{
				if (fTargetDelay - fChosen <= A3D_D3D_REFLECTION_DRIFT_THRESHOLD_S)
				{
					if (fabs(fChosen - fTargetDelay) < A3D_D3D_REFLECTION_SETTLE_THRESHOLD_S)
					{
						if (!m_fPitchSettled)
						{
							m_fPitch = 1.0f;
							m_pA3dCtrl->fFreqFactor =
								m_fFreqFactor * m_fPitch;
							m_pReflectionDal->PushReflectionSuperCtrl(
								m_pA3dCtrl);
						}

						m_fPitchSettled = 1;
					}
				}
				else
				{
					m_fPitchSettled         = 0;
					m_fPitch                = A3D_D3D_REFLECTION_PITCH_DOWN;
					m_pA3dCtrl->fFreqFactor = m_fFreqFactor * m_fPitch;
					m_pReflectionDal->PushReflectionSuperCtrl(m_pA3dCtrl);
				}
			}
			else
			{
				m_fPitchSettled         = 0;
				m_fPitch                = A3D_D3D_REFLECTION_PITCH_UP;
				m_pA3dCtrl->fFreqFactor = m_fFreqFactor * m_fPitch;
				m_pReflectionDal->PushReflectionSuperCtrl(m_pA3dCtrl);
			}
		}
	}
	else
	{
		m_fSynced       = 0;
		m_fPitchSettled = 0;
	}

	return (S_OK);
}

/* =============================================================
// CReflection::Sync()
// (RE) dbg:0x10048700
//
// Position the reflection behind the source by the target delay and play it.
//
// Returns: Reflection voice Play result.
// =============================================================*/

HRESULT
CReflection::Sync(void)
{
DWORD	dwDelta;
DWORD	dwSourcePosition;

	ASSERT(m_dwBytesInBuffer);

	dwDelta = (DWORD) ((__int64) (m_fTargetDelay * m_fFrequency *
			   (double) m_nBytesPerSample)) % m_dwBytesInBuffer;

	m_pSource->GetCurrentPosition(&dwSourcePosition, NULL);

	if (dwSourcePosition <= dwDelta)
	{
		ASSERT((int) (m_dwBytesInBuffer - dwDelta % m_dwBytesInBuffer +
			      dwSourcePosition) >= 0);

		m_pReflectionBuffer->SetCurrentPosition(dwSourcePosition + m_dwBytesInBuffer -
							dwDelta % m_dwBytesInBuffer);
	}
	else
	{
		m_pReflectionBuffer->SetCurrentPosition(dwSourcePosition - dwDelta);
	}

	m_fSynced       = 1;
	m_fPitchSettled = 1;
	m_fStarted      = 0;

	return (m_pReflectionBuffer->Play(0, 0, m_dwStartFlags));
}
