/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * splash.cpp
 *
 * Launches the external A3DSplsh.exe program using the A3D registry
 * settings. The launcher checks whether screen or audio output is
 * enabled, starts the program and handles the associated wait and window
 * restoration.
 *
 * Device startup calls the shared splash entry point. The splash
 * presentation itself belongs to the external executable; this file
 * contains the launch and synchronization behavior.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "splash.h"

#include <shellapi.h>
#include <stdio.h>
#include <string.h>

/* Wait after launching enabled splash output */
#define A3D_SPLASH_WAIT_MS 3500

/* =============================================================
// splash_screen()
// (RE) dbg:0x1003F340
//
// Launch A3DSplsh.exe, wait when audio or screen output is enabled and
// restore the caller window. Preserve the uninitialized path after a
// registry-open failure and the shared query-size value.
// =============================================================*/

void
splash::splash_screen(HWND hWnd)
{
DWORD   dwScreen;
DWORD   dwAudio;
LSTATUS lScreen;
LSTATUS lAudio;
LSTATUS lPath;
HKEY    hKey;
DWORD   cbData;
char    szPath[256];

	cbData   = sizeof(szPath);
	dwAudio  = 0;
	dwScreen = 0;

	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Aureal\\A3D", 0,
	                  KEY_READ, &hKey) == ERROR_SUCCESS)
	{
		lPath   = RegQueryValueExA(hKey, "SplashPath", NULL, NULL,
		                           (LPBYTE) szPath, &cbData);
		lAudio  = RegQueryValueExA(hKey, "SplashAudio", NULL, NULL,
		                           (LPBYTE) &dwAudio, &cbData);
		lScreen = RegQueryValueExA(hKey, "SplashScreen", NULL, NULL,
		                           (LPBYTE) &dwScreen, &cbData);

		if (hKey)
			RegCloseKey(hKey);
	}

	if (lPath != ERROR_SUCCESS)
		sprintf(szPath, "A3DSplsh.exe");
	else
		strcat(szPath, "\\A3DSplsh.exe");

	ShellExecuteA(hWnd, NULL, szPath, NULL, NULL, SW_SHOWNORMAL);

	if (dwAudio || dwScreen)
		Sleep(A3D_SPLASH_WAIT_MS);

	if (hWnd)
	{
		if (IsIconic(hWnd))
			ShowWindow(hWnd, SW_RESTORE);
	}
}
