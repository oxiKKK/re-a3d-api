/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * emubuffer.cpp
 *
 * Implements EMUBuffer, a voice that tracks playback without audio
 * storage or output. Play and seek operations establish timing state, and
 * cursor queries advance the position from elapsed time and playback
 * frequency.
 *
 * The buffer retains format, volume, pan, priority and source controls
 * for the resource manager. Lock returns no audio regions, and many DAL
 * and property operations are unsupported or leave outputs unchanged.
 * DAL_EMU creates and tracks these voices.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "emubuffer.h"

/* Playback tick conversion  */

/* =============================================================
// EMUBuffer::EMUBuffer()
// (RE) dbg:0x10058490; rtl:0x10022720
//
// Initialize a stopped voice without a format or control block.
// =============================================================*/

EMUBuffer::EMUBuffer(void)
{
	m_cRef         = 1;
	m_dwFlags      = 0;
	m_lVolume      = 0;
	m_lPan         = 0;

	m_dwSampleRate  = 0;
	m_dwFrequency   = 0;
	m_dwBufferBytes = 0;
	m_dwPlayCursor  = 0;

	m_fPriority    = 0;

	m_dwState      = A3DVOICE_STATE_STOPPED;

	m_dwStartTick  = 0;
	m_dwLastTick   = 0;

	m_pList        = NULL;
}

/* =============================================================
// EMUBuffer::~EMUBuffer()
// (RE) dbg:0x10058600
//
// Remove the voice from its device list.
// =============================================================*/

EMUBuffer::~EMUBuffer(void)
{
	if (m_pList != NULL)
	{
		if (m_pList->Find(this) != NULL)
			m_pList->RemoveAt(m_pList->Find(this));
	}
}

/* =============================================================
// EMUBuffer::QueryInterface()
// (RE) rtl:0x10022860; dbg:0x100586c0
//
// Obtain a referenced buffer interface. The original returns IDirectSoundBuffer
// for IID_IA3dPropertySet and never exposes IKsPropertySet.
//
// Returns:
//   S_OK
//   E_INVALIDARG   a null output
//   E_NOINTERFACE  an unknown IID
// =============================================================*/

