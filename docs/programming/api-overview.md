# Choosing and using the API

A3D offers a scene API for positional sounds and acoustic geometry, plus a
legacy DirectSound-facing API. Select the interface used by the client and
keep that choice consistent through initialization and playback. The interface
generation can affect behavior as well as the available operations.

## Scene objects

The root represents an audio scene and its device connection. The listener
describes the player's hearing position. Sources combine sound data with
playback and spatial state. Geometry and materials describe surfaces that
affect sound; retained lists allow the game to reuse geometry submissions.
Reverb and manual reflections add effect state when the selected route supports
them. The [concepts guide](../concepts.md) explains their relationships.

Create and initialize a root, acquire the scene objects the game needs, and
submit coherent scene updates. Keep those objects alive while they are in use
and release dependent objects before the root. Start with the
[working application](first-application.md), then follow
[game integration](game-engine-integration.md) and
[object lifetime](initialization-and-lifetime.md).

## Declarations and implementation contracts

Use [ia3dapi.h](../../inc/ia3dapi.h) for public interfaces, argument types,
structures, flags, identifiers, and error definitions. Parameter names,
validation, ownership, and return behavior belong to the implementation and
its source comments. The [architecture guide](../internals/architecture.md)
links each subsystem to its owning source files.

Do not assume a declaration establishes support. Some retained operations are
unsupported; others preserve original defects or return conventions that
differ from ordinary COM expectations. Check the implementation for the
selected interface version and use [status](../status.md) to understand what
has been validated. Private interfaces and binary compatibility are covered
by [COM and ABI](../internals/com-and-abi.md).

## Activation and older clients

The SDK helpers initialize COM and create the scene root. Their declarations
and behavior are in [ia3dutil.h](../../inc/ia3dutil.h) and the original
[utility source](../../samples/lib/ia3dutil.c). Registration side effects make
the loaded DLL worth verifying even when helper initialization succeeds.
The working application uses an explicit DLL and its class factory instead.

The separate `a3d.dll` supports older clients through a different creation
entry point and DirectSound objects. Its creation function is not
interchangeable with the SDK helper of the same name. Follow the
[legacy client guide](legacy-a3d.md) for that route. Export declarations are
maintained in [a3dapi.def](../../src/a3dapi/a3dapi.def) and
[a3d.def](../../src/a3d/a3d.def).
