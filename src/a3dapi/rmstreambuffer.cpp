/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * rmstreambuffer.cpp
 *
 * Implements resource-manager buffers whose waveform storage is separate
 * from the assigned DAL playback buffer. Playback and seeks update source
 * state, while service callbacks copy queued audio into the current
 * binding and deliver notifications.
 *
 * The buffer stores priority and audibility information used for voice
 * allocation and can retain waveform data across binding changes or
 * duplication. ResManBuffer supplies shared controls and property
 * synchronization; DalBufferInfo performs device-format conversion and
 * refilling.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "rmstreambuffer.h"
#include "dalinfo.h"
#include "resman.h"
#include "rmstatbuffer.h"
#include "Waveform.h"
#include "PropertySetItem.h"

#include <new>
#include <stdio.h>
#include <string.h>

/* Fixed playback ceiling  */
#define RESMANSTREAMBUFFER_MAX_FREQUENCY 100000

/* Fill-pass counter saturation  */
#define RESMANSTREAMBUFFER_MAX_REPEAT_COUNT ((DWORD) -1)

/* =============================================================
// ResManStreamBuffer::ResManStreamBuffer()
// (RE) dbg:0x100703f0
//
// Initialize a stopped streaming buffer for the resource manager.
// =============================================================*/

ResManStreamBuffer::ResManStreamBuffer(ResMan *pResMan, int nResourceManagerMode)
{
	m_lpResMan              = pResMan;
	m_cRef                  = 1;
	m_pDalBufferInfo        = NULL;
	m_lpA3dCtrlSuper        = NULL;
	m_dwA3dCtrlSuperSize    = sizeof(m_aCtrlBuffers[0]);
	m_lpA3dCtrlSuperPending = NULL;
	m_nResourceManagerMode  = nResourceManagerMode;

	m_pWaveForm      = NULL;

	m_lVolume               = 0;
	m_lPan                  = 0;
	m_dwOriginalSampleRate  = 0;
	m_dwFrequency           = 100;
	m_dwPosition            = 0;
	m_dwTargetPosition      = RESMANSTREAMBUFFER_NO_PENDING_SEEK;
	m_dwState               = A3DVOICE_STATE_STOPPED;
	m_dwFlags               = 0;
	m_cNotify               = 0;
	m_paNotify              = NULL;
	m_hNotifyMutex          = NULL;
	m_dwCurrentPos          = 0;
	m_fPriority             = 0.0f;
	m_dwRenderMode          = RESMANBUFFER_RENDER_3D;
	m_fSortKey              = 0.0f;
	m_dwRepeatCount         = 0;
	m_dwRepeatGate          = 0;
	m_dwFillPos             = 0;
	m_dwFillPosCopy         = 0;
	m_dwHardwareBound       = 0;
}

