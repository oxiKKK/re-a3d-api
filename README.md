# A3D reconstruction

C++ reconstruction of Aureal's **A3D API 3.3.677.0** (`a3dapi.dll`) and its
A3D 1.x compatibility library (`a3d.dll`). It includes the COM interfaces,
geometry engine, resource manager and software audio renderer.

The reconstruction follows the reference binaries, including their defects.
Implementation gaps and test failures remain; see [current status](docs/status.md).

## Build

Requires Windows, CMake 3.21 or newer, and MSVC with x86 C++ build tools.
Run from the repository root:

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A Win32
cmake --build build --config Release
```

The DLLs are written to `build/Release/`. Use `--config Debug` for a Debug
build. Both use the static CRT and x87 floating-point instructions to match
the original compiler's behavior.

See [build configurations](docs/development/building.md) for reference comparisons
and decoder builds, and [getting started](docs/user/getting-started.md) for a
game-oriented emulation build. [Configuration reference](docs/reference/configuration.md)
covers build and runtime settings. The default `A3D_FIXES=ON` build ships
[a3dapi.conf](a3dapi.conf) beside the DLL. Edit its booleans and restart the game;
no rebuild is needed. Audio additions default to enabled, credits additions to
disabled. Use `A3D_FIXES=OFF` for reference comparisons.

## Documentation

Start at the [complete documentation](docs/README.md) for user guides, game integration,
implementation details, and the public API reference.

## Find your way around

| Path | Contents |
| --- | --- |
| `src/a3dapi/` | A3D API implementation |
| `src/a3d/` | A3D 1.x compatibility library |
| `inc/` | Original SDK headers |
| `tests/`, `tools/`, `samples/` | Tests, developer tools and sample applications |
| `ref/` | Reference binaries (not distributed; see [ref/README.md](ref/README.md)) |
| `artifacts/` | Ignored captures, logs and dumps |

- [A3D overview](docs/overview.md): concepts and application flow.
- [Architecture](docs/internals/architecture.md): implementation and object ownership.
- [Audio backends](docs/internals/audio-backends.md): device selection and rendering limits.
- [Modern systems](docs/user/modern-systems.md): Windows audio and environment support.
- [Sound card hardware](docs/user/hardware.md): not tested on real Aureal cards; what that means for you.
- [Programming](docs/programming/first-application.md): a complete client example.
- [API overview](docs/programming/api-overview.md): scene objects, activation, and links to declarations.
- [Developer tools](tools/README.md): capture, diagnostics and binary analysis.
- [Reference binaries](ref/README.md): versions, hashes and build differences.

## Provenance

Reconstructed from the binaries in IDA. Source citations use virtual addresses
at imagebase `0x10000000`, qualified by module and build. Aureal's copyright
notices are retained. For study only.
