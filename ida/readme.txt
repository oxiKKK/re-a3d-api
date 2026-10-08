build_til.ps1

a3dapi.h is an umbrella over src/a3dapi/*.h.  Parsing it gives a3dapi.til,
which carries the layout and the vtable of every class and struct in the
a3dapi.dll 3.3.677 reconstruction.  a3d.dll is not in it - src/a3d has an
a3dprv.h of its own and the two cannot share an include path.

How to use:

1. Open PowerShell in this directory.
2. Run:
   powershell -ExecutionPolicy Bypass -File .\build_til.ps1
3. The script parses a3dapi.h and writes a3dapi.til.

Put idaclang.exe on PATH or pass -IDAClang with its full path. The script
finds MSVC and the Windows SDK itself; override them with -MSVCInclude and
-WindowsSdkInclude. -LogWarnings prints what clang had to say.

How to load in IDA:

1. Shift+F11
2. Ctrl+N -> New type
3. Load Type Library file...

To read the result without IDA:

   <ida>\tools\tilib\tilib.exe -l a3dapi.til
