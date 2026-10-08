<#
.SYNOPSIS
	Compare two scripted SceneRooms logs.

.DESCRIPTION
	Compare corresponding key=value records from identical scripted runs:

	  exact    occ, gain, px/py/pz, lx/ly/lz, yaw
	           Differences above Tolerance fail the comparison.

	  spread   aud
	           Reported only; asynchronous updates can vary before settling.

	  timing   st, pp
	           Status and play position, on the mixer's clock.  Reported
	           and ignored.

	Measure reference-versus-reference variation before comparing DLLs:

	    SceneRooms --dll ref  --script --log ref1.log
	    SceneRooms --dll ref  --script --log ref2.log
	    .\Compare-SceneLog.ps1 ref1.log ref2.log      # the baseline
	    SceneRooms --dll ours --script --log ours.log
	    .\Compare-SceneLog.ps1 ref1.log ours.log      # against it

	NOT PART OF THE ORIGINAL.  A bench tool.

.PARAMETER Reference
	The log from the run against Aureal's binary.

.PARAMETER Candidate
	The log from the run being judged.

.PARAMETER Tolerance
	How far apart two exact-class floats may be and still count as equal.
	Default 1e-4, which is below anything the scene changes by between
	samples and above the last-bit noise of a float printed to six places.

.PARAMETER MaxReport
	How many differing lines to print before stopping.  Default 20.

.EXAMPLE
	.\Compare-SceneLog.ps1 ref.log ours.log
#>

