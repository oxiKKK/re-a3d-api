/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * ac3fgraph.cpp
 *
 * Implements the DirectShow fallback for AC-3 sources behind IA3dSource2.
 * It constructs a filter graph for file playback, selects decoder
 * components and maps source transport operations to graph control and
 * seeking.
 *
 * A worker thread handles media events and playback notifications. Many
 * spatial and raw-audio operations are unsupported on this path.
 * CA3dSourceCom selects and owns the graph implementation when the normal
 * source decoder path cannot be used.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include <dshow.h>
#include <stdio.h>
#include "ac3fgraph.h"

/* Worker event-read timeout */
#define A3D_AC3_EVENT_READ_TIMEOUT_MS 100

/* (RE) Out-of-line macro helpers: SUCCEEDED dbg:0x100323a0,
 * FAILED dbg:0x10035320, SUCCEEDED dbg:0x10035340.
 */

static const GUID CLSID_FilterGraph_Ac3 =
	{ 0xE436EBB3, 0x524F, 0x11CE, { 0x9F, 0x53, 0x00, 0x20, 0xAF, 0xB0, 0xA7, 0x70 } };
static const GUID IID_IGraphBuilder_Ac3 =
	{ 0x56A868A9, 0x0AD4, 0x11CE, { 0xB0, 0x3A, 0x00, 0x20, 0xAF, 0xB0, 0xA7, 0x70 } };
static const GUID IID_IBaseFilter_Ac3 =
	{ 0x56A86895, 0x0AD4, 0x11CE, { 0xB0, 0x3A, 0x00, 0x20, 0xAF, 0xB0, 0xA7, 0x70 } };
static const GUID IID_IAsyncReader_Ac3 =
	{ 0x56A868AA, 0x0AD4, 0x11CE, { 0xB0, 0x3A, 0x00, 0x20, 0xAF, 0xB0, 0xA7, 0x70 } };

static const GUID CLSID_Ac3Parser_Ac3 =
	{ 0x280A3020, 0x86CF, 0x11D1, { 0xAB, 0xE6, 0x00, 0xA0, 0xC9, 0x05, 0xF3, 0x75 } };
static const GUID CLSID_McAc3Decoder_Ac3 =
	{ 0x36A5F770, 0xFE4C, 0x11CE, { 0xA9, 0xED, 0x00, 0xAA, 0x00, 0x2F, 0xEA, 0xB5 } };

static const GUID CLSID_DSoundRender_Ac3 =
	{ 0x79376820, 0x07D0, 0x11CF, { 0xA2, 0x4D, 0x00, 0x20, 0xAF, 0xD7, 0x97, 0x67 } };

static const GUID CLSID_FilterMapper_Ac3 =			/* (SDK) CLSID_FilterMapper */
	{ 0xE436EBB2, 0x524F, 0x11CE, { 0x9F, 0x53, 0x00, 0x20, 0xAF, 0xBB, 0xA7, 0x70 } };
static const GUID IID_IFilterMapper_Ac3 =			/* (SDK) IID_IFilterMapper */
	{ 0x56A868A3, 0x0AD4, 0x11CE, { 0xB0, 0x3A, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70 } };

static const GUID GUID_NULL_Ac3 =
	{ 0x00000000, 0x0000, 0x0000, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } };
