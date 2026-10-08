# A3D 1.x clients

The reconstructed `a3d.dll` presents older A3D and DirectSound interfaces and
connects them to `a3dapi.dll`. A game using this model can create DirectSound
buffers and obtain 3D source/listener interfaces for their sounds.

## Two different A3dCreate functions

The older DLL exports `_A3dCreate@12`, a 3-argument stdcall entry. The SDK utility
library in `samples/lib/ia3dutil.c` defines a different, 4-argument `A3dCreate`
helper for the newer API. Do not use one function's declaration to invoke the
other. `a3dapi.dll` exports its four COM server functions; the SDK library
supplies the helper.

Use the original SDK/DirectSound interface declarations and obtain interfaces
through their supported creation or `QueryInterface` paths. Legacy headers and
game-shipped DLLs can differ from the exact reference reconstructed here.

## Client flow

An older client acquires the root/DirectSound object, sets a cooperative level,
creates a primary buffer/listener and secondary sound buffers, fills their
audio, and updates 3D state. The bridge implements buffer, source, listener,
property-set, and DSP behavior before forwarding to the resource manager.

The source and listener classes in `src/a3d/` are distinct from similarly named
classes in `src/a3dapi/`. Neither their layouts nor their private interfaces
can be substituted for the other DLL's types.

## Deployment and compatibility

Loading a local `a3d.dll` can still cause a registered `a3dapi.dll` to be selected
through COM. Inspect both modules. The reference `a3d_orig.dll` is the specific
98,304-byte driver-package library described in [ref/README.md](../../ref/README.md);
verify its identity when comparing it with SDK or game-bundled variants.

Use [legacy internals](../internals/legacy-bridge.md) to follow the bridge and
[game compatibility](../user/game-compatibility.md) for the scope of current
client probes. Do not infer full A3D 1.x game support from IA3d5 PCM comparisons.
