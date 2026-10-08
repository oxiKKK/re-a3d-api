// Project-added A3D development tooling.
#include "scenario_runner.hpp"
#include <cstring>
#include <cstdlib>
#include <cerrno>

namespace a3ddiff {
void emit(const char *name, HRESULT hr, bool faulted, const char *detail)
{
	std::printf("STEP\t%s\t0x%08lX\t%d\t%s\n", name, (unsigned long) hr,
		    faulted ? 1 : 0, detail);
	std::fflush(stdout);
}

void take_line(ScenarioRunResult &rr, char *line)
{
    char* line_end = std::strpbrk(line, "\r\n");
    if (line_end) *line_end = 0;
	if (!std::strcmp(line, "END")) {
		rr.ended = true;
		return;
	}
	if (!std::strncmp(line, "LOAD\tFAIL", 9) || !std::strcmp(line, "ISOLATE\tFAIL")) {
		rr.loaded = false;
		return;
	}
	if (std::strncmp(line, "STEP\t", 5))
		return;

	char *name = line + 5;
	char *tab = std::strchr(name, '\t');
	if (!tab)
		return;
	*tab = 0;
	char *hrs = tab + 1;
	tab = std::strchr(hrs, '\t');
	if (!tab)
		return;
	*tab = 0;
	char *flt = tab + 1;
	tab = std::strchr(flt, '\t');
	if (!tab)
		return;
	*tab = 0;
	char *detail = tab + 1;
	char *nl = std::strpbrk(detail, "\r\n");
	if (nl)
		*nl = 0;

    char* end = nullptr;
    errno = 0;
    const unsigned long value = std::strtoul(hrs, &end, 16);
    if (!*name || !*hrs || *end || errno || (std::strcmp(flt, "0") && std::strcmp(flt, "1"))) return;
	RecordedStepResult s;
	s.name = name;
	s.hr = (HRESULT) value;
	s.faulted = flt[0] == '1';
	s.detail = detail;
	rr.steps.push_back(s);
}
std::string hrs(HRESULT hr)
{
	char b[16];
	std::snprintf(b, sizeof b, "0x%08lX", (unsigned long) hr);
	return b;
}

const RecordedStepResult *find_step(const ScenarioRunResult &r, const std::string &name)
{
	for (const RecordedStepResult &s : r.steps)
		if (s.name == name)
			return &s;
	return nullptr;
}

}
