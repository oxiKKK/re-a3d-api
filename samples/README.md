# Samples

Seven original programs from the A3D 3.0 SDK, plus the project-added
`SceneRooms` comparison application. Original source came from
`resources/a3d/A3D SDK 3.0/A3D 3.0 SDK/sdk/src/`.

| Program | Demonstrates |
| --- | --- |
| `polygon` | Occlusion by a quad |
| `reflect` | Reflections and materials |
| `loading` | WAV, MP3 and AC-3 loading/streaming |
| `reverb` | I3DL2 reverb presets |
| `ManualReflections` | Application-defined reflections |
| `PropSets` | Creative EAX property sets |
| `volsrc` | Source/listener volumes in a DirectDraw visualizer |
| `SceneRooms` | Distance, panning, occlusion, Doppler, cones, head-relative sound, reflections and reverb |

## Build and run

Samples build by default with the [root build commands](../README.md#build).
Media files are copied beside the executables. MP3 and AC-3 playback requires
[decoder support](../third_party/README.md).

Two programs need additional SDK files:

- `PropSets`: set `A3D_EAX_INCLUDE` to the directory containing Creative's `eax.h`.
- `volsrc`: set `A3D_DX_D3DIM` to the **DirectX 6** SDK's
  `samples/Multimedia/D3DIM` directory, containing `include` and `src/d3dframe`.
  The build stages its headers and compiles the framework sources. The DirectX 7
  framework renamed `CD3DFramework` and removed `D3DEnum_DriverInfo`, so it is
  incompatible.

These targets are skipped when their dependencies are unset. `lib/glaux.c` is
a project replacement for the eight legacy glaux functions used by `polygon`
and `reflect`; changes to original sample code must carry a reason at the site.

The original samples use the registered `CLSID_A3dApi` server. To select one,
use [Set-A3dApiServer.ps1](../tools/configuration/Set-A3dApiServer.ps1).
`SceneRooms` can load a DLL directly without changing registration.

## SceneRooms

```powershell
.\build\Release\SceneRooms.exe --dll ref --room occlusion
.\build\Release\SceneRooms.exe --dll ours
```

`--dll` accepts `ref` (677 Retail), `ours` (`build/Release/a3dapi.dll`), `reg`
(registered server) or a file path. One source plays in the selected room.
The camera represents the listener; source brightness shows audibility and
a red listener/source line indicates occlusion.

| Controls | Action |
| --- | --- |
| WASD / mouse | Move / look |
| PgUp, PgDn / number keys | Select room |
| F1 | Cycle first-person, chase and plan views |
| F2 | Remove/restore room walls |
| F3 / F4 | Toggle occlusion / reflections |

`--script` runs a fixed path through each room. `--frames` sets frames per
room; `--settle` defaults to 50 frames before measurements, allowing tracing
to stabilize. Play position and status remain timing-dependent.

```powershell
.\build\Release\SceneRooms.exe --dll ref --script --log ref1.log
.\build\Release\SceneRooms.exe --dll ref --script --log ref2.log
powershell -File tests\audio\Compare-SceneLog.ps1 ref1.log ref2.log
.\build\Release\SceneRooms.exe --dll ours --script --log ours.log
powershell -File tests\audio\Compare-SceneLog.ps1 ref1.log ours.log
```

Compare two reference runs first to check repeatability. Reflections and reverb
may be refused (`0x8004003E` and `0x8004003D`); matching direct-path results in
those rooms do not validate either effect.

Two recorded API constraints affect the runner: `SetCooperativeLevel` can hang
with a console window on both DLLs, so `SceneRooms` uses `InitEx` with
`GetDesktopWindow()`. `Shutdown()` destroys dependent objects; releasing them
afterward caused a use-after-free in the reference.
