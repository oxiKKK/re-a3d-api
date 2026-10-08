// Project-added A3D development tooling.
#include "scenario_runner.hpp"
#include <cstring>
#include <cstdlib>

namespace a3ddiff {
/* Run child with a pipe and deadline. _popen would block forever on a hung step.
   Child killed on deadline still reports every finished step. */
ScenarioRunResult run_child(const std::string &self, const std::string &dll,
		     const char *verb, const char *scenario)
{
	ScenarioRunResult rr;

	SECURITY_ATTRIBUTES sa;
	sa.nLength	  = sizeof sa;
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle = TRUE;

	HANDLE hRead = NULL, hWrite = NULL;
	if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
		rr.loaded = false;
		return rr;
	}
	SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

	STARTUPINFOA si;
	std::memset(&si, 0, sizeof si);
	si.cb		= sizeof si;
	si.dwFlags	= STARTF_USESTDHANDLES;
	si.hStdOutput	= hWrite;
	si.hStdError	= hWrite;
	si.hStdInput	= GetStdHandle(STD_INPUT_HANDLE);

	std::string cmd = "\"" + self + "\" " + verb + " \"" + dll + "\"";
	if (std::strcmp(verb, "--child") == 0) cmd += std::string(" --scenario ") + scenario;
	std::vector<char> cmdbuf(cmd.begin(), cmd.end());
	cmdbuf.push_back(0);

	PROCESS_INFORMATION pi;
	std::memset(&pi, 0, sizeof pi);

	if (!CreateProcessA(NULL, cmdbuf.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW,
			    NULL, NULL, &si, &pi)) {
		CloseHandle(hRead);
		CloseHandle(hWrite);
		rr.loaded = false;
		return rr;
	}

	/* Parent must close its copy of the write end, or read never sees end-of-file. */
	CloseHandle(hWrite);

	const DWORD dwStart = GetTickCount();
	std::string pending;
	char	    buf[1024];

	for (;;) {
		if (GetTickCount() - dwStart > A3D_CHILD_TIMEOUT_MS) {
			rr.timed_out = true;
			TerminateProcess(pi.hProcess, 1);
			WaitForSingleObject(pi.hProcess, 2000);
			break;
		}
		DWORD cbAvail = 0;
		if (!PeekNamedPipe(hRead, NULL, 0, NULL, &cbAvail, NULL))
			break;			/* the child closed the pipe */

		if (cbAvail) {
			DWORD cbRead = 0;
			if (!ReadFile(hRead, buf, sizeof buf - 1, &cbRead, NULL)
			    || !cbRead)
				break;
			buf[cbRead] = 0;
			pending += buf;

			for (;;) {
				size_t nl = pending.find('\n');
				if (nl == std::string::npos)
					break;
				std::string one = pending.substr(0, nl + 1);
				pending.erase(0, nl + 1);
				take_line(rr, &one[0]);
			}
			continue;
		}

		if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {
			/* Drained above and the child is gone. */
			break;
		}


		Sleep(5);
	}

	if (!pending.empty())
		take_line(rr, &pending[0]);

	CloseHandle(hRead);
    const DWORD elapsed = GetTickCount() - dwStart;
    const DWORD remaining = elapsed < A3D_CHILD_TIMEOUT_MS ? A3D_CHILD_TIMEOUT_MS - elapsed : 0;
    if (WaitForSingleObject(pi.hProcess, remaining) != WAIT_OBJECT_0) {
        rr.timed_out = true;
        TerminateProcess(pi.hProcess, 124);
        WaitForSingleObject(pi.hProcess, 2000);
    }
	CloseHandle(pi.hThread);
	GetExitCodeProcess(pi.hProcess, &rr.exit_code);
	CloseHandle(pi.hProcess);
	return rr;
}

std::string self_path(void)
{
	char buf[MAX_PATH];
	DWORD n = GetModuleFileNameA(NULL, buf, MAX_PATH);
	return std::string(buf, n);
}


}