static const GUID MEDIATYPE_Audio_Ac3 =
	{ 0x73647561, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
static const GUID MEDIASUBTYPE_PCM_Ac3 =
	{ 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
static const GUID MEDIASUBTYPE_DOLBY_AC3_Ac3 =
	{ 0xE06D802C, 0xDB46, 0x11CF, { 0xB4, 0xD1, 0x00, 0x80, 0x5F, 0x6C, 0xBB, 0xEA } };
static const GUID FORMAT_WaveFormatEx_Ac3 =
	{ 0x05589F81, 0xC356, 0x11CE, { 0xBF, 0x01, 0x00, 0xAA, 0x00, 0x55, 0x59, 0x5A } };

static const GUID IID_IMediaEvent_Ac3 =
	{ 0x56A868C0, 0x0AD4, 0x11CE, { 0xB0, 0x3A, 0x00, 0x20, 0xAF, 0xB0, 0xA7, 0x70 } };
static const GUID IID_IMediaSeeking_Ac3 =
	{ 0x36B73880, 0xC2C8, 0x11CF, { 0x8B, 0x46, 0x00, 0x80, 0x5F, 0x6C, 0xEF, 0x60 } };
static const GUID IID_IMediaControl_Ac3 =
	{ 0x56A868B1, 0x0AD4, 0x11CE, { 0xB0, 0x3A, 0x00, 0x20, 0xAF, 0xB0, 0xA7, 0x70 } };

static const GUID IID_IMCAC3_Ac3 =
	{ 0xFFC08882, 0xCDAC, 0x11CE, { 0x8A, 0x03, 0x00, 0xAA, 0x00, 0x6E, 0xCB, 0x65 } };

/* Private decoder control interface; slots 0..2 are IUnknown. */
struct IMCAC3 : public IUnknown
{
	/* slot 3 */ virtual HRESULT STDMETHODCALLTYPE GetHardwareChannels(LPDWORD pdwChannels) = 0;
	/* slot 4 */ virtual HRESULT STDMETHODCALLTYPE SetNotifyEvent(DWORD dwPosition, DWORD dwReserved, HANDLE hEvent) = 0;
	/* slot 5 */ virtual HRESULT STDMETHODCALLTYPE ClearNotifyEvents(void) = 0;
	/* slot 6 */ virtual HRESULT STDMETHODCALLTYPE SetPosition(DWORD dwPosition, DWORD dwReserved) = 0;
	/* slot 7 */ virtual HRESULT STDMETHODCALLTYPE GetPosition(LPDWORD pdwPosition) = 0;
};

static HANDLE	ms_hThread        = INVALID_HANDLE_VALUE;	/* (RE) dbg:0x101478b4. Shared worker handle. */
static DWORD	ms_dwThreadId     = 0;				/* (RE) dbg:0x101478b8. Worker thread ID. */
static HANDLE	ms_hEvent_die     = INVALID_HANDLE_VALUE;	/* (RE) dbg:0x101478bc. Manual-reset shutdown request. */
static HANDLE	ms_hEvent_die_ack = INVALID_HANDLE_VALUE;	/* (RE) dbg:0x101478c0. Manual-reset shutdown acknowledgement. */
static DWORD	ms_cInstances     = 0;				/* (RE) dbg:0x10152a70. Loaded graph instance count. */
static DWORD	ms_fThreadRunning = 0;				/* (RE) dbg:0x10152a74. Worker running flag. */

/* =============================================================
// GetPin()
// (RE) dbg:0x100350f0
//
// Find the filter's first pin in the requested direction. The original adds a
// reference without releasing the enumerator's pin reference.
//
// Returns: Matching pin; null if none matches or enumeration fails.
// =============================================================*/

static IPin *
GetPin(IBaseFilter *pFilter, PIN_DIRECTION dirWanted)
{
IEnumPins      *pEnum;
IPin           *pPin;
ULONG           cFetched;
PIN_DIRECTION   dir;

	pEnum = NULL;
	if (FAILED(pFilter->EnumPins(&pEnum)))
		return (NULL);

	cFetched = 0;
	pEnum->Reset();

	for (;;)
	{
		pPin = NULL;
		if (FAILED(pEnum->Next(1, &pPin, &cFetched)))
		{
			DBGSTR("Failed to enum a pin\n");
			pEnum->Release();
			return (NULL);
		}

		if (pPin)
		{
			pPin->QueryDirection(&dir);
			if (dir == dirWanted)
				break;
		}

		if (pPin)
			pPin->Release();

		if (!cFetched)
		{
			pEnum->Release();
			return (NULL);
		}
	}

	pPin->AddRef();
	pEnum->Release();

	return (pPin);
}

/* =============================================================
// FreeMediaType()
// (RE) dbg:0x100a5084
//
// Free the format block and release the media type's object reference.
// =============================================================*/

static void
FreeMediaType(AM_MEDIA_TYPE *pmt)
{
	if (pmt->cbFormat != 0)
	{
		CoTaskMemFree(pmt->pbFormat);
		pmt->cbFormat = 0;
		pmt->pbFormat = NULL;
	}

	if (pmt->pUnk != NULL)
	{
		pmt->pUnk->Release();
		pmt->pUnk = NULL;
	}
}

/* =============================================================
// Ac3FilterGraph::Ac3FilterGraph()
// (RE) dbg:0x10032450; rtl:0x10016220; thunk dbg:0x1000103c
//
// Initialize an idle graph with no filters or worker mutex.
// =============================================================*/

Ac3FilterGraph::Ac3FilterGraph(void)
{
	m_cRef = 1;

	m_pGraphBuilder         = NULL;
	m_pSourceFilter         = NULL;
	m_pParserFilter         = NULL;
	m_pDecoderFilter        = NULL;
	m_pMcAc3                = NULL;
	m_pMediaControl         = NULL;
	m_pMediaEvent           = NULL;
	m_pMediaSeeking         = NULL;

	m_dwStreamSize  = 0;
	m_llDuration    = 0;
	m_fPlaying      = 0;
	m_dwPlayFlags   = 0;

	m_pszFilename   = NULL;
	m_hMutex        = INVALID_HANDLE_VALUE;
	m_hPlayEvent    = NULL;
}

/* =============================================================
// Ac3FilterGraph::~Ac3FilterGraph()
// (RE) dbg:0x10032e30; rtl:0x10016350
//
// Stop the shared event worker, release graph resources and close handles.
// =============================================================*/

Ac3FilterGraph::~Ac3FilterGraph(void)
{
	if (ms_fThreadRunning)
	{
		SetEvent(ms_hEvent_die);
		WaitForSingleObject(ms_hEvent_die_ack, INFINITE);
		DBGSTR("Thread die acked\n");
	}

	ReleaseGraph();

	if (m_pszFilename)
	{
		operator delete(m_pszFilename);
		m_pszFilename = NULL;
	}

	if (m_hMutex != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_hMutex);
		m_hMutex = INVALID_HANDLE_VALUE;
	}

	if (ms_hEvent_die != INVALID_HANDLE_VALUE)
	{
		CloseHandle(ms_hEvent_die);
		ms_hEvent_die = INVALID_HANDLE_VALUE;
	}

	if (ms_hEvent_die_ack != INVALID_HANDLE_VALUE)
	{
		CloseHandle(ms_hEvent_die_ack);
		ms_hEvent_die_ack = INVALID_HANDLE_VALUE;
	}

	if (ms_hThread != INVALID_HANDLE_VALUE)
	{
		CloseHandle(ms_hThread);
		ms_hThread = INVALID_HANDLE_VALUE;
	}

	if (ms_cInstances > 0)
		--ms_cInstances;
}

/* =============================================================
// Ac3FilterGraph::QueryInterface()
// (RE) dbg:0x10032f80; rtl:0x10016400
//
// Obtain IUnknown or IA3dSource2 and add a reference.
//
// Returns:
//   S_OK
//   E_POINTER      a null output
//   E_NOINTERFACE  otherwise
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::QueryInterface(REFIID riid, void **ppv)
{
	if (!ppv)
		return (E_POINTER);

	if (IsEqualIID(riid, IID_IUnknown) ||
	    IsEqualIID(riid, IID_IA3dSource2))
	{
		*ppv = (IA3dSource2 *) this;
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
// Ac3FilterGraph::AddRef()
// (RE) dbg:0x10033020; rtl:0x10016470
//
// Add a reference to the graph source.
//
// Returns: The updated reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
Ac3FilterGraph::AddRef(void)
{
	return (++m_cRef);
}

/* =============================================================
// Ac3FilterGraph::Release()
// (RE) dbg:0x10033050; rtl:0x10016490
//
// Release a reference and delete the graph source at zero.
//
// Returns: The remaining reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
Ac3FilterGraph::Release(void)
{
	if (--m_cRef == 0)
	{
		delete this;

		return (0);
	}

	return (m_cRef);
}

/* =============================================================
// Ac3FilterGraph::GetAudioSize()
// (RE) dbg:0x10032600; rtl:0x10016290
//
// Read the loaded stream size.
//
// Returns: The file length in bytes.
// =============================================================*/

STDMETHODIMP_(DWORD)
Ac3FilterGraph::GetAudioSize(void)
{
	return (m_dwStreamSize);
}

/* =============================================================
// Ac3FilterGraph::GetType()
// (RE) dbg:0x10032620; rtl:0x100162a0
//
// Report the AC-3 source type.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetType(LPDWORD pdwType)
{
	if (!pdwType)
		return (E_POINTER);

	*pdwType = A3DSOURCE_AC3_HARDWARE;

	return (S_OK);
}

/* =============================================================
// Ac3FilterGraph::GetStatus()
// (RE) dbg:0x100330d0; rtl:0x100164c0
//
// Report playback and looping status.
//
// Returns:
//   S_OK
//   E_POINTER  a null output
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetStatus(LPDWORD pdwStatus)
{
	if (!pdwStatus)
		return (E_POINTER);

	*pdwStatus = 0;

	if (m_fPlaying)
	{
		*pdwStatus |= DSBSTATUS_PLAYING;

		if (m_dwPlayFlags & DSBPLAY_LOOPING)
			*pdwStatus |= DSBSTATUS_LOOPING;
	}

	return (S_OK);
}

/* =============================================================
// Ac3FilterGraph::Play()
// (RE) dbg:0x10033140; rtl:0x10016500
//
// Run the graph under the playback mutex.
// An escaping exception leaves the mutex owned, as in the original.
//
// Returns: PlayInternal result.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::Play(INT nMode)
{
HRESULT	hr;

	WaitForSingleObject(m_hMutex, INFINITE);
	hr = PlayInternal(nMode);
	ReleaseMutex(m_hMutex);

	return (hr);
}

/* =============================================================
// Ac3FilterGraph::Stop()
// (RE) dbg:0x10033330; rtl:0x10016630
//
// Stop the graph under the playback mutex.
//
// Returns: StopInternal result.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::Stop(void)
{
HRESULT	hr;

	WaitForSingleObject(m_hMutex, INFINITE);
	hr = StopInternal();
	ReleaseMutex(m_hMutex);

	return (hr);
}

/* =============================================================
// Ac3FilterGraph::Rewind()
// (RE) dbg:0x100326a0; rtl:0x1000c860
//
// Seek to the start of the stream.
//
// Returns: SetPlayPosition result.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::Rewind(void)
{
	return (SetPlayPosition(0));
}

/* =============================================================
// Ac3FilterGraph::GetPlayPosition()
// (RE) dbg:0x10036410; rtl:0x10017c30
//
// Read the byte position under the playback mutex.
//
// Returns:
//   GetPlayPositionInternal result
//   A3DERROR_NO_SOUND_BUFFERS_CREATED  if no graph is loaded
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetPlayPosition(LPDWORD pdwPosition)
{
HRESULT	hr;

	if (!m_pGraphBuilder)
	{
		DBGSTR("Ac3FilterGraph::GetPlayPosition - No wave data.\n");

		return (A3DERROR_NO_SOUND_BUFFERS_CREATED);
	}

	WaitForSingleObject(m_hMutex, INFINITE);
	hr = GetPlayPositionInternal(pdwPosition);
	ReleaseMutex(m_hMutex);

	return (hr);
}

/* =============================================================
// Ac3FilterGraph::SetPlayPosition()
// (RE) dbg:0x10036590; rtl:0x10017ce0
//
// Seek to a byte position under the playback mutex.
//
// Returns:
//   SetPlayPositionInternal result
//   A3DERROR_NO_SOUND_BUFFERS_CREATED  if no graph is loaded
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::SetPlayPosition(DWORD dwPosition)
{
HRESULT	hr;

	if (!m_pGraphBuilder)
	{
		DBGSTR("Ac3FilterGraph::GetPlayPosition - No wave data.\n");

		return (A3DERROR_NO_SOUND_BUFFERS_CREATED);
	}

	WaitForSingleObject(m_hMutex, INFINITE);
	hr = SetPlayPositionInternal(dwPosition);
	ReleaseMutex(m_hMutex);

	return (hr);
}

/* =============================================================
// Ac3FilterGraph::SetPlayEvent()
// (RE) dbg:0x10033a10; rtl:0x10016970
//
// Register a hardware position event or retain an end event. The original
// compares the byte position against media duration for the end test.
//
// Returns: S_OK for an end event; the decoder result otherwise;
//          A3DERROR_NO_SOUND_BUFFERS_CREATED without a graph;
//          A3DERROR_FEATURE_UNSUPPORTED_BY_DECODER without hardware control.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::SetPlayEvent(DWORD dwPosition, HANDLE hEvent)
{
	if (!m_pGraphBuilder)
		return (A3DERROR_NO_SOUND_BUFFERS_CREATED);

	if (!m_pMcAc3)
		return (A3DERROR_FEATURE_UNSUPPORTED_BY_DECODER);

	if (dwPosition != (DWORD) -1 &&
	    (dwPosition != (DWORD) m_llDuration ||
	     (DWORD) (m_llDuration >> 32) != 0))
		return (m_pMcAc3->SetNotifyEvent(dwPosition, 0, hEvent));

	m_hPlayEvent = hEvent;

	return (S_OK);
}

/* =============================================================
// Ac3FilterGraph::ClearPlayEvents()
// (RE) dbg:0x100339a0; rtl:0x10016930
//
// Clear the retained completion event and decoder notifications.
//
// Returns:
//   Decoder result
//   A3DERROR_NO_SOUND_BUFFERS_CREATED   without a graph
//   A3DERROR_FEATURE_UNSUPPORTED_BY_DECODER
//                                       without hardware control
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::ClearPlayEvents(void)
{
	m_hPlayEvent = NULL;

	if (!m_pGraphBuilder)
		return (A3DERROR_NO_SOUND_BUFFERS_CREATED);

	if (m_pMcAc3)
		return (m_pMcAc3->ClearNotifyEvents());

	return (A3DERROR_FEATURE_UNSUPPORTED_BY_DECODER);
}

/* =============================================================
// Ac3FilterGraph::GetCaps()
// (RE) dbg:0x10036740; rtl:0x10017da0
//
// Report AC-3 capabilities, the loaded filename and hardware-decoder presence.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetCaps(LPA3DCAPS_SOURCE lpSourceCaps)
{
	lpSourceCaps->dwType                            = A3DSOURCE_FORMAT_AC3;
	lpSourceCaps->szFilename                        = m_pszFilename;
	lpSourceCaps->data.ac3Info.dwSize               = sizeof(A3DSOURCE_AC3INFO);
	lpSourceCaps->data.ac3Info.bPlayingInHardware   = m_pMcAc3 ? TRUE : FALSE;

	return (S_OK);
}

/* =============================================================
// Ac3FilterGraph::SetPlayTime()
// (RE) dbg:0x100326d0; rtl:0x100162e0
//
// Reject time-based seeking for encoded sources.
//
// Returns: A3DERROR_ENCODED_SOURCE_TYPE_CANNOT_BE_TIME_SEEKED.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::SetPlayTime(A3DVAL)
{
	return (A3DERROR_ENCODED_SOURCE_TYPE_CANNOT_BE_TIME_SEEKED);
}

/* =============================================================
// Ac3FilterGraph::GetPlayTime()
// (RE) dbg:0x100326f0; rtl:0x100162e0
//
// Reject time-based seeking for encoded sources.
//
// Returns: A3DERROR_ENCODED_SOURCE_TYPE_CANNOT_BE_TIME_SEEKED.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetPlayTime(LPA3DVAL)
{
	return (A3DERROR_ENCODED_SOURCE_TYPE_CANNOT_BE_TIME_SEEKED);
}

/* =============================================================
// Ac3FilterGraph::LoadWaveFile()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::LoadWaveFile(LPSTR)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::LoadWaveData()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::LoadWaveData(LPVOID, DWORD)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::AllocateAudioData()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::AllocateAudioData(INT)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::FreeAudioData()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::FreeAudioData(void)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::SetAudioFormat()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::SetAudioFormat(LPVOID)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::GetAudioFormat()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetAudioFormat(LPVOID)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::Lock()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::Lock(DWORD, DWORD, LPVOID *, LPDWORD, LPVOID *, LPDWORD, DWORD)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::Unlock()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::Unlock(LPVOID, DWORD, LPVOID, DWORD)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::SetPosition3f()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::SetPosition3f(A3DVAL, A3DVAL, A3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::GetPosition3f()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetPosition3f(LPA3DVAL, LPA3DVAL, LPA3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::SetPosition3fv()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::SetPosition3fv(LPA3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::GetPosition3fv()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetPosition3fv(LPA3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::SetOrientationAngles3f()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::SetOrientationAngles3f(A3DVAL, A3DVAL, A3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::GetOrientationAngles3f()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetOrientationAngles3f(LPA3DVAL, LPA3DVAL, LPA3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::SetOrientationAngles3fv()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::SetOrientationAngles3fv(LPA3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::GetOrientationAngles3fv()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetOrientationAngles3fv(LPA3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::SetOrientation6f()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::SetOrientation6f(A3DVAL, A3DVAL, A3DVAL, A3DVAL, A3DVAL, A3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::GetOrientation6f()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetOrientation6f(LPA3DVAL, LPA3DVAL, LPA3DVAL, LPA3DVAL,
                                 LPA3DVAL, LPA3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::SetOrientation6fv()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::SetOrientation6fv(LPA3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::GetOrientation6fv()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetOrientation6fv(LPA3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::SetVelocity3f()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::SetVelocity3f(A3DVAL, A3DVAL, A3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::GetVelocity3f()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetVelocity3f(LPA3DVAL, LPA3DVAL, LPA3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::SetVelocity3fv()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::SetVelocity3fv(LPA3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::GetVelocity3fv()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetVelocity3fv(LPA3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::SetCone()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::SetCone(A3DVAL, A3DVAL, A3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::GetCone()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetCone(LPA3DVAL, LPA3DVAL, LPA3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::SetMinMaxDistance()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::SetMinMaxDistance(A3DVAL, A3DVAL, DWORD)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::GetMinMaxDistance()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetMinMaxDistance(LPA3DVAL, LPA3DVAL, LPDWORD)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::SetGain()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::SetGain(A3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::GetGain()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetGain(LPA3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::SetPitch()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::SetPitch(A3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::GetPitch()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetPitch(LPA3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::SetDopplerScale()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::SetDopplerScale(A3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::GetDopplerScale()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetDopplerScale(LPA3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::SetDistanceModelScale()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::SetDistanceModelScale(A3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::GetDistanceModelScale()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetDistanceModelScale(LPA3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::SetEq()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::SetEq(A3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::GetEq()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetEq(LPA3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::SetPriority()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::SetPriority(A3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::GetPriority()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetPriority(LPA3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::SetRenderMode()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::SetRenderMode(DWORD)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::GetRenderMode()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetRenderMode(LPDWORD)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::GetAudibility()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetAudibility(LPA3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::GetOcclusionFactor()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetOcclusionFactor(LPA3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::SetPanValues()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::SetPanValues(DWORD, LPA3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::GetPanValues()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetPanValues(DWORD, LPA3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::SetTransformMode()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::SetTransformMode(DWORD)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::GetTransformMode()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetTransformMode(LPDWORD)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::SetReflectionDelayScale()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::SetReflectionDelayScale(A3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::GetReflectionDelayScale()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetReflectionDelayScale(LPA3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::SetReflectionGainScale()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::SetReflectionGainScale(A3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::GetReflectionGainScale()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetReflectionGainScale(LPA3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::SetVolumetricBounds()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::SetVolumetricBounds(A3DVAL, A3DVAL, A3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::GetVolumetricBounds()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetVolumetricBounds(LPA3DVAL, LPA3DVAL, LPA3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::SetVolumetricDamping()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::SetVolumetricDamping(A3DVOLSRCDAMPINFO *)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::GetVolumetricDamping()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetVolumetricDamping(A3DVOLSRCDAMPINFO *)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::SetReverbMix()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::SetReverbMix(A3DVAL, A3DVAL)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::GetReverbMix()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetReverbMix(A3DVAL *, A3DVAL *)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::NewManualReflection()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::NewManualReflection(LPA3DREFLECTION *)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::FreeManualReflections()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::FreeManualReflections(void)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::GetNumManualReflections()
//
// Reject this operation for hardware AC-3 sources.
//
// Returns: A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::GetNumManualReflections(int *)
{
	return (A3DERROR_HARDWARE_AC3_OBJECT_DOES_NOT_IMPLEMENT_THIS_MEMBER);
}

/* =============================================================
// Ac3FilterGraph::PlayInternal()
// (RE) dbg:0x100331a0; rtl:0x10016540
//
// Run the graph and start the event worker if needed.
//
// Returns: A3DERROR_NO_SOUND_BUFFERS_CREATED without media control; otherwise
//          the Run or worker-start result, replaced by Stop's result on
//          failure.
// =============================================================*/

HRESULT
Ac3FilterGraph::PlayInternal(INT nMode)
{
HRESULT	hr;

	if (!m_pMediaControl)
		return (A3DERROR_NO_SOUND_BUFFERS_CREATED);

	try
	{
		hr = m_pMediaControl->Run();
		if (FAILED(hr))
			throw "Media control Fail to run";

		if (!ms_fThreadRunning)
		{
			hr = StartAc3EventThread();
			if (FAILED(hr))
				throw "Fail to start up event callback thread";
		}

		m_dwPlayFlags   = nMode;
		m_fPlaying      = 1;
	}
	/* (RE) Catch handler: dbg:0x1003327d. */
	catch (const char *pszWhy)
	{
		TRACE("Ac3FilterGraph::play() -- %s\n", pszWhy);

		hr = m_pMediaControl->Stop();
	}

	return (hr);
}

/* =============================================================
// Ac3FilterGraph::StopInternal()
// (RE) dbg:0x10033380
//
// Stop playback and rewind the stream, clearing the playing flag if Stop
// succeeds.
//
// Returns: A3DERROR_NO_SOUND_BUFFERS_CREATED without media control; otherwise
//          the seek result, even when Stop fails.
// =============================================================*/

HRESULT
Ac3FilterGraph::StopInternal(void)
{
HRESULT	hr;

	if (!m_pMediaControl)
		return (A3DERROR_NO_SOUND_BUFFERS_CREATED);

	hr = m_pMediaControl->Stop();
	if (SUCCEEDED(hr))
		m_fPlaying = 0;

	return (SetPlayPosition(0));
}

/* =============================================================
// Ac3FilterGraph::GetPlayPositionInternal()
// (RE) dbg:0x100364d0
//
// Read the decoder byte position or convert the graph position to bytes.
//
// Returns: The decoder or media-seeking result; the output is written on
//          failure too.
// =============================================================*/

HRESULT
Ac3FilterGraph::GetPlayPositionInternal(LPDWORD pdwPosition)
{
HRESULT		hr;
LONGLONG	llValue;

	hr      = S_OK;
	llValue = 0;

	if (m_pMcAc3)
	{
		hr = m_pMcAc3->GetPosition((LPDWORD) &llValue);
		*pdwPosition = (DWORD) llValue;
	}
	else
	{
		hr = m_pMediaSeeking->GetCurrentPosition(&llValue);
		*pdwPosition = (DWORD) ((double) llValue / (double) m_llDuration *
					(double) m_dwStreamSize);
	}

	return (hr);
}

/* =============================================================
// Ac3FilterGraph::SetPlayPositionInternal()
// (RE) dbg:0x10036650
//
// Set the hardware byte cursor when available and seek the graph to matching
// media time.
//
// Returns: Media-seeking result; A3DERROR_NO_SOUND_BUFFERS_CREATED without
//          media seeking. The hardware seek result is ignored.
// =============================================================*/

HRESULT
Ac3FilterGraph::SetPlayPositionInternal(DWORD dwPosition)
{
LONGLONG	llTime;
LONGLONG	llStop;

	if (!m_pMediaSeeking)
	{
		DBGSTR("Ac3FilterGraph::GetPlayPosition - No wave data.\n");

		return (A3DERROR_NO_SOUND_BUFFERS_CREATED);
	}

	if (m_pMcAc3)
		m_pMcAc3->SetPosition(dwPosition, 0);

	llTime = (LONGLONG) ((double) m_llDuration *
			     ((double) dwPosition / (double) m_dwStreamSize));
	llStop = 0;

	return (m_pMediaSeeking->SetPositions(&llTime, AM_SEEKING_AbsolutePositioning,
					      &llStop, AM_SEEKING_NoPositioning));
}

/* =============================================================
// Ac3FilterGraph::StartAc3EventThread()
// (RE) dbg:0x10033740; rtl:0x100167c0
//
// Create the playback mutex, shutdown events and media-event worker.
//
// Returns:
//   S_OK
//   E_FAIL  if a mutex, event or thread could not be created
// =============================================================*/

HRESULT
Ac3FilterGraph::StartAc3EventThread(void)
{
HRESULT	hr;

	hr = S_OK;

	try
	{
		m_hMutex = CreateMutexA(NULL, FALSE, NULL);
		if (!m_hMutex)
			throw "Could not create callback.";

		ms_hEvent_die = CreateEventA(NULL, TRUE, FALSE, NULL);
		if (!ms_hEvent_die)
			throw "Could not create event ms_hEvent_die";

		ms_hEvent_die_ack = CreateEventA(NULL, TRUE, FALSE, NULL);
		if (!ms_hEvent_die_ack)
			throw "Could not create event ms_hEvent_die_ack";

		ms_hThread = CreateThread(NULL, 0, EventThreadProc, this, 0, &ms_dwThreadId);
		if (!ms_hThread)
			throw "Could not create thread.";

		ms_fThreadRunning = 1;
	}
	/* (RE) Catch handler: dbg:0x10033851. */
	catch (const char *pszWhy)
	{
		TRACE("A3CSource::StartAc3EventThread() - %s\n", pszWhy);

		if (m_hMutex != INVALID_HANDLE_VALUE)
		{
			CloseHandle(m_hMutex);
			m_hMutex = INVALID_HANDLE_VALUE;
		}

		if (ms_hEvent_die != INVALID_HANDLE_VALUE)
		{
			CloseHandle(ms_hEvent_die);
			ms_hEvent_die = INVALID_HANDLE_VALUE;
		}

		if (ms_hEvent_die_ack != INVALID_HANDLE_VALUE)
		{
			CloseHandle(ms_hEvent_die_ack);
			ms_hEvent_die_ack = INVALID_HANDLE_VALUE;
		}

		if (ms_hThread != INVALID_HANDLE_VALUE)
		{
			CloseHandle(ms_hThread);
			ms_hThread = INVALID_HANDLE_VALUE;
		}

		return (E_FAIL);
	}

	return (hr);
}

/* =============================================================
// Ac3FilterGraph::EventThreadProc()
// (RE) dbg:0x10033490; rtl:0x10016690
//
// Process graph completion events until shutdown, then signal the
// acknowledgement.
//
// Returns: 0.
// =============================================================*/

DWORD WINAPI
Ac3FilterGraph::EventThreadProc(LPVOID pvRef)
{
Ac3FilterGraph *psrc;
HRESULT         hr;
OAEVENT         hGraphEvent;
HANDLE          ahWait[2];
BOOL            fDone;
long            lEventCode;
LONG_PTR        lParam1;
LONG_PTR        lParam2;

	ASSERT(pvRef);

	psrc = (Ac3FilterGraph *) pvRef;

	ASSERT(psrc->m_pMediaEvent);

	hGraphEvent     = 0;
	hr              = psrc->m_pMediaEvent->GetEventHandle(&hGraphEvent);

	ASSERT((HANDLE) hGraphEvent != INVALID_HANDLE_VALUE);
	ASSERT(SUCCEEDED(hr));
	ASSERT(ms_hEvent_die != INVALID_HANDLE_VALUE);

	ahWait[0] = ms_hEvent_die;
	ahWait[1] = (HANDLE) hGraphEvent;

	fDone = FALSE;

	while (!fDone)
	{
		if (WaitForMultipleObjects(2, ahWait, FALSE, INFINITE) == WAIT_OBJECT_0 + 1)
		{
			hr = psrc->m_pMediaEvent->GetEvent(&lEventCode, &lParam1, &lParam2,
							 A3D_AC3_EVENT_READ_TIMEOUT_MS);

			if (lEventCode == EC_COMPLETE)
			{
				WaitForSingleObject(psrc->m_hMutex, INFINITE);
				ResetPosition(psrc);
				ReleaseMutex(psrc->m_hMutex);
			}

			hr = psrc->m_pMediaEvent->FreeEventParams(lEventCode, lParam1, lParam2);
		}
		else
		{
			fDone = TRUE;
		}
	}

	ms_fThreadRunning = 0;
	SetEvent(ms_hEvent_die_ack);

	return (0);
}

/* =============================================================
// Ac3FilterGraph::ResetPosition()
// (RE) dbg:0x10033400
//
// Signal completion and restart looping playback, or clear the playing flag.
// =============================================================*/

void
Ac3FilterGraph::ResetPosition(Ac3FilterGraph *pThis)
{
	if (pThis->m_hPlayEvent)
		SetEvent(pThis->m_hPlayEvent);

	if (pThis->m_dwPlayFlags & DSBPLAY_LOOPING)
	{
		pThis->SetPlayPositionInternal(0);
		pThis->PlayInternal(pThis->m_dwPlayFlags);
		DBGSTR(" Reseting position\n");
	}
	else
	{
		pThis->m_fPlaying = 0;
	}
}

/* =============================================================
// Ac3FilterGraph::ReleaseGraph()
// (RE) dbg:0x10033ac0; rtl:0x100169e0
//
// Release the graph, filters and control interfaces.
// =============================================================*/

void
Ac3FilterGraph::ReleaseGraph(void)
{
	if (m_pMediaControl)
	{
		m_pMediaControl->Release();
		m_pMediaControl = NULL;
	}

	if (m_pMediaEvent)
	{
		m_pMediaEvent->Release();
		m_pMediaEvent = NULL;
	}

	if (m_pMediaSeeking)
	{
		m_pMediaSeeking->Release();
		m_pMediaSeeking = NULL;
	}

	if (m_pDecoderFilter)
	{
		m_pDecoderFilter->Release();
		m_pDecoderFilter = NULL;
	}

	if (m_pParserFilter)
	{
		m_pParserFilter->Release();
		m_pParserFilter = NULL;
	}

	if (m_pSourceFilter)
	{
		m_pSourceFilter->Release();
		m_pSourceFilter = NULL;
	}

	if (m_pGraphBuilder)
	{
		m_pGraphBuilder->Release();
		m_pGraphBuilder = NULL;
	}

	if (m_pMcAc3)
	{
		m_pMcAc3->Release();
		m_pMcAc3 = NULL;
	}
}

/* =============================================================
// Ac3FilterGraph::LoadFile()
// (RE) dbg:0x10033c50; rtl:0x10016a70
//
// Build an AC-3 playback graph, trying hardware decoding before software
// filters.
//
// Returns: The duration-query result on success; E_FAIL after graph cleanup on
//          failure.
// =============================================================*/

STDMETHODIMP
Ac3FilterGraph::LoadFile(char *szFile, DWORD dwFormat)
{
HRESULT                 hr;
LPWSTR                  pwszFile;
int                     cchWide;
IPin                   *pSourceOut;
IPin                   *pParserIn;
IPin                   *pParserOut;
IPin                   *pDecoderIn;
IAsyncReader           *pReader;
LONGLONG                llTotal;
LONGLONG                llAvailable;
DWORD                   dwChannels;
IBaseFilter            *pRenderer;
IPin                   *pRenderIn;
Ac3OptimalDecoder       optimal;
IUnknown               *pOptimal;
IPin                   *pOptimalIn;
IPin                   *pOptimalOut;
BOOL                    fConnected;
size_t                  cbFile;

	hr              = S_OK;
	pwszFile        = NULL;
	pSourceOut      = NULL;
	pParserIn       = NULL;
	pParserOut      = NULL;
	pDecoderIn      = NULL;

	ASSERT((dwFormat & ~(A3DSOURCE_FORMAT_AC3 | A3DSOURCE_STREAMING)) == 0);

	try
	{
		hr = CoCreateInstance(CLSID_FilterGraph_Ac3, NULL, CLSCTX_INPROC_SERVER,
				      IID_IGraphBuilder_Ac3, (void **) &m_pGraphBuilder);
		if (FAILED(hr))
			throw "No DirectShow installed.";

		if (!szFile)
			throw "Bad file name";

		if (!strlen(szFile))
			throw "Bad file name";

		cchWide = MultiByteToWideChar(CP_ACP, 0, szFile, -1, NULL, 0);
		if (!cchWide)
			throw "Bad file name";

		pwszFile = (LPWSTR) operator new(2 * cchWide);
		if (!pwszFile)
			throw "Fail memory allocation!";

		memset(pwszFile, 0, 2 * cchWide);
		if (!MultiByteToWideChar(CP_ACP, 0, szFile, -1, pwszFile, cchWide))
			throw "Bad file name";

		hr = m_pGraphBuilder->AddSourceFilter(pwszFile, L"File Source (Async.)",
						      &m_pSourceFilter);
		if (FAILED(hr))
			throw "File not found";

		pSourceOut = GetPin(m_pSourceFilter, PINDIR_OUTPUT);
		if (!pSourceOut)
			throw "Failed to enumerate pins.\n";

		pReader = NULL;
		hr      = pSourceOut->QueryInterface(IID_IAsyncReader_Ac3, (void **) &pReader);
		if (FAILED(hr) || !pReader)
			throw "Couldn't query for AsyncReader\n";

		llTotal         = 0;
		llAvailable     = 0;
		hr              = pReader->Length(&llTotal, &llAvailable);
		m_dwStreamSize  = (DWORD) llTotal;

		pReader->Release();

		if (!m_dwStreamSize)
			throw "Empty file";

		hr = CoCreateInstance(CLSID_Ac3Parser_Ac3, NULL, CLSCTX_INPROC_SERVER,
				      IID_IBaseFilter_Ac3, (void **) &m_pParserFilter);
		if (FAILED(hr) || !m_pParserFilter)
			throw "Couldn't create ac3 parser.\n";

		hr = m_pGraphBuilder->AddFilter(m_pParserFilter, L"ac3 parser");
		if (FAILED(hr))
			throw "Failed to add parser filter.\n";

		pParserIn = GetPin(m_pParserFilter, PINDIR_INPUT);
		if (!pParserIn)
			throw "Failed to enumerate parser input pin.\n";

		hr = m_pGraphBuilder->ConnectDirect(pSourceOut, pParserIn, NULL);
		if (FAILED(hr))
			throw "Failed to connect graph.\n";

		hr = CoCreateInstance(CLSID_McAc3Decoder_Ac3, NULL, CLSCTX_INPROC_SERVER,
				      IID_IBaseFilter_Ac3, (void **) &m_pDecoderFilter);

		try
		{
			if (FAILED(hr))
				throw "Couldn't create McAc3 filter.";

			hr = m_pGraphBuilder->AddFilter(m_pDecoderFilter, L"ac3 decoder");
			if (FAILED(hr))
				throw "Failed to add ac3 render filter.\n";

			pParserOut = GetPin(m_pParserFilter, PINDIR_OUTPUT);
			if (!pParserOut)
				throw "Failed to enumerate parser output pin.\n";

			pDecoderIn = GetPin(m_pDecoderFilter, PINDIR_INPUT);
			if (!pDecoderIn)
				throw "Failed to enumerate Ac3 render input pin.\n";

			hr = m_pGraphBuilder->ConnectDirect(pParserOut, pDecoderIn, NULL);
			if (FAILED(hr))
				throw "Fail to complete the filter graph connection.";

			m_pGraphBuilder->SetDefaultSyncSource();

			hr = m_pDecoderFilter->QueryInterface(IID_IMCAC3_Ac3,
							      (void **) &m_pMcAc3);
			if (FAILED(hr) || !m_pMcAc3)
				throw "Fail to get IMCAC3 interface";

			dwChannels      = 0;
			hr              = m_pMcAc3->GetHardwareChannels(&dwChannels);
			if (!SUCCEEDED(hr))
				throw "MCAC3 interface failure";

			if (!dwChannels)
				throw "No Ac3 hardware to play AC3";
		}
		/* (RE) Catch handler: dbg:0x100343f5. */
		catch (const char *pszError)
		{
			TRACE("Ac3FilterGraph::LoadFile - McAc3 error: %s\n",
			      pszError);

			if (pParserOut)
			{
				hr = m_pGraphBuilder->Disconnect(pParserOut);

				if (FAILED(hr))
					DBGSTR(
						"Ac3FilterGraph::LoadFile - Unable to disconnect pin during McAc3 cleanup.\n");
			}

			if (m_pMcAc3)
			{
				m_pMcAc3->Release();
				m_pMcAc3 = NULL;
			}

			if (m_pDecoderFilter)
			{
				m_pDecoderFilter->Release();
				m_pDecoderFilter = NULL;
			}

			if (pDecoderIn)
			{
				pDecoderIn->Release();
				pDecoderIn = NULL;
			}
		}

		if (!m_pMcAc3)
		{
			fConnected = FALSE;

			if (!pParserOut)
			{
				pParserOut = GetPin(m_pParserFilter, PINDIR_OUTPUT);
				if (!pParserOut)
					throw "Failed to enumerate parser output pin.\n";
			}

			pRenderer = NULL;
			hr = CoCreateInstance(CLSID_DSoundRender_Ac3, NULL, CLSCTX_INPROC_SERVER,
					      IID_IBaseFilter_Ac3, (void **) &pRenderer);
			if (FAILED(hr) || !pRenderer)
				throw "Couldn't create the audio renderer.\n";

			hr = m_pGraphBuilder->AddFilter(pRenderer, L"pcm audio renderer");
			if (FAILED(hr))
				throw "Failed to add PCM audio renderer to graph.\n";

			pRenderIn = GetPin(pRenderer, PINDIR_INPUT);
			if (!pRenderIn)
				throw "Failed to enumerate PCM audio renderer input pin.\n";

			optimal.pClsid          = NULL;
			optimal.dwMaxChannels   = 0;
			FindOptimalDecoder(&optimal, pParserOut, pRenderIn);

			if (optimal.dwMaxChannels)
			{
				pOptimal        = NULL;
				pOptimalIn      = NULL;
				pOptimalOut     = NULL;

				try
				{
					hr = CoCreateInstance(*optimal.pClsid, NULL,
							      CLSCTX_INPROC_SERVER,
							      IID_IBaseFilter_Ac3,
							      (void **) &pOptimal);
					if (FAILED(hr))
						throw "Unable to CoCreate optimal decoder.";

					hr = m_pGraphBuilder->AddFilter((IBaseFilter *) pOptimal,
									L"optimal ac3 decoder");
					if (FAILED(hr))
						throw "Unable to locate optimal decoder.";

					pOptimalIn = GetPin((IBaseFilter *) pOptimal, PINDIR_INPUT);
					if (!pOptimalIn)
						throw "Failed to enumerate best decoder input pin.\n";

					pOptimalOut = GetPin((IBaseFilter *) pOptimal, PINDIR_OUTPUT);
					if (!pOptimalOut)
						throw "Failed to enumerate best decoder input pin.\n";

					hr = m_pGraphBuilder->ConnectDirect(pParserOut, pOptimalIn, NULL);
					if (FAILED(hr))
						throw "Failed to connect parser output with best decoder input.\n";

					hr = m_pGraphBuilder->ConnectDirect(pOptimalOut, pRenderIn, NULL);
					if (FAILED(hr))
						throw "Failed to connect best decoder output with render input.\n";

					m_pGraphBuilder->SetDefaultSyncSource();
					fConnected = TRUE;
				}
				/* (RE) Catch handler: dbg:0x1003488b. */
				catch (const char *pszError)
				{
					TRACE("Ac3FilterGraph::LoadFile - %s\n\tHR: 0x%08X",
					      pszError, hr);

					if (pOptimal)
					{
						pOptimal->Release();
						pOptimal = NULL;
					}

					if (pOptimalIn)
					{
						pOptimalIn->Release();
						pOptimalIn = NULL;
					}

					if (pOptimalOut)
					{
						pOptimalOut->Release();
						pOptimalOut = NULL;
					}

					fConnected = FALSE;
				}
			}

			if (!fConnected)
			{
				DBGSTR(
					"Ac3FilterGraph::LoadFile - Using last ditch effort to locate a DirectShow filter.\n");

				hr = m_pGraphBuilder->Render(pParserOut);
				if (FAILED(hr))
					throw "Unable to complete filter graph";
			}
		}

		hr = m_pGraphBuilder->QueryInterface(IID_IMediaEvent_Ac3,
						     (void **) &m_pMediaEvent);
		if (FAILED(hr) || !m_pMediaEvent)
			throw "Failed query for IMediaEvent";

		hr = m_pGraphBuilder->QueryInterface(IID_IMediaSeeking_Ac3,
						     (void **) &m_pMediaSeeking);
		if (FAILED(hr) || !m_pMediaSeeking)
			throw "Failed query for Media seeking";

		hr = m_pGraphBuilder->QueryInterface(IID_IMediaControl_Ac3,
						     (void **) &m_pMediaControl);
		if (FAILED(hr) || !m_pMediaControl)
			throw "Failed  query for Media control";

		hr = m_pMediaSeeking->GetDuration(&m_llDuration);
		if (FAILED(hr))
			throw "Fail to get file duration";

		if (SUCCEEDED(hr))
		{
			cbFile          = strlen(szFile);
			m_pszFilename   = (char *) operator new(cbFile + 1);
			if (!m_pszFilename)
			{
				hr = A3DERROR_MEMORY_ALLOCATION;
				throw "Ac3FilterGraph::LoadFile - Unable to allocate memory to store filename.\n";
			}

			strcpy(m_pszFilename, szFile);
		}

		++ms_cInstances;
	}
	/* (RE) Catch handler: dbg:0x10034bd5. */
	catch (const char *pszError)
	{
		TRACE("Ac3FilterGraph::LoadFile error-- %s\n", pszError);

		m_dwStreamSize = 0;
		m_llDuration   = 0;

		ReleaseGraph();

		hr = E_FAIL;
	}

	if (pwszFile)
		operator delete(pwszFile);

	if (pSourceOut)
		pSourceOut->Release();

	if (pParserIn)
		pParserIn->Release();

	if (pParserOut)
		pParserOut->Release();

	if (pDecoderIn)
		pDecoderIn->Release();

	return (hr);
}

/* =============================================================
// Ac3FilterGraph::FindOptimalDecoder()
// (RE) dbg:0x10035370; rtl:0x10017570
//
// Keep the registered AC-3 decoder with the greatest PCM channel count.
// Original defects: rejected REGFILTER records leak and replaced records use
// operator delete despite CoTaskMem allocation.
//
// Returns: The last enumeration or candidate-query result. Setup and
//          enumeration failures throw const char *.
// =============================================================*/

HRESULT
Ac3FilterGraph::FindOptimalDecoder(Ac3OptimalDecoder *pBest,
				   IPin *pParserOut, IPin *pRenderIn)
{
IFilterMapper          *pMapper;
IEnumRegFilters        *pEnum;
REGFILTER              *pRegFilter;
ULONG                   cFetched;
DWORD                   dwChannels;
HRESULT                 hr;

	hr      = S_OK;
	pMapper = NULL;
	pEnum   = NULL;

	try
	{
		hr = CoCreateInstance(CLSID_FilterMapper_Ac3, NULL, CLSCTX_INPROC_SERVER,
				      IID_IFilterMapper_Ac3, (void **) &pMapper);
		if (FAILED(hr))
			throw "Failed to create IFilterMapper object";

		hr = pMapper->EnumMatchingFilters(&pEnum, MERIT_DO_NOT_USE, TRUE,
						  GUID_NULL_Ac3, MEDIASUBTYPE_DOLBY_AC3_Ac3,
						  FALSE, TRUE,
						  MEDIATYPE_Audio_Ac3, MEDIASUBTYPE_PCM_Ac3);
		if (FAILED(hr))
			throw "Couldn't get filter enumerator";

		hr = pEnum->Reset();
		if (FAILED(hr))
			throw "pEnum->Reset() failure";

		pRegFilter      = NULL;
		cFetched        = 0;
		dwChannels      = 0;

		do
		{
			hr = pEnum->Next(1, &pRegFilter, &cFetched);
			if (FAILED(hr))
				throw "pEnum->Next() failure";

			if (cFetched)
			{
				hr = GetMaxOutputChannels(pRegFilter, &dwChannels,
							  pParserOut, pRenderIn);

				if (SUCCEEDED(hr) && dwChannels)
				{
					if (pBest->pClsid)
					{
						if (dwChannels > pBest->dwMaxChannels)
						{
							operator delete(pBest->pClsid);
							pBest->pClsid = NULL;

							pBest->pClsid           = (CLSID *) pRegFilter;
							pBest->dwMaxChannels    = dwChannels;
						}
					}
					else
					{
						pBest->pClsid           = (CLSID *) pRegFilter;
						pBest->dwMaxChannels    = dwChannels;
					}
				}
			}
		}
		while (cFetched);
	}
	catch (const char *)
	{
		if (pMapper)
			pMapper->Release();

		if (pEnum)
			pEnum->Release();

		throw;
	}

	if (pMapper)
	{
		pMapper->Release();
		pMapper = NULL;
	}

	if (pEnum)
	{
		pEnum->Release();
		pEnum = NULL;
	}

	return (hr);
}

/* =============================================================
// Ac3FilterGraph::GetMaxOutputChannels()
// (RE) dbg:0x10035780; rtl:0x100177b0
//
// Measure a candidate decoder's PCM channel count using temporary graph
// connections.
//
// Returns: S_OK after probing; filter creation or input-pin enumeration errors.
// =============================================================*/

HRESULT
Ac3FilterGraph::GetMaxOutputChannels(REGFILTER *pRegFilter, DWORD *pdwMaxChannels,
				     IPin *pAc3Out, IPin *pPcmIn)
{
IBaseFilter			*pFilter;
IEnumPins			*pEnumPins;
IPin				*pInputPin;
std::list<IPin *>		inputPinList;
std::list<IPin *>::iterator	it;
HRESULT				hr;

	ASSERT(pRegFilter != 0 && !IsBadReadPtr(pRegFilter, sizeof(REGFILTER)));
	ASSERT(pdwMaxChannels != 0 && !IsBadReadPtr(pdwMaxChannels, sizeof(DWORD)));
	ASSERT(pAc3Out != 0 && !IsBadReadPtr(pAc3Out, sizeof(IPin)));
	ASSERT(pPcmIn != 0 && !IsBadReadPtr(pPcmIn, sizeof(IPin)));

	*pdwMaxChannels = 0;

	pFilter = NULL;
	hr = CoCreateInstance(pRegFilter->Clsid, NULL, CLSCTX_INPROC_SERVER,
			      IID_IBaseFilter_Ac3, (void **) &pFilter);
	if (FAILED(hr))
	{
		TRACE("Ac3FilterGraph::GetMaxOutputChannels - Failed CoCreate\n\tHR: 0x%08X\n", hr);

		return (hr);
	}

	pEnumPins       = NULL;
	hr              = pFilter->EnumPins(&pEnumPins);
	if (FAILED(hr))
	{
		pFilter->Release();
		pFilter = NULL;

		TRACE("Ac3FilterGraph::GetMaxOutputChannels - Failed EnumPins\n\tHR: 0x%08X\n", hr);

		return (hr);
	}

	hr = EnumInputPins(pEnumPins, &inputPinList);
	if (FAILED(hr))
	{
		pEnumPins->Release();
		pFilter->Release();

		TRACE("Ac3FilterGraph::GetMaxOutputChannels - Failed EnumInputPins\n\tHR: 0x%08X\n", hr);

		return (hr);
	}

	hr = m_pGraphBuilder->AddFilter(pFilter, NULL);
	ASSERT(SUCCEEDED(hr));

	for (it = inputPinList.begin(); it != inputPinList.end(); ++it)
	{
		pInputPin = *it;

		hr = m_pGraphBuilder->ConnectDirect(pAc3Out, pInputPin, NULL);
		if (FAILED(hr))
		{
			pInputPin->Release();
		}
		else
		{
			hr = EnumOutputPins(pEnumPins, pPcmIn, pdwMaxChannels);

			m_pGraphBuilder->Disconnect(pInputPin);
			m_pGraphBuilder->Disconnect(pAc3Out);

			pInputPin->Release();
		}
	}

	hr = m_pGraphBuilder->RemoveFilter(pFilter);
	ASSERT(SUCCEEDED(hr));

	pEnumPins->Release();
	pFilter->Release();

	return (S_OK);
}

/* =============================================================
// Ac3FilterGraph::EnumInputPins()
// (RE) dbg:0x10035d30; rtl:0x100179f0
//
// Append referenced input pins to the caller's list.
//
// Returns: S_OK; enumerator reset, Next or direction-query errors.
// =============================================================*/

HRESULT
Ac3FilterGraph::EnumInputPins(IEnumPins *pEnumPins, std::list<IPin *> *pInputPinList)
{
IPin           *pPin;
ULONG           cFetched;
PIN_DIRECTION   dir;
HRESULT         hr;

	ASSERT(pEnumPins != 0 && !IsBadReadPtr(pEnumPins, sizeof(IEnumPins)));
	ASSERT(pInputPinList != 0 && !IsBadReadPtr(pInputPinList, sizeof(std::list<IPin *>)));

	hr = pEnumPins->Reset();
	if (FAILED(hr))
	{
		TRACE("Ac3FilterGraph::EnumInputPins - Failed enum pin reset.\n\tHR: 0x%08X\n", hr);

		return (hr);
	}

	pPin            = NULL;
	cFetched        = 0;

	for (;;)
	{
		hr = pEnumPins->Next(1, &pPin, &cFetched);
		if (FAILED(hr))
		{
			TRACE("Ac3FilterGraph::EnumInputPins - Failed enum pin next.\n\tHR: 0x%08X\n", hr);

			return (hr);
		}

		if (!cFetched)
			return (S_OK);

		hr = pPin->QueryDirection(&dir);
		if (FAILED(hr))
		{
			pPin->Release();
			pPin = NULL;

			TRACE("Ac3FilterGraph::EnumInputPins - Failed pin direction query.\n\tHR: 0x%08X\n", hr);

			return (hr);
		}

		if (dir == PINDIR_OUTPUT)
		{
			pPin->Release();
			pPin = NULL;
		}
		else
		{
			pInputPinList->push_back(pPin);
		}
	}
}

/* =============================================================
// Ac3FilterGraph::EnumOutputPins()
// (RE) dbg:0x10035fb0; rtl:0x10017af0
//
// Measure the greatest channel count negotiated with the PCM renderer.
// The original overwrites c:\a3doutput.txt for each improved count.
//
// Returns: S_OK; enumerator reset, Next or direction-query errors.
// =============================================================*/

HRESULT
Ac3FilterGraph::EnumOutputPins(IEnumPins *pEnumPins, IPin *pPcmIn, DWORD *pdwMaxChannels)
{
IPin           *pPin;
ULONG           cFetched;
PIN_DIRECTION   dir;
AM_MEDIA_TYPE   mt;
WAVEFORMATEX   *pwfx;
FILE           *fp;
HRESULT         hr;

	ASSERT(pEnumPins != 0 && !IsBadReadPtr(pEnumPins, sizeof(IEnumPins)));
	ASSERT(pPcmIn != 0 && !IsBadReadPtr(pPcmIn, sizeof(IPin)));
	ASSERT(pdwMaxChannels != 0 && !IsBadReadPtr(pdwMaxChannels, sizeof(DWORD)));

	hr = pEnumPins->Reset();
	if (FAILED(hr))
	{
		TRACE("Ac3FilterGraph::EnumOutputPins - Failed enum pin reset.\n\tHR: 0x%08X\n", hr);

		return (hr);
	}

	pPin            = NULL;
	cFetched        = 0;

	for (;;)
	{
		hr = pEnumPins->Next(1, &pPin, &cFetched);
		if (FAILED(hr))
		{
			TRACE("Ac3FilterGraph::EnumOutputPins - Failed enum pin next.\n\tHR: 0x%08X\n", hr);

			return (hr);
		}

		if (!cFetched)
			return (S_OK);

		hr = pPin->QueryDirection(&dir);
		if (FAILED(hr))
		{
			pPin->Release();
			pPin = NULL;

			TRACE("Ac3FilterGraph::EnumOutputPins - Failed pin direction query.\n\tHR: 0x%08X\n", hr);

			return (hr);
		}

		if (dir == PINDIR_OUTPUT)
		{
			hr = m_pGraphBuilder->ConnectDirect(pPin, pPcmIn, NULL);
			if (SUCCEEDED(hr))
			{
				pPcmIn->ConnectionMediaType(&mt);

				if (mt.formattype == FORMAT_WaveFormatEx_Ac3)
				{
					pwfx = (WAVEFORMATEX *) mt.pbFormat;

					if (pwfx->nChannels > *pdwMaxChannels)
					{
						*pdwMaxChannels = pwfx->nChannels;

						TRACE("Ac3FilterGraph::EnumOutputPins - Working decoder with %d channel PCM output.\n",
						      *pdwMaxChannels);

						fp = fopen("c:\\a3doutput.txt", "wt");
						ASSERT(fp != 0);
						fprintf(fp,
							"Ac3FilterGraph::EnumOutputPins - Working decoder with %d channel PCM output.\n",
							*pdwMaxChannels);
						fclose(fp);
					}
				}

				FreeMediaType(&mt);
			}

			m_pGraphBuilder->Disconnect(pPcmIn);
			m_pGraphBuilder->Disconnect(pPin);
		}

		pPin->Release();
		pPin = NULL;
	}
}
