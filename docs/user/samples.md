# Sample applications

Samples demonstrate API call sequences and provide interactive checks. They
are useful for understanding a feature, but are not all automated parity tests.
Build them with `A3D_BUILD_SAMPLES=ON` as described in
[building](../development/building.md).

| Sample | Topic |
| --- | --- |
| `polygon` | Geometry submission and moving spatial sound |
| `loading` | Source loading and supported file formats |
| `reflect` | Geometry reflections |
| `reverb` | Environmental reverb |
| `PropSets` | Property-set access; requires Creative's `eax.h` |
| `ManualReflections` | Explicit reflection objects |
| `volsrc` | Volumetric sources; requires the optional older DirectX sample framework |
| `SceneRooms` | Project-added scene inspection, scripted runs, and logs |

The exact target names and dependencies are in
[samples/CMakeLists.txt](../../samples/CMakeLists.txt). The historical sources
retain their original text. Media copied beside the executables includes WAV,
MP3, and AC-3 files; copying an encoded file does not enable its decoder.

## Running SceneRooms

SceneRooms supports selecting the reconstruction or reference, scripting,
and text logging. Its command-line defaults, controls, and comparison commands
are maintained in [samples/README.md](../../samples/README.md).

The usual comparison runs the reference twice before comparing it to the
reconstruction. That establishes which fields vary between otherwise identical
runs. Playback positions and status can depend on timing. A refusal of
reflections or reverb on both sides validates refusal behavior. Validating
the effect requires rendered audio.

## Running historical samples

Run samples from their output directory so relative media paths resolve.
Their SDK utilities can use registration-based activation. If a sample loads
the wrong server, follow [installation](installation.md); the presence of a
fresh DLL in the build tree does not establish that the sample used it.

Inspect the sample's source before copying its initialization or shutdown
sequence into a new application. The [programming guides](../programming/first-application.md)
explain the reconstruction's lifetime constraints and error handling.

If a sample is unavailable because of optional graphics prerequisites, use
`a3d_sdk_loading_probe` or `a3d_pcm_capture` to exercise the relevant audio path.
See [diagnostics](../development/diagnostics.md).
