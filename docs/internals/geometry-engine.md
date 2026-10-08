# Geometry engine

The geometry engine transforms submitted surfaces, evaluates acoustic paths,
and converts those paths into source controls. It also contains scene builders
for rooms, walls, openings, and material links.

## Public geometry stream

`CA3dRoot` stores geometry in pools. `Begin` selects a primitive mode; vertices
are transformed and completed primitives are appended to the stream. Material
binding appends coefficient information. Tags identify subsequent primitives.
Lists capture reusable blocks of this representation.

The public matrix stack and source/listener binding operations affect the
coordinate transforms used by tracing. Matrices are column-major. Geometry
state persists across `Clear`, while frame pools are reset. Refer to
[A3dGeom.cpp](../../src/a3dapi/A3dGeom.cpp),
[A3dList.cpp](../../src/a3dapi/A3dList.cpp), and
[Polygon.cpp](../../src/a3dapi/Polygon.cpp).

## Scene objects

| Object group | Role |
| --- | --- |
| Scene and room | Spatial organization and room lookup |
| Wall, wall edge, opening | Boundaries, connectivity, and transmission paths |
| Polygon and builders | Input validation and construction of geometric objects |
| Material and material link | Acoustic properties and their attachment to geometry |
| Matrix, corners, frame | Placement, bounds, and frame data |

Implementation files are named after these classes, including
[A3dRoomBuilder.cpp](../../src/a3dapi/A3dRoomBuilder.cpp),
[A3dWallBuilder.cpp](../../src/a3dapi/A3dWallBuilder.cpp), and
[A3dOpeningBuilder.cpp](../../src/a3dapi/A3dOpeningBuilder.cpp).
Their private scene interface is accessed by
[sceneman.cpp](../../src/a3dapi/sceneman.cpp). Internal availability does not
establish a complete public environment interface.

## Occlusion and reflections

Occlusion traversal tests geometry on the source-to-listener path and derives
attenuation from material transmission. Reflection traversal mirrors sources
across surfaces, checks whether paths are valid, and ranks audible candidates.
The renderer receives the selected paths within its resource limits.

Materials retain broadband and high-frequency gains and derived curves. Their
constructor preserves original uninitialized high-frequency fields, so client
examples set both properties before use. A primitive submission can also
overwrite supplied normals in the original path; do not replace that behavior
merely because another implementation would use normals differently.

## Volumetric sources and geometric reverb

Volumetric controls describe spatial extent and damping. Helpers for coverage
and size damping exist, but complete integration remains unresolved. A working
setter/getter pair is insufficient evidence that the volume affects all
rendering paths.

The legacy reverb estimate uses geometry extents and history to choose
environment parameters. The newer path also has geometric reverb controls.
These calculations must be distinguished from the DSP that renders the tail.
See [effects](../programming/reflections-and-reverb.md).

## Constraints and unresolved behavior

The recorded room-transition cache/cull gap in `TraceWalk`, hit refinement in
`TraceHitOpen`, room-finder behavior, and list secondary-base identity require
binary evidence before completion. List recording also preserves original
pool-copy/count defects. Current source banners identify the relevant entry
points and qualifications; [status](../status.md) summarizes the limitations.

Regression scenes should isolate one surface/path change, verify repeatability
against the reference, and measure both control outputs and PCM where possible.
Increasing geometry complexity before a basic scene agrees makes failures
harder to locate.
