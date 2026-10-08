# Getting started

Start with a WAV capture to verify the software path, then try a sample or a
game. This separates a library/build problem from a game's loading and
capability requirements.

## Build for modern game experiments

Install the prerequisites in [building](../development/building.md), then run:

```powershell
cmake -S . -B build-game -G "Visual Studio 17 2022" -A Win32 -DA3D_FIXES=ON -DA3D_BUILD_TESTS=ON
cmake --build build-game --config Release
ctest --test-dir build-game -C Release -R "^(audible_render_gate|q3_emulation_playback)$" --output-on-failure
```

The DLLs and executables are in `build-game/Release/`. The accompanying `a3dapi.conf`
enables the legacy hardware-capability workaround and software effects by
default. Copy it with the DLL and restart the game after edits. Use
the separate [reference build](../development/building.md#reference-comparison-build)
when testing fidelity to Aureal's binary.

## Capture a positioned source

```powershell
.\build-game\Release\a3d_pcm_capture.exe --dll .\build-game\Release\a3dapi.dll --wave .\samples\data\Heli.wav --scene right
```

The tool reports a new capture directory under `artifacts/captures/`. It writes
a manifest, statistics, and WAV files using a process-local DirectSound
recorder. It does not require registering this DLL globally. A successful
capture confirms PCM production in that isolated path; it does not test the
computer's speakers or a game's sound engine.

## Try a client

Use [sample applications](samples.md) for an interactive scene, or follow the
version-specific instructions in [game compatibility](game-compatibility.md).
Read [installation](installation.md) before changing registration: placing
`a3dapi.dll` next to an executable and choosing a COM server are different
loading mechanisms.

If capture succeeds but a client is silent, inspect its loaded DLL, requested
features, and output route. [Troubleshooting](troubleshooting.md) gives an
ordered diagnosis. Available features and known limitations are summarized in
[status](../status.md).
