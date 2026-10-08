# A3D 1.x compatibility library

Reconstructs the 98,304-byte `a3d.dll` from the Vortex driver package,
linked 19 November 1999, MD5 `19310a4444c951293b1a292c21010f6a`.
It differs from Half-Life's `a3d.dll` and the A3D 1.2 SDK library.

The library creates `CLSID_A3dApi` through COM and exposes the older A3D and
DirectSound interfaces. The root CMake build enables it with `A3D_BUILD_A3D`.

| Coclass | CLSID |
| --- | --- |
| `CLSID_A3d` | `{D8F1EEE0-F634-11cf-8700-00A0245D918B}` |
| `CLSID_A3dDal` | `{442D12A1-2641-11d2-90FB-006008A1F441}` |

| file | holds |
| --- | --- |
| `A3d.cpp` | the object both coclasses return, `A3dCreate()`, the buffer factories, the splash screen |
| `Listener.cpp` | the primary buffer and the two interfaces torn off it |
| `dsbuffer.cpp` | the secondary DirectSound buffer and its gain block |
| `A3dSource.cpp` | the `IDirectSound3DBuffer` torn off a secondary |
| `a3dclsfc.cpp` | the class factory and the four `Dll*` exports |
| `Plex.cpp` | the plex block allocator |
| `a3ddsp.cpp` | the geometry and DSP engine |
| `a3dguid.cpp` | the GUID definitions |

Each source has a corresponding header; `a3dprv.h` holds shared includes.
Filenames are inferred, with unresolved boundaries and the differing function-count
scopes (182 Aureal functions in the original inventory).

Addresses refer to `a3d.dll` at imagebase `0x10000000`; qualify them by module
because `a3dapi.dll` uses the same base. Aureal's copyright notices are retained.
For study only.
