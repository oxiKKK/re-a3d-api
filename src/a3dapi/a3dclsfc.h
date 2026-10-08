/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * a3dclsfc.h
 *
 * Declares CA3dClassFactory, the IClassFactory implementation used to
 * activate objects from a3dapi.dll. It exposes interface lookup,
 * reference counting, instance creation and server locking to COM.
 *
 * The corresponding implementation in a3dclsfc.cpp selects the concrete
 * API object and contains the DLL activation, unload and registration
 * exports. This class is separate from the similarly named factory in
 * src/a3d, which serves the compatibility DLL.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _A3DCLSFC_H
#define _A3DCLSFC_H

#include "A3dPrivate.h"

/* Back end selected during API object creation. */

#define A3D_BACKEND_DAL 0 /* DAL object */
#define A3D_BACKEND_EMU 1 /* A3D 1.x fallback */

/* Server object and lock counts; DllCanUnloadNow() reports on both. */

extern LONG g_cObjects;
extern LONG g_cLocks;

/* Driver-version checks implemented in refaudbin.cpp. */

int  A3dCheckDriverVersion(void);
int  A3dCheckFileVersion(CHAR *lptstrFilename, DWORD dwLen, LPVOID lpData);

#define A3DVER_FOREIGN 0    /* version strings identify another vendor */
#define A3DVER_AUREAL  1    /* Aureal or SM Emulation */
#define A3DVER_NONE    (-1) /* relevant strings are absent */

/* =============================================================
// Class: CA3dClassFactory
//
// Description: COM factory for the API root and compatibility wrapper.
//
// Size: 0x08
//
// (RE) Constructor: dbg:0x1000D970
// =============================================================*/

class CA3dClassFactory : public IClassFactory
{
public:
	CA3dClassFactory(void);

	~CA3dClassFactory(void);

	STDMETHODIMP         QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG) AddRef(void);
	STDMETHODIMP_(ULONG) Release(void);
	STDMETHODIMP         CreateInstance(IUnknown *pUnkOuter, REFIID riid, void **ppv);
	STDMETHODIMP         LockServer(BOOL fLock);

protected:
	/* 0x04 */ LONG m_cRef; /* COM reference count. */
};

#endif /* _A3DCLSFC_H */
