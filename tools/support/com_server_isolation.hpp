/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * com_server_isolation.hpp - keep foreign A3D COM servers out of a test process.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling. See tests/README.md.
 *
 * ResMan::AcquireW95Interface (dbg:0x10063700, rtl:0x10025D10) calls
 * CoCreateInstance(CLSID_A3d). The class factory's dwUseDal branch also
 * requests CLSID_A3dDal. Registered servers can therefore load another
 * engine and invalidate reference capture results.
 *
 * The Retail reference can activate the registered reconstruction through
 * CLSID_A3d -> CLSID_A3dApi, causing a reference capture to record its PCM.
 *
 * Register process-local class factories with CoRegisterClassObject;
 * each returns CLASS_E_CLASSNOTAVAILABLE to prevent registry activation.
 * Revoke these registrations on destruction. Both DLLs then use the
 * software path without an external A3D server.
 *
 *---------------------------------------------------------------------------
 */

#ifndef A3DTEST_CLSID_ISOLATE_HPP
#define A3DTEST_CLSID_ISOLATE_HPP

#include <windows.h>
#include <objbase.h>

namespace a3dtest {

/*
 * A class factory that refuses.  It is a process-wide singleton held by the
 * isolation below and never freed, so AddRef and Release are constants.
 */

class deny_factory : public IClassFactory {
public:
	STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
	{
		if (ppv == NULL)
			return (E_POINTER);

		if (IsEqualIID(riid, IID_IUnknown) ||
		    IsEqualIID(riid, IID_IClassFactory)) {
			*ppv = static_cast<IClassFactory *>(this);
			return (S_OK);
		}

		*ppv = NULL;
		return (E_NOINTERFACE);
	}

	STDMETHODIMP_(ULONG) AddRef(void) override  { return (2); }
	STDMETHODIMP_(ULONG) Release(void) override { return (1); }

	STDMETHODIMP CreateInstance(LPUNKNOWN, REFIID, void **ppv) override
	{
		if (ppv != NULL)
			*ppv = NULL;

		return (CLASS_E_CLASSNOTAVAILABLE);
	}

	STDMETHODIMP LockServer(BOOL) override { return (S_OK); }
};

/* Initialize COM, then register the blocked CLSIDs before loading the DLL.
   Keep this object alive for the full capture or comparison. */

class clsid_isolation {
public:
	enum { max_denied = 8 };

	clsid_isolation() : m_cCookies(0) {}

	~clsid_isolation(void)
	{
		while (m_cCookies > 0)
			CoRevokeClassObject(m_adwCookie[--m_cCookies]);
	}

	/* Return false if registration fails. The caller must stop because
	   the registered external server remains reachable. */
	bool deny(REFCLSID rclsid)
	{
		DWORD	dwCookie = 0;
		HRESULT	hr;

		if (m_cCookies >= max_denied)
			return (false);

		hr = CoRegisterClassObject(rclsid, &m_factory,
					   CLSCTX_INPROC_SERVER,
					   REGCLS_MULTIPLEUSE, &dwCookie);

		if (FAILED(hr))
			return (false);

		m_adwCookie[m_cCookies++] = dwCookie;

		return (true);
	}

	int count(void) const { return (m_cCookies); }

private:
	clsid_isolation(const clsid_isolation &);
	clsid_isolation &operator = (const clsid_isolation &);

	deny_factory	m_factory;
	DWORD		m_adwCookie[max_denied];
	int		m_cCookies;
};

}	/* namespace a3dtest */

#endif	/* A3DTEST_CLSID_ISOLATE_HPP */
