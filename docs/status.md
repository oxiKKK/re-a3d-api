# Status

The API and the software audio path work. Some parts are still missing, and a
few optional features behave differently from Aureal's original DLLs. We don't
track a "percent complete" number.

## Features

| Area | State | How it's verified / caveats |
| --- | --- | --- |
| COM activation and public interfaces | Done for both DLLs | Exports, vtables and API behavior compared against the originals. Matching vtable sizes don't prove every slot behaves the same. |
| Source and listener controls | Done | Getters/setters (including initial listener orientation) and static audio scenes compared against the originals. |
| WAV/PCM playback | Done | Captured software output and an audible-render test. |
| A2D positional audio | HRTF software mixer | Output PCM matches the original in six static scenes. Other inputs and moving sources haven't been checked. |
| D2D native audio | DirectSound secondary buffers | Buffer descriptors and PCM checked in the native scene. |
| D3D and external hardware | Routing and buffers implemented | Untested on real Aureal hardware; all tests are software-only. |
| Timing-only EMU | Virtual playback and events | Produces no audio. Not the same thing as `EmulateHardware`. |
| Geometry and occlusion | Mostly done | Checked via API and scene comparisons. See open issues below. |
| Reflections and reverb | API objects done; behavior depends on backend | The original software paths may refuse effects. The optional software DSP is our addition. |
| MP3 | Optional, via minimp3 | Integration tests only. Output isn't claimed to match Aureal's decoder. |
| AC-3 | Optional, via liba52 or DirectShow | liba52 integration and load-failure handling are tested. Playback through a system DirectShow filter hasn't been confirmed. |
| A3D 1.x | Separate DLL with buffers, listener and DSP | Less coverage than `a3dapi.dll`. In GoldSrc, only the startup probe is tested. |
| Legacy hardware emulation | Optional `EmulateHardware` | Tested by replaying Quake III's startup checks, plus a positional PCM test. |

## Open issues and deliberate differences

- Hit refinement calls both volumetric helpers, but we don't yet know how the
  volume axes get initialized, so volumetric results are incomplete.
- List base identities and parts of recording behavior are still unknown.
- Older "missing code" notes may be out of date. Check the source and the
  [reconstruction records](llm/ARCHITECTURE.md#known-gaps) first.

`A3D_FIXES` turns on runtime-selectable additions and is ON by default. The
shipped config enables the audio additions and disables the credits additions.
To compare against the original DLLs, build with `A3D_FIXES=OFF`. See
[configuration](reference/configuration.md).

## Reading test results

A passing test only proves what it actually checks:

| If you see | It means |
| --- | --- |
| A method returns success | That call worked with those inputs. |
| A capability flag is set | The current config reports the feature. |
| A source reports playing | Its state says it's playing, possibly on a virtual (silent) route. |
| A capture has nonzero PCM | That route produced samples. |
| Left and right channels differ | The output is asymmetric. That alone doesn't mean spatialization is correct. |
| PCM matches the reference | It matches for that scene, format, time window and tolerance only. |
| A game starts with A3D | Startup worked. Gameplay and effects need their own checks. |

[Test coverage](llm/testing_coverage.md) lists past runs and specific
regressions. Each result only holds for the revision, devices and settings it
was recorded with. [Testing](development/testing.md) explains how to produce a
new result, including recording DLL hashes and configuration.

## Reporting compatibility

Include:

- game executable version or hash
- OS version
- paths of the loaded DLLs
- build options and runtime config
- audio backend
- steps to reproduce and what you heard

Report startup, playback, spatial audio, effects and shutdown separately.
Anything not tested on a given platform should be treated as unverified.
