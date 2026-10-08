<#
.SYNOPSIS
	Measure line coverage of src/a3dapi under the comparison and linked tests.

.DESCRIPTION
	NOT PART OF THE ORIGINAL.  Project tooling.

	Builds build-cov (Release defines, optimization off, debug information,
	A3D_FIXES=OFF), runs a3d_reference_comparison_tests under OpenCppCoverage
	with child processes followed, merges a run of
	a3d_linked_implementation_tests, and writes Cobertura and HTML reports to
	build-cov\coverage. Prints the per-file summary.

	OpenCppCoverage runs the tests under a debugger, which changes timing and
	enables the debug heap; a few comparisons fail under it. This script
	reports coverage only. Take pass/fail results from a normal build.

	The comparison children mute their own audio session, so the run is silent.

.PARAMETER NoBuild
	Reuse the existing build-cov binaries.

.PARAMETER Rows
	Number of files to list, ordered by uncovered lines; 0 lists all.

.EXAMPLE
	.\tools\coverage\Run-Coverage.ps1 -Rows 30
#>

param(
	[switch] $NoBuild,
	[int]    $Rows = 40
)

$ErrorActionPreference = 'Stop'

$root  = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$build = Join-Path $root 'build-cov'
$out   = Join-Path $build 'coverage'
$bin   = Join-Path $build 'Release'
$occ   = 'C:\Program Files\OpenCppCoverage\OpenCppCoverage.exe'

if (-not (Test-Path $occ)) {
	$cmd = Get-Command OpenCppCoverage -ErrorAction SilentlyContinue
	if (-not $cmd) { throw 'OpenCppCoverage was not found.' }
	$occ = $cmd.Source
}

if (-not $NoBuild) {
	cmake -S $root -B $build -G 'Visual Studio 18 2026' -A Win32 `
		-DA3D_BUILD_TESTS=ON -DA3D_FIXES=OFF -DA3D_BUILD_SAMPLES=OFF `
		'-DCMAKE_CXX_FLAGS_RELEASE=/Od /Ob0 /Zi /DNDEBUG' `
		'-DCMAKE_C_FLAGS_RELEASE=/Od /Ob0 /Zi /DNDEBUG' `
		'-DCMAKE_SHARED_LINKER_FLAGS_RELEASE=/DEBUG /INCREMENTAL:NO' `
		'-DCMAKE_EXE_LINKER_FLAGS_RELEASE=/DEBUG /INCREMENTAL:NO' | Out-Null
	if ($LASTEXITCODE) { throw 'Configure failed.' }

	cmake --build $build --config Release | Out-Null
	if ($LASTEXITCODE) { throw 'Build failed.' }
}

New-Item -ItemType Directory -Force $out | Out-Null
$binary = Join-Path $out 'comparison.cov'
$xml    = Join-Path $out 'coverage.xml'
$html   = Join-Path $out 'html'
$src    = Join-Path $root 'src\a3dapi'

# The tests resolve some inputs relative to the repository root. OpenCppCoverage
# writes LastCoverageResults.log to the working directory; move it afterwards.
Push-Location $root
try {
	& $occ --quiet --cover_children --sources $src `
		--modules (Join-Path $bin 'a3dapi.dll') `
		--export_type "binary:$binary" `
		-- (Join-Path $bin 'a3d_reference_comparison_tests.exe') | Out-Null

	& $occ --quiet --sources $src `
		--modules (Join-Path $bin 'a3d_linked_implementation_tests.exe') `
		--input_coverage $binary `
		--export_type "cobertura:$xml" --export_type "html:$html" `
		-- (Join-Path $bin 'a3d_linked_implementation_tests.exe') | Out-Null
}
finally {
	$log = Join-Path $root 'LastCoverageResults.log'
	if (Test-Path $log) { Move-Item $log (Join-Path $out 'LastCoverageResults.log') -Force }
	Pop-Location
}

if (-not (Test-Path $xml)) { throw 'No coverage report was written.' }

python (Join-Path $PSScriptRoot 'summarize_coverage.py') $xml $Rows
Write-Host "HTML report: $html\index.html"
