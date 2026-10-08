<#
.SYNOPSIS
	Collect dumps and A3D logs when a client becomes unresponsive.

.DESCRIPTION
	Launch with A3D_CAPTURE/A3D_TRACE in a timestamped folder. On a hang,
	collect two minidumps five seconds apart plus console and A3D logs.
	On process exit, collect the newest crash dump.

	Use 32-bit PowerShell for MiniDumpWriteDump to capture the WOW64
	application context. -DumpOnly is the internal subprocess mode.

	NOT PART OF THE ORIGINAL.  A bench tool, like tools/configuration/Set-A3dApiServer.ps1.

.PARAMETER Server
	Which a3dapi to register before launching: vanilla, reverse, or current
	(the default - leaves the registration alone).

.PARAMETER GameDir
	Directory holding the executable. Required when launching a client.

.PARAMETER Exe
	Executable to launch, relative to GameDir.  Defaults to hl.exe.

.PARAMETER GameArgs
	Arguments for it.  The default runs windowed with -condebug so the console
	log is written, which is one of the artefacts collected.

.PARAMETER Timeout
	Give up watching after this many seconds if nothing goes wrong.

.EXAMPLE
	.\Watch-A3dHang.ps1 -Server vanilla -GameDir "<game-directory>"

.EXAMPLE
	.\Watch-A3dHang.ps1 -Server vanilla -GameDir "<game-directory>" `
		-GameArgs '-window -console -condebug -novid'
#>

[CmdletBinding(DefaultParameterSetName = 'Watch')]
param(
	[Parameter(ParameterSetName = 'Watch', Position = 0)]
	[ValidateSet('current', 'vanilla', 'reverse')]
	[string] $Server = 'current',

	[Parameter(ParameterSetName = 'Watch', Mandatory = $true)]
	[string] $GameDir,

	[Parameter(ParameterSetName = 'Watch')]
	[string] $Exe = 'hl.exe',

	[Parameter(ParameterSetName = 'Watch')]
	[string] $GameArgs = '-window -w 800 -h 600 -novid -console -condebug',

	[Parameter(ParameterSetName = 'Watch')]
	[int] $Timeout = 900,

	# Resolve the default below; $PSScriptRoot may be empty during
	# parameter binding, which would place output at C:\hangs.
	[Parameter(ParameterSetName = 'Watch')]
	[string] $OutRoot,

	[Parameter(ParameterSetName = 'DumpOnly', Mandatory = $true)]
	[switch] $DumpOnly,

	[Parameter(ParameterSetName = 'DumpOnly', Mandatory = $true)]
	[int] $TargetPid,

	[Parameter(ParameterSetName = 'DumpOnly', Mandatory = $true)]
	[string] $DumpPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------

function Write-Dump
{
	param([int] $ProcessId, [string] $Path)

	Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Runtime.InteropServices;

public static class Dumper
{
	[DllImport("kernel32.dll", SetLastError = true)]
	static extern IntPtr OpenProcess(uint access, bool inherit, int pid);

	[DllImport("kernel32.dll", SetLastError = true)]
	static extern bool CloseHandle(IntPtr h);

	[DllImport("dbghelp.dll", SetLastError = true)]
	static extern bool MiniDumpWriteDump(IntPtr hProcess, int pid, IntPtr hFile,
					     int type, IntPtr ex, IntPtr user,
					     IntPtr callback);

	public static string Write(int pid, string path)
	{
		// QUERY_INFORMATION | VM_READ
		IntPtr h = OpenProcess(0x0400 | 0x0010, false, pid);

		if (h == IntPtr.Zero)
			return "OpenProcess failed, err " + Marshal.GetLastWin32Error();

		try
		{
			using (FileStream fs = new FileStream(path, FileMode.Create,
							      FileAccess.Write))
			{
				// Normal | WithThreadInfo | WithHandleData
				bool ok = MiniDumpWriteDump(h, pid, fs.SafeFileHandle.DangerousGetHandle(),
							    0x1000 | 0x0004, IntPtr.Zero,
							    IntPtr.Zero, IntPtr.Zero);

				if (!ok)
					return "MiniDumpWriteDump failed, err " +
					       Marshal.GetLastWin32Error();
			}
		}
		finally
		{
			CloseHandle(h);
		}

		return null;
	}
}
'@

	return [Dumper]::Write($ProcessId, $Path)
}

if ($DumpOnly)
{
	$err = Write-Dump -ProcessId $TargetPid -Path $DumpPath

	if ($err) { Write-Host "  dump failed: $err"; exit 1 }

	exit 0
}

# ---------------------------------------------------------------------------

function Invoke-Dump32
{
	param([int] $ProcessId, [string] $Path)

	$ps32 = "$env:SystemRoot\SysWOW64\WindowsPowerShell\v1.0\powershell.exe"

	if (-not (Test-Path -LiteralPath $ps32))
	{
		# Already 32-bit, or no WOW64 - do it in-process.
		$err = Write-Dump -ProcessId $ProcessId -Path $Path
		if ($err) { Write-Host "  dump failed: $err" }
		return
	}

	& $ps32 -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath `
		-DumpOnly -TargetPid $ProcessId -DumpPath $Path
}

