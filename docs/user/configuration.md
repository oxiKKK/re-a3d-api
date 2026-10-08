# Choosing a configuration

Build with `A3D_FIXES=ON` and keep [a3dapi.conf](../../a3dapi.conf) beside the
loaded DLL. Edit its booleans and restart the game. No rebuild is needed.
The shipped defaults enable hardware-capability emulation, both software
effects, property-wait fixes and replacement decoders. Credits additions are off.

| Purpose | Configuration |
| --- | --- |
| Run a game that insists on hardware A3D | Keep `EmulateHardware=true`; Quake III also needs `SoftwareReverb=true` in the tested setup |
| Use hardware acquisition | Set both `SoftwareReverb=false` and `SoftwareReflections=false` |
| Experiment with one software effect | Enable its setting independently; either effect selects A2D |
| Compare against 677 Retail | Build separately with `A3D_FIXES=OFF` |

The [configuration reference](../reference/configuration.md) covers parsing,
missing-file defaults, removed build options and historical registry inputs.
A2D software effects do not establish Aureal hardware DSP equivalence.

## Output and game settings

First verify basic WAV playback and positional output with the documented
configuration. Then enable the game's A3D option using its version-specific
instructions. Check the game's logs and the loaded module path; an enabled menu
item alone does not prove that A3D is active.

Use the output mode that matches the client and listening arrangement. Native
stereo and A3D-positioned sources take different routes. Additional endpoint
effects or a DirectSound replacement introduce another processing stage;
record those settings when comparing results.

## Runtime settings

Historical registry values control selected device, diagnostic, and tracing
paths. `UseDALInterface` can request an external driver DAL; enabling it on a
machine without that server can prevent initialization. Refer to the source-backed
[runtime inventory](../reference/configuration.md#runtime-settings) before
changing any value.
