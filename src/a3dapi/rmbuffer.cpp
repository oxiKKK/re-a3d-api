/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * rmbuffer.cpp
 *
 * Implements the control and property operations shared by
 * resource-manager buffers. ResManBuffer retains pending source controls,
 * applies mute state and sends changes when a DAL buffer is available.
 *
 * Property queries and updates are queued for callback execution, with
 * synchronization and saved records supporting deferred calls and later
 * replay. Static and streaming buffer classes build on this common state.
 * The file also contains the property-triggered unpacking and launch of
 * the embedded credits program.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "rmbuffer.h"
#include "PropertySetItem.h"
#include "resman.h"
#include "dalinfo.h"

#include <string.h>
#include <stdio.h>

static void	CarryReflectionAvailability(A3DCTRL_SRC_SUPER *lpDest,
				   const A3DCTRL_SRC_SUPER *lpcSrc);

/* (RE) Shared mute control block: rtl:0x10069d98; dbg:0x10155468. */

static A3DCTRL_SRC_SUPER	g_A3dCtrlMute;

/* (RE) First-construction clear flag: rtl:0x1005f920; dbg:0x101480e4. */
static BOOL			g_bClearA3dCtrlMute = TRUE;

/* =============================================================
// ResManBuffer::ResManBuffer()
// (RE) rtl:0x100287e0; dbg:0x1006b6e0
//
// Initialize mute state, the property queue and synchronization handles.
// Original handle-creation failures leave later members uninitialized.
// =============================================================*/

ResManBuffer::ResManBuffer(void) : m_listPropSetItems(A3D_LIST_DEFAULT_BLOCK_SIZE)
{
	m_bMuted = FALSE;

	if (g_bClearA3dCtrlMute)
	{
		memset(&g_A3dCtrlMute, 0, sizeof(A3DCTRL_SRC_SUPER));
		g_bClearA3dCtrlMute = FALSE;
	}

	m_hPropSetMutex = CreateMutex(NULL, FALSE, NULL);

	if (m_hPropSetMutex)
	{
		m_hPropSetCacheFlushed = CreateEvent(NULL, FALSE, FALSE, NULL);

		if (m_hPropSetCacheFlushed)
		{
			m_lpReflectionSuperCtrl = NULL;
			m_lpResMan              = NULL;
		}
	}
}

/* =============================================================
// ResManBuffer::~ResManBuffer()
// (RE) rtl:0x100288e0; dbg:0x1006b8a0
//
// Close synchronization handles and delete cached property records. The
// original tests handles against INVALID_HANDLE_VALUE despite null failures.
// =============================================================*/

ResManBuffer::~ResManBuffer(void)
{
POSITION                pos;
CPropertySetItem       *pItem;

	if (m_hPropSetMutex != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_hPropSetMutex);
		m_hPropSetMutex = INVALID_HANDLE_VALUE;
	}

	if (m_hPropSetCacheFlushed != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_hPropSetCacheFlushed);
		m_hPropSetCacheFlushed = INVALID_HANDLE_VALUE;
	}

	pos = m_listPropSetItems.GetHeadPosition();

	while (pos)
	{
		pItem = (CPropertySetItem *) m_listPropSetItems.GetNext(pos);

		delete pItem;
	}
}

/* =============================================================
// ResManBuffer::GetCtrlBuffers()
// (RE) rtl:0x100289d0; dbg:0x1006ba10
//
// Clear and return the buffer-owned storage for two super-control blocks.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
ResManBuffer::GetCtrlBuffers(LPVOID *lplpCtrlBuffers)
{
	memset(m_aCtrlBuffers, 0, sizeof(m_aCtrlBuffers));

	*lplpCtrlBuffers = m_aCtrlBuffers;

	return (S_OK);
}

/* =============================================================
// ResManBuffer::SetA3dSuperCtrl()
// (RE) rtl:0x100289f0; dbg:0x1006ba60
//
// Retain the caller's control-block pointer for pending and later unmute sends.
// The caller must keep the block alive while retained.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
ResManBuffer::SetA3dSuperCtrl(LPA3DCTRL_SRC_SUPER lpA3dCtrlSuper, DWORD dwSize)
{
	ASSERT(lpA3dCtrlSuper != 0);
	ASSERT(dwSize > 0);

	m_lpA3dCtrlSuper        = lpA3dCtrlSuper;
	m_lpA3dCtrlSuperPending = m_lpA3dCtrlSuper;
	m_dwA3dCtrlSuperSize    = dwSize;

	return (S_OK);
}

/* =============================================================
// ResManBuffer::SendPendingCtrl()
// (RE) rtl:0x10028a20; dbg:0x1006bb30
//
// Send pending controls to the DAL, preserving reflection availability or
// substituting mute controls that retain pitch, then consume the pending pointer.
// Original return type unresolved: Debug clears EAX; Retail leaves ReleaseMutex's result.
//
// Returns: S_OK; the DAL result is ignored.
// =============================================================*/

