# Reconstruction method and evidence

The target is Aureal's 3.3.677.0 behavior, including defects. A plausible
implementation must also satisfy the observed ABI, layouts, call sites, and
failure paths.

## Evidence sources

| Source | Use |
| --- | --- |
| 677 Retail | Shipping behavior, optimized code, exports, and vtables |
| 677 Debug | Same-version unoptimized code, ASSERT/TRACE, file-line records, and link-table symbols |
| 677 DebugViewer | Same-version diagnostics plus viewer-related evidence |
| 678 | Later cross-check; does not override 677 behavior |
| SDK headers/docs/samples | Intended public contract and vocabulary |
| A3D-Live | Naming vocabulary only where marked; no proof of 677 behavior |

Reference identities and hashes are in [ref/README.md](../../ref/README.md).
Earlier 2.02/2.12 behavior is not automatically evidence for this target.

## Addresses and layouts

Reference images use imagebase `0x10000000`. Tag API addresses with `rtl:`,
`dbg:`, `dbgv:`, or `678:`. Include the module for addresses in `a3d.dll`.
Source method banners retain known entries; fields retain offsets, structures
retain sizes, and vtables retain slot/table records.

Before implementing an unknown body, establish the location and width of every
accessed field. Declare it in the owning class and access it by name. If purpose is unknown but layout is established, use the repository's
`Unknown_0x...` naming convention. If layout is unknown, record what evidence
is needed before declaring the field.

## Bounded reconstruction workflow

1. Find the method in current source, public/private declarations, and reference maps.
2. Verify the matching binary entry, callers, vtable slot, and argument widths.
3. Establish behavior on normal, invalid-input, and cleanup paths.
4. Add declarations and implementation in the owning source/header.
5. Compare the affected API or PCM behavior against the reference.
6. Update durable evidence/coverage records and regenerate derived reports.

Compiler-generated helpers, thunks, folded functions, and inline copies need
separate identification. A function address can be shared by multiple names;
one annotation per address is not necessarily one implemented source method.

## Reports and generators

For an IDA type library, use [build_til.ps1](../../ida/build_til.ps1). Put
`idaclang.exe` on `PATH` or pass its location with `-IDAClang`; see the
[IDA instructions](../../ida/readme.txt) for setup.

The IDA candidate verifier records only the input database filename in
`unproven.tsv`. Local SDK and database directories do not belong in committed
metadata or command examples. Existing evidence rows can be retained when
regenerating only the header through the verifier's `result_header` function;
this does not repeat decompilation or validate the recorded results.

The [analysis tools](../../tools/README.md) generate class names, source-file
attribution, layouts, addresses, unknown declarations, and extracted tables.
Generated reports name their generator. Make changes in the generator and regenerate
the report.

Use source and evidence to resolve conflicts. SDK descriptions sometimes differ
from the binary, and a source banner can become stale after an implementation
change. A decompiler's inferred type is not authoritative when header arity,
call sites, and the actual table disagree.

## Validation limits

Citation counts include comments and do not prove execution. A coverage
classifier's unresolved category is not proof that a function body is absent.
ABI checks, API comparisons, PCM tests, and real hardware observations establish
different properties. Keep those distinctions in reports.

Preserve copyright, proprietary notices, and original SDK/sample text. This
manual documents provenance; it does not grant rights to distribute historical
binaries or third-party code.
