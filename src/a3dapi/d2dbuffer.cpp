/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * d2dbuffer.cpp
 *
 * Implements D2DBuffer, a DAL voice backed by an ordinary DirectSound
 * buffer. Most buffer operations are forwarded, while A3D source controls
 * are reduced to playback frequency, volume and pan.
 *
 * The adapter retains optional property-set access and resource-manager
 * priority and state. This file also defines a group of A2DBuffer methods
 * for software playback controls and format queries. DAL_D2D creates the
 * wrapped voices; DAL_A2D owns the software voices.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "d2dbuffer.h"
#include "a2dbuffer.h"

#include <math.h>

/* Gain conversion constants.*/
#define A3D_D2D_MIN_LOG_GAIN            0.00001f
#define A3D_D2D_GAIN_TO_VOLUME_SCALE    2000.0

/* =============================================================
// D2DBuffer::D2DBuffer()
// (RE) rtl:0x1001b7a0; dbg:0x10041f10
//
// Initialize an empty DirectSound buffer adapter.
// =============================================================*/

D2DBuffer::D2DBuffer(void)
{
	m_cRef          = 1;
	m_fPriority     = 0;
	m_dwBufferState = 0;

	m_lpDirectSoundBuffer = NULL;

	m_pList         = NULL;
	m_lpPropertySet = NULL;
}

/* =============================================================
// D2DBuffer::~D2DBuffer()
// (RE) rtl:0x1001b800; dbg:0x10041ff0
//
// Release the buffer and property set, then remove device list membership.
// =============================================================*/

D2DBuffer::~D2DBuffer(void)
{
	if (m_lpPropertySet != NULL)
	{
		m_lpPropertySet->Release();
		m_lpPropertySet = NULL;
	}

	if (m_lpDirectSoundBuffer != NULL)
	{
		m_lpDirectSoundBuffer->Release();
		m_lpDirectSoundBuffer = NULL;
	}

	if (m_pList != NULL)
	{
		if (m_pList->Find(this) != NULL)
			m_pList->RemoveAt(m_pList->Find(this));
	}
}

/* =============================================================
// D2DBuffer::QueryInterface()
// (RE) rtl:0x1001b8f0; dbg:0x10042110
//
// Obtain a referenced buffer interface. The original returns IDirectSoundBuffer
// for IID_IDirectSoundNotify.
//
// Returns:
//   S_OK
//   E_INVALIDARG   a null output
//   E_NOINTERFACE  an unknown IID
// =============================================================*/

