# Build an x86 IDA type library from the repository declarations.

param(
	[string]$Header = "",
	[string]$Output = "",
	[string]$IDAClang = "",
	[string]$MSVCInclude = "",
	[string]$WindowsSdkInclude = "",
	[switch]$LogWarnings
)

$RepoRoot = Split-Path $PSScriptRoot -Parent

if( $Header -eq "" )
{
	$Header = Join-Path $PSScriptRoot "a3dapi.h"
}

if( $Output -eq "" )
{
	$Output = [System.IO.Path]::ChangeExtension( $Header, ".til" )
}

if( $IDAClang -eq "" )
{
	$IDAClang = (Get-Command idaclang.exe -CommandType Application `
		-ErrorAction SilentlyContinue | Select-Object -First 1).Source
	if( -not $IDAClang )
	{
		throw "idaclang.exe not found; pass -IDAClang <path>"
	}
}

if( $MSVCInclude -eq "" )
{
	$MSVCRoots = @(
		"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Tools\MSVC",
		"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC",
		"C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\MSVC",
		"C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC"
	)
	$MSVCVersion = $MSVCRoots |
		Where-Object { Test-Path $_ } |
		ForEach-Object { Get-ChildItem $_ -Directory } |
		Sort-Object Name -Descending |
		Select-Object -First 1
	if( -not $MSVCVersion )
	{
		throw "no MSVC toolset found; pass -MSVCInclude <path>"
	}
	$MSVCInclude = Join-Path $MSVCVersion.FullName "include"
}

if( $WindowsSdkInclude -eq "" )
{
	$WindowsSdkRoot = "C:\Program Files (x86)\Windows Kits\10\Include"
	$WindowsSdkInclude = (Get-ChildItem $WindowsSdkRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
}

# Exclude src\a3d: its a3dprv.h belongs to a3d.dll and conflicts with
# a3dapi header resolution.
$IncludeDirs = @(
	(Join-Path $RepoRoot "inc"),
	(Join-Path $RepoRoot "src\a3dapi"),
	(Join-Path $RepoRoot "src"),
	$MSVCInclude,
	(Join-Path $WindowsSdkInclude "ucrt"),
	(Join-Path $WindowsSdkInclude "shared"),
	(Join-Path $WindowsSdkInclude "um")
)

# C++17 supports current SDK headers. Only STDMETHOD carries nothrow,
# requiring suppression of exception-specification warnings.
$Args = @(
	"-target", "i686-pc-windows-msvc",
	"-x", "c++",
	"-std=c++17",
	"-fms-extensions",
	"-Wno-microsoft-exception-spec",
	"--idaclang-tilname", $Output,
	"--idaclang-tildesc", "a3dapi.dll 2.02 reconstruction"
)

foreach( $Dir in $IncludeDirs )
{
	$Args += "-I"
	$Args += $Dir
}

if( $LogWarnings )
{
	$Args += "--idaclang-log-warnings"
}

$Args += $Header

& $IDAClang @Args
exit $LASTEXITCODE
