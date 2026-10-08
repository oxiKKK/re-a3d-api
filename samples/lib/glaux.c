/*
 * glaux.c - the eight glaux entry points the A3D 2.0 samples call
 *
 * Project compatibility shim; see GL/glaux.h. auxInitWindow makes the
 * window foreground so the samples' GetForegroundWindow call obtains the
 * correct cooperative-level HWND. Idle callbacks update audio; WM_PAINT
 * invokes the display callback. Escape exits.
*/

#include <GL/glaux.h>
#include <stdio.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------- */

static int	    g_nDisplayMode = AUX_RGB | AUX_SINGLE;
static int	    g_nX	   = 0;
static int	    g_nY	   = 0;
static int	    g_nWidth	   = 100;
static int	    g_nHeight	   = 100;

static HWND	    g_hWnd;
static HDC	    g_hDC;
static HGLRC	    g_hRC;
static int	    g_fQuit;

static AUXIDLEPROC    g_pfnIdle;
static AUXMAINPROC    g_pfnDisplay;
static AUXRESHAPEPROC g_pfnReshape;
static AUXMOUSEPROC   g_pfnLeftDown;
static AUXMOUSEPROC   g_pfnRightDown;

static const char g_szClass[] = "AuxWindowClass";

/* -------------------------------------------------------------------------- */

static LRESULT CALLBACK
AuxWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
AUX_EVENTREC ev;

	switch (uMsg)
	{
	case WM_SIZE:
		if (g_pfnReshape)
			g_pfnReshape((GLsizei) LOWORD(lParam),
				     (GLsizei) HIWORD(lParam));
		return (0);

	case WM_CHAR:
		if (wParam == VK_ESCAPE)
			g_fQuit = 1;
		return (0);

	case WM_PAINT:
		/*
		 * Dispatch display callbacks from WM_PAINT. The samples' idle callbacks
		 * already call update_scene; calling display from the idle loop would
		 * submit each audio frame twice.
		*/
		if (g_pfnDisplay)
			g_pfnDisplay();

		ValidateRect(hWnd, NULL);
		return (0);

	case WM_LBUTTONDOWN:
		if (g_pfnLeftDown)
		{
			ev.event   = AUX_MOUSEDOWN;
			ev.data[AUX_MOUSEX]      = (GLint) LOWORD(lParam);
			ev.data[AUX_MOUSEY]      = (GLint) HIWORD(lParam);
			ev.data[AUX_MOUSESTATUS] = AUX_LEFTBUTTON;
			ev.data[3]               = 0;

			g_pfnLeftDown(&ev);
		}
		return (0);

	case WM_RBUTTONDOWN:
		if (g_pfnRightDown)
		{
			ev.event   = AUX_MOUSEDOWN;
			ev.data[AUX_MOUSEX]      = (GLint) LOWORD(lParam);
			ev.data[AUX_MOUSEY]      = (GLint) HIWORD(lParam);
			ev.data[AUX_MOUSESTATUS] = AUX_RIGHTBUTTON;
			ev.data[3]               = 0;

			g_pfnRightDown(&ev);
		}
		return (0);

	case WM_CLOSE:
	case WM_DESTROY:
		g_fQuit = 1;
		return (0);
	}

	return (DefWindowProcA(hWnd, uMsg, wParam, lParam));
}

/* -------------------------------------------------------------------------- */

/*
 * Idempotent cleanup for atexit and auxMainLoop. Audio initialization
 * failure can return from main before the loop; leaving a GL context
 * current caused ICD process-detach failure 0xC0000409
 * (STATUS_STACK_BUFFER_OVERRUN), also reproduced by a shim-only test.
*/

static void
AuxShutdown(void)
{
	if (g_hRC)
	{
		wglMakeCurrent(NULL, NULL);
		wglDeleteContext(g_hRC);
		g_hRC = NULL;
	}

	if (g_hDC)
	{
		ReleaseDC(g_hWnd, g_hDC);
		g_hDC = NULL;
	}

	if (g_hWnd)
	{
		DestroyWindow(g_hWnd);
		g_hWnd = NULL;
	}
}

void APIENTRY
auxInitDisplayMode(GLenum mode)
{
	g_nDisplayMode = (int) mode;
}

void APIENTRY
auxInitPosition(int x, int y, int width, int height)
{
	g_nX	  = x;
	g_nY	  = y;
	g_nWidth  = width;
	g_nHeight = height;
}

