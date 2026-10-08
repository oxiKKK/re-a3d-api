/*
 * scenario_runner.hpp - named interface steps and child-run results.
 *
 * NOT PART OF THE ORIGINAL.  Project tooling. See tests/README.md.
 *
 * API test files append steps. scenario_runner executes them in a child process;
 * child_process_runner enforces its lifetime, and result_protocol reads records.
 */

#ifndef A3D_TESTS_SCENARIO_RUNNER_HPP
#define A3D_TESTS_SCENARIO_RUNNER_HPP

#include <windows.h>
#include <objbase.h>

#include <cstdio>
#include <string>
#include <vector>


#include "ia3dapi.h"

namespace a3ddiff {

/* What one step reports back: its HRESULT and a short text of the out-params, so
   a value that reads back wrong on one side is a diff, not just a return code. */
struct StepResult {
	HRESULT	hr;
	char	detail[256];
};

/* Root and source shared by the scenario steps. create() calls the DLL
   class factory independently, allowing tests of additional engine creation. */
struct ScenarioContext {
	IA3d5		*pRoot;
	IA3dSource2	*pSrc;
	HRESULT		(*create)(REFCLSID clsid, REFIID iid, void **pp);
};

typedef void (*step_fn)(ScenarioContext *, StepResult *);

/* One named step; the parent diffs the two runs by this name. */
struct Step {
	const char	*name;
	step_fn		fn;
};

typedef std::vector<Step> Script;

/* Return the queried interface, or NULL on failure. */
template <class T>
T *qi_as(IA3d5 *pRoot, REFIID iid)
{
	void *p = NULL;
	if (FAILED(pRoot->QueryInterface(iid, &p)) || !p)
		return (NULL);
	return (reinterpret_cast<T *>(p));
}

/* Query the root for iid, record pointer availability, then release it. */
inline void qi(ScenarioContext *c, StepResult *o, REFIID iid)
{
	void *p = NULL;
	o->hr = c->pRoot->QueryInterface(iid, &p);
	std::snprintf(o->detail, sizeof o->detail, "%s", p ? "obj" : "null");
	if (p)
		((IUnknown *) p)->Release();
}

/* BuildScenario in scenario_runner.cpp appends these in dependency order.
   Root initialization precedes source setup; legacy queries follow IA3d5 cases. */
void add_root_steps(Script &s);
void add_source_steps(Script &s);
void add_source_data_steps(Script &s);
void add_scene_steps(Script &s);
void add_source_shutdown_steps(Script &s);
void add_listener_steps(Script &s);
void add_reverb_steps(Script &s);
void add_geometry_steps(Script &s);
void add_occlusion_steps(Script &s);
void add_material_steps(Script &s);
void add_reflection_steps(Script &s);
void add_propertyset_steps(Script &s);
void add_error_steps(Script &s);
void add_legacy_steps(Script &s);
void add_reverb_engine_steps(Script &s);

/* -- parent side: the two runs, diffed by step name ------------------- */

struct RecordedStepResult {
	std::string	name;
	HRESULT		hr;
	std::string	detail;
	bool		faulted;
};

struct ScenarioRunResult {
	std::vector<RecordedStepResult>	steps;
	bool			loaded = true;
	bool			ended = false;		/* false: the child crashed
							   or was timed out */
	DWORD exit_code = STILL_ACTIVE;
	bool			timed_out = false;	/* terminated after timeout */
};

/* Child deadline in milliseconds. Completed step records remain available for
   diagnostics; the fixture rejects timed-out or incomplete runs. */
#define A3D_CHILD_TIMEOUT_MS	60000

ScenarioRunResult run_child(const std::string &self, const std::string &dll,
		     const char *verb = "--child", const char *scenario = "all");
std::string self_path(void);
std::string hrs(HRESULT hr);
const RecordedStepResult *find_step(const ScenarioRunResult &r, const std::string &name);

/* The child entry gtest_main calls for `--child <dll>`: drive one DLL through
   the whole script, streaming a line per step. */
int interface_diff_child(const char *pszDll, const char *scenario = "all");
Script BuildScenario(const char* name);

/* Entry point for --occl-child <dll>. Uses a separate process because
   the shared root lacks A3D_OCCLUSIONS and a second engine is rejected. */
int occlusion_diff_child(const char *pszDll);

/* Entry point for --scene-child <dll>. Replays the 8 SceneRooms
   configurations in a separate process. */
int scenerooms_diff_child(const char *pszDll);

void emit(const char*, HRESULT, bool, const char*);
void take_line(ScenarioRunResult&, char*);
}	/* namespace a3ddiff */


#endif