HRESULT
ResManBuffer::SendPendingCtrl(void)
{
	WaitForSingleObject(m_lpResMan->m_hSuperCtrlMutex, INFINITE);

	if (m_pDalBufferInfo && m_lpA3dCtrlSuperPending)
	{
		ASSERT(m_pDalBufferInfo != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo, sizeof(DalBufferInfo)));
		ASSERT(m_pDalBufferInfo->GetIDalBuffer() != 0 &&
		       !IsBadReadPtr(m_pDalBufferInfo->GetIDalBuffer(),
				     sizeof(IA3dDalBuffer)));

		if (m_bMuted)
		{
			g_A3dCtrlMute.fPitch = m_lpA3dCtrlSuperPending->fPitch;

			m_pDalBufferInfo->SetA3dSuperCtrl(&g_A3dCtrlMute,
							  m_dwA3dCtrlSuperSize);
		}
		else
		{
			if (m_lpReflectionSuperCtrl && m_lpReflectionSuperCtrl != m_lpA3dCtrlSuperPending)
				CarryReflectionAvailability(m_lpA3dCtrlSuperPending, m_lpReflectionSuperCtrl);

			m_pDalBufferInfo->SetA3dSuperCtrl(m_lpA3dCtrlSuperPending,
							  m_dwA3dCtrlSuperSize);
		}

		m_lpA3dCtrlSuperPending = NULL;
	}

	VERIFY(ReleaseMutex(m_lpResMan->m_hSuperCtrlMutex));

	return (S_OK);
}

/* =============================================================
// CarryReflectionAvailability()
// (RE) dbg:0x1006bd80; inlined in Retail SendPendingCtrl at rtl:0x10028A75
//
// Copy reflection availability flags between control blocks.
// =============================================================*/

static void
CarryReflectionAvailability(A3DCTRL_SRC_SUPER *lpDest, const A3DCTRL_SRC_SUPER *lpcSrc)
{
int	i;

	for (i = 0; i < A3D_MAX_SOURCE_REFLECTIONS; i++)
		lpDest->Reflections[i].bAvailable =
			lpcSrc->Reflections[i].bAvailable;
}

/* =============================================================
// ResManBuffer::MuteBuffer()
// (RE) rtl:0x10028ac0; dbg:0x1006bde0
//
// Enable muting and send any pending controls.
//
// Returns: SendPendingCtrl result.
// =============================================================*/

HRESULT
ResManBuffer::MuteBuffer(void)
{
	DBGSTR("ResManBuffer::MuteBuffer() Called.\n");

	m_bMuted = TRUE;

	return (SendPendingCtrl());
}

/* =============================================================
// ResManBuffer::UnMuteBuffer()
// (RE) rtl:0x10028ad0; dbg:0x1006be40
//
// Disable muting and resend the retained control block.
//
// Returns: SendPendingCtrl result.
// =============================================================*/

HRESULT
ResManBuffer::UnMuteBuffer(void)
{
	DBGSTR("ResManBuffer::UnMuteBuffer() Called.\n");

	m_bMuted = FALSE;
	m_lpA3dCtrlSuperPending = m_lpA3dCtrlSuper;

	return (SendPendingCtrl());
}

/* =============================================================
// ResManBuffer::QuerySupport()
// (RE) rtl:0x10028af0; dbg:0x1006beb0
//
// Queue a support query and wait for the timer callback's result.
// Original allocation or enqueue failure can leave a null list position
// for the subsequent result lookup.
//
// Returns: Recorded HRESULT; E_FAIL if the completion or result-mutex wait
//          fails.
// =============================================================*/

