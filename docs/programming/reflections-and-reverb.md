# Reflections and reverberation

Request the effect at initialization, check availability, configure its objects,
and verify that the selected backend renders it. A successful setter only
establishes that state was accepted.

## Geometry reflections

Request `A3D_1ST_REFLECTIONS`, enable reflection work on the geometry interface,
and use reflective materials and source/polygon modes that permit reflections.
The engine derives candidate paths from surfaces, evaluates them, and selects
audible reflections under resource limits.

Root maximum delay, geometry gain/delay scales, source gain/delay scales, and
update intervals affect different stages. They should not be used as
interchangeable volume controls. Test one reflective surface first, then add
multiple surfaces and motion.

## Manual reflections

`IA3dSource2::NewManualReflection` returns an `IA3dReflection` attached to that
source. Set its position, transform mode, gain scale, delay in seconds, and EQ.
Its position controls the apparent reflected direction; its delay is supplied
explicitly. Keep the source alive while using the reflection.

Initialize the effect deliberately. Freeing a source's manual reflections
invalidates its collection, including pointers held by the client. Ownership
and parameter contracts are recorded in
[A3dReflection.h](../../src/a3dapi/A3dReflection.h) and
[A3dSource.cpp](../../src/a3dapi/A3dSource.cpp).

## Reverb objects

Request `A3D_REVERB`, call `NewReverb`, configure a preset or custom properties,
and bind it with `BindReverb`. Preset controls include volume, decay time, and
damping. Custom settings use `A3DREVERB_PROPERTIES` with the correct outer and
selected union-member `dwSize` fields.

`A3D_GEOMETRIC_REVERB` additionally asks the engine to derive environment state
from geometry. Older-interface reflection requests can implicitly enable this
path. A client should not infer the exact effects requested by another API
version from a superficially similar initialization mask.

Reverb control forwarding uses property sets and varies by backend. Some
original paths discard a property-write error, so a successful frame is not
proof of a working effect.

## Software effects in this project

`SoftwareReverb` enables a software property-set adapter and stereo
reverb engine. `SoftwareReflections` independently enables [per-voice reflection taps](../voice.md#reflection-voices). `EmulateHardware` enables
neither effect. Older-interface reflection requests can still require a reverb
interface during initialization; a reflection-only configuration does not supply one.
This DSP is project-added. Its reflection delay capacity is 0.5 seconds,
independent of the root's maximum-delay setting; reverb uses damped comb and
allpass filters.

Compare software effects as an extension with their own expected behavior.
Disable them for original-output parity tests. See
[compatibility extensions](../internals/compatibility-extensions.md).

## Verify each effect

Use a short impulse or a separated transient. Compare direct-only output with
occlusion, reflection, and reverb enabled separately. Inspect timing and tail
energy as well as source state. Geometry needs a meaningful source/listener
placement and enough update frames to settle.

The capture tool supports `--effect none|reverb|reflect`; SceneRooms supplies
interactive examples. Their existing checks do not establish full hardware
equivalence. See [diagnostics](../development/diagnostics.md) and [status](../status.md).
