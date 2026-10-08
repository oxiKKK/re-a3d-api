/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * linked_initialization_tests.cpp - check initialization with the linked implementation.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling. See tests/README.md.
 *
 * Links a3dapi_obj with the DLL's /arch:IA32 /fp:precise flags. Calls
 * DllGetClassObject directly, creates an IA3d5 and releases it.
 *
 *---------------------------------------------------------------------------
 */

#include <windows.h>
#include <objbase.h>

#include <ios>

#include <gtest/gtest.h>

#include "ia3dapi.h"
#include "A3d3.h"
#include "a3dsourcecom.h"
#include "dal_a2d.h"

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID, REFIID, void **);

TEST(LinkedEngine, LinksAndCreatesRoot)
{
	IClassFactory	*pcf = NULL;
	IA3d5		*pA3d = NULL;
	HRESULT		hr;

	hr = DllGetClassObject(CLSID_A3dApi, IID_IClassFactory, (void **) &pcf);
	ASSERT_FALSE(FAILED(hr)) << "DllGetClassObject hr=" << std::hex << hr;
	ASSERT_TRUE(pcf != NULL);

	hr = pcf->CreateInstance(NULL, IID_IA3d5, (void **) &pA3d);
	EXPECT_FALSE(FAILED(hr)) << "CreateInstance IA3d5 hr=" << std::hex << hr;
	if (SUCCEEDED(hr) && pA3d)
		pA3d->Release();
	pcf->Release();
}

TEST(LinkedEngine, SourceDestructionClearsWrapper)
{
	DAL_A2D dal;
	IClassFactory *factory = NULL;
	IA3d5 *api = NULL;
	ASSERT_EQ(DllGetClassObject(CLSID_A3dApi, IID_IClassFactory,
		(void **) &factory), S_OK);
	HRESULT hr = factory->CreateInstance(NULL, IID_IA3d5, (void **) &api);
	factory->Release();
	ASSERT_EQ(hr, S_OK);
	CA3dSourceCom *wrapper = new CA3dSourceCom(&dal,
		static_cast<CA3dRoot *>(api), A3DSOURCE_TYPEDEFAULT);
	ASSERT_NE(wrapper->m_pSource, nullptr);
	ASSERT_EQ(wrapper->m_dwPointerType, 1u);

	EXPECT_EQ(wrapper->m_pSource->Release(), 0u);
	EXPECT_EQ(wrapper->m_pSource, nullptr);
	EXPECT_EQ(wrapper->m_dwPointerType, 0u);
	EXPECT_EQ(dal.m_cRef, 1);

	// Permit cleanup even when the regression leaves a stale source pointer.
	wrapper->m_pSource = NULL;
	wrapper->m_dwPointerType = 0;
	EXPECT_EQ(wrapper->Release(), 0u);
	api->Release();
}
