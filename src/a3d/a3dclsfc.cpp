/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * a3dclsfc.cpp
 *
 * Implements the class factory and COM server exports for a3d.dll. Both
 * supported coclasses create CA3d, the compatibility object that exposes
 * older A3D and DirectSound interfaces.
 *
 * The factory handles instance creation and server locks, while the DLL
 * exports report unload status and register or remove both classes.
 * Object destruction updates the shared count through the release hook
 * passed to CA3d.
 *
 *---------------------------------------------------------------------------
 */

#include "a3dprv.h"
#include "a3dclsfc.h"
#include "A3d.h"

LONG	g_cObjects = 0;

/* =============================================================
// A3dReleaseObject()
// (RE) a3d.dll rtl:0x10003FB0
//
// Decrement the server object count.
// =============================================================*/

void
A3dReleaseObject(void)
{
	g_cObjects--;
}
LONG	g_cLocks   = 0;

static char	g_szClsidA3d[64];
static char	g_szKeyA3d[128];
static char	g_szClsidA3dDAL[64];
static char	g_szKeyA3dDAL[128];

/* =============================================================
// CA3dClassFactory()
//
// Initialize the factory reference count.
// =============================================================*/

CA3dClassFactory::CA3dClassFactory(void)
{
	m_cRef = 0;
}

/* =============================================================
// ~CA3dClassFactory()
//
// Destroy the class factory.
// =============================================================*/

CA3dClassFactory::~CA3dClassFactory(void)
{
}

/* =============================================================
// QueryInterface()
//
// Acquire an IUnknown or IClassFactory interface.
//
// Returns:
//   S_OK
//   E_INVALIDARG   a null output
//   E_NOINTERFACE  otherwise
// =============================================================*/

STDMETHODIMP
CA3dClassFactory::QueryInterface(REFIID riid, void **ppv)
{
	if (!ppv)
	{
		return (E_INVALIDARG);
	}

	*ppv = NULL;

	if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory))
	{
		*ppv = this;
	}

	if (!*ppv)
	{
		return (E_NOINTERFACE);
	}

	AddRef();

	return (S_OK);
}

/* =============================================================
// AddRef()
// (RE) a3d.dll rtl:0x10003E30
//
// Increment the reference count.
//
// Returns: The updated reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dClassFactory::AddRef(void)
{
	return (++m_cRef);
}

/* =============================================================
// Release()
//
// Release a reference and delete the factory when the count reaches zero.
//
// Returns: The remaining reference count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dClassFactory::Release(void)
{
	if (--m_cRef)
	{
		return (m_cRef);
	}

	delete this;

	return (0);
}

/* =============================================================
// CreateInstance()
// (RE) a3d.dll rtl:0x10003E80
//
// Create CA3d and acquire the requested interface.
//
// Returns:
//   The QueryInterface result
//   E_INVALIDARG               a null output
//   CLASS_E_NOAGGREGATION      aggregation
//   E_OUTOFMEMORY              allocation failure
// =============================================================*/

STDMETHODIMP
CA3dClassFactory::CreateInstance(IUnknown *pUnkOuter, REFIID riid, void **ppv)
{
CA3d    *pObj;
HRESULT  hr;

	if (!ppv)
	{
		return (E_INVALIDARG);
	}

	*ppv = NULL;

	if (pUnkOuter)
	{
		return (CLASS_E_NOAGGREGATION);
	}

	pObj = new CA3d(A3dReleaseObject);

	if (!pObj)
	{
		return (E_OUTOFMEMORY);
	}

	hr = pObj->QueryInterface(riid, ppv);

	if (FAILED(hr))
	{
		/* Preserve the original object-count decrement before any increment. */

		delete pObj;

		return (hr);
	}

	g_cObjects++;

	return (hr);
}

