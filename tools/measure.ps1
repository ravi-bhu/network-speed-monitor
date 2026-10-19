<#
.SYNOPSIS
  Samples a running process every 5 s and checks the resource budgets.
  Exits non-zero if CPU >= 0.5% of the machine, max private memory > 15 MB,
  or usage.db changes more than 2 times per 10 minutes (scaled to the run length).
#>
param(
    [int]$Minutes = 10,
    [string]$ProcessName = 'NetworkSpeedMonitor',
    [string]$DbPath = (Join-Path $env:LOCALAPPDATA 'NetworkSpeedMonitor\usage.db')
)

$ErrorActionPreference = 'Stop'
$cores = [Environment]::ProcessorCount
$p = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { Write-Error "Process '$ProcessName' is not running."; exit 2 }

function Db-Time { if (Test-Path -LiteralPath $DbPath) { (Get-Item -LiteralPath $DbPath).LastWriteTimeUtc.Ticks } else { 0 } }

$deadline = [DateTime]::UtcNow.AddMinutes($Minutes)
$sw = [Diagnostics.Stopwatch]::StartNew()
$p.Refresh()
$cpu0 = $p.TotalProcessorTime.TotalSeconds
$t0 = $sw.Elapsed.TotalSeconds
$lastDb = Db-Time
$dbWrites = 0
$maxPriv = 0
$samples = 0

while ([DateTime]::UtcNow -lt $deadline) {
    Start-Sleep -Seconds 5
    $p.Refresh()
    if ($p.HasExited) { Write-Error 'Process exited during measurement.'; exit 2 }
    $samples++
    if ($p.PrivateMemorySize64 -gt $maxPriv) { $maxPriv = $p.PrivateMemorySize64 }
    $db = Db-Time
    if ($db -ne $lastDb) { $dbWrites++; $lastDb = $db }
}

$p.Refresh()
$cpuPct = 100.0 * ($p.TotalProcessorTime.TotalSeconds - $cpu0) / ($sw.Elapsed.TotalSeconds - $t0) / $cores
$privMB = $maxPriv / 1MB
$dbLimit = [Math]::Max(2, [Math]::Ceiling(2.0 * $Minutes / 10))

"Process      : $ProcessName (pid $($p.Id)), $cores logical cores"
"Duration     : {0:N1} min, {1} samples" -f ($sw.Elapsed.TotalMinutes), $samples
"Average CPU  : {0:N3} % of the machine (limit < 0.5)" -f $cpuPct
"Max private  : {0:N2} MB (limit <= 15)" -f $privMB
"usage.db writes: $dbWrites (limit <= $dbLimit for $Minutes min)"

$fail = @()
if ($cpuPct -ge 0.5) { $fail += 'CPU' }
if ($privMB -gt 15) { $fail += 'private memory' }
if ($dbWrites -gt $dbLimit) { $fail += 'db writes' }
if ($fail) { Write-Error ("FAIL: threshold exceeded: " + ($fail -join ', ')); exit 1 }
'PASS'
exit 0
