# COM and binary interfaces

The public ABI is Windows x86 COM. Interface method order, arity, calling
convention, structure packing, and object lifetime are part of compatibility.

## Activation and identity

`a3dapi.dll` exposes a COM server through its
[export declarations](../../src/a3dapi/a3dapi.def).
`CLSID_A3dApi` creation selects a root, mapper, or resource manager according
to the requested interface. `IID_IA3dPrv4` can obtain the resource manager
without a root, a path used by the legacy DLL.

`CA3dRoot` shares lifetime across its interface subobjects. A source wrapper
delegates operations to its selected implementation. `ResMan` uses its
`IA3dPrv4` subobject for IUnknown identity. Preserve interface adjustments.
Compare IUnknown identity when testing whether interfaces belong to the same
object.

The original's singleton guards are interface-dependent. In particular, an
IA3d5 request and an older software-route request need not be rejected in the
same way. See [a3dclsfc.cpp](../../src/a3dapi/a3dclsfc.cpp) and
[root QueryInterface](../../src/a3dapi/A3dRoot.cpp).

## Public and private contracts

Public declarations come from [ia3dapi.h](../../inc/ia3dapi.h) and Windows
DirectSound headers. Private contracts are declared in
[a3d33.h](../../src/a3dapi/a3d33.h) and [ia3ddal.h](../../src/ia3ddal.h).
Private interfaces include the DAL/device and DAL/buffer surfaces, resource
manager, scene, and source interfaces.

`IA3dPrvA2D` and `IA3dPrvD3D` share an IID and each has a backend startup slot.
This does not make arbitrary private interfaces interchangeable. The retained
`IA3dPrv2` declaration is not exposed by the current root/resource-manager
`QueryInterface` implementations.

## Vtable validation

Compare the [SDK declarations](../../inc/ia3dapi.h) with the target binary.
Retail and Debug vtable inventories, produced by
[extract_vtables.py](../../tools/analysis/extract_vtables.py), locate binary tables.
Validate table lengths, slot identities, and argument types separately. A header from a newer SDK can extend beyond the target's actual table
into a destructor or unrelated data.

Never recover an interface by adding an assumed byte offset in executable
code. Declare the owning subobject/field at an established offset and width,
and check the total layout. The compiler-generated
layout report is separate from public SDK structures.

## ABI exceptions applications must know

- Feature queries on newer roots and geometry `IsEnabled` return Boolean
  values through an HRESULT declaration.
- `IA3dList::End` and `Call` can return block counts.
- `A3dEnumerate` forwards ANSI strings although the SDK callback type says
  `LPCWSTR`; retain this mismatch in the ABI contract.
- Material and list `QueryInterface` have original output/reference defects.
- `Shutdown` destroys the root despite outstanding references.
- The old DLL's `_A3dCreate@12` differs from the 4-argument SDK utility helper.

The owning implementations record these contracts; follow the
[architecture source links](architecture.md). Use meaningful negative-path
tests before changing any of these behaviors.

## Compiler constraints

The build requires MSVC x86, C++98 for the DLLs, static CRT linkage, and the
configured x87 floating-point behavior. Inline assembly has register and x87
stack contracts beyond ordinary C++ signatures. An x64 build or wholesale
conversion to SIMD changes more than the compiler switch.
