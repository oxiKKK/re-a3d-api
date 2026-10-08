# Tests

Requires Windows x86, MSVC and the tracked reference DLLs in `ref/`.
Python 3 is required for the diagnostic source checks. GoogleTest is vendored. Run from the repository root:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32 -DA3D_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release -E a3d_reference_comparison_tests --output-on-failure
```

| Directory | Checks |
| --- | --- |
| `abi/` | PE exports and reference vtable lengths |
| `api/` | Interface results, faults, output values and geometry behavior |
| `audio/` | DirectSound buffer descriptors and captured PCM |
| `decoders/` | Optional replacement decoder integration |
| `integration/` | Statically linked initialization |
| `support/` | Test fixtures, scenarios, process isolation and result parsing |

`a3d_reference_comparison_tests` runs ABI, API, audio and tooling checks.
`a3d_linked_implementation_tests` runs initialization and enabled decoder tests.
Both accept `--gtest_list_tests` and `--gtest_filter`. CTest also runs
`audible_render_gate`, which checks PCM production.
`diagnostic_side_effect_audit` rejects unreviewed calls and mutations inside
discarded diagnostic arguments; `diagnostic_audit_tests` checks its parser.
The linked tests verify ASSERT/VERIFY evaluation in each build configuration.

With `A3D_FIXES=ON`, CTest adds `q3_emulation_playback` for the software
device's legacy capability contract and positional PCM. See the
[game build and launch steps](../docs/user/game-compatibility.md#quake-iii-demo).
Reference parity tests require an `A3D_FIXES=OFF` build. Keep the supplied
configuration defaults for the Quake III and decoder checks.
`runtime_configuration` uses isolated executable directories to exercise all
effect combinations, parsing, concurrent first access, decoder creation, both
property-wait fixes, intercepted credits activation/payload selection and
restart behavior. Each child has a deadline; credits executables are never
launched. Full reference comparisons use a separate
[reference build](../docs/development/building.md#reference-comparison-build).

```powershell
.\build\Release\a3d_reference_comparison_tests.exe --gtest_filter=InterfaceVsReference.ListenerGetters
cmake --build build --config Debug
.\build\Debug\a3d_reference_comparison_tests.exe --gtest_filter=AbiVsReference.*
```

Runtime comparisons use the Release DLL and retail reference, including when
the test executable is built in Debug. `A3D_COMPARISON_DLL` overrides the
default `build/Release/a3dapi.dll`. Build Release first. Debug ABI inspection
uses the matching Debug reference. Missing inputs fail the comparison.

## Investigating a failure

Interface comparisons run an ordered scenario in one child per DLL, with a
60-second deadline and streamed results. Occlusion and SceneRooms use separate
children. Each comparison child mutes its own default audio session
([process_audio_mute.hpp](support/process_audio_mute.hpp)), so scenarios that
play through the real DirectSound device stay off the speakers. Session volume
is applied after DirectSound mixing, so reported values are unaffected. PCM
comparisons use `a3d_pcm_capture`, which substitutes DirectSound and opens no
device. Named groups can be run in isolation:

```powershell
.\build\Release\a3d_reference_comparison_tests.exe --child .\build\Release\a3dapi.dll --scenario material
```

Each named group initializes a fresh root. The reference material results
depend on allocation/setup order, so standalone runs can differ from the
ordered automated scenario.

Audio results are under `<build>/artifacts/audio/`: `run.json` records the
configuration and DLL hash, `capture.log` holds diagnostics, and `capture.tsv`
and WAV files hold the measurements. Captures have a 30-second deadline;
the parent allows 35 seconds. See [capture tools](../tools/README.md#capture-output).

Record the commit, build options and full output with a reported result.
Comparison tests read the reference DLLs in `ref/` and the media files in
`samples/data/`.
