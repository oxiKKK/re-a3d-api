# Troubleshooting

First identify the loaded DLL, A3D_FIXES and its configuration file, then separate initialization,
source state, PCM production, and real-device output. Keep the first failing
call and its hexadecimal result in the report.

| Symptom | Check | Next action |
| --- | --- | --- |
| DLL fails to load | Client architecture, file path, dependencies | Use the x86 build and an explicit-DLL probe |
| COM class is unavailable | Current-user and machine 32-bit registration | Follow [installation](installation.md); verify the loaded path after restarting |
| Game does not offer A3D | Game version and capability checks | Try its documented emulation configuration; inspect startup logs |
| Initialization fails | Requested features, window, DirectSound device, external DAL setting | Begin with direct-path playback and a valid window; inspect the first error |
| Source reports playing but is silent | Virtual status, [voice capacity](../voice.md#audible-virtual-hardware-and-software-voices), gain, position, `Flush`, selected route | Capture the `default` and `right` scenes |
| Capture works but speakers are silent | Client output route and Windows endpoint | Verify the game uses the same DLL and test real playback separately |
| Left/right or front/back seems wrong | Coordinate handedness, listener orientation, units, output mode | Use a fixed listener and known positions before testing motion |
| Reflection/reverb is missing | Init features, per-source/geometry flags, backend, runtime effect settings | Read [effects](../programming/reflections-and-reverb.md) and [status](../status.md) |
| MP3 or AC-3 fails while WAV works | Runtime decoder settings and DirectShow availability | Check [decoding](../internals/decoding-and-streaming.md) |
| Audio distorts or changes pitch | Gain, units, velocity, pitch, input format | Start with a known WAV and neutral controls |
| Application hangs | Last completed call and worker stacks | Use a deadline and capture stacks with [diagnostics](../development/diagnostics.md) |
| Crash during exit | Outstanding interfaces and explicit `Shutdown` | Follow [object lifetime](../programming/initialization-and-lifetime.md) |

## Minimal reproduction

```powershell
.\build-game\Release\a3d_pcm_capture.exe --dll .\build-game\Release\a3dapi.dll --wave .\samples\data\Heli.wav --scene default
.\build-game\Release\a3d_pcm_capture.exe --dll .\build-game\Release\a3dapi.dll --wave .\samples\data\Heli.wav --scene right
```

Use the printed capture directories. Compare `run.json`, `capture.tsv`, and
the WAV output. An audible-capture exit code means nonzero PCM was observed;
perceptual quality and hardware equivalence require separate evaluation.

## Hangs and original defects

The original can block during property-set work or initialization on modern
software devices. `FixPropertyDeadlocks` changes selected paths in an
`A3D_FIXES` build; compare both settings before
comparing two runs. Debug references can display modal assertion dialogs.
Use child-process deadlines when running unfamiliar call sequences.

Do not continue using interfaces after `Shutdown`: the reconstructed method
deletes the root. Releasing already-destroyed dependent objects can cause a
use-after-free. Prefer orderly `Release`-based cleanup in new clients.

## Useful failure report

Include the command, working directory, input file, DLL hashes, CMake options,
operating system, game/probe version, full HRESULT or exception code, and the
last completed operation. Attach a capture or stack log when relevant. Preserve
the original failure before changing several settings at once.
