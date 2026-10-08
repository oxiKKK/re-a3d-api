# Developer tools

Build native commands with `A3D_BUILD_DIAGNOSTICS=ON` (default). They target
Windows x86, accept `--help`, and are written to `<build>/<configuration>/`.
Audio tests also build `a3d_pcm_capture` when diagnostics are disabled.

| Command | Purpose |
| --- | --- |
| `dump_thread_stacks` | Sample a process's thread stacks; briefly suspends each inspected thread |
| `a3d_goldsrc_init_probe` | Reproduce GoldSrc's A3D initialization calls |
| `a3d_software_playback_probe` | Exercise software playback with a WAV file |
| `a3d_sdk_loading_probe` | Run selected SDK call sequences against an explicit DLL |
| `a3d_pcm_capture` | Record software-rendered PCM using a process-local DirectSound substitute |

Run from the repository root. Playback probes may produce audio.

```powershell
.\build\Release\dump_thread_stacks.exe --pid 1234 --module a3dapi.dll --samples 3
.\build\Release\a3d_goldsrc_init_probe.exe --dll .\build\Release\a3dapi.dll --timeout-ms 20000
.\build\Release\a3d_software_playback_probe.exe --dll .\build\Release\a3dapi.dll --wave .\samples\data\heli.wav
.\build\Release\a3d_sdk_loading_probe.exe --dll .\build\Release\a3dapi.dll --media-dir .\samples\data --scenario occlusion
.\build\Release\a3d_pcm_capture.exe --dll .\build\Release\a3dapi.dll --wave .\samples\data\heli.wav --scene right --output .\artifacts\captures\right-reconstruction
```

SDK-probe DLL/media paths are resolved before it changes working directory.
PowerShell helpers have comment-based help: `diagnostics/Watch-A3dHang.ps1`
launches and monitors a process; `configuration/Set-A3dApiServer.ps1` changes
the per-user COM server selection; `coverage/Run-Coverage.ps1` measures line
coverage of `src/a3dapi` with OpenCppCoverage in `build-cov/`. Automated comparisons do not use that script.

## Capture output

Named capture commands require DLL and WAV paths. The output directory must
be new; by default it is a unique directory under `artifacts/captures/`.

| File | Contents |
| --- | --- |
| `run.json` | Version, build, architecture, arguments, DLL path/hash, timing and status |
| `capture.tsv` | Versioned statistics and buffer records |
| `capture-<index>.wav` | PCM from each written buffer |

Diagnostics go to stdout. Scenes are `default`, `mono`, `right`, `behind`,
`native`, `pitch`, `doppler`, `eq`, `far`, `near` and `listener`; use
`--list-scenes` to list them. Effects use
`--effect none|reverb|reflect|manual`. `--duration-ms` excludes the 500 ms warmup;
the default process deadline is 30 seconds.

Exit codes: 0 audible PCM, 1 runtime failure, 2 invalid arguments, 3 missing
input, 124 timeout. Native faults record their exception code in the manifest.
Legacy positional arguments/stdout records remain supported. Use named
arguments and `capture.tsv` for new automation.

## Binary analysis

Python tools are under `analysis/`; their `--help` describes inputs. Run from
the repository root. PE extraction needs `ref/`; IDA tools additionally need
IDA Python/idalib and a matching database. Evidence is stored in `docs/llm/groundtruth/`.

| Command under `analysis/` | Inputs | Output |
| --- | --- | --- |
| `extract_filelines.py` | Debug PE image | Assertion/file-line call sites (TSV) |
| `infer_source_file_regions.py` | File-line TSV | Source-file address regions |
| `extract_functions.py` | PE image and evidence tables | Function inventory |
| `extract_vtables.py` | PE image and evidence tables | Vtable slot inventory |
| `extract_symbols.py` | Debug PE image, optional regions | Linker symbol evidence |
| `extract_labels.py` | PE image and regions | Trace method labels |
| `extract_types.py` | PE image and file-line evidence | Type-name evidence |
| `extract_sdkdocs.py` | SDK document | Extracted reference text |
| `extract_exception_handlers.py` | Reference debug image | Exception-handler evidence |
| `match_debug_release_functions.py` | Debug/Release images and inventories | Function address mapping |
| `generate_class_inventory.py` | Evidence tables | Class inventory |
| `generate_class_name_report.py` | Source and evidence tables | `docs/llm/CLASSMAP.md` |
| `generate_source_file_report.py` | Source and evidence tables | `docs/llm/FILEMAP.md` |
| `generate_class_layout_report.py` | Compiler layout report | `docs/llm/LAYOUT.md` |
| `generate_address_report.py` | Source address annotations and evidence | `docs/llm/OFFSETS.md` |
| `report_reconstruction_coverage.py` | Source annotations and function inventory | Citation coverage report |
| `classify_missing_reconstructions.py` | Coverage/evidence tables | Missing-reconstruction classifications |
| `compare_dll_imports.py` | Rebuilt and reference DLL imports | Import differences |
| `check_assertion_side_effects.py` | ASSERT/TRACE/DBGSTR arguments | Unreviewed calls and mutations; exit 1 when found |
| `compare_assertion_sites.py` | Rebuilt assertion extract and reference evidence | Assertion reproduction report |
| `ida/export_pseudocode.py` | IDB and function mapping | Pseudocode files |
| `ida/verify_function_candidates.py` | IDB and candidate inventory | Decompilation evidence |

`pe_image.py` is the shared PE reader. Report generators overwrite their
documented outputs. Citation coverage counts addresses, not executed behavior.

`codegen/generate_interface_stubs.py` writes interface stubs;
`codegen/generate_impulse_wav.py` creates the reverb click fixture.
Run generators explicitly and review their output. Put logs, captures and
dumps in ignored `artifacts/`. Shared runtime helpers are in `support/`;
automated comparisons are documented in [tests](../tests/README.md).

## Documentation tools

`documentation/check_links.py` checks local documentation files, heading
fragments, and source-line links using the Python standard library.
`documentation/render_diagrams.mjs` renders Mermaid blocks for visual review
in light and dark themes. Declarations and function contracts stay in code;
the manual does not generate API inventories.

```powershell
python tools\documentation\check_links.py
```

See [documentation maintenance](../docs/development/documentation.md) for
ownership and validation rules.
