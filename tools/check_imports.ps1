<#
.SYNOPSIS
  Fails if an executable imports any networking DLL (the app must make zero network connections).
.PARAMETER Exe
  Path of the executable to check.
.PARAMETER SelfTest
  Negative self-test: runs the check against a system binary known to import ws2_32.dll
  and succeeds only if the check fails.
#>
param(
    [Parameter(Mandatory, Position = 0)][string]$Exe,
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
$forbidden = @('ws2_32.dll', 'winhttp.dll', 'wininet.dll', 'urlmon.dll')

function Find-Dumpbin {
    $cmd = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }
    $found = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -find 'VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe' | Select-Object -First 1
    if ($found -and (Test-Path $found)) { return $found }
    return $null
}

function Get-Imports([string]$Path, [string]$Dumpbin) {
    $out = & $Dumpbin /nologo /imports $Path 2>&1
    if ($LASTEXITCODE -ne 0) { throw "dumpbin failed on '$Path': $($out -join "`n")" }
    # DLL names appear as unindented "    NAME.dll" lines (4-space indent) right before their function lists.
    $out | ForEach-Object { if ($_ -match '^\s{4}(\S+\.dll)\s*$') { $Matches[1].ToLowerInvariant() } } | Sort-Object -Unique
}

if (-not (Test-Path -LiteralPath $Exe)) { Write-Error "Executable not found: $Exe"; exit 1 }
$dumpbin = Find-Dumpbin
if (-not $dumpbin) { Write-Error 'dumpbin.exe not found (not on PATH, and vswhere found no VC tools).'; exit 1 }

$imports = @(Get-Imports (Resolve-Path -LiteralPath $Exe).Path $dumpbin)
Write-Host "Imports of ${Exe}:"
$imports | ForEach-Object { Write-Host "  $_" }
$bad = @($imports | Where-Object { $forbidden -contains $_ })

if ($SelfTest) {
    if ($bad.Count -gt 0) { Write-Host "SELF-TEST OK: forbidden import detected as expected ($($bad -join ', '))."; exit 0 }
    Write-Error "SELF-TEST FAILED: $Exe was expected to import a forbidden DLL but none was detected."; exit 1
}
if ($bad.Count -gt 0) {
    Write-Error "FAIL: $Exe imports forbidden networking DLL(s): $($bad -join ', ')"
    exit 1
}
Write-Host "OK: no networking DLL imported."
exit 0