STDMETHODIMP
D2DBuffer::QueryInterface(REFIID riid, void **ppv)
{
	if (ppv == NULL)
		return (E_INVALIDARG);

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
// D2DBuffer::AddRef()
// (RE) rtl:0x1001ba20; dbg:0x100422b0
//
// Increment the reference count.
//
// Returns: Reference count read after the increment.
// =============================================================*/

STDMETHODIMP_(ULONG)
D2DBuffer::AddRef(void)
{
	InterlockedIncrement(&m_cRef);

	return ((ULONG) m_cRef);
}

/* =============================================================
// D2DBuffer::Release()
// (RE) rtl:0x1001ba40; dbg:0x100422e0
//
// Release a reference and delete the buffer at zero.
// The original rereads the count after the interlocked decrement.
//
// Returns: Remaining reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
D2DBuffer::Release(void)
{
	InterlockedDecrement(&m_cRef);

	if (m_cRef != 0)
		return ((ULONG) m_cRef);

	delete this;

	return (0);
}

/* =============================================================
// D2DBuffer::GetCaps()
// (RE) rtl:0x1001ba80; dbg:0x100423a0
//
// Read buffer capabilities.
//
// Returns:
//   DirectSound GetCaps result
//   E_FAIL                      without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D2DBuffer::GetCaps(LPDSBCAPS lpDSBufferCaps)
{
	ASSERT(lpDSBufferCaps != 0 &&
	       !IsBadReadPtr(lpDSBufferCaps, sizeof(DSBCAPS)));

	if (m_lpDirectSoundBuffer != NULL)
		return (m_lpDirectSoundBuffer->GetCaps(lpDSBufferCaps));

	DBGSTR("D2DBuffer::GetCaps - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D2DBuffer::GetCurrentPosition()
// (RE) rtl:0x1001bab0; dbg:0x10042460
//
// Read playback and write cursors.
//
// Returns:
//   DirectSound GetCurrentPosition result
//   E_FAIL                              without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D2DBuffer::GetCurrentPosition(LPDWORD pdwCurrentPlayCursor,
			      LPDWORD pdwCurrentWriteCursor)
{
	if (m_lpDirectSoundBuffer != NULL)
		return (m_lpDirectSoundBuffer->GetCurrentPosition(pdwCurrentPlayCursor,
								  pdwCurrentWriteCursor));

	DBGSTR("D2DBuffer::GetCurrentPosition - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D2DBuffer::GetFormat()
// (RE) rtl:0x1001bae0; dbg:0x100424d0
//
// Read the buffer format.
//
// Returns:
//   DirectSound GetFormat result
//   E_FAIL                        without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D2DBuffer::GetFormat(LPWAVEFORMATEX lpwfxFormat, DWORD dwSizeAllocated,
		     LPDWORD lpdwSizeWritten)
{
	ASSERT(lpwfxFormat != 0 &&
	       !IsBadReadPtr(lpwfxFormat, sizeof(WAVEFORMATEX)));
	ASSERT(lpdwSizeWritten != 0 &&
	       !IsBadReadPtr(lpdwSizeWritten, sizeof(DWORD)));

	if (m_lpDirectSoundBuffer != NULL)
		return (m_lpDirectSoundBuffer->GetFormat(lpwfxFormat, dwSizeAllocated,
							 lpdwSizeWritten));

	DBGSTR("D2DBuffer::GetFormat - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D2DBuffer::GetVolume()
// (RE) rtl:0x1001bb10; dbg:0x100425f0
//
// Read playback volume.
//
// Returns:
//   DirectSound GetVolume result
//   E_FAIL                        without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D2DBuffer::GetVolume(LPLONG lplVolume)
{
	ASSERT(lplVolume != 0 && !IsBadReadPtr(lplVolume, sizeof(LONG)));

	if (m_lpDirectSoundBuffer != NULL)
		return (m_lpDirectSoundBuffer->GetVolume(lplVolume));

	DBGSTR("D2DBuffer::GetVolume - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D2DBuffer::GetPan()
// (RE) rtl:0x1001bb40; dbg:0x100426b0
//
// Read playback pan.
//
// Returns:
//   DirectSound GetPan result
//   E_FAIL                     without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D2DBuffer::GetPan(LPLONG lplPan)
{
	ASSERT(lplPan != 0 && !IsBadReadPtr(lplPan, sizeof(LONG)));

	if (m_lpDirectSoundBuffer != NULL)
		return (m_lpDirectSoundBuffer->GetPan(lplPan));

	DBGSTR("D2DBuffer::GetPan - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D2DBuffer::GetFrequency()
// (RE) rtl:0x1001bb70; dbg:0x10042770
//
// Read playback frequency.
//
// Returns:
//   DirectSound GetFrequency result
//   E_FAIL                           without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D2DBuffer::GetFrequency(LPDWORD lpdwFrequency)
{
	ASSERT(lpdwFrequency != 0 && !IsBadReadPtr(lpdwFrequency, sizeof(DWORD)));

	if (m_lpDirectSoundBuffer != NULL)
		return (m_lpDirectSoundBuffer->GetFrequency(lpdwFrequency));

	DBGSTR("D2DBuffer::GetFrequency - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D2DBuffer::GetStatus()
// (RE) rtl:0x1001bba0; dbg:0x10042830
//
// Read playback status.
//
// Returns:
//   DirectSound GetStatus result
//   E_FAIL                        without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D2DBuffer::GetStatus(LPDWORD lpdwStatus)
{
	ASSERT(lpdwStatus != 0 && !IsBadReadPtr(lpdwStatus, sizeof(DWORD)));

	if (m_lpDirectSoundBuffer != NULL)
		return (m_lpDirectSoundBuffer->GetStatus(lpdwStatus));

	DBGSTR("D2DBuffer::GetStatus - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D2DBuffer::Initialize()
// (RE) rtl:0x1001bbd0; dbg:0x100428f0
//
// Initialize the wrapped buffer.
//
// Returns:
//   DirectSound Initialize result
//   E_FAIL                         without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D2DBuffer::Initialize(LPDIRECTSOUND lpDirectSound, LPCDSBUFFERDESC lpcDSBufferDesc)
{
	ASSERT(lpDirectSound != 0 &&
	       !IsBadReadPtr(lpDirectSound, sizeof(IDirectSound)));
	ASSERT(lpcDSBufferDesc != 0 &&
	       !IsBadReadPtr(lpcDSBufferDesc, sizeof(DSBUFFERDESC)));

	if (m_lpDirectSoundBuffer != NULL)
		return (m_lpDirectSoundBuffer->Initialize(lpDirectSound, lpcDSBufferDesc));

	DBGSTR("D2DBuffer::Initialize - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D2DBuffer::Lock()
// (RE) rtl:0x1001bc00; dbg:0x10042a10
//
// Lock audio storage.
//
// Returns:
//   DirectSound Lock result
//   E_FAIL                   without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D2DBuffer::Lock(DWORD dwOffset, DWORD dwWriteBytes, LPVOID *lplpvAudioPtr1,
		LPDWORD lpdwAudioBytes1, LPVOID *lplpvAudioPtr2,
		LPDWORD lpdwAudioBytes2, DWORD dwFlags)
{
	ASSERT(lplpvAudioPtr1 != 0);
	ASSERT(lpdwAudioBytes1 != 0 &&
	       !IsBadReadPtr(lpdwAudioBytes1, sizeof(DWORD)));
	ASSERT(dwWriteBytes <= m_DSBufferDesc.dwBufferBytes);

	if (m_lpDirectSoundBuffer != NULL)
		return (m_lpDirectSoundBuffer->Lock(dwOffset, dwWriteBytes,
						    lplpvAudioPtr1, lpdwAudioBytes1,
						    lplpvAudioPtr2, lpdwAudioBytes2,
						    dwFlags));

	DBGSTR("D2DBuffer::Lock - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D2DBuffer::Play()
// (RE) rtl:0x1001bc40; dbg:0x10042b70
//
// Start playback.
//
// Returns:
//   DirectSound Play result
//   E_FAIL                   without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D2DBuffer::Play(DWORD dwReserved1, DWORD dwReserved2, DWORD dwFlags)
{
	if (m_lpDirectSoundBuffer != NULL)
		return (m_lpDirectSoundBuffer->Play(dwReserved1, dwReserved2, dwFlags));

	DBGSTR("D2DBuffer::Play - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D2DBuffer::SetCurrentPosition()
// (RE) rtl:0x1001bc70; dbg:0x10042bf0
//
// Set the playback cursor.
//
// Returns:
//   DirectSound SetCurrentPosition result
//   E_FAIL                              without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D2DBuffer::SetCurrentPosition(DWORD dwNewPosition)
{
	if (m_lpDirectSoundBuffer != NULL)
		return (m_lpDirectSoundBuffer->SetCurrentPosition(dwNewPosition));

	DBGSTR("D2DBuffer::SetCurrentPosition - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D2DBuffer::SetFormat()
// (RE) rtl:0x1001bca0; dbg:0x10042c60
//
// Set the buffer format.
//
// Returns:
//   DirectSound SetFormat result
//   E_FAIL                        without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D2DBuffer::SetFormat(LPCWAVEFORMATEX lpcfxFormat)
{
	if (m_lpDirectSoundBuffer != NULL)
		return (m_lpDirectSoundBuffer->SetFormat(lpcfxFormat));

	DBGSTR("D2DBuffer::SetFormat - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D2DBuffer::SetVolume()
// (RE) rtl:0x1001bcd0; dbg:0x10042cd0
//
// Set playback volume.
//
// Returns:
//   DirectSound SetVolume result
//   E_FAIL                        without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D2DBuffer::SetVolume(LONG lVolume)
{
	if (m_lpDirectSoundBuffer != NULL)
		return (m_lpDirectSoundBuffer->SetVolume(lVolume));

	DBGSTR("D2DBuffer::SetVolume - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D2DBuffer::SetPan()
// (RE) rtl:0x1001bd00; dbg:0x10042d40
//
// Set playback pan.
//
// Returns:
//   DirectSound SetPan result
//   E_FAIL                     without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D2DBuffer::SetPan(LONG lPan)
{
	if (m_lpDirectSoundBuffer != NULL)
		return (m_lpDirectSoundBuffer->SetPan(lPan));

	DBGSTR("D2DBuffer::SetPan - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D2DBuffer::SetFrequency()
// (RE) rtl:0x1001bd30; dbg:0x10042db0
//
// Set playback frequency.
//
// Returns:
//   DirectSound SetFrequency result
//   E_FAIL                           without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D2DBuffer::SetFrequency(DWORD dwFrequency)
{
	if (m_lpDirectSoundBuffer != NULL)
		return (m_lpDirectSoundBuffer->SetFrequency(dwFrequency));

	DBGSTR("D2DBuffer::SetFrequency - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D2DBuffer::Stop()
// (RE) rtl:0x1001bd60; dbg:0x10042e20
//
// Stop playback.
//
// Returns:
//   DirectSound Stop result
//   E_FAIL                   without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D2DBuffer::Stop(void)
{
	if (m_lpDirectSoundBuffer != NULL)
		return (m_lpDirectSoundBuffer->Stop());

	DBGSTR("D2DBuffer::Stop - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D2DBuffer::Unlock()
// (RE) rtl:0x1001bd80; dbg:0x10042e90
//
// Unlock audio storage.
//
// Returns:
//   DirectSound Unlock result
//   E_FAIL                     without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D2DBuffer::Unlock(LPVOID lpvAudioPtr1, DWORD dwAudioBytes1, LPVOID lpvAudioPtr2,
		  DWORD dwAudioBytes2)
{
	if (m_lpDirectSoundBuffer != NULL)
		return (m_lpDirectSoundBuffer->Unlock(lpvAudioPtr1, dwAudioBytes1,
						      lpvAudioPtr2, dwAudioBytes2));

	DBGSTR("D2DBuffer::Unlock - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D2DBuffer::Restore()
// (RE) rtl:0x1001bdb0; dbg:0x10042f10
//
// Restore audio storage.
//
// Returns:
//   DirectSound Restore result
//   E_FAIL                      without a wrapped buffer
// =============================================================*/

STDMETHODIMP
D2DBuffer::Restore(void)
{
	if (m_lpDirectSoundBuffer != NULL)
		return (m_lpDirectSoundBuffer->Restore());

	DBGSTR("D2DBuffer::Restore - m_lpDirectSoundBuffer is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D2DBuffer::Unknown_0x54()
// (RE) rtl:0x10003500; dbg:0x10043760
//
// Debug ret 0x0C conflicts with the declared signature; ABI unresolved.
// Leave buffer state unchanged; the operation is unresolved.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
D2DBuffer::Unknown_0x54(DWORD, DWORD)
{
	return (E_NOTIMPL);
}

/* =============================================================
// D2DBuffer::SetA3dSuperCtrl()
// (RE) rtl:0x1001bdd0; dbg:0x10042f80
//
// Store source controls and apply frequency, volume and pan to DirectSound.
//
// Returns: S_OK; individual setter failures are ignored.
// =============================================================*/

STDMETHODIMP
D2DBuffer::SetA3dSuperCtrl(LPA3DCTRL_SRC_SUPER lpA3dCtrlSuper, DWORD dwSize)
{
FLOAT	fLeft;
FLOAT	fRight;
FLOAT	fRatio;
LONG	lVolume;
LONG	lPan;

	CopyMemory(&m_A3dCtrlSuper, lpA3dCtrlSuper, sizeof(m_A3dCtrlSuper));

	SetFrequency((DWORD) ((double) m_wfx.nSamplesPerSec *
			      lpA3dCtrlSuper->fFreqFactor));

	fLeft  = lpA3dCtrlSuper->LeftEar.fGain;
	fRight = lpA3dCtrlSuper->RightEar.fGain;

	if (fLeft <= fRight)
	{
		if (fRight >= A3D_D2D_MIN_LOG_GAIN)
			lVolume = (LONG) (log10((double) fRight) * A3D_D2D_GAIN_TO_VOLUME_SCALE);
		else
			lVolume = DSBVOLUME_MIN;

		fRatio = fLeft / fRight;

		if (fRatio >= A3D_D2D_MIN_LOG_GAIN)
			lPan = (LONG) (log10((double) fRatio) * A3D_D2D_GAIN_TO_VOLUME_SCALE * -1.0);
		else
			lPan = DSBPAN_RIGHT;
	}
	else
	{
		if (fLeft >= A3D_D2D_MIN_LOG_GAIN)
			lVolume = (LONG) (log10((double) fLeft) * A3D_D2D_GAIN_TO_VOLUME_SCALE);
		else
			lVolume = DSBVOLUME_MIN;

		fRatio = fRight / fLeft;

		if (fRatio >= A3D_D2D_MIN_LOG_GAIN)
			lPan = (LONG) (log10((double) fRatio) * A3D_D2D_GAIN_TO_VOLUME_SCALE);
		else
			lPan = DSBPAN_LEFT;
	}

	SetVolume(lVolume);
	SetPan(lPan);

	return (S_OK);
}

/* =============================================================
// D2DBuffer::GetAllocationStatus()
// (RE) rtl:0x100034f0; dbg:0x100431a0
//
// Leave the allocation status output unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
D2DBuffer::GetAllocationStatus(LPDWORD lpdwStatus)
{
	ASSERT(lpdwStatus != 0 && !IsBadReadPtr(lpdwStatus, sizeof(DWORD)));

	return (S_OK);
}

/* =============================================================
// D2DBuffer::GetWave()
// (RE) rtl:0x100034f0; dbg:0x10043210
//
// Leave the audio pointer output unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
D2DBuffer::GetWave(LPBYTE *lplpbWave)
{
	ASSERT(lplpbWave != 0);

	return (S_OK);
}

/* =============================================================
// D2DBuffer::GetDriverInfo()
// (RE) rtl:0x1001b380; dbg:0x10043270
//
// Leave driver information outputs unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
D2DBuffer::GetDriverInfo(void **lplpIDsDriverBuffer, void **lplpIA3dDriverBuffer)
{
	ASSERT(lplpIDsDriverBuffer != 0);
	ASSERT(lplpIA3dDriverBuffer != 0);

	return (S_OK);
}

/* =============================================================
// D2DBuffer::SetNewBuffer()
// (RE) rtl:0x1001b380; dbg:0x10043310
//
// Leave audio storage unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
D2DBuffer::SetNewBuffer(LPBYTE lpbWave, DWORD dwBufferBytes)
{
	ASSERT(lpbWave != 0);

	return (S_OK);
}

/* =============================================================
// D2DBuffer::SetA3dDirectCtrl()
// (RE) rtl:0x1001b380; dbg:0x10043370
//
// Leave direct controls unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
D2DBuffer::SetA3dDirectCtrl(LPA3DCTRL_SRC_SUPER lpA3dCtrlDirect, DWORD dwSize)
{
	ASSERT(lpA3dCtrlDirect != 0);

	return (S_OK);
}

/* =============================================================
// D2DBuffer::GetStatusEx()
// (RE) rtl:0x100034f0; dbg:0x100433d0
//
// Leave the extended status output unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
D2DBuffer::GetStatusEx(LPDWORD lpdwStatusEx)
{
	ASSERT(lpdwStatusEx != 0 && !IsBadReadPtr(lpdwStatusEx, sizeof(DWORD)));

	return (S_OK);
}

/* =============================================================
// D2DBuffer::GetPriority()
// (RE) rtl:0x1000ac30; dbg:0x10043440
//
// Write the resource manager priority.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
D2DBuffer::GetPriority(FLOAT *pfPriority)
{
	ASSERT(pfPriority != 0 && !IsBadReadPtr(pfPriority, sizeof(FLOAT)));

	if (pfPriority != NULL)
	{
		ASSERT(m_fPriority >= 0.0 && m_fPriority <= 1.0);

		*pfPriority = m_fPriority;

		return (S_OK);
	}

	DBGSTR("D2DBuffer::GetPriority() - Invalid pointer passed in.\n");

	return (E_POINTER);
}

/* =============================================================
// D2DBuffer::GetBufferState()
// (RE) rtl:0x1001bef0; dbg:0x10043560
//
// Write the stored buffer state.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
D2DBuffer::GetBufferState(LPDWORD lpdwBufferState)
{
	ASSERT(lpdwBufferState != 0 &&
	       !IsBadReadPtr(lpdwBufferState, sizeof(DWORD)));

	*lpdwBufferState = m_dwBufferState;

	return (S_OK);
}

/* =============================================================
// D2DBuffer::SetPriority()
// (RE) rtl:0x1001bf10; dbg:0x100435e0
//
// Store the resource manager priority.
//
// Returns:
//   S_OK
//   E_INVALIDARG  unless priority is in [0, 1]
// =============================================================*/

STDMETHODIMP
D2DBuffer::SetPriority(FLOAT fPriority)
{
	ASSERT(fPriority >= 0.0 && fPriority <= 1.0);

	if (fPriority >= 0.0 && fPriority <= 1.0)
	{
		m_fPriority = fPriority;

		return (S_OK);
	}

	DBGSTR("D2DBuffer::SetPriority() - Invalid priority value sent.\n");

	return (E_INVALIDARG);
}

/* =============================================================
// D2DBuffer::Enable()
// (RE) rtl:0x1001b1c0; dbg:0x100436c0
//
// Leave buffer state unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
D2DBuffer::Enable(void)
{
	return (S_OK);
}

/* =============================================================
// D2DBuffer::Disable()
// (RE) rtl:0x1001b1c0; dbg:0x100436e0
//
// Leave buffer state unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
D2DBuffer::Disable(void)
{
	return (S_OK);
}

/* =============================================================
// D2DBuffer::SetNativeModeDisabled()
// (RE) rtl:0x10020b90; dbg:0x10043700
//
// Reject the native-mode-disable request
//
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
D2DBuffer::SetNativeModeDisabled(DWORD)
{
	return (E_NOTIMPL);
}

/* =============================================================
// D2DBuffer::Unknown_0x28()
// (RE) rtl:0x10020b90; dbg:0x10043720
//
// Leave buffer state unchanged; the operation is unresolved.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
D2DBuffer::Unknown_0x28(DWORD)
{
	return (E_NOTIMPL);
}

/* =============================================================
// D2DBuffer::GetControlBufferPair()
// (RE) rtl:0x10020b90; dbg:0x10043740
//
// Reject the control-buffer-pair query
//
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
D2DBuffer::GetControlBufferPair(DWORD)
{
	return (E_NOTIMPL);
}

/* =============================================================
// D2DBuffer::Init()
// (RE) rtl:0x1001bf50; dbg:0x10043780
//
// Retain the DirectSound buffer, copy its format and record its device list.
// Query the optional property set.
//
// Returns: S_OK, including when the property set is unavailable.
// =============================================================*/

HRESULT
D2DBuffer::Init(IDirectSoundBuffer *pDSBuffer, const DSBUFFERDESC1 *pDesc,
		CList *pList)
{
	m_lpDirectSoundBuffer = pDSBuffer;
	m_lpDirectSoundBuffer->AddRef();

	CopyMemory(&m_DSBufferDesc, pDesc, sizeof(m_DSBufferDesc));

	m_wfx                           = *pDesc->lpwfxFormat;
	m_DSBufferDesc.lpwfxFormat      = &m_wfx;

	m_pList = pList;

	if (FAILED(m_lpDirectSoundBuffer->QueryInterface(IID_IKsPropertySet,
							 (void **) &m_lpPropertySet)))
		m_lpPropertySet = NULL;

	return (S_OK);
}

/* =============================================================
// D2DBuffer::InitDuplicate()
// (RE) rtl:0x1001bfd0; dbg:0x10043870
//
// Take ownership of the duplicate reference and copy the original format and
// list.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
D2DBuffer::InitDuplicate(LPDIRECTSOUNDBUFFER lpOriginalBuffer,
			 LPDIRECTSOUNDBUFFER lpDuplicateSoundBuffer)
{
D2DBuffer	*pOriginal;

	ASSERT(lpOriginalBuffer != 0 &&
	       !IsBadReadPtr(lpOriginalBuffer, sizeof(IDirectSoundBuffer)));
	ASSERT(lpDuplicateSoundBuffer != 0 &&
	       !IsBadReadPtr(lpDuplicateSoundBuffer, sizeof(IDirectSoundBuffer)));

	pOriginal = (D2DBuffer *) lpOriginalBuffer;

	CopyMemory(&m_DSBufferDesc, &pOriginal->m_DSBufferDesc,
		   sizeof(m_DSBufferDesc) + sizeof(m_wfx));
	m_DSBufferDesc.lpwfxFormat = &m_wfx;

	m_pList               = pOriginal->m_pList;
	m_lpDirectSoundBuffer = lpDuplicateSoundBuffer;

	return (S_OK);
}

/* =============================================================
// D2DBuffer::Get()
// (RE) rtl:0x1001c0a0; dbg:0x10043b90
//
// Read a property.
//
// Returns:
//   IKsPropertySet Get result
//   E_FAIL                     without a property set
// =============================================================*/

STDMETHODIMP
D2DBuffer::Get(REFGUID rguidPropSet, ULONG ulId, LPVOID pInstanceData,
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

	DBGSTR("D2DBuffer::Get - m_lpPropertySet is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D2DBuffer::Set()
// (RE) rtl:0x1001c060; dbg:0x10043aa0
//
// Write a property.
//
// Returns:
//   IKsPropertySet Set result
//   E_FAIL                     without a property set
// =============================================================*/

STDMETHODIMP
D2DBuffer::Set(REFGUID rguidPropSet, ULONG ulId, LPVOID pInstanceData,
	       ULONG ulInstanceLength, LPVOID pPropertyData, ULONG ulDataLength)
{
	if (m_lpPropertySet != NULL)
	{
		ASSERT(m_lpPropertySet != 0 &&
		       !IsBadReadPtr(m_lpPropertySet, sizeof(IKsPropertySet)));

		return (m_lpPropertySet->Set(rguidPropSet, ulId, pInstanceData,
					     ulInstanceLength, pPropertyData,
					     ulDataLength));
	}

	DBGSTR("D3DBuffer::Set - m_lpPropertySet is NULL.\n");

	return (E_FAIL);
}

/* =============================================================
// D2DBuffer::QuerySupport()
// (RE) rtl:0x1001c030; dbg:0x100439c0
//
// Read property support.
//
// Returns:
//   IKsPropertySet QuerySupport result
//   E_FAIL                              without a property set
// =============================================================*/

STDMETHODIMP
D2DBuffer::QuerySupport(REFGUID rguidPropSet, ULONG ulId, PULONG pulTypeSupport)
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
// A2DBuffer::QueryInterface()
// (RE) dbg:0x1003ffb0; rtl:0x1001ace0
//
// Obtain a referenced buffer interface. The original returns IDirectSoundBuffer
// for IID_IDirectSoundNotify. This body calls primary AddRef; the original
// calls through the returned interface.
//
// Returns:
//   S_OK
//   E_INVALIDARG   a null output
//   E_NOINTERFACE  an unknown IID
// =============================================================*/

STDMETHODIMP
A2DBuffer::QueryInterface(REFIID riid, void **ppv)
{
	if (!ppv)
		return (E_INVALIDARG);

	if (IsEqualIID(riid, IID_IUnknown) ||
	    IsEqualIID(riid, IID_IDirectSoundBuffer))
	{
		*ppv = static_cast<IDirectSoundBuffer *>(this);
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
		*ppv = static_cast<IDirectSoundBuffer *>(this);
	}
	else if (IsEqualIID(riid, IID_IKsPropertySet))
	{
		*ppv = static_cast<IKsPropertySet *>(this);
	}
	else
	{
		*ppv = NULL;

		return (E_NOINTERFACE);
	}

	AddRef();

	return (S_OK);
}

/* =============================================================
// A2DBuffer::SetCurrentPosition()
// (RE) dbg:0x10040b30; rtl:0x1001b0e0
//
// Set the playback byte cursor.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
A2DBuffer::SetCurrentPosition(DWORD dwPlay)
{
	m_nPendingSeek = dwPlay;

	return (S_OK);
}

/* =============================================================
// A2DBuffer::SetPan()
// (RE) dbg:0x10040bf0; rtl:0x1001b130
//
// Store playback pan.
//
// Returns:
//   S_OK
//   E_INVALIDARG  outside [-10000, 10000]
// =============================================================*/

STDMETHODIMP
A2DBuffer::SetPan(LONG lPan)
{
	if (lPan >= DSBPAN_LEFT && lPan <= DSBPAN_RIGHT)
	{
		m_lPan = lPan;

		return (S_OK);
	}
	else
	{
		DBGSTR("A2DBuffer::SetPan() - Invalid value passed in.  Must be between DSBPAN_LEFT & DSBPAN_RIGHT.\n");

		return (E_INVALIDARG);
	}
}

/* =============================================================
// A2DBuffer::SetFrequency()
// (RE) dbg:0x10040c60; rtl:0x1001b160
//
// Set playback frequency, using the original sample rate for zero.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
A2DBuffer::SetFrequency(DWORD dwFrequency)
{
	if (dwFrequency)
		m_dwFrequency = dwFrequency;
	else
		m_dwFrequency = m_dwSampleRate;

	return (S_OK);
}

/* =============================================================
// A2DBuffer::Stop()
// (RE) dbg:0x10040ca0; rtl:0x1001b190
//
// Mark the voice stopped.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
A2DBuffer::Stop(void)
{
	m_dwState = A3DVOICE_STATE_STOPPED;

	return (S_OK);
}

/* =============================================================
// A2DBuffer::GetCaps()
// (RE) dbg:0x100401d0; rtl:0x1001ae60
//
// Write software voice capabilities and buffer size.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
A2DBuffer::GetCaps(LPDSBCAPS lpDSBufferCaps)
{
	ASSERT(lpDSBufferCaps != 0 &&
	       !IsBadReadPtr(lpDSBufferCaps, sizeof(DSBCAPS)));

	lpDSBufferCaps->dwFlags  = 0;
	lpDSBufferCaps->dwFlags |= DSBCAPS_CTRL3D;
	lpDSBufferCaps->dwFlags |= DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLPAN |
				   DSBCAPS_CTRLVOLUME;
	lpDSBufferCaps->dwFlags |= DSBCAPS_LOCSOFTWARE;

	lpDSBufferCaps->dwBufferBytes        = m_DSBufferDesc.dwBufferBytes;
	lpDSBufferCaps->dwUnlockTransferRate = -1;
	lpDSBufferCaps->dwPlayCpuOverhead    = 0;

	return (S_OK);
}

/* =============================================================
// A2DBuffer::GetFormat()
// (RE) dbg:0x10040310; rtl:0x1001aed0
//
// Copy the stored format. The original ignores the allocated size and leaves
// the size-written output unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
A2DBuffer::GetFormat(LPWAVEFORMATEX lpwfxFormat, DWORD cb, DWORD *lpdwSizeWritten)
{
	ASSERT(lpwfxFormat != 0 && !IsBadReadPtr(lpwfxFormat, sizeof(WAVEFORMATEX)));
	ASSERT(lpdwSizeWritten != 0 && !IsBadReadPtr(lpdwSizeWritten, sizeof(DWORD)));

	*lpwfxFormat = m_wfx;

	return (S_OK);
}

/* =============================================================
// A2DBuffer::Initialize()
// (RE) dbg:0x10040660; rtl:0x1001b380
//
// Leave the buffer unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
A2DBuffer::Initialize(LPDIRECTSOUND lpDirectSound, LPCDSBUFFERDESC lpcDSBufferDesc)
{
	ASSERT(lpDirectSound != 0 && !IsBadReadPtr(lpDirectSound, sizeof(IDirectSound)));
	ASSERT(lpcDSBufferDesc != 0 && !IsBadReadPtr(lpcDSBufferDesc, sizeof(DSBUFFERDESC)));

	return (S_OK);
}

/* =============================================================
// A2DBuffer::SetFormat()
// (RE) dbg:0x10040b60; rtl:0x100034f0
//
// Leave the format unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
A2DBuffer::SetFormat(LPCWAVEFORMATEX pwfx)
{
	return (S_OK);
}

/* =============================================================
// A2DBuffer::Unlock()
// (RE) dbg:0x10040cd0; rtl:0x1001b1b0
//
// Leave the buffer unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
A2DBuffer::Unlock(LPVOID pvAudioPtr1, DWORD dwAudioBytes1, LPVOID pvAudioPtr2,
		  DWORD dwAudioBytes2)
{
	return (S_OK);
}

/* =============================================================
// A2DBuffer::Restore()
// (RE) dbg:0x10040cf0; rtl:0x1001b1c0
//
// Leave the buffer unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
A2DBuffer::Restore(void)
{
	return (S_OK);
}