/* =============================================================
// LockServer()
// (RE) a3d.dll rtl:0x10003F80
//
// Adjust the server lock count. Preserve the original unchecked unlock
// decrement, which can cancel a live lock or make the count negative.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dClassFactory::LockServer(BOOL fLock)
{
	if (fLock)
	{
		g_cLocks++;
	}
	else
	{
		g_cLocks--;
	}

	return (S_OK);
}

/* =============================================================
// DllGetClassObject()
//
// Create the shared factory for either A3D coclass.
//
// Returns:
//   The factory QueryInterface result
//   E_FAIL                             an unknown CLSID
//   E_OUTOFMEMORY                      allocation failure
// =============================================================*/

STDAPI
DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
CA3dClassFactory *pcf;
HRESULT          hr;

	if (!IsEqualCLSID(rclsid, CLSID_A3d) && !IsEqualCLSID(rclsid, CLSID_A3dDal))
	{
		return (E_FAIL);
	}

	pcf = new CA3dClassFactory;

	if (!pcf)
	{
		return (E_OUTOFMEMORY);
	}

	hr = pcf->QueryInterface(riid, ppv);

	if (FAILED(hr))
	{
		delete pcf;
	}

	return (hr);
}

/* =============================================================
// DllCanUnloadNow()
//
// Report whether the server has objects or locks.
//
// Returns:
//   S_OK     when both counts are zero
//   S_FALSE  otherwise
// =============================================================*/

STDAPI
DllCanUnloadNow(void)
{
	return (g_cObjects || g_cLocks);
}

/* =============================================================
// DllRegisterServer()
// (RE) a3d.dll rtl:0x10003960
//
// Register both A3D coclasses. Preserve the original REG_SZ key-class
// argument and ignored registry failures.
//
// Returns: S_OK unconditionally.
// =============================================================*/