STDMETHODIMP
ResManBuffer::QuerySupport(REFGUID rguidPropSet, ULONG ulId,
			   PULONG pulTypeSupport)
{
CPropertySetItem       *pItem;
POSITION                pos;
HRESULT                 hr;

	hr      = E_FAIL;
	pos     = NULL;

	pItem = new CPropertySetItem;

	if (pItem != NULL)
	{
		if (WaitForSingleObject(m_hPropSetMutex, INFINITE) == WAIT_OBJECT_0)
		{
			pItem->Create(rguidPropSet, ulId, pulTypeSupport);
			pos = m_listPropSetItems.AddTail(pItem);
		}

		VERIFY(ReleaseMutex(m_hPropSetMutex));
	}

	if (WaitForSingleObject(m_hPropSetCacheFlushed, INFINITE) != WAIT_OBJECT_0)
	{
		DBGSTR("ResManBuffer::QuerySupport() - TimerCallback failed to signal Property Set event.\n");
		ASSERT(0);
		return (E_FAIL);
	}

	if (WaitForSingleObject(m_hPropSetMutex, INFINITE) == WAIT_OBJECT_0)
	{
		pItem = (CPropertySetItem *) m_listPropSetItems.GetAt(pos);

		ASSERT(pItem != 0 &&
		       !IsBadReadPtr(pItem, sizeof(CPropertySetItem)));

		*pulTypeSupport = pItem->m_ulTypeSupport;
		hr = pItem->m_hrCall;

		m_listPropSetItems.RemoveAt(pos);
		delete pItem;
	}

	VERIFY(ReleaseMutex(m_hPropSetMutex));
	VERIFY(ResetEvent(m_hPropSetCacheFlushed));

	return (hr);
}

/* =============================================================
// ResManBuffer::Get()
// (RE) rtl:0x10028d40; dbg:0x1006c260
//
// Queue a property read and wait for its result. The original copies the
// returned byte count without bounding it to the caller's property buffer.
//
// Returns: Recorded HRESULT; E_INVALIDARG for GUID_NULL; E_POINTER for missing
//          data with nonzero size or a null byte-count output; E_FAIL if a
//          result wait fails.
// =============================================================*/

