/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * support/support_headers_tests.cpp - compile every support header.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling.
 *
 *---------------------------------------------------------------------------
 */

#include <windows.h>
#include <objbase.h>

#include "ia3dapi.h"

#include "com_vtable_call.hpp"
#include "com_lifetime.hpp"
#include "crt_error_reporting.hpp"
#include "result_comparison.hpp"
#include "com_dll_loader.hpp"
#include "interface_descriptors.hpp"
#include "pe_image.hpp"

namespace {

/* Reference each header's main type so a change that breaks one is noticed. */
void framework_selftest_use()
{
	int n = 0;
	(void) a3dtest::published_interfaces(n);
	a3dtest::pe32 pe("");
	(void) pe.ok();
	a3dtest::com_ptr<IUnknown> p;
	(void) p;
	(void) &a3dtest::crt_quiet;
	(void) &framework_selftest_use;
}

}	/* namespace */
