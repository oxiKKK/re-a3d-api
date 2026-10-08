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
 * Implements COM activation and registration for a3dapi.dll. The exported
 * DllGetClassObject entry point creates a class factory, which selects a
 * resource manager, API root or compatibility wrapper according to the
 * requested class and interface.
 *
 * This file also applies the backend selection and instance restrictions
 * used during activation, maintains server lock accounting, and writes or
 * removes registry entries. The objects it creates are implemented in
 * resman.cpp, A3dRoot.cpp and apimapper.cpp.
 *
 *---------------------------------------------------------------------------
 */

#include "A3dPrivate.h"
#include "a3dclsfc.h"
#include "A3d3.h"
#include "apimapper.h"
#include "resman.h"

#include <string.h>

/* Creation count; the original never decrements it. */
LONG g_cObjects = 0;
/* COM server lock count. */
LONG g_cLocks   = 0;

extern ResMan *g_lpResMan;

/* =============================================================
// DllGetClassObject()
// (RE) rtl:0x10003680; dbg:0x1000D500
//
// Return a factory interface for CLSID_A3dApi or CLSID_A3d.
//
// Returns: S_OK; CLASS_E_CLASSNOTAVAILABLE for another CLSID; E_OUTOFMEMORY on
//          allocation failure; a failed QueryInterface result.
// =============================================================*/

STDAPI
DllGetClassObject(
	REFCLSID rclsid,
	REFIID riid,
	LPVOID *ppv)
{
CA3dClassFactory       *pcf;
HRESULT                 hr;

	if (!IsEqualCLSID(rclsid, CLSID_A3dApi) && !IsEqualCLSID(rclsid, CLSID_A3d))
		return (CLASS_E_CLASSNOTAVAILABLE);

#if defined(A3D_FIXES)
	A3dGetConfig();
#endif

	pcf = new CA3dClassFactory;
	if (!pcf)
		return (E_OUTOFMEMORY);

	hr = pcf->QueryInterface(riid, ppv);
	if (!FAILED(hr))
		return (S_OK);

	pcf->Release();

	return (hr);
}

/* =============================================================
// DllCanUnloadNow()
// (RE) rtl:0x10003720; dbg:0x1000D6B0
//
// Check the process object and lock counts. The original never decrements
// g_cObjects, preventing unload after its first increment.
//
// Returns: S_OK if both counts are zero, otherwise S_FALSE.
// =============================================================*/

STDAPI
DllCanUnloadNow(void)
{
	return (g_cObjects || g_cLocks);
}

/* =============================================================
// A3dSetKeyValue()
// (RE) dbg:0x1000D830
//
// Write a string below HKEY_CLASSES_ROOT. Preserve the original REG_SZ
// class string and omitted terminating null in the value data.
//
// Returns: A failed RegCreateKeyExA result, otherwise the RegCloseKey result;
//          value-write errors are ignored.
// =============================================================*/

static LONG
A3dSetKeyValue(
	LPCSTR lpSubKey,
	LPCSTR lpValueName,
	LPCSTR lpString)
{
HKEY    hkey;
DWORD   dwDisposition;
LONG    lResult;

	hkey = NULL;

	lResult = RegCreateKeyExA(HKEY_CLASSES_ROOT, lpSubKey, 0,
				  (LPSTR) "REG_SZ", 0, KEY_ALL_ACCESS, NULL,
				  &hkey, &dwDisposition);
	if (lResult)
		return (lResult);

	RegSetValueExA(hkey, lpValueName, 0, REG_SZ, (const BYTE *) lpString,
		       lstrlenA(lpString));

	return (RegCloseKey(hkey));
}

/* =============================================================
// DllRegisterServer()
// (RE) rtl:0x10003740; dbg:0x1000D6F0
//
// Register the API class and ProgID using the bare DLL filename.
//
// Returns: S_OK, ignoring registry errors.
// =============================================================*/

STDAPI
DllRegisterServer(void)
{
CHAR szKey[256];
CHAR szClsid[40];

	strcpy(szClsid, "{92FA2C24-253C-11d2-90FB-006008A1F441}");

	lstrcpyA(szKey, "CLSID\\");
	lstrcatA(szKey, szClsid);

	A3dSetKeyValue("A3dApi", NULL, "A3dApi Object");
	A3dSetKeyValue("A3dApi\\CLSID", NULL, szClsid);
	A3dSetKeyValue(szKey, NULL, "A3dApi Object");

	lstrcatA(szKey, "\\InprocServer32");

	A3dSetKeyValue(szKey, NULL, "a3dapi.dll");
	A3dSetKeyValue(szKey, "ThreadingModel", "Apartment");

	return (S_OK);
}

/* =============================================================
// DllUnregisterServer()
// (RE) rtl:0x10003950; dbg:0x1000D7F0
//
// Delete the API registration roots. Preserve the original failure to
// remove subkeys on NT.
//
// Returns: S_OK, ignoring registry errors.
// =============================================================*/

