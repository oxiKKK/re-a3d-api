# Reference binaries

These tracked DLLs are used by the tests and binary analysis tools.

| file | size | what it is |
| --- | --- | --- |
| `a3dapi_33_rtl.dll` | 466,944 | Aureal A3D API **3.3.677.0** retail, linked 12 Feb 2000. MD5 `1a3f7ceba0015b0be90e8dea71421fb7`. |
| `a3dapi_33_dbg.dll` | 1,454,141 | The same source built `/Od /ZI` with ASSERT and TRACE, linked 11 Feb 2000. Carries the edit-and-continue link table, which names 54 object files and 150 decorated symbols. MD5 `48e5694265b3e052dd8e796dd2308754`. |
| `a3dapi_33_dbgv.dll` | 1,470,531 | The DebugViewer sibling of the same build, linked 11 Feb 2000. Same asserts, plus the shared-memory channel to `DebugViewerGl.exe`. MD5 `2d00f767a884997cc3d0aee0ba695c90`. |
| `a3dapi_33_678.dll` | 471,040 | **3.3.678.0**, one build later, 10 Jul 2000, from the Win2K/XP driver package. Cross-check only. MD5 `fce59b9b829fbaa5f46664f604b3df14`. |
| `a3d_orig.dll` | 98,304 | Aureal **A3D 1.x**, linked 19 Nov 1999, the second subject, reconstructed in `src/a3d`. From the same driver package. The A3D 1.2 SDK's own `a3d.dll` is a different and larger program. MD5 `19310a4444c951293b1a292c21010f6a`. |

The earlier 2.02 reconstruction and reference are on the `2.0` branch.
All five images load at `0x10000000`. Source citations are virtual addresses,
qualified by module and build: `rtl:`, `dbg:`, `dbgv:` or `678:`.

## Build differences

- The three 677 builds provide same-version Retail, ASSERT/TRACE and linker
  evidence. Version 678 is a later cross-check; use 677 when they disagree.
- Both instrumented 677 builds link the CRT statically. External CRT report
  hooks cannot suppress their assertion dialogs. Test children need deadlines.
  The older 2.12 DebugViewer used a shared debug CRT.
- The 677 instrumented builds contain `A3DSplsh.exe`/`ShellExecute` support;
  677 Retail does not. Version 678 Retail includes it.
- Only `DAL_D2D` appears by name in 677 Retail. The other back-ends still exist;
  their constructors and vtables establish their presence.

IDA databases and backups are ignored; open a reference DLL to create a local
database. Keep references outside `build/` so cleaning build output cannot
remove them.
