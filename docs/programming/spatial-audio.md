# Positions, orientation, and spatial controls

Convert the game world's coordinates once at the audio boundary, then use that
convention consistently for sources, listener, geometry, and velocities.

## Coordinates and units

The SDK's default convention is right-handed: +X right, +Y up, +Z toward the
viewer, so forward is -Z. `SetCoordinateSystem` selects the alternative
left-handed convention where required. This setting is constrained by
initialization/use state; do not treat it as a per-frame switch.

`SetUnitsPerMeter` defines how many game units represent one meter. If the
engine stores centimeters, the corresponding scale is 100 units per meter.
Velocities must use game units per second. Apply the same axis conversion to
orientation vectors and polygon vertices.

## Listener and source orientation

Source orientation controls its cone; listener orientation controls the
perceived direction of the entire scene. Use a consistent pair of perpendicular
direction vectors, or convert the engine's angular representation at the audio
boundary. Argument ordering and units belong to the
[listener implementation](../../src/a3dapi/Listener.cpp) and
[source implementation](../../src/a3dapi/A3dSource.cpp).

Explicitly initialize orientation:
the current comparison records an initial listener-orientation mismatch.
See [status](../status.md).

## Distance and directional gain

Choose near and far distance behavior for each sound's role, and use a cone
for sources whose direction matters. Source gain, output gain, distance,
cone, and material effects all contribute to audibility. Test each contribution
separately before tuning them together. A3D and DirectSound do not necessarily
accept the same control ranges; check the source implementation before adapting
an existing engine's values.

## Movement and Doppler

For a moving entity, update both position and velocity from the same simulation
time. A velocity estimate is `(newPosition - oldPosition) / elapsedSeconds`.
Treat teleports, spawning, and pause/resume separately to avoid artificial
large velocities. Root and source Doppler/distance scales modify the model.

Submit a coherent scene through `Flush` after updates. Use the actual frame
interval for game-side motion and keep the units consistent.
For diagnosis, zero both velocities and set pitch to 1 before evaluating
direction or distance.

## Transform binding

Geometry matrix operations can bind a source or the listener to the current
transform. That is useful when a model's acoustic objects share its placement.
Distinguish an object's local position from the binding matrix so the same
translation is not applied twice. Head-relative mode attaches a source to the
listener's coordinate system.

Test known positions: listener at the origin facing -Z, source at `(3, 0, -4)`,
then rotate the listener while leaving the source fixed. The project's `right`
capture scene uses that source position. For matrix layout and flag values,
see [ia3dapi.h](../../inc/ia3dapi.h).
