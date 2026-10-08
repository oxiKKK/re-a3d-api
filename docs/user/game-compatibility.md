# Game compatibility and case studies

Compatibility is recorded for an executable and a call sequence. The following
evidence does not establish support for every release of a game or every effect.

| Client | Evidence | Scope |
| --- | --- | --- |
| Quake III demo executable identified below | Inspected startup plus a dedicated playback-contract regression | IA3d4 initialization, hardware capability gate, source limits, and positional PCM |
| GoldSrc-style initialization | Dedicated probe | Startup sequence only; no full-game compatibility claim |
| SDK samples and SceneRooms | Interactive clients and selected API comparisons | Individual features and scripted scenes, subject to backend support |

## Quake III demo

The inspected `quake3.exe` has SHA-256
`2c0dd4d06e3282e0abc41ebc8e84e2d9868a17182ac175b232a9bb604a8269a3`.
It creates `IA3d4`, initializes with feature mask `0x42`, and checks
`GetHardwareCaps().dwFlags & 0x28`. The emulation configuration lets the software
device satisfy this historical gate. The call-site evidence and measured
regression details are in [the backend record](../llm/EMULATION.md#quake-iii-capability-gate)
and [coverage record](../llm/testing_coverage.md#q3-emulation-validation).

Build the [game configuration](getting-started.md), then preserve any existing
`a3dapi.dll` and copy `build-game/Release/a3dapi.dll` beside this executable.
This demo writes COM registration with a bare DLL filename during startup;
verify its loaded path when another A3D installation is present.

Run from the game directory:

```powershell
.\quake3.exe +set r_fullscreen 0 +set logfile 2 +map q3dm1 +s_enable_a3d
```

The map precedes the enable command because early command processing can occur
before sound initialization. In the console, inspect `s_usingA3D`. Startup
should log `Initializing Aureal A3D...` and `ok` in `demoq3/qconsole.log`.
These log messages establish startup. Validate acoustic behavior separately.

The isolated regression is:

```powershell
ctest --test-dir build-game -C Release -R "^q3_emulation_playback$" --output-on-failure
```

It replays the capability contract and captures positional audio through an
IA3d5 query after the IA3d4 startup. It does not execute Quake III's complete
sound engine or validate the game's geometry effects.

## GoldSrc initialization probe

```powershell
.\build-game\Release\a3d_goldsrc_init_probe.exe --dll .\build-game\Release\a3dapi.dll --timeout-ms 20000
```

This probe isolates a historical initialization pattern and gives hangs a
deadline. Its [source](../../tools/probes/goldsrc_init/main.cpp) is the exact
sequence being exercised. It does not load a map, run a game's mixer, or prove
that the reconstructed `a3d.dll` matches the variant shipped with Half-Life.

## Adding a game result

Record executable identity, launch command, A3D API version, loaded module
paths/hashes, build options and runtime configuration, operating system, and output implementation.
Exercise startup, repeated sounds, moving sources, listener rotation, level
changes, pause/focus transitions, and exit. For effects, use scenes that can
distinguish direct sound, occlusion, individual reflections, and a reverb tail.

Attach logs or captures to the result and identify untested behavior. Keep
observations separate from inferences about how the game uses A3D.
