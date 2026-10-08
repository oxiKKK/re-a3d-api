# Configuration reference

This is the inventory of build and runtime settings. Defaults below come from
the current [root CMake file](../../CMakeLists.txt),
[sample configuration](../../samples/CMakeLists.txt), and
[test configuration](../../tests/CMakeLists.txt). Existing build directories
retain their cached values.

## Build options

| Option | Default | Effect |
| --- | --- | --- |
| `A3D_BUILD_A3D` | ON | Build the sibling A3D 1.x DLL |
| `A3D_BUILD_SAMPLES` | ON | Build samples whose prerequisites are present |
| `A3D_BUILD_DIAGNOSTICS` | ON | Build probes and native diagnostics |
| `A3D_BUILD_TESTS` | OFF | Build/register automated comparisons and integration checks |
| `A3D_FIXES` | ON | Include project additions and DLL-local runtime configuration |

`A3D_FIXES=OFF` excludes the optional implementations and ignores the
configuration file. It retains the reconstruction's unavailable decoder stubs;
it does not restore the proprietary decoder bodies.

## DLL-local configuration

The shipped [a3dapi.conf](../../a3dapi.conf) describes each setting and its
default beside the editable values. Put it beside the loaded `a3dapi.dll` and
restart the game after editing. All audio additions default to enabled; both
credits additions default to disabled. The same defaults apply without a file.

`EmulateHardware`, `SoftwareReverb` and `SoftwareReflections` are independent.
Either effect selects A2D instead of hardware acquisition. Reverb alone grants
no software reflection capacity. Existing historical registry readers remain
in effect; this file does not override `UseDALInterface` or other registry
settings.

Settings are read once, outside DLL initialization, and remain fixed until
module unload. Values accept case-insensitive `true`/`false` and `1`/`0`.
Missing, unreadable, invalid or truncated values use the corresponding default;
unknown keys are ignored. No file is read from the current working directory.
The DLL never rewrites the file. Builds copy the sample only when no file exists.
The loader and settings contract are in
[A3dConfig.cpp](../../src/a3dapi/A3dConfig.cpp) and its owning header.

## Migration from removed build options

These CMake options have been removed. Supplying one produces a migration
warning and does not change the DLL's settings. Build with `A3D_FIXES=ON` and
edit the corresponding key in the `[A3D]` section:

| Removed option | Runtime key |
| --- | --- |
| `A3D_EMULATION` | `EmulateHardware` |
| `A3D_HW_REVERB_EMULATION` | `SoftwareReverb` |
| `A3D_HW_REFLECTION_EMULATION` | `SoftwareReflections` |
| `A3D_MP3_MINIMP3` | `EnableMP3Decoder` |
| `A3D_AC3_LIBA52` | `EnableAC3Decoder` |
| `A3D_ROLLING_CREDITS_NEW` | `UseNewCredits` |

The former combined effect build is represented by enabling both
`SoftwareReverb` and `SoftwareReflections`. Use `A3D_FIXES=OFF` for reference
builds; runtime-disabled additions remain compiled into an ON build.

## Build paths and test inputs

| Cache variable | Default | Use |
| --- | --- | --- |
| `A3D_COMPARISON_DLL` | `<build>/Release/a3dapi.dll` | Runtime comparison target, including when the test executable is Debug |
| `A3D_EAX_INCLUDE` | Empty | Directory containing `eax.h`; enables `PropSets` |
| `A3D_DX_D3DIM` | Empty | DirectX 6 D3DIM sample-framework directory; enables `volsrc` |
| `A3D_PYTHON` | Program discovered by CMake | Retained Python cache entry for existing build directories |
| `Python3_EXECUTABLE` | Discovered when tests are enabled | Interpreter for diagnostic source checks |

Standard CMake generator, platform, and configuration options also apply. The
project requires MSVC x86; see [building](../development/building.md).

## Runtime settings

The following values are read under `HKLM\Software\Aureal\A3D` in the process's
registry view unless specified otherwise. These are historical implementation
inputs; inspect their readers before changing them. Reads do not always
validate registry types or initialize every destination when a value is absent.
Use the located reader before prescribing a numeric value.

| Value | Reader and established effect |
| --- | --- |
| `UseDALInterface` / `UseDalInterface` | [Class factory](../../src/a3dapi/a3dclsfc.cpp): nonzero selects external DAL activation; root also reads it. Registry value names are case-insensitive |
| `DisableReflections` | [Root constructor](../../src/a3dapi/A3dRoot.cpp): reads reflection-disable state; later compatibility/source paths determine its effect |
| `DisableOcclusions` | Root constructor: reads occlusion-disable state |
| `do_refs_every` | Root constructor: reads legacy reflection scheduling state; do not equate it automatically with the public update-interval setter |
| `do_occs_every` | Root constructor: reads legacy occlusion scheduling state |
| `geom_reverb_ctrl` | Root/init path: controls forced geometric-reverb policy according to private constants |
| `ref_orders` | [Flush](../../src/a3dapi/A3d3.cpp): polled at most once per 2 seconds and passed to compatibility mode 1012 |
| `A2DBufferSize` | [DAL_A2D::Init](../../src/a3dapi/dal_a2d.cpp): output buffer bytes, capped at the implementation maximum; no minimum is enforced at this read |
| `StreamingBufferLatency` | [ResMan::SetBufferLatency](../../src/a3dapi/resman.cpp): overrides the requested value if 0..5000 ms |
| `StreamingBufferRefreshThreshold` | Resource-manager threshold setter: override if 0..5000 ms |
| `debug_enabled` | [Mapper Debug log](../../src/a3dapi/apimapper.cpp): value 1 enables configured file logging |
| `debug_outputfile` | Mapper Debug log: path opened for writing |
| `SplashPath`, `SplashAudio`, `SplashScreen` | [Splash implementation](../../src/a3dapi/splash.cpp): executable location and waiting behavior; the reference builds differ in splash support |

`DalInfo` also reads `A3DMode` or `A3DType` from
`HKLM\Software\Aureal\Vortex\ControlPanel`, or its `AU8810\ControlPanel`
variant, for specific device capability adjustments. These are driver-specific
inputs for the named Aureal devices.

COM server selection uses `Software\Classes\CLSID\...\InprocServer32`. The per-user helper changes that registration as
described in [installation](../user/installation.md).

## Application controls

Output mode/gain, coordinate system, units, source priorities, resource limits,
stream properties, and acoustic modes are also controlled through API methods.
Use the [API overview](../programming/api-overview.md) to find the relevant
declarations and implementations. `Compat` is an internal mode
dispatch with build-specific meanings; its modes are declared in
[a3d33.h](../../src/a3dapi/a3d33.h).

## Reproduce a configuration

Record the commit and working-tree changes, generator/platform, Release/Debug
configuration, A3D_FIXES, the configuration file or its absence, relevant registry overrides, DLL hash, and loaded
DirectSound implementation. Explicitly set behavior-changing options in
reference builds.