# ---------------------------------------------------------------------------

if (-not $OutRoot)
{
	$OutRoot = Join-Path $PSScriptRoot '..\..\artifacts\hangs'
}

$stamp  = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$outDir = Join-Path ([IO.Path]::GetFullPath($OutRoot)) $stamp

New-Item -ItemType Directory -Path $outDir -Force | Out-Null

if ($Server -ne 'current')
{
	& "$PSScriptRoot\..\configuration\Set-A3dApiServer.ps1" $Server
}

& "$PSScriptRoot\..\configuration\Set-A3dApiServer.ps1" | Tee-Object -FilePath "$outDir\registration.txt"

$env:A3D_CAPTURE = "$outDir\dal-capture.log"
$env:A3D_TRACE   = "$outDir\dal-trace.log"

$exePath = Join-Path $GameDir $Exe

Write-Host ''
Write-Host "artefacts -> $outDir"
Write-Host "launching  $exePath $GameArgs"
Write-Host ''

$before = @(Get-ChildItem -LiteralPath $GameDir -Filter *.mdmp -ErrorAction SilentlyContinue |
	    ForEach-Object { $_.Name })

$proc = Start-Process -FilePath $exePath -ArgumentList $GameArgs `
		      -WorkingDirectory $GameDir -PassThru

Write-Host "pid $($proc.Id) - watching.  Reproduce the freeze; Ctrl+C to stop watching."

$deadline = (Get-Date).AddSeconds($Timeout)
$bad      = 0
$verdict  = 'timed out with nothing to report'

while ((Get-Date) -lt $deadline)
{
	Start-Sleep -Milliseconds 500

	$proc.Refresh()

	if ($proc.HasExited)
	{
		$verdict = "the process exited (code $($proc.ExitCode))"

		$new = @(Get-ChildItem -LiteralPath $GameDir -Filter *.mdmp -ErrorAction SilentlyContinue |
			 Where-Object { $before -notcontains $_.Name })

		foreach ($f in $new)
		{
			Copy-Item -LiteralPath $f.FullName -Destination "$outDir\crash-$($f.Name)"
			$verdict = "the process crashed - dump collected: crash-$($f.Name)"
		}

		break
	}

	if ($proc.Responding)
	{
		$bad = 0
		continue
	}

	$bad++

	# Require six half-second unresponsive samples to exclude brief stalls.
	if ($bad -lt 6) { continue }

	Write-Host ''
	Write-Host "NOT RESPONDING for 3 s - dumping (pid $($proc.Id))"

	Invoke-Dump32 -ProcessId $proc.Id -Path "$outDir\hang-1.dmp"

	Write-Host '  waiting 5 s for the second dump ...'
	Start-Sleep -Seconds 5

	Invoke-Dump32 -ProcessId $proc.Id -Path "$outDir\hang-2.dmp"

	$verdict = 'HUNG - two dumps five seconds apart'
	break
}

foreach ($log in @('qconsole.log', 'valve\qconsole.log', 'cstrike\qconsole.log'))
{
	$p = Join-Path $GameDir $log

	if (Test-Path -LiteralPath $p)
	{
		Copy-Item -LiteralPath $p -Destination "$outDir\$(Split-Path $log -Leaf)" -Force
	}
}

Write-Host ''
Write-Host "verdict: $verdict"
Write-Host ''

Get-ChildItem -LiteralPath $outDir | ForEach-Object {
	Write-Host ('  {0}' -f $_.Name)
}

# Cached writes can leave the directory entry size at zero. Show the log
# tail to identify a DAL call with no recorded exit.

$trace = "$outDir\dal-trace.log"

if (Test-Path -LiteralPath $trace)
{
	Write-Host ''
	Write-Host 'last DAL calls before the freeze:'

	Get-Content -LiteralPath $trace -Tail 6 | ForEach-Object { Write-Host "  $_" }
}

Write-Host ''
Write-Host "The game is still running if it hung - kill it when you are done:"
Write-Host "  taskkill /F /IM $Exe"
