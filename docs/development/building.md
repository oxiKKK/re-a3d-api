# Building the project

The supported build produces Windows x86 DLLs with MSVC. The source uses
Windows interfaces and x86 assembly; other architectures/toolchains are not
provided as supported build targets.

## Prerequisites

- Windows with Visual Studio 2022 C++ tools and the x86 toolchain.
- A Windows SDK providing DirectSound, multimedia, COM, and graphics headers/libraries.
- CMake 3.21 or newer.
- Python 3 when automated tests or analysis/documentation tools are used.
- The tracked reference DLLs in `ref/` for binary/API/audio comparisons.

GoogleTest and optional decoder source dependencies are vendored. The optional
`PropSets` and `volsrc` samples need separate SDK material described in
[samples/README.md](../../samples/README.md).

## Standard build

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release
```

DLLs, samples, and native tools are written to `build/Release/`. Debug uses
`--config Debug` and `build/Debug/`. Use `--target a3dapi a3d` to build just
the DLL targets when both are enabled.

The DLL targets use C++98, the static CRT (`/MT` or `/MTd`), and
`/arch:IA32 /fp:precise`. The configured x87 arithmetic supports fidelity to
the original compiler's behavior. Tests and tools can use different C++ language
requirements; do not apply a DLL restriction globally without checking them.

## Reference-comparison build

```powershell
cmake -S . -B build-reference -G "Visual Studio 17 2022" -A Win32 -DA3D_BUILD_TESTS=ON -DA3D_BUILD_SAMPLES=OFF -DA3D_FIXES=OFF
cmake --build build-reference --config Release
ctest --test-dir build-reference -C Release --output-on-failure
```

Known reference failures or hangs can remain; see [testing](testing.md) and
[status](../status.md). This configuration defines the comparison experiment.

## Game and decoder builds

The [game quick start](../user/getting-started.md) enables capability emulation
and fixes. Keep it in a separate directory from reference comparisons.

For decoder integration, use an additions-enabled build and keep both decoder
settings enabled in its configuration file:

```powershell
cmake -S . -B build-decoders -G "Visual Studio 17 2022" -A Win32 -DA3D_BUILD_TESTS=ON -DA3D_BUILD_SAMPLES=OFF -DA3D_FIXES=ON
cmake --build build-decoders --config Release
.\build-decoders\Release\a3d_linked_implementation_tests.exe --gtest_filter=Decoder.*
```

Dependency provenance and output limitations are in
[decoding](../internals/decoding-and-streaming.md). All options and defaults are
in [configuration reference](../reference/configuration.md).

## Inspect and troubleshoot a build

At the end of configuration, CMake prints an aligned summary using an explicit
message for each project setting. Values are printed as configured; unset paths
are blank. Update the summary in the root `CMakeLists.txt` when adding settings.
Use the cache listing below to inspect additional cache entries.

```powershell
cmake -LA -N build
ctest --test-dir build-reference -C Release -N
```

The first lists cached settings; dependency overrides can affect effective
values. The second lists configured tests without running them. Configuration
errors for non-MSVC or non-32-bit builds are intentional. Missing optional
sample dependencies cause those sample targets to be skipped.

A CMake build does not register the DLLs. Samples using COM can still load an
older server until [registration](../user/installation.md) is changed. Runtime
tests normally use the Release DLL even when the test executable is Debug;
build Release first or explicitly set `A3D_COMPARISON_DLL`.
