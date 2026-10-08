/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * com_dll_loader.hpp - load a DLL and reach its class factory with no registry entry.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling.
 *
 * LoadLibrary and DllGetClassObject provide access without COM
 * registration. The recorded module base converts runtime vtable
 * pointers to RVAs for comparison with pe_image.hpp scans.
 *
 *---------------------------------------------------------------------------
 */

#ifndef A3DTEST_DLL_HPP
#define A3DTEST_DLL_HPP

#include <windows.h>
#include <objbase.h>

#include <cstdint>
#include <string>

namespace a3dtest {

typedef HRESULT (__stdcall *pfn_get_class_object)(REFCLSID, REFIID, void **);

class ComDllLoader {
public:
	explicit ComDllLoader(const std::string &path)
	{
		h_ = LoadLibraryA(path.c_str());
		if (h_) {
			path_ = module_path(h_);
			get_class_object_ = reinterpret_cast<pfn_get_class_object>(
				GetProcAddress(h_, "DllGetClassObject"));
		}
	}
	~ComDllLoader() { if (h_) FreeLibrary(h_); }
	ComDllLoader(const ComDllLoader &) = delete;
	ComDllLoader &operator=(const ComDllLoader &) = delete;

	bool ok() const { return h_ != nullptr && get_class_object_ != nullptr; }
	HMODULE handle() const { return h_; }
	const std::string &path() const { return path_; }

	HRESULT get_class_object(REFCLSID clsid, REFIID iid, void **pp) const
	{
		if (!get_class_object_)
			return E_NOTIMPL;
		return get_class_object_(clsid, iid, pp);
	}

	/* Create an object of clsid and return the requested interface. */
	HRESULT create(REFCLSID clsid, REFIID iid, void **pp) const
	{
		IClassFactory *pcf = nullptr;
		HRESULT hr = get_class_object(clsid, IID_IClassFactory,
					      reinterpret_cast<void **>(&pcf));
		if (FAILED(hr))
			return hr;
		hr = pcf->CreateInstance(nullptr, iid, pp);
		pcf->Release();
		return hr;
	}

	/* RVA of a runtime address that lies in this module. */
	std::uint32_t rva_of(const void *addr) const
	{
		return static_cast<std::uint32_t>(
			reinterpret_cast<const std::uint8_t *>(addr) -
			reinterpret_cast<const std::uint8_t *>(h_));
	}

private:
	static std::string module_path(HMODULE h)
	{
		char buf[MAX_PATH];
		DWORD n = GetModuleFileNameA(h, buf, MAX_PATH);
		return std::string(buf, n);
	}

	HMODULE h_ = nullptr;
	pfn_get_class_object get_class_object_ = nullptr;
	std::string path_;
};

}	/* namespace a3dtest */

#endif	/* A3DTEST_DLL_HPP */