STDAPI
DllRegisterServer(void)
{
HKEY  hKey;
DWORD dwDisposition;

	strcpy(g_szClsidA3d, "{D8F1EEE0-F634-11cf-8700-00A0245D918B}");

	lstrcpyA(g_szKeyA3d, "CLSID\\");
	lstrcatA(g_szKeyA3d, g_szClsidA3d);

	hKey = NULL;

	if (!RegCreateKeyExA(HKEY_CLASSES_ROOT, "A3d", 0, (LPSTR) "REG_SZ", 0,
	                     KEY_ALL_ACCESS, NULL, &hKey, &dwDisposition))
	{
		RegSetValueExA(hKey, NULL, 0, REG_SZ, (const BYTE *) "A3d Object",
		               lstrlenA("A3d Object"));
		RegCloseKey(hKey);
	}

	hKey = NULL;

	if (!RegCreateKeyExA(HKEY_CLASSES_ROOT, "A3d\\CLSID", 0, (LPSTR) "REG_SZ", 0,
	                     KEY_ALL_ACCESS, NULL, &hKey, &dwDisposition))
	{
		RegSetValueExA(hKey, NULL, 0, REG_SZ, (const BYTE *) g_szClsidA3d,
		               lstrlenA(g_szClsidA3d));
		RegCloseKey(hKey);
	}

	lstrcatA(g_szKeyA3d, "\\InprocServer32");

	hKey = NULL;

	if (!RegCreateKeyExA(HKEY_CLASSES_ROOT, g_szKeyA3d, 0, (LPSTR) "REG_SZ", 0,
	                     KEY_ALL_ACCESS, NULL, &hKey, &dwDisposition))
	{
		RegSetValueExA(hKey, NULL, 0, REG_SZ, (const BYTE *) "A3D.dll",
		               lstrlenA("A3D.dll"));
		RegCloseKey(hKey);
	}

	hKey = NULL;

	if (!RegCreateKeyExA(HKEY_CLASSES_ROOT, g_szKeyA3d, 0, (LPSTR) "REG_SZ", 0,
	                     KEY_ALL_ACCESS, NULL, &hKey, &dwDisposition))
	{
		RegSetValueExA(hKey, "ThreadingModel", 0, REG_SZ,
		               (const BYTE *) "Apartment", lstrlenA("Apartment"));
		RegCloseKey(hKey);
	}

	strcpy(g_szClsidA3dDAL, "{442D12A1-2641-11d2-90FB-006008A1F441}");

	lstrcpyA(g_szKeyA3dDAL, "CLSID\\");
	lstrcatA(g_szKeyA3dDAL, g_szClsidA3dDAL);

	hKey = NULL;

	if (!RegCreateKeyExA(HKEY_CLASSES_ROOT, "A3dDAL", 0, (LPSTR) "REG_SZ", 0,
	                     KEY_ALL_ACCESS, NULL, &hKey, &dwDisposition))
	{
		RegSetValueExA(hKey, NULL, 0, REG_SZ, (const BYTE *) "A3dDAL Object",
		               lstrlenA("A3dDAL Object"));
		RegCloseKey(hKey);
	}

	hKey = NULL;

	if (!RegCreateKeyExA(HKEY_CLASSES_ROOT, "A3dDAL\\CLSID", 0, (LPSTR) "REG_SZ", 0,
	                     KEY_ALL_ACCESS, NULL, &hKey, &dwDisposition))
	{
		RegSetValueExA(hKey, NULL, 0, REG_SZ, (const BYTE *) g_szClsidA3dDAL,
		               lstrlenA(g_szClsidA3dDAL));
		RegCloseKey(hKey);
	}

	hKey = NULL;

	if (!RegCreateKeyExA(HKEY_CLASSES_ROOT, g_szKeyA3dDAL, 0, (LPSTR) "REG_SZ", 0,
	                     KEY_ALL_ACCESS, NULL, &hKey, &dwDisposition))
	{
		RegSetValueExA(hKey, NULL, 0, REG_SZ, (const BYTE *) "A3dDAL Object",
		               lstrlenA("A3dDAL Object"));
		RegCloseKey(hKey);
	}

	hKey = NULL;

	if (!RegCreateKeyExA(HKEY_CLASSES_ROOT, g_szKeyA3dDAL, 0, (LPSTR) "REG_SZ", 0,
	                     KEY_ALL_ACCESS, NULL, &hKey, &dwDisposition))
	{
		RegSetValueExA(hKey, "AppID", 0, REG_SZ, (const BYTE *) g_szClsidA3dDAL,
		               lstrlenA(g_szClsidA3dDAL));
		RegCloseKey(hKey);
	}

	lstrcatA(g_szKeyA3dDAL, "\\InprocServer32");

	hKey = NULL;

	if (!RegCreateKeyExA(HKEY_CLASSES_ROOT, g_szKeyA3dDAL, 0, (LPSTR) "REG_SZ", 0,
	                     KEY_ALL_ACCESS, NULL, &hKey, &dwDisposition))
	{
		RegSetValueExA(hKey, NULL, 0, REG_SZ, (const BYTE *) "A3D.dll",
		               lstrlenA("A3D.dll"));
		RegCloseKey(hKey);
	}

	hKey = NULL;

	if (!RegCreateKeyExA(HKEY_CLASSES_ROOT, g_szKeyA3dDAL, 0, (LPSTR) "REG_SZ", 0,
	                     KEY_ALL_ACCESS, NULL, &hKey, &dwDisposition))
	{
		RegSetValueExA(hKey, "ThreadingModel", 0, REG_SZ,
		               (const BYTE *) "Apartment", lstrlenA("Apartment"));
		RegCloseKey(hKey);
	}

	return (S_OK);
}

/* =============================================================
// DllUnregisterServer()
// (RE) a3d.dll rtl:0x10003D80
//
// Remove both registrations. Preserve the original failure to remove
// populated keys on Windows NT.
//
// Returns: S_OK unconditionally.
// =============================================================*/

STDAPI
DllUnregisterServer(void)
{
	RegDeleteKeyA(HKEY_CLASSES_ROOT, "A3d");
	RegDeleteKeyA(HKEY_CLASSES_ROOT, "CLSID\\{D8F1EEE0-F634-11cf-8700-00A0245D918B}");
	RegDeleteKeyA(HKEY_CLASSES_ROOT, "A3dDAL");
	RegDeleteKeyA(HKEY_CLASSES_ROOT, "CLSID\\{442D12A1-2641-11d2-90FB-006008A1F441}");

	return (S_OK);
}
