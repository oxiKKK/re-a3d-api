/*
 * GL/glaux.h - the part of the OpenGL auxiliary library the A3D 2.0 samples use
 *
 * Not Aureal's and not SGI's: project replacement for the samples' glaux
 * subset. Provides window/context setup, callbacks and buffer swapping
 * without the obsolete glaux.lib. See samples/README.md.
*/

#ifndef _GLAUX_SHIM_H
#define _GLAUX_SHIM_H

#include <windows.h>
#include <GL/gl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* auxInitDisplayMode() flags.  The values are glaux's own. */

#define AUX_RGB			0
#define AUX_RGBA		AUX_RGB
#define AUX_INDEX		1
#define AUX_SINGLE		0
#define AUX_DOUBLE		2
#define AUX_DIRECT		0
#define AUX_INDIRECT		4
#define AUX_ACCUM		8
#define AUX_ALPHA		16
#define AUX_DEPTH		32
#define AUX_STENCIL		64

/* auxMouseFunc() buttons and modes. */

#define AUX_LEFTBUTTON		1
#define AUX_RIGHTBUTTON		2
#define AUX_MIDDLEBUTTON	4

#define AUX_MOUSEDOWN		16
#define AUX_MOUSEUP		32
#define AUX_MOUSELOC		64

/*
 * The record a mouse callback is handed.  None of the three samples reads a
 * field of it, but the callbacks are declared to take one.
*/

typedef struct _AUX_EVENTREC
{
	GLint	event;
	GLint	data[4];
} AUX_EVENTREC;

#define AUX_WINDOWX		0
#define AUX_WINDOWY		1
#define AUX_MOUSEX		0
#define AUX_MOUSEY		1
#define AUX_MOUSESTATUS		2

typedef void (CALLBACK *AUXMAINPROC)(void);
typedef void (CALLBACK *AUXIDLEPROC)(void);
typedef void (CALLBACK *AUXRESHAPEPROC)(GLsizei, GLsizei);
typedef void (CALLBACK *AUXMOUSEPROC)(AUX_EVENTREC *);

void APIENTRY auxInitDisplayMode(GLenum mode);
void APIENTRY auxInitPosition(int x, int y, int width, int height);
GLenum APIENTRY auxInitWindow(LPCSTR title);
void APIENTRY auxReshapeFunc(AUXRESHAPEPROC func);
void APIENTRY auxIdleFunc(AUXIDLEPROC func);
void APIENTRY auxMouseFunc(int button, int mode, AUXMOUSEPROC func);
void APIENTRY auxMainLoop(AUXMAINPROC func);
void APIENTRY auxSwapBuffers(void);

#ifdef __cplusplus
}
#endif

#endif /* _GLAUX_SHIM_H */