STDAPI
DllUnregisterServer(void)
{
	RegDeleteKeyA(HKEY_CLASSES_ROOT, "A3dApi");
	RegDeleteKeyA(HKEY_CLASSES_ROOT,
		      "CLSID\\{92FA2C24-253C-11d2-90FB-006008A1F441}");

	return (S_OK);
}

/* =============================================================
// CA3dClassFactory()
// (RE) dbg:0x1000D970
//
// Initialize the factory reference count.
// =============================================================*/

CA3dClassFactory::CA3dClassFactory(void)
{
	m_cRef = 0;
}

/* =============================================================
// ~CA3dClassFactory()
// (RE) dbg:0x1000DA20
//
// Destroy the factory.
// =============================================================*/

CA3dClassFactory::~CA3dClassFactory(void)
{
}

/* =============================================================
// QueryInterface()
//
// Return a referenced IUnknown or IClassFactory interface.
//
// Returns:
//   S_OK
//   E_NOINTERFACE  with a null output for an unsupported IID
// =============================================================*/

STDMETHODIMP
CA3dClassFactory::QueryInterface(REFIID riid, void **ppv)
{
	if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory))
	{
		*ppv = this;
		AddRef();

		return (S_OK);
	}

	*ppv = NULL;

	return (E_NOINTERFACE);
}

/* =============================================================
// AddRef()
//
// Increment the factory reference count.
//
// Returns: The incremented count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dClassFactory::AddRef(void)
{
	return (InterlockedIncrement(&m_cRef));
}

/* =============================================================
// Release()
//
// Release a positive reference count and delete when it reaches zero.
//
// Returns: The remaining count, including an already nonpositive count.
// =============================================================*/

STDMETHODIMP_(ULONG)
CA3dClassFactory::Release(void)
{

	if (m_cRef <= 0 || InterlockedDecrement(&m_cRef))
		return (m_cRef);

	delete this;

	return (0);
}

/* =============================================================
// RedirectForeignClsid()
// (RE) dbg:0x1000D8C0
//
// Redirect the legacy A3D class to a3dfake.dll when the driver-version
// check reports a foreign implementation.
//
// Returns: 1 for a foreign driver, otherwise 0; registry errors are ignored.
// =============================================================*/

static int
RedirectForeignClsid(void)
{
CHAR    szKey[256];
CHAR    szClsid[40];
int     fForeign;

	fForeign = 0;

	if (A3dCheckDriverVersion() == A3DVER_FOREIGN)
	{
		fForeign = 1;

		strcpy(szClsid, "{D8F1EEE0-F634-11cf-8700-00A0245D918B}");

		lstrcpyA(szKey, "CLSID\\");
		lstrcatA(szKey, szClsid);
		lstrcatA(szKey, "\\InprocServer32");

		A3dSetKeyValue(szKey, NULL, "a3dfake.dll");
	}

	return (fForeign);
}

/* =============================================================
// CreateInstance()
// (RE) rtl:0x10003A40; dbg:0x1000DB90
//
// Create a resource manager, API root or legacy wrapper using the selected
// DAL. Preserve the original reference leaks on early failure paths.
//
// Returns: S_OK on creation; CLASS_E_NOAGGREGATION for aggregation;
//          E_OUTOFMEMORY on allocation failure; E_NOINTERFACE for an
//          unsupported IID; an instance-limit, DAL creation/query or root
//          attachment error.
// =============================================================*/

