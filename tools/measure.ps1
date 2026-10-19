<#
.SYNOPSIS
  Samples a running process every 5 s and checks the resource budgets.
  Exit codes: 0 pass, 1 threshold exceeded, 2 setup problem (process missing/ambiguous/exited).
  Thresholds: average CPU >= 0.5% of the machine, max private bytes > 15 MB,
  or more than 2 write bursts per 10 minutes (scaled to the run length, floor of 2).

  Disk writes are counted from the process itself (Win32_Process WriteOperationCount/WriteTransferCount).
  File mtimes and sizes of usage.db, usage.db-wal and usage.db-shm cannot be used: NTFS does not refresh
  the directory-entry mtime or size of a file while it is held open, and the database is in WAL mode, so
  flushes land in the -wal file whose directory entry stays stale until the handles close.
  A "write burst" is a 5 s sample in which WriteOperationCount increased. The shutdown flush is outside
  the window. The counters include every write the process makes (files, pipes, console), not only SQLite.
#>
param(
    [int]$Minutes = 10,
    [string]$ProcessName = 'NetworkSpeedMonitor'
)

$ErrorActionPreference = 'Stop'
$cores = [Environment]::ProcessorCount

function Fail([int]$Code, [string]$Msg) { [Console]::Error.WriteLine($Msg); exit $Code }

$procs = @(Get-Process -Name $ProcessName -ErrorAction SilentlyContinue)
if ($procs.Count -eq 0) { Fail 2 "Process '$ProcessName' is not running." }
if ($procs.Count -gt 1) { Fail 2 "$($procs.Count) instances of '$ProcessName' are running (pids $($procs.Id -join ', ')); expected exactly one." }
$p = $procs[0]
$procId = $p.Id

function Get-Io { Get-CimInstance Win32_Process -Filter "ProcessId=$procId" | Select-Object WriteOperationCount, WriteTransferCount }

$io = Get-Io
if (-not $io) { Fail 2 'Process exited before measurement started.' }
$lastOps = [uint64]$io.WriteOperationCount
$startOps = $lastOps
$startBytes = [uint64]$io.WriteTransferCount
$lastBytes = $startBytes

$deadline = [DateTime]::UtcNow.AddMinutes($Minutes)
$sw = [Diagnostics.Stopwatch]::StartNew()
$p.Refresh()
$cpu0 = $p.TotalProcessorTime.TotalSeconds
$t0 = $sw.Elapsed.TotalSeconds
$bursts = 0
$maxPriv = 0
$samples = 0

while ([DateTime]::UtcNow -lt $deadline) {
    Start-Sleep -Seconds 5
    $p.Refresh()
    if ($p.HasExited) { Fail 2 'Process exited during measurement.' }
    $samples++
    if ($p.PrivateMemorySize64 -gt $maxPriv) { $maxPriv = $p.PrivateMemorySize64 }
    $io = Get-Io
    if (-not $io) { Fail 2 'Process exited during measurement.' }
    if ([uint64]$io.WriteOperationCount -gt $lastOps) { $bursts++ }
    $lastOps = [uint64]$io.WriteOperationCount
    $lastBytes = [uint64]$io.WriteTransferCount
}

$p.Refresh()
$elapsed = $sw.Elapsed.TotalSeconds - $t0
$cpuPct = 100.0 * ($p.TotalProcessorTime.TotalSeconds - $cpu0) / $elapsed / $cores
$privMB = $maxPriv / 1MB
$burstLimit = [Math]::Max(2, [Math]::Ceiling(2.0 * $Minutes / 10))
$totalOps = $lastOps - $startOps
$totalBytes = $lastBytes - $startBytes

"Process      : $ProcessName (pid $procId), $cores logical cores"
"Path         : $($p.Path)"
"Duration     : {0:N1} s ({1:N2} min), {2} samples at 5 s" -f $elapsed, ($elapsed / 60), $samples
"Average CPU  : {0:N3} % of the machine (limit < 0.5)" -f $cpuPct
"Max private  : {0:N2} MB (limit <= 15)" -f $privMB
"Write bursts : $bursts (limit <= $burstLimit for $Minutes min)"
"Write ops    : $totalOps, bytes written: $totalBytes"

$fail = @()
if ($cpuPct -ge 0.5) { $fail += 'CPU' }
if ($privMB -gt 15) { $fail += 'private memory' }
if ($bursts -gt $burstLimit) { $fail += 'write bursts' }
if ($fail) { Fail 1 ("FAIL: threshold exceeded: " + ($fail -join ', ')) }
'PASS'
exit 0
