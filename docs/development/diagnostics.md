# Diagnostics and capture

Use an explicit-DLL probe to isolate a call sequence, PCM capture to measure
rendering, and thread stacks to locate a wait. Each tool answers a different
question.

| Tool | Purpose |
| --- | --- |
| `a3d_goldsrc_init_probe` | Bounded GoldSrc-style startup sequence |
| `a3d_software_playback_probe` | Source loading and real-device playback sequence |
| `a3d_sdk_loading_probe` | Selected SDK loading/effect scenarios |
| `a3d_pcm_capture` | Process-local DirectSound recorder and PCM measurements |
| `dump_thread_stacks` | Thread stack sampling in an existing process |
| `Watch-A3dHang.ps1` | Launch/monitor a process and gather hang diagnostics |
| `Set-A3dApiServer.ps1` | Per-user COM server selection, separately from tests |

Build native tools with `A3D_BUILD_DIAGNOSTICS=ON`. Audio tests can build the
capture tool even when the other diagnostics are disabled. Each executable
supports `--help`. The authoritative invocation and output contracts are in
[tools/README.md](../../tools/README.md).

## Capture an experiment

```powershell
.\build-game\Release\a3d_pcm_capture.exe --dll .\build-game\Release\a3dapi.dll --wave .\samples\data\Heli.wav --scene right --effect none
```

Named arguments require a DLL and input file. The output directory must be new;
omitting it creates a unique directory. `--duration-ms` excludes a 500 ms
warmup. Use `--list-scenes` to inspect available scenes and `--effect` to
select `none`, `reverb`, or `reflect`.

| Artifact | Contents |
| --- | --- |
| `run.json` | Schema/version, invocation, DLL path/hash, architecture, timing, and status |
| `capture.tsv` | Statistics and buffer records |
| `capture-<index>.wav` | PCM written to each captured buffer |

Exit codes are 0 for observed audible PCM, 1 for runtime failure, 2 for invalid
arguments, 3 for missing input, and 124 for timeout. Native faults retain their
exception code in the manifest. Other probes have their own exit contracts;
do not apply the capture meaning to every tool's exit 0.

## Locate a hang

When using `Watch-A3dHang.ps1`, pass `-GameDir` explicitly with the directory
containing the client executable. The script has no default game location.

```powershell
.\build-game\Release\dump_thread_stacks.exe --pid 1234 --module a3dapi.dll --samples 3
```

Replace the process ID. The tool briefly suspends inspected threads. Sample
more than once to distinguish a persistent wait from transient work. Record
the last completed public call and identify service/mixer/streaming workers
using [threading](../internals/threading-and-synchronization.md).

Reference Debug DLLs link the CRT statically and can display assertions that
external CRT hooks do not suppress. Prefer bounded child processes when
replaying a failure. The property-set waits documented under
[extensions](../internals/compatibility-extensions.md) are known causes.
Use thread stacks to identify the cause of each hang.

## Binary inspection

Analysis tools extract PE functions, vtables, assertions, labels, symbols, and
SDK text. IDA/idalib is needed only for the IDA-specific tools. Use the
matching reference image/database and build-tagged addresses. Start with
[reconstruction method](reconstruction-method.md); the full tool inventory is
in the tools README.
