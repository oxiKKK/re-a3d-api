# Implementation and validation status

This reconstruction has working API and software-audio paths, remaining
implementation gaps, and optional behavior that differs from Aureal's binary.
There is no established percentage of complete functionality.

## Feature matrix

| Area | Current implementation | Evidence and limits |
| --- | --- | --- |
| COM activation and public interfaces | Implemented for both DLLs | Export/vtable and API comparisons; matching vtable lengths alone do not establish every slot's semantics |
| Source/listener controls | Implemented | Getter/setter comparisons, including initial listener orientation, and static audio scenes |
| WAV/PCM playback | Implemented | Software output captures and an audible-render gate |
| A2D positional audio | HRTF software mixer | Reference PCM comparisons cover six static scenes; broader input and motion coverage remains unverified |
| D2D native audio | DirectSound secondary buffers | Descriptor and PCM coverage in the native scene |
| D3D and external hardware | Routing and buffer implementations | Physical Aureal hardware/DSP output is not established by software-only tests |
| Timing-only EMU | Virtual playback and events | Does not produce audio; distinct from `EmulateHardware` |
| Geometry and occlusion | Implemented with unresolved paths | API and scene comparisons; see remaining gaps below |
| Reflections and reverb | API objects and backend-dependent paths | Original software routes can refuse effects; optional software DSP is project-added |
| MP3 | Optional minimp3 adapter | Integration tests; no proprietary-decoder PCM equivalence claim |
| AC-3 | Optional liba52 adapter and DirectShow path | Static-adapter integration and failed-load coverage; successful system-filter playback is not established |
| A3D 1.x compatibility | Separate DLL, buffers, listener, and DSP | Coverage is narrower than for `a3dapi.dll`; GoldSrc validation covers the initialization probe |
| Legacy hardware capability emulation | Optional `EmulateHardware` | Quake III startup-contract replay and positional PCM gate |

## Remaining gaps and intentional differences

Hit refinement calls both volumetric helpers, but volume-axis initialization is
not established, so volumetric results are not complete. List base identities
and some recording behavior remain unresolved.
Follow the located source and [reconstruction records](llm/ARCHITECTURE.md#known-gaps)
before treating an older missing-code note as current.

`A3D_FIXES` includes runtime-selectable additions and defaults to ON.
The shipped file enables audio additions and disables credits additions.
Reference comparisons require a separate `A3D_FIXES=OFF` build. See [configuration](reference/configuration.md).

## What the tests establish

| Observation | Conclusion |
| --- | --- |
| Method returns success | That call completed successfully for the exercised inputs |
| Capability flag is set | The selected configuration advertises the feature |
| A source reports playing | Its state indicates playback; this can include a virtual route |
| Capture contains nonzero PCM | The exercised route produced samples |
| Left/right samples differ | The output is asymmetric; this alone does not establish correct spatialization |
| PCM matches the reference | Agreement for the compared scene, format, interval, and tolerance |
| A game starts with A3D | Its startup path succeeded; gameplay and all effects need separate checks |

[Recorded test coverage](llm/testing_coverage.md) contains historical runs and
specific regressions. Each result applies to its recorded revision, devices,
and settings. [Testing](development/testing.md)
describes how to produce a new result, including DLL hashes and configuration.

## Reporting compatibility

Record the game executable version/hash, operating system, loaded DLL paths,
build options and runtime configuration, audio backend, reproduction steps, and observed output. Report
startup, playback, spatial audio, effects, and shutdown separately. Unmeasured
platforms and effects remain unverified.
