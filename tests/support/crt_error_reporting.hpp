/*------------------------- re-a3d-api, 2026 -------------------------------
 *
 * This file is part of re-a3d-api, a historical and archival reconstruction
 * of Aureal A3D. It is maintained to preserve and study the behavior,
 * interfaces, and implementation of the original software.
 *
 *---------------------------------------------------------------------------
 *
 * crt_error_reporting.hpp - keep a failing assert from stalling an unattended run.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling.
 *
 * Suppress Windows error dialogs and route this executable's CRT reports
 * to stderr/debug output. A DLL with a statically linked CRT has separate
 * reporting state; these settings do not suppress its assertion dialogs.
 *
 *---------------------------------------------------------------------------
 */

#ifndef A3DTEST_CRT_QUIET_HPP
#define A3DTEST_CRT_QUIET_HPP

#include <windows.h>

#include <cstdlib>

#ifdef _DEBUG
#include <crtdbg.h>
#endif

namespace a3dtest {

inline void crt_quiet()
{
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
		     SEM_NOOPENFILEERRORBOX | SEM_NOALIGNMENTFAULTEXCEPT);

	/* These report settings affect this executable's CRT only. The comparison
	   harness uses Retail DLLs to avoid independent Debug CRT dialogs. */
	_set_error_mode(_OUT_TO_STDERR);
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

#ifdef _DEBUG
	int mode = _CRTDBG_MODE_DEBUG | _CRTDBG_MODE_FILE;
	_CrtSetReportMode(_CRT_ASSERT, mode);
	_CrtSetReportMode(_CRT_ERROR, mode);
	_CrtSetReportMode(_CRT_WARN, mode);
	_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
	_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
	_CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
#endif
}

}	/* namespace a3dtest */

#endif	/* A3DTEST_CRT_QUIET_HPP */