STDMETHODIMP
EMUBuffer::QueryInterface(REFIID riid, void **ppv)
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
		*ppv = (IDirectSoundNotify *) this;
	}
	else if (IsEqualIID(riid, IID_IA3dPropertySet))
	{
		*ppv = (IDirectSoundBuffer *) this;
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
// EMUBuffer::AddRef()
// (RE) rtl:0x10022990; dbg:0x10058860
//
// Increment the reference count.
//
// Returns: Reference count read after the increment.
// =============================================================*/

STDMETHODIMP_(ULONG)
EMUBuffer::AddRef(void)
{
	InterlockedIncrement(&m_cRef);

	return ((ULONG) m_cRef);
}

/* =============================================================
// EMUBuffer::Release()
// (RE) rtl:0x100229b0; dbg:0x10058890
//
// Release a reference and delete the buffer at zero.
// The original rereads the count after the interlocked decrement.
//
// Returns: Remaining reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
EMUBuffer::Release(void)
{
	InterlockedDecrement(&m_cRef);

	if (m_cRef != 0)
		return ((ULONG) m_cRef);

	delete this;

	return (0);
}

/* =============================================================
// EMUBuffer::GetCaps()
// (RE) rtl:0x100034f0; dbg:0x10058900
//
// Leave capability outputs unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
EMUBuffer::GetCaps(LPDSBCAPS lpDSBufferCaps)
{
	ASSERT(lpDSBufferCaps != 0 &&
	       !IsBadReadPtr(lpDSBufferCaps, sizeof(DSBCAPS)));

	return (S_OK);
}

/* =============================================================
// EMUBuffer::GetCurrentPosition()
// (RE) rtl:0x10022ac0; dbg:0x10058970
//
// Advance playback using elapsed time and write identical play and write cursors.
// The original tick-wrap calculation loses 1 millisecond.
// Retail keeps the rate product in x87; arithmetic precision parity is unresolved.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
EMUBuffer::GetCurrentPosition(LPDWORD pdwCurrentPlayCursor,
			      LPDWORD pdwCurrentWriteCursor)
{
	DWORD	dwTick;
	DWORD	dwElapsed;
	FLOAT	fRate;
	int	cbMoved;

	dwTick = GetTickCount();

	if (dwTick >= m_dwLastTick)
		dwElapsed = dwTick - m_dwLastTick;
	else
		dwElapsed = dwTick - 1 - m_dwLastTick;

	if (m_dwState == A3DVOICE_STATE_PLAYING)
	{
		fRate   = (FLOAT) ((double) m_dwFrequency *
				   m_A3dCtrlSuper.fFreqFactor);
		cbMoved = (int) ((double) dwElapsed / 1000.0 * fRate);

		if (m_wfx.wBitsPerSample == 16)
			cbMoved *= 2;

		if (m_wfx.nChannels == 2)
			cbMoved *= 2;
	}
	else
	{
		cbMoved = 0;
	}

	if ((m_dwFlags & A3DVOICE_FLAG_LOOPING) ||
	    (DWORD) (cbMoved + m_dwPlayCursor) < m_desc.dwBufferBytes)
	{
		m_dwPlayCursor = (DWORD) (cbMoved + m_dwPlayCursor) % m_desc.dwBufferBytes;
	}
	else
	{
		m_dwPlayCursor = m_desc.dwBufferBytes;
		m_dwState      = A3DVOICE_STATE_STOPPED;
	}

	m_dwLastTick = dwTick;

	*pdwCurrentPlayCursor  = m_dwPlayCursor;
	*pdwCurrentWriteCursor = m_dwPlayCursor;

	return (S_OK);
}

/* =============================================================
// EMUBuffer::GetFormat()
// (RE) rtl:0x10022b80; dbg:0x10058af0
//
// Copy the stored format. The original ignores the allocated size and leaves
// the size-written output unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
EMUBuffer::GetFormat(LPWAVEFORMATEX lpwfxFormat, DWORD dwSizeAllocated,
		     LPDWORD lpdwSizeWritten)
{
	ASSERT(lpwfxFormat != 0 &&
	       !IsBadReadPtr(lpwfxFormat, sizeof(WAVEFORMATEX)));
	ASSERT(lpdwSizeWritten != 0 &&
	       !IsBadReadPtr(lpdwSizeWritten, sizeof(DWORD)));

	*lpwfxFormat = m_wfx;

	return (S_OK);
}

/* =============================================================
// EMUBuffer::GetVolume()
// (RE) rtl:0x1001af00; dbg:0x10058bf0
//
// Write the stored volume.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null output
// =============================================================*/

STDMETHODIMP
EMUBuffer::GetVolume(LPLONG lplVolume)
{
	ASSERT(lplVolume != 0 && !IsBadReadPtr(lplVolume, sizeof(LONG)));

	if (lplVolume == NULL)
		return (E_INVALIDARG);

	*lplVolume = m_lVolume;

	return (S_OK);
}

/* =============================================================
// EMUBuffer::GetPan()
// (RE) rtl:0x1001af30; dbg:0x10058c80
//
// Write the stored pan.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null output
// =============================================================*/

STDMETHODIMP
EMUBuffer::GetPan(LPLONG lplPan)
{
	ASSERT(lplPan != 0 && !IsBadReadPtr(lplPan, sizeof(LONG)));

	if (lplPan == NULL)
		return (E_INVALIDARG);

	*lplPan = m_lPan;

	return (S_OK);
}

/* =============================================================
// EMUBuffer::GetFrequency()
// (RE) rtl:0x1001af60; dbg:0x10058d10
//
// Write the current playback frequency.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
EMUBuffer::GetFrequency(LPDWORD lpdwFrequency)
{
	ASSERT(lpdwFrequency != 0 && !IsBadReadPtr(lpdwFrequency, sizeof(DWORD)));

	*lpdwFrequency = m_dwFrequency;

	return (S_OK);
}

/* =============================================================
// EMUBuffer::GetStatus()
// (RE) rtl:0x10022bb0; dbg:0x10058d90
//
// Write playback status. The original suppresses the looping status when
// any other voice flag is set.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
EMUBuffer::GetStatus(LPDWORD lpdwStatus)
{
	ASSERT(lpdwStatus != 0 && !IsBadReadPtr(lpdwStatus, sizeof(DWORD)));

	*lpdwStatus = 0;

	if (m_dwState == A3DVOICE_STATE_PLAYING)
	{
		*lpdwStatus |= DSBSTATUS_PLAYING;

		if (m_dwFlags == A3DVOICE_FLAG_LOOPING)
			*lpdwStatus |= DSBSTATUS_LOOPING;
	}

	return (S_OK);
}

/* =============================================================
// EMUBuffer::Initialize()
// (RE) rtl:0x1001b380; dbg:0x10058e40
//
// Leave the buffer unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
EMUBuffer::Initialize(LPDIRECTSOUND lpDirectSound, LPCDSBUFFERDESC lpcDSBufferDesc)
{
	ASSERT(lpDirectSound != 0 &&
	       !IsBadReadPtr(lpDirectSound, sizeof(IDirectSound)));
	ASSERT(lpcDSBufferDesc != 0 &&
	       !IsBadReadPtr(lpcDSBufferDesc, sizeof(DSBUFFERDESC)));

	return (S_OK);
}

/* =============================================================
// EMUBuffer::Lock()
// (RE) rtl:0x10022bf0; dbg:0x10058f00
//
// Clear both audio pointers and byte counts without providing storage.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
EMUBuffer::Lock(DWORD dwOffset, DWORD dwWriteBytes, LPVOID *lplpvAudioPtr1,
		LPDWORD lpdwAudioBytes1, LPVOID *lplpvAudioPtr2,
		LPDWORD lpdwAudioBytes2, DWORD dwFlags)
{
	*lplpvAudioPtr1 = NULL;
	*lpdwAudioBytes1 = 0;
	*lplpvAudioPtr2 = NULL;
	*lpdwAudioBytes2 = 0;

	return (S_OK);
}

/* =============================================================
// EMUBuffer::Play()
// (RE) rtl:0x10022c20; dbg:0x10058f50
//
// Start timed playback with the requested looping mode.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
EMUBuffer::Play(DWORD dwReserved1, DWORD dwReserved2, DWORD dwFlags)
{
	if (m_dwState == A3DVOICE_STATE_STOPPED)
	{
		m_dwLastTick = GetTickCount();

		if (m_dwPlayCursor >= m_dwBufferBytes)
			m_dwPlayCursor = 0;
	}

	m_dwState = A3DVOICE_STATE_PLAYING;

	if (dwFlags & DSBPLAY_LOOPING)
		m_dwFlags |= A3DVOICE_FLAG_LOOPING;
	else
		m_dwFlags &= ~A3DVOICE_FLAG_LOOPING;

	return (S_OK);
}

/* =============================================================
// EMUBuffer::SetCurrentPosition()
// (RE) rtl:0x10022c80; dbg:0x10058fe0
//
// Set the byte cursor and restart timing if playing.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
EMUBuffer::SetCurrentPosition(DWORD dwNewPosition)
{
	if (m_dwState == A3DVOICE_STATE_PLAYING)
		m_dwLastTick = GetTickCount();

	m_dwPlayCursor = dwNewPosition;

	return (S_OK);
}

/* =============================================================
// EMUBuffer::SetFormat()
// (RE) rtl:0x100034f0; dbg:0x10059020
//
// Leave the format unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
EMUBuffer::SetFormat(LPCWAVEFORMATEX lpcfxFormat)
{
	return (S_OK);
}

/* =============================================================
// EMUBuffer::SetVolume()
// (RE) rtl:0x1001b100; dbg:0x10059040
//
// Store the playback volume.
//
// Returns:
//   S_OK
//   E_INVALIDARG  outside DSBVOLUME_MIN through DSBVOLUME_MAX
// =============================================================*/

STDMETHODIMP
EMUBuffer::SetVolume(LONG lVolume)
{
	CHAR	OutputString[92];

	if (lVolume >= DSBVOLUME_MIN && lVolume <= DSBVOLUME_MAX)
	{
		m_lVolume = lVolume;

		return (S_OK);
	}

	DBGSTR("EMUBuffer::SetVolume() - Invalid value.  Must be between DSBVOLUME_MIN & DSBVOLUME_MAX.\n");

	return (E_INVALIDARG);
}

/* =============================================================
// EMUBuffer::SetPan()
// (RE) rtl:0x1001b130; dbg:0x100590b0
//
// Store the playback pan.
//
// Returns:
//   S_OK
//   E_INVALIDARG  outside DSBPAN_LEFT through DSBPAN_RIGHT
// =============================================================*/

STDMETHODIMP
EMUBuffer::SetPan(LONG lPan)
{
	CHAR	OutputString[96];

	if (lPan >= DSBPAN_LEFT && lPan <= DSBPAN_RIGHT)
	{
		m_lPan = lPan;

		return (S_OK);
	}

	DBGSTR("EMUBuffer::SetPan() - Invalid value passed in.  Must be between DSBPAN_LEFT & DSBPAN_RIGHT.\n");

	return (E_INVALIDARG);
}

/* =============================================================
// EMUBuffer::SetFrequency()
// (RE) rtl:0x1001b160; dbg:0x10059120
//
// Set playback frequency, using the original sample rate for zero.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
EMUBuffer::SetFrequency(DWORD dwFrequency)
{
	if (dwFrequency)
		m_dwFrequency = dwFrequency;
	else
		m_dwFrequency = m_dwSampleRate;

	return (S_OK);
}

/* =============================================================
// EMUBuffer::Stop()
// (RE) rtl:0x1001b190; dbg:0x10059160
//
// Mark the voice stopped without advancing its cursor.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
EMUBuffer::Stop(void)
{
	m_dwState = A3DVOICE_STATE_STOPPED;

	return (S_OK);
}

/* =============================================================
// EMUBuffer::Unlock()
// (RE) rtl:0x1001b1b0; dbg:0x10059190
//
// Leave the buffer unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
EMUBuffer::Unlock(LPVOID lpvAudioPtr1, DWORD dwAudioBytes1, LPVOID lpvAudioPtr2,
		  DWORD dwAudioBytes2)
{
	return (S_OK);
}

/* =============================================================
// EMUBuffer::Restore()
// (RE) rtl:0x1001b1c0; dbg:0x100591b0
//
// Leave the buffer unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
EMUBuffer::Restore(void)
{
	return (S_OK);
}

/* =============================================================
// EMUBuffer::SetA3dSuperCtrl()
// (RE) rtl:0x10022cb0; dbg:0x100591d0
//
// Copy the source control block. The original reports failure after storing it.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
EMUBuffer::SetA3dSuperCtrl(LPA3DCTRL_SRC_SUPER lpA3dCtrlSuper, DWORD dwSize)
{
	ASSERT(lpA3dCtrlSuper != 0);

	CopyMemory(&m_A3dCtrlSuper, lpA3dCtrlSuper, sizeof(m_A3dCtrlSuper));

	return (E_NOTIMPL);
}

/* =============================================================
// EMUBuffer::GetAllocationStatus()
// (RE) rtl:0x100034f0; dbg:0x10059240
//
// Leave the allocation status output unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
EMUBuffer::GetAllocationStatus(LPDWORD lpdwStatus)
{
	ASSERT(lpdwStatus != 0 && !IsBadReadPtr(lpdwStatus, sizeof(DWORD)));

	return (S_OK);
}

/* =============================================================
// EMUBuffer::GetWave()
// (RE) rtl:0x100034f0; dbg:0x100592b0
//
// Leave the audio pointer output unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
EMUBuffer::GetWave(LPBYTE *lplpbWave)
{
	ASSERT(lplpbWave != 0);

	return (S_OK);
}

/* =============================================================
// EMUBuffer::GetDriverInfo()
// (RE) rtl:0x1001b380; dbg:0x10059310
//
// Leave driver information outputs unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
EMUBuffer::GetDriverInfo(void **lplpIDsDriverBuffer, void **lplpIA3dDriverBuffer)
{
	ASSERT(lplpIDsDriverBuffer != 0);
	ASSERT(lplpIA3dDriverBuffer != 0);

	return (S_OK);
}

/* =============================================================
// EMUBuffer::SetNewBuffer()
// (RE) rtl:0x1001b380; dbg:0x100593b0
//
// Leave audio storage unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
EMUBuffer::SetNewBuffer(LPBYTE lpbWave, DWORD dwBufferBytes)
{
	ASSERT(lpbWave != 0);

	return (S_OK);
}

/* =============================================================
// EMUBuffer::SetA3dDirectCtrl()
// (RE) rtl:0x1001b380; dbg:0x10059410
//
// Leave direct controls unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
EMUBuffer::SetA3dDirectCtrl(LPA3DCTRL_SRC_SUPER lpA3dCtrlDirect, DWORD dwSize)
{
	ASSERT(lpA3dCtrlDirect != 0);

	return (S_OK);
}

/* =============================================================
// EMUBuffer::GetStatusEx()
// (RE) rtl:0x100034f0; dbg:0x10059470
//
// Leave the extended status output unchanged.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
EMUBuffer::GetStatusEx(LPDWORD lpdwStatusEx)
{
	ASSERT(lpdwStatusEx != 0 && !IsBadReadPtr(lpdwStatusEx, sizeof(DWORD)));

	return (S_OK);
}

/* =============================================================
// EMUBuffer::GetPriority()
// (RE) rtl:0x1000b6f0; dbg:0x100594e0
//
// Write the resource manager priority.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
EMUBuffer::GetPriority(FLOAT *pfPriority)
{
	CHAR	OutputString[56];

	ASSERT(pfPriority != 0 && !IsBadReadPtr(pfPriority, sizeof(FLOAT)));

	if (pfPriority != NULL)
	{
		ASSERT(m_fPriority >= 0.0 && m_fPriority <= 1.0);

		*pfPriority = m_fPriority;

		return (S_OK);
	}

	DBGSTR("EMUBuffer::GetPriority() - Invalid pointer passed in.\n");

	return (E_POINTER);
}

/* =============================================================
// EMUBuffer::GetBufferState()
// (RE) rtl:0x10022cd0; dbg:0x10059600
//
// Write the voice state.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
EMUBuffer::GetBufferState(LPDWORD lpdwBufferState)
{
	ASSERT(lpdwBufferState != 0 &&
	       !IsBadReadPtr(lpdwBufferState, sizeof(DWORD)));

	*lpdwBufferState = m_dwState;

	return (S_OK);
}

/* =============================================================
// EMUBuffer::SetPriority()
// (RE) rtl:0x1001b3d0; dbg:0x10059680
//
// Store the resource manager priority.
//
// Returns:
//   S_OK
//   E_INVALIDARG  unless priority is in [0, 1]
// =============================================================*/

STDMETHODIMP
EMUBuffer::SetPriority(FLOAT fPriority)
{
	CHAR	OutputString[60];

	ASSERT(fPriority >= 0.0 && fPriority <= 1.0);

	if (fPriority >= 0.0 && fPriority <= 1.0)
	{
		m_fPriority = fPriority;

		return (S_OK);
	}

	DBGSTR("EMUBuffer::SetPriority() - Invalid priority value sent.\n");

	return (E_INVALIDARG);
}

/* =============================================================
// EMUBuffer::Enable()
// (RE) rtl:0x1001b410; dbg:0x10059760
//
// Set the voice enable flag.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
EMUBuffer::Enable(void)
{
	m_dwFlags |= A3DVOICE_FLAG_ENABLED;

	return (S_OK);
}

/* =============================================================
// EMUBuffer::Disable()
// (RE) rtl:0x10022cf0; dbg:0x10059790
//
// Clear the voice enable flag.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
EMUBuffer::Disable(void)
{
	m_dwFlags &= ~A3DVOICE_FLAG_ENABLED;

	return (S_OK);
}

/* =============================================================
// EMUBuffer::SetNativeModeDisabled()
// (RE) rtl:0x10020b90; dbg:0x100597c0
//
// Reject the native-mode-disable request
//
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
EMUBuffer::SetNativeModeDisabled(DWORD)
{
	return (E_NOTIMPL);
}

/* =============================================================
// EMUBuffer::Unknown_0x28()
// (RE) rtl:0x10020b90; dbg:0x100597e0
//
// Leave buffer state unchanged; the operation is unresolved.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
EMUBuffer::Unknown_0x28(DWORD)
{
	return (E_NOTIMPL);
}

/* =============================================================
// EMUBuffer::GetControlBufferPair()
// (RE) rtl:0x10020b90; dbg:0x10059800
//
// Reject the control-buffer-pair query
//
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
EMUBuffer::GetControlBufferPair(DWORD)
{
	return (E_NOTIMPL);
}

/* =============================================================
// EMUBuffer::SetNotificationPositions()
// (RE) rtl:0x10003500; dbg:0x10059820
//
// Leave notification positions unchanged.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
EMUBuffer::SetNotificationPositions(DWORD cPositions,
				    LPCDSBPOSITIONNOTIFY pcPositionNotifies)
{
	return (E_NOTIMPL);
}

/* =============================================================
// EMUBuffer::Get()
// (RE) dbg:0x10059940
//
// Leave property outputs unchanged.
//
// Returns: 0x8004004C.
// =============================================================*/

STDMETHODIMP
EMUBuffer::Get(REFGUID rguidPropSet, ULONG ulId, LPVOID pInstanceData,
	       ULONG ulInstanceLength, LPVOID pPropertyData, ULONG ulDataLength,
	       PULONG pulBytesReturned)
{
	return (A3DERROR_BUFFER_IN_SOFTWARE);
}

/* =============================================================
// EMUBuffer::Set()
// (RE) dbg:0x10059960
//
// Leave property state unchanged.
//
// Returns: 0x8004004C.
// =============================================================*/

STDMETHODIMP
EMUBuffer::Set(REFGUID rguidPropSet, ULONG ulId, LPVOID pInstanceData,
	       ULONG ulInstanceLength, LPVOID pPropertyData, ULONG ulDataLength)
{
	return (A3DERROR_BUFFER_IN_SOFTWARE);
}

/* =============================================================
// EMUBuffer::QuerySupport()
// (RE) dbg:0x10059920
//
// Leave the property support output unchanged.
//
// Returns: 0x8004004C.
// =============================================================*/

STDMETHODIMP
EMUBuffer::QuerySupport(REFGUID rguidPropSet, ULONG ulId, PULONG pulTypeSupport)
{
	return (A3DERROR_BUFFER_IN_SOFTWARE);
}

/* =============================================================
// EMUBuffer::Init()
// (RE) rtl:0x10022d10; dbg:0x10059840
//
// Initialize timing, format, buffer size and device list membership.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
EMUBuffer::Init(const DSBUFFERDESC1 *pDesc, CList *pList)
{
	m_dwStartTick = GetTickCount();
	m_dwLastTick  = m_dwStartTick;

	CopyMemory(&m_desc, pDesc, sizeof(m_desc));

	m_wfx                   = *pDesc->lpwfxFormat;
	m_desc.lpwfxFormat      = &m_wfx;

	m_dwSampleRate  = pDesc->lpwfxFormat->nSamplesPerSec;
	m_dwFrequency   = m_dwSampleRate;

	m_dwBufferBytes = pDesc->dwBufferBytes;

	m_pList = pList;

	return (S_OK);
}
