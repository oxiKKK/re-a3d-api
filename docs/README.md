# A3D documentation

This manual describes the A3D system in this repository: the reconstruction of
Aureal's `a3dapi.dll` 3.3.677.0, the sibling A3D 1.x `a3d.dll`, and the project's
optional compatibility extensions. It covers using the DLLs, writing an A3D
client, understanding the implementation, and verifying changes.

## Choose a starting point

| Task | Read |
| --- | --- |
| Understand A3D without reading code | [Overview](overview.md), then [audio concepts](concepts.md) and [voices](voice.md) |
| Run a game | [Getting started](user/getting-started.md), [installation](user/installation.md), [game compatibility](user/game-compatibility.md) |
| Understand modern Windows output | [Modern systems](user/modern-systems.md) |
| Use an Aureal sound card | [Sound card hardware](user/hardware.md) |
| Write an application | [First application](programming/first-application.md), then [game integration](programming/game-engine-integration.md) |
| Find a method or constant | [SDK declarations](../inc/ia3dapi.h); [architecture](internals/architecture.md) links to implementations |
| Build or change the DLLs | [Building](development/building.md), [architecture](internals/architecture.md), [testing](development/testing.md) |
| Find current limitations | [Implementation and validation status](status.md) |

## User guides

- [Getting started](user/getting-started.md)
- [Installation, DLL loading, and removal](user/installation.md)
- [Choosing a configuration](user/configuration.md)
- [Modern Windows and other environments](user/modern-systems.md)
- [Sound card hardware](user/hardware.md)
- [Game compatibility and case studies](user/game-compatibility.md)
- [Sample applications](user/samples.md)
- [Troubleshooting](user/troubleshooting.md)

## Programming guides

- [Choosing and using the API](programming/api-overview.md)
- [First application](programming/first-application.md)
- [Initialization and object lifetime](programming/initialization-and-lifetime.md)
- [Sources, audio data, and playback](programming/sources-and-playback.md)
- [Positions, orientation, and spatial controls](programming/spatial-audio.md)
- [Acoustic geometry and materials](programming/acoustic-geometry.md)
- [Reflections and reverberation](programming/reflections-and-reverb.md)
- [Game engine integration](programming/game-engine-integration.md)
- [A3D 1.x clients](programming/legacy-a3d.md)

## Implementation

- [Architecture and source ownership](internals/architecture.md)
- [COM and binary interfaces](internals/com-and-abi.md)
- [Frame processing](internals/frame-processing.md)
- [Geometry engine](internals/geometry-engine.md)
- [Voice: playback instances and capacity](voice.md)
- [Resource management](internals/resource-management.md)
- [Audio backends](internals/audio-backends.md)
- [A2D: software renderer](internals/software-renderer.md)
- [D2D: DirectSound buffer playback](internals/backend-d2d.md)
- [D3D: DirectSound3D and driver controls](internals/backend-d3d.md)
- [EMU: virtual playback timing](internals/backend-emu.md)
- [External DALs and the W95 route](internals/backend-external.md)
- [Decoding and streaming](internals/decoding-and-streaming.md)
- [Threads and synchronization](internals/threading-and-synchronization.md)
- [Legacy bridge](internals/legacy-bridge.md)
- [Compatibility extensions](internals/compatibility-extensions.md)

## Reference and maintenance

- [Configuration](reference/configuration.md)
- [Building](development/building.md), [testing](development/testing.md), [diagnostics](development/diagnostics.md)
- [Reconstruction method](development/reconstruction-method.md), [maintaining documentation](development/documentation.md)
- [Binary and source records](llm/README.md)

## How to interpret claims

The SDK describes the intended API. Source describes this reconstruction.
Build-tagged binary evidence describes the original. These can disagree.
Original defects are preserved except where a documented extension setting changes
them. A feature being declared, returning success, or having a reference address
does not establish that it renders correctly. See [status](status.md) for the
limits of the available tests.

Commands use PowerShell from the repository root unless a page says otherwise.
Paths such as `build-game` are build directories created by the documented
commands. The source files, headers, and evidence linked throughout this manual
are the authority for implementation details.