STDMETHODIMP
CA3dClassFactory::CreateInstance(
	IUnknown *pUnkOuter,
	REFIID riid,
	void **ppv)
{
IDirectSound   *pDS;
IA3d2          *pA3d2;
IA3d           *pA3d;
IA3dDal        *pDal;
CA3dRoot       *pApi;
CA3dMapper     *pWrap;
ResMan         *pResMan;
HKEY            hkey;
DWORD           dwUseDal;
DWORD           cbData;
BOOL fEnforceSingleInstance;
HRESULT hr;

	pDS   = NULL;
	pA3d2 = NULL;
	pA3d  = NULL;
	pDal  = NULL;
	pApi  = NULL;

	fEnforceSingleInstance = TRUE;

	if (IsEqualIID(riid, IID_IA3d4) || IsEqualIID(riid, IID_IA3d3) ||
	    IsEqualIID(riid, IID_IA3d2) || IsEqualIID(riid, IID_IA3d))
		fEnforceSingleInstance = FALSE;

	RedirectForeignClsid();

	*ppv = NULL;

	if (pUnkOuter)
		return (CLASS_E_NOAGGREGATION);

	dwUseDal = 0;

	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Aureal\\A3D", 0,
			  KEY_READ, &hkey) == ERROR_SUCCESS)
	{
		cbData = sizeof(dwUseDal);

		RegQueryValueExA(hkey, "UseDALInterface", NULL, NULL,
				 (LPBYTE) &dwUseDal, &cbData);

		if (hkey)
			RegCloseKey(hkey);
	}

	if (IsEqualIID(riid, IID_IA3dPrv4))
	{
		if (g_lpResMan)
			return (A3DERROR_CANT_INSTANTIATE_MORE_ONE_INSTANCE);

		pResMan = new ResMan;
		if (!pResMan)
			return (E_OUTOFMEMORY);

		hr = pResMan->QueryInterface(riid, ppv);

		pResMan->Release();

		if (FAILED(hr))
			return (hr);

		InterlockedIncrement(&g_cObjects);

		return (S_OK);
	}

	if (dwUseDal)
	{

		hr = CoCreateInstance(CLSID_A3dDal, NULL, CLSCTX_INPROC_SERVER,
				      IID_IA3dDal, (void **) &pDal);
		if (FAILED(hr))
			return (hr);

		if (FAILED(pDal->QueryInterface(IID_IDirectSound,
						(void **) &pDS)))
		{
			if (pDal)
			{
				pDal->Release();
				pDal = NULL;
			}

			return (A3DERROR_FAILED_QUERY_DIRECTSOUND);
		}

		if (FAILED(pDal->QueryInterface(IID_IA3d2, (void **) &pA3d2)))
		{
			if (pDS)
			{
				pDS->Release();
				pDS = NULL;
			}

			if (pDal)
			{
				pDal->Release();
				pDal = NULL;
			}

			return (A3DERROR_FAILED_QUERY_A3D2);
		}

		if (pDal)
		{
			pDal->Release();
			pDal = NULL;
		}
	}
	else
	{

		if (fEnforceSingleInstance && g_lpResMan)
			return (A3DERROR_CANT_INSTANTIATE_MORE_ONE_INSTANCE);

		pResMan = new ResMan;
		if (!pResMan)
			return (E_OUTOFMEMORY);

		ASSERT((pResMan != 0 && !IsBadReadPtr(pResMan, sizeof(ResMan))));

		InterlockedIncrement(&g_cObjects);

		if (FAILED(pResMan->QueryInterface(IID_IDirectSound,
						   (void **) &pDS)))
		{
			pResMan->Release();

			return (A3DERROR_FAILED_QUERY_DIRECTSOUND);
		}

		if (FAILED(pResMan->QueryInterface(IID_IA3d2, (void **) &pA3d2)))
		{
			if (pDS)
			{
				pDS->Release();
				pDS = NULL;
			}

			pResMan->Release();

			return (A3DERROR_FAILED_QUERY_A3D2);
		}

		pResMan->Release();
	}

	pApi = new CA3dRoot(0);
	if (!pApi)
	{
		if (pA3d2)
		{
			pA3d2->Release();
			pA3d2 = NULL;
		}

		if (pDS)
		{
			pDS->Release();
			pDS = NULL;
		}

		return (E_OUTOFMEMORY);
	}

	if (FAILED(pApi->Attach(pDS, pA3d2, pA3d)))
	{
		pApi->Release();

		if (pA3d2)
		{
			pA3d2->Release();
			pA3d2 = NULL;
		}

		if (pDS)
		{
			pDS->Release();
			pDS = NULL;
		}

		return (A3DERROR_FAILED_INIT_A3D3);
	}

	if (IsEqualIID(riid, IID_IA3d3) || IsEqualIID(riid, IID_IA3d4) ||
	    IsEqualIID(riid, IID_IA3d5))
	{
		if (FAILED(pApi->QueryInterface(riid, ppv)))
		{
			pApi->Release();

			if (pA3d2)
			{
				pA3d2->Release();
				pA3d2 = NULL;
			}

			if (pDS)
			{
				pDS->Release();
				pDS = NULL;
			}

			return (A3DERROR_FAILED_QUERY_A3D3);
		}

		InterlockedIncrement(&g_cObjects);

		return (S_OK);
	}

	if (!IsEqualIID(riid, IID_IDirectSound) &&
	    !IsEqualIID(riid, IID_IA3d) &&
	    !IsEqualIID(riid, IID_IA3d2))
		return (E_NOINTERFACE);

	pWrap = new CA3dMapper(pApi);
	if (!pWrap)
	{
		pApi->Release();

		if (pA3d2)
		{
			pA3d2->Release();
			pA3d2 = NULL;
		}

		if (pDS)
		{
			pDS->Release();
			pDS = NULL;
		}

		return (E_OUTOFMEMORY);
	}

	if (FAILED(pWrap->QueryInterface(riid, ppv)))
	{
		pWrap->Release();
		pApi->Release();

		if (pA3d2)
		{
			pA3d2->Release();
			pA3d2 = NULL;
		}

		if (pDS)
		{
			pDS->Release();
			pDS = NULL;
		}

		return (A3DERROR_FAILED_QUERY_DIRECTSOUND);
	}

	InterlockedIncrement(&g_cObjects);

	return (S_OK);
}

/* =============================================================
// LockServer()
// (RE) rtl:0x10004150; dbg:0x1000E770
//
// Adjust the server lock count. Preserve negative counts after an
// unmatched unlock.
//
// Returns: S_OK.
// =============================================================*/

STDMETHODIMP
CA3dClassFactory::LockServer(BOOL fLock)
{
	if (fLock)
		InterlockedIncrement(&g_cLocks);
	else
		InterlockedDecrement(&g_cLocks);

	return (S_OK);
}
