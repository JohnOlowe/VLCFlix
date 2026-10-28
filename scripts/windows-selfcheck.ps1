# VLCFlix Studio — self-check for Windows
#
# Runs the application on this machine and puts everything a bug report needs into
# one zip. Nothing is installed and nothing outside its own folder is touched except
# the app's data directory (%LOCALAPPDATA%\VLCFlixStudio).
#
#   powershell -ExecutionPolicy Bypass -File windows-selfcheck.ps1
#   powershell -ExecutionPolicy Bypass -File windows-selfcheck.ps1 -Library "D:\Videos"
#
# It answers three questions in order, and stops pretending as soon as one fails:
#   1. does the executable start at all?
#   2. can it read a real library (probe, artwork, catalog)?
#   3. can it draw its screens and play a film?
#
# The result is `windows-selfcheck-evidence.zip` beside this script: screenshots,
# the scan output, the JSON status, the test log and the app's own log file.

param(
  [string]$Library = "",
  [string]$Out     = "",
  [int]$PlaySeconds = 5
)

$ErrorActionPreference = "Continue"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe  = Join-Path $here "VLCFlixStudio.exe"
if (-not $Out) { $Out = Join-Path $here "evidence" }
$data = Join-Path $env:LOCALAPPDATA "VLCFlixStudio"

$report = New-Object System.Collections.Generic.List[string]
function Say($line) { $report.Add($line); Write-Host $line }
function Head($line) { Say ""; Say "== $line" }
function Run($label, $argline, $timeoutSec = 300) {
  Head $label
  Say ("  > VLCFlixStudio.exe " + $argline)
  $p = Start-Process -FilePath $exe -ArgumentList $argline -NoNewWindow -PassThru `
        -RedirectStandardOutput (Join-Path $env:TEMP "vlcflix-out.txt") `
        -RedirectStandardError  (Join-Path $env:TEMP "vlcflix-err.txt")
  if (-not $p.WaitForExit($timeoutSec * 1000)) {
    Say "  TIMEOUT after $timeoutSec s - killing"
    try { $p.Kill() } catch {}
    return 124
  }
  $out = Get-Content (Join-Path $env:TEMP "vlcflix-out.txt") -ErrorAction SilentlyContinue
  $err = Get-Content (Join-Path $env:TEMP "vlcflix-err.txt") -ErrorAction SilentlyContinue
  foreach ($line in @($out) + @($err)) { if ($line) { Say "  | $line" } }
  Say ("  exit code: " + $p.ExitCode)
  return $p.ExitCode
}

New-Item -ItemType Directory -Force -Path $Out | Out-Null

Head "this machine"
Say ("  Windows: " + [System.Environment]::OSVersion.VersionString)
Say ("  arch:    " + $env:PROCESSOR_ARCHITECTURE)
Say ("  exe:     " + $exe)
Say ("  data:    " + $data)
if (-not (Test-Path $exe)) {
  Say "  FATAL: VLCFlixStudio.exe is not next to this script."
  exit 2
}

# 1. does it start? A GUI-subsystem exe run from here attaches to this console, so
#    `version` should print. If this step produces nothing, nothing else matters.
$rc = Run "1. the executable starts" "version" 60
if ($rc -ne 0) { Say ""; Say "STOPPING: the exe did not answer 'version'." }
else {
  Run "1b. what it found on this machine" "doctor" 120 | Out-Null

  # 2. a real library. Without one, use the app's own folder - the scan will find
  #    nothing, which is still a result worth recording.
  $folder = $Library
  if (-not $folder) { $folder = $here }
  Run "2. scan a folder" "scan --folders `"$folder`" --data `"$data`"" 900 | Out-Null
  Run "2b. what the catalog holds" "status --data `"$data`"" 120 | Out-Null
  Run "2c. the same, for machines" "status --json --data `"$data`"" 120 | Out-Null

  # 3. drawing and playing.
  $shots = Join-Path $Out "shots"
  New-Item -ItemType Directory -Force -Path $shots | Out-Null
  Run "3. render every screen" "render --all `"$shots`" --size 1280x720 --data `"$data`"" 600 | Out-Null
  Run "3b. play the first film for ${PlaySeconds}s" "play 0 --wait $PlaySeconds --data `"$data`"" 300 | Out-Null
}

# Evidence: the app's own log is the most valuable file here, because it exists even
# when the app dies before drawing anything.
Head "collecting evidence"
$logDir = Join-Path $data "logs"
if (Test-Path $logDir) {
  Copy-Item (Join-Path $logDir "*.log") $Out -ErrorAction SilentlyContinue
  Say "  copied the app logs from $logDir"
}
$crashDir = Join-Path $data "crash"
if (Test-Path $crashDir) { Copy-Item $crashDir $Out -Recurse -ErrorAction SilentlyContinue }
$report | Set-Content (Join-Path $Out "report.txt")
if (Test-Path (Join-Path $data "catalog.json")) {
  Copy-Item (Join-Path $data "catalog.json") $Out -ErrorAction SilentlyContinue
}
if (Test-Path (Join-Path $data "settings.json")) {
  Copy-Item (Join-Path $data "settings.json") $Out -ErrorAction SilentlyContinue
}

$zip = Join-Path $here "windows-selfcheck-evidence.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $Out "*") -DestinationPath $zip -Force
Say ""
Say "evidence: $zip"
Say "Send that file back - it has the screenshots, the logs and this report."
Say ""
Say "DONE"
