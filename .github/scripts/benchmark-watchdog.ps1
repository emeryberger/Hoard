# Runs one benchmark under a watchdog. If it exceeds the timeout, captures
# all-thread stacks (and minidumps) of the entire process tree with cdb
# before killing it, so CI hangs produce actionable diagnostics instead of
# a 6-hour timeout. See https://github.com/emeryberger/Hoard/issues/100.
#
# Usage: benchmark-watchdog.ps1 -Name larson -TimeoutSec 120 -OutDir wd -LogFile hoard.txt exe arg1 arg2...
param(
  [Parameter(Mandatory=$true)][string]$Name,
  [int]$TimeoutSec = 120,
  [string]$OutDir = "watchdog",
  [string]$LogFile = "",
  [Parameter(Mandatory=$true, ValueFromRemainingArguments=$true)][string[]]$Cmd
)

$ErrorActionPreference = "Continue"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$outFile = Join-Path $OutDir "$Name.out.txt"
$errFile = Join-Path $OutDir "$Name.err.txt"

$exe = $Cmd[0]
$cmdArgs = if ($Cmd.Count -gt 1) { $Cmd[1..($Cmd.Count-1)] } else { @() }

Write-Host "--- $Name ---"
if ($cmdArgs.Count -gt 0) {
  $p = Start-Process -FilePath $exe -ArgumentList $cmdArgs -NoNewWindow -PassThru `
       -RedirectStandardOutput $outFile -RedirectStandardError $errFile
} else {
  $p = Start-Process -FilePath $exe -NoNewWindow -PassThru `
       -RedirectStandardOutput $outFile -RedirectStandardError $errFile
}

$finished = $p.WaitForExit($TimeoutSec * 1000)

# Mirror benchmark output into the console and the aggregate log.
$output = @()
if (Test-Path $outFile) { $output += Get-Content $outFile }
if (Test-Path $errFile) { $output += Get-Content $errFile }
$output | Write-Host
if ($LogFile) {
  "--- $Name ---" | Out-File -FilePath $LogFile -Append
  $output | Out-File -FilePath $LogFile -Append
}

if ($finished) {
  Write-Host "$Name exited with $($p.ExitCode)"
  exit $p.ExitCode
}

Write-Host "### $Name HUNG after ${TimeoutSec}s - capturing process-tree stacks ###"
if ($LogFile) { "### $Name HUNG after ${TimeoutSec}s ###" | Out-File -FilePath $LogFile -Append }

# Collect the full process tree rooted at the watchdog child (withdll.exe
# spawns the actual benchmark as a grandchild, which is where the hang is).
$all = Get-CimInstance Win32_Process
$tree = @($p.Id)
do {
  $added = $false
  foreach ($proc in $all) {
    if (($tree -contains [int]$proc.ParentProcessId) -and -not ($tree -contains [int]$proc.ProcessId)) {
      $tree += [int]$proc.ProcessId
      $added = $true
    }
  }
} while ($added)
Write-Host "process tree: $($tree -join ', ')"

$cdb = @(
  "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe",
  "C:\Program Files\Windows Kits\10\Debuggers\x64\cdb.exe",
  "C:\Program Files (x86)\Windows Kits\10\Debuggers\arm64\cdb.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1

# Microsoft symbol server: makes ntdll/kernelbase wait chains readable.
$env:_NT_SYMBOL_PATH = "srv*C:\symbols*https://msdl.microsoft.com/download/symbols"

foreach ($treePid in $tree) {
  $proc = Get-Process -Id $treePid -ErrorAction SilentlyContinue
  if (-not $proc) { continue }
  $stackFile = Join-Path $OutDir "$Name-pid$treePid-$($proc.ProcessName)-stacks.txt"
  $dumpFile = Join-Path $OutDir "$Name-pid$treePid-$($proc.ProcessName).dmp"
  Write-Host "--- stacks: PID $treePid ($($proc.ProcessName)) ---"
  if ($cdb) {
    # Noninvasive attach: dump every thread's stack and a full minidump.
    & $cdb -pv -p $treePid -c ".lines; ~*kb 64; .dump /ma `"$dumpFile`"; qd" 2>&1 |
      Tee-Object -FilePath $stackFile | Write-Host
  } else {
    Write-Host "cdb.exe not found on this runner; capturing thread list only"
    $proc.Threads | Format-Table Id, ThreadState, WaitReason | Out-String |
      Tee-Object -FilePath $stackFile | Write-Host
  }
}

# Kill the tree, leaf-first.
[array]::Reverse($tree)
foreach ($treePid in $tree) {
  Stop-Process -Id $treePid -Force -ErrorAction SilentlyContinue
}

exit 124
