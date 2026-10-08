// Project-added A3D development tooling.
#include "scenario_runner.hpp"
#include <cstring>
#include <stdexcept>
#include <mmreg.h>
#include "ia3ddal.h"
#include "com_server_isolation.hpp"
#include "com_lifetime.hpp"
#include "process_audio_mute.hpp"
#include "crt_error_reporting.hpp"
#include "com_dll_loader.hpp"
using namespace a3dtest;
namespace a3ddiff {

struct ScenarioDefinition { const char* name; void (*append)(Script&); };
static const ScenarioDefinition scenario_definitions[] = {
    {"root", add_root_steps},
    {"source", add_source_steps},
    {"source_data", add_source_data_steps},
    {"scene", add_scene_steps},
    {"listener", add_listener_steps},
    {"reverb", add_reverb_steps},
    {"geometry", add_geometry_steps},
    {"occlusion", add_occlusion_steps},
    {"material", add_material_steps},
    {"reflection", add_reflection_steps},
    {"propertyset", add_propertyset_steps},
    {"error", add_error_steps},
    {"legacy", add_legacy_steps},
    {"reverb_engine", add_reverb_engine_steps},
};

// Standalone scenarios initialize a root and source as needed; "all" preserves the shared sequence.
Script BuildScenario(const char* name)
{
    Script script;
    if (std::strcmp(name, "source_shutdown") == 0) {
        add_source_shutdown_steps(script);
        return script;
    }
    if (std::strcmp(name, "all") == 0) {
        for (const auto& definition : scenario_definitions) definition.append(script);
        return script;
    }
    const ScenarioDefinition* selected = nullptr;
    for (const auto& definition : scenario_definitions)
        if (std::strcmp(name, definition.name) == 0) selected = &definition;
    if (!selected) throw std::runtime_error(std::string("Unknown scenario: ") + name);
    add_root_steps(script);
    if (std::strcmp(name, "root") != 0) add_source_steps(script);
    if (std::strcmp(name, "root") != 0 && std::strcmp(name, "source") != 0) selected->append(script);
    return script;
}

/* -- the child: guard each call, stream a line per step --------------- */

namespace {

/* No C++ object needing unwinding lives in a step, so __try is allowed. */
bool guarded(step_fn fn, ScenarioContext *c, StepResult *o)
{
	__try {
		fn(c, o);
		return (true);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return (false);
	}
}

/* The DLL this child is driving, for ScenarioContext::create.  A file static because the
   step signature carries no state of its own. */
const ComDllLoader *g_pDll = NULL;

HRESULT child_create(REFCLSID clsid, REFIID iid, void **pp)
{
	if (!g_pDll)
		return (E_UNEXPECTED);
	return (g_pDll->create(clsid, iid, pp));
}

}	/* namespace */

int
interface_diff_child(const char *pszDll, const char *scenario)
{
	crt_quiet();
	com_init ci;
	(void) ci;

	process_audio_mute mute;
	(void) mute;

	/* Refuse CLSIDs the engine reaches for at startup to prevent loading both DLLs. */
	clsid_isolation iso;

	if (!iso.deny(CLSID_A3d) || !iso.deny(CLSID_A3dDal)) {
		std::printf("ISOLATE\tFAIL\n");
		std::fflush(stdout);
		return (1);
	}

	ComDllLoader dll(pszDll);
	if (!dll.ok()) {
		std::printf("LOAD\tFAIL\t%s\n", pszDll);
		std::fflush(stdout);
		return (1);
	}

	g_pDll = &dll;

	ScenarioContext c;
	c.pRoot = NULL;
	c.pSrc = NULL;
	c.create = child_create;

	void *root = NULL;
	HRESULT hr = dll.create(CLSID_A3dApi, IID_IA3d5, &root);
	emit("CreateInstance IA3d5", hr, false, root ? "obj" : "null");
	if (FAILED(hr) || !root) {
		std::printf("END\n");
		std::fflush(stdout);
		return (0);
	}
	c.pRoot = reinterpret_cast<IA3d5 *>(root);

	Script script = BuildScenario(scenario);

	for (const Step &st : script) {
		StepResult o;
		o.hr = E_FAIL;
		o.detail[0] = 0;
		bool ok = guarded(st.fn, &c, &o);
		emit(st.name, o.hr, !ok, o.detail);
	}

	std::printf("END\n");
	std::fflush(stdout);
	return (0);
}


}