GLenum APIENTRY
auxInitWindow(LPCSTR title)
{
WNDCLASSA		wc;
PIXELFORMATDESCRIPTOR	pfd;
RECT			rc;
int			nFormat;

	ZeroMemory(&wc, sizeof(wc));

	wc.style	 = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc	 = AuxWndProc;
	wc.hInstance	 = GetModuleHandleA(NULL);
	wc.hCursor	 = LoadCursorA(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH) (COLOR_WINDOW + 1);
	wc.lpszClassName = g_szClass;

	RegisterClassA(&wc);

	/*
	 * glaux takes the size as the client area, so the window has to be
	 * grown by whatever the frame costs.
	*/

	rc.left	  = g_nX;
	rc.top	  = g_nY;
	rc.right  = g_nX + g_nWidth;
	rc.bottom = g_nY + g_nHeight;

	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

	g_hWnd = CreateWindowExA(0, g_szClass, title, WS_OVERLAPPEDWINDOW,
				 rc.left, rc.top,
				 rc.right - rc.left, rc.bottom - rc.top,
				 NULL, NULL, wc.hInstance, NULL);

	if (!g_hWnd)
		return (GL_FALSE);

	g_hDC = GetDC(g_hWnd);

	ZeroMemory(&pfd, sizeof(pfd));

	pfd.nSize	 = sizeof(pfd);
	pfd.nVersion	 = 1;
	pfd.dwFlags	 = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
	pfd.iPixelType	 = PFD_TYPE_RGBA;
	pfd.cColorBits	 = 24;
	pfd.cDepthBits	 = (g_nDisplayMode & AUX_DEPTH) ? 24 : 0;
	pfd.iLayerType	 = PFD_MAIN_PLANE;

	if (g_nDisplayMode & AUX_DOUBLE)
		pfd.dwFlags |= PFD_DOUBLEBUFFER;

	/*
	 * Report setup failures: samples ignore auxInitWindow's return value.
	 * Missing pixel-format/context setup can later fault in SwapBuffers.
	*/

	nFormat = ChoosePixelFormat(g_hDC, &pfd);

	if (!nFormat)
	{
		fprintf(stderr, "glaux: ChoosePixelFormat failed (%lu)\n",
			GetLastError());

		return (GL_FALSE);
	}

	if (!SetPixelFormat(g_hDC, nFormat, &pfd))
	{
		fprintf(stderr, "glaux: SetPixelFormat failed (%lu)\n",
			GetLastError());

		return (GL_FALSE);
	}

	g_hRC = wglCreateContext(g_hDC);

	if (!g_hRC)
	{
		fprintf(stderr, "glaux: wglCreateContext failed (%lu)\n",
			GetLastError());

		return (GL_FALSE);
	}

	if (!wglMakeCurrent(g_hDC, g_hRC))
	{
		fprintf(stderr, "glaux: wglMakeCurrent failed (%lu)\n",
			GetLastError());

		return (GL_FALSE);
	}

	ShowWindow(g_hWnd, SW_SHOW);
	UpdateWindow(g_hWnd);
	SetForegroundWindow(g_hWnd);

	atexit(AuxShutdown);

	return (GL_TRUE);
}

void APIENTRY
auxReshapeFunc(AUXRESHAPEPROC func)
{
	g_pfnReshape = func;

	/*
	 * glaux calls a newly installed reshape function once with the current
	 * size; the samples set up their projection matrix in it and never draw
	 * a correct frame otherwise.
	*/

	if (func && g_hWnd)
		func((GLsizei) g_nWidth, (GLsizei) g_nHeight);
}

void APIENTRY
auxIdleFunc(AUXIDLEPROC func)
{
	g_pfnIdle = func;
}

void APIENTRY
auxMouseFunc(int button, int mode, AUXMOUSEPROC func)
{
	if (mode != AUX_MOUSEDOWN)
		return;

	if (button == AUX_LEFTBUTTON)
		g_pfnLeftDown = func;
	else if (button == AUX_RIGHTBUTTON)
		g_pfnRightDown = func;
}

void APIENTRY
auxMainLoop(AUXMAINPROC func)
{
MSG msg;

	g_pfnDisplay = func;

	/* One frame up front, the way a freshly shown window gets its paint. */

	if (g_pfnDisplay)
		g_pfnDisplay();

	while (!g_fQuit)
	{
		if (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
				break;

			TranslateMessage(&msg);
			DispatchMessageA(&msg);

			continue;
		}

		/*
		 * Run only the idle callback here; WM_PAINT invokes display. Without an
		 * idle callback, wait for messages (the samples' right-button pause mode).
		*/

		if (g_pfnIdle)
			g_pfnIdle();
		else
			WaitMessage();
	}

	AuxShutdown();
}

void APIENTRY
auxSwapBuffers(void)
{
	/*
	 * Skip SwapBuffers until pixel-format setup succeeds; an invalid DC
	 * previously caused an ICD dispatch fault.
	*/

	if (!g_hDC || !g_hRC)
		return;

	if (g_nDisplayMode & AUX_DOUBLE)
		SwapBuffers(g_hDC);
	else
		glFlush();
}