[CmdletBinding()]
param(
	[Parameter(Mandatory = $true, Position = 0)]
	[string] $Reference,

	[Parameter(Mandatory = $true, Position = 1)]
	[string] $Candidate,

	[double] $Tolerance = 1e-4,

	[int] $MaxReport = 20
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------

$ExactFields  = @('occ', 'gain', 'px', 'py', 'pz', 'lx', 'ly', 'lz', 'yaw')
$SpreadFields = @('aud')
$TimingFields = @('st', 'pp')
$KeyFields    = @('f', 'room')

# A line is "key=value key=value ...".  A value never contains whitespace, so
# any token without an '=' is a formatting error rather than a field.
function ConvertTo-Fields
{
	param([string] $Line)

	$fields = @{}

	foreach ($tok in ($Line -split '\s+'))
	{
		if (-not $tok)
		{
			continue
		}

		if ($tok -notmatch '^([A-Za-z]+)=(.+)$')
		{
			Write-Warning "unparsed token '$tok'"
			continue
		}

		$fields[$Matches[1]] = $Matches[2]
	}

	return $fields
}

function Get-Delta
{
	param([string] $A, [string] $B)

	if ($A -eq $B)
	{
		return 0.0
	}

	$da = 0.0
	$db = 0.0

	if ([double]::TryParse($A, [ref] $da) -and [double]::TryParse($B, [ref] $db))
	{
		return [math]::Abs($da - $db)
	}

	# Not numbers and not equal: hex status words, names.
	return [double]::PositiveInfinity
}

function Get-ServerLine
{
	param([string] $Path)

	$line = Get-Content -LiteralPath $Path |
		Where-Object { $_ -like '# server=*' } |
		Select-Object -First 1

	if ($line) { return $line.Substring(9) }
	return '(not recorded)'
}

# ---------------------------------------------------------------------------

$refLines = @(Get-Content -LiteralPath $Reference |
	      Where-Object { $_ -and -not $_.StartsWith('#') })
$canLines = @(Get-Content -LiteralPath $Candidate |
	      Where-Object { $_ -and -not $_.StartsWith('#') })

Write-Host ''
Write-Host ("reference : {0}" -f (Get-ServerLine $Reference))
Write-Host ("            {0}, {1} samples" -f $Reference, $refLines.Count)
Write-Host ("candidate : {0}" -f (Get-ServerLine $Candidate))
Write-Host ("            {0}, {1} samples" -f $Candidate, $canLines.Count)
Write-Host ("tolerance : {0} on the exact fields" -f $Tolerance)
Write-Host ''

$stats     = @{}
$rooms     = [ordered] @{}
$exactBad  = 0
$reported  = 0
$count     = [math]::Min($refLines.Count, $canLines.Count)

for ($i = 0; $i -lt $count; $i++)
{
	$a = ConvertTo-Fields $refLines[$i]
	$b = ConvertTo-Fields $canLines[$i]

	# Group differences by room/effect.
	$room = if ($a.ContainsKey('room')) { $a['room'] } else { '(none)' }

	if (-not $rooms.Contains($room))
	{
		$rooms[$room] = [pscustomobject] @{
			Room	 = $room
			Samples	 = 0
			AudDiffer = 0
			AudMax	 = 0.0
			AudSum	 = 0.0
			ExactBad = 0
		}
	}

	$rooms[$room].Samples++

	$lineBad = @()

	foreach ($key in $a.Keys)
	{
		if ($KeyFields -contains $key)
		{
			# The two logs must be describing the same sample.
			if ($a[$key] -ne $b[$key])
			{
				$lineBad += "$key $($a[$key]) -> $($b[$key]) (the logs are not aligned)"
				$exactBad++
			}

			continue
		}

		if (-not $b.ContainsKey($key))
		{
			$lineBad += "$key missing from the candidate"
			$exactBad++
			continue
		}

		$delta = Get-Delta $a[$key] $b[$key]

		if (-not $stats.ContainsKey($key))
		{
			$stats[$key] = [pscustomobject] @{
				Field	= $key
				Class	= if ($ExactFields -contains $key)  { 'exact' }
					  elseif ($SpreadFields -contains $key) { 'spread' }
					  elseif ($TimingFields -contains $key) { 'timing' }
					  else { 'other' }
				Differ	= 0
				Total	= 0
				MaxDiff	= 0.0
				SumDiff	= 0.0
			}
		}

		$s = $stats[$key]
		$s.Total++

		if ($delta -ne 0.0)
		{
			$s.Differ++

			if ([double]::IsInfinity($delta))
			{
				$s.MaxDiff = [double]::PositiveInfinity
			}
			else
			{
				$s.SumDiff += $delta
				if ($delta -gt $s.MaxDiff) { $s.MaxDiff = $delta }
			}
		}

		if ($s.Class -eq 'exact' -and $delta -gt $Tolerance)
		{
			$lineBad += "$key $($a[$key]) -> $($b[$key])"
			$exactBad++
			$rooms[$room].ExactBad++
		}

		if ($key -eq 'aud' -and $delta -ne 0.0 -and
		    -not [double]::IsInfinity($delta))
		{
			$rooms[$room].AudDiffer++
			$rooms[$room].AudSum += $delta

			if ($delta -gt $rooms[$room].AudMax)
			{
				$rooms[$room].AudMax = $delta
			}
		}
	}

	if ($lineBad.Count -and $reported -lt $MaxReport)
	{
		$reported++
		Write-Host ("sample {0}:" -f ($i + 1)) -ForegroundColor Yellow
		Write-Host ("  ref  " + $refLines[$i])
		Write-Host ("  cand " + $canLines[$i])
		Write-Host ("  " + ($lineBad -join '; ')) -ForegroundColor Red
	}
}

if ($refLines.Count -ne $canLines.Count)
{
	Write-Host ''
	Write-Host ("sample counts differ: {0} against {1}.  One run stopped early." -f
		    $refLines.Count, $canLines.Count) -ForegroundColor Red
	$exactBad++
}

Write-Host ''
Write-Host 'field          class    differ/total     max diff     mean diff'
Write-Host '---------------------------------------------------------------'

foreach ($s in ($stats.Values | Sort-Object Class, Field))
{
	$mean = if ($s.Differ -gt 0) { $s.SumDiff / $s.Differ } else { 0.0 }

	$colour = 'Gray'

	if ($s.Class -eq 'exact')
	{
		$colour = if ($s.MaxDiff -gt $Tolerance) { 'Red' } else { 'Green' }
	}

	Write-Host ("{0,-14} {1,-8} {2,6}/{3,-8} {4,12:0.000000} {5,12:0.000000}" -f
		    $s.Field, $s.Class, $s.Differ, $s.Total, $s.MaxDiff, $mean) `
		   -ForegroundColor $colour
}

Write-Host '---------------------------------------------------------------'

# Report by room to identify the effect responsible for a difference.
# Compare audibility spread with the reference-versus-reference baseline.
if ($rooms.Count -gt 1)
{
	Write-Host ''
	Write-Host 'room           samples   exact bad   aud differ   aud max   aud mean'
	Write-Host '---------------------------------------------------------------------'

	foreach ($r in $rooms.Values)
	{
		$mean = if ($r.AudDiffer -gt 0) { $r.AudSum / $r.AudDiffer } else { 0.0 }

		Write-Host ("{0,-14} {1,7}   {2,9}   {3,10}  {4,8:0.0000}  {5,8:0.0000}" -f
			    $r.Room, $r.Samples, $r.ExactBad, $r.AudDiffer,
			    $r.AudMax, $mean) `
			   -ForegroundColor $(if ($r.ExactBad) { 'Red' } else { 'Gray' })
	}

	Write-Host '---------------------------------------------------------------------'
}

Write-Host ''

# Require at least one exact-field comparison.
$seen = @($stats.Values | Where-Object { $_.Class -eq 'exact' } |
	  ForEach-Object { $_.Field })
$absent = @($ExactFields | Where-Object { $seen -notcontains $_ })

if ($count -eq 0)
{
	Write-Host 'No samples to compare.  Were both runs given --script?' -ForegroundColor Red
	exit 2
}

if ($absent.Count)
{
	Write-Host ("These fields were not in the logs: {0}" -f ($absent -join ', ')) -ForegroundColor Red
	Write-Host 'Nothing was judged.  The logs are from a different version of' -ForegroundColor Red
	Write-Host 'SceneHangar than this script expects.' -ForegroundColor Red
	exit 2
}

if ($exactBad -eq 0)
{
	Write-Host 'The exact fields agree: occlusion, gain, the source positions and' -ForegroundColor Green
	Write-Host 'the listener pose are the same in both runs.' -ForegroundColor Green
	Write-Host ''
	Write-Host 'Read the audibility row against a run of one DLL against itself.'
	Write-Host 'It moves between two runs of Aureal''s binary too.'
	exit 0
}

Write-Host ("{0} exact-field differences.  These are real." -f $exactBad) -ForegroundColor Red
exit 1
