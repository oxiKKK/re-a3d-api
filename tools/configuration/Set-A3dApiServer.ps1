<#
.SYNOPSIS
	Choose which a3dapi.dll serves CLSID_A3dApi - Aureal's or this tree's.

.DESCRIPTION
	So that the same client can be run against both without being rebuilt or
	reinstalled.  A client resolves the coclass in CoCreateInstance, so a
	switch takes effect at the next launch and not before.

	NOT PART OF THE ORIGINAL.  Aureal shipped no such thing; this is a bench
	tool.  It writes to HKEY_CURRENT_USER's 32-bit view only: a build must
	never touch the machine, so registration is done out of band and by hand.
	No administrator rights are needed.

	The 32-bit view is asked for explicitly rather than by key path, so the
	script does the same thing under 32-bit and 64-bit PowerShell.

.PARAMETER Action
	status	 what is registered now (the default)
	vanilla	 ref\a3dapi_33_rtl.dll		Aureal's 3.3.677 Retail, the subject
	reverse	 build\Release\a3dapi.dll	this tree

.EXAMPLE
	.\Set-A3dApiServer.ps1 vanilla

.EXAMPLE
	powershell -NoProfile -ExecutionPolicy Bypass -File tools\configuration\Set-A3dApiServer.ps1
#>

[CmdletBinding()]
param(
	[Parameter(Position = 0)]
	[ValidateSet('status', 'vanilla', 'orig', 'reverse', 'recon')]
	[string] $Action = 'status'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$CLSID_A3dApi = '{92FA2C24-253C-11D2-90FB-006008A1F441}'

# ---------------------------------------------------------------------------

function Get-InprocServer
{
	param(
		[Microsoft.Win32.RegistryHive] $Hive,
		[string] $Clsid
	)

	$base = [Microsoft.Win32.RegistryKey]::OpenBaseKey($Hive,
		[Microsoft.Win32.RegistryView]::Registry32)

	try
	{
		$key = $base.OpenSubKey("Software\Classes\CLSID\$Clsid\InprocServer32")

		if (-not $key)
		{
			return $null
		}

		try
		{
			return $key.GetValue('')
		}
		finally
		{
			$key.Dispose()
		}
	}
	finally
	{
		$base.Dispose()
	}
}

function Set-InprocServer
{
	param(
		[string] $Clsid,
		[string] $Path
	)

	$base = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
		[Microsoft.Win32.RegistryHive]::CurrentUser,
		[Microsoft.Win32.RegistryView]::Registry32)

	try
	{
		$key = $base.CreateSubKey("Software\Classes\CLSID\$Clsid\InprocServer32")

		try
		{
			$key.SetValue('', $Path, [Microsoft.Win32.RegistryValueKind]::String)
			$key.SetValue('ThreadingModel', 'Apartment',
				[Microsoft.Win32.RegistryValueKind]::String)
		}
		finally
		{
			$key.Dispose()
		}
	}
	finally
	{
		$base.Dispose()
	}
}

# Use Debug if no Release build exists.

function Find-Build
{
	param([string] $Root, [string] $Leaf)

	$release = Join-Path $Root "build\Release\$Leaf"
	$debug   = Join-Path $Root "build\Debug\$Leaf"

	if ((-not (Test-Path -LiteralPath $release)) -and (Test-Path -LiteralPath $debug))
	{
		return $debug
	}

	return $release
}

# ---------------------------------------------------------------------------

$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path

# Reference DLL: 3.3.677.0 Retail.
$vanilla = Join-Path $root 'ref\a3dapi_33_rtl.dll'
$reverse = Find-Build $root 'a3dapi.dll'

switch ($Action)
{
	{ $_ -in 'vanilla', 'orig' }	{ $target = $vanilla }
	{ $_ -in 'reverse', 'recon' }	{ $target = $reverse }
	default				{ $target = $null }
}

if ($target)
{
	if (-not (Test-Path -LiteralPath $target))
	{
		Write-Host "$target is not there."

		if ($Action -in 'reverse', 'recon')
		{
			Write-Host 'Build it first - cmake --build build --config Release --target a3dapi'
		}

		exit 1
	}

	Set-InprocServer $CLSID_A3dApi $target

	Write-Host "CLSID_A3dApi -> $target"

	exit 0
}

# ---------------------------------------------------------------------------

$api = Get-InprocServer ([Microsoft.Win32.RegistryHive]::CurrentUser) $CLSID_A3dApi

if ($api)
{
	$tag = ''

	if ($api -eq $vanilla) { $tag = '  [vanilla]' }
	if ($api -eq $reverse) { $tag = '  [reverse]' }

	if (-not (Test-Path -LiteralPath $api)) { $tag += '  ** missing **' }

	Write-Host "CLSID_A3dApi  $api$tag"
}
else
{
	Write-Host 'CLSID_A3dApi  not registered for this user'
}

exit 0
