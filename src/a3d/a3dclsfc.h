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
 * Declares the compatibility DLL's CA3dClassFactory and shared server
 * accounting. The factory exposes IClassFactory operations for creating
 * CA3d instances and locking the COM server.
 *
 * a3dclsfc.cpp implements both coclass activation paths, registration
 * exports and unload checks. This factory belongs to a3d.dll and is
 * independent of the similarly named class in a3dapi.dll.
 *
 *---------------------------------------------------------------------------
 */

#ifndef _A3D_A3DCLSFC_H
#define _A3D_A3DCLSFC_H

#include "a3dprv.h"

class CA3d;

/* =============================================================
// Class: CA3dClassFactory
//
// Description: COM factory shared by both A3D coclasses.
//
// Size: 0x08
// =============================================================*/

class CA3dClassFactory : public IClassFactory
{
public:
	CA3dClassFactory(void);
	virtual ~CA3dClassFactory(void);

	STDMETHODIMP         QueryInterface(REFIID riid, void **ppv);
	STDMETHODIMP_(ULONG) AddRef(void);
	STDMETHODIMP_(ULONG) Release(void);
	STDMETHODIMP         CreateInstance(IUnknown *pUnkOuter, REFIID riid,
	                                    void **ppv);
	STDMETHODIMP         LockServer(BOOL fLock);

protected:
	LONG m_cRef; /* Non-interlocked reference count. */
};

extern LONG	g_cObjects;

void A3dReleaseObject(void);
extern LONG	g_cLocks;

#endif /* _A3D_A3DCLSFC_H */