STDMETHODIMP
ResManBuffer::Get(REFGUID rguidPropSet, ULONG ulId,
		  LPVOID pInstanceData, ULONG cbInstanceData,
		  LPVOID pPropertyData, ULONG cbPropertyData,
		  PULONG pulBytesReturned)
{
CPropertySetItem       *pProcessedItem;
CPropertySetItem       *pItem;
POSITION                pos;
HRESULT                 hr;

	if (IsEqualGUID(rguidPropSet, GUID_NULL))
	{
		DBGSTR("ResManBuffer::Get() - NULL GUID passed in.\n");
		return (E_INVALIDARG);
	}

	if (cbInstanceData != 0 && pInstanceData == NULL)
	{
		DBGSTR("ResManBuffer::SetPropertySet() - Instance Data is NULL and size is not zero.\n");
		return (E_POINTER);
	}

	if (cbPropertyData != 0 && pPropertyData == NULL)
	{
		DBGSTR("ResManBuffer::SetPropertySet() - Property Data is NULL and size is not zero.\n");
		return (E_POINTER);
	}

	if (pulBytesReturned == NULL)
	{
		DBGSTR("ResManBuffer::Get() - pulBytesReturned is NULL.\n");
		return (E_POINTER);
	}

	hr      = E_FAIL;
	pos     = NULL;

	pItem = new CPropertySetItem;

	if (pItem != NULL)
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
	{
		DBGSTR("ResManBuffer::Get() - TimerCallback failed to signal Property Set event.\n");
		ASSERT(0);
		return (E_FAIL);
	}

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
// ResManBuffer::Set()
// (RE) rtl:0x10029070; dbg:0x1006c7e0
//
// Queue a property write, waiting and copying data back when
// A3DPROPSET_WAITFORRESULTS is set.
//
// Returns: S_OK without waiting; otherwise the recorded HRESULT or E_FAIL if a
//          result wait fails. E_INVALIDARG for GUID_NULL; E_POINTER for missing
//          nonempty data.
// =============================================================*/

STDMETHODIMP
ResManBuffer::Set(REFGUID rguidPropSet, ULONG ulId,
		  LPVOID pInstanceData, ULONG cbInstanceData,
		  LPVOID pPropertyData, ULONG cbPropertyData,
		  DWORD dwFlags)
{
CPropertySetItem       *pProcessedItem;
CPropertySetItem       *pItem;
POSITION                pos;
DWORD                   dwWaitForResults;
HRESULT                 hr;

	if (IsEqualGUID(rguidPropSet, GUID_NULL))
	{
		DBGSTR("ResManBuffer::SetPropertySet() - NULL GUID passed in.\n");
		return (E_INVALIDARG);
	}

	if (cbInstanceData != 0 && pInstanceData == NULL)
	{
		DBGSTR("ResManBuffer::SetPropertySet() - Instance Data is NULL and size is not zero.\n");
		return (E_POINTER);
	}

	if (cbPropertyData != 0 && pPropertyData == NULL)
	{
		DBGSTR("ResManBuffer::SetPropertySet() - Property Data is NULL and size is not zero.\n");
		return (E_POINTER);
	}

	pos                     = NULL;
	dwWaitForResults        = dwFlags & A3DPROPSET_WAITFORRESULTS;

	pItem = new CPropertySetItem;

	if (pItem != NULL)
	{
		if (WaitForSingleObject(m_hPropSetMutex, INFINITE) == WAIT_OBJECT_0)
		{
			pItem->AddSet(rguidPropSet, ulId, pInstanceData, cbInstanceData,
				      pPropertyData, cbPropertyData, dwWaitForResults, 1);
			pos = m_listPropSetItems.AddTail(pItem);
		}

		VERIFY(ReleaseMutex(m_hPropSetMutex));
	}

	if (!dwWaitForResults)
		return (S_OK);

	hr = E_FAIL;

	if (WaitForSingleObject(m_hPropSetCacheFlushed, INFINITE) != WAIT_OBJECT_0)
	{
		DBGSTR("ResManBuffer::SetPropertySet() - TimerCallback failed to signal Property Set event.\n");
		ASSERT(0);
		return (E_FAIL);
	}

	if (WaitForSingleObject(m_hPropSetMutex, INFINITE) == WAIT_OBJECT_0)
	{
		pProcessedItem = (CPropertySetItem *) m_listPropSetItems.GetAt(pos);

		ASSERT(pProcessedItem != 0 &&
		       !IsBadReadPtr(pProcessedItem, sizeof(CPropertySetItem)));

		memcpy(pInstanceData, pProcessedItem->m_pInstanceData, cbInstanceData);
		memcpy(pPropertyData, pProcessedItem->m_pPropertyData, cbPropertyData);
		hr = pProcessedItem->m_hrCall;

		m_listPropSetItems.RemoveAt(pos);
		delete pProcessedItem;
	}

	VERIFY(ReleaseMutex(m_hPropSetMutex));
	VERIFY(ResetEvent(m_hPropSetCacheFlushed));

	return (hr);
}

/* =============================================================
// ResManBuffer::AddInitialStateParameters()
// (RE) rtl:0x10029390; dbg:0x1006cd40
//
// Reject initial-state parameters on a buffer, as in the original.
//
// Returns: E_NOTIMPL.
// =============================================================*/

STDMETHODIMP
ResManBuffer::AddInitialStateParameters(REFGUID rguidPropSet, ULONG ulId,
					LPVOID pInstanceData,
					ULONG cbInstanceData,
					LPVOID pPropertyData,
					ULONG cbPropertyData)
{
	return (E_NOTIMPL);
}

/* =============================================================
// ResManBuffer::HasUnappliedPropertySet()
// (RE) rtl:0x100293a0; dbg:0x1006cd60
//
// Check for an unapplied property record. The original releases the mutex
// even if its acquisition failed.
//
// Returns:
//   TRUE   when an unapplied record is found
//   FALSE  otherwise or if the wait fails
// =============================================================*/

BOOL
ResManBuffer::HasUnappliedPropertySet(void)
{
POSITION                pos;
CPropertySetItem       *pItem;
BOOL                    bPending;

	bPending = FALSE;

	if (WaitForSingleObject(m_hPropSetMutex, INFINITE) == WAIT_OBJECT_0)
	{
		pos = m_listPropSetItems.GetHeadPosition();

		while (pos && !bPending)
		{
			pItem = (CPropertySetItem *)
				m_listPropSetItems.GetNext(pos);

			ASSERT(pItem != 0 &&
			       !IsBadReadPtr(pItem, sizeof(CPropertySetItem)));

			if (!pItem->m_dwApplied)
				bPending = TRUE;
		}
	}

	VERIFY(ReleaseMutex(m_hPropSetMutex));

	return (bPending);
}

#ifdef A3D_FIXES
/* (RE) Optional 678 credits program: 678:0x1005f9e4, 0x4293 packed bytes.
   Run SceneRooms.exe --dll C:\path\to\a3dapi.dll with Calculator and Explorer
   titled "Calculator" and "My Computer" open, then close SceneRooms. Click the
   tray icon within 10 seconds. White credits scroll on black until closed.

   C decompilation without CRT. Addresses below refer to the unpacked
   678:credits.exe at imagebase 0x00400000; helper/global names are reconstructed.
   Resource 103 is a dialog titled "Aureal A3D 3.0 Development Team"; WinMain
   changes its caption. Named icon resource: IDI_A3DDEV01.
   Original defects retained: busy-wait scrolling thread, no join before GDI
   cleanup, DeleteObject on a DC, EndDialog on a modeless dialog, leaked icon.

   #include <windows.h>
   #include <shellapi.h>

   static int g_y = 170;
   static BOOL g_bRunning = TRUE;
   static HINSTANCE g_hInstance;
   static HWND g_hWnd;
   static HICON g_hIcon;
   static UINT_PTR g_idTimer;
   static DWORD g_dwTicks;
   static HANDLE g_hThread;
   static HBITMAP g_hBitmap;
   static HGDIOBJ g_hOldBitmap;
   static HDC g_hMemoryDC;
   static int g_nLineHeight;
   static BOOL g_bShown;
   static RECT g_rect;

   static const char *g_apszCredits[] = {
       "The Aureal A3D 3.0 Product Team",
       "",
       "Software Engineering:",
       "Terry Blanchard",
       "Nam Do",
       "Sherman Mui",
       "Andrew Wheeler",
       "Rob Bishop",
       "",
       "Developer Relations:",
       "Suneil Mishra",
       "Micah Mason",
       "Scott Etherton",
       "",
       "Evangelism:",
       "David Gasior",
       "Skip McIlvaine",
       "",
       "Quality Assurance:",
       "Joe Longworth",
       "",
       "Special Thanks To:",
       "Nancy Blanchard",
       "Lisa Do",
       "Zoe Bishop",
       "",
       "We Couldn't Have Done it Without:",
       "Coca-Cola",
       "Mountain Dew",
       "Pepsi",
       "French Vanilla Coffee",
       "Multi-Player A3D Titles",
       "DinnerMan",
       "Canadian Beer",
       "Golf",
   };

   static BOOL NotifyIcon(HWND hWnd, DWORD message, UINT id, HICON icon)
   {
       NOTIFYICONDATAA nid;
       nid.cbSize               = NOTIFYICONDATAA_V1_SIZE;
       nid.hWnd                 = hWnd;
       nid.uID                  = id;
       nid.uFlags               = NIF_MESSAGE | NIF_ICON;
       nid.uCallbackMessage     = WM_APP + 100;
       nid.hIcon                = icon;
       return Shell_NotifyIconA(message, &nid);
   }

   static BOOL DrawItem(const DRAWITEMSTRUCT *item)
   {
       if (g_hIcon)
           DrawIconEx(item->hDC, item->rcItem.left, item->rcItem.top,
               g_hIcon, 16, 16, 0, NULL, DI_NORMAL);
       return TRUE;
   }

   static DWORD WINAPI ScrollCredits(void *parameter)
   {
       DWORD next = GetTickCount();
       while (g_bRunning) {
           if (GetTickCount() >= next) {
               HDC dc = GetDC(g_hWnd);
               BitBlt(dc, 0, g_y, 256, 170 - g_y, g_hMemoryDC, 0, 0, SRCCOPY);
               ReleaseDC(g_hWnd, dc);
               if (--g_y < -468)
                   g_y = 170;
               next += 50;
           }
       }
       return 0;
   }

   static void CloseCredits(HWND hWnd)
   {
       g_bRunning = FALSE;
       NotifyIcon(hWnd, NIM_DELETE, 1000, NULL);
       DeleteObject(SelectObject(g_hMemoryDC, g_hOldBitmap));
       DeleteObject(g_hMemoryDC);
       CloseHandle(g_hThread);
       EndDialog(hWnd, 0);
       PostQuitMessage(0);
   }

   static INT_PTR CALLBACK DialogProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
   {
       switch (msg) {
       case WM_PAINT: {
           PAINTSTRUCT ps;
           HDC dc = BeginPaint(hWnd, &ps);
           if (!IsRectEmpty(&ps.rcPaint)) {
               SetBkColor(dc, RGB(0, 0, 0));
               ExtTextOutA(dc, 0, 0, ETO_OPAQUE, &ps.rcPaint, NULL, 0, NULL);
           }
           EndPaint(hWnd, &ps);
           return TRUE;
       }
       case WM_ERASEBKGND:
           return TRUE;
       case WM_DRAWITEM:
           if (g_idTimer)
               return DrawItem((const DRAWITEMSTRUCT *)lp);
           break;
       case WM_INITDIALOG: {
           HDC          dc;
           TEXTMETRICA  tm;
           unsigned     i;
           int y = 0;
           NotifyIcon(hWnd, NIM_ADD, 1000, g_hIcon);
           g_dwTicks    = 0;
           g_idTimer    = SetTimer(hWnd, 1, 1000, NULL);
           dc           = GetDC(hWnd);
           g_hBitmap    = CreateCompatibleBitmap(dc, 256, 468);
           ReleaseDC(hWnd, dc);
           g_hMemoryDC  = CreateCompatibleDC(NULL);
           g_hOldBitmap = SelectObject(g_hMemoryDC, g_hBitmap);
           SelectObject(g_hMemoryDC, GetStockObject(ANSI_VAR_FONT));
           SetTextColor(g_hMemoryDC, RGB(255, 255, 255));
           SetTextAlign(g_hMemoryDC, TA_CENTER);
           GetTextMetricsA(g_hMemoryDC, &tm);
           g_nLineHeight        = tm.tmHeight + tm.tmExternalLeading;
           g_rect.left          = g_rect.top = 0;
           g_rect.right         = 256;
           g_rect.bottom        = 468;
           SetBkColor(g_hMemoryDC, RGB(0, 0, 0));
           ExtTextOutA(g_hMemoryDC, 0, 0, ETO_OPAQUE, &g_rect, NULL, 0, NULL);
           for (i = 0; i < sizeof(g_apszCredits) / sizeof(g_apszCredits[0]); ++i) {
               ExtTextOutA(g_hMemoryDC, 128, y, ETO_CLIPPED, &g_rect,
                   g_apszCredits[i], lstrlenA(g_apszCredits[i]), NULL);
               y += g_nLineHeight;
           }
           return TRUE;
       }
       case WM_COMMAND:
           if (LOWORD(wp) == IDCANCEL)
               CloseCredits(hWnd);
           return TRUE;
       case WM_TIMER:
           if (g_idTimer) {
               ++g_dwTicks;
               if (!g_bShown && g_dwTicks >= 10)
                   CloseCredits(hWnd);
           }
           break;
       case WM_APP + 100:
           if (lp == WM_LBUTTONDOWN || lp == WM_RBUTTONDOWN) {
               g_bShown = TRUE;
               if (!g_hThread) {
                   DWORD threadId = 0;
                   g_hThread = CreateThread(NULL, 0, ScrollCredits, NULL, 0, &threadId);
               }
               ShowWindow(hWnd, SW_SHOW);
               SetForegroundWindow(hWnd);
           }
           return TRUE;
       default:
           return FALSE;
       }
       return TRUE;
   }

   int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show)
   {
       MSG msg;
       if (FindWindowA(NULL, "Aureal A3D 3.0 Team"))
           return 0;
       g_hInstance      = instance;
       g_hIcon          = (HICON)LoadImageA(instance, "IDI_A3DDEV01", IMAGE_ICON, 16, 16, 0);
       g_hWnd           = CreateDialogParamA(instance, MAKEINTRESOURCEA(103), NULL, DialogProc, 0);
       SetWindowTextA(g_hWnd, "Aureal A3D 3.0 Team");
       while (GetMessageA(&msg, NULL, 0, 0)) {
           TranslateMessage(&msg);
           DispatchMessageA(&msg);
       }
       return 1;
   }
 */
static BYTE g_abNewEasterEggProgram[0x4293] =
{
#include "easteregg_new.inc"
};
#endif
/* (RE) Packed credits program: rtl:0x1005f924; dbg:0x101480e8, 0x36CE bytes. */
/* To trigger on modern Windows:
   1. Run SceneRooms.exe --dll C:\path\to\a3dapi.dll.
   2. Open Calculator and Explorer with titles "Calculator" and "My Computer"
      (rename "This PC" accordingly), then close SceneRooms.
   3. Click the A3D tray icon within 15 seconds; credits remain for 60 seconds.
   The unpacked A3D*.tmp file remains in TEMP.

   Dialog resource 103, (RE) rtl:credits.exe:0x004085b8:

   +---------------------------------------------------------+
   | Aureal A3D 2.0                                          |
   |                                                         |
   |       Aureal A3D 2.0 Software Development Kit           |
   |                                                         |
   | Engineering                                             |
   |   Alan Gerrard               Nam Do                     |
   |   Andrew Wheeler             Terry Blanchard            |
   |   Mark Pereira                                          |
   |                                                         |
   | Developer Support                                       |
   |   Micah Mason                Suneil Mishra              |
   |                                                         |
   | Evangelized by                                          |
   |   David Gasior               Skip McIlvaine             |
   |                                                         |
   | Grudging respect for                                    |
   |   Mike Taylor                Toni Schneider             |
   |                                                         |
   | Special thanks to                                       |
   |   Eleanor Gerrard            Nancy Blanchard            |
   |   Heather Shedd              Paula Koehler              |
   |   Lisa Do                                               |
   +---------------------------------------------------------+

   C decompilation without CRT; helper/global names are reconstructed.
   rtl:credits.exe denotes the unpacked 677 program at imagebase 0x00400000;
   Debug and DebugViewer embed identical bytes. Original defects retained:
   EndDialog on a modeless dialog, and no destruction of the loaded icon.

   =============================================================================

   #include <windows.h>
   #include <shellapi.h>

   static DWORD g_dwTimeout = 150;
   static HINSTANCE g_hInstance;
   static HICON g_hIcon;
   static UINT_PTR g_idTimer;
   static DWORD g_dwTicks;

   static BOOL
   NotifyIcon(HWND hWnd, DWORD dwMessage, UINT uID, HICON hIcon)
   {
   NOTIFYICONDATAA nid;

    nid.cbSize                  = NOTIFYICONDATAA_V1_SIZE;
    nid.hWnd                    = hWnd;
    nid.uID                     = uID;
    nid.uFlags                  = NIF_MESSAGE | NIF_ICON;
    nid.uCallbackMessage        = WM_APP + 100;
    nid.hIcon                   = hIcon;
    return (Shell_NotifyIconA(dwMessage, &nid));
   }

   static BOOL
   DrawItem(const DRAWITEMSTRUCT *pItem)
   {
    if (g_hIcon)
        DrawIconEx(pItem->hDC, pItem->rcItem.left, pItem->rcItem.top,
            g_hIcon, 16, 16, 0, NULL, DI_NORMAL);
    return (TRUE);
   }

   static INT_PTR CALLBACK
   DialogProc(HWND hWnd, UINT uMessage, WPARAM wParam, LPARAM lParam)
   {
    switch (uMessage)
    {
    case WM_INITDIALOG:
        NotifyIcon(hWnd, NIM_ADD, 1000, g_hIcon);
        g_dwTicks       = 0;
        g_dwTimeout     = 150;
        g_idTimer       = SetTimer(hWnd, 1, 100, NULL);
        return (TRUE);

    case WM_APP + 100:
        if (lParam == WM_LBUTTONDOWN || lParam == WM_RBUTTONDOWN)
        {
            g_dwTicks   = 0;
            g_dwTimeout = 600;
            ShowWindow(hWnd, SW_SHOW);
            SetForegroundWindow(hWnd);
        }
        return (TRUE);

    case WM_TIMER:
        if (!g_idTimer || ++g_dwTicks != g_dwTimeout)
            return (TRUE);
        goto Close;

    case WM_COMMAND:
        if (LOWORD(wParam) != IDCANCEL)
            return (TRUE);
   Close:
        g_idTimer = 0;
        KillTimer(hWnd, 1);
        NotifyIcon(hWnd, NIM_DELETE, 1000, NULL);

        EndDialog(hWnd, 0);
        PostQuitMessage(0);
        return (TRUE);

    case WM_DRAWITEM:
        if (g_idTimer)
            return (DrawItem((const DRAWITEMSTRUCT *)lParam));
        return (TRUE);
    }
    return (FALSE);
   }

   int WINAPI
   WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine,
    int nShowCmd)
   {
   MSG msg;

    if (FindWindowA(NULL, "Aureal A3D 2.0"))
        return (0);
    g_hInstance = hInstance;
    g_hIcon = (HICON)LoadImageA(hInstance, "IDI_A3DDEV01", IMAGE_ICON,
        16, 16, 0);
    CreateDialogParamA(hInstance, MAKEINTRESOURCEA(103), NULL, DialogProc, 0);
    while (GetMessageA(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (1);
   }
 */
static BYTE	g_abEasterEggProgram[0x36CE] =
{
#include "easteregg.inc"
};

/* =============================================================
// CheckEasterEgg()
// (RE) rtl:0x100293f0; dbg:0x1006ceb0
//
// Unpack the embedded program to a temporary file and execute it when the
// Calculator and My Computer windows exist.
// =============================================================*/

/* Zero-run escape length  */
#define A3D_EASTER_EGG_ZERO_RUN_PREFIX 3

void
CheckEasterEgg(LPCSTR lpszMessage)
{
CHAR    szTempPath[MAX_PATH];
CHAR    szTempFile[MAX_PATH];
FILE   *pf;
UINT    i;
BYTE    cRun;
int     c;
#if defined(A3D_FIXES)
BYTE   *pbProgram;
UINT    cbProgram;

	pbProgram = A3dGetConfig().bUseNewCredits ?
		g_abNewEasterEggProgram : g_abEasterEggProgram;
	cbProgram = A3dGetConfig().bUseNewCredits ?
		sizeof(g_abNewEasterEggProgram) : sizeof(g_abEasterEggProgram);
#endif

	if (!FindWindowA(NULL, "Calculator"))
		return;

#if defined(A3D_FIXES)
	if (!A3dGetConfig().bRelaxCreditsActivation)
#endif
		if (!FindWindowA(NULL, "My Computer"))
			return;

	if (!GetTempPathA(MAX_PATH, szTempPath))
		return;

	if (!GetTempFileNameA(szTempPath, "A3D", 0, szTempFile))
		return;

	pf = fopen(szTempFile, "wb");

	if (!pf)
		return;

#if defined(A3D_FIXES)
#define A3D_CREDITS_SIZE  cbProgram
#define A3D_CREDITS_DATA pbProgram
#else
#define A3D_CREDITS_SIZE  sizeof(g_abEasterEggProgram)
#define A3D_CREDITS_DATA g_abEasterEggProgram
#endif
	i = 0;

	while (i < A3D_CREDITS_SIZE)
	{
		c = A3D_CREDITS_DATA[i++];

		fputc(c, pf);

		if (c)
			continue;

		for (cRun = 1; cRun < A3D_EASTER_EGG_ZERO_RUN_PREFIX; cRun++)
		{
			if (i >= A3D_CREDITS_SIZE)
				break;

			c = A3D_CREDITS_DATA[i++];

			fputc(c, pf);

			if (c)
				break;
		}

		if (cRun == A3D_EASTER_EGG_ZERO_RUN_PREFIX && i < A3D_CREDITS_SIZE)
		{
			c = A3D_CREDITS_DATA[i++];

			for (cRun = 0; cRun < c; cRun++)
				fputc(0, pf);
		}
	}

	fclose(pf);

	WinExec(szTempFile, SW_SHOW);
}

#undef A3D_CREDITS_SIZE
#undef A3D_CREDITS_DATA