/* =============================================================
// ResManStreamBuffer::GetPriority()
// (RE) dbg:0x10071ff0
//
// Read the stored buffer priority.
//
// Returns:
//   S_OK
//   E_POINTER  if the output pointer is null
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::GetPriority(FLOAT *pfPriority)
{
	ASSERT(pfPriority != 0 && !IsBadReadPtr(pfPriority, sizeof(FLOAT)));

	if (pfPriority)
	{
		ASSERT(m_fPriority >= 0.0 && m_fPriority <= 1.0);

		*pfPriority = m_fPriority;

		return (S_OK);
	}
	else
	{
		DBGSTR("IResManStreamBuffer::GetPriority() - Invalid pointer passed in.\n");

		return (E_POINTER);
	}
}

/* =============================================================
// ResManStreamBuffer::SetPriority()
// (RE) dbg:0x10072280
//
// Store the buffer priority.
//
// Returns:
//   S_OK
//   E_INVALIDARG  if priority is outside [0,1]
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::SetPriority(FLOAT fPriority)
{
	ASSERT(fPriority >= 0.0 && fPriority <= 1.0);

	if (fPriority >= 0.0 && fPriority <= 1.0)
	{
		m_fPriority = fPriority;

		return (S_OK);
	}
	else
	{
		DBGSTR("IResManStreamBuffer::SetPriority() - Invalid priority value sent.\n");

		return (E_INVALIDARG);
	}
}

/* =============================================================
// ResManStreamBuffer::SetCurrentPosition()
// (RE) dbg:0x100717f0
//
// Defer a seek to the requested byte position.
// The original aligns by shifting nBlockAlign - 1 bits.
//
// Returns:
//   S_OK
//   E_INVALIDARG  if the position exceeds the buffer size
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::SetCurrentPosition(DWORD dwPlay)
{
	if (dwPlay <= m_DSBufferDesc.dwBufferBytes)
	{
		if (m_DSBufferDesc.lpwfxFormat->nBlockAlign)
			dwPlay = dwPlay >> (m_wfxFormat.nBlockAlign - 1)
					<< (m_wfxFormat.nBlockAlign - 1);

		m_dwTargetPosition = dwPlay;

		ASSERT(m_dwTargetPosition <= m_DSBufferDesc.dwBufferBytes);

		return (S_OK);
	}
	else
	{
		DBGSTR("ResManStreamBuffer::SetCurrentPosition() - Requested a position not within the buffer.\n");

		return (E_INVALIDARG);
	}
}

/* =============================================================
// ResManStreamBuffer::Lock()
// (RE) dbg:0x10071230
//
// Return waveform storage for a write, splitting the span at the buffer end.
//
// Returns:
//   S_OK
//   E_INVALIDARG  missing first-span outputs or a requested byte count greater
//                 than the buffer size
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::Lock(DWORD dwWriteCursor, DWORD dwWriteBytes,
			 LPVOID *lplpvAudioPtr1, LPDWORD lpdwAudioBytes1,
			 LPVOID *ppvAudioPtr2, LPDWORD pdwAudioBytes2,
			 DWORD dwFlags)
{
	DWORD	dwBytes;

	ASSERT(lplpvAudioPtr1 != 0);
	ASSERT(lpdwAudioBytes1 != 0 &&
	       !IsBadReadPtr(lpdwAudioBytes1, sizeof(DWORD)));
	ASSERT(dwWriteBytes <= m_DSBufferDesc.dwBufferBytes);
	ASSERT(m_dwPosition <= m_DSBufferDesc.dwBufferBytes);

	if (lplpvAudioPtr1)
	{
		if (lpdwAudioBytes1)
		{
			if (dwWriteBytes <= m_DSBufferDesc.dwBufferBytes)
			{
				dwBytes = dwWriteBytes;

				if (dwFlags & DSBLOCK_ENTIREBUFFER)
					dwBytes = m_DSBufferDesc.dwBufferBytes;

				if (dwFlags & DSBLOCK_FROMWRITECURSOR)
					dwWriteCursor = m_dwPosition;

				if (dwWriteCursor + dwBytes > m_DSBufferDesc.dwBufferBytes)
				{
					ASSERT(m_pWaveForm != 0 &&
					       !IsBadReadPtr(m_pWaveForm, sizeof(CWaveForm)));

					*lplpvAudioPtr1 = m_pWaveForm->GetBuffer() + dwWriteCursor;
					*lpdwAudioBytes1 = m_DSBufferDesc.dwBufferBytes - dwWriteCursor;

					if (ppvAudioPtr2 && pdwAudioBytes2)
					{
						*ppvAudioPtr2 = m_pWaveForm->GetBuffer();
						*pdwAudioBytes2 = dwBytes - *lpdwAudioBytes1;
					}
				}
				else
				{
					ASSERT(m_pWaveForm != 0 &&
					       !IsBadReadPtr(m_pWaveForm, sizeof(CWaveForm)));

					*lplpvAudioPtr1 = m_pWaveForm->GetBuffer() + dwWriteCursor;
					*lpdwAudioBytes1 = dwBytes;

					if (ppvAudioPtr2 && pdwAudioBytes2)
					{
						*ppvAudioPtr2 = NULL;
						*pdwAudioBytes2 = 0;
					}
				}

				return (S_OK);
			}
			else
			{
				DBGSTR("ResManStreamBuffer::Lock() - Requested write bytes greater than the size of the actual buffer.\n");

				return (E_INVALIDARG);
			}
		}
		else
		{
			DBGSTR("ResManStreamBuffer::Lock() - lpdwAudioBytes1 is NULL.\n");

			return (E_INVALIDARG);
		}
	}
	else
	{
		DBGSTR("ResManStreamBuffer::Lock() - lplpvAudioPtr1 is NULL.\n");

		return (E_INVALIDARG);
	}
}

/* =============================================================
// ResManStreamBuffer::Play()
// (RE) rtl:0x1002aa20; dbg:0x10071640
//
// Mark the stream playing, set looping and wake the service thread.
//
// Returns:
//   S_OK
//   E_INVALIDARG  nonzero reserved arguments
//   E_FAIL        if the W95 voice-count check rejects playback
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::Play(DWORD dwReserved1, DWORD dwPriority, DWORD dwFlags)
{
	DWORD	dwEntries;
	DWORD	dwPlaying;

	if (dwReserved1 || dwPriority)
	{
		DBGSTR("ResManStreamBuffer::Play() - Values other than zero passed in the dwReserved parameters.\n");

		return (E_INVALIDARG);
	}

	ASSERT(m_dwState >= 1 && m_dwState <= 3);

	if (m_dwState != A3DVOICE_STATE_PLAYING && m_nResourceManagerMode == A3D_RESOURCE_MODE_NOTIFY)
	{
		dwPlaying = 0;
		dwEntries = m_lpResMan->CountEntriesW95();

		GetTickCount();

		if (dwEntries)
			dwPlaying = m_lpResMan->CountPlayingOnW95();

		if (!(dwPlaying >= dwEntries ||
		      dwPlaying <= m_lpResMan->FreeVoices() + dwEntries))
			return (E_FAIL);
	}

	m_dwState = A3DVOICE_STATE_PLAYING;

	if (dwFlags & DSBPLAY_LOOPING)
		m_dwFlags |= A3DVOICE_FLAG_LOOPING;
	else
		m_dwFlags &= ~A3DVOICE_FLAG_LOOPING;

	m_lpResMan->ForceHeavyPass();

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::Stop()
// (RE) dbg:0x10071bb0
//
// Signal stop notifications, mark the stream stopped and reset its repeat
// count.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::Stop(void)
{
DWORD	i;

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

	m_dwState       = A3DVOICE_STATE_STOPPED;
	m_dwRepeatCount = 0;

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::Init()
// (RE) dbg:0x100726f0; rtl:0x1002ae50
//
// Copy the buffer description and format, allocate waveform storage and select
// the initial render mode.
//
// Returns: S_OK; E_POINTER for a null description; E_OUTOFMEMORY if waveform
//          allocation fails. The render-mode result is ignored.
// =============================================================*/

HRESULT
ResManStreamBuffer::Init(const DSBUFFERDESC1 *lpDSBdesc)
{
const WAVEFORMATEX	*pwfx;

	ASSERT(lpDSBdesc != 0 &&
	       !IsBadReadPtr(lpDSBdesc, sizeof(DSBUFFERDESC1)));

	if (!lpDSBdesc)
	{
		DBGSTR("ResManStreamBuffer::Init() - NULL pointer passed in for lpDSBdesc.\n");

		return (E_POINTER);
	}

	m_DSBufferDesc = *lpDSBdesc;

	pwfx = lpDSBdesc->lpwfxFormat;

	m_wfxFormat                     = *pwfx;
	m_DSBufferDesc.lpwfxFormat      = &m_wfxFormat;

	m_dwOriginalSampleRate  = pwfx->nSamplesPerSec;
	m_dwFrequency           = pwfx->nSamplesPerSec;

	ASSERT(m_pWaveForm == 0);

	m_pWaveForm = new CWaveForm();

	if (!m_pWaveForm)
	{
		DBGSTR("ResManStreamBuffer::Init() - Could not allocate CWaveForm.\n");

		return (E_OUTOFMEMORY);
	}

	if (!m_pWaveForm->Create(m_DSBufferDesc.dwBufferBytes))
	{
		DBGSTR("ResManStreamBuffer::Init() - Failed to allocated Wave Buffer.\n");

		if (m_pWaveForm)
		{
			m_pWaveForm->Release();

			m_pWaveForm = NULL;
		}

		return (E_OUTOFMEMORY);
	}

	if (m_wfxFormat.nChannels == 2)
		m_dwFlags |= A3DVOICE_FLAG_STEREO;

	if (m_wfxFormat.wBitsPerSample == 16)
		m_dwFlags |= A3DVOICE_FLAG_16BIT;

	if (lpDSBdesc->dwFlags & DSBCAPS_CTRL3D)
		SetRenderMode(RESMANBUFFER_RENDER_3D);
	else
		SetRenderMode(0);

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::SetRenderMode()
// (RE) dbg:0x100735e0; rtl:0x1002b400
//
// Store a supported requested mode, or choose a default for the channel count.
//
// Returns:
//   S_OK
//   E_INVALIDARG  if a nonzero requested mode has no supported bits
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::SetRenderMode(DWORD dwRequested)
{
	if (dwRequested)
	{
		if ((m_lpResMan->m_dwDalTypeMask & dwRequested) == 0)
			return (E_INVALIDARG);
	}
	else
	{
		dwRequested = RESMANBUFFER_RENDER_DEFAULT;

		if (m_wfxFormat.nChannels == 4 &&
		    (m_lpResMan->m_dwDalTypeMask & RESMANBUFFER_RENDER_FOUR_CHANNEL))
			dwRequested = RESMANBUFFER_RENDER_FOUR_CHANNEL;
	}

	m_dwRenderMode = dwRequested;

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::GetBufferState()
// (RE) dbg:0x10072120; rtl:0x1002ac90
//
// Read the stored voice state.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::GetBufferState(LPDWORD lpdwBufferState)
{
	ASSERT(lpdwBufferState != 0 &&
	       !IsBadReadPtr(lpdwBufferState, sizeof(DWORD)));
	ASSERT(m_dwState >= 0x00000001 && m_dwState <= 0x00000003);

	*lpdwBufferState = m_dwState;

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::GetRenderMode()
// (RE) dbg:0x10073680; rtl:0x1002b460
//
// Read the stored render mode.
//
// Returns:
//   S_OK
//   E_INVALIDARG  if the output pointer is null
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::GetRenderMode(LPDWORD lpdwRenderMode)
{
	if (!lpdwRenderMode)
		return (E_INVALIDARG);

	*lpdwRenderMode = m_dwRenderMode;

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::GetDalBufferInfo()
// (RE) dbg:0x10072a90; rtl:0x10029fa0
//
// Return the current DAL buffer binding.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
ResManStreamBuffer::GetDalBufferInfo(DalBufferInfo **lplpDalBufferInfo)
{
	ASSERT(lplpDalBufferInfo != 0 &&
	       !IsBadReadPtr(lplpDalBufferInfo, sizeof(DalBufferInfo *)));

	*lplpDalBufferInfo = m_pDalBufferInfo;

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::Get()
// (RE) dbg:0x10073a50
//
// Queue a property read and wait for its result. Original allocation or enqueue
// failure can leave a null result position; returned data can exceed caller
// capacity.
//
// Returns: Recorded HRESULT; A3DERROR_BUFFER_IN_SOFTWARE without hardware or if
//          the completion wait fails; E_FAIL if the result-mutex wait fails.
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::Get(REFGUID rguidPropSet, ULONG ulId,
			LPVOID pInstanceData, ULONG cbInstanceData,
			LPVOID pPropertyData, ULONG cbPropertyData,
			PULONG pulBytesReturned)
{
POSITION                pos;
CPropertySetItem       *pItem;
CPropertySetItem       *pProcessedItem;
HRESULT                 hr;

	if (!m_dwHardwareBound)
		return (A3DERROR_BUFFER_IN_SOFTWARE);

	hr  = E_FAIL;
	pos = NULL;

	pItem = new CPropertySetItem();

	if (pItem)
	{
		if (WaitForSingleObject(m_hPropSetMutex, INFINITE) == WAIT_OBJECT_0)
		{
			pItem->Get(rguidPropSet, ulId, pInstanceData, cbInstanceData,
				   pPropertyData, cbPropertyData, pulBytesReturned);

			pos = m_listPropSetItems.AddTail(pItem);
		}

		VERIFY(ReleaseMutex(m_hPropSetMutex));
	}

	if (WaitForSingleObject(m_hPropSetCacheFlushed, INFINITE) != WAIT_OBJECT_0)
		return (A3DERROR_BUFFER_IN_SOFTWARE);

	if (WaitForSingleObject(m_hPropSetMutex, INFINITE) == WAIT_OBJECT_0)
	{
		pProcessedItem = (CPropertySetItem *) m_listPropSetItems.GetAt(pos);

		ASSERT(pProcessedItem != 0 &&
		       !IsBadReadPtr(pProcessedItem, sizeof(CPropertySetItem)));

		memcpy(pInstanceData, pProcessedItem->m_pInstanceData, cbInstanceData);
		memcpy(pPropertyData, pProcessedItem->m_pPropertyData,
		       pProcessedItem->m_cbBytesReturned);

		*pulBytesReturned = pProcessedItem->m_cbBytesReturned;
		hr = pProcessedItem->m_hrCall;

		m_listPropSetItems.RemoveAt(pos);

		delete pProcessedItem;
	}

	VERIFY(ReleaseMutex(m_hPropSetMutex));
	VERIFY(ResetEvent(m_hPropSetCacheFlushed));

	return (hr);
}

/* =============================================================
// ResManStreamBuffer::Set()
// (RE) dbg:0x10073de0
//
// Cache a property write, replacing matches unless A3DPROPSET_APPENDTOCACHE is
// set. On hardware, A3DPROPSET_WAITFORRESULTS waits and copies the result data
// back. Original append-and-wait, allocation or enqueue failures can leave a
// null result position.
//
// Returns: E_INVALIDARG for GUID_NULL; E_POINTER for missing nonempty data;
//          A3DOK_BUFFER_IN_SOFTWARE without hardware; S_OK without waiting;
//          otherwise the recorded HRESULT or E_FAIL if a result wait fails.
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::Set(REFGUID rguidPropSet, ULONG ulId,
			LPVOID pInstanceData, ULONG cbInstanceData,
			LPVOID pPropertyData, ULONG cbPropertyData,
			DWORD dwFlags)
{
POSITION                pos;
POSITION                posCurrent;
POSITION                posProcessed;
CPropertySetItem       *pItem;
CPropertySetItem       *pItemID;
CPropertySetItem       *pProcessedItem;
BOOL                    bReplaced;
HRESULT                 hr;

	if (A3dIsGuidEqual(&rguidPropSet, &GUID_NULL))
	{
		DBGSTR("ResManBuffer::SetPropertySet() - NULL GUID passed in.\n");

		return (E_INVALIDARG);
	}

	if (cbInstanceData && !pInstanceData)
	{
		DBGSTR("ResManBuffer::SetPropertySet() - Instance Data is NULL and size is not zero.\n");

		return (E_POINTER);
	}

	if (cbPropertyData && !pPropertyData)
	{
		DBGSTR("ResManBuffer::SetPropertySet() - Property Data is NULL and size is not zero.\n");

		return (E_POINTER);
	}

	posProcessed = NULL;

	pItem = new CPropertySetItem();

	if (pItem)
	{
		if (WaitForSingleObject(m_hPropSetMutex, INFINITE) == WAIT_OBJECT_0)
		{
			pItem->AddSet(rguidPropSet, ulId, pInstanceData,
				      cbInstanceData, pPropertyData, cbPropertyData,
				      1, 1);

			if (dwFlags & A3DPROPSET_APPENDTOCACHE)
			{
				m_listPropSetItems.AddTail(pItem);
			}
			else
			{
				bReplaced = FALSE;

				pos = m_listPropSetItems.GetHeadPosition();

				while (pos)
				{
					posCurrent = pos;

					pItemID = (CPropertySetItem *)
						m_listPropSetItems.GetNext(pos);

					ASSERT(pItemID != 0 &&
					       !IsBadReadPtr(pItemID, sizeof(CPropertySetItem)));

					if (pItemID->m_ulId == ulId &&
					    A3dIsGuidEqual(&rguidPropSet,
							       &pItemID->m_guidPropertySet))
					{
						m_listPropSetItems.GetAt(posCurrent) = pItem;

						posProcessed = posCurrent;
						bReplaced    = TRUE;

						delete pItemID;
					}
				}

				if (!bReplaced)
					posProcessed = m_listPropSetItems.AddTail(pItem);
			}
		}

		VERIFY(ReleaseMutex(m_hPropSetMutex));
	}

	if (!m_dwHardwareBound)
		return (A3DOK_BUFFER_IN_SOFTWARE);

	if (!(dwFlags & A3DPROPSET_WAITFORRESULTS))
		return (S_OK);

	if (WaitForSingleObject(m_hPropSetCacheFlushed, INFINITE))
	{
		DBGSTR("ResManStreamBuffer::SetPropertySet() - TimerCallback failed to signal Property Set event.\n");

		ASSERT(0);

		return (E_FAIL);
	}

	hr = E_FAIL;

	if (WaitForSingleObject(m_hPropSetMutex, INFINITE) == WAIT_OBJECT_0)
	{
		pProcessedItem = (CPropertySetItem *) m_listPropSetItems.GetAt(posProcessed);

		ASSERT(pProcessedItem != 0 &&
		       !IsBadReadPtr(pProcessedItem, sizeof(CPropertySetItem)));

		memcpy(pInstanceData, pProcessedItem->m_pInstanceData, cbInstanceData);
		memcpy(pPropertyData, pProcessedItem->m_pPropertyData, cbPropertyData);

		hr = pProcessedItem->m_hrCall;
	}

	VERIFY(ReleaseMutex(m_hPropSetMutex));
	VERIFY(ResetEvent(m_hPropSetCacheFlushed));

	return (hr);
}

/* =============================================================
// ResManStreamBuffer::GetFillPosition()
// (RE) dbg:0x10070b30
//
// Return the pending seek or source position adjusted for queued DAL bytes,
// with an optional copy in the second output.
//
// Returns: S_OK; the DAL query result is ignored.
// =============================================================*/

HRESULT
ResManStreamBuffer::GetFillPosition(DWORD *pdwFillPos, DWORD *pdwFillPosCopy)
{
DWORD	dwSourceBytesQueued;

	if (m_dwTargetPosition == RESMANSTREAMBUFFER_NO_PENDING_SEEK)
	{
		if (m_pDalBufferInfo)
		{
			m_pDalBufferInfo->GetSourceBytesQueued(&dwSourceBytesQueued,
							       m_dwFlags);

			if (dwSourceBytesQueued > m_dwPosition)
				*pdwFillPos = m_DSBufferDesc.dwBufferBytes -
					      (dwSourceBytesQueued - m_dwPosition) %
					      m_DSBufferDesc.dwBufferBytes;
			else
				*pdwFillPos = m_dwPosition - dwSourceBytesQueued;
		}
		else
		{
			*pdwFillPos = m_dwPosition;
		}
	}
	else if (pdwFillPos)
	{
		*pdwFillPos = (DWORD) m_dwTargetPosition;
	}

	if (pdwFillPosCopy)
		*pdwFillPosCopy = *pdwFillPos;

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::Tick()
// (RE) dbg:0x10072eb0
//
// Apply pending seeks, fill the DAL buffer and signal playback notifications.
// The service-thread argument is cast to the unused first FillFromSource
// argument.
//
// Returns: S_OK; E_ABORT if no DAL buffer is attached. DAL errors are ignored.
// =============================================================*/

HRESULT
ResManStreamBuffer::Tick(int nWhen)
{
DWORD	dwStatus;
DWORD	dwBytesToFill;
DWORD	dwPosition;
DWORD	dwSpanStart;
DWORD	dwSpanEnd;
BOOL	fSeeked;
DWORD	i;

	if (!m_pDalBufferInfo)
		return (E_ABORT);

	if (m_dwTargetPosition == RESMANSTREAMBUFFER_NO_PENDING_SEEK)
	{
		fSeeked = FALSE;
	}
	else
	{
		ASSERT(m_pDalBufferInfo->GetDSBuffer() != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo->GetDSBuffer(),
				     sizeof(IDirectSoundBuffer)));

		m_pDalBufferInfo->GetDSBuffer()->Stop();

		m_dwPosition       = (DWORD) m_dwTargetPosition;
		m_dwCurrentPos     = (DWORD) m_dwTargetPosition;
		m_dwTargetPosition = RESMANSTREAMBUFFER_NO_PENDING_SEEK;

		fSeeked = TRUE;
	}

	ASSERT(m_pDalBufferInfo->GetDSBuffer() != 0 &&
	       !IsBadReadPtr(m_pDalBufferInfo->GetDSBuffer(),
			     sizeof(IDirectSoundBuffer)));

	m_pDalBufferInfo->GetDSBuffer()->GetStatus(&dwStatus);

	if (fSeeked || !(dwStatus & DSBSTATUS_PLAYING))
		m_pDalBufferInfo->SilenceAndRewind();

	GetFillPosition(&m_dwFillPos, &m_dwFillPosCopy);

	if (m_pDalBufferInfo->Refresh(&dwBytesToFill, NULL))
	{
		ASSERT(m_pWaveForm != 0 &&
		       !IsBadReadPtr(m_pWaveForm, sizeof(CWaveForm)));

		dwPosition = m_dwPosition;

		m_pDalBufferInfo->FillFromSource((void *) nWhen,
						     m_pWaveForm->GetBuffer(),
						     m_DSBufferDesc.dwBufferBytes,
						     &dwPosition, m_dwFlags);

		m_dwPosition = dwPosition;

		if (m_dwRepeatGate)
		{
			if (m_dwRepeatCount != RESMANSTREAMBUFFER_MAX_REPEAT_COUNT)
				m_dwRepeatCount++;
		}
		else
		{
			m_dwRepeatCount = 0;
		}

		if (!(m_dwFlags & A3DVOICE_FLAG_LOOPING) &&
		    m_dwFillPos >= m_DSBufferDesc.dwBufferBytes)
		{
			Stop();

			SetCurrentPosition(0);

			m_dwFillPos     = 0;
			m_dwFillPosCopy = 0;
		}

		if (m_dwState == A3DVOICE_STATE_PLAYING &&
		    (fSeeked || !(dwStatus & DSBSTATUS_PLAYING)))
		{
			m_pDalBufferInfo->GetDSBuffer()->Play(0, 0, DSBPLAY_LOOPING);
		}
	}

	if (m_cNotify)
	{
		WaitForSingleObject(m_hNotifyMutex, INFINITE);

		dwSpanStart = m_dwCurrentPos;
		dwSpanEnd   = m_dwFillPos;

		if (dwSpanStart > dwSpanEnd)
		{
			for (i = 0; i < m_cNotify; i++)
			{
				if (m_paNotify[i].dwOffset >= dwSpanStart &&
				    m_paNotify[i].dwOffset < m_DSBufferDesc.dwBufferBytes)
					SetEvent(m_paNotify[i].hEventNotify);
			}

			for (i = 0; i < m_cNotify; i++)
			{
				if (m_paNotify[i].dwOffset < dwSpanEnd)
					SetEvent(m_paNotify[i].hEventNotify);
			}
		}
		else
		{
			for (i = 0; i < m_cNotify; i++)
			{
				if (m_paNotify[i].dwOffset >= dwSpanStart &&
				    m_paNotify[i].dwOffset < dwSpanEnd)
					SetEvent(m_paNotify[i].hEventNotify);
			}
		}

		m_dwCurrentPos = dwSpanEnd;

		VERIFY(ReleaseMutex(m_hNotifyMutex));
	}

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::RecomputeSortKey()
// (RE) dbg:0x10072360; thunk dbg:0x10001e42
//
// Blend priority with source and enabled-reflection audibility.
// The reference leaves this in the return register; callers ignore it.
// =============================================================*/

void
ResManStreamBuffer::RecomputeSortKey(void)
{
A3DCTRL_SRC_SUPER      *pSuper;
A3DVAL                  fWeight;
FLOAT                   fMaxAudibility;
int                     i;

	fMaxAudibility = 0.0f;

	pSuper = m_lpA3dCtrlSuper;

	if (pSuper != NULL)
	{
		fMaxAudibility = pSuper->fAudibility;

		for (i = 0; i < A3D_MAX_SOURCE_REFLECTIONS; i++)
		{
			if (pSuper->Reflections[i].bEnable == 1 &&
			    pSuper->Reflections[i].fAudibility >= fMaxAudibility)
				fMaxAudibility = pSuper->Reflections[i].fAudibility;
		}
	}

	fWeight = 0.0f;

	((IA3dPrv4 *) m_lpResMan)->GetPriorityWeight(&fWeight);

	m_fSortKey = fWeight * m_fPriority + (1.0f - fWeight) * fMaxAudibility;
}

/* =============================================================
// ResManStreamBuffer::SetRepeatGate()
// (RE) dbg:0x10065d70; thunk dbg:0x10001cda
//
// Set the repeat gate and clear the repeat count when disabling it.
// =============================================================*/

void
ResManStreamBuffer::SetRepeatGate(DWORD dwGate)
{
	m_dwRepeatGate = dwGate;

	if (!dwGate)
		m_dwRepeatCount = 0;
}

/* =============================================================
// ResManStreamBuffer scalar deleting destructor
// (RE) rtl:0x1002a480; dbg:0x10070650
// =============================================================*/

/* =============================================================
// ResManStreamBuffer::~ResManStreamBuffer()
// (RE) dbg:0x100706a0
//
// Close the notification mutex and release waveform storage.
// The original also closes a null notification handle.
// =============================================================*/

ResManStreamBuffer::~ResManStreamBuffer(void)
{
	if (m_hNotifyMutex != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_hNotifyMutex);

		m_hNotifyMutex = INVALID_HANDLE_VALUE;
	}

	if (m_pWaveForm)
	{
		m_pWaveForm->Release();

		m_pWaveForm = NULL;
	}
}

/* =============================================================
// ResManStreamBuffer::QueryInterface()
// (RE) dbg:0x100707a0
//
// Obtain a supported interface and add a reference to it.
//
// Returns:
//   S_OK
//   E_INVALIDARG   a null output pointer
//   E_NOINTERFACE  an unsupported interface
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::QueryInterface(REFIID riid, void **ppv)
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
	else if (IsEqualIID(riid, IID_ResManStreamBuffer))
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
// ResManStreamBuffer::AddRef()
// (RE) dbg:0x10070980
//
// Add a client reference to the buffer.
//
// Returns: The updated reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
ResManStreamBuffer::AddRef(void)
{
	InterlockedIncrement(&m_cRef);

	return ((ULONG) m_cRef);
}

/* =============================================================
// ResManStreamBuffer::Release()
// (RE) dbg:0x100709c0
//
// Release a client reference and mark the voice released at zero.
// The creator owns destruction.
//
// Returns: The remaining reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
ResManStreamBuffer::Release(void)
{
	if (InterlockedDecrement(&m_cRef))
		return ((ULONG) m_cRef);

	m_dwState = A3DVOICE_STATE_RELEASED;

	return (0);
}

/* =============================================================
// ResManStreamBuffer::GetCaps()
// (RE) dbg:0x10070a20
//
// Build capabilities from the buffer description, render mode and hardware
// binding.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::GetCaps(LPDSBCAPS lpDSBufferCaps)
{
	ASSERT(lpDSBufferCaps != 0 &&
	       !IsBadReadPtr(lpDSBufferCaps, sizeof(DSBCAPS)));

	lpDSBufferCaps->dwFlags = 0;

	if (m_dwRenderMode == RESMANBUFFER_RENDER_3D)
		lpDSBufferCaps->dwFlags |= DSBCAPS_CTRL3D;

	lpDSBufferCaps->dwFlags |= DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLPAN |
				   DSBCAPS_CTRLVOLUME;

	if (m_dwHardwareBound)
		lpDSBufferCaps->dwFlags |= DSBCAPS_LOCHARDWARE;
	else
		lpDSBufferCaps->dwFlags |= DSBCAPS_LOCSOFTWARE;

	lpDSBufferCaps->dwBufferBytes        = m_DSBufferDesc.dwBufferBytes;
	lpDSBufferCaps->dwUnlockTransferRate = (DWORD) -1;
	lpDSBufferCaps->dwPlayCpuOverhead    = 0;

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::GetCurrentPosition()
// (RE) dbg:0x10070c30
//
// Return the pending seek or fill cursor through both outputs.
// The original dereferences null when only the write-cursor output is supplied.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::GetCurrentPosition(DWORD *pdwPlay, DWORD *pdwWrite)
{
	if (pdwPlay)
	{
		if (m_dwTargetPosition == RESMANSTREAMBUFFER_NO_PENDING_SEEK)
			*pdwPlay = m_dwFillPos;
		else
			*pdwPlay = (DWORD) m_dwTargetPosition;
	}

	if (pdwWrite)
		*pdwWrite = *pdwPlay;

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::GetFormat()
// (RE) dbg:0x10070ca0
//
// Copy the stored format. The original ignores output capacity and leaves the
// size-written output untouched.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::GetFormat(LPWAVEFORMATEX lpwfxFormat, DWORD cb, DWORD *lpdwSizeWritten)
{
	ASSERT(lpwfxFormat != 0 &&
	       !IsBadReadPtr(lpwfxFormat, sizeof(WAVEFORMATEX)));
	ASSERT(lpdwSizeWritten != 0 &&
	       !IsBadReadPtr(lpdwSizeWritten, sizeof(DWORD)));

	*lpwfxFormat = m_wfxFormat;

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::GetVolume()
// (RE) dbg:0x10070da0
//
// Read the stored volume.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null output pointer
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::GetVolume(LONG *lplVolume)
{
	ASSERT(lplVolume != 0 && !IsBadReadPtr(lplVolume, sizeof(LONG)));

	if (!lplVolume)
		return (E_INVALIDARG);

	ASSERT(m_lVolume >= -10000 && m_lVolume <= 0);

	*lplVolume = m_lVolume;

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::GetPan()
// (RE) dbg:0x10070e90
//
// Read the stored pan.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a null output pointer
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::GetPan(LONG *lplPan)
{
	ASSERT(lplPan != 0 && !IsBadReadPtr(lplPan, sizeof(LONG)));

	if (!lplPan)
		return (E_INVALIDARG);

	ASSERT(m_lPan >= -10000 && m_lPan <= 10000);

	*lplPan = m_lPan;

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::GetFrequency()
// (RE) dbg:0x10070f80
//
// Read the stored playback frequency.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::GetFrequency(DWORD *lpdwFrequency)
{
	ASSERT(lpdwFrequency != 0 && !IsBadReadPtr(lpdwFrequency, sizeof(DWORD)));
	ASSERT(m_dwFrequency >= 100 && m_dwFrequency <= 100000);

	*lpdwFrequency = m_dwFrequency;

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::GetStatus()
// (RE) dbg:0x10071060
//
// Report playback and looping status from the stored state.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::GetStatus(DWORD *lpdwStatus)
{
	ASSERT(lpdwStatus != 0 && !IsBadReadPtr(lpdwStatus, sizeof(DWORD)));
	ASSERT(m_dwState >= 0x00000001 && m_dwState <= 0x00000003);

	*lpdwStatus = 0;

	if (m_dwState == A3DVOICE_STATE_PLAYING)
	{
		*lpdwStatus |= DSBSTATUS_PLAYING;

		if (m_dwFlags & A3DVOICE_FLAG_LOOPING)
			*lpdwStatus |= DSBSTATUS_LOOPING;
	}

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::Initialize()
// (RE) dbg:0x10071170
//
// Accept initialization without changing the buffer.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::Initialize(LPDIRECTSOUND lpDirectSound, LPCDSBUFFERDESC lpcDSBufferDesc)
{
	ASSERT(lpDirectSound != 0 &&
	       !IsBadReadPtr(lpDirectSound, sizeof(IDirectSound)));
	ASSERT(lpcDSBufferDesc != 0 &&
	       !IsBadReadPtr(lpcDSBufferDesc, sizeof(DSBUFFERDESC1)));

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::SetFormat()
// (RE) dbg:0x10071900
//
// Accept the format request without changing the buffer.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::SetFormat(LPCWAVEFORMATEX pwfx)
{
	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::SetVolume()
// (RE) dbg:0x10071920
//
// Store the volume.
//
// Returns:
//   S_OK
//   E_INVALIDARG  outside [-10000,0]
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::SetVolume(LONG lVolume)
{
	if (lVolume >= DSBVOLUME_MIN && lVolume <= DSBVOLUME_MAX)
	{
		m_lVolume = lVolume;

		ASSERT(m_lVolume >= -10000 && m_lVolume <= 0);

		return (S_OK);
	}

	DBGSTR("ResManStreamBuffer::SetVolume() - Invalid value.  Must be between DSBVOLUME_MIN & DSBVOLUME_MAX.\n");

	return (E_INVALIDARG);
}

/* =============================================================
// ResManStreamBuffer::SetPan()
// (RE) dbg:0x100719f0
//
// Store the pan.
//
// Returns:
//   S_OK
//   E_INVALIDARG  outside [-10000,10000]
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::SetPan(LONG lPan)
{
	if (lPan >= DSBPAN_LEFT && lPan <= DSBPAN_RIGHT)
	{
		m_lPan = lPan;

		ASSERT(m_lPan >= -10000 && m_lPan <= 10000);

		return (S_OK);
	}

	DBGSTR("ResManStreamBuffer::SetPan() - Invalid value passed in.  Must be between DSBPAN_LEFT & DSBPAN_RIGHT.\n");

	return (E_INVALIDARG);
}

/* =============================================================
// ResManStreamBuffer::SetFrequency()
// (RE) dbg:0x10071ac0
//
// Store the playback frequency, restoring the source sample rate for zero.
//
// Returns:
//   S_OK
//   E_INVALIDARG  a nonzero frequency outside [100,100000] Hz
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::SetFrequency(DWORD dwFrequency)
{
	if ((dwFrequency < DSBFREQUENCY_MIN ||
	     dwFrequency > RESMANSTREAMBUFFER_MAX_FREQUENCY) && dwFrequency)
	{
		DBGSTR("ResManStreamBuffer::SetFrequency() - Invalid frequency passed in.\n");

		return (E_INVALIDARG);
	}

	if (dwFrequency)
		m_dwFrequency = dwFrequency;
	else
		m_dwFrequency = m_dwOriginalSampleRate;

	ASSERT(m_dwFrequency >= 100 && m_dwFrequency <= 100000);

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::Unlock()
// (RE) dbg:0x10071cd0
//
// Accept the unlock request without changing waveform storage.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::Unlock(LPVOID pvAudioPtr1, DWORD dwAudioBytes1,
			   LPVOID pvAudioPtr2, DWORD dwAudioBytes2)
{
	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::Restore()
// (RE) dbg:0x10071cf0
//
// Accept restoration without changing the buffer.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::Restore(void)
{
	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::GetAllocationStatus()
// (RE) dbg:0x10071d10
//
// Accept the allocation query without writing the output.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::GetAllocationStatus(LPDWORD lpdwStatus)
{
	ASSERT(lpdwStatus != 0 && !IsBadReadPtr(lpdwStatus, sizeof(DWORD)));

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::GetWave()
// (RE) dbg:0x10071d80
//
// Accept the waveform query without writing the output.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::GetWave(LPBYTE *lplpbWave)
{
	ASSERT(lplpbWave != 0);

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::GetDriverInfo()
// (RE) dbg:0x10071de0
//
// Accept the driver-interface query without writing either output.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::GetDriverInfo(void **lplpIDsDriverBuffer,
				  void **lplpIA3dDriverBuffer)
{
	ASSERT(lplpIDsDriverBuffer != 0);
	ASSERT(lplpIA3dDriverBuffer != 0);

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::SetNewBuffer()
// (RE) dbg:0x10071e80
//
// Accept replacement storage without retaining it.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::SetNewBuffer(LPBYTE lpbWave, DWORD dwBufferBytes)
{
	ASSERT(lpbWave != 0);

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::SetA3dDirectCtrl()
// (RE) dbg:0x10071ee0
//
// Accept direct controls without applying them.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::SetA3dDirectCtrl(LPA3DCTRL_SRC_SUPER lpA3dCtrlDirect,
				     DWORD dwSize)
{
	ASSERT(lpA3dCtrlDirect != 0);
	ASSERT(dwSize > 0);

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::GetStatusEx()
// (RE) dbg:0x10071f80
//
// Accept the status query without writing the output.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::GetStatusEx(LPDWORD lpdwStatusEx)
{
	ASSERT(lpdwStatusEx != 0 && !IsBadReadPtr(lpdwStatusEx, sizeof(DWORD)));

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::MuteForFocusLoss()
// (RE) dbg:0x100724a0; rtl:0x10029d90
//
// Set the focus mute flag and request muting.
//
// Returns: S_OK; the mute result is ignored.
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::MuteForFocusLoss(void)
{
	m_dwFlags |= RESMANBUFFER_FLAG_FOCUSMUTE;

	MuteBuffer();

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::UnMuteForFocusGain()
// (RE) dbg:0x100724f0; rtl:0x10029dc0
//
// Clear the focus mute flag and request unmuting.
//
// Returns: S_OK; the unmute result is ignored.
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::UnMuteForFocusGain(void)
{
	m_dwFlags &= ~RESMANBUFFER_FLAG_FOCUSMUTE;

	UnMuteBuffer();

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::SetNotificationPositions()
// (RE) dbg:0x10072540
//
// Replace or clear the notification array, retaining the caller's storage.
//
// Returns:
//   S_OK
//   E_INVALIDARG   a nonzero count with a null array
//   E_OUTOFMEMORY  if mutex creation fails
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::SetNotificationPositions(DWORD cPositions,
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

		m_cNotify  = cPositions;
		m_paNotify = (LPDSBPOSITIONNOTIFY) pcPositionNotifies;

		VERIFY(ReleaseMutex(m_hNotifyMutex));
	}
	else if (m_cNotify)
	{
		WaitForSingleObject(m_hNotifyMutex, INFINITE);

		m_cNotify  = 0;
		m_paNotify = NULL;

		VERIFY(ReleaseMutex(m_hNotifyMutex));
	}

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::QuerySupport()
// (RE) dbg:0x10073710
//
// Queue a property-support query and wait for the result. Original allocation
// or enqueue failure can leave a null result position.
//
// Returns: Recorded HRESULT; A3DERROR_BUFFER_IN_SOFTWARE without hardware or if
//          the completion wait fails; E_FAIL if the result-mutex wait fails.
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::QuerySupport(REFGUID rguidPropSet, ULONG ulId,
				 PULONG pulTypeSupport)
{
POSITION                pos;
CPropertySetItem       *pItem;
HRESULT                 hr;

	if (!m_dwHardwareBound)
		return (A3DERROR_BUFFER_IN_SOFTWARE);

	pos = NULL;
	hr  = E_FAIL;

	pItem = new CPropertySetItem();

	if (pItem)
	{
		if (WaitForSingleObject(m_hPropSetMutex, INFINITE) == WAIT_OBJECT_0)
		{
			pItem->Create(rguidPropSet, ulId, pulTypeSupport);

			pos = m_listPropSetItems.AddTail(pItem);
		}

		VERIFY(ReleaseMutex(m_hPropSetMutex));
	}

	if (WaitForSingleObject(m_hPropSetCacheFlushed, INFINITE) != WAIT_OBJECT_0)
		return (A3DERROR_BUFFER_IN_SOFTWARE);

	if (WaitForSingleObject(m_hPropSetMutex, INFINITE) == WAIT_OBJECT_0)
	{
		pItem = (CPropertySetItem *) m_listPropSetItems.GetAt(pos);

		ASSERT(pItem != 0 &&
		       !IsBadReadPtr(pItem, sizeof(CPropertySetItem)));

		*pulTypeSupport = pItem->m_ulTypeSupport;
		hr              = pItem->m_hrCall;

		m_listPropSetItems.RemoveAt(pos);

		delete pItem;
	}

	VERIFY(ReleaseMutex(m_hPropSetMutex));
	VERIFY(ResetEvent(m_hPropSetCacheFlushed));

	return (hr);
}

/* =============================================================
// ResManStreamBuffer::AttachDalBufferInfo()
// (RE) dbg:0x100734f0
//
// Attach the DAL buffer and queue any saved super controls.
//
// Returns: S_OK; the control update result is ignored.
// =============================================================*/

HRESULT
ResManStreamBuffer::AttachDalBufferInfo(DalBufferInfo *lpDalBufferInfo)
{
	ASSERT(lpDalBufferInfo != 0 &&
	       !IsBadReadPtr(lpDalBufferInfo, sizeof(DalBufferInfo)));

	SetDalBufferInfoBare(lpDalBufferInfo);

	if (m_lpA3dCtrlSuper)
		SetA3dSuperCtrl(m_lpA3dCtrlSuper, m_dwA3dCtrlSuperSize);

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::Duplicate()
// (RE) dbg:0x10072b80
//
// Copy settings and saved controls from another stream, sharing its waveform.
//
// Returns: S_OK; the QueryInterface error if IID_ResManStreamBuffer is
//          unavailable.
// =============================================================*/

HRESULT
ResManStreamBuffer::Duplicate(LPDIRECTSOUNDBUFFER lpDSB)
{
ResManStreamBuffer     *pOriginalResManStreamBuffer;
HRESULT                 hr;

	ASSERT(lpDSB != 0 && !IsBadReadPtr(lpDSB, sizeof(IDirectSoundBuffer)));

	pOriginalResManStreamBuffer = NULL;

	hr = lpDSB->QueryInterface(IID_ResManStreamBuffer,
				   (void **) &pOriginalResManStreamBuffer);

	if (FAILED(hr))
	{
		DBGSTR("ResManStreamBuffer::Duplicate - failed to get ResManStream interface\n");

		return (hr);
	}

	ASSERT(pOriginalResManStreamBuffer != 0 &&
	       !IsBadReadPtr(pOriginalResManStreamBuffer, sizeof(ResManStreamBuffer)));
	ASSERT(pOriginalResManStreamBuffer->m_pWaveForm != 0 &&
	       !IsBadReadPtr(pOriginalResManStreamBuffer->m_pWaveForm, sizeof(CWaveForm)));

	pOriginalResManStreamBuffer->m_pWaveForm->AddRef();

	m_DSBufferDesc                  = pOriginalResManStreamBuffer->m_DSBufferDesc;
	m_wfxFormat                     = pOriginalResManStreamBuffer->m_wfxFormat;
	m_DSBufferDesc.lpwfxFormat      = &m_wfxFormat;

	m_pWaveForm             = pOriginalResManStreamBuffer->m_pWaveForm;
	m_dwFlags               = pOriginalResManStreamBuffer->m_dwFlags;
	m_dwFrequency           = pOriginalResManStreamBuffer->m_dwFrequency;
	m_dwOriginalSampleRate  = pOriginalResManStreamBuffer->m_dwOriginalSampleRate;
	m_dwRenderMode          = pOriginalResManStreamBuffer->m_dwRenderMode;

	if (pOriginalResManStreamBuffer->m_lpA3dCtrlSuper)
		memcpy(m_aCtrlBuffers, pOriginalResManStreamBuffer->m_lpA3dCtrlSuper, sizeof(m_aCtrlBuffers[0]));

	m_lpA3dCtrlSuper        = &m_aCtrlBuffers[0];
	m_lpA3dCtrlSuperPending = &m_aCtrlBuffers[0];
	m_dwA3dCtrlSuperSize    = sizeof(m_aCtrlBuffers[0]);

	if (pOriginalResManStreamBuffer)
	{
		pOriginalResManStreamBuffer->Release();

		pOriginalResManStreamBuffer = NULL;
	}

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::SetDalBufferInfoBare()
// (RE) dbg:0x10072b20
//
// Store the DAL binding and its hardware-mode flag.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
ResManStreamBuffer::SetDalBufferInfoBare(DalBufferInfo *lpDalBufferInfo)
{
	m_pDalBufferInfo = lpDalBufferInfo;

	if (lpDalBufferInfo)
		m_dwHardwareBound = lpDalBufferInfo->ReportsHardwareStatus();
	else
		m_dwHardwareBound = 0;

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::GetA3dCtrlSuper()
// (RE) dbg:0x10070620
//
// Return the retained super-control block pointer.
//
// Returns: S_OK.
// =============================================================*/

HRESULT
ResManStreamBuffer::GetA3dCtrlSuper(LPVOID *lplpCtrl)
{
	*lplpCtrl = (LPVOID) m_lpA3dCtrlSuper;

	return (S_OK);
}

/* =============================================================
// ResManStreamBuffer::Unknown_0x74()
// (RE) dbg:0x100736f0
//
// Reject the call; the purpose of primary slot 29 is unknown.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
ResManStreamBuffer::Unknown_0x74(DWORD dwArg)
{
	return (E_NOTIMPL);
}

/* (RE) Compiler-generated adjustors:
 * Secondary QueryInterface/AddRef/Release slots: dbg:0x10074530..0x100745e0.
 * IA3dDalBuffer slot 3 -> ResManBuffer::SetA3dSuperCtrl: dbg:0x100735b0.
 * IResManBuffer slot 11 -> ResManBuffer::GetCtrlBuffers: dbg:0x100736c0.
 */
